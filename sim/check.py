"""Smoke check of the headless simulator: Pip follows the finger and the tilt, stays inside the round screen,
and every reaction fires from its real input path (touch, accelerometer, buttons).
Usage: py sim/check.py [path/to/pipsim]"""
import math, os, re, subprocess, sys, tempfile
from PIL import Image, ImageSequence
def pixels(im): return (im.get_flattened_data if hasattr(im, "get_flattened_data") else im.getdata)()  # getdata is deprecated in Pillow 12

sim = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "build/pipsim" + (".exe" if os.name == "nt" else ""))  # Windows wants it whole
tmp = tempfile.mkdtemp()

def run(events, secs=1.5, name="x"):
    # the classic answers unless a check asks for the living ones: pet_tune.vary picks among answers, life_gap is his own
    # moments; a check of those sets them itself (the knobs at 0.0 s, before anything happens)
    if "Kvary" not in events: events = "0:Kvary=0 " + events
    if "Klife_gap" not in events: events = "0:Klife_gap=0 " + events
    if "Kcalm" not in events: events = "0:Kcalm=0 " + events
    if "Ktalk" not in events: events = "0:Ktalk=0 " + events  # his tales after events: their own checks turn them on
    if "Keveryday" not in events: events = "0:Keveryday=0 " + events  # the classic palettes: the everyday faces have their own checks
    if "Kzones" not in events: events = "0:Kzones=0 " + events  # cheeks, chin, sides, an eye held: their own checks
    path = os.path.join(tmp, name + ".png")
    out = subprocess.run([sim, "--out", path, "--secs", str(secs), "--events", events],
                         check=True, capture_output=True, text=True, encoding="utf-8", errors="replace",
                         timeout=180).stdout  # a hang fails the run instead of stalling the suite
    status = dict(re.findall(r"(\w+)=(\S*)", out.splitlines()[-1]))
    # brightness = the strongest channel, so tinted eyes (red rage, green envy) still count as eyes
    rgb = Image.open(path).convert("RGB")
    r, g, b = rgb.split()
    from PIL import ImageChops
    return ImageChops.lighter(ImageChops.lighter(r, g), b), status

def pupils(img):
    """centroid of the dark-grey pupil pixels (#1C1C1C), which is where Pip is looking"""
    g = lambda x, y: 15 < img.getpixel((x, y)) < 45
    # solid patches only: the antialiased rims of the white eyes pass through the same greys for a pixel or two
    inside = lambda x, y: (x - 233) ** 2 + (y - 233) ** 2 < 225 ** 2  # the bezel around the glass is grey too
    pts = [(x, y) for y in range(3, 463, 2) for x in range(3, 463, 2) if inside(x, y) and all(g(x + dx, y + dy) for dx, dy in
           ((0, 0), (3, 0), (-3, 0), (0, 3), (0, -3)))]
    return sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts)

def eyes(img):
    """centroid x/y of bright pixels and the farthest one from the screen center"""
    pts = [(x, y) for y in range(0, 466, 2) for x in range(0, 466, 2) if img.getpixel((x, y)) > 128]
    if not pts: return 0, 0, 0
    far = max(((x + 0.5 - 233) ** 2 + (y + 0.5 - 233) ** 2) ** 0.5 for x, y in pts)
    return sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts), far

def fedups(events, secs):
    """the faces he showed each time he was fed up, in order (from --trace)"""
    out = subprocess.run([sim, "--trace", "--out", os.path.join(tmp, "f.png"), "--secs", str(secs), "--events",
                          "0:Kvary=0 0:Klife_gap=0 0:Kcalm=0 0:Ktalk=0 0:Keveryday=0 0:Kzones=0 " + events], check=True, capture_output=True, text=True,
                         encoding="utf-8", errors="replace", timeout=300).stdout.splitlines()
    seen = []
    for l in out:
        p = l.split(None, 4)
        if l.startswith("trace") and len(p) > 3 and p[3] == "fed":  # the trace prints the old face, then the new, at one moment
            if seen and seen[-1][0] == p[1]: seen[-1] = (p[1], p[2])
            elif not seen or seen[-1][1] != p[2] or True: seen.append((p[1], p[2]))
    return [m for _, m in seen]

def check(name, cond, info):
    print(("ok  " if cond else "FAIL") + "  " + name + ("" if cond else f"  {info}"))
    return cond

ok = True
r = subprocess.run([os.path.join(os.path.dirname(sim), "face_test")], capture_output=True, text=True, encoding="utf-8", errors="replace")
ok &= check("renderer: no clipped or shaved eyes (face_test)", r.returncode == 0, r.stderr.strip())
imgs = {n: run(e, name=n)[0] for n, e in [("left", "0:k5 0.1:d20,233"), ("right", "0:k5 0.1:d446,233"), ("up", "0:k5 0.1:d233,20")]}
left, right, up = (eyes(imgs[n]) for n in ("left", "right", "up"))
pl, pr, pu = (pupils(imgs[n]) for n in ("left", "right", "up"))
ok &= check("looks at the finger", pl[0] < 213 < 253 < pr[0] and pu[1] < 213, (pl, pr, pu))
ok &= check("stays inside the screen", all(e[2] < 231 for e in (left, right, up)), (left, right, up))

# tilt the right side down: accelerometer x goes negative, eyes slide right
still, _ = run("0:a0,1,0", 1.5, "still")
tilted, _ = run("0:a0,1,0 0.5:a-0.5,0.87,0", 1.5, "tilted")
ok &= check("tilt moves the gaze downhill", pupils(tilted)[0] > pupils(still)[0] + 15, (pupils(still), pupils(tilted)))
eco_still, _ = run("0:E1 0:a0,1,0", 1.5, "eco_still")
eco_tilted, _ = run("0:E1 0:a0,1,0 0.5:a-0.5,0.87,0", 1.5, "eco_tilted")
ok &= check("energy saving: the eyes ignore the tilt", abs(pupils(eco_tilted)[0] - pupils(eco_still)[0]) < 5, (pupils(eco_still), pupils(eco_tilted)))
_, st = run("0:E1 0.1:V35 0.5:Q", 1.5)
ok &= check("energy saving: no listening, blowing does nothing", st["reaction"] != "blown" and st["mic"] == "0" and st["eco"] == "1", st)
_, st = run("0.1:V35", 0.5)
ok &= check("without it the mic is wanted", st["mic"] == "1" and st["eco"] == "0", st)

_, st = run("0.3:S 1.3:S", 1.6)
ok &= check("shake -> dizzy", st["mood"] == "dizzy", st)
_, st = run("0.1:oeight 0.5:S 1.8:S", 3.0)
ok &= check("off his face he is switched off: the 8 shaken, no reaction from him", st["reaction"] == "" and st["mood"] != "dizzy"
            and st["screen"] == "eight", st)
tilts = " ".join(f"{i * 5}:a{0.3 * (i % 2)},0.95,0.1" for i in range(1, 15))
_, st = run("0.1:oball " + tilts, 72)
ok &= check("a game played by tilting alone holds the screen lit", st["display"] == "1" and st["screen"] == "ball", st)
_, st = run("0.1:oball", 160)
ok &= check("a game left lying still: the sleep timer runs again, home and dark", st["display"] == "0" and st["screen"] == "pip", st)
_, st = run("0.2:a0,0,-1", 3.0)
ok &= check("face down -> asleep", st["mood"] == "asleep" and st["face_down"] == "1", st)
_, st = run("0.2:a0,0,-1 2.6:a0,1,0", 3.0)
ok &= check("turned back up -> surprised", st["mood"] == "surprised", st)
img, st = run("0.2:a0,0,-1", 4.0)
ok &= check("face down a second asleep -> the screen goes dark", st["display"] == "0" and eyes(img)[2] == 0, st)
img, st = run("0.2:a0,0,-1 3.6:a0,1,0", 4.0)
ok &= check("turned over -> the screen is back", st["display"] == "1" and eyes(img)[2] > 0, st)
_, st = run("0.2:a0,0,-1 3.6:P", 4.0)
ok &= check("a button wakes the dark screen and does nothing else", st["display"] == "1" and st["screen"] == "pip", st)
_, st = run("0.2:a0,0,-1 3.6:P", 9.5)
ok &= check("still face down: dark again 5 s later", st["display"] == "0", st)
_, st = run("0.2:W", 5.2)
ok &= check("walking counts steps (1.8/s for 5s)", 7 <= int(st["steps"]) <= 10, st)
_, st = run("0.1:V35 0.5:Q", 1.5)
ok &= check("blowing on the mic -> he screws his eyes up", st["reaction"] == "blown", st)
_, st = run("0.1:V85", 1.5)
ok &= check("a loud room is not breath", st["reaction"] != "blown", st)
_, st = run("0.1:J7", 1.0)
ok &= check("battery under 10% -> tired", st["mood"] == "tired", st)
_, st = run("0.1:J50 0.4:J50+", 1.0)
ok &= check("plugged in -> happy (dinner)", st["reaction"] == "charging", st)
_, st = run("0.2:W0.6", 5.2)
ok &= check("a hard walk, keys swinging, still counts (and is not a shake)", 7 <= int(st["steps"]) <= 10 and st["mood"] != "dizzy", st)
_, st = run("0.01:Ksneeze_poke=0 " + " ".join(f"{0.2 + i * 0.25:.2f}:d233,400 {0.25 + i * 0.25:.2f}:u" for i in range(3)), 1.0)
ok &= check("three pokes: only ouch, not cross yet", st["reaction"] == "poke" and st["mood"] == "ouch", st)
quick = lambda n: "0.01:Ksneeze_poke=0 " + " ".join(f"{0.2 + i * 0.25:.2f}:d233,400 {0.25 + i * 0.25:.2f}:u" for i in range(n))
f = fedups("0.01:Y2 " + quick(10), 3.0)
ok &= check("Требовательный: ten quick pokes -> fed up, at first just annoyed", f[:1] == ["annoyed"], f)
f = fedups("0.01:Y2 " + quick(6), 2.0)
ok &= check("...six are not enough (he is kind: it was five)", not f, f)
f = fedups("0.01:Y0 " + quick(13), 3.6)
ok &= check("Неприхотливый: thirteen quick pokes, still not fed up", not f, f)
f = fedups("0.01:Y1 " + quick(40), 10.5)
LADDER = ["annoyed", "angry", "furious", "rage", "swearing", "berserk"]
steps = lambda f: all(LADDER.index(b) <= LADDER.index(a) + 1 for a, b in zip(f, f[1:]))  # never two rungs at once
ok &= check("Уравновешенный: forty quick pokes - put out first, then up a rung at a time, no further than furious",
            f[:1] == ["annoyed"] and steps(f) and set(f) <= {"annoyed", "angry", "furious"} and len(f) >= 2, f)
f = fedups("0.01:Y0 " + quick(40), 10.5)
ok &= check("Неприхотливый: forty quick pokes - only put out", set(f) == {"annoyed"}, f)
_, st = run("0.2:d233,400 0.25:u", 0.4)
ok &= check("one poke -> ouch", st["mood"] == "ouch", st)
# the screen going dark on its own is a nap (not for Требовательный, who sleeps face down only); lit again, calmly awake
_, st = run("0.05:Y1", 33)
ok &= check("dark screen, Уравновешенный: a nap", st["display"] == "0" and st["asleep"] == "1", st)
_, st = run("0.05:Y2", 33)
ok &= check("dark screen, Требовательный: no nap", st["display"] == "0" and st["asleep"] == "0", st)
_, st = run("0.05:Y1 33:p1", 34)
ok &= check("lit again after a nap: awake, no grudge", st["display"] == "1" and st["asleep"] == "0" and st["reaction"] != "woken", st)
# the first meeting (a clean flash): dormant, deaf to touch and shakes, the first button wakes him, no menu
_, st = run("0.05:O 0.3:d233,400 0.35:u 0.5:S 1.0:S", 1.5)
ok &= check("first meeting: dormant, touch and shakes do nothing", st["met"] == "0" and st["mood"] == "asleep" and st["reaction"] == "", st)
img, st = run("0.05:O", 1.0)
ok &= check("first meeting: the screen is black, no eyes", eyes(img)[2] == 0, eyes(img))
_, st = run("0.05:O 0.3:P", 1.0)
ok &= check("first meeting: a button wakes him (not the menu)", st["screen"] == "pip" and st["met"] == "0", st)
_, st = run("0.05:O 1.0:T48 1.3:B", 35.0)
ok &= check("first meeting: after the show he loves you, day 1 starts then", st["met"] == "1" and st["reaction"] == "hello"
            and st["screen"] == "pip", st)
# his birth (cosmos.c): stars, the spark, the dark, the bang's rings; then his eyes
def lit_px(img):  # bright pixels on a 2 px grid (the eyes are thousands of them, the spark and the dust a few dozen)
    return [(x, y) for y in range(0, 466, 2) for x in range(0, 466, 2) if img.getpixel((x, y)) > 120]
in_mid = lambda l, r: [p for p in l if (p[0] - 233) ** 2 + (p[1] - 233) ** 2 < r * r]
lit = lit_px(run("0.05:O 0.3:B", 0.3 + 3.3)[0])
ok &= check("birth: stars all over the sky, nothing else", 20 < len(lit) < 500 and len(in_mid(lit, 200)) < len(lit) and len(in_mid(lit, 12)) < 8, len(lit))
lit = lit_px(run("0.05:O 0.3:B", 0.3 + 5.0)[0])
ok &= check("...a star has fallen into the middle: the spark", len(in_mid(lit, 14)) > 10, len(in_mid(lit, 14)))
lit = lit_px(run("0.05:O 0.3:B", 0.3 + 13.6)[0])
ok &= check("...then dark, before the bang", len(lit) < 12, len(lit))
lit = lit_px(run("0.05:O 0.3:B", 0.3 + 14.45)[0])
ok &= check("...the bang: rings of light flying out", len(lit) - len(in_mid(lit, 150)) > 100, len(lit) - len(in_mid(lit, 150)))
lit = lit_px(run("0.05:O 0.3:B", 0.3 + 16.4)[0])
ok &= check("...two slits of light: his eyes, shut", 60 < len(lit) < 900, len(lit))
lit = lit_px(run("0.05:O 0.3:B", 0.3 + 20.0)[0])
ok &= check("...open: the world", len(lit) > 4000, len(lit))
_, st = run("0.05:O 0.3:B 5:d233,233 5.1:u 6:B 7:P 8:S 9:S", 0.3 + 25)
ok &= check("birth: not over yet at 25 s (the show runs ~32 s; met=0 while it plays, a touch, a key or a shake does nothing)", st["met"] == "0" and st["reaction"] != "hello"
            and st["screen"] == "pip", st)
stroke = " ".join(f"{0.2 + i * 0.05:.2f}:m{150 if i % 8 < 4 else 320},233" for i in range(70))
slow = " ".join(f"{0.2 + i * 0.05:.2f}:m{150 + i * 4},233" for i in range(30))  # 80 px/s
_, st = run("0.15:d150,233 " + slow, 1.7)
ok &= check("a slow drag is followed, not petted", st["reaction"] != "pet" and st["mood"] != "love", st)
img, st = run("0.3:d140,233 0.35:u", 0.6)
ok &= check("a tap on an eye: no wince of the whole face", st["reaction"] != "poke", st)
_, st = run("0.05:nangry 0.6:d140,233 0.65:u", 0.9)
ok &= check("scowling, a tap on an eye is a plain poke (no squint)", st["reaction"] == "poke", st)
_, st = run("0.15:d150,233 " + stroke, 1.5)
ok &= check("stroke -> pet (happy)", st["mood"] == "happy", st)
_, st = run("0.15:d150,233 " + stroke, 3.8)
ok &= check("long stroke -> love", st["mood"] == "love", st)
_, st = run("0.2:p1", 1.0)
ok &= check("bottom button (BOOT) -> meet (curious)", st["mood"] == "curious", st)
_, st = run("0.2:p1", 2.0)
ok &= check("meet ends in like or dislike", st["reaction"] in ("likes", "dislikes", "shy", "wary", "not"), st)
# the pet behind the face
r = subprocess.run([os.path.join(os.path.dirname(sim), "pet_test")], capture_output=True, text=True, encoding="utf-8", errors="replace")
ok &= check("pet logic over simulated days (pet_test)", r.returncode == 0, r.stderr.strip())
_, st = run("0.2:T8 0.5:F", 1.6)  # the biscuit takes ~0.55 s to reach him
ok &= check("feeding a hungry pet -> yum", st["reaction"] == "yum", st)
_, st = run("0.2:T8 0.4:B1", 2.2)
ok &= check("holding the bottom key feeds him (with the biscuit)", st["reaction"] == "yum", st)
_, st = run("0.3:B", 0.8)
ok &= check("a tap of the bottom key is still meeting", st["reaction"] == "meet", st)
# a quick series of bottom taps builds up, each answered at once
taps = lambda n, t0=0.5, g=0.2: " ".join(f"{t0 + g * i:.2f}:B" for i in range(n))
_, st = run(taps(3), 1.0)
ok &= check("bottom taps: three quick ones tickle him (he laughs)", st["reaction"] == "tickled" and st["mood"] == "laughing", st)
_, st = run("0.01:Y2 " + taps(4), 1.2)
ok &= check("bottom taps, Требовательный: four still tickle (it was annoyed at four)", st["reaction"] == "tickled", st)
_, st = run("0.01:Y2 " + taps(5), 1.4)
ok &= check("bottom taps, Требовательный: five -> that's enough (annoyed)", st["reaction"] == "stop" and st["mood"] == "annoyed", st)
_, st = run("0.01:Y2 " + taps(8), 2.0)
ok &= check("bottom taps, Требовательный: eight -> angry, and he stays hurt (why 3.9)", st["reaction"] == "pestered" and st["mood"] == "angry"
            and st["base"] == "hurt" and st["why"] == "3.9", st)
_, st = run(taps(9), 2.2)
ok &= check("bottom taps, Уравновешенный: nine -> only 'that's enough', never pestered", st["reaction"] == "stop", st)
_, st = run("0.01:Y0 " + taps(8), 2.0)
ok &= check("bottom taps, Неприхотливый: eight -> still a game to him (tickled)", st["reaction"] == "tickled", st)
_, st = run("0.01:Y2 " + taps(8) + " 2.5:d150,233 " + stroke, 4.5)
ok &= check("bottom taps: pestered, a short stroke does not make it up yet (still cross)", st["why"] == "3.9"
            and st["reaction"] == "still" and float(st["grudge"]) > 0.2, st)
long_stroke = " ".join(f"{2.55 + i * 0.05:.2f}:m{150 if i % 8 < 4 else 320},233" for i in range(500))
_, st = run(taps(7) + " 2.5:d150,233 " + long_stroke, 28)
ok &= check("bottom taps: pestered, stroked on and on -> made up", st["why"] != "3.9", st)
# the grudge: anger that outlasts its face (pet.c grudge_add / soothe, pet_t.grudge; pipsim e<v>)
_, st = run("0.05:e0.8", 0.5)
ok &= check("grudge: seething, it shows as his mood (why 3.19)", st["why"] == "3.19" and st["base"] in ("annoyed", "indignant"), st)
_, st = run("0.05:e0.45", 0.5)
ok &= check("grudge: a milder one is sore and wary", st["why"] == "3.19" and st["base"] in ("hurt", "suspicious"), st)
_, st = run("0.05:e0.8 0.2:opong", 0.6)
ok &= check("grudge: seething, ping-pong with him -> the 'Пип обиделся' note", st["screen"] == "tired", st)
_, st = run("0.1:T8 0.3:e0.9 0.5:F", 1.6)
ok &= check("grudge: seething, a biscuit is turned down", st["reaction"] == "sulky:" and float(st["food"]) < 0.6, st)  # eaten it would be over 0.9
_, st = run("0.04:c0.1 0.05:e0.9 0.3:B 0.5:B2", 1.4)
ok &= check("grudge: seething, the sponge is turned down", st["reaction"] in ("sulky:", "no") and float(st["clean"]) < 0.15, st)
_, st = run("0.3:e0.3 0.5:F", 1.6)
ok &= check("grudge: still sore, a biscuit is eaten with no thanks", st["reaction"] == "sulky" and st["mood"] in ("skeptical", "pouty"), st)
_, st = run("0.05:e0.5 0.15:d150,233 " + stroke, 3.0)
ok &= check("grudge: stroked, he only puts up with it, and it eases a little", st["reaction"] == "still"
            and 0.45 < float(st["grudge"]) < 0.5, st)
coax = " ".join(f"{0.2 + i * 0.05:.2f}:m{150 if i % 8 < 4 else 320},233" for i in range(1000))
_, st = run("0.05:e0.9 0.15:d150,233 " + coax, 8)
ok &= check("grudge: seething, stroked a while -> he shows it is getting through (softening)", st["reaction"] == "softening"
            and st["mood"] in ("pouty", "shy"), st)
