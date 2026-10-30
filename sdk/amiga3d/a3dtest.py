"""Tests for the amiga3d games on the 3DO (sdk/amiga3d). A game's test.py:

    import sys; from pathlib import Path
    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "sdk" / "amiga3d"))
    from a3dtest import A3Test
    t = A3Test("block_breaker_3d")
    t.selftest()                    # run_tests on the 3DO: the trace hash too
    t.boot(); ...; t.finish()

selftest() builds the A3D_TEST=1 disc (build/selftest/<name>.iso), runs
it until TDO:DONE and checks every test passed, the long autopilot trace
hash (tests/vectors.h) among them. The rest is PortTest (tdo.porttest)
plus: st() - the game's status line (a3_game_status) as a dict, fps() -
the frame rate from the "A3D: fps=" lines, keep() - a screenshot copied
to the project's screenshots/.
"""
from __future__ import annotations

import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "tdo" / "src"))
from tdo.porttest import PortTest  # noqa: E402


class A3Test(PortTest):
    def selftest(self, hash_test: str = "vectors.h", max_frames: int = 400000) -> bool:
        d = self.dir
        r = subprocess.run([str(ROOT / "bin" / "3do-make"), "-C", str(d), "A3D_TEST=1", "BUILD=build/selftest"],
                           capture_output=True, text=True)
        if not self.check("the test disc builds (A3D_TEST=1)", r.returncode == 0, (r.stdout + r.stderr)[-800:]):
            return False
        emu = self.emu                 # one emulator a process: the test disc, then the game's again
        emu.reboot(d / "build" / "selftest" / f"{self.name}.iso")
        c = emu.log_cursor()
        f0 = emu.frame_count
        done = emu.run_until_log("TDO:DONE", max_frames, since=c)
        log = emu.debug_log[c:]
        self.selftest_log = log
        frames = emu.frame_count - f0
        emu.reboot(self.iso)
        self._boot_at = emu.log_cursor()
        fails = [l for l in log if "TDO:FAIL" in l]
        passes = [l for l in log if "TDO:PASS" in l]
        took = next((l for l in log if "A3D: tests took" in l), "")
        print(f"  INFO {len(passes)} tests on the 3DO, {frames} frames; {took.strip()}")
        self.check("the game's tests run on the 3DO", done, "\n".join(log[-10:]))
        self.check(f"all {len(passes) + len(fails)} pass", done and not fails, "\n".join(fails[:10]))
        hp = [l for l in passes if hash_test in l]
        return self.check(f"the long autopilot trace hash matches ({hash_test})", bool(hp),
                          "\n".join(fails[:5]))

    _boot_at = 0
    selftest_log: list = []

    def host_trace(self, sources: list, prefix: str = "TRACE ") -> bool:
        """Build sources (the game's trace program) natively and compare its
        output with the selftest's lines that start with prefix."""
        import shutil as _sh
        cc = _sh.which("cc") or _sh.which("gcc")
        if not cc:
            print("  INFO no host C compiler: the trace comparison is skipped")
            return True
        out = self.dir / "build" / "host_trace"
        inc = ["-iquote", str(self.dir / "src"), "-iquote", str(ROOT / "sdk" / "amiga3d" / "include")]
        r = subprocess.run([cc, "-O2", "-w", *inc, "-o", str(out), "-x", "c", *[str(self.dir / x) for x in sources], "-x", "none",
                            str(ROOT / "sdk/amiga3d/src/a3d_math.c"), str(ROOT / "sdk/amiga3d/src/a3d_fix.c")],
                           capture_output=True, text=True)
        if not self.check("the host trace builds", r.returncode == 0, r.stderr[-600:]):
            return False
        host = subprocess.run([str(out)], capture_output=True, text=True).stdout.strip().splitlines()
        tdo = [l.split(prefix, 1)[1].strip() for l in self.selftest_log if prefix in l]
        diff = next((f"host: {a}\n 3DO: {b}" for a, b in zip(host, tdo) if a.strip() != b), "")
        print(f"  INFO trace: {len(host)} lines on the host, {len(tdo)} on the 3DO; last: {host[-1] if host else ''}")
        return self.check("the autopilot flight traces the same on the 3DO as on the host",
                          bool(host) and len(host) == len(tdo) and not diff, diff)

    def boot(self, max_frames: int = 1500):
        ok = self.emu.run_until_log("AMIGA3DO: ready", max_frames, since=self._boot_at)
        self.check("boots (AMIGA3DO: ready)", ok, self.log_tail())
        self.emu.step(30)   # let the main loop start: presses need an edge
        return ok

    def wait_ready(self, max_frames: int = 900):
        return self.check("the game is ready",
                          self.emu.run_until_log("A3D: ready", max_frames, since=self._boot_at), self.log_tail())

    def st(self, since: int | None = None) -> dict:
        """the last status line (since a log cursor) as a dict"""
        lines = self.emu.debug_log[since or 0:]
        for l in reversed(lines):
            if "A3D: st " in l:
                return dict(re.findall(r"(\w+)=(-?\w+)", l.split("A3D: st ", 1)[1]))
        return {}

    def sts(self, since: int) -> list[dict]:
        return [dict(re.findall(r"(\w+)=(-?\w+)", l.split("A3D: st ", 1)[1]))
                for l in self.emu.debug_log[since:] if "A3D: st " in l]

    def fps(self, frames: int = 700) -> float:
        """frames drawn a second over the next stretch of play (A3D: fps= lines)"""
        c = self.emu.log_cursor()
        self.emu.step(frames)
        vals = [float(m.group(1)) for l in self.emu.debug_log[c:] for m in [re.search(r"A3D: fps=([\d.]+)", l)] if m]
        fps = sum(vals) / len(vals) if vals else 0.0
        print(f"  INFO drawing {fps:.1f} fps ({vals})")
        return fps

    def keep(self, label: str, min_colours: int = 8):
        img = self.shot(label, min_colours)
        out = self.dir / "screenshots"
        out.mkdir(exist_ok=True)
        shutil.copy(self.dir / "build" / f"test-{label}.png", out / f"{label}.png")
        return img
