#!/usr/bin/env python3
"""Fail if BeyondMastery.dll depends on the player's Visual C++ runtime.

The plugin must not import msvcp140.dll or vcruntime140.dll. Those are the
redistributable C++ runtime, and the player's copy can be arbitrarily old: the
first in-game run met msvcp140.dll 14.0.24215.1 (VS 2015 Update 3) in the
Proton prefix while the plugin was built against STL 14.44. STL 14.40 changed
std::mutex to a constexpr-initialised layout that older runtimes cannot lock,
so the very first std::mutex::lock() faulted and SKSE disabled the plugin.

Linking the CRT statically (/MT) puts the STL implementation inside the DLL,
built by the same toolset as the headers, so they cannot drift apart. This
check is what keeps that true: a dependency or a preset that quietly brings
back /MD shows up here as a build failure instead of as a crash on someone
else's machine.

Usage: scripts/check_runtime_deps.py <path/to/plugin.dll>
"""

from __future__ import annotations

import importlib.util
import pathlib
import struct
import sys

# The redistributable C++ runtime. The UCRT (api-ms-win-crt-*) is part of the
# OS on every supported Windows and is not version-skewed the same way.
FORBIDDEN_PREFIXES = ("msvcp140", "vcruntime140")


def _pe_file():
    spec = importlib.util.spec_from_file_location(
        "verify_sites", pathlib.Path(__file__).with_name("verify_sites.py")
    )
    mod = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = mod
    spec.loader.exec_module(mod)
    return mod.PEFile


def imported_dlls(path) -> list[str]:
    """Names of every DLL in the image's import directory, in table order."""
    pe = _pe_file()(path)
    d = pe.data
    (pe_off,) = struct.unpack_from("<I", d, 0x3C)
    opt = pe_off + 24
    (magic,) = struct.unpack_from("<H", d, opt)
    if magic != 0x20B:
        raise ValueError(f"{path}: not a PE32+ image")
    # Data directory 1 is the import table; directories start at opt + 112 in PE32+.
    import_rva, _ = struct.unpack_from("<II", d, opt + 112 + 8)
    if import_rva == 0:
        return []

    names = []
    off = pe.file_offset(import_rva)
    while True:
        # IMAGE_IMPORT_DESCRIPTOR is 20 bytes; Name is the RVA at offset 12.
        entry = struct.unpack_from("<5I", d, off)
        if not any(entry):
            break
        name_rva = entry[3]
        name = d[pe.file_offset(name_rva):].split(b"\0", 1)[0].decode("ascii")
        names.append(name)
        off += 20
    return names


def forbidden(names: list[str]) -> list[str]:
    return [n for n in names if n.lower().startswith(FORBIDDEN_PREFIXES)]


def main(argv=None) -> int:
    argv = sys.argv[1:] if argv is None else argv
    if len(argv) != 1:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2
    names = imported_dlls(argv[0])
    bad = forbidden(names)
    if bad:
        print(f"FAIL {argv[0]} depends on the player's C++ runtime: {', '.join(bad)}")
        print("     Build with the static CRT (/MT); see the docstring of this script.")
        return 1
    print(f"OK   {argv[0]}: no C++ runtime imports ({len(names)} DLLs imported)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
