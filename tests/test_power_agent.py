import os
from pathlib import Path
import socket
import sys
import tempfile
import threading
from types import SimpleNamespace
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "linux_client"))
from power_agent import LifecycleServer, open_serial, run_system_action
from power_protocol import PowerProtocol
from power_event import send_event


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
                    except Exception as error:
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


if __name__ == "__main__":
    unittest.main()
