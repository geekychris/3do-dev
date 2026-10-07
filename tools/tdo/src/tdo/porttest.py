"""Standard headless test for games ported with the sdk/amiga layer.

A game's projects/<name>/test.py is usually just:

    import sys; sys.path.insert(0, __import__("pathlib").Path(__file__).resolve().parents[2].joinpath("tools/tdo/src").as_posix())
    from tdo.porttest import PortTest
    t = PortTest("rock_blaster")
    t.boot()                                   # AMIGA3DO: ready
    t.shot("title")
    t.start(["P"], expect="state=PLAYING")     # press P, expect a log line
    t.play(frames=300, hold=["A", "LEFT"])     # hold buttons for a while
    t.shot("playing", min_colours=3)
    t.check_speed(min_fps=45)                  # from AMIGA3DO: frame=N lines
    t.quit()                                   # X -> AMIGA3DO: exit
    t.finish()                                 # prints PASS/FAIL, exits 0/1

Screenshots go to projects/<name>/build/test-<label>.png.
"""
from __future__ import annotations

import sys
from pathlib import Path

from .core import Emulator, repo_root


class PortTest:
    def __init__(self, name: str, iso: str | None = None):
        self.name = name
        self.dir = repo_root() / "projects" / name
        self.iso = Path(iso) if iso else self.dir / "build" / f"{name}.iso"
        self.failures: list[str] = []
        self.emu = Emulator()
        self.emu.load(self.iso)

    # ------------------------------------------------------------ reporting
    def check(self, label: str, ok: bool, detail: str = "") -> bool:
        print(f"  {'PASS' if ok else 'FAIL'} {label}" + (f"  ({detail})" if detail and not ok else ""))
        if not ok:
            self.failures.append(label)
        return ok

    def log_tail(self, n: int = 15) -> str:
        return "\n".join(self.emu.debug_log[-n:])

    # ------------------------------------------------------------ steps
    def boot(self, max_frames: int = 1500):
        ok = self.emu.run_until_log("AMIGA3DO: ready", max_frames, since=0)
        self.check("boots (AMIGA3DO: ready)", ok, self.log_tail())
        self.emu.step(30)   # let the main loop start: presses need an edge
        return ok

    def wait_log(self, text: str, max_frames: int = 600, label: str | None = None) -> bool:
        return self.check(label or f"log '{text}'", self.emu.run_until_log(text, max_frames))

    def press(self, buttons, hold: int = 6, release: int = 6):
        self.emu.press(list(buttons) if not isinstance(buttons, str) else [buttons], hold, release)

    def start(self, buttons=("P",), expect: str | None = None, max_frames: int = 300):
        c = self.emu.log_cursor()
        self.press(buttons)
        if expect:
            self.check(f"start with {'+'.join(buttons)} -> '{expect}'",
                       self.emu.run_until_log(expect, max_frames, since=c), self.log_tail())

    def play(self, frames: int = 300, hold=(), port: int = 0):
        self.emu.set_buttons(list(hold), port)
        self.emu.step(frames)
        self.emu.set_buttons([], port)

    def shot(self, label: str, min_colours: int = 2):
        out = self.dir / "build" / f"test-{label}.png"
        img = self.emu.screenshot(out, scale=2)
        colours = len(img.getcolors(1 << 16) or [])
        self.check(f"screen '{label}' has content ({colours} colours)", colours >= min_colours)
        return img

    def check_speed(self, min_fps: float = 45.0, max_wait: int = 1800):
        """Measure frames/s from two consecutive 'AMIGA3DO: frame=' lines."""
        c = self.emu.log_cursor()
        if not self.emu.run_until_log("AMIGA3DO: frame=", max_wait, since=c):
            return self.check("speed measured", False, "no AMIGA3DO: frame= line")
        f0 = self.emu.frame_count
        c = self.emu.log_cursor()
        if not self.emu.run_until_log("AMIGA3DO: frame=", max_wait, since=c):
            return self.check("speed measured", False, "only one frame= line")
        vbls = self.emu.frame_count - f0
        fps = 250 * self.emu.fps / max(vbls, 1)
        return self.check(f"speed {fps:.1f} fps (>= {min_fps})", fps >= min_fps)

    def quit(self, max_frames: int = 300):
        c = self.emu.log_cursor()
        self.press("X")
        self.check("X quits (AMIGA3DO: exit)",
                   self.emu.run_until_log("AMIGA3DO: exit", max_frames, since=c), self.log_tail())

    def finish(self):
        self.emu.close()
        print(f"{'FAIL' if self.failures else 'PASS'}: {self.name} ({len(self.failures)} failure(s))")
        sys.exit(1 if self.failures else 0)