_, st = run("0.05:e0.5 0.15:d150,233 " + coax, 20)
ok &= check("grudge: sulky, stroked on and on -> made up (pet_coax)", st["reaction"] in ("made", "touched") and float(st["grudge"]) < 0.2, st)
_, st = run("0.05:e0.9 0.15:d150,233 " + coax, 50)
ok &= check("grudge: even seething, a minute of stroking wins him round", float(st["grudge"]) < 0.12 and st["why"] != "3.19", st)
_, st = run("0.05:e0.5 0.1:T2", 0.5)
ok &= check("grudge: time heals it", float(st["grudge"]) == 0 and st["why"] != "3.19", st)
_, st = run(taps(10), 2.6)
ok &= check("bottom taps: ten -> his head spins", st["reaction"] == "head" and st["mood"] == "dizzy", st)
_, st = run(taps(10), 6.0)
ok &= check("bottom taps: the spin wears off into a phew", st["reaction"] == "phew", st)
_, st = run("0.2:T13 " + taps(2, 0.6), 1.2)
ok &= check("bottom taps: asleep, two only stir him", st["asleep"] == "1" and st["reaction"] == "", st)
_, st = run("0.2:T13 " + taps(3, 0.6), 1.4)
ok &= check("bottom taps: asleep, three wake him, grumpy", st["asleep"] == "0" and st["reaction"] == "tapped" and st["mood"] == "annoyed", st)
def blinks_after(n):  # the series is blinked back: dips of the eyes' white while he counts (0.4 s a blink)
    g = os.path.join(tmp, f"count{n}.gif")
    subprocess.run([sim, "--out", g, "--secs", "4.6", "--fps", "50", "--events", taps(n)], check=True, capture_output=True)
    lit = [sum(1 for p in f.convert("L").resize((117, 117)).get_flattened_data() if p > 128) for f in ImageSequence.Iterator(Image.open(g))]
    seq = lit[int((0.5 + 0.2 * (n - 1) + 0.5) * 50):][:int((0.4 * n + 0.7) * 50)]
    dips, low = 0, False
    for i in range(1, len(seq)):
        ref = max(seq[max(0, i - 12):i])  # against the open eye just before: the face may change meanwhile
        if not low and seq[i] < ref * 0.2: dips, low = dips + 1, True  # a blink all but shuts them; a hop on a new face does not
        elif low and seq[i] > ref * 0.6: low = False
    return dips
for n in (2, 3, 5):
    d = blinks_after(n)
    ok &= check(f"bottom taps: {n} in a row, then he blinks {n} times (counts them)", d == n, d)
# a slow even beat: he bobs along, drums a pattern back, you repeat it (one of the nine answers is right)
beat4 = taps(4, 0.5, 0.7)
_, st = run(beat4, 3.0)
ok &= check("rhythm: four even slow taps -> he grooves", st["reaction"] == "groove", st)
_, st = run(beat4, 4.6)
ok &= check("rhythm: you stop, he drums", st["reaction"] == "drums", st)
_, st = run(beat4, 7.8)
ok &= check("rhythm: then it is your turn", st["reaction"] == "your", st)
_, st = run(beat4, 11.5)  # your turn from 5.7..7.1 s (his pattern is random), bored 4 s later for 2 s
ok &= check("rhythm: no answer -> bored", st["reaction"] == "no" and st["mood"] == "bored", st)
answers = [run(f"{beat4} 8.0:B {8.0 + a:.2f}:B {8.0 + a + b:.2f}:B", 8.0 + a + b + 0.3)[1]["reaction"]
           for a in (0.35, 0.7, 1.05) for b in (0.35, 0.7, 1.05)]
ok &= check("rhythm: the right repeat cheers him, the wrong ones he is smug about", answers.count("right") == 1 and answers.count("wrong") == 8, answers)
# the back gesture: from the very right edge drawn left
edge = lambda t: f"{t:.2f}:d455,233 {t + 0.05:.2f}:m430,234 {t + 0.1:.2f}:m390,236 {t + 0.15:.2f}:m350,238 {t + 0.25:.2f}:u"
_, st = run("0.2:oclock " + edge(0.8), 1.8)
ok &= check("back gesture in an app -> its group in the menu", st["screen"] == "menu", st)
_, st = run("0.2:P " + edge(0.8), 1.8)
ok &= check("back gesture in the menu -> Pip", st["screen"] == "pip", st)
_, st = run("0.2:ocare " + edge(0.8) + " " + edge(1.5), 2.4)
ok &= check("back gesture twice: an app on the top list -> menu -> Pip", st["screen"] == "pip", st)
_, st = run(edge(0.5), 1.4)
ok &= check("back gesture on his face: nothing (no menu)", st["screen"] == "pip", st)
_, st = run("0.2:oclock 0.8:d300,233 0.85:m260,234 0.9:m220,236 0.95:m180,238 1.05:u", 1.8)
ok &= check("a swipe left from the middle is no back gesture", st["screen"] == "clock", st)
_, st = run("0.2:oclock 0.8:d455,233 0.95:u", 1.6)
ok &= check("a tap at the edge is no back gesture", st["screen"] == "clock", st)
_, st = run("0.2:ostopwatch 0.5:P " + edge(1.0), 2.0)
ok &= check("back gesture in the stopwatch: leaves (does not just reset it)", st["screen"] == "menu", st)
# snake (status snake=len,state: 0 ready, 1 playing, 2 paused, 3 over)
_, st = run("0.2:osnake", 1.0)
ok &= check("snake: opens ready, three long", st["screen"] == "snake" and st["snake"] == "3,0", st)
_, st = run("0.2:osnake 0.8:d233,233 0.85:u", 3.5)
ok &= check("snake: a tap starts it, straight on it hits the wall", st["snake"] == "3,3", st)
_, st = run("0.2:osnake 0.8:d233,233 0.85:u 1.2:d233,300 1.25:m233,270 1.3:m233,240 1.35:u 1.6:d100,233 1.65:u", 2.5)
ok &= check("snake: a swipe up, then a tap on the left: turned, still going", st["snake"] == "3,1", st)
_, st = run("0.2:osnake 0.8:d233,233 0.85:u 1.0:d300,233 1.05:m260,233 1.1:m220,233 1.15:u", 1.6)
ok &= check("snake: a swipe straight back is ignored (no biting itself)", st["snake"] == "3,1", st)
eats = [run(f"0.2:osnake 0.8:d233,233 0.85:u {0.85 + t:.2f}:d233,100 {0.9 + t:.2f}:u", 3.6)[1]["snake"] for t in (0.35, 0.45, 0.55, 0.65, 0.75)]
ok &= check("snake: up into the food: it grows by one", any(s.startswith("4,") for s in eats), eats)
_, st = run("0.2:osnake 0.8:d233,233 0.85:u 1.0:P", 3.0)
ok &= check("snake: the top key pauses it", st["snake"] == "3,2", st)
_, st = run("0.2:osnake 0.8:d233,233 0.85:u 1.0:P 1.5:P", 1.8)
ok &= check("...and goes on", st["snake"] == "3,1", st)
_, st = run("0.2:osnake 0.8:d233,233 0.85:u " + edge(1.0), 2.0)
ok &= check("snake: the back gesture leaves it", st["screen"] == "menu" and st["snake"] == "-", st)
_, st = run("0.2:osnake 0.8:B", 1.4)
ok &= check("snake: the bottom key leaves it", st["screen"] == "menu", st)
# the locator: the phone link's signal, read only while it is open
_, st = run("0.05:H0 0.3:olocate", 1.5)
ok &= check("locator: no phone -> no link, nothing read", st["screen"] == "locate" and st["locate"] == "-", st)
_, st = run("0.05:H0 0.1:L2 0.15:i-52 0.3:olocate", 1.5)
ok &= check("locator: a strong signal -> 'here' (the top band)", st["locate"].startswith("4,"), st)
_, st = run("0.05:H0 0.1:L2 0.15:i-88 0.3:olocate", 1.5)
ok &= check("locator: a weak one -> 'far' (the bottom band)", st["locate"].startswith("0,"), st)
_, quiet = run("0.05:H0 0.1:L2 0.15:i-88 0.3:olocate", 4.5)
_, st = run("0.05:H0 0.1:L2 0.15:i-88 0.3:olocate 1.0:i-55", 4.5)
ok &= check("locator: walking toward it, the band climbs (smoothed) and each step up dings",
            int(st["locate"].split(",")[0]) >= 3 and int(st["voices"]) > int(quiet["voices"]) + 2, (quiet["voices"], st))
_, st = run("0.05:H0 0.1:l2 0.15:i-70 0.3:olocate", 1.5)
ok &= check("locator: an Android phone works the same", st["locate"].startswith("2,"), st)
_, st = run("0.05:H0 0.1:L2 0.15:i-56 0.3:olocate", 1.5)
ok &= check("locator: the phone against it (~-55 dBm on this keychain) -> 'here'", st["locate"].startswith("4,"), st)
_, st = run("0.05:H0 0.1:L2 0.15:i-70 0.3:olocate 1.0:L0", 2.0)
ok &= check("locator: the link lost while looking -> no reading ('lost' shown)", st["screen"] == "locate" and st["locate"] == "-", st)
_, st = run("0.05:H0 0.1:L2 0.15:i-70 0.3:olocate 1.0:L0 1.6:L2", 2.6)
ok &= check("...linked again: it goes on looking by itself", st["locate"].startswith("2,"), st)
_, st = run("0.05:H0 0.1:L2 0.3:olocate 0.8:B", 1.5)
ok &= check("locator: the bottom key leaves it", st["screen"] == "menu", st)
# Pip's flight: his eyes through the gaps
_, st = run("0.05:H0 0.3:oflight", 3.0)
ok &= check("flight: opens ready, hovering, waits for a tap", st["screen"] == "flight" and st["flight"] == "0,0", st)
_, st = run("0.05:H0 0.3:oflight 0.8:d233,233 0.85:u", 3.5)
ok &= check("flight: one flap and no more: he falls out of the sky (over)", st["flight"] == "0,2", st)
_, st = run("0.05:H0 0.3:oflight 0.8:d233,233 0.85:u 2.0:d233,233 2.05:u", 2.1)
ok &= check("...a tap right after the crash does not start it again", st["flight"] == "0,2", st)
_, st = run("0.05:H0 0.3:oflight 0.8:d233,233 0.85:u 3.0:d233,233 3.05:u", 3.2)
ok &= check("...a moment later a tap makes it ready again", st["flight"] == "0,0", st)
_, st = run("0.05:H0 0.3:oflight 0.8:P", 1.0)
ok &= check("flight: the top key flaps as well", st["flight"] == "0,1", st)
_, st = run("0.05:H0 0.3:oflight " + " ".join(f"{0.95 + i * 0.58:.2f}:d233,233 {1.0 + i * 0.58:.2f}:u" for i in range(8)), 4.3)
ok &= check("flight: flying through a gap scores a point", st["flight"] == "1,1", st)
ok &= check("...every flap is a 'fwip', the point a 'ding-ding' (6 flaps, 1 point; the flap 0.2 s after it silent: 6 sounds)", st["voices"] == "6", st)
_, st = run("0.05:H0 0.3:oflight 0.8:d233,233 0.85:u", 1.2)
ok &= check("flight: one flap, one sound", st["voices"] == "1", st)
pt = " ".join(f"{0.95 + i * 0.58:.2f}:d233,233 {1.0 + i * 0.58:.2f}:u" for i in range(5))  # the point comes at ~3.64 s
_, base = run("0.05:H0 0.3:oflight " + pt, 3.9)
_, st = run("0.05:H0 0.3:oflight " + pt + " 3.72:d233,233 3.77:u", 3.9)
ok &= check("flight: a flap right on a point's 'ding-ding' does not cut it off (no sound of its own)",
            base["flight"].startswith("1,") and int(st["voices"]) == int(base["voices"]), (base["voices"], st["voices"], base["flight"]))
_, st = run("0.05:H0 0.3:oflight " + pt + " 3.95:d233,233 3.99:u", 4.1)
ok &= check("...once it has rung, a flap is heard again", int(st["voices"]) == int(base["voices"]) + 1, (base["voices"], st["voices"]))
_, st = run("0.05:H0 0.3:oflight 0.8:B", 1.6)
ok &= check("flight: the bottom key leaves it", st["screen"] == "menu", st)
# attention: busy with the keychain off his face (pet_attention), by its kind
taps = " ".join(f"{i * 10}.0:d233,240 {i * 10}.05:u" for i in range(1, 60))
love = lambda ev: float(run("0:s0 0:v0.3 " + ev, 600)[1]["love"])
idle, app, game, left = love(""), love("0.2:oclock " + taps), love("0.2:ocoin " + taps), love("0.2:oclock")
ok &= check("attention: an app in use slows his loneliness, a game more, an app left lying open hardly", game > app > idle and left < app, (idle, app, game, left))
# "Повтори за мной" (status repeat=len,state: 0 ready, 1 showing, 2 your turn, 3 right, 4 over)
_, st = run("0.2:orepeat", 1.0)
ok &= check("repeat: opens ready", st["screen"] == "repeat" and st["repeat"] == "0,0", st)
_, st = run("0.2:orepeat 0.6:d233,233 0.65:u", 1.2)
ok &= check("repeat: a tap starts it, he shows the first one", st["repeat"] == "1,1", st)
taps = {0: "233,90", 1: "380,233", 2: "233,380", 3: "90,233"}  # a tap on that side
after = [run(f"0.2:orepeat 0.6:d233,233 0.65:u 2.6:d{taps[k]} 2.65:u", 4.2)[1]["repeat"] for k in range(4)]
ok &= check("repeat: the right side takes him on to round 2, any other is over", sorted(a.split(",")[0] for a in after) == ["1", "1", "1", "2"]
            and sum(a == "1,4" for a in after) == 3, after)
k = next((i for i, a in enumerate(after) if a.startswith("2")), 0)
w = {0: "233,300 2.65:m233,260 2.7:m233,220", 1: "150,233 2.65:m190,233 2.7:m230,233", 2: "233,150 2.65:m233,190 2.7:m233,230", 3: "300,233 2.65:m260,233 2.7:m220,233"}
_, st = run(f"0.2:orepeat 0.6:d233,233 0.65:u 2.6:d{w[k]} 2.75:u", 4.2)
ok &= check("repeat: a swipe that way answers as well", st["repeat"].startswith("2,"), st)
_, st = run("0.2:orepeat 0.6:d233,233 0.65:u", 9.0)
ok &= check("repeat: no answer for 6 s: over", st["repeat"] == "1,4", st)
_, st = run("0.1:y0.05 0.2:orepeat", 0.8)
ok &= check("repeat: worn out, \"Пип устал\" opens instead", st["screen"] == "tired", st)
_, st = run("0.2:orepeat 0.6:B", 1.2)
ok &= check("repeat: the bottom key leaves it", st["screen"] == "menu" and st["repeat"] == "-", st)
# twice quickly on the bottom key from an app or a group: straight home to Pip
_, st = run("0.2:oclock 0.6:B 0.8:B", 1.6)
ok &= check("bottom twice from an app (in its group) -> Pip", st["screen"] == "pip", st)
_, st = run("0.2:oclock 0.6:B 1.3:B", 1.8)
ok &= check("bottom twice slowly -> one step at a time (the menu)", st["screen"] == "menu", st)
_, st = run("0.2:P 0.5:d233,300 0.55:m233,240 0.6:m233,188 0.9:u 1.6:P 2.2:B 2.4:B", 3.0)
ok &= check("bottom twice from a group's list -> Pip", st["screen"] == "pip", st)
_, st = run("0.3:F", 1.4)
ok &= check("a fresh pet takes the first biscuit (food starts just under full)", st["reaction"] == "yum", st)
_, st = run("0.3:F 2.0:F", 3.2)
ok &= check("feeding a full pet -> too full", st["reaction"] == "too" and st["mood"] == "nauseous", st)
# beaten again and again he takes it badly, the sooner and the harder the more demanding he is
beat = lambda n: " ".join(f"{0.3 + 1.5 * k}:w1" for k in range(n))  # why 3.1: just lost, 3.8: sore
_, st = run("0.05:Y1 " + beat(2), 2.6)
ok &= check("Уравновешенный: two lost games -> just indignant", st["why"] == "3.1" and st["base"] == "indignant", st)
_, st = run("0.05:Y1 " + beat(3), 3.6)
ok &= check("Уравновешенный: three in a row -> sore (hurt or angry)", st["reaction"] == "sore" and st["why"] == "3.8" and st["base"] in ("hurt", "angry"), st)
_, st = run("0.05:Y0 " + beat(4), 5.1)
ok &= check("Неприхотливый: four in a row he still shrugs it off", st["why"] == "3.1", st)
_, st = run("0.05:Y0 " + beat(5), 6.6)
ok &= check("Неприхотливый: five -> sad", st["reaction"] == "sore" and st["base"] == "sad", st)
_, st = run("0.05:Y2 " + beat(2), 2.6)
ok &= check("Требовательный: two -> furious or belligerent", st["reaction"] == "sore" and st["base"] in ("furious", "belligerent"), st)
_, st = run("0.05:Y1 0.3:w1 1.8:w1 3.3:w0 4.8:w1", 5.4)
ok &= check("a win of his in between starts the count over", st["why"] == "3.1", st)
for ev, mood in (("w0", "smug"), ("w1", "no")):  # a game with him ends on its own screen (his voice is off there otherwise)
    _, st = run("0.05:H0 0.3:oxo 0.8:" + ev, 2.0)
    ok &= check(f"a game with him ends ({ev}): heard once, glad or sad, and it shows in his mood", st["voices"] == "1" and st["mood"] == mood, st)
_, st = run("0.05:H0 0.3:w0", 2.0)
ok &= check("...on his face: still one sound, not two", st["voices"] == "1", st)
_, st = run("0.05:Y1 0.3:w1 1.8:w1 2.5:T0.6 3.3:w1", 3.9)
ok &= check("over half an hour between games starts it over too", st["why"] == "3.1", st)
# drowsy just after waking: a shake or a stroke may rouse him, not always the first time
woke = "0.1:a0,0,-1 2.5:a0,1,0"
_, st = run(woke, 3.0)
ok &= check("just woken: drowsy", st["base"] == "sleepy", st)
_, st = run("0.05:Kperk_shake=0.01 " + woke + " 3.0:S 3.8:S", 4.5)  # a long shot: the shake misses
ok &= check("drowsy: a shake need not rouse him", st["reaction"] == "dizzy" and st["base"] == "sleepy", st)
_, st = run(woke + " 3.0:S 3.8:S 5.0:S 5.8:S", 6.5)  # each miss makes the next likelier: by now he is up
ok &= check("drowsy: shaken again and again he perks up, no sleepy face", st["base"] != "sleepy", st)
pets = lambda t: f"{t:.1f}:d200,200 " + " ".join(f"{t + 0.05 * (i + 1):.2f}:m{260 if i % 2 == 0 else 200},200" for i in range(11)) + f" {t + 0.6:.1f}:u"
_, st = run(woke + " " + " ".join(pets(3.0 + 2 * k) for k in range(4)), 10.5)
ok &= check("drowsy: stroked a few times he perks up", st["base"] != "sleepy", st)
_, st = run(woke + " " + " ".join(f"{3.0 + 1.2 * k:.1f}:d233,400 {3.05 + 1.2 * k:.2f}:u" for k in range(12)), 18.0)
ok &= check("drowsy: poked again and again he perks up too", st["base"] != "sleepy", st)
_, st = run("0.2:T13", 1.0)
ok &= check("01:00 -> asleep on its own", st["mood"] == "asleep" and st["asleep"] == "1", st)
_, st = run("0.2:T13 0.6:d233,400 0.65:u", 1.0)
ok &= check("poke at night wakes him up", st["reaction"] == "woken" and st["asleep"] == "0", st)
_, st = run("0.2:T21", 1.0)
ok &= check("a day of neglect shows in the mood", st["mood"] in ("angry", "furious", "annoyed", "belligerent", "lonely", "despondent", "hurt", "sad",
                                                                  "bored", "cynical", "gloomy", "nervous", "sick"), st)
_, st = run("0.2:nrage 0.4:A", 1.0)
ok &= check("A returns to the automatic mood", st["auto"] == "1" and st["mood"] != "rage", st)

# the mood tree: every branch from its real input. `why` is kind.which (ui/pet.h): 1 rare card, 2 body, 3 aftertaste,
# 4 need, 5 night, 6 situation, 7 palette. K<knob>=<v> sets a knob of the tree, as the debug page does.
def pokes(n, t=0.2):  # counted pokes: no sneezes from the tickle to swallow any (the sneeze has its own checks)
    return "0.01:Ksneeze_poke=0 " + " ".join(f"{t + i * 0.25:.2f}:d233,400 {t + i * 0.25 + 0.05:.2f}:u" for i in range(n))
BAD_DAY = "0.02:Kcross_good=0 0.02:Kswear_v=1 0.02:Kberserk_v=1 "  # as on a dreadful day: his mood holds nothing back
# Требовательный (patience 1: each poke adds 0.35 to his annoyance, the ladder in pet.c cross); a fed-up every ~9 quick pokes
f = fedups("0.01:Y2 " + BAD_DAY + pokes(100), 26)
ok &= check("bad day, Требовательный poked on and on: annoyed, angry, furious ... swearing, at last berserk",
            f[:3] == ["annoyed", "angry", "furious"] and "swearing" in f and f[-1] == "berserk" and f.index("swearing") > 3, f)
f = fedups("0.01:Y2 " + pokes(100), 26)
ok &= check("good day, Требовательный poked on and on: no worse than rage", f[:1] == ["annoyed"] and set(f) <= {"annoyed", "angry", "furious", "rage"}, f)
f = fedups("0.01:Y0 " + BAD_DAY + pokes(100), 26)
ok &= check("bad day, Неприхотливый poked on and on (25 s, 4 a second): put out twice, then a rung at a time, never swearing",
            f[:2] == ["annoyed", "annoyed"] and steps(f) and "swearing" not in f and "berserk" not in f, f)
