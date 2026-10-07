import os
import subprocess
import sys
import tempfile
import unittest

SCAN = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "secret_scan.py")
MARKER = b"CINDER-DEV-SEED-BUILD"


class SecretScan(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)

    def write(self, name, data):
        path = os.path.join(self.dir.name, name)
        with open(path, "wb") as f:
            f.write(data if isinstance(data, bytes) else data.encode())
        return path

    def scan(self, images, secrets="", env=""):
        args = [sys.executable, SCAN, "--secrets", self.write("sdkconfig.secrets", secrets),
                "--env", self.write("producer.env", env)]
        args += [self.write(f"img{i}.bin", data) for i, data in enumerate(images)]
        r = subprocess.run(args, capture_output=True, text=True)
        return r.returncode, r.stdout + r.stderr

    def test_clean_image_passes(self):
        code, out = self.scan([b"\x00app\xffbytes"], 'CONFIG_CINDER_WIFI_PASSWORD="hunter2pass"\n')
        self.assertEqual(code, 0, out)
        self.assertIn("secret scan: clean", out)

    def test_wifi_password_in_image_fails_without_printing_it(self):
        secrets = 'CONFIG_CINDER_WIFI_SSID="homenet"\nCONFIG_CINDER_WIFI_PASSWORD="hunter2pass"\n'
        code, out = self.scan([b"head hunter2pass tail"], secrets)
        self.assertEqual(code, 1, out)
        self.assertIn("CONFIG_CINDER_WIFI_SSID: absent", out)
        self.assertIn("CONFIG_CINDER_WIFI_PASSWORD: PRESENT", out)
        self.assertNotIn("hunter2pass", out)

    def test_secret_in_any_file_fails(self):
        secrets = 'CONFIG_CINDER_WIFI_PASSWORD="hunter2pass"\n'
        for images in ([b"bin hunter2pass", b"clean elf"], [b"clean bin", b"elf hunter2pass"]):
            code, out = self.scan(images, secrets)
            self.assertEqual(code, 1, out)

    def test_ember_token_from_env_fails_without_printing_it(self):
        for line in ("export EMBER_TOKEN='tok_live_ABC123'", 'EMBER_TOKEN="tok_live_ABC123"', "EMBER_TOKEN=tok_live_ABC123"):
            code, out = self.scan([b"...tok_live_ABC123..."], env="EMBER_SERVER_URL=http://h\n" + line + "\n")
            self.assertEqual(code, 1, line)
            self.assertIn("EMBER_TOKEN (producer.env): PRESENT", out)
            self.assertNotIn("tok_live_ABC123", out)

    def test_dev_seed_marker_fails(self):
        code, out = self.scan([b"x" + MARKER + b"y"])
        self.assertEqual(code, 1, out)
        self.assertIn("dev-seed marker: PRESENT", out)

    def test_escaped_value_matches_unescaped_bytes(self):
        code, out = self.scan([b'pa"ss\\word'], 'CONFIG_CINDER_WIFI_PASSWORD="pa\\"ss\\\\word"\n')
        self.assertEqual(code, 1, out)

    def test_empty_and_non_string_values_are_not_secrets(self):
        secrets = 'CONFIG_CINDER_WIFI_PASSWORD=""\nCONFIG_CINDER_DEV_SEED=y\nCONFIG_CINDER_EMBER_POLL_MS=2000\n'
        code, out = self.scan([b"y 2000 anything"], secrets, env='EMBER_TOKEN=""\n')
        self.assertEqual(code, 0, out)


if __name__ == "__main__":
    unittest.main()
