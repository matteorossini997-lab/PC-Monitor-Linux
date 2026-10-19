import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "linux_client"))

from power_protocol import LineDecoder, PowerProtocol
from power_event import main as event_main
from unittest.mock import patch


class ProtocolTests(unittest.TestCase):
    def setUp(self):
        self.sent = []
        self.actions = []
        self.protocol = PowerProtocol(self.sent.append, self.actions.append)

    def test_stop_and_sleep_are_allowlisted_and_acknowledged(self):
        self.assertTrue(self.protocol.handle("PWR_REQUEST:42:STOP"))
        self.assertTrue(self.protocol.handle("PWR_REQUEST:43:SLEEP"))
        self.assertEqual(self.actions, ["poweroff", "suspend"])
        self.assertEqual(self.sent, ["PWR_ACK:42:OK\n", "PWR_ACK:43:OK\n"])

    def test_replayed_request_does_not_execute_twice(self):
        for _ in range(3):
            self.protocol.handle("PWR_REQUEST:7:STOP")
        self.assertEqual(self.actions, ["poweroff"])
        self.assertEqual(len(self.sent), 3)

    def test_reusing_id_for_different_action_is_rejected(self):
        self.protocol.handle("PWR_REQUEST:7:STOP")
        self.protocol.handle("PWR_REQUEST:7:SLEEP")
        self.assertEqual(self.actions, ["poweroff"])
        self.assertEqual(self.sent[-1], "PWR_ACK:7:ERROR\n")

    def test_command_failure_is_reported_and_not_retried(self):
        def fail(action):
            raise OSError("polkit denied")
        self.protocol = PowerProtocol(self.sent.append, fail)
        self.protocol.handle("PWR_REQUEST:99:SLEEP")
        self.protocol.handle("PWR_REQUEST:99:SLEEP")
        self.assertEqual(self.sent, ["PWR_ACK:99:ERROR\n"] * 2)

    def test_invalid_commands_cannot_become_shell_commands(self):
        for line in ("PWR_REQUEST:1:reboot", "PWR_REQUEST:1:STOP;id",
                     "PWR_REQUEST:-1:STOP", "PWR_REQUEST:4294967296:STOP",
                     "PWR_REQUEST:1:STOP:extra", "PWR_REQUEST:x:SLEEP"):
            self.assertTrue(self.protocol.handle(line))
        self.assertFalse(self.protocol.handle("SCARAB_CLIENT_OK|H:test"))
        self.assertEqual(self.actions, [])

    def test_events_are_explicit_and_final_confirmation_is_not_a_runtime_event(self):
        for event in ("RUNNING", "SUSPENDING", "RESUMED", "SHUTTING_DOWN"):
            self.protocol.event(event)
        self.assertEqual(len(self.sent), 4)
        for event in ("OFF_CONFIRMED", "UNKNOWN", "RUNNING\nPWR_CONFIRM:OFF"):
            with self.assertRaises(ValueError):
                self.protocol.event(event)

    def test_decoder_handles_fragmentation_and_crlf(self):
        decoder = LineDecoder(32)
        self.assertEqual(decoder.feed(b"PWR_REQ"), [])
        self.assertEqual(decoder.feed(b"UEST:1:STOP\r\nhello\n"),
                         ["PWR_REQUEST:1:STOP", "hello"])

    def test_decoder_discards_entire_oversized_or_non_ascii_line(self):
        decoder = LineDecoder(16)
        self.assertEqual(decoder.feed(b"x" * 20 + b"PWR_REQUEST:1:STOP\nOK\n"), ["OK"])
        self.assertEqual(decoder.feed(b"bad\xffline\nGOOD\n"), ["GOOD"])

    def test_dedupe_cache_is_bounded(self):
        for i in range(200):
            self.protocol.handle(f"PWR_REQUEST:{i}:STOP")
        self.assertLessEqual(len(self.protocol.history), 64)

    def test_lifecycle_cli_success_and_failure(self):
        with patch("sys.argv", ["power_event", "RESUMED"]), \
             patch("power_event.send_event") as send:
            event_main()
            send.assert_called_once_with("RESUMED", "/run/bc250-monitor/events.sock")
        with patch("sys.argv", ["power_event", "RESUMED"]), \
             patch("power_event.send_event", side_effect=OSError("not connected")):
            with self.assertRaises(SystemExit) as error:
                event_main()
            self.assertEqual(error.exception.code, 1)


if __name__ == "__main__":
    unittest.main()