_, st = run("0.01:Y2 " + pokes(10), 5.0)
ok &= check("fed up wears off as a minute of annoyed (aftertaste)", st["mood"] == "annoyed" and st["why"] == "3.7", st)
_, st = run("0.05:Kmeet_like=200 0.06:Kmeet_shy=1 0.2:p1", 2.0)
ok &= check("meet: liked, shy", st["reaction"] == "shy" and st["mood"] == "shy", st)
_, st = run("0.05:Kmeet_like=200 0.06:nangry 0.2:p1", 2.0)
ok &= check("meet while cross: however likeable, no love (sceptical)", st["reaction"] == "not" and st["mood"] == "skeptical", st)
_, st = run("0.05:Kmeet_like=200 0.06:Kmeet_shy=0 0.2:p1", 2.0)
ok &= check("meet: liked -> love", st["reaction"] == "likes" and st["mood"] == "love", st)
_, st = run("0.05:Kmeet_like=-100 0.06:Kmeet_wary=1 0.2:p1", 2.0)
ok &= check("meet: disliked, wary -> suspicious", st["reaction"] == "wary" and st["mood"] == "suspicious", st)
_, st = run("0.05:Kmeet_like=-100 0.06:Kmeet_wary=0 0.2:p1", 2.0)
ok &= check("meet: disliked -> angry", st["reaction"] == "dislikes" and st["mood"] == "angry", st)
_, st = run("0.01:Y2 0.05:Kmeet_like=-100 " + pokes(6) + " 1.6:p1", 3.4)
ok &= check("meet while annoyed: disliked -> belligerent", st["mood"] == "belligerent", st)
_, st = run("0.1:V35 2.5:V95 2.65:V35", 3.2)
ok &= check("a bang (95 dB over a quiet room) -> shocked", st["reaction"] == "bang" and st["mood"] == "shocked", st)
_, st = run("0.1:V40 2.5:V80 2.65:V40", 3.2)
ok &= check("a smaller bang (80 dB) -> afraid", st["reaction"] == "bang" and st["mood"] == "afraid", st)
_, st = run("0.1:V35 2.5:V95 2.65:V35", 6.0)
ok &= check("after a bang he stays afraid a while (aftertaste)", st["mood"] == "afraid" and st["why"] == "3.3", st)
_, st = run("0.1:V35 2.5:Q 4:Q", 6.0)
ok &= check("blowing on the mic is no bang", st["base"] != "afraid", st)
_, st = run("0.1:V35 2.5:Q2", 3.5)
ok &= check("blown on close up (the board's mic overloaded, broadband): breath, not a bang", st["reaction"].startswith("blown") and st["mood"] not in ("shocked", "afraid"), st)
_, st = run("0.1:V35 2.5:Q2 4:Q2", 6.0)
ok &= check("...and after it no fright", st["base"] != "afraid", st)
_, st = run("0.1:V80", 4.0)
ok &= check("a very loud room (80 dB) is not breath", not st["reaction"].startswith("blown"), st)
_, st = run("0.2:a0,-1,0", 2.0)
ok &= check("held upside down -> puzzled", st["reaction"] == "upside" and st["mood"] == "puzzled", st)
_, st = run("0.2:a0,-1,0 2.0:a0,1,0", 2.6)
ok &= check("turned back over: puzzled no more", st["reaction"] != "upside" and st["mood"] != "puzzled", st)
_, st = run("0.3:a0,0,0 0.6:a0,1,0", 1.2)
ok &= check("dropped (free fall) -> shocked, then hurt", st["reaction"] == "dropped" and st["mood"] == "shocked" and st["base"] == "hurt", st)
_, st = run("0.3:a3,1,0 0.42:a0,0,0 0.8:a0,1,0", 1.4)
ok &= check("thrown (a shove, then free fall) -> angry, then hurt", st["reaction"] == "thrown" and st["mood"] == "angry" and st["base"] == "hurt", st)
_, st = run("0.3:a3,1,0 0.42:a0,0,0 0.8:a0,1,0 2.0:a3,1,0 2.12:a0,0,0 2.5:a0,1,0", 3.0)
ok &= check("thrown again -> furious", st["reaction"] == "thrown" and st["mood"] == "furious", st)
_, st = run("0.3:a0,0,0 0.6:a0,1,0 " + " ".join(f"{1.0 + 0.05 * i:.2f}:{'d200,200' if i == 0 else ('m260,200' if i % 2 else 'm200,200')}" for i in range(30)) + " 2.55:u", 3.0)
ok &= check("dropped: a stroke makes it up", st["base"] != "hurt", st)
_, st = run("0.1:V35 2.5:V95 2.65:V35 " + " ".join(f"{5.0 + 0.05 * i:.2f}:{'d200,200' if i == 0 else ('m260,200' if i % 2 else 'm200,200')}" for i in range(30)) + " 6.55:u", 9.0)
ok &= check("a bang, then a stroke: the fright is over (no afraid face coming back)", st["base"] != "afraid" and st["mood"] != "afraid", st)
_, st = run("0.1:V35 2.5:V95 2.65:V35", 14.0)
ok &= check("a bang: afraid ~10 s, not half a minute", st["base"] != "afraid", st)
_, st = run("0.1:V35 2.5:V95 2.65:V35 5:B1", 8.0)
ok &= check("a bang, then a biscuit: the fright is over too", st["base"] != "afraid" and st["mood"] != "afraid", st)
_, st = run("0.1:f8,0 0.3:a0,0,0 0.6:a0,1,0 0.9:a3,1,0 1.02:a0,0,0 1.4:a0,1,0", 2.0)
ok &= check("falls switched off (the panel's Падения): dropped and thrown pass unnoticed", st["reaction"] not in ("dropped", "thrown") and st["base"] != "hurt", st)
_, st = run("0.2:a0,0,-1 2.6:a0,1,0 3.0:a0,0,-1 5.4:a0,1,0", 6.0)
ok &= check("put to bed and picked up twice in a row -> indignant", st["reaction"] == "woke" and st["mood"] == "indignant", st)
_, st = run("0.2:a0,0,-1 2.6:a0,1,0", 5.0)
ok &= check("just woken: sleepy for a while (situation)", st["mood"] == "sleepy" and st["why"] == "6.0", st)
_, st = run("0.1:H1 0.2:L2 0.4:N 0.5:N 0.6:N", 1.2)
ok &= check("three notifications at once -> dismayed", st["reaction"] == "flood" and st["mood"] == "dismayed" and st["why"] == "3.4", st)
_, st = run("0.1:H1 0.2:L2 0.4:N 6:N 12:N", 12.5)
ok &= check("notifications one by one: no flood", st["reaction"] != "flood" and st["base"] != "dismayed", st)
_, st = run("0.2:L2 0.4:C1 0.7:P", 1.2)
ok &= check("call answered -> he nods (yes)", st["reaction"] == "answered" and st["mood"] == "yes", st)
_, st = run("0.2:L2 0.4:C1 0.7:B", 1.2)
ok &= check("call declined -> he shakes his head (no)", st["reaction"] == "declined" and st["mood"] == "no", st)
_, st = run("0.2:L2 0.4:C1 0.8:C0", 1.4)
ok &= check("missed call -> puzzled", st["reaction"] == "missed" and st["mood"] == "puzzled", st)
_, st = run("0.2:L1 0.5:P", 1.0)
ok &= check("phone paired -> yes", st["reaction"] == "paired" and st["mood"] == "yes", st)
_, st = run("0.2:T6 0.6:d233,400 0.65:u", 1.2)
ok &= check("back after 6 h, loved enough -> glad (love, then happy for a while)",
            st["reaction"] == "glad" and st["mood"] == "love" and st["base"] == "happy", st)
_, st = run("0.05:Y2 0.2:T10 0.6:d233,400 0.65:u", 1.2)  # Требовательный: lonely within 10 h
ok &= check("back after 10 h, neglected -> sulks (hurt)", st["reaction"] == "sulks" and st["mood"] == "hurt" and st["why"] == "3.5", st)
stroke_late = " ".join(f"{1.05 + i * 0.05:.2f}:m{150 if i % 8 < 4 else 320},233" for i in range(70))
_, st = run("0.2:T10 0.6:d233,400 0.65:u 1.0:d150,233 " + stroke_late, 5.0)
ok &= check("sulking, stroked -> made up", st["why"] != "3.5", st)
for card in ("stoned", "pinkeye"):
    _, st = run(f"0.2:R{card}", 1.0)
    ok &= check(f"rare card -> {card}", st["mood"] == card and st["why"].startswith("1."), st)
_, st = run("0.1:J15", 1.0)
ok &= check("battery 15% -> nervous", st["mood"] == "nervous" and st["why"] == "2.3", st)
_, st = run("0.1:J3", 1.0)
ok &= check("battery 3% -> sick", st["mood"] == "sick" and st["why"] == "2.0", st)
_, st = run("0.1:J50 0.4:J50+ 0.6:T0.05", 6.0)
ok &= check("on the charger a while -> comfy", st["mood"] == "comfy" and st["why"] == "6.2", st)
_, st = run("0.2:W", 20.0)
ok &= check("out on a walk -> curious / amazed", st["mood"] in ("curious", "amazed") and st["why"] == "6.1", st)
full = "0.3:F 2.0:F "  # the first biscuit fills him up, the next one is too much
_, st = run(full + "3.7:F", 4.9)  # one bite at a time (each ~1.6 s)
_, st3 = run(full + "3.7:F 5.4:F", 6.1)
_, st4 = run(full + "3.7:F 5.4:F 6.4:F", 7.1)
ok &= check("fed twice when full -> disgusted", st["reaction"] == "enough" and st["mood"] == "disgusted", st)
ok &= check("a third time -> no (the biscuit stays out)", st3["reaction"] == "no" and st3["mood"] == "no", st3)
ok &= check("and again -> cross (angry, furious, swearing...)", st4["reaction"] == "stop" and
            st4["mood"] in ("angry", "indignant", "annoyed", "furious", "rage", "swearing"), st4)
moods = set()
for k in range(5):  # keep at it: it darkens, and swearing is among what comes
    _, st = run("0.3:F 2.0:F 3.7:F " + " ".join(f"{4.7 + i * 1.0:.1f}:F" for i in range(k + 2)), 4.9 + (k + 2) * 1.0)
    moods.add(st["mood"])
ok &= check("overfed again and again: more than one way of being cross", len(moods) >= 2, moods)
_, st = run("0.05:Y2 0.2:T8 0.5:G", 1.0)  # Требовательный: bored within 8 h
ok &= check("bored, offered a game -> yes", st["reaction"] == "play" and st["mood"] == "yes", st)
_, st = run("0.2:T8.5", 1.0)
ok &= check("20:30 -> the evening palettes", st["why"].startswith("7.") and int(st["why"][2:]) >= 4, st)
def xo(turns):  # endless noughts and crosses with Pip: each turn taps cells in this order, the first free one counts
    ev, t = ["0.1:oxo"], 0.6
    for pri in turns:
        for c in map(int, pri):
            ev += [f"{t:.2f}:d{137 + c % 3 * 96},{137 + c // 3 * 96}", f"{t + 0.02:.2f}:u"]
            t += 0.04
        t += 0.8
    return run(" ".join(ev), round(t, 2))[1]
games = {}
import random
pick = random.Random(7)  # tap orders that do not hang on how Pip's own moves fall: play until he has won once and lost once
for attempt in range(24):
    st = xo(["".join(pick.sample("012345678", 9)) for _ in range(pick.randint(3, 7))])
    if st["reaction"] not in ("won", "lost") or st["reaction"] in games: continue
    games[st["reaction"]] = st
    ok &= check(f"noughts and crosses: {st['reaction']} -> " + ("smug" if st["reaction"] == "won" else "no, then indignant"),
                (st["reaction"], st["mood"], st["base"]) in (("won", "smug", "smug"), ("lost", "no", "indignant")), st)
    if len(games) == 2: break
ok &= check("noughts and crosses: he both won and lost", set(games) == {"won", "lost"}, games)
# yes / no: the eyes swing up and down / side to side
for name, axis in (("yes", 1), ("no", 0)):
    pos = [eyes(run(f"0:n{name}", 1.5 + i * 0.06, "sw")[0])[:2] for i in range(8)]
    swing = [max(p[k] for p in pos) - min(p[k] for p in pos) for k in (0, 1)]
    ok &= check(f"{name}: the eyes swing {'up and down' if axis else 'side to side'}", swing[axis] > 12 and swing[axis] > 2 * swing[1 - axis], swing)

# apps: navigation with a finger and the side buttons
flick0 = "0.5:P"  # the menu opens with the top button only (no swipe on Pip's face)
_, st = run("0.2:P 0.7:P", 1.2)
ok &= check("top button: Pip -> menu -> the app in the middle", st["screen"] == "care", st)
# the menu laid out on the panel (shell_menu_set, pipsim "g"): its own order, apps moved between blocks, names
lay = "0.05:gput_top_tools_care;put_tools_torch_xo;name_tools_Stuff"
_, st = run(lay + " 0.2:P 0.6:P 1.0:P", 1.5)
ok &= check("panel menu: a block moved to the top, an app moved into it, first", st["screen"] == "torch", st)
_, st = run(lay + " 0.2:P 0.6:P 1.0:P 1.4:B", 1.9)
ok &= check("panel menu: back from it lands in its new block, not the old one", st["screen"] == "menu", st)
_, st = run("0.05:gput_top_xo_care 0.2:P 0.6:P", 1.0)
ok &= check("panel menu: an app out of its block, first on the top list", st["screen"] == "xo", st)
empty = "0.05:gput_top_games_care_xo_pong_snake_flight_repeat_ball_eight_coin_dice_maze"
_, st = run(empty + " 0.2:P 0.6:P", 1.0)
ok &= check("panel menu: an emptied block is not listed", st["screen"] == "care", st)
_, st = run(empty + " 0.2:ogames", 0.6)
ok &= check("panel menu: an emptied block does not open", st["screen"] == "pip", st)
_, st = run(lay + " 0.1:g 0.2:P 0.6:P", 1.0)
ok &= check("panel menu: reset to the default one", st["screen"] == "care", st)
_, st = run("0.2:P 0.5:gput_top_maze 0.9:P", 1.4)
ok &= check("panel menu: changed with the menu open, it is rebuilt at once", st["screen"] == "maze", st)
_, st = run("0.2:ocare 0.6:B 1.2:B", 1.8)  # a top-level app that exists ("qr" opened nothing)
ok &= check("bottom button: app -> menu -> Pip", st["screen"] == "pip", st)
_, st = run("0.3:B", 1.0)
ok &= check("bottom button on Pip -> meet", st["reaction"] == "meet", st)
_, st = run("0.2:ostopwatch 0.5:P 1.0:P 1.4:B", 1.8)
ok &= check("stopwatch keeps both buttons (start/lap, reset)", st["screen"] == "stopwatch", st)
_, st = run("0.2:otimer 0.6:d179,325 0.65:u", 3.0)
ok &= check("timer starts", st["screen"] == "timer", st)
_, st = run("0.01:H0 0.2:otimer 0.4:d62,320 0.5:m85,350 0.6:m100,372 0.7:u 0.9:d179,325 0.95:u", 64)  # knob to 1 min, start
ok &= check("1-minute timer rings: back to Pip, surprised, the time's-up card on top", st["screen"] == "timer_done" and st["reaction"] == "time's", st)
_, st = run("0.01:H0 0.2:otimer 0.4:d62,320 0.5:m85,350 0.6:m100,372 0.7:u 0.9:d179,325 0.95:u 1.3:B 1.7:B 2.1:B", 64)
ok &= check("closed while running, it still ends: the card shows over Pip", st["screen"] == "timer_done", st)
_, st = run("0.01:H0 0.2:otimer 0.4:d62,320 0.5:m85,350 0.6:m100,372 0.7:u 0.9:d179,325 0.95:u 1.3:B 1.7:B 2.1:B 63:d233,320 63.05:u", 63.6)
ok &= check("a tap puts the card away", st["screen"] == "pip", st)
for key in ("P", "B"):  # any button puts it away, and the song stops at once (no voice busy after the press)
    _, st = run(f"0.01:H0 0.2:otimer 0.4:d62,320 0.5:m85,350 0.6:m100,372 0.7:u 0.9:d179,325 0.95:u 1.3:B 1.7:B 2.1:B 63:{key}", 63.2)
    ok &= check(f"ringing timer: the {'top' if key == 'P' else 'bottom'} button puts it away", st["screen"] == "pip", st)
_, st = run("0.2:otimer 0.5:P", 1.0)
ok &= check("timer: the top button goes back, as the bottom one does", st["screen"] == "menu", st)
_, st = run("0.2:otimer 0.4:d233,150 0.45:m233,250 0.5:m233,350 0.55:u", 1.2)
ok &= check("no swipe closes an app", st["screen"] == "timer", st)
_, st = run("0.3:S 1.0:S 1.8:S 2.5:S 3.3:S 4.0:S", 4.5)
ok &= check("shaken three times in a row -> dull for a while", st["mood"] == "dull" and st["reaction"] == "shaken", st)

# settings, the list menu, and every app opening in every theme
_, st = run("0.2:osound 0.5:P", 0.9)  # every app lets you out with either button (apps whose top button does something: stopwatch, maze, camera, music)
ok &= check("Звуки: the top button switches nothing and goes back", st["screen"] == "menu", st)
_, st = run("0.2:osound 0.5:B", 0.9)
ok &= check("Звуки: the bottom one goes back to Настройки", st["screen"] == "menu", st)
dim, _ = run("0.2:otorch 0.5:d300,233 0.6:m200,233 0.7:m120,233 0.8:u", 1.2)
ok &= check("torch: white, a drag to the left dims it", 60 < dim.getpixel((233, 400)) < 200, dim.getpixel((233, 400)))
_, st = run("0.3:P", 0.8)
ok &= check("Стандартная is the default theme (0)", st["theme"] == "0", st)
_, st = run("0.2:ostyle 0.5:d233,234 0.55:u", 1.2)
ok &= check("Темы in Настройки: a tap on Ретро puts it on, the screen stays on Темы", st["theme"] == "1" and st["screen"] == "style", st)
_, st = run("0.2:ostyle 0.5:d233,234 0.55:u 1.0:d233,310 1.05:u 1.6:B", 2.0)
ok &= check("Темы: then Барби, the bottom button back to Настройки", st["theme"] == "2" and st["screen"] == "menu", st)
_, st = run("0.2:ostyle 0.5:P", 0.9)
ok &= check("Темы: the top button changes nothing and goes back", st["theme"] == "0" and st["screen"] == "menu", st)
_, st = run("0.2:oeyes 0.6:d233,198 0.65:u", 1.2)
ok &= check("Глаза in Настройки: a tap on the next row puts on Бабл-гам", st["eyes"] == "1" and st["screen"] == "eyes", st)
_, st = run("0.2:oeyes 0.5:d233,340 0.6:m233,260 0.7:m233,180 0.8:m233,100 0.85:u 2.5:d233,132 2.55:u", 3.0)
ok &= check("Глаза: scrolled down, a tap on the top row puts another set on", st["eyes"] not in ("0", "1") and st["screen"] == "eyes", st)
img, _ = run("0.2:oeyes " + " ".join(f"{0.5 + k * 0.4:.1f}:d233,340 {0.55 + k * 0.4:.2f}:m233,200 {0.6 + k * 0.4:.1f}:m233,60 {0.65 + k * 0.4:.2f}:u" for k in range(5)), 4.5)
lit = sum(1 for y in range(300, 352, 4) for x in range(90, 380, 8) if img.getpixel((x, y)) > 12)
ok &= check("Глаза: flung past the end, the last row still fills the bottom of the window", lit > 40, lit)
_, st = run("0.2:oeyes 0.5:d233,330 0.52:m233,310 0.54:m233,290 0.56:m233,270 0.57:u 2.0:d233,132 2.05:u", 2.6)
ok &= check("Глаза: a short flick coasts on like the menu's wheel (60 px -> 3+ rows)", int(st["eyes"]) >= 3 and st["screen"] == "eyes", st)
for x in (30, 436):  # beside the window, left and right of the rows
    _, st = run(f"0.2:oeyes 0.5:d{x},330 0.55:m{x},280 0.6:m{x},230 0.65:m{x},180 0.7:m{x},130 0.75:u 1.6:d233,132 1.65:u", 2.1)
    ok &= check(f"Глаза: a drag beside the list (x {x}) scrolls it too", st["eyes"] not in ("0", "1") and st["screen"] == "eyes", st)
_, st = run("0.1:H0 " + flick0 + " 1.2:d233,233 1.25:u", 1.8)
ok &= check("Стандартная list menu: tap the middle row -> care", st["screen"] == "care", st)
_, st = run("0.1:H0 " + flick0 + " 1.2:d233,300 1.25:m233,240 1.3:m233,188 1.6:u 2.2:d233,233 2.25:u 2.8:d233,233 2.85:u", 3.4)
ok &= check("list menu: drag up two rows (past the games), tap -> the utilities' list, tap -> clock", st["screen"] == "clock", st)
# the second top press opens the first game if the flick stopped on the games row (a sub-list, not an app)
_, st = run("0.1:H1 0.2:P 0.5:d233,330 0.53:m233,280 0.56:m233,230 0.59:m233,180 0.6:u 2.2:P 2.5:P", 2.9)
ok &= check("list menu: a flick coasts several rows and lands on one", st["screen"] not in ("menu", "clock", "care", "noise"), st)
app_ids = ["bright", "bt", "clock", "care", "noise", "games", "tools", "settings", "xo", "eight", "coin", "dice", "maze", "timer", "stopwatch", "torch", "ball", "sound", "style", "eyes", "pong", "snake", "flight", "repeat", "notify", "music", "camera", "locate", "phone", "privacy"]
# the maze: the physics on its own (perfect mazes, a steered ball gets out, nothing shakes it through a wall), then the app
r = subprocess.run([os.path.join(os.path.dirname(sim), "maze_test")], capture_output=True, text=True, encoding="utf-8", errors="replace")
ok &= check("maze: perfect mazes, a steered ball gets out, never through a wall (maze_test)", r.returncode == 0, r.stderr.strip())
def blob(img):  # the ball: the only white thick enough for a 9x9 square (walls are 4-5 px)
    w = lambda x, y: img.getpixel((x, y)) > 200
    pts = [(x, y) for y in range(70, 400, 2) for x in range(70, 400, 2) if all(w(x + dx, y + dy) for dx in (-4, 0, 4) for dy in (-4, 0, 4))]
    return (sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts)) if pts else None
