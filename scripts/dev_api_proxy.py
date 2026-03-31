#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import logging
import os
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


DEFAULT_HOST = "0.0.0.0"
DEFAULT_PORT = 8787
DEFAULT_TIMEOUT = 15.0
DEFAULT_TTL = 20

UPSTREAMS = {
    "coingecko": "https://api.coingecko.com/api/v3",
    "yahoo": "https://query1.finance.yahoo.com",
    "alternative": "https://api.alternative.me",
    "onoff": "https://onoff.markets",
}

ROUTES = {
    "/coingecko/simple_price": ("coingecko", "/simple/price"),
    "/yahoo/quote": ("yahoo", "/v7/finance/quote"),
    "/yahoo/chart": ("yahoo", "/v8/finance/chart"),
    "/alternative/fng": ("alternative", "/fng/"),
    "/stocks/fear_greed": ("onoff", "/data/stocks-fear-greed.json"),
}


class CacheEntry:
    def __init__(self, status: int, headers: dict[str, str], body: bytes, expires_at: float) -> None:
        self.status = status
        self.headers = headers
        self.body = body
        self.expires_at = expires_at


class ProxyState:
    def __init__(self, ttl_seconds: int, timeout_seconds: float) -> None:
        self.ttl_seconds = ttl_seconds
        self.timeout_seconds = timeout_seconds
        self.cache: dict[str, CacheEntry] = {}
        self.lock = threading.Lock()

    def get_cache(self, key: str) -> CacheEntry | None:
        now = time.time()
        with self.lock:
            entry = self.cache.get(key)
            if not entry:
                return None
            if entry.expires_at < now:
                self.cache.pop(key, None)
                return None
            return entry

    def set_cache(self, key: str, status: int, headers: dict[str, str], body: bytes) -> None:
        expires_at = time.time() + self.ttl_seconds
        with self.lock:
            self.cache[key] = CacheEntry(status, headers, body, expires_at)


def join_upstream(base: str, path: str, query: str) -> str:
    base = base.rstrip("/")
    path = path if path.startswith("/") else "/" + path
    url = base + path
    if query:
        url += "?" + query
    return url


def normalize_cache_headers(headers: dict[str, str]) -> dict[str, str]:
    keep = {}
    content_type = headers.get("Content-Type")
    cache_control = headers.get("Cache-Control")
    if content_type:
        keep["Content-Type"] = content_type
    if cache_control:
        keep["Cache-Control"] = cache_control
    return keep


class ProxyHandler(BaseHTTPRequestHandler):
    server_version = "PixelDevProxy/1.0"

    @property
    def state(self) -> ProxyState:
        return self.server.state  # type: ignore[attr-defined]

    def log_message(self, fmt: str, *args) -> None:
        logging.info("%s - %s", self.address_string(), fmt % args)

    def send_json(self, status: int, payload: dict) -> None:
        body = json.dumps(payload, ensure_ascii=True, indent=2).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path == "/health":
          self.send_json(HTTPStatus.OK, {"ok": True, "service": "dev_api_proxy"})
          return
        if parsed.path == "/routes":
          self.send_json(
              HTTPStatus.OK,
              {
                  "ok": True,
                  "routes": {route: {"provider": provider, "path": upstream_path} for route, (provider, upstream_path) in ROUTES.items()},
                  "providers": sorted(UPSTREAMS.keys()),
              },
          )
          return

        target_url = self.resolve_target(parsed)
        if not target_url:
            self.send_json(
                HTTPStatus.NOT_FOUND,
                {
                    "ok": False,
                    "error": "route_not_found",
                    "hint": "Use /routes to inspect supported endpoints",
                },
            )
            return

        cache_key = target_url
        cached = self.state.get_cache(cache_key)
        if cached:
            self.respond_bytes(cached.status, cached.headers, cached.body, cache_hit=True)
            return

        try:
            req = urllib.request.Request(
                target_url,
                headers={
                    "User-Agent": "esp32-pixel-dev-proxy/1.0",
                    "Accept": "application/json,text/plain,*/*",
                },
                method="GET",
            )
            with urllib.request.urlopen(req, timeout=self.state.timeout_seconds) as resp:
                body = resp.read()
                headers = {k: v for k, v in resp.headers.items()}
                status = resp.status
        except urllib.error.HTTPError as e:
            body = e.read()
            self.respond_bytes(
                e.code,
                {"Content-Type": e.headers.get("Content-Type", "text/plain; charset=utf-8")},
                body or str(e).encode("utf-8"),
                cache_hit=False,
            )
            return
        except Exception as e:
            self.send_json(
                HTTPStatus.BAD_GATEWAY,
                {
                    "ok": False,
                    "error": "upstream_fetch_failed",
                    "detail": str(e),
                    "target": target_url,
                },
            )
            return

        normalized_headers = normalize_cache_headers(headers)
        self.state.set_cache(cache_key, status, normalized_headers, body)
        self.respond_bytes(status, normalized_headers, body, cache_hit=False)

    def respond_bytes(self, status: int, headers: dict[str, str], body: bytes, cache_hit: bool) -> None:
        self.send_response(status)
        for key, value in headers.items():
            if key.lower() in {"content-length", "connection", "transfer-encoding", "server", "date"}:
                continue
            self.send_header(key, value)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("X-Proxy-Cache", "HIT" if cache_hit else "MISS")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def resolve_target(self, parsed: urllib.parse.ParseResult) -> str | None:
        for route, (provider, upstream_path) in sorted(ROUTES.items(), key=lambda item: len(item[0]), reverse=True):
            if parsed.path == route:
                return join_upstream(UPSTREAMS[provider], upstream_path, parsed.query)
            if parsed.path.startswith(route + "/"):
                suffix = parsed.path[len(route):]
                return join_upstream(UPSTREAMS[provider], upstream_path + suffix, parsed.query)

        prefix = "/proxy/"
        if not parsed.path.startswith(prefix):
            return None

        remainder = parsed.path[len(prefix):]
        if not remainder:
            return None
        parts = remainder.split("/", 1)
        provider = parts[0]
        subpath = "/" + parts[1] if len(parts) > 1 else "/"
        base = UPSTREAMS.get(provider)
        if not base:
            return None
        return join_upstream(base, subpath, parsed.query)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Development HTTP proxy for ESP32 pixel apps")
    parser.add_argument("--host", default=os.environ.get("PIXEL_PROXY_HOST", DEFAULT_HOST))
    parser.add_argument("--port", type=int, default=int(os.environ.get("PIXEL_PROXY_PORT", DEFAULT_PORT)))
    parser.add_argument("--ttl", type=int, default=int(os.environ.get("PIXEL_PROXY_TTL", DEFAULT_TTL)))
    parser.add_argument("--timeout", type=float, default=float(os.environ.get("PIXEL_PROXY_TIMEOUT", DEFAULT_TIMEOUT)))
    parser.add_argument("--log-level", default=os.environ.get("PIXEL_PROXY_LOG_LEVEL", "INFO"))
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    logging.basicConfig(level=getattr(logging, str(args.log_level).upper(), logging.INFO), format="%(asctime)s %(levelname)s %(message)s")

    server = ThreadingHTTPServer((args.host, args.port), ProxyHandler)
    server.state = ProxyState(ttl_seconds=args.ttl, timeout_seconds=args.timeout)  # type: ignore[attr-defined]

    logging.info("dev_api_proxy listening on http://%s:%d", args.host, args.port)
    logging.info("preset routes: %s", ", ".join(sorted(ROUTES.keys())))
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        logging.info("dev_api_proxy shutting down")
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
