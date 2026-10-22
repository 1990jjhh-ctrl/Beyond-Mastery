#!/usr/bin/env python3
"""Tests for scripts/check_runtime_deps.py, the gate that keeps the plugin off
the player's Visual C++ runtime."""

import importlib.util
import os
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
SKYRIM_EXE = pathlib.Path(
    os.environ.get(
        "SKYRIM_EXE",
        "/work/Games/steamapps/common/Skyrim Special Edition/SkyrimSE.exe",
    )
)


def _load():
    spec = importlib.util.spec_from_file_location(
        "check_runtime_deps", ROOT / "scripts" / "check_runtime_deps.py"
    )
    mod = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = mod
    spec.loader.exec_module(mod)
    return mod


crd = _load()


class TestForbidden(unittest.TestCase):
    def test_flags_the_redistributable_runtime(self):
        self.assertEqual(
            crd.forbidden(["KERNEL32.dll", "MSVCP140.dll", "VCRUNTIME140.dll", "USER32.dll"]),
            ["MSVCP140.dll", "VCRUNTIME140.dll"],
        )

    def test_is_case_insensitive(self):
        # Import tables spell these either way; missing one spelling would let
        # the dynamic CRT back in unnoticed.
        self.assertEqual(crd.forbidden(["msvcp140.dll"]), ["msvcp140.dll"])
        self.assertEqual(crd.forbidden(["Vcruntime140_1.DLL"]), ["Vcruntime140_1.DLL"])

    def test_catches_the_numbered_variants(self):
        # The STL is split across several DLLs; any of them is the same problem.
        names = ["msvcp140_1.dll", "msvcp140_2.dll", "msvcp140_atomic_wait.dll"]
        self.assertEqual(crd.forbidden(names), names)

    def test_leaves_the_ucrt_and_os_alone(self):
        # The UCRT ships with the OS and is not version-skewed like the redist.
        self.assertEqual(
            crd.forbidden(["api-ms-win-crt-runtime-l1-1-0.dll", "KERNEL32.dll", "ole32.dll"]),
            [],
        )


@unittest.skipUnless(SKYRIM_EXE.is_file(), f"game binary not present at {SKYRIM_EXE}")
class TestImportParsing(unittest.TestCase):
    def test_reads_a_real_import_table(self):
        # Any real Windows executable imports KERNEL32; this proves the parser
        # walks the import directory rather than returning something plausible.
        names = [n.lower() for n in crd.imported_dlls(SKYRIM_EXE)]
        self.assertIn("kernel32.dll", names)
        self.assertTrue(all(n.endswith(".dll") for n in names))


if __name__ == "__main__":
    unittest.main()
