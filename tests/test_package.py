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
        self.archives = pkg.build_archives(self.dll, "1.2.3", self.out, ROOT)

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
            ],
        )

    def test_plugins_archive_is_rooted_at_the_plugins_folder(self):
        self.assertEqual(
            self.names(self.out / "BeyondMastery-1.2.3-SKSE-Plugins.zip"),
            ["BeyondMastery.dll", "BeyondMastery/LICENSE", "BeyondMastery/README.md"],
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
            pkg.build_archives(self.dir / "missing.dll", "1.2.3", self.out, ROOT)


if __name__ == "__main__":
    unittest.main()
