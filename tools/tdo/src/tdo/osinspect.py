"""Portfolio (3DO OS) runtime introspection from emulated RAM.

Everything here reads the OS's own data structures through the harness's
side-effect-free memory reads; nothing runs on the 3DO. Structure offsets
were printed by tools/probe (Norcroft offsetof() against the Portfolio 2.5
headers). Rerun the probe if the OS version changes.

Entry points (all return JSON-able dicts/lists):
    OS(emu).summary() / items() / item(n) / tasks() / ports() / semaphores()
    devices() / drivers() / ioreqs() / folios() / graphics() / audio() / files()
    memory() / snapshot() and diff(a, b)
"""
from __future__ import annotations

import struct
import time

from .kernel import Kernel, KB as _KB, N, T

DRAM = 0x200000
RAM_END = 0x300000           # DRAM + VRAM

KB = dict(_KB, mem_free_lists=116, mem_hdr_list=120, drivers=128, semaphores=148)
ITEMNODE = 36
LIST_ANCHOR = 20
ITEMS_PER_BLOCK = 128
GEN_MASK = 0x7FFF0000
INDEX_MASK = 0x00000FFF

MSGPORT = dict(signal=36, msgs=40, userdata=72)
MESSAGE = dict(reply_port=36, result=40, data=44, size=48, port=52)
SEMA = dict(bit=36, owner=40, nest=44, waiting=48)
MEMHDR = dict(types=20, page_size=24, free_bits=40, base=44, top=48, bits_size=52, page_shift=53)
MEMLIST = dict(types=20, own_bits=24, memhdr=32, own_bits_size=44)
DEVICE = dict(driver=36, open_cnt=40, max_unit=96)
DRIVER = dict(open_cnt=40)
FOLIO = dict(open_cnt=36, max_swi=41)
IOREQ = dict(dev=44, info=52, actual=84, flags=88, error=92, msg_item=104)
BITMAP = dict(buffer=36, width=40, height=44)
SCREEN = dict(group=36, vdl_item=44, bitmap_count=52, bitmap_list=56)

SUBSYS = {1: "kernel", 2: "graphics", 3: "filesystem", 4: "audio", 5: "math", 6: "network",
          7: "jetstream", 8: "av", 9: "intl", 10: "connection", 15: "security"}
NODE_TYPES = {
    1: {1: "MemFree", 2: "List", 3: "MemHdr", 4: "Folio", 5: "Task", 6: "FIRQ", 7: "Semaphore",
        8: "Sema4Wait", 9: "Message", 10: "MsgPort", 11: "MemList", 12: "RomTag", 13: "Driver",
        14: "IOReq", 15: "Device", 16: "Timer", 17: "ErrorText", 18: "HLInt"},
    2: {1: "ScreenGroup", 2: "Screen", 3: "Bitmap", 4: "VDL", 5: "DisplayInfo"},
    3: {1: "FileSystem", 2: "File", 3: "FileAlias"},
    4: {1: "Template", 2: "Instrument", 3: "Knob", 4: "Sample", 5: "Cue", 6: "Envelope",
        7: "Attachment", 8: "Tuning", 9: "Probe"},
}
IO_COMMANDS = {0: "WRITE", 1: "READ", 2: "STATUS"}


class OSNotReady(RuntimeError):
    """Raised when Portfolio's kernel isn't in RAM yet (session not booted)."""


def _h(v: int) -> str:
    return f"0x{v:06x}"


