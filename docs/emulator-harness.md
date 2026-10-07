# Remote control, debugging and the emulator harness

Every running emulator is a **session**: one process holding the Opera core, a
JSON control port, a GDB remote port and an optional window. Agents, scripts,
gdb and people can all drive the same session at once.

```
                     ┌────────────── tdo session process ───────────────┐
 MCP (Claude) ──┐    │  control port (JSON lines) ── Controller ──┐     │
 tdo ctl ───────┼───▶│  gdb port (RSP stub) ──────────────────────┼──▶ Opera core
 your harness ──┘    │  window (pygame: keys, pad, audio) ────────┘  (patched)
 gdb ───────────────▶│  registry: build/sessions/<pid>.json             │
                     └──────────────────────────────────────────────────┘
```

## Starting sessions

| | |
|---|---|
| `./3do run demo` | window, free-running, control **127.0.0.1:7330**, gdb **:2330** |
| `./3do serve demo` | headless, **paused**, random free ports (printed + registered) |
| `./3do serve demo --window --mode running --port 7400 --gdb-port 2400` | anything in between |
| `./3do sessions` | list running sessions (pid, iso, ports, window) |
| MCP `emu_boot("demo", window=True)` | Claude starts one; `emu_attach()` joins an existing one |

**Run modes:** `paused` advances frames only on `step`/`press`/`run_until`
commands, which keeps agent runs deterministic. `running` free-runs at `speed`
× real time (0 = as fast as possible). Separately, the **CPU can be halted** by
the debugger (breakpoint, watchpoint, halt, single-step). While halted,
nothing advances until `continue`/`stepi`, and the interrupted frame later
resumes exactly where it stopped.

## Control API

`./3do ctl <cmd> key=value ...` (newest session, or `--port N`), MCP `emu_cmd`,
gdb `monitor <cmd> key=value`, or raw JSON lines on the control port:

```json
{"id": 1, "cmd": "press", "args": {"buttons": "A"}}
{"id": 1, "ok": true, "result": {"frame": 412}}
```

`./3do ctl help` prints the full list. Grouped:

| Area | Commands |
|---|---|
| session | `status` `run(speed)` `pause` `step(frames)` `run_until(text,max_frames)` `reset` `reboot(iso)` `load(iso)` `set_option` `options` `events` `quit` |
| input | `press(buttons,hold_frames,release_frames,port)` `hold` `release` `sequence(steps)` `set_device(port,device)` `analog(port,lx,ly,rx,ry)` `mouse(port,dx,dy,buttons)` `lightgun(port,x,y,buttons,offscreen)` |
| output | `screenshot(scale,path)` `record_gif(path,frames,every)` `log(since)` `crashes` |
| OS | `ps` (tasks), `folios`, `devices`, `os_summary` `os_items(type)` `os_item(item)` `os_tasks` `os_ports` `os_semaphores` `os_devices` `os_folios` `os_graphics` `os_audio` `os_files` `os_memory(per_page)` `os_snapshot(name)` `os_diff(a,b)` – see [devbench.md](devbench.md) |
| symbols | `symbols(pattern)` `addr2sym(addr)` `disasm(addr,count)` |
| debugger | `regs` `set_reg` `halt` `continue(max_frames)` `stepi(count)` `stop_info` `break(addr)` `delete` `watch(addr,length,kind)` `unwatch` `breakpoints` `tracepoint(action,addr)` `swi_trace` `swi_calls` (named system calls) `profile(frames,interval,top)` |
| memory | `read_mem` `read_u32` `write_mem` `write_u32` `find_mem(text|hex)` |
| states | `save_state(name)` `load_state(name)` |

Anywhere an address is expected you can pass a number, a symbol (`main`,
`s_palette`), `symbol+0x10`, or `pc`.

**Devices:** `joypad`, `flightstick` (analog `lx/ly` stick and `ry` throttle,
plus `FIRE`), `mouse` (relative motion), `lightgun`/`arcade_lightgun` (screen
pixels), `orbatak_trackball`, `none`. Pad buttons: `A B C P X L R UP DOWN LEFT
RIGHT`.

