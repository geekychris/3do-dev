"""Smoke test for __NAME__ (run by `./3do test __NAME__`).

Boots the ISO headless, waits for the ready line, presses A, checks the log
and that something was drawn. Extend it as the program grows.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "tdo" / "src"))

from tdo.core import Emulator  # noqa: E402

ISO = Path(__file__).resolve().parent / "build" / "__NAME__.iso"
failures = []


def check(name, ok):
    print(f"  {'PASS' if ok else 'FAIL'} {name}")
    if not ok:
        failures.append(name)


with Emulator() as emu:
    emu.load(ISO)
    check("boots to ready", emu.run_until_log("__NAME__: ready", 1500))
    emu.step(30)  # let the main loop start: DoControlPad only reports new presses
    c = emu.log_cursor()
    emu.press("A")
    check("A press is logged", emu.run_until_log("__NAME__: A pressed", 60, since=c))
    img = emu.screenshot(ISO.parent / "test.png")
    check("screen has content", len(img.getcolors(1 << 16) or []) > 2)
    if failures:
        print("\n".join(emu.debug_log[-15:]))

print(f"{'FAIL' if failures else 'PASS'}: __NAME__")
sys.exit(1 if failures else 0)
