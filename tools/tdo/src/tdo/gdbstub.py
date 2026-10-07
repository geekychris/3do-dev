"""GDB remote serial protocol stub for the 3DO (ARM60, big-endian).

    gdb projects/demo/build/demo.elf -ex 'target remote :PORT'     (./3do gdb demo)

Runs on the emulator thread: `poll()` is called from the session loop and
never blocks. Breakpoints/watchpoints are implemented in the core (no code
patching), so they work in ROM too. `qOffsets` reports the program's load
address so gdb relocates the ELF's symbols automatically.

`monitor <command> [key=value ...]` forwards to the session's control API
(e.g. `monitor ps`, `monitor press buttons=A`, `monitor screenshot path=x.png`,
`monitor reboot`, `monitor help`).
"""
from __future__ import annotations

import json
import shlex
import socket
import struct

TARGET_XML = """<?xml version="1.0"?>
<!DOCTYPE target SYSTEM "gdb-target.dtd">
<target version="1.0">
  <architecture>arm</architecture>
  <feature name="org.gnu.gdb.arm.core">
""" + "".join(f'    <reg name="r{i}" bitsize="32" type="uint32"/>\n' for i in range(13)) + """\
    <reg name="sp" bitsize="32" type="data_ptr"/>
    <reg name="lr" bitsize="32"/>
    <reg name="pc" bitsize="32" type="code_ptr"/>
    <reg name="cpsr" bitsize="32" regnum="25"/>
  </feature>
</target>
"""
REG_ORDER = [f"r{i}" for i in range(13)] + ["sp", "lr", "pc", "cpsr"]
GDB_REGNUM = {i: i for i in range(16)} | {25: 16}   # gdb regnum -> index in REG_ORDER


def _checksum(data: bytes) -> bytes:
    return b"%02x" % (sum(data) & 0xFF)


def _escape_binary(data: bytes) -> bytes:
    out = bytearray()
    for b in data:
        if b in b"#$}*":
            out += bytes([0x7D, b ^ 0x20])
        else:
            out.append(b)
    return bytes(out)


def _unescape_binary(data: bytes) -> bytes:
    out = bytearray()
    it = iter(data)
    for b in it:
        out.append((next(it) ^ 0x20) if b == 0x7D else b)
    return bytes(out)


