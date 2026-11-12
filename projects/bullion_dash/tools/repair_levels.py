"""Repair Bullion Dash's 150 levels so every one can be won (as released,
only level 24 could be), with the smallest edits that do it.

Per level, while tools/bdlevels.py's route search finds no winning order
(the gold in some order, each reachable from the last, then row 0 with the
hidden ladders shown), it takes the first break and tries the cheapest fix:

  - a ladder (new or extending one) in one column, from a cell the player
    can reach (or the cell beside one), up or down: an empty or
    hidden-ladder cell costs 1, a bar's cell 2, a brick 3 (opened for the
    ladder), a gold, the start or a guard 4 (moved to the nearest place it
    can lie); solid blocks and traps stay;
  - for the way out (row 0), a hidden ladder up to row 1 (it appears with
    the last gold, as the original's exits do), or a plain ladder where it
    has to go through bricks;
  - two ladders, when they cost less than the best single one (or when
    no single one helps);
  - if no ladder (cost up to 20) fixes it, the gold is moved to the
    nearest place the player can reach (never needed for the 150).

A fix is kept only if it brings the break nearer (more gold collected in
order, or the exit reached; for the exit, a higher row reached). The original
levels go to levels_original.txt, the repaired ones to levels_repaired.txt
and level.c, and every changed cell to REPAIRED_LEVELS.md.

    python3 tools/repair_levels.py          # from bullion_dash/
"""
import multiprocessing
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bdlevels as bd  # noqa: E402
from bdlevels import E, B, L, X, G, W, H  # noqa: E402

HERE = Path(__file__).resolve().parents[1]
MAXCOST = 20


def progress(g):
    """(the gold collected in order before the break, then: the exit; for
    the exit, the highest row he gets to)"""
    order, brk = bd.plan(g)
    if order is not None:
        return (10_000, 0)
    if brk[0] == "exit":
        r = bd.reach(bd.shown(bd.plain(g)), brk[1])
        return (brk[3] * 10 + 5, -min(y for _, y in r))
    return (brk[3] * 10, 0)


def ladder_candidates(g, cur, exit_only):
    """column segments starting at a cell reachable from cur: (cost, cells, kind)"""
    pg = bd.plain(g)
    r = bd.reach(bd.shown(pg) if exit_only else pg, cur)
    if not exit_only:                      # (or on the way there: the route can change)
        r |= bd.reach(pg, bd.start_of(g))
    starts = set(r)
    for (x, y) in r:                       # (or the cell beside one: he walks onto the ladder)
        if pg[y][x] in (L, bd.R) or bd.support(pg, x, y):
            starts.update((x + d, y) for d in (-1, 1) if 0 <= x + d < W)
    seen = set()
    out = []
    for (x, y0) in sorted(starts):
        for direction in (-1, 1):
            cells = []
            cost = 0
            y = y0
            while 0 <= y < H:
                t = g[y][x]                # (markers stay: a ladder can't go through one)
                if t in (E, X):
                    cost += 1
                elif t == B:
                    cost += 3
                elif t == bd.R:
                    cost += 2              # (a bar's cell becomes the ladder's)
                elif t in (G, bd.P, bd.EN):
                    cost += 4              # (moved aside: apply())
                elif t != L:
                    break
                if t != L:
                    cells.append((x, y))
                if cost > MAXCOST:
                    break
                if cells:
                    key = (x, min(c[1] for c in cells), max(c[1] for c in cells))
                    if key not in seen:
                        seen.add(key)
                        out.append((cost, list(cells)))
                y += direction
    out.sort(key=lambda c: (c[0], len(c[1])))
    return out


def apply(g, cells, hidden):
    """the ladder; gold, the start or a guard in its way moves to the
    nearest space it can stand on (by the original's rules) off the ladder"""
    g2 = [row[:] for row in g]
    moved = []
    for x, y in cells:
        if g[y][x] in (G, bd.P, bd.EN):
            moved.append(g[y][x])
        g2[y][x] = X if (hidden and g[y][x] in (E, X)) else L
    for t in moved:
        x0, y0 = next(c for c in cells if g[c[1]][c[0]] == t)
        pg = bd.plain(g2)
        spots = [(x, y) for y in range(H) for x in range(W)
                 if g2[y][x] == E and bd.support(pg, x, y)]
        if not spots:
            return None
        x, y = min(spots, key=lambda c: (abs(c[0] - x0) + abs(c[1] - y0), c[1], c[0]))
        g2[y][x] = t
    return g2


