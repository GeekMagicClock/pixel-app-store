#!/usr/bin/env python3
"""
Simple LAN gateway for CoinGecko requests.

Usage:
  python3 python/coingecko_gateway.py

Env:
  HOST=0.0.0.0
  PORT=8080
  CG_UPSTREAM=https://api.coingecko.com
  HTTPS_PROXY=http://127.0.0.1:7890
  HTTP_PROXY=http://127.0.0.1:7890
"""

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlencode, urlparse
from urllib.request import Request, build_opener
import json
import os
import urllib.error


HOST = os.getenv("HOST", "0.0.0.0")
PORT = int(os.getenv("PORT", "8080"))
CG_UPSTREAM = os.getenv("CG_UPSTREAM", "https://api.coingecko.com").rstrip("/")

# urllib will honor HTTP(S)_PROXY from environment via ProxyHandler.
OPENER = build_opener()


class Handler(BaseHTTPRequestHandler):
    server_version = "cg-gateway/1.0"

    def _send_json(self, status: int, payload: dict):
        body = json.dumps(payload, ensure_ascii=True).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _send_bytes(self, status: int, body: bytes, content_type: str = "application/json"):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        parsed = urlparse(self.path)

        if parsed.path == "/health":
            self._send_json(200, {"ok": True, "service": "coingecko_gateway"})
            return

        if parsed.path != "/coingecko/simple_price":
            self._send_json(404, {"ok": False, "error": "not found"})
            return

        q = parse_qs(parsed.query, keep_blank_values=False)
        ids = (q.get("ids", [""])[0] or "").strip()
        if not ids:
            self._send_json(400, {"ok": False, "error": "missing ids"})
            return

        upstream_q = {
            "vs_currencies": "usd",
            "ids": ids,
            "include_24hr_change": "true",
            "precision": "full",
        }
        upstream = f"{CG_UPSTREAM}/api/v3/simple/price?{urlencode(upstream_q)}"

        try:
            req = Request(
                upstream,
                method="GET",
                headers={
                    "Accept": "application/json",
                    "User-Agent": "esp32-pixel-gateway/1.0",
                },
            )
            with OPENER.open(req, timeout=8) as resp:
                data = resp.read()
                ctype = resp.headers.get("Content-Type", "application/json")
                self._send_bytes(resp.status, data, ctype)
                return
        except urllib.error.HTTPError as e:
            body = e.read() if hasattr(e, "read") else b""
            if not body:
                body = json.dumps({"ok": False, "error": f"upstream http {e.code}"}).encode("utf-8")
            self._send_bytes(e.code, body, "application/json")
            return
        except Exception as e:
            self._send_json(502, {"ok": False, "error": str(e)})
            return

    def log_message(self, fmt, *args):
        # concise logs
        print(f"[gateway] {self.address_string()} - {fmt % args}")


def main():
    srv = ThreadingHTTPServer((HOST, PORT), Handler)
    print(f"coingecko gateway listening on http://{HOST}:{PORT}")
    print("endpoints: /health, /coingecko/simple_price?ids=bitcoin,ethereum")
    srv.serve_forever()


if __name__ == "__main__":
    main()
