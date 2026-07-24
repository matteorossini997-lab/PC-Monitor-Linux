#!/usr/bin/env python3
"""Scarab Monitor client for headless Linux systems and the AMD BC-250.

The client keeps nvtop as the primary AMD telemetry source, starts without a
running desktop session, and reconnects automatically after USB disconnects or
ESP32 resets.
"""

from __future__ import annotations

import glob
import json
import os
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import Any, Iterable, Optional

import psutil
import serial

BAUD = 115200
SAMPLE_SECONDS = float(os.getenv("SCARAB_SAMPLE_SECONDS", "0.5"))
RECONNECT_SECONDS = float(os.getenv("SCARAB_RECONNECT_SECONDS", "3"))
PORT_OVERRIDE = os.getenv("SCARAB_PORT", "").strip()
REQUIRE_HANDSHAKE = os.getenv("SCARAB_REQUIRE_HANDSHAKE", "1") != "0"

CFG_ACTIVE_SCREEN = int(os.getenv("SCARAB_SCREEN", "1"))
CFG_BG_COLOR = os.getenv("SCARAB_BG", "FFFFFF")
CFG_COLOR_CPU = os.getenv("SCARAB_CPU_COLOR", "0071C5")
CFG_COLOR_GPU = os.getenv("SCARAB_GPU_COLOR", "76B900")
CFG_COLOR_RAM = os.getenv("SCARAB_RAM_COLOR", "888888")


def _read_number(path: Path, divisor: float = 1.0) -> Optional[float]:
    try:
        return float(path.read_text(encoding="ascii").strip()) / divisor
    except (OSError, ValueError):
        return None


def _first_existing(paths: Iterable[Path]) -> Optional[Path]:
    return next((path for path in paths if path.exists()), None)


def _numeric(value: Any, suffixes: tuple[str, ...] = ()) -> Optional[float]:
    if value is None:
        return None
    text = str(value).strip()
    for suffix in suffixes:
        text = text.replace(suffix, "")
    try:
        return float(text.strip())
    except ValueError:
        return None


