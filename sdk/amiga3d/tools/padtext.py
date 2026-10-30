#!/usr/bin/env python3
"""padtext.py - give a game's on-screen key names a 3DO pad variant.

Each source line whose string literals name Amiga keys (the help pages'
controls, "PRESS FIRE", "HELP: NEXT") is kept and a 3DO version added:

    #ifdef AMIGA3DO
          "A                GRAB / LET GO",
    #else
          "SPACE / FIRE     GRAB / LET GO",
    #endif

Generic rules (SPACE/FIRE -> A, HELP/H -> the game's help button, ESC -> L,
ARROWS/JOYSTICK -> D-PAD) plus each game's own (GAMES below). Lines already
inside an #ifdef AMIGA3DO are left alone, so it can be run again.

    python3 sdk/amiga3d/tools/padtext.py [--dry] <game> ...
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]

# help: the pad button for the help pages (None: there is none)
# esc: the button for Esc (back to the title)
GAMES = {
    "block_breaker_3d": dict(help="C", esc="L"),
    "marble_tilt": dict(help="C", esc="L"),
    "harbour_crane": dict(help="C", esc="L"),
    "tank_duel": dict(help="C", esc="L"),
    "ridge_runner": dict(help="C", esc="L"),
    "drone_courier": dict(help="C", esc="L"),
    "canyon_rail": dict(help="C", esc="L"),
    "orbit_docking": dict(help=None, esc=None, own={
        "W / FIRE + UP       FORWARD (IN)": "A                   FORWARD (IN)",
        "S / FIRE + DOWN     BACK (OUT)": "C                   BACK (OUT)",
        "A / D               ROLL": "L / R               ROLL",
        "FIRE + LEFT/RIGHT   ROLL": "",
        "BEST %ld   HELP: HOW TO PLAY": "BEST %ld",
        "HELP: NEXT  FIRE: PLAY": "A: PLAY",
    }),
    "stack_tower": dict(help="R", esc=None, own={
        "UP / X         ROTATE CLOCKWISE": "UP             ROTATE CLOCKWISE",
        "Z              ROTATE ANTICLOCKWISE": "B              ROTATE ANTICLOCKWISE",
        "G              GHOST ON / OFF": "L              GHOST ON / OFF",
    }),
    "lander_3d": dict(help="R", esc="L", own={
        "Q / E          STEADY THROTTLE - / +": "C / B          STEADY THROTTLE - / +",
        "UP / W         TIP FORWARD": "UP             TIP FORWARD",
        "DOWN / S       TIP BACK": "DOWN           TIP BACK",
        "LEFT / A       TIP LEFT": "LEFT           TIP LEFT",
        "RIGHT / D      TIP RIGHT": "RIGHT          TIP RIGHT",
    }),
    "boing_pinball": dict(help="C", esc=None, own={
        "LEFT SHIFT  Z     LEFT FLIPPERS": "L, LEFT           LEFT FLIPPERS",
        "RIGHT SHIFT /     RIGHT FLIPPERS": "R, RIGHT          RIGHT FLIPPERS",
        "SPACE  DOWN       PLUNGER: HOLD TO": "A, DOWN           PLUNGER: HOLD TO",
        "LEFT RIGHT UP     NUDGE THE TABLE": "UP                NUDGE THE TABLE",
        "P  PAUSE          ESC  THE TITLE": "P  PAUSE          X  THE MENU",
        "JOYSTICK: LEFT AND RIGHT FLIP,": "B: THE BOT PLAYS ON / OFF",
        "FIRE OR DOWN: THE PLUNGER, UP: NUDGE": "",
        "FIRE: PLAY   LEFT/RIGHT: TABLE": "A: PLAY   L/R: TABLE",
        "LEFT/RIGHT: TABLE   HELP: RULES": "L/R: TABLE   C: RULES",
        "LEFT/RIGHT: TABLE": "L/R: TABLE",
        "THREE TABLES  HELP FOR THE RULES": "THREE TABLES  C FOR THE RULES",
    }),
    "terra_strike": dict(help=None, esc="L", own={
        "ARROWS  FLY      A / Z  THROTTLE": "D-PAD  FLY      B / C  THROTTLE",
        "SPACE  FIRE      P  PAUSE": "A  FIRE      P  START / PAUSE",
        "ARROWS FLY  A/Z SPEED  SPACE FIRE": "D-PAD FLY  B/C SPEED  A FIRE",
    }),
    "scairy_fun": dict(help="C", esc="L", own={
        "V              CHASE / COCKPIT VIEW": "B              CHASE / COCKPIT VIEW",
        "FIRE: AUTOPILOT   V: VIEW": "A: AUTOPILOT   B: VIEW",
        "FIRE: AUTOPILOT   UP: DRIVE": "A: AUTOPILOT   UP: DRIVE",
        "HELP OR H: HOW TO PLAY": "C: HOW TO PLAY",
    }),
}


def pad(new, old):
    """new, padded with spaces to the length of old (for aligned columns)"""
    return new + " " * max(0, len(old) - len(new))


def translate(s, cfg):
    own = cfg.get("own", {})
    if s in own:
        return own[s]
    h = cfg["help"]
    t = s
    m = re.match(r"^(SPACE / FIRE|FIRE / SPACE|SPACE / FIRE / J)(\s+)", t)
    if m:
        t = pad("A", m.group(1) + m.group(2)) + t[m.end():]
    m = re.match(r"^(ARROWS / WASD / JOYSTICK|ARROWS / JOYSTICK|ARROWS / WASD)(\s+)", t)
    if m:
        t = pad("D-PAD", m.group(1) + m.group(2)) + t[m.end():]
    m = re.match(r"^(HELP / H)(\s+)", t)
    if m:
        if not h:
            return ""
        t = pad(h, m.group(1) + m.group(2)) + t[m.end():]
    m = re.match(r"^(ESC)(\s+)(TITLE / QUIT)$", t)
    if m:
        k = (cfg["esc"] + " / X") if cfg["esc"] else "X"
        t = pad(k, m.group(1) + m.group(2)) + ("TITLE / MENU" if cfg["esc"] else "THE MENU")
    t = t.replace("PRESS FIRE", "PRESS A")
    t = re.sub(r"\bFIRE: (PLAY|FLY|START|THE TITLE)", r"A: \1", t)
    if h:
        t = re.sub(r"\bHELP: ", h + ": ", t)
    return t


LIT = re.compile(r'"((?:[^"\\]|\\.)*)"')


def process(game, dry):
    cfg = GAMES[game]
    out_changes = 0
    for f in ("render.c", "game.c"):
        p = ROOT / "projects" / game / "src" / f
        lines = p.read_text().split("\n")
        res, depth3do = [], 0
        for ln in lines:
            st = ln.strip()
            if st.startswith("#ifdef AMIGA3DO"): depth3do += 1
            if st.startswith("#endif") and depth3do: depth3do -= 1
            if depth3do or "AB_" in ln or "ab_register" in ln or "hook" in ln or not LIT.search(ln):
                res.append(ln); continue
            new = LIT.sub(lambda m: '"' + translate(m.group(1), cfg) + '"', ln)
            if new == ln:
                res.append(ln); continue
            out_changes += 1
            if dry:
                print(f"{game}/{f}:\n  - {ln.strip()}\n  + {new.strip()}")
            res += ["#ifdef AMIGA3DO", new, "#else", ln, "#endif"]
        if not dry:
            p.write_text("\n".join(res))
    print(f"{game}: {out_changes} line(s)")


args = [a for a in sys.argv[1:] if a != "--dry"]
for g in args or GAMES:
    process(g, "--dry" in sys.argv)
