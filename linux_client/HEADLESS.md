# Headless Linux installation

The Linux client can run as a system service without KDE, Deck Mode, or a logged-in desktop session.

## Packages

On CachyOS/Arch Linux:

```bash
sudo pacman -S --needed python python-pip nvtop
```

Install the Python dependencies in a virtual environment or system package context:

```bash
cd /opt/PC-Monitor-Linux/linux_client
python -m venv .venv
.venv/bin/pip install -r requirements.txt
```

If using the included virtual environment, change `ExecStart` in the service to:

```ini
ExecStart=/opt/PC-Monitor-Linux/linux_client/.venv/bin/python /opt/PC-Monitor-Linux/linux_client/pc_monitor.py
```

## Service installation

The supplied unit expects the repository in `/opt/PC-Monitor-Linux` and a user named `bc250ai`.
Adjust those values when needed.

```bash
sudo install -m 0644 systemd/99-scarab-monitor.rules /etc/udev/rules.d/
sudo install -m 0644 systemd/scarab-monitor.service /etc/systemd/system/
sudo usermod -aG uucp,video,render bc250ai
sudo udevadm control --reload-rules
sudo udevadm trigger
sudo systemctl daemon-reload
sudo systemctl enable --now scarab-monitor.service
```

Inspect the logs:

```bash
journalctl -u scarab-monitor.service -f
```

## Telemetry behavior

`nvtop -s` is the primary AMD GPU source for utilization, temperature, memory, and power. The client caches the result for one second to avoid spawning `nvtop` for every serial update. If nvtop is unavailable or returns invalid JSON, amdgpu sysfs is used as a fallback.

## Environment variables

The service can override these values:

- `SCARAB_PORT=/dev/ttyACM0`: force one serial device.
- `SCARAB_SAMPLE_SECONDS=0.5`: serial update interval.
- `SCARAB_RECONNECT_SECONDS=3`: retry interval after disconnect.
- `SCARAB_REQUIRE_HANDSHAKE=1`: require `WHO_ARE_YOU?` / `SCARAB_CLIENT_OK`.
- `SCARAB_SCREEN`, `SCARAB_BG`, `SCARAB_CPU_COLOR`, `SCARAB_GPU_COLOR`, `SCARAB_RAM_COLOR`: display configuration.

For firmware versions without the handshake, set `SCARAB_REQUIRE_HANDSHAKE=0`.

## Test mode

Run telemetry without opening the serial device:

```bash
python pc_monitor.py --test
```