class HardwareMonitor:
    """Collect CPU, memory, network, and AMD GPU telemetry."""

    def __init__(self) -> None:
        self.net_previous = psutil.net_io_counters()
        self.time_previous = time.monotonic()
        self.amd_card = self._find_amd_card()
        self.amd_hwmon = self._find_amdgpu_hwmon(self.amd_card)
        self.last_nvtop_stats = (-1, -1.0, 0.0, 0.0, -1.0)
        self.last_nvtop_time = 0.0
        psutil.cpu_percent(interval=None)

    @staticmethod
    def _find_amd_card() -> Optional[Path]:
        for value in sorted(glob.glob("/sys/class/drm/card[0-9]*")):
            card = Path(value)
            try:
                if (card / "device/vendor").read_text(encoding="ascii").strip() == "0x1002":
                    return card
            except OSError:
                continue
        return None

    @staticmethod
    def _find_amdgpu_hwmon(card: Optional[Path]) -> Optional[Path]:
        if card is None:
            return None
        for value in sorted(glob.glob(str(card / "device/hwmon/hwmon*"))):
            path = Path(value)
            try:
                if (path / "name").read_text(encoding="ascii").strip() == "amdgpu":
                    return path
            except OSError:
                continue
        return None

    @staticmethod
    def cpu_temperature() -> float:
        try:
            temperatures = psutil.sensors_temperatures()
        except (AttributeError, OSError):
            return -1.0

        for name in ("k10temp", "zenpower", "coretemp", "cpu_thermal"):
            if name in temperatures and temperatures[name]:
                return float(temperatures[name][0].current)
        for entries in temperatures.values():
            if entries:
                return float(entries[0].current)
        return -1.0

    def _nvtop_stats(self) -> tuple[int, float, float, float, float]:
        """Read AMD telemetry from nvtop, the primary source on the BC-250."""
        now = time.monotonic()
        if now - self.last_nvtop_time < 1.0:
            return self.last_nvtop_stats

        try:
            output = subprocess.check_output(
                ["nvtop", "-s"],
                stderr=subprocess.DEVNULL,
                timeout=2,
                text=True,
            )
            data = json.loads(output.strip())
            if isinstance(data, dict):
                data = data.get("Devices", data.get("devices", data))
            if isinstance(data, dict):
                devices = [data]
            elif isinstance(data, list):
                devices = data
            else:
                devices = []

            device = next((item for item in devices if isinstance(item, dict)), None)
            if device is None:
                raise ValueError("nvtop returned no GPU device")

            load_value = next(
                (
                    device.get(key)
                    for key in ("gpu_util", "GPU rate", "gpu_utilization_percent", "gpu_utilization_rate")
                    if key in device
                ),
                None,
            )
            temp_value = next(
                (device.get(key) for key in ("temp", "temperature", "gpu_temp") if key in device),
                None,
            )
            power_value = next(
                (device.get(key) for key in ("power_draw", "power", "gpu_power") if key in device),
                None,
            )
            used_value = next(
                (device.get(key) for key in ("mem_used", "memory_used", "vram_used") if key in device),
                None,
            )
            total_value = next(
                (device.get(key) for key in ("mem_total", "memory_total", "vram_total") if key in device),
                None,
            )

            load = _numeric(load_value, ("%",))
            temperature = _numeric(temp_value, ("°C", "C"))
            power = _numeric(power_value, ("W",))
            used_bytes = _numeric(used_value)
            total_bytes = _numeric(total_value)

            stats = (
                int(load) if load is not None else -1,
                round(temperature, 1) if temperature is not None else -1.0,
                (used_bytes / 1024**3) if used_bytes is not None else 0.0,
                (total_bytes / 1024**3) if total_bytes is not None else 0.0,
                round(power, 1) if power is not None else -1.0,
            )
            self.last_nvtop_stats = stats
            self.last_nvtop_time = now
            return stats
        except (FileNotFoundError, subprocess.SubprocessError, json.JSONDecodeError, ValueError):
            return self._sysfs_gpu_stats()

    def _sysfs_gpu_stats(self) -> tuple[int, float, float, float, float]:
        """Fallback only: use amdgpu sysfs when nvtop is unavailable."""
        if self.amd_card is None:
            return -1, -1.0, 0.0, 0.0, -1.0

        device = self.amd_card / "device"
        load = _read_number(device / "gpu_busy_percent")
        temperature = None
        power = None

        if self.amd_hwmon is not None:
            temperature_path = _first_existing(
                self.amd_hwmon / name for name in ("temp1_input", "temp2_input", "temp3_input")
            )
            if temperature_path:
                temperature = _read_number(temperature_path, 1000.0)

            power_path = _first_existing(
                self.amd_hwmon / name for name in ("power1_average", "power1_input")
            )
            if power_path:
                power = _read_number(power_path, 1_000_000.0)

        used_path = _first_existing(
            device / name for name in ("mem_info_gtt_used", "mem_info_vram_used")
        )
        total_path = _first_existing(
            device / name for name in ("mem_info_gtt_total", "mem_info_vram_total")
        )
        used = _read_number(used_path, 1024**3) if used_path else 0.0
        total = _read_number(total_path, 1024**3) if total_path else 0.0

        return (
            int(load) if load is not None else -1,
            round(temperature, 1) if temperature is not None else -1.0,
            float(used or 0.0),
            float(total or 0.0),
            round(power, 1) if power is not None else -1.0,
        )

    def snapshot(self) -> dict[str, object]:
        now = time.monotonic()
        current_net = psutil.net_io_counters()
        elapsed = max(now - self.time_previous, 0.001)
        down_mbps = (current_net.bytes_recv - self.net_previous.bytes_recv) * 8 / 1024**2 / elapsed
        up_mbps = (current_net.bytes_sent - self.net_previous.bytes_sent) * 8 / 1024**2 / elapsed
        self.net_previous = current_net
        self.time_previous = now

        ram = psutil.virtual_memory()
        gpu_load, gpu_temp, gpu_used, gpu_total, power = self._nvtop_stats()

        return {
            "CPU": int(psutil.cpu_percent(interval=None)),
            "CPUT": round(self.cpu_temperature(), 1),
            "GPU": gpu_load,
            "GPUT": gpu_temp,
            "VRAM": f"{gpu_used:.1f}/{gpu_total:.1f}",
            "RAM": f"{ram.used / 1024**3:.1f}/{ram.total / 1024**3:.1f}",
            "PWR": power,
            "NET": "LAN",
            "SPEED": "AUTO",
            "DOWN": round(down_mbps, 1),
            "UP": round(up_mbps, 1),
        }


