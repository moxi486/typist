import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


@unittest.skipUnless(os.name == "posix", "The CI installer runs on Ubuntu")
class InstallAptTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.trace = self.root / "calls.jsonl"
        self.limits = self.root / "limits.txt"
        self.mirrors = self.root / "apt-mirrors.txt"
        self.mirrors.write_text("http://azure.archive.ubuntu.com/ubuntu/\tpriority:1\n")
        self.command("sudo", '#!/usr/bin/env bash\nexec "$@"\n')
        self.command("timeout", '#!/usr/bin/env bash\nprintf "%s\\n" "$1" >> "$TYPIST_APT_LIMITS"\nshift\nexec "$@"\n')
        self.command("apt-get", '''#!/usr/bin/env python3
import json, os, sys
with open(os.environ["TYPIST_APT_TRACE"], "a") as trace:
    trace.write(json.dumps(sys.argv[1:]) + "\\n")
step = "UPDATE" if sys.argv[-1] == "update" else "INSTALL"
sys.exit(int(os.environ.get("TYPIST_APT_FAIL_" + step, "0")))
''')
        self.env = dict(os.environ, PATH=str(self.bin) + os.pathsep + os.environ["PATH"],
                        TYPIST_APT_MIRRORS_FILE=str(self.mirrors),
                        TYPIST_APT_TRACE=str(self.trace), TYPIST_APT_LIMITS=str(self.limits))
        self.script = Path(__file__).resolve().parents[2] / ".github/scripts/install-apt.sh"

    def command(self, name, source):
        path = self.bin / name
        path.write_text(source)
        path.chmod(0o755)

    def run_installer(self, *packages):
        return subprocess.run(["bash", str(self.script), *packages], env=self.env,
                              capture_output=True, text=True, timeout=10)

    def calls(self):
        return [json.loads(line) for line in self.trace.read_text().splitlines()]

    def test_official_https_mirrors_and_bounded_downloads(self):
        result = self.run_installer("libcmark-gfm-dev", "qt6-base-dev")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.mirrors.read_text().splitlines(), [
            "https://archive.ubuntu.com/ubuntu/\tpriority:1",
            "https://security.ubuntu.com/ubuntu/\tpriority:2"])
        update, install = self.calls()
        self.assertEqual(update[-1], "update")
        self.assertEqual(install[-5:], ["install", "-y", "--", "libcmark-gfm-dev", "qt6-base-dev"])
        for call in (update, install):
            self.assertIn("Acquire::Retries=1", call)
            self.assertIn("Acquire::http::Timeout=15", call)
            self.assertIn("Acquire::https::Timeout=15", call)
        self.assertEqual(self.limits.read_text().splitlines(), ["5m", "10m"])

    def test_update_failure_stops_before_install(self):
        self.env["TYPIST_APT_FAIL_UPDATE"] = "42"
        self.assertEqual(self.run_installer("qt6-base-dev").returncode, 42)
        self.assertEqual(len(self.calls()), 1)

    def test_install_failure_reaches_ci(self):
        self.env["TYPIST_APT_FAIL_INSTALL"] = "43"
        self.assertEqual(self.run_installer("qt6-base-dev").returncode, 43)
        self.assertEqual(len(self.calls()), 2)

    def test_missing_mirror_configuration_is_left_absent(self):
        self.mirrors.unlink()
        self.assertEqual(self.run_installer("qt6-base-dev").returncode, 0)
        self.assertFalse(self.mirrors.exists())

    def test_package_arguments_are_literal(self):
        package = "literal$(printf unintended)"
        self.assertEqual(self.run_installer(package).returncode, 0)
        self.assertEqual(self.calls()[1][-1], package)


if __name__ == "__main__":
    unittest.main()
