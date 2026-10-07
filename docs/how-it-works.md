# How it works

This project connects four things that were never designed to work together:
a 1990s ARM compiler, a libretro emulator, a Python automation layer and AI
agents. This document explains each piece, why it's built the way it is, and
the non-obvious problems that had to be solved.

## The 3DO in one paragraph

The 3DO Interactive Multiplayer (1993) has an ARM60 CPU at 12.5 MHz
(big-endian, ARMv3), 2 MB of DRAM plus 1 MB of VRAM, and two custom chips:
**MADAM**, whose *cel engine* draws scaled, rotated, blended sprites ("cels")
and does matrix maths, and **CLIO** (video, audio DSP, timers, I/O). Games
don't hit the hardware directly. They run on **Portfolio**, a preemptive
multitasking OS with tasks, message ports, semaphores and shared libraries
called *folios* (graphics, audio, math, filesystem). Programs are AIF
executables on a CD whose boot code is RSA-signed.

## 1. Building: a 1990s compiler in a 2020s container

The only complete toolchain is ARM's own SDT 2.51 (the **Norcroft** C
compiler), which is what 3DO developers used. trapexit's
[3do-devkit](https://github.com/trapexit/3do-devkit) packages it with the
Portfolio 2.5 SDK and modern disc tools (`3dt`, `3it`, `modbin`).

The compilers are **32-bit i386 Linux** binaries and the disc tools are x86-64
Linux binaries. Nothing runs them natively on a Mac, and Apple's Rosetta
doesn't do 32-bit. So:

- `docker/Dockerfile` is a tiny amd64 Debian with the i386 C library.
- On an arm64 Docker VM (Apple Silicon), `scripts/build-toolchain-image.sh`
  registers **QEMU user-mode emulation** for i386/x86-64 with the kernel's
  `binfmt_misc`, so the container's x86 binaries run transparently.
- `bin/3do-make` runs `make` in that container, with the repo mounted at
  `/work`, as your own user ID (so the files it creates belong to you). On an
  x86-64 Linux host with i386 libs it runs natively instead.

`sdk/project.mk` holds the build rules shared by every project:

1. `armcc -g -O2 -bigend -apcs 3/32/nofp/swst/...` compiles your C.
2. `armlink -aif -reloc` links a self-relocating **AIF** executable, the
   format Portfolio loads.
3. `modbin` stamps the 3DO header (name, stack size).
4. The devkit's base filesystem (`System/` folder, boot code, `rom_tags`) is
   copied in with your files, and **`3dt pack`** builds an ISO and RSA-signs it
   so the retail BIOS will boot it.
5. The same objects are linked a second time as an **ELF with DWARF** at
   address 0x80, so ELF addresses equal offsets into the loaded AIF (whose
   0x80-byte header precedes the code). This is what makes symbols and gdb work.
6. `tdo.elffix` (run by `3do-make` on the host) repairs three things in
   Norcroft's ELF that break gdb. See [Debug info](#debug-info-and-norcrofts-quirks).

## 2. Emulating: Opera, patched

[Opera](https://github.com/libretro/opera-libretro) is the actively
maintained 3DO emulator (FreeDO → 4DO → Opera). It's a **libretro core**: a
shared library with a small C API (`retro_init`, `retro_load_game`,
`retro_run` = "emulate one frame", `retro_serialize`, ...) plus callbacks to
the host for video, audio, input and settings. It has no window or main loop
of its own.

`scripts/build-emulator.sh` builds it natively (it's plain C) after applying
`emulator/patches/0001-tdo-harness-hooks.patch` and adding
`emulator/ext/opera_harness.c`. The patch adds hooks at a few key points:

- **Debug console.** The Portfolio kernel prints `kprintf`/`printf` output one
  byte at a time to a MADAM register (meant for a dev-station's debug port). We
  copy those bytes into a ring buffer, so you see kernel boot messages and
  everything your program prints.
- **Before each instruction** (only while a breakpoint, tracepoint or step is
  active): breakpoints, single-step, halt requests, tracepoints.
- **Memory accesses** (only while watchpoints exist): data watchpoints.
- **System calls (SWI)**: an optional trace of OS calls with their arguments.

### Stopping the CPU without breaking time

Opera emulates a frame by running the CPU in slices interleaved with scanline
processing (video, timers, interrupts) inside `opera_3do_process_frame`. To
halt at a breakpoint mid-frame, that function was made **resumable**: when the
debugger says stop, it saves its scanline and cycle position and returns
early. `retro_run()` then returns without completing the frame, and the next
call continues the same frame exactly where it stopped. From the 3DO's point
of view, no time passes while you sit at a breakpoint.

## 3. Hosting: Python drives native code

`tools/tdo/src/tdo/core.py` is a minimal libretro **frontend** written with
Python's `ctypes`. It loads the core into the Python process and calls it
directly:

```
Python: lib.retro_run() ─▶ C: emulate one frame ─▶ calls back into Python:
                                                     • input_state(port, id)  → injected buttons
                                                     • video_refresh(pixels)  → keep the frame
                                                     • audio_batch(samples)   → speakers
                           ◀─ returns to Python: read debug text, check halts, handle commands
```

All the heavy work (CPU, graphics, sound) is C. Python only runs between
frames and at a few callbacks per frame, so headless emulation still reaches
~900 frames/s (about 15× real time).

libretro cores keep global state, so **one process = one emulator**. That's
why every emulator runs in its own **session process**.

## 4. Sessions: one emulator, many clients

`tdo.session` wraps an emulator in a server loop:

```
session process
 ├─ control port   JSON lines on 127.0.0.1 ◀── tdo ctl, scripts, the MCP server
 ├─ gdb port       GDB remote serial protocol ◀── gdb, ./3do gdb, MCP gdb_run
 ├─ window         pygame-ce: video, keyboard/gamepad, audio (optional)
 └─ registry       build/sessions/<pid>.json so tools can find it
```

- **Run modes.** `paused` (frames advance only on commands, so agent runs are
  deterministic) and `running` at a chosen speed (1× real time, or turbo).
  Separately, the CPU can be **halted** by the debugger.
- All commands run on the session's own thread between frames, so there's no
  locking inside the emulator.
- The same command set is available everywhere: `./3do ctl <cmd>`, MCP
  `emu_cmd`, gdb `monitor <cmd>`, or raw JSON from any language.

## 5. Seeing inside the 3DO

**Symbols and load address.** Portfolio loads your program wherever it finds
memory. To map symbols to runtime addresses, `tdo.symbols` searches emulated
DRAM for the program's code. It confirms the match because AIF
self-relocation has replaced header word 1 (a `BL`) with a `NOP`. Runtime
address = load base + ELF address. Breakpoints, watchpoints, disassembly,
`read_u32 addr=s_palette` and gdb's `qOffsets` all use this.

**Process list.** `tdo.kernel` reads Portfolio's own data structures from
emulated RAM. It finds `KernelBase` by locating the kernel folio's node (named
"kernel", subsystem 1, type FOLIONODE, with a valid current-task pointer),
then walks the task, ready and wait queues. The structure offsets come from
`tools/probe`, a 3DO program that prints `offsetof()` values for the SDK
structs, so they're right by construction for Portfolio 2.5.

<a id="os-introspection"></a>
**OS introspection.** `tdo.osinspect` goes further and walks the **item
table**. Every Portfolio object is an *item*: a number whose low 12 bits index
`kb_ItemTable` (blocks of 128 `{address, info}` entries) and whose high bits
are a generation count, so a stale number never matches a reused slot. Each
item starts with an `ItemNode` that gives its subsystem (kernel, graphics,
filesystem, audio...), type, name, owner task and size. From there,
type-specific readers decode message ports and their queued messages,
semaphores (owner, waiters), devices and drivers, I/O requests (command,
unit, state, bytes transferred), screens and their framebuffers, and so on.
Memory ownership comes from the two `MemHdr`s (DRAM in 32 KB pages, VRAM in
16 KB pages). Each has a free-page bitmap; each task's `MemList` has an
ownership bitmap. Both are LSB-first, and privileged tasks share one list.
Snapshots record items, tasks and free memory so two points in time can be
diffed. System-call names for the SWI trace are parsed from the SDK's
`__swi(...)` declarations. All of this reads RAM through the harness, so it
works on a halted or crashed program and costs the guest nothing. The UI and
REST surface for it is [DevBench](devbench.md).

**Disassembly** uses Capstone (ARM, big-endian), annotated with symbols.

## 6. Debug info and Norcroft's quirks

Norcroft emits DWARF 2, but in three ways modern gdb rejects. `tdo.elffix`
fixes each after linking:

| Problem | Symptom | Fix |
|---|---|---|
| armlink names every loadable section after the output file | gdb doesn't recognise `.data`/`.bss`, so it relocates code but not data: globals read from (and **write to**) wrong addresses | rename sections by their flags to `.text/.data/.bss` |
| `DW_AT_stmt_list` (and location lists) use `DW_FORM_addr` instead of an offset form | gdb silently ignores **all** line-number info | patch the forms in `.debug_abbrev` to `DW_FORM_data4` (same size, so nothing else moves) |
| `.debug_frame` CFI uses conventions gdb misreads | backtraces show odd addresses and stop after one frame | hide the section; gdb's ARM prologue analyzer unwinds this code correctly |

## 7. The GDB server

`tdo.gdbstub` implements the GDB remote serial protocol on the session
thread (non-blocking). It sends a target description (r0–r15 + cpsr,
big-endian), handles memory/register access, breakpoints and watchpoints
(implemented in the core, not by patching code), step/continue/Ctrl-C, and
`qOffsets` (so gdb relocates the ELF itself). `monitor` forwards to the
session's control API. Attaching halts the CPU; detaching removes gdb's own
breakpoints and resumes.

## 8. Agents: the MCP server

`tdo.mcp_server` is an MCP server (Model Context Protocol; stdio, and HTTP at `/mcp` via DevBench) registered
in `.mcp.json`. It spawns or attaches to sessions and exposes ~50 tools:
build/test, session lifecycle, run control, input for every peripheral,
screenshots (returned as images), GIF capture, logs, process list, symbols,
breakpoints/watchpoints, memory, snapshots, OS introspection (`os_*`), and `gdb_run`, which runs gdb
commands and returns the transcript. `CLAUDE.md` tells Claude the workflow
and the 3DO pitfalls found while building this.

The loop an agent runs:

```
edit C ─▶ build ─▶ emu_boot ─▶ emu_run_until("ready") ─▶ emu_press / emu_look (screenshot + log)
   ▲                                                         │
   └──── emu_break / emu_watch / gdb_run / emu_regs ◀────────┘ (when something's wrong)
```

## 9. Testing

- **On-target tests:** the program calls `tdo_check()` and `tdo_test_done()`,
  which print `TDO:PASS/FAIL/DONE`; `./3do test` turns that into an exit code
  (`projects/unittest` checks compiler, libc, math, kernel and filesystem
  behaviour on the emulated console).
- **Host-side tests:** `projects/<p>/test.py` drives a headless emulator
  (boot, press buttons, measure frame rate, compare screenshots). See
  `projects/demo/test.py`.
- **The harness itself:** `tools/tdo/tests/test_remote.py` (45 checks covering
  control, input, debugger, gdb and MCP).
- `./3do selftest` runs all of it, and CI runs it on Linux.

## 10. Reproducibility

Everything external is pinned: devkit and Opera by commit, BIOS by MD5,
Python dependencies by version range. Setup is idempotent, `./3do doctor`
diagnoses, and the one-line installer re-runs safely to update.
