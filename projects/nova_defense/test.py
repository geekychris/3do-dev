"""Nova Defense port test (./3do test nova_defense)."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "tdo" / "src"))
from tdo.porttest import PortTest

t = PortTest("nova_defense")
t.boot()
t.shot("title", min_colours=4)
t.start(["A"], expect="state=PLAYING")
# The gun's explosion sound plays once per death (the original restarted it
# every frame of the 60-frame dying state). Sweep out from behind the shields
# until an alien bullet hits the gun.
died = False
for _ in range(40):
    t.play(frames=37, hold=["LEFT"])
    t.play(frames=37, hold=["RIGHT"])
    t.play(frames=10)
    if any(l.startswith("NOVA: state=DYING") for l in t.emu.debug_log):
        died = True
        break
t.play(frames=90)                   # let the dying state finish
booms = [l for l in t.emu.debug_log if "sfx=player_explode" in l]
deaths = [l for l in t.emu.debug_log if l.startswith("NOVA: state=DYING")]
t.check("the gun was hit", died, t.log_tail())
t.check("one explosion sound per death", died and len(booms) == len(deaths),
        "booms=%d deaths=%d" % (len(booms), len(deaths)))
for _ in range(25):                 # fire is edge-triggered: tap A while drifting left
    t.play(frames=6, hold=["A", "LEFT"])
    t.play(frames=6, hold=["LEFT"])
t.check("shooting scores points", any(l.startswith("NOVA: score=") for l in t.emu.debug_log), t.log_tail())
t.shot("playing", min_colours=4)
t.check_speed(min_fps=45)
t.quit()
t.finish()
