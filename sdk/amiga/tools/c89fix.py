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


def split_decl_statements(lines):
    """`LONG env = 256 - t; if (env < 0) env = 0;` -> declaration and the
    statement on separate lines, so the statement isn't hidden in the
    declaration section."""
    out = []
    for line in lines:
        m = DECL.match(line)
        code = strip_code(line)
        if m and "for" not in code.split("(")[0] and code.count(";") >= 2:
            parts = split_top(code, ";")
            if len(parts) >= 3 and parts[-1] == "" and re.match(r"^" + TYPES + r"[\s*]+[A-Za-z_]", parts[0].strip()):
                ind = m.group("ind")
                first = parts[0].strip() + ";"
                rest = line[line.index(parts[0].strip()) + len(parts[0].strip()):].lstrip()[1:].strip()
                out.append(ind + first)
                out.append(ind + rest)
                continue
        out.append(line)
    return out


def struct_fields(text):
    """typedef struct { ... } Name;  ->  {Name: [field, ...]} (simple members only)"""
    out = {}
    for m in re.finditer(r"typedef\s+struct\s*\w*\s*\{(.*?)\}\s*(\w+)\s*;", text, re.S):
        body = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
        fields = []
        for decl in body.split(";"):
            decl = decl.strip()
            if not decl:
                continue
            names = decl.split(",")
            first = re.match(r".*?([A-Za-z_]\w*)\s*(\[[^\]]*\])?\s*$", names[0].strip())
            if not first:
                fields = None
                break
            fields.append(first.group(1))
            for n in names[1:]:
                nm = re.match(r"\s*\**\s*([A-Za-z_]\w*)", n)
                if nm:
                    fields.append(nm.group(1))
        if fields:
            out[m.group(2)] = fields
    return out


def compound_literals(text):
    """lhs = (Type){ a, b, c };  ->  lhs.f1 = a; lhs.f2 = b; lhs.f3 = c;"""
    types = struct_fields(text)
    count = 0

    def repl(m):
        nonlocal count
        lhs, typ, vals = m.group(1), m.group(2), split_top(m.group(3), ",")
        if typ not in types or len(vals) > len(types[typ]):
            return m.group(0)
        count += 1
        return " ".join(f"{lhs}.{f} = {v};" for f, v in zip(types[typ], vals))
    text = re.sub(r"([A-Za-z_][\w\.\[\]>-]*)\s*=\s*\((\w+)\)\s*\{([^{}]*)\}\s*;", repl, text)
    return text, count


def fix(path):
    text = open(path).read()
    text, n_lit = compound_literals(text)
    if n_lit:
        print(f"{path}: rewrote {n_lit} compound literal(s)")
    n_inline = len(re.findall(r"\bstatic\s+inline\b|\binline\s+static\b|\b__inline\b", text))
    text = re.sub(r"\bstatic\s+inline\b|\binline\s+static\b", "static", text)
    text = re.sub(r"\b__inline\b\s*", "", text)
    if n_inline:
        print(f"{path}: removed {n_inline} inline keyword(s)")
    lines = split_decl_statements(text.split("\n"))
    # stack of blocks: [line index of '{', seen_statement, insert_at, depth]
    stack = []
    hoists = {}          # line index -> list of declarations to insert after it
    manual = []
    in_comment = False
    prev_end = ";"          # last character of the previous code line
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
        # a declaration can only start a new statement: after ; { } or a label
        starts_statement = prev_end in ";{}:"
        prev_end = bare[-1]
        top = stack[-1] if stack else None
        if top is not None and top[4]:
            top = None          # inside a struct body / initializer: leave alone
        m = DECL.match(line)
        is_decl = False
        if m and starts_statement and top is not None and m.group("type").split()[-1] not in KEYWORDS \
                and not re.match(r"\s*(return|else|case)\b", line):
            first = split_top(m.group("rest"))[0]
            # "Foo bar = ..." / "Foo *bar" / "Foo bar;" but not "x = y" or calls "foo(bar);"
            if re.match(r"^[A-Za-z_]\w*(\s*\[[^\]]*\])*\s*(=|$)", first):
                is_decl = True
        if is_decl and top is not None:
            if top[1]:
                ind, typ = m.group("ind"), (m.group("type") + m.group("ptr").rstrip())
                decls = split_top(m.group("rest"))
                if m.group("const") or "static" in line or any("{" in d for d in decls) or \
                        any("[" in d.split("=")[0] and "=" in d for d in decls):
                    manual.append((i + 1, line.strip()))
                else:
                    names, assigns = [], []
                    for d in decls:
                        nm, _, init = d.partition("=")
                        stars = re.match(r"^\s*(\**)", nm).group(1)
                        nm = nm.strip().lstrip("*").strip()
                        names.append(stars + nm)          # arrays keep their [N]
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
        # braces in source order: "enum { A, B };" opens and closes on one line
        ind = len(line) - len(line.lstrip())
        data = bool(re.search(r"\b(struct|union|enum)\b[^;()]*\{|=\s*\{", bare)) or \
            (bare.startswith("{") and i > 0 and
             re.search(r"\b(struct|union|enum)\b[^;()]*$|=\s*$", strip_code(lines[i - 1]).strip()))
        for ch in bare:
            if ch == "{":
                d = bool(data) or bool(stack and stack[-1][4])
                stack.append([i, False, i, " " * (ind + 4), d])
            elif ch == "}" and stack:
                stack.pop()
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
