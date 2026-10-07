"""End-to-end tests for the remote-control / debugger surface.

Spawns real session processes against the demo ISO and drives them through
the control port, gdb (if installed) and the MCP server. Run by
`./3do selftest`, or directly:  .venv/bin/python tools/tdo/tests/test_remote.py
"""
import asyncio
import base64
import json
import shutil
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "tdo" / "src"))

from tdo.control import ProtocolError, SessionClient, list_sessions  # noqa: E402

ISO = ROOT / "projects" / "demo" / "build" / "demo.iso"
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}" + (f"  ({detail})" if detail and not ok else ""))
    if not ok:
        failures.append(name)
    return ok


def section(t):
    print(f"-- {t}")


def test_control():
    section("session lifecycle + run modes")
    c = SessionClient.spawn(str(ISO))
    try:
        st = c.call("status")
        check("spawned headless session is paused", st["mode"] == "paused" and st["frame"] == 0)
        check("session is registered", any(s["pid"] == st["pid"] for s in list_sessions()))
        check("boots to ready", c.call("run_until", text="DEMO: ready", max_frames=1500)["found"])
        f0 = c.call("status")["frame"]
        time.sleep(0.3)
        check("paused session doesn't advance on its own", c.call("status")["frame"] == f0)
        c.call("run", speed=0)
        time.sleep(0.5)
        f1 = c.call("status")["frame"]
        check("run (turbo) advances", f1 > f0 + 30, f"{f0}->{f1}")
        c.call("pause")
        check("help lists commands", "disasm" in c.call("help")["commands"])

        section("input injection")
        c.call("log")
        c.call("step", frames=30)
        c.call("press", buttons="A")
        check("press A reaches the game", any("palette=1" in l for l in c.call("log")["lines"]))
        c.call("hold", buttons="UP")
        c.call("step", frames=20)
        check("hold persists across steps", c.call("status")["held_buttons"].get("0") == ["UP"])
        c.call("release")
        r = c.call("sequence", steps=[{"buttons": "B", "frames": 4}, {"frames": 10},
                                      {"buttons": "B", "frames": 4}, {"frames": 10}])
        lines = c.call("log")["lines"]
        check("sequence: B toggles vortex off and on",
              any("vortex=0" in l for l in lines) and any("vortex=1" in l for l in lines), str(lines[-4:]))
        dev = c.call("set_device", port=1, device="flightstick")
        check("set_device plugs a flightstick into port 1", dev["devices"][1] == "flightstick")
        an = c.call("analog", port=1, lx=-20000, ry=12000)
        check("analog axes stored", an["analog"]["lx"] == -20000 and an["analog"]["ry"] == 12000)
        c.call("set_device", port=1, device="mouse")
        c.call("mouse", port=1, dx=10, dy=-4, buttons=["LEFT"])
        c.call("set_device", port=1, device="lightgun")
        c.call("lightgun", port=1, x=100, y=50, buttons=["TRIGGER"])
        c.call("set_device", port=1, device="none")
        check("mouse/lightgun injection accepted", True)

        section("screens")
        shot = c.call("screenshot", scale=1, path=str(ROOT / "build" / "test-remote.png"))
        png = base64.b64decode(shot["png_b64"])
        check("screenshot returns a PNG", png[:4] == b"\x89PNG" and shot["width"] == 320)
        gif = c.call("record_gif", path=str(ROOT / "build" / "test-remote.gif"), frames=20, every=5)
        check("record_gif writes frames", gif["frames"] == 4 and Path(gif["path"]).exists())

        section("OS introspection")
        ps = c.call("ps")
        names = [t["name"] for t in ps["tasks"]]
        check("ps lists kernel + our task", "Operator" in names and "LaunchMe" in names, str(names))
        check("folios include graphics", any(f["name"] == "Graphics" for f in c.call("folios")["folios"]))

        section("symbols + memory")
        sy = c.call("symbols", pattern="^update_plasma$")["symbols"]
        check("symbol lookup with runtime address", sy and sy[0]["addr"] is not None)
        before = c.call("read_u32", addr="s_palette")["values"][0]
        c.call("write_u32", addr="s_palette", value=3)
        check("write a global via its symbol", c.call("read_u32", addr="s_palette")["values"][0] == 3)
        c.call("write_u32", addr="s_palette", value=before)
        check("find_mem finds a string", bool(c.call("find_mem", text="HELLO FROM CLAUDE")["hits"]))

        section("debugger")
        c.call("break", addr="update_plasma")
        st = c.call("step", frames=5)
        check("breakpoint halts at the function", st.get("stopped", {}).get("where") == "update_plasma", str(st))
        regs = c.call("regs")
        check("regs symbolized", regs["pc_sym"] == "update_plasma" and regs["lr_sym"].startswith("main"))
        dis = c.call("disasm", addr="pc", count=4)["listing"]
        check("disasm marks pc", dis.startswith("=> ") and "push" in dis, dis.splitlines()[0])
        si = c.call("stepi", count=3)
        check("stepi advances 3 instructions", si["reason"] == "step" and si["where"] == "update_plasma+0xc", str(si))
        c.call("set_reg", name="r9", value="0x1234")
        check("set_reg", c.call("regs")["r9"] == "0x00001234")
        c.call("delete")
        cont = c.call("continue", max_frames=30)
        check("continue runs on after deleting the breakpoint", not cont["halted"])
        c.call("watch", addr="s_show_stars")
        c.call("hold", buttons="C")
        st = c.call("step", frames=10)
        c.call("release")
        w = st.get("stopped", {})
        check("write watchpoint catches the C button handler",
              w.get("reason") == "watchpoint" and (w.get("where") or "").startswith("handle_input"), str(st))
        c.call("unwatch")
        c.call("continue")
        c.call("halt")
        check("halt stops the CPU", c.call("status")["halted"])
        c.call("continue")
        c.call("tracepoint", action="add", addr="update_text")
        c.call("step", frames=3)
        hits = c.call("tracepoint", action="hits")
        check("tracepoint logs calls with caller", hits["total"] >= 3 and hits["hits"][0]["caller"].startswith("main"))
        c.call("tracepoint", action="clear")
        c.call("swi_trace", enable=True)
        c.call("step", frames=2)
        check("SWI trace records system calls", c.call("swi_calls")["total"] > 0)
        c.call("swi_trace", enable=False)

        section("states + restart")
        c.call("save_state", name="test-remote")
        f = c.call("status")["frame"]
        c.call("step", frames=60)
        c.call("load_state", name="test-remote")
        check("save/load state", c.call("status")["frame"] >= f)
        c.call("reset")
        check("reset reboots the disc", c.call("run_until", text="DEMO: ready", max_frames=1500)["found"])
        c.call("reboot", iso=str(ROOT / "projects" / "unittest" / "build" / "unittest.iso"))
        check("reboot into another disc", c.call("run_until", text="TDO:DONE", max_frames=1500)["found"])
        c.call("reboot", iso=str(ISO))
        check("reboot back", c.call("run_until", text="DEMO: ready", max_frames=1500)["found"])
        return c
    except Exception:
        c.close(stop=True)
        raise


