#!/usr/bin/env python3
"""Generates BeyondMastery.esp, the plugin that holds the MCM quest.

    scripts/make_esp.py [OUT]    (default: build/BeyondMastery.esp)

The plugin is ESL-flagged with Skyrim.esm as its only master, and holds one
quest: BeyondMasteryMCM, Start Game Enabled, with script BeyondMasteryMCM and a
reference alias PlayerAlias forced to the player with SkyUI's
SKI_PlayerLoadGameAlias, which calls the MCM's OnGameReload on every load.

The layouts follow UESP's "Skyrim Mod:Mod File Format" pages (record, group and
field headers; TES4; QUST; VMAD Field), checked byte for byte against the same
kind of quest in SkyUI_SE.esp. The output is reproducible: timestamps and
version-control fields are 0, so the same generator gives the same bytes.

parse() reads a plugin back strictly, for the tests: it accepts only what
build() writes and raises ValueError on anything else.
"""

import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

ESL_FLAG = 0x200
FORM_VERSION = 44  # Skyrim SE
HEDR_VERSION = 1.70  # as SkyUI_SE.esp; 1.71 is only needed for ESL object IDs below 0x800
MASTERS = ["Skyrim.esm"]

QUEST_EDID = "BeyondMasteryMCM"
QUEST_OBJECT_ID = 0x800  # the first ESL object ID
QUEST_SCRIPT = "BeyondMasteryMCM"
START_GAME_ENABLED = 0x01  # DNAM flags, first byte
RUN_ONCE = 0x01  # DNAM flags, second byte
ALIAS_ID = 0
ALIAS_NAME = "PlayerAlias"
PLAYER_REF = 0x00000014  # Skyrim.esm's PlayerRef
ALIAS_SCRIPT = "SKI_PlayerLoadGameAlias"

VMAD_VERSION = 5
VMAD_OBJ_FORMAT = 2
QUEST_FRAGMENTS_VERSION = 2  # "always 2", or the game drops the alias scripts


def _self_form_id(object_id):
    """A form ID defined in this file: its mod index is the number of masters."""
    return (len(MASTERS) << 24) | object_id


def _zstring(s):
    return s.encode("ascii") + b"\0"


def _wstring(s):
    data = s.encode("ascii")
    return struct.pack("<H", len(data)) + data


def _field(kind, data):
    if len(data) > 0xFFFF:
        raise ValueError(f"{kind} field too large: {len(data)} bytes")
    return kind.encode("ascii") + struct.pack("<H", len(data)) + data


def _record(kind, flags, form_id, fields):
    data = b"".join(fields)
    # timestamp, version control, form version, unknown
    return kind.encode("ascii") + struct.pack("<IIIHHHH", len(data), flags, form_id, 0, 0, FORM_VERSION, 0) + data


def _group(label, records):
    data = b"".join(records)
    # size includes the 24-byte header; type 0 is a top group; timestamp, vc, unknown
    return b"GRUP" + struct.pack("<I4siHHI", 24 + len(data), label.encode("ascii"), 0, 0, 0, 0) + data


def _script(name):
    # name, status 0 (local script), no properties
    return _wstring(name) + struct.pack("<BH", 0, 0)


def _vmad(quest_form_id):
    primary = struct.pack("<hhH", VMAD_VERSION, VMAD_OBJ_FORMAT, 1) + _script(QUEST_SCRIPT)
    # Quest fragments: version, fragment count 0, empty file name, then the aliases.
    fragments = struct.pack("<bH", QUEST_FRAGMENTS_VERSION, 0) + _wstring("")
    # objFormat 2 object: uint16 unused, uint16 alias ID, form ID of the owning quest
    alias = struct.pack("<HHI", 0, ALIAS_ID, quest_form_id)
    alias += struct.pack("<hhH", VMAD_VERSION, VMAD_OBJ_FORMAT, 1) + _script(ALIAS_SCRIPT)
    return primary + fragments + struct.pack("<H", 1) + alias


