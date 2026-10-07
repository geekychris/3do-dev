"""3DO DevBench: REST API + live event stream + MCP-over-HTTP + web UI.

    tdo devbench [--port 3330]          (./3do devbench)
    open http://127.0.0.1:3330/

One server manages builds and emulator sessions and exposes everything a
session can do (67+ commands) as REST:

    GET  /api/health                         liveness
    GET  /api/commands                       command catalogue (name, doc, params, group)
    GET  /api/openapi.json                   OpenAPI 3 spec for all of the below
    GET  /api/projects                       projects + ISOs
    POST /api/projects/{name}/build          compile (returns output)
    POST /api/projects/{name}/test           run its tests
    GET  /api/sessions                       running sessions (+ which is current)
    POST /api/sessions                       start one: {"target": "demo", "window": false, "mode": "paused"}
    POST /api/sessions/{pid}/select          make it the current session
    DELETE /api/sessions/{pid}               stop it
    GET|POST /api/sessions/{pid}/cmd/{command}   run any command (query params or JSON body)
    GET|POST /api/cmd/{command}              same, on the current session
    GET  /api/sessions/{pid}/screen.png      current frame as PNG (?scale=2)
    GET  /api/events?session={pid}           Server-Sent Events: status, log, stop, crash, event
    GET  /api/artifact?path=build/x.gif      a generated file under build/
    /mcp                                     the MCP server (streamable HTTP)

Errors use real HTTP status codes with {"error": "..."} bodies.
"""
from __future__ import annotations

import asyncio
import base64
import json
import os
import subprocess
import threading
import time
from pathlib import Path

from starlette.concurrency import run_in_threadpool
from starlette.requests import Request
from starlette.responses import FileResponse, JSONResponse, Response, StreamingResponse
from starlette.routing import Mount, Route
from starlette.staticfiles import StaticFiles

from .control import Controller, ProtocolError, SessionClient, list_sessions
from .core import repo_root

ROOT = repo_root()
WEB = Path(__file__).parent / "web"

# ------------------------------------------------------------------ state


class Bench:
    def __init__(self):
        self.clients: dict[int, SessionClient] = {}
        self.current: int | None = None
        self.lock = threading.Lock()

    def client(self, pid: int | None) -> SessionClient:
        pid = pid or self.current
        if pid is None:
            ss = list_sessions()
            if not ss:
                raise LookupError("no running session; POST /api/sessions to start one")
            pid = ss[0]["pid"]
            self.current = pid
        with self.lock:
            c = self.clients.get(pid)
            if c is None or not c.alive:
                info = next((s for s in list_sessions() if s["pid"] == pid), None)
                if info is None:
                    self.clients.pop(pid, None)
                    raise LookupError(f"no session with pid {pid}")
                c = SessionClient(info["control_port"])
                c.info = info
                self.clients[pid] = c
            return c

    def spawned(self, c: SessionClient):
        pid = c.info["pid"]
        with self.lock:
            self.clients[pid] = c
        self.current = pid
        return pid


bench = Bench()

# ------------------------------------------------------------------ helpers


def ok(data, status: int = 200):
    return JSONResponse(data, status_code=status)


def err(msg: str, status: int):
    return JSONResponse({"error": msg}, status_code=status)


async def call(pid, cmd: str, args: dict):
    try:
        c = bench.client(pid)
    except LookupError as ex:
        return None, err(str(ex), 404)
    try:
        return await run_in_threadpool(lambda: c.call(cmd, **args)), None
    except ProtocolError as ex:
        msg = str(ex)
        if "unknown command" in msg:
            return None, err(msg, 404)
        if msg.startswith(("TypeError", "ValueError", "KeyError")):
            return None, err(msg, 400)
        if "closed the connection" in msg or "connection failed" in msg:
            return None, err(msg, 502)
        return None, err(msg, 409)


def _coerce(v: str):
    try:
        return json.loads(v)
    except ValueError:
        return v


async def args_of(request: Request) -> dict:
    args = {k: _coerce(v) for k, v in request.query_params.items() if k not in ("session",)}
    if request.method in ("POST", "PUT"):
        body = await request.body()
        if body.strip():
            try:
                data = json.loads(body)
            except ValueError:
                raise ValueError("body must be JSON") from None
            if not isinstance(data, dict):
                raise ValueError("body must be a JSON object of arguments")
            args.update(data)
    return args

# ------------------------------------------------------------------ routes


async def index(request: Request):
    return FileResponse(WEB / "index.html")


async def health(request: Request):
    return ok({"ok": True, "time": time.time(), "sessions": len(list_sessions()), "current": bench.current})


async def commands(request: Request):
    return ok({"commands": Controller.describe()})


