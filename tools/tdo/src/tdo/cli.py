"""tdo – command line for the 3DO dev harness.

GAME is an ISO path or a project name (projects/<name>).

  tdo run      GAME [--port N] [--gdb-port N]     window + remote control + gdb
  tdo serve    GAME [--window] [--mode paused|running]   headless session server
  tdo sessions                                    list running sessions
  tdo ctl      [--port N] CMD [key=value ...]     send a control command (ctl help)
  tdo gdb      GAME [--attach]                    gdb with symbols, connected
  tdo shot     GAME -o out.png [--frames N | --until TEXT] [--press A@300 ...]
  tdo log      GAME [--frames N]                  dump debug console output
  tdo test     GAME [--frames N] [--expect TEXT]  run on-target tests (TDO:DONE)
  tdo info                                        core / harness / BIOS info
  tdo mcp                                         MCP server (stdio) for Claude
"""
from __future__ import annotations

import argparse
import re
import sys
import time


def _parse_presses(items):
    """'A@300' or 'A+B@300:12' -> (frame, [buttons], hold)"""
    out = []
    for it in items or []:
        m = re.fullmatch(r"([A-Za-z+]+)@(\d+)(?::(\d+))?", it)
        if not m:
            raise SystemExit(f"bad --press {it!r}; use BUTTON[+BUTTON]@FRAME[:HOLD]")
        out.append((int(m.group(2)), m.group(1).upper().split("+"), int(m.group(3) or 6)))
    return sorted(out)


def _run_scripted(emu, frames, presses, until=None):
    """Step to `frames`, applying presses at absolute frame numbers."""
    for at, buttons, hold in presses:
        if at > emu.frame_count:
            emu.step(at - emu.frame_count)
        emu.press(buttons, hold, 0)
    if until:
        return emu.run_until_log(until, max(frames - emu.frame_count, 1), since=0)
    if frames > emu.frame_count:
        emu.step(frames - emu.frame_count)
    return True


def _game(target):
    from .session import resolve_iso
    return str(resolve_iso(target))


def cmd_run(a):
    from .session import serve
    return serve(a.game, bios=a.bios, window=True, mode="running", scale=a.scale,
                 audio=not a.no_audio, port=a.port, gdb_port=None if a.no_gdb else a.gdb_port,
                 verbose_log=True)


def cmd_serve(a):
    from .session import serve
    return serve(a.game, bios=a.bios, window=a.window, mode=a.mode, scale=a.scale,
                 audio=a.window, port=a.port, gdb_port=None if a.no_gdb else a.gdb_port,
                 print_ports=a.print_ports, verbose_log=a.verbose)


def _pick_session(port):
    from .control import list_sessions
    if port:
        return port
    ss = list_sessions()
    if not ss:
        raise SystemExit("no running session (start one: tdo run GAME / tdo serve GAME)")
    return ss[0]["control_port"]


def cmd_sessions(a):
    from .control import list_sessions
    import json
    ss = list_sessions()
    if not ss:
        print("no running sessions")
    for s in ss:
        print(json.dumps(s))
    return 0


def cmd_ctl(a):
    """tdo ctl screenshot path=x.png scale=2 ; tdo ctl press buttons=A ; tdo ctl help"""
    import json
    from .control import ProtocolError, SessionClient
    args = {}
    for tok in a.args:
        k, sep, v = tok.partition("=")
        if not sep:
            raise SystemExit(f"argument {tok!r} must be key=value")
        try:
            args[k] = json.loads(v)
        except ValueError:
            args[k] = v
    c = SessionClient(_pick_session(a.port))
    try:
        res = c.call(a.command, **args)
    except ProtocolError as ex:
        print(f"error: {ex}", file=sys.stderr)
        return 1
    if isinstance(res, dict):
        if "png_b64" in res and not a.raw:
            res["png_b64"] = f"<{len(res['png_b64'])} base64 chars; use path=FILE>"
        for key in ("commands", "listing", "hex"):
            if key in res and isinstance(res[key], str) and len(res) <= 3:
                print(res[key])
                return 0
    print(json.dumps(res, indent=1))
    return 0


def _find_gdb():
    import os
    import shutil
    for cand in (os.environ.get("TDO_GDB"), "arm-none-eabi-gdb", "gdb-multiarch", "gdb"):
        if cand and shutil.which(cand):
            return shutil.which(cand)
    raise SystemExit("no gdb found: brew install gdb (macOS) / apt install gdb-multiarch (Linux)")