def _quest():
    form_id = _self_form_id(QUEST_OBJECT_ID)
    # flags, flags, priority, unknown, unknown int32, quest type 0 (none)
    dnam = struct.pack("<BBBBiI", START_GAME_ENABLED, RUN_ONCE, 0, 0, 0, 0)
    fields = [
        _field("EDID", _zstring(QUEST_EDID)),
        _field("VMAD", _vmad(form_id)),
        _field("DNAM", dnam),
        _field("NEXT", b""),
        _field("ANAM", struct.pack("<I", ALIAS_ID + 1)),  # next alias ID
        _field("ALST", struct.pack("<I", ALIAS_ID)),
        _field("ALID", _zstring(ALIAS_NAME)),
        _field("FNAM", struct.pack("<I", 0)),
        _field("ALFR", struct.pack("<I", PLAYER_REF)),
        _field("VTCK", struct.pack("<I", 0)),
        _field("ALED", b""),
    ]
    return _record("QUST", 0, form_id, fields)


def _header(num_records):
    fields = [_field("HEDR", struct.pack("<fII", HEDR_VERSION, num_records, QUEST_OBJECT_ID + 1))]
    for master in MASTERS:
        fields += [_field("MAST", _zstring(master)), _field("DATA", struct.pack("<Q", 0))]
    fields.append(_field("INTV", struct.pack("<I", 1)))
    return _record("TES4", ESL_FLAG, 0, fields)


def build(path=None):
    """Returns the plugin's bytes, and writes them to path if one is given."""
    body = _group("QUST", [_quest()])
    data = _header(num_records=2) + body  # the group and the quest
    if path is not None:
        pathlib.Path(path).write_bytes(data)
    return data


class _Reader:
    """Sequential little-endian reads that fail on overrun."""

    def __init__(self, data, what):
        self.data = data
        self.pos = 0
        self.what = what

    def take(self, n):
        if self.pos + n > len(self.data):
            raise ValueError(f"{self.what}: truncated at offset {self.pos}")
        out = self.data[self.pos : self.pos + n]
        self.pos += n
        return out

    def unpack(self, fmt):
        values = struct.unpack(fmt, self.take(struct.calcsize(fmt)))
        return values[0] if len(values) == 1 else values

    def wstring(self):
        return self.take(self.unpack("<H")).decode("ascii")

    def done(self):
        if self.pos != len(self.data):
            raise ValueError(f"{self.what}: {len(self.data) - self.pos} unexpected trailing bytes")


def _expect(cond, message):
    if not cond:
        raise ValueError(message)


def _read_record(r, kind):
    _expect(r.take(4) == kind.encode("ascii"), f"expected a {kind} record at offset {r.pos - 4}")
    size, flags, form_id, timestamp, vc, version, unknown = r.unpack("<IIIHHHH")
    _expect((timestamp, vc, version, unknown) == (0, 0, FORM_VERSION, 0), f"{kind}: unexpected header fields")
    fields = []
    fr = _Reader(r.take(size), kind)
    while fr.pos < len(fr.data):
        name = fr.take(4).decode("ascii")
        fields.append((name, fr.take(fr.unpack("<H"))))
    return flags, form_id, fields


def _expect_fields(fields, names, what):
    _expect([n for n, _ in fields] == names, f"{what}: fields {[n for n, _ in fields]}, expected {names}")
    return [d for _, d in fields]


def _zread(data, what):
    _expect(data.endswith(b"\0") and b"\0" not in data[:-1], f"{what}: not a zstring")
    return data[:-1].decode("ascii")


def _read_scripts(r, count):
    names = []
    for _ in range(count):
        name = r.wstring()
        status, properties = r.unpack("<BH")
        _expect((status, properties) == (0, 0), f"script {name}: unexpected status or properties")
        names.append(name)
    return names