held = "0:a0,0.6,0.8 0.1:H0 0.3:omaze"
b0, b1 = blob(run(held, 0.9)[0]), blob(run(held + " 1.0:a0,0.9,0.43 2.0:a-0.4,0.82,0.4", 3.4)[0])
# the maze is random: a wall may stop it one way, so it is down and right together that it must have gone
ok &= check("maze: tilt down, then right -> the ball rolls down and right", b0 and b1 and b1[1] > b0[1] and b1[0] > b0[0]
            and (b1[0] - b0[0]) + (b1[1] - b0[1]) > 50, (b0, b1))
_, st = run(held + " 1.0:P", 1.4)
ok &= check("maze: top button deals a new one, stays in the app", st["screen"] == "maze", st)
# worn out (asleep after a night's jump): no games with him; noughts and crosses with a friend only; the toys stay
_, st = run("0.1:H0 0.2:T13 0.5:opong", 1.0)
ok &= check("asleep: ping-pong (a game with him) shows 'Pip is tired' instead", st["screen"] == "tired" and st["asleep"] == "1", st)
_, st = run("0.1:H0 0.2:T13 0.5:opong 0.8:B", 1.2)
ok &= check("asleep: back from the tired note -> the games list", st["screen"] == "menu", st)
for g in ("xo", "ball", "maze", "eight", "coin", "dice"):
    _, st = run(f"0.1:H0 0.2:T13 0.5:o{g}", 1.0)
    ok &= check(f"asleep: {g} still opens (on your own, with a friend, a toy)", st["screen"] == g, st)
_, st = run("0.1:H0 0.2:opong 0.4:d233,330 " + " ".join(f"{i / 2}:m{150 + i % 2 * 160},330" for i in range(1, 79)), 40)
ok &= check("rested: ping-pong played with a finger held and moved (never lifted), the screen stays lit", st["screen"] == "pong" and st["display"] == "1", st)
for a in ("eyes", "privacy", "pong"):
    _, st = run(f"0.2:o{a} 0.5:P", 0.9)
    ok &= check(f"{a}: the top button goes back too", st["screen"] == "menu", st)
for g in ("coin", "dice", "eight"):
    _, st = run(f"0.1:H0 0.2:o{g} 0.4:d233,200 0.45:u 2.0:P", 2.4)
    ok &= check(f"{g}: a tap shuffles it, the top button goes back", st["screen"] == "menu", st)
# his age: day 1 when he comes, day 3 fifty hours later
def days(events, secs=0.6):
    out = subprocess.run([sim, "--out", os.path.join(tmp, "d.png"), "--secs", str(secs), "--events", events], check=True,
                         capture_output=True, text=True, encoding="utf-8", errors="replace").stdout
    return int(re.search(r"days=(\d+)", out).group(1))
AGE50 = "0.1:H0 0.2:T25 0.3:F 1.8:F 3.3:T25"  # fed half way (each biscuit eaten before the next): fifty hours alive
ok &= check("age: day 1 at first, day 3 fifty hours on", (days("0.1:H0"), days(AGE50, 3.6)) == (1, 3), (days("0.1:H0"), days(AGE50, 3.6)))
# the menu opens with the top button only: a quick flick across the face leaves him where he is
_, st = run("0.1:H0 0.5:d400,233 0.53:m300,233 0.56:m180,233 0.59:m60,233 0.6:u", 1.2)
ok &= check("a flick across Pip does not open the menu", st["screen"] == "pip", st)
_, st = run("0.1:H0 0.5:P", 1.0)
ok &= check("the top button on Pip opens the menu", st["screen"] == "menu", st)
_, st = run("0.1:H0 0.3:ogames 0.8:B", 1.6)
ok &= check("games in the list menu: bottom button goes back to the menu, not to Pip", st["screen"] == "menu", st)
_, st = run("0.1:H0 0.3:odice 0.6:B", 1.4)
ok &= check("back out of a game lands in the list menu (on the games)", st["screen"] == "menu", st)
_, st = run("0.1:L2 0.4:N", 0.9)
ok &= check("a notification shows a banner (its text hidden by default)", st["screen"] == "banner", st)
_, st = run("0.1:L2 0.3:oprivacy 0.6:d233,255 0.65:u 0.9:B 1.3:N 1.5:onotify", 1.9)  # Настройки > Приватность > Выключено
ok &= check("privacy off: no banner, nothing kept", st["screen"] == "notify", st)
_, st = run("0.05:H0", 33)
ok &= check("left alone 30 s: the screen sleeps", st["display"] == "0", st)
_, st = run("0.05:H0 34:d233,233 34.1:u", 36)
ok &= check("asleep: a touch wakes him", st["display"] == "1", st)
_, st = run("0.05:H0 0:a0,0,1 34:a0,0.6,0.8", 36)
ok &= check("asleep: picked up (turned) wakes him", st["display"] == "1", st)
_, st = run("0.05:H0 0:a0,0,1 34:S", 36)
ok &= check("asleep: shaken, he wakes", st["display"] == "1", st)
_, st = run("0.05:H0 0.1:f9,0 0:a0,0,1 34:a0,0.6,0.8", 36)
ok &= check("Просыпается от движения off: picked up, the screen stays dark", st["display"] == "0", st)
_, st = run("0.05:H0 0.1:f9,0 0:a0,0,1 34:S", 36)
ok &= check("...shaken too", st["display"] == "0", st)
_, base = run("0.05:H0 0.1:f9,0 0:a0,0,1", 36)  # the sound of him dozing off, and nothing after it
ok &= check("...and not a sound or a face from him", st["voices"] == base["voices"] and st["reaction"] == "", (st, base["voices"]))
_, st = run("0.05:H0 0.1:f9,0 0:a0,0,1 34:a0,0,-1 34.5:a0,0,10 35:a0,0,-1", 36)
ok &= check("...nor dropped or turned over in the dark", st["voices"] == base["voices"] and st["display"] == "0", (st, base["voices"]))
_, st = run("0.05:H0 0.1:f9,0 34:d233,233 34.1:u", 36)
ok &= check("...a touch still wakes him", st["display"] == "1", st)
_, st = run("0.05:H0 0.3:otorch", 36)
ok &= check("the torch sleeps on the set timer (only games played without touching hold the screen)", st["display"] == "0", st)
_, st = run("0.05:H0 0.3:ostopwatch", 36)
ok &= check("the stopwatch sleeps on the set timer too", st["display"] == "0", st)
img, st = run("0.3:Z", 40); img = img.convert("RGB")
ok &= check("updating: the notice stays lit until the reset", st["display"] == "1" and img.getpixel((233, 140)) == (0, 0, 0) and max(pixels(img.crop((120, 170, 346, 210)))) > (200, 200, 200), st)
for t in range(3):
    bad = []
    for a in app_ids:
        img, st = run(f"0.1:H{t} {'0:V45 ' if a == 'noise' else ''}0.3:o{a}", 0.9, "th")  # the meter: a room to hear (the mic opens with it)
        lit = sum(1 for y in range(0, 466, 6) for x in range(0, 466, 6) if abs(img.getpixel((x, y)) - img.getpixel((233, 8))) > 40)
        want = "menu" if a in ("games", "tools", "phone", "settings") else a  # a group's row turns the list into the group
        if st["screen"] != want or lit < 20: bad.append((a, st["screen"], lit))
    ok &= check(f"theme {t}: all {len(app_ids)} apps open and draw something", not bad, bad)
_, st2 = run("0.1:H0 0:V45 0.3:onoise", 2.2)
ok &= check("the noise meter: the mic comes on with it, the level only (Pip hears nothing there)", st2["screen"] == "noise" and st2["mic"] == "0", st2)
_, st = run("0.05:H0 0:V45 0.5:P", 1.5)
ok &= check("the menu: the microphone is off", st["screen"] == "menu" and st["mic"] == "0", st)
_, st = run("0.05:H0 0:V45 0.2:T13", 1.5)
ok &= check("asleep: the microphone is off", st["asleep"] == "1" and st["mic"] == "0", st)


# Ретро: the terminal layouts drive the same apps through the buttons the hints promise
_, st = run("0.1:H1 0.2:P 0.5:d233,300 0.55:m233,240 0.6:m233,188 0.9:u 1.5:P 2.0:P", 2.4)
ok &= check("Ретро list: drag up two rows, top -> the utilities, top -> clock", st["screen"] == "clock", st)
_, st = run("0.1:H1 0.2:ostopwatch 0.5:B", 0.9)
ok &= check("stopwatch at zero: bottom button leaves -> menu", st["screen"] == "menu", st)
_, st = run("0.1:H1 0.2:otimer 0.4:d243,40 0.45:m240,40 0.5:u 0.8:d233,308 0.85:u", 63.5)
ok &= check("Ретро timer: ring set to 1 min, a tap on старт, it rings", st["screen"] == "timer_done" and st["reaction"] == "time's", st)

# the iPhone (a fake one): pairing, the banner, calls, music and the camera send the right commands back
_, st = run("0.1:H1 0.2:L1", 0.6)
ok &= check("phone: pairing shows the code on top", st["screen"] == "pair", st)
_, st = run("0.1:H1 0.2:L1 0.5:P", 0.9)
ok &= check("phone: top button confirms the code -> pair_ok, back to Pip", st["phone"] == "pair_ok" and st["screen"] == "pip", st)
_, st = run("0.1:H1 0.2:L2 0.4:N", 0.8)
ok &= check("phone: a notification drops a banner over Pip", st["screen"] == "banner", st)
_, st = run("0.1:H1 0.2:L2 0.4:N", 7.0)
ok &= check("phone: the banner goes by itself after 6 s", st["screen"] == "pip", st)
_, st = run("0.1:H1 0.2:L2 0.4:N 0.8:P 1.3:P", 1.7)
ok &= check("phone: banner -> top opens it -> top again marks it read on the phone", st["phone"] == "note_dismiss" and st["screen"] == "notify", st)
_, st = run("0.1:H1 0.2:L2 0.4:N 0.5:N 0.6:N 0.8:onotify 1.2:B", 1.6)
ok &= check("phone: the notification list leaves with the bottom button", st["screen"] == "menu", st)
_, st = run("0.1:H0 0.2:L2 0.4:N 0.8:P 1.4:B 1.6:B", 2.2)
ok &= check("phone: a banner opened, then bottom twice quickly -> Pip", st["screen"] == "pip", st)
_, st = run("0.1:H0 0.2:L2 0.4:N 0.8:P 1.4:B 2.4:B", 3.0)
ok &= check("...twice slowly: the text, the list, then the menu", st["screen"] == "menu", st)
_, st = run("0.1:H1 0.2:L2 0.4:C1", 0.8)
ok &= check("phone: an incoming call takes the screen", st["screen"] == "call", st)
_, st = run("0.1:H1 0.2:L2 0.4:C1 0.7:B", 1.0)
ok &= check("phone: bottom button declines the call", st["phone"] == "call_decline" and st["screen"] == "pip", st)
_, st = run("0.1:H0 0.2:L2 0.4:C1", 40)
ok &= check("phone: a long ringing keeps the screen lit (past the 30 s sleep timer)", st["screen"] == "call" and st["display"] == "1", st)
_, st = run("0.1:H0 0.2:L2 0.4:C1 39:P", 39.3)
ok &= check("...and the top button still answers it at once", st["phone"] == "call_answer", st)
_, st = run("0.1:H1 0.2:L2 0.3:M1 0.4:omusic 0.8:P", 1.1)
ok &= check("phone: music, top button -> play_pause", st["phone"] == "play_pause" and st["screen"] == "music", st)
_, st = run("0.1:H1 0.2:L2 0.3:M1 0.4:omusic 0.8:d330,240 0.83:m230,240 0.86:m120,240 0.9:u", 1.2)
ok &= check("phone: music, swipe left -> next track", st["phone"] == "next", st)
_, st = run("0.1:H1 0.2:L2 0.3:M1 0.4:omusic 0.8:d233,200 0.9:m233,240 1.0:m233,280 1.1:m233,320 1.2:u", 1.5)
ok &= check("phone: music, drag down turns the volume down and stays in the app", st["phone"] == "vol_down" and st["screen"] == "music", st)
_, st = run("0.1:H1 0.2:L2 0.4:ocamera 0.8:P", 4.2)
ok &= check("phone: camera, top button, 3 s countdown -> one shot", st["shots"] == "1" and st["screen"] == "camera", st)
_, st = run("0.1:H1 0.2:L2 0.4:ocamera 0.8:P 1.5:B", 4.2)
ok &= check("phone: camera, bottom button cancels the countdown", st["shots"] == "0", st)
# an Android phone: no ANCS/AMS without an app, only the shutter key
_, st = run("0.1:H0 0.2:l1 0.5:P", 0.9)
ok &= check("android: pairs like the iPhone (the code, top confirms)", st["link"] == "android" and st["screen"] == "pip", st)
_, st = run("0.1:H0 0.2:l2 0.4:N", 0.9)
ok &= check("android: no notifications come (no banner)", st["screen"] == "pip", st)
_, st = run("0.1:H1 0.2:l2 0.4:omusic 0.8:P", 1.1)
ok &= check("android: music says not supported, the top button sends nothing (it is the way back)", st["phone"] == "-" and st["screen"] == "menu", st)
_, st = run("0.1:H1 0.2:l2 0.4:ocamera 0.8:P", 4.2)
ok &= check("android: the camera's shutter works", st["shots"] == "1", st)
a, _ = run("0.1:H0 0.2:t0 0.3:oclock", 0.8, "ck0")
b, _ = run("0.1:H0 0.3:oclock", 0.8, "ck1")
from PIL import ImageChops
ok &= check("clock: unset after a power-off -> it asks for a phone or the panel", ImageChops.difference(a, b).crop((90, 120, 376, 175)).getbbox() is not None, {})
c, _ = run("0.1:H0 0.2:t0 0.3:oclock 0.5:L2", 0.8, "ck2")
same = lambda x, y: sum(ImageChops.difference(x, y).crop((40, 40, 426, 426)).histogram()[24:]) < 400
ok &= check("clock: unset shows only the words, no dial", sum(1 for x in range(40, 426, 8) for y in (233,) if a.getpixel((x, 360)) > 60) == 0 and a.getpixel((233, 60)) < 40, {})
ok &= check("clock: an iPhone links -> its time, the dial is back", same(c, b), {})
d, _ = run("0.1:H0 0.2:t0 0.3:oclock 0.5:l2", 0.8, "ck3")
ok &= check("clock: an Android phone tells no time -> still unset", same(d, a), {})
_, st = run("0.1:H0 0.2:L2 0.3:obt", 0.8)
ok &= check("bluetooth page: the phone and the panel (no computer any more)", st["screen"] == "bt", st)
_, st = run("0.05:H0 0.1:L2 0.3:obt 0.6:d340,350 0.65:u", 1.0)
ok &= check("bluetooth: one tap on «забыть всё» only asks", st["link"] == "iphone", st)
_, st = run("0.05:H0 0.1:L2 0.3:obt 0.6:d340,350 0.65:u 1.0:d340,350 1.05:u", 1.4)
ok &= check("bluetooth: the second tap forgets: the phone is gone", st["link"] == "off", st)
for t in range(3):
    bad = []
    for a in ("notify", "music", "camera"):
        img, st = run(f"0.1:H{t} 0.15:L2 0.25:M1 0.3:o{a} 0.5:N", 0.9, "th")  # in the list app a new one lands in the list
        lit = sum(1 for y in range(0, 466, 6) for x in range(0, 466, 6) if abs(img.getpixel((x, y)) - img.getpixel((233, 8))) > 40)
        if st["screen"] != (a if a == "notify" else "banner") or lit < 20: bad.append((a, st["screen"], lit))
    ok &= check(f"theme {t}: the phone apps draw with a phone connected (banners over the others)", not bad, bad)

# voice: the synth itself, and Pip speaking through the real paths
r = subprocess.run([os.path.join(os.path.dirname(sim), "voice_test")], capture_output=True, text=True, encoding="utf-8", errors="replace")
ok &= check("voice: every sound audible, as loud as the rest, unclipped, ending, pitched right (voice_test)", r.returncode == 0, r.stderr.strip())
import wave, struct
def loudness(events, secs, windows):
    if "Kvary" not in events: events = "0:Kvary=0 " + events  # as run(): the classic answers
    if "Klife_gap" not in events: events = "0:Klife_gap=0 " + events
    if "Kcalm" not in events: events = "0:Kcalm=0 " + events
    if "Ktalk" not in events: events = "0:Ktalk=0 " + events  # his tales after events: their own checks turn them on
    if "Keveryday" not in events: events = "0:Keveryday=0 " + events  # the classic palettes: the everyday faces have their own checks
    if "Kzones" not in events: events = "0:Kzones=0 " + events  # cheeks, chin, sides, an eye held: their own checks
    path = os.path.join(tmp, "v.wav")
    subprocess.run([sim, "--out", os.path.join(tmp, "v.png"), "--wav", path, "--secs", str(secs), "--events", events], check=True, capture_output=True)
    w = wave.open(path); d = struct.unpack(f"<{w.getnframes()}h", w.readframes(w.getnframes()))
    return [(sum(x * x for x in d[int(a * 16000):int(b * 16000)]) / max(1, int((b - a) * 16000))) ** .5 for a, b in windows]
quiet, poked = loudness("0.5:d233,400 0.55:u", 1.0, [(0, .45), (.5, .8)])  # under the eyes (between them is his nose: a boop)
ok &= check("silent at rest, a poke says eep", quiet < 50 and poked > 1000, (quiet, poked))
rings = loudness("0.01:H0 0.2:otimer 0.4:d62,320 0.5:m85,350 0.6:m100,372 0.7:u 0.9:d179,325 0.95:u", 63, [(61.0, 62.5)])[0]
ok &= check("the timer sings when it rings", rings > 1000, rings)
set1 = "0.01:H0 0.2:otimer 0.4:d62,320 0.5:m85,350 0.6:m100,372 0.7:u 0.9:d179,325 0.95:u"  # a minute, started
again = loudness(set1, 70, [(63.5, 63.9), (65.5, 67.5), (67.9, 68.3), (68.0, 70)])
ok &= check("time's up: the song rings again and again until put away", again[1] > 1000 and again[3] > 1000, again)
stopped = loudness(set1 + " 64.5:P", 70, [(65.5, 70)])[0]
ok &= check("time's up: a press puts it away and the ringing stops", stopped < 50, stopped)
ticks = loudness("0.1:H1 0.2:P 0.6:d233,330 0.63:m233,280 0.66:m233,230 0.69:m233,180 0.7:u", 2.0, [(0.3, 0.55), (0.6, 1.4)])
ok &= check("the menu wheel clicks as rows pass, silent before", ticks[0] < 50 and ticks[1] > 300, ticks)
tap = loudness("0.1:H0 0.2:P 1.0:d233,233 1.05:u", 1.4, [(0.6, 0.95), (1.0, 1.15)])
ok &= check("a tap on the menu's middle row clicks as it opens", tap[0] < 50 and tap[1] > 300, tap)
curse = loudness("0:nswearing", 11, [(0, 0.5), (1.2, 4.8), (5.0, 5.5), (6.2, 9.8), (10.0, 10.5)])
ok &= check("swearing: one curse and its beep every 5 s, quiet between", min(curse[0::2]) > 1000 and max(curse[1::2]) < 50, curse)
# Звуки: a row's five segments (his voice at y 194..212, the menu's at y 304..322; the first segment x 87..139):
# a tap picks a level, the first segment again when it is the level turns it off
off2 = "0.5:d110,203 0.55:u 0.7:d110,203 0.75:u"
muted = loudness(f"0.01:H0 0.2:osound {off2} 1:B 1.2:B 2:d233,400 2.05:u", 2.6, [(2.05, 2.4)])[0]
ok &= check("Звуки Пипа at 0 mutes him", muted < 50, muted)
low = loudness("0.01:H0 0.2:osound 0.5:d110,203 0.55:u 1:B 1.2:B 2:d233,400 2.05:u", 2.6, [(2.05, 2.4)])[0]
full = loudness("0.01:H0 0.2:osound 1:B 1.2:B 2:d233,400 2.05:u", 2.6, [(2.05, 2.4)])[0]
ok &= check("Звуки Пипа at 1 of 5: quieter, still heard", 50 < low < full * 0.45, (low, full))
quiet = loudness("0.1:H1 0.2:osound 0.5:d110,313 0.55:u 0.7:d110,313 0.75:u 1:B 1.2:B 2.0:P 2.4:d233,330 2.43:m233,280 2.46:m233,230 2.49:m233,180 2.5:u", 3.4, [(2.4, 3.3)])[0]
ok &= check("Звуки меню at 0: the wheel turns silently", quiet < 50, quiet)

# eye colours (the panel's "eyes i"): Леди Баг paints the white red with a dark pupil; gloomy has no rain cloud any more
def rgb_at(events, secs, pick):
    path = os.path.join(tmp, "c.png")
    out = subprocess.run([sim, "--out", path, "--secs", str(secs), "--events", events], check=True, capture_output=True,
                         text=True, encoding="utf-8", errors="replace").stdout
    im = Image.open(path).convert("RGB")
    return ([im.getpixel((x, y)) for y in range(0, 466, 2) for x in range(0, 466, 2) if pick(*im.getpixel((x, y)))],
        dict(re.findall(r"(\w+)=(\S*)", out.splitlines()[-1])))
