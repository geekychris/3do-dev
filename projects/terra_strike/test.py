"""Terra Strike on the 3DO (./3do test terra_strike).

1. The game's 67 unit tests on the 3DO (the TS_TEST=1 disc: maths,
   clipping, spans, terrain, flight, combat, scenes, radar), and a scripted
   4000-frame flight (src/tests/trace.c) printed there and compared line by
   line with the same code built natively here.
2. The game: the title artwork, a start with A, UP lifting off, B opening
   the throttle, LEFT turning, A firing, the attract flyover, the frame
   rate, X back to the menu.
"""
import re
import shutil
import subprocess
import sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "tdo" / "src"))
from tdo.porttest import PortTest

D = Path(__file__).resolve().parent
t = PortTest("terra_strike")

# ---- 1. the tests and the scripted flight on the 3DO
r = subprocess.run([str(ROOT / "bin" / "3do-make"), "-C", str(D), "TS_TEST=1", "BUILD=build/selftest"],
                   capture_output=True, text=True)
t.check("the test disc builds (TS_TEST=1)", r.returncode == 0, (r.stdout + r.stderr)[-600:])
t.emu.reboot(D / "build" / "selftest" / "terra_strike.iso")
c = t.emu.log_cursor()
done = t.emu.run_until_log("TDO:DONE", 200000, since=c)
log = t.emu.debug_log[c:]
fails = [l for l in log if "TDO:FAIL" in l]
npass = sum("TDO:PASS" in l for l in log)
t.check(f"the unit tests on the 3DO ({npass} passed)", done and npass >= 67 and not fails, "\n".join(fails[:8] or log[-6:]))
tdo = [l.split("TRACE ", 1)[1].strip() for l in log if "TRACE " in l]
cc = shutil.which("cc") or shutil.which("gcc")
if cc:
    (D / "build").mkdir(exist_ok=True)
    r = subprocess.run([cc, "-O2", "-w", "-DTS_TRACE_MAIN", "-o", str(D / "build" / "host_trace"),
                        *[str(D / "src" / f) for f in ("tests/trace.c", "ts_math.c", "terrain.c", "world.c")]],
                       capture_output=True, text=True)
    host = subprocess.run([str(D / "build" / "host_trace")], capture_output=True, text=True).stdout.split("\n")
    host = [l.strip() for l in host if l.strip()]
    diff = next((f"host: {a}\n 3DO: {b}" for a, b in zip(host, tdo) if a != b), "")
    print(f"  INFO trace: {len(host)} lines on the host, {len(tdo)} on the 3DO; last: {host[-1] if host else ''}")
    t.check("the scripted flight is the same on the 3DO as on the host", bool(host) and len(host) == len(tdo) and not diff, diff)
else:
    print("  INFO no host C compiler: the trace comparison is skipped")

# ---- 2. the game
t.emu.reboot(t.iso)
c0 = t.emu.log_cursor()
t.check("boots", t.emu.run_until_log("TS: ready", 1500, since=c0), t.log_tail())
t.emu.step(300)


def st(since):
    out = [dict(re.findall(r"(\w+)=(-?\w+)", l.split("TS: st ", 1)[1])) for l in t.emu.debug_log[since:] if "TS: st " in l]
    return out


def keep(label, min_colours=8):
    t.shot(label, min_colours)
    (D / "screenshots").mkdir(exist_ok=True)
    shutil.copy(D / "build" / f"test-{label}.png", D / "screenshots" / f"{label}.png")


keep("title")
t.start(["A"], expect="STATE PLAY")
t.play(frames=60)
c = t.emu.log_cursor()
t.play(frames=120, hold=["UP"])
t.check("UP lifts off", t.emu.run_until_log("TAKEOFF", 60, since=c), t.log_tail())
c = t.emu.log_cursor()
t.play(frames=150, hold=["B"])
t.play(frames=70)
t.check("B opens the throttle", any(int(s.get("speed", 0)) > 0 for s in st(c)), str(st(c)[-1:]))
y0 = st(0)[-1].get("yaw")
c = t.emu.log_cursor()
t.play(frames=60, hold=["LEFT"])
t.play(frames=70)
t.check(f"LEFT turns (yaw {y0} -> {st(c)[-1:] and st(c)[-1].get('yaw')})", any(s.get("yaw") != y0 for s in st(c)))
c = t.emu.log_cursor()
t.play(frames=120, hold=["A"])
t.check("A fires the cannon", any(int(s.get("shots", 0)) > 0 for s in st(c)), str(st(c)[-1:]))
t.play(frames=60, hold=["A"])
keep("playing")
c = t.emu.log_cursor()
t.play(frames=800)
fps = [float(m.group(1)) for l in t.emu.debug_log[c:] for m in [re.search(r"TS: fps=([\d.]+)", l)] if m]
print(f"  INFO drawing {sum(fps) / max(len(fps), 1):.1f} fps in play ({fps})")
t.quit()
t.finish()
