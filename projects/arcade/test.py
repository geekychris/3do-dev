"""Arcade menu test (./3do test arcade): launch a game, quit back, relaunch.

Also uses the OS inspector: the game runs as its own task while it plays, and
after it exits the menu gets its memory back and no OS items are left over.
"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "tdo" / "src"))
from tdo.osinspect import OS, diff
from tdo.porttest import PortTest

t = PortTest("arcade")
t.boot()
t.check("menu lists games", any(l.startswith("ARCADE: menu games=") for l in t.emu.debug_log), t.log_tail())
t.shot("menu", min_colours=4)
before = OS(t.emu).snapshot()
pages_before = OS(t.emu).memory(per_page=False)["regions"][0]["free_pages"]

c = t.emu.log_cursor()
t.start(["A"], expect="ARCADE: launched")
t.check("game boots", t.emu.run_until_log("ROCK: state=TITLE", 900, since=c), t.log_tail())
tasks = [x["name"] for x in OS(t.emu).tasks()]
t.check("game runs as its own task", len(tasks) == len(before["tasks"]) + 1, str(tasks))
t.play(frames=120, hold=["A"])
t.shot("game", min_colours=3)

c = t.emu.log_cursor()
t.press("X")
t.check("X returns to the menu", t.emu.run_until_log("ARCADE: returned", 600, since=c), t.log_tail())
t.emu.step(60)
after = OS(t.emu).snapshot()
d = diff(before, after)
# the menu recreates its screen/audio items after a game, so compare net counts;
# eventbroker may have one request in flight
leaked = {k: v for k, v in d["net_by_type"].items() if v > 0 and "eventbroker" not in k}
t.check("no OS items leaked (net count per type)", not leaked, str(d["net_by_type"]))
t.check("game task is gone", len(after["tasks"]) == len(before["tasks"]), str(list(after["tasks"].values()))[:300])
pages_after = OS(t.emu).memory(per_page=False)["regions"][0]["free_pages"]
t.check(f"DRAM pages returned ({pages_before} -> {pages_after} free)", pages_after >= pages_before - 1)

c = t.emu.log_cursor()
t.start(["A"], expect="ARCADE: launched")
t.check("relaunch works", t.emu.run_until_log("ROCK: state=TITLE", 900, since=c), t.log_tail())
t.finish()
