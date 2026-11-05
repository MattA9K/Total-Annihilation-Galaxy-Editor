#!/usr/bin/env python3
"""Serve the built site at the GitHub project Pages base path.

The default address is the same path the public site will use, so asset and
language links match production. This server does not redirect / to a language.
"""

from __future__ import annotations

import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import build_site


class PagesHandler(SimpleHTTPRequestHandler):
    base = ""

    def do_GET(self) -> None:
        path = self.path.split("?", 1)[0]
        prefix = type(self).base
        if prefix:
            if path == prefix:
                self.send_response(302)
                self.send_header("Location", prefix + "/")
                self.end_headers()
                return
            if path != prefix + "/" and not path.startswith(prefix + "/"):
                self._serve_not_found()
                return
            self.path = path[len(prefix):] or "/"
        translated = Path(self.translate_path(self.path))
        if not translated.exists():
            self._serve_not_found()
            return
        super().do_GET()

    def _serve_not_found(self) -> None:
        body = (Path(self.directory) / "404.html").read_bytes()
        self.send_response(404)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def main() -> int:
    parser = argparse.ArgumentParser(description="Preview the Galaxy Editor site.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--base", default=build_site.DEFAULT_BASE)
    parser.add_argument("--directory", type=Path, default=None)
    args = parser.parse_args()
    root = build_site.repo_root()
    directory = args.directory if args.directory is not None else root / "_site"
    if not (directory / "index.html").is_file():
        raise SystemExit(f"No built site at {directory}. Run: python3 site/build_site.py")
    base = build_site.normalize_base(args.base)
    PagesHandler.base = base
    handler = partial(PagesHandler, directory=str(directory.resolve()))
    server = ThreadingHTTPServer((args.host, args.port), handler)
    home = f"http://{args.host}:{args.port}{base}/"
    print(f"Serving {directory} at {home}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print()
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
