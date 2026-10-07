"""Remote-control command set for an emulator session, and its clients.

Transport is JSON lines over TCP (127.0.0.1):

    -> {"id": 1, "cmd": "press", "args": {"buttons": "A"}}
    <- {"id": 1, "ok": true, "result": {...}}      or {"id": 1, "ok": false, "error": "..."}

`help` lists every command with its arguments. The same commands are exposed
through the MCP server, `tdo ctl`, and gdb's `monitor`.
"""
from __future__ import annotations

import base64
import inspect
import io
import json
import os
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

from .core import BUTTONS, DEVICES, repo_root

STATE_DIR = repo_root() / "build" / "states"
SESSION_DIR = repo_root() / "build" / "sessions"


def _png_b64(img) -> str:
    buf = io.BytesIO()
    img.save(buf, format="PNG")
    return base64.b64encode(buf.getvalue()).decode()


def _hexdump(addr: int, data: bytes) -> str:
    lines = []
    for off in range(0, len(data), 16):
        chunk = data[off:off + 16]
        hexs = " ".join(f"{b:02x}" for b in chunk)
        asc = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        lines.append(f"{addr + off:08x}  {hexs:<47}  {asc}")
    return "\n".join(lines)


def _buttons(spec) -> list[str]:
    if spec is None or spec == "":
        return []
    if isinstance(spec, str):
        spec = [b.strip() for b in spec.replace("+", ",").split(",") if b.strip()]
    out = [b.upper() for b in spec]
    for b in out:
        if b not in BUTTONS:
            raise ValueError(f"unknown button {b!r}; valid: {sorted(BUTTONS)}")
    return out


def _h(v: int) -> str:
    return f"0x{v:08x}"


