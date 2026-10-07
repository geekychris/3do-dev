# DevBench: web UI, REST API and OS introspection

```sh
./3do devbench --open          # http://127.0.0.1:3330/
```

DevBench is one local server that holds everything you can do to a running 3DO:

- a **web UI** (Dashboard, Screen, OS, Debug, Memory, Console, API)
- a **REST API**: every session command plus projects, sessions, screen PNG and an OpenAPI spec
- a **live event stream** (Server-Sent Events) carrying status, the debug console, CPU stop/run and crashes
- the **MCP server** over streamable HTTP at `/mcp`, for agents that prefer HTTP to stdio

It has no build step and no extra services. It's a Starlette app inside the `tdo` venv, and the
UI is a single HTML file (`tools/tdo/src/tdo/web/index.html`). It talks to emulator sessions over
their normal control sockets, so sessions started by `./3do run`, `./3do serve`, MCP or the UI
itself all show up, and several clients can drive the same session.

It binds to 127.0.0.1 by default. `--host 0.0.0.0` exposes it on your network. **There is
no authentication**, so only do that on a network you trust.

![DevBench OS view](images/devbench-os.png)

## OS introspection

Portfolio keeps every OS object (task, message port, semaphore, device, I/O request,
screen, bitmap, sample, file...) as an **item** in a table hanging off `KernelBase`.
`tdo.osinspect` reads that table and the structures it points to straight out of emulated
RAM. Nothing runs on the 3DO, so introspection works on any program, including one that's
crashed or halted at a breakpoint, and it costs the guest no CPU time.

