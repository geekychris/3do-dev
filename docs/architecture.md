# Architecture

```
                 ┌──────────────── host (macOS / Linux) ────────────────┐
 projects/X/src  │  bin/3do-make ──docker──▶ 3do-dev-toolchain image     │
 ───────────────▶│      (amd64 Debian + i386 libc; QEMU on arm64)        │
                 │      armcc/armasm/armlink ─▶ LaunchMe (AIF) + X.elf   │
                 │      modbin ─▶ 3dt pack+sign ─▶ build/X.iso           │
                 │                                                       │
                 │  session process (tdo serve / tdo run)                │
                 │   ├─ core.py     ctypes libretro host ─▶ opera dylib  │
                 │   ├─ control.py  JSON control port (127.0.0.1)        │
                 │   ├─ gdbstub.py  GDB remote serial protocol           │
                 │   ├─ kernel.py   Portfolio task/folio/device lists    │
                 │   ├─ symbols.py  .sym/.elf symbols + load base        │
                 │   └─ window.py   optional pygame window, audio, pad   │
                 │                                                       │
                 │  clients: MCP server (Claude), tdo ctl, gdb, scripts  │
                 └───────────────────────────────────────────────────────┘
```

## Why Docker for the compiler

The devkit ships its Norcroft tools as 32-bit i386 Linux ELF binaries (and
`3dt`/`3it`/`modbin` as x86-64 static ones). Nothing runs them natively on
Apple Silicon, so `bin/3do-make` runs `make` in a minimal amd64 container with
the i386 libc. On arm64 Docker VMs, `scripts/build-toolchain-image.sh`
registers QEMU binfmt handlers (`tonistiigi/binfmt`). Those registrations vanish
when the Docker VM restarts; `3do-make` detects that and says how to fix it.
On x86_64 Linux with i386 libs installed, `3do-make` runs the tools natively.

The devkit is bind-mounted rather than baked into the image, so bumping the
devkit pin doesn't need an image rebuild.

## Build rules

`sdk/project.mk` mirrors the devkit's flags (`-bigend -za1 -zi4 -fpu none
-arch 3 -apcs 3/32/nofp/swst/wide/softfp`, AIF relocatable output, Portfolio
2.5 libs), and adds:

- `sdk/src/*.c` (tdo_log + test helpers) compiled into every project
- base disc = devkit `takeme/{System,AppStartup,BannerScreen,rom_tags,signatures}`
- `takeme/`, `assets/*.png` → cels, `banner.png` → BannerScreen
- `3dt pack` produces a signed ISO, so the retail BIOS boots it

## Emulator

`scripts/build-emulator.sh` checks out Opera at the pinned commit, copies
`emulator/ext/opera_harness.[ch]` in, applies `emulator/patches/*.patch` and
builds natively. See [emulator-harness.md](emulator-harness.md).

`tdo.core.Emulator` is a minimal libretro frontend in ctypes. libretro cores
are process-global, so each emulator lives in its own *session* process
(`tdo.session`); the MCP server, `tdo ctl` and gdb talk to sessions over
localhost sockets, and a core crash can't take the client down. Sessions
register in `build/sessions/` so any tool can find them. Details:
[emulator-harness.md](emulator-harness.md).

## Reproducibility

Everything external is pinned: devkit and Opera by commit (`scripts/lib.sh`),
BIOS by MD5, Python deps by range in `tools/tdo/pyproject.toml`. `./setup.sh`
is idempotent and `./3do selftest` is the end-to-end check.
