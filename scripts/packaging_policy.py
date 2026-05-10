#!/usr/bin/env python3
from __future__ import annotations


def is_forbidden_doc_asset(rel_path: str) -> bool:
    """Return True when a relative asset path is forbidden in published app zips."""
    norm = str(rel_path or "").strip().strip("/").lower()
    if not norm:
        return False
    parts = [p for p in norm.split("/") if p]
    if any(p in {"doc", "docs"} for p in parts):
        return True
    if norm.endswith(".md") or norm.endswith(".markdown"):
        return True
    return False
