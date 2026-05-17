#!/usr/bin/env python3
"""Bump patch version for apps whose manifest version matches remote index.

This script reads the remote apps-index.json via SSH (same remote as publish_apps.py default)
and increments the patch (third) segment for any app whose local manifest version equals the remote's version.
It writes back the updated manifest.json files.
"""
from __future__ import annotations

import json
import argparse
import shlex
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / 'scripts'
APPS = ROOT / 'apps_src'

DEFAULT_REMOTE = 'root@111.229.177.3:/www/wwwroot/ota.geekmagic.cc/store_data/pixel64x32V2'
DEFAULT_CHANNEL = 'beta'
DEFAULT_SSH_PORT = 22
DEFAULT_IDENTITY = str(Path.home() / '.ssh' / 't20260309.pem')


def parse_args(argv: list[str]):
    p = argparse.ArgumentParser(
        description=(
            'Bump patch versions by comparing local manifests with remote index. '
            'By default, only specified app IDs are processed.'
        )
    )
    p.add_argument(
        'app_ids',
        nargs='*',
        help='App IDs to bump (e.g. basketball_scoreboard soccer_schedule).',
    )
    p.add_argument(
        '--all',
        action='store_true',
        help='Process all apps under apps_src (explicit full bump).',
    )
    return p.parse_args(argv)

def run(cmd):
    print('+', ' '.join(shlex.quote(x) for x in cmd))
    return subprocess.run(cmd, check=True, text=True, capture_output=True)

def parse_manifest(path: Path) -> dict:
    return json.loads(path.read_text(encoding='utf-8'))

def write_manifest(path: Path, obj: dict):
    path.write_text(json.dumps(obj, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')

def semver_bump_patch(v: str) -> str:
    parts = v.split('-', 1)
    main = parts[0]
    suffix = '-' + parts[1] if len(parts) > 1 else ''
    nums = main.split('.')
    while len(nums) < 3:
        nums.append('0')
    try:
        nums[2] = str(int(nums[2]) + 1)
    except Exception:
        nums[2] = '1'
    return '.'.join(nums[:3]) + suffix

def read_remote_index(remote: str, channel: str = DEFAULT_CHANNEL, identity: str | None = DEFAULT_IDENTITY) -> dict | None:
    remote_login, remote_path = remote.split(':', 1)
    # append channel if needed
    tail = remote_path.rstrip('/').split('/')[-1].lower()
    if tail not in {'stable', 'beta'}:
        remote_path = remote_path.rstrip('/') + '/' + channel
    cmd = ['ssh', '-p', str(DEFAULT_SSH_PORT)]
    if identity:
        cmd.extend(['-i', identity])
    cmd.extend([remote_login, f'cat {shlex.quote(remote_path)}/apps-index.json'])
    try:
        cp = run(cmd)
        return json.loads(cp.stdout)
    except Exception as e:
        print('warning: could not read remote index:', e, file=sys.stderr)
        return None

def main(argv: list[str] | None = None):
    args = parse_args(sys.argv[1:] if argv is None else argv)

    if args.all and args.app_ids:
        print('error: cannot use --all together with app_ids', file=sys.stderr)
        return 2

    target_ids = set(args.app_ids)
    if not args.all and not target_ids:
        print(
            'error: no app_ids provided. Pass app IDs or use --all for explicit full bump.',
            file=sys.stderr,
        )
        return 2

    remote = DEFAULT_REMOTE
    remote_json = read_remote_index(remote)
    if not remote_json:
        print('no remote index available; aborting', file=sys.stderr)
        return 1
    remote_map = {str(a.get('id')): a for a in remote_json.get('apps', [])}

    changed = []
    for app_dir in sorted(APPS.iterdir()):
        if not app_dir.is_dir():
            continue
        if not args.all and app_dir.name not in target_ids:
            continue
        mf = app_dir / 'manifest.json'
        if not mf.exists():
            continue
        m = parse_manifest(mf)
        app_id = app_dir.name
        local_ver = str(m.get('version') or '').strip()
        remote_entry = remote_map.get(app_id)
        remote_ver = str(remote_entry.get('version') or '').strip() if remote_entry else None
        if remote_ver and local_ver and remote_ver == local_ver:
            new_ver = semver_bump_patch(local_ver)
            print(f'{app_id}: {local_ver} -> {new_ver}')
            m['version'] = new_ver
            write_manifest(mf, m)
            changed.append((app_id, local_ver, new_ver))

    if not args.all:
        missing = sorted(a for a in target_ids if not (APPS / a / 'manifest.json').exists())
        for app_id in missing:
            print(f'warning: app not found or missing manifest: {app_id}', file=sys.stderr)

    if not changed:
        print('no manifests needed bump')
        return 0
    print('bumped', len(changed), 'manifests')
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
