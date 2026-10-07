"""Read Portfolio (3DO OS) kernel structures from guest memory.

Offsets come from tools/probe (offsetof() printed by Norcroft on the target,
Portfolio 2.5 headers). Rebuild and rerun the probe if the OS version changes.
"""
from __future__ import annotations

import struct

# struct KernelBase
KB = dict(folio_list=124, devices=132, task_wait_q=136, task_ready_q=140, msgports=144,
          current_task=152, item_table=172, max_item=176, task_switches=252, tasks=276,
          mem_end=280)
# ItemNode
N = dict(next=0, prev=4, subsys=8, type=9, priority=10, flags=11, size=12, name=16,
         version=20, revision=21, item=24, owner=28)
# Task
T = dict(thread_task=36, wait_bits=48, sig_bits=52, stack_base=60, stack_size=64,
         super_stack_base=160, wait_item=164, free_mem=168, elapsed=180, launches=204,
         flags=208, link=212)
LIST_ANCHOR = 20
NST_KERNEL, FOLIONODE, TASKNODE = 1, 4, 5
DRAM = 0x200000


class Kernel:
    def __init__(self, emu):
        self.emu = emu
        self.base: int | None = None

    # ------------------------------------------------------------ memory
    def u32(self, a: int) -> int:
        return struct.unpack(">I", self.emu.read_mem(a, 4))[0]

    def u8(self, a: int) -> int:
        return self.emu.read_mem(a, 1)[0]

    def cstr(self, a: int, limit: int = 64) -> str | None:
        if not a or a >= DRAM:
            return None
        raw = self.emu.read_mem(a, limit)
        return raw.split(b"\0", 1)[0].decode("latin-1")

    # ------------------------------------------------------------ discovery
    def find_base(self) -> int | None:
        """Locate KernelBase: the kernel folio's ItemNode is named "kernel",
        has subsystem NST_KERNEL and type FOLIONODE, and its kb_CurrentTask
        points at a TASKNODE."""
        if self.base and self._valid(self.base):
            return self.base
        ram = self.emu.read_mem(0, DRAM)
        i = ram.find(b"kernel\0")
        while i != -1:
            needle = struct.pack(">I", i)
            j = ram.find(needle)
            while j != -1:
                node = j - N["name"]
                if node >= 0 and j % 4 == 0 and self._valid(node, ram):
                    self.base = node
                    return node
                j = ram.find(needle, j + 4)
            i = ram.find(b"kernel\0", i + 1)
        return None

    def _valid(self, node: int, ram: bytes | None = None) -> bool:
        get8 = (lambda a: ram[a]) if ram else self.u8
        get32 = (lambda a: struct.unpack(">I", ram[a:a + 4])[0]) if ram else self.u32
        try:
            if get8(node + N["subsys"]) != NST_KERNEL or get8(node + N["type"]) != FOLIONODE:
                return False
            cur = get32(node + KB["current_task"])
            return 0 < cur < DRAM and get8(cur + N["type"]) == TASKNODE
        except (IndexError, struct.error):
            return False

    def _list(self, list_addr: int, link_offset: int = 0, limit: int = 512):
        """Yield node addresses of an Exec-style List (link at node+link_offset)."""
        if not list_addr or list_addr >= DRAM:
            return
        end = list_addr + LIST_ANCHOR + 4
        n = self.u32(list_addr + LIST_ANCHOR)
        for _ in range(limit):
            if n == end or n == 0 or n >= DRAM:
                return
            yield n - link_offset
            n = self.u32(n)

    # ------------------------------------------------------------ queries
    def _node(self, a: int) -> dict:
        return {"addr": f"0x{a:06x}", "name": self.cstr(self.u32(a + N["name"])),
                "item": self.u32(a + N["item"]), "priority": self.u8(a + N["priority"]),
                "version": f"{self.u8(a + N['version'])}.{self.u8(a + N['revision'])}"}

    def tasks(self) -> list[dict]:
        kb = self.find_base()
        if kb is None:
            raise RuntimeError("kernel not found in memory (still booting?)")
        cur = self.u32(kb + KB["current_task"])
        ready = set(self._list(self.u32(kb + KB["task_ready_q"])))
        waiting = set(self._list(self.u32(kb + KB["task_wait_q"])))
        out = []
        for t in self._list(self.u32(kb + KB["tasks"]), link_offset=T["link"]):
            d = self._node(t)
            parent = self.u32(t + T["thread_task"])
            secs, usecs = struct.unpack(">II", self.emu.read_mem(t + T["elapsed"], 8))
            d.update({
                "state": "running" if t == cur else "ready" if t in ready
                         else "waiting" if t in waiting else "other",
                "thread_of": self.cstr(self.u32(parent + N["name"])) if parent else None,
                "owner_item": self.u32(t + N["owner"]),
                "sig_bits": f"0x{self.u32(t + T['sig_bits']):08x}",
                "wait_bits": f"0x{self.u32(t + T['wait_bits']):08x}",
                "wait_item": self.u32(t + T["wait_item"]),
                "stack": f"0x{self.u32(t + T['stack_base']):06x}+{self.u32(t + T['stack_size'])}",
                "cpu_time": f"{secs}.{usecs:06d}s",
                "launches": self.u32(t + T["launches"]),
            })
            out.append(d)
        return out

    def folios(self) -> list[dict]:
        kb = self.find_base()
        if kb is None:
            raise RuntimeError("kernel not found")
        return [self._node(f) for f in self._list(self.u32(kb + KB["folio_list"]))]

    def devices(self) -> list[dict]:
        kb = self.find_base()
        if kb is None:
            raise RuntimeError("kernel not found")
        return [self._node(d) for d in self._list(self.u32(kb + KB["devices"]))]

    def summary(self) -> dict:
        kb = self.find_base()
        if kb is None:
            raise RuntimeError("kernel not found")
        cur = self.u32(kb + KB["current_task"])
        return {"kernel_base": f"0x{kb:06x}",
                "current_task": self.cstr(self.u32(cur + N["name"])),
                "task_switches": self.u32(kb + KB["task_switches"]),
                "max_item": self.u32(kb + KB["max_item"]),
                "mem_end": f"0x{self.u32(kb + KB['mem_end']):06x}"}
