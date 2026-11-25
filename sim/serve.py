"""Serves sim/web without browser caching, so a rebuilt page or wasm always shows up on reload.
Usage: py sim/serve.py [port]"""
import functools, http.server, os, sys


class NoCache(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


port = int(sys.argv[1]) if len(sys.argv) > 1 else 8466
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "web")
http.server.ThreadingHTTPServer(("", port), functools.partial(NoCache, directory=root)).serve_forever()
