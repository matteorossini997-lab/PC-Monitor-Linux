import os
import pty
import subprocess
import tempfile
import tty
import unittest
from pathlib import Path


class ShutdownHookTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.binary = str(Path(cls.temp.name) / "shutdown-hook")
        source = Path(__file__).resolve().parents[1] / "linux_client/systemd/bc250_final_shutdown.c"
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-DBC250_HOOK_TEST", str(source), "-o", cls.binary], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_hook(self, action):
        master, slave = pty.openpty()
        try:
            tty.setraw(slave)
            result = subprocess.run([self.binary, action, os.ttyname(slave)], timeout=3, check=False)
            os.set_blocking(master, False)
            try:
                data = os.read(master, 4096)
            except BlockingIOError:
                data = b""
            return result.returncode, data
        finally:
            os.close(master)
            os.close(slave)

    def test_final_confirmation_only_on_poweroff_or_halt(self):
        for action in ("poweroff", "halt"):
            code, data = self.run_hook(action)
            self.assertEqual(code, 0)
            self.assertEqual(data, b"PWR_OS:OFF_CONFIRMED\n" * 3)

    def test_reboot_kexec_and_sleep_never_authorize_cut(self):
        for action in ("reboot", "kexec", "suspend", "unknown"):
            code, data = self.run_hook(action)
            self.assertEqual(code, 0)
            self.assertEqual(data, b"")


if __name__ == "__main__":
    unittest.main()
