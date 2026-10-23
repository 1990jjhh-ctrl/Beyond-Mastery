#!/usr/bin/env python3
"""Tests for scripts/package.py, which turns a built BeyondMastery.dll into the
release archives: one laid out for Nexus and mod managers, one to extract
straight into Data/SKSE/Plugins."""

import importlib.util
import pathlib
import sys
import tempfile
import unittest
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent


def _load():
    spec = importlib.util.spec_from_file_location("package", ROOT / "scripts" / "package.py")
    mod = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = mod
    spec.loader.exec_module(mod)
    return mod


pkg = _load()


class TestVersion(unittest.TestCase):
    def test_reads_the_project_version_from_cmake(self):
        text = 'cmake_minimum_required(VERSION 3.25)\n\nproject(\n\tBeyondMastery\n\tVERSION 1.2.3\n\tLANGUAGES CXX\n)\n'
        self.assertEqual(pkg.project_version(text), "1.2.3")

    def test_the_repository_has_one(self):
        # CMakeLists.txt is the single source of the version.
        version = pkg.project_version((ROOT / "CMakeLists.txt").read_text())
        self.assertRegex(version, r"^\d+\.\d+\.\d+$")

    def test_a_matching_tag_passes(self):
        pkg.check_tag("v1.2.3", "1.2.3")

    def test_a_mismatched_tag_is_refused(self):
        with self.assertRaises(SystemExit):
            pkg.check_tag("v1.2.4", "1.2.3")
        with self.assertRaises(SystemExit):
            pkg.check_tag("1.2.3", "1.2.3")  # tags carry the v


class TestArchives(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = pathlib.Path(self.tmp.name)
        self.dll = self.dir / "BeyondMastery.dll"
        self.dll.write_bytes(b"MZ fake dll")
        self.out = self.dir / "dist"
        self.archives = pkg.build_archives(self.dll, "1.2.3", self.out, ROOT, b"notices")

    def tearDown(self):
        self.tmp.cleanup()

    def names(self, path):
        with zipfile.ZipFile(path) as z:
            return sorted(z.namelist())

    def test_names_carry_the_version(self):
        self.assertEqual(
            sorted(p.name for p in self.archives),
            ["BeyondMastery-1.2.3-SKSE-Plugins.zip", "BeyondMastery-1.2.3.zip"],
        )

    def test_mod_manager_archive_is_rooted_at_data(self):
        self.assertEqual(
            self.names(self.out / "BeyondMastery-1.2.3.zip"),
            [
                "SKSE/Plugins/BeyondMastery.dll",
                "SKSE/Plugins/BeyondMastery/LICENSE",
                "SKSE/Plugins/BeyondMastery/README.md",
                "SKSE/Plugins/BeyondMastery/THIRD-PARTY-NOTICES.txt",
            ],
        )

    def test_plugins_archive_is_rooted_at_the_plugins_folder(self):
        self.assertEqual(
            self.names(self.out / "BeyondMastery-1.2.3-SKSE-Plugins.zip"),
            [
                "BeyondMastery.dll",
                "BeyondMastery/LICENSE",
                "BeyondMastery/README.md",
                "BeyondMastery/THIRD-PARTY-NOTICES.txt",
            ],
        )

    def test_the_dll_is_shipped_unchanged(self):
        with zipfile.ZipFile(self.out / "BeyondMastery-1.2.3.zip") as z:
            self.assertEqual(z.read("SKSE/Plugins/BeyondMastery.dll"), b"MZ fake dll")

    def test_no_ini_is_shipped(self):
        # The plugin writes its INI on first start; shipping one would
        # overwrite the player's settings on every update.
        for path in self.archives:
            self.assertFalse(any(n.endswith(".ini") for n in self.names(path)))

    def test_the_license_is_the_gpl(self):
        # GPL-3.0 requires the license text with every distributed copy.
        with zipfile.ZipFile(self.out / "BeyondMastery-1.2.3.zip") as z:
            self.assertIn(b"GNU GENERAL PUBLIC LICENSE", z.read("SKSE/Plugins/BeyondMastery/LICENSE"))

    def test_a_missing_dll_is_refused(self):
        with self.assertRaises(SystemExit):
            pkg.build_archives(self.dir / "missing.dll", "1.2.3", self.out, ROOT, b"notices")


class TestThirdPartyNotices(unittest.TestCase):
    """MIT and BSD libraries linked into the DLL require their notices with
    every binary copy; vcpkg keeps each port's license as share/<port>/copyright."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.share = pathlib.Path(self.tmp.name) / "share"
        for port, text in (("spdlog", "MIT spdlog notice"), ("xbyak", "BSD xbyak notice")):
            (self.share / port).mkdir(parents=True)
            (self.share / port / "copyright").write_text(text)
        (self.share / "pkgconfig").mkdir()  # no copyright file: not a port

    def tearDown(self):
        self.tmp.cleanup()

    def test_every_port_with_a_license_is_included(self):
        text = pkg.third_party_notices(self.share, ROOT / "lib" / "commonlibsse-ng").decode()
        self.assertIn("spdlog", text)
        self.assertIn("MIT spdlog notice", text)
        self.assertIn("BSD xbyak notice", text)
        self.assertNotIn("pkgconfig", text)

    def test_commonlibsse_ng_notices_are_included(self):
        # The original CommonLibSSE (MIT) and the bundled hde64 disassembler.
        text = pkg.third_party_notices(self.share, ROOT / "lib" / "commonlibsse-ng").decode()
        self.assertIn("CommonLibSSE-NG", text)
        mit = (ROOT / "lib" / "commonlibsse-ng" / "licenses" / "LICENSE-MIT.txt").read_text()
        hde = (ROOT / "lib" / "commonlibsse-ng" / "licenses" / "LICENSE-hde64.txt").read_text()
        self.assertIn(mit.strip(), text)
        self.assertIn(hde.strip(), text)

    def test_ports_appear_in_a_stable_order(self):
        text = pkg.third_party_notices(self.share, ROOT / "lib" / "commonlibsse-ng").decode()
        self.assertLess(text.index("spdlog"), text.index("xbyak"))

    def test_no_ports_is_refused(self):
        # Packaging without a build would silently ship no notices.
        with self.assertRaises(SystemExit):
            pkg.third_party_notices(pathlib.Path(self.tmp.name) / "missing", ROOT / "lib" / "commonlibsse-ng")


if __name__ == "__main__":
    unittest.main()
