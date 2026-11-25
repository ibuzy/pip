"""Print the board's serial log for a while (no interactive monitor needed).
Usage: python firmware/serlog.py <seconds> [--reset] [--port COM5]
       python firmware/serlog.py --which      (just print the board's port: flash.ps1 uses it)"""
import argparse, sys, time
import serial
from serial.tools import list_ports


def board_port():
    """the ESP32-S3's own USB port: the COM port with Espressif's VID 0x303A"""
    for p in sorted(list_ports.comports(), key=lambda p: p.device):
        if p.vid == 0x303A:
            return p.device
    return None


ap = argparse.ArgumentParser()
ap.add_argument("seconds", type=float, nargs="?", default=13)
ap.add_argument("--reset", action="store_true", help="restart the app first (RTS pulses EN)")
ap.add_argument("--port", default=None)
ap.add_argument("--which", action="store_true", help="print the board's port and exit")
ap.add_argument("--send", action="append", default=[], help="a debug command to send first (again for more)")
ap.add_argument("--clock", action="store_true", help="first set the board's clock to this computer's (time <unix> <tz min>)")
a = ap.parse_args()
if a.which:
    print(board_port() or "")
    sys.exit(0 if board_port() else 1)
a.port = a.port or board_port()
if not a.port:
    sys.exit("serlog: the board is not connected (no USB port with VID 0x303A)")
s = serial.Serial(a.port, 115200, timeout=0.2)
if a.reset:
    s.dtr = False
    s.rts = True
    time.sleep(0.1)
    s.rts = False
if a.clock:
    now = time.time()
    a.send.insert(0, "time %d %d" % (now, time.localtime(now).tm_gmtoff // 60))
for c in a.send:
    s.write((c + "\n").encode())
    time.sleep(0.2)
end = time.time() + a.seconds
while time.time() < end:  # as it comes, so a long capture can be read while it runs
    chunk = s.read(4096)
    if chunk:
        sys.stdout.write(chunk.decode("utf-8", "replace"))
        sys.stdout.flush()