def describe(g, cells, hidden=False):
    xs = cells[0][0]
    ys = [c[1] for c in cells]
    extra = []
    nb = sum(1 for x, y in cells if g[y][x] == B)
    if nb:
        extra.append(f"{nb} through brick")
    for t, name in ((G, "gold"), (bd.P, "the start"), (bd.EN, "a guard")):
        n = sum(1 for x, y in cells if g[y][x] == t)
        if n:
            extra.append(f"{name} moved aside")
    return (f"{'hidden ' if hidden else ''}ladder at column {xs}, rows {min(ys)}-{max(ys)}"
            + (f" ({', '.join(extra)})" if extra else ""))


def repair(g, log):
    for _ in range(40):
        order, brk = bd.plan(g)
        if order is not None:
            return g
        kind, cur, target, _n = brk
        base = progress(g)
        exit_ = kind == "exit"
        tries = []
        for cost, cells in ladder_candidates(g, cur, exit_):
            hidden = exit_ and min(c[1] for c in cells) == 1 and all(g[y][x] in (E, X) for x, y in cells)
            g2 = apply(g, cells, hidden)
            if g2 is not None:
                tries.append((g2, describe(g, cells, hidden), cost))
        # the cheapest that gets more gold in (or out); else the cheapest that gets him further
        best = next(((g2, d, c) for g2, d, c in tries if progress(g2)[0] > base[0]), None)
        if best is None or best[2] > 6:
            # two cheap ladders rather than one long one
            two = pair(g, cur, exit_, base, best[2] if best else 99)
            if two:
                best = two
        if best is None and exit_:
            best = next(((g2, d, c) for g2, d, c in tries[:200]
                         if progress(g2)[0] == base[0] and progress(g2)[1] > base[1]), None)
        if best is None and not exit_:
            tries = []
            # the gold he can't reach, moved to the nearest place he can reach and get back from
            pg = bd.plain(g)
            r = bd.reach(pg, cur)
            spots = [c for c in r if g[c[1]][c[0]] == E and bd.support(pg, *c) and cur in bd.reach(pg, c)]
            spots.sort(key=lambda c: abs(c[0] - target[0]) + abs(c[1] - target[1]))
            for to in spots[:3]:
                g2 = [row[:] for row in g]
                g2[target[1]][target[0]] = E
                g2[to[1]][to[0]] = G
                tries.append((g2, f"gold moved from {target} to {to}", 0))
            best = next(((g2, d, c) for g2, d, c in tries if progress(g2)[0] > base[0]), None)
        if best is None:
            log.append(f"UNREPAIRED: {kind} at {cur}")
            return g
        g = best[0]
        log.append(("exit: " if exit_ else "") + best[1])
    return g


def pair(g, cur, exit_, base, limit, n=25):
    """two ladders costing less than limit together, the cheapest pair that
    gets more gold in (or out)"""
    def seen(gr):                          # (a quick test before the whole plan)
        pg = bd.plain(gr)
        r = bd.reach(bd.shown(pg) if exit_ else pg, cur)
        if exit_:
            return any(y == 0 for _, y in r)
        return sum(1 for x, y in r if pg[y][x] == G)
    before = seen(g)
    found = None
    for c1, cells1 in ladder_candidates(g, cur, exit_)[:n]:
        if c1 >= (found[0] if found else limit):
            break
        h1 = exit_ and min(c[1] for c in cells1) == 1 and all(g[y][x] in (E, X) for x, y in cells1)
        g1 = apply(g, cells1, h1)
        if g1 is None:
            continue
        for c2, cells2 in ladder_candidates(g1, cur, exit_):
            if c1 + c2 >= (found[0] if found else limit):
                break
            h2 = exit_ and min(c[1] for c in cells2) == 1 and all(g1[y][x] in (E, X) for x, y in cells2)
            g2 = apply(g1, cells2, h2)
            if g2 is None or seen(g2) <= before:
                continue
            if progress(g2)[0] > base[0]:
                found = (c1 + c2, g2, describe(g, cells1, h1) + "; " + describe(g1, cells2, h2))
    return (found[1], found[2], found[0]) if found else None


def repair_one(g):
    log = []
    return repair(g, log), log


def diff(a, b):
    return [(x, y, bd.CHARS[a[y][x]], bd.CHARS[b[y][x]])
            for y in range(H) for x in range(W) if a[y][x] != b[y][x]]


