"""Packs firmware/build-release for the panel: docs/firmware/firmware.js (every part with its address, base64) and
docs/firmware/version.js (its version, date and size: the panel loads only that with the page, firmware.js when it flashes).
Run from the repository root after the release build (tools/release.ps1)."""
import base64, json, os, re, time
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
build = "firmware/build-release"
args = open(os.path.join(build, "flash_args"), encoding="utf-8").read().split()
parts = [{"address": int(a, 16), "file": f} for a, f in zip(args[6::2], args[7::2])]  # after the three --flash_* pairs
for p in parts:
    p["data"] = base64.b64encode(open(os.path.join(build, p.pop("file")), "rb").read()).decode()
ver = re.search(r'PROJECT_VER "([^"]+)"', open("firmware/CMakeLists.txt", encoding="utf-8").read()).group(1)  # the version the board reports too
# where the board keeps its data, for the panel's "Прошить начисто" (it writes erased flash there; an update leaves it):
# NVS (pet, settings, bonds) and the first 128 KB of "storage" (mood history, battery log). Offsets as ESP-IDF lays out
# partitions.csv: an empty offset follows the previous partition, apps aligned to 64 KB.
def size_of(v):
    v = v.strip().upper()
    return int(v[:-1], 0) * 1024 * 1024 if v.endswith("M") else int(v[:-1], 0) * 1024 if v.endswith("K") else int(v, 0)
wipe, at = [], 0x9000
for line in open("firmware/partitions.csv", encoding="utf-8"):
    f = [x.strip() for x in line.split("#")[0].split(",")]
    if len(f) < 5 or not f[0]:
        continue
    align = 0x10000 if f[1] == "app" else 0x1000
    at = int(f[3], 0) if f[3] else (at + align - 1) // align * align
    size = size_of(f[4])
    if f[0] == "nvs": wipe.append([at, size])
    if f[0] == "storage": wipe.append([at, min(size, 0x20000)])
    at += size
info = {"version": ver or "dev", "built": time.strftime("%Y-%m-%d"), "parts": parts, "wipe": wipe}
os.makedirs("docs/firmware", exist_ok=True)
with open("docs/firmware/firmware.js", "w", encoding="utf-8", newline="\n") as f:
    f.write("/* Pip's firmware for the panel: made by tools/release.ps1, do not edit */\nwindow.PIP_FIRMWARE = ")
    json.dump(info, f)
    f.write(";\n")
with open("docs/firmware/version.js", "w", encoding="utf-8", newline="\n") as f:
    f.write("/* what docs/firmware/firmware.js holds (bytes: its size, for the download's progress): made by tools/release.ps1 */\n")
    f.write("window.PIP_FIRMWARE_INFO = " + json.dumps({"version": info["version"], "built": info["built"],
                                                     "bytes": os.path.getsize("docs/firmware/firmware.js")}) + ";\n")
size = sum(len(base64.b64decode(p["data"])) for p in parts)
print(f"docs/firmware/firmware.js: {ver}, {len(parts)} parts, {size // 1024} KB")
