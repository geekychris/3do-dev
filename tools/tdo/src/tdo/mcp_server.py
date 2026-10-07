"""MCP server: full remote control of 3DO builds and emulator sessions.

Registered for Claude Code in the repo's .mcp.json. A *session* is a separate
`tdo serve` process (one emulator + control port + gdb port, optional window).

  emu_boot(project)        start a private headless session (paused; you step it)
  emu_boot(project, window=True)   ... with a window the user can watch
  emu_attach()             drive a session someone else started (`./3do run X`)

Every session command is also reachable generically through emu_cmd (see
emu_cmd("help")), and gdb_run runs gdb commands with source-level symbols.
"""
from __future__ import annotations

import base64
import json
import shutil
import subprocess
from pathlib import Path

from mcp.server.mcpserver import Image, MCPServer

from .control import ProtocolError, SessionClient, list_sessions
from .core import repo_root
from .session import resolve_iso

ROOT = repo_root()

server = MCPServer(
    name="3do",
    instructions=(
        "Build, run, inspect and debug 3DO (Opera) homebrew. Loop: build(project) -> "
        "emu_boot(project) -> emu_run_until('<log line>') -> emu_look / emu_press / emu_log. "
        "Sessions start paused (frames advance only when you step/press); emu_run makes them "
        "free-run. Debugging: emu_break('func'), emu_continue(max_frames=...), emu_regs, "
        "emu_disasm, emu_watch('global'), gdb_run for source-level gdb. "
        "OS introspection (Portfolio kernel structures read from RAM): os_overview, "
        "os_inspect('tasks'|'items'|'ports'|'semaphores'|'devices'|'folios'|'graphics'|'audio'|'files'), "
        "os_item(n), os_memory_map, os_snapshot + os_diff (leaks / what changed), emu_syscalls "
        "(SWI snoop), emu_profile (where CPU time goes), emu_crashes. "
        "emu_cmd('help') lists every low-level command. "
        "Prefer tdo_log() over kprintf() in guest code (kprintf garbles >3 args)."
    ),
)

_session: SessionClient | None = None


def _j(obj) -> str:
    return json.dumps(obj, indent=1)


def _s() -> SessionClient:
    global _session
    if _session is None:
        # Served over HTTP by DevBench, or a session started elsewhere: use the newest one.
        from .control import list_sessions
        ss = list_sessions()
        if ss:
            _session = SessionClient(ss[-1]["control_port"])
            _session.info = ss[-1]
    if _session is None:
        raise ValueError("no emulator session; call emu_boot(project) or emu_attach() first")
    if not _session.alive:
        raise ValueError("the session process has exited; call emu_boot again (or emu_restart)")
    return _session


def _call(cmd: str, **args):
    try:
        return _s().call(cmd, **{k: v for k, v in args.items() if v is not None})
    except ProtocolError as ex:
        raise ValueError(str(ex)) from None


def _img(res) -> Image:
    return Image(data=base64.b64decode(res["png_b64"]), format="png")


# ================================================================== build


@server.tool()
def list_projects() -> str:
    """List 3DO projects (projects/<name>) and their built ISOs."""
    out = []
    for d in sorted((ROOT / "projects").iterdir()):
        if (d / "Makefile").exists():
            isos = list((d / "build").glob("*.iso"))
            out.append({"project": d.name, "iso": str(isos[0].relative_to(ROOT)) if isos else None})
    return _j(out)


@server.tool()
def build(project: str, clean: bool = False) -> str:
    """Compile projects/<project> (Norcroft ARM C) and pack a signed ISO + ELF/symbols.
    Returns compiler diagnostics on failure."""
    proj = ROOT / "projects" / project
    if not (proj / "Makefile").exists():
        raise ValueError(f"no project at {proj}")
    cmd = [str(ROOT / "bin" / "3do-make"), "-C", str(proj)]
    if clean:
        subprocess.run(cmd + ["clean"], capture_output=True, text=True)
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=900)
    out = (r.stdout + r.stderr).splitlines()
    keep = [l for l in out if any(k in l for k in (
        "Error", "error", "Serious", "undefined", "built ", "src/", "Fatal", "No rule", "make:"))
        and "3dt: warning" not in l and "3do-devkit/include" not in l]
    status = "OK" if r.returncode == 0 else f"FAILED (exit {r.returncode})"
    tail = "\n".join(out[-25:]) if r.returncode else ""
    return f"build {project}: {status}\n" + "\n".join(keep[-80:]) + (f"\n--- tail ---\n{tail}" if tail else "")


