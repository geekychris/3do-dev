#!/usr/bin/env python3
"""Convert a typical Amiga game main.c for the 3DO layer, keeping its code.

    python3 sdk/amiga/tools/amigamain.py src/main.c <game> [palette_var] [ncolors]

For games whose main.c holds the game loop (instead of rewriting it as
main_3do.c). Applies the conversions every one of these games needed, all
under #ifdef AMIGA3DO / #ifndef AMIGA3DO so the Amiga build is unchanged:

  - setup_display() / swap_buffers() / cleanup_display(): replaced by the
    layer's gfx_init(palette, n) / gfx_swap() / gfx_exit()
  - OpenLibrary/CloseLibrary, the IDCMP message loop, Ctrl-C checks: compiled out
  - rp = &rp_buf[cur_buf]  ->  gfx_back()
  - WaitTOF() before swap_buffers(): dropped (gfx_swap paces to 50 fps)
  - "DH2:Dev/<file>" paths -> "PROGDIR:<file>" + amiga_set_progdir("<game>")

Prints what it changed and what it couldn't find; check the result, then
write input_3do.c for the game's input functions.
Run c89fix.py first (it doesn't follow the preprocessor).
"""
import re
import sys


def brace_block_end(s, start):
    """Index just past the '}' that closes the first '{' at/after start."""
    i = s.index("{", start)
    depth = 0
    while True:
        c = s[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1


def statement_end(s, start):
    """End of the statement starting at start: a {...} block or up to ';'."""
    semi = s.index(";", start)
    brace = s.find("{", start)
    if brace != -1 and brace < semi:
        return brace_block_end(s, start)
    return semi + 1


def wrap(s, a, b, guard="#ifndef AMIGA3DO"):
    line_start = s.rfind("\n", 0, a) + 1
    nl = s.find("\n", b)
    end = len(s) if nl == -1 else nl + 1
    return s[:line_start] + guard + "\n" + s[line_start:end] + "#endif\n" + s[end:], len(guard) + 8


def convert(path, game, palette="palette", ncolors=16):
    s = open(path).read()
    done, missing = [], []

    # 1. display functions
    m = re.search(r"static\s+\w+\s+setup_display\s*\(\s*void\s*\)", s)
    if m:
        cm = re.search(r"static\s+void\s+cleanup_display\s*\(\s*void\s*\)\s*\{", s)
        if cm and cm.start() > m.start():
            end = brace_block_end(s, cm.start())
            region = s[m.start():end]
            s = s[:m.start()] + (
                "#ifdef AMIGA3DO\n"
                "/* 3DO port: screen + double buffering from the sdk/amiga layer */\n"
                f"static WORD setup_display(void)   {{ return gfx_init({palette}, {ncolors}) ? 1 : 0; }}\n"
                "static void swap_buffers(void)    { gfx_swap(); }\n"
                "static void cleanup_display(void) { gfx_exit(); }\n"
                "#else\n" + region + "\n#endif") + s[end:]
            done.append("display functions -> gfx_init/gfx_swap/gfx_exit")
        else:
            missing.append("cleanup_display() after setup_display()")
    else:
        missing.append("setup_display()")

    # 2. OpenLibrary block
    m = re.search(r"\n[ \t]*/\*[^\n]*[Oo]pen librar[^\n]*\*/\n", s)
    o = s.find("OpenLibrary(")
    if o != -1:
        a = m.start() + 1 if m and m.start() < o and o - m.end() < 200 else s.rfind("\n", 0, o) + 1
        # the IntuitionBase/GfxBase assignments and the following if (...) check
        b = o
        while True:
            nxt = s.find("OpenLibrary(", b + 1)
            stmt_end = statement_end(s, b)
            if nxt != -1 and nxt - stmt_end < 120:
                b = nxt
                continue
            b = stmt_end
            break
        after = re.match(r"\s*if\s*\(", s[b:])
        if after:
            b = statement_end(s, b + after.start() + s[b:].index("if"))
        s, _ = wrap(s, a, b)
        done.append("OpenLibrary block")
    # every CloseLibrary group
    count = 0
    pos = 0
    while True:
        c = s.find("CloseLibrary(", pos)
        if c == -1:
            break
        line_start = s.rfind("\n", 0, c) + 1
        if s[max(0, line_start - 17):line_start].strip().endswith("#ifndef AMIGA3DO") or \
           "#ifndef AMIGA3DO" in s[max(0, line_start - 400):line_start].split("#endif")[-1]:
            pos = c + 1
            continue
        end = s.index("\n", c) + 1
        while re.match(r"[ \t]*(if \(\w+\) )?CloseLibrary\(", s[end:]):
            end = s.index("\n", end) + 1
        s = s[:line_start] + "#ifndef AMIGA3DO\n" + s[line_start:end] + "#endif\n" + s[end:]
        count += 1
        pos = end + 24
    if count:
        done.append(f"{count} CloseLibrary group(s)")

    # 3. IDCMP loops and Ctrl-C checks
    for pat, what in [(r"while\s*\(\s*\(\s*\w+\s*=\s*\(struct IntuiMessage \*\)\s*GetMsg\(", "IDCMP loop"),
                      (r"if\s*\(\s*SetSignal\(", "Ctrl-C check"),
                      (r"\n[ \t]*SetSignal\(0L?, *SIGBREAKF_CTRL_C\);", "SetSignal reset")]:
        pos, n = 0, 0
        while True:
            m = re.compile(pat).search(s, pos)
            if not m:
                break
            a = m.start() + (1 if s[m.start()] == "\n" else 0)
            b = statement_end(s, a)
            s, add = wrap(s, a, b)
            pos = b + add
            n += 1
        if n:
            done.append(f"{n} {what}(s)")

    # 4. back buffer
    s, n = re.subn(r"(\n([ \t]*)(struct RastPort \*)?rp = &rp_buf\[cur_buf\];)",
                   lambda m: f"\n#ifdef AMIGA3DO\n{m.group(2)}{m.group(3) or ''}rp = gfx_back();\n#else{m.group(1)}\n#endif", s)
    if n:
        done.append("rp = gfx_back()")

    # 5. WaitTOF before swap_buffers
    s, n = re.subn(r"\n([ \t]*)WaitTOF\(\);\n(\s*)swap_buffers\(\);",
                   r"\n#ifndef AMIGA3DO   /* gfx_swap() paces to 50 fps */\n\1WaitTOF();\n#endif\n\2swap_buffers();", s)
    if n:
        done.append("WaitTOF before swap dropped")

    # 6. file paths
    s, n = re.subn(r'"DH\d:Dev/([^"/]+)"', r'"PROGDIR:\1"', s)
    if n:
        s, k = re.subn(r'(ab_init\("[^"]*"\);)', rf'\1\n#ifdef AMIGA3DO\n    amiga_set_progdir("{game}");\n#endif', s, count=1)
        done.append(f"{n} DH2:Dev path(s) -> PROGDIR:" + ("" if k else " (add amiga_set_progdir yourself)"))

    # 7. include
    if '"amiga3do.h"' not in s:
        last = list(re.finditer(r'#include [<"][^>"]+[>"]\n', s))[-1]
        s = s[:last.end()] + '#ifdef AMIGA3DO\n#include "amiga3do.h"\n#endif\n' + s[last.end():]

    open(path, "w").write(s)
    print(f"{path}:")
    for d in done:
        print("  converted:", d)
    for mi in missing:
        print("  NOT FOUND:", mi)


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(2)
    convert(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else "palette",
            int(sys.argv[4]) if len(sys.argv) > 4 else 16)