def serial_candidates() -> list[str]:
    if PORT_OVERRIDE:
        return [PORT_OVERRIDE]
    values: list[str] = []
    for pattern in ("/dev/serial/by-id/*", "/dev/ttyACM*", "/dev/ttyUSB*"):
        values.extend(sorted(glob.glob(pattern)))
    return list(dict.fromkeys(values))


def _is_scarab(connection: serial.Serial) -> bool:
    if not REQUIRE_HANDSHAKE:
        return True
    try:
        connection.reset_input_buffer()
        connection.write(b"WHO_ARE_YOU?\n")
        connection.flush()
        deadline = time.monotonic() + 2.0
        while time.monotonic() < deadline:
            line = connection.readline().decode("ascii", errors="ignore").strip()
            if line == "SCARAB_CLIENT_OK":
                return True
        return False
    except (OSError, serial.SerialException):
        return False


def connect_forever() -> serial.Serial:
    while True:
        for port in serial_candidates():
            connection: Optional[serial.Serial] = None
            try:
                connection = serial.Serial(
                    port,
                    BAUD,
                    timeout=0.2,
                    write_timeout=2,
                    exclusive=True,
                )
                time.sleep(4)
                if not _is_scarab(connection):
                    connection.close()
                    continue
                print(f"Connected to Scarab Monitor on {port}", flush=True)
                return connection
            except (OSError, serial.SerialException):
                if connection is not None:
                    connection.close()
                continue
        print("Scarab Monitor not found; retrying...", flush=True)
        time.sleep(RECONNECT_SECONDS)


def start_reader(connection: serial.Serial, stop: threading.Event) -> threading.Thread:
    def reader() -> None:
        while not stop.is_set():
            try:
                line = connection.readline()
                if line:
                    print(f"[ESP32] {line.decode('ascii', errors='ignore').strip()}", flush=True)
            except (OSError, serial.SerialException):
                return

    thread = threading.Thread(target=reader, daemon=True)
    thread.start()
    return thread


def send_configuration(connection: serial.Serial) -> None:
    text = (
        f"CFG:SCR={CFG_ACTIVE_SCREEN},BG={CFG_BG_COLOR},CCPU={CFG_COLOR_CPU},"
        f"CGPU={CFG_COLOR_GPU},CRAM={CFG_COLOR_RAM}\n"
    )
    connection.write(text.encode("ascii"))
    connection.flush()


def format_snapshot(values: dict[str, object]) -> str:
    return (
        f"CPU:{values['CPU']},CPUT:{values['CPUT']},GPU:{values['GPU']},"
        f"GPUT:{values['GPUT']},VRAM:{values['VRAM']},RAM:{values['RAM']},"
        f"PWR:{values['PWR']},NET:{values['NET']},SPEED:{values['SPEED']},"
        f"DOWN:{values['DOWN']},UP:{values['UP']}\n"
    )


def main() -> None:
    if "--test" in sys.argv:
        monitor = HardwareMonitor()
        while True:
            print(format_snapshot(monitor.snapshot()).strip(), flush=True)
            time.sleep(max(SAMPLE_SECONDS, 1.0))

    monitor = HardwareMonitor()
    while True:
        connection = connect_forever()
        stop = threading.Event()
        start_reader(connection, stop)
        try:
            send_configuration(connection)
            while True:
                connection.write(format_snapshot(monitor.snapshot()).encode("ascii"))
                connection.flush()
                time.sleep(SAMPLE_SECONDS)
        except (OSError, serial.SerialException) as error:
            print(f"Serial disconnected: {error}", flush=True)
        finally:
            stop.set()
            try:
                connection.close()
            except OSError:
                pass
            time.sleep(2)


if __name__ == "__main__":
    main()
