#!/usr/bin/env python3
"""Checks src/CustomSkillsSites.h against real CustomSkills.dll files.

Set BM_CSF_DLL to one or more DLL paths, separated by os.pathsep; each is
matched to the build whose function entries it has, and every site of that
build is checked. Without it the binary tests skip, like the SkyrimSE.exe
tests in test_verify_sites.py. The parsing tests always run.
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
CSF_DLLS = [pathlib.Path(p) for p in os.environ.get("BM_CSF_DLL", "").split(os.pathsep) if p]


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
_BUILD_RE = re.compile(r"inline\s+constexpr\s+Build\s+(?P<var>\w+)\s*\{\s*\"(?P<version>[^\"]+)\"\s*,(?P<fields>[^}]*)\}")
_BUILD_FIELDS = ("advance", "increment", "useXP", "loopEntry", "loopRepeat", "incrementCheck", "incrementCount",
                 "loopTop", "loopExit", "incrementReturn")

# Where each build's `call fmaf` goes: the code the use-XP stub calls instead.
FMAF_RVA = {"3.2.0": 0xB8D20, "3.1.0": 0x7C148}


def parse_header():
    text = HEADER.read_text()
    functions = {m["var"]: (m["name"], int(m["rva"], 16), m["entry"]) for m in _FUNCTION_RE.finditer(text)}
    sites = {
        m["var"]: (m["name"], m["function"], int(m["offset"], 16), m["pattern"], int(m["length"]))
        for m in _SITE_RE.finditer(text)
    }
    consts = {m["var"]: int(m["value"], 16) for m in _CONST_RE.finditer(text)}
    builds = {}
    for m in _BUILD_RE.finditer(text):
        fields = [f.strip().lstrip("&") for f in m["fields"].split(",") if f.strip()]
        builds[m["version"]] = dict(zip(_BUILD_FIELDS, fields, strict=True))
    return functions, sites, consts, builds


FUNCTIONS, SITES, CONSTS, BUILDS = parse_header()


class TestHeaderParsing(unittest.TestCase):
    def test_every_build_is_parsed(self):
        self.assertEqual(set(BUILDS), {"3.2.0", "3.1.0"})
        self.assertEqual(set(BUILDS), set(FMAF_RVA))

    def test_every_build_names_its_own_functions_and_sites(self):
        for version, build in BUILDS.items():
            for field in ("advance", "increment"):
                self.assertIn(build[field], FUNCTIONS, version)
            for field, function in (("useXP", "advance"), ("loopEntry", "advance"), ("loopRepeat", "advance"),
                                    ("incrementCheck", "increment"), ("incrementCount", "increment")):
                site = SITES[build[field]]
                self.assertEqual(site[1], build[function], (version, field))
                self.assertGreaterEqual(len(vs.pattern_bytes(site[3])), site[4], (version, field))
            for field in ("loopTop", "loopExit", "incrementReturn"):
                self.assertIn(build[field], CONSTS, version)


@unittest.skipUnless(CSF_DLLS, "set BM_CSF_DLL to one or more CustomSkills.dll files")
class TestAgainstCustomSkillsDll(unittest.TestCase):
    def matches(self, pe, rva, pattern):
        expected = vs.pattern_bytes(pattern)
        return vs.matches(expected, pe.bytes_at(rva, len(expected)))

    def build_of(self, pe):
        """The version whose two function entries this DLL has."""
        found = [
            version
            for version, build in BUILDS.items()
            if all(self.matches(pe, FUNCTIONS[build[f]][1], FUNCTIONS[build[f]][2]) for f in ("advance", "increment"))
        ]
        self.assertEqual(len(found), 1, f"entries match {found}")
        return found[0]

    def branch_target(self, pe, site_var):
        """Offset from the function entry that the site's jcc rel32 goes to."""
        _, function, offset, pattern, _ = SITES[site_var]
        length = len(vs.pattern_bytes(pattern))
        (rel,) = struct.unpack_from("<i", pe.bytes_at(FUNCTIONS[function][1] + offset + length - 4, 4))
        return offset + length + rel

    def check(self, path):
        pe = vs.PEFile(path)
        version = self.build_of(pe)
        build = BUILDS[version]
        for field in ("useXP", "loopEntry", "loopRepeat", "incrementCheck", "incrementCount"):
            name, function, offset, pattern, _ = SITES[build[field]]
            rva = FUNCTIONS[function][1] + offset
            self.assertTrue(self.matches(pe, rva, pattern), f"{version} {name} at RVA {rva:#x}")
        for site, target in (("loopEntry", "loopExit"), ("loopRepeat", "loopTop"), ("incrementCheck", "incrementReturn")):
            self.assertEqual(self.branch_target(pe, build[site]), CONSTS[build[target]], (version, site))
        _, function, offset, _, _ = SITES[build["useXP"]]
        call = FUNCTIONS[function][1] + offset
        (rel,) = struct.unpack_from("<i", pe.bytes_at(call + 1, 4))
        self.assertEqual(call + 5 + rel, FMAF_RVA[version], version)

    def test_every_given_dll(self):
        for path in CSF_DLLS:
            with self.subTest(dll=str(path)):
                self.check(path)


if __name__ == "__main__":
    unittest.main()
