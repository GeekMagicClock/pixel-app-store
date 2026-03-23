#!/usr/bin/env python3
import argparse
import gzip
import json
import os
import pathlib
import posixpath
import shlex
import subprocess
import sys
import urllib.request
import urllib.parse


SCRIPT_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent
DEFAULT_APPS_ROOT = REPO_ROOT / "data_littlefs" / "apps"
DEFAULT_OUT_DIR = REPO_ROOT / "dist" / "store" / "pixel64x32V2" / "apps"
DEFAULT_SSH_USER = os.environ.get("PIXEL_STORE_SSH_USER", "root")
DEFAULT_SSH_HOST = os.environ.get("PIXEL_STORE_SSH_HOST", "111.229.177.3")
DEFAULT_SSH_PORT = int(os.environ.get("PIXEL_STORE_SSH_PORT", "22"))
DEFAULT_SSH_KEY = os.environ.get("PIXEL_STORE_SSH_KEY", str(pathlib.Path("~/.ssh/t20260309.pem").expanduser()))
DEFAULT_REMOTE_DIR = os.environ.get("PIXEL_STORE_REMOTE_DIR", "/root/fw/pixel64x32V2/apps")
DEFAULT_REMOTE_WEB_ROOT = os.environ.get("PIXEL_STORE_REMOTE_WEB_ROOT", "/root")
DEFAULT_HTTP_HOST = os.environ.get("PIXEL_STORE_HTTP_HOST", "ota.geekmagic.cc")
DEFAULT_HTTP_PORT = int(os.environ.get("PIXEL_STORE_HTTP_PORT", "8001"))
DEFAULT_HTTP_BASE_PATH = os.environ.get("PIXEL_STORE_HTTP_BASE_PATH", "/fw/pixel64x32V2/apps")
DEFAULT_HTTP_BIND = os.environ.get("PIXEL_STORE_HTTP_BIND", "0.0.0.0")
THUMBNAIL_NAMES = ("thumbnail.png", "thumbnail.jpg", "thumbnail.jpeg", "thumb.png", "preview.png")


def run(cmd: list[str], *, cwd: pathlib.Path | None = None, check: bool = True) -> subprocess.CompletedProcess:
    print("+", " ".join(shlex.quote(part) for part in cmd))
    return subprocess.run(cmd, cwd=str(cwd) if cwd else None, check=check)


def parse_app_ids(values: list[str]) -> list[str]:
    seen: set[str] = set()
    ordered: list[str] = []
    for raw in values:
        for item in str(raw).split(","):
            app_id = item.strip()
            if not app_id or app_id in seen:
                continue
            seen.add(app_id)
            ordered.append(app_id)
    return ordered


def app_has_thumbnail(app_dir: pathlib.Path) -> bool:
    return any((app_dir / name).is_file() for name in THUMBNAIL_NAMES)


def select_thumbnail_apps(args: argparse.Namespace) -> list[str]:
    selected = parse_app_ids(args.thumbnail_apps)
    if selected or args.thumbnail_all:
        return selected

    if not args.apps_root.exists():
        return []

    return [
        app_dir.name
        for app_dir in sorted(args.apps_root.iterdir())
        if app_dir.is_dir() and not app_dir.name.startswith(".") and not app_has_thumbnail(app_dir)
    ]


def capture_or_generate_thumbnails(args: argparse.Namespace) -> None:
    if args.thumbnail_mode == "keep":
        return

    target_apps = select_thumbnail_apps(args)
    if not args.thumbnail_all and not target_apps:
        print("[thumbs] no missing app thumbnails detected; keeping current artwork")
        return

    if args.thumbnail_mode == "capture":
        if not args.device:
            raise SystemExit("--thumbnail-mode capture requires --device")
        cmd = [
            sys.executable,
            str(REPO_ROOT / "python" / "store" / "capture_real_thumbnails.py"),
            "--device",
            args.device,
            "--apps-root",
            str(args.apps_root),
        ]
        if args.overwrite_thumbnails:
            cmd.append("--overwrite")
        for app_id in target_apps:
            cmd.extend(["--apps", app_id])
        run(cmd, cwd=REPO_ROOT)
        return

    if args.thumbnail_mode == "generate":
        cmd = [
            sys.executable,
            str(REPO_ROOT / "python" / "store" / "generate_app_thumbnails.py"),
            "--apps-root",
            str(args.apps_root),
        ]
        if args.overwrite_thumbnails:
            cmd.append("--force")
        for app_id in target_apps:
            cmd.extend(["--apps", app_id])
        run(cmd, cwd=REPO_ROOT)
        return

    raise SystemExit(f"unsupported thumbnail mode: {args.thumbnail_mode}")