async def openapi(request: Request):
    paths = {}
    for c in Controller.describe():
        props = {p["name"]: {"description": f"{p['type']}" + ("" if p["required"] else f" (default {p['default']!r})")}
                 for p in c["params"]}
        op = {"summary": c["summary"], "description": c["doc"], "tags": [c["group"]],
              "requestBody": {"content": {"application/json": {"schema": {
                  "type": "object", "properties": props,
                  "required": [p["name"] for p in c["params"] if p["required"]]}}}},
              "responses": {"200": {"description": "result"}, "400": {"description": "bad arguments"},
                            "404": {"description": "no session / unknown command"},
                            "409": {"description": "command failed"}}}
        paths[f"/api/cmd/{c['name']}"] = {"post": op}
        paths[f"/api/sessions/{{pid}}/cmd/{c['name']}"] = {"post": dict(op, parameters=[
            {"name": "pid", "in": "path", "required": True, "schema": {"type": "integer"}}])}
    fixed = {
        "/api/health": {"get": {"summary": "liveness"}},
        "/api/commands": {"get": {"summary": "command catalogue"}},
        "/api/projects": {"get": {"summary": "projects and ISOs"}},
        "/api/projects/{name}/build": {"post": {"summary": "build a project"}},
        "/api/projects/{name}/test": {"post": {"summary": "test a project"}},
        "/api/sessions": {"get": {"summary": "list sessions"},
                          "post": {"summary": "start a session: {target, window, mode, bios}"}},
        "/api/sessions/{pid}": {"delete": {"summary": "stop a session"}},
        "/api/sessions/{pid}/select": {"post": {"summary": "make current"}},
        "/api/sessions/{pid}/screen.png": {"get": {"summary": "current frame (PNG)"}},
        "/api/events": {"get": {"summary": "Server-Sent Events (status, log, stop, crash, event)"}},
    }
    paths.update(fixed)
    return ok({"openapi": "3.0.3", "info": {"title": "3DO DevBench", "version": "1.0",
                                            "description": __doc__}, "paths": paths})


async def projects(request: Request):
    out = []
    for d in sorted((ROOT / "projects").iterdir()):
        if (d / "Makefile").exists():
            isos = sorted((d / "build").glob("*.iso"))
            arcade = {}
            if (d / "arcade.txt").exists():
                for line in (d / "arcade.txt").read_text().splitlines():
                    k, _, v = line.partition("=")
                    arcade[k.strip()] = v.strip()
            out.append({"name": d.name, "iso": str(isos[0].relative_to(ROOT)) if isos else None,
                        "built": isos[0].stat().st_mtime if isos else None,
                        "title": arcade.get("title"), "test": (d / "test.py").exists()})
    return ok({"projects": out})


def _run(cmd, timeout=1800):
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    return {"exit": r.returncode, "output": (r.stdout + r.stderr)[-20000:]}


async def build(request: Request):
    name = request.path_params["name"]
    if not (ROOT / "projects" / name / "Makefile").exists():
        return err(f"no project {name}", 404)
    res = await run_in_threadpool(_run, [str(ROOT / "bin" / "3do-make"), "-C", str(ROOT / "projects" / name)])
    return ok(res, 200 if res["exit"] == 0 else 422)


async def test(request: Request):
    name = request.path_params["name"]
    if not (ROOT / "projects" / name / "Makefile").exists():
        return err(f"no project {name}", 404)
    res = await run_in_threadpool(_run, [str(ROOT / "3do"), "test", name])
    return ok(res, 200 if res["exit"] == 0 else 422)


async def sessions(request: Request):
    if request.method == "POST":
        try:
            a = await args_of(request)
        except ValueError as ex:
            return err(str(ex), 400)
        target = a.get("target") or a.get("project") or a.get("iso")
        if not target:
            return err("target (project name or ISO path) required", 400)
        from .session import resolve_iso
        try:
            iso = str(resolve_iso(target))
        except FileNotFoundError as ex:
            return err(str(ex), 404)
        try:
            c = await run_in_threadpool(lambda: SessionClient.spawn(
                iso, bios=a.get("bios", "panafz10.bin"), window=bool(a.get("window")),
                mode=a.get("mode") or ("running" if a.get("window") else "paused")))
        except ProtocolError as ex:
            return err(str(ex), 500)
        pid = bench.spawned(c)
        st = await run_in_threadpool(lambda: c.call("status"))
        return ok({"pid": pid, "status": st}, 201)
    ss = list_sessions()
    for s in ss:
        s["current"] = s["pid"] == bench.current
    return ok({"sessions": ss, "current": bench.current})


async def session_one(request: Request):
    pid = int(request.path_params["pid"])
    if request.method == "DELETE":
        try:
            c = bench.client(pid)
        except LookupError as ex:
            return err(str(ex), 404)
        await run_in_threadpool(lambda: c.close(stop=True))
        bench.clients.pop(pid, None)
        if bench.current == pid:
            bench.current = None
        return ok({"stopped": pid})
    return err("method not allowed", 405)


async def select(request: Request):
    pid = int(request.path_params["pid"])
    try:
        bench.client(pid)
    except LookupError as ex:
        return err(str(ex), 404)
    bench.current = pid
    return ok({"current": pid})


async def run_cmd(request: Request):
    pid = request.path_params.get("pid")
    pid = int(pid) if pid else (int(request.query_params["session"]) if "session" in request.query_params else None)
    try:
        args = await args_of(request)
    except ValueError as ex:
        return err(str(ex), 400)
    res, e = await call(pid, request.path_params["command"], args)
    return e or ok(res)


