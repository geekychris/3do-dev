"""Bullion Dash's levels: read/write level.c's level_data[], the text format,
and the route search under the original's rules (player.c).

The movement model is player.c's: the player walks left/right when on a
ladder or a bar or supported (on the bottom row, on a ladder, or over a
brick, solid block, trap or ladder); climbs up only from a ladder cell; goes
down a ladder (his cell or the one below is a ladder); falls when none of
those hold. He can't drop from a bar (player.c's "release" never takes
effect). Hidden ladders are plain space until all the gold is in. The guards
are ignored, and so is digging: a dug brick grows back on a player standing
in it, so a route never relies on it.
"""
import re
from collections import deque

CHARS = ".#@H-$XPET"
E, B, S, L, R, G, X, P, EN, T = range(10)
W, H = 28, 16


# ------------------------------------------------------------ level.c
def _block(text):
    a = text.index("level_data[150 * 224] = {")
    a = text.index("{", a) + 1
    b = text.index("};", a)
    return a, b


def read_level_c(path):
    text = open(path).read()
    a, b = _block(text)
    vals = [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{2})", text[a:b])]
    assert len(vals) == 150 * 224, len(vals)
    levels = []
    for lv in range(150):
        p = vals[lv * 224:(lv + 1) * 224]
        g = [[0] * W for _ in range(H)]
        i = 0
        for x in range(W):                 # unpack_level(): column-major, high nibble = even row
            for y in range(0, H, 2):
                g[y][x], g[y + 1][x] = p[i] >> 4, p[i] & 15
                i += 1
        levels.append(g)
    return levels


def pack(g):
    out = []
    for x in range(W):
        for y in range(0, H, 2):
            out.append((g[y][x] << 4) | g[y + 1][x])
    return out


def write_level_c(path, levels):
    text = open(path).read()
    a, b = _block(text)
    lines = []
    for lv, g in enumerate(levels):
        lines.append(f"    /* Level {lv + 1} */")
        p = pack(g)
        for i in range(0, 224, 16):
            lines.append("    " + ",".join(f"0x{v:02X}" for v in p[i:i + 16]) + ",")
    body = "\n" + "\n".join(lines)[:-1] + "\n"
    open(path, "w").write(text[:a] + body + text[b:])


def to_text(levels, title):
    out = [f"; {title}", f"size {W} {H}",
           "legend " + "  ".join(f"{c} {i}" for i, c in enumerate(CHARS))]
    for lv, g in enumerate(levels):
        out.append(f"room LEVEL {lv + 1}")
        out += ["".join(CHARS[t] for t in row) for row in g]
    return "\n".join(out) + "\n"


def from_text(text):
    levels, cur = [], None
    for line in text.splitlines():
        if line.startswith("room "):
            cur = []
            levels.append(cur)
        elif cur is not None and len(line) == W and len(cur) < H:
            cur.append([CHARS.index(c) for c in line])
    return levels


# ------------------------------------------------------------ the rules
def enter(t):
    return t in (E, G, L, R, X, T, P, EN)


def solid(t):
    return t in (B, S, T)


def tile(g, x, y):
    return g[y][x] if 0 <= x < W and 0 <= y < H else S


def support(g, x, y):
    return y >= H - 1 or g[y][x] == L or solid(tile(g, x, y + 1)) or tile(g, x, y + 1) == L


def moves(g, x, y):
    """(cell, kind) the player can go to from (x, y)"""
    t = g[y][x]
    if t not in (L, R) and not support(g, x, y):
        return [((x, y + 1), "fall")] if y + 1 < H else []
    out = []
    for d in (-1, 1):
        if 0 <= x + d < W and enter(g[y][x + d]):
            out.append(((x + d, y), "left" if d < 0 else "right"))
    if t == L and y > 0 and enter(g[y - 1][x]):
        out.append(((x, y - 1), "up"))
    if (t == L or tile(g, x, y + 1) == L) and y + 1 < H and enter(g[y + 1][x]):
        out.append(((x, y + 1), "down"))
    return out


def reach(g, start):
    seen = {start}
    q = deque([start])
    while q:
        c = q.popleft()
        for n, _ in moves(g, *c):
            if n not in seen:
                seen.add(n)
                q.append(n)
    return seen


def path(g, a, goals):
    """the moves of a shortest route from a to any of goals, or None"""
    prev = {a: None}
    q = deque([a])
    while q:
        c = q.popleft()
        if c in goals:
            out = []
            while prev[c]:
                p, kind = prev[c]
                out.append((kind, c))
                c = p
            return out[::-1]
        for n, kind in moves(g, *c):
            if n not in prev:
                prev[n] = (c, kind)
                q.append(n)
    return None


def plain(g):
    """the markers as space (the player and guards start there)"""
    return [[E if t in (P, EN) else t for t in row] for row in g]


def shown(g):
    return [[L if t == X else t for t in row] for row in g]


def start_of(g):
    return next((x, y) for y in range(H) for x in range(W) if g[y][x] == P)


def golds_of(g):
    return [(x, y) for y in range(H) for x in range(W) if g[y][x] == G]


def plan(g):
    """a winning order of the gold and the route, or (None, the break):
    greedy: from where he is, the reachable gold from which most of the
    rest can still be reached (then the nearest); at the end, row 0 with
    the hidden ladders shown."""
    pg = plain(g)
    cur = start_of(g)
    rem = golds_of(g)
    order = []
    memo = {}

    def rch(c):
        if c not in memo:
            memo[c] = reach(pg, c)
        return memo[c]
    while rem:
        r = rch(cur)
        cand = [c for c in rem if c in r]
        if not cand:
            far = min(rem, key=lambda c: abs(c[0] - cur[0]) + abs(c[1] - cur[1]))
            return None, ("gold", cur, far, len(order))
        def score(c):
            rc = rch(c)
            return (-sum(1 for o in rem if o != c and o in rc), abs(c[0] - cur[0]) + abs(c[1] - cur[1]))
        best = min(cand, key=score)
        order.append(best)
        rem.remove(best)
        cur = best
    sg = shown(pg)
    if not any(y == 0 for _, y in reach(sg, cur)):
        return None, ("exit", cur, None, len(order))
    return order, None


def route(g, order):
    """the moves: start -> each gold in order -> row 0 (hidden ladders shown)"""
    pg = plain(g)
    cur = start_of(g)
    out = []
    for c in order:
        p = path(pg, cur, {c})
        out += p
        cur = c
    top = {(x, 0) for x in range(W)}
    out += path(shown(pg), cur, top)
    return out
