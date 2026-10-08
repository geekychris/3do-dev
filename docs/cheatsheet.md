# Cheatsheet

Everything runs from the repo root with `./3do`.

## Play

```sh
./3do run arcade          # the arcade: all 19 games behind a menu
./3do run rock_blaster    # one game on its own
```

`run` rebuilds the project first, so it always shows your latest changes.

```sh
./3do setup               # first time only: toolchain, emulator, Python env
```

In the arcade, **Up/Down** chooses, **Left/Right** pages, **A** (Z key) plays. In a game,
**Esc** (the pad's X) returns to the menu. Screenshots and controls for every game:
[games.md](games.md).

## Keys in the emulator window

| Key | 3DO pad | | Key | Emulator |
|---|---|---|---|---|
| Arrows | D-pad | | F1 | screenshot (build/screenshots/) |
| Z / X / C | A / B / C | | F2 | pause / run |
| Enter | P (play/pause) | | F3 | one frame (while paused) |
| Esc or Backspace | X (stop: back to the menu) | | F5 / F9 | save / load state |
| Q / W | L / R shoulders | | F8 | reset |
| | | | Tab (hold) | fast-forward |
| | | | Ctrl+Q / Cmd+Q | quit the emulator |

A USB or Bluetooth game controller works too (Start = P, Back/Select = X).

## Develop

```sh
./3do new mygame              # new project from the template
./3do build mygame            # -> projects/mygame/build/mygame.iso (signed)
./3do run mygame              # window, plus remote control :7330 and gdb :2330
./3do test mygame             # headless test (projects/mygame/test.py)
./3do log mygame --frames 600 # debug console output
./3do shot mygame -o out.png --until "ready"
./3do profile mygame --press A@400    # where the CPU time goes
./3do gdb mygame              # source-level debugger
./3do selftest                # build and test everything (do before pushing)
./3do doctor                  # check the environment
```

## Watch and inspect a running game

```sh
./3do devbench --open         # web UI on http://localhost:3330
```

Dashboard, live screen, OS tasks/items/memory, debugger, memory viewer, console,
and the REST API. Start a session from the UI, or attach to a `./3do run`
window. Details: [devbench.md](devbench.md).

From Claude Code the same things are MCP tools (server `3do`): `build`,
`emu_boot`, `emu_run_until`, `emu_look`, `emu_press`, `os_overview`, ...

## Remote control from a shell

```sh
./3do sessions                      # what's running
./3do ctl press buttons=A           # press A in the newest session
./3do ctl screenshot path=x.png
./3do ctl help                      # every command
```

## On a real 3DO

`projects/<game>/build/<game>.iso` and `projects/arcade/build/arcade.iso` are
signed disc images: burn one to a CD-R at the lowest speed, or load it on an
optical drive emulator. See [getting-started.md](getting-started.md#running-on-a-real-3do).

## When something's wrong

| Problem | Try |
|---|---|
| Window shows an old build | quit it (Ctrl+Q) and `./3do run arcade` again |
| Build fails oddly | `./3do clean <project>` then build; `./3do doctor` |
| DevBench OS views say "not running yet" | press Run in the banner (the session is paused) |
| No sound | check the host volume; `./3do log <game>` should say "audio ready (DSP voices)" |

More in [troubleshooting.md](troubleshooting.md).