def _parse_vmad(data, quest_form_id):
    r = _Reader(data, "VMAD")
    version, obj_format, count = r.unpack("<hhH")
    _expect((version, obj_format) == (VMAD_VERSION, VMAD_OBJ_FORMAT), "VMAD: unexpected version or object format")
    scripts = _read_scripts(r, count)
    frag_version, frag_count = r.unpack("<bH")
    _expect((frag_version, frag_count) == (QUEST_FRAGMENTS_VERSION, 0), "VMAD: unexpected fragment section")
    _expect(r.wstring() == "", "VMAD: unexpected fragment file name")
    aliases = {}
    for _ in range(r.unpack("<H")):
        unused, alias_id, owner = r.unpack("<HHI")
        _expect(unused == 0 and owner == quest_form_id, "VMAD: alias object not on this quest")
        a_version, a_format, a_count = r.unpack("<hhH")
        _expect((a_version, a_format) == (version, obj_format), "VMAD: alias version differs from the primary")
        aliases[alias_id] = _read_scripts(r, a_count)
    r.done()
    return scripts, aliases


def parse(data):
    """Reads back what build() writes. Raises ValueError on anything else."""
    r = _Reader(data, "plugin")
    flags, form_id, fields = _read_record(r, "TES4")
    _expect(form_id == 0, "TES4: nonzero form ID")
    names = ["HEDR"] + ["MAST", "DATA"] * ((len(fields) - 2) // 2) + ["INTV"]
    values = _expect_fields(fields, names, "TES4")
    _expect(len(values[0]) == 12, "TES4: HEDR size")
    _, num_records, next_object_id = struct.unpack("<fII", values[0])
    masters = [_zread(m, "MAST") for m in values[1:-1:2]]
    _expect(all(d == bytes(8) for d in values[2:-1:2]), "TES4: nonzero master DATA")

    _expect(r.take(4) == b"GRUP", "expected the QUST group")
    group_size, label, group_type, timestamp, vc, unknown = r.unpack("<I4siHHI")
    _expect((label, group_type, timestamp, vc, unknown) == (b"QUST", 0, 0, 0, 0), "GRUP: unexpected header")
    group = _Reader(r.take(group_size - 24), "QUST group")
    r.done()
    quest_flags, quest_form_id, fields = _read_record(group, "QUST")
    group.done()
    _expect(quest_flags == 0, "QUST: unexpected record flags")
    names = ["EDID", "VMAD", "DNAM", "NEXT", "ANAM", "ALST", "ALID", "FNAM", "ALFR", "VTCK", "ALED"]
    edid, vmad, dnam, nxt, anam, alst, alid, fnam, alfr, vtck, aled = _expect_fields(fields, names, "QUST")
    _expect(len(dnam) == 12 and nxt == b"" and aled == b"", "QUST: unexpected DNAM, NEXT or ALED size")
    _expect(all(len(d) == 4 for d in (anam, alst, fnam, alfr, vtck)), "QUST: unexpected alias field size")
    scripts, alias_scripts = _parse_vmad(vmad, quest_form_id)
    alias_id = struct.unpack("<I", alst)[0]
    _expect(set(alias_scripts) <= {alias_id}, "VMAD: script on an alias the quest lacks")
    _expect(struct.unpack("<I", anam)[0] > alias_id, "QUST: ANAM not past the alias ID")

    return {
        "flags": flags,
        "num_records": num_records,
        "next_object_id": next_object_id,
        "masters": masters,
        "quest": {
            "form_id": quest_form_id,
            "edid": _zread(edid, "EDID"),
            "dnam_flags": dnam[0],
            "scripts": scripts,
            "aliases": [
                {
                    "id": alias_id,
                    "name": _zread(alid, "ALID"),
                    "forced_ref": struct.unpack("<I", alfr)[0],
                    "scripts": alias_scripts.get(alias_id, []),
                }
            ],
        },
    }


def main(argv):
    out = pathlib.Path(argv[1]) if len(argv) > 1 else ROOT / "build" / "BeyondMastery.esp"
    out.parent.mkdir(parents=True, exist_ok=True)
    build(out)
    print(out)


if __name__ == "__main__":
    main(sys.argv)
