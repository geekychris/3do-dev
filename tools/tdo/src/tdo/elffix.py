"""Make Norcroft/armlink ELF output friendly to gdb.

armlink (SDT 2.51) names the loadable sections after the output file
("build/demo.elf"). gdb identifies text/data/bss by section *name* when it
applies the target's qOffsets relocation, so with those names only code got
relocated and globals were read from (and written to!) the wrong addresses.

This renames the alloc sections by their flags (.text/.data/.bss), writing a
new section-name table at the end of the file. It also:

  * hides .debug_frame: Norcroft's CFI makes gdb compute bogus (odd) frame
    addresses, while gdb's ARM prologue analyzer unwinds this code correctly;
  * rewrites DW_FORM_addr -> DW_FORM_data4 in .debug_abbrev for attributes
    that are section offsets (stmt_list, macro_info, location lists). gdb
    rejects the addr form there and silently drops all line-number info.
    Both forms are 4 bytes, so .debug_info is unchanged.

Idempotent.

    python -m tdo.elffix build/*.elf
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

SHT_PROGBITS, SHT_NOBITS = 1, 8
SHF_WRITE, SHF_ALLOC, SHF_EXECINSTR = 0x1, 0x2, 0x4


def fix(path: str | Path) -> bool:
    p = Path(path)
    d = bytearray(p.read_bytes())
    if d[:4] != b"\x7fELF" or d[4] != 1:
        raise ValueError(f"{p}: not a 32-bit ELF")
    E = ">" if d[5] == 2 else "<"
    shoff = struct.unpack_from(E + "I", d, 0x20)[0]
    shentsize, shnum, shstrndx = struct.unpack_from(E + "HHH", d, 0x2E)

    def sh(i):
        return list(struct.unpack_from(E + "10I", d, shoff + i * shentsize))

    strhdr = sh(shstrndx)
    oldtab = bytes(d[strhdr[4]:strhdr[4] + strhdr[5]])

    def name(i):
        off = sh(i)[0]
        return oldtab[off:oldtab.index(b"\0", off)].decode("latin-1")

    for i in range(1, shnum):
        if name(i) == ".debug_abbrev":
            h = sh(i)
            _fix_abbrev_forms(d, h[4], h[5])

    wanted = {}
    for i in range(1, shnum):
        h = sh(i)
        t, f = h[1], h[2]
        if name(i) == ".debug_frame":
            wanted[i] = ".debug_frame.norcroft"
            continue
        if not f & SHF_ALLOC:
            continue
        if t == SHT_NOBITS:
            want = ".bss"
        elif t == SHT_PROGBITS:
            want = ".text" if f & SHF_EXECINSTR else (".data" if f & SHF_WRITE else ".rodata")
        else:
            continue
        if name(i) != want:
            wanted[i] = want
    if not wanted:
        p.write_bytes(bytes(d))   # abbrev form fixes may still apply
        return False

    newtab = bytearray(oldtab)
    offsets = {}
    for want in sorted(set(wanted.values())):
        offsets[want] = len(newtab)
        newtab += want.encode() + b"\0"
    while len(d) % 4:
        d.append(0)
    tab_off = len(d)
    d += newtab
    for i, want in wanted.items():
        struct.pack_into(E + "I", d, shoff + i * shentsize, offsets[want])
    struct.pack_into(E + "I", d, shoff + shstrndx * shentsize + 16, tab_off)   # sh_offset
    struct.pack_into(E + "I", d, shoff + shstrndx * shentsize + 20, len(newtab))  # sh_size
    p.write_bytes(bytes(d))
    return True


DW_FORM_addr, DW_FORM_data4 = 0x01, 0x06
# attributes whose value is an offset into another debug section
_OFFSET_ATTRS = {0x02: "location", 0x10: "stmt_list", 0x40: "frame_base",
                 0x43: "macro_info", 0x2A: "return_addr", 0x48: "static_link"}


def _uleb(d, o):
    v = shift = 0
    while True:
        b = d[o]
        o += 1
        v |= (b & 0x7F) << shift
        shift += 7
        if not b & 0x80:
            return v, o


def _fix_abbrev_forms(d: bytearray, off: int, size: int) -> int:
    """Patch in place; returns number of attribute forms changed."""
    o, end, n = off, off + size, 0
    while o < end:
        code, o = _uleb(d, o)
        if code == 0:
            continue           # end of one CU's abbrev table
        _tag, o = _uleb(d, o)
        o += 1                 # children flag
        while True:
            attr, o = _uleb(d, o)
            form_pos = o
            form, o = _uleb(d, o)
            if attr == 0 and form == 0:
                break
            if form == DW_FORM_addr and attr in _OFFSET_ATTRS:
                d[form_pos] = DW_FORM_data4
                n += 1
    return n


def main(argv=None):
    for f in (argv if argv is not None else sys.argv[1:]):
        if fix(f):
            print(f"elffix: renamed sections in {f}")


if __name__ == "__main__":
    main()
