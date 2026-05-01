#!/usr/bin/env python3
from __future__ import annotations

import argparse
import gzip
import hashlib
import html.parser
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import zlib
import zipfile
from datetime import datetime, timezone
from pathlib import Path
from urllib import request

ROOT = Path(__file__).resolve().parents[1]
APPS_DIR = ROOT / "data_littlefs" / "apps"
DEFAULT_DIST_ROOT = ROOT / "dist" / "store"
DEFAULT_BOARD = "pixel64x32V2"
DEFAULT_CHANNEL = "stable"
DEFAULT_REMOTE = "root@111.229.177.3:/www/wwwroot/ota.geekmagic.cc/store_data/pixel64x32V2"
DEFAULT_PUBLIC_BASE = "https://ota.geekmagic.cc/apps/{board}/{channel}"
DEFAULT_HTTP_HOST = "ota.geekmagic.cc"
DEFAULT_HTTP_PORT = 8001
DEFAULT_SSH_PORT = 22
DEFAULT_SSH_IDENTITY = str(Path.home() / ".ssh" / "t20260309.pem")
DEFAULT_LUAC_TOOL = ROOT / "python" / "store" / "tools" / "luac-esp-compat"
FORBIDDEN_TERMS = ("lua", "http", "api", "json", "proxy", "firmware", "littlefs")

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
        if rel.name == "settings.html":
            gz = path.with_name(path.name + ".gz")
            if not gz.exists():
                raise SystemExit(f"{app_dir.name}: settings.html.gz is required when settings.html exists")
            # Publish only compressed settings page.
            continue
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


def crc32_file(path: Path) -> str:
    crc = 0
    with path.open("rb") as f:
        while True:
            chunk = f.read(1024 * 1024)
            if not chunk:
                break
            crc = zlib.crc32(chunk, crc)
    return f"{crc & 0xFFFFFFFF:08x}"


def normalize_category(value: str | None) -> str:
    text = (value or "other").strip().lower()
    return text or "other"


def parse_remote_target(remote: str) -> tuple[str, str]:
    if ":" not in remote:
        raise SystemExit("--remote must look like user@host:/abs/path")
    remote_login, remote_path = remote.split(":", 1)
    return remote_login, remote_path.rstrip("/")

def remote_with_channel(remote: str, channel: str) -> str:
    remote_login, remote_path = parse_remote_target(remote)
    tail = remote_path.rstrip("/").split("/")[-1].lower()
    if tail in {"stable", "beta"}:
        return f"{remote_login}:{remote_path}"
    return f"{remote_login}:{remote_path}/{channel}"


def semver_cmp(a: str, b: str) -> int:
    def parse(v: str) -> tuple[list[int], str]:
        s = v.strip()
        main, sep, suffix = s.partition("-")
        nums = [int(x) if x.isdigit() else 0 for x in main.split(".")]
        while len(nums) < 3:
            nums.append(0)
        return nums[:3], suffix

    a_nums, a_suffix = parse(a)
    b_nums, b_suffix = parse(b)
    if a_nums != b_nums:
        return (a_nums > b_nums) - (a_nums < b_nums)
    if a_suffix == b_suffix:
        return 0
    if not a_suffix:
        return 1
    if not b_suffix:
        return -1
    return (a_suffix > b_suffix) - (a_suffix < b_suffix)


