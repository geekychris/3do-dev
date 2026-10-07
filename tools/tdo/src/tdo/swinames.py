"""SWI (system call) number -> name, parsed from the 3DO SDK headers.

The table is built once from third_party/3do-devkit/include/3do and cached
in build/swinames.json.
"""
from __future__ import annotations

import json
import re

from .core import repo_root

BASES = {"KERNELSWI": 0x10000, "GRAFSWI": 0x20000, "FILEFOLIOSWI": 0x30000,
         "AUDIOSWI": 0x40000, "MATHSWI": 0x50000}
FOLIOS = {1: "kernel", 2: "graphics", 3: "file", 4: "audio", 5: "math"}

_table: dict[int, str] | None = None


def _build() -> dict[int, str]:
    inc = repo_root() / "third_party" / "3do-devkit" / "include" / "3do"
    out: dict[int, str] = {}
    if not inc.exists():
        return out
    for h in sorted(inc.glob("*.h")):
        if h.name.startswith("cplusswi"):
            continue                      # C++ redefinitions use other bases
        s = h.read_text(errors="replace")
        for base, n, name in re.findall(r"__swi\(\s*([A-Z]+SWI)\s*\+\s*(\d+)\s*\)\s*\**\s*(\w+)", s):
            if base in BASES:
                out.setdefault(BASES[base] + int(n), name)
        for num, name in re.findall(r"__swi\(\s*(0x[0-9a-fA-F]+)\s*\)\s*\**\s*(\w+)", s):
            out.setdefault(int(num, 16), name)
        # #define _DRAWCELS (GRAFSWI+39)  ...  __swi(_DRAWCELS) Err DrawCels(...)
        macros = {m: BASES[b] + int(n) for m, b, n in
                  re.findall(r"#define\s+(_\w+)\s+\(\s*([A-Z]+SWI)\s*\+\s*(\d+)\s*\)", s) if b in BASES}
        for macro, name in re.findall(r"__swi\(\s*(_\w+)\s*\)\s*\w+\s*\**\s*(\w+)\s*\(", s):
            if macro in macros:
                out.setdefault(macros[macro], name)
    return out


def table() -> dict[int, str]:
    global _table
    if _table is None:
        cache = repo_root() / "build" / "swinames.json"
        try:
            _table = {int(k): v for k, v in json.loads(cache.read_text()).items()}
        except (OSError, ValueError):
            _table = _build()
            try:
                cache.parent.mkdir(parents=True, exist_ok=True)
                cache.write_text(json.dumps({str(k): v for k, v in sorted(_table.items())}, indent=1))
            except OSError:
                pass
    return _table


def name(swi: int) -> str:
    """'kernel.LockSemaphore' style name, or 'graphics#38' if unknown."""
    folio = FOLIOS.get((swi >> 16) & 0xFF, f"folio{(swi >> 16) & 0xFF}")
    n = table().get(swi)
    return f"{folio}.{n}" if n else f"{folio}#{swi & 0xFFFF}"