red, st = rgb_at("0.05:H0 0.1:I7", 1.5, lambda r, g, b: r > 150 and g < 60 and b < 60)
white, _ = rgb_at("0.05:H0 0.1:I7", 1.5, lambda r, g, b: min(r, g, b) > 200)
ok &= check("eye colours: Леди Баг is red, no white left", len(red) > 500 and len(white) < 20 and st["eyes"] == "7", (len(red), len(white), st))
std, st = rgb_at("0.05:H0 0.1:I7 0.2:I0", 1.5, lambda r, g, b: min(r, g, b) > 200)
ok &= check("eye colours: back to the standard white", len(std) > 500 and st["eyes"] == "0", (len(std), st))
def pet_line(events, secs):
    out = subprocess.run([sim, "--out", os.path.join(tmp, "p.png"), "--secs", str(secs), "--events", events], check=True,
                         capture_output=True, text=True, encoding="utf-8", errors="replace").stdout
    return dict(re.findall(r"(\w+)=([0-9.]+)", [l for l in out.splitlines() if l.startswith("pet:")][0])), \
        dict(re.findall(r"(\w+)=(\S*)", out.splitlines()[-1]))
easy, st0 = pet_line("0.05:Y0 0.2:T10", 0.5)
hard, st2 = pet_line("0.05:Y2 0.2:T10", 0.5)
ok &= check("care: Требовательный gets hungry, bored and cooped up faster than Неприхотливый",
            st0["care"] == "0" and st2["care"] == "2" and all(float(easy[k]) > float(hard[k]) + 0.2 for k in ("food", "fun", "walk")), (easy, hard))
nap, _ = pet_line("0.05:Y1 0.1:T8 40:T2", 40.5)
bed, _ = pet_line("0.05:Y1 0.1:T8 0.2:a0,0,-1 40:T2", 40.5)
ok &= check("a nap with the screen dark rests him much less than sleeping face down",
            float(bed["energy"]) > float(nap["energy"]) + 0.15, (nap["energy"], bed["energy"]))
green, st = rgb_at("0.05:I1 0.1:nnauseous", 2.5, lambda r, g, b: g > 180 and g > r + 15 and g > b + 40)
ok &= check("eye colours: nausea turns Бабл-гам green", len(green) > 500 and st["mood"] == "nauseous", len(green))
pink, st = rgb_at("0.05:I6 0.1:npinkeye", 2.5, lambda r, g, b: r > 220 and 100 < g < 170 and 100 < b < 170)
ok &= check("eye colours: pinkeye turns Лазурь pink", len(pink) > 500 and st["mood"] == "pinkeye", len(pink))
_, st = rgb_at("0:ngloomy", 2.5, lambda r, g, b: False)
im = Image.open(os.path.join(tmp, "c.png")).convert("RGB")
lit_top = sum(1 for y in range(40, 130, 2) for x in range(120, 346, 2) if max(im.getpixel((x, y))) > 40)
ok &= check("gloomy: no rain cloud over him", st["mood"] == "gloomy" and lit_top == 0, lit_top)

# dirt: no dust at all until clean drops under the line, then more and more of it on the glass, over the eyes too
diff = lambda a, b: sum(1 for p, q in zip(a.convert("L").get_flattened_data(), b.convert("L").get_flattened_data()) if abs(p - q) > 12)
still = "0.05:nneutral"
clean_img, _ = run(still, 0.6, "c1")
fair_img, st = run("0.04:c0.6 " + still, 0.6, "c06")
ok &= check("dirt: a little grubby (0.6) shows not a speck", diff(clean_img, fair_img) == 0 and st["clean"] == "0.60", diff(clean_img, fair_img))
dusty_img, _ = run("0.04:c0.4 " + still, 0.6, "c04")
filthy_img, st = run("0.04:c0 " + still, 0.6, "c0")
d1, d2 = diff(clean_img, dusty_img), diff(clean_img, filthy_img)
ok &= check("dirt: dust builds up as he gets grubbier", 0 < d1 < d2 and d2 > 1500, (d1, d2))
_, st = run("0.04:c0.25", 0.5)
ok &= check("dirt: grubby -> disgusted (a need)", st["base"] == "disgusted" and st["why"].startswith("4."), st)
_, st = run("0.04:c0.05 0.2:oxo", 0.6)
ok &= check("dirt: filthy, noughts and crosses with him is closed (with a friend only)", st["screen"] == "xo", st)
_, st = run("0.04:c0.05 0.2:opong", 0.6)
ok &= check("dirt: filthy, ping-pong with him -> the 'Пип грязный' note", st["screen"] == "tired", st)
# washing: on his face the bottom key tapped, then held (the top key opens the menu, as ever)
wash = lambda t, secs: f"{t}:B {t + 0.2:.2f}:B{secs}"
_, st = run("0.3:P", 0.8)
ok &= check("top key on Pip -> the menu", st["screen"] == "menu", st)
_, st = run("0.04:c0.1 " + wash(0.3, 3), 1.4)
ok &= check("bottom key tapped then held -> the sponge scrubs (he enjoys it)", st["washing"] == "1" and st["reaction"] == "scrub"
            and st["screen"] == "pip" and 0.1 < float(st["clean"]) < 1, st)
_, st = run("0.04:c0.1 0.3:B1", 1.9)  # fed at 0.9 s, the biscuit lands 0.55 s later
ok &= check("bottom key held alone still feeds him, no sponge", st["washing"] == "0" and st["reaction"] == "yum", st)
img_mid, _ = run("0.04:c0.1 " + wash(0.3, 3), 1.4, "wmid")
ok &= check("washing: a sponge and lather on screen", diff(img_mid, run("0.04:c0.1", 1.4, "wnone")[0]) > 3000, 0)
_, st = run("0.04:c0.1 " + wash(0.3, 3), 3.8)
ok &= check("washed clean -> fresh and glad (an aftertaste of it)", st["washing"] == "0" and st["reaction"] == "fresh"
            and st["clean"] == "1.00" and st["mood"] == "happy", st)
_, st = run("0.04:c0.1 " + wash(0.3, 3), 7.0)
ok &= check("washed: content for a while after", st["why"] == "3.10" and st["base"] == "content", st)
def dust_px(img):  # lit pixels in the bands over and under the eyes, where only dust ever is
    g = img.convert("L")
    return sum(1 for y in list(range(24, 92, 2)) + list(range(392, 444, 2)) for x in range(80, 386, 2)
               if (x - 233) ** 2 + (y - 233) ** 2 < 220 ** 2 and g.getpixel((x, y)) > 20)
ok &= check("dirt: filthy, dust over and under the eyes too", dust_px(filthy_img) > 15 and dust_px(clean_img) == 0, (dust_px(filthy_img), dust_px(clean_img)))
after_img, _ = run("0.04:c0 " + wash(0.3, 3.5), 7.0, "wafter")
ok &= check("washed: the dust is gone", dust_px(after_img) == 0, dust_px(after_img))
_, st = run(wash(0.3, 1.5), 1.4)
ok &= check("washing a clean Pip: he squirms, annoyed", st["reaction"] == "not" and st["mood"] in ("skeptical", "annoyed"), st)
_, st = run(wash(0.3, 2), 2.6)
ok &= check("washing a clean Pip: no joy after, indignant", st["reaction"] == "not" and st["mood"] == "indignant", st)
_, st = run(" ".join(wash(t, 1) for t in (0.3, 2.0)), 3.4)
ok &= check("washing him clean twice: angry", st["reaction"] == "not" and st["mood"] in ("angry", "furious"), st)
_, st = run(" ".join(wash(t, 1) for t in (0.3, 2.0, 3.6)), 4.7)  # the sponge comes at 4.1 s and is off again by 4.5
ok &= check("a third time he will not have it (as with food): no, the sponge flung off", st["reaction"] == "no"
            and st["mood"] == "no" and st["washing"] == "0", st)
_, st = run(wash(0.3, 8), 4.8)
ok &= check("scrubbed clean for long: boiling over (furious)", st["reaction"] == "not" and st["mood"] == "furious" and st["washing"] == "1", st)
_, st = run(wash(0.3, 8), 5.8)
ok &= check("scrubbed clean for ages: he flings the sponge away, the bath is over though the key is held", st["reaction"] == "sponge"
            and st["mood"] in ("furious", "rage") and st["washing"] == "0", st)
_, st = run("0.04:c0.1 " + wash(0.3, 10), 10.5)
ok &= check("washed dirty, then scrubbed on and on once clean: no joy, the sponge flies too", st["reaction"] == "sponge", st)
_, st = run("0.04:c0.95 " + " ".join(wash(t, 1) for t in (0.3, 2.0, 3.6)), 4.7)  # really clean (wash_clean 0.9)
ok &= check("really clean (over wash_clean): the third wash refused", st["reaction"] == "no" and st["washing"] == "0", st)
_, st = run(" ".join(wash(t, 1) for t in (0.3, 2.0)) + " 298:P " + wash(300.0, 1), 301.3)
ok &= check("washed clean twice, a third five minutes later: still refused", st["reaction"] == "no", st)
# the panel linked: "РЕЖИМ НАСТРОЙКИ" over all, keys, touches and motion do nothing until it lets go
img, st = run("0.3:X1 0.5:P 0.8:B1 1.0:d233,233 1.1:u 1.2:S", 2.0); img = img.convert("RGB")
ok &= check("panel linked: setup screen, no menu, no food, no shake", st["screen"] == "setup" and st["reaction"] == ""
            and max(pixels(img.crop((100, 170, 366, 260)))) > (200, 200, 200), st)
_, st = run("0.3:X1 0.6:X0 1.0:P", 2.0)
ok &= check("panel gone: the keys work again", st["screen"] == "menu", st)
_, st = run("0.3:X1 0.6:Z", 1.0)
ok &= check("flashing over the setup screen: the update notice takes over", st["screen"] == "pip", st)
# Настройки > Приватность: a row each, a tap puts it on
_, st = run("0.2:oprivacy 0.5:d233,131 0.55:u 0.8:N", 1.1)
ok &= check("privacy: Открыто chosen, the banner still comes", st["screen"] == "banner", st)
_, st = run("0.2:oprivacy 0.5:d233,255 0.55:u 0.8:N", 1.1)
ok &= check("privacy: Выключено -> notifications do not come", st["screen"] == "privacy", st)
# notifications (the fake phone sends one connected or not): a banner that slides away by itself
_, st = run("0.3:N", 0.8)
ok &= check("a notification -> the banner", st["screen"] == "banner", st)
_, st = run("0.3:N", 3.2)
ok &= check("the banner slides away by itself after ~2 s", st["screen"] == "pip", st)
_, st = run(" ".join(f"{0.3 + 0.1 * k:.1f}:N" for k in range(5)), 1.0)
ok &= check("notifications in a row: one banner, the newest", st["screen"] == "banner", st)# rare cards: each shows as the base mood; hay fever sneezes now and then
for card, i in (("allergic", 2), ("frozen", 3), ("smitten", 5), ("zombie", 8)):
    _, st = run(f"0.1:R{card}", 0.6)
    ok &= check(f"rare card {card} shows (why 1.{i})", st["base"] == card and st["why"] == f"1.{i}", st)
# a sneeze: on its own now and then, likelier with pokes; the face goes through it in ~1.4 s with the "a-a-a-CHOO"
_, st = run("0.3:kz", 0.9)
ok &= check("sneeze: the reaction and the build-up (wide eyes)", st["reaction"] == "sneeze", st)
_, st = run("0.3:kz", 2.2)
ok &= check("sneeze: over in a moment, he is himself again", st["reaction"] == "", st)
_, st = run("0.2:T13 0.4:kz", 1.0)
ok &= check("sneeze: never in his sleep", st["reaction"] != "sneeze" and st["asleep"] == "1", st)
_, st = run("0.3:ky", 1.0)
ok &= check("birthday on its own (key y): the song", st["reaction"] == "birthday" and st["mood"] == "excited", st)
_, st = run("0.3:kp", 1.0)
ok &= check("birthday on its own (key p): a burst of poppers", st["reaction"] == "festive", st)
_, st = run("0.3:kr", 1.0)
ok &= check("birthday on its own (key r): a rain of confetti", st["reaction"] == "festive", st)
loud = loudness("0.3:kz", 2.0, [(0.0, 0.25), (0.3, 1.2)])
ok &= check("sneeze: you hear it", loud[0] < 50 and loud[1] > 1000, loud)
pokes = " ".join(f"{0.3 + 0.7 * k:.1f}:d233,420 {0.33 + 0.7 * k:.2f}:u" for k in range(30))
seen = [run(pokes, 2.0 + 0.8 * k)[1]["reaction"] for k in range(26)]
ok &= check("sneeze: poked again and again, his nose tickles and he sneezes", "sneeze" in seen, seen)
# a burp: swells, "BUUERP", embarrassed; always when stuffed, now and then after a biscuit
_, st = run("0.3:kx", 1.0)
ok &= check("burp: the reaction", st["reaction"] == "burp", st)
_, st = run("0.3:kx", 2.4)
ok &= check("burp: over after the embarrassed moment", st["reaction"] == "", st)
loud = loudness("0.3:kx", 1.8, [(0.0, 0.6), (0.68, 1.1)])
ok &= check("burp: heard when he burps, not before", loud[0] < 50 and loud[1] > 1000, loud)
_, st = run("0.3:F 2.0:F", 4.3)  # the second biscuit leaves him stuffed: the burp follows
ok &= check("stuffed: a burp comes after", st["reaction"] == "burp", st)
# the panel's "Неловкие звуки": each switched off (pip_feature 11 sneeze, 12 burp, 13 toot), none at all
_, st = run("0.1:f11,0 0.3:kz", 0.9)
ok &= check("sneezes switched off: none", st["reaction"] != "sneeze", st)
_, st = run("0.1:f12,0 0.3:F 2.0:F", 4.3)
ok &= check("burps switched off: stuffed, still none", st["reaction"] != "burp", st)
_, st = run("0.1:f13,0 0.3:kt", 1.0)
ok &= check("toots switched off: none", st["reaction"] != "toot", st)
_, st = run("0.3:kt", 1.0)
ok &= check("toot: on by default", st["reaction"] == "toot", st)
# grubby he burps more: on his own now and then (burp_min), clean never
burping = lambda c: sum(run(f"0.05:H0 0.1:Kburp_min=0.03 0.15:c{c}", 4 + t / 2)[1]["reaction"] == "burp" for t in range(17))
n = burping(0)
ok &= check("filthy: he burps on his own (burp_min 2 s)", n >= 3, n)
n = burping(1)
ok &= check("clean: never on his own", n == 0, n)
# stroking: one giggle as it starts, not a giggle a second
quiet = loudness("0.15:d150,233 " + stroke, 2.4, [(0.25, 0.9), (1.3, 2.4)])
ok &= check("petting: he giggles once, then just enjoys it", quiet[0] > 1000 and quiet[1] < 50, quiet)
sneezes = loudness("0.1:Rallergic", 11, [(0.2, 1.8), (2.0, 11)])
ok &= check("allergic: he sneezes now and then", sneezes[0] < 50 and sneezes[1] > 1000, sneezes)

# the newer feelings, each from its own input
def strokes(t0, dur, x0, x1, y, amp=None, step=0.03):
    """a finger down at x0, then rubbing: back and forth between x0 and x1, or (amp) in a small patch round x0"""
    ev, t, i = [f"{t0:.2f}:d{x0},{y}"], t0, 0
    while t < t0 + dur:
        t, i = t + step, i + 1
        x = (x0 if i % 2 == 0 else x1) if amp is None else x0 + (amp if i % 2 else -amp)
        ev.append(f"{t:.2f}:m{x},{y + (i % 3) * 3}")
    return " ".join(ev) + f" {t + step:.2f}:u", t + step
swing = "0.2:a0,1,0 " + " ".join(f"{0.5 + i * 0.55:.2f}:a{-0.5 if i % 2 else 0.5},0.87,0" for i in range(8))
tease, t = [], 0.3
for _ in range(3):
    e, t = strokes(t, 0.5, 120, 340, 300)
    tease.append(e)
    t += 0.6
feelings = [
    ("swung side to side: excited", swing, 5.0, "excited", "swing"),
    ("a finger resting on him: dreamy", "0.3:d233,300", 3.4, "dreamy", "warm"),
    ("a finger moving slowly that he follows: focused", "0.3:d100,300 " + " ".join(f"{0.3 + i * 0.1:.1f}:m{100 + i * 9},{300 - i * 2}" for i in range(1, 30)), 3.2, "focused", "tracking"),
    ("rubbed in one small patch: tickled, laughing", strokes(0.3, 1.6, 233, 0, 300, amp=28)[0], 1.9, "laughing", "tickle"),
    ("stroked for a long time: touched", strokes(0.3, 8.0, 120, 340, 300)[0], 8.4, "touched", "touched"),
    ("strokes broken off three times: teased, pouty", " ".join(tease), t + 0.2, "pouty", "teased"),
    ("face down a second and a half and back: peekaboo, playful", "0.2:a0,1,0 0.5:a0,0,-1 2.2:a0,1,0", 2.7, "playful", "peekaboo"),
    ("upside down a while, then upright: relieved", "0.2:a0,1,0 0.5:a0,-1,0 4.0:a0,1,0", 4.3, "relieved", "phew"),
    ("fed when hungry: grateful", "0.1:Khungry_line=0.9 0.3:B1", 2.6, "grateful", "thanks"),
]
for name, ev, secs, mood, reaction in feelings:
    _, st = run("0.05:H0 " + ev, secs)
    ok &= check(name, st["mood"] == mood and st["reaction"] == reaction, st)
_, st = run("0.05:H0 " + strokes(0.3, 2.0, 120, 340, 300)[0], 2.3)
ok &= check("stroked across (not one patch): loved, not tickled", st["reaction"] in ("pet", "love"), st)
_, st = run("0.05:H0 0.2:a0,1,0 0.5:a0,0,-1 3.0:a0,1,0", 3.4)
ok &= check("face down over two seconds: bedtime, not peekaboo (woken up)", st["reaction"] == "woke", st)
_, st = run("0.05:H0 0.2:a0,1,0 0.5:a0,0,-1 0.62:a0,1,0", 1.0)
ok &= check("face down only a flicker: no peekaboo", st["reaction"] != "peekaboo", st)
_, st = run("0.05:H0 0.2:a0,1,0 0.5:a0,-1,0 1.9:a0,1,0", 2.2)
ok &= check("upside down only a moment: no relief to show", st["reaction"] == "", st)
_, st = run("0.05:H0 0.3:B1", 2.6)
ok &= check("fed when not hungry: just yum", st["reaction"] == "yum", st)
loud = loudness("0.05:H0 " + swing, 5.0, [(0.0, 0.5), (2.6, 4.0)])
ok &= check("excited: you hear the whee", loud[0] < 50 and loud[1] > 1000, loud)
# ---- being alive (pip.c life): new gestures, breath, rocking, a wet Pip, illness, varied answers, moments of his own
_, st = run("0.05:H0 0.3:d233,233 0.35:u", 0.8)
ok &= check("a tap right between the eyes: a boop on the nose", st["reaction"] == "boop", st)
snort = loudness("0.05:H0 0.3:d233,233 0.35:u", 1.4, [(0.0, 0.28), (0.9, 1.25)])
ok &= check("...he snorts", snort[0] < 50 and snort[1] > 1000, snort)
lids = lambda t0, y0, y1: f"{t0}:d233,{y0} " + " ".join(f"{t0 + 0.07 * i:.2f}:m233,{y0 + (y1 - y0) * i // 10}" for i in range(1, 11)) + f" {t0 + 0.75:.2f}:u"
_, st = run("0.05:H0 0.1:y0.15 " + lids(0.3, 90, 380), 2.5)
ok &= check("worn out, a finger drawn slowly down over his eyes: they close, he sleeps", st["asleep"] == "1", st)
_, st = run("0.05:H0 0.1:y0.95 " + lids(0.3, 90, 380), 1.2)
ok &= check("...rested: he pops them open again", st["asleep"] == "0" and st["reaction"] == "eyes:", st)
_, st = run("0.05:H0 0.2:T13 " + lids(1.0, 380, 90), 2.2)
ok &= check("asleep, drawn up over his eyes: he prises them open and grumbles", st["asleep"] == "0" and st["reaction"] == "eyes", st)
_, st = run("0.05:H0 " + strokes(0.3, 2.0, 150, 320, 95)[0], 2.0)
ok &= check("stroked along the top of his head: a head pat", st["reaction"] == "head", st)
_, st = run("0.05:H0 0.3:d150,300 0.35:m250,300 0.4:m330,300 0.45:u", 0.8)
ok &= check("a quick flick across him: startled", st["reaction"] == "flick" and st["mood"] == "surprised", st)
def spiral(t0, turns, r0, r1, dur, d=1, wobble=0, step=0.03):
    """a finger drawn round (d -1: the other way) from radius r0 to r1 about the middle; wobble: a hand's uneven circle"""
    n, ev = int(dur / step), []
    for i in range(n + 1):
        f = i / n
        a, r = d * 2 * math.pi * turns * f, (r0 + (r1 - r0) * f) * (1 + wobble * math.sin(i * 0.7))
        ev.append(f"{t0 + i * step:.2f}:{'d' if i == 0 else 'm'}{int(233 + r * math.cos(a))},{int(250 + r * math.sin(a))}")
    return " ".join(ev), t0 + dur
for name, (ev, t) in [("a spiral drawn outwards on him: whirled, his pupils ride round with it", spiral(0.3, 2.5, 25, 170, 2.0)),
                      ("...inwards, the other way round", spiral(0.3, 2.5, 170, 25, 2.0, d=-1)),
                      ("...slowly", spiral(0.3, 2.5, 25, 170, 4.0)),
                      ("...a hand's uneven one", spiral(0.3, 2.5, 25, 170, 2.0, wobble=0.12)),
                      ("round and round in a circle (two turns) counts too", spiral(0.3, 3, 100, 100, 2.4)),
                      ("...a hand's uneven circle", spiral(0.3, 3, 100, 100, 2.7, wobble=0.2)),
                      ("...a small slow one", spiral(0.3, 4, 30, 30, 2.0))]:
    _, st = run("0.05:H0 " + ev, t + 0.1)
    ok &= check(name, st["reaction"] == "spiral" and st["mood"] == "whirled", st)
