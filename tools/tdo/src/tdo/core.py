"""Minimal, scriptable libretro host for the (patched) Opera 3DO core.

This deliberately implements only what Opera needs. It runs headless; the
interactive window (window.py) and the MCP server (mcp_server.py) are built on
top of the same `Emulator` object.

The patched core also exports a `tdo_*` debug API (see
emulator/ext/opera_harness.h) which is surfaced here as methods.
"""
from __future__ import annotations

import ctypes as C
import os
import struct
import sys
import threading
from dataclasses import dataclass, field
from pathlib import Path

# ---------------------------------------------------------------- libretro ABI

RETRO_ENVIRONMENT_EXPERIMENTAL = 0x10000
ENV_GET_CAN_DUPE = 3
ENV_SET_MESSAGE = 6
ENV_SHUTDOWN = 7
ENV_SET_PERFORMANCE_LEVEL = 8
ENV_GET_SYSTEM_DIRECTORY = 9
ENV_SET_PIXEL_FORMAT = 10
ENV_SET_INPUT_DESCRIPTORS = 11
ENV_GET_VARIABLE = 15
ENV_SET_VARIABLES = 16
ENV_GET_VARIABLE_UPDATE = 17
ENV_SET_SUPPORT_NO_GAME = 18
ENV_GET_LOG_INTERFACE = 27
ENV_GET_SAVE_DIRECTORY = 31
ENV_SET_SYSTEM_AV_INFO = 32
ENV_SET_CONTROLLER_INFO = 35
ENV_SET_GEOMETRY = 37
ENV_SET_SERIALIZATION_QUIRKS = 44
ENV_GET_CORE_OPTIONS_VERSION = 52

PIXFMT_0RGB1555, PIXFMT_XRGB8888, PIXFMT_RGB565 = 0, 1, 2

RETRO_DEVICE_NONE = 0
RETRO_DEVICE_JOYPAD = 1
RETRO_DEVICE_MOUSE = 2
RETRO_DEVICE_LIGHTGUN = 4
RETRO_DEVICE_ANALOG = 5


def _subclass(base, n):
    return ((n + 1) << 8) | base


# 3DO peripherals Opera emulates, by the name the API uses
DEVICES = {
    "none": RETRO_DEVICE_NONE,
    "joypad": RETRO_DEVICE_JOYPAD,
    "flightstick": _subclass(RETRO_DEVICE_JOYPAD, 0),
    "mouse": RETRO_DEVICE_MOUSE,
    "lightgun": RETRO_DEVICE_LIGHTGUN,
    "arcade_lightgun": _subclass(RETRO_DEVICE_LIGHTGUN, 0),
    "orbatak_trackball": _subclass(RETRO_DEVICE_JOYPAD, 1),
}
MOUSE_IDS = dict(X=0, Y=1, LEFT=2, RIGHT=3, MIDDLE=6, BUTTON4=9)
LIGHTGUN_IDS = dict(TRIGGER=2, AUX_A=3, START=6, SELECT=7, SCREEN_X=13, SCREEN_Y=14,
                    IS_OFFSCREEN=15, RELOAD=16)
STOP_REASONS = {0: "none", 1: "halt", 2: "breakpoint", 3: "step", 4: "watchpoint"}
WATCH_TYPES = {"write": 1, "read": 2, "access": 3}
RETRO_MEMORY_SAVE_RAM = 0
RETRO_MEMORY_SYSTEM_RAM = 2
RETRO_MEMORY_VIDEO_RAM = 3

# libretro joypad ids
_RJ = dict(B=0, Y=1, SELECT=2, START=3, UP=4, DOWN=5, LEFT=6, RIGHT=7,
           A=8, X=9, L=10, R=11)

# 3DO controller button -> libretro id, per opera-libretro lr_input.c
BUTTONS: dict[str, int] = {
    "A": _RJ["Y"],
    "B": _RJ["B"],
    "C": _RJ["A"],
    "P": _RJ["START"],      # "Play/Pause"
    "X": _RJ["SELECT"],     # "Stop"
    "L": _RJ["L"],
    "R": _RJ["R"],
    "UP": _RJ["UP"],
    "DOWN": _RJ["DOWN"],
    "LEFT": _RJ["LEFT"],
    "RIGHT": _RJ["RIGHT"],
    "FIRE": 12,             # flightstick trigger (libretro R2)
}


