#!/usr/bin/env python3
"""Offline verification of the hook sites in src/HookSites.h.

Resolves every Address Library ID in the site table against an Address Library
database and byte-compares the instructions the table expects against a real
SkyrimSE.exe. This is how a site earns `verified = true` without a debugger and
without launching the game (docs/hook-points.md section 9).

Address Library databases come in three shapes, all parsed here, following
lib/commonlibsse-ng/src/REL/IDDB.cpp:

  format 1, 2  sparse, delta-encoded id/offset pairs (SE and AE up to 1.6.x)
  format 5     dense u32 array indexed directly by id (1.7.99 and later)

A database for a neighbouring game version is still useful evidence: functions
move between builds, so each site reports the *drift* between the address the
database predicts and the address its bytes were actually found at. Drift is
region-local, not global -- do not assume one shift for the whole binary.

Usage:
    scripts/verify_sites.py --exe <SkyrimSE.exe> --db <versionlib.bin> [--runtime ae|se]

Exits non-zero if any site could not be matched.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import struct
import sys
from dataclasses import dataclass

# How far either side of the predicted address to look for a site's bytes when
# the database is for a different build than the exe. Wide enough for the drift
# seen between 1.7.99 and 1.7.104 (0x260), narrow enough that a generic function
# prologue still resolves uniquely.
DEFAULT_WINDOW = 0x1000


# --------------------------------------------------------------------------
# Byte patterns
# --------------------------------------------------------------------------


def pattern_bytes(pattern: str) -> list[int | None]:
    """Parse "FF 50 18 ?? 0F" into [0xFF, 0x50, 0x18, None, 0x0F].

    `??` is a wildcard. An empty pattern parses to an empty list, which means
    "no byte check at this site".
    """
    out: list[int | None] = []
    for token in pattern.split():
        if token == "??":
            out.append(None)
        elif len(token) == 2:
            try:
                out.append(int(token, 16))
            except ValueError:
                raise ValueError(f"not a hex byte: {token!r} in {pattern!r}") from None
        else:
            raise ValueError(f"not a hex byte: {token!r} in {pattern!r}")
    return out


def matches(pattern: list[int | None], data: bytes) -> bool:
    if len(data) < len(pattern):
        return False
    return all(p is None or p == b for p, b in zip(pattern, data))


def _to_regex(pattern: list[int | None]) -> bytes:
    return b"".join(
        b"." if p is None else re.escape(bytes([p])) for p in pattern
    )


# --------------------------------------------------------------------------
# Address Library databases
# --------------------------------------------------------------------------


def load_db(path) -> tuple[int, tuple[int, int, int, int], dict[int, int]]:
    """Return (format, version, {id: offset}) for an Address Library database."""
    data = pathlib.Path(path).read_bytes()
    if len(data) < 4:
        raise ValueError(f"{path}: too short to hold a format tag")
    (fmt,) = struct.unpack_from("<i", data, 0)
    if fmt == 5:
        return fmt, *_load_v5(data)
    if fmt in (1, 2):
        return fmt, *_load_v2(data)
    raise ValueError(f"{path}: unsupported address library format {fmt}")


def _load_v5(data: bytes) -> tuple[tuple[int, int, int, int], dict[int, int]]:
    version = struct.unpack_from("<4I", data, 4)
    off = 4 + 16 + 64  # format, version[4], name[64]
    _ptr_size, _data_format, count = struct.unpack_from("<3i", data, off)
    base = off + 12
    need = base + count * 4
    if len(data) < need:
        raise ValueError(f"truncated: need {need} bytes for {count} offsets, have {len(data)}")
    offsets = struct.unpack_from(f"<{count}I", data, base)
    # A dense array is indexed by id; 0 means "this id has no address here".
    return version, {i: v for i, v in enumerate(offsets) if v}


def _load_v2(data: bytes) -> tuple[tuple[int, int, int, int], dict[int, int]]:
    version = struct.unpack_from("<4i", data, 4)
    (name_len,) = struct.unpack_from("<i", data, 20)
    p = 24 + name_len
    ptr_size, count = struct.unpack_from("<2i", data, p)
    p += 8

    mapping: dict[int, int] = {}
    prev_id = prev_off = 0
    for _ in range(count):
        kind = data[p]
        p += 1
        lo, hi = kind & 0xF, kind >> 4

        if lo == 0:
            (ident,) = struct.unpack_from("<Q", data, p); p += 8
        elif lo == 1:
            ident = prev_id + 1
        elif lo == 2:
            ident = prev_id + data[p]; p += 1
        elif lo == 3:
            ident = prev_id - data[p]; p += 1
        elif lo == 4:
            ident = prev_id + struct.unpack_from("<H", data, p)[0]; p += 2
        elif lo == 5:
            ident = prev_id - struct.unpack_from("<H", data, p)[0]; p += 2
        elif lo == 6:
            (ident,) = struct.unpack_from("<H", data, p); p += 2
        else:
            (ident,) = struct.unpack_from("<I", data, p); p += 4

        # With bit 3 set the offset is expressed in pointer-size units, and is
        # delta-encoded against the previous offset in those same units.
        base = (prev_off // ptr_size) if (hi & 8) else prev_off
        shape = hi & 7
        if shape == 0:
            (offset,) = struct.unpack_from("<Q", data, p); p += 8
        elif shape == 1:
            offset = base + 1
        elif shape == 2:
            offset = base + data[p]; p += 1
        elif shape == 3:
            offset = base - data[p]; p += 1
        elif shape == 4:
            offset = base + struct.unpack_from("<H", data, p)[0]; p += 2
        elif shape == 5:
            offset = base - struct.unpack_from("<H", data, p)[0]; p += 2
        elif shape == 6:
            (offset,) = struct.unpack_from("<H", data, p); p += 2
        else:
            (offset,) = struct.unpack_from("<I", data, p); p += 4
        if hi & 8:
            offset *= ptr_size

        mapping[ident] = offset
        prev_id, prev_off = ident, offset

    return version, mapping


# --------------------------------------------------------------------------
# The site table
# --------------------------------------------------------------------------


@dataclass(frozen=True)
class Site:
    name: str
    se_id: int
    se_off: int
    ae_id: int
    ae_off: int
    pattern_se: str
    pattern_ae: str
    verified: bool

    def location(self, runtime: str) -> tuple[int, int]:
        return (self.se_id, self.se_off) if runtime == "se" else (self.ae_id, self.ae_off)

    def pattern(self, runtime: str) -> str:
        return self.pattern_se if runtime == "se" else self.pattern_ae


_SITE_RE = re.compile(
    r"inline\s+constexpr\s+Site\s+\w+\s*\{\s*"
    r'"(?P<name>[^"]+)"\s*,\s*'
    r"\{\s*(?P<se_id>\d+)\s*,\s*(?P<se_off>0[xX][0-9A-Fa-f]+|\d+)\s*\}\s*,\s*"
    r"\{\s*(?P<ae_id>\d+)\s*,\s*(?P<ae_off>0[xX][0-9A-Fa-f]+|\d+)\s*\}\s*,\s*"
    r'"(?P<pse>[^"]*)"\s*,\s*'
    r'"(?P<pae>[^"]*)"\s*,\s*'
    r"(?P<verified>true|false)",
    re.MULTILINE,
)


def parse_sites(header_path) -> list[Site]:
    """Parse every `Site` literal out of src/HookSites.h, in declaration order."""
    text = pathlib.Path(header_path).read_text()
    sites = [
        Site(
            name=m["name"],
            se_id=int(m["se_id"]),
            se_off=int(m["se_off"], 0),
            ae_id=int(m["ae_id"]),
            ae_off=int(m["ae_off"], 0),
            pattern_se=m["pse"],
            pattern_ae=m["pae"],
            verified=m["verified"] == "true",
        )
        for m in _SITE_RE.finditer(text)
    ]
    if not sites:
        raise ValueError(f"{header_path}: no Site literals found")
    return sites


# --------------------------------------------------------------------------
# The game binary
# --------------------------------------------------------------------------


@dataclass(frozen=True)
class Section:
    name: str
    vaddr: int
    vsize: int
    rawptr: int
    rawsize: int


class PEFile:
    """Just enough PE parsing to map an RVA to file bytes."""

    def __init__(self, path):
        self.path = pathlib.Path(path)
        self.data = self.path.read_bytes()
        (pe,) = struct.unpack_from("<I", self.data, 0x3C)
        if self.data[pe : pe + 4] != b"PE\0\0":
            raise ValueError(f"{path}: not a PE image")
        (nsec,) = struct.unpack_from("<H", self.data, pe + 6)
        (optsz,) = struct.unpack_from("<H", self.data, pe + 20)
        (self.image_base,) = struct.unpack_from("<Q", self.data, pe + 24 + 24)

        self.sections: list[Section] = []
        for k in range(nsec):
            o = pe + 24 + optsz + 40 * k
            name = self.data[o : o + 8].rstrip(b"\0").decode("latin1")
            vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", self.data, o + 8)
            self.sections.append(Section(name, vaddr, vsize, rawptr, rawsize))

        # SkyrimSE.exe carries two sections named .text; the code is the first.
        self.text = next(s for s in self.sections if s.name == ".text")

    def file_offset(self, rva: int) -> int | None:
        for s in self.sections:
            if s.rawsize and s.vaddr <= rva < s.vaddr + max(s.rawsize, s.vsize):
                return s.rawptr + (rva - s.vaddr)
        return None

    def bytes_at(self, rva: int, n: int) -> bytes:
        off = self.file_offset(rva)
        if off is None:
            return b""
        return self.data[off : off + n]

    def read_f32(self, rva: int) -> float | None:
        raw = self.bytes_at(rva, 4)
        return struct.unpack("<f", raw)[0] if len(raw) == 4 else None

    @property
    def text_bytes(self) -> bytes:
        return self.data[self.text.rawptr : self.text.rawptr + self.text.rawsize]


# --------------------------------------------------------------------------
# Checking
# --------------------------------------------------------------------------


@dataclass(frozen=True)
class Result:
    name: str
    ok: bool
    addr: int | None        # where the bytes were actually found
    predicted: int | None   # where the database said to look
    drift: int | None       # addr - predicted
    actual_bytes: bytes
    note: str = ""


def check_site(pe: PEFile, db: dict[int, int], site: Site, runtime: str,
               window: int = DEFAULT_WINDOW) -> Result:
    """Resolve one site and confirm its expected bytes are present.

    Looks first at exactly the address the database predicts. If the exe is a
    different build, searches +/- `window` around it and requires a single
    unambiguous match, reporting how far the site moved.
    """
    ident, site_off = site.location(runtime)
    func = db.get(ident)
    if func is None:
        return Result(site.name, False, None, None, None, b"",
                      f"id {ident} not in this address library")

    predicted = func + site_off
    pattern = pattern_bytes(site.pattern(runtime))
    if not pattern:
        return Result(site.name, True, predicted, predicted, 0, b"",
                      "no pattern: whole-function hook, nothing to byte-check")

    exact = pe.bytes_at(predicted, len(pattern))
    if matches(pattern, exact):
        return Result(site.name, True, predicted, predicted, 0, exact)

    # Different build: look nearby.
    lo = max(pe.text.vaddr, predicted - window)
    hi = predicted + window
    blob = pe.text_bytes
    start = lo - pe.text.vaddr
    end = min(len(blob), hi - pe.text.vaddr + len(pattern))
    found = [
        pe.text.vaddr + start + m.start()
        for m in re.finditer(_to_regex(pattern), blob[start:end], re.DOTALL)
    ]
    if len(found) == 1:
        addr = found[0]
        return Result(site.name, True, addr, predicted, addr - predicted,
                      pe.bytes_at(addr, len(pattern)))
    if not found:
        return Result(site.name, False, None, predicted, None, exact,
                      f"bytes not found within +/-0x{window:x}")
    return Result(site.name, False, None, predicted, None, exact,
                  f"{len(found)} candidates within +/-0x{window:x}, ambiguous")


def _hex(bs: bytes) -> str:
    return " ".join(f"{b:02X}" for b in bs)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", required=True, help="path to SkyrimSE.exe")
    ap.add_argument("--db", required=True, help="path to an Address Library database")
    ap.add_argument("--runtime", choices=("se", "ae"), default="ae",
                    help="which column of the site table to check (default: ae)")
    ap.add_argument("--header", default=str(pathlib.Path(__file__).resolve().parent.parent
                                            / "src" / "HookSites.h"))
    ap.add_argument("--window", type=lambda v: int(v, 0), default=DEFAULT_WINDOW,
                    help="search radius when the database is for another build")
    args = ap.parse_args(argv)

    pe = PEFile(args.exe)
    fmt, version, db = load_db(args.db)
    sites = parse_sites(args.header)

    print(f"exe      {args.exe}")
    print(f"database {args.db}  (format {fmt}, version {'.'.join(map(str, version))}, "
          f"{len(db)} ids)")
    print(f"runtime  {args.runtime}      sites {len(sites)}")
    print()
    print(f"{'site':<30} {'address':>12} {'drift':>8}  result")
    print("-" * 78)

    failures = 0
    for site in sites:
        r = check_site(pe, db, site, args.runtime, args.window)
        addr = f"0x{r.addr:x}" if r.addr is not None else "-"
        drift = f"{r.drift:+#x}" if r.drift else ("0" if r.drift == 0 else "-")
        status = "MATCH" if r.ok else "FAIL"
        print(f"{site.name:<30} {addr:>12} {drift:>8}  {status}"
              + (f"  {r.note}" if r.note else ""))
        if not r.ok:
            failures += 1
            if r.actual_bytes:
                print(f"{'':<30} {'':>12} {'':>8}  found: {_hex(r.actual_bytes)}")

    print()
    if failures:
        print(f"{failures} of {len(sites)} site(s) did not match.")
    else:
        print(f"all {len(sites)} sites match.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