@server.tool()
def new_project(name: str) -> str:
    """Create projects/<name> from the template (main loop, input, logging, smoke test)."""
    r = subprocess.run([str(ROOT / "3do"), "new", name], capture_output=True, text=True)
    return (r.stdout + r.stderr).strip()


@server.tool()
def run_tests(project: str = "", all_projects: bool = False) -> str:
    """Run a project's tests headless (`./3do test <project>`), or every project (`./3do selftest`)."""
    cmd = [str(ROOT / "3do"), "selftest"] if all_projects or not project else [str(ROOT / "3do"), "test", project]
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=1800)
    return f"exit {r.returncode}\n" + (r.stdout + r.stderr).strip()[-6000:]


# ================================================================== sessions


@server.tool()
def emu_boot(project_or_iso: str, window: bool = False, bios: str = "panafz10.bin",
             run: bool = False, frames: int = 0) -> str:
    """Start a new emulator session for a project name or ISO path (replaces the current one).
    window=True opens a window the user can watch (it free-runs); headless sessions start
    paused. run=True free-runs headless at real time. frames: run this many frames first.
    BIOS: panafz10.bin (default), panafz1.bin, panafz10-norsa.bin (unsigned discs)."""
    global _session
    iso = resolve_iso(project_or_iso)
    if _session is not None:
        _session.close(stop=_session.proc is not None)
        _session = None
    _session = SessionClient.spawn(str(iso), bios=bios, window=window,
                                   mode="running" if (window or run) else "paused")
    if frames:
        _call("step", frames=frames)
    st = _call("status")
    st["note"] = ("window open; it free-runs – use emu_pause for deterministic stepping"
                  if window else "headless and paused: advance with emu_step / emu_run_until / emu_press")
    return _j(st)


@server.tool()
def emu_attach(port: int = 0) -> str:
    """Attach to a running session (e.g. a window started with `./3do run demo`, control
    port 7330). port=0 picks the newest registered session."""
    global _session
    if not port:
        ss = list_sessions()
        if not ss:
            raise ValueError("no running sessions (start one: ./3do run <project>, or emu_boot)")
        port = ss[0]["control_port"]
    if _session is not None:
        _session.close(stop=_session.proc is not None)
    _session = SessionClient(port)
    return _j(_call("status"))


@server.tool()
def emu_sessions() -> str:
    """List running emulator sessions on this machine (pid, iso, control/gdb ports, window)."""
    return _j({"sessions": list_sessions(),
               "current": _session.port if _session else None})


@server.tool()
def emu_stop() -> str:
    """End the current session (stops it if this server started it; detaches otherwise)."""
    global _session
    if _session is not None:
        _session.close(stop=_session.proc is not None)
        _session = None
    return "stopped"


@server.tool()
def emu_restart() -> str:
    """Kill and respawn the session process (same disc, same window setting). Use if it hangs."""
    global _session
    s = _s() if _session and _session.alive else _session
    if s is None:
        raise ValueError("no session")
    info = s.info or {}
    iso = info.get("iso") or _call("status")["game"]
    window = bool(info.get("window"))
    s.close(stop=True)
    if s.proc is not None and s.proc.poll() is None:
        s.proc.kill()
    _session = SessionClient.spawn(iso, window=window, mode="running" if window else "paused")
    return _j(_call("status"))


@server.tool()
def emu_status() -> str:
    """Game, frame, run mode, CPU halt state + location, held input, devices, ports."""
    return _j(_call("status"))


