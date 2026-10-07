"""Host-side test for the demo (run by `./3do test demo`).

Drives the emulator like a player would and checks behaviour through the
debug console ("DEMO:" lines) and the rendered frames.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "tdo" / "src"))

from tdo.core import Emulator  # noqa: E402

ISO = ROOT / "projects" / "demo" / "build" / "demo.iso"
OUT = ROOT / "projects" / "demo" / "build"
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{(' – ' + detail) if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def pixels_stats(img):
    import numpy as np
    a = np.asarray(img).astype(int)
    return a.std(), len(np.unique(a.reshape(-1, 3), axis=0))


with Emulator() as emu:
    emu.load(ISO)
    check("boots to 'DEMO: ready'", emu.run_until_log("DEMO: ready", 1500))
    ready = [l for l in emu.debug_log if l.startswith("DEMO: ready")]
    check("ready line reports 320x240", bool(ready) and "320x240" in ready[0], str(ready))
    check("no FATAL lines", not any("FATAL" in l for l in emu.debug_log))

    # Frame budget: 300 demo frames should take ~300 VBLs (60fps).
    c = emu.log_cursor()
    emu.run_until_log("DEMO: status", 1000, since=c)
    f0 = emu.frame_count
    c = emu.log_cursor()
    emu.run_until_log("DEMO: status", 1000, since=c)
    vbls = emu.frame_count - f0
    check("holds 60 fps (<=310 VBLs per 300 frames)", vbls <= 310, f"{vbls} VBLs")

    a = emu.screenshot()
    emu.step(30)
    b = emu.screenshot()
    std, colours = pixels_stats(a)
    check("frame is not blank", std > 20 and colours > 50, f"std={std:.1f} colours={colours}")
    import numpy as np
    diff = np.abs(np.asarray(a).astype(int) - np.asarray(b).astype(int)).mean()
    check("animation advances", diff > 5, f"mean diff {diff:.1f}")
    a.resize((640, 480)).save(OUT / "test-running.png")

    for button, expect in [("A", "palette=1"), ("B", "vortex=0"), ("C", "stars=0"),
                           ("B", "vortex=1"), ("P", "paused=1")]:
        c = emu.log_cursor()
        emu.press(button)
        check(f"{button} -> {expect}", emu.run_until_log(expect, 60, since=c))

    # Paused: frames should now be identical.
    p1 = emu.screenshot(); emu.step(20); p2 = emu.screenshot()
    still = np.abs(np.asarray(p1).astype(int) - np.asarray(p2).astype(int)).mean()
    check("pause freezes the picture", still < 0.5, f"mean diff {still:.2f}")
    emu.press("P")
    p2.resize((640, 480)).save(OUT / "test-paused.png")

    c = emu.log_cursor()
    emu.press("X")
    check("X quits cleanly", emu.run_until_log("DEMO: exit", 120, since=c))

print(f"{'FAIL' if failures else 'PASS'}: demo ({len(failures)} failure(s)); screenshots in {OUT}")
sys.exit(1 if failures else 0)