def cmd_gdb(a):
    """Start (or attach to) a session for GAME and run gdb against it with symbols."""
    import os
    import subprocess
    from pathlib import Path
    from .control import SessionClient, list_sessions
    iso = _game(a.game)
    client = None
    gdb_port = None
    if a.attach:
        for s in list_sessions():
            if s["iso"] == iso and s.get("gdb_port"):
                gdb_port = s["gdb_port"]
                break
        if gdb_port is None:
            raise SystemExit(f"no running session for {iso} with gdb enabled")
    else:
        client = SessionClient.spawn(iso, bios=a.bios, window=a.window,
                                     mode="running" if a.window else "paused")
        gdb_port = client.info["gdb_port"]
        if a.boot_frames:
            client.call("step", frames=a.boot_frames)   # let the OS load the program
    elf = Path(iso).with_suffix(".elf")
    cmd = [_find_gdb(), "-q",
           "-ex", "set confirm off", "-ex", "set pagination off",
           "-ex", "set endian big", "-ex", "set architecture arm"]
    if elf.exists():
        from .core import repo_root
        proj = elf.parent.parent
        cmd += ["-ex", f"directory {proj}",                      # src/... paths
                "-ex", f"set substitute-path /work {repo_root()}",  # container paths
                str(elf)]
    cmd += ["-ex", f"target remote 127.0.0.1:{gdb_port}"]
    for x in a.ex or []:
        cmd += ["-ex", x]
    if a.batch:
        cmd.insert(1, "-batch")
    print(f"[tdo] gdb -> 127.0.0.1:{gdb_port} ({'symbols: ' + elf.name if elf.exists() else 'no symbols'})",
          file=sys.stderr)
    try:
        return subprocess.call(cmd)
    finally:
        if client:
            client.close(stop=True)


def cmd_shot(a):
    from .core import Emulator
    with Emulator(bios=a.bios) as emu:
        emu.load(a.game)
        ok = _run_scripted(emu, a.frames, _parse_presses(a.press), a.until)
        if a.until and not ok:
            print(f"warning: {a.until!r} not seen within {a.frames} frames", file=sys.stderr)
        emu.step(a.settle)
        emu.screenshot(a.output, scale=a.scale)
        print(f"{a.output} (frame {emu.frame_count})")
    return 0


def cmd_log(a):
    from .core import Emulator
    with Emulator(bios=a.bios) as emu:
        emu.load(a.game)
        _run_scripted(emu, a.frames, _parse_presses(a.press))
        for line in emu.debug_log:
            print(line)
        if emu._debug_partial:
            print(emu._debug_partial)
    return 0


def cmd_test(a):
    """Boot the game headless and evaluate TDO:PASS/FAIL/DONE lines and --expect strings."""
    from .core import Emulator
    t0 = time.time()
    with Emulator(bios=a.bios) as emu:
        emu.load(a.game)
        presses = _parse_presses(a.press)
        expects = list(a.expect or [])
        pending = list(expects)
        done_line = None
        seen = 0
        while emu.frame_count < a.frames:
            while presses and presses[0][0] <= emu.frame_count:
                _, buttons, hold = presses.pop(0)
                emu.press(buttons, hold, 0)
            emu.step(10)
            for line in emu.debug_log[seen:]:
                if done_line:
                    break  # a program that returns from main() is relaunched; judge run #1
                if line.startswith("TDO:") or a.verbose:
                    print(f"  {line}")
                if line.startswith("TDO:DONE"):
                    done_line = line
                pending = [e for e in pending if e not in line]
                seen += 1
            seen = len(emu.debug_log) if not done_line else seen
            if (done_line or not a.wait_done) and not pending and not presses:
                break
        judged = emu.debug_log[:seen]
        fails = [l for l in judged if l.startswith("TDO:FAIL")]
        passes = [l for l in judged if l.startswith("TDO:PASS")]
        problems = []
        if a.wait_done and not done_line:
            problems.append(f"no TDO:DONE within {a.frames} frames")
        if fails:
            problems.append(f"{len(fails)} on-target check(s) failed")
        for e in pending:
            problems.append(f"expected output not seen: {e!r}")
        if a.screenshot:
            emu.screenshot(a.screenshot, scale=2)
        dt = time.time() - t0
        status = "FAIL" if problems else "PASS"
        print(f"{status}: {len(passes)} passed, {len(fails)} failed, "
              f"{len(expects) - len(pending)}/{len(expects)} expectations, "
              f"{emu.frame_count} frames in {dt:.1f}s")
        for p in problems:
            print(f"  - {p}")
        if problems and not a.verbose:
            print("  last debug output:")
            for line in emu.debug_log[-15:]:
                print(f"    {line}")
        return 1 if problems else 0


def cmd_info(a):
    from .core import Emulator, default_core_path, default_system_dir
    print(f"core:   {default_core_path()}")
    print(f"bios:   {default_system_dir()}")
    for p in sorted(default_system_dir().glob("*.bin")):
        print(f"        {p.name}")
    with Emulator(bios=a.bios) as emu:
        info = emu.system_info()
        print(f"opera:  {info['name']} {info['version']} ({info['extensions']})")
        print(f"harness: {'v%d' % emu.lib.tdo_version() if emu.has_harness else 'MISSING'}")
    return 0


