#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import logging
import mimetypes
import os
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import quote


def _norm_channel(value: str) -> str:
    return "beta" if str(value or "").strip().lower() == "beta" else "stable"


def _safe_join(root: Path, rel: str) -> Path | None:
    rel_path = Path(rel)
    if rel_path.is_absolute():
        return None
    full = (root / rel_path).resolve()
    try:
        full.relative_to(root.resolve())
    except Exception:
        return None
    return full


class StoreServerState:
    def __init__(
        self,
        *,
        board: str,
        data_root: Path,
        public_base: str,
        default_channel: str,
        stable_zip_base: str,
        beta_zip_base: str,
        github_repo: str = "",
        github_branch: str = "main",
        github_dist_prefix: str = "dist/store",
        cache_seconds: int,
    ) -> None:
        self.board = board
        self.data_root = data_root
        self.public_base = public_base.rstrip("/")
        self.default_channel = _norm_channel(default_channel)
        self.stable_zip_base = stable_zip_base.rstrip("/")
        self.beta_zip_base = beta_zip_base.rstrip("/")
        self.github_repo = str(github_repo or "").strip()
        self.github_branch = str(github_branch or "main").strip() or "main"
        self.github_dist_prefix = str(github_dist_prefix or "dist/store").strip().strip("/")
        self.cache_seconds = max(0, int(cache_seconds))

    def channel_root(self, channel: str) -> Path:
        return self.data_root / self.board / _norm_channel(channel)

    def index_url(self, channel: str) -> str:
        return f"{self.public_base}/{self.board}/{_norm_channel(channel)}/apps-index.json"

    def zip_base_for_channel(self, channel: str) -> str:
        return self.beta_zip_base if _norm_channel(channel) == "beta" else self.stable_zip_base

    def describe_source(self) -> str:
        if self.github_repo:
            return f"github:{self.github_repo}@{self.github_branch}/{self.github_dist_prefix}/{self.board}/apps"
        return "custom-zip-base"


class StoreHandler(BaseHTTPRequestHandler):
    server_version = "PixelOtaStore/1.0"

    @property
    def state(self) -> StoreServerState:
        return self.server.state  # type: ignore[attr-defined]

    def do_OPTIONS(self) -> None:
        self.send_response(HTTPStatus.NO_CONTENT)
        self.send_cors_headers()
        self.end_headers()

    def do_GET(self) -> None:
        path = self.path.split("?", 1)[0]
        if path == "/healthz":
            self.send_json(HTTPStatus.OK, {"ok": True, "service": "ota_store_server"})
            return
        if path == "/api/store/index":
            self.handle_store_index()
            return
        if path.startswith(f"/apps/{self.state.board}/"):
            self.handle_apps_path(path)
            return
        self.send_error_json(HTTPStatus.NOT_FOUND, "not found")

    def handle_store_index(self) -> None:
        stable_url = self.state.index_url("stable")
        beta_url = self.state.index_url("beta")
        default_url = beta_url if self.state.default_channel == "beta" else stable_url
        self.send_json(
            HTTPStatus.OK,
            {
                "ok": True,
                "default_channel": self.state.default_channel,
                "stable_index_url": stable_url,
                "beta_index_url": beta_url,
                "default_index_url": default_url,
            },
        )

    def handle_apps_path(self, path: str) -> None:
        # /apps/{board}/{channel}/redirect/<zip_name>
        prefix = f"/apps/{self.state.board}/"
        remainder = path[len(prefix) :]
        parts = [p for p in remainder.split("/") if p]
        if len(parts) < 2:
            self.send_error_json(HTTPStatus.NOT_FOUND, "invalid apps path")
            return
        channel = _norm_channel(parts[0])
        if parts[1] == "redirect":
            if len(parts) < 3:
                self.send_error_json(HTTPStatus.BAD_REQUEST, "missing zip filename")
                return
            zip_name = parts[-1]
            target = f"{self.state.zip_base_for_channel(channel)}/{quote(zip_name)}"
            self.send_response(HTTPStatus.FOUND)
            self.send_header("Location", target)
            self.send_header("Cache-Control", "public, max-age=60")
            self.send_cors_headers()
            self.end_headers()
            return

        channel_root = self.state.channel_root(channel)
        rel = "/".join(parts[1:])
        fs_path = _safe_join(channel_root, rel)
        if fs_path is None or not fs_path.exists() or not fs_path.is_file():
            self.send_error_json(HTTPStatus.NOT_FOUND, "file not found")
            return
        self.send_file(fs_path)

    def send_file(self, path: Path) -> None:
        content_type = mimetypes.guess_type(str(path))[0] or "application/octet-stream"
        if path.name.endswith(".json"):
            content_type = "application/json; charset=utf-8"
        try:
            data = path.read_bytes()
        except Exception as exc:
            logging.exception("read file failed: %s", exc)
            self.send_error_json(HTTPStatus.INTERNAL_SERVER_ERROR, "read file failed")
            return

        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", f"public, max-age={self.state.cache_seconds}")
        self.send_cors_headers()
        self.end_headers()
        self.wfile.write(data)

    def send_json(self, status: HTTPStatus, payload: dict) -> None:
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_cors_headers()
        self.end_headers()
        self.wfile.write(body)

    def send_error_json(self, status: HTTPStatus, message: str) -> None:
        self.send_json(status, {"ok": False, "error": message})

    def log_message(self, fmt: str, *args) -> None:  # noqa: A003
        logging.info("%s - %s", self.address_string(), fmt % args)

    def send_cors_headers(self) -> None:
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, HEAD, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "*")