class _VisibleTextParser(html.parser.HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self._skip = False
        self.parts: list[str] = []

    def handle_starttag(self, tag: str, attrs) -> None:  # type: ignore[override]
        if tag.lower() in {"script", "style"}:
            self._skip = True

    def handle_endtag(self, tag: str) -> None:  # type: ignore[override]
        if tag.lower() in {"script", "style"}:
            self._skip = False

    def handle_data(self, data: str) -> None:  # type: ignore[override]
        if not self._skip:
            self.parts.append(data)


def _find_forbidden_term(text: str) -> str | None:
    lower = text.lower()
    for term in FORBIDDEN_TERMS:
        if term in lower:
            return term
    return None


def validate_user_facing_texts(app_dir: Path) -> None:
    settings_path = app_dir / "settings.html"
    if settings_path.exists():
        parser = _VisibleTextParser()
        parser.feed(settings_path.read_text(encoding="utf-8", errors="replace"))
        visible_text = " ".join(parser.parts)
        hit = _find_forbidden_term(visible_text)
        if hit:
            raise SystemExit(f"{app_dir.name}: settings.html visible text contains forbidden term '{hit}'")

    main_path = app_dir / "main.lua"
    if main_path.exists():
        for ln_no, line in enumerate(main_path.read_text(encoding="utf-8", errors="replace").splitlines(), start=1):
            if not ("text_box(" in line or "draw_chip_text(" in line or "draw_chip(" in line):
                continue
            for m in re.finditer(r"\"([^\"]*)\"|'([^']*)'", line):
                lit = m.group(1) if m.group(1) is not None else m.group(2)
                hit = _find_forbidden_term(lit)
                if hit:
                    raise SystemExit(f"{app_dir.name}: main.lua:{ln_no} user text contains forbidden term '{hit}'")


def validate_zip_has_no_lua_source(zip_path: Path) -> None:
    with zipfile.ZipFile(zip_path, "r") as zf:
        for name in zf.namelist():
            if name.endswith("/"):
                continue
            if name.lower().endswith(".lua"):
                raise SystemExit(f"{zip_path.name}: contains forbidden source file '{name}'")


def package_app(
    app_id: str,
    board_dir: Path,
    thumbs_dir: Path,
    thumbnail_url_base: str,
    zip_url_base: str,
    luac: str,
    luac_version: str,
) -> dict:
    app_dir = APPS_DIR / app_id
    if not app_dir.exists():
        raise SystemExit(f"missing app dir: {app_dir}")
    validate_user_facing_texts(app_dir)

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
    thumb_sha = sha256_file(thumb_dst)

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
        validate_zip_has_no_lua_source(zip_path)

    category = normalize_category(manifest.get("category"))
    desc = str(manifest.get("description") or "")
    min_fw = str(
        manifest.get("min_fw")
        or manifest.get("min_firmware")
        or manifest.get("min_firmware_version")
        or ""
    ).strip()
    entry = {
        "id": app_id,
        "name": str(manifest.get("name") or app_id),
        "version": version,
        "category": category,
        "description": desc,
        "zip_url": f"{zip_url_base}/{zip_name}",
        "thumbnail_url": f"{thumbnail_url_base}/thumbs/{thumb_name}?v={thumb_sha[:12]}",
        "thumbnail_sha256": thumb_sha,
        "crc32": crc32_file(zip_path),
        "size": zip_path.stat().st_size,
        "lua_bytecode": True,
        "entry": "app.bin",
        "compiler": luac_version,
    }
    if min_fw:
        entry["min_fw"] = min_fw
    if extra_lua:
        entry["compiled_helpers"] = True
    return entry


def validate_published_entry(entry: dict, app_id: str) -> None:
    crc32 = str(entry.get("crc32") or "").strip().lower()
    if not crc32:
        raise SystemExit(f"{app_id}: missing crc32 in published catalog entry")
    if not re.fullmatch(r"(?:crc32:)?[0-9a-f]{8}", crc32):
        raise SystemExit(f"{app_id}: invalid crc32 '{entry.get('crc32')}'")
    size = int(entry.get("size") or 0)
    if size <= 0:
        raise SystemExit(f"{app_id}: missing or invalid size in published catalog entry")


def write_index(board_dir: Path, entries: list[dict], channel: str) -> None:
    for item in entries:
        validate_published_entry(item, str(item.get("id") or "unknown"))
    categories = []
    for item in entries:
        cat = item["category"]
        if cat not in categories:
            categories.append(cat)
    payload = {
        "schema": 1,
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "channel": channel,
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


def read_local_index(index_path: Path) -> list[dict] | None:
    if not index_path.exists():
        return None
    payload = json.loads(index_path.read_text(encoding="utf-8"))
    apps = payload.get("apps")
    return apps if isinstance(apps, list) else None


def read_remote_index_via_http(base_url: str) -> list[dict] | None:
    url = base_url.rstrip("/") + "/apps-index.json"
    try:
        with request.urlopen(url, timeout=10) as resp:
            body = resp.read()
            encoding = str(resp.headers.get("Content-Encoding") or "").lower()
        if "gzip" in encoding:
            body = gzip.decompress(body)
        payload = json.loads(body.decode("utf-8"))
        apps = payload.get("apps")
        if isinstance(apps, list):
            return apps
    except Exception:
        return None
    return None


def read_remote_index_via_ssh(remote: str, ssh_port: int, identity: str | None) -> list[dict] | None:
    remote_login, remote_path = parse_remote_target(remote)
    ssh_cmd = ["ssh", "-p", str(ssh_port)]
    if identity:
        ssh_cmd.extend(["-i", identity])
    cmd = ssh_cmd + [remote_login, f"cat {shlex.quote(remote_path)}/apps-index.json"]
    try:
        cp = run(cmd)
        payload = json.loads(cp.stdout or "{}")
        apps = payload.get("apps")
        if isinstance(apps, list):
            return apps
    except Exception:
        return None
    return None


def load_existing_entries(
    board_dir: Path,
    base_url: str,
    remote: str,
    ssh_port: int,
    identity: str | None,
    prefer_remote: bool,
) -> list[dict]:
    if prefer_remote:
        remote_entries = read_remote_index_via_ssh(remote, ssh_port, identity)
        if remote_entries is not None:
            return remote_entries
        http_entries = read_remote_index_via_http(base_url)
        if http_entries is not None:
            return http_entries
    local_entries = read_local_index(board_dir / "apps-index.json")
    if local_entries is not None:
        return local_entries
    raise SystemExit(
        "incremental publish needs existing apps-index.json. "
        "Use --upload (to merge with remote index), or run a full publish first."
    )


def merge_entries(existing: list[dict], updates: list[dict], enforce_version_bump: bool) -> list[dict]:
    merged: dict[str, dict] = {}
    for item in existing:
        app_id = str(item.get("id") or "")
        if app_id:
            merged[app_id] = item

    for item in updates:
        app_id = str(item.get("id") or "")
        if not app_id:
            continue
        old = merged.get(app_id)
        if old and enforce_version_bump:
            old_ver = str(old.get("version") or "").strip()
            new_ver = str(item.get("version") or "").strip()
            if old_ver and new_ver and semver_cmp(new_ver, old_ver) <= 0:
                raise SystemExit(
                    f"{app_id}: version must increase for incremental publish (old={old_ver}, new={new_ver})"
                )
        merged[app_id] = item

    out = list(merged.values())
    out.sort(key=lambda e: (str(e.get("category") or ""), str(e.get("name") or "").lower(), str(e.get("id") or "")))
    return out


def deploy_remote(
    local_dir: Path,
    remote: str,
    ssh_port: int,
    http_host: str,
    http_port: int,
    bind: str,
    identity: str | None,
) -> None:
    remote_target = remote
    remote_login, remote_path = parse_remote_target(remote)
    helper_script = Path(tempfile.gettempdir()) / "pixel_store_cors_server.py"
    helper_script.write_text(CORS_SERVER_SCRIPT + "\n", encoding="utf-8")

    scp_cmd = ["scp", "-r", "-P", str(ssh_port)]
    ssh_cmd = ["ssh", "-p", str(ssh_port)]
    if identity:
        scp_cmd.extend(["-i", identity])
        ssh_cmd.extend(["-i", identity])

    run(ssh_cmd + [remote_login, f"mkdir -p {shlex.quote(remote_path)}"])
    run(scp_cmd + [str(helper_script), f"{remote_login}:/tmp/pixel_store_cors_server.py"])
    run(scp_cmd + [f"{str(local_dir)}/.", f"{remote_target}/"])
    pidfile = f"{remote_path}/web.pid"
    stop_cmd = (
        f"if [ -f {shlex.quote(pidfile)} ]; then "
        f"kill $(cat {shlex.quote(pidfile)}) >/dev/null 2>&1 || true; "
        f"rm -f {shlex.quote(pidfile)}; "
        f"fi"
    )
    web_cmd = (
        f"nohup python3 /tmp/pixel_store_cors_server.py {http_port} {bind} {remote_path} "
        f">{remote_path}/web.log 2>&1 & echo $! > {shlex.quote(pidfile)}"
    )
    run(ssh_cmd + [remote_login, stop_cmd])
    run(ssh_cmd + [remote_login, web_cmd])
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
    ap.add_argument("--channel", choices=("stable", "beta"), default=DEFAULT_CHANNEL)
    ap.add_argument("--dist-root", default=str(DEFAULT_DIST_ROOT))
    ap.add_argument(
        "--base-url",
        default=DEFAULT_PUBLIC_BASE,
        help="public base for index/thumb URL. Supports {board} and {channel} placeholders.",
    )
    ap.add_argument(
        "--thumbnail-base-url",
        default="",
        help="base URL for thumbnail_url. Supports {board} and {channel} placeholders. Defaults to --base-url.",
    )
    ap.add_argument(
        "--zip-base-url",
        default="",
        help="base URL for zip_url. Supports {board} and {channel} placeholders. Defaults to --base-url.",
    )
    ap.add_argument(
        "--zip-url-mode",
        choices=("redirect", "direct"),
        default="redirect",
        help="redirect: zip_url -> {base-url}/redirect/<zip>; direct: zip_url -> {base-url}/<zip>",
    )
    ap.add_argument("--remote", default=DEFAULT_REMOTE)
    ap.add_argument("--ssh-port", type=int, default=DEFAULT_SSH_PORT)
    ap.add_argument("--identity", default=DEFAULT_SSH_IDENTITY, help="ssh private key path for scp/ssh upload")
    ap.add_argument("--http-host", default=DEFAULT_HTTP_HOST)
    ap.add_argument("--http-port", type=int, default=DEFAULT_HTTP_PORT)
    ap.add_argument("--bind", default="0.0.0.0")
    default_luac = str(DEFAULT_LUAC_TOOL) if DEFAULT_LUAC_TOOL.exists() else (shutil.which("luac") or "luac")
    ap.add_argument("--luac", default=default_luac)
    ap.add_argument("--upload", action="store_true", help="upload built artifacts to the configured remote server")
    ap.add_argument("--clean", action="store_true", help="remove previous board dist dir before packaging")
    ap.add_argument(
        "--full-index",
        action="store_true",
        help="rewrite apps-index.json with only selected apps (dangerous; default for selected apps is safe incremental merge)",
    )
    ap.add_argument(
        "--allow-no-bump",
        action="store_true",
        help="allow publishing selected apps without version increase (not recommended)",
    )
    args = ap.parse_args()

    base_url = args.base_url.format(board=args.board, channel=args.channel).rstrip("/")
    thumbnail_base_url = (args.thumbnail_base_url or args.base_url).format(board=args.board, channel=args.channel).rstrip("/")
    zip_base_url = (args.zip_base_url or args.base_url).format(board=args.board, channel=args.channel).rstrip("/")
    zip_url_base = f"{zip_base_url}/redirect" if args.zip_url_mode == "redirect" else zip_base_url
    remote_target = remote_with_channel(args.remote, args.channel)
    board_dir = Path(args.dist_root).resolve() / args.board / args.channel
    thumbs_dir = board_dir / "thumbs"
    if args.clean and board_dir.exists():
        shutil.rmtree(board_dir)
    board_dir.mkdir(parents=True, exist_ok=True)
    thumbs_dir.mkdir(parents=True, exist_ok=True)

    if Path(args.luac).name != DEFAULT_LUAC_TOOL.name and DEFAULT_LUAC_TOOL.exists():
        print(f"warning: overriding preferred compiler {DEFAULT_LUAC_TOOL} with {args.luac}", file=sys.stderr)
    luac_info = run([args.luac, "-v"])
    luac_version = luac_info.stderr.strip() or luac_info.stdout.strip() or "luac"
    selected_apps = list(args.apps)
    app_ids = discover_apps(selected_apps)
    if not app_ids:
        raise SystemExit("no apps selected")

    entries: list[dict] = []
    for app_id in app_ids:
        entry = package_app(app_id, board_dir, thumbs_dir, thumbnail_base_url, zip_url_base, args.luac, luac_version)
        entries.append(entry)

    incremental_mode = bool(selected_apps) and not args.full_index
    if incremental_mode:
        identity = os.path.expanduser(args.identity) if args.identity else None
        existing_entries = load_existing_entries(
            board_dir=board_dir,
            base_url=base_url,
            remote=remote_target,
            ssh_port=args.ssh_port,
            identity=identity,
            prefer_remote=bool(args.upload),
        )
        merged_entries = merge_entries(
            existing=existing_entries,
            updates=entries,
            enforce_version_bump=not args.allow_no_bump,
        )
        write_index(board_dir, merged_entries, args.channel)
        print(
            f"incremental index merge: updated {len(entries)} app(s), total {len(merged_entries)} app(s) in index"
        )
    else:
        entries.sort(key=lambda item: (item["category"], item["name"].lower(), item["id"]))
        write_index(board_dir, entries, args.channel)

    print(f"packaged {len(entries)} app(s) into {board_dir}")
    if args.upload:
        identity = os.path.expanduser(args.identity) if args.identity else None
        deploy_remote(board_dir, remote_target, args.ssh_port, args.http_host, args.http_port, args.bind, identity)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
