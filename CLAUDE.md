# 3DO development workspace

Homebrew for the 3DO Interactive Multiplayer (ARM60 @ 12.5 MHz, Portfolio OS,
MADAM cel engine). Code is C89 compiled with Norcroft ARM C 4.91 from
trapexit's 3do-devkit; it runs in a patched Opera emulator we can script.

## Commands

```sh
./3do build <project>        # -> projects/<p>/build/<p>.iso (signed)
./3do test <project>         # projects/<p>/test.py if present, else waits for TDO:DONE
./3do log <project> --frames 600 [--press A@400]
./3do shot <project> -o out.png --until "ready" [--press A@400]
./3do run <project>          # window for the human (control :7330, gdb :2330)
./3do ctl <cmd> key=value    # remote-control the newest session (./3do ctl help)
./3do gdb <project>          # gdb with source-level symbols
./3do new <name>             # from sdk/template
./3do selftest               # build + test everything (do this before saying "done")
./3do doctor                 # environment problems
./3do devbench               # web UI + REST (/api) + MCP over HTTP (/mcp) on :3330
```

## MCP tools (server "3do", .mcp.json)

Inner loop: `build` -> `emu_boot` -> `emu_run_until("<your log line>")` ->
`emu_look` / `emu_press` / `emu_log`. Sessions are separate processes:
`emu_boot(p)` = private headless (paused – you step it), `emu_boot(p, window=True)`
= user-visible, `emu_attach()` = join `./3do run X`. Also: `emu_run`/`emu_pause`,
`emu_sequence`, `emu_input` (flightstick/mouse/lightgun), `emu_record_gif`,
`emu_ps` (OS tasks), `emu_reset`/`emu_reboot`/`emu_restart`.

Debugging: `emu_break("func")` + `emu_continue(max_frames)`, `emu_watch("global")`,
`emu_stepi`, `emu_regs`, `emu_disasm`, `emu_symbols`, `emu_read_mem("sym", as_u32=True)`,
`emu_trace` (tracepoints, SWI trace), `gdb_run([...])` for source-level gdb
(break file:line, bt, info locals, p *ptr). `emu_cmd("help")` lists everything.

OS introspection (reads Portfolio structures from RAM, works while halted/crashed):
`os_overview`, `os_inspect("tasks"|"items"|"ports"|"semaphores"|"devices"|"folios"|
"graphics"|"audio"|"files")`, `os_item(n)`, `os_memory_map`, `os_snapshot` then
`os_diff` (leaked items, CPU per task), `emu_syscalls("start")`/`("fetch")` (named SWIs),
`emu_profile(frames)`, `emu_crashes`. See docs/devbench.md.
Details: docs/emulator-harness.md.

- Frames are 1/60 s of guest time; headless runs ~15x real time.
- `emu_run_until` only matches output emitted after the last `emu_log` call
  (and after the last reset/reboot).
- Make programs observable: log state changes with `tdo_log("TAG: key=%d\n", v)`.
- A halted CPU (breakpoint) freezes everything; `emu_status` says where. Resume
  with `emu_continue` before expecting frames to advance.

## Rules learned the hard way

- **kprintf() garbles args after the 3rd** (stack varargs don't survive the SWI).
  Use `tdo_log()` from `sdk/include/tdo.h` for >3 args. kprintf/printf/tdo_log
  output appears in the harness debug log (the kernel writes it to MADAM).
- Returning from `main()` makes the OS relaunch LaunchMe; tests judge run #1.
- `DoControlPad` is edge-triggered: in scripted input, step ~30 frames after
  the program's ready line before the first press, or it is missed.
- Norcroft is C89: declarations at the top of blocks, no `//` in our code,
  no `inline`, no C99 headers. Compiler warnings about devkit headers are noise.
- No FPU: avoid `float`/`double` in hot code; use frac16 (16.16) + operamath
  (`MulSF16`, `SinF16` – 256 angle units per circle; call `OpenMathFolio()`).
- Cels: `ccb_HDX/HDY` are 12.20 fixed, `ccb_VDX/VDY` 16.16, `ccb_XPos/YPos` 16.16.
  Uncoded 16bpp pixel value 0 is transparent. PIXC 0x1F00 = opaque;
  `MF_k|SF_8` scales brightness by k/8; `2S_CFBD|2D_1` adds the framebuffer
  (additive) – overlapping texel seams then double-add, so prefer opaque.
- Measure performance instead of guessing: count emulator frames between two
  periodic status lines (see projects/demo/test.py). The demo's 80x60 software
  plasma dropped it to 30 fps; 64x48 holds 60.
- Default BIOS panafz10.bin needs signed discs; `3dt pack` signs automatically.

## Layout

- `projects/<name>/` – `Makefile` (NAME + include sdk/project.mk), `src/`,
  optional `takeme/` (disc files), `assets/*.png` (-> cels/*.cel), `banner.png`,
  `test.py` (host-side test).
- `sdk/` – shared make rules, `tdo.h` helpers, project template.
- `emulator/` – our Opera patch (`patches/`) and harness extension (`ext/`).
- `tools/tdo/` – Python emulator host, CLI, MCP server.
- `third_party/3do-devkit/` – compilers, headers (`include/3do`), libs, and
  the original SDK docs in `docs/3dosdk/` (HTML; grep them for API details) +
  `examples/`. Fetched by setup, not committed. More at https://3dodev.com.