for name, (ev, t) in [("a turn and a half round a circle: still a stroke", spiral(0.3, 1.5, 100, 100, 1.2)),
                      ("under a turn and a half of a spiral: nothing yet", spiral(0.3, 1.2, 25, 170, 1.0)),
                      ("a tiny circle rubbed round one patch: tickling, no spiral", spiral(0.3, 4, 12, 12, 2.0))]:
    _, st = run("0.05:H0 " + ev, t + 0.1)
    ok &= check(name, not st["reaction"].startswith("spiral"), st)
ev, t = spiral(0.3, 4, 110, 110, 3.0)
_, st = run("0.05:H0 " + ev + f" {t + 0.03:.2f}:u", t + 1.0)
ok &= check("...let go after a good spin: woozy, his eyes go on round by themselves", st["reaction"] == "spun" and st["mood"] == "woozy", st)
ev, t = spiral(0.3, 2.5, 100, 100, 1.9)
_, st = run("0.05:H0 " + ev + f" {t + 0.03:.2f}:u", t + 0.6)
ok &= check("...let go as soon as he noticed: no after-effect", st["reaction"] == "spiral", st)
evs, t, ends = [], 0.3, []
for k in range(8):
    e, t = spiral(t, 2.2, 160, 25, 1.8) if k % 3 == 0 else spiral(t, 2.2, 25, 160, 1.8, d=1 if k % 2 else -1)
    evs.append(e + f" {t + 0.05:.2f}:u"), ends.append(t)
    t += 2.5
seen = {run("0:Kvary=1 0.05:H0 " + " ".join(evs[:k + 1]), e + 0.1)[1]["mood"] for k, e in enumerate(ends)}
ok &= check("spirals again and again: not the same answer every time", len(seen) >= 3, seen)
import math
rock = " ".join(f"{t / 10:.1f}:a{0.38 * math.sin(2 * math.pi * t / 24):.3f},{math.sqrt(1 - (0.38 * math.sin(2 * math.pi * t / 24)) ** 2):.3f},0" for t in range(2, 110))
_, st = run("0.05:H0 0.1:y0.2 0.15:Krock_s=4 " + rock, 11)
ok &= check("worn out and rocked gently: a lullaby, and he falls asleep", st["asleep"] == "1", st)
_, st = run("0.05:H0 0.1:y0.95 " + rock, 11)
ok &= check("...rested: he will not sleep, rocked a while he is bored of it", st["asleep"] == "0" and st["reaction"] == "rocked:", st)
quick_rock = " ".join(f"{t / 20:.2f}:a{0.3 * math.sin(2 * math.pi * t / 20):.3f},{math.sqrt(1 - (0.3 * math.sin(2 * math.pi * t / 20)) ** 2):.3f},0" for t in range(10, 160))  # 0.5 s a side
_, st = run("0.05:H0 0.1:y0.3 " + quick_rock, 5)
ok &= check("rocked at a hand's own pace (0.5 s a side): a tired one takes it for a lullaby", st["reaction"] == "rocked", st)
_, st = run("0.05:H0 0.1:y0.9 " + quick_rock, 5)
ok &= check("...a lively one for a swing", st["reaction"] == "swing", st)
_, st = run("0.05:H0 0.1:y0.7 0.15:Krock_s=4 " + rock, 16)
ok &= check("lulled off by day at 70%: he sleeps on, not up again at once", st["asleep"] == "1", st)
_, st = run("0.05:H0 0.1:y0.7 0.15:Krock_s=4 " + rock + " 12:T0.5", 14)
ok &= check("...and half an hour on he is up", st["asleep"] == "0", st)
lull = loudness("0.05:H0 0.1:y0.2 " + rock, 7, [(0.0, 1.5), (4.5, 7.0)])
ok &= check("...you hear the lullaby", lull[0] < 50 and lull[1] > 700, lull)
_, st = run("0.05:H0 0.1:c0.3 0.3:B 0.5:B3 3.6:V35 3.7:Q", 5.0)
ok &= check("washed, then blown on while wet: drying him, he loves it", st["reaction"] == "drying" and st["mood"] == "content", st)
breaths = "0.05:H0 0.1:V35 0.3:Q 2.8:Q 3.8:Q 6.3:Q 7.3:Q 9.8:Q 10.8:Q 13.3:Q 14.3:Q"  # 2.5 s a breath, 1 s to breathe in
_, st = run(breaths, 3.3)
ok &= check("blowing, a pause to breathe in: his face holds", st["reaction"] == "blown", st)
_, st = run(breaths, 5.0)
ok &= check("...a few seconds of it: he has had enough (or it frightens him)", st["reaction"] == "blown:" and st["mood"] == "annoyed", st)
_, st = run(breaths, 9.0)
ok &= check("...then he feels chilly (shivers)", st["reaction"] == "blown:" and st["mood"] == "nervous", st)
_, st = run(breaths, 14.8)
ok &= check("...breaths and all, ~10 s of it: cold", st["reaction"] == "cold", st)
_, st = run("0.05:H0 0.1:V35 0.3:Q", 9.0)
ok &= check("blown on 9 s: not cold yet (cold_s 10)", st["reaction"] != "cold", st)
_, st = run("0.05:H0 0.1:V35 0.3:Q", 11.0)
ok &= check("blown on past 10 s: cold (frozen, brrr)", st["reaction"] == "cold" and st["mood"] == "frozen", st)
_, st = run("0.05:H0 0.1:c0.3 0.3:B 0.5:B3 3.6:V35", 4.0)
ok &= check("just washed: soaked", float(st["wet"]) > 0.9, st)
_, st = run("0.05:H0 0.1:c0.3 0.3:B 0.5:B3 3.6:V35 3.7:Q", 6.9)
ok &= check("...blown on ~3 s: dry, no more drips", float(st["wet"]) == 0, st)
_, st = run("0.05:H0 0.1:c0.3 0.3:B 0.5:B3 3.6:V35", 30)
ok &= check("...not blown on: still damp half a minute later (he dries on his own in dry_s)", float(st["wet"]) > 0.5, st)
_, st = run("0.05:H0 0.1:c0.3 0.3:B 0.5:B3 3.6:V35 3.7:Q", 10.5)
ok &= check("wet from the shower: drying first, then cold sooner (6 s)", st["reaction"] == "cold", st)
_, st = run("0:Kcalm=1 0.05:H0 0.06:s0 30:c0.05", 40)
ok &= check("no reason for it (grubby by degrees): his face gets there by steps, not at once", st["base"] == "gloomy" and st["mood"] != "gloomy", st)
_, st = run("0:Kcalm=1 0.05:H0 0.06:s0 30:c0.05", 75)
ok &= check("...and there in the end", st["mood"] == "gloomy", st)
_, st = run("0:Kcalm=1 0.05:H0 0.06:s0 30:c0.05 70:p1", 71.5)
ok &= check("...a reason (met with a button) shows at once", st["reaction"] != "", st)
_, st = run("0.05:H0 0.1:c0.05 0.2:T6", 1.0)
ok &= check("left filthy for hours: ill (why 2.5)", st["why"] == "2.5" and st["base"] in ("sick", "nauseous"), st)
_, st = run("0.05:H0 0.1:c0.05 0.2:T6 0.3:c1 0.4:T5", 1.0)
ok &= check("...washed, a few hours on: well again", st["why"] != "2.5", st)
POKES = [0.3 + 1.5 * i for i in range(8)]
seen = {run("0:Kvary=1 0.01:Ksneeze_poke=0 0.05:H0 " + " ".join(f"{t:.1f}:d233,400 {t + 0.05:.2f}:u" for t in POKES[:k + 1]), POKES[k] + 0.2)[1]["mood"]
        for k in range(8)}
ok &= check("varied: the same poke gets different faces", len(seen) >= 3, seen)
seen = {run("0:s0 0:Klife_gap=4 0:Kvary=1 0.05:H0", t)[1]["reaction"] for t in range(8, 64, 2)}
ok &= check("left alone, he does things of his own (different ones)", len(seen - {""}) >= 2, seen)
# the 8 ball: tapped, it hums while it thinks; the answer rises into the window with a click (0.55 of its 1.8 s in)
loud = loudness("0.05:H0 0.2:oeight 0.4:d233,233 0.45:u", 2.2, [(0.85, 1.35), (1.38, 1.5)])
ok &= check("8 ball: a click as the answer comes up, quiet before", loud[0] < 50 and loud[1] > 300, loud)
# music: a beat that keeps time (pipsim U<bpm>: kick, snare, hats, bass) and he dances; talk (D) or plain noise has no beat
for bpm in (80, 120, 160):
    _, st = run(f"0.05:H0 0.1:U{bpm}", 8.0)
    ok &= check(f"music at {bpm}: he dances, the tempo heard right", st["reaction"] == "dancing" and abs(float(st["bpm"]) - bpm) < bpm * 0.05, st)
_, st = run("0.05:H0 0.1:D", 14.0)
ok &= check("talking nearby: no beat, no dance", st["reaction"] != "dancing" and st["music"] == "0.00", st)
_, st = run("0.05:H0 0.1:V60 0.2:D", 14.0)
ok &= check("a noisy room and talking: no dance", st["reaction"] != "dancing" and st["music"] == "0.00", st)
_, st = run("0.05:H0 0.1:U120 9:U0", 16.0)
ok &= check("the song ends: so does the dance", st["reaction"] != "dancing" and st["music"] == "0.00", st)
_, st = run("0.05:H0 0.1:U90r", 9.0)
ok &= check("hip-hop (syncopated kicks with gaps, half-time snare, chords): heard as music, he dances", st["reaction"] == "dancing", st)
# his mood decides: cheerful he always dances, sad or grubby-gloomy he does not, in between it is a chance (dance_will)
_, st = run("0.05:H0 0.1:nsad 0.2:U120", 8.0)
ok &= check("sad: the music plays, he does not dance", st["reaction"] != "dancing" and st["music"] != "0.00", st)
_, st = run("0.05:H0 0.1:c0 0.2:U120", 8.0)
ok &= check("grubby and gloomy: no dance either", st["mood"] == "gloomy" and st["reaction"] != "dancing", st)
_, st = run("0.05:H0 0.1:nhappy 0.2:U120", 8.0)
ok &= check("happy: he dances", st["reaction"] == "dancing", st)
_, st = run("0.05:H0 0.1:Kdance_will=-2 0.15:nneutral 0.2:U120", 8.0)
ok &= check("neutral and not in the mood at all (dance_will -2): no dance", st["reaction"] != "dancing", st)
_, st = run("0.05:H0 0.1:Kdance_will=2 0.15:nneutral 0.2:U120", 8.0)
ok &= check("...glad to (dance_will 2): he dances", st["reaction"] == "dancing", st)
# not for ever: a bout of dance_s..1.6x s, "danced out", a rest of dance_rest..2x s, then again while it plays
_, st = run("0.05:H0 0.1:Kdance_s=3 0.15:Kdance_rest=60 0.2:U120", 12.0)
ok &= check("a bout of dancing ends: he rests though the song plays on", st["reaction"] != "dancing" and st["music"] != "0.00", st)
_, st = run("0.05:H0 0.1:Kdance_s=3 0.15:Kdance_rest=60 0.2:U120", 9.0)
ok &= check("...with \"danced out\"", st["reaction"] == "danced" and st["mood"] == "relieved", st)
_, st = run("0.05:H0 0.1:Kdance_s=3 0.15:Kdance_rest=2 0.2:U120", 12.0)
ok &= check("rested: he dances again", st["reaction"] == "dancing", st)
# disturbed (a touch, a button) he stops at once and leaves it for dance_break s
_, st = run("0.05:H0 0.2:U120 7:d233,300 7.1:u", 10.0)
ok &= check("touched mid-dance: he stops", st["reaction"] != "dancing" and st["music"] != "0.00", st)
_, st = run("0.05:H0 0.2:U120 7:B", 12.0)
ok &= check("a button mid-dance: he stops", st["reaction"] != "dancing" and st["music"] != "0.00", st)
_, st = run("0.05:H0 0.1:Kdance_break=3 0.2:U120 7:d233,300 7.1:u", 13.0)
ok &= check("after the break he dances again", st["reaction"] == "dancing", st)
# the linked phone playing music (AMS): he cannot hear it, he dances anyway (his own tempo), even saving energy
_, st = run("0.05:H0 0.1:L2 0.2:M1", 3.0)
ok &= check("the phone plays music: he dances to it", st["reaction"] == "dancing", st)
_, st = run("0.05:H0 0.1:Kdance_s=60 0.2:L2 0.3:M1", 33.0)
ok &= check("the phone's music holds nothing: left alone he sleeps on the 30 s timer mid-dance", st["display"] == "0", st)
_, st = run("0.05:H0 0.1:Kdance_s=60 0.2:U120", 33.0)
ok &= check("...the mic's music neither", st["display"] == "0", st)
_, st = run("0.05:H0 0.1:L2 0.2:M1 3:M0", 35.0)
ok &= check("the music stopped: the screen sleeps as usual", st["display"] == "0", st)
_, st = run("0.05:H0 0.1:L2 0.2:M1 0.3:E1", 3.0)
ok &= check("...not in energy saving: no music at all", st["reaction"] != "dancing", st)
_, st = run("0.05:H0 0.1:f7,0 0.2:L2 0.3:M1", 3.0)
ok &= check("the panel's Музыка off: not to the phone's either", st["reaction"] != "dancing", st)
_, st = run("0.05:H0 0.1:L2 0.2:M1 2:M0", 4.0)
ok &= check("the phone's music stops: so does he", st["reaction"] != "dancing", st)
_, st = run("0.05:H0 0.1:L2 0.2:M1 1.5:d233,300 1.6:u", 5.0)
ok &= check("the phone's music, touched: he stops for a while", st["reaction"] != "dancing", st)
_, st = run("0.05:H0 0.1:E1 0.2:U120", 8.0)
ok &= check("energy saving (no listening): no dance", st["reaction"] != "dancing", st)
_, st = run("0.05:H0 0.1:T13 0.2:U120", 8.0)
ok &= check("asleep: no dance", st["reaction"] != "dancing", st)
# a beat on the bottom key, then stroking: the rhythm game ends and the two never take turns restarting their sounds
# (that played as a crackle: dozens of cut-off starts)
_, st = run("0.05:H0 0.5:B 1.1:B 1.7:B 2.3:B " + strokes(2.9, 3.1, 120, 340, 300)[0], 6.3)
ok &= check("a beat, then stroking: no crackle of restarted sounds", int(st["voices"]) < 10 and st["reaction"] in ("pet", "love"), st)

names = subprocess.run([sim, "--list"], check=True, capture_output=True, text=True, encoding="utf-8", errors="replace").stdout.split()
ok &= check(f"{len(names)} moods, all with a table entry", len(names) >= 40, names)
for n in names:  # every mood shows up as itself and draws eyes (asleep is a thin line, still bright)
    img, st = run(f"0:n{n}", 2.0, "m")
    ok &= check(f"mood {n} renders", st["mood"] == n and eyes(img)[2] > 0, st)

# ---- his voice: tales after what happened, a budget for his sounds, faces that hold their moment ----
def traced(events, secs):
    """the faces he showed, as --trace prints them (time, face, reaction), and the last status"""
    out = subprocess.run([sim, "--trace", "--out", os.path.join(tmp, "t.png"), "--secs", str(secs), "--events",
                          "0:Klife_gap=0 0:Kcalm=0 0:Kzones=0 0.05:H0 " + events], check=True, capture_output=True, text=True,
                         encoding="utf-8", errors="replace", timeout=300).stdout.splitlines()
    tr = [l.split(None, 4)[1:4] for l in out if l.startswith("trace")]
    return tr, dict(re.findall(r"(\w+)=(\S*)", out[-1]))
tr, st = traced("0:Kvary=0 0.04:c0.7 " + wash(0.3, 1.6), 4.5)  # no dust showing, but not spotless: a bath he enjoys
ok &= check("a little dusty (0.7, no dust on the glass): enjoys the bath, glad once clean, fresh after - not 'not dirty'",
            not [r for r in tr if r[2] == "not"] and any(r[2] == "scrub:" for r in tr) and any(r[2] == "fresh" for r in tr), tr)
shakes =" ".join(f"{0.5 + k * 9:.1f}:S {1.6 + k * 9:.1f}:S" for k in range(8))
tr, _ = traced(shakes, 73)
told = [r for r in tr if r[2] == "tells"]
ok &= check("shaken eight times: now and then he tells about it after (not every time)", 2 <= len(told) <= 7, told)
ok &= check("...not always with the same face", len({r[1] for r in told}) >= 2, told)
tr, _ = traced("0:Ktalk=0 " + shakes, 73)
ok &= check("...talk 0: never", not [r for r in tr if r[2] == "tells"], tr)
# tuned on a hand shaking the board (imutrace): light 0.25..0.35 of imu energy, middling 0.3..0.6, hard 1.7..2, wild ~4.5
for ev, what in (("1:S4y 4:S0", "up and down"), ("1:S6 3:S0", "side to side"), ("1:S9 4:S0", "wild")):
    tr, _ = traced("0:Kvary=1 " + ev, 6)  # hard: dizzy at once, never glad (it was a giddy tumble or a swing's whee)
    faces = [r[1] for r in tr if 1 <= float(r[0]) <= 4.5]
    ok &= check(f"shaken hard ({what}): dizzy", "dizzy" in faces, tr)
    ok &= check(f"shaken hard ({what}): no tumble, no swing, no glee", not set(faces) & {"tumbling", "laughing", "happy", "excited"}, tr)
tr, _ = traced("0:Kvary=1 1:S1.4 4:S0", 6)
faces = [r[1] for r in tr if 1 <= float(r[0]) <= 4.5]
ok &= check("a middling shake kept up: dizzy in the end (glad at first, maybe)", "dizzy" in faces and faces[-1] == "dizzy", tr)
tr, _ = traced("0:Kvary=1 1:S0.9 2.6:S0", 5)
faces = [r[1] for r in tr if 1 <= float(r[0]) <= 4.5]
ok &= check("a light short shake: fun, not dizzy", "dizzy" not in faces and bool(set(faces) & {"happy", "excited", "laughing", "juggler"}), tr)
tr, _ = traced("0:Kvary=1 0:V40 1:S9 1:Q2 4:S0 4:Q", 6)
ok &= check("a wild shake rattling the mic: no breath heard in it", not [r for r in tr if r[2].startswith("blown")], tr)
at = []
for sh in ("0.5:S 1.6:S", "0.5:S 1.4:S", "0.5:S4 1.4:S0", "0.7:S 1.8:S", "0.5:S3y 1.5:S0"):  # a shake he tells about after
    tr, _ = traced(sh, 7)
    at = [float(r[0]) for r in tr if r[2] == "tells"]
    if at:
        break
if at:  # a poke while he is telling: it is answered at once, the tale is over
    tr, _ = traced(f"{sh} {at[0] + 0.3:.2f}:d233,400 {at[0] + 0.35:.2f}:u", at[0] + 1.5)
    after = [r for r in tr if float(r[0]) >= at[0] + 0.3]
    ok &= check("a tale cut short by a poke: the poke is answered at once", bool(after) and after[0][2] in ("poke", "poke:", "clowns", "rolls", "brr", "brrr"), tr)
else:
    ok &= check("a tale cut short by a poke: (no tale to cut short)", False, tr)
import random
random.seed(3)
mix, t = [], 0.5
while t < 15:  # a busy quarter of a minute: pokes, shakes, a breath, the bottom key, strokes
    k = random.choice("pppsbBw")
    if k == "p": mix.append(f"{t:.2f}:d{random.choice(['233,400', '150,240', '320,240'])} {t + 0.05:.2f}:u"); t += random.uniform(0.25, 0.9)
    elif k == "s": mix.append(f"{t:.2f}:S4 {t + 0.8:.2f}:S0"); t += 1.2  # a hard shake: dizzy every time
    elif k == "b": mix.append(f"{t:.2f}:Q {t + 1.2:.2f}:Q"); t += 1.6
    elif k == "B": mix.append(f"{t:.2f}:B"); t += 0.4
    else: mix.append(strokes(t, 1.2, 150, 340, 300)[0]); t += 1.6
mix = "0:V40 " + " ".join(mix)
_, old = run("0:Kvary=1 0:Ksnd_refill=0 " + mix, 17)
_, new = run("0:Kvary=1 " + mix, 17)
ok &= check("a busy quarter minute: fewer sounds than one a feeling (his voice's budget, sounds of one thing joined)",
            int(new["voices"]) < int(old["voices"]), (old["voices"], new["voices"]))
