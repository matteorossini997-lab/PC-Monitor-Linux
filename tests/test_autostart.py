"""Autostart unit contract; never starts systemd, serial or a power action."""

import configparser
import shlex
import unittest
from pathlib import Path


class AutostartTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.unit = configparser.ConfigParser(interpolation=None)
        path = Path(__file__).resolve().parents[1] / "linux_client/systemd/bc250-monitor.service"
        cls.unit.read_string(path.read_text())

    def test_restart_after_clean_exit_as_well_as_crash(self):
        self.assertEqual(self.unit["Service"]["Restart"], "always")
        self.assertEqual(self.unit["Service"]["RestartSec"], "3")

    def test_repeated_failures_do_not_exhaust_boot_retry_budget(self):
        self.assertEqual(self.unit["Unit"].get("StartLimitIntervalSec"), "0")

    def test_enable_starts_at_boot_without_login_or_network(self):
        self.assertEqual(self.unit["Install"]["WantedBy"], "multi-user.target")
        for section in ("Unit", "Service", "Install"):
            text = " ".join(self.unit[section].values())
            for dependency in ("graphical.target", "network-online.target", "display-manager.service"):
                self.assertNotIn(dependency, text)
        self.assertEqual(self.unit["Service"]["Type"], "simple")

    def test_missing_usb_does_not_skip_or_stop_service(self):
        unit = self.unit["Unit"]
        for directive in ("ConditionPathExists", "AssertPathExists", "BindsTo"):
            self.assertNotIn(directive, unit)
        self.assertNotIn(".device", " ".join(unit.values()))

    def test_one_agent_owns_serial_and_keeps_telemetry_enabled(self):
        args = shlex.split(self.unit["Service"]["ExecStart"])
        self.assertEqual(args, ["/opt/pc-monitor/.venv/bin/python",
                                "/opt/pc-monitor/linux_client/power_agent.py",
                                "--port", "${BC250_SERIAL}"])
        self.assertEqual(self.unit["Service"]["EnvironmentFile"], "/etc/bc250-power.conf")


if __name__ == "__main__":
    unittest.main()
