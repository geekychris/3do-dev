"""Rolling Steel test (./3do test rolling_steel).

1. The marble physics on the host (tools/sim.c): the Unity game's own demo
   driver must reach the goal of all six courses, as its `make verify` does.
2. The 3DO: title, a start, the pad pushing the marble down course 1, speed.
3. The attract demo: left alone on the title, the autopilot plays course 1
   to the finish pad.
4. Two players on two pads, split screen.
"""
import re
import shutil
import subprocess
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "tdo" / "src"))
from tdo.porttest import PortTest

D = Path(__file__).resolve().parent
t = PortTest("rolling_steel")

cc = shutil.which("cc") or shutil.which("gcc")
if cc:
    (D / "build").mkdir(exist_ok=True)
    r = subprocess.run([cc, "-std=c89", "-DRS_HOST", "-Isrc", "-o", "build/sim", "tools/sim.c", "src/phys.c", "src/course.c"],
                       cwd=D, capture_output=True, text=True)
    if r.returncode == 0:
        r = subprocess.run(["build/sim"], cwd=D, capture_output=True, text=True)
    t.check("the autopilot finishes all six courses (host)", r.returncode == 0 and r.stdout.count("GOAL") == 6,
            (r.stdout + r.stderr)[-900:])
else:
    print("  INFO no host C compiler: physics check skipped")


def expect(since, text, frames, label):
    return t.check(label, t.emu.run_until_log(text, frames, since=since), t.log_tail())


t.boot()
t.wait_log("ROLL: ready courses=6", 900, "ready")
t.play(frames=120)
t.shot("title", min_colours=24)
c = t.emu.log_cursor()
t.start(["A"], expect="state=1 ")
expect(c, "ROLL: level 1 PRACTICE", 60, "course 1 loads")
t.play(frames=30)
c = t.emu.log_cursor()
t.play(frames=330, hold=["UP"])
t.emu.run_until_log("ROLL: run course=1", 200, since=c)
prog = [int(m.group(1)) for l in t.emu.debug_log[c:] for m in [re.search(r"run course=1 t=\d+ progress=(\d+)", l)] if m]
t.check("the pad rolls the marble down the course", bool(prog) and max(prog) >= 10, f"progress {prog}")
t.shot("playing", min_colours=24)
t.check_game_speed(min_steps=45)
t.report_fps()
t.quit()
t.emu.close()
failed = list(t.failures)

# the attract demo plays course 1 to the end
t = PortTest("rolling_steel")
t.boot()
t.wait_log("ROLL: ready courses=6", 900, "ready")
t.wait_log("ROLL: demo starts course=1", 1800, "the demo starts after 25 s on the title")
c = t.emu.log_cursor()
t.play(frames=600)
t.shot("demo", min_colours=24)
expect(c, "ROLL: goal p1 course=1", 1500, "the autopilot reaches the finish pad")
t.emu.close()
failed += t.failures

# two players, two pads
t = PortTest("rolling_steel")
t.emu.set_device(1, "joypad")
t.boot()
t.wait_log("ROLL: ready courses=6", 900, "ready")
t.play(frames=60)
t.press(["RIGHT"])
t.play(frames=20)
t.start(["A"], expect="state=1 ")
t.emu.set_buttons(["UP"], 1)
t.play(frames=150, hold=["UP"])
t.emu.set_buttons([], 1)
t.shot("twoplayer", min_colours=24)
t.failures = failed + t.failures
t.finish()
