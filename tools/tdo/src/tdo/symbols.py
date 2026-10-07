"""Program symbols and load-address discovery.

Every project build produces, next to the ISO:

  build/<name>.sym   armlink symbol table (offsets from the AIF load address)
  build/<name>.elf   same code with DWARF, linked so addresses == those offsets
  build/disc/LaunchMe  the AIF that the 3DO loads

The OS loads LaunchMe wherever it likes, so runtime address = load base +
offset. We find the base by locating the AIF in guest DRAM: its code matches
the file and, once the self-relocation has run, header word 1 has been turned
from a BL into a NOP.
"""
from __future__ import annotations

import bisect
import re
import struct
from dataclasses import dataclass
from pathlib import Path

AIF_HEADER = 0x80
NOP = 0xE1A00000
DRAM_SIZE = 0x200000

_SYM_RE = re.compile(r"^(\S+)\s+([0-9A-Fa-f]{6,8})\s*$")


@dataclass
class Program:
    name: str
    iso: Path
    sym: Path | None
    elf: Path | None
    aif: Path | None

    @classmethod
    def from_iso(cls, iso: str | Path) -> "Program":
        iso = Path(iso).resolve()
        build = iso.parent
        name = iso.stem
        def opt(p):
            return p if p.exists() else None
        return cls(name, iso, opt(build / f"{name}.sym"), opt(build / f"{name}.elf"),
                   opt(build / "disc" / "LaunchMe"))


class Symbols:
    def __init__(self, program: Program):
        self.program = program
        self.by_name: dict[str, int] = {}
        if program.sym:
            for line in program.sym.read_text(errors="replace").splitlines():
                m = _SYM_RE.match(line)
                if m and not m.group(1).endswith("$$Base") and not m.group(1).endswith("$$Limit"):
                    self.by_name[m.group(1)] = int(m.group(2), 16)
        # code-ish symbols for address -> name (skip linker section markers)
        self._sorted = sorted((a, n) for n, a in self.by_name.items() if "$" not in n)
        self._addrs = [a for a, _ in self._sorted]
        self._aif = program.aif.read_bytes() if program.aif else None
        self.base: int | None = None

    def __len__(self):
        return len(self.by_name)

    # ------------------------------------------------------------ load base
    def find_base(self, emu, force: bool = False) -> int | None:
        """Locate the loaded program in DRAM. Returns None if not loaded yet."""
        if self.base is not None and not force:
            # cheap revalidation: still our code at the same place?
            probe = self._aif[AIF_HEADER:AIF_HEADER + 32] if self._aif else b""
            if emu.read_mem(self.base + AIF_HEADER, len(probe)) == probe:
                return self.base
            self.base = None
        if not self._aif or len(self._aif) < AIF_HEADER + 64:
            return None
        ram = emu.read_mem(0, DRAM_SIZE)
        needle = self._aif[AIF_HEADER:AIF_HEADER + 48]
        best = None
        i = ram.find(needle)
        while i != -1:
            base = i - AIF_HEADER
            if base >= 0:
                w1 = struct.unpack(">I", ram[base + 4:base + 8])[0]
                relocated = (w1 == NOP) and struct.unpack(">I", self._aif[4:8])[0] != NOP
                score = (2 if relocated else 1)
                if best is None or score > best[0]:
                    best = (score, base)
            i = ram.find(needle, i + 1)
        self.base = best[1] if best else None
        return self.base

    # ------------------------------------------------------------ lookups
    def offset(self, name: str) -> int:
        if name not in self.by_name:
            close = [n for n in self.by_name if name.lower() in n.lower()][:8]
            raise KeyError(f"no symbol {name!r}" + (f"; similar: {close}" if close else ""))
        return self.by_name[name]

    def address(self, name: str, emu=None) -> int:
        base = self.base if emu is None else self.find_base(emu)
        if base is None:
            raise RuntimeError("program not loaded yet (no load base); run a few frames first")
        return base + self.offset(name)

    def symbolize(self, addr: int) -> str | None:
        """'func+0x1c' for a runtime address inside the program."""
        if self.base is None:
            return None
        off = addr - self.base
        if off < 0 or (self._aif and off > len(self._aif) + 0x10000):
            return None
        i = bisect.bisect_right(self._addrs, off) - 1
        if i < 0:
            return None
        a, n = self._sorted[i]
        return n if off == a else f"{n}+0x{off - a:x}"

    def search(self, pattern: str, limit: int = 50) -> list[tuple[str, int]]:
        rx = re.compile(pattern, re.I)
        out = [(n, a) for n, a in sorted(self.by_name.items(), key=lambda x: x[1]) if rx.search(n)]
        return out[:limit]


def resolve_addr(spec, symbols: Symbols | None, emu) -> int:
    """'0x1234', '1234', 'main', 'main+0x10' -> runtime address."""
    if isinstance(spec, int):
        return spec
    s = str(spec).strip()
    try:
        return int(s, 0)
    except ValueError:
        pass
    m = re.fullmatch(r"([A-Za-z_.$][\w.$]*)\s*(?:\+\s*(0x[0-9a-fA-F]+|\d+))?", s)
    if not m or symbols is None:
        raise ValueError(f"can't resolve address {spec!r} (no symbols loaded?)")
    return symbols.address(m.group(1), emu) + (int(m.group(2), 0) if m.group(2) else 0)
