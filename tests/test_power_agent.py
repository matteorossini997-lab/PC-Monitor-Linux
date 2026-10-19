import os
import signal
import socket
import sys
import tempfile
import threading
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "linux_client"))
from power_agent import LifecycleServer, main, open_serial, run_system_action
from power_event import send_event
from power_protocol import PowerProtocol


class AgentTests(unittest.TestCase):
    def test_systemctl_uses_only_allowlisted_argv(self):
        calls = []
        def runner(argv, **kwargs):
            calls.append((argv, kwargs))
            return SimpleNamespace(returncode=0)
        run_system_action("suspend", runner)
        run_system_action("poweroff", runner)
        self.assertEqual(calls[0][0], ["/usr/bin/systemctl", "--no-block", "suspend"])
        self.assertNotIn("shell", calls[0][1])
        for action in ("reboot", "poweroff;id", "", "--help"):
            with self.assertRaises(ValueError):
                run_system_action(action, runner)
        self.assertEqual(len(calls), 2)

    def test_systemctl_failure_is_not_success(self):
        with self.assertRaises(RuntimeError):
            run_system_action("poweroff", lambda *a, **kw: SimpleNamespace(returncode=1))

    def test_systemctl_timeout_is_reported(self):
        import subprocess
        def timeout(*args, **kwargs):
            raise subprocess.TimeoutExpired("systemctl", 5)
        with self.assertRaises(RuntimeError):
            run_system_action("suspend", timeout)

    def test_serial_reset_lines_are_deasserted_before_open(self):
        class Port:
            def open(self):
                self.at_open = (self.dtr, self.rts, self.port)
        port = Port()
        module = SimpleNamespace(Serial=lambda **kwargs: port)
        self.assertIs(open_serial("/dev/example", module), port)
        self.assertEqual(port.at_open, (False, False, "/dev/example"))

    def test_runtime_socket_notifies_suspend_and_resume(self):
        sent = []
        protocol = PowerProtocol(sent.append, lambda action: None)
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "events.sock"
            with LifecycleServer(path) as server:
                failures = []
                def notify():
                    try:
                        send_event("SUSPENDING", str(path))
                    except (OSError, RuntimeError) as error:
                        failures.append(error)
                thread = threading.Thread(target=notify)
                thread.start()
                self.assertTrue(server.poll(protocol, timeout=2))
                thread.join(3)
                self.assertFalse(thread.is_alive())
                self.assertEqual(failures, [])
                self.assertEqual(sent, ["PWR_OS:SUSPENDING\n"])
            self.assertFalse(path.exists())

    def test_final_shutdown_confirmation_is_rejected_by_runtime_socket(self):
        with self.assertRaises(ValueError):
            send_event("OFF_CONFIRMED", "/does/not/exist")

    def test_existing_regular_file_is_not_deleted_for_socket(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "events.sock"
            path.write_text("user data")
            with self.assertRaises(RuntimeError):
                LifecycleServer(path).__enter__()
            self.assertEqual(path.read_text(), "user data")

    def test_shared_runtime_directory_is_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            os.chmod(d, 0o777)
            with self.assertRaises(RuntimeError):
                LifecycleServer(Path(d) / "events.sock").__enter__()

    def test_owned_stale_socket_is_replaced_but_live_socket_is_not(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "events.sock"
            with socket.socket(socket.AF_UNIX) as stale:
                stale.bind(str(path))
            with LifecycleServer(path), self.assertRaises(RuntimeError):
                LifecycleServer(path).__enter__()
            self.assertFalse(path.exists())

    def test_cleanup_does_not_delete_replacement_file(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "events.sock"
            with LifecycleServer(path):
                path.unlink()
                path.write_text("replacement")
            self.assertEqual(path.read_text(), "replacement")

    def test_socket_rejects_invalid_events_and_other_users(self):
        with tempfile.TemporaryDirectory() as d, LifecycleServer(Path(d) / "events.sock") as server:
            protocol = PowerProtocol(lambda line: None, lambda action: None)
            for payload in (b"OFF_CONFIRMED\n", b"x" * 40 + b"\n", b"\xff\n"):
                with socket.socket(socket.AF_UNIX) as client:
                    client.connect(str(server.path))
                    client.sendall(payload)
                    client.shutdown(socket.SHUT_WR)
                    self.assertFalse(server.poll(protocol, 1))
            with socket.socket(socket.AF_UNIX) as client:
                client.connect(str(server.path))
                client.sendall(b"RESUMED\n")
                with patch("power_agent.struct.unpack", return_value=(1, 987654, 1)):
                    self.assertFalse(server.poll(protocol, 1))
                self.assertEqual(client.recv(32), b"DENIED\n")
            self.assertFalse(server.poll(protocol, 0))

    def run_fake_agent(self, *, telemetry=False, dry_run=False, broken_write=False, silent=False):
        """Run the actual CLI loop with an in-memory serial device, never systemctl."""
        handlers, writes, executed = {}, [], []
        class Port:
            reads = 0
            def __enter__(self):
                return self
            def __exit__(self, *args):
                pass
            def open(self):
                pass
            def write(self, data):
                writes.append(data)
                return len(data) - 1 if broken_write else len(data)
            def read(self, count):
                self.reads += 1
                if silent:
                    return b""
                if self.reads == 1:
                    return b"SCARAB_CLIENT_OK|H:test\n"
                if self.reads == 2:
                    return b"PWR_REQUEST:42:STOP\nPWR_REQUEST:43:SLEEP\nPWR_NET:https://test/\n"
                handlers[signal.SIGTERM](signal.SIGTERM, None)
                return b""
        stats = {key: 1 for key in ("CPU", "CPUT", "GPU", "GPUT", "VRAM", "RAM", "PWR", "NET", "SPEED", "DOWN", "UP")}
        fake_monitor = SimpleNamespace(
            HardwareMonitor=lambda: SimpleNamespace(get_stats=lambda: stats),
            CFG_ACTIVE_SCREEN=1, CFG_BG_COLOR="FFFFFF", CFG_COLOR_CPU="0071C5",
            CFG_COLOR_GPU="76B900", CFG_COLOR_RAM="888888")
        clock = iter(range(0, 10000, 10))
        def sleep(seconds):
            handlers[signal.SIGTERM](signal.SIGTERM, None)
        with tempfile.TemporaryDirectory() as d:
            argv = ["--port", "/dev/NOT_A_REAL_DEVICE", "--socket", str(Path(d) / "events.sock")]
            if not telemetry:
                argv.append("--no-telemetry")
            if dry_run:
                argv.append("--dry-run")
            with patch.dict(sys.modules, {"serial": SimpleNamespace(Serial=lambda **kw: Port()),
                                          "pc_monitor": fake_monitor}), \
                 patch("power_agent.signal.signal", side_effect=lambda sig, fn: handlers.update({sig: fn})), \
                 patch("power_agent.time.monotonic", side_effect=lambda: next(clock)), \
                 patch("power_agent.time.sleep", side_effect=sleep), \
                 patch("power_agent.run_system_action", side_effect=executed.append):
                main(argv)
        return writes, executed

    def test_full_bridge_loop_routes_requests_and_keeps_existing_telemetry(self):
        writes, executed = self.run_fake_agent(telemetry=True)
        self.assertEqual(executed, ["poweroff", "suspend"])
        self.assertIn(b"PWR_ACK:42:OK\n", writes)
        self.assertIn(b"PWR_OS:RESUMED\n", writes)
        self.assertTrue(any(line.startswith(b"CPU:") for line in writes))
        self.assertTrue(any(line.startswith(b"CFG:") for line in writes))

    def test_dry_run_never_executes_power_actions(self):
        writes, executed = self.run_fake_agent(dry_run=True)
        self.assertEqual(executed, [])
        self.assertIn(b"PWR_ACK:42:ERROR\n", writes)

    def test_serial_failure_and_handshake_timeout_do_not_execute_commands(self):
        for kwargs in ({"broken_write": True}, {"silent": True}):
            _, executed = self.run_fake_agent(**kwargs)
            self.assertEqual(executed, [])


if __name__ == "__main__":
    unittest.main()
