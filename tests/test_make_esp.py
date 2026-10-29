#!/usr/bin/env python3
"""Tests for scripts/make_esp.py, which generates BeyondMastery.esp: the plugin
holding the MCM quest and its player alias. The round trip goes through the
generator's own strict parser, so record sizes and the VMAD layout are checked
too; only the game proves the plugin works."""

import importlib.util
import pathlib
import struct
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent


def _load():
    spec = importlib.util.spec_from_file_location("make_esp", ROOT / "scripts" / "make_esp.py")
    mod = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = mod
    spec.loader.exec_module(mod)
    return mod


esp = _load()


class TestHeader(unittest.TestCase):
    def setUp(self):
        self.plugin = esp.parse(esp.build())

    def test_is_esl_flagged(self):
        self.assertTrue(self.plugin["flags"] & 0x200)

    def test_has_skyrim_as_only_master(self):
        self.assertEqual(self.plugin["masters"], ["Skyrim.esm"])

    def test_counts_the_group_and_the_quest(self):
        self.assertEqual(self.plugin["num_records"], 2)

    def test_next_object_id_follows_the_quest(self):
        self.assertEqual(self.plugin["next_object_id"], 0x801)


class TestQuest(unittest.TestCase):
    def setUp(self):
        self.quest = esp.parse(esp.build())["quest"]

    def test_editor_id(self):
        self.assertEqual(self.quest["edid"], "BeyondMasteryMCM")

    def test_form_id_is_the_first_esl_object_in_this_file(self):
        # Mod index 1: the file's own index, after its one master.
        self.assertEqual(self.quest["form_id"], 0x01000800)

    def test_starts_game_enabled(self):
        self.assertTrue(self.quest["dnam_flags"] & 0x01)

    def test_has_the_mcm_script(self):
        self.assertEqual(self.quest["scripts"], ["BeyondMasteryMCM"])

    def test_player_alias(self):
        self.assertEqual(
            self.quest["aliases"],
            [{"id": 0, "name": "PlayerAlias", "forced_ref": 0x14, "scripts": ["SKI_PlayerLoadGameAlias"]}],
        )


def _field(data, name):
    i = data.index(name.encode("ascii"))
    size = struct.unpack_from("<H", data, i + 4)[0]
    return data[i + 6 : i + 6 + size]


class TestGolden(unittest.TestCase):
    # Checked byte for byte against SkyUI_SE.esp's SKI_MainInstance, a quest of
    # the same shape (one quest script, alias 0 with SKI_PlayerLoadGameAlias):
    # only the script name and the quest's form ID differ.
    VMAD = (
        "0500" "0200" "0100"  # version 5, objFormat 2, one script
        "1000" + b"BeyondMasteryMCM".hex() + "00" "0000"  # name, status, no properties
        + "02" "0000" "0000"  # fragments: version 2, count 0, empty file name
        "0100"  # one alias
        "0000" "0000" "00080001"  # object: unused, alias 0, quest 0x01000800
        "0500" "0200" "0100"  # alias: version, objFormat, one script
        "1700" + b"SKI_PlayerLoadGameAlias".hex() + "00" "0000"
    )
    # Start Game Enabled, Run Once, priority 0, unknown 0, int32 0, type 0
    DNAM = "01" "01" "00" "00" "00000000" "00000000"

    def test_vmad_bytes(self):
        self.assertEqual(_field(esp.build(), "VMAD").hex(), self.VMAD)

    def test_dnam_bytes(self):
        self.assertEqual(_field(esp.build(), "DNAM").hex(), self.DNAM)

    def test_hedr_version_is_1_70(self):
        # As SkyUI_SE.esp; the quest's object ID 0x800 needs no 1.71.
        self.assertEqual(_field(esp.build(), "HEDR")[:4].hex(), "9a99d93f")


class TestReproducible(unittest.TestCase):
    def test_two_builds_are_byte_identical(self):
        self.assertEqual(esp.build(), esp.build())

    def test_writes_the_same_bytes_to_a_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / "BeyondMastery.esp"
            data = esp.build(path)
            self.assertEqual(path.read_bytes(), data)


class TestStrictParse(unittest.TestCase):
    def test_rejects_trailing_bytes(self):
        with self.assertRaises(ValueError):
            esp.parse(esp.build() + b"\0")

    def test_rejects_a_wrong_record_size(self):
        data = bytearray(esp.build())
        size = struct.unpack_from("<I", data, 4)[0]
        struct.pack_into("<I", data, 4, size + 1)
        with self.assertRaises(ValueError):
            esp.parse(bytes(data))

    def test_rejects_a_truncated_file(self):
        with self.assertRaises(ValueError):
            esp.parse(esp.build()[:-1])


if __name__ == "__main__":
    unittest.main()