def cmd_mcp(a):
    from .mcp_server import main as mcp_main
    mcp_main()
    return 0


def main(argv=None):
    p = argparse.ArgumentParser(prog="tdo", description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--bios", default="panafz10.bin", help="BIOS file in ./bios (default panafz10.bin)")
    sub = p.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("run", help="play in a window (with remote control + gdb)")
    s.add_argument("game")
    s.add_argument("--scale", type=int, default=3)
    s.add_argument("--no-audio", action="store_true")
    s.add_argument("--port", "--listen", type=int, default=7330, dest="port",
                   help="control port (default 7330; 0 = any free)")
    s.add_argument("--gdb-port", type=int, default=2330, help="gdb port (default 2330; 0 = any)")
    s.add_argument("--no-gdb", action="store_true")
    s.set_defaults(fn=cmd_run)

    s = sub.add_parser("serve", help="run a session server (headless unless --window)")
    s.add_argument("game")
    s.add_argument("--window", action="store_true")
    s.add_argument("--mode", choices=["paused", "running"], default=None)
    s.add_argument("--scale", type=int, default=3)
    s.add_argument("--port", type=int, default=0, help="control port (0 = any free)")
    s.add_argument("--gdb-port", type=int, default=0)
    s.add_argument("--no-gdb", action="store_true")
    s.add_argument("--print-ports", action="store_true", help="print a TDO_SESSION json line on stdout")
    s.add_argument("-v", "--verbose", action="store_true", help="echo the 3DO debug console")
    s.set_defaults(fn=cmd_serve)

    s = sub.add_parser("sessions", help="list running sessions")
    s.set_defaults(fn=cmd_sessions)

    s = sub.add_parser("ctl", help="send a control command to a session (try: ctl help)")
    s.add_argument("--port", type=int, help="session control port (default: newest session)")
    s.add_argument("--raw", action="store_true", help="don't elide base64 payloads")
    s.add_argument("command")
    s.add_argument("args", nargs="*", help="key=value (values parsed as JSON when possible)")
    s.set_defaults(fn=cmd_ctl)

    s = sub.add_parser("gdb", help="debug GAME with gdb (spawns a session unless --attach)")
    s.add_argument("game")
    s.add_argument("--attach", action="store_true", help="use an already running session for GAME")
    s.add_argument("--window", action="store_true", help="spawned session gets a window")
    s.add_argument("--boot-frames", type=int, default=400,
                   help="frames to run before gdb connects so the program is loaded (default 400)")
    s.add_argument("--ex", action="append", help="extra gdb command (repeatable)")
    s.add_argument("--batch", action="store_true", help="run --ex commands and exit")
    s.set_defaults(fn=cmd_gdb)

    press_help = "BUTTON[+BUTTON]@FRAME[:HOLD], e.g. A@400 or UP+A@500:10 (buttons: A B C P X L R UP DOWN LEFT RIGHT)"
    s = sub.add_parser("shot", help="headless screenshot")
    s.add_argument("game")
    s.add_argument("-o", "--output", default="screenshot.png")
    s.add_argument("--frames", type=int, default=600)
    s.add_argument("--until", metavar="TEXT", help="stop once TEXT appears in the debug log")
    s.add_argument("--settle", type=int, default=2, help="extra frames after --until")
    s.add_argument("--press", action="append", help=press_help)
    s.add_argument("--scale", type=int, default=1)
    s.set_defaults(fn=cmd_shot)

    s = sub.add_parser("log", help="print debug console output")
    s.add_argument("game")
    s.add_argument("--frames", type=int, default=600)
    s.add_argument("--press", action="append", help=press_help)
    s.set_defaults(fn=cmd_log)

    s = sub.add_parser("test", help="headless test run")
    s.add_argument("game")
    s.add_argument("--frames", type=int, default=3600, help="timeout in frames (60/s)")
    s.add_argument("--expect", action="append", metavar="TEXT", help="debug output that must appear")
    s.add_argument("--press", action="append", help=press_help)
    s.add_argument("--no-wait-done", dest="wait_done", action="store_false",
                   help="don't require a TDO:DONE line (use with --expect)")
    s.add_argument("--screenshot", metavar="PNG", help="save final frame")
    s.add_argument("-v", "--verbose", action="store_true")
    s.set_defaults(fn=cmd_test)

    s = sub.add_parser("info", help="show core/harness/BIOS info")
    s.set_defaults(fn=cmd_info)

    s = sub.add_parser("mcp", help="run the MCP server on stdio")
    s.set_defaults(fn=cmd_mcp)

    a = p.parse_args(argv)
    if getattr(a, "game", None) and a.fn not in (cmd_run, cmd_serve, cmd_gdb):
        a.game = _game(a.game)
    sys.exit(a.fn(a))


if __name__ == "__main__":
    main()
