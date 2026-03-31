#!/usr/bin/env python3
from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
APPS_DIR = ROOT / "data_littlefs" / "apps"
DEFAULT_DIST_ROOT = ROOT / "dist" / "store"
DEFAULT_BOARD = "pixel64x32V2"
DEFAULT_REMOTE = "root@111.229.177.3:/root/fw/pixel64x32V2/apps"
DEFAULT_PUBLIC_BASE = "http://ota.geekmagic.cc:8001/fw/pixel64x32V2/apps"
DEFAULT_HTTP_HOST = "ota.geekmagic.cc"
DEFAULT_HTTP_PORT = 8001
DEFAULT_SSH_PORT = 22
DEFAULT_LUAC_TOOL = ROOT / "python" / "store" / "tools" / "luac-esp-compat"

CATEGORY_LABELS = {
    "art": "Art",
    "astronomy": "Astronomy",
    "clock": "Clock",
    "finance": "Finance",
    "game": "Arcade Scenes",
    "games": "Arcade Scenes",
    "other": "Other",
    "scene": "Scene",
    "system": "System",
    "tool": "Tool",
    "weather": "Weather",
}

CORS_SERVER_SCRIPT = r"""
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


def run(cmd: list[str], cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    print("+", " ".join(shlex.quote(x) for x in cmd))
    return subprocess.run(cmd, cwd=str(cwd) if cwd else None, check=True, text=True, capture_output=True)


def load_manifest(app_dir: Path) -> dict:
    manifest_path = app_dir / "manifest.json"
    if not manifest_path.exists():
        raise SystemExit(f"missing manifest.json: {app_dir}")
    return json.loads(manifest_path.read_text(encoding="utf-8"))


def compile_lua(src: Path, dst: Path, luac: str) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    run([luac, "-o", str(dst), str(src)])


def copy_or_compile_tree(app_dir: Path, stage_dir: Path, luac: str) -> bool:
    lua_compiled = False
    for path in app_dir.rglob("*"):
        if path.is_dir():
            continue
        rel = path.relative_to(app_dir)
        if rel.as_posix() in {"manifest.json", "thumbnail.png", "main.lua"}:
            continue
        out = stage_dir / rel
        if path.suffix == ".lua":
            compile_lua(path, out, luac)
            lua_compiled = True
        else:
            out.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, out)
    return lua_compiled


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        while True:
            chunk = f.read(1024 * 1024)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()


def normalize_category(value: str | None) -> str:
    text = (value or "other").strip().lower()
    return text or "other"


def package_app(app_id: str, board_dir: Path, thumbs_dir: Path, base_url: str, luac: str, luac_version: str) -> dict:
    app_dir = APPS_DIR / app_id
    if not app_dir.exists():
        raise SystemExit(f"missing app dir: {app_dir}")

    manifest = load_manifest(app_dir)
    version = str(manifest.get("version") or "").strip()
    if not version:
        raise SystemExit(f"{app_id}: manifest version is required")

    if not (app_dir / "main.lua").exists():
        raise SystemExit(f"{app_id}: main.lua is required for compiled release packaging")

    thumb_src = app_dir / "thumbnail.png"
    if not thumb_src.exists():
        raise SystemExit(f"{app_id}: thumbnail.png is required for publishing")

    zip_name = f"{app_id}-{version}.zip"
    zip_path = board_dir / zip_name
    thumb_name = f"{app_id}.png"
    thumb_dst = thumbs_dir / thumb_name
    thumb_dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(thumb_src, thumb_dst)

    with tempfile.TemporaryDirectory(prefix=f"publish_{app_id}_") as tmp:
        stage = Path(tmp)
        compile_lua(app_dir / "main.lua", stage / "app.bin", luac)
        extra_lua = copy_or_compile_tree(app_dir, stage, luac)

        packaged_manifest = dict(manifest)
        packaged_manifest["entry"] = "app.bin"
        packaged_manifest["lua_bytecode"] = True
        packaged_manifest["build"] = {
            "packaged_at": datetime.now(timezone.utc).isoformat(),
            "compiler": luac_version,
        }
        (stage / "manifest.json").write_text(json.dumps(packaged_manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        shutil.copy2(thumb_src, stage / "thumbnail.png")

        zip_path.parent.mkdir(parents=True, exist_ok=True)
        if zip_path.exists():
            zip_path.unlink()
        shutil.make_archive(str(zip_path.with_suffix("")), "zip", root_dir=stage)

    category = normalize_category(manifest.get("category"))
    desc = str(manifest.get("description") or "")
    entry = {
        "id": app_id,
        "name": str(manifest.get("name") or app_id),
        "version": version,
        "category": category,
        "description": desc,
        "zip_url": f"{base_url}/{zip_name}",
        "thumbnail_url": f"{base_url}/thumbs/{thumb_name}",
        "sha256": sha256_file(zip_path),
        "size": zip_path.stat().st_size,
        "lua_bytecode": True,
        "entry": "app.bin",
        "compiler": luac_version,
    }
    if extra_lua:
        entry["compiled_helpers"] = True
    return entry


def write_index(board_dir: Path, entries: list[dict]) -> None:
    categories = []
    for item in entries:
        cat = item["category"]
        if cat not in categories:
            categories.append(cat)
    payload = {
        "schema": 1,
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "category_order": categories,
        "category_labels": {cat: CATEGORY_LABELS.get(cat, cat.replace("_", " ").title()) for cat in categories},
        "apps": entries,
    }
    index_path = board_dir / "apps-index.json"
    index_path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    gz_path = board_dir / "apps-index.json.gz"
    with gz_path.open("wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, compresslevel=9, mtime=0) as gz:
            gz.write(index_path.read_bytes())


def deploy_remote(
    local_dir: Path,
    remote: str,
    ssh_port: int,
    http_host: str,
    http_port: int,
    bind: str,
    identity: str | None,
) -> None:
    if ":" not in remote:
        raise SystemExit("--remote must look like user@host:/abs/path")
    remote_target = remote
    remote_login, remote_path = remote.split(":", 1)
    remote_path = remote_path.rstrip("/")

    scp_cmd = ["scp", "-r", "-P", str(ssh_port)]
    ssh_cmd = ["ssh", "-p", str(ssh_port)]
    if identity:
        scp_cmd.extend(["-i", identity])
        ssh_cmd.extend(["-i", identity])

    run(ssh_cmd + [remote_login, f"mkdir -p {shlex.quote(remote_path)}"])
    run(scp_cmd + [f"{str(local_dir)}/.", f"{remote_target}/"])
    kill_cmd = (
        f"pkill -f 'python3 .* {http_port} {bind} {remote_path}' || true; "
        f"pkill -f 'http.server {http_port}' || true"
    )
    web_cmd = (
        "nohup python3 -c "
        + shlex.quote(CORS_SERVER_SCRIPT)
        + f" {http_port} {bind} {remote_path} >{remote_path}/web.log 2>&1 &"
    )
    run(ssh_cmd + [remote_login, f"{kill_cmd}; {web_cmd}"])
    print(f"published: http://{http_host}:{http_port}/")
    print(f"index: http://{http_host}:{http_port}/apps-index.json")


def discover_apps(selected: list[str]) -> list[str]:
    if selected:
        return selected
    app_ids: list[str] = []
    for path in sorted(APPS_DIR.iterdir()):
        if not path.is_dir():
            continue
        if (path / "manifest.json").exists() and (path / "main.lua").exists():
            app_ids.append(path.name)
    return app_ids


def main() -> int:
    ap = argparse.ArgumentParser(description="Build compiled app-store packages and optionally publish them to the remote app server.")
    ap.add_argument("apps", nargs="*", help="app ids to publish; default: all apps with manifest.json + main.lua")
    ap.add_argument("--board", default=DEFAULT_BOARD)
    ap.add_argument("--dist-root", default=str(DEFAULT_DIST_ROOT))
    ap.add_argument("--base-url", default=DEFAULT_PUBLIC_BASE)
    ap.add_argument("--remote", default=DEFAULT_REMOTE)
    ap.add_argument("--ssh-port", type=int, default=DEFAULT_SSH_PORT)
    ap.add_argument("--identity", help="ssh private key path for scp/ssh upload")
    ap.add_argument("--http-host", default=DEFAULT_HTTP_HOST)
    ap.add_argument("--http-port", type=int, default=DEFAULT_HTTP_PORT)
    ap.add_argument("--bind", default="0.0.0.0")
    default_luac = str(DEFAULT_LUAC_TOOL) if DEFAULT_LUAC_TOOL.exists() else (shutil.which("luac") or "luac")
    ap.add_argument("--luac", default=default_luac)
    ap.add_argument("--upload", action="store_true", help="upload built artifacts to the configured remote server")
    ap.add_argument("--clean", action="store_true", help="remove previous board dist dir before packaging")
    args = ap.parse_args()

    board_dir = Path(args.dist_root).resolve() / args.board / "apps"
    thumbs_dir = board_dir / "thumbs"
    if args.clean and board_dir.exists():
        shutil.rmtree(board_dir)
    board_dir.mkdir(parents=True, exist_ok=True)
    thumbs_dir.mkdir(parents=True, exist_ok=True)

    if Path(args.luac).name != DEFAULT_LUAC_TOOL.name and DEFAULT_LUAC_TOOL.exists():
        print(f"warning: overriding preferred compiler {DEFAULT_LUAC_TOOL} with {args.luac}", file=sys.stderr)
    luac_info = run([args.luac, "-v"])
    luac_version = luac_info.stderr.strip() or luac_info.stdout.strip() or "luac"
    app_ids = discover_apps(args.apps)
    if not app_ids:
        raise SystemExit("no apps selected")

    entries = []
    for app_id in app_ids:
        entry = package_app(app_id, board_dir, thumbs_dir, args.base_url.rstrip("/"), args.luac, luac_version)
        entries.append(entry)

    entries.sort(key=lambda item: (item["category"], item["name"].lower(), item["id"]))
    write_index(board_dir, entries)

    print(f"packaged {len(entries)} app(s) into {board_dir}")
    if args.upload:
        identity = os.path.expanduser(args.identity) if args.identity else None
        deploy_remote(board_dir, args.remote, args.ssh_port, args.http_host, args.http_port, args.bind, identity)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
