#!/usr/bin/env python3
"""Native tests for scripts/verify_sites.py.

Stdlib unittest, because this project's toolchain has no pytest. Run with:
    python3 -m unittest discover -s tests -p 'test_*.py'
"""

import importlib.util
import os
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
REL = ROOT / "lib" / "commonlibsse-ng" / "tests" / "REL"
SKYRIM_EXE = pathlib.Path(
    os.environ.get(
        "SKYRIM_EXE",
        "/work/Games/steamapps/common/Skyrim Special Edition/SkyrimSE.exe",
    )
)


def _load_module():
    spec = importlib.util.spec_from_file_location(
        "verify_sites", ROOT / "scripts" / "verify_sites.py"
    )
    mod = importlib.util.module_from_spec(spec)
    # Register before exec: dataclasses resolves annotations through
    # sys.modules[cls.__module__], which is None for an unregistered module.
    sys.modules[spec.name] = mod
    spec.loader.exec_module(mod)
    return mod


vs = _load_module()


class TestPatternParsing(unittest.TestCase):
    def test_pattern_bytes_parses_hex_and_wildcards(self):
        self.assertEqual(
            vs.pattern_bytes("FF 50 18 ?? 0F"), [0xFF, 0x50, 0x18, None, 0x0F]
        )

    def test_pattern_bytes_empty_is_no_check(self):
        self.assertEqual(vs.pattern_bytes(""), [])

    def test_pattern_bytes_rejects_malformed(self):
        for bad in ("FF 5", "FF ZZ", "F"):
            with self.assertRaises(ValueError):
                vs.pattern_bytes(bad)


class TestAddressLibraryFormats(unittest.TestCase):
    def test_load_db_format5_dense(self):
        fmt, ver, m = vs.load_db(REL / "versionlib-1-7-99-0.bin")
        self.assertEqual(fmt, 5)
        self.assertEqual(ver, (1, 7, 99, 0))
        # Confirmed against the 1.7.104 disassembly.
        self.assertEqual(m[41561], 0x78DA50)  # XP routine
        self.assertEqual(m[51449], 0x927070)  # enchanting charge cost
        self.assertEqual(m[27284], 0x447180)  # spell cost, holds the skill clamp

    def test_load_db_format2_sparse(self):
        fmt, ver, m = vs.load_db(REL / "versionlib-1-6-1179-0.bin")
        self.assertEqual(fmt, 2)
        self.assertEqual(ver, (1, 6, 1179, 0))
        self.assertEqual(m[41561], 0x77D090)

    def test_load_db_format1_se(self):
        fmt, ver, m = vs.load_db(REL / "version-1-5-97-0.bin")
        self.assertEqual(ver, (1, 5, 97, 0))
        self.assertEqual(m[40554], 0x6E61D0)
        self.assertEqual(m[37517], 0x620D60)

    def test_load_db_rejects_unknown_format(self):
        with self.assertRaises(ValueError):
            vs.load_db(REL / "version-invalid-format.bin")


class TestSiteTableParsing(unittest.TestCase):
    def setUp(self):
        self.sites = vs.parse_sites(ROOT / "src" / "HookSites.h")

    def test_parses_every_site_from_the_header(self):
        self.assertEqual(len(self.sites), 13)
        self.assertEqual(
            [s.name for s in self.sites][:2],
            ["GetSkillCap", "ImprovePlayerSkillPoints"],
        )

    def test_parses_ids_and_offsets(self):
        g = next(s for s in self.sites if s.name == "GetSkillCap")
        self.assertEqual((g.se_id, g.se_off, g.ae_id, g.ae_off), (40554, 0x45, 41561, 0x6F))

    def test_parses_patterns(self):
        g = next(s for s in self.sites if s.name == "GetSkillCap")
        self.assertTrue(g.pattern_ae.startswith("FF 50 18"))
        self.assertEqual(len(vs.pattern_bytes(g.pattern_ae)), 16)


ADDRESS_LIBRARY = pathlib.Path(
    os.environ.get(
        "ADDRESS_LIBRARY",
        "/work/Games/steamapps/common/Skyrim Special Edition/Data/SKSE/Plugins/"
        "versionlib-1-7-104-0.bin",
    )
)


@unittest.skipUnless(
    SKYRIM_EXE.is_file() and ADDRESS_LIBRARY.is_file(),
    "need both the game binary and its own address library",
)
class TestAgainstExactAddressLibrary(unittest.TestCase):
    """The strongest form of the check: the database for this exact build.

    With a matching database every site must resolve to precisely the address
    whose bytes the table expects, so drift must be zero. A non-zero drift here
    would mean an ID or an offset in HookSites.h is wrong -- unlike the
    neighbouring-database run, where drift is expected noise.
    """

    @classmethod
    def setUpClass(cls):
        cls.pe = vs.PEFile(SKYRIM_EXE)
        _, cls.version, cls.db = vs.load_db(ADDRESS_LIBRARY)
        cls.sites = vs.parse_sites(ROOT / "src" / "HookSites.h")

    def test_database_matches_the_installed_binary(self):
        # If this fails the zero-drift assertion below is meaningless, so it is
        # asserted outright rather than guarded.
        self.assertEqual(tuple(self.version), vs.pe_file_version(self.pe))

    def test_every_site_matches_at_zero_drift(self):
        problems = []
        for site in self.sites:
            r = vs.check_site(self.pe, self.db, site, "ae")
            if not r.ok or r.drift != 0:
                problems.append(f"{site.name}: ok={r.ok} drift={r.drift} {r.note}")
        self.assertEqual(problems, [])


@unittest.skipUnless(SKYRIM_EXE.is_file(), f"game binary not present at {SKYRIM_EXE}")
class TestAgainstRealBinary(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.pe = vs.PEFile(SKYRIM_EXE)

    def test_text_section_picks_the_first_of_two(self):
        # SkyrimSE.exe has two sections named .text; the code section is the first.
        self.assertEqual(self.pe.text.vaddr, 0x1000)
        self.assertGreater(self.pe.text.rawsize, 0x1000000)

    def test_every_ae_site_matches_in_the_installed_binary(self):
        # The 1.7.99 DB against a 1.7.104 exe: each site still lands on its
        # expected bytes, at a per-region drift the tool reports.
        _, _, db = vs.load_db(REL / "versionlib-1-7-99-0.bin")
        sites = vs.parse_sites(ROOT / "src" / "HookSites.h")
        results = [vs.check_site(self.pe, db, s, "ae") for s in sites]
        bad = [r.name for r in results if not r.ok]
        self.assertEqual(bad, [], f"sites that did not match: {bad}")

    def test_reports_drift_per_site(self):
        _, _, db = vs.load_db(REL / "versionlib-1-7-99-0.bin")
        sites = vs.parse_sites(ROOT / "src" / "HookSites.h")
        by_name = {s.name: vs.check_site(self.pe, db, s, "ae") for s in sites}
        # Eight sites sit in regions shifted by 0x260 between 1.7.99 and 1.7.104;
        # GetClampedActorValue did not move.
        self.assertEqual(by_name["GetSkillCap"].drift, 0x260)
        self.assertEqual(by_name["CalculateChargePointsPerUse"].drift, 0x260)
        self.assertEqual(by_name["GetClampedActorValue"].drift, 0x0)

    def test_cap_constant_is_one_hundred(self):
        # The constant the cap hook replaces (PRD FR-1..FR-4).
        self.assertEqual(self.pe.read_f32(0x1B5BF14), 100.0)


if __name__ == "__main__":
    unittest.main()
