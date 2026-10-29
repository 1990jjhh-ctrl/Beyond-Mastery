#!/usr/bin/env python3
"""Checks src/CustomSkillsSites.h against a real CustomSkills.dll (CSF 3.2.0).

Set BM_CSF_DLL to the DLL's path; without it the binary tests skip, like the
SkyrimSE.exe tests in test_verify_sites.py. The parsing tests always run.
"""

import importlib.util
import os
import pathlib
import re
import struct
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
HEADER = ROOT / "src" / "CustomSkillsSites.h"
CSF_DLL = pathlib.Path(os.environ.get("BM_CSF_DLL", "/nonexistent/CustomSkills.dll"))


def _load_verify_sites():
    spec = importlib.util.spec_from_file_location("verify_sites", ROOT / "scripts" / "verify_sites.py")
    mod = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = mod
    spec.loader.exec_module(mod)
    return mod


vs = _load_verify_sites()

_FUNCTION_RE = re.compile(
    r"inline\s+constexpr\s+Function\s+(?P<var>\w+)\s*\{\s*"
    r'"(?P<name>[^"]+)"\s*,\s*(?P<rva>0[xX][0-9A-Fa-f]+)\s*,\s*"(?P<entry>[^"]*)"'
)
_SITE_RE = re.compile(
    r"inline\s+constexpr\s+Site\s+(?P<var>\w+)\s*\{\s*"
    r'"(?P<name>[^"]+)"\s*,\s*&(?P<function>\w+)\s*,\s*(?P<offset>0[xX][0-9A-Fa-f]+)\s*,\s*'
    r'"(?P<pattern>[^"]*)"\s*,\s*(?P<length>\d+)'
)
_CONST_RE = re.compile(r"inline\s+constexpr\s+std::uint32_t\s+(?P<var>\w+)\s*=\s*(?P<value>0[xX][0-9A-Fa-f]+)")


def parse_header():
    text = HEADER.read_text()
    functions = {m["var"]: (m["name"], int(m["rva"], 16), m["entry"]) for m in _FUNCTION_RE.finditer(text)}
    sites = {
        m["var"]: (m["name"], m["function"], int(m["offset"], 16), m["pattern"], int(m["length"]))
        for m in _SITE_RE.finditer(text)
    }
    consts = {m["var"]: int(m["value"], 16) for m in _CONST_RE.finditer(text)}
    return functions, sites, consts


FUNCTIONS, SITES, CONSTS = parse_header()


class TestHeaderParsing(unittest.TestCase):
    def test_every_function_and_site_is_parsed(self):
        self.assertEqual(set(FUNCTIONS), {"kAdvance", "kIncrement"})
        self.assertEqual(
            set(SITES),
            {"kAdvanceUseXP", "kAdvanceLoopEntry", "kAdvanceLoopRepeat", "kIncrementCheck", "kIncrementCount"},
        )
        for var, (_, function, _, pattern, length) in SITES.items():
            self.assertIn(function, FUNCTIONS, var)
            self.assertGreaterEqual(len(vs.pattern_bytes(pattern)), length, var)


@unittest.skipUnless(CSF_DLL.is_file(), "set BM_CSF_DLL to a CustomSkills.dll 3.2.0")
class TestAgainstCustomSkillsDll(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.pe = vs.PEFile(CSF_DLL)

    def assert_matches(self, rva, pattern, what):
        expected = vs.pattern_bytes(pattern)
        actual = self.pe.bytes_at(rva, len(expected))
        self.assertTrue(vs.matches(expected, actual), f"{what} at RVA {rva:#x}: {actual.hex(' ')}")

    def assert_site(self, var):
        name, function, offset, pattern, _ = SITES[var]
        self.assert_matches(FUNCTIONS[function][1] + offset, pattern, name)

    def branch_target(self, var):
        """Offset from the function entry that the site's jcc rel32 goes to."""
        _, function, offset, pattern, _ = SITES[var]
        length = len(vs.pattern_bytes(pattern))
        (rel,) = struct.unpack_from("<i", self.pe.bytes_at(FUNCTIONS[function][1] + offset + length - 4, 4))
        return offset + length + rel

    def test_function_entries(self):
        for name, rva, entry in FUNCTIONS.values():
            self.assert_matches(rva, entry, name)

    def test_advance_use_xp(self):
        self.assert_site("kAdvanceUseXP")
        # The call goes to fmaf, the one the stub calls in its place.
        _, _, offset, _, _ = SITES["kAdvanceUseXP"]
        call = FUNCTIONS["kAdvance"][1] + offset
        (rel,) = struct.unpack_from("<i", self.pe.bytes_at(call + 1, 4))
        self.assertEqual(call + 5 + rel, 0xB8D20)

    def test_advance_loop_entry(self):
        self.assert_site("kAdvanceLoopEntry")
        self.assertEqual(self.branch_target("kAdvanceLoopEntry"), CONSTS["kAdvanceLoopExit"])

    def test_advance_loop_repeat(self):
        self.assert_site("kAdvanceLoopRepeat")
        self.assertEqual(self.branch_target("kAdvanceLoopRepeat"), CONSTS["kAdvanceLoopTop"])

    def test_increment_check(self):
        self.assert_site("kIncrementCheck")
        self.assertEqual(self.branch_target("kIncrementCheck"), CONSTS["kIncrementReturn"])

    def test_increment_count(self):
        self.assert_site("kIncrementCount")


if __name__ == "__main__":
    unittest.main()