async def screen_png(request: Request):
    pid = int(request.path_params["pid"]) if "pid" in request.path_params else None
    scale = int(request.query_params.get("scale", 1))
    res, e = await call(pid, "screenshot", {"scale": scale})
    if e:
        return e
    return Response(base64.b64decode(res["png_b64"]), media_type="image/png",
                    headers={"Cache-Control": "no-store", "X-Frame": str(res["frame"])})


async def events(request: Request):
    """SSE: status every ~0.5 s; new debug log lines; stop/run transitions; crashes; session events."""
    pid = int(request.query_params["session"]) if "session" in request.query_params else None

    async def gen():
        log_next = None
        ev_next = 0
        crash_n = 0
        halted = None
        yield "retry: 2000\n\n"
        while True:
            if await request.is_disconnected():
                return
            st, e = await call(pid, "status", {})
            if e:
                yield f"event: error\ndata: {e.body.decode()}\n\n"
                await asyncio.sleep(2)
                continue
            yield f"event: status\ndata: {json.dumps(st)}\n\n"
            # a new stop while already halted (halt -> continue -> breakpoint between polls)
            # changes the stop record, so compare that as well as the halted flag
            key = (st.get("halted"), json.dumps(st.get("stop"), sort_keys=True) if st.get("halted") else None)
            if key != halted:
                halted = key
                yield f"event: {'stop' if key[0] else 'run'}\ndata: {json.dumps(st.get('stop') or {})}\n\n"
            if log_next is None:
                log_next = max(0, st["log_lines"] - 200)
            lg, e = await call(pid, "log", {"since": log_next, "advance": False, "max_lines": 500})
            if not e and lg["lines"]:
                yield f"event: log\ndata: {json.dumps({'from': log_next, 'lines': lg['lines']})}\n\n"
            if not e:
                log_next = lg["next"]
            ev, e = await call(pid, "events", {"since": ev_next})
            if not e:
                for line in ev["events"]:
                    yield f"event: event\ndata: {json.dumps(line)}\n\n"
                ev_next = ev["next"]
            cr, e = await call(pid, "crashes", {})
            if not e and len(cr["crashes"]) > crash_n:
                for c in cr["crashes"][crash_n:]:
                    yield f"event: crash\ndata: {json.dumps(c)}\n\n"
                crash_n = len(cr["crashes"])
            await asyncio.sleep(0.5)

    return StreamingResponse(gen(), media_type="text/event-stream",
                             headers={"Cache-Control": "no-store", "X-Accel-Buffering": "no"})


async def artifact(request: Request):
    """Serve a generated file (screenshots, GIFs) from the repo's build/ directory only."""
    rel = request.query_params.get("path", "")
    p = (ROOT / rel).resolve()
    if not str(p).startswith(str((ROOT / "build").resolve()) + os.sep) or not p.is_file():
        return err("not found (only files under build/ are served)", 404)
    return FileResponse(p)


async def favicon(request: Request):
    svg = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16"><rect width="16" height="16" rx="3" '
           'fill="#e07a5f"/><text x="8" y="12" font-size="9" text-anchor="middle" font-family="sans-serif" '
           'font-weight="bold" fill="#111">3D</text></svg>')
    return Response(svg, media_type="image/svg+xml")


ROUTES = [
    Route("/", index),
    Route("/favicon.ico", favicon),
    Route("/api/health", health),
    Route("/api/commands", commands),
    Route("/api/openapi.json", openapi),
    Route("/api/projects", projects),
    Route("/api/projects/{name}/build", build, methods=["POST"]),
    Route("/api/projects/{name}/test", test, methods=["POST"]),
    Route("/api/sessions", sessions, methods=["GET", "POST"]),
    Route("/api/sessions/{pid:int}", session_one, methods=["DELETE"]),
    Route("/api/sessions/{pid:int}/select", select, methods=["POST"]),
    Route("/api/sessions/{pid:int}/screen.png", screen_png),
    Route("/api/sessions/{pid:int}/cmd/{command}", run_cmd, methods=["GET", "POST"]),
    Route("/api/screen.png", screen_png),
    Route("/api/cmd/{command}", run_cmd, methods=["GET", "POST"]),
    Route("/api/events", events),
    Route("/api/artifact", artifact),
    Mount("/static", app=StaticFiles(directory=str(WEB)), name="static"),
]


def make_app():
    """Our REST routes on the MCP server's Starlette app (keeps its lifespan for /mcp)."""
    from .mcp_server import server as mcp
    app = mcp.streamable_http_app(streamable_http_path="/mcp")
    app.router.routes[0:0] = ROUTES
    return app


def main(port: int = 3330, host: str = "127.0.0.1", open_browser: bool = False):
    import uvicorn
    url = f"http://{host}:{port}/"
    print(f"3DO DevBench on {url}  (REST /api, events /api/events, MCP /mcp)", flush=True)
    if open_browser:
        threading.Timer(1.0, lambda: os.system(f"open {url} >/dev/null 2>&1 || xdg-open {url} >/dev/null 2>&1")).start()
    uvicorn.run(make_app(), host=host, port=port, log_level="warning")
