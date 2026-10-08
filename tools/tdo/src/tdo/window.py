"""pygame-ce window frontend for a Session.

Keyboard (3DO pad, port 0):
  arrows      D-pad            Z / X / C   A / B / C
  Enter       P (play/pause)   Esc / Backspace   X (stop: games quit to the menu)
  Q / W       L / R shoulder
Host keys:
  F1 screenshot      F2 pause/run          F3 advance one frame (paused)
  F5 save state      F9 load state         F8 reset         F12 halt/continue CPU
  Tab (hold) fast-forward                  Ctrl+Q / Cmd+Q (or close) quit
A connected game controller also works (SDL mapping). Remote control and gdb
keep working while the window is open.
"""
from __future__ import annotations

import collections
import sys
import threading
import time

from .core import repo_root

KEYMAP = {
    "UP": "K_UP", "DOWN": "K_DOWN", "LEFT": "K_LEFT", "RIGHT": "K_RIGHT",
    "A": "K_z", "B": "K_x", "C": "K_c", "P": "K_RETURN", "X": "K_BACKSPACE",
    "L": "K_q", "R": "K_w",
}
PADMAP = {
    "A": "CONTROLLER_BUTTON_X", "B": "CONTROLLER_BUTTON_A", "C": "CONTROLLER_BUTTON_B",
    "P": "CONTROLLER_BUTTON_START", "X": "CONTROLLER_BUTTON_BACK",
    "L": "CONTROLLER_BUTTON_LEFTSHOULDER", "R": "CONTROLLER_BUTTON_RIGHTSHOULDER",
    "UP": "CONTROLLER_BUTTON_DPAD_UP", "DOWN": "CONTROLLER_BUTTON_DPAD_DOWN",
    "LEFT": "CONTROLLER_BUTTON_DPAD_LEFT", "RIGHT": "CONTROLLER_BUTTON_DPAD_RIGHT",
}


class _AudioRing:
    def __init__(self, max_bytes: int):
        self.buf = collections.deque()
        self.size = 0
        self.max = max_bytes
        self.lock = threading.Lock()

    def push(self, b: bytes):
        with self.lock:
            self.buf.append(b)
            self.size += len(b)
            while self.size > self.max:           # drop oldest if we run ahead
                self.size -= len(self.buf.popleft())

    def pull(self, n: int) -> bytes:
        out = bytearray()
        with self.lock:
            while self.buf and len(out) < n:
                chunk = self.buf.popleft()
                need = n - len(out)
                if len(chunk) > need:
                    self.buf.appendleft(chunk[need:])
                    chunk = chunk[:need]
                out += chunk
                self.size -= len(chunk)
        if len(out) < n:
            out += bytes(n - len(out))            # underrun: silence
        return bytes(out)


class Window:
    def __init__(self, session, scale: int = 3, audio: bool = True):
        import pygame
        self.pg = pygame
        self.s = session
        self.scale = scale
        pygame.init()
        pygame.display.set_caption(f"3DO – {session.iso.name}")
        self.w, self.h = 320, 240
        self.screen = pygame.display.set_mode((self.w * scale, self.h * scale))
        self.font = pygame.font.Font(None, 22)
        self.keys = {b: getattr(pygame, k) for b, k in KEYMAP.items()}
        self.pads = []
        try:
            from pygame._sdl2 import controller as sdlc
            sdlc.init()
            self.pads = [sdlc.Controller(i) for i in range(sdlc.get_count()) if sdlc.is_controller(i)]
        except Exception:  # noqa: BLE001 - controller support is best-effort
            pass
        self.state = None
        self.osd, self.osd_until = "", 0.0
        self.last_render = 0.0
        self.dev = None
        if audio:
            try:
                from pygame._sdl2.audio import AUDIO_S16, AudioDevice
                emu = session.emu
                ring = _AudioRing(int(emu.sample_rate) * 4 // 4)   # ~250ms
                emu.on_audio = ring.push

                def cb(_dev, mem):
                    mem[:] = ring.pull(len(mem))

                self.dev = AudioDevice(devicename=None, iscapture=False,
                                       frequency=int(emu.sample_rate), audioformat=AUDIO_S16,
                                       numchannels=2, chunksize=1024, allowed_changes=0, callback=cb)
                self.dev.pause(0)
            except Exception as ex:  # noqa: BLE001
                print(f"[tdo] audio disabled: {ex}", file=sys.stderr)

    def _say(self, text):
        self.osd, self.osd_until = text, time.time() + 2

    def handle_events(self):
        pg, s, emu = self.pg, self.s, self.s.emu
        for ev in pg.event.get():
            if ev.type == pg.QUIT:
                s.quit = True
            elif ev.type == pg.KEYDOWN:
                k = ev.key
                if k == pg.K_q and (ev.mod & (pg.KMOD_CTRL | pg.KMOD_META)):
                    s.quit = True       # Esc is the pad's X (stop), not "quit"
                elif k == pg.K_F1:
                    d = repo_root() / "build" / "screenshots"
                    d.mkdir(parents=True, exist_ok=True)
                    p = d / f"{s.iso.stem}-{emu.frame_count}.png"
                    emu.screenshot(p)
                    self._say(f"saved {p.name}")
                elif k == pg.K_F2:
                    s.mode = "paused" if s.mode == "running" else "running"
                    self._say(s.mode)
                elif k == pg.K_F3 and s.mode == "paused":
                    emu.step(1)
                elif k == pg.K_F5 and not emu.halted:
                    self.state = emu.save_state()
                    self._say("state saved")
                elif k == pg.K_F9 and self.state:
                    emu.load_state(self.state)
                    self._say("state loaded")
                elif k == pg.K_F8:
                    emu.reset()
                    self._say("reset")
                elif k == pg.K_F12:
                    if emu.halted:
                        emu.cont()
                        self._say("CPU continue")
                    else:
                        emu.halt()
                        s.settle_halt()
                        self._say("CPU halted")
        pressed = pg.key.get_pressed()
        held = [b for b, key in self.keys.items() if pressed[key]]
        if pressed[pg.K_ESCAPE] and "X" not in held:
            held.append("X")          # Esc is the pad's X: games quit to the menu
        for pad in self.pads:
            for b, name in PADMAP.items():
                if pad.get_button(getattr(pg, name)):
                    held.append(b)
        s.keyboard = held
        s.fast_forward = bool(pressed[pg.K_TAB])

    def render(self):
        now = time.time()
        if now - self.last_render < 1 / 60:
            return
        self.last_render = now
        pg, emu = self.pg, self.s.emu
        f = emu.frame()
        if f:
            surf = pg.image.frombuffer(f.rgb, (f.width, f.height), "RGB")
            if (f.width, f.height) != (self.w, self.h):
                self.w, self.h = f.width, f.height
                self.screen = pg.display.set_mode((self.w * self.scale, self.h * self.scale))
            self.screen.blit(pg.transform.scale(surf, (self.w * self.scale, self.h * self.scale)), (0, 0))
        status = None
        if emu.halted:
            info = emu.stop_info()
            status = f"HALTED ({info['reason']}) pc={info['pc']:08x} – F12 continue"
        elif self.s.mode == "paused":
            status = "PAUSED – F2 run, F3 frame advance"
        for i, text in enumerate(t for t in (status, self.osd if now < self.osd_until else None) if t):
            self.screen.blit(self.font.render(text, True, (255, 255, 255), (0, 0, 0)), (6, 6 + 20 * i))
        pg.display.flip()

    def close(self):
        if self.dev:
            self.dev.pause(1)
        self.pg.quit()