| View | What you see |
|---|---|
| **Summary** | kernel base, current task, item counts by type, DRAM/VRAM used vs free |
| **Tasks** | state (running/ready/waiting), priority, parent thread, signal bits received / waited for, the item it's blocked on, user + supervisor stacks, CPU ms, memory pages owned |
| **Items** | every live item: number, type, subsystem, name, owning task, address, size |
| **Ports** | message ports, their signal and queued messages (reply port, data, size) |
| **Semaphores** | locked?, by whom, nest count, waiters |
| **Devices & I/O** | devices with their driver and open count, drivers, and every IOReq: device, command, unit, offset, bytes transferred, error, done/pending |
| **Folios** | loaded folios (the OS's shared libraries) with open counts and SWI table sizes |
| **Graphics** | screen groups, screens (with their bitmaps, framebuffer addresses and sizes), bitmaps, VDLs |
| **Audio / Files** | audio folio items (templates, instruments, knobs, samples, attachments); filesystems, open files, aliases |
| **Memory map** | each DRAM page (32 KB) and VRAM page (16 KB), coloured by the task (or group of tasks sharing a memory list) that owns it. Click a page to hex-dump it |
| **Snapshots** | take a snapshot, play, diff: items created/deleted (resource leaks), CPU time per task, page ownership changes, free memory delta |
| **Item detail** | click any row: type-specific fields plus the raw node bytes, with a jump to the memory view |

How the walk works, and the structure offsets it relies on (which `tools/probe` measured
on target), are in [how-it-works.md](how-it-works.md#os-introspection) and in the comments
at the top of `tools/tdo/src/tdo/osinspect.py`.

## Debugging from the browser

- **Registers** (symbolized pc/lr), **disassembly** with symbols. Click a line to toggle
  a breakpoint.
- **Breakpoints, watchpoints** (read/write/access), **tracepoints** (log r0-r3 and the
  caller without stopping).
- **Halt / continue / step instruction.** When the CPU stops, a banner shows where and why.
- **System calls (SWI snoop):** every `SWI` the program makes, named from the SDK headers
  (`kernel.WaitSignal`, `graphics.DrawCels`, ...), with counts, call site and arguments.
- **Profiler:** samples the PC at random points inside frames and reports the percentage
  of CPU per function, with idle and OS time bucketed separately.
- **Crashes:** aborts and exceptions the OS prints are captured with registers and log
  context, and pushed to the UI as they happen.
- **Memory:** hex view by address or symbol, search DRAM for text or bytes, write words.
- **Screen:** live view, on-screen control pad (or your keyboard), peripheral selection
  (joypad, flightstick with analog sliders, mouse, light gun: click the screen to shoot),
  screenshot and GIF capture.

## REST API

Everything is JSON. Errors use real status codes with `{"error": "..."}`:
400 for bad arguments, 404 for an unknown session or command, 409 when a command fails,
422 when a build or test fails.

| Method | Path | |
|---|---|---|
| GET | `/api/health` | liveness |
| GET | `/api/commands` | command catalogue: name, doc, params, group |
| GET | `/api/openapi.json` | OpenAPI 3 spec (import it into Postman, Insomnia, ...) |
| GET | `/api/projects` | projects and their ISOs |
| POST | `/api/projects/{name}/build` | compile; `{"exit", "output"}` |
| POST | `/api/projects/{name}/test` | run its tests |
| GET | `/api/sessions` | running sessions (and which one is current) |
| POST | `/api/sessions` | start one: `{"target": "demo", "window": false, "mode": "paused", "bios": "panafz10.bin"}` |
| POST | `/api/sessions/{pid}/select` | make it the current session |
| DELETE | `/api/sessions/{pid}` | stop it |
| GET / POST | `/api/sessions/{pid}/cmd/{command}` | run any session command, with args as query params or a JSON body |
| GET / POST | `/api/cmd/{command}` | same, on the current session |
| GET | `/api/sessions/{pid}/screen.png?scale=2`, `/api/screen.png` | current frame |
| GET | `/api/events?session={pid}` | SSE: `status` (twice a second), `log`, `stop`, `run`, `crash`, `event` |
| GET | `/api/artifact?path=build/...` | fetch a file a command wrote (screenshots, GIFs) |
| * | `/mcp` | MCP (streamable HTTP) |

The commands are the same ones `./3do ctl help` lists (~70) and are documented in
[emulator-harness.md](emulator-harness.md). The OS ones are `os_summary`,
`os_items [type=]`, `os_item item=N`, `os_tasks`, `os_ports`, `os_semaphores`,
`os_devices`, `os_folios`, `os_graphics`, `os_audio`, `os_files`,
`os_memory [per_page=]`, `os_snapshot [name=]`, `os_diff [a=] [b=]`.

```sh
B=http://127.0.0.1:3330
curl -s -X POST $B/api/sessions -d '{"target":"demo"}'                 # boot (paused)
curl -s -X POST $B/api/cmd/run_until -d '{"text":"DEMO: ready"}'
curl -s $B/api/cmd/os_tasks | jq '.tasks[] | {name, state, cpu_ms}'
curl -s -X POST $B/api/cmd/press -d '{"buttons":"A"}'
curl -s $B/api/screen.png?scale=2 -o shot.png
curl -s -X POST $B/api/cmd/os_snapshot -d '{"name":"a"}'
curl -s -X POST $B/api/cmd/step -d '{"frames":600}'
curl -s "$B/api/cmd/os_diff?a=a" | jq '.items_created'
curl -sN "$B/api/events" | grep --line-buffered '^event: log' -A1   # follow the console
```

From Python, it's plain HTTP:

```python
import requests
B = "http://127.0.0.1:3330/api"
pid = requests.post(f"{B}/sessions", json={"target": "rock_blaster"}).json()["pid"]
requests.post(f"{B}/cmd/run_until", json={"text": "AMIGA3DO: frame=250"})
print(requests.get(f"{B}/cmd/os_memory", params={"per_page": "false"}).json())
```

## MCP

The stdio server (`.mcp.json`, used by Claude Code) and DevBench's `/mcp` are the same
server. The OS tools are:

| Tool | |
|---|---|
| `os_overview` | summary |
| `os_inspect(view)` | tasks, items, ports, semaphores, devices, folios, graphics, audio, files |
| `os_item(n)` | one item in depth |
| `os_memory_map` | page ownership as text |
| `os_snapshot` / `os_diff` | what changed: leaks, CPU per task |
| `emu_syscalls` | SWI snoop: start, stop, fetch |
| `emu_profile` | where the CPU time goes |
| `emu_crashes` | OS-reported aborts |

A client connected over HTTP with no session of its own uses the newest running
session. A session you start in the UI can be handed straight to an agent.

## Compared with amiga_mcp's DevBench

This takes its layout from the Amiga DevBench (tabs, a single SSE bus, a memory-map
view, snapshot diffs, crash capture, a SnoopDos-style call log). The main difference: the
Amiga version gets its data from a daemon running on the Amiga, which talks over serial.
Here everything comes from the emulator, so:

- no guest code is needed, and nothing is perturbed (no daemon task, no extra memory)
- it works while the CPU is halted or the program has crashed
- system-call tracing sees every `SWI` from every task, not just patched library calls

The other side of this is that it doesn't work on real hardware, and app-cooperative
features (registered variables and hooks) aren't implemented. Use `tdo_log()` lines plus
`watch`/`read_u32` on globals instead.
