#!/usr/bin/env python3
"""Tests for scripts/normalize_pex.py, which zeroes the times and empties the
user and machine names the Papyrus compiler writes into each .pex, so the
committed scripts are reproducible and carry no personal details."""

import importlib.util
import pathlib
import struct
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent


def _load():
    spec = importlib.util.spec_from_file_location("normalize_pex", ROOT / "scripts" / "normalize_pex.py")
    mod = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = mod
    spec.loader.exec_module(mod)
    return mod


pex = _load()


def _w(s):
    return struct.pack(">H", len(s)) + s


def _synthetic(debug=True):
    out = struct.pack(">IBBHQ", 0xFA57C0DE, 3, 2, 1, 0x6AE344CC)
    out += _w(b"Test.psc") + _w(b"steamuser") + _w(b"PC-NAME")
    out += struct.pack(">H", 2) + _w(b"Test") + _w(b"GetState")
    if debug:
        out += struct.pack(">BQ", 1, 0x6AE30000) + b"debug-and-objects"
    else:
        out += b"\x00" + b"objects"
    return out


class TestNormalize(unittest.TestCase):
    def test_zeroes_times_and_names(self):
        h = pex.read_header(pex.normalize(_synthetic()))
        self.assertEqual((h["compile_time"], h["modification_time"]), (0, 0))
        self.assertEqual((h["user"], h["machine"]), ("", ""))

    def test_keeps_everything_else(self):
        out = pex.normalize(_synthetic())
        h = pex.read_header(out)
        self.assertEqual((h["version"], h["game"], h["source"]), ((3, 2), 1, "Test.psc"))
        self.assertTrue(out.endswith(_w(b"Test") + _w(b"GetState") + struct.pack(">BQ", 1, 0) + b"debug-and-objects"))

    def test_without_debug_info(self):
        out = pex.normalize(_synthetic(debug=False))
        self.assertTrue(out.endswith(b"\x00objects"))
        self.assertIsNone(pex.read_header(out)["modification_time"])

    def test_is_idempotent(self):
        once = pex.normalize(_synthetic())
        self.assertEqual(pex.normalize(once), once)

    def test_rejects_a_non_pex(self):
        with self.assertRaises(ValueError):
            pex.normalize(b"\0" * 32)


class TestCommitted(unittest.TestCase):
    def test_committed_scripts_are_normalized(self):
        for path in sorted((ROOT / "papyrus" / "compiled").glob("*.pex")):
            with self.subTest(path.name):
                data = path.read_bytes()
                self.assertEqual(pex.normalize(data), data)


if __name__ == "__main__":
    unittest.main()
