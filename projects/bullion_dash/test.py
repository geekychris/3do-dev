"""Bullion Dash port test (./3do test bullion_dash)."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "tdo" / "src"))
import struct
from tdo.porttest import PortTest
from tdo.symbols import Program, Symbols

t = PortTest("bullion_dash")
t.boot()
t.shot("title", min_colours=5)
t.start(["A"], expect="state=PLAYING")
sym = Symbols(Program.from_iso(t.iso))
gs = sym.address("gs", t.emu)
home = t.emu.read_mem(sym.address("gold_home", t.emu), 28 * 16)
tiles = t.emu.read_mem(gs + 28 + 36 + 44 * 5 + 4, 28 * 16)     # GameState.tiles[28][16]
t.check("a normal game marks where gold lay (guards can drop stolen gold)",
        sum(home) > 0 and all(h == (v == 5) for h, v in zip(home, tiles)),
        "gold_home=%d cells" % sum(home))
c = t.emu.log_cursor()
t.emu.write_mem(gs + 28 + 20, struct.pack(">i", 6))            # GameState.player.state = PS_DEAD (a regrown brick)
t.play(frames=90)
lines = t.emu.debug_log[c:]
t.check("a buried (PS_DEAD) player loses the life instead of freezing",
        any("Player died at" in l for l in lines)
        and any("state=PLAYING" in l and "lives=4" in l for l in lines), t.log_tail())
c = t.emu.log_cursor()
t.play(frames=10, hold=["L", "R"])                             # give up the life
t.play(frames=60)
lines = t.emu.debug_log[c:]
t.check("L+R gives up the life and restarts the level",
        any("Gave up level" in l for l in lines)
        and any("state=PLAYING" in l and "lives=" in l for l in lines), t.log_tail())
c = t.emu.log_cursor()
for d in ["LEFT", "RIGHT", "RIGHT", "LEFT", "UP", "LEFT"]:     # run around level 1
    t.play(frames=50, hold=[d])
t.play(frames=10, hold=["B"])                                  # dig left
t.play(frames=10, hold=["C"])                                  # dig right
t.shot("playing", min_colours=6)
t.play(frames=1200)                                            # the guards start after 3 s, slower
lines = t.emu.debug_log[c:]
t.check("the runner collects gold or meets a guard",
        any(l.startswith("BULLION: score=") and "gold=0/" not in l for l in lines)
        or any("state=DYING" in l for l in lines), t.log_tail())
t.check_speed(min_fps=45)
t.quit()
t.finish()
