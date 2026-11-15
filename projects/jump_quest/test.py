"""Jump Quest port test (./3do test jump_quest)."""
import re
import struct
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "tdo" / "src"))
from tdo.porttest import PortTest

t = PortTest("jump_quest")
t.boot()
sym = [l for l in t.emu.debug_log if "SYMBOL game" in l]
m = re.search(r"game 0x([0-9a-f]+)", sym[-1]) if sym else None
game = int(m.group(1), 16) if m else None
m = re.search(r"custom 0x([0-9a-f]+)", " ".join(l for l in t.emu.debug_log if "SYMBOL custom" in l))
custom = int(m.group(1), 16) if m else None
t.check("game and custom addresses logged", game is not None and custom is not None, t.log_tail())
def dmaconr():
    return struct.unpack(">H", t.emu.read_mem(custom + 2, 2))[0]
# Game: state, level, players, active (BYTEs), cam_x, transition (WORDs),
# then player[0]: x, y, vx, vy (WORDs), dir, on_ground, character, health,
# max_health, lives (BYTEs), score (LONG, at +16 of the player)
P0 = 8
def player():
    x, y, vx, vy, d, og, ch, hp, mh, lives = struct.unpack(">hhhhbbbbbb", t.emu.read_mem(game + P0, 14))
    return {"x": x, "y": y, "on_ground": og, "health": hp, "lives": lives}
def transition():
    return struct.unpack(">H", t.emu.read_mem(game + 6, 2))[0]

t.play(frames=40)
t.shot("title", min_colours=5)
t.start(["A"], expect="JUMP: state=1 level=0")          # STATE_PLAYING on level 1

# jump height: -78 rose 14 px, short of level 1's 32 px pipes; -180 rises 56
for _ in range(200):                                    # land first
    t.emu.step(1)
    if player()["on_ground"]:
        break
t.emu.step(10)
y0 = top = player()["y"]
t.emu.set_buttons(["A"])
sfx_on = []
for _ in range(60):
    t.emu.step(1)
    top = min(top, player()["y"])
    sfx_on.append(bool(dmaconr() & 4))
t.emu.set_buttons([])
t.play(frames=40)
t.check(f"a jump rises at least 48 px (rose {y0 - top})", y0 - top >= 48)
# the jump effect plays once (Paula used to loop it until the next effect)
t.check(f"the jump sound stops ({sum(sfx_on)} of 60 frames on)", not any(sfx_on[10:]))

c = t.emu.log_cursor()
for i in range(25):                                     # run right, hopping
    t.play(frames=12, hold=["RIGHT"] + (["A"] if i % 3 == 0 else []))
t.shot("playing", min_colours=6)
lines = t.emu.debug_log[c:]
t.check("running through level 1 scores or ends a life",
        any(l.startswith("JUMP: score=") for l in lines) or any("state=2" in l for l in lines), t.log_tail())
t.check_speed(min_fps=45)

# lives carry over: each death costs a life and the last one ends the game
# (player_init() used to reset them on every respawn, so it never ended)
def state():
    return t.emu.read_mem(game, 1)[0]
def wait_state(ok, frames=400):
    for _ in range(frames):
        if ok(state()):
            return True
        t.emu.step(1)
    return False
seen = []
for n in range(5):
    if not wait_state(lambda s: s in (1, 4)) or state() == 4:
        break
    t.emu.step(5)
    before = player()["lives"]
    t.emu.write_mem(game + P0 + 11, b"\x00")            # health = 0
    wait_state(lambda s: s == 2, 60)
    wait_state(lambda s: s != 2, 300)
    seen.append((before, player()["lives"], state()))
t.check(f"each death costs a life ((before, after, state): {seen})",
        bool(seen) and all(after == before - 1 for before, after, st in seen))
t.check("the last life ends the game (state=4)", bool(seen) and seen[-1][1] == 0 and seen[-1][2] == 4, str(seen))
t.play(frames=200)
t.check(f"game-over countdown stops at 0 (transition={transition()})", transition() == 0)
t.shot("gameover", min_colours=3)
t.start(["A"], expect="JUMP: state=0")                   # back to the title
t.quit()
t.finish()
