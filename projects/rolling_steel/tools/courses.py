#!/usr/bin/env python3
"""Rolling Steel courses for the 3DO: courses/N.course -> takeme/rolling_steel/courseN.bin

    python3 projects/rolling_steel/tools/courses.py

The course files are the Unity game's own (CourseLibrary.cs). This is
CourseScript.cs + CourseBuilder (Course.cs) + LevelBuilder.cs + Decor.cs in
Python, with Unity's quaternion maths, so the geometry comes out the same.
What the 3DO gets is the result, ready to use:

  * every visible face as small quads (<= 3 units a side, so a painter's
    sort works), lit like the Unity scene - key + fill light, trilight
    ambient, emission, in linear colour - and pre-fogged at six depths
  * the solid faces as triangles for the marble's collision, with friction
    and bounce classes, and a 4-unit grid over them
  * the route (densified, with the "safe to respawn here" flags), triggers
    (acid, fans, boosts, the goal), pillars, sweepers, crushers, enemies,
    crumbling tiles and the floating scenery

World space = course space yawed 45 degrees (LevelBuilder.CourseYaw), so the
3DO never sees course space. Numbers are big-endian int32; positions are
Q12 (4096 = one unit), directions Q14.
"""
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, "takeme", "rolling_steel")

# ---------------------------------------------------------------- maths

def add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def mul(a, s): return (a[0] * s, a[1] * s, a[2] * s)
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def length(a): return math.sqrt(dot(a, a))
def norm(a):
    l = length(a)
    return (0.0, 0.0, 0.0) if l < 1e-9 else mul(a, 1.0 / l)
def lerp(a, b, t): return a + (b - a) * t
def vlerp(a, b, t): return add(a, mul(sub(b, a), t))

UP, FWD, RIGHT = (0.0, 1.0, 0.0), (0.0, 0.0, 1.0), (1.0, 0.0, 0.0)
QI = (0.0, 0.0, 0.0, 1.0)

def angle_axis(deg, axis):
    axis = norm(axis)
    h = math.radians(deg) * 0.5
    s = math.sin(h)
    return (axis[0] * s, axis[1] * s, axis[2] * s, math.cos(h))

def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)

def qrot(q, v):
    x, y, z, w = q
    u = (x, y, z)
    t = mul(cross(u, v), 2.0)
    return add(add(v, mul(t, w)), cross(u, t))

def euler(x, y, z):
    """Unity's Quaternion.Euler: Z, then X, then Y."""
    return qmul(qmul(angle_axis(y, UP), angle_axis(x, RIGHT)), angle_axis(z, FWD))