def main():
    src = HERE / "src" / "level.c" if (HERE / "src" / "level.c").exists() else HERE / "level.c"
    orig_txt = HERE / "levels_original.txt"
    if orig_txt.exists():
        levels = bd.from_text(orig_txt.read_text())
    else:
        levels = bd.read_level_c(src)
        orig_txt.write_text(bd.to_text(levels, "Bullion Dash: the original's 150 levels (as released; "
                                               "149 can't be won: ORIGINAL_BUGS.md)"))
    out, report = [], [
        "# Bullion Dash: the repaired levels", "",
        "As released, 149 of the 150 levels could not be won (only level 24 could):",
        "gold the player can't get to, or no way up to row 0 once the gold is in,",
        "by the game's own movement rules (player.c: he climbs only from a ladder",
        "cell, can't drop from a bar, and a dug hole grows back on him). The levels",
        "in `level.c` are now repaired with the smallest edits that make each one",
        "winnable, keeping its layout; the levels as released are in",
        "`levels_original.txt` (the editor's text format), the repaired ones in",
        "`levels_repaired.txt`.", "",
        "`tools/repair_levels.py` made them. Its route search (`tools/bdlevels.py`)",
        "follows player.c, with the guards left out and no digging; a level is",
        "winnable when all the gold can be collected in some order, each from the",
        "last, and row 0 reached afterwards with the hidden ladders shown. While a",
        "level isn't, the cheapest fix at the first place the route breaks is kept:",
        "a ladder in one column (new or extending one; through space, a brick, a",
        "bar's cell, or a gold or guard in its way, which moves to the nearest",
        "place it can lie: 1-2 cells), or two such ladders when they cost less",
        "than one; for the way out, a hidden ladder up to row 1 where it only",
        "crosses space (it appears with the last gold, as the original exits do).",
        "The last resort, moving a gold the player can't reach, was never needed.", "",
        "`tools/check_levels.py` plays every level of `level.c` to the exit with",
        "the game's own player.c and level.c built for the host",
        "(`tools/hostcheck.c`), frame by frame, along the route search's moves:",
        "150 of 150 are won (on the released levels: only level 24).", "",
        "**The guards** were fixed separately, with the same rule in every version",
        "(`game.h`, `enemy.c`, `level.c`). The original's guards step a tile every",
        "3rd frame (the player takes 5 across and 7 on ladders) and keep stolen gold",
        "until trapped. Now:",
        "- **Speed:** a tile every 8 frames across and 10 up or down.",
        "- **Start:** every guard waits 3 s at the start of a level, and one placed",
        "  on the player's start row enters from the top instead.",
        "- **Stolen gold:** a guard drops it again after 4 s, on the first free cell",
        "  with a floor.", "",
        "The ZX Spectrum port's bot (`tools/try_levels.py` there, the same rules in",
        "C) plays the 150 repaired levels:",
        "- **The original's guards:** 3 cleared, 139 caught, 8 out of time.",
        "- **The fixed guards:** 66 cleared, 56 caught, 28 out of time (62-74",
        "  cleared across runs).",
        "- **No guards:** 124 cleared, the bot's own limit.", "",
        "Cells are (column, row), row 0 at the top; `.` space, `#` brick, `@`",
        "solid, `H` ladder, `X` hidden ladder, `-` bar, `$` gold, `P` the start,",
        "`E` a guard.", ""]
    new_head = list(report)
    total = 0
    with multiprocessing.Pool() as pool:
        done = pool.map(repair_one, levels)
    for lv, (g, (g2, log)) in enumerate(zip(levels, done), 1):
        order, brk = bd.plan(g2)
        d = diff(g, g2)
        total += len(d)
        out.append(g2)
        status = "winnable" if order is not None else f"NOT WINNABLE ({brk[0]})"
        report.append(f"## Level {lv}: {status}, {len(d)} cell{'s' if len(d) != 1 else ''} changed")
        report.append("")
        if not log:
            report.append("- unchanged")
        for line in log:
            report.append(f"- {line}")
        if d:
            report.append("- cells: " + ", ".join(f"({x},{y}) {a}->{b}" for x, y, a, b in d))
        report.append("")
        print(f"level {lv:3d}: {status:16s} {len(d):3d} cells  " + "; ".join(log)[:110])
    (HERE / "levels_repaired.txt").write_text(bd.to_text(out, "Bullion Dash: the 150 levels, repaired "
                                                              "(tools/repair_levels.py, REPAIRED_LEVELS.md)"))
    bd.write_level_c(src, out)
    bad = [i + 1 for i, g in enumerate(out) if bd.plan(g)[0] is None]
    report.insert(len(new_head), f"{150 - len(bad)} of 150 levels winnable; {total} cells changed in all"
                     f" ({total / 150:.1f} a level).\n")
    (HERE / "REPAIRED_LEVELS.md").write_text("\n".join(report) + "\n")
    print(f"{150 - len(bad)} winnable, {total} cells changed; not: {bad}")


if __name__ == "__main__":
    main()
