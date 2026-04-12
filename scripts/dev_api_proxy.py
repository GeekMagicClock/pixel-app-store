#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import logging
import os
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import re
import tempfile
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


DEFAULT_HOST = "0.0.0.0"
DEFAULT_PORT = 8787
DEFAULT_TIMEOUT = 15.0
DEFAULT_TTL = 20
CNN_CACHE_TTL_SECONDS = 10 * 60
CNN_STALE_MAX_AGE_SECONDS = 24 * 60 * 60
BROWSER_UA = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36"
YAHOO_UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/115.0.0.0 Safari/537.36"

UPSTREAMS = {
    "coingecko": "https://api.coingecko.com/api/v3",
    "yahoo": "https://query1.finance.yahoo.com",
    "alternative": "https://api.alternative.me",
    "onoff": "https://onoff.markets",
    "cnn": "https://production.dataviz.cnn.io",
}

ROUTES = {
    "/coingecko/simple_price": ("coingecko", "/simple/price"),
    "/yahoo/quote": ("yahoo", "/v7/finance/quote"),
    "/yahoo/chart": ("yahoo", "/v8/finance/chart"),
    "/alternative/fng": ("alternative", "/fng/"),
    "/stocks/fear_greed": ("onoff", "/data/stocks-fear-greed.json"),
}


class CacheEntry:
    def __init__(self, status: int, headers: dict[str, str], body: bytes, expires_at: float, stored_at: float) -> None:
        self.status = status
        self.headers = headers
        self.body = body
        self.expires_at = expires_at
        self.stored_at = stored_at


class ProxyState:
    def __init__(self, ttl_seconds: int, timeout_seconds: float) -> None:
        self.ttl_seconds = ttl_seconds
        self.timeout_seconds = timeout_seconds
        self.curl_proxy = os.environ.get("PIXEL_PROXY_CURL_PROXY", "").strip()
        self.cache: dict[str, CacheEntry] = {}
        self.lock = threading.Lock()
        self.yahoo_cookie = ""
        self.yahoo_crumb = ""
        self.yahoo_auth_expires_at = 0.0

    def get_cache(self, key: str) -> CacheEntry | None:
        now = time.time()
        with self.lock:
            entry = self.cache.get(key)
            if not entry:
                return None
            if entry.expires_at < now:
                return None
            return entry

    def get_stale_cache(self, key: str, max_age_seconds: int) -> CacheEntry | None:
        now = time.time()
        with self.lock:
            entry = self.cache.get(key)
            if not entry:
                return None
            if (now - entry.stored_at) > max_age_seconds:
                self.cache.pop(key, None)
                return None
            return entry

    def set_cache(self, key: str, status: int, headers: dict[str, str], body: bytes, ttl_seconds: int | None = None) -> None:
        now = time.time()
        ttl = self.ttl_seconds if ttl_seconds is None else max(1, int(ttl_seconds))
        expires_at = now + ttl
        with self.lock:
            self.cache[key] = CacheEntry(status, headers, body, expires_at, now)


