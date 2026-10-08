# Getting started

This guide takes you from a clean machine to building and debugging your own
3DO program. Expect 5–15 minutes, mostly downloads.

## 1. What gets installed

| Thing | Where | Size |
|---|---|---|
| This repo | wherever you clone it (`~/3do-dev` with the one-liner) | < 1 MB |
| 3do-devkit (compilers, SDK, docs, examples) | `third_party/3do-devkit` | ~650 MB |
| Opera emulator source + build | `third_party/opera-libretro`, `emulator/build/` | ~60 MB |
| Toolchain Docker image `3do-dev-toolchain:1` | your Docker engine | ~120 MB |
| BIOS ROMs (3 × 1 MB) | `bios/` | 3 MB |
| Python env (numpy, pillow, pygame-ce, mcp, capstone) | `.venv/` | ~150 MB |

Nothing is installed system-wide unless you opt into prerequisite
auto-install (`TDO_DEV_AUTO_INSTALL=1`), which uses your package manager.

## 2. Prerequisites

### macOS (Apple Silicon or Intel)

```sh
xcode-select --install          # git, make, clang
brew install uv                 # or: brew install python@3.12
brew install gdb                # optional: source-level debugging
```

Docker: any of **Docker Desktop**, **Rancher Desktop**, **OrbStack**
(`brew install --cask orbstack`) or **colima** (`brew install colima docker && colima start`).
Start it before running setup.

On Apple Silicon, the devkit's x86 compilers run under QEMU inside Docker.
Setup registers the QEMU handlers automatically (`tonistiigi/binfmt`). If your
Docker VM is ever recreated or restarted, run `./scripts/build-toolchain-image.sh`
again; `./3do doctor` will tell you when.

### Debian / Ubuntu (x86_64 or arm64)

```sh
sudo apt update
sudo apt install -y git curl build-essential python3 python3-venv docker.io gdb-multiarch
sudo usermod -aG docker "$USER"     # then log out and back in
```

On x86_64 the compilers run natively inside Docker. On arm64 Linux, setup
registers QEMU binfmt handlers as on the Mac.

### Fedora / Arch

Install the equivalents (`make gcc git curl python3 docker gdb`), enable the
Docker service and add yourself to the `docker` group. `scripts/install.sh`
knows the package names (`TDO_DEV_AUTO_INSTALL=1`).

### Windows

Use **WSL2 with Ubuntu** and follow the Debian instructions inside it. To use
Docker Desktop's WSL integration instead of `docker.io`, turn it on for your
distro. Emulator windows need WSLg (Windows 11) or an X server.

## 3. Install

One-liner:

```sh
curl -fsSL https://raw.githubusercontent.com/geekychris/3do-dev/main/scripts/install.sh | bash
```

Or by hand:

```sh
git clone https://github.com/geekychris/3do-dev.git
cd 3do-dev
./setup.sh
```

`setup.sh` runs these steps, each safe to re-run on its own:

| Step | Script | What it does |
|---|---|---|
| devkit | `scripts/fetch-devkit.sh` | shallow-clones trapexit/3do-devkit at the pinned commit |
| toolchain | `scripts/build-toolchain-image.sh` | checks Docker, registers QEMU if needed, builds the image, verifies `armcc` and `3dt` run |
| emulator | `scripts/build-emulator.sh` | clones Opera at the pinned commit, applies our patch, builds the core |
| BIOS | `scripts/fetch-bios.sh` | downloads ROMs from 3dodev.com, verifies MD5s |
| python | `scripts/setup-python.sh` | creates `.venv` with the `tdo` package |
| tests | `./3do selftest` | builds every project, runs their tests and the remote-control suite |

Options: `./setup.sh --quick` (skip tests), `--no-bios` (bring your own ROM).

## 4. Verify

```sh
./3do doctor
```

Every line should be a ✓. Each ✗ comes with the exact fix command.

## 5. Play and explore

```sh
./3do run demo
```

| Key | 3DO pad |
|---|---|
| arrows | D-pad |
| Z / X / C | A / B / C |
| Enter | P (play/pause) |
| Backspace | X (stop) |
| Q / W | L / R |

Host keys: F1 screenshot (to `build/screenshots/`), F2 pause/run, F3 advance
one frame, F5/F9 save/load state, F8 reset, F12 halt/continue the CPU, Tab
fast-forward, Esc quit.

The terminal shows the 3DO's debug console: kernel boot messages and
everything the program prints.

While it runs, in another terminal:

```sh
./3do ctl status
./3do ctl ps                      # tasks running on the 3DO
./3do ctl press buttons=A         # changes the demo's palette
./3do ctl screenshot path=a.png
./3do gdb demo --attach           # then: break update_plasma / continue / bt / p s_speed
```

### Running on a real 3DO

`./3do build <project>` produces `projects/<project>/build/<project>.iso`: a
complete 3DO disc image (Opera file system, boot code, banner, `LaunchMe`),
RSA-signed by `3dt pack` the same way retail discs were. A retail console
boots a disc whose signature verifies; that check is the protection, so
there's nothing to circumvent. The default emulator BIOS performs the same check, which is
why every test here runs the signed image.

To play on hardware, either burn the ISO to a CD-R as a single data track at
the lowest speed (older 3DO lasers are picky about CD-R media), or copy it
to an optical drive emulator. The games have been developed and tested in
the emulator only. The hardware matches what the emulator models (2 MB
DRAM, 1 MB VRAM, 12.5 MHz ARM60), but check on real hardware before relying
on exact timing. `tdo_log`/`kprintf` output goes to the debug port, which
retail consoles don't have; it's harmless there.

## 6. Your first program

```sh
./3do new hello2
./3do run hello2
```

`projects/hello2/src/main.c` is a small, commented program: a double-buffered
display, text, a moving sprite and controller input. Edit it, save, and re-run
`./3do run hello2`; it rebuilds automatically. Then continue with
[writing-programs.md](writing-programs.md).

## 7. Updating

```sh
git pull && ./setup.sh
```

The one-line installer does the same. Upstream versions (devkit, Opera) are
pinned in `scripts/lib.sh`. To try newer ones, change `DEVKIT_COMMIT` /
`OPERA_COMMIT` and re-run setup; `./3do selftest` tells you if anything broke.

## 8. Uninstalling

```sh
rm -rf ~/3do-dev                  # or wherever you cloned it
docker rmi 3do-dev-toolchain:1
```

Optionally remove the QEMU binfmt handlers: `docker run --privileged --rm tonistiigi/binfmt --uninstall qemu-*`.
