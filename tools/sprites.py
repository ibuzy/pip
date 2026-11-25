"""The little pictures round Pip's eyes (hearts, stars, notes, the fire...): ui/sprite_art.c, A8 coverage masks kept in flash.

    py tools/sprites.py

Each is a shape function over -1..1 (y down), sampled 8x8 a pixel. Made here and not on the board: a big one takes it
a good part of a second (a hundred thousand hypotf). Only the sizes ui/pip.c uses are made (SIZES); a picture asked for in
another size is simply not drawn. ~85 KB of flash, no RAM."""
import math, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PX = [18, 28, 44, 64, 92]  # sprite.c's sizes 0..4
SS = 6


def seg(x, y, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    t = max(0, min(1, ((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy)))
    return math.hypot(x - ax - dx * t, y - ay - dy * t)


def ell(x, y, cx, cy, rx, ry, a=0):
    c, s = math.cos(a), math.sin(a)
    return math.hypot(((x - cx) * c + (y - cy) * s) / rx, (-(x - cx) * s + (y - cy) * c) / ry)


def heart(x, y):
    X, Y = x * 1.2, -y * 1.2 + 0.12
    a = X * X + Y * Y - 1
    return a ** 3 - X * X * Y ** 3 < 0


def star(x, y):  # a rounded five-pointed star (Inigo Quilez's sdStar5, drawn out by its rounding)
    r, rf = 0.82, 0.52
    y = -(y + 0.02)
    x = abs(x)
    d = max(0.809017 * x - 0.587785 * y, 0) * 2
    x, y = x - d * 0.809017, y + d * 0.587785
    d = max(-0.809017 * x - 0.587785 * y, 0) * 2
    x, y = x + d * 0.809017, y + d * 0.587785
    x, y = abs(x), y - r
    bx, by = rf * 0.587785, rf * 0.809017 - 1
    h = max(0, min(r, (x * bx + y * by) / (bx * bx + by * by)))
    l = math.hypot(x - bx * h, y - by * h)
    return (l if y * bx - x * by > 0 else -l) < 0.13


def spark(x, y):
    return math.sqrt(abs(x)) + math.sqrt(abs(y)) < 1 or math.hypot(x, y) < 0.2


def note(x, y):
    return (ell(x, y, -0.18, 0.58, 0.4, 0.3, -0.4) < 1 or seg(x, y, 0.13, 0.5, 0.13, -0.85) < 0.08 or
            seg(x, y, 0.13, -0.8, 0.5, -0.4) < 0.11 or seg(x, y, 0.5, -0.4, 0.55, -0.05) < 0.08)


def note2(x, y):
    return (ell(x, y, -0.6, 0.6, 0.32, 0.25, -0.4) < 1 or ell(x, y, 0.4, 0.45, 0.32, 0.25, -0.4) < 1 or
            seg(x, y, -0.35, 0.55, -0.35, -0.6) < 0.07 or seg(x, y, 0.65, 0.4, 0.65, -0.75) < 0.07 or
            seg(x, y, -0.35, -0.55, 0.65, -0.72) < 0.15)


def zed(x, y):
    return seg(x, y, -0.6, -0.65, 0.6, -0.65) < 0.17 or seg(x, y, 0.6, -0.65, -0.6, 0.65) < 0.17 or seg(x, y, -0.6, 0.65, 0.6, 0.65) < 0.17


def ring(x, y):  # a bubble: the rim, a glint, the film hardly there
    r = math.hypot(x, y)
    if r >= 0.96: return 0
    return 1 if r > 0.86 or ell(x, y, -0.4, -0.42, 0.22, 0.12, -0.8) < 1 else 0.14


def bfly(k):
    def f(x, y):
        X = abs(x) / k
        return ell(X, y, 0.5, -0.35, 0.5, 0.4, -0.6) < 1 or ell(X, y, 0.38, 0.4, 0.34, 0.36, 0.5) < 1 or seg(abs(x), y, 0, -0.45, 0, 0.5) < 0.1
    return f


def ear(side):  # a rounded triangle leaning out
    def f(x, y):
        x *= side
        p = ((-0.35, -0.75), (-0.7, 0.75), (0.75, 0.45))
        e = [(p[(i + 1) % 3][0] - p[i][0]) * (y - p[i][1]) - (p[(i + 1) % 3][1] - p[i][1]) * (x - p[i][0]) for i in range(3)]
        return all(v >= 0 for v in e) or all(v <= 0 for v in e) or min(seg(x, y, *p[i], *p[(i + 1) % 3]) for i in range(3)) < 0.2
    return f


def flame(lean):
    def f(x, y):
        v = (y + 1) / 2  # 0 the tip .. 1 the bottom
        if v < 0 or v > 0.98: return 0
        w = 0.62 * math.sqrt(max(0, 1 - (v - 0.68) ** 2 / 0.09)) if v > 0.68 else 0.62 * (v / 0.68) ** 1.25
        return abs(x - lean * (1 - v) ** 2) < w
    return f


def dot(x, y):
    return math.hypot(x, y) < 0.96


def bolt(x, y):
    q = ((.2, -.95), (-.58, .14), (-.06, .14), (-.24, .95), (.58, -.2), (.06, -.2))
    inside, d = False, 9
    for i in range(6):
        a, b = q[i], q[i - 1]
        if (a[1] > y) != (b[1] > y) and x < (b[0] - a[0]) * (y - a[1]) / (b[1] - a[1]) + a[0]: inside = not inside
        d = min(d, seg(x, y, *a, *b))
    return inside or d < 0.05


def poly(x, y, q):
    inside, d = False, 9
    for i in range(len(q)):
        a, b = q[i], q[i - 1]
        if (a[1] > y) != (b[1] > y) and x < (b[0] - a[0]) * (y - a[1]) / (b[1] - a[1]) + a[0]: inside = not inside
        d = min(d, seg(x, y, *a, *b))
    return -d if inside else d


ZIG = ((0.02, -1.1), (0.02, -0.6), (-0.16, -0.38), (0.2, -0.1), (-0.16, 0.22), (0.14, 0.5), (0, 1.1))


def half(sd):  # a heart broken along a zigzag
    def f(x, y):
        if not heart(x, y): return 0
        zx = 0.02
        for (ax, ay), (bx, by) in zip(ZIG, ZIG[1:]):
            if ay <= y <= by: zx = ax + (bx - ax) * (y - ay) / (by - ay)
        return (x - zx) * sd > 0.02
    return f


def flower(x, y):
    if math.hypot(x, y) < 0.2: return 0
    return any(ell(x, y, 0.56 * math.cos(a), 0.56 * math.sin(a), 0.4, 0.34, a) < 1 for a in (math.radians(-90 + 72 * i) for i in range(5)))


def leaf(sd):
    def f(x, y):
        x *= sd  # from the lower left up to the right
        u, v = (x - y) / 1.414, (x + y) / 1.414
        if abs(u) > 0.98: return 0
        w = 0.42 * (1 - u * u) ** 0.8 * (1 - 0.25 * u)
        return abs(v) < w and (u > 0.55 or abs(v) > 0.035)
    return f


def ribbon(x, y):  # a medal's: two bands meeting below
    return min(seg(x, y, -0.6, -0.95, -0.08, 0.8), seg(x, y, 0.6, -0.95, 0.08, 0.8)) < 0.23


def disco(x, y):
    r = math.hypot(x, y)
    if r > 0.97: return 0
    lat = math.asin(max(-1, min(1, y / 0.97)))
    lon = math.asin(max(-1, min(1, x / max(0.05, math.sqrt(max(0.002, 0.95 - y * y))))))
    g = abs((lat / 0.42) % 1 - .5) < .42 and abs((lon / 0.45 + .5) % 1 - .5) < .41
    return 1 if g else 0.35


def bulb(x, y):
    return math.hypot(x, y + 0.28) < 0.68 or poly(x, y, ((-0.5, 0.1), (0.5, 0.1), (0.3, 0.85), (-0.3, 0.85))) < 0.05


def bulbbase(x, y):
    return any(seg(x, y, -0.6 + i * .1, -0.62 + i * 0.6, 0.6 - i * .1, -0.62 + i * 0.6) < 0.22 for i in range(3))


def paw(x, y):  # a soft mitt: a palm and four fat toes
    if ell(x, y, 0, 0.2, 0.84, 0.76) < 1: return 1
    return any(ell(x, y, a, b, 0.28, 0.4, r) < 1 for a, b, r in ((-0.66, -0.28, -0.4), (-0.23, -0.56, -0.1), (0.23, -0.56, 0.1), (0.66, -0.28, 0.4)))


def pads(x, y):
    if ell(x, y, 0, 0.4, 0.42, 0.34) < 1: return 1
    return any(ell(x, y, a, b, 0.15, 0.18, r) < 1 for a, b, r in ((-0.6, -0.36, -0.4), (-0.22, -0.64, 0), (0.22, -0.64, 0), (0.6, -0.36, 0.4)))


def crown(x, y):
    q = ((-1.12, 0.86), (-1.3, -0.35), (-0.62, 0.16), (0, -0.68), (0.62, 0.16), (1.3, -0.35), (1.12, 0.86))
    return poly(x, y, q) < 0.06 or math.hypot(x + 1.28, y + 0.45) < 0.14 or math.hypot(x, y + 0.78) < 0.17 or math.hypot(x - 1.28, y + 0.45) < 0.14


def halo(x, y):
    return ell(x, y, 0, 0, 2.65, 0.95) < 1 and ell(x, y, 0, -0.04, 2.1, 0.52) > 1


def horn(sd):  # curling up and out, a point
    def f(x, y):
        x *= sd
        v = (0.95 - y) / 1.85  # 0 the root .. 1 the tip
        if v < 0 or v > 1: return 0
        return abs(x - (-0.25 + 0.95 * v ** 2.2)) < 0.47 * (1 - v) ** 0.75 + 0.02
    return f


def balloon(x, y):
    Y = y + 0.14
    if Y < 0: ok = math.hypot(x / 0.78, Y / 0.82) < 1
    else: ok = abs(x) < 0.78 * math.sqrt(max(0, 1 - (Y / 0.94) ** 2.2))
    return ok or poly(x, y, ((0, 0.74), (0.13, 0.95), (-0.13, 0.95))) < 0.02


def cone(x, y):
    if poly(x, y, ((-0.66, -0.92), (0.66, -0.92), (0.04, 0.95), (-0.04, 0.95))) > 0.03: return 0
    a, b = (x * 1.5 + y * 0.9) / 0.42, (x * 1.5 - y * 0.9) / 0.42
    return 0.62 if abs(a % 1 - .5) > .41 or abs(b % 1 - .5) > .41 else 1


def umbrella(x, y):
    Y = y - 0.82
    if Y > 0: return 0
    if math.hypot(x / 2.0, Y / 1.62) > 1: return seg(x, y, 0, -0.95, 0, -0.75) < 0.05
    for i in range(4):  # scalloped below
        if ell(x, Y, -1.5 + 1.0 * i, 0.05, 0.46, 0.26) < 1: return 0
    return 0.72 if abs(abs(x) - 1.0 * math.sqrt(max(0, 1 - (Y / 1.62) ** 2))) < 0.03 or abs(x) < 0.025 else 1


def cloud(x, y):
    return any(math.hypot(x - a, y - b) < r for a, b, r in ((-0.95, 0.3, 0.55), (-0.35, -0.22, 0.7), (0.42, -0.1, 0.62), (1.0, 0.32, 0.52))) or (abs(x) < 1.0 and 0.1 < y < 0.845)


def drop(x, y):
    if y > 0.2: return math.hypot(x, y - 0.3) < 0.62
    v = (y + 0.95) / 1.15
    return 0 <= v <= 1 and abs(x) < 0.62 * v ** 1.15


def wool(x, y):
    return any(math.hypot(x - a, y - b) < r for a, b, r in ((-0.62, 0.02, 0.4), (-0.4, -0.42, 0.38), (0.06, -0.5, 0.4), (0.42, -0.28, 0.36), (0.5, 0.12, 0.36), (0.1, 0.22, 0.42), (-0.3, 0.26, 0.4), (0, -0.1, 0.5)))


def sheepdark(x, y):
    return (ell(x, y, 0.8, -0.2, 0.26, 0.36, 0.25) < 1 or ell(x, y, 0.6, -0.46, 0.2, 0.1, 0.5) < 1 or
            any(seg(x, y, a, 0.5, a, 0.88) < 0.085 for a in (-0.5, -0.2, 0.15, 0.42)))


def shur(a0):
    def f(x, y):
        c, s = math.cos(a0), math.sin(a0)
        X, Y = abs(x * c + y * s), abs(y * c - x * s)
        return X ** 0.62 + Y ** 0.62 < 0.97 and math.hypot(X, Y) > 0.13
    return f


def tophat(x, y):  # standing on its crown: the brim up
    return seg(x, y, -1.42, -0.72, 1.42, -0.72) < 0.19 or poly(x, y, ((-0.95, -0.7), (0.95, -0.7), (0.8, 0.9), (-0.8, 0.9))) < 0.06


def bunny(x, y):
    return ell(x, y, 0, 0, 0.34, 0.97) < 1


def weed(a0):  # dry stalks rolled into a ball, turned by a0
    import random
    rr = random.Random(7)
    arcs = [(rr.uniform(0, 6.28), rr.uniform(0.15, 0.6), rr.uniform(0.35, 0.75)) for _ in range(11)]

    def f(x, y):
        c, s = math.cos(a0), math.sin(a0)
        X, Y = x * c + y * s, y * c - x * s
        r = math.hypot(X, Y)
        if r > 0.97: return 0
        return abs(r - 0.88) < 0.05 or any(abs(math.hypot(X - d * math.cos(a), Y - d * math.sin(a)) - q) < 0.045 for a, d, q in arcs if r < 0.93)
    return f


def ghost(x, y):
    if ell(x, y, -0.26, -0.3, 0.12, 0.17) < 1 or ell(x, y, 0.26, -0.3, 0.12, 0.17) < 1 or ell(x, y, 0, 0.1, 0.13, 0.17) < 1: return 0
    if y < -0.18: return math.hypot(x / 0.76, (y + 0.18) / 0.78) < 1
    return abs(x) < 0.76 and y < 0.76 + 0.17 * math.cos(x * 12.4)


CHIPS = ((-0.35, -0.4), (0.3, -0.5), (0.05, 0.02), (-0.5, 0.25), (0.5, 0.2), (-0.05, 0.55), (0.35, 0.62))


def cookie(bite):
    def f(x, y):
        if math.hypot(x, y) > 0.95 + 0.02 * math.sin(math.atan2(y, x) * 9): return 0
        if bite and any(math.hypot(x - a, y - b) < r for a, b, r in ((0.95, -0.6, 0.42), (0.6, -0.95, 0.42), (1.0, -0.1, 0.3))): return 0
        return 0.35 if any(math.hypot(x - a, y - b) < 0.125 for a, b in CHIPS) else 1
    return f


def puff(x, y):
    return any(math.hypot(x - a, y - b) < r for a, b, r in ((-0.45, 0.2, 0.48), (0.05, -0.35, 0.52), (0.5, 0.15, 0.44), (0.05, 0.4, 0.45)))


def anger(x, y):  # the comic book mark: four bent strokes
    X, Y = abs(x), abs(y)
    return min(seg(X, Y, 0.22, 0.88, 0.3, 0.45), seg(X, Y, 0.3, 0.45, 0.45, 0.3), seg(X, Y, 0.45, 0.3, 0.88, 0.22)) < 0.13


def hook(x, y):  # an umbrella handle
    return (abs(math.hypot(x, y + 0.4) - 0.62) < 0.2 and y > -0.4) or math.hypot(x + 0.62, y + 0.4) < 0.2 or math.hypot(x - 0.62, y + 0.4) < 0.2


def fir(x, y):  # a little New Year tree: three tiers, each a soft triangle with a scalloped hem
    for top, bot, hw in ((-0.98, -0.28, 0.46), (-0.6, 0.3, 0.7), (-0.2, 0.92, 0.97)):
        if top <= y <= bot + 0.06:
            v = (y - top) / (bot - top)
            w = hw * min(1, v) ** 0.95
            hem = bot - 0.05 * math.cos(x * 9.5) ** 2  # the boughs' tips droop a little
            if abs(x) < w + 0.02 and y <= hem: return 1
    return 0


def wreath(x, y):  # a Christmas wreath: a ring of fir with ragged, bushy edges
    r, a = math.hypot(x, y), math.atan2(y, x)
    inner = 0.5 + 0.05 * math.sin(a * 11) + 0.02 * math.sin(a * 23 + 1)
    outer = 0.92 + 0.045 * math.sin(a * 13 + 1) + 0.02 * math.sin(a * 29)
    return inner < r < outer


def bow(x, y):  # a ribbon bow: two loops, the knot, two tails
    return (ell(x, y, -0.48, -0.22, 0.46, 0.3, 0.35) < 1 or ell(x, y, 0.48, -0.22, 0.46, 0.3, -0.35) < 1 or ell(x, y, 0, -0.15, 0.17, 0.2) < 1 or
            seg(x, y, -0.08, 0, -0.42, 0.88) < 0.13 or seg(x, y, 0.08, 0, 0.42, 0.88) < 0.13)


def flake(x, y):  # a snowflake: six arms, each with two little branches
    r, a = math.hypot(x, y), math.atan2(y, x)
    if r > 0.97: return 0
    k = (a + math.pi / 6) % (math.pi / 3) - math.pi / 6  # into one arm's sector
    X, Y = r * math.cos(k), r * math.sin(k)
    return (abs(Y) < 0.1 and X < 0.93) or r < 0.2 or min(seg(X, abs(Y), 0.5, 0, 0.76, 0.24), seg(X, abs(Y), 0.25, 0, 0.42, 0.17)) < 0.075


# name (the enum in sprite.h, in its order): the shape, the sizes used
ART = [("HEART", heart, (0, 1, 3, 4), (136, 124)), ("STAR", star, (0, 1, 2, 3, 4)), ("SPARK", spark, (0, 1, 2, 3)), ("NOTE", note, (2,)), ("NOTE2", note2, (2,)),
       ("ZED", zed, (0, 1, 2)), ("RING", ring, (0, 1, 2, 3)), ("BFLY0", bfly(1), (3,)), ("BFLY1", bfly(0.42), (3,)), ("EARL", ear(1), (2, 4)),
       ("EARR", ear(-1), (2, 4)), ("FLAME0", flame(-0.25), (1, 2, 3)), ("FLAME1", flame(0.05), (1, 2, 3)), ("FLAME2", flame(0.3), (1, 2, 3)),
       ("DOT", dot, (0, 1, 2, 3, 4)), ("BOLT", bolt, (1, 2, 3, 4)), ("HEART2", heart, (), (112, 102)),
       ("HALFL", half(-1), (4,)), ("HALFR", half(1), (4,)), ("FLOWER", flower, (1, 2, 3)), ("LEAFR", leaf(1), (2,)),
       ("LEAFL", leaf(-1), (2,)), ("RIBBON", ribbon, (3,)), ("DISCO", disco, (4,)), ("BULB", bulb, (), (72, 88)), ("SCREW", bulbbase, (), (38, 34)),
       ("PAW", paw, (), (134, 134)), ("PADS", pads, (), (104, 104)), ("CROWN", crown, (), (150, 104)), ("HALO", halo, (), (150, 54)),
       ("HORNL", horn(-1), (), (60, 74)), ("HORNR", horn(1), (), (60, 74)), ("BALLOON", balloon, (), (100, 122)), ("CONE", cone, (), (66, 92)),
       ("UMBRELLA", umbrella, (), (264, 130)), ("HOOK", hook, (2,)), ("CLOUD", cloud, (), (176, 100)), ("DROP", drop, (0, 1)), ("WOOL", wool, (), (92, 84)),
       ("SHEEP", sheepdark, (), (92, 84)), ("SHUR0", shur(0), (2, 3)), ("SHUR1", shur(.785), (3,)), ("TOPHAT", tophat, (), (150, 92)),
       ("BUNNY", bunny, (), (40, 104)), ("WEED0", weed(0), (), (76, 76)), ("WEED1", weed(-.8), (), (76, 76)), ("WEED2", weed(-1.6), (), (76, 76)),
       ("GHOST", ghost, (), (96, 108)), ("COOKIE", cookie(0), (3,)), ("COOKIE2", cookie(1), (3,)), ("PUFF", puff, (1, 2, 3)), ("ANGER", anger, (3,)),
       ("FIR", fir, (), (132, 136)), ("FLAKE", flake, (0, 1)), ("WREATH", wreath, (), (150, 150)), ("BOW", bow, (), (76, 58))]


def mask(f, w, h=0):
    h = h or w
    o = [((i + 0.5) / SS) for i in range(SS)]
    return [round(sum(float(f(((x + a) / w * 2 - 1) * (w / h), (y + b) / h * 2 - 1)) for a in o for b in o) / SS / SS * 255) for y in range(h) for x in range(w)]


def main():
    out = ["/* Made by tools/sprites.py: A8 coverage of the little pictures round Pip's eyes (sprite.c). Do not edit. */",
           '#include "sprite.h"', ""]
    table, own, total = [], [], 0
    for name, f, sizes, *big in ART:
        row = []
        for z in range(len(PX)):
            if z not in sizes:
                row.append("NULL")
                continue
            m = mask(f, PX[z])
            total += len(m)
            v = f"{name.lower()}{z}"
            out.append(f"static const uint8_t {v}[{len(m)}] = {{")
            out += ["    " + ",".join(map(str, m[i:i + 46])) + "," for i in range(0, len(m), 46)]
            out.append("};")
            row.append(v)
        if big:
            w, h = big[0]
            m = mask(f, w, h)
            total += len(m)
            v = f"{name.lower()}_own"
            out.append(f"static const uint8_t {v}[{len(m)}] = {{")
            out += ["    " + ",".join(map(str, m[i:i + 46])) + "," for i in range(0, len(m), 46)]
            out.append("};")
            row.append(v)
            own.append(f"    [SP_{name}] = {{{w}, {h}}},")
        else:
            row.append("NULL")
        table.append(f"    [SP_{name}] = {{{', '.join(row)}}},")
    out += ["", "const uint8_t *const sprite_art[SP_KINDS][SPRITE_SIZES] = {", *table, "};", "", "const uint16_t sprite_own[SP_KINDS][2] = {", *own, "};", ""]
    with open(os.path.join(ROOT, "ui", "sprite_art.c"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out))
    print(f"ui/sprite_art.c: {len(ART)} pictures, {total} bytes")


if __name__ == "__main__":
    main()