def look_rotation(f, up=UP):
    f = norm(f)
    r = cross(up, f)
    if length(r) < 1e-6:
        r = RIGHT
    r = norm(r)
    u = cross(f, r)
    m00, m01, m02 = r[0], u[0], f[0]
    m10, m11, m12 = r[1], u[1], f[1]
    m20, m21, m22 = r[2], u[2], f[2]
    tr = m00 + m11 + m22
    if tr > 0:
        s = math.sqrt(tr + 1.0) * 2
        return ((m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25 * s)
    if m00 > m11 and m00 > m22:
        s = math.sqrt(1.0 + m00 - m11 - m22) * 2
        return (0.25 * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s)
    if m11 > m22:
        s = math.sqrt(1.0 + m11 - m00 - m22) * 2
        return ((m01 + m10) / s, 0.25 * s, (m12 + m21) / s, (m02 - m20) / s)
    s = math.sqrt(1.0 + m22 - m00 - m11) * 2
    return ((m02 + m20) / s, (m12 + m21) / s, 0.25 * s, (m10 - m01) / s)

def ease(t): return t * t * (3 - 2 * t)

# ---------------------------------------------------------------- Course.cs

N_NORMAL, N_ROUGH, N_ICE, N_ACID, N_GOAL, N_START, N_RAIL, N_CRUMBLE = range(8)
THICK = 1.2
MARBLE_R = 0.5
OVERLAP = 0.06


class Level:
    def __init__(self):
        self.name, self.time, self.spawn = "COURSE", 60.0, (0, 0, 0)
        self.decor, self.music = 0, 1
        self.gold = self.silver = self.bronze = 0.0
        self.blocks, self.ribbons, self.enemies, self.props, self.path = [], [], [], [], []


class Builder:
    def __init__(self, name, time, width):
        self.L = Level()
        self.L.name, self.L.time = name, time
        self.width = width
        self.cur = (0.0, 0.0, 0.0)
        self.heading = 0.0
        self.L.spawn = (0.0, MARBLE_R + 0.4, 2.5)
        self.L.path.append(self.cur)
        self.seg_start, self.seg_start_idx = self.cur, 0
        self.last = None            # ("slab", a, b, w, rot) or ("ribbon", rib)

    def frame(self): return angle_axis(self.heading, UP)
    def fwd(self): return qrot(self.frame(), FWD)
    def right(self): return qrot(self.frame(), RIGHT)

    def slab(self, a, b, w, s):
        d = sub(b, a)
        ln = length(d)
        if ln < 0.001:
            return
        rot = look_rotation(mul(d, 1 / ln), UP)
        up = qrot(rot, UP)
        self.L.blocks.append(dict(c=sub(mul(add(a, b), 0.5), mul(up, THICK * 0.5)),
                                  size=(w, THICK, ln + 2 * OVERLAP), rot=rot, s=s))
        self.last = ("slab", a, b, w, rot)

    def segment(self, local, w, s=N_NORMAL):
        delta = qrot(self.frame(), local)
        self.seg_start, self.seg_start_idx = self.cur, len(self.L.path) - 1
        taper = s in (N_NORMAL, N_ICE, N_ROUGH)
        if taper and abs(w - self.width) > 0.01:
            steps = max(4, math.ceil(length(delta) / 2))
            pts, rolls, ws = [], [], []
            for i in range(steps + 1):
                t = i / steps
                pts.append(add(self.cur, mul(delta, t)))
                rolls.append(0.0)
                ws.append(lerp(self.width, w, ease(t)))
            self.sweep(pts, rolls, ws, s, THICK)
        else:
            self.slab(self.cur, add(self.cur, delta), w, s)
        self.cur = add(self.cur, delta)
        self.width = w
        self.L.path.append(self.cur)

    def pad(self, d, w, s=N_NORMAL): self.segment((0, 0, d), w, s)
    def run(self, ln, w=-1): self.segment((0, 0, ln), self.width if w < 0 else w)
    def ice(self, ln, w=-1): self.segment((0, 0, ln), self.width if w < 0 else w, N_ICE)
    def rough(self, ln, w=-1): self.segment((0, 0, ln), self.width if w < 0 else w, N_ROUGH)
    def slope(self, ln, drop, w=-1): self.segment((0, -drop, ln), self.width if w < 0 else w)
    def jog(self, dx, d, w=-1): self.segment((dx, 0, d), self.width if w < 0 else w)

    def gap(self, ln):
        self.cur = add(self.cur, mul(self.fwd(), ln))
        self.L.path.append(self.cur)

    def step(self, dy):
        self.cur = (self.cur[0], self.cur[1] - dy, self.cur[2])
        self.L.path.append(self.cur)

    def jump(self, gap, drop, lip=5.0):
        self.run(lip)
        self.gap(gap)
        self.step(drop)

    def sweep(self, pts, rolls, widths, s, thick):
        pts, rolls, widths = list(pts), list(rolls), list(widths)
        head = mul(norm(sub(pts[0], pts[1])), OVERLAP)
        tail = mul(norm(sub(pts[-1], pts[-2])), OVERLAP)
        pts.insert(0, add(pts[0], head)); rolls.insert(0, rolls[0]); widths.insert(0, widths[0])
        pts.append(add(pts[-1], tail)); rolls.append(rolls[-1]); widths.append(widths[-1])
        rib = dict(s=s, thick=thick, nodes=[])
        for i in range(len(pts)):
            prev, nxt = pts[max(0, i - 1)], pts[min(len(pts) - 1, i + 1)]
            tg = sub(nxt, prev)
            if dot(tg, tg) < 1e-6:
                tg = self.fwd()
            fr = qmul(look_rotation(norm(tg), UP), angle_axis(rolls[i], FWD))
            rib["nodes"].append((pts[i], fr, widths[i]))
        self.L.ribbons.append(rib)
        self.last = ("ribbon", rib)
        return rib

    def kerbs(self, rib, h, left=True, right=True):
        kw = 0.4
        for side in (-1, 1):
            if (side < 0 and not left) or (side > 0 and not right):
                continue
            k = dict(s=N_RAIL, thick=h, nodes=[])
            for p, r, w in rib["nodes"]:
                k["nodes"].append((add(p, qrot(r, (side * (w + kw) * 0.5, h, 0.0))), r, kw))
            self.L.ribbons.append(k)

    def curve(self, radius, ang, drop=0.0, w=-1, bank=0.0, rails=False, s=N_NORMAL):
        wid = self.width if w < 0 else w
        w0 = self.width
        sign = 1.0 if ang > 0 else -1.0 if ang < 0 else 0.0
        sweep = abs(ang)
        if sweep < 0.01 or radius <= 0.01:
            return
        centre = add(self.cur, mul(self.right(), radius * sign))
        radial = sub(self.cur, centre)
        radial = (radial[0], 0.0, radial[2])
        steps = max(8, math.ceil(sweep / 4))
        pts, rolls, ws = [], [], []
        for i in range(steps + 1):
            t = i / steps
            p = add(centre, qrot(angle_axis(sweep * t * sign, UP), radial))
            p = (p[0], self.cur[1] - drop * ease(t), p[2])
            pts.append(p)
            rolls.append(-bank * sign * math.sin(t * math.pi))
            ws.append(lerp(w0, wid, ease(t)))
        rib = self.sweep(pts, rolls, ws, s, THICK)
        if rails:
            self.kerbs(rib, 0.7)
        self.L.path.extend(pts[1:])
        self.cur = pts[-1]
        self.heading += ang
        self.width = wid

    def hill(self, ln, drop, w=-1, rails=False, s=N_NORMAL):
        wid = self.width if w < 0 else w
        w0 = self.width
        steps = max(8, math.ceil(ln / 1.5))
        pts, rolls, ws = [], [], []
        for i in range(steps + 1):
            t = i / steps
            p = add(self.cur, mul(self.fwd(), ln * t))
            pts.append((p[0], self.cur[1] - drop * ease(t), p[2]))
            rolls.append(0.0)
            ws.append(lerp(w0, wid, ease(t)))
        rib = self.sweep(pts, rolls, ws, s, THICK)
        if rails:
            self.kerbs(rib, 0.7)
        self.L.path.extend(pts[1:])
        self.cur = pts[-1]
        self.width = wid

    def chicane(self, radius, ang, drop=0.0, w=-1, bank=0.0, rails=False):
        self.curve(radius, ang, drop * 0.5, w, bank, rails)
        self.curve(radius, -ang, drop * 0.5, w, bank, rails)

    def split(self, ln, side_w, gap_w, apron=5.0):
        full = gap_w + 2 * side_w
        self.pad(apron, full)
        off = (gap_w + side_w) * 0.5
        a, b = self.cur, add(self.cur, mul(self.fwd(), ln))
        r = self.right()
        self.slab(sub(a, mul(r, off)), sub(b, mul(r, off)), side_w, N_NORMAL)
        self.slab(add(a, mul(r, off)), add(b, mul(r, off)), side_w, N_NORMAL)
        self.cur = b
        self.width = full
        self.last = ("slab", a, b, full, self.frame())
        self.L.path.pop()
        self.L.path.append(sub(sub(a, mul(r, off)), mul(self.fwd(), apron * 0.55)))
        self.L.path.append(sub(a, mul(r, off)))
        self.L.path.append(sub(b, mul(r, off)))

    def rails(self, left=True, right=True, h=0.7):
        if self.last and self.last[0] == "ribbon":
            self.kerbs(self.last[1], h, left, right)
            return
        if not self.last:
            return
        _, a, b, w, rot = self.last
        rw = 0.35
        mid = mul(add(a, b), 0.5)
        ln = length(sub(b, a))
        up, side = qrot(rot, UP), qrot(rot, RIGHT)
        for i in range(2):
            is_left = i == 0
            if (is_left and not left) or (not is_left and not right):
                continue
            sg = -1.0 if is_left else 1.0
            self.L.blocks.append(dict(c=add(add(mid, mul(side, sg * (w + rw) * 0.5)), mul(up, h * 0.5)),
                                      size=(rw, h, ln), rot=rot, s=N_RAIL))

    def insert_along(self, p):
        f = self.fwd()
        key = dot(sub(p, self.seg_start), f)
        i = self.seg_start_idx + 1
        while i < len(self.L.path) - 1 and dot(sub(self.L.path[i], self.seg_start), f) <= key:
            i += 1
        self.L.path.insert(i, p)

    def at(self, local): return add(self.cur, qrot(self.frame(), local))

    def acid(self, w, d, x=0.0, back=0.0, lead=4.0):
        bz = d * 0.5 + back
        self.L.blocks.append(dict(c=self.at((x, 0.06, -bz)), size=(w, 0.12, d), rot=self.frame(), s=N_ACID))
        dl, dr = -self.width * 0.5, self.width * 0.5
        al, ar = x - w * 0.5, x + w * 0.5
        lg, rg = al - dl, dr - ar
        by = dl + lg * 0.5 if lg > rg else dr - rg * 0.5
        self.insert_along(self.at((by, 0, -(bz + d * 0.5 + lead))))
        self.insert_along(self.at((by, 0, -(bz - d * 0.5 - 1.5))))

    def detour(self, x, hw, cb, hd, lead):
        dl, dr = -self.width * 0.5, self.width * 0.5
        ol, orr = x - hw, x + hw
        lg, rg = ol - dl, dr - orr
        by = dl + lg * 0.5 if lg > rg else dr - rg * 0.5
        self.insert_along(self.at((by, 0, -(cb + hd + lead))))
        self.insert_along(self.at((by, 0, -(cb - hd - 1.5))))

    def prop(self, kind, pos, **kw):
        d = dict(kind=kind, pos=pos, rot=self.frame(), size=0, height=0, speed=0, phase=0, power=0, depth=0)
        d.update(kw)
        self.L.props.append(d)

    def pillar(self, x, back, r=0.85, h=2.6):
        self.prop("pillar", self.at((x, 0, -back)), size=r, height=h)
        self.detour(x, r + 0.7, back, r + 0.7, 4.0)

    def sweeper(self, x, back, ln=5.0, speed=70.0, h=0.55, phase=0.0):
        self.prop("sweeper", self.at((x, 0, -back)), size=ln, height=h, speed=speed, phase=phase)

    def crusher(self, x, back, w=3.0, period=2.4, phase=0.0, lift=4.5):
        self.prop("crusher", self.at((x, 0, -back)), size=w, height=lift, speed=period, phase=phase)
        self.detour(x, w * 0.5 + 0.6, back, w * 0.5, 5.0)

    def fan(self, x, back, w=6.0, d=6.0, push=16.0):
        self.prop("fan", self.at((x, 0.05, -(d * 0.5 + back))), size=w, depth=d, power=push)

    def boost(self, x, back, w=4.0, d=5.0, push=26.0):
        self.prop("boost", self.at((x, 0.05, -(d * 0.5 + back))), size=w, depth=d, power=push)

    def crumble(self, ln, w=-1, tile=2.4):
        wid = self.width if w < 0 else w
        n = max(1, round(ln / tile))
        st = ln / n
        self.seg_start, self.seg_start_idx = self.cur, len(self.L.path) - 1
        for i in range(n):
            self.slab(add(self.cur, mul(self.fwd(), st * i)), add(self.cur, mul(self.fwd(), st * (i + 1))), wid, N_CRUMBLE)
        self.cur = add(self.cur, mul(self.fwd(), ln))
        self.width = wid
        self.L.path.append(self.cur)

    def enemy(self, kind, x, back, rng=16.0, speed=9.0):
        self.L.enemies.append(dict(kind=kind, pos=self.at((x, MARBLE_R + 0.3, -back)), range=rng, speed=speed))

    def goal(self, d=9.0, w=-1):
        self.pad(d, max(self.width, 9.0) if w < 0 else w, N_GOAL)

# ---------------------------------------------------------------- CourseScript.cs

def parse(text):
    cmds = []
    for raw in text.split("\n"):
        line = raw.replace("\r", "").strip()
        if "#" in line:
            line = line[:line.index("#")].strip()
        if not line:
            continue
        parts = line.split()
        verb = parts[0].lower()
        c = dict(verb=verb, vals={}, flags=set(), text="")
        if verb == "name":
            c["text"] = line[len(parts[0]):].strip()
            cmds.append(c)
            continue
        header = verb in ("name", "time", "width", "decor", "music", "medals")
        for p in parts[1:]:
            if "=" in p and p.index("=") > 0:
                k, v = p.split("=", 1)
                try:
                    c["vals"][k] = float(v)
                except ValueError:
                    pass
            else:
                try:
                    if header:
                        c["vals"]["v"] = float(p)
                        continue
                except ValueError:
                    pass
                c["flags"].add(p.lower())
        cmds.append(c)
    return cmds


def build(text):
    cmds = parse(text)
    G = lambda c, k, d: c["vals"].get(k, d)
    first = lambda c, d: c["vals"].get("v", next(iter(c["vals"].values()), d))
    name, time, width, decor, music = "COURSE", 60.0, 9.0, 0, 1
    gold = silver = bronze = 0.0
    for c in cmds:
        v = c["verb"]
        if v == "name": name = c["text"]
        elif v == "time": time = first(c, 60.0)
        elif v == "width": width = first(c, 9.0)
        elif v == "decor": decor = round(first(c, 0.0))
        elif v == "music": music = round(first(c, 1.0))
        elif v == "medals":
            gold, silver, bronze = G(c, "gold", 0.0), G(c, "silver", 0.0), G(c, "bronze", 0.0)
    b = Builder(name, time, width)
    b.L.decor, b.L.music = decor, music
    b.L.gold, b.L.silver, b.L.bronze = gold, silver, bronze
    for c in cmds:
        v, has = c["verb"], (lambda f, c=c: f in c["flags"])
        g = lambda k, d, c=c: G(c, k, d)
        if v == "pad": b.pad(g("d", 8.0), g("w", width), N_START if has("start") else N_NORMAL)
        elif v == "run": b.run(g("len", 10.0), g("w", -1.0))
        elif v == "ice": b.ice(g("len", 10.0), g("w", -1.0))
        elif v == "rough": b.rough(g("len", 10.0), g("w", -1.0))
        elif v == "crumble": b.crumble(g("len", 8.0), g("w", -1.0), g("tile", 2.4))
        elif v == "slope": b.slope(g("len", 14.0), g("drop", 4.0), g("w", -1.0))
        elif v == "hill": b.hill(g("len", 14.0), g("drop", 4.0), g("w", -1.0), has("rails"))
        elif v == "jog": b.jog(g("dx", 0.0), g("d", 10.0), g("w", -1.0))
        elif v == "gap": b.gap(g("len", 3.0))
        elif v == "step": b.step(g("dy", 2.0))
        elif v == "jump": b.jump(g("gap", 2.6), g("drop", 2.4), g("lip", 5.0))
        elif v == "curve": b.curve(g("r", 14.0), g("a", 60.0), g("drop", 0.0), g("w", -1.0), g("bank", 0.0), has("rails"))
        elif v == "chicane": b.chicane(g("r", 14.0), g("a", 40.0), g("drop", 0.0), g("w", -1.0), g("bank", 0.0), has("rails"))
        elif v == "split": b.split(g("len", 14.0), g("side", 3.0), g("pit", 4.0), g("apron", 5.0))
        elif v == "rails": b.rails(g("l", 1.0) > 0.5, g("r", 1.0) > 0.5, g("h", 0.7))
        elif v == "acid": b.acid(g("w", 3.0), g("d", 3.0), g("x", 0.0), g("back", 3.0), g("lead", 4.0))
        elif v == "pillar": b.pillar(g("x", 0.0), g("back", 3.0), g("r", 0.85), g("h", 2.6))
        elif v == "sweeper": b.sweeper(g("x", 0.0), g("back", 4.0), g("len", 5.0), g("speed", 70.0), g("h", 0.55), g("phase", 0.0))
        elif v == "crusher": b.crusher(g("x", 0.0), g("back", 4.0), g("w", 3.0), g("period", 2.4), g("phase", 0.0), g("lift", 4.5))
        elif v == "fan": b.fan(g("x", 0.0), g("back", 3.0), g("w", 6.0), g("d", 6.0), g("push", 16.0))
        elif v == "boost": b.boost(g("x", 0.0), g("back", 3.0), g("w", 4.0), g("d", 5.0), g("push", 26.0))
        elif v == "chaser": b.enemy("chaser", g("x", 0.0), g("back", 4.0), g("range", 20.0), g("speed", 9.0))
        elif v == "blob": b.enemy("blob", g("x", 0.0), g("back", 4.0), g("range", 999.0), g("speed", 6.0))
        elif v == "goal": b.goal(g("d", 9.0), g("w", -1.0))
    return b.L

# ---------------------------------------------------------------- materials and light (linear colour)

def lin(c): return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
def srgb(l):
    l = max(0.0, min(1.0, l))
    return l * 12.92 if l <= 0.0031308 else 1.055 * l ** (1 / 2.4) - 0.055

# name: albedo (sRGB), metallic, emission (sRGB) - ProjectSetup.cs
MATS = {
    "Deck": ((0.58, 0.62, 0.70), 0.05, None), "DeckAlt": ((0.52, 0.56, 0.65), 0.05, None),
    "Rail": ((0.28, 0.31, 0.39), 0.25, None), "Ice": ((0.62, 0.86, 0.96), 0.0, None),
    "Rough": ((0.72, 0.57, 0.34), 0.0, None), "Acid": ((0.24, 0.92, 0.34), 0.0, (0.10, 0.45, 0.13)),
    "Goal": ((1.0, 0.82, 0.20), 0.10, (0.55, 0.38, 0.05)), "Start": ((0.26, 0.55, 1.0), 0.10, (0.06, 0.16, 0.42)),
    "Crumble": ((0.72, 0.52, 0.28), 0.0, None), "Danger": ((0.72, 0.20, 0.18), 0.35, (0.22, 0.03, 0.02)),
    "Prop": ((0.38, 0.40, 0.46), 0.55, None), "Boost": ((0.45, 1.0, 0.35), 0.0, (0.12, 0.45, 0.08)),
    "Fan": ((0.72, 0.35, 1.0), 0.0, (0.30, 0.08, 0.48)), "Bark": ((0.32, 0.24, 0.18), 0.0, None),
    "Leaf": ((0.24, 0.55, 0.30), 0.0, None), "Stone": ((0.26, 0.27, 0.33), 0.05, None),
    "Crystal": ((0.55, 0.82, 0.95), 0.20, (0.05, 0.15, 0.22)), "Glow": ((1.0, 0.66, 0.28), 0.0, (0.60, 0.30, 0.06)),
    "Pale": ((0.82, 0.88, 0.96), 0.05, None), "Candy": ((1.0, 0.38, 0.66), 0.0, (0.40, 0.06, 0.22)),
}
SURF_MAT = {N_NORMAL: "Deck", N_ROUGH: "Rough", N_ICE: "Ice", N_ACID: "Acid", N_GOAL: "Goal",
            N_START: "Start", N_RAIL: "Rail", N_CRUMBLE: "Crumble"}

def ldir(pitch, yaw):          # a directional light's travel direction (forward of its rotation)
    return qrot(euler(pitch, yaw, 0), FWD)

KEY = (norm(mul(ldir(52, -30), -1)), tuple(lin(c) * 1.15 for c in (1.0, 0.96, 0.88)))
FILL = (norm(mul(ldir(20, 160), -1)), tuple(lin(c) * 0.45 for c in (0.45, 0.55, 0.85)))
SKY, EQ, GND = [tuple(lin(c) for c in x) for x in ((0.32, 0.38, 0.5), (0.2, 0.22, 0.3), (0.07, 0.07, 0.1))]
FOG = tuple(lin(c) for c in (0.05, 0.06, 0.11))
FOG_LEVELS = [0.0, 0.04, 0.08, 0.13, 0.19, 0.26]  # a faint depth cue; the 3DO picks one by depth

def shade(mat, n):
    """Diffuse light fitted to the Unity game's screenshots (Standard shader,
    linear colour): lit tops come out at about their albedo - the shader's
    reflections brighten the grazing top faces - and sides lit by the key
    light at about half. The fill light tints the shaded sides blue."""
    alb, metal, emis = MATS[mat]
    a = [lin(c) for c in alb]
    fill_col = [lin(c) for c in (0.45, 0.55, 0.85)]
    out = []
    for i in range(3):
        light = 0.06 + 0.70 * max(0.0, dot(n, KEY[0])) + 0.30 * fill_col[i] * max(0.0, dot(n, FILL[0])) \
            + 0.50 * max(0.0, n[1])
        v = a[i] * light * (1 - 0.75 * metal) + a[i] * metal * 0.22
        if emis:
            v += lin(emis[i])
        out.append(v)
    return out

def rgb15(c):
    r, g, b = (min(31, int(srgb(x) * 31 + 0.5)) for x in c)
    v = (r << 10) | (g << 5) | b
    return v or 1

def fogged(c):
    return [rgb15([lerp(c[i], FOG[i], f) for i in range(3)]) for f in FOG_LEVELS]

# ---------------------------------------------------------------- meshing

YAW45 = angle_axis(45, UP)
def W(p): return qrot(YAW45, p)       # course space -> world


class Mesh:
    """Quads to draw (world space) and triangles to collide with."""
    def __init__(self):
        self.quads = []     # (v0..v3, linear colour, group, kind)
        self.tris = []      # (v0, v1, v2, surface class, group)
        self.caps = []      # ((centre, outward normal), first quad, first tri, count): piece ends

    def drop_buried_caps(self):
        """Pieces overlap their neighbours by a hair, so most end faces are
        buried inside the next piece: find facing pairs and drop both."""
        dead = set()
        for i, ((c1, n1), q1, t1, k1) in enumerate(self.caps):
            for j in range(i + 1, len(self.caps)):
                (c2, n2), q2, t2, k2 = self.caps[j]
                if dot(n1, n2) < -0.9 and length(sub(c1, c2)) < 0.4:
                    dead.add(i)
                    dead.add(j)
        dq, dt = set(), set()
        for i in dead:
            _, q, t, k = self.caps[i]
            dq.update(range(q, q + k))
            dt.update(range(t, t + 2 * k))
        self.quads = [q for i, q in enumerate(self.quads) if i not in dq]
        self.tris = [t for i, t in enumerate(self.tris) if i not in dt]
        return len(dead)

    def face(self, a, b, c, d, mat, n, collide=None, group=0, kind=0, maxlen=3.0, draw=True, cap=None):
        """Quad a b c d (outward winding), split into pieces no longer than maxlen.
        Deck tops are only split along the deck: across it nothing needs
        sorting against the marble."""
        lu = max(length(sub(b, a)), length(sub(c, d)))
        lv = max(length(sub(d, a)), length(sub(c, b)))
        nu = max(1, math.ceil(lu / maxlen))
        nv = max(1, math.ceil(lv / maxlen))
        if n[1] > 0.7 and lu < 14:
            nu = 1                        # deck tops: a -> b runs across the deck
        if cap is not None:
            self.caps.append((cap, len(self.quads), len(self.tris), nu * nv))
        col = shade(mat, n) if draw else None
        for i in range(nu):
            for j in range(nv):
                def P(u, v):
                    return vlerp(vlerp(a, b, u), vlerp(d, c, u), v)
                q = (P(i / nu, j / nv), P((i + 1) / nu, j / nv), P((i + 1) / nu, (j + 1) / nv), P(i / nu, (j + 1) / nv))
                if draw:
                    self.quads.append((q, col, group, kind))
                if collide is not None:
                    self.tris.append((q[0], q[1], q[2], collide, group))
                    self.tris.append((q[0], q[2], q[3], collide, group))


def facing(a, b, d, want):
    """True if a->b x a->d points along `want` (else the quad needs flipping)."""
    return dot(cross(sub(b, a), sub(d, a)), want) > 0

def add_quad(m, a, b, c, d, out, mat, **kw):
    if not facing(a, b, d, out):
        a, b, c, d = a, d, c, b
    m.face(a, b, c, d, mat, norm(out), **kw)

# collision classes: (friction mu, bounce) - PhysX Multiply / Maximum against the marble's 0.5 / 0.15
C_DECK, C_ROUGH, C_ICE, C_RAIL, C_BOUNCY = range(5)
SURF_CLASS = {N_NORMAL: C_DECK, N_ROUGH: C_ROUGH, N_ICE: C_ICE, N_GOAL: C_DECK, N_START: C_DECK,
              N_RAIL: C_RAIL, N_CRUMBLE: C_DECK}


def box(m, centre, size, rot, mat, collide=None, group=0, kind=0, bottom=False, draw=True):
    """A Unity cube primitive: course-space centre/size/rotation -> world quads."""
    hx, hy, hz = size[0] / 2, size[1] / 2, size[2] / 2
    def C(x, y, z):
        return W(add(centre, qrot(rot, (x * hx, y * hy, z * hz))))
    faces = [((1, 0, 0), [(1, -1, -1), (1, -1, 1), (1, 1, 1), (1, 1, -1)]),
             ((-1, 0, 0), [(-1, -1, -1), (-1, 1, -1), (-1, 1, 1), (-1, -1, 1)]),
             ((0, 1, 0), [(-1, 1, -1), (1, 1, -1), (1, 1, 1), (-1, 1, 1)]),
             ((0, -1, 0), [(-1, -1, -1), (-1, -1, 1), (1, -1, 1), (1, -1, -1)]),
             ((0, 0, 1), [(-1, -1, 1), (-1, 1, 1), (1, 1, 1), (1, -1, 1)]),
             ((0, 0, -1), [(-1, -1, -1), (1, -1, -1), (1, 1, -1), (-1, 1, -1)])]
    for nl, cs in faces:
        n = W(qrot(rot, nl))
        if n[1] < -0.5 and not bottom:
            continue                      # undersides are never seen and never hit
        a, b, c, d = (C(*p) for p in cs)
        cap = None
        if nl[2] != 0:
            cap = (mul(add(add(a, b), add(c, d)), 0.25), n)
        add_quad(m, a, b, c, d, n, mat, collide=collide, group=group, kind=kind, draw=draw, cap=cap)


def prism(m, base, radius, height, sides, mat, collide=None, top=True):
    """A Unity cylinder, as an n-sided prism standing on world point `base`."""
    ring = [(base[0] + radius * math.cos(2 * math.pi * k / sides), base[2] + radius * math.sin(2 * math.pi * k / sides))
            for k in range(sides)]
    y0, y1 = base[1], base[1] + height
    for k in range(sides):
        x0, z0 = ring[k]
        x1, z1 = ring[(k + 1) % sides]
        mid = ((x0 + x1) / 2 - base[0], 0.0, (z0 + z1) / 2 - base[2])
        add_quad(m, (x0, y0, z0), (x1, y0, z1), (x1, y1, z1), (x0, y1, z0), norm(mid), mat, collide=collide)
    if top:
        # an octagon top as three quads
        p = [(x, y1, z) for x, z in ring]
        for q in ((0, 1, 2, 3), (0, 3, 4, 7), (4, 5, 6, 7)) if sides == 8 else ((0, 1, 2, 3), (0, 3, 4, 5)):
            add_quad(m, p[q[0]], p[q[1]], p[q[2]], p[q[3]], UP, mat)


def ribbon(m, rib, mat):
    nodes = rib["nodes"]
    n = len(nodes)
    tl, tr, bl, br, up = [], [], [], [], []
    for p, r, w in nodes:
        right, u = qrot(r, RIGHT), qrot(r, UP)
        h = w * 0.5
        tl.append(W(sub(p, mul(right, h))))
        tr.append(W(add(p, mul(right, h))))
        bl.append(W(sub(sub(p, mul(right, h)), mul(u, rib["thick"]))))
        br.append(W(sub(add(p, mul(right, h)), mul(u, rib["thick"]))))
        up.append(W(u))
    cls = SURF_CLASS[rib["s"]]

    def strip(ids, draw, collide):
        for a, b in zip(ids, ids[1:]):
            out = norm(add(up[a], up[b]))
            add_quad(m, tl[a], tr[a], tr[b], tl[b], out, mat, collide=collide, draw=draw)
            side = norm(sub(tl[a], tr[a]))
            add_quad(m, bl[a], tl[a], tl[b], bl[b], side, mat, collide=collide, draw=draw)
            add_quad(m, tr[a], br[a], br[b], tr[b], mul(side, -1), mat, collide=collide, draw=draw)

    # collide with every cross-section; draw every other one on long sweeps -
    # an 8-degree step still reads as a smooth curve at 320 x 240
    every = list(range(n))
    strip(every, False, cls)
    drawn = every if n < 8 else every[::2] + ([n - 1] if (n - 1) % 2 else [])
    strip(drawn, True, None)
    for i, sgn in ((0, -1), (n - 1, 1)):
        f = norm(sub(add(tl[min(n - 1, i + 1)], tr[min(n - 1, i + 1)]), add(tl[max(0, i - 1)], tr[max(0, i - 1)])))
        out = mul(f, sgn)
        add_quad(m, tl[i], tr[i], br[i], bl[i], out, mat, collide=cls,
                 cap=(mul(add(add(tl[i], tr[i]), add(br[i], bl[i])), 0.25), out))


# ---------------------------------------------------------------- the course

def fx(v): return int(round(v * 4096))
def q14(v): return int(round(v * 16384))


class Course:
    def __init__(self, path):
        self.L = build(open(path).read())
        L = self.L
        self.m = Mesh()
        normal_index = 0
        self.crumbles = []
        self.acids = []
        self.goal = None
        lowest = 1e9
        for blk in L.blocks:
            s = blk["s"]
            mat = SURF_MAT[s]
            if s == N_NORMAL:
                if normal_index & 1:
                    mat = "DeckAlt"
                normal_index += 1
            lowest = min(lowest, blk["c"][1] - blk["size"][1])
            if s == N_ACID:
                box(self.m, blk["c"], blk["size"], blk["rot"], mat, kind=2)      # a decal on the deck
                self.acids.append(self.obb(blk["c"], blk["size"], blk["rot"]))
            elif s == N_CRUMBLE:
                g = len(self.crumbles) + 1
                box(self.m, blk["c"], blk["size"], blk["rot"], mat, collide=C_DECK, group=g, kind=1)
                self.crumbles.append((W(blk["c"]), g))
            else:
                box(self.m, blk["c"], blk["size"], blk["rot"], mat, collide=SURF_CLASS[s])
            if s == N_GOAL:
                up = qrot(blk["rot"], UP)
                c = add(blk["c"], mul(up, blk["size"][1] * 0.5 + 1.3))
                self.goal = self.obb(c, (blk["size"][0] * 0.9, 2.6, blk["size"][2] * 0.9), blk["rot"])
                self.goal_world = W(add(blk["c"], (0, 1.5, 0)))
        for rib in L.ribbons:
            mat = SURF_MAT[rib["s"]]
            if rib["s"] == N_NORMAL:
                if normal_index & 1:
                    mat = "DeckAlt"
                normal_index += 1
            ribbon(self.m, rib, mat)
            for p, _, _ in rib["nodes"]:
                lowest = min(lowest, p[1] - rib["thick"] * 2)
        self.kill_y = lowest - 14
        self.buried = self.m.drop_buried_caps()
        self.props()
        self.route()
        self.decor()

    def obb(self, c, size, rot):
        """A trigger box: world centre, half sizes, and its yaw (these are all yaw-only)."""
        f = W(qrot(rot, FWD))
        yaw = math.atan2(f[0], f[2])
        return (W(c), (size[0] / 2, size[1] / 2, size[2] / 2), yaw)

    def props(self):
        self.pillars, self.sweepers, self.crushers, self.zones = [], [], [], []
        for p in self.L.props:
            pos = W(p["pos"])
            f = W(qrot(p["rot"], FWD))
            yaw = math.atan2(f[0], f[2])
            k = p["kind"]
            if k == "pillar":
                prism(self.m, pos, p["size"] * 1.3, 0.24, 8, "Rail")
                prism(self.m, pos, p["size"], p["height"], 8, "Prop")
                self.pillars.append((pos, p["size"], p["height"]))
            elif k == "sweeper":
                prism(self.m, pos, 0.25, 1.4, 6, "Prop")
                self.sweepers.append((add(pos, (0, p["height"], 0)), p["size"], p["speed"], yaw))
            elif k == "crusher":
                period = max(0.6, p["speed"])
                self.crushers.append((add(pos, (0, 0.9, 0)), p["size"], period, p["phase"], p["height"], yaw))
            elif k in ("fan", "boost"):
                fwd = k == "boost"
                box(self.m, p["pos"], (p["size"], 0.1, p["depth"]), p["rot"], "Boost" if fwd else "Fan", kind=2)
                d = qrot(p["rot"], FWD if fwd else RIGHT)
                d = W(mul(d, 1.0 if p["power"] >= 0 else -1.0))
                c = add(p["pos"], qrot(p["rot"], (0, 1.2, 0)))
                self.zones.append((self.obb(c, (p["size"], 2.4, p["depth"]), p["rot"]), d, abs(p["power"]), 1 if fwd else 0))
        self.enemies = []
        across = W(RIGHT)
        for e in self.L.enemies:
            self.enemies.append((0 if e["kind"] == "chaser" else 1, W(e["pos"]), e["range"], e["speed"], across))

    # -------- the route, and where it's safe to put a marble back down
    def ray_down(self, p, dist):
        """Highest collision triangle under p within dist (a vertical ray)."""
        best = None
        for a, b, c, cls, g in self.m.tris:
            n = cross(sub(b, a), sub(c, a))
            if abs(n[1]) < 1e-9:
                continue
            # barycentric in xz
            def side(p0, p1):
                return (p1[0] - p0[0]) * (p[2] - p0[2]) - (p1[2] - p0[2]) * (p[0] - p0[0])
            s1, s2, s3 = side(a, b), side(b, c), side(c, a)
            if not ((s1 >= 0 and s2 >= 0 and s3 >= 0) or (s1 <= 0 and s2 <= 0 and s3 <= 0)):
                continue
            y = a[1] - (n[0] * (p[0] - a[0]) + n[2] * (p[2] - a[2])) / n[1]
            if p[1] - dist <= y <= p[1] and (best is None or y > best):
                best = y
        return best

    def in_acid(self, p, r):
        for c, h, yaw in self.acids:
            d = sub(p, c)
            cs, sn = math.cos(yaw), math.sin(yaw)
            lx, lz = d[0] * cs - d[2] * sn, d[0] * sn + d[2] * cs
            q = (max(-h[0], min(h[0], lx)), max(-h[1], min(h[1], d[1])), max(-h[2], min(h[2], lz)))
            if (lx - q[0]) ** 2 + (d[1] - q[1]) ** 2 + (lz - q[2]) ** 2 < r * r:
                return True
        return False

    def route(self):
        dense = []
        pts = self.L.path
        for i in range(len(pts)):
            if i > 0:
                a, b = pts[i - 1], pts[i]
                steps = max(1, math.ceil(length(sub(b, a)) / 2))
                for k in range(1, steps):
                    dense.append(vlerp(a, b, k / steps))
            dense.append(pts[i])
        self.path = [W(add(p, (0, 0.5, 0))) for p in dense]
        self.safe = []
        for w in self.path:
            hit = self.ray_down(add(w, (0, 1.0, 0)), 4.0) is not None
            self.safe.append(hit and not self.in_acid(w, 0.8))
        run = [0.0]
        for i in range(1, len(self.path)):
            run.append(run[-1] + length(sub(self.path[i], self.path[i - 1])))
        tot = run[-1] or 1.0
        self.progress = [d / tot for d in run]
        self.spawn = W(self.L.spawn)

    # -------- Decor.cs: a dozen floating pieces of themed scenery
    def decor(self):
        from_seed = sum(ord(ch) * 31 ** i for i, ch in enumerate(self.L.name)) & 0x7FFFFFFF
        rng = NetRandom(from_seed ^ (self.L.decor * 7919))
        R = lambda lo, hi: lo + rng.next_double() * (hi - lo)
        path = self.L.path
        self.decors = []
        if len(path) < 4:
            return
        for i in range(12):
            idx = min(max(round((i + 0.5) / 12 * (len(path) - 1)), 1), len(path) - 2)
            anchor = path[idx]
            tg = sub(path[idx + 1], path[idx - 1])
            tg = norm((tg[0], 0.0, tg[2])) if (tg[0] ** 2 + tg[2] ** 2) > 0.01 else FWD
            side = cross(UP, tg)
            sgn = -1.0 if rng.next(2) == 0 else 1.0
            pos = add(add(add(anchor, mul(side, sgn * R(22, 42))), mul(tg, R(-7, 7))), (0, R(-24, 2), 0))
            rot = euler(0, R(0, 360), 0)
            s = R(0.9, 2.2)
            parts = []
            build_decor(self.L.decor, parts, rng, R)
            phase, amp, speed, spin = R(0, 6.28), R(0.3, 1.1), R(0.2, 0.55), R(-11, 11)
            self.decors.append((W(pos), rot, s, phase, amp, speed, spin, parts))


def build_decor(theme, parts, rng, R):
    """Decor.cs: parts are (kind, centre, scale, rotation, material); kinds 0 cube 1 cylinder 2 sphere."""
    def part(kind, p, scale, rot, mat): parts.append((kind, p, scale, rot, mat))
    def tree():
        part(1, (0, 0, 0), (0.35, 1.6, 0.35), QI, "Bark")
        tiers, y, w = rng.next(2, 4), 1.6, R(2.2, 3.0)
        for i in range(tiers):
            part(0, (0, y, 0), (w, w * 0.55, w), euler(0, 45 + i * 22, 0), "Leaf")
            y += w * 0.5
            w *= 0.68
    def island():
        part(0, (0, 0, 0), (R(4, 7), 0.9, R(4, 7)), euler(0, R(0, 90), 0), "Stone")
        part(0, (0, 1.1, 0), (1.6, 1.4, 1.6), euler(0, 30, 0), "Leaf")
    def crystal():
        for _ in range(rng.next(2, 4)):
            part(0, (R(-0.8, 0.8), R(-0.4, 1.2), R(-0.8, 0.8)), (R(0.5, 0.9), R(2.4, 4.5), R(0.5, 0.9)),
                 euler(R(-28, 28), R(0, 90), R(-28, 28)), "Crystal")
    def orb():
        part(2, (0, 0, 0), (R(1.6, 2.6),) * 3, QI, "Crystal")
        part(1, (0, 0, 0), (3.2, 0.06, 3.2), euler(R(-25, 25), 0, R(-25, 25)), "Glow")
    def arch():
        span, h = R(3.5, 6), R(3, 5)
        for sd in (-1, 1):
            part(0, (sd * span * 0.5, h * 0.5, 0), (0.5, h, 0.5), QI, "Pale")
        part(0, (0, h + 0.3, 0), (span + 0.5, 0.6, 0.7), QI, "Pale")
    def ring():
        r = R(2.4, 4.2)
        rot = euler(R(60, 120), R(0, 360), 0)
        part(1, (0, 0, 0), (r, 0.12, r), rot, "Crystal")
        part(1, (0, 0, 0), (r * 0.72, 0.16, r * 0.72), rot, "Pale")
    def candy():
        mats = ["Candy", "Glow", "Leaf", "Crystal"]
        y = 0.0
        for _ in range(rng.next(3, 6)):
            w = R(1.2, 2.6)
            part(0, (R(-0.5, 0.5), y, R(-0.5, 0.5)), (w, w, w), euler(R(-30, 30), R(0, 90), R(-30, 30)), mats[rng.next(4)])
            y += w * 0.85
    def balloon():
        r = R(1.6, 2.8)
        part(2, (0, r, 0), (r, r, r), QI, "Candy" if rng.next(2) == 0 else "Glow")
        part(1, (0, 0, 0), (0.1, r * 0.9, 0.1), QI, "Pale")
    def monolith():
        h, w = R(5, 11), R(1.4, 2.6)
        part(0, (0, h * 0.5, 0), (w, h, w * 0.7), euler(0, R(0, 90), R(-6, 6)), "Stone")
        part(0, (0, h * 0.78, 0), (w * 1.05, h * 0.06, w * 0.75), euler(0, R(0, 90), 0), "Danger")
    def spire():
        y, w = 0.0, R(1.8, 2.6)
        for i in range(rng.next(3, 6)):
            h = R(1.0, 2.0)
            part(0, (0, y + h * 0.5, 0), (w, h, w), euler(0, i * 18, 0), "Stone")
            y += h
            w *= 0.74
        part(0, (0, y + 0.6, 0), (w, 1.2, w), euler(0, 45, 0), "Glow")
    def shard():
        part(0, (0, 0, 0), (R(0.5, 1.1), R(4, 8), R(0.5, 1.1)), euler(R(-50, 50), R(0, 360), R(-50, 50)), "Stone")
    if theme == 1:
        orb() if rng.next(3) == 0 else crystal()
    elif theme == 2:
        shard() if rng.next(3) == 0 else spire()
    elif theme == 3:
        ring() if rng.next(2) == 0 else arch()
    elif theme == 4:
        balloon() if rng.next(2) == 0 else candy()
    elif theme == 5:
        shard() if rng.next(3) == 0 else monolith()
    else:
        island() if rng.next(4) == 0 else tree()


class NetRandom:
    """.NET's seeded System.Random (Decor.cs uses it)."""
    MBIG = 2147483647
    def __init__(self, seed):
        sa = [0] * 56
        sub_ = 2147483647 if seed == -2147483648 else abs(seed)
        mj = 161803398 - sub_
        sa[55] = mj
        mk = 1
        for i in range(1, 55):
            ii = (21 * i) % 55
            sa[ii] = mk
            mk = mj - mk
            if mk < 0:
                mk += self.MBIG
            mj = sa[ii]
        for _ in range(4):
            for i in range(1, 56):
                sa[i] -= sa[1 + (i + 30) % 55]
                if sa[i] < 0:
                    sa[i] += self.MBIG
        self.sa, self.a, self.b = sa, 0, 21
    def _s(self):
        self.a = 1 if self.a + 1 >= 56 else self.a + 1
        self.b = 1 if self.b + 1 >= 56 else self.b + 1
        r = self.sa[self.a] - self.sa[self.b]
        if r == self.MBIG:
            r -= 1
        if r < 0:
            r += self.MBIG
        self.sa[self.a] = r
        return r / self.MBIG
    def next_double(self): return self._s()
    def next(self, lo, hi=None):
        if hi is None:
            lo, hi = 0, lo
        return int(self._s() * (hi - lo)) + lo

# ---------------------------------------------------------------- output

def decor_geometry(parts):
    """Unit-space quads and spheres of one decor piece (local, before bob/spin/scale)."""
    m = Mesh()
    spheres = []
    for kind, p, scale, rot, mat in parts:
        if kind == 0:
            hx, hy, hz = scale[0] / 2, scale[1] / 2, scale[2] / 2
            corners = {}
            for nl, cs in [((1, 0, 0), [(1, -1, -1), (1, -1, 1), (1, 1, 1), (1, 1, -1)]),
                           ((-1, 0, 0), [(-1, -1, -1), (-1, 1, -1), (-1, 1, 1), (-1, -1, 1)]),
                           ((0, 1, 0), [(-1, 1, -1), (1, 1, -1), (1, 1, 1), (-1, 1, 1)]),
                           ((0, -1, 0), [(-1, -1, -1), (-1, -1, 1), (1, -1, 1), (1, -1, -1)]),
                           ((0, 0, 1), [(-1, -1, 1), (-1, 1, 1), (1, 1, 1), (1, -1, 1)]),
                           ((0, 0, -1), [(-1, -1, -1), (1, -1, -1), (1, 1, -1), (-1, 1, -1)])]:
                n = qrot(rot, nl)
                q = [add(p, qrot(rot, (x * hx, y * hy, z * hz))) for x, y, z in cs]
                add_quad(m, q[0], q[1], q[2], q[3], n, mat, maxlen=99)
        elif kind == 1:
            # Unity's cylinder: radius scale.x/2, height 2 * scale.y, centred
            sides = 6
            r, h = scale[0] / 2, scale[1]
            ring = [(r * math.cos(2 * math.pi * k / sides), r * math.sin(2 * math.pi * k / sides)) for k in range(sides)]
            for k in range(sides):
                x0, z0 = ring[k]
                x1, z1 = ring[(k + 1) % sides]
                q = [add(p, qrot(rot, v)) for v in ((x0, -h, z0), (x1, -h, z1), (x1, h, z1), (x0, h, z0))]
                add_quad(m, q[0], q[1], q[2], q[3], qrot(rot, norm(((x0 + x1) / 2, 0, (z0 + z1) / 2))), mat, maxlen=99)
            for ys, nl in ((h, (0, 1, 0)), (-h, (0, -1, 0))):
                pts = [add(p, qrot(rot, (x, ys, z))) for x, z in ring]
                for q in ((0, 1, 2, 3), (0, 3, 4, 5)):
                    add_quad(m, pts[q[0]], pts[q[1]], pts[q[2]], pts[q[3]], qrot(rot, nl), mat, maxlen=99)
        else:
            spheres.append((p, scale[0] / 2, mat))
    return m, spheres


SPHERE_MATS = ["Marble", "MarbleTwo", "Steel", "Blob", "Ghost", "Crystal", "Candy", "Glow"]


def write(course, path):
    m = course.m
    verts, vindex = [], {}
    def V(p):
        k = (fx(p[0]), fx(p[1]), fx(p[2]))
        if k not in vindex:
            vindex[k] = len(verts)
            verts.append(k)
        return vindex[k]
    def packn(q):
        n = norm(cross(sub(q[1], q[0]), sub(q[3], q[0])))
        c = [max(-511, min(511, int(round(v * 511)))) & 1023 for v in n]
        return (c[0] << 20) | (c[1] << 10) | c[2]
    quads = [([V(p) for p in q], fogged(col), g, kind, packn(q)) for q, col, g, kind in m.quads]
    tris = []
    for a, b, c, cls, g in m.tris:
        n = norm(cross(sub(b, a), sub(c, a)))
        if length(n) < 0.5:
            continue
        tris.append(([V(a), V(b), V(c)], n, cls, g))
    # spatial grids over x/z
    xs = [v[0] for v in verts]
    zs = [v[2] for v in verts]
    x0, z0 = min(xs) - fx(4), min(zs) - fx(4)

    def grid(items, cell, cells_of):
        nx = (max(xs) - x0) // fx(cell) + 2
        nz = (max(zs) - z0) // fx(cell) + 2
        lists = [[] for _ in range(nx * nz)]
        for i, it in enumerate(items):
            for cx, cz in cells_of(it, cell, nx, nz):
                lists[cz * nx + cx].append(i)
        return nx, nz, lists

    def tri_cells(t, cell, nx, nz):
        vs = [verts[i] for i in t[0]]
        m_ = fx(0.8)                      # sphere radius (steel marbles are 0.7) + a little
        ax0 = (min(v[0] for v in vs) - m_ - x0) // fx(cell)
        ax1 = (max(v[0] for v in vs) + m_ - x0) // fx(cell)
        az0 = (min(v[2] for v in vs) - m_ - z0) // fx(cell)
        az1 = (max(v[2] for v in vs) + m_ - z0) // fx(cell)
        return [(x, z) for x in range(max(0, ax0), min(nx - 1, ax1) + 1) for z in range(max(0, az0), min(nz - 1, az1) + 1)]

    def quad_cells(q, cell, nx, nz):
        vs = [verts[i] for i in q[0]]
        cx = (sum(v[0] for v in vs) // 4 - x0) // fx(cell)
        cz = (sum(v[2] for v in vs) // 4 - z0) // fx(cell)
        return [(min(nx - 1, max(0, cx)), min(nz - 1, max(0, cz)))]

    cnx, cnz, clists = grid(tris, 2, tri_cells)
    rnx, rnz, rlists = grid(quads, 8, quad_cells)
    out = []
    def I(*vals): out.extend(int(v) for v in vals)
    name = course.L.name.encode()[:31].ljust(32, b"\0")
    I(0x52534331)                                          # 'RSC1'
    out.extend(struct.unpack(">8i", name))
    L = course.L
    I(round(L.time * 1000), round(L.gold * 1000), round(L.silver * 1000), round(L.bronze * 1000), L.decor, L.music)
    I(*(fx(c) for c in course.spawn))
    gc, gh, gyaw = course.goal
    I(*(fx(c) for c in gc), *(fx(c) for c in gh), q14(math.cos(gyaw)), q14(math.sin(gyaw)))
    I(*(fx(c) for c in course.goal_world))
    I(fx(course.kill_y))
    I(len(verts))
    for v in verts:
        I(*v)
    I(len(quads))
    for vs, cols, g, kind, pn in quads:
        I(*vs)
        I((cols[0] << 16) | cols[1], (cols[2] << 16) | cols[3], (cols[4] << 16) | cols[5], (g << 8) | kind)
        I(pn - (1 << 32) if pn & 0x80000000 else pn)
    I(len(tris))
    for vs, n, cls, g in tris:
        I(*vs, q14(n[0]), q14(n[1]), q14(n[2]), (g << 8) | cls)
    for nx, nz, lists, cell in ((cnx, cnz, clists, 2), (rnx, rnz, rlists, 8)):
        I(x0, z0, fx(cell), nx, nz)
        off = 0
        for l in lists:
            I(off)
            off += len(l)
        I(off)
        for l in lists:
            I(*l)
        if cell == 8:
            # bounding sphere of each render cell
            for l in lists:
                if not l:
                    I(0, 0, 0, 0)
                    continue
                ps = [verts[i] for qi in l for i in quads[qi][0]]
                lo = [min(p[k] for p in ps) for k in range(3)]
                hi = [max(p[k] for p in ps) for k in range(3)]
                c = [(lo[k] + hi[k]) // 2 for k in range(3)]
                r = max(math.sqrt(sum((p[k] - c[k]) ** 2 for k in range(3))) for p in ps)
                I(*c, int(r) + 1)
    I(len(course.path))
    for p, s, pr in zip(course.path, course.safe, course.progress):
        I(*(fx(c) for c in p), 1 if s else 0, int(pr * 65536))
    def obb(o):
        c, h, yaw = o
        I(*(fx(v) for v in c), *(fx(v) for v in h), q14(math.cos(yaw)), q14(math.sin(yaw)))
    I(len(course.acids))
    for a in course.acids:
        obb(a)
    I(len(course.zones))
    for o, d, power, kind in course.zones:
        obb(o)
        I(q14(d[0]), q14(d[1]), q14(d[2]), fx(power), kind)
    I(len(course.pillars))
    for p, r, h in course.pillars:
        I(*(fx(c) for c in p), fx(r), fx(h))
    I(len(course.sweepers))
    for p, ln, speed, yaw in course.sweepers:
        I(*(fx(c) for c in p), fx(ln), fx(speed), fx(math.degrees(yaw)))
    I(len(course.crushers))
    for p, w, period, phase, lift, yaw in course.crushers:
        I(*(fx(c) for c in p), fx(w), fx(period), fx(phase), fx(lift), q14(math.cos(yaw)), q14(math.sin(yaw)))
    I(len(course.enemies))
    for kind, p, rng_, speed, across in course.enemies:
        I(kind, *(fx(c) for c in p), fx(min(rng_, 999)), fx(speed), q14(across[0]), q14(across[2]))
    I(len(course.crumbles))
    for c, g in course.crumbles:
        I(*(fx(v) for v in c), g)
    # decor: per piece its placement and motion, then its own little mesh
    I(len(course.decors))
    for pos, rot, s, phase, amp, speed, spin, parts in course.decors:
        dm, spheres = decor_geometry(parts)
        yaw0 = math.degrees(2 * math.atan2(rot[1], rot[3]))
        I(*(fx(c) for c in pos), fx(s), fx(phase), fx(amp), fx(speed), fx(spin), fx(yaw0))
        dv, dvi = [], {}
        def DV(p):
            k = (fx(p[0]), fx(p[1]), fx(p[2]))
            if k not in dvi:
                dvi[k] = len(dv)
                dv.append(k)
            return dvi[k]
        dq = [([DV(p) for p in q], fogged(col)) for q, col, _, _ in dm.quads]
        I(len(dv))
        for v in dv:
            I(*v)
        I(len(dq))
        for vs, cols in dq:
            I(*vs, (cols[0] << 16) | cols[1], (cols[2] << 16) | cols[3], (cols[4] << 16) | cols[5])
        I(len(spheres))
        for p, r, mat in spheres:
            I(*(fx(c) for c in p), fx(r), SPHERE_MATS.index(mat))
    data = struct.pack(">%di" % len(out), *out)
    open(path, "wb").write(data)
    return len(verts), len(quads), len(tris), len(data)


def main():
    os.makedirs(OUT, exist_ok=True)
    for i in range(1, 7):
        c = Course(os.path.join(ROOT, "courses", "%d.course" % i))
        nv, nq, nt, size = write(c, os.path.join(OUT, "course%d.bin" % i))
        print("course%d %-12s verts %5d quads %5d tris %5d path %3d (%d safe) %6d bytes  decor %d  buried caps %d" %
              (i, c.L.name, nv, nq, nt, len(c.path), sum(c.safe), size, len(c.decors), c.buried))


if __name__ == "__main__":
    main()