@server.tool()
def emu_reset() -> str:
    """Soft reset (console reset button)."""
    return _j(_call("reset"))


@server.tool()
def emu_reboot(project_or_iso: str = "") -> str:
    """Power-cycle the console, optionally inserting another disc (project name or ISO).
    Rebuild first with build() to pick up code changes."""
    iso = str(resolve_iso(project_or_iso)) if project_or_iso else None
    return _j(_call("reboot", iso=iso))


# ================================================================== running


@server.tool()
def emu_step(frames: int = 60) -> str:
    """Advance N frames (60 = 1 s of 3DO time). Stops early on a breakpoint/watchpoint."""
    return _j(_call("step", frames=frames))


@server.tool()
def emu_run(speed: float = 1.0) -> str:
    """Free-run continuously (speed 1.0 = real time, 0 = max). Also resumes a halted CPU."""
    return _j(_call("run", speed=speed))


@server.tool()
def emu_pause() -> str:
    """Stop free-running (frames then only advance on step/press)."""
    return _j(_call("pause"))


@server.tool()
def emu_run_until(text: str, max_frames: int = 1800) -> str:
    """Step until TEXT appears in debug output emitted after the last emu_log call."""
    return _j(_call("run_until", text=text, max_frames=max_frames))


# ================================================================== input


@server.tool()
def emu_press(buttons: str, hold_frames: int = 6, release_frames: int = 6, port: int = 0) -> str:
    """Press+release pad buttons: "A", "UP+B", "P". Buttons: A B C P(play) X(stop) L R UP DOWN LEFT RIGHT
    (FIRE on a flightstick). Tip: wait ~30 frames after a program starts before the first press."""
    return _j(_call("press", buttons=buttons, hold_frames=hold_frames,
                    release_frames=release_frames, port=port))


@server.tool()
def emu_hold(buttons: str = "", port: int = 0) -> str:
    """Hold buttons across steps/free-run until changed; "" releases."""
    return _j(_call("hold", buttons=buttons, port=port))


@server.tool()
def emu_sequence(steps: list[dict]) -> str:
    """Scripted input in one call. Each step: {"buttons":"A","frames":6} | {"frames":30} |
    {"analog":{"lx":-20000},"frames":10} | {"mouse":{"dx":5,"dy":0}} | {"lightgun":{"x":160,"y":120,
    "buttons":["TRIGGER"]}} | {"wait_log":"ready","max_frames":600} | {"screenshot":"build/x.png"};
    any step may add "port":1 or "device":"flightstick"."""
    return _j(_call("sequence", steps=steps))


@server.tool()
def emu_input(port: int = 0, device: str = "", lx: int | None = None, ly: int | None = None,
              rx: int | None = None, ry: int | None = None, mouse_dx: int = 0, mouse_dy: int = 0,
              mouse_buttons: str | None = None, gun_x: int | None = None, gun_y: int | None = None,
              gun_buttons: str | None = None, frames: int = 1) -> str:
    """Non-pad input. device: joypad|flightstick|mouse|lightgun|arcade_lightgun|orbatak_trackball|none
    (plugs it into the port). Analog axes -32768..32767 persist. Mouse moves are relative.
    Light gun aims at screen pixels. Buttons as "LEFT+RIGHT" / "TRIGGER". Then runs `frames`."""
    out = {}
    if device:
        out["device"] = _call("set_device", port=port, device=device)
    if any(v is not None for v in (lx, ly, rx, ry)):
        out["analog"] = _call("analog", port=port, lx=lx, ly=ly, rx=rx, ry=ry)
    if mouse_dx or mouse_dy or mouse_buttons is not None:
        btn = [b for b in (mouse_buttons or "").split("+") if b] if mouse_buttons is not None else None
        out["mouse"] = _call("mouse", port=port, dx=mouse_dx, dy=mouse_dy, buttons=btn, frames=0)
    if gun_x is not None or gun_y is not None or gun_buttons is not None:
        btn = [b for b in (gun_buttons or "").split("+") if b] if gun_buttons is not None else None
        out["lightgun"] = _call("lightgun", port=port, x=gun_x, y=gun_y, buttons=btn, frames=0)
    if frames:
        out["step"] = _call("step", frames=frames)
    return _j(out)