def detect_system_curl_proxy() -> str:
    if sys.platform != "darwin":
        return ""
    try:
        proc = subprocess.run(["scutil", "--proxy"], capture_output=True, check=False, text=True)
        if proc.returncode != 0:
            return ""
        kv: dict[str, str] = {}
        for ln in proc.stdout.splitlines():
            if ":" not in ln:
                continue
            k, v = ln.split(":", 1)
            kv[k.strip()] = v.strip()

        # Prefer HTTPS proxy for Yahoo/finance endpoints.
        if kv.get("HTTPSEnable") == "1" and kv.get("HTTPSProxy") and kv.get("HTTPSPort"):
            return f"http://{kv['HTTPSProxy']}:{kv['HTTPSPort']}"
        if kv.get("HTTPEnable") == "1" and kv.get("HTTPProxy") and kv.get("HTTPPort"):
            return f"http://{kv['HTTPProxy']}:{kv['HTTPPort']}"
        if kv.get("SOCKSEnable") == "1" and kv.get("SOCKSProxy") and kv.get("SOCKSPort"):
            return f"socks5h://{kv['SOCKSProxy']}:{kv['SOCKSPort']}"
    except Exception:
        return ""
    return ""


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
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            logging.warning("client disconnected while sending json response")

    def do_GET(self) -> None:
        parsed = urllib.parse.urlparse(self.path)
        logging.debug("client req path=%s headers=%s", self.path, dict(self.headers.items()))
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
            self.respond_bytes(cached.status, cached.headers, cached.body, cache_state="HIT")
            return

        stale = self.state.get_stale_cache(cache_key, max_age_seconds=CNN_STALE_MAX_AGE_SECONDS)

        if target_url.startswith(UPSTREAMS["cnn"]):
            try:
                status, headers, body = self.fetch_cnn_with_browser_headers(target_url)
                normalized_headers = normalize_cache_headers(headers)
                self.state.set_cache(cache_key, status, normalized_headers, body, ttl_seconds=CNN_CACHE_TTL_SECONDS)
                self.respond_bytes(status, normalized_headers, body, cache_state="MISS")
                return
            except Exception as e:
                if stale:
                    logging.warning("cnn upstream error (%s), serving stale cache for %s", e, target_url)
                    self.respond_bytes(stale.status, stale.headers, stale.body, cache_state="STALE")
                    return
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

        if target_url.startswith(UPSTREAMS["yahoo"]):
            try:
                if parsed.path.startswith("/yahoo/quote"):
                    retry = self.build_yahoo_quote_with_crumb(parsed)
                    if retry is not None:
                        headers = {"Content-Type": "application/json; charset=utf-8"}
                        self.state.set_cache(cache_key, 200, headers, retry)
                        self.respond_bytes(200, headers, retry, cache_state="MISS-CRUMB")
                        return
                    logging.warning("yahoo quote crumb-first failed, fallback to direct upstream")
                if parsed.path.startswith("/yahoo/chart"):
                    retry_status, retry_body = self.build_yahoo_chart_with_cookie(target_url)
                    if retry_status == 200 and retry_body is not None:
                        headers = {"Content-Type": "application/json; charset=utf-8"}
                        self.state.set_cache(cache_key, 200, headers, retry_body)
                        self.respond_bytes(200, headers, retry_body, cache_state="MISS-COOKIE")
                        return
                    logging.warning("yahoo chart cookie-first failed (status=%s), fallback to direct upstream", retry_status)

                req_headers = {
                    "User-Agent": BROWSER_UA,
                    "Accept": "application/json,text/plain,*/*",
                    "Accept-Language": "en-US,en;q=0.9",
                    "Referer": "https://finance.yahoo.com/",
                }
                logging.debug("upstream yahoo req url=%s headers=%s", target_url, req_headers)
                status, resp_headers, body, headers_raw = self.curl_get_status_body(target_url, req_headers)
                logging.debug("upstream yahoo resp status=%s headers=%s", status, resp_headers)
                logging.debug("upstream yahoo resp headers_raw=%s", headers_raw)
                if parsed.path.startswith("/yahoo/chart") and status == 200:
                    body = self.trim_yahoo_chart_body(body, max_points=64)
                if parsed.path.startswith("/yahoo/quote") and status in (401, 403, 429):
                    retry = self.build_yahoo_quote_with_crumb(parsed)
                    if retry is not None:
                        headers = {"Content-Type": "application/json; charset=utf-8"}
                        self.state.set_cache(cache_key, 200, headers, retry)
                        self.respond_bytes(200, headers, retry, cache_state="MISS-CRUMB")
                        return
                    if stale:
                        self.respond_bytes(stale.status, stale.headers, stale.body, cache_state="STALE")
                        return
                headers = {"Content-Type": "application/json; charset=utf-8"}
                if status == 200:
                    self.state.set_cache(cache_key, status, headers, body)
                self.respond_bytes(status, headers, body, cache_state="MISS")
                return
            except Exception as e:
                if parsed.path.startswith("/yahoo/quote"):
                    retry = self.build_yahoo_quote_with_crumb(parsed)
                    if retry is not None:
                        headers = {"Content-Type": "application/json; charset=utf-8"}
                        self.state.set_cache(cache_key, 200, headers, retry)
                        self.respond_bytes(200, headers, retry, cache_state="MISS-CRUMB")
                        return
                if stale:
                    self.respond_bytes(stale.status, stale.headers, stale.body, cache_state="STALE")
                    return
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

        if target_url.startswith(UPSTREAMS["alternative"]):
            try:
                req_headers = {
                    "User-Agent": BROWSER_UA,
                    "Accept": "application/json,text/plain,*/*",
                    "Accept-Language": "en-US,en;q=0.9",
                    "Referer": "https://alternative.me/",
                }
                logging.debug("upstream alternative req url=%s headers=%s", target_url, req_headers)
                status, resp_headers, body, headers_raw = self.curl_get_status_body(target_url, req_headers)
                logging.debug("upstream alternative resp status=%s headers=%s", status, resp_headers)
                logging.debug("upstream alternative resp headers_raw=%s", headers_raw)
                headers = {"Content-Type": "application/json; charset=utf-8"}
                if status == 200:
                    self.state.set_cache(cache_key, status, headers, body)
                self.respond_bytes(status, headers, body, cache_state="MISS")
                return
            except Exception as e:
                if stale:
                    self.respond_bytes(stale.status, stale.headers, stale.body, cache_state="STALE")
                    return
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

        try:
            req_headers = {
                "User-Agent": BROWSER_UA,
                "Accept": "application/json,text/plain,*/*",
                "Accept-Language": "en-US,en;q=0.9",
            }
            logging.debug("upstream generic req url=%s headers=%s", target_url, req_headers)
            req = urllib.request.Request(
                target_url,
                headers=req_headers,
                method="GET",
            )
            with urllib.request.urlopen(req, timeout=self.state.timeout_seconds) as resp:
                body = resp.read()
                headers = {k: v for k, v in resp.headers.items()}
                status = resp.status
                logging.debug("upstream generic resp status=%s headers=%s", status, headers)
        except urllib.error.HTTPError as e:
            logging.debug("upstream generic HTTPError code=%s headers=%s", e.code, dict(e.headers.items()) if e.headers else {})
            if parsed.path.startswith("/yahoo/quote") and e.code in (401, 403, 429):
                retry = self.build_yahoo_quote_with_crumb(parsed)
                if retry is not None:
                    headers = {"Content-Type": "application/json; charset=utf-8"}
                    self.state.set_cache(cache_key, 200, headers, retry)
                    self.respond_bytes(200, headers, retry, cache_state="MISS-CRUMB")
                    return
                if stale:
                    self.respond_bytes(stale.status, stale.headers, stale.body, cache_state="STALE")
                    return
            if target_url.startswith(UPSTREAMS["cnn"]) and stale:
                logging.warning("cnn upstream HTTP %s, serving stale cache for %s", e.code, target_url)
                self.respond_bytes(stale.status, stale.headers, stale.body, cache_state="STALE")
                return
            body = e.read()
            self.respond_bytes(
                e.code,
                {"Content-Type": e.headers.get("Content-Type", "text/plain; charset=utf-8")},
                body or str(e).encode("utf-8"),
                cache_state="MISS",
            )
            return
        except Exception as e:
            if parsed.path.startswith("/yahoo/quote"):
                retry = self.build_yahoo_quote_with_crumb(parsed)
                if retry is not None:
                    headers = {"Content-Type": "application/json; charset=utf-8"}
                    self.state.set_cache(cache_key, 200, headers, retry)
                    self.respond_bytes(200, headers, retry, cache_state="MISS-CRUMB")
                    return
                if stale:
                    self.respond_bytes(stale.status, stale.headers, stale.body, cache_state="STALE")
                    return
            if target_url.startswith(UPSTREAMS["cnn"]) and stale:
                logging.warning("cnn upstream error (%s), serving stale cache for %s", e, target_url)
                self.respond_bytes(stale.status, stale.headers, stale.body, cache_state="STALE")
                return
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
        ttl_override = CNN_CACHE_TTL_SECONDS if target_url.startswith(UPSTREAMS["cnn"]) else None
        self.state.set_cache(cache_key, status, normalized_headers, body, ttl_seconds=ttl_override)
        self.respond_bytes(status, normalized_headers, body, cache_state="MISS")

    def build_yahoo_quote_with_crumb(self, parsed: urllib.parse.ParseResult) -> bytes | None:
        params = urllib.parse.parse_qs(parsed.query, keep_blank_values=False)
        raw_symbols = params.get("symbols", [""])[0]
        symbols = ",".join([s.strip().upper() for s in raw_symbols.split(",") if s.strip()])
        if symbols == "":
            return None

        for attempt in range(2):
            force_refresh = attempt == 1
            cookie, crumb = self.get_yahoo_auth(force_refresh=force_refresh)
            if cookie == "" or crumb == "":
                logging.warning("yahoo quote retry skipped: missing auth (attempt=%d)", attempt + 1)
                continue

            q = urllib.parse.urlencode(
                {
                    "symbols": symbols,
                    "fields": "currency,priceHint,regularMarketChange,regularMarketChangePercent,regularMarketPrice,shortName,symbol",
                    "crumb": crumb,
                }
            )
            url = join_upstream(UPSTREAMS["yahoo"], "/v7/finance/quote", q)
            req_headers = {
                "User-Agent": YAHOO_UA,
                "Accept": "application/json,text/plain,*/*",
                "Accept-Language": "en-US,en;q=0.9",
                "Cookie": cookie,
                "Referer": "https://finance.yahoo.com/",
            }
            try:
                logging.debug("yahoo quote crumb req url=%s headers=%s", url, req_headers)
                status, resp_headers, body, headers_raw = self.curl_get_status_body(url, req_headers)
                logging.debug("yahoo quote crumb resp status=%s headers=%s", status, resp_headers)
                logging.debug("yahoo quote crumb resp headers_raw=%s", headers_raw)
                if status != 200:
                    if status in (401, 403):
                        logging.warning("yahoo quote unauthorized with crumb (attempt=%d code=%d), clearing auth", attempt + 1, status)
                        self.clear_yahoo_auth()
                        continue
                    continue
                obj = json.loads(body.decode("utf-8", errors="replace"))
                result = (((obj or {}).get("quoteResponse") or {}).get("result") or [])
                if isinstance(result, list) and len(result) > 0:
                    return json.dumps(obj, ensure_ascii=False).encode("utf-8")
            except Exception:
                pass
        return None

    def build_yahoo_chart_with_cookie(self, target_url: str) -> tuple[int, bytes | None]:
        for attempt in range(2):
            cookie = self.state.yahoo_cookie
            if not cookie:
                cookie = self.refresh_yahoo_cookie(force_refresh=(attempt == 1))
            if not cookie:
                continue
            req_headers = {
                "User-Agent": YAHOO_UA,
                "Accept": "application/json,text/plain,*/*",
                "Accept-Language": "en-US,en;q=0.9",
                "Cookie": cookie,
                "Referer": "https://finance.yahoo.com/",
            }
            try:
                logging.debug("yahoo chart cookie req url=%s headers=%s", target_url, req_headers)
                status, resp_headers, body, headers_raw = self.curl_get_status_body(target_url, req_headers)
                logging.debug("yahoo chart cookie resp status=%s headers=%s", status, resp_headers)
                logging.debug("yahoo chart cookie resp headers_raw=%s", headers_raw)
                if status == 200:
                    body = self.trim_yahoo_chart_body(body, max_points=64)
                    return 200, body
                if status in (401, 403, 429):
                    self.clear_yahoo_auth()
                    continue
                return status, None
            except Exception:
                continue
        return 0, None

    def trim_yahoo_chart_body(self, body: bytes, max_points: int = 64) -> bytes:
        if max_points <= 0:
            return body
        try:
            obj = json.loads(body.decode("utf-8", errors="replace"))
            chart = obj.get("chart") if isinstance(obj, dict) else None
            results = chart.get("result") if isinstance(chart, dict) else None
            if not isinstance(results, list):
                return body

            changed = False
            for item in results:
                if not isinstance(item, dict):
                    continue
                ts = item.get("timestamp")
                if isinstance(ts, list) and len(ts) > max_points:
                    item["timestamp"] = ts[-max_points:]
                    changed = True
                indicators = item.get("indicators")
                if not isinstance(indicators, dict):
                    continue
                for key in ("quote", "adjclose"):
                    arr = indicators.get(key)
                    if not isinstance(arr, list):
                        continue
                    for row in arr:
                        if not isinstance(row, dict):
                            continue
                        for k, v in list(row.items()):
                            if isinstance(v, list) and len(v) > max_points:
                                row[k] = v[-max_points:]
                                changed = True
            if not changed:
                return body
            return json.dumps(obj, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        except Exception:
            return body

    def refresh_yahoo_cookie(self, force_refresh: bool = False) -> str:
        now = time.time()
        if (not force_refresh) and self.state.yahoo_cookie and self.state.yahoo_auth_expires_at > now:
            return self.state.yahoo_cookie

        fc_headers = {"User-Agent": YAHOO_UA}
        if self.state.yahoo_cookie:
            fc_headers["Cookie"] = self.state.yahoo_cookie
        jar_file = ""
        try:
            jar_file = os.path.join(
                tempfile.gettempdir(),
                f"pixel_yahoo_cookie_{os.getpid()}_{int(time.time() * 1000)}.cookie",
            )
            with open(jar_file, "w", encoding="utf-8") as f:
                f.write("")
            set_cookie = self.curl_get_first_set_cookie(
                "https://fc.yahoo.com",
                fc_headers,
                method="POST",
                data="",
                cookie_jar=jar_file,
            )
            cookie = self.extract_a3_cookie(set_cookie)
            if not cookie:
                cookie = self.extract_a3_cookie(self.cookie_header_from_jar(jar_file))
            if cookie:
                self.state.yahoo_cookie = cookie
                # Reuse same ttl window used by crumb auth cache.
                self.state.yahoo_auth_expires_at = now + 15 * 60
                return cookie
        except Exception:
            pass
        finally:
            try:
                if jar_file and os.path.exists(jar_file):
                    os.remove(jar_file)
            except Exception:
                pass
        return ""

    def get_yahoo_auth(self, force_refresh: bool = False) -> tuple[str, str]:
        now = time.time()
        if (not force_refresh) and self.state.yahoo_cookie and self.state.yahoo_crumb and self.state.yahoo_auth_expires_at > now:
            return self.state.yahoo_cookie, self.state.yahoo_crumb

        # Step 1 (same as pro-english): request fc.yahoo.com and read Set-Cookie
        # even when status is 404.
        fc_headers = {"User-Agent": YAHOO_UA}
        if self.state.yahoo_cookie:
            fc_headers["Cookie"] = self.state.yahoo_cookie
        try:
            jar_file = os.path.join(
                tempfile.gettempdir(),
                f"pixel_yahoo_{os.getpid()}_{int(time.time() * 1000)}.cookie",
            )
            with open(jar_file, "w", encoding="utf-8") as f:
                f.write("")

            set_cookie = self.curl_get_first_set_cookie(
                "https://fc.yahoo.com",
                fc_headers,
                method="POST",
                data="",
                cookie_jar=jar_file,
            )
            cookie = self.extract_a3_cookie(set_cookie)
            logging.info("yahoo fc set-cookie raw_len=%d cookie_len=%d raw_head=%s cookie_head=%s",
                         len(set_cookie),
                         len(cookie),
                         set_cookie[:96] if set_cookie else "",
                         cookie[:96] if cookie else "")
            logging.debug("yahoo fc set-cookie raw_full=%s", set_cookie)
            logging.debug("yahoo fc cookie normalized_full=%s", cookie)
            if not set_cookie and not cookie:
                self.clear_yahoo_auth()
                return "", ""

            jar_cookie_full = self.cookie_header_from_jar(jar_file)
            candidates = []
            for c in (cookie, jar_cookie_full, set_cookie, self.state.yahoo_cookie):
                if c and c not in candidates:
                    candidates.append(c)
            if not candidates:
                candidates.append("")

            crumb = ""
            success_cookie = ""
            crumb_status = 0
            crumb_resp_headers: dict[str, str] = {}
            crumb_headers_raw = ""
            crumb_body = b""
            for idx, cookie_for_crumb in enumerate(candidates):
                crumb_headers = {"User-Agent": YAHOO_UA}
                if cookie_for_crumb:
                    crumb_headers["Cookie"] = cookie_for_crumb
                logging.debug("yahoo crumb request cookie_full=%s", cookie_for_crumb)
                logging.debug("yahoo crumb req url=%s headers=%s", "https://query1.finance.yahoo.com/v1/test/getcrumb", crumb_headers)
                crumb_status, crumb_resp_headers, crumb_body, crumb_headers_raw = self.curl_get_status_body(
                    "https://query1.finance.yahoo.com/v1/test/getcrumb",
                    crumb_headers,
                    method="POST",
                    data="",
                    cookie_jar=jar_file,
                )
                if crumb_status == 405:
                    crumb_status, crumb_resp_headers, crumb_body, crumb_headers_raw = self.curl_get_status_body(
                        "https://query1.finance.yahoo.com/v1/test/getcrumb",
                        crumb_headers,
                        method="GET",
                        data=None,
                        cookie_jar=jar_file,
                    )
                logging.debug("yahoo crumb resp status=%s headers=%s", crumb_status, crumb_resp_headers)
                logging.debug("yahoo crumb resp headers_raw=%s", crumb_headers_raw)
                crumb = crumb_body.decode("utf-8", errors="replace").strip() if crumb_status == 200 else ""
                if crumb:
                    success_cookie = cookie_for_crumb
                    break
                if idx + 1 < len(candidates):
                    logging.debug("yahoo crumb retry with next cookie candidate (%d/%d)", idx + 2, len(candidates))
            if crumb_status != 200:
                logging.warning("yahoo crumb fetch failed status=%d", crumb_status)
                body_preview = crumb_body.decode("utf-8", errors="replace")[:120] if crumb_body else ""
                logging.warning("yahoo crumb body preview=%s", body_preview)
                if crumb_status == 403 and ("<title>Yahoo</title>" in body_preview or "<html lang=\"zh\">" in body_preview):
                    if not self.state.curl_proxy:
                        logging.warning("yahoo direct egress appears blocked; set --curl-proxy or PIXEL_PROXY_CURL_PROXY")
            if crumb:
                cookie_from_jar = self.cookie_header_from_jar(jar_file)
                preferred_cookie = (
                    self.extract_a3_cookie(success_cookie)
                    or self.extract_a3_cookie(cookie_from_jar)
                    or cookie
                    or self.extract_a3_cookie(set_cookie)
                )
                self.state.yahoo_cookie = preferred_cookie
                self.state.yahoo_crumb = crumb
                self.state.yahoo_auth_expires_at = now + 15 * 60
                logging.info("yahoo auth refreshed cookie_len=%d crumb_len=%d", len(preferred_cookie), len(crumb))
                return preferred_cookie, crumb
            logging.warning("yahoo auth missing crumb")
        except Exception as e:
            logging.warning("yahoo auth error: %s", e)
        finally:
            try:
                if 'jar_file' in locals() and jar_file and os.path.exists(jar_file):
                    os.remove(jar_file)
            except Exception:
                pass
        self.clear_yahoo_auth()
        return "", ""

    def clear_yahoo_auth(self) -> None:
        self.state.yahoo_cookie = ""
        self.state.yahoo_crumb = ""
        self.state.yahoo_auth_expires_at = 0.0

    def parse_headers_raw(self, headers_raw: str) -> dict[str, str]:
        out: dict[str, str] = {}
        if not headers_raw:
            return out
        for ln in headers_raw.splitlines():
            if ":" not in ln:
                continue
            k, v = ln.split(":", 1)
            key = k.strip()
            if not key:
                continue
            val = v.strip()
            if key in out and out[key]:
                out[key] = out[key] + "; " + val
            else:
                out[key] = val
        return out

    def curl_get_status_body(self,
                             target_url: str,
                             headers: dict[str, str] | None = None,
                             method: str = "GET",
                             data: str | None = None,
                             cookie_jar: str | None = None) -> tuple[int, dict[str, str], bytes, str]:
        timeout_s = max(5, int(self.state.timeout_seconds))
        marker = "__HTTP_STATUS__:"
        marker2 = "__HEADER_SIZE__:"
        cmd = [
            "curl",
            "-sS",
            "-L",
            "-m",
            str(timeout_s),
            "-i",
            "-X",
            method,
            target_url,
            "-w",
            f"\n{marker}%{{http_code}}\n{marker2}%{{size_header}}",
        ]
        if headers:
            for k, v in headers.items():
                cmd.extend(["-H", f"{k}: {v}"])
        if self.state.curl_proxy:
            cmd.extend(["-x", self.state.curl_proxy])
        if cookie_jar:
            cmd.extend(["-b", cookie_jar, "-c", cookie_jar])
        if data is not None:
            cmd.extend(["--data", data])
        proc = subprocess.run(cmd, capture_output=True, check=False)
        if proc.returncode != 0:
            detail = proc.stderr.decode("utf-8", errors="replace").strip() or f"curl exit {proc.returncode}"
            raise RuntimeError(detail)
        raw = proc.stdout
        marker_b = marker.encode("utf-8")
        idx = raw.rfind(marker_b)
        if idx < 0:
            raise RuntimeError("missing http status marker")
        payload = raw[:idx]
        trailer = raw[idx + len(marker_b):].decode("utf-8", errors="replace")
        lines = trailer.splitlines()
        status_text = lines[0].strip() if len(lines) > 0 else ""
        header_size = 0
        if len(lines) > 1 and lines[1].startswith(marker2):
            try:
                header_size = int(lines[1][len(marker2):].strip())
            except Exception:
                header_size = 0
        status = int(status_text)
        if header_size < 0:
            header_size = 0
        if header_size > len(payload):
            header_size = len(payload)
        headers_raw_bytes = payload[:header_size]
        body = payload[header_size:]
        headers_raw = headers_raw_bytes.decode("utf-8", errors="replace")
        headers_map = self.parse_headers_raw(headers_raw)
        return status, headers_map, body, headers_raw

    def curl_get_first_set_cookie(self,
                                  target_url: str,
                                  headers: dict[str, str] | None = None,
                                  method: str = "GET",
                                  data: str | None = None,
                                  cookie_jar: str | None = None) -> str:
        timeout_s = max(5, int(self.state.timeout_seconds))
        logging.debug("curl set-cookie req url=%s headers=%s", target_url, headers or {})
        cmd = [
            "curl",
            "-sS",
            "-L",
            "-m",
            str(timeout_s),
            "-i",
            "-X",
            method,
            target_url,
        ]
        if headers:
            for k, v in headers.items():
                cmd.extend(["-H", f"{k}: {v}"])
        if self.state.curl_proxy:
            cmd.extend(["-x", self.state.curl_proxy])
        if cookie_jar:
            cmd.extend(["-b", cookie_jar, "-c", cookie_jar])
        if data is not None:
            cmd.extend(["--data", data])
        proc = subprocess.run(cmd, capture_output=True, check=False)
        if proc.returncode != 0:
            return ""
        text = proc.stdout.decode("utf-8", errors="replace")
        logging.debug("curl set-cookie resp raw_headers=%s", text.split("\r\n\r\n", 1)[0] if "\r\n\r\n" in text else text.split("\n\n", 1)[0])
        lines = text.splitlines()
        for ln in lines:
            low = ln.lower()
            if low.startswith("set-cookie:"):
                return ln.split(":", 1)[1].strip()
        return ""

    def normalize_set_cookie_header(self, raw: str) -> str:
        text = (raw or "").strip()
        if not text:
            return ""
        # Convert Set-Cookie lines to Cookie header form:
        # keep only key=value pair before first ';' for each cookie.
        pairs = []
        seen = set()
        for part in re.split(r";\s*(?=[A-Za-z0-9_\-]+=)", text):
            item = part.strip()
            if not item:
                continue
            first = item.split(";", 1)[0].strip()
            if "=" not in first:
                continue
            k, v = first.split("=", 1)
            k = k.strip()
            v = v.strip()
            if not k:
                continue
            low = k.lower()
            if low in {"expires", "max-age", "domain", "path", "samesite", "secure", "httponly"}:
                continue
            kv = f"{k}={v}"
            if k not in seen:
                seen.add(k)
                pairs.append(kv)
        return "; ".join(pairs)

    def extract_a3_cookie(self, raw: str) -> str:
        text = (raw or "").strip()
        if not text:
            return ""
        m = re.search(r"(?:^|;\s*)A3=([^;]+)", text, re.IGNORECASE)
        if not m:
            return ""
        return "A3=" + m.group(1).strip()

    def cookie_header_from_jar(self, jar_file: str) -> str:
        if not jar_file or not os.path.exists(jar_file):
            return ""
        pairs: list[str] = []
        try:
            with open(jar_file, "r", encoding="utf-8", errors="replace") as f:
                for ln in f:
                    s = ln.strip()
                    if not s or s.startswith("#"):
                        continue
                    cols = s.split("\t")
                    if len(cols) < 7:
                        continue
                    name = cols[5].strip()
                    val = cols[6].strip()
                    if not name or not val:
                        continue
                    pairs.append(f"{name}={val}")
        except Exception:
            return ""
        return "; ".join(pairs)

    def fetch_cnn_with_browser_headers(self, target_url: str) -> tuple[int, dict[str, str], bytes]:
        timeout_s = max(5, int(self.state.timeout_seconds))
        marker = "__HTTP_STATUS__:"
        cmd = [
            "curl",
            "-sS",
            "-m",
            str(timeout_s),
            target_url,
            "-H",
            "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36",
            "-H",
            "Accept: application/json,text/plain,*/*",
            "-H",
            "Referer: https://www.cnn.com/markets/fear-and-greed",
            "-H",
            "Origin: https://www.cnn.com",
            "-H",
            "Accept-Language: en-US,en;q=0.9",
            "-w",
            f"\n{marker}%{{http_code}}",
        ]
        if self.state.curl_proxy:
            cmd.extend(["-x", self.state.curl_proxy])
        proc = subprocess.run(cmd, capture_output=True, check=False)
        if proc.returncode != 0:
            detail = proc.stderr.decode("utf-8", errors="replace").strip() or f"curl exit {proc.returncode}"
            raise RuntimeError(detail)
        raw = proc.stdout
        idx = raw.rfind(marker.encode("utf-8"))
        if idx < 0:
            raise RuntimeError("missing http status marker")
        body = raw[:idx]
        status_text = raw[idx + len(marker):].strip().decode("utf-8", errors="replace")
        try:
            status = int(status_text)
        except Exception as e:
            raise RuntimeError(f"invalid status marker: {status_text}") from e
        if status < 200 or status >= 300:
            raise RuntimeError(f"cnn upstream http {status}")
        return status, {"Content-Type": "application/json; charset=utf-8"}, body

    def respond_bytes(self, status: int, headers: dict[str, str], body: bytes, cache_state: str) -> None:
        self.send_response(status)
        for key, value in headers.items():
            if key.lower() in {"content-length", "connection", "transfer-encoding", "server", "date"}:
                continue
            self.send_header(key, value)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("X-Proxy-Cache", cache_state)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            logging.warning("client disconnected while sending bytes response")

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
    parser.add_argument("--curl-proxy", default=os.environ.get("PIXEL_PROXY_CURL_PROXY", ""))
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    logging.basicConfig(level=getattr(logging, str(args.log_level).upper(), logging.INFO), format="%(asctime)s %(levelname)s %(message)s")

    server = ThreadingHTTPServer((args.host, args.port), ProxyHandler)
    server.state = ProxyState(ttl_seconds=args.ttl, timeout_seconds=args.timeout)  # type: ignore[attr-defined]
    cli_proxy = str(args.curl_proxy or "").strip()
    if cli_proxy:
        server.state.curl_proxy = cli_proxy  # type: ignore[attr-defined]
        proxy_source = "cli/env"
    else:
        detected = detect_system_curl_proxy()
        server.state.curl_proxy = detected  # type: ignore[attr-defined]
        proxy_source = "macos-system" if detected else "none"

    logging.info("dev_api_proxy listening on http://%s:%d", args.host, args.port)
    logging.info("preset routes: %s", ", ".join(sorted(ROUTES.keys())))
    if server.state.curl_proxy:  # type: ignore[attr-defined]
        logging.info("curl upstream proxy: %s (source=%s)", server.state.curl_proxy, proxy_source)  # type: ignore[attr-defined]
    else:
        logging.info("curl upstream proxy: <none>")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        logging.info("dev_api_proxy shutting down")
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