def test_gdb(c: SessionClient):
    section("gdb remote protocol")
    gdb = next((shutil.which(x) for x in ("arm-none-eabi-gdb", "gdb-multiarch", "gdb") if shutil.which(x)), None)
    if not gdb:
        print("  SKIP gdb not installed")
        return
    port = c.call("status")["gdb_port"]
    elf = ISO.with_suffix(".elf")
    cmd = [gdb, "-q", "-batch", "-ex", "set endian big", "-ex", "set architecture arm",
           "-ex", f"directory {ROOT / 'projects' / 'demo'}", str(elf),
           "-ex", f"target remote 127.0.0.1:{port}",
           "-ex", "break update_vortex", "-ex", "continue", "-ex", "bt",
           "-ex", "p s_speed", "-ex", "set var s_speed = 7", "-ex", "p s_speed",
           "-ex", "info line *$pc", "-ex", "monitor ps", "-ex", "delete", "-ex", "detach"]
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    out = r.stdout + r.stderr
    check("gdb stops at a function breakpoint with source info", "update_vortex (t_=" in out and "main.c:" in out, out[-500:])
    check("gdb backtrace reaches main", "in main () at src/main.c" in out, out[-500:])
    check("gdb reads + writes globals", "$1 = " in out and "$2 = 7" in out, out[-300:])
    check("gdb monitor commands reach the session", "LaunchMe" in out)
    st = c.call("status")
    check("CPU resumes after detach", not st["halted"])
    check("session value changed by gdb", c.call("read_u32", addr="s_speed")["values"][0] == 7)


def test_mcp():
    section("MCP server")
    try:
        from mcp.client.session import ClientSession
        from mcp.client.stdio import StdioServerParameters, stdio_client
    except ImportError:
        print("  SKIP mcp client not importable")
        return

    async def run():
        p = StdioServerParameters(command=str(ROOT / ".venv" / "bin" / "tdo-mcp"), args=[])
        async with stdio_client(p) as (r, w):
            async with ClientSession(r, w) as s:
                await s.initialize()
                names = {t.name for t in (await s.list_tools()).tools}
                check("MCP exposes control + debug tools",
                      {"emu_boot", "emu_break", "emu_ps", "gdb_run", "emu_input", "emu_cmd"} <= names)

                async def call(n, **a):
                    res = await s.call_tool(n, a)
                    return res

                await call("emu_boot", project_or_iso="demo")
                res = await call("emu_run_until", text="DEMO: ready", max_frames=1500)
                check("MCP emu_run_until", '"found": true' in res.content[0].text)
                res = await call("emu_look", buttons="A", frames=30)
                kinds = [c.type for c in res.content]
                check("MCP emu_look returns text + image", kinds == ["text", "image"], str(kinds))
                res = await call("emu_break", location="update_text")
                res = await call("emu_continue", max_frames=10)
                check("MCP breakpoint + continue", "update_text" in res.content[0].text)
                res = await call("gdb_run", commands=["bt 1"])
                check("MCP gdb_run gives a source backtrace", "main.c" in res.content[0].text,
                      res.content[0].text[-300:])
                await call("emu_break", location="all", remove=True)
                res = await call("emu_ps")
                check("MCP emu_ps", "LaunchMe" in res.content[0].text)
                await call("emu_stop")

    asyncio.run(asyncio.wait_for(run(), 300))


def main():
    if not ISO.exists():
        print("build the demo first: ./3do build demo")
        return 2
    c = test_control()
    try:
        test_gdb(c)
    finally:
        c.close(stop=True)
    test_mcp()
    print(f"{'FAIL' if failures else 'PASS'}: remote control ({len(failures)} failure(s))")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
