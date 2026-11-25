"""Pip's frame rate on the board, mood by mood: every mood pinned in turn, then the special animations (sneeze, burp,
washing, feeding, the meet), each held a few seconds; the firmware's `perf` gives the times since the last call.

    python tools/bench.py [--secs 3] [--port COM5] [--out bench.csv] [--only name,name]

Columns: Pip's frames a second, his frame (avg / worst, ms: from one finished frame to the next), the face draw on two
cores (avg / worst), LVGL's refresh and render, the panel's stream and the rows sent a frame. Needs the debug server
stopped (it holds the port) and the keychain face up, awake (the script keeps the screen lit)."""
import argparse, json, sys, time
import serial
from serial.tools import list_ports

ap = argparse.ArgumentParser()
ap.add_argument("--secs", type=float, default=3.0)
ap.add_argument("--port")
ap.add_argument("--out", default=None)
ap.add_argument("--only", default="")
ap.add_argument("--tasks", action="store_true", help="each task's share of a core during the phase")
ap.add_argument("--log", default=None, help="every line the board says, to this file (a reset or a panic mid-run shows there)")
args = ap.parse_args()
blog = open(args.log, "w", encoding="utf-8") if args.log else None

port = args.port or next((p.device for p in list_ports.comports() if p.vid == 0x303A), None)
if not port:
    sys.exit("no keychain on USB (VID 0x303A)")
s = serial.Serial(port, 115200, timeout=0.05)
buf = b""


def lines(secs):
    global buf
    end = time.time() + secs
    while time.time() < end:
        buf += s.read(4096)
        while b"\n" in buf:
            l, buf = buf.split(b"\n", 1)
            l = l.decode(errors="replace").strip()
            if blog:
                blog.write(l + "\n"), blog.flush()
            yield l


def send(cmd):
    s.write((cmd + "\n").encode())


def ask(cmd, tag, secs=2.0):
    send(cmd)
    for l in lines(secs):
        if l.startswith(tag):
            return l[len(tag):].strip()
    return None


def perf():
    for _ in range(3):  # another task's log line can land inside it: ask again
        r = ask("perf lit", "@P")
        try:
            return json.JSONDecoder().raw_decode(r)[0] if r else None
        except ValueError:
            pass
    return None


names = []
for _ in range(8):  # just flashed: it may still be starting
    names = (ask("names", "@N") or "").split(",")
    if names != [""]:
        break
if not names or names == [""]:
    sys.exit("no answer to `names`: is the debug server holding the port?")
send("home")
send("dev 0")
list(lines(1.0))

# "wake" first: at night he falls asleep mid-run and the screen goes dark (3 s "frames", no answers)
phases = [(f"mood {n}", ["wake", f"mood {i}"], None) for i, n in enumerate(names) if n]
phases += [
    ("bday song", ["auto", "key y"], 5.0),   # the birthday: the song with two pairs of poppers
    ("bday poppers", ["auto", "key p"], 2.5),  # the day's bursts
    ("bday rain", ["auto", "key r"], 3.0),
    ("ny snow", ["mood 0", "key s"], 6.0),   # 1 January's snowfall over a plain face
    ("ny snow+hyp", ["mood hypnotized", "key s"], 6.0),  # ...and over the costliest face
    ("sneeze", ["mood 0", "key z"], 1.6),
    ("burp", ["mood 0", "key x"], 2.1),
    ("hiccups", ["mood 0", "key h"], 6.0),  # a run of "hic!"s: the eyes pop and he jolts
    ("toot", ["mood 0", "key t"], 2.4),
    ("wash", ["mood 0", "wash 1"], None),
    ("feed", ["mood 0", "feed"], None),
    ("meet", ["mood 0", "meet"], None),
    # his birth (cosmos.c full frames, then his eyes), each part from its own moment
    ("birth sky", ["auto", "birth 0.6"], 3.0), ("birth heart", ["birth 4.4"], 3.6), ("birth galaxy", ["birth 8.6"], 3.6),
    ("birth bang", ["birth 13.6"], 1.6), ("birth waking", ["birth 15.8"], 3.6), ("birth world", ["birth 19.4"], 5.5), ("birth joy", ["birth 27.6"], 4.0),
    ("auto", ["auto"], None),
]
if args.only:
    by = {p[0].split(" ")[-1]: p for p in phases}
    phases = [by[w] for w in args.only.split(",") if w in by]

rows = []
for name, cmds, secs in phases:
    for c in cmds:
        if c.startswith("wait "):  # not a board command: let a show run on before measuring
            list(lines(float(c[5:])))
        else:
            send(c)
    list(lines(0.4))  # settle: the springs on their way
    perf()
    list(lines(secs or args.secs))
    p = perf()
    if name == "wash":
        send("wash 0")
    if not p:
        print(f"{name:24s} no answer")
        continue
    n, avg, worst = p["pip"]
    dur = secs or args.secs
    fps = n / (dur + 0.05)
    row = dict(name=name, fps=fps, frame=avg, frame_max=worst, face=p["face"][1], face_max=p["face"][2], b0=p["b0"][1], b1=p["b1"][1],
               refr=p["refr"][1], refr_max=p["refr"][2], render=p["render"][1], stream=p["stream"][1], te=p["te"][1], rows=p["rows"][1],
               panel=p["stream"][0] / (dur + 0.05), scr=p["scr"])
    rows.append(row)
    if "prof" in p:  # a profiling build: cycles a frame (thousands) and calls a frame per pixel path
        PATHS = ["skip", "white", "rim", "full", "clear", "blush", "eyes", "out", "flat", "blocks", "plines", "rowset", "nothing", "f.flat", "f.cresc", "f.mixed"]
        print("    " + "  ".join(f"{PATHS[k]} {c / 1000:.0f}k/{m}" for k, (c, m) in enumerate(p["prof"]) if c))
    print(f"{name:24s} {fps:5.1f} fps  frame {avg:5.1f}/{worst:5.1f}  face {row['face']:5.1f}/{row['face_max']:5.1f} "
          f"(bands {row['b0']:4.1f}/{row['b1']:4.1f})  refr {row['refr']:4.1f}/{row['refr_max']:4.1f} render {row['render']:4.1f}  "
          f"stream {row['stream']:4.1f} te {row['te']:4.1f} rows {row['rows']:3.0f}  panel {row['panel']:4.1f}/s  {row['scr']} "
          f"{p.get('mhz', '?')} MHz t={p.get('t', '?')} cal={p.get('cal')}", flush=True)
    if args.tasks and "tasks" in p:
        print("    " + "  ".join(f"{k} {v}%" for k, v in p["tasks"].items()))
send("auto")

if rows:
    rows_w = sorted(rows, key=lambda r: r["frame_max"], reverse=True)
    print("\nslowest (worst frame):")
    for r in rows_w[:12]:
        print(f"  {r['name']:24s} {r['fps']:5.1f} fps  worst {r['frame_max']:5.1f} ms  face {r['face']:5.1f}/{r['face_max']:5.1f}")
    fps = [r["fps"] for r in rows]
    print(f"\nall: {len(rows)} phases, fps min {min(fps):.1f} median {sorted(fps)[len(fps) // 2]:.1f} max {max(fps):.1f}")
if args.out:
    with open(args.out, "w", encoding="utf-8") as f:
        keys = list(rows[0].keys())
        f.write(",".join(keys) + "\n")
        for r in rows:
            f.write(",".join(f"{r[k]:.2f}" if isinstance(r[k], float) else str(r[k]) for k in keys) + "\n")