@dataclass
class PortInput:
    """Everything a host can inject on one controller port."""
    device: str = "joypad"
    buttons: int = 0                      # libretro joypad bitmask
    analog: dict = field(default_factory=lambda: {"lx": 0, "ly": 0, "rx": 0, "ry": 0})
    mouse_dx: int = 0                      # consumed by the next poll
    mouse_dy: int = 0
    mouse_buttons: set = field(default_factory=set)   # LEFT/RIGHT/MIDDLE/BUTTON4
    gun_x: int = 160                       # screen pixels
    gun_y: int = 120
    gun_buttons: set = field(default_factory=set)     # TRIGGER/RELOAD/START/SELECT/AUX_A
    gun_offscreen: bool = False


class retro_system_info(C.Structure):
    _fields_ = [("library_name", C.c_char_p), ("library_version", C.c_char_p),
                ("valid_extensions", C.c_char_p), ("need_fullpath", C.c_bool),
                ("block_extract", C.c_bool)]


class retro_game_info(C.Structure):
    _fields_ = [("path", C.c_char_p), ("data", C.c_void_p),
                ("size", C.c_size_t), ("meta", C.c_char_p)]


class retro_game_geometry(C.Structure):
    _fields_ = [("base_width", C.c_uint), ("base_height", C.c_uint),
                ("max_width", C.c_uint), ("max_height", C.c_uint),
                ("aspect_ratio", C.c_float)]


class retro_system_timing(C.Structure):
    _fields_ = [("fps", C.c_double), ("sample_rate", C.c_double)]


class retro_system_av_info(C.Structure):
    _fields_ = [("geometry", retro_game_geometry), ("timing", retro_system_timing)]


class retro_variable(C.Structure):
    _fields_ = [("key", C.c_char_p), ("value", C.c_char_p)]


# The log callback is variadic in C. We only take the format string; the
# varargs are ignored (it's diagnostics only).
LOG_CB = C.CFUNCTYPE(None, C.c_int, C.c_char_p)


class retro_log_callback(C.Structure):
    _fields_ = [("log", LOG_CB)]


ENV_CB = C.CFUNCTYPE(C.c_bool, C.c_uint, C.c_void_p)
VIDEO_CB = C.CFUNCTYPE(None, C.c_void_p, C.c_uint, C.c_uint, C.c_size_t)
AUDIO_CB = C.CFUNCTYPE(None, C.c_int16, C.c_int16)
AUDIO_BATCH_CB = C.CFUNCTYPE(C.c_size_t, C.POINTER(C.c_int16), C.c_size_t)
INPUT_POLL_CB = C.CFUNCTYPE(None)
INPUT_STATE_CB = C.CFUNCTYPE(C.c_int16, C.c_uint, C.c_uint, C.c_uint, C.c_uint)

# ---------------------------------------------------------------- paths


def repo_root() -> Path:
    env = os.environ.get("TDO_ROOT")
    if env:
        return Path(env).resolve()
    # tools/tdo/src/tdo/core.py -> repo root
    return Path(__file__).resolve().parents[4]


def default_core_path() -> Path:
    ext = {"darwin": "dylib", "win32": "dll"}.get(sys.platform, "so")
    return repo_root() / "emulator" / "build" / f"opera_libretro.{ext}"


def default_system_dir() -> Path:
    return repo_root() / "bios"


# ---------------------------------------------------------------- emulator


@dataclass
class Frame:
    width: int
    height: int
    rgb: bytes  # packed RGB888, width*height*3

    def to_image(self):
        from PIL import Image
        return Image.frombytes("RGB", (self.width, self.height), self.rgb)


@dataclass
class TraceHit:
    frame: int
    pc: int
    r0: int
    r1: int
    r2: int
    r3: int
    sp: int
    lr: int
    cpsr: int
    hits: int


@dataclass
class SwiCall:
    frame: int
    swi: int
    pc: int
    r0: int
    r1: int
    r2: int
    r3: int


