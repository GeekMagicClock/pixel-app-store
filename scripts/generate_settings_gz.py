#!/usr/bin/env python3
"""
Generate settings.html.gz for apps under apps_src where settings.html exists
but settings.html.gz is missing or out-of-date. Uses gzip mtime=0 to make deterministic output.
Prints a newline-separated list of app ids for which gz files were created/updated.
"""
import gzip
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
APPS_DIR = ROOT / 'apps_src'

if not APPS_DIR.exists():
    print(f"apps dir not found: {APPS_DIR}", file=sys.stderr)
    sys.exit(1)

updated = []
for appdir in sorted(APPS_DIR.iterdir()):
    if not appdir.is_dir():
        continue
    src = appdir / 'settings.html'
    dst = appdir / 'settings.html.gz'
    if not src.exists():
        continue
    try:
        src_stat = src.stat()
        need = False
        if not dst.exists():
            need = True
        else:
            if src_stat.st_mtime > dst.stat().st_mtime + 0.001:
                need = True
        if not need:
            continue
        # Read source
        data = src.read_bytes()
        # Write gzip with mtime=0 for deterministic output
        # gzip.GzipFile accepts mtime parameter in Python 3.8+
        with gzip.GzipFile(filename=str(dst.name), mode='wb', fileobj=open(dst, 'wb'), mtime=0) as gz:
            gz.write(data)
        # Ensure the gz file is readable and exists
        if dst.exists():
            updated.append(appdir.name)
            print(appdir.name)
    except Exception as e:
        print(f"error processing {appdir}: {e}", file=sys.stderr)

if not updated:
    # Print nothing but exit 0
    pass

sys.exit(0)