# ================================================================== observing


@server.tool()
def emu_screenshot(scale: int = 2, save_as: str = "") -> Image:
    """Current frame (320x240 x scale). save_as: also write a PNG (relative to repo root)."""
    path = str((ROOT / save_as).resolve()) if save_as else None
    return _img(_call("screenshot", scale=scale, path=path))


@server.tool()
def emu_look(buttons: str = "", frames: int = 30, scale: int = 2) -> list:
    """Optionally press BUTTONS, run FRAMES, then return new debug output + a screenshot."""
    if buttons:
        _call("press", buttons=buttons)
    if frames:
        _call("step", frames=frames)
    r = _call("log", max_lines=60)
    shot = _call("screenshot", scale=scale)
    status = _call("status")
    head = f"frame {shot['frame']}" + (f" – CPU HALTED: {status['stop']}" if status.get("halted") else "")
    return [head + "\n" + ("\n".join(r["lines"]) or "(no new debug output)"), _img(shot)]


@server.tool()
def emu_record_gif(path: str = "build/capture.gif", frames: int = 120, every: int = 2, scale: int = 1) -> str:
    """Run FRAMES frames capturing every Nth into an animated GIF (path relative to repo root)."""
    return _j(_call("record_gif", path=str((ROOT / path).resolve()), frames=frames, every=every, scale=scale))


@server.tool()
def emu_log(since: int | None = None, max_lines: int = 200) -> str:
    """Debug console (kernel messages, kprintf/printf/tdo_log). Default: new since last call; since=0 for all."""
    r = _call("log", since=since, max_lines=max_lines)
    head = f"[lines up to #{r['next']}" + (f", {r['truncated']} older omitted" if r["truncated"] else "") + "]"
    body = "\n".join(r["lines"]) + (f"\n(partial) {r['partial']}" if r["partial"] else "")
    return head + "\n" + (body or "(no new output)")


@server.tool()
def emu_ps(what: str = "tasks") -> str:
    """OS introspection from kernel memory. what: tasks (process list: state, priority,
    parent, signals, stack, CPU time) | folios | devices."""
    if what == "folios":
        return _j(_call("folios"))
    if what == "devices":
        return _j(_call("devices"))
    return _j(_call("ps"))


# ================================================================== OS introspection

OS_VIEWS = {"tasks": "os_tasks", "items": "os_items", "ports": "os_ports", "semaphores": "os_semaphores",
            "devices": "os_devices", "folios": "os_folios", "graphics": "os_graphics",
            "audio": "os_audio", "files": "os_files"}


@server.tool()
def os_overview() -> str:
    """Portfolio OS summary: kernel base, current task, live item counts by type, DRAM/VRAM used/free."""
    return _j(_call("os_summary"))


@server.tool()
def os_inspect(view: str = "tasks", type_filter: str = "") -> str:
    """Walk an OS data structure. view: tasks (state, priority, parent, signals, wait item,
    stacks, CPU ms, pages) | items (every item; type_filter e.g. 'Semaphore' or 'audio') |
    ports (message queues) | semaphores (owner/waiters) | devices (devices, drivers, I/O
    requests in flight) | folios | graphics (screens, bitmaps, VDLs) | audio | files."""
    if view not in OS_VIEWS:
        raise ValueError(f"view must be one of {sorted(OS_VIEWS)}")
    if view == "items":
        return _j(_call("os_items", type=type_filter or None))
    return _j(_call(OS_VIEWS[view]))


@server.tool()
def os_item(item: int) -> str:
    """One OS item by number, with type-specific detail and its raw node bytes."""
    return _j(_call("os_item", item=item))