class Emulator:
    """One running instance of the Opera core.

    libretro cores are process-global singletons, so only one Emulator may
    exist per process.
    """

    _instance_lock = threading.Lock()
    _alive = False

    def __init__(self, core_path: str | os.PathLike | None = None,
                 system_dir: str | os.PathLike | None = None,
                 save_dir: str | os.PathLike | None = None,
                 bios: str = "panafz10.bin",
                 options: dict[str, str] | None = None,
                 verbose: bool = False):
        with Emulator._instance_lock:
            if Emulator._alive:
                raise RuntimeError("only one Emulator per process (libretro cores are singletons)")
            Emulator._alive = True

        self.core_path = Path(core_path or default_core_path())
        self.system_dir = Path(system_dir or default_system_dir())
        self.save_dir = Path(save_dir or (repo_root() / "build" / "saves"))
        self.save_dir.mkdir(parents=True, exist_ok=True)
        self.verbose = verbose
        if not self.core_path.exists():
            raise FileNotFoundError(f"core not found: {self.core_path} (run ./setup.sh)")
        if not (self.system_dir / bios).exists():
            raise FileNotFoundError(f"BIOS {bios} not found in {self.system_dir} (run scripts/fetch-bios.sh)")

        self.options: dict[str, str] = {
            "opera_bios": bios,
            "opera_vdlp_pixel_format": "XRGB8888",
            "opera_nvram_storage": "per game",
            "opera_dsp_threaded": "disabled",  # deterministic stepping
        }
        if options:
            self.options.update(options)
        self._options_dirty = False

        self._pixfmt = PIXFMT_0RGB1555
        self._frame: Frame | None = None
        self.audio_buffer = bytearray()        # interleaved s16 stereo
        self.audio_capture = False
        self.on_audio = None                   # optional callable(bytes)
        self.ports = [PortInput(), PortInput()]
        self.frame_count = 0                   # completed frames (VBLs)
        self.debug_log = []                    # list[str] of complete lines
        self._debug_partial = ""
        self.core_log: list[str] = []
        self.game_path: str | None = None
        self.av_info = retro_system_av_info()

        self._keep = []  # keep ctypes objects alive
        self._sysdir_b = str(self.system_dir).encode()
        self._savedir_b = str(self.save_dir).encode()
        self._opt_cache: dict[str, C.c_char_p] = {}

        self.lib = C.CDLL(str(self.core_path))
        self._bind()
        self._setup_callbacks()
        self.lib.retro_init()
        self.has_harness = hasattr(self.lib, "tdo_version")
        self.debug_v2 = self.has_harness and self.lib.tdo_version() >= 2

    # ------------------------------------------------------------ binding
    def _bind(self):
        L = self.lib
        L.retro_api_version.restype = C.c_uint
        L.retro_get_system_info.argtypes = [C.POINTER(retro_system_info)]
        L.retro_get_system_av_info.argtypes = [C.POINTER(retro_system_av_info)]
        L.retro_load_game.argtypes = [C.POINTER(retro_game_info)]
        L.retro_load_game.restype = C.c_bool
        L.retro_serialize_size.restype = C.c_size_t
        L.retro_serialize.argtypes = [C.c_void_p, C.c_size_t]
        L.retro_serialize.restype = C.c_bool
        L.retro_unserialize.argtypes = [C.c_void_p, C.c_size_t]
        L.retro_unserialize.restype = C.c_bool
        L.retro_get_memory_data.argtypes = [C.c_uint]
        L.retro_get_memory_data.restype = C.c_void_p
        L.retro_get_memory_size.argtypes = [C.c_uint]
        L.retro_get_memory_size.restype = C.c_size_t
        L.retro_set_controller_port_device.argtypes = [C.c_uint, C.c_uint]
        if hasattr(L, "tdo_version"):
            L.tdo_version.restype = C.c_uint32
            L.tdo_frame_count.restype = C.c_uint64
            L.tdo_debug_read.argtypes = [C.c_char_p, C.c_size_t]
            L.tdo_debug_read.restype = C.c_size_t
            L.tdo_kprintf_swi_enable.argtypes = [C.c_int]
            L.tdo_get_regs.argtypes = [C.POINTER(C.c_uint32)]
            L.tdo_mem_read.argtypes = [C.c_uint32, C.c_char_p, C.c_uint32]
            L.tdo_mem_read.restype = C.c_uint32
            L.tdo_mem_write.argtypes = [C.c_uint32, C.c_char_p, C.c_uint32]
            L.tdo_mem_write.restype = C.c_uint32
            L.tdo_swi_trace_enable.argtypes = [C.c_int]
            L.tdo_swi_trace_read.argtypes = [C.POINTER(C.c_uint32), C.c_uint32]
            L.tdo_swi_trace_read.restype = C.c_uint32
            L.tdo_trace_add.argtypes = [C.c_uint32]
            L.tdo_trace_add.restype = C.c_int
            L.tdo_trace_remove.argtypes = [C.c_uint32]
            L.tdo_trace_remove.restype = C.c_int
            L.tdo_trace_read.argtypes = [C.POINTER(C.c_uint32), C.c_uint32]
            L.tdo_trace_read.restype = C.c_uint32
            if L.tdo_version() >= 2:
                L.tdo_set_reg.argtypes = [C.c_int, C.c_uint32]
                L.tdo_dbg_halted.restype = C.c_int
                L.tdo_dbg_stop_info.argtypes = [C.POINTER(C.c_uint32)]
                L.tdo_bp_add.argtypes = [C.c_uint32]
                L.tdo_bp_add.restype = C.c_int
                L.tdo_bp_remove.argtypes = [C.c_uint32]
                L.tdo_bp_remove.restype = C.c_int
                L.tdo_wp_add.argtypes = [C.c_uint32, C.c_uint32, C.c_int]
                L.tdo_wp_add.restype = C.c_int
                L.tdo_wp_remove.argtypes = [C.c_uint32, C.c_uint32, C.c_int]
                L.tdo_wp_remove.restype = C.c_int

    def _setup_callbacks(self):
        env = ENV_CB(self._env)
        video = VIDEO_CB(self._video)
        audio = AUDIO_CB(self._audio)
        audio_batch = AUDIO_BATCH_CB(self._audio_batch)
        poll = INPUT_POLL_CB(lambda: None)
        state = INPUT_STATE_CB(self._input_state)
        self._log_cb = LOG_CB(self._log)
        self._keep += [env, video, audio, audio_batch, poll, state, self._log_cb]
        L = self.lib
        L.retro_set_environment(env)
        L.retro_set_video_refresh(video)
        L.retro_set_audio_sample(audio)
        L.retro_set_audio_sample_batch(audio_batch)
        L.retro_set_input_poll(poll)
        L.retro_set_input_state(state)

    # ------------------------------------------------------------ callbacks
    def _log(self, level, fmt):
        msg = (fmt or b"").decode("latin-1").rstrip()
        self.core_log.append(msg)
        del self.core_log[:-500]
        if self.verbose:
            print(f"[core:{level}] {msg}", file=sys.stderr)

    def _env(self, cmd, data):
        cmd &= ~RETRO_ENVIRONMENT_EXPERIMENTAL
        if cmd == ENV_GET_SYSTEM_DIRECTORY:
            C.cast(data, C.POINTER(C.c_char_p))[0] = self._sysdir_b
            return True
        if cmd == ENV_GET_SAVE_DIRECTORY:
            C.cast(data, C.POINTER(C.c_char_p))[0] = self._savedir_b
            return True
        if cmd == ENV_SET_PIXEL_FORMAT:
            self._pixfmt = C.cast(data, C.POINTER(C.c_int))[0]
            return self._pixfmt in (PIXFMT_0RGB1555, PIXFMT_XRGB8888, PIXFMT_RGB565)
        if cmd == ENV_GET_VARIABLE:
            var = C.cast(data, C.POINTER(retro_variable))[0]
            key = var.key.decode()
            if key in self.options:
                # The core keeps the returned char* around, so hand it a
                # buffer we own for the lifetime of the emulator.
                want = self.options[key].encode()
                buf = self._opt_cache.get(key)
                if buf is None or buf.value != want:
                    buf = C.create_string_buffer(want)
                    self._opt_cache[key] = buf
                value_field = data + retro_variable.value.offset
                C.cast(value_field, C.POINTER(C.c_void_p))[0] = C.addressof(buf)
                return True
            return False
        if cmd == ENV_SET_VARIABLES:
            # legacy "Desc; default|other|..." – record defaults for unset keys
            arr = C.cast(data, C.POINTER(retro_variable))
            i = 0
            while arr[i].key:
                key = arr[i].key.decode()
                desc = (arr[i].value or b"").decode("latin-1")
                if ";" in desc and key not in self.options:
                    default = desc.split(";", 1)[1].strip().split("|")[0]
                    if default:
                        self.options[key] = default
                i += 1
            return True
        if cmd == ENV_GET_VARIABLE_UPDATE:
            C.cast(data, C.POINTER(C.c_bool))[0] = self._options_dirty
            self._options_dirty = False
            return True
        if cmd == ENV_GET_CORE_OPTIONS_VERSION:
            C.cast(data, C.POINTER(C.c_uint))[0] = 0
            return True
        if cmd == ENV_GET_LOG_INTERFACE:
            C.cast(data, C.POINTER(retro_log_callback))[0].log = self._log_cb
            return True
        if cmd in (ENV_GET_CAN_DUPE,):
            C.cast(data, C.POINTER(C.c_bool))[0] = True
            return True
        if cmd == ENV_SET_SYSTEM_AV_INFO:
            self.av_info = C.cast(data, C.POINTER(retro_system_av_info))[0]
            return True
        if cmd in (ENV_SET_GEOMETRY, ENV_SET_INPUT_DESCRIPTORS, ENV_SET_CONTROLLER_INFO,
                   ENV_SET_PERFORMANCE_LEVEL, ENV_SET_SUPPORT_NO_GAME,
                   ENV_SET_SERIALIZATION_QUIRKS, ENV_SET_MESSAGE):
            return True
        if cmd == ENV_SHUTDOWN:
            return True
        return False

    def _video(self, data, width, height, pitch):
        if not data:  # dupe frame
            return
        size = pitch * height
        raw = C.string_at(data, size)
        self._frame_raw = (raw, width, height, pitch, self._pixfmt)
        self._frame = None  # convert lazily

    def _audio(self, left, right):
        if self.audio_capture or self.on_audio:
            b = struct.pack("<hh", left, right)
            if self.audio_capture:
                self.audio_buffer += b
            if self.on_audio:
                self.on_audio(b)

    def _audio_batch(self, data, frames):
        if self.audio_capture or self.on_audio:
            b = C.string_at(data, frames * 4)
            if self.audio_capture:
                self.audio_buffer += b
            if self.on_audio:
                self.on_audio(b)
        return frames

    def _input_state(self, port, device, index, id_):
        if port > 1:
            return 0
        p = self.ports[port]
        if device == RETRO_DEVICE_JOYPAD:
            return 1 if (p.buttons >> id_) & 1 else 0
        if device == RETRO_DEVICE_ANALOG:
            key = ("l" if index == 0 else "r") + ("x" if id_ == 0 else "y")
            return int(max(-32768, min(32767, p.analog.get(key, 0))))
        if device == RETRO_DEVICE_MOUSE:
            if id_ == MOUSE_IDS["X"]:
                v, p.mouse_dx = p.mouse_dx, 0
                return int(max(-32768, min(32767, v)))
            if id_ == MOUSE_IDS["Y"]:
                v, p.mouse_dy = p.mouse_dy, 0
                return int(max(-32768, min(32767, v)))
            for name, i in MOUSE_IDS.items():
                if i == id_:
                    return 1 if name in p.mouse_buttons else 0
            return 0
        if device == RETRO_DEVICE_LIGHTGUN:
            w, h = (self.frame().width, self.frame().height) if self.frame() else (320, 240)
            if id_ == LIGHTGUN_IDS["SCREEN_X"]:
                return int(max(-32767, min(32767, p.gun_x * 65534 // max(w - 1, 1) - 32767)))
            if id_ == LIGHTGUN_IDS["SCREEN_Y"]:
                return int(max(-32767, min(32767, p.gun_y * 65534 // max(h - 1, 1) - 32767)))
            if id_ == LIGHTGUN_IDS["IS_OFFSCREEN"]:
                return 1 if p.gun_offscreen else 0
            for name, i in LIGHTGUN_IDS.items():
                if i == id_:
                    return 1 if name in p.gun_buttons else 0
        return 0

    # ------------------------------------------------------------ lifecycle
    def system_info(self) -> dict:
        info = retro_system_info()
        self.lib.retro_get_system_info(C.byref(info))
        return {"name": info.library_name.decode(), "version": info.library_version.decode(),
                "extensions": info.valid_extensions.decode()}

    def load(self, game_path: str | os.PathLike):
        p = str(Path(game_path).resolve())
        if not Path(p).exists():
            raise FileNotFoundError(p)
        gi = retro_game_info(path=p.encode(), data=None, size=0, meta=None)
        self._keep.append(gi)
        if not self.lib.retro_load_game(C.byref(gi)):
            raise RuntimeError(f"core refused to load {p}: " + " | ".join(self.core_log[-5:]))
        self.lib.retro_get_system_av_info(C.byref(self.av_info))
        self.game_path = p
        for port, pi in enumerate(self.ports):
            self.lib.retro_set_controller_port_device(port, DEVICES[pi.device])
        return self

    def reboot(self, game_path: str | os.PathLike | None = None):
        """Power-cycle: tear the core down completely and boot `game_path`
        (default: the current disc). Clears RAM, breakpoints and harness state."""
        path = str(game_path or self.game_path)
        if self.game_path:
            self.lib.retro_unload_game()
        self.lib.retro_deinit()
        self.game_path = None
        self._frame = None
        self._frame_raw = None
        self._debug_partial = ""
        self.lib.retro_init()
        if self.has_harness:   # drop output from the previous boot
            buf = C.create_string_buffer(65536)
            while self.lib.tdo_debug_read(buf, 65536):
                pass
            if self.debug_v2:
                self.lib.tdo_bp_clear()
                self.lib.tdo_wp_clear()
                self.lib.tdo_trace_clear()
        self.debug_log.append(f"---- tdo: reboot {Path(path).name} ----")
        return self.load(path)

    def reset(self):
        self.lib.retro_reset()

    def close(self):
        if getattr(self, "lib", None) is not None:
            try:
                if self.game_path:
                    self.lib.retro_unload_game()
                self.lib.retro_deinit()
            finally:
                self.lib = None
                Emulator._alive = False

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    @property
    def fps(self) -> float:
        return self.av_info.timing.fps or 60.0

    @property
    def sample_rate(self) -> float:
        return self.av_info.timing.sample_rate or 44100.0

    def set_option(self, key: str, value: str):
        self.options[key] = value
        self._options_dirty = True

    # ------------------------------------------------------------ running
    def step(self, frames: int = 1) -> int:
        """Run up to `frames` frames. Stops early if the debugger halts the
        CPU (breakpoint/watchpoint/step/halt). Returns frames completed."""
        done = 0
        if self.halted:
            return 0
        while done < frames:
            before = self.lib.tdo_frame_count() if self.has_harness else None
            self.lib.retro_run()
            if self.has_harness:
                self._pump_debug()
                if self.lib.tdo_frame_count() == before:   # stopped mid-frame
                    return done
            done += 1
            self.frame_count += 1
        return done

    def log_cursor(self) -> int:
        """Index to pass as `since=` to only consider output after this point."""
        return len(self.debug_log)

    def run_until_log(self, needle: str, max_frames: int = 3600,
                      since: int | None = None) -> bool:
        """Step until `needle` appears in debug output emitted after `since`
        (default: now). Returns False if max_frames elapse first."""
        start = len(self.debug_log) if since is None else since
        for i in range(max_frames + 1):
            text = "\n".join(self.debug_log[start:]) + self._debug_partial
            if needle in text:
                return True
            if i < max_frames:
                self.step(1)
        return False

    # ------------------------------------------------------------ input
    def set_buttons(self, buttons: list[str] | set[str] | tuple = (), port: int = 0):
        mask = 0
        for b in buttons:
            b = b.upper()
            if b not in BUTTONS:
                raise ValueError(f"unknown button {b!r}; valid: {sorted(BUTTONS)}")
            mask |= 1 << BUTTONS[b]
        self.ports[port].buttons = mask

    def press(self, buttons, frames: int = 6, release_frames: int = 6, port: int = 0):
        """Hold buttons for `frames` then release for `release_frames`."""
        if isinstance(buttons, str):
            buttons = [buttons]
        self.set_buttons(buttons, port)
        self.step(frames)
        self.set_buttons([], port)
        self.step(release_frames)

    # ------------------------------------------------------------ video
    def frame(self) -> Frame | None:
        if self._frame is None and getattr(self, "_frame_raw", None):
            raw, w, h, pitch, fmt = self._frame_raw
            self._frame = Frame(w, h, _to_rgb888(raw, w, h, pitch, fmt))
        return self._frame

    def screenshot(self, path: str | os.PathLike | None = None, scale: int = 1):
        f = self.frame()
        if f is None:
            raise RuntimeError("no frame rendered yet; step() first")
        img = f.to_image()
        if scale != 1:
            from PIL import Image
            img = img.resize((f.width * scale, f.height * scale), Image.NEAREST)
        if path:
            img.save(path, format=None if Path(path).suffix else "PNG")
        return img

    # ------------------------------------------------------------ save states
    def save_state(self) -> bytes:
        if self.halted:
            raise RuntimeError("cannot snapshot while halted mid-frame; continue first")
        n = self.lib.retro_serialize_size()
        buf = C.create_string_buffer(n)
        if not self.lib.retro_serialize(buf, n):
            raise RuntimeError("serialize failed")
        return buf.raw

    def load_state(self, data: bytes):
        buf = C.create_string_buffer(data, len(data))
        if not self.lib.retro_unserialize(buf, len(data)):
            raise RuntimeError("unserialize failed")

    # ------------------------------------------------------------ harness API
    def _need_harness(self):
        if not self.has_harness:
            raise RuntimeError("core lacks tdo harness extension (build with ./scripts/build-emulator.sh)")

    def _pump_debug(self):
        buf = C.create_string_buffer(65536)
        n = self.lib.tdo_debug_read(buf, 65536)
        if not n:
            return
        text = self._debug_partial + buf.raw[:n].decode("latin-1")
        lines = text.replace("\r\n", "\n").split("\n")
        self._debug_partial = lines.pop()
        for line in lines:
            self.debug_log.append(line)
            if self.verbose:
                print(f"[3do] {line}", file=sys.stderr)
        del self.debug_log[:-20000]

    def read_debug(self, since: int = 0) -> tuple[list[str], int]:
        """Return (lines, next_index). Pass next_index back to get only new lines."""
        lines = self.debug_log[since:]
        return lines, len(self.debug_log)

    def kprintf_swi(self, enable: bool):
        self._need_harness()
        self.lib.tdo_kprintf_swi_enable(1 if enable else 0)

    def regs(self) -> dict[str, int]:
        self._need_harness()
        out = (C.c_uint32 * 17)()
        self.lib.tdo_get_regs(out)
        names = [f"r{i}" for i in range(13)] + ["sp", "lr", "pc", "cpsr"]
        return dict(zip(names, list(out)))

    def read_mem(self, addr: int, length: int) -> bytes:
        self._need_harness()
        buf = C.create_string_buffer(length)
        n = self.lib.tdo_mem_read(addr, buf, length)
        return buf.raw[:n]

    def write_mem(self, addr: int, data: bytes) -> int:
        self._need_harness()
        return self.lib.tdo_mem_write(addr, data, len(data))

    def read_u32(self, addr: int) -> int:
        return struct.unpack(">I", self.read_mem(addr, 4))[0]

    def swi_trace(self, enable: bool):
        self._need_harness()
        self.lib.tdo_swi_trace_enable(1 if enable else 0)

    def swi_calls(self, max_entries: int = 4096) -> list[SwiCall]:
        self._need_harness()
        out = (C.c_uint32 * (max_entries * 7))()
        n = self.lib.tdo_swi_trace_read(out, max_entries)
        return [SwiCall(*out[i * 7:(i + 1) * 7]) for i in range(n)]

    def trace_add(self, addr: int) -> int:
        self._need_harness()
        return self.lib.tdo_trace_add(addr)

    def trace_remove(self, addr: int) -> int:
        self._need_harness()
        return self.lib.tdo_trace_remove(addr)

    def trace_clear(self):
        self._need_harness()
        self.lib.tdo_trace_clear()

    def trace_hits(self, max_entries: int = 4096) -> list[TraceHit]:
        self._need_harness()
        out = (C.c_uint32 * (max_entries * 10))()
        n = self.lib.tdo_trace_read(out, max_entries)
        return [TraceHit(*out[i * 10:(i + 1) * 10]) for i in range(n)]


# ---------------------------------------------------------------- input API
def _emu_set_device(self, port: int, device: str):
    """Plug a peripheral into port 0/1: joypad, flightstick, mouse, lightgun,
    arcade_lightgun, orbatak_trackball, none."""
    if device not in DEVICES:
        raise ValueError(f"unknown device {device!r}; valid: {sorted(DEVICES)}")
    self.ports[port].device = device
    active = sum(1 for p in self.ports if p.device != "none")
    self.set_option("opera_active_devices", str(max(active, 1)))
    if self.game_path:
        self.lib.retro_set_controller_port_device(port, DEVICES[device])


def _emu_set_analog(self, port: int = 0, lx=None, ly=None, rx=None, ry=None):
    """Analog axes, -32768..32767 (flightstick: lx/ly stick, ry throttle)."""
    a = self.ports[port].analog
    for k, v in (("lx", lx), ("ly", ly), ("rx", rx), ("ry", ry)):
        if v is not None:
            a[k] = int(v)


def _emu_mouse(self, port: int = 0, dx: int = 0, dy: int = 0, buttons=None):
    p = self.ports[port]
    p.mouse_dx += int(dx)
    p.mouse_dy += int(dy)
    if buttons is not None:
        p.mouse_buttons = {b.upper() for b in buttons}


def _emu_lightgun(self, port: int = 0, x=None, y=None, buttons=None, offscreen=False):
    p = self.ports[port]
    if x is not None:
        p.gun_x = int(x)
    if y is not None:
        p.gun_y = int(y)
    if buttons is not None:
        p.gun_buttons = {b.upper() for b in buttons}
    p.gun_offscreen = bool(offscreen)


Emulator.set_device = _emu_set_device
Emulator.set_analog = _emu_set_analog
Emulator.mouse = _emu_mouse
Emulator.lightgun = _emu_lightgun


# ---------------------------------------------------------------- debugger API
def _need_dbg(self):
    if not getattr(self, "debug_v2", False):
        raise RuntimeError("core lacks debugger support (rebuild: ./scripts/build-emulator.sh)")


def _halted(self) -> bool:
    return bool(getattr(self, "debug_v2", False) and self.lib and self.lib.tdo_dbg_halted())


def _stop_info(self) -> dict:
    _need_dbg(self)
    out = (C.c_uint32 * 4)()
    self.lib.tdo_dbg_stop_info(out)
    return {"halted": self.halted, "reason": STOP_REASONS.get(out[0], str(out[0])),
            "pc": out[1], "watch_addr": out[2],
            "watch_type": {1: "write", 2: "read", 3: "access"}.get(out[3])}


def _halt(self):
    """Request a stop. Takes effect before the next instruction, i.e. during
    the next step()/run."""
    _need_dbg(self)
    self.lib.tdo_dbg_halt()


def _cont(self):
    _need_dbg(self)
    self.lib.tdo_dbg_continue()


def _stepi(self, count: int = 1) -> dict:
    """Execute `count` instructions with the CPU otherwise halted."""
    _need_dbg(self)
    for _ in range(max(1, count)):
        self.lib.tdo_dbg_step()
        guard = 0
        while not self.lib.tdo_dbg_halted():
            self.lib.retro_run()
            self._pump_debug()
            guard += 1
            if guard > 4:     # step always lands within a frame; be safe
                break
        info = _stop_info(self)
        if info["reason"] != "step":
            return info
    return _stop_info(self)


def _run_until_stop(self, max_frames: int = 600) -> dict:
    """Continue and run until a breakpoint/watchpoint hits or max_frames pass."""
    _need_dbg(self)
    if self.halted:
        self.lib.tdo_dbg_continue()
    self.step(max_frames)
    return _stop_info(self)


def _set_reg(self, name_or_index, value: int):
    _need_dbg(self)
    names = {f"r{i}": i for i in range(16)}
    names.update(sp=13, lr=14, pc=15, cpsr=16, fp=11, ip=12)
    n = names[name_or_index] if isinstance(name_or_index, str) else int(name_or_index)
    self.lib.tdo_set_reg(n, value & 0xFFFFFFFF)


def _bp_add(self, addr):
    _need_dbg(self)
    if self.lib.tdo_bp_add(addr) < 0:
        raise RuntimeError("breakpoint table full (64)")


def _bp_remove(self, addr):
    _need_dbg(self)
    return self.lib.tdo_bp_remove(addr) == 0


def _wp_add(self, addr, length=4, kind="write"):
    _need_dbg(self)
    if self.lib.tdo_wp_add(addr, length, WATCH_TYPES[kind]) < 0:
        raise RuntimeError("watchpoint table full (16) or bad args")


def _wp_remove(self, addr, length=4, kind="write"):
    _need_dbg(self)
    return self.lib.tdo_wp_remove(addr, length, WATCH_TYPES[kind]) == 0


Emulator.halted = property(_halted)
Emulator.stop_info = _stop_info
Emulator.halt = _halt
Emulator.cont = _cont
Emulator.stepi = _stepi
Emulator.run_until_stop = _run_until_stop
Emulator.set_reg = _set_reg
Emulator.bp_add = _bp_add
Emulator.bp_remove = _bp_remove
Emulator.wp_add = _wp_add
Emulator.wp_remove = _wp_remove
Emulator.bp_clear = lambda self: (_need_dbg(self), self.lib.tdo_bp_clear())
Emulator.wp_clear = lambda self: (_need_dbg(self), self.lib.tdo_wp_clear())


def _to_rgb888(raw: bytes, w: int, h: int, pitch: int, fmt: int) -> bytes:
    import numpy as np
    if fmt == PIXFMT_XRGB8888:
        a = np.frombuffer(raw, dtype="<u4").reshape(h, pitch // 4)[:, :w]
        r = (a >> 16) & 0xFF
        g = (a >> 8) & 0xFF
        b = a & 0xFF
    else:
        a = np.frombuffer(raw, dtype="<u2").reshape(h, pitch // 2)[:, :w].astype(np.uint32)
        if fmt == PIXFMT_RGB565:
            r = ((a >> 11) & 0x1F) << 3
            g = ((a >> 5) & 0x3F) << 2
            b = (a & 0x1F) << 3
        else:
            r = ((a >> 10) & 0x1F) << 3
            g = ((a >> 5) & 0x1F) << 3
            b = (a & 0x1F) << 3
    return np.dstack([r, g, b]).astype(np.uint8).tobytes()
