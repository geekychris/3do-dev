"""An emulator session: one Opera instance + remote control + GDB + optional window.

    tdo serve GAME.iso [--window] [--port N] [--gdb-port N] [--mode paused|running]
    tdo run   GAME.iso                      (= serve --window --mode running)

Run modes
  paused   frames advance only on `step`/`press`/... commands (deterministic;
           default for headless sessions driven by agents)
  running  free-run at `speed` x real time (0 = as fast as possible)

Independently, the CPU can be *halted* by the debugger (breakpoint, watchpoint,
halt, single-step); then nothing advances until `continue`/`stepi`.

Each session registers itself in build/sessions/<pid>.json so tools can find
it (`tdo sessions`, MCP `emu_attach`).
"""
from __future__ import annotations

import json
import os
import sys
import time
from pathlib import Path

from .control import SESSION_DIR, Controller, SocketServer
from .core import Emulator, repo_root
from .kernel import Kernel
from .symbols import Program, Symbols, resolve_addr


def resolve_iso(target: str) -> Path:
    """Project name (projects/<name>) or ISO path -> ISO path."""
    p = Path(target).expanduser()
    if p.suffix.lower() in (".iso", ".bin", ".cue", ".chd"):
        if not p.is_absolute() and not p.exists():
            p = repo_root() / p
        if not p.exists():
            raise FileNotFoundError(p)
        return p.resolve()
    proj = repo_root() / "projects" / target
    isos = sorted((proj / "build").glob("*.iso"))
    if not isos:
        raise FileNotFoundError(f"no ISO in {proj}/build – build it first (./3do build {target})")
    return isos[0].resolve()