@server.tool()
def os_memory_map() -> str:
    """DRAM/VRAM page map: which task group owns each page ('.' free, '?' kernel)."""
    m = _call("os_memory")
    lines = [m["legend"]]
    for r in m["regions"]:
        lines.append(f"{r['name']}: {r['pages']} x {r['page_size']} B pages, {r['free_pages']} free")
        lines.append("  " + "".join(str(x)[-1] if isinstance(x, int) else x for x in r["map"]))
    lines += [f"  {k}: {v}" for k, v in m["task_names"].items()]
    return "\n".join(lines)


@server.tool()
def os_snapshot(name: str = "snap") -> str:
    """Record OS state (items, tasks, free memory) to compare later with os_diff."""
    return _j(_call("os_snapshot", name=name))


@server.tool()
def os_diff(name: str = "snap") -> str:
    """What changed since os_snapshot(name): net item count per type/owner (positive = possible
    leak), items created/deleted, CPU ms per task, page ownership and free memory deltas."""
    return _j(_call("os_diff", a=name))


@server.tool()
def emu_syscalls(action: str = "fetch", max_entries: int = 60) -> str:
    """System-call snoop. action: start | stop | fetch (counts by call name + latest calls
    with caller and r0-r3)."""
    if action in ("start", "stop"):
        return _j(_call("swi_trace", enable=action == "start"))
    return _j(_call("swi_calls", max_entries=max_entries))


@server.tool()
def emu_profile(frames: int = 300, top: int = 20) -> str:
    """Statistical CPU profile over FRAMES frames: % of samples per function (OS/idle bucketed)."""
    return _j(_call("profile", frames=frames, top=top))


@server.tool()
def emu_crashes() -> str:
    """Aborts / exceptions the 3DO OS reported, with registers and log context."""
    return _j(_call("crashes"))


# ================================================================== debugging


@server.tool()
def emu_break(location: str, remove: bool = False) -> str:
    """Set (or remove) a halting breakpoint at a function/symbol ("update_plasma", "main+0x20")
    or address. Then emu_continue / emu_step to run into it."""
    if remove:
        return _j(_call("delete", addr=location if location != "all" else None))
    return _j(_call("break", addr=location))


@server.tool()
def emu_watch(location: str, length: int = 4, kind: str = "write", remove: bool = False) -> str:
    """Data watchpoint on a global/symbol or address (kind: write|read|access); halts after
    the CPU access. remove=True deletes it ("all" clears every watchpoint)."""
    if remove:
        if location == "all":
            return _j(_call("unwatch"))
        return _j(_call("unwatch", addr=location, length=length, kind=kind))
    return _j(_call("watch", addr=location, length=length, kind=kind))


@server.tool()
def emu_continue(max_frames: int = 600) -> str:
    """Resume a halted CPU and run until the next breakpoint/watchpoint or max_frames
    (returns where it stopped). max_frames=0: resume without waiting."""
    return _j(_call("continue", max_frames=max_frames))


@server.tool()
def emu_halt() -> str:
    """Stop the CPU now (before its next instruction); shows where."""
    return _j(_call("halt"))


@server.tool()
def emu_stepi(count: int = 1) -> str:
    """Execute COUNT machine instructions then halt; returns the new pc and symbol."""
    return _j(_call("stepi", count=count))


@server.tool()
def emu_regs(set_name: str = "", set_value: str = "") -> str:
    """CPU registers with symbolized pc/lr and mode. set_name/set_value change one (e.g. r0, 0x10)."""
    if set_name:
        return _j(_call("set_reg", name=set_name, value=set_value))
    return _j(_call("regs"))


@server.tool()
def emu_disasm(location: str = "pc", count: int = 16) -> str:
    """Disassemble ARM code at pc, a symbol, or an address, annotated with symbols."""
    return _call("disasm", addr=location, count=count)["listing"]


@server.tool()
def emu_symbols(pattern: str = ".", limit: int = 50) -> str:
    """Search the program's symbols (regex) with runtime addresses."""
    return _j(_call("symbols", pattern=pattern, limit=limit))


