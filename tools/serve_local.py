"""The panel and the web simulator on localhost, for as long as this runs (local-servers.bat keeps it in its console:
closing that window stops both). No browser caching, so a rebuilt page or wasm shows up on reload.
  panel      http://localhost:8465   (docs/: the panel, its guide, docs/sim)
  simulator  http://localhost:8466   (sim/web, built by tools\\sim.ps1 web-build)
Usage: py tools/serve_local.py [--no-open]"""
import functools, http.server, os, sys, threading, webbrowser

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SITES = [("Панель", 8465, os.path.join(ROOT, "docs")), ("Симулятор", 8466, os.path.join(ROOT, "sim", "web"))]


class NoCache(http.server.SimpleHTTPRequestHandler):
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map, ".wasm": "application/wasm", ".js": "text/javascript"}

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, fmt, *args):  # quiet: only errors
        if args and str(args[1])[:1] in "45":
            super().log_message(fmt, *args)


servers = []
for name, port, root in SITES:
    try:
        s = http.server.ThreadingHTTPServer(("127.0.0.1", port), functools.partial(NoCache, directory=root))
    except OSError:
        print(f"  {name}: порт {port} занят — наверное, уже запущен")
        continue
    servers.append(s)
    threading.Thread(target=s.serve_forever, daemon=True).start()
    print(f"  {name}: http://localhost:{port}")

if not servers:
    sys.exit(1)
if "--no-open" not in sys.argv:
    for name, port, _ in SITES:
        webbrowser.open(f"http://localhost:{port}")
print("\nРаботает, пока открыто это окно. Закрой его (или Ctrl+C), чтобы остановить.")
try:
    threading.Event().wait()
except KeyboardInterrupt:
    pass
