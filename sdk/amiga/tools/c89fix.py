#!/usr/bin/env python3
"""Make C99-style sources acceptable to Norcroft (C89) for Amiga ports.

    python3 sdk/amiga/tools/c89fix.py src/game.c src/draw.c ...   (edits in place)

Handles the two things the Amiga games use most:
  * declarations after statements inside a block: `WORD x = f();` becomes
    `x = f();` in place, with `WORD x;` hoisted to the top of the block
  * `for (int i = 0; ...)`: the declaration is hoisted the same way

Declarations it cannot move safely (static/array/brace initializers,
function-pointer declarators) are left alone and listed, so fix those by
hand. Run the compiler afterwards; this is a helper, not a C parser.
"""
import re
import sys

TYPES = (r"(?:unsigned\s+|signed\s+)?(?:WORD|UWORD|LONG|ULONG|BYTE|UBYTE|BOOL|APTR|STRPTR|"
         r"int|short|long|char|float|double|size_t|"
         r"struct\s+\w+|[A-Z][A-Za-z0-9_]*)")
DECL = re.compile(r"^(?P<ind>\s*)(?P<const>const\s+)?(?P<type>" + TYPES + r")(?P<ptr>[\s*]+)(?P<rest>[A-Za-z_]\w*.*);\s*(?P<cmt>/\*.*\*/|//.*)?$")
FOR_INT = re.compile(r"\bfor\s*\(\s*(?P<type>int|WORD|UWORD|LONG|ULONG|short|long|unsigned(?:\s+int)?)\s+(?P<name>[A-Za-z_]\w*)\s*=")
KEYWORDS = {"return", "goto", "case", "default", "else", "typedef", "if", "while", "for", "switch", "do", "sizeof"}


def split_top(s, sep=","):
    out, depth, cur, q = [], 0, "", None
    for ch in s:
        if q:
            cur += ch
            if ch == q:
                q = None
            continue
        if ch in "'\"":
            q = ch
        elif ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        if ch == sep and depth == 0:
            out.append(cur)
            cur = ""
        else:
            cur += ch
    out.append(cur)
    return [x.strip() for x in out]


def strip_code(line):
    """Line with strings, chars and comments blanked (for brace counting)."""
    line = re.sub(r'"(\\.|[^"\\])*"', '""', line)
    line = re.sub(r"'(\\.|[^'\\])*'", "''", line)
    line = re.sub(r"/\*.*?\*/", "", line)
    return line.split("//")[0]


def fix(path):
    lines = open(path).read().split("\n")
    # stack of blocks: [line index of '{', seen_statement, insert_at, depth]
    stack = []
    hoists = {}          # line index -> list of declarations to insert after it
    manual = []
    in_comment = False
    out = list(lines)
    for i, line in enumerate(lines):
        code = line
        if in_comment:
            if "*/" in code:
                in_comment = False
                code = code.split("*/", 1)[1]
            else:
                continue
        if "/*" in code and "*/" not in code.split("/*", 1)[1]:
            in_comment = True
            code = code.split("/*", 1)[0]
        bare = strip_code(code).strip()
        if not bare or bare.startswith("#"):
            continue
        top = stack[-1] if stack else None
        m = DECL.match(line)
        is_decl = False
        if m and top is not None and m.group("type").split()[-1] not in KEYWORDS \
                and not re.match(r"\s*(return|else|case)\b", line):
            first = split_top(m.group("rest"))[0]
            # "Foo bar = ..." / "Foo *bar" / "Foo bar;" but not "x = y" or calls "foo(bar);"
            if re.match(r"^[A-Za-z_]\w*(\s*\[[^\]]*\])*\s*(=|$)", first):
                is_decl = True
        if is_decl and top is not None:
            if top[1]:
                ind, typ = m.group("ind"), (m.group("type") + m.group("ptr").rstrip())
                decls = split_top(m.group("rest"))
                if m.group("const") or "static" in line or any("{" in d or "[" in d.split("=")[0] for d in decls):
                    manual.append((i + 1, line.strip()))
                else:
                    names, assigns = [], []
                    for d in decls:
                        nm, _, init = d.partition("=")
                        stars = re.match(r"^\s*(\**)", nm).group(1)
                        nm = nm.strip().lstrip("*").strip()
                        names.append(stars + nm)
                        if init.strip():
                            assigns.append(f"{nm} = {init.strip()};")
                    base = m.group("type")
                    ptr = m.group("ptr").replace(" ", "")
                    decl = f"{base} " + ", ".join(ptr + n for n in names) + ";"
                    hoists.setdefault(top[2], []).append((top[3], decl))
                    cmt = (" " + m.group("cmt")) if m.group("cmt") else ""
                    out[i] = (ind + " ".join(assigns) + cmt) if assigns else (ind + cmt.strip() if cmt else None)
            else:
                top[2] = i      # still in the declaration section
        fm = FOR_INT.search(line)
        if fm and top is not None:
            hoists.setdefault(top[2] if not top[1] else top[2], []).append((top[3], f"{fm.group('type')} {fm.group('name')};"))
            out[i] = line[:fm.start()] + FOR_INT.sub(lambda mm: f"for ({mm.group('name')} =", line[fm.start():], count=1)
        # statement tracking + braces
        opens = bare.count("{")
        closes = bare.count("}")
        if top is not None and not is_decl and bare not in ("{", "}") and not bare.startswith("}"):
            top[1] = True
        for _ in range(closes):
            if stack:
                stack.pop()
        for _ in range(opens):
            ind = len(line) - len(line.lstrip())
            stack.append([i, False, i, " " * (ind + 4)])
        if opens and stack and bare.endswith("{") and closes == 0 and top is not None and bare != "{":
            top[1] = True
    result = []
    for i, line in enumerate(out):
        if line is not None:
            result.append(line)
        for ind, decl in hoists.get(i, []):
            result.append(ind + decl)
    open(path, "w").write("\n".join(result))
    n = sum(len(v) for v in hoists.values())
    print(f"{path}: hoisted {n} declaration(s)")
    for ln, text in manual:
        print(f"  manual: line {ln}: {text}")


if __name__ == "__main__":
    for p in sys.argv[1:]:
        fix(p)
