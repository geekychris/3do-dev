"""Spectral Keep test (./3do test spectral_keep).

First the game logic on the host (tools/sim.c: lifts, crates, gates and
keys, the throne and the next keep, the monsters, spikes, game over), then
the real thing in the emulator, played with the pad: talk to the sage, walk
through the east door, climb the courtyard's block stair to its relic, then
step into the guard's path.
"""
import shutil
import subprocess
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "tdo" / "src"))
from tdo.porttest import PortTest


def walk(t, dirs, frames, jump_every=0):
    """Hold a direction; with jump_every, tap A that often."""
    if not jump_every:
        t.play(frames=frames, hold=dirs)
        return
    done = 0
    while done < frames:
        t.play(frames=3, hold=dirs + ["A"])
        t.play(frames=jump_every - 3, hold=dirs)
        done += jump_every


def expect(t, since, text, frames, label):
    """`text` logged since the cursor (or within `frames` more)."""
    return t.check(label, t.emu.run_until_log(text, frames, since=since), t.log_tail())


t = PortTest("spectral_keep")
# the game logic on the host, against the shipped levels (tools/sim.c)
cc = shutil.which("cc") or shutil.which("gcc")
if cc:
    d = Path(__file__).resolve().parent
    (d / "build").mkdir(exist_ok=True)
    src = ["tools/sim.c"] + [f"src/{f}.c" for f in ("world", "room", "game", "levels")]
    r = subprocess.run([cc, "-std=c89", "-Isrc", "-o", "build/sim"] + src, cwd=d, capture_output=True, text=True)
    if r.returncode == 0:
        r = subprocess.run(["build/sim"], cwd=d, capture_output=True, text=True)
    t.check("game logic scenarios (tools/sim.c)", r.returncode == 0, (r.stdout + r.stderr)[-1500:])
else:
    print("  INFO no host C compiler: logic scenarios skipped")
t.boot()
t.wait_log("KEEP: ready keeps=3", 900, "sprites drawn")
t.play(frames=90)
t.shot("title", min_colours=12)
c = t.emu.log_cursor()
t.start(["A"], expect="state=1 ")
expect(t, c, "KEEP: room gate visited=1/6", 60, "the gate is the first room")
t.play(frames=30)
# UP+LEFT on the pad is world +z (screen-relative controls), DOWN+LEFT is -x
c = t.emu.log_cursor()
walk(t, ["UP", "LEFT"], 42)
walk(t, ["DOWN", "LEFT"], 90)
expect(t, c, "KEEP: talk gate", 60, "the sage talks")
c = t.emu.log_cursor()
walk(t, ["UP", "RIGHT"], 160)
expect(t, c, "KEEP: room yard visited=2/6", 60, "east door to the courtyard")
t.play(frames=20)
t.shot("yard", min_colours=12)
c = t.emu.log_cursor()
walk(t, ["UP", "LEFT"], 46)
walk(t, ["UP", "RIGHT"], 260, jump_every=24)
expect(t, c, "KEEP: pickup R yard", 60, "climbs the block stair to the relic")
t.shot("relic", min_colours=12)
c = t.emu.log_cursor()
walk(t, ["DOWN", "RIGHT"], 34)
expect(t, c, "KEEP: death A GUARD", 900, "the guard is deadly")
expect(t, c, "KEEP: state=1 ", 200, "respawn at the doorway")
t.check_game_speed(min_steps=45)
t.report_fps()
t.quit()
t.finish()
