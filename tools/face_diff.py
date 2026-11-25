"""Renderer refactors must not change the picture: every mood rendered by pipsim at a few moments, saved as a reference
(--ref) or compared with it (default). A pixel off by more than --tol in any channel counts as changed.

    python tools/face_diff.py --ref           # before touching ui/face.c
    python tools/face_diff.py [--tol 0]       # after: lists the moods whose frames differ"""
import argparse, os, re, subprocess, sys
from PIL import Image, ImageChops
def pixels(im): return (im.get_flattened_data if hasattr(im, "get_flattened_data") else im.getdata)()  # getdata is deprecated in Pillow 12

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ap = argparse.ArgumentParser()
ap.add_argument("--ref", action="store_true")
ap.add_argument("--tol", type=int, default=0)
ap.add_argument("--dir", default=os.path.join(ROOT, "build", "face_ref"))
ap.add_argument("--sim", default=os.path.join(ROOT, "build", "pipsim.exe"))
args = ap.parse_args()

src = open(os.path.join(ROOT, "ui", "pip.c"), encoding="utf-8").read()
moods = re.findall(r'^\s*\[PIP_[A-Z_]+\]\s*=\s*\{\s*(?:\.name\s*=\s*)?"([a-z_]+)"', src, re.M)  # neutral is {.name = ...}
os.makedirs(args.dir, exist_ok=True)
cases = [(m, t, e) for m in moods for t in (0.9, 2.3) for e in (0, 2)] + [("sneeze", 0.55, 0), ("burp", 0.9, 0)]
bad, total, new = [], 0, set()
for m, t, eyes in cases:
    if not args.ref and not os.path.exists(os.path.join(args.dir, f"{m}_{t}_{eyes}.png")):
        new.add(m)  # a mood added since the reference: nothing to compare it with
        continue
    ev = f"0.02:I{eyes} 0.05:kz" if m == "sneeze" else f"0.02:I{eyes} 0.05:kx" if m == "burp" else f"0.02:I{eyes} 0.05:n{m}"
    name = f"{m}_{t}_{eyes}.png"
    out = os.path.join(args.dir, name) if args.ref else os.path.join(args.dir, "new_" + name)
    subprocess.run([args.sim, "--out", out, "--secs", str(t), "--events", ev], capture_output=True, check=True)
    if args.ref:
        continue
    a, b = Image.open(os.path.join(args.dir, name)).convert("RGB"), Image.open(out).convert("RGB")
    d = ImageChops.difference(a, b)
    px = sum(1 for p in pixels(d) if max(p) > args.tol)
    total += 1
    if px:
        bad.append((name, px, max(max(p) for p in pixels(d))))
if args.ref:
    print(f"reference: {len(cases)} frames in {args.dir}")
else:
    for n, px, mx in bad:
        print(f"  {n}: {px} px differ (max {mx})")
    print(f"{total - len(bad)}/{total} frames identical" + ("" if not bad else f", {len(bad)} differ") +
          (f"; {len(new)} new moods with no reference" if new else ""))
    sys.exit(1 if bad else 0)
