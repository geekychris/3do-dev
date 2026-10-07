"""StakAttack port test (./3do test stakattack)."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "tdo" / "src"))
from tdo.porttest import PortTest

t = PortTest("stakattack")
t.boot()
t.check("music loads from disc", any(l.startswith("STAK: loaded stakattack.mod") for l in t.emu.debug_log), t.log_tail())
t.shot("title", min_colours=4)
t.start(["A"], expect="state=PLAYING")
t.play(frames=60, hold=["LEFT"])
t.shot("playing", min_colours=4)
t.check_speed(min_fps=45)
c = t.emu.log_cursor()
for i in range(40):                       # hard-drop pieces until the stack tops out
    t.press(["B"])
    if t.emu.run_until_log("state=GAMEOVER", 20, since=c):
        break
t.check("pieces drop, lock and stack up to game over",
        any("state=GAMEOVER" in l for l in t.emu.debug_log[c:]), t.log_tail())
t.shot("gameover", min_colours=4)
t.quit()
t.finish()