class OS:
    """One view of the OS, valid until the emulator runs again."""

    def __init__(self, emu, symbolize=None):
        self.emu = emu
        self.k = Kernel(emu)
        self.symbolize = symbolize or (lambda a: None)
        self._ram = None

    # ------------------------------------------------------------ memory access
    def _snap(self):
        if self._ram is None:
            self._ram = self.emu.read_mem(0, RAM_END)
        return self._ram

    def u32(self, a: int) -> int:
        r = self._snap()
        return struct.unpack_from(">I", r, a)[0] if 0 <= a <= len(r) - 4 else 0

    def u8(self, a: int) -> int:
        r = self._snap()
        return r[a] if 0 <= a < len(r) else 0

    def s32(self, a: int) -> int:
        v = self.u32(a)
        return v - (1 << 32) if v & 0x80000000 else v

    def cstr(self, a: int, limit: int = 64) -> str | None:
        if not a or a >= RAM_END:
            return None
        r = self._snap()
        end = r.find(b"\0", a, a + limit)
        return r[a:end if end >= 0 else a + limit].decode("latin-1")

    def kb(self) -> int:
        b = self.k.find_base()
        if b is None:
            frame = getattr(self.emu, "frame_count", 0)
            raise OSNotReady(
                f"the 3DO OS isn't running yet (frame {frame}): the session is still booting"
                + (" or paused - run it (Run button / run command) for a few seconds" if frame < 300 else ""))
        return b

    def _list(self, list_addr: int, link_offset: int = 0, limit: int = 1024):
        if not list_addr or list_addr >= RAM_END:
            return
        end = list_addr + LIST_ANCHOR + 4
        n = self.u32(list_addr + LIST_ANCHOR)
        for _ in range(limit):
            if n == end or n == 0 or n >= RAM_END:
                return
            yield n - link_offset
            n = self.u32(n)

    # ------------------------------------------------------------ items
    def _item_table(self):
        kb = self.kb()
        tbl, maxi = self.u32(kb + KB["item_table"]), self.u32(kb + KB["max_item"])
        for idx in range(min(maxi, 4096)):
            blk = self.u32(tbl + 4 * (idx // ITEMS_PER_BLOCK))
            if not blk or blk >= RAM_END:
                continue
            e = blk + 8 * (idx % ITEMS_PER_BLOCK)
            addr, info = self.u32(e), self.u32(e + 4)
            if addr and addr < RAM_END:
                yield idx, addr, info

    def _node(self, a: int) -> dict:
        subsys, typ = self.u8(a + N["subsys"]), self.u8(a + N["type"])
        return {
            "addr": _h(a),
            "item": self.u32(a + N["item"]),
            "name": self.cstr(self.u32(a + N["name"])) if self.u8(a + 11) & 0x80 or True else None,
            "subsys": SUBSYS.get(subsys, str(subsys)),
            "type": NODE_TYPES.get(subsys, {}).get(typ, f"type{typ}"),
            "priority": self.u8(a + N["priority"]),
            "owner": self.u32(a + N["owner"]),
            "size": self.s32(a + 12),
            "version": f"{self.u8(a + N['version'])}.{self.u8(a + N['revision'])}",
        }

    def items(self, type_filter: str | None = None) -> list[dict]:
        """Every live item: number, type, name, owner (task name), address."""
        owners = {}
        out = []
        for idx, addr, info in self._item_table():
            d = self._node(addr)
            if d["item"] & INDEX_MASK != idx:
                continue                       # stale entry
            out.append(d)
            if d["type"] == "Task":
                owners[d["item"]] = d["name"]
        for d in out:
            d["owner_name"] = owners.get(d["owner"])
        if type_filter:
            tf = type_filter.lower()
            out = [d for d in out if d["type"].lower() == tf or d["subsys"].lower() == tf]
        return out

    def _item_addr(self, item: int) -> int | None:
        if item <= 0:
            return None
        for idx, addr, info in self._item_table():
            if idx == item & INDEX_MASK and self.u32(addr + N["item"]) == item:
                return addr
        return None

    def item(self, item: int) -> dict:
        """One item with type-specific details and a raw dump of its node."""
        addr = self._item_addr(int(item))
        if addr is None:
            raise ValueError(f"no live item {item}")
        d = self._node(addr)
        detail = getattr(self, "_detail_" + d["type"], None)
        if detail:
            d["detail"] = detail(addr)
        d["raw"] = self._snap()[addr:addr + min(max(d["size"], ITEMNODE), 256)].hex()
        return d

    def _name_of_item(self, item: int) -> str | None:
        a = self._item_addr(item)
        return self.cstr(self.u32(a + N["name"])) if a else None

    # ------------------------------------------------------------ per-type details
    def _detail_MsgPort(self, a):
        msgs = list(self._list(a + MSGPORT["msgs"]))
        return {"signal": f"0x{self.u32(a + MSGPORT['signal']):08x}", "queued": len(msgs),
                "messages": [self._msg(m) for m in msgs[:32]]}

    def _msg(self, m):
        return {"item": self.u32(m + N["item"]), "reply_port": self.u32(m + MESSAGE["reply_port"]),
                "data": _h(self.u32(m + MESSAGE["data"])), "size": self.s32(m + MESSAGE["size"]),
                "result": f"0x{self.u32(m + MESSAGE['result']):08x}"}

    def _detail_Message(self, a):
        d = self._msg(a)
        d["queued_on"] = self.u32(a + MESSAGE["port"])
        return d

    def _detail_Semaphore(self, a):
        owner = self.s32(a + SEMA["owner"])
        waiting = list(self._list(a + SEMA["waiting"]))
        return {"locked": owner > 0, "owner": owner if owner > 0 else None,
                "owner_name": self._name_of_item(owner) if owner > 0 else None,
                "nest_count": self.s32(a + SEMA["nest"]), "waiters": len(waiting)}

    def _detail_Device(self, a):
        drv = self.u32(a + DEVICE["driver"])
        return {"open_count": self.s32(a + DEVICE["open_cnt"]),
                "max_unit": self.u8(a + DEVICE["max_unit"]),
                "driver": self.cstr(self.u32(drv + N["name"])) if drv else None}

    def _detail_Driver(self, a):
        return {"open_count": self.s32(a + DRIVER["open_cnt"])}

    def _detail_Folio(self, a):
        return {"open_count": self.s32(a + FOLIO["open_cnt"]),
                "swi_functions": self.u8(a + FOLIO["max_swi"])}

    def _detail_IOReq(self, a):
        dev = self.u32(a + IOREQ["dev"])
        info = a + IOREQ["info"]
        flags = self.u32(a + IOREQ["flags"])
        cmd = self.u8(info)
        return {"device": self.cstr(self.u32(dev + N["name"])) if dev and dev < RAM_END else None,
                "command": IO_COMMANDS.get(cmd, str(cmd)), "unit": self.u8(info + 2),
                "offset": self.s32(info + 12), "actual": self.s32(a + IOREQ["actual"]),
                "error": self.s32(a + IOREQ["error"]),
                "state": "done" if flags & 1 else ("waiting" if flags & 8 else "pending")}

    def _detail_Bitmap(self, a):
        return {"buffer": _h(self.u32(a + BITMAP["buffer"])),
                "width": self.s32(a + BITMAP["width"]), "height": self.s32(a + BITMAP["height"])}

    def _detail_Screen(self, a):
        bms = [self._detail_Bitmap(b) | {"item": self.u32(b + N["item"])}
               for b in self._list(a + SCREEN["bitmap_list"], limit=8)]
        return {"vdl_item": self.u32(a + SCREEN["vdl_item"]),
                "bitmap_count": self.s32(a + SCREEN["bitmap_count"]), "bitmaps": bms}

    def _detail_Task(self, a):
        return next((t for t in self.tasks() if t["addr"] == _h(a)), {})

    # ------------------------------------------------------------ views
    def summary(self) -> dict:
        kb = self.kb()
        cur = self.u32(kb + KB["current_task"])
        items = self.items()
        by_type: dict[str, int] = {}
        for d in items:
            by_type[d["type"]] = by_type.get(d["type"], 0) + 1
        mem = self.memory(per_page=False)
        return {"kernel_base": _h(kb), "current_task": self.cstr(self.u32(cur + N["name"])),
                "task_switches": self.u32(kb + KB["task_switches"]),
                "items": len(items), "max_item": self.u32(kb + KB["max_item"]),
                "items_by_type": dict(sorted(by_type.items(), key=lambda x: -x[1])),
                "memory": {r["name"]: {"free_kb": r["free_bytes"] // 1024,
                                       "used_kb": r["used_bytes"] // 1024} for r in mem["regions"]},
                "frame": self.emu.frame_count}

    def tasks(self) -> list[dict]:
        kb = self.kb()
        cur = self.u32(kb + KB["current_task"])
        ready = set(self._list(self.u32(kb + KB["task_ready_q"])))
        waiting = set(self._list(self.u32(kb + KB["task_wait_q"])))
        owned = self._pages_by_owner()
        out = []
        for t in self._list(self.u32(kb + KB["tasks"]), link_offset=T["link"]):
            d = self._node(t)
            parent = self.u32(t + T["thread_task"])
            secs, usecs = self.u32(t + T["elapsed"]), self.u32(t + T["elapsed"] + 4)
            wait_item = self.s32(t + T["wait_item"])
            d.update({
                "state": "running" if t == cur else "ready" if t in ready
                         else "waiting" if t in waiting else "other",
                "thread_of": self.cstr(self.u32(parent + N["name"])) if parent else None,
                "sig_bits": f"0x{self.u32(t + T['sig_bits']):08x}",
                "wait_bits": f"0x{self.u32(t + T['wait_bits']):08x}",
                "wait_item": wait_item if wait_item > 0 else None,
                "wait_item_name": self._name_of_item(wait_item) if wait_item > 0 else None,
                "stack_base": _h(self.u32(t + T["stack_base"])),
                "stack_size": self.s32(t + T["stack_size"]),
                "super_stack": _h(self.u32(t + T["super_stack_base"])),
                "cpu_ms": secs * 1000 + usecs // 1000,
                "launches": self.u32(t + T["launches"]),
                "flags": f"0x{self.u32(t + T['flags']):08x}",
                "owned_pages": owned.get(d["item"], 0),
            })
            out.append(d)
        return out

    def ports(self) -> list[dict]:
        return [d | {"detail": self._detail_MsgPort(int(d["addr"], 16))} for d in self.items("MsgPort")]

    def semaphores(self) -> list[dict]:
        return [d | {"detail": self._detail_Semaphore(int(d["addr"], 16))} for d in self.items("Semaphore")]

    def devices(self) -> list[dict]:
        return [d | {"detail": self._detail_Device(int(d["addr"], 16))} for d in self.items("Device")]

    def drivers(self) -> list[dict]:
        return [d | {"detail": self._detail_Driver(int(d["addr"], 16))} for d in self.items("Driver")]

    def ioreqs(self) -> list[dict]:
        return [d | {"detail": self._detail_IOReq(int(d["addr"], 16))} for d in self.items("IOReq")]

    def folios(self) -> list[dict]:
        return [d | {"detail": self._detail_Folio(int(d["addr"], 16))} for d in self.items("Folio")]

    def graphics(self) -> dict:
        items = self.items("graphics")
        for d in items:
            det = getattr(self, "_detail_" + d["type"], None)
            if det:
                d["detail"] = det(int(d["addr"], 16))
        return {"items": items}

    def audio(self) -> dict:
        return {"items": self.items("audio")}

    def files(self) -> dict:
        return {"items": self.items("filesystem")}

    # ------------------------------------------------------------ memory
    def _memhdrs(self):
        kb = self.kb()
        for h in self._list(self.u32(kb + KB["mem_hdr_list"]), limit=8):
            yield h

    def _pages_by_owner(self) -> dict[int, int]:
        """Pages owned per task item (from each task's MemList ownership bits)."""
        kb = self.kb()
        owned: dict[int, int] = {}
        for t in self._list(self.u32(kb + KB["tasks"]), link_offset=T["link"]):
            item = self.u32(t + N["item"])
            fl = self.u32(t + T["free_mem"])
            total = 0
            for ml in self._list(fl, limit=8):
                bits, n = self.u32(ml + MEMLIST["own_bits"]), self.u8(ml + MEMLIST["own_bits_size"])
                for w in range(n):
                    total += bin(self.u32(bits + 4 * w)).count("1")
            owned[item] = total
        return owned

    def memory(self, per_page: bool = True) -> dict:
        """Memory regions (DRAM/VRAM): page size, free/used, and optionally a
        per-page map with the owning task."""
        kb = self.kb()
        # Privileged tasks share one free-memory list, so owners are groups of
        # tasks keyed by their list; the label names every task in the group.
        groups: dict[int, list[str]] = {}
        for t in self._list(self.u32(kb + KB["tasks"]), link_offset=T["link"]):
            groups.setdefault(self.u32(t + T["free_mem"]), []).append(self.cstr(self.u32(t + N["name"])))
        owners_pages: dict[int, list] = {}
        tasks = {}
        for gid, (fl, names) in enumerate(groups.items(), 1):
            tasks[gid] = names[0] if len(names) == 1 else f"{names[0]} (+{len(names) - 1} sharing: {', '.join(names[1:])})"
            for ml in self._list(fl, limit=8):
                hdr = self.u32(ml + MEMLIST["memhdr"])
                bits, n = self.u32(ml + MEMLIST["own_bits"]), self.u8(ml + MEMLIST["own_bits_size"])
                owners_pages.setdefault(hdr, []).append((gid, [self.u32(bits + 4 * w) for w in range(n)]))
        regions = []
        for h in self._memhdrs():
            base, top = self.u32(h + MEMHDR["base"]), self.u32(h + MEMHDR["top"])
            psize = self.s32(h + MEMHDR["page_size"])
            if psize <= 0 or top <= base:
                continue
            npages = (top - base) // psize
            fb, fbn = self.u32(h + MEMHDR["free_bits"]), self.u8(h + MEMHDR["bits_size"])
            free_words = [self.u32(fb + 4 * w) for w in range(fbn)]

            def bit(words, i):            # page i = bit (i % 32), LSB first
                w = i // 32
                return w < len(words) and (words[w] >> (i % 32)) & 1

            free = [bool(bit(free_words, i)) for i in range(npages)]
            types = self.u32(h + MEMHDR["types"])
            name = "VRAM" if base >= DRAM and base < RAM_END else "DRAM"
            reg = {"name": f"{name}@{_h(base)}", "base": _h(base), "top": _h(top),
                   "page_size": psize, "pages": npages, "types": f"0x{types:08x}",
                   "free_pages": sum(free), "free_bytes": sum(free) * psize,
                   "used_bytes": (npages - sum(free)) * psize}
            if per_page:
                owner = [None] * npages
                for item, words in owners_pages.get(h, []):
                    for i in range(npages):
                        if bit(words, i):
                            owner[i] = item
                reg["map"] = ["." if free[i] else (owner[i] if owner[i] is not None else "?")
                              for i in range(npages)]
            regions.append(reg)
        return {"regions": regions, "task_names": tasks,
                "legend": "'.' free, '?' kernel/system (not in any task list), number = owner group in task_names"}

    # ------------------------------------------------------------ snapshots
    def snapshot(self) -> dict:
        return {"time": time.time(), "frame": self.emu.frame_count,
                "items": {d["item"]: {k: d[k] for k in ("type", "name", "owner_name", "addr")}
                          for d in self.items()},
                "tasks": {t["item"]: {k: t[k] for k in ("name", "state", "cpu_ms", "owned_pages")}
                          for t in self.tasks()},
                "memory": {r["name"]: r["free_bytes"] for r in self.memory(per_page=False)["regions"]}}


def diff(a: dict, b: dict) -> dict:
    """What changed between two snapshots."""
    ai, bi = a["items"], b["items"]
    created = [dict(item=k, **v) for k, v in bi.items() if k not in ai]
    deleted = [dict(item=k, **v) for k, v in ai.items() if k not in bi]
    tasks = []
    for k, t in b["tasks"].items():
        o = a["tasks"].get(k)
        if o:
            tasks.append({"item": k, "name": t["name"], "cpu_ms": t["cpu_ms"] - o["cpu_ms"],
                          "pages": t["owned_pages"] - o["owned_pages"],
                          "state": t["state"] if t["state"] != o["state"] else None})
    mem = {r: b["memory"][r] - a["memory"].get(r, 0) for r in b["memory"]}
    # Programs often tear down and recreate items (new numbers, same things), so
    # the leak signal is the net count per type and owner, not the raw lists.
    net: dict[str, int] = {}
    for sign, lst in ((1, created), (-1, deleted)):
        for i in lst:
            k = f"{i['type']} ({i['owner_name'] or '?'})"
            net[k] = net.get(k, 0) + sign
    return {"frames": b["frame"] - a["frame"], "seconds": round(b["time"] - a["time"], 2),
            "net_by_type": {k: v for k, v in sorted(net.items(), key=lambda x: -x[1]) if v},
            "items_created": created, "items_deleted": deleted,
            "tasks": sorted(tasks, key=lambda t: -t["cpu_ms"]), "free_bytes_delta": mem}