class GdbStub:
    def __init__(self, session, port: int = 0, host: str = "127.0.0.1"):
        self.s = session
        self.srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.srv.bind((host, port))
        self.srv.listen(1)
        self.srv.setblocking(False)
        self.port = self.srv.getsockname()[1]
        self.conn: socket.socket | None = None
        self.buf = b""
        self.ack = True
        self.running = False      # gdb issued c/s and waits for a stop reply
        self.my_bps: set[int] = set()          # only what gdb inserted is
        self.my_wps: set[tuple] = set()        # removed when it detaches

    # ------------------------------------------------------------ plumbing
    @property
    def emu(self):
        return self.s.emu

    def poll(self):
        if self.conn is None:
            try:
                self.conn, _ = self.srv.accept()
                self.conn.setblocking(False)
                self.conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                self.buf, self.ack, self.running = b"", True, False
                self.s.note("gdb attached")
                self.emu.halt()          # gdb expects a stopped target
                self.s.settle_halt()
            except BlockingIOError:
                return
        try:
            data = self.conn.recv(65536)
            if not data:
                return self._drop("gdb disconnected")
            self.buf += data
        except BlockingIOError:
            pass
        except OSError:
            return self._drop("gdb connection error")
        self._process()
        if self.running and self.emu.halted:
            self.running = False
            self._send(self._stop_reply())

    def _drop(self, why):
        self.s.note(why)
        try:
            self.conn.close()
        except OSError:
            pass
        self.conn = None
        self.running = False
        for a in self.my_bps:
            if a not in self.s.breakpoints:
                self.emu.bp_remove(a)
        for w in self.my_wps:
            if w not in self.s.watchpoints:
                self.emu.wp_remove(*w)
        self.my_bps.clear()
        self.my_wps.clear()
        if self.emu.halted:
            self.emu.cont()

    def _send(self, payload: str | bytes):
        if isinstance(payload, str):
            payload = payload.encode("latin-1")
        pkt = b"$" + payload + b"#" + _checksum(payload)
        try:
            self.conn.setblocking(True)
            self.conn.sendall(pkt)
            self.conn.setblocking(False)
        except OSError:
            self._drop("gdb send failed")

    def _process(self):
        while self.buf and self.conn:
            c = self.buf[0:1]
            if c in (b"+", b"-"):
                self.buf = self.buf[1:]
                continue
            if c == b"\x03":                      # ^C
                self.buf = self.buf[1:]
                self.emu.halt()
                continue
            if c != b"$":
                self.buf = self.buf[1:]
                continue
            end = self.buf.find(b"#")
            if end < 0 or len(self.buf) < end + 3:
                return                            # incomplete
            payload = self.buf[1:end]
            self.buf = self.buf[end + 3:]
            if self.ack:
                self.conn.sendall(b"+")
            reply = self._handle(payload)
            if reply is not None:
                self._send(reply)

    # ------------------------------------------------------------ helpers
    def _stop_reply(self) -> str:
        info = self.emu.stop_info()
        r = info["reason"]
        if r == "halt":
            return "T02thread:1;"
        if r == "breakpoint":
            return "T05swbreak:;thread:1;"
        if r == "watchpoint":
            kind = {"write": "watch", "read": "rwatch", "access": "awatch"}[info["watch_type"] or "write"]
            return f"T05{kind}:{info['watch_addr']:x};thread:1;"
        return "T05thread:1;"

    def _regs(self) -> list[int]:
        r = self.emu.regs()
        return [r[n] for n in REG_ORDER]

    # ------------------------------------------------------------ packets
    def _handle(self, p: bytes):
        s = p.decode("latin-1")
        e = self.emu
        try:
            if s == "?":
                if not e.halted:
                    e.halt()
                    self.s.settle_halt()
                return self._stop_reply()
            if s.startswith("qSupported"):
                return "PacketSize=4000;qXfer:features:read+;swbreak+;hwbreak+;vContSupported+;QStartNoAckMode+"
            if s == "QStartNoAckMode":
                self._send("OK")
                self.ack = False
                return None
            if s.startswith("qXfer:features:read:target.xml:"):
                off, ln = (int(x, 16) for x in s.rsplit(":", 1)[1].split(","))
                chunk = TARGET_XML[off:off + ln]
                return ("l" if off + ln >= len(TARGET_XML) else "m") + chunk
            if s == "qAttached":
                return "1"
            if s == "qC":
                return "QC1"
            if s == "qfThreadInfo":
                return "m1"
            if s == "qsThreadInfo":
                return "l"
            if s.startswith("qThreadExtraInfo"):
                return "ARM60 CPU".encode().hex()
            if s == "qOffsets":
                base = self.s.load_base() or 0
                return f"Text={base:x};Data={base:x};Bss={base:x}"
            if s.startswith("qRcmd,"):
                return self._monitor(bytes.fromhex(s[6:]).decode("latin-1"))
            if s.startswith(("H", "T")):
                return "OK"
            if s in ("vMustReplyEmpty", "qTStatus") or s.startswith(("qSymbol", "qTfV", "qTfP")):
                return "OK" if s.startswith("qSymbol") else ""
            if s == "vCont?":
                return "vCont;c;C;s;S"
            if s.startswith("vCont;"):
                action = s[6:].split(";")[0].split(":")[0]
                return self._resume(step=action[0] in "sS")
            if s[0] in "cC":
                return self._resume(step=False)
            if s[0] in "sS":
                return self._resume(step=True)
            if s == "g":
                return "".join(struct.pack(">I", v).hex() for v in self._regs())
            if s[0] == "G":
                data = bytes.fromhex(s[1:])
                for i, name in enumerate(REG_ORDER):
                    if 4 * i + 4 <= len(data):
                        e.set_reg(name, struct.unpack(">I", data[4 * i:4 * i + 4])[0])
                return "OK"
            if s[0] == "p":
                n = int(s[1:], 16)
                if n not in GDB_REGNUM:
                    return "E01"
                return struct.pack(">I", self._regs()[GDB_REGNUM[n]]).hex()
            if s[0] == "P":
                n, v = s[1:].split("=")
                n = int(n, 16)
                if n not in GDB_REGNUM:
                    return "E01"
                e.set_reg(REG_ORDER[GDB_REGNUM[n]], struct.unpack(">I", bytes.fromhex(v))[0])
                return "OK"
            if s[0] == "m":
                a, ln = (int(x, 16) for x in s[1:].split(","))
                data = e.read_mem(a, min(ln, 0x1000))
                return data.hex() if data else "E01"
            if s[0] == "M":
                head, hexdata = s[1:].split(":")
                a, _ = (int(x, 16) for x in head.split(","))
                n = e.write_mem(a, bytes.fromhex(hexdata))
                return "OK" if n == len(hexdata) // 2 else "E01"
            if s[0] == "X":
                colon = p.index(b":")
                a, ln = (int(x, 16) for x in p[1:colon].decode().split(","))
                if ln == 0:
                    return "OK"
                n = e.write_mem(a, _unescape_binary(p[colon + 1:]))
                return "OK" if n == ln else "E01"
            if s[0] in "Zz":
                kind, a, _ = s[1:].split(",")[:3]
                a = int(a, 16)
                add = s[0] == "Z"
                if kind in ("0", "1"):
                    if add:
                        e.bp_add(a)
                        self.my_bps.add(a)
                    else:
                        if a not in self.s.breakpoints:
                            e.bp_remove(a)
                        self.my_bps.discard(a)
                    return "OK"
                if kind in ("2", "3", "4"):
                    ln = int(s.split(",")[2].split(";")[0], 16)
                    wk = {"2": "write", "3": "read", "4": "access"}[kind]
                    if add:
                        e.wp_add(a, ln, wk)
                        self.my_wps.add((a, ln, wk))
                    else:
                        e.wp_remove(a, ln, wk)
                        self.my_wps.discard((a, ln, wk))
                    return "OK"
                return ""
            if s[0] == "D":
                self._send("OK")
                self._drop("gdb detached")
                return None
            if s[0] == "k":
                self._drop("gdb kill (target keeps running)")
                return None
            if s.startswith("vKill"):
                return "OK"
        except Exception as ex:  # noqa: BLE001 - never kill the stub on a bad packet
            self.s.note(f"gdb packet {s[:40]!r} failed: {ex}")
            return "E01"
        return ""   # unsupported

    def _resume(self, step: bool):
        e = self.emu
        if step:
            e.stepi(1)
            return self._stop_reply()
        e.cont()
        self.s.resume_for_debugger()
        self.running = True
        return None   # stop reply sent from poll() when the CPU halts

    def _monitor(self, line: str) -> str:
        parts = shlex.split(line)
        if not parts:
            return "OK"
        cmd, args = parts[0], {}
        for tok in parts[1:]:
            k, _, v = tok.partition("=")
            try:
                args[k] = json.loads(v)
            except ValueError:
                args[k] = v
        try:
            if cmd == "help":
                out = self.s.ctl.help_text()
            else:
                res = self.s.ctl.handle(cmd, args)
                res.pop("png_b64", None) if isinstance(res, dict) else None
                out = res if isinstance(res, str) else json.dumps(res, indent=1)
        except Exception as ex:  # noqa: BLE001
            out = f"error: {ex}"
        return (out.rstrip() + "\n").encode().hex()

    def close(self):
        if self.conn:
            self._drop("session closing")
        self.srv.close()
