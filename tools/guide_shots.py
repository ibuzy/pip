r"""Pictures for the panel's documentation tab (docs/guide/*.webp): pipsim runs, the round glass cut out.
Usage: py tools/guide_shots.py [path/to/pipsim]   (after tools\sim.ps1 build)"""
import os, subprocess, sys, tempfile
from PIL import Image, ImageDraw

sim = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "build/pipsim" + (".exe" if os.name == "nt" else ""))  # Windows wants it whole
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "docs", "guide")
os.makedirs(out, exist_ok=True)
tmp = tempfile.mkdtemp()

SHOTS = {  # name: (seconds, events) - the last frame is the picture; only those docs/index.html shows
    "hero": (2.0, "0:nhappy"),
    "m_love": (2.0, "0:nlove"), "m_sad": (2.0, "0:nsad"), "m_angry": (2.0, "0:nangry"), "m_sleepy": (2.0, "0:nsleepy"),
    "m_curious": (2.0, "0:ncurious"), "m_asleep": (2.0, "0:nasleep"),
    "feed": (1.45, "0.2:B1"),
    "wash": (2.2, "0:c0.3 0.3:B 0.5:B3"),
    "dirty": (1.5, "0:c0.12"),
}

r = 233
mask = Image.new("L", (466 * 2, 466 * 2), 0)
ImageDraw.Draw(mask).ellipse((0, 0, 466 * 2 - 1, 466 * 2 - 1), fill=255)
mask = mask.resize((466, 466), Image.LANCZOS)
only = sys.argv[2:] if len(sys.argv) > 2 else None
for name, (secs, ev) in SHOTS.items():
    if only and name not in only: continue
    png = os.path.join(tmp, name + ".png")
    subprocess.run([sim, "--out", png, "--secs", str(secs), "--events", ev], check=True, capture_output=True)
    im = Image.open(png).convert("RGB")
    im.putalpha(mask)
    im.resize((360, 360), Image.LANCZOS).save(os.path.join(out, name + ".webp"), quality=86, method=6)
    print(name)