def build_base_url(args: argparse.Namespace) -> str:
    base_path = args.http_base_path if args.http_base_path.startswith("/") else "/" + args.http_base_path
    return f"http://{args.http_host}:{args.http_port}{base_path}".rstrip("/")


def build_store_index(args: argparse.Namespace, base_url: str) -> None:
    args.out_dir.mkdir(parents=True, exist_ok=True)

    cmd = [
        sys.executable,
        str(REPO_ROOT / "python" / "store" / "build_store_index.py"),
        "--apps-root",
        str(args.apps_root),
        "--out-dir",
        str(args.out_dir),
        "--base-url",
        base_url,
    ]
    if args.lua_bytecode:
        cmd.extend(
            [
                "--lua-bytecode",
                "--luac-bin",
                args.luac_bin,
                "--entry-name",
                args.entry_name,
            ]
        )
    run(cmd, cwd=REPO_ROOT)


def ssh_target(args: argparse.Namespace) -> str:
    return f"{args.ssh_user}@{args.ssh_host}"


def ssh_cmd(args: argparse.Namespace, remote_command: str) -> list[str]:
    return [
        "ssh",
        "-i",
        str(args.ssh_key),
        "-p",
        str(args.ssh_port),
        ssh_target(args),
        remote_command,
    ]


def build_remote_cors_server_command(port: int, bind: str, directory: str, log_path: str) -> str:
    server_script = r"""
import functools
import http.server
import os
import sys
import urllib.parse
from http import HTTPStatus

port = int(sys.argv[1])
bind = sys.argv[2]
directory = sys.argv[3]

class CORSRequestHandler(http.server.SimpleHTTPRequestHandler):
    def _gzip_index_path(self):
        req_path = urllib.parse.urlsplit(self.path).path or "/"
        if not req_path.endswith("/apps-index.json") and req_path != "/apps-index.json":
            return None
        fs_path = self.translate_path(req_path)
        gz_path = fs_path + ".gz"
        if not os.path.isfile(gz_path):
            return None
        return gz_path

    def send_head(self):
        gz_path = self._gzip_index_path()
        if gz_path:
            f = open(gz_path, "rb")
            try:
                fs = os.fstat(f.fileno())
                self.send_response(HTTPStatus.OK)
                self.send_header("Content-Type", "application/json; charset=utf-8")
                self.send_header("Content-Encoding", "gzip")
                self.send_header("Content-Length", str(fs.st_size))
                self.send_header("Cache-Control", "public, max-age=60, no-transform")
                self.send_header("Vary", "Accept-Encoding")
                self.end_headers()
                return f
            except Exception:
                f.close()
                raise
        return super().send_head()

    def end_headers(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, HEAD, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "*")
        super().end_headers()

    def do_OPTIONS(self):
        self.send_response(204)
        self.end_headers()

handler = functools.partial(CORSRequestHandler, directory=directory)
http.server.ThreadingHTTPServer((bind, port), handler).serve_forever()
""".strip()
    return (
        f"exec /usr/bin/python3 -c {shlex.quote(server_script)} "
        f"{shlex.quote(str(port))} {shlex.quote(bind)} {shlex.quote(directory)} "
        f"> {shlex.quote(log_path)} 2>&1"
    )


def sync_store(args: argparse.Namespace) -> None:
    remote_prep = (
        f"mkdir -p {shlex.quote(args.remote_dir)} && "
        f"find {shlex.quote(args.remote_dir)} -mindepth 1 -maxdepth 1 -exec rm -rf {{}} +"
    )
    run(ssh_cmd(args, remote_prep), cwd=REPO_ROOT)
    items = [str(item) for item in sorted(args.out_dir.iterdir())]
    if not items:
        raise RuntimeError(f"store output is empty: {args.out_dir}")
    cmd = [
        "scp",
        "-i",
        str(args.ssh_key),
        "-P",
        str(args.ssh_port),
        "-r",
        *items,
        f"{ssh_target(args)}:{args.remote_dir}/",
    ]
    run(cmd, cwd=REPO_ROOT)