class Session:
    def __init__(self, iso: str, bios: str = "panafz10.bin", options: dict | None = None,
                 window: bool = False, mode: str | None = None, speed: float = 1.0,
                 port: int = 0, gdb_port: int | None = 0, scale: int = 3, audio: bool = True,
                 verbose_log: bool = False):
        self.iso = resolve_iso(iso)
        self.emu = Emulator(bios=bios, options=options or {})
        self.emu.load(self.iso)
        self.mode = mode or ("running" if window else "paused")
        self.speed = speed
        self.holds: dict[int, list[str]] = {0: [], 1: []}
        self.keyboard: list[str] = []
        self.fast_forward = False
        self.breakpoints: set[int] = set()
        self.watchpoints: set[tuple] = set()
        self.events: list[str] = []
        self.quit = False
        self.verbose_log = verbose_log
        self._load_symbols()
        self.kernel = Kernel(self.emu)
        self.ctl = Controller(self)
        self.server = SocketServer(self.ctl, port)
        self.control_port = self.server.port
        self.gdb = None
        if gdb_port is not None:
            from .gdbstub import GdbStub
            self.gdb = GdbStub(self, gdb_port)
        self.window = None
        if window:
            from .window import Window
            self.window = Window(self, scale=scale, audio=audio)
        self._register()

    # ------------------------------------------------------------ helpers used by Controller
    @property
    def held(self):
        return {p: b for p, b in self.holds.items() if b}

    def held_on(self, port: int) -> list[str]:
        return list(self.holds.get(int(port), [])) + (self.keyboard if int(port) == 0 else [])

    def apply_input(self):
        for port in (0, 1):
            self.emu.set_buttons(sorted(set(self.held_on(port))), port)

    def note(self, msg: str):
        stamp = f"[frame {self.emu.frame_count}] {msg}"
        self.events.append(stamp)
        del self.events[:-1000]
        print(f"[tdo] {stamp}", file=sys.stderr)

    def _load_symbols(self):
        prog = Program.from_iso(self.iso)
        self.symbols = Symbols(prog) if prog.sym else None

    def load_base(self) -> int | None:
        return self.symbols.find_base(self.emu) if self.symbols else None

    def symbolize(self, addr: int) -> str | None:
        if not self.symbols or self.load_base() is None:
            return None
        return self.symbols.symbolize(addr)

    def resolve(self, spec) -> int:
        if spec == "pc":
            return self.emu.regs()["pc"]
        if self.symbols:
            self.load_base()
        return resolve_addr(spec, self.symbols, self.emu)

    def settle_halt(self):
        """After requesting a halt, run until the CPU actually stops (<= 1 instruction)."""
        for _ in range(3):
            if self.emu.halted:
                return
            self.emu.step(1)

    def resume_for_debugger(self):
        if self.mode == "paused":
            self.mode = "running"
            self.note("gdb continue: session switched to running")

    def after_boot(self, keep_debug: bool = True):
        if self.symbols:
            self.symbols.base = None
        # waits (run_until) must only match output from the new boot
        self.ctl.log_cursor = len(self.emu.debug_log)
        if not keep_debug:
            self.breakpoints.clear()
            self.watchpoints.clear()

    def reboot(self, iso: str | None = None):
        if iso:
            self.iso = resolve_iso(iso)
        self.emu.reboot(self.iso)
        self._load_symbols()
        self.kernel = Kernel(self.emu)
        self.after_boot(keep_debug=False)
        self.note(f"rebooted {self.iso.name}")
        self._register()

    # ------------------------------------------------------------ registry
    def _reg_path(self) -> Path:
        return SESSION_DIR / f"{os.getpid()}.json"

    def info(self) -> dict:
        return {"pid": os.getpid(), "iso": str(self.iso), "control_port": self.control_port,
                "gdb_port": self.gdb.port if self.gdb else None,
                "window": self.window is not None, "started": getattr(self, "_started", time.time())}

    def _register(self):
        self._started = getattr(self, "_started", time.time())
        SESSION_DIR.mkdir(parents=True, exist_ok=True)
        self._reg_path().write_text(json.dumps(self.info()))

    # ------------------------------------------------------------ main loop
    def run(self):
        e = self.emu
        was_halted = False
        next_t = time.perf_counter()
        printed = 0
        try:
            while not self.quit:
                self.server.pump()
                if self.gdb:
                    self.gdb.poll()
                if self.window:
                    self.window.handle_events()
                    if self.quit:
                        break
                self.apply_input()

                ran = 0
                if self.mode == "running" and not e.halted:
                    batch = 8 if self.speed == 0 else (4 if self.fast_forward else 1)
                    ran = e.step(batch)

                if e.halted and not was_halted:
                    info = self.ctl.cmd_stop_info()
                    self.note(f"CPU stopped: {info['reason']} at {info['pc']} {info['where'] or ''}")
                was_halted = e.halted

                if self.verbose_log:
                    for line in e.debug_log[printed:]:
                        print(f"[3do] {line}", flush=True)
                    printed = len(e.debug_log)

                if self.window:
                    self.window.render()

                # pacing
                if ran and self.speed > 0:
                    next_t += (1 if self.fast_forward else ran) / (e.fps * self.speed)
                    delay = next_t - time.perf_counter()
                    if delay > 0:
                        time.sleep(delay)
                    elif delay < -0.25:
                        next_t = time.perf_counter()   # fell behind; don't spiral
                elif not ran:
                    next_t = time.perf_counter()
                    if not self.server.pending():
                        time.sleep(0.004 if self.window else 0.002)
        finally:
            self.close()

    def close(self):
        if getattr(self, "_closed", False):
            return
        self._closed = True
        try:
            self._reg_path().unlink(missing_ok=True)
        except OSError:
            pass
        if self.gdb:
            self.gdb.close()
        self.server.close()
        if self.window:
            self.window.close()
        self.emu.close()


def serve(iso: str, print_ports: bool = False, **kw) -> int:
    s = Session(iso, **kw)
    banner = {"control_port": s.control_port, "gdb_port": s.gdb.port if s.gdb else None,
              "pid": os.getpid(), "iso": str(s.iso), "mode": s.mode}
    if print_ports:
        print("TDO_SESSION " + json.dumps(banner), flush=True)
    print(f"[tdo] session pid {os.getpid()}: control 127.0.0.1:{s.control_port}"
          + (f", gdb 127.0.0.1:{s.gdb.port}" if s.gdb else "") + f", mode {s.mode}",
          file=sys.stderr, flush=True)
    s.run()
    return 0
