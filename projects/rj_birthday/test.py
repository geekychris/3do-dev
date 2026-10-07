"""RJ Birthday port test (./3do test rj_birthday)."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "tdo" / "src"))
from tdo.porttest import PortTest

t = PortTest("rj_birthday")
t.boot()
t.play(frames=60)
t.check("party music loads from disc", any("party.mod" in l or "music" in l.lower() for l in t.emu.debug_log), t.log_tail())
t.shot("title", min_colours=5)
t.start(["A"], expect="state=1 ")            # GS_PLAYING
for d in ["RIGHT", "RIGHT", "DOWN", "LEFT", "UP", "RIGHT"]:
    t.play(frames=40, hold=[d])
    t.press(["A"])
t.shot("party", min_colours=6)
t.check_game_speed(min_steps=45)
t.report_fps()
for _ in range(3):                           # Esc chain: game -> credits -> title -> quit
    c = t.emu.log_cursor()
    t.press(["X"])
    if t.emu.run_until_log("AMIGA3DO: exit", 100, since=c):
        break
t.check("X leaves the game", any("AMIGA3DO: exit" in l for l in t.emu.debug_log), t.log_tail())
t.emu.close()
print(f"{'FAIL' if t.failures else 'PASS'}: rj_birthday ({len(t.failures)} failure(s))")
sys.exit(1 if t.failures else 0)