def start_remote_server(args: argparse.Namespace) -> None:
    unit_name = f"pixel-store-http{args.http_port}"
    log_dir = posixpath.dirname(args.remote_dir.rstrip("/")) or "/tmp"
    log_path = posixpath.join(log_dir, f"http-{args.http_port}.log")
    inner = build_remote_cors_server_command(args.http_port, args.http_bind, args.remote_web_root, log_path)
    remote = (
        f"mkdir -p {shlex.quote(log_dir)} {shlex.quote(args.remote_dir)} && "
        f"systemctl stop {shlex.quote(unit_name)} >/dev/null 2>&1 || true && "
        f"systemd-run --unit {shlex.quote(unit_name)} "
        f"--property=WorkingDirectory={shlex.quote(args.remote_web_root)} "
        f"/bin/sh -lc {shlex.quote(inner)} >/dev/null && "
        f"sleep 1 && systemctl is-active {shlex.quote(unit_name)} >/dev/null && "
        f"ss -lntp | grep -q ':{args.http_port} '"
    )
    run(ssh_cmd(args, remote), cwd=REPO_ROOT)


def verify_store(base_url: str, timeout: float) -> None:
    index_url = base_url.rstrip("/") + "/apps-index.json"
    with urllib.request.urlopen(index_url, timeout=timeout) as response:
        payload_bytes = response.read()
        encoding = str(response.headers.get("Content-Encoding", "") or "")
    if "gzip" in encoding.lower() or payload_bytes[:2] == b"\x1f\x8b":
        try:
            payload_bytes = gzip.decompress(payload_bytes)
        except OSError:
            pass
    payload = json.loads(payload_bytes.decode("utf-8"))
    apps = payload.get("apps")
    if not isinstance(apps, list):
        raise RuntimeError(f"invalid apps-index.json at {index_url}: missing apps list")
    print(f"verified {index_url} ({len(apps)} apps)")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build and publish the Pixel app store over SSH")
    parser.add_argument("--apps-root", type=pathlib.Path, default=DEFAULT_APPS_ROOT)
    parser.add_argument("--out-dir", type=pathlib.Path, default=DEFAULT_OUT_DIR)
    parser.add_argument("--thumbnail-mode", choices=["keep", "capture", "generate"], default="keep")
    parser.add_argument("--thumbnail-apps", action="append", default=[], help="Only refresh thumbnails for these app ids (comma-separated or repeated)")
    parser.add_argument("--thumbnail-all", action="store_true", help="Allow thumbnail refresh to touch all apps instead of only apps missing artwork")
    parser.add_argument("--overwrite-thumbnails", action="store_true", help="Allow capture/generate to overwrite existing app thumbnails")
    parser.add_argument("--device", help="Device host or host:port for thumbnail capture")
    parser.add_argument("--lua-bytecode", action="store_true")
    parser.add_argument("--plain-lua", dest="lua_bytecode", action="store_false")
    parser.add_argument("--luac-bin", default="luac")
    parser.add_argument("--entry-name", default="app.bin")
    parser.add_argument("--ssh-user", default=DEFAULT_SSH_USER)
    parser.add_argument("--ssh-host", default=DEFAULT_SSH_HOST)
    parser.add_argument("--ssh-port", type=int, default=DEFAULT_SSH_PORT)
    parser.add_argument("--ssh-key", type=pathlib.Path, default=pathlib.Path(DEFAULT_SSH_KEY))
    parser.add_argument("--remote-dir", default=DEFAULT_REMOTE_DIR)
    parser.add_argument("--remote-web-root", default=DEFAULT_REMOTE_WEB_ROOT)
    parser.add_argument("--http-host", default=DEFAULT_HTTP_HOST)
    parser.add_argument("--http-port", type=int, default=DEFAULT_HTTP_PORT)
    parser.add_argument("--http-base-path", default=DEFAULT_HTTP_BASE_PATH)
    parser.add_argument("--http-bind", default=DEFAULT_HTTP_BIND)
    parser.add_argument("--skip-upload", action="store_true")
    parser.add_argument("--skip-serve", action="store_true")
    parser.add_argument("--skip-verify", action="store_true")
    parser.add_argument("--verify-timeout", type=float, default=15.0)
    parser.set_defaults(lua_bytecode=True)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    base_url = build_base_url(args)

    print("[1/4] prepare thumbnails")
    capture_or_generate_thumbnails(args)

    print("[2/4] build store packages")
    build_store_index(args, base_url)

    if not args.skip_upload:
        print("[3/4] upload store files")
        sync_store(args)
    else:
        print("[3/4] upload skipped")

    if not args.skip_serve:
        print("[4/4] start remote web service")
        start_remote_server(args)
    else:
        print("[4/4] remote web service start skipped")

    if not args.skip_verify:
        verify_store(base_url, args.verify_timeout)

    print("publish complete")
    print(f"store base: {base_url}/")
    print(f"index: {base_url}/apps-index.json")


if __name__ == "__main__":
    main()
