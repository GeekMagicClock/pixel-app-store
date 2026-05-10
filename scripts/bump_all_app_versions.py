#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
APPS_DIR = ROOT / "apps_src"
SEMVER_RE = re.compile(r"^(\d+)\.(\d+)\.(\d+)(-.+)?$")


def bump_patch(version: str) -> str:
    text = version.strip()
    m = SEMVER_RE.match(text)
    if not m:
        raise ValueError(f"unsupported version format: {version!r}")
    major = int(m.group(1))
    minor = int(m.group(2))
    patch = int(m.group(3)) + 1
    suffix = m.group(4) or ""
    return f"{major}.{minor}.{patch}{suffix}"


def discover_apps() -> list[Path]:
    out: list[Path] = []
    for path in sorted(APPS_DIR.iterdir()):
        if not path.is_dir():
            continue
        if (path / "manifest.json").is_file() and (path / "main.lua").is_file():
            out.append(path)
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description="Bump patch version for all publishable apps under apps_src.")
    ap.add_argument("--dry-run", action="store_true", help="show planned version changes without writing files")
    args = ap.parse_args()

    apps = discover_apps()
    if not apps:
        raise SystemExit("no publishable apps found")

    changed = 0
    for app_dir in apps:
        manifest_path = app_dir / "manifest.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        old_version = str(manifest.get("version") or "").strip()
        if not old_version:
            raise SystemExit(f"{app_dir.name}: missing version in manifest")
        new_version = bump_patch(old_version)
        print(f"{app_dir.name}: {old_version} -> {new_version}")
        if not args.dry_run:
            manifest["version"] = new_version
            manifest_path.write_text(
                json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8",
            )
        changed += 1

    mode = "would bump" if args.dry_run else "bumped"
    print(f"{mode} {changed} app version(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