pokes = " ".join(f"{t}:d233,400 {t}.05:u" for t in range(10, 180, 25))
_, alone = run("0:Kvary=1 0:Klife_gap=55 0:Kcalm=1 0:Ktalk=1 0:s0", 180)
_, company = run("0:Kvary=1 0:Klife_gap=55 0:Kcalm=1 0:Ktalk=1 0:s0 " + pokes, 180)
ok &= check("left alone three minutes: he keeps mostly quiet", int(alone["voices"]) <= 4, alone["voices"])
tr, _ = traced("0:Kvary=1 0:Klife_gap=55 0:Kcalm=1 0:Ktalk=1 0:s0 " + pokes, 180)
said = [r for r in tr if r[2] in ("tells", "chats")]
ok &= check("...with you about (a poke now and then): he talks to you, a few times", 2 <= len(said) <= 12, said)
# ---- trust, jealousy, a bad night, the finger chase, death ----
CHASE = "0.05:H0 0.5:d150,233 0.6:m200,200 1:m300,250 1.5:m200,300 2:m300,200 2.5:m180,250 3:m300,300 3.5:m200,200 4:m320,250 4.5:m180,220 5:m300,280 5.5:m200,200 6:m300,250 6.5:m200,300"
_, st = run(CHASE, 8)
ok &= check("a finger chased about for 4.5 s: a game, he chases it", st["reaction"] == "chase" and st["mood"] == "playful", st)
_, st = run(CHASE.split(" 5:")[0], 4.2)
ok &= check("...not yet at 4 s: he just follows it", st["reaction"] != "chase", st)
_, st = run(CHASE + " 8:u", 9)
ok &= check("...let go mid-chase: he got you", st["reaction"] == "caught", st)
TAPS = [0.3 + 3.4 * i for i in range(6)]
taps = lambda q, upto: f"0.05:H0 0.1:q{q} " + " ".join(f"{t:.1f}:d233,233 {t + 0.6:.1f}:u" for t in TAPS[:upto])  # held: a tap's release would poke
n = sum(run(taps(0, i + 1), TAPS[i] + 0.3)[1]["reaction"] == "flinch" for i in range(6))
ok &= check("trust: a timid one flinches from a finger, now and then", 1 <= n <= 5, n)
n = sum(run(taps(0.6, i + 1), TAPS[i] + 0.3)[1]["reaction"] == "flinch" for i in range(6))
ok &= check("...a sure one never", n == 0, n)
_, st = run("0.05:H0 0:a0,0,1 1:a3,0,0 1.05:a0,0,0 1.4:a0,0,0 1.6:a0,0,1", 3)
ok &= check("dropped: trust drops at once", float(st["trust"]) < 0.495, st)
_, st = run("0.05:H0 0.3:d233,300 0.4:m300,300 0.5:m233,300 0.6:m300,300 0.7:m233,300 0.8:m300,300 0.9:m233,300 1:m300,300 1.1:m233,300 1.2:m300,300 1.3:m233,300 1.4:m300,300 1.5:m233,300 1.6:m300,300 1.7:m233,300 1.8:m300,300 1.9:m233,300 2:m300,300 2.1:u", 2.5)
ok &= check("stroked: trust grows, a little", 0.5 < float(st["trust"]) < 0.531, st)
_, st = run("0.1:H0 0.15:Kjealous_s=15 0.2:Kjealous_chance=1 0.25:f7,0 0.3:L2 0.4:M1", 25)
ok &= check("jealous: the phone's music and no look for him, he sulks", st["mood"] == "pouty" and st["why"] == "3.14", st)
_, st = run("0.1:H0 0.15:Kjealous_s=15 0.2:Kjealous_chance=1 0.25:f7,0 0.3:L2 0.4:M1 10:d233,233 10.1:u 20:d233,233 20.1:u", 25)
ok &= check("...a touch now and then for him: no jealousy", st["why"] != "3.14", st)
NIGHT = "0.1:H0 0.2:L2 0.3:T12 0.5:a0,0,-1 3:N 3.3:N 3.6:N 3.9:N 4.2:N 4.5:N 5:T8 6:a0,0,1"
_, st = run(NIGHT, 8)
ok &= check("a night of notifications: he wakes up angry", st["mood"] == "angry" and st["why"] == "3.15", st)
_, st = run("0.1:H0 0.2:L2 0.3:T12 0.5:a0,0,-1 3:N 5:T8 6:a0,0,1", 8)
ok &= check("...one notification is no bad night", st["why"] != "3.15", st)
_, st = run("0.05:H0 0.5:T40", 2)
ok &= check("hungry a day and a half: alive", st["dead"] == "0", st)
_, st = run("0.05:H0 0.5:T120", 2)
ok &= check("forgotten five days: he died, the days he lived kept", int(st["dead"]) >= 5 and st["screen"] == "pip", st)
img, st = run("0.05:H0 0.5:T120 1:P 1.3:B 1.6:d233,233 1.7:u 1.9:ocare", 2.4)
ok &= check("dead: no button or touch does anything", st["screen"] == "pip" and int(st["dead"]) >= 5, st)
_, st = run("0.05:H0 0.5:T30 1:F 2:T30", 3)
ok &= check("fed in time, he lives on", st["dead"] == "0", st)
_, st = run("0.05:H0 0.1:Kdie_h=10 0.5:T40", 2)
ok &= check("die_h shorter: dies sooner", st["dead"] != "0", st)
# the more care his character needs, the sooner neglect kills him
_, st = run("0.05:Y2 0.5:T32", 1)
ok &= check("Требовательный: dead after 32 h forgotten", st["dead"] != "0", st)
_, st = run("0.05:Y2 0.5:T24", 1)
ok &= check("Требовательный: alive after 24 h", st["dead"] == "0", st)
_, st = run("0.05:Y1 0.5:T40", 1)
ok &= check("Уравновешенный: alive after 40 h", st["dead"] == "0", st)
_, st = run("0.05:Y1 0.5:T50", 1)
ok &= check("Уравновешенный: dead after 50 h", st["dead"] != "0", st)
_, st = run("0.05:Y0 0.5:T66", 1)
ok &= check("Неприхотливый: alive after 66 h", st["dead"] == "0", st)
_, st = run("0.05:Y0 0.5:T76", 1)
ok &= check("Неприхотливый: dead after 76 h", st["dead"] != "0", st)
# the charger: kept on it full for hours sours him (asleep he sleeps on), taken off at 80-95% pleases him, the cable
# in and out again and again annoys him
# run flat (the board: under 3.3 V for 30 s, no cable) and switched on again: food and energy -0.4 at once, angry after the intro
_, before = run("0:Kvary=0", 1)
tr, st = traced("0:Kvary=0 1:z", 8)
ok &= check("run flat: food and energy drop by drain_food / drain_energy at once",
            abs(float(before["food"]) - float(st["food"]) - 0.4) < 0.02 and float(st["trust"]) < float(before["trust"]), (before["food"], st["food"]))
ok &= check("run flat: the power-on intro ends angry, and he stays cross (aftertaste 3.20, a grudge)",
            any(r[1] == "angry" and r[2] == "drained" for r in tr) and st["why"] == "3.20" and float(st["grudge"]) > 0, (tr, st))
_, st = run("0:Kvary=0 1:z 6:T0.5", 9)
ok &= check("run flat: half an hour on, no longer angry about it", st["why"] != "3.20", st)
_, st = run("0.05:H0 0.1:J100 0.2:J100+ 0.3:T4", 5)
ok &= check("charger: Уравновешенный full for 4 h -> not yet overcharged", st["why"] != "2.4", st)
_, st = run("0.05:H0 0.1:J100 0.2:J100+ 0.3:T5", 5)
ok &= check("charger: Уравновешенный full for 5 h -> overcharged, cross", st["why"] == "2.4" and st["base"] in ("angry", "annoyed"), st)
_, st = run("0.05:H0 0.1:J100 0.2:J100+ 0.3:T10", 5)
ok &= check("charger: full for 10 h -> sad or hurt", st["why"] == "2.4" and st["base"] in ("sad", "hurt"), st)
_, st = run("0.05:H0 0.06:Y2 0.1:J100 0.2:J100+ 0.3:T3.5", 5)
ok &= check("charger: Требовательный overcharged after 3 h", st["why"] == "2.4", st)
_, st = run("0.05:H0 0.06:Y0 0.1:J100 0.2:J100+ 0.3:T5", 5)
ok &= check("charger: Неприхотливый fine after 5 h", st["why"] != "2.4", st)
_, st = run("0.05:H0 0.06:Y0 0.1:J100 0.2:J100+ 0.3:T6.5", 5)
ok &= check("charger: Неприхотливый overcharged after 6 h", st["why"] == "2.4", st)
_, st = run("0.05:H0 0.1:J100 0.2:J100+ 0.3:a0,0,-1 3:T5", 5)
ok &= check("charger: overcharged asleep he sleeps on", st["asleep"] == "1", st)
_, st = run("0.05:H0 0.1:J100 0.2:J100+ 0.3:T5 0.8:J100", 1.5)
ok &= check("charger: taken off overcharged -> hurt", st["reaction"] == "overcharged" and st["why"] == "3.16", st)
_, st = run("0.05:H0 0.1:J70 0.2:J70+ 0.3:T0.5 0.5:J88+ 0.8:J88", 1.2)
ok &= check("charger: taken off at 88% after a real charge -> pleased", st["reaction"] == "unplugged" and st["why"] == "3.17", st)
_, st = run("0.05:H0 0.1:J70 0.2:J70+ 0.5:J88+ 0.8:J88", 1.2)
ok &= check("charger: taken off at 88% a moment after plugging -> nothing", st["why"] != "3.17", st)
_, st = run("0.05:H0 0.1:J70 0.2:J70+ 0.3:T0.5 0.5:J88 0.7:T0.1 0.8:J88+ 0.9:T0.5 1.5:J88", 2)
ok &= check("charger: pleased once, not again an hour on (no farming)", st["why"] != "3.17", st)
_, st = run("0.05:H0 0.1:J50 0.2:J50+ 0.4:J50 0.6:J50+", 1)
ok &= check("charger: the cable in twice -> a doubtful look", st["reaction"] == "cable" and st["mood"] == "skeptical", st)
_, st = run("0.05:H0 0.1:J50 0.2:J50+ 0.5:J60+ 0.8:J60", 1.2)
ok &= check("charger: taken off at 60% -> nothing special", st["why"] != "3.17", st)
_, st = run("0.05:H0 0.1:J50 0.2:J50+ 0.4:J50 0.6:J50+ 0.8:J50 1.0:J50+", 2)
ok &= check("charger: the cable in three times in a minute -> annoyed", st["reaction"] == "cable" and st["why"] == "3.18" and st["mood"] == "annoyed", st)
_, st = run("0.05:H0 0.06:Y2 0.1:J50 0.2:J50+ 0.4:J50 0.6:J50+ 0.8:J50 1.0:J50+", 2)
ok &= check("charger: Требовательный fiddled with -> angry", st["mood"] == "angry" and st["why"] == "3.18", st)
FIDDLE = " ".join(f"{0.2 + i * 0.4:.1f}:J50{'+' if i % 2 == 0 else ''}" for i in range(16))
_, st = run("0.05:H0 0.1:J50 " + FIDDLE, 7)
ok &= check("charger: in and out again and again -> rage and worse", st["mood"] in ("rage", "swearing", "berserk") and st["reaction"] == "cable", st)
# tumbling: turned upside down and back twice within 4 s; kept up, the fourth leaves him dizzy; the switch turns it off
TUMBLE = "0.2:a0,-1,0 0.5:a0,1,0 0.8:a0,-1,0 1.1:a0,1,0"
_, st = run("0.05:H0 " + TUMBLE, 1.5)
ok &= check("tumbling: over and back twice -> giddy", st["mood"] == "tumbling" and st["reaction"] == "tumbling", st)
_, st = run("0.05:H0 0.2:a0,-1,0 2:a0,1,0 4:a0,-1,0 5.8:a0,1,0", 6)
ok &= check("tumbling: slow turns are no tumble", st["reaction"] != "tumbling", st)
_, st = run("0.05:H0 0.06:f10,0 " + TUMBLE, 1.5)
ok &= check("tumbling: switched off on the panel, nothing", st["reaction"] != "tumbling", st)
MUCH = " ".join(f"{t + d:.1f}:a0,{y},0" for t in (0.2, 2.8, 5.4, 8.0) for d, y in ((0, -1), (0.2, 1), (0.4, -1), (0.6, 1)))
_, st = run("0.05:H0 " + MUCH, 9)
ok &= check("tumbling: the fourth in a row -> dizzy", st["mood"] == "dizzy" and st["reaction"] == "tumbled", st)
# the owner's birthday (runs start 10 October): sung once, on the real clock only, never to a sleeping Pip; festive all day
_, st = run("0.05:H0 0.1:b10,10", 1.0)
ok &= check("birthday: on the day he sings, excited", st["reaction"] == "birthday" and st["mood"] == "excited", st)
_, st = run("0.05:H0 0.1:b11,10", 1.0)
ok &= check("birthday: another day, nothing", st["reaction"] != "birthday", st)
_, st = run("0.05:H0 0.08:X1 0.1:b10,10", 1.5)
ok &= check("birthday set from the panel while linked: no song under the setup screen", st["reaction"] != "birthday" and st["voices"] == "0", st)
_, st = run("0.05:H0 0.08:X1 0.1:b10,10 1.0:X0", 2.0)
ok &= check("the panel gone: he sings it then", st["reaction"] == "birthday", st)
_, st = run("0.05:H0 0.1:t0 0.2:b10,10", 1.0)
ok &= check("birthday: the clock not synchronised, nothing", st["reaction"] != "birthday", st)
_, st = run("0.05:H0 0.1:a0,0,-1 3:b10,10", 4.0)
ok &= check("birthday: asleep face down he is not woken for it", st["asleep"] == "1" and st["reaction"] != "birthday", st)
_, st = run("0.05:H0 0.1:b10,10 6:T1", 7.5)
ok &= check("birthday: once a day, then the great palette all day", st["reaction"] != "birthday" and st["why"] in ("7.0", "7.4"), st)
_, st = run("0.05:H0 0.1:b10,10 0.1:b0,0", 1.0)  # on and off before a frame
ok &= check("birthday: switched off, nothing", st["reaction"] != "birthday", st)
SOUR = ("sad", "angry", "annoyed", "furious", "rage", "swearing", "berserk", "hurt", "disgusted", "gloomy", "despondent",
        "lonely", "cynical", "suspicious", "nervous", "sick", "dismayed", "pouty", "indignant", "belligerent")
_, st = run("0.05:H0 0.06:c0.02", 7.5)
ok &= check("filthy on an ordinary day: a sour face", st["base"] in SOUR, st)
_, st = run("0.05:H0 0.06:c0.02 0.1:b10,10", 7.5)
ok &= check("birthday: filthy, still no sour face that day", st["base"] not in SOUR, st)
_, st = run("0.05:H0 0.06:Kbday_gap=0.01 0.06:Kbday_encore=0 0.1:b10,10", 12)
ok &= check("birthday: bursts of confetti all day", st["reaction"] == "festive" and st["mood"] in ("happy", "excited", "laughing"), st)
_, st = run("0.05:H0 0.06:Kbday_gap=0.01 0.06:Kbday_encore=1 0.1:b10,10", 12)
ok &= check("birthday: the song again now and then", st["reaction"] == "birthday", st)
# dead: his cracked screen goes dark in 5 s; touch and motion leave it dark, a button lights it
_, st = run("0.05:H0 0.5:T120", 7)
ok &= check("dead: the screen goes dark by itself after 5 s", st["display"] == "0", st)
_, st = run("0.05:H0 0.5:T120 7:d233,233 7.2:u 7.4:S 7.9:S", 8.2)
ok &= check("dead: a touch or a shake does not light it", st["display"] == "0", st)
_, st = run("0.05:H0 0.5:T120 7:P", 7.5)
ok &= check("dead: a button lights it again", st["display"] == "1", st)

# asleep face down the phone does not disturb him: no sound from a notification or a call
_, base = run("0.1:H0 0.2:L2 0.5:a0,0,-1", 6)
_, st = run("0.1:H0 0.2:L2 0.5:a0,0,-1 4:N 4.5:C1", 6)
ok &= check("asleep face down: a notification and a call make no sound", st["voices"] == base["voices"] and st["asleep"] == "1", (st, base["voices"]))
_, st = run("0.1:H0 0.2:L2 4:N 4.5:C1", 6)
ok &= check("...awake they do", int(st["voices"]) > 0, st)

# Приватность > Звонки: off, a call never shows on the keychain; on again, it does
_, st = run("0.1:H0 0.2:L2 0.4:oprivacy 0.7:d233,353 0.75:u 1.0:B 1.4:C1", 2.0)
ok &= check("calls switched off: a call does not show", st["screen"] != "call", st)
_, st = run("0.1:H0 0.2:L2 0.4:oprivacy 0.7:d233,353 0.75:u 1.0:d233,353 1.05:u 1.3:B 1.7:C1", 2.2)
ok &= check("...switched on again: it rings here", st["screen"] == "call", st)

# a feature number out of range (the panel's or a terminal's garbage) changes nothing: "feat 35 0" once shifted
# past the word (on x86 the same as 3, the voice) and silenced him
_, st = run("0.1:H0 0.15:f35,0 0.2:f-1,0 0.3:B", 2.0)  # the meet speaks (with f3,0 it would not)
ok &= check("feature out of range: ignored, his voice stays on", int(st["voices"]) > 0, st)

# peekaboo over and over (face down half a second and back) pays fun and love once in PEEK_GAP_S, not each time
_, one = run("0.1:H0 0.2:a0,1,0", 13)
_, st = run("0.1:H0 0.2:a0,1,0 0.3:a0,0,-1 0.8:a0,1,0 1.5:a0,0,-1 2.0:a0,1,0 2.7:a0,0,-1 3.2:a0,1,0 3.9:a0,0,-1 4.4:a0,1,0 5.1:a0,0,-1 5.6:a0,1,0 6.3:a0,0,-1 6.8:a0,1,0 7.5:a0,0,-1 8.0:a0,1,0 8.7:a0,0,-1 9.2:a0,1,0 9.9:a0,0,-1 10.4:a0,1,0 11.1:a0,0,-1 11.6:a0,1,0", 13)
ok &= check("peekaboo farmed: fun rises once, not ten times", float(st["fun"]) - float(one["fun"]) < 0.05, (st["fun"], one["fun"]))

# a group opened over an open app (debug "open games" from the clock): the app closes, the list turns into the group,
# and going home afterwards works (it used to delete the menu screen and hang)
_, st = run("0.1:H0 0.3:oclock 0.8:ogames", 1.3)
ok &= check("group over an app: the menu, the app closed", st["screen"] == "menu", st)
_, st = run("0.1:H0 0.3:oclock 0.8:ogames 1.3:B 1.5:B 2.2:P", 3.5)
ok &= check("...and home and back again: no hang", st["screen"] in ("pip", "menu"), st)

# a timer that runs out after he has died: no "Время вышло" card (no button could put it away, its alarm lit his screen)
_, st = run("0.1:H0 0.3:otimer 0.6:d163,321 0.7:u 1:B 1.5:T200 303:B 304:P 305:B", 306)
ok &= check("timer ends after his death: no card", st["screen"] != "timer_done" and st["dead"] != "0", st)

# ---- the everyday faces, the new zones of touch, hiccups and toots, how much he talks ----
EVERYDAY = ("impatient", "wistful", "blissful", "adoring", "sly", "sheepish", "determined", "eureka", "eyeroll", "flustered")
for m in EVERYDAY:
    img, st = run(f"0.1:n{m}", 1.2)
    ok &= check(f"the everyday face '{m}' shows (pinned)", st["mood"] == m and img.getbbox() is not None, st)
seen = set()
for hrs in range(0, 48, 2):  # through two days: the palette's slots are now and then one of the everyday faces
    _, st = run(f"0:Keveryday=1 0.05:T{hrs}", 0.3)
    seen.add(st["base"])
ok &= check("everyday faces turn up in the day's palette", bool(seen & set(EVERYDAY)), sorted(seen))
seen0 = set()
for hrs in range(0, 48, 2):
    _, st = run(f"0.05:T{hrs}", 0.3)
    seen0.add(st["base"])
ok &= check("...and never with Keveryday=0 (the classic palettes)", not (seen0 & set(EVERYDAY)), sorted(seen0))
Z = "0:Kzones=1 "
_, st = run(Z + "0.3:d140,395 0.35:u", 0.8)
ok &= check("a tap on a cheek: flustered (his own answer, not a poke)", st["reaction"] == "cheek" and st["mood"] == "flustered", st)
_, st = run(Z + "0.3:d233,405 0.35:u", 0.8)
ok &= check("a tap under his chin: chin up, glad", st["reaction"] == "chin", st)
_, st = run(Z + "0.3:d14,300 0.35:u", 0.8)  # the rim below a wide eye (at 22,233 an amazed eye reaches: that is the eye)
ok &= check("a poke in his side: ticklish, he jumps away", st["reaction"] == "side", st)  # "side poke" (the status splits at spaces)
for x in (420, 46):  # an eye's outer part, past SIDE_R from the middle: the eye, not his ticklish side (it laughed)
    _, st = run(Z + f"0.3:d{x},233 0.35:u", 0.8)
    ok &= check(f"a poke in an eye's outer edge (x {x}): not a side poke, no laugh", st["reaction"] != "side" and st["mood"] != "laughing", st)
rub = " ".join(f"{0.2 + i * 0.05:.2f}:m{182 if i % 8 < 4 else 284},405" for i in range(45))
_, st = run(Z + "0.15:d182,405 " + rub, 2.3)
ok &= check("stroked under the chin: blissful, eyes shut, head up", st["reaction"] == "chin" and st["mood"] == "blissful", st)  # "chin rub"
rub = " ".join(f"{0.2 + i * 0.05:.2f}:m{70 if i % 8 < 4 else 165},390" for i in range(45))
_, st = run(Z + "0.15:d70,390 " + rub, 2.3)
ok &= check("a cheek stroked: he nestles into the hand", st["reaction"] == "cheek", st)  # "cheek rub"
# tickled (measured on the board: fast, in a patch of ~19 px): a laugh wherever it is, the cheeks and the chin too
tickle = lambda x, y, t0, n: f"{t0}:d{x},{y} " + " ".join(f"{t0 + i * 0.025:.3f}:m{x + (14 if i % 2 else -14)},{y + i % 3 - 1}" for i in range(1, n)) + f" {t0 + n * 0.025:.3f}:u"
for x, y, where in ((233, 233, "between the eyes"), (150, 350, "a cheek"), (233, 420, "the chin")):
    tr, st = traced(Z + tickle(x, y, 0.2, 120), 3.0)
    ok &= check(f"tickled ({where}): he laughs", any(r[1] == "laughing" and r[2] == "tickle" for r in tr), tr)