The same commands are available over HTTP from [DevBench](devbench.md)
(`POST /api/cmd/<name>` with a JSON body), along with an OpenAPI spec.

## GDB

```sh
./3do gdb demo                  # spawns a headless session, gdb with symbols + sources
./3do run demo  &  ./3do gdb demo --attach      # debug the window you're watching
gdb projects/demo/build/demo.elf -ex 'set endian big' -ex 'target remote :2330'
```

Supported: registers (r0–r15, cpsr), memory read/write, breakpoints (`Z0/Z1`,
in the core so no code patching), watchpoints (`Z2/3/4`, CPU accesses only,
not DMA), `stepi`/`next`/`finish`, Ctrl-C, `qOffsets` (gdb relocates the ELF to
the load address automatically), `monitor` commands, source lines, locals,
structs and backtraces. Attaching halts the CPU; detaching removes gdb's
breakpoints and resumes.

Symbols: every build writes `build/<name>.elf` (the same objects linked with
DWARF 2) and `build/<name>.sym`. `tdo.elffix` repairs two armlink quirks that
would otherwise break gdb: section names, and the `DW_FORM_addr` forms that
made gdb drop all line info. It also hides Norcroft's CFI, which mis-unwinds,
so gdb uses its prologue analyzer instead.

## How it works

The Opera patch (`emulator/patches/0001-tdo-harness-hooks.patch`) adds
observe-only hooks feeding `emulator/ext/opera_harness.c`:

| Hook | Where | Purpose |
|---|---|---|
| debug console | MADAM ID register write | the kernel prints kprintf/printf here; captured into a ring |
| before-exec | `opera_3do_process_frame` loop | breakpoints, step, halt, tracepoints; returns early to stop mid-frame |
| memory access | `mreadw/mreadb/mwritew/mwriteb` (DRAM) | data watchpoints (only checked while any exist) |
| SWI | `decode_swi` | system-call trace |
| frame | `retro_run` | counts *completed* frames |

`opera_3do_process_frame` was made resumable: when the debugger stops the
CPU, it saves the scanline/cycle position and the next call continues the
same frame, so emulated timing is unaffected. Snapshots are refused while
halted mid-frame.

Exported C API (`tdo_*`, next to `retro_*`): debug text, regs get/set,
memory, SWI trace, tracepoints, `tdo_dbg_halt/continue/step/halted/stop_info`,
`tdo_bp_*`, `tdo_wp_*`. See `emulator/ext/opera_harness.h`.

**Process list:** `ps` finds KernelBase by locating the kernel folio's
ItemNode (named "kernel", subsystem 1, type FOLIONODE), then walks
`kb_Tasks`, `kb_TaskReadyQ` and `kb_TaskWaitQ`. Structure offsets were
printed by `tools/probe` (compiled with Norcroft against the Portfolio 2.5
headers). Rerun it if the OS version changes.

**Load base:** found by locating LaunchMe's code in DRAM; once self-relocation
has run, AIF header word 1 has become a NOP. Runtime address = base + ELF
address (the ELF is linked at 0x80 to match the AIF header).

## Guest memory map

| Range | |
|---|---|
| `0x000000–0x1FFFFF` | DRAM (2 MB): kernel, OS tasks, your program (typically at 0x70000) |
| `0x200000–0x2FFFFF` | VRAM (1 MB): framebuffers |
| `0x3000000–0x30FFFFF` | BIOS ROM |

## Python

```python
from tdo.control import SessionClient
s = SessionClient.spawn("projects/demo/build/demo.iso")      # or SessionClient(7330)
s.call("run_until", text="DEMO: ready")
s.call("break", addr="update_plasma"); print(s.call("step", frames=5)["stopped"])
print(s.call("disasm", addr="pc", count=8)["listing"])
s.close(stop=True)
```

`tools/tdo/tests/test_remote.py` exercises all of this end to end (run by
`./3do selftest`).