def main() -> int:
    ap = argparse.ArgumentParser(description="OTA app-store server for stable/beta index + 302 zip redirect")
    ap.add_argument("--host", default=os.environ.get("PIXEL_STORE_HOST", "0.0.0.0"))
    ap.add_argument("--port", type=int, default=int(os.environ.get("PIXEL_STORE_PORT", "8001")))
    ap.add_argument("--board", default=os.environ.get("PIXEL_STORE_BOARD", "pixel64x32V2"))
    ap.add_argument("--data-root", default=os.environ.get("PIXEL_STORE_DATA_ROOT", str(Path("dist/store").resolve())))
    ap.add_argument("--public-base", default=os.environ.get("PIXEL_STORE_PUBLIC_BASE", "https://ota.geekmagic.cc/apps"))
    ap.add_argument("--default-channel", choices=("stable", "beta"), default=os.environ.get("PIXEL_STORE_DEFAULT_CHANNEL", "stable"))
    ap.add_argument("--github-repo", default=os.environ.get("PIXEL_STORE_GITHUB_REPO", ""))
    ap.add_argument("--github-branch", default=os.environ.get("PIXEL_STORE_GITHUB_BRANCH", "main"))
    ap.add_argument("--github-dist-prefix", default=os.environ.get("PIXEL_STORE_GITHUB_DIST_PREFIX", "dist/store"))
    ap.add_argument(
        "--stable-zip-base",
        default=os.environ.get("PIXEL_STORE_STABLE_ZIP_BASE", ""),
        help="redirect target base for stable zips (e.g. GitHub Releases URL)",
    )
    ap.add_argument(
        "--beta-zip-base",
        default=os.environ.get("PIXEL_STORE_BETA_ZIP_BASE", ""),
        help="redirect target base for beta zips (e.g. GitHub Releases URL)",
    )
    ap.add_argument("--cache-seconds", type=int, default=int(os.environ.get("PIXEL_STORE_CACHE_SECONDS", "60")))
    ap.add_argument("--log-level", default=os.environ.get("PIXEL_STORE_LOG_LEVEL", "INFO"))
    args = ap.parse_args()

    stable_zip_base = str(args.stable_zip_base or "").strip()
    beta_zip_base = str(args.beta_zip_base or "").strip()
    github_repo = str(args.github_repo or "").strip()
    github_branch = str(args.github_branch or "main").strip() or "main"
    github_dist_prefix = str(args.github_dist_prefix or "dist/store").strip().strip("/")
    if github_repo and (not stable_zip_base or not beta_zip_base):
        raw_base = f"https://raw.githubusercontent.com/{github_repo}/{github_branch}/{github_dist_prefix}/{args.board}/apps"
        stable_zip_base = stable_zip_base or raw_base
        beta_zip_base = beta_zip_base or raw_base
    if not stable_zip_base or not beta_zip_base:
        raise SystemExit("stable/beta zip bases are required (via flags or PIXEL_STORE_STABLE_ZIP_BASE / PIXEL_STORE_BETA_ZIP_BASE)")

    logging.basicConfig(level=getattr(logging, str(args.log_level).upper(), logging.INFO), format="%(asctime)s %(levelname)s %(message)s")
    state = StoreServerState(
        board=args.board,
        data_root=Path(args.data_root).resolve(),
        public_base=args.public_base,
        default_channel=args.default_channel,
        stable_zip_base=stable_zip_base,
        beta_zip_base=beta_zip_base,
        github_repo=github_repo,
        github_branch=github_branch,
        github_dist_prefix=github_dist_prefix,
        cache_seconds=args.cache_seconds,
    )
    server = ThreadingHTTPServer((args.host, args.port), StoreHandler)
    server.state = state  # type: ignore[attr-defined]
    logging.info("ota_store_server listening on http://%s:%d", args.host, args.port)
    logging.info("index stable=%s", state.index_url("stable"))
    logging.info("index beta=%s", state.index_url("beta"))
    logging.info("zip source=%s", state.describe_source())
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        logging.info("ota_store_server shutting down")
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