tr, st = traced(Z + tickle(233, 233, 0.2, 140), 5.0)
ok &= check("tickled a good while, let go: still giggling a moment", any(r[2] == "tickle:" for r in tr), tr)
_, st = run(Z + "0.3:d118,233", 1.4)
ok &= check("a finger held on an eye: that eye shut, he is put out", st["reaction"] == "finger" and st["mood"] in ("annoyed", "indignant", "ouch", "skeptical"), st)
_, st = run(Z + "0.3:d118,233", 4.0)
ok &= check("...kept there: cross", st["reaction"] == "finger" and st["mood"] == "angry", st)
_, st = run("0.3:d118,233", 1.4)
ok &= check("...zones off: a finger resting on an eye is as before (no 'finger in eye')", st["reaction"] != "finger", st)
_, st = run(Z + "0.3:d233,90 0.35:u", 0.6)
ok &= check("a tap elsewhere is still a poke", st["reaction"].startswith("poke"), st)
_, st = run("0.2:kh", 0.6)
ok &= check("hiccups: 'hic!'", st["reaction"] == "hic", st)
_, st = run("0.2:kh", 40)
ok &= check("...and they pass", st["reaction"] != "hic", st)
_, st = run("0.2:kt", 0.7)
ok &= check("a toot: 'pfrrt'", st["reaction"] == "toot", st)
_, st = run("0.2:kt", 2.8)
ok &= check("...then sheepish, sly or cheeky about it", st["mood"] in ("sheepish", "sly", "cheeky"), st)
img1, _ = run("0.1:neyeroll", 0.55)
img2, _ = run("0.1:neyeroll", 1.6)
ok &= check("rolling his eyes: the gaze goes up over the top, then aside and down", pupils(img1)[1] < pupils(img2)[1] - 25, (pupils(img1), pupils(img2)))
for lv in range(3):
    _, st = run(f"0.1:j{lv}", 0.3)
    ok &= check(f"how much he talks: level {lv} set", st["chat"] == str(lv), st)
def chats(level):  # how often he talks, left alone for ten minutes with his own moments on (as on the board)
    out = subprocess.run([sim, "--trace", "--out", os.path.join(tmp, "c.png"), "--secs", "600", "--events",
                          f"0:s0 0.1:j{level}"], check=True, capture_output=True, text=True, encoding="utf-8",
                         errors="replace", timeout=600).stdout.splitlines()
    return sum(1 for l in out if l.startswith("trace") and (" chats " in l or " tells " in l))
quiet, chatty = chats(0), chats(2)
ok &= check("a chatterbox talks more than a quiet one (ten minutes alone)", chatty > quiet and chatty >= 4, (quiet, chatty))

# hands (pip_fingers: pipsim h; the board's panel reports two fingers at most and flags a palm). The status' reaction is its
# first word: these read the trace, a change of face a line (mood, reaction in full)
def faces(events, secs):
    if "Kvary" not in events: events = "0:Kvary=0 0:Klife_gap=0 0:Ktalk=0 0:Keveryday=0 0:Kzones=0 0:Kcalm=0 " + events
    out = subprocess.run([sim, "--trace", "--out", os.path.join(tmp, "f.png"), "--secs", str(secs), "--events", events], check=True,
                         capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=300).stdout.splitlines()
    return [(l[14:28].strip(), l[29:49].strip()) for l in out if l.startswith("trace")]
f = faces("0.05:H0 0.3:hP 1.2:h0", 1.6)
ok &= check("a palm on him: squished; lifted, he pops back up", ("squished", "palm") in f and ("squished", "palm off") in f, f)
SPREAD = "0.3:h125,233,345,233 0.45:h110,233,360,233 0.6:h80,233,390,233 0.75:h60,233,410,233"
f = faces("0.05:H0 " + SPREAD + " 1.0:h0", 1.3)
ok &= check("two fingers on his eyes drawn apart: stretched; let go, they twang back", ("stretched", "stretched") in f and ("stretched", "let go") in f, f)
PINCH = "0.3:h110,233,360,233 0.45:h130,233,340,233 0.6:h160,233,310,233"
f = faces("0.05:H0 " + PINCH + " 1.0:h0", 1.3)
ok &= check("...drawn together: pinched; let go, they pop apart", ("pinched", "pinched") in f and ("pinched", "popped") in f, f)
f = faces("0.05:H0 0.3:h125,233,345,233 1.0:h0", 1.2)
ok &= check("two fingers on his eyes held still: nothing, no poke", not any(r in ("stretched", "pinched", "two fingers", "poke", "finger in eye") for _, r in f), f)
f = faces("0.05:H0 0.3:h233,410,233,60 1.3:h0", 1.6)
ok &= check("two fingers on him elsewhere: overwhelmed; gone, relieved", ("overwhelmed", "two fingers") in f and ("relieved", "fingers gone") in f, f)
f = faces("0.05:H0 0.3:opong 1:w1 9:B 9.2:B", 10)
ok &= check("a game's end on its screen: shown again when you come back to him within 10 s", [r for _, r in f].count("lost") == 2, f)
f = faces("0.05:H0 0.3:opong 1:w1 13:B 13.2:B", 14)
ok &= check("...but not after longer", [r for _, r in f].count("lost") == 1, f)
_, st = run("0.05:H0 0.3:hP 1.2:h0", 1.6)
ok &= check("a palm is no poke and no stroke", st["reaction"] not in ("poke", "pet", "tickle"), st)

# the newer faces as the other face of an answer, at vary 1: games lost to you one after another, some deflated
def traced(events, secs):
    out = subprocess.run([sim, "--trace", "--out", os.path.join(tmp, "t.png"), "--secs", str(secs), "--events", events], check=True,
                         capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=300).stdout.splitlines()
    return [l.split()[2] for l in out if l.startswith("trace") and len(l.split()) > 3 and l.split()[3] in ("lost", "sore")]
lost = traced("0:Kvary=1 0:Klife_gap=0 0:Ktalk=0 0.05:H0 " + " ".join(f"{1 + 4 * k}:T0.6 {1.05 + 4 * k}:y1 {1.1 + 4 * k}:w1" for k in range(10)), 42)
ok &= check("you beat him: now sad, now deflated", "deflated" in lost and len(set(lost)) > 1, lost)

# the little scenes with pictures (moods.h after PIP_GROGGY, pip.c show_face, sprite.c)
def picture(name, secs):
    path = os.path.join(tmp, "s.png")
    subprocess.run([sim, "--out", path, "--secs", str(secs), "--events", f"0:Kvary=0 0:Klife_gap=0 0:s0 0:Keveryday=0 0.05:n{name}"], check=True, capture_output=True, timeout=180)
    return Image.open(path).convert("RGB")
def ink(img, test): return sum(1 for q in pixels(img.resize((233, 233))) if test(*q))
gold, red, green = lambda r, g, b: r > 200 and 120 < g < 220 and b < 90, lambda r, g, b: r > 220 and g < 130 and 60 < b < 190, lambda r, g, b: g > 190 and r < 160 and b < 140
for name, secs, test, least, what in (("starstruck", 2.0, gold, 300, "gold stars for pupils"), ("kiss", 1.6, red, 60, "a heart flies"), ("cool", 2.0, gold, 60, "a gold frame"),
                                      ("zapped", 0.65, gold, 60, "a bolt"), ("sipping", 3.2, green, 60, "drinking the charge, green"), ("butterfly", 3.6, gold, 40, "the butterfly sits down"),
                                      ("campfire", 2.0, gold, 40, "a fire burns")):
    n = ink(picture(name, secs), test)
    ok &= check(f"{name}: {what}", n >= least, n)
img = picture("peeking", 0.65)
ok &= check("peeking: hidden under the edge at first", ink(img, lambda r, g, b: r > 128) < 400, ink(img, lambda r, g, b: r > 128))
n = ink(picture("neutral", 1.0), gold) + ink(picture("neutral", 1.0), red)
ok &= check("...and none of that on a plain face", n == 0, n)
_, st = run("0.05:H0 0.1:nstarstruck 1.5:nneutral", 2.2)
path = os.path.join(tmp, "s2.png")
subprocess.run([sim, "--out", path, "--secs", "2.6", "--events", "0:Kvary=0 0:Klife_gap=0 0.05:H0 0.1:nstarstruck 1.5:nneutral"], check=True, capture_output=True)
ok &= check("the face gone, its pictures go with it", ink(Image.open(path).convert("RGB"), gold) == 0, ink(Image.open(path).convert("RGB"), gold))
def names(events, secs): # every face and reaction he showed, at vary 1
    out = subprocess.run([sim, "--trace", "--out", os.path.join(tmp, "t.png"), "--secs", str(secs), "--events", events], check=True,
                         capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=300).stdout.splitlines()
    return [tuple(l.split(None, 4)[2:4]) for l in out if l.startswith("trace") and len(l.split()) > 3]
plugs = lambda pct: "0:Kvary=1 0:Klife_gap=0 0:Ktalk=0 0:s0 0:Kfiddle_s=1 0:Kcalm=0 0.05:H0 0.1:J%d " % pct + " ".join(f"{1 + 8 * k}:J{pct}+ {7 + 8 * k}:J{pct}" for k in range(8))
hi, lo = [m for m, r in names(plugs(70), 66) if r.startswith("charging")], [m for m, r in names(plugs(15), 66) if r.startswith("charging")]
ok &= check("the cable in, lively: mostly a jolt of energy, not always", hi.count("zapped") > hi.count("sipping") and len(set(hi)) > 1, hi)
ok &= check("...low on charge: mostly drinks it in", lo.count("sipping") > lo.count("zapped"), lo)
holds = " ".join(f"{1 + 8 * k}:d233,300 {7 + 8 * k}:u" for k in range(8))
seen = [m for m, _ in names("0:Kvary=1 0:Klife_gap=0 0:Ktalk=0 0:s0 0:Kcalm=0 0.05:H0 " + holds, 66)]
ok &= check("a finger held long: dreamy, now and then zen", "dreamy" in seen and 1 <= seen.count("zen") <= 7, seen)
seen = {m for m, _ in names("0:Kvary=1 0:Klife_gap=4 0:s0 0.05:H0", 400)}
new = "starstruck singing flirty rainbow peeking cool puppy butterfly bubbles robot kitty silly snicker campfire".split()
ok &= check("left alone, the little scenes come by themselves", len(seen & set(new)) >= 4, seen)
ok &= check("...the stars only at night", "stargazing" not in seen, seen)
seen = {m for m, _ in names("0:Kvary=1 0:Klife_gap=0 0:s0 0.05:H0 " + " ".join(f"{1 + 6 * k}:a0,1,0 {2 + 6 * k}:a0,0,-1 {5 + 6 * k}:a0,1,0" for k in range(6)), 38)}
ok &= check("asleep, well looked after: sweetly, now and then", "sweetdream" in seen or "asleep" in seen, seen)

# the 38 scenes of Nov 2026 (moods.h after PIP_SIPPING): symbols for eyes, in the set's colour; dark scenes come back
white, orange = lambda r, g, b: r > 200 and g > 200 and b > 200, lambda r, g, b: r > 220 and 60 < g < 190 and b < 100
for name, secs, test, least, what in (("xd", 1.1, white, 1000, "squeezed laughing eyes"), ("knockout", 1.3, white, 2500, "his own eyes, dazed"), ("knockout", 1.3, gold, 150, "stars round his head"),
                                      ("hearteyes", 1.2, red, 1000, "a heart beats over him"), ("hearteyes", 1.2, white, 2500, "...his own eyes under it"), ("burning", 1.4, orange, 2000, "eyes on fire"),
                                      ("medal", 1.3, gold, 400, "a gold medal"), ("king", 0.4, gold, 400, "a crown")):
    n = ink(picture(name, secs), test)
    ok &= check(f"{name}: {what}", n >= least, n)
lit = lambda r, g, b: max(r, g, b) > 60
n_dark, n_ghost = ink(picture("spooked", 1.8), lit), ink(picture("spooked", 3.3), lit)
ok &= check("spooked: dark, only the pupils glow - then a ghost, eyes wide", n_dark < 600 and n_ghost > 5000, (n_dark, n_ghost))
path = os.path.join(tmp, "s3.png")
for name, at in (("spooked", 1.0), ("sulking", 1.0)):  # the eyes hidden (dark, turned away) come back when the face goes
    subprocess.run([sim, "--out", path, "--secs", str(at + 1.4), "--events", f"0:Kvary=0 0:Klife_gap=0 0:s0 0.05:n{name} {at}:nneutral"], check=True, capture_output=True)
    n = ink(Image.open(path).convert("RGB"), white)
    ok &= check(f"{name}: left mid-scene, both eyes are back", n > 3000, n)
# ...and their occasions, at vary 1 (never at vary 0: the classic checks above)
L38 = "xd rofl bouncy blooming hearteyes fireworks medal disco popeyed lightbulb paws hello king angel imp juggler balloon icecream umbrella strongman magician sunrise sheep ninja sprout raincloud sobbing heartbroken fuming burning sulking spooked tumbleweed lowbatt hungry knockout wilted sniffles".split()
V1 = "0:Kvary=1 0:Klife_gap=0 0:Ktalk=0 0:s0 0:Kcalm=0 0.05:H0 "
seen = [m for m, _ in names(V1 + Z + " ".join(tickle(233, 233, 1 + 4 * k, 60) for k in range(8)), 34)]
ok &= check("tickled: the real laugh and \"> <\" by turns", "laughing" in seen and "xd" in seen, seen)
seen = [m for m, _ in names(V1 + Z + " ".join(tickle(233, 233, 1 + 8 * k, 220) for k in range(5)), 42)]
ok &= check("tickled past 4 s: now and then he rolls about laughing", "rofl" in seen, seen)
seen = [m for m, _ in names(V1 + "0:v0 " + " ".join(f"{1 + 5 * k}:a0,0,0 {1.3 + 5 * k}:a0,1,0" for k in range(6)), 32)]
ok &= check("dropped when hardly loved: now and then he sobs", "sobbing" in seen, seen)
seen = [m for m, _ in names(V1 + " ".join(f"{1 + 10 * k}:S2.5 {7 + 10 * k}:S0" for k in range(4)), 42)]
ok &= check("shaken hard and long: knocked out, x x", "knockout" in seen, seen)
tr = names(V1 + "0:Y0 0.5:W 421:a0,0,-1 424:a0,1,0", 427)
ok &= check("out walking (half the day's steps), taken out of the pocket: the barbell", ("strongman", "lifts") in tr, tr[-3:])
tr = names(V1 + "0.5:W 121:a0,0,-1 124:a0,1,0", 127)
ok &= check("...a few steps only: just woken", "strongman" not in [m for m, _ in tr], tr[-3:])
tr = names(V1 + "0.1:T18 0.5:F 2.5:F 4.5:v1 5:a0,0,-1 7:T2 9:a0,1,0", 11)
ok &= check("woken in the morning, fed and loved: good morning", ("sunrise", "good") in tr, tr[-3:])
seen = {m for m, _ in names("0:Kvary=1 0:Klife_gap=4 0:s0 0.05:H0", 600)}
ok &= check("left alone, the 38's own moments come by themselves", len(seen & set(L38)) >= 3, sorted(seen & set(L38)))
seen = {m for m, _ in names("0:Kvary=0 0:Klife_gap=4 0:s0 0.05:H0 " + Z + tickle(233, 233, 1, 220) + " 8:a0,0,0 8.3:a0,1,0", 300)}
ok &= check("...and none of them at vary 0", not seen & set(L38), sorted(seen & set(L38)))

# the cable pulled out (pip_battery): a scene by the charge he got; none straight after plugging, none at vary 0
for pct, want in ((90, ("charged", "unplugged:")), (55, ("cutoff", "unplugged:")), (15, ("empty", "unplugged:"))):
    tr = names(f"{V1}0.1:J{pct}+ 30:J{pct}", 32)
    ok &= check(f"the cable out at {pct}%: {want[0]}", want in tr, tr[-3:])
tr = names(f"{V1}0.1:J55+ 10:J55", 12)
ok &= check("...pulled straight after plugging: no scene", not {"charged", "cutoff", "empty"} & {m for m, _ in tr}, tr[-3:])
tr = names("0:Kvary=0 0:Klife_gap=0 0:Ktalk=0 0:s0 0.05:H0 0.1:J55+ 30:J55", 32)
ok &= check("...and none at vary 0", not {"charged", "cutoff", "empty"} & {m for m, _ in tr}, tr[-3:])
green = lambda r, g, b: g > 180 and r < 200 and b < 180
n = ink(picture("charged", 1.0), green)
ok &= check("charged: green sparks of charge", n >= 15, n)
n_on, n_off = ink(picture("empty", 0.3), lambda r, g, b: max(r, g, b) > 60), ink(picture("empty", 2.1), lambda r, g, b: max(r, g, b) > 60)
ok &= check("empty: his eyes go out, then back on", n_off < 300 and n_on > 3000, (n_on, n_off))
# fed when full: now and then it nearly comes back up (queasy), and no burp from an earlier biscuit over it
seen = [names(f"{V1}{pk}1.0:F 2.7:F 4.4:F", 5.5) for pk in ("", "0.1:d233,400 0.15:u ", "0.1:d233,400 0.15:u 0.2:d233,400 0.25:u ", "0.1:d233,400 0.15:u 0.4:d233,400 0.45:u 0.5:d233,400 0.55:u ")]
ok &= check("fed too much: now and then queasy", any(("queasy", "queasy") in tr for tr in seen), [tr[-4:] for tr in seen])
ok &= check("...with no burp over it", not any(("queasy", "burp") in tr for tr in seen), [tr[-4:] for tr in seen])
n = ink(picture("queasy", 1.3), lambda r, g, b: g > 150 and r < 170 and b < 120)
ok &= check("queasy: green, cheeks puffed", n >= 400, n)
# final check fixes (Nov 2026)
_, st = run("0.1:s0 0.5:a0,-1,0 2.5:P 5:a0,0,1 6:B", 7.0)
ok &= check("upside down a while, righted in the menu: \"phew\" when back on his face", st["reaction"] == "phew", st)
_, st = run("0.1:s0 0.5:a0,-1,0 1.0:P 1.5:a0,0,1 2:B", 3.0)
ok &= check("...upside down only a moment: quietly over", st["reaction"] == "", st)
_, st = run("0.05:O " + " ".join(f"{i}.1:J50+" for i in range(1, 30)), 31.0)
ok &= check("on the cable before his birth: he is not fed", st["food"] == "0.850", st)
_, st = run("0.05:H0 0.1:J50 0.2:J50+ 0.3:T0.2 0.6:F", 1.8)
ok &= check("the cable feeds only up to full_line: after a flash (on the cable) the first biscuit is no 'too full'",
            st["reaction"] == "yum" and st["mood"] != "nauseous", st)
# New Year (Nov 2026): 1 January 12:00 UTC is 1992 h after a run's start; his time stopped (f5,0) so the jump costs him nothing
NY = ("new", "wreath")  # the status cuts a reaction at its first space: "new year tree" is "new"
_, st = run("0:Kvary=1 0.05:H0 0.06:f5,0 0.1:T1992", 1.5)
ok &= check("1 January: the tree or the wreath at once", st["reaction"] in NY, st)
_, st = run("0:Kvary=1 0.05:H0 0.06:f5,0 0.1:T1968", 1.5)
ok &= check("...not on 31 December", st["reaction"] not in NY, st)
_, st = run("0:Kvary=1 0.05:H0 0.06:f5,0 0.08:t0 0.1:T1992", 1.5)
ok &= check("...nor with a clock never set", st["reaction"] not in NY, st)
_, st = run("0.05:H0 0.06:f5,0 0.1:T1992", 1.5)
ok &= check("...nor at vary 0", st["reaction"] not in NY, st)
n = ink(picture("newtree", 2.2), lambda r, g, b: g > 120 and r < 90 and b < 120)
ok &= check("newtree: a green fir under him", n >= 1200, n)
img = picture("wreath", 2.6)
n, red = ink(img, lambda r, g, b: g > 110 and r < 90 and b < 110), ink(img, lambda r, g, b: r > 190 and g < 90 and b < 90)
ok &= check("wreath: a green wreath with a red bow", n >= 1200 and red >= 150, (n, red))
img, st = run("0.05:H0 0.06:nneutral 0.1:ks", 4.0)
snowy = lambda img: ink(img.convert("RGB").crop((0, 0, 466, 110)), lambda r, g, b: r > 200 and g > 200 and b > 200)
n = snowy(img)
ok &= check("1 January's snowfall: flakes over the top of the glass", n >= 60, n)
# 2..10 January: once or twice a day at 9..21 h - at 22:00 on the 5th the day's snow is due and falls as soon as he is seen
n = snowy(run("0:Kvary=1 0.05:H0 0.06:f5,0 0.07:nneutral 0.1:T2098", 4.0)[0])
ok &= check("5 January, evening: the day's rare snowfall", n >= 60, n)
n = snowy(run("0:Kvary=1 0.05:H0 0.06:f5,0 0.07:nneutral 0.1:T2242", 4.0)[0])
ok &= check("...none on 11 January", n < 20, n)
for h in (1, 2):
    img, st = run(f"0.05:H{h} 0.3:otorch", 1.0)
    ok &= check(f"torch: white in theme {h}", img.getpixel((233, 233)) > 240 and img.getpixel((233, 60)) > 240, st)
_, st = run("0.3:opong", 200.0)
ok &= check("ping-pong left lying: the screen sleeps (no .awake)", st["display"] == "0", st)
print("ok" if ok else "FAILED")
sys.exit(0 if ok else 1)