@server.tool()
def emu_read_mem(location: str, length: int = 128, as_u32: bool = False) -> str:
    """Read memory at an address or symbol (e.g. a global). as_u32: decode big-endian words."""
    if as_u32:
        return _j(_call("read_u32", addr=location, count=max(1, length // 4)))
    return _call("read_mem", addr=location, length=length)["hex"]


@server.tool()
def emu_write_mem(location: str, hex_bytes: str = "", u32: str = "") -> str:
    """Write bytes (hex) or one 32-bit value (u32="0x1234") at an address or symbol."""
    if u32:
        return _j(_call("write_u32", addr=location, value=u32))
    return _j(_call("write_mem", addr=location, hex=hex_bytes))


@server.tool()
def emu_trace(action: str = "hits", location: str = "", max_entries: int = 100) -> str:
    """Tracepoints (log args without stopping): action add|remove|clear|hits; plus action
    swi_on|swi_off|swi to trace OS system calls."""
    if action == "swi_on":
        return _j(_call("swi_trace", enable=True))
    if action == "swi_off":
        return _j(_call("swi_trace", enable=False))
    if action == "swi":
        return _j(_call("swi_calls", max_entries=max_entries))
    return _j(_call("tracepoint", action=action, addr=location or None, max_entries=max_entries))


@server.tool()
def emu_state(action: str = "save", name: str = "quick") -> str:
    """Save or load a full machine snapshot (action save|load)."""
    return _j(_call("save_state" if action == "save" else "load_state", name=name))


@server.tool()
def emu_cmd(command: str, args: dict | None = None) -> str:
    """Any session control command, e.g. emu_cmd("help"), emu_cmd("find_mem", {"text": "DEMO"}),
    emu_cmd("set_option", {"key": "opera_cpu_overclock", "value": "2.0x"}), emu_cmd("events")."""
    res = _call(command, **(args or {}))
    if isinstance(res, dict):
        res.pop("png_b64", None)
        if command == "help":
            return res["commands"]
    return _j(res)


@server.tool()
def gdb_run(commands: list[str], timeout_s: int = 60) -> str:
    """Run gdb commands against the current session with full source-level symbols
    (e.g. ["break main.c:396", "continue", "bt", "info locals", "p *s_squares[0]"]).
    gdb attaches (halting the CPU), runs the commands, then detaches (CPU resumes)."""
    st = _call("status")
    if not st.get("gdb_port"):
        raise ValueError("this session has no gdb port")
    gdb = next((shutil.which(c) for c in ("arm-none-eabi-gdb", "gdb-multiarch", "gdb") if shutil.which(c)), None)
    if not gdb:
        raise ValueError("gdb not installed (brew install gdb / apt install gdb-multiarch)")
    iso = Path(st["game"])
    elf = iso.with_suffix(".elf")
    cmd = [gdb, "-q", "-batch", "-ex", "set confirm off", "-ex", "set pagination off",
           "-ex", "set endian big", "-ex", "set architecture arm"]
    if elf.exists():
        cmd += ["-ex", f"directory {elf.parent.parent}", "-ex", f"set substitute-path /work {ROOT}", str(elf)]
    cmd += ["-ex", f"target remote 127.0.0.1:{st['gdb_port']}"]
    for c in commands:
        cmd += ["-ex", c]
    cmd += ["-ex", "delete", "-ex", "detach"]
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout_s)
        out = r.stdout + r.stderr
    except subprocess.TimeoutExpired as ex:
        out = ((ex.stdout or b"").decode(errors="replace") if isinstance(ex.stdout, bytes) else (ex.stdout or ""))
        out += f"\n[gdb timed out after {timeout_s}s – the target may still be running; use emu_halt]"
    lines = [l for l in out.splitlines() if not l.startswith("During symbol reading")
             and "DWARF" not in l and l.strip() not in ("The target is set to big endian.",
                                                         'The target architecture is set to "arm".')]
    return "\n".join(lines)[-8000:]


def main():
    server.run("stdio")


if __name__ == "__main__":
    main()
