import hashlib
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from urllib.parse import unquote, urlparse


SCRIPT = Path(__file__).resolve().parents[2] / "packaging" / "prepare_release.py"
SPEC = importlib.util.spec_from_file_location("prepare_release", SCRIPT)
RELEASE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RELEASE)


class PrepareReleaseTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.artifacts = self.root / "artifacts"
        self.output = self.root / "out"

    def package(self, platform, name):
        path = self.artifacts / platform / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(f"{platform}/{name}".encode())
        return path

    def prepare(self, tag="v0.1.0"):
        return RELEASE.prepare_release(self.artifacts, self.output, "moxi486/typist", tag)

    def test_packages_have_downloadable_urls_and_correct_checksums(self):
        originals = [
            self.package("windows", "Typist-win64.zip"),
            self.package("windows", "Typist-0.1.0-win64-installer.exe"),
            self.package("linux", "Typist-0.1.0-Linux.deb"),
            self.package("linux", "Typist-0.1.0-Linux.rpm"),
            self.package("linux", "Typist-0.1.0-Linux.tar.gz"),
            self.package("linux", "Typist-x86_64.AppImage"),
            self.package("macos", "Typist-0.1.0-Darwin.dmg"),
            self.package("macos", "Typist-0.1.0-Darwin.tar.gz"),
        ]
        self.package("linux", "linuxdeploy.AppImage")
        self.package("windows", "checksums.txt")
        feed = self.prepare()
        self.assertEqual(feed, json.loads((self.output / "latest.json").read_text()))
        self.assertEqual(feed["version"], "0.1.0")
        self.assertEqual({asset["name"] for asset in feed["assets"]}, {p.name for p in originals})
        for asset in feed["assets"]:
            self.assertNotIn("/", asset["name"])
            self.assertEqual(
                unquote(urlparse(asset["url"]).path),
                f"/moxi486/typist/releases/download/v0.1.0/{asset['name']}",
            )
            self.assertTrue((self.output / asset["name"]).is_file())
        checksums = (self.output / "checksums.txt").read_text().splitlines()
        self.assertEqual(len(checksums), len(originals))
        for line in checksums:
            digest, name = line.split("  ", 1)
            self.assertEqual(digest, hashlib.sha256((self.output / name).read_bytes()).hexdigest())
        for original in originals:
            self.assertEqual((self.output / original.name).read_bytes(), original.read_bytes())

    def test_url_escapes_filenames_and_prerelease_tags(self):
        self.package("windows", "Typist portable.zip")
        self.package("windows", "Typist-portable x64.zip")
        feed = self.prepare("v0.1.0-beta+build")
        self.assertEqual(len(feed["assets"]), 1)
        self.assertIn("v0.1.0-beta%2Bbuild/Typist-portable%20x64.zip", feed["assets"][0]["url"])

    def test_duplicate_package_names_fail_before_copying(self):
        self.package("linux", "Typist-portable.tar.gz")
        self.package("macos", "Typist-portable.tar.gz")
        with self.assertRaisesRegex(ValueError, "unique"):
            self.prepare()
        self.assertFalse(self.output.exists())

    def test_missing_packages_fail(self):
        with self.assertRaisesRegex(ValueError, "No Typist"):
            self.prepare()

    def test_branch_names_cannot_be_published(self):
        self.package("windows", "Typist-win64.zip")
        for tag in ("main", "v", "vfeature/test"):
            with self.subTest(tag=tag), self.assertRaisesRegex(ValueError, "version tag"):
                self.prepare(tag)

    def test_existing_output_cannot_introduce_stale_assets(self):
        self.package("windows", "Typist-win64.zip")
        self.output.mkdir()
        stale = self.output / "Typist-old.zip"
        stale.write_bytes(b"previous release")
        with self.assertRaisesRegex(ValueError, "empty"):
            self.prepare()
        self.assertEqual(stale.read_bytes(), b"previous release")


if __name__ == "__main__":
    unittest.main()
