#!/usr/bin/env python3
import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess
import tempfile
import zipfile
from datetime import datetime, timezone


def sha256_file(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def zip_app(app_dir: pathlib.Path, out_zip: pathlib.Path) -> None:
    if out_zip.exists():
        out_zip.unlink()
    with zipfile.ZipFile(out_zip, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for p in sorted(app_dir.rglob('*')):
            if p.is_dir():
                continue
            rel = p.relative_to(app_dir).as_posix()
            zf.write(p, arcname=rel)


def check_luac_version(luac_bin: str) -> None:
    try:
        cp = subprocess.run([luac_bin, '-v'], check=True, capture_output=True, text=True)
    except Exception as e:
        raise RuntimeError(f'failed to run {luac_bin} -v: {e}')
    out = (cp.stdout or '') + (cp.stderr or '')
    if 'Lua 5.4' not in out:
        raise RuntimeError(f'luac version mismatch, need Lua 5.4.x, got: {out.strip()}')


def zip_app_with_bytecode(app_dir: pathlib.Path, out_zip: pathlib.Path, luac_bin: str, entry_name: str) -> None:
    if out_zip.exists():
        out_zip.unlink()
    main_src = app_dir / 'main.lua'
    if not main_src.exists():
        raise RuntimeError(f'missing main.lua in {app_dir}')

    with tempfile.TemporaryDirectory(prefix='app_pack_') as td:
        stage = pathlib.Path(td) / app_dir.name
        shutil.copytree(app_dir, stage)
        if not entry_name or '..' in entry_name or entry_name.startswith('/') or entry_name.endswith('/'):
            raise RuntimeError(f'invalid entry name: {entry_name}')

        entry_dst = stage / entry_name
        entry_dst.parent.mkdir(parents=True, exist_ok=True)
        # Strip debug info and output bytecode chunk.
        subprocess.run([luac_bin, '-s', '-o', str(entry_dst), str(main_src)], check=True)

        # Remove clear-text source entry from package when using a different name.
        if entry_name != 'main.lua':
            legacy = stage / 'main.lua'
            if legacy.exists():
                legacy.unlink()

        # Ensure manifest has runtime entry field.
        manifest_path = stage / 'manifest.json'
        manifest = {}
        if manifest_path.exists():
            try:
                manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
            except Exception:
                manifest = {}
        manifest['entry'] = entry_name
        manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')

        with zipfile.ZipFile(out_zip, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
            for p in sorted(stage.rglob('*')):
                if p.is_dir():
                    continue
                rel = p.relative_to(stage).as_posix()
                zf.write(p, arcname=rel)


def load_manifest(app_dir: pathlib.Path) -> dict:
    m = app_dir / 'manifest.json'
    if m.exists():
        try:
            return json.loads(m.read_text(encoding='utf-8'))
        except Exception:
            return {}
    return {}


def resolve_entry_path(app_dir: pathlib.Path, manifest: dict) -> pathlib.Path | None:
    entry = str(manifest.get('entry', '') or '').strip()
    if entry:
        rel = pathlib.Path(entry)
        if not rel.is_absolute() and '..' not in rel.parts:
            candidate = app_dir / rel
            if candidate.exists() and candidate.is_file():
                return candidate

    main_lua = app_dir / 'main.lua'
    if main_lua.exists() and main_lua.is_file():
        return main_lua
    return None

def find_thumbnail(app_dir: pathlib.Path) -> pathlib.Path | None:
    preferred = [
        'thumbnail.png', 'thumbnail.jpg', 'thumbnail.jpeg',
        'thumb.png', 'preview.png',
    ]
    for name in preferred:
        p = app_dir / name
        if p.exists() and p.is_file():
            return p

    candidates = []
    for ext in ('*.png', '*.jpg', '*.jpeg'):
        for p in app_dir.rglob(ext):
            if not p.is_file():
                continue
            rel = p.relative_to(app_dir).as_posix().lower()
            if rel.startswith('icons/'):
                continue
            candidates.append(p)
    if candidates:
        candidates.sort(key=lambda x: len(x.relative_to(app_dir).as_posix()))
        return candidates[0]
    return None

def detect_category(app_id: str, manifest: dict) -> str:
    c = str(manifest.get('category', '')).strip().lower()
    return c if c else 'other'


def main() -> None:
    ap = argparse.ArgumentParser(description='Build app zip files and apps-index.json for GitHub store')
    ap.add_argument('--apps-root', default='data_littlefs/apps')
    ap.add_argument('--out-dir', default='dist/store')
    ap.add_argument('--base-url', required=True, help='Base URL where zip files are hosted, e.g. https://raw.githubusercontent.com/<org>/<repo>/main/dist/store')
    ap.add_argument('--lua-bytecode', action='store_true', help='Compile app main.lua to Lua 5.4 bytecode before zipping')
    ap.add_argument('--luac-bin', default='luac', help='Path to Lua 5.4 luac compiler')
    ap.add_argument('--entry-name', default='app.bin', help='Entry file name used in package when --lua-bytecode is enabled')
    args = ap.parse_args()

    apps_root = pathlib.Path(args.apps_root)
    out_dir = pathlib.Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    thumbs_dir = out_dir / 'thumbs'
    thumbs_dir.mkdir(parents=True, exist_ok=True)
    if args.lua_bytecode:
        check_luac_version(args.luac_bin)

    apps = []
    for app_dir in sorted([p for p in apps_root.iterdir() if p.is_dir()]):
        app_id = app_dir.name
        if app_id.startswith('.'):
            continue

        manifest = load_manifest(app_dir)
        entry_path = resolve_entry_path(app_dir, manifest)
        if entry_path is None:
            continue

        name = manifest.get('name', app_id)
        version = str(manifest.get('version', '0.1.0'))
        description = manifest.get('description', '')

        zip_name = f'{app_id}-{version}.zip'
        zip_path = out_dir / zip_name
        packaged_entry = entry_path.relative_to(app_dir).as_posix()
        package_uses_bytecode = False
        if args.lua_bytecode and (app_dir / 'main.lua').exists():
            zip_app_with_bytecode(app_dir, zip_path, args.luac_bin, args.entry_name)
            packaged_entry = args.entry_name
            package_uses_bytecode = True
        else:
            zip_app(app_dir, zip_path)
        digest = sha256_file(zip_path)
        thumbnail_url = ''
        thumb_src = find_thumbnail(app_dir)
        if thumb_src:
            ext = thumb_src.suffix.lower()
            thumb_name = f'{app_id}{ext}'
            thumb_dst = thumbs_dir / thumb_name
            shutil.copyfile(thumb_src, thumb_dst)
            thumbnail_url = args.base_url.rstrip('/') + '/thumbs/' + thumb_name

        apps.append({
            'id': app_id,
            'name': name,
            'version': version,
            'category': detect_category(app_id, manifest),
            'description': description,
            'zip_url': args.base_url.rstrip('/') + '/' + zip_name,
            'thumbnail_url': thumbnail_url,
            'sha256': digest,
            'size': zip_path.stat().st_size,
            'lua_bytecode': package_uses_bytecode or packaged_entry.endswith('.bin'),
            'entry': packaged_entry,
        })

    index = {
        'schema': 1,
        'generated_at': datetime.now(timezone.utc).isoformat(),
        'apps': apps,
    }
    (out_dir / 'apps-index.json').write_text(json.dumps(index, ensure_ascii=False, indent=2), encoding='utf-8')
    print(f'Wrote {len(apps)} app(s) to {out_dir}')


if __name__ == '__main__':
    main()
