"""BC250 Linux lifecycle bridge and existing Scarab telemetry over ONE serial port.

Only run this agent on the BC250 powered by the controller. It deliberately has
no TCP listener; remote control terminates on the authenticated ESP32 HTTPS API.
"""

import argparse
import logging
import os
from pathlib import Path
import select
import signal
import socket
import stat
import struct
import subprocess
import time

from power_protocol import LineDecoder, PowerProtocol, RUNTIME_EVENTS

LOG = logging.getLogger("bc250-agent")


def run_system_action(action, runner=None):
    if action not in {"poweroff", "suspend"}:
        raise ValueError("Not an allowed power action")
    if runner is None:
        runner = subprocess.run
    try:
        result = runner(["/usr/bin/systemctl", "--no-block", action],
                        timeout=5, capture_output=True, check=False)
    except subprocess.SubprocessError as error:
        raise RuntimeError("systemctl request failed") from error
    if result.returncode:
        raise RuntimeError("systemctl rejected power action")


def open_serial(device, serial_module):
    # Set DTR/RTS before open; CH343 auto-reset is wired to these lines.
    port = serial_module.Serial(port=None, baudrate=115200, timeout=0.05,
                                write_timeout=0.5, exclusive=True)
    port.dtr = False
    port.rts = False
    port.port = device
    port.open()
    return port


class LifecycleServer:
    """Private AF_UNIX endpoint; never deletes non-socket files or accepts peers
    other than root/the service user. Only non-final lifecycle events are allowed.
    """

    def __init__(self, path):
        self.path = Path(path)
        self.listener = None
        self.identity = None

    def __enter__(self):
        self.path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
        parent = self.path.parent.stat()
        if parent.st_uid != os.getuid() or stat.S_IMODE(parent.st_mode) & 0o077:
            raise RuntimeError("Lifecycle socket directory must be owned by the service user and mode 0700")
        if self.path.exists() or self.path.is_symlink():
            existing = self.path.lstat()
            if not stat.S_ISSOCK(existing.st_mode) or existing.st_uid != os.getuid():
                raise RuntimeError("Refusing to replace an unrelated lifecycle socket path")
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as probe:
                probe.settimeout(0.2)
                try:
                    probe.connect(str(self.path))
                except ConnectionRefusedError:
                    self.path.unlink()  # Owned, stale socket only.
                else:
                    raise RuntimeError("A lifecycle agent is already running")
        self.listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            self.listener.bind(str(self.path))
            os.chmod(self.path, 0o600)
            self.identity = self.path.stat()
            self.listener.listen(2)
        except BaseException:
            self.__exit__(None, None, None)
            raise
        return self

    def __exit__(self, *args):
        if self.listener:
            self.listener.close()
        try:
            current = self.path.lstat()
        except FileNotFoundError:
            return
        if (self.identity and stat.S_ISSOCK(current.st_mode) and
                (current.st_dev, current.st_ino) == (self.identity.st_dev, self.identity.st_ino)):
            self.path.unlink()

    def poll(self, protocol, timeout=0):
        if not select.select([self.listener], [], [], timeout)[0]:
            return False
        connection, _ = self.listener.accept()
        with connection:
            connection.settimeout(0.2)
            try:
                _, uid, _ = struct.unpack("3i", connection.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
                if uid not in {0, os.getuid()}:
                    connection.sendall(b"DENIED\n")
                    return False
                data = bytearray()
                while b"\n" not in data and len(data) <= 32:
                    chunk = connection.recv(33 - len(data))
                    if not chunk:
                        break
                    data.extend(chunk)
                event = data.decode("ascii").removesuffix("\n")
                if event not in RUNTIME_EVENTS:
                    connection.sendall(b"DENIED\n")
                    return False
                protocol.event(event)
                connection.sendall(b"OK\n")
                return True
            except (OSError, UnicodeError, ValueError, RuntimeError):
                LOG.warning("Lifecycle event could not be delivered")
                return False


def telemetry_line(stats):
    keys = ("CPU", "CPUT", "GPU", "GPUT", "VRAM", "RAM", "PWR", "NET", "SPEED", "DOWN", "UP")
    return ",".join(f"{key}:{stats[key]}" for key in keys) + "\n"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="Stable /dev/serial/by-id/... path")
    parser.add_argument("--socket", default="/run/bc250-monitor/events.sock")
    parser.add_argument("--no-telemetry", action="store_true")
    parser.add_argument("--dry-run", action="store_true", help="Reject, log and never execute OS power requests")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s")
    # Optional hardware/serial dependencies are not imported by unit tests/hooks.
    import serial
    monitor = None
    if not args.no_telemetry:
        from pc_monitor import HardwareMonitor
        monitor = HardwareMonitor()

    stopping = False
    def stop(signum, frame):
        nonlocal stopping
        stopping = True
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)

    def execute(action):
        if args.dry_run:
            LOG.warning("Dry run: refused OS action %s", action)
            raise RuntimeError("dry run")
        run_system_action(action)

    # Preserve duplicate suppression across a serial reconnect in this process.
    history = None
    with LifecycleServer(args.socket) as events:
        while not stopping:
            try:
                with open_serial(args.port, serial) as port:
                    def send(line):
                        data = line.encode("ascii")
                        if port.write(data) != len(data):
                            raise OSError("Incomplete serial write")
                    decoder = LineDecoder()
                    protocol = PowerProtocol(send, execute)
                    if history is not None:
                        protocol.history = history
                    history = protocol.history
                    connected = False
                    started = time.monotonic()
                    last_probe = last_stats = last_heartbeat = 0
                    while not stopping:
                        now = time.monotonic()
                        if not connected and now - last_probe >= 1:
                            send("WHO_ARE_YOU?\n")
                            last_probe = now
                        for line in decoder.feed(port.read(512)):
                            if line.startswith("SCARAB_CLIENT_OK"):
                                if not connected:
                                    connected = True
                                    protocol.event("RESUMED")
                                    if monitor is not None:
                                        from pc_monitor import (CFG_ACTIVE_SCREEN, CFG_BG_COLOR,
                                            CFG_COLOR_CPU, CFG_COLOR_GPU, CFG_COLOR_RAM)
                                        send(f"CFG:SCR={CFG_ACTIVE_SCREEN},BG={CFG_BG_COLOR},CCPU={CFG_COLOR_CPU},"
                                             f"CGPU={CFG_COLOR_GPU},CRAM={CFG_COLOR_RAM}\n")
                                    LOG.info("Scarab power bridge connected")
                            elif connected and not protocol.handle(line):
                                if line.startswith(("PWR_NET:", "PWR_ERROR:", "PWR_RESULT:")):
                                    LOG.info("%s", line)
                        if not connected:
                            if now - started > 15:
                                raise OSError("Scarab handshake timed out")
                            continue
                        events.poll(protocol)
                        if now - last_heartbeat >= 5:
                            protocol.event("RUNNING")
                            last_heartbeat = now
                        if monitor is not None and now - last_stats >= 1:
                            send(telemetry_line(monitor.get_stats()))
                            last_stats = now
            except (OSError, RuntimeError) as error:
                LOG.warning("Bridge disconnected: %s; power is not cut", error)
                if not stopping:
                    time.sleep(2)


if __name__ == "__main__":
    main()