class Controller:
    """Executes commands against a Session. Call only from the session thread."""

    def __init__(self, session):
        self.s = session
        self.log_cursor = 0

    @property
    def emu(self):
        return self.s.emu

    def handle(self, cmd: str, args: dict | None = None):
        fn = getattr(self, f"cmd_{cmd}", None)
        if fn is None:
            raise ValueError(f"unknown command {cmd!r}; try 'help'")
        return fn(**(args or {}))

    @classmethod
    def describe(cls) -> list[dict]:
        """Machine-readable command catalogue (drives the REST API docs / OpenAPI)."""
        out = []
        for name in sorted(n for n in dir(cls) if n.startswith("cmd_")):
            fn = getattr(cls, name)
            params = []
            for p in list(inspect.signature(fn).parameters.values())[1:]:
                if p.annotation is inspect.Parameter.empty:
                    ann = "any"
                else:
                    ann = p.annotation if isinstance(p.annotation, str) else getattr(p.annotation, "__name__", "any")
                params.append({"name": p.name, "type": str(ann).replace("'", ""),
                               "required": p.default is inspect.Parameter.empty,
                               "default": None if p.default is inspect.Parameter.empty else p.default})
            doc = inspect.getdoc(fn) or ""
            out.append({"name": name[4:], "doc": doc, "summary": doc.split("\n")[0], "params": params,
                        "group": cls._group(name[4:])})
        return out

    @staticmethod
    def _group(n: str) -> str:
        if n.startswith("os_") or n in ("ps", "folios", "devices"):
            return "os"
        if n in ("press", "hold", "release", "sequence", "set_device", "analog", "mouse", "lightgun"):
            return "input"
        if n in ("screenshot", "record_gif", "log", "events", "crashes"):
            return "output"
        if n in ("regs", "set_reg", "halt", "continue", "stepi", "stop_info", "break", "delete", "watch",
                 "unwatch", "breakpoints", "tracepoint", "swi_trace", "swi_calls", "disasm", "symbols", "addr2sym", "profile"):
            return "debug"
        if n in ("read_mem", "read_u32", "write_mem", "write_u32", "find_mem"):
            return "memory"
        if n in ("save_state", "load_state"):
            return "states"
        return "session"

    def help_text(self) -> str:
        lines = []
        for name in sorted(n for n in dir(self) if n.startswith("cmd_")):
            fn = getattr(self, name)
            sig = str(inspect.signature(fn)).replace("'", "")
            doc = (inspect.getdoc(fn) or "").split("\n")[0]
            lines.append(f"{name[4:]}{sig}\n    {doc}")
        return "\n".join(lines)

    def _sym(self, addr):
        return self.s.resolve(addr)

    # ================================================================ session
    def cmd_help(self):
        """List all commands."""
        return {"commands": self.help_text()}

    def cmd_ping(self):
        """Liveness check."""
        return {"pong": True}

    def cmd_status(self):
        """Game, frame, run mode, halt state, input, ports, log size."""
        e = self.emu
        st = {
            "game": e.game_path, "frame": e.frame_count, "mode": self.s.mode,
            "speed": self.s.speed, "halted": e.halted, "fps": round(e.fps, 3),
            "held_buttons": self.s.held, "devices": [p.device for p in e.ports],
            "log_lines": len(e.debug_log), "bios": e.options.get("opera_bios"),
            "load_base": (_h(self.s.load_base()) if self.s.load_base() is not None else None),
            "symbols": len(self.s.symbols) if self.s.symbols else 0,
            "gdb_port": self.s.gdb.port if self.s.gdb else None,
            "control_port": self.s.control_port, "pid": os.getpid(),
            "window": self.s.window is not None,
        }
        if e.halted:
            info = e.stop_info()
            st["stop"] = {"reason": info["reason"], "pc": _h(info["pc"]),
                          "where": self.s.symbolize(info["pc"])}
        return st

    def cmd_run(self, speed: float = 1.0):
        """Free-run the emulator. speed: 1.0 = real time, 2.0 = double, 0 = as fast as possible."""
        self.s.mode = "running"
        self.s.speed = float(speed)
        if self.emu.halted:
            self.emu.cont()
        return {"mode": "running", "speed": self.s.speed}

    def cmd_pause(self):
        """Stop free-running between frames (the CPU is not halted; use step to advance)."""
        self.s.mode = "paused"
        return {"mode": "paused", "frame": self.emu.frame_count}

    def cmd_step(self, frames: int = 1):
        """Advance N frames (60 = one second of 3DO time). Stops early at a breakpoint."""
        e = self.emu
        self.s.apply_input()
        n = e.step(int(frames))
        res = {"frames_run": n, "frame": e.frame_count}
        if e.halted:
            res["stopped"] = self.cmd_stop_info()
        return res

    def cmd_run_until(self, text: str, max_frames: int = 1800, since: int | None = None):
        """Step until TEXT appears in debug output (after the last `log` call by default)."""
        e = self.emu
        self.s.apply_input()
        start = self.log_cursor if since is None else int(since)
        found = e.run_until_log(text, int(max_frames), since=start)
        res = {"found": found, "frame": e.frame_count}
        if e.halted:
            res["stopped"] = self.cmd_stop_info()
        return res

    def cmd_reset(self):
        """Soft reset (like the console's reset button): reboots the disc, keeps the core."""
        self.emu.reset()
        self.s.after_boot()
        return self.cmd_status()

    def cmd_reboot(self, iso: str | None = None):
        """Power-cycle: reinitialise the core and boot ISO (default: current disc)."""
        self.s.reboot(iso)
        return self.cmd_status()

    def cmd_load(self, iso: str):
        """Insert a different disc (ISO path or project name) and power-cycle."""
        return self.cmd_reboot(iso)

    def cmd_set_option(self, key: str, value: str):
        """Set an Opera core option (e.g. opera_cpu_overclock=2.0x, opera_region=pal1)."""
        self.emu.set_option(key, str(value))
        return {"key": key, "value": str(value), "note": "some options apply only at reboot"}

    def cmd_options(self):
        """Current core options."""
        return dict(self.emu.options)

    def cmd_quit(self):
        """Shut this session down."""
        self.s.quit = True
        return {"quitting": True}

    # ================================================================ input
    def cmd_press(self, buttons, hold_frames: int = 6, release_frames: int = 6, port: int = 0):
        """Press and release joypad buttons, e.g. "A", "UP+B". Buttons: A B C P X L R UP DOWN LEFT RIGHT."""
        e = self.emu
        b = _buttons(buttons)
        e.set_buttons(b + self.s.held_on(port), port)
        e.step(int(hold_frames))
        e.set_buttons(self.s.held_on(port), port)
        e.step(int(release_frames))
        return {"frame": e.frame_count}

    def cmd_hold(self, buttons="", port: int = 0):
        """Hold buttons until changed ("" releases). Held buttons persist across steps and free-run."""
        self.s.holds[int(port)] = _buttons(buttons)
        self.s.apply_input()
        return {"held": self.s.holds}

    def cmd_release(self, port: int | None = None):
        """Release all held buttons (one port or all)."""
        for p in ([int(port)] if port is not None else list(self.s.holds)):
            self.s.holds[p] = []
        self.s.apply_input()
        return {"held": self.s.holds}

    def cmd_sequence(self, steps: list):
        """Scripted input. steps: [{"buttons":"A","frames":6}, {"frames":30}, {"analog":{"lx":-32000},"frames":10}, {"mouse":{"dx":5}}, {"wait_log":"ready","max_frames":600}, {"screenshot":"x.png"}]."""
        e = self.emu
        out = []
        for st in steps:
            port = int(st.get("port", 0))
            if "device" in st:
                e.set_device(port, st["device"])
            if "analog" in st:
                e.set_analog(port, **st["analog"])
            if "mouse" in st:
                e.mouse(port, **st["mouse"])
            if "lightgun" in st:
                e.lightgun(port, **st["lightgun"])
            if "wait_log" in st:
                ok = e.run_until_log(st["wait_log"], int(st.get("max_frames", 1800)), since=self.log_cursor)
                out.append({"wait_log": st["wait_log"], "found": ok})
                continue
            if "screenshot" in st:
                e.screenshot(st["screenshot"], scale=int(st.get("scale", 1)))
                out.append({"screenshot": st["screenshot"]})
                continue
            e.set_buttons(_buttons(st.get("buttons")) + self.s.held_on(port), port)
            e.step(int(st.get("frames", 1)))
            e.set_buttons(self.s.held_on(port), port)
        return {"frame": e.frame_count, "results": out}

    def cmd_set_device(self, port: int = 0, device: str = "joypad"):
        """Plug a peripheral into port 0/1: joypad, flightstick, mouse, lightgun, arcade_lightgun, orbatak_trackball, none."""
        self.emu.set_device(int(port), device)
        return {"devices": [p.device for p in self.emu.ports], "valid": sorted(DEVICES)}

    def cmd_analog(self, port: int = 0, lx: int | None = None, ly: int | None = None,
                   rx: int | None = None, ry: int | None = None):
        """Set analog axes (-32768..32767). Flightstick: lx/ly = stick, ry = throttle. Persist until changed."""
        self.emu.set_analog(int(port), lx, ly, rx, ry)
        return {"analog": self.emu.ports[int(port)].analog}

    def cmd_mouse(self, port: int = 0, dx: int = 0, dy: int = 0, buttons=None, frames: int = 1):
        """Move the 3DO mouse by dx/dy (applied on the next poll) and set held buttons (LEFT, RIGHT, MIDDLE, BUTTON4)."""
        self.emu.mouse(int(port), dx, dy, buttons if buttons is None else
                       ([buttons] if isinstance(buttons, str) else buttons))
        if frames:
            self.emu.step(int(frames))
        return {"frame": self.emu.frame_count}

    def cmd_lightgun(self, port: int = 0, x: int | None = None, y: int | None = None,
                     buttons=None, offscreen: bool = False, frames: int = 1):
        """Aim the light gun at screen pixel x,y and set buttons (TRIGGER, RELOAD, START, SELECT, AUX_A)."""
        self.emu.lightgun(int(port), x, y, buttons if buttons is None else
                          ([buttons] if isinstance(buttons, str) else buttons), offscreen)
        if frames:
            self.emu.step(int(frames))
        return {"frame": self.emu.frame_count}

    # ================================================================ output
    def cmd_screenshot(self, scale: int = 2, path: str | None = None, inline: bool = True):
        """Current frame as PNG (base64 in png_b64 unless inline=false); optionally saved to PATH."""
        e = self.emu
        img = e.screenshot(scale=int(scale))
        res = {"width": img.width, "height": img.height, "frame": e.frame_count}
        if path:
            p = Path(path).expanduser()
            p.parent.mkdir(parents=True, exist_ok=True)
            img.save(p)
            res["path"] = str(p.resolve())
        if inline:
            res["png_b64"] = _png_b64(img)
        return res

    def cmd_record_gif(self, path: str, frames: int = 120, every: int = 2, scale: int = 1):
        """Run FRAMES frames capturing every Nth into an animated GIF at PATH."""
        e = self.emu
        self.s.apply_input()
        imgs = []
        for _ in range(0, int(frames), int(every)):
            if e.step(int(every)) == 0:
                break
            imgs.append(e.screenshot(scale=int(scale)).convert("P", palette=1, colors=255))
        p = Path(path).expanduser()
        p.parent.mkdir(parents=True, exist_ok=True)
        imgs[0].save(p, save_all=True, append_images=imgs[1:], loop=0,
                     duration=int(1000 * int(every) / 60))
        return {"path": str(p.resolve()), "frames": len(imgs)}

    def cmd_log(self, since: int | None = None, max_lines: int = 400, advance: bool = True):
        """Debug console lines (kprintf/printf/tdo_log, kernel). Default: new since the last call."""
        e = self.emu
        start = self.log_cursor if since is None else int(since)
        lines, nxt = e.read_debug(start)
        truncated = max(0, len(lines) - int(max_lines))
        if truncated:
            lines = lines[-int(max_lines):]
        if advance:
            self.log_cursor = nxt
        return {"lines": lines, "next": nxt, "truncated": truncated, "partial": e._debug_partial}

    def cmd_crashes(self):
        """Aborts/exceptions the OS reported on the debug console (with registers at detection)."""
        return {"crashes": self.s.crashes}

    def cmd_events(self, since: int = 0):
        """Session event log (gdb attach/detach, reboots, breakpoint stops...)."""
        return {"events": self.s.events[int(since):], "next": len(self.s.events)}

    # ================================================================ OS introspection
    def cmd_ps(self):
        """Process list: every Portfolio task with state, priority, parent, signals, stack, CPU time."""
        return {"tasks": self.s.kernel.tasks(), "kernel": self.s.kernel.summary()}

    def cmd_folios(self):
        """Loaded folios (shared libraries)."""
        return {"folios": self.s.kernel.folios()}

    def cmd_devices(self):
        """OS device list."""
        return {"devices": self.s.kernel.devices()}

    # ================================================================ OS introspection (Portfolio)
    def _os(self):
        from .osinspect import OS
        return OS(self.emu, self.s.symbolize)

    def cmd_os_summary(self):
        """OS overview: kernel base, current task, item counts by type, free/used memory."""
        return self._os().summary()

    def cmd_os_items(self, type: str | None = None):
        """Every live OS item (task, port, semaphore, device, ioreq, screen, sample...). type: filter by type or subsystem."""
        items = self._os().items(type)
        return {"count": len(items), "items": items}

    def cmd_os_item(self, item):
        """One item in detail (type-specific fields + raw node bytes)."""
        return self._os().item(int(str(item), 0))

    def cmd_os_tasks(self):
        """Tasks: state, priority, parent, signals, wait item, stacks, CPU ms, owned memory pages."""
        return {"tasks": self._os().tasks()}

    def cmd_os_ports(self):
        """Message ports with queued messages."""
        return {"ports": self._os().ports()}

    def cmd_os_semaphores(self):
        """Semaphores: owner, nest count, waiters."""
        return {"semaphores": self._os().semaphores()}

    def cmd_os_devices(self):
        """Devices + drivers (open counts) and I/O requests (device, command, state)."""
        o = self._os()
        return {"devices": o.devices(), "drivers": o.drivers(), "ioreqs": o.ioreqs()}

    def cmd_os_folios(self):
        """Loaded folios (shared libraries) with open counts and SWI table sizes."""
        return {"folios": self._os().folios()}

    def cmd_os_graphics(self):
        """Graphics items: screen groups, screens (with bitmaps/framebuffers), bitmaps, VDLs."""
        return self._os().graphics()

    def cmd_os_audio(self):
        """Audio folio items: templates, instruments, knobs, samples, attachments."""
        return self._os().audio()

    def cmd_os_files(self):
        """Filesystem items: mounted filesystems, open files, aliases."""
        return self._os().files()

    def cmd_os_memory(self, per_page: bool = True):
        """Memory regions (DRAM/VRAM): page size, free/used, per-page map of owning task ('.' free, '?' system)."""
        return self._os().memory(per_page=bool(per_page))

    def cmd_os_snapshot(self, name: str = "snap"):
        """Record the OS state (items, tasks, memory) under NAME for os_diff."""
        snap = self._os().snapshot()
        self.s.os_snapshots[name] = snap
        return {"name": name, "items": len(snap["items"]), "frame": snap["frame"],
                "snapshots": sorted(self.s.os_snapshots)}

    def cmd_os_diff(self, a: str = "snap", b: str | None = None):
        """Compare two OS snapshots (b defaults to 'now'): items created/deleted, CPU per task, memory delta."""
        from .osinspect import diff
        if a not in self.s.os_snapshots:
            raise ValueError(f"no snapshot {a!r}; take one with os_snapshot")
        sb = self.s.os_snapshots[b] if b else self._os().snapshot()
        return diff(self.s.os_snapshots[a], sb)

    # ================================================================ symbols
    def cmd_symbols(self, pattern: str = ".", limit: int = 50):
        """Search program symbols (regex). Shows runtime addresses once the program is loaded."""
        sy = self.s.symbols
        if not sy:
            return {"error": "no symbols (build with ./3do build to get build/<name>.sym)"}
        base = self.s.load_base()
        return {"load_base": _h(base) if base is not None else None,
                "symbols": [{"name": n, "offset": _h(a),
                             "addr": _h(base + a) if base is not None else None}
                            for n, a in sy.search(pattern, int(limit))]}

    def cmd_addr2sym(self, addr):
        """Runtime address -> symbol+offset."""
        a = self._sym(addr)
        return {"addr": _h(a), "symbol": self.s.symbolize(a)}

    def cmd_disasm(self, addr="pc", count: int = 16):
        """Disassemble COUNT ARM instructions at ADDR (address, symbol, symbol+off or "pc")."""
        e = self.emu
        a = e.regs()["pc"] if addr == "pc" else self._sym(addr)
        try:
            import capstone
        except ImportError:
            return {"error": "pip install capstone (re-run scripts/setup-python.sh)"}
        md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM | capstone.CS_MODE_BIG_ENDIAN)
        code = e.read_mem(a, 4 * int(count))
        pc = e.regs()["pc"]
        out = []
        for ins in md.disasm(code, a):
            sym = self.s.symbolize(ins.address)
            out.append(("=> " if ins.address == pc else "   ") +
                       f"{ins.address:08x}  {ins.bytes.hex()}  {ins.mnemonic:<6} {ins.op_str}"
                       + (f"   <{sym}>" if sym else ""))
        return {"listing": "\n".join(out)}

    # ================================================================ CPU / debugger
    def cmd_regs(self):
        """CPU registers (current mode) with symbolized pc/lr."""
        r = self.emu.regs()
        out = {k: _h(v) for k, v in r.items()}
        out["pc_sym"] = self.s.symbolize(r["pc"])
        out["lr_sym"] = self.s.symbolize(r["lr"])
        out["mode"] = {0x10: "usr", 0x11: "fiq", 0x12: "irq", 0x13: "svc", 0x17: "abt",
                       0x1B: "und", 0x1F: "sys"}.get(r["cpsr"] & 0x1F, "?")
        return out

    def cmd_set_reg(self, name: str, value):
        """Set a register (r0-r15, sp, lr, pc, cpsr). Best done while halted."""
        self.emu.set_reg(name, int(str(value), 0))
        return self.cmd_regs()

    def cmd_halt(self):
        """Stop the CPU before its next instruction (like gdb ^C)."""
        self.emu.halt()
        self.s.settle_halt()
        return self.cmd_stop_info()

    def cmd_continue(self, max_frames: int = 0):
        """Resume a halted CPU. max_frames>0: run synchronously until a stop or that many frames."""
        e = self.emu
        if max_frames:
            info = e.run_until_stop(int(max_frames))
            return {"frame": e.frame_count, **self._fmt_stop(info)}
        e.cont()
        return {"halted": False}

    def cmd_stepi(self, count: int = 1):
        """Execute COUNT instructions, then halt."""
        info = self.emu.stepi(int(count))
        return self._fmt_stop(info)

    def cmd_stop_info(self):
        """Why/where the CPU is halted."""
        return self._fmt_stop(self.emu.stop_info())

    def _fmt_stop(self, info):
        out = {"halted": info["halted"], "reason": info["reason"], "pc": _h(info["pc"]),
               "where": self.s.symbolize(info["pc"])}
        if info["reason"] == "watchpoint":
            out["watch"] = {"addr": _h(info["watch_addr"]), "type": info["watch_type"],
                            "symbol": self.s.symbolize(info["watch_addr"])}
        return out

    def cmd_break(self, addr):
        """Set a halting breakpoint at an address or symbol ("main", "update_plasma+0x10")."""
        a = self._sym(addr)
        self.emu.bp_add(a)
        self.s.breakpoints.add(a)
        return {"breakpoints": [f"{_h(x)} {self.s.symbolize(x) or ''}".strip() for x in sorted(self.s.breakpoints)]}

    def cmd_breakpoints(self):
        """List halting breakpoints, watchpoints and tracepoint count."""
        return {"breakpoints": [{"addr": _h(x), "where": self.s.symbolize(x)} for x in sorted(self.s.breakpoints)],
                "watchpoints": [{"addr": _h(x), "len": l, "kind": k, "where": self.s.symbolize(x)}
                                for x, l, k in sorted(self.s.watchpoints)]}

    def cmd_delete(self, addr=None):
        """Remove a breakpoint (or all if no addr)."""
        if addr is None:
            self.emu.bp_clear()
            self.s.breakpoints.clear()
        else:
            a = self._sym(addr)
            self.emu.bp_remove(a)
            self.s.breakpoints.discard(a)
        return {"breakpoints": [_h(x) for x in sorted(self.s.breakpoints)]}

    def cmd_watch(self, addr, length: int = 4, kind: str = "write"):
        """Data watchpoint (kind: write/read/access) on ADDR..ADDR+LENGTH; halts after the access. CPU accesses only."""
        a = self._sym(addr)
        self.emu.wp_add(a, int(length), kind)
        self.s.watchpoints.add((a, int(length), kind))
        return {"watchpoints": [{"addr": _h(x), "len": l, "kind": k} for x, l, k in self.s.watchpoints]}

    def cmd_unwatch(self, addr=None, length: int = 4, kind: str = "write"):
        """Remove a watchpoint (or all if no addr)."""
        if addr is None:
            self.emu.wp_clear()
            self.s.watchpoints.clear()
        else:
            a = self._sym(addr)
            self.emu.wp_remove(a, int(length), kind)
            self.s.watchpoints.discard((a, int(length), kind))
        return {"watchpoints": len(self.s.watchpoints)}

    def cmd_tracepoint(self, action: str = "hits", addr=None, max_entries: int = 200):
        """Non-stopping breakpoints that log r0-r3/sp/lr. action: add|remove|clear|hits."""
        e = self.emu
        if action == "add":
            a = self._sym(addr)
            if e.trace_add(a) < 0:
                raise RuntimeError("tracepoint table full (64)")
            return {"added": _h(a)}
        if action == "remove":
            return {"removed": e.trace_remove(self._sym(addr)) == 0}
        if action == "clear":
            e.trace_clear()
            return {"cleared": True}
        hits = e.trace_hits(4096)
        return {"total": len(hits), "hits": [
            {"frame": h.frame, "pc": _h(h.pc), "where": self.s.symbolize(h.pc),
             "r0": _h(h.r0), "r1": _h(h.r1), "r2": _h(h.r2), "r3": _h(h.r3),
             "sp": _h(h.sp), "lr": _h(h.lr), "caller": self.s.symbolize(h.lr), "hit": h.hits}
            for h in hits[-int(max_entries):]]}

    def cmd_profile(self, frames: int = 300, interval: int = 1500, top: int = 25):
        """Sample the PC every ~INTERVAL instructions while running FRAMES frames; per-function %."""
        import collections
        e = self.emu
        self.s.apply_input()
        e.prof_enable(int(interval))
        e.step(int(frames))
        samples = e.prof_samples()
        e.prof_enable(0)

        def name(pc):
            s = self.s.symbolize(pc)
            if s:
                return s.split("+")[0]
            return "[idle: waiting for VBL]" if 0x28000 <= pc < 0x29000 else f"[OS/ROM 0x{pc & ~0xFFF:x}]"
        funcs = collections.Counter(name(pc) for pc, _ in samples)
        total = max(1, len(samples))
        return {"samples": len(samples), "frames": int(frames),
                "functions": [{"name": n, "percent": round(100 * c / total, 1), "samples": c}
                              for n, c in funcs.most_common(int(top))]}

    def cmd_swi_trace(self, enable: bool = True):
        """Start/stop recording SWI (system call) invocations."""
        self.emu.swi_trace(bool(enable))
        return {"enabled": bool(enable)}

    def cmd_swi_calls(self, max_entries: int = 200):
        """Drain recorded SWIs: per-number counts + the latest calls."""
        from .swinames import name as swi_name
        calls = self.emu.swi_calls(4096)
        counts: dict[str, int] = {}
        for c in calls:
            k = f"0x{c.swi:x} {swi_name(c.swi)}"
            counts[k] = counts.get(k, 0) + 1
        return {"total": len(calls), "counts": dict(sorted(counts.items(), key=lambda x: -x[1])), "last": [
            {"frame": c.frame, "swi": f"0x{c.swi:x}", "name": swi_name(c.swi),
             "pc": _h(c.pc), "where": self.s.symbolize(c.pc),
             "r0": _h(c.r0), "r1": _h(c.r1), "r2": _h(c.r2), "r3": _h(c.r3)}
            for c in calls[-int(max_entries):]]}

    # ================================================================ memory
    def cmd_read_mem(self, addr, length: int = 64):
        """Hex dump guest memory (address or symbol). DRAM 0-0x1FFFFF, VRAM 0x200000-0x2FFFFF, ROM 0x3000000+."""
        a = self._sym(addr)
        data = self.emu.read_mem(a, min(int(length), 65536))
        return {"addr": _h(a), "length": len(data), "hex": _hexdump(a, data)}

    def cmd_read_u32(self, addr, count: int = 1):
        """Read big-endian 32-bit words (address or symbol, e.g. a global variable)."""
        a = self._sym(addr)
        import struct
        data = self.emu.read_mem(a, 4 * int(count))
        vals = list(struct.unpack(f">{len(data) // 4}I", data[:len(data) // 4 * 4]))
        return {"addr": _h(a), "values": vals, "hex": [_h(v) for v in vals]}

    def cmd_write_mem(self, addr, hex: str):
        """Write bytes (hex string) to guest DRAM/VRAM."""
        a = self._sym(addr)
        data = bytes.fromhex(hex.replace(" ", ""))
        return {"addr": _h(a), "written": self.emu.write_mem(a, data)}

    def cmd_write_u32(self, addr, value):
        """Write one big-endian 32-bit word (address or symbol)."""
        import struct
        a = self._sym(addr)
        return {"addr": _h(a), "written": self.emu.write_mem(a, struct.pack(">I", int(str(value), 0) & 0xFFFFFFFF))}

    def cmd_find_mem(self, text: str | None = None, hex: str | None = None, limit: int = 20):
        """Search DRAM for a string or hex byte pattern."""
        needle = text.encode() if text is not None else bytes.fromhex(hex.replace(" ", ""))
        ram = self.emu.read_mem(0, 0x200000)
        hits, i = [], ram.find(needle)
        while i != -1 and len(hits) < int(limit):
            hits.append({"addr": _h(i), "symbol": self.s.symbolize(i)})
            i = ram.find(needle, i + 1)
        return {"hits": hits}

    # ================================================================ states
    def cmd_save_state(self, name: str = "quick"):
        """Snapshot the whole machine to build/states/NAME.state."""
        STATE_DIR.mkdir(parents=True, exist_ok=True)
        p = STATE_DIR / f"{Path(name).name}.state"
        p.write_bytes(self.emu.save_state())
        return {"path": str(p)}

    def cmd_load_state(self, name: str = "quick"):
        """Restore a snapshot saved with save_state."""
        p = STATE_DIR / f"{Path(name).name}.state"
        self.emu.load_state(p.read_bytes())
        if self.s.symbols:
            self.s.symbols.base = None
        return {"loaded": str(p), "frame": self.emu.frame_count}


# ==================================================================== transport


class SocketServer:
    """Accepts JSON-line clients on 127.0.0.1:port (0 = pick one); requests run
    on the session thread via `pump()`."""

    def __init__(self, controller: Controller, port: int = 0):
        import queue
        self.ctl = controller
        self.q: "queue.Queue" = queue.Queue()
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            self.sock.bind(("127.0.0.1", port))
        except OSError as ex:
            raise OSError(f"control port {port} is in use ({ex}); pick another with --port") from None
        self.sock.listen(8)
        self.port = self.sock.getsockname()[1]
        threading.Thread(target=self._accept, daemon=True).start()

    def _accept(self):
        while True:
            try:
                conn, _ = self.sock.accept()
            except OSError:
                return
            threading.Thread(target=self._client, args=(conn,), daemon=True).start()

    def _client(self, conn):
        import queue
        f = conn.makefile("rw")
        try:
            for line in f:
                if not line.strip():
                    continue
                done: "queue.Queue" = queue.Queue(1)
                self.q.put((line, done))
                f.write(done.get() + "\n")
                f.flush()
        except OSError:
            pass

    def pending(self) -> bool:
        return not self.q.empty()

    def pump(self, budget: int = 16):
        import queue
        for _ in range(budget):
            try:
                line, done = self.q.get_nowait()
            except queue.Empty:
                return
            req = {}
            try:
                req = json.loads(line)
                res = {"id": req.get("id"), "ok": True,
                       "result": self.ctl.handle(req["cmd"], req.get("args") or {})}
            except Exception as ex:  # noqa: BLE001 - report everything to the client
                res = {"id": req.get("id"), "ok": False, "error": f"{type(ex).__name__}: {ex}"}
            done.put(json.dumps(res))

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


# ==================================================================== clients


class ProtocolError(RuntimeError):
    pass


class SessionClient:
    """Talks to a session's control port."""

    def __init__(self, port: int, host: str = "127.0.0.1", timeout: float = 600):
        self.port = port
        self._id = 0
        self._lock = threading.Lock()
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.f = self.sock.makefile("rw")
        self.proc: subprocess.Popen | None = None
        self.info: dict = {}

    def call(self, cmd: str, **args):
        with self._lock:
            self._id += 1
            try:
                self.f.write(json.dumps({"id": self._id, "cmd": cmd, "args": args}) + "\n")
                self.f.flush()
                line = self.f.readline()
            except OSError as ex:
                raise ProtocolError(f"session connection failed: {ex}") from None
        if not line:
            raise ProtocolError("session closed the connection" + self._diag())
        res = json.loads(line)
        if not res.get("ok"):
            raise ProtocolError(res.get("error", "unknown error"))
        return res["result"]

    def _diag(self):
        if self.proc is not None and self.proc.poll() is not None:
            log = self.info.get("log")
            tail = Path(log).read_text(errors="replace")[-1500:] if log and Path(log).exists() else ""
            return f" (session process exited {self.proc.returncode}) {tail}"
        return ""

    @property
    def alive(self) -> bool:
        return self.proc is None or self.proc.poll() is None

    def close(self, stop: bool = False):
        if stop and self.alive:
            try:
                self.call("quit")
            except ProtocolError:
                pass
        try:
            self.sock.close()
        except OSError:
            pass
        if stop and self.proc is not None:
            try:
                self.proc.wait(5)
            except subprocess.TimeoutExpired:
                self.proc.kill()

    @classmethod
    def spawn(cls, iso: str, bios: str = "panafz10.bin", window: bool = False,
              mode: str | None = None, gdb: bool = True, timeout: float = 30) -> "SessionClient":
        """Start `tdo serve` in a new process and connect to it."""
        SESSION_DIR.mkdir(parents=True, exist_ok=True)
        log = SESSION_DIR / f"spawn-{os.getpid()}-{int(time.time() * 1000)}.log"
        cmd = [sys.executable, "-m", "tdo.cli", "--bios", bios, "serve", str(iso), "--port", "0",
               "--print-ports"]
        if not gdb:
            cmd.append("--no-gdb")
        if window:
            cmd.append("--window")
        if mode:
            cmd += ["--mode", mode]
        logf = open(log, "w")
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=logf, text=True,
                                env=dict(os.environ, PYTHONUNBUFFERED="1"))
        t0 = time.time()
        info = None
        while time.time() - t0 < timeout:
            line = proc.stdout.readline()
            if not line:
                break
            if line.startswith("TDO_SESSION "):
                info = json.loads(line[len("TDO_SESSION "):])
                break
        if info is None:
            proc.kill()
            raise ProtocolError(f"session failed to start: {log.read_text(errors='replace')[-1500:]}")
        threading.Thread(target=lambda: [None for _ in proc.stdout], daemon=True).start()
        c = cls(info["control_port"])
        c.proc = proc
        info["log"] = str(log)
        c.info = info
        return c


def list_sessions() -> list[dict]:
    """Running sessions registered in build/sessions/*.json (stale ones removed)."""
    out = []
    if not SESSION_DIR.exists():
        return out
    for p in SESSION_DIR.glob("*.json"):
        try:
            info = json.loads(p.read_text())
            os.kill(info["pid"], 0)
            out.append(info)
        except (OSError, ValueError, KeyError):
            p.unlink(missing_ok=True)
    return sorted(out, key=lambda i: i.get("started", 0), reverse=True)
