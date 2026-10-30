#!/usr/bin/env python3
"""c89init.py - rewrite C99 aggregate initialisers with run-time values
(Norcroft C89 rejects them) into assignments:

    const int32_t v[] = { g->a, g->b };     ->   int32_t v[2];
                                                 ... (after the declarations)
                                                 v[0] = g->a; v[1] = g->b;

Two such arrays one after the other come out declaration, assignments,
declaration: join them by hand. Only arrays declared inside a function body whose initialiser mentions a
'->' or '.' member (a run-time value) are touched. The assignments go
right after the block's last declaration. Prints what it changed.

    python3 sdk/amiga3d/tools/c89init.py src/*.c
"""
import re
import sys

DECL = re.compile(r'^(\s*)(?:const\s+)?((?:unsigned\s+|signed\s+)?[A-Za-z_][A-Za-z0-9_]*)\s+(\w+)\[\s*\w*\s*\]\s*=\s*\{', re.M)


def split_top(s):
    out, depth, cur = [], 0, ''
    for ch in s:
        if ch in '([{': depth += 1
        if ch in ')]}': depth -= 1
        if ch == ',' and depth == 0:
            out.append(cur.strip()); cur = ''
        else:
            cur += ch
    if cur.strip(): out.append(cur.strip())
    return out


def fix(text, name):
    pos = 0
    changed = []
    while True:
        m = DECL.search(text, pos)
        if not m: break
        start = m.end()
        depth, i = 1, start
        while depth:
            if text[i] == '{': depth += 1
            elif text[i] == '}': depth -= 1
            i += 1
        body = text[start:i - 1]
        semi = text.index(';', i)
        if text[i:semi].strip():            # more declarators after it: leave it to a hand edit
            pos = semi; continue
        if ('->' not in body and not re.search(r'\w\.\w', body)) or m.group(1) == '':
            pos = semi; continue
        items = split_top(body)
        ind, typ, var = m.group(1), m.group(2), m.group(3)
        decl = f'{ind}{typ} {var}[{len(items)}];'
        assigns = ' '.join(f'{var}[{k}] = {v};' for k, v in enumerate(items))
        text = text[:m.start()] + decl + text[semi + 1:]
        # after the last declaration of the block: the first line that isn't one
        j = m.start() + len(decl) + 1
        lines_after = text[j:].split('\n')
        k = 0
        declre = re.compile(r'^\s*(?:const\s+|static\s+|unsigned\s+|signed\s+)*(?:int|long|short|char|uint\w*|int\w*|fix|[A-Z]\w*)\s*\**\s*\w+.*;\s*$')
        while k < len(lines_after) and declre.match(lines_after[k]) and '=' not in lines_after[k].split(';')[0].split('(')[0] or (k < len(lines_after) and declre.match(lines_after[k]) and '(' not in lines_after[k]):
            k += 1
        ins = j + sum(len(l) + 1 for l in lines_after[:k])
        text = text[:ins] + f'{ind}{assigns}\n' + text[ins:]
        changed.append(f'{name}: {var}[{len(items)}]')
        pos = ins
    return text, changed


for p in sys.argv[1:]:
    t = open(p).read()
    t2, ch = fix(t, p)
    if ch:
        open(p, 'w').write(t2)
        for c in ch: print('  fixed', c)
