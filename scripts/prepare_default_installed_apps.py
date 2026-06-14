#!/usr/bin/env python3
from __future__ import annotations

import json
import shutil
from pathlib import Path

from package_selected_littlefs_16m import DEFAULT_APPS, stage_release_app


ROOT = Path(__file__).resolve().parents[1]
APPS_SRC_DIR = ROOT / "apps_src"
APPS_DST_DIR = ROOT / "data_littlefs" / "apps"
SYS_DIR = ROOT / "data_littlefs" / ".sys"
INSTALLED_INDEX_PATH = SYS_DIR / "installed_apps.json"
LUAC_PATH = (ROOT / "python" / "store" / "tools" / "luac-esp-compat").resolve()


def _build_installed_entry(app_id: str) -> dict:
    src = APPS_SRC_DIR / app_id
    manifest_path = src / "manifest.json"
    manifest = {}
    if manifest_path.is_file():
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    name = str(manifest.get("name") or app_id).strip() or app_id
    version = str(manifest.get("version") or "").strip()
    category = str(manifest.get("category") or "").strip()

    has_thumb = (src / "thumbnail.png").is_file()
    has_settings = (src / "settings.html").is_file() or (src / "settings.html.gz").is_file()

    return {
        "id": app_id,
        "name": name,
        "version": version,
        "category": category,
        "has_thumbnail": has_thumb,
        "thumbnail_url": f"/api/apps/thumbnail/{app_id}" if has_thumb else "",
        "has_settings_page": has_settings,
        "settings_page_url": f"/api/apps/web/{app_id}/settings.html" if has_settings else "",
    }


def _write_installed_index(app_ids: list[str]) -> None:
    SYS_DIR.mkdir(parents=True, exist_ok=True)
    body = {
        "ok": True,
        "apps": [_build_installed_entry(app_id) for app_id in app_ids],
    }
    INSTALLED_INDEX_PATH.write_text(
        json.dumps(body, ensure_ascii=False, separators=(",", ":")) + "\n",
        encoding="utf-8",
    )


def prepare_default_installed_apps() -> list[str]:
    if not APPS_SRC_DIR.is_dir():
        raise SystemExit(f"apps source dir not found: {APPS_SRC_DIR}")
    if not LUAC_PATH.is_file():
        raise SystemExit(f"luac tool not found: {LUAC_PATH}")

    selected = [a.strip() for a in DEFAULT_APPS if str(a).strip()]
    if not selected:
        raise SystemExit("DEFAULT_APPS is empty")

    missing = [app for app in selected if not (APPS_SRC_DIR / app).is_dir()]
    if missing:
        raise SystemExit("Missing app directories in apps_src:\n" + "\n".join(f"- {m}" for m in missing))

    for app in selected:
        src = APPS_SRC_DIR / app
        if (src / "settings.html").is_file() and not (src / "settings.html.gz").is_file():
            raise SystemExit(f"{app}: settings.html.gz is required when settings.html exists")

    if APPS_DST_DIR.exists():
        shutil.rmtree(APPS_DST_DIR)
    APPS_DST_DIR.mkdir(parents=True, exist_ok=True)

    for app in selected:
        stage_release_app(APPS_SRC_DIR / app, APPS_DST_DIR / app, str(LUAC_PATH), ROOT)

    _write_installed_index(selected)

    return selected


def main() -> int:
    selected = prepare_default_installed_apps()
    print("Prepared compiled default apps into data_littlefs/apps:")
    for app in selected:
        print(f" - {app}")
    print(f"installed index: {INSTALLED_INDEX_PATH}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
