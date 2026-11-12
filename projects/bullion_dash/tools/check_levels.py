"""Play Bullion Dash's levels to the exit with the game's own rules.

For each level of level.c (or a levels .txt), the route search
(tools/bdlevels.py) gives an order for the gold and the moves; they are
played frame by frame through player.c and level.c built for the host
(tools/hostcheck.c), as main.c runs them (the player, the bricks, the hidden
ladders with the last gold; won with all the gold on row 0). The guards are
left out (ORIGINAL_BUGS.md: their speed is a separate problem).

    python3 tools/check_levels.py [levels.txt] [-v] [levels...]
"""
import ctypes
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bdlevels as bd  # noqa: E402

HERE = Path(__file__).resolve().parents[1]
SRC = HERE / "src" if (HERE / "src" / "player.c").exists() else HERE    # (the ports keep it in src/)
SO = HERE / "tools" / "build" / "hostcheck.so"
KEYS = {"left": (-1, 0), "right": (1, 0), "up": (0, -1), "down": (0, 1), "fall": (0, 0)}


def build():
    SO.parent.mkdir(exist_ok=True)
    srcs = [HERE / "tools" / "hostcheck.c", SRC / "player.c", SRC / "level.c"]
    if not SO.exists() or any(s.stat().st_mtime > SO.stat().st_mtime for s in srcs):
        subprocess.run(["cc", "-O2", "-shared", "-fPIC", "-w", "-I", str(HERE / "tools" / "hostinc"),
                        "-I", str(SRC)] + [str(s) for s in srcs] + ["-o", str(SO)], check=True)
    return ctypes.CDLL(str(SO))


def play(lib, g, from_data=None):
    """True when the route wins; else the reason"""
    order, brk = bd.plan(g)
    if order is None:
        return f"no route ({brk[0]} at {brk[1]})"
    moves = bd.route(g, order)
    if from_data:
        lib.hc_load(from_data)                 # the level as the game loads it
    else:
        cols = bytes(g[y][x] for x in range(bd.W) for y in range(bd.H))
        lib.hc_custom(cols)
    for kind, (tx, ty) in moves:
        dx, dy = KEYS[kind]
        for _ in range(60):
            r = lib.hc_tick(dx, dy)
            if r == 1:
                return True
            if r == 2:
                return f"died going {kind} to {(tx, ty)}"
            if lib.hc_gx() == tx and lib.hc_gy() == ty and lib.hc_aligned():
                break
        else:
            return f"stuck going {kind} to {(tx, ty)} at {(lib.hc_gx(), lib.hc_gy())}"
    for _ in range(30):                        # (the last step up into row 0)
        if lib.hc_tick(0, -1) == 1:
            return True
    return f"route done, not won (gold {lib.hc_gold()}/{lib.hc_gold_total()})"


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    txt = next((a for a in args if a.endswith(".txt")), None)
    only = [int(a) for a in args if a.isdigit()]
    lib = build()
    if txt:
        levels = bd.from_text(Path(txt).read_text())
        data = False
    else:
        levels = bd.read_level_c(SRC / "level.c")
        data = True
    won = []
    for lv, g in enumerate(levels, 1):
        if only and lv not in only:
            continue
        r = play(lib, g, lv if data else None)
        if r is True:
            won.append(lv)
        if r is not True or "-v" in sys.argv:
            print(f"level {lv:3d}: {'won' if r is True else r}")
    n = len(only) if only else len(levels)
    print(f"{len(won)} of {n} levels won with the game's rules")
    sys.exit(0 if len(won) == n else 1)


if __name__ == "__main__":
    main()
