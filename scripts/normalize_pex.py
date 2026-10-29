#!/usr/bin/env python3
"""Strips the build-machine details from compiled Papyrus scripts, in place.

    scripts/normalize_pex.py FILE.pex...

The Creation Kit's compiler writes the compile time, the user name and the
machine name into every .pex, and the source's modification time into its
debug info. Zeroing the times and emptying the names makes a recompile of the
same source byte-identical and keeps personal details out of the repository.

Layout (big-endian; UESP "Skyrim Mod:Compiled Script File Format", checked
against the compiler's output): uint32 magic 0xFA57C0DE, uint8 major, uint8
minor, uint16 game id, uint64 compile time, wstring source file name, wstring
user name, wstring machine name; then the string table (uint16 count,
wstrings) and the debug info (uint8 present, and if nonzero a uint64
modification time, then the rest). A wstring is a uint16 length plus that
many bytes. Nothing after the header holds an absolute offset, so the rest is
copied unchanged.
"""

import struct
import sys

MAGIC = 0xFA57C0DE


class _Reader:
    def __init__(self, data):
        self.data = data
        self.pos = 0

    def take(self, n):
        if self.pos + n > len(self.data):
            raise ValueError(f"truncated .pex at offset {self.pos}")
        out = self.data[self.pos : self.pos + n]
        self.pos += n
        return out

    def unpack(self, fmt):
        values = struct.unpack(">" + fmt, self.take(struct.calcsize(">" + fmt)))
        return values[0] if len(values) == 1 else values

    def wstring(self):
        return self.take(self.unpack("H"))


def _wstring(data):
    return struct.pack(">H", len(data)) + data


def read_header(data):
    """The header and debug-info fields this script touches, for the tests."""
    r = _Reader(data)
    magic, major, minor, game, compile_time = r.unpack("IBBHQ")
    if magic != MAGIC:
        raise ValueError(f"not a .pex: magic {magic:#x}")
    source, user, machine = r.wstring(), r.wstring(), r.wstring()
    for _ in range(r.unpack("H")):
        r.wstring()
    table_end = r.pos
    has_debug = r.unpack("B")
    modification_time = r.unpack("Q") if has_debug else None
    return {
        "version": (major, minor),
        "game": game,
        "compile_time": compile_time,
        "source": source.decode("ascii"),
        "user": user.decode("ascii"),
        "machine": machine.decode("ascii"),
        "table_end": table_end,
        "debug": bool(has_debug),
        "modification_time": modification_time,
        "rest": r.pos,
    }


def normalize(data):
    """data with both times zeroed and the user and machine names emptied."""
    h = read_header(data)
    r = _Reader(data)
    head = r.take(8)  # magic, major, minor, game id
    r.unpack("Q")
    source = r.wstring()
    r.wstring()
    r.wstring()
    out = head + struct.pack(">Q", 0) + _wstring(source) + _wstring(b"") + _wstring(b"")
    out += data[r.pos : h["table_end"]]
    if h["debug"]:
        out += data[h["table_end"] : h["table_end"] + 1] + struct.pack(">Q", 0) + data[h["rest"] :]
    else:
        out += data[h["table_end"] :]
    return out


def main(paths):
    for path in paths:
        with open(path, "rb") as f:
            data = f.read()
        with open(path, "wb") as f:
            f.write(normalize(data))


if __name__ == "__main__":
    main(sys.argv[1:])
