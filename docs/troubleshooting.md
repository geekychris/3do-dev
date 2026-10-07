# Troubleshooting

Run `./3do doctor` first. It checks every component and prints the fix.

## Setup and builds

**`docker daemon not reachable`.** Start your Docker app (Docker Desktop /
Rancher Desktop / OrbStack) or `colima start`. On Linux:
`sudo systemctl start docker`, and make sure you're in the `docker` group
(`groups | grep docker`; log out and in after `usermod`).

**Build fails with `Exec format error`, or `3do-make` says the compilers can't
execute.** The Docker VM lost its QEMU x86 emulation (common after a Docker
Desktop / Rancher restart on Apple Silicon). Fix:
`./scripts/build-toolchain-image.sh`.

**`binfmt registration failed`.** Your Docker engine doesn't allow privileged
containers. Enable them, or register QEMU some other way: Docker Desktop
ships it built in; on Linux, `sudo apt install qemu-user-static binfmt-support`.

**Lots of compiler `Warning:` lines.** Normal. The SDK headers trigger
"padding inserted in struct" and "apcs /wide" warnings. Real problems show as
`Error:` or `Serious error:`.

**`armlink: undefined symbol`.** You called a function from a library that
isn't linked, or misspelled it. `sdk/project.mk` links all standard
Portfolio libs. Add others with `EXTRA_LIBS` in your project Makefile.

**`no ISO in projects/x/build`.** Build first (`./3do build x`), or use
`./3do run x`, which builds for you.

## Emulator

**Process killed (exit 137) or hangs on start (macOS).** A running emulator
had the old core loaded when it was rebuilt. Close all emulator windows and
sessions (`./3do sessions`, `./3do ctl quit`), then retry. Current scripts
install the core by rename to avoid this, but an older running process can
still hold the old file.

**Black screen / `no BIOS ROM found`.** `./scripts/fetch-bios.sh`, or check
`bios/panafz10.bin` exists (MD5 in `bios/README.md`).

**Disc boots to the 3DO logo and stops.** The ISO isn't signed or the
program crashed early. `./3do log <project>` shows the kernel messages. Our
builds are signed by `3dt pack`; for unsigned third-party discs use
`--bios panafz10-norsa.bin`.

**Input ignored in a scripted test.** `DoControlPad` is edge-triggered and
misses a button that's already down when the program first reads it. Step
~30 frames after the program's "ready" log line before pressing.

**`run_until` returns immediately.** It matches output after the last `log`
read. Reboots and resets move that cursor forward, but if you read the log
yourself, note `next` and pass `since=`.

**No sound.** Audio plays only in windowed sessions (`./3do run`). Check the
terminal for `audio disabled: ...`.

**Port already in use.** `./3do run demo --port 7331 --gdb-port 2331`, or
find the old session with `./3do sessions` and `./3do ctl --port N quit`.

## Debugging

**gdb: globals read wrong values or `list` shows nothing.** The ELF wasn't
post-processed. `bin/3do-make` runs `tdo.elffix` automatically if `.venv`
exists; run it by hand with `.venv/bin/python -m tdo.elffix projects/x/build/x.elf`.

**gdb: breakpoint never hits.** Check the program is loaded
(`./3do ctl status` shows `load_base`), and the function is actually called.
`./3do ctl tracepoint action=add addr=func` logs calls without stopping.

**gdb `next` jumps around / locals look stale.** Code is built with `-O2`.
Build with `make DEBUG=1` (`./3do build x DEBUG=1`) for `-O0`.

**Everything froze.** The CPU is probably halted at a breakpoint or
watchpoint: `./3do ctl status` shows `halted` and where. Use
`./3do ctl continue` (or F12 in the window).

**`cannot snapshot while halted mid-frame`.** Continue (or `stepi` until the
frame ends) before `save_state`.

## Claude Code / MCP

**Claude doesn't see the `3do` tools.** Approve the MCP server when prompted
(or `/mcp` in Claude Code). The server is `./.venv/bin/tdo-mcp`, so run
setup first.

**`the session process has exited`.** Look at `build/sessions/spawn-*.log`.
`emu_restart` respawns it.
