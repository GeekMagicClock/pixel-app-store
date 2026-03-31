#!/usr/bin/env python3
from __future__ import annotations

import argparse
import copy
import datetime as dt
import hashlib
import json
import logging
import os
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from zoneinfo import ZoneInfo


DEFAULT_HOST = "0.0.0.0"
DEFAULT_PORT = 8791
DEFAULT_TIMEOUT = 12.0
DEFAULT_SOURCE_TTL = 10
DEFAULT_SNAPSHOT_TTL = 2
DEFAULT_HORIZON_DAYS = 90
DEFAULT_MAX_BODY = 256 * 1024

WEEKDAY_LABELS = ["MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"]
MONTH_LABELS = ["JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"]
SPECIAL_KINDS = {"holiday", "anniversary", "birthday", "bill", "payday"}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Calendar snapshot gateway for pixel display apps")
    parser.add_argument("--config", required=True, help="Path to gateway config JSON")
    parser.add_argument("--host", default=os.environ.get("PIXEL_CAL_HOST", DEFAULT_HOST))
    parser.add_argument("--port", type=int, default=int(os.environ.get("PIXEL_CAL_PORT", DEFAULT_PORT)))
    parser.add_argument("--timeout", type=float, default=float(os.environ.get("PIXEL_CAL_TIMEOUT", DEFAULT_TIMEOUT)))
    parser.add_argument("--source-ttl", type=int, default=int(os.environ.get("PIXEL_CAL_SOURCE_TTL", DEFAULT_SOURCE_TTL)))
    parser.add_argument("--snapshot-ttl", type=int, default=int(os.environ.get("PIXEL_CAL_SNAPSHOT_TTL", DEFAULT_SNAPSHOT_TTL)))
    parser.add_argument("--horizon-days", type=int, default=int(os.environ.get("PIXEL_CAL_HORIZON_DAYS", DEFAULT_HORIZON_DAYS)))
    parser.add_argument("--log-level", default=os.environ.get("PIXEL_CAL_LOG_LEVEL", "INFO"))
    return parser.parse_args()


def now_utc() -> dt.datetime:
    return dt.datetime.now(dt.timezone.utc)


def month_label(month: int) -> str:
    month = max(1, min(12, int(month)))
    return MONTH_LABELS[month - 1]


def weekday_label(weekday: int) -> str:
    weekday = weekday % 7
    return WEEKDAY_LABELS[weekday]


def clamp_int(value: Any, default: int, lo: int, hi: int) -> int:
    try:
        number = int(value)
    except Exception:
        number = default
    return max(lo, min(hi, number))


def compact_text(text: Any, limit: int = 64) -> str:
    s = str(text or "")
    s = " ".join(s.replace("\r", " ").replace("\n", " ").replace("\t", " ").split())
    if len(s) > limit:
        return s[: max(1, limit - 1)] + "…"
    return s


def revision_for_payload(payload: dict[str, Any]) -> str:
    body = copy.deepcopy(payload)
    if isinstance(body, dict):
        body.pop("generated_at", None)
    raw = json.dumps(body, ensure_ascii=True, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha1(raw).hexdigest()[:12]


def parse_iso_like(value: str | None, default_tz: ZoneInfo) -> tuple[dt.datetime | None, bool]:
    if not value:
        return None, False
    raw = str(value).strip()
    if not raw:
        return None, False

    if len(raw) == 10 and raw[4] == "-" and raw[7] == "-":
        date_value = dt.date.fromisoformat(raw)
        return dt.datetime.combine(date_value, dt.time(0, 0), tzinfo=default_tz), True

    normalized = raw.replace("Z", "+00:00")
    try:
        parsed = dt.datetime.fromisoformat(normalized)
    except ValueError:
        return None, False
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=default_tz)
    return parsed.astimezone(default_tz), False


def add_months(value: dt.datetime, months: int) -> dt.datetime:
    month_index = value.month - 1 + months
    year = value.year + month_index // 12
    month = month_index % 12 + 1
    day = min(
        value.day,
        [31, 29 if (year % 4 == 0 and (year % 100 != 0 or year % 400 == 0)) else 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31][month - 1],
    )
    return value.replace(year=year, month=month, day=day)


def flatten_json_path(payload: Any, key: str | None) -> Any:
    if not key:
        return payload
    current = payload
    for part in str(key).split("."):
        if isinstance(current, dict):
            current = current.get(part)
        else:
            return None
    return current


def unfold_ics(text: str) -> list[str]:
    lines: list[str] = []
    for raw in text.splitlines():
        if raw.startswith(" ") or raw.startswith("\t"):
            if lines:
                lines[-1] += raw[1:]
            continue
        lines.append(raw.rstrip("\r\n"))
    return lines


def parse_ics_property(line: str) -> tuple[str, dict[str, str], str]:
    head, _, value = line.partition(":")
    parts = head.split(";")
    name = parts[0].upper()
    params: dict[str, str] = {}
    for part in parts[1:]:
        if "=" not in part:
            continue
        key, raw = part.split("=", 1)
        params[key.upper()] = raw
    return name, params, value


def unescape_ics_text(text: str) -> str:
    return (
        text.replace("\\n", "\n")
        .replace("\\N", "\n")
        .replace("\\,", ",")
        .replace("\\;", ";")
        .replace("\\\\", "\\")
    )


def parse_ics_datetime(value: str, params: dict[str, str], default_tz: ZoneInfo, assume_utc_as_local: bool = False) -> tuple[dt.datetime | None, bool]:
    raw = value.strip()
    if not raw:
        return None, False

    if params.get("VALUE", "").upper() == "DATE" or (len(raw) == 8 and raw.isdigit()):
        date_value = dt.datetime.strptime(raw[:8], "%Y%m%d").date()
        return dt.datetime.combine(date_value, dt.time(0, 0), tzinfo=default_tz), True

    tzinfo: dt.tzinfo = default_tz
    if raw.endswith("Z"):
        raw = raw[:-1]
        if assume_utc_as_local:
            tzinfo = default_tz
        else:
            tzinfo = dt.timezone.utc
    elif "TZID" in params:
        try:
            tzinfo = ZoneInfo(params["TZID"])
        except Exception:
            tzinfo = default_tz

    parsed = None
    for fmt in ("%Y%m%dT%H%M%S", "%Y%m%dT%H%M"):
        try:
            parsed = dt.datetime.strptime(raw, fmt)
            break
        except ValueError:
            continue
    if parsed is None:
        return None, False
    parsed = parsed.replace(tzinfo=tzinfo)
    return parsed.astimezone(default_tz), False


def parse_rrule(value: str) -> dict[str, str]:
    out: dict[str, str] = {}
    for part in str(value or "").split(";"):
        if "=" not in part:
            continue
        key, raw = part.split("=", 1)
        out[key.upper()] = raw
    return out


def parse_exdates(values: list[tuple[str, dict[str, str]]], default_tz: ZoneInfo, assume_utc_as_local: bool = False) -> set[str]:
    out: set[str] = set()
    for raw, params in values:
        for token in raw.split(","):
            parsed, all_day = parse_ics_datetime(token, params, default_tz, assume_utc_as_local)
            if not parsed:
                continue
            out.add(("D:" if all_day else "T:") + parsed.isoformat())
    return out


def recurrence_key(start: dt.datetime, all_day: bool) -> str:
    return ("D:" if all_day else "T:") + start.isoformat()


def iter_ics_components(lines: list[str]) -> list[dict[str, Any]]:
    out: list[dict[str, Any]] = []
    active: dict[str, Any] | None = None
    for line in lines:
        upper = line.upper()
        if upper == "BEGIN:VEVENT":
            active = {"type": "VEVENT", "props": []}
            continue
        if upper == "BEGIN:VTODO":
            active = {"type": "VTODO", "props": []}
            continue
        if upper == "END:VEVENT" or upper == "END:VTODO":
            if active:
                out.append(active)
            active = None
            continue
        if active is not None:
            active["props"].append(line)
    return out


@dataclass
class NormalizedItem:
    item_id: str
    title: str
    kind: str
    source: str
    priority: int
    start_at: dt.datetime | None = None
    end_at: dt.datetime | None = None
    due_at: dt.datetime | None = None
    all_day: bool = False
    location: str = ""
    status: str = ""
    special: bool = False
    countdown: bool = False

    def primary_time(self) -> dt.datetime | None:
        return self.start_at or self.due_at

    def occurs_on(self, day: dt.date) -> bool:
        start = self.start_at or self.due_at
        if not start:
            return False
        end = self.end_at or start
        return start.date() <= day <= end.date()


class TimedCache:
    def __init__(self, ttl_seconds: int) -> None:
        self.ttl_seconds = ttl_seconds
        self.lock = threading.Lock()
        self.entries: dict[str, tuple[float, Any]] = {}

    def get(self, key: str) -> Any | None:
        with self.lock:
            entry = self.entries.get(key)
            if not entry:
                return None
            expires_at, value = entry
            if expires_at < time.time():
                self.entries.pop(key, None)
                return None
            return copy.deepcopy(value)

    def set(self, key: str, value: Any, ttl_override: int | None = None) -> None:
        ttl = ttl_override if ttl_override is not None else self.ttl_seconds
        with self.lock:
            self.entries[key] = (time.time() + ttl, copy.deepcopy(value))


class CalendarGateway:
    def __init__(self, config_path: str, timeout_seconds: float, source_ttl: int, snapshot_ttl: int, horizon_days: int) -> None:
        self.config_path = Path(config_path).resolve()
        self.timeout_seconds = timeout_seconds
        self.source_cache = TimedCache(source_ttl)
        self.snapshot_cache = TimedCache(snapshot_ttl)
        self.horizon_days = horizon_days
        self.last_good_snapshot: dict[str, Any] | None = None
        self.last_good_items: list[dict[str, Any]] | None = None
        self.last_good_views: dict[str, Any] | None = None
        self.last_source_errors: list[dict[str, str]] = []

    def load_config(self) -> dict[str, Any]:
        data = json.loads(self.config_path.read_text(encoding="utf-8"))
        if not isinstance(data, dict):
            raise ValueError("config root must be an object")
        return data

    def _fetch_url_bytes(self, url: str, headers: dict[str, str] | None = None, timeout: float | None = None, max_body: int | None = None) -> bytes:
        request = urllib.request.Request(
            url,
            headers={
                "User-Agent": "esp32-pixel-calendar-gateway/1.0",
                "Accept": "application/json,text/calendar,text/plain,*/*",
                **(headers or {}),
            },
            method="GET",
        )
        with urllib.request.urlopen(request, timeout=timeout or self.timeout_seconds) as response:
            body = response.read()
        if max_body is not None and len(body) > max_body:
            raise ValueError(f"body too large: {len(body)} bytes > {max_body}")
        return body

    def _read_source_bytes(self, source: dict[str, Any]) -> bytes:
        cache_key = json.dumps(source, sort_keys=True)
        cached = self.source_cache.get(cache_key)
        if cached is not None:
            return cached

        max_body = clamp_int(source.get("max_body", DEFAULT_MAX_BODY), DEFAULT_MAX_BODY, 1024, 2 * 1024 * 1024)
        timeout = float(source.get("timeout", self.timeout_seconds))
        body: bytes
        if source.get("path"):
            path = (self.config_path.parent / str(source["path"])).resolve()
            body = path.read_bytes()
        else:
            body = self._fetch_url_bytes(str(source["url"]), headers=source.get("headers"), timeout=timeout, max_body=max_body)

        ttl_override = clamp_int(source.get("ttl_seconds", 0), 0, 0, 24 * 3600) or None
        self.source_cache.set(cache_key, body, ttl_override)
        return body

    def _normalize_static_item(self, source_name: str, payload: dict[str, Any], default_tz: ZoneInfo) -> NormalizedItem | None:
        title = compact_text(payload.get("title"), 80)
        if not title:
            return None
        kind = str(payload.get("kind") or "anniversary")
        priority = clamp_int(payload.get("priority", payload.get("importance", 70)), 70, 1, 100)

        start_at = None
        due_at = None
        all_day = False
        if payload.get("date"):
            start_at, all_day = parse_iso_like(str(payload["date"]), default_tz)
        elif payload.get("target_at"):
            start_at, all_day = parse_iso_like(str(payload["target_at"]), default_tz)
        elif payload.get("month") and payload.get("day"):
            now_local = now_utc().astimezone(default_tz)
            month = clamp_int(payload["month"], now_local.month, 1, 12)
            day = clamp_int(payload["day"], now_local.day, 1, 31)
            year = now_local.year
            try:
                date_value = dt.date(year, month, day)
            except ValueError:
                return None
            if date_value < now_local.date():
                date_value = dt.date(year + 1, month, day)
            start_at = dt.datetime.combine(date_value, dt.time(0, 0), tzinfo=default_tz)
            all_day = True
        else:
            due_at, all_day = parse_iso_like(payload.get("due_at"), default_tz)

        item = NormalizedItem(
            item_id=str(payload.get("id") or f"{source_name}:{title}"),
            title=title,
            kind=kind,
            source=str(payload.get("source") or source_name),
            priority=priority,
            start_at=start_at,
            due_at=due_at,
            all_day=all_day,
            special=bool(payload.get("special", kind in SPECIAL_KINDS)),
            countdown=bool(payload.get("countdown", kind in SPECIAL_KINDS or kind == "deadline")),
            location=compact_text(payload.get("location"), 80),
            status=str(payload.get("status") or ""),
        )
        return item

    def _expand_rrule(self, start_at: dt.datetime, end_at: dt.datetime, all_day: bool, rrule_raw: str, exdates: set[str], window_start: dt.datetime, window_end: dt.datetime) -> list[tuple[dt.datetime, dt.datetime]]:
        rule = parse_rrule(rrule_raw)
        freq = rule.get("FREQ", "").upper()
        interval = clamp_int(rule.get("INTERVAL", 1), 1, 1, 365)
        count_limit = int(rule.get("COUNT", "0") or 0)
        until_value = None
        if rule.get("UNTIL"):
            until_value, _ = parse_ics_datetime(rule["UNTIL"], {}, start_at.tzinfo if isinstance(start_at.tzinfo, ZoneInfo) else ZoneInfo("UTC"))

        out: list[tuple[dt.datetime, dt.datetime]] = []
        duration = end_at - start_at
        occurrences = 0

        def maybe_add(candidate: dt.datetime) -> None:
            nonlocal occurrences
            if candidate < start_at:
                return
            if until_value and candidate > until_value:
                return
            if count_limit and occurrences >= count_limit:
                return
            key = recurrence_key(candidate, all_day)
            if key in exdates:
                return
            candidate_end = candidate + duration
            if candidate_end < window_start or candidate > window_end:
                return
            out.append((candidate, candidate_end))
            occurrences += 1

        if freq == "DAILY":
            candidate = start_at
            while candidate <= window_end:
                maybe_add(candidate)
                if count_limit and occurrences >= count_limit:
                    break
                candidate += dt.timedelta(days=interval)
            return out

        if freq == "WEEKLY":
            byday_raw = rule.get("BYDAY", "")
            weekday_map = {"MO": 0, "TU": 1, "WE": 2, "TH": 3, "FR": 4, "SA": 5, "SU": 6}
            bydays = [weekday_map[token] for token in byday_raw.split(",") if token in weekday_map]
            if not bydays:
                bydays = [start_at.weekday()]
            day_cursor = start_at.date()
            while day_cursor <= window_end.date():
                delta_days = (day_cursor - start_at.date()).days
                if delta_days >= 0 and ((delta_days // 7) % interval == 0) and day_cursor.weekday() in bydays:
                    candidate = dt.datetime.combine(day_cursor, start_at.timetz().replace(tzinfo=None), tzinfo=start_at.tzinfo)
                    maybe_add(candidate)
                    if count_limit and occurrences >= count_limit:
                        break
                day_cursor += dt.timedelta(days=1)
            return out

        if freq == "MONTHLY":
            candidate = start_at
            while candidate <= window_end:
                maybe_add(candidate)
                if count_limit and occurrences >= count_limit:
                    break
                candidate = add_months(candidate, interval)
            return out

        if freq == "YEARLY":
            candidate = start_at
            while candidate <= window_end:
                maybe_add(candidate)
                if count_limit and occurrences >= count_limit:
                    break
                candidate = candidate.replace(year=candidate.year + interval)
            return out

        maybe_add(start_at)
        return out

    def _parse_ics_source(self, source: dict[str, Any], default_tz: ZoneInfo, window_start: dt.datetime, window_end: dt.datetime) -> list[NormalizedItem]:
        text = self._read_source_bytes(source).decode("utf-8", errors="replace")
        lines = unfold_ics(text)
        components = iter_ics_components(lines)
        items: list[NormalizedItem] = []
        assume_utc_as_local = bool(source.get("assume_utc_as_local", False))
        for component in components:
            props: dict[str, list[tuple[dict[str, str], str]]] = {}
            for raw in component["props"]:
                name, params, value = parse_ics_property(raw)
                props.setdefault(name, []).append((params, value))

            title = compact_text(unescape_ics_text((props.get("SUMMARY") or [({}, "")])[0][1]), 80)
            if not title:
                continue

            source_name = str(source.get("name") or "calendar")
            priority = clamp_int(source.get("default_priority", 60), 60, 1, 100)
            kind = str(source.get("kind") or ("task" if component["type"] == "VTODO" else "event"))
            status = str((props.get("STATUS") or [({}, "")])[0][1] or "")
            location = compact_text(unescape_ics_text((props.get("LOCATION") or [({}, "")])[0][1]), 80)

            exdates = parse_exdates([(value, params) for params, value in props.get("EXDATE", [])], default_tz, assume_utc_as_local)
            start_at = None
            end_at = None
            due_at = None
            all_day = False

            if component["type"] == "VEVENT":
                if "DTSTART" not in props:
                    continue
                start_at, all_day = parse_ics_datetime(props["DTSTART"][0][1], props["DTSTART"][0][0], default_tz, assume_utc_as_local)
                if not start_at:
                    continue
                if "DTEND" in props:
                    end_at, _ = parse_ics_datetime(props["DTEND"][0][1], props["DTEND"][0][0], default_tz, assume_utc_as_local)
                if not end_at:
                    end_at = start_at + (dt.timedelta(days=1) if all_day else dt.timedelta(hours=1))

                occurrences: list[tuple[dt.datetime, dt.datetime]]
                if "RRULE" in props:
                    occurrences = self._expand_rrule(start_at, end_at, all_day, props["RRULE"][0][1], exdates, window_start, window_end)
                else:
                    occurrences = [(start_at, end_at)] if not (end_at < window_start or start_at > window_end) else []

                uid = (props.get("UID") or [({}, title)])[0][1]
                for idx, (occ_start, occ_end) in enumerate(occurrences):
                    items.append(
                        NormalizedItem(
                            item_id=f"{uid}:{idx}",
                            title=title,
                            kind=kind,
                            source=source_name,
                            priority=priority,
                            start_at=occ_start,
                            end_at=occ_end,
                            all_day=all_day,
                            location=location,
                            status=status,
                            special=bool(source.get("special", kind in SPECIAL_KINDS)),
                            countdown=bool(source.get("countdown", kind in SPECIAL_KINDS)),
                        )
                    )
                continue

            if component["type"] == "VTODO":
                if "DUE" in props:
                    due_at, all_day = parse_ics_datetime(props["DUE"][0][1], props["DUE"][0][0], default_tz, assume_utc_as_local)
                elif "DTSTART" in props:
                    due_at, all_day = parse_ics_datetime(props["DTSTART"][0][1], props["DTSTART"][0][0], default_tz, assume_utc_as_local)
                if not due_at:
                    continue
                if status.upper() == "COMPLETED" and not source.get("include_completed", False):
                    continue
                items.append(
                    NormalizedItem(
                        item_id=(props.get("UID") or [({}, title)])[0][1],
                        title=title,
                        kind=kind,
                        source=source_name,
                        priority=priority,
                        due_at=due_at,
                        all_day=all_day,
                        location=location,
                        status=status,
                        special=bool(source.get("special", kind in SPECIAL_KINDS)),
                        countdown=bool(source.get("countdown", kind == "deadline" or kind in SPECIAL_KINDS)),
                    )
                )
        return items

    def _parse_json_source(self, source: dict[str, Any], default_tz: ZoneInfo) -> list[NormalizedItem]:
        payload = json.loads(self._read_source_bytes(source).decode("utf-8"))
        payload = flatten_json_path(payload, source.get("items_path"))
        if isinstance(payload, dict) and isinstance(payload.get("items"), list):
            payload = payload["items"]
        if not isinstance(payload, list):
            raise ValueError(f"{source.get('name','json')} json source must resolve to a list")

        items: list[NormalizedItem] = []
        for index, raw in enumerate(payload):
            if not isinstance(raw, dict):
                continue
            title = compact_text(raw.get("title"), 80)
            if not title:
                continue
            kind = str(raw.get("kind") or source.get("kind") or "event")
            priority = clamp_int(raw.get("priority", source.get("default_priority", 60)), 60, 1, 100)
            start_at, all_day = parse_iso_like(raw.get("start_at"), default_tz)
            end_at, _ = parse_iso_like(raw.get("end_at"), default_tz)
            due_at, due_all_day = parse_iso_like(raw.get("due_at") or raw.get("date"), default_tz)
            all_day = bool(raw.get("all_day", all_day or due_all_day))
            item = NormalizedItem(
                item_id=str(raw.get("id") or f"{source.get('name','json')}:{index}:{title}"),
                title=title,
                kind=kind,
                source=str(raw.get("source") or source.get("name") or "json"),
                priority=priority,
                start_at=start_at,
                end_at=end_at,
                due_at=due_at,
                all_day=all_day,
                location=compact_text(raw.get("location"), 80),
                status=str(raw.get("status") or ""),
                special=bool(raw.get("special", source.get("special", kind in SPECIAL_KINDS))),
                countdown=bool(raw.get("countdown", source.get("countdown", kind in SPECIAL_KINDS or kind == "deadline"))),
            )
            items.append(item)
        return items

    def _parse_static_source(self, source: dict[str, Any], default_tz: ZoneInfo) -> list[NormalizedItem]:
        items: list[NormalizedItem] = []
        for raw in source.get("items", []) or []:
            if not isinstance(raw, dict):
                continue
            item = self._normalize_static_item(str(source.get("name") or "static"), raw, default_tz)
            if item:
                items.append(item)
        return items

    def _parse_todoist_source(self, source: dict[str, Any], default_tz: ZoneInfo) -> list[NormalizedItem]:
        token = str(source.get("token") or "").strip()
        if not token:
            raise ValueError("todoist source requires token")

        base_url = str(source.get("base_url") or "https://api.todoist.com/api/v1").rstrip("/")
        query = {}
        endpoint = "/tasks"
        if source.get("filter"):
            endpoint = "/tasks/filter"
            query["query"] = str(source["filter"])
        if source.get("project_id"):
            query["project_id"] = str(source["project_id"])
        if source.get("section_id"):
            query["section_id"] = str(source["section_id"])
        if source.get("label"):
            query["label"] = str(source["label"])
        url = base_url + endpoint
        if query:
            url += "?" + urllib.parse.urlencode(query)

        payload = json.loads(
            self._fetch_url_bytes(
                url,
                headers={"Authorization": f"Bearer {token}"},
                timeout=float(source.get("timeout", self.timeout_seconds)),
                max_body=clamp_int(source.get("max_body", DEFAULT_MAX_BODY), DEFAULT_MAX_BODY, 1024, 2 * 1024 * 1024),
            ).decode("utf-8")
        )
        if isinstance(payload, dict) and isinstance(payload.get("results"), list):
            payload = payload["results"]
        if not isinstance(payload, list):
            raise ValueError("todoist response must be a list")

        items: list[NormalizedItem] = []
        source_name = str(source.get("name") or "todoist")
        priority_map = {1: 35, 2: 55, 3: 75, 4: 95}
        for raw in payload:
            if not isinstance(raw, dict):
                continue
            title = compact_text(raw.get("content"), 80)
            if not title:
                continue
            due = raw.get("due") if isinstance(raw.get("due"), dict) else {}
            due_at, due_all_day = parse_iso_like(due.get("datetime") or due.get("date"), default_tz)
            is_recurring = bool(due.get("is_recurring"))
            priority = priority_map.get(int(raw.get("priority") or 1), clamp_int(source.get("default_priority", 70), 70, 1, 100))
            labels = raw.get("labels") if isinstance(raw.get("labels"), list) else []
            label_suffix = ""
            if labels:
                label_suffix = " · " + compact_text(", ".join(str(label) for label in labels), 24)
            description = compact_text(raw.get("description"), 80)
            display_title = title
            if description:
                display_title = compact_text(f"{title} - {description}", 80)

            items.append(
                NormalizedItem(
                    item_id=str(raw.get("id") or f"{source_name}:{title}"),
                    title=display_title,
                    kind="deadline" if due_at and not is_recurring else "task",
                    source=source_name,
                    priority=priority,
                    due_at=due_at,
                    all_day=bool(raw.get("is_completed", False) is False and due_all_day),
                    status="RECURRING" if is_recurring else "",
                    special=False,
                    countdown=bool(due_at),
                    location=label_suffix,
                )
            )
        return items

    def collect_items(self) -> list[NormalizedItem]:
        config = self.load_config()
        tz = ZoneInfo(str(config.get("timezone") or "UTC"))
        now_local = now_utc().astimezone(tz)
        window_start = now_local - dt.timedelta(days=1)
        window_end = now_local + dt.timedelta(days=self.horizon_days)
        items: list[NormalizedItem] = []
        self.last_source_errors = []

        for source in config.get("sources", []) or []:
            if not isinstance(source, dict):
                continue
            source_type = str(source.get("type") or "").lower()
            try:
                if source_type == "ics":
                    items.extend(self._parse_ics_source(source, tz, window_start, window_end))
                elif source_type == "json":
                    items.extend(self._parse_json_source(source, tz))
                elif source_type == "todoist":
                    items.extend(self._parse_todoist_source(source, tz))
                elif source_type == "static":
                    items.extend(self._parse_static_source(source, tz))
                else:
                    logging.warning("unsupported source type: %s", source_type)
            except Exception as exc:
                logging.warning("source %s failed: %s", source.get("name") or source_type, exc)
                self.last_source_errors.append({
                    "source": str(source.get("name") or source_type),
                    "detail": compact_text(exc, 96),
                })

        return items

    def _choose_today_holiday(self, items: list[NormalizedItem], today: dt.date) -> str:
        best = None
        for item in items:
            if item.kind != "holiday":
                continue
            if not item.occurs_on(today):
                continue
            if best is None or item.priority > best.priority:
                best = item
        return best.title if best else ""

    def _minutes_until(self, target: dt.datetime, now_local: dt.datetime) -> int:
        return int((target - now_local).total_seconds() // 60)

    def _sort_next_key(self, item: NormalizedItem, now_local: dt.datetime) -> tuple[int, int]:
        primary = item.primary_time()
        if not primary:
            return (999999, -item.priority)

        score = 0
        delta_min = self._minutes_until(primary, now_local)
        if item.start_at and item.end_at and item.start_at <= now_local <= item.end_at:
            score = -200000
        elif item.kind in {"task", "deadline"} and delta_min <= 0:
            score = -180000
        elif delta_min <= 30:
            score = -160000 + delta_min
        elif item.kind in {"task", "deadline"} and primary.date() == now_local.date():
            score = -140000 + max(delta_min, 0)
        elif delta_min >= 0:
            score = -120000 + delta_min
        else:
            score = 500000 + abs(delta_min)
        return (score, -item.priority)

    def _select_next_item(self, items: list[NormalizedItem], now_local: dt.datetime) -> NormalizedItem | None:
        candidates: list[NormalizedItem] = []
        fallback_tasks: list[NormalizedItem] = []
        for item in items:
            primary = item.primary_time()
            if item.kind == "task" and not primary and item.status.upper() != "COMPLETED":
                fallback_tasks.append(item)
                continue
            if not primary:
                continue
            if item.kind in SPECIAL_KINDS and not item.countdown:
                continue
            if item.status.upper() == "COMPLETED":
                continue
            if item.end_at and item.end_at < now_local and item.kind == "event":
                continue
            if primary < now_local - dt.timedelta(days=1):
                continue
            candidates.append(item)
        if not candidates:
            if not fallback_tasks:
                return None
            fallback_tasks.sort(key=lambda item: (-item.priority, item.title))
            return fallback_tasks[0]
        candidates.sort(key=lambda item: self._sort_next_key(item, now_local))
        return candidates[0]

    def _build_busy_map(self, items: list[NormalizedItem], year: int, month: int) -> list[int]:
        days_in_month = (dt.date(year + (month // 12), (month % 12) + 1, 1) - dt.timedelta(days=1)).day if month < 12 else 31
        busy = [0] * days_in_month
        month_start = dt.date(year, month, 1)
        month_end = dt.date(year, month, days_in_month)
        for item in items:
            primary = item.primary_time()
            if not primary:
                continue
            if item.status.upper() == "COMPLETED":
                continue
            start_day = primary.date()
            end_day = (item.end_at or primary).date()
            if end_day < month_start or start_day > month_end:
                continue
            day_cursor = max(start_day, month_start)
            while day_cursor <= min(end_day, month_end):
                index = day_cursor.day - 1
                busy[index] += 1
                day_cursor += dt.timedelta(days=1)
        return busy

    def _build_today_summary(self, items: list[NormalizedItem], now_local: dt.datetime) -> dict[str, Any]:
        today = now_local.date()
        todays_events = [item for item in items if item.kind == "event" and item.occurs_on(today)]
        meetings_left = [item for item in todays_events if item.start_at and item.start_at >= now_local]
        due_today = [item for item in items if item.kind in {"task", "deadline"} and item.due_at and item.due_at.date() == today and item.status.upper() != "COMPLETED"]

        timed_events = [item for item in todays_events if item.start_at and item.end_at and not item.all_day and item.end_at >= now_local]
        timed_events.sort(key=lambda item: item.start_at or now_local)
        free_until = ""
        longest_gap = 0
        cursor = now_local
        if timed_events:
            first = timed_events[0]
            if first.start_at:
                free_until = first.start_at.strftime("%H:%M")
        end_of_day = dt.datetime.combine(today, dt.time(23, 59), tzinfo=now_local.tzinfo)
        for item in timed_events:
            if item.start_at and item.start_at > cursor:
                longest_gap = max(longest_gap, int((item.start_at - cursor).total_seconds() // 60))
            if item.end_at and item.end_at > cursor:
                cursor = item.end_at
        if end_of_day > cursor:
            longest_gap = max(longest_gap, int((end_of_day - cursor).total_seconds() // 60))

        return {
            "events_total": len(todays_events),
            "meetings_left": len(meetings_left),
            "tasks_due_today": len(due_today),
            "focus_block_minutes": longest_gap,
            "free_until": free_until,
        }

    def _serialize_next_item(self, item: NormalizedItem | None, now_local: dt.datetime) -> dict[str, Any] | None:
        if not item:
            return None
        primary = item.primary_time()
        minutes_left = self._minutes_until(primary, now_local) if primary else None
        payload = {
            "kind": item.kind,
            "title": item.title,
            "start_at": item.start_at.isoformat() if item.start_at else None,
            "due_at": item.due_at.isoformat() if item.due_at else None,
            "all_day": item.all_day,
            "source": item.source,
            "source_label": str(item.source).upper(),
            "location": item.location,
            "minutes_left": minutes_left,
            "target_unix": int(primary.timestamp()) if primary else None,
            "priority": item.priority,
        }
        return payload

    def _build_special_lists(self, items: list[NormalizedItem], now_local: dt.datetime) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
        specials: list[dict[str, Any]] = []
        countdowns: list[dict[str, Any]] = []
        for item in items:
            primary = item.primary_time()
            if not primary:
                continue
            if item.status.upper() == "COMPLETED":
                continue
            if primary.date() < now_local.date():
                continue
            days_left = (primary.date() - now_local.date()).days
            payload = {
                "title": item.title,
                "kind": item.kind,
                "date": primary.date().isoformat(),
                "days_left": days_left,
                "importance": item.priority,
            }
            if item.special or item.kind in SPECIAL_KINDS:
                specials.append(payload)
            if item.countdown or item.kind in SPECIAL_KINDS or item.kind == "deadline":
                countdowns.append(
                    {
                        "title": item.title,
                        "kind": item.kind,
                        "target_at": primary.isoformat(),
                        "days_left": days_left,
                        "importance": item.priority,
                    }
                )
        specials.sort(key=lambda row: (-int(row["importance"]), int(row["days_left"])))
        countdowns.sort(key=lambda row: (-int(row["importance"]), int(row["days_left"])))
        return specials[:5], countdowns[:5]

    def build_snapshot(self) -> tuple[dict[str, Any], list[dict[str, Any]]]:
        cached = self.snapshot_cache.get("snapshot")
        cached_items = self.snapshot_cache.get("items")
        if cached is not None and cached_items is not None:
            return cached, cached_items

        config = self.load_config()
        tz = ZoneInfo(str(config.get("timezone") or "UTC"))
        now_local = now_utc().astimezone(tz)
        items = self.collect_items()

        today = now_local.date()
        first_day = today.replace(day=1)
        if today.month == 12:
            next_month = dt.date(today.year + 1, 1, 1)
        else:
            next_month = dt.date(today.year, today.month + 1, 1)
        days_in_month = (next_month - first_day).days
        next_item = self._select_next_item(items, now_local)
        specials, countdowns = self._build_special_lists(items, now_local)

        snapshot = {
            "generated_at": now_local.isoformat(),
            "timezone": str(tz),
            "health": {
                "sources_total": len(config.get("sources", []) or []),
                "sources_failed": len(self.last_source_errors),
                "source_errors": self.last_source_errors,
            },
            "today": {
                "date": today.isoformat(),
                "weekday": weekday_label(today.weekday()),
                "month_label": month_label(today.month),
                "year": today.year,
                "month": today.month,
                "day": today.day,
                "holiday": self._choose_today_holiday(items, today),
                "is_workday": today.weekday() < 5,
            },
            "month": {
                "year": today.year,
                "month": today.month,
                "month_label": month_label(today.month),
                "first_weekday": (first_day.weekday() + 1) % 7,
                "days": days_in_month,
                "today": today.day,
                "busy_map": self._build_busy_map(items, today.year, today.month),
            },
            "next_item": self._serialize_next_item(next_item, now_local),
            "today_summary": self._build_today_summary(items, now_local),
            "countdowns": countdowns,
            "special_dates": specials,
        }

        debug_items = []
        for item in sorted(items, key=lambda it: (it.primary_time() or now_local, -it.priority, it.title)):
            debug_items.append(
                {
                    "id": item.item_id,
                    "title": item.title,
                    "kind": item.kind,
                    "source": item.source,
                    "priority": item.priority,
                    "start_at": item.start_at.isoformat() if item.start_at else None,
                    "end_at": item.end_at.isoformat() if item.end_at else None,
                    "due_at": item.due_at.isoformat() if item.due_at else None,
                    "all_day": item.all_day,
                    "location": item.location,
                    "status": item.status,
                    "special": item.special,
                    "countdown": item.countdown,
                }
            )

        self.last_good_snapshot = snapshot
        self.last_good_items = debug_items
        self.snapshot_cache.set("snapshot", snapshot)
        self.snapshot_cache.set("items", debug_items)
        return snapshot, debug_items

    def _time_label(self, value: str | None, tz_name: str) -> str:
        if not value:
          return ""
        try:
          parsed, _ = parse_iso_like(value, ZoneInfo(tz_name))
        except Exception:
          parsed = None
        if not parsed:
          return ""
        return parsed.strftime("%H:%M")

    def _date_label(self, value: str | None, tz_name: str) -> str:
        if not value:
          return ""
        try:
          parsed, _ = parse_iso_like(value, ZoneInfo(tz_name))
        except Exception:
          parsed = None
        if not parsed:
          return str(value)[:10]
        return parsed.date().isoformat()

    def _payload_with_meta(self, snapshot: dict[str, Any], payload: dict[str, Any]) -> dict[str, Any]:
        out = {"generated_at": snapshot.get("generated_at"), "timezone": snapshot.get("timezone")}
        out.update(payload)
        out["revision"] = revision_for_payload(out)
        return out

    def _meta_for_view(self, payload: dict[str, Any]) -> dict[str, Any]:
        return {
            "generated_at": payload.get("generated_at"),
            "timezone": payload.get("timezone"),
            "revision": payload.get("revision") or revision_for_payload(payload),
            "stale": bool(payload.get("stale", False)),
            "health": payload.get("health") or {},
        }

    def build_views(self) -> tuple[dict[str, Any], list[dict[str, Any]]]:
        cached_views = self.snapshot_cache.get("views")
        cached_items = self.snapshot_cache.get("items")
        if cached_views is not None and cached_items is not None:
            return cached_views, cached_items

        snapshot, items = self.build_snapshot()
        tz_name = str(snapshot.get("timezone") or "UTC")
        today = snapshot.get("today") or {}
        summary = snapshot.get("today_summary") or {}
        next_item = snapshot.get("next_item") or {}
        specials = snapshot.get("special_dates") or []
        countdowns = snapshot.get("countdowns") or []

        next_time = self._time_label(next_item.get("start_at") or next_item.get("due_at"), tz_name) if isinstance(next_item, dict) else ""
        next_title = compact_text(next_item.get("title") if isinstance(next_item, dict) else "", 32)
        next_kind = str(next_item.get("kind") or "") if isinstance(next_item, dict) else ""

        special_item = specials[0] if specials else (countdowns[0] if countdowns else None)

        today_view = self._payload_with_meta(
            snapshot,
            {
                "health": snapshot.get("health"),
                "today": {
                    "weekday": today.get("weekday") or "",
                    "month_label": today.get("month_label") or "",
                    "day": today.get("day") or 0,
                    "holiday": today.get("holiday") or "",
                },
                "summary": {
                    "events_total": summary.get("events_total") or 0,
                    "tasks_due_today": summary.get("tasks_due_today") or 0,
                    "free_until": summary.get("free_until") or "",
                },
                "next": {
                "title": next_title,
                "kind": next_kind,
                "minutes_left": next_item.get("minutes_left") if isinstance(next_item, dict) else None,
                "target_unix": next_item.get("target_unix") if isinstance(next_item, dict) else None,
                "time_label": next_time,
                "source_label": str(next_item.get("source") or "").upper() if isinstance(next_item, dict) else "",
                "all_day": bool(next_item.get("all_day")) if isinstance(next_item, dict) else False,
            },
                "primary": {
                    "title": next_title,
                },
                "secondary": {
                    "text": (today.get("holiday") or ("Free until " + str(summary.get("free_until")) if summary.get("free_until") else "")) or str(next_item.get("source") or "").upper(),
                },
            },
        )

        countdown_item = None
        if isinstance(next_item, dict) and next_item.get("title"):
            countdown_item = {
                "title": next_title,
                "kind": next_kind,
                "minutes_left": next_item.get("minutes_left"),
                "target_unix": next_item.get("target_unix"),
                "days_left": None,
                "time_label": next_time,
                "date_label": self._date_label(next_item.get("start_at") or next_item.get("due_at"), tz_name),
                "all_day": bool(next_item.get("all_day")),
                "label": str(next_item.get("source") or "").upper(),
            }
        elif special_item:
            countdown_item = {
                "title": compact_text(special_item.get("title"), 32),
                "kind": special_item.get("kind"),
                "minutes_left": None,
                "days_left": special_item.get("days_left"),
                "time_label": "",
                "date_label": special_item.get("date") or self._date_label(special_item.get("target_at"), tz_name),
                "all_day": True,
                "label": str(special_item.get("kind") or "").upper(),
            }

        countdown_view = self._payload_with_meta(snapshot, {"item": countdown_item})

        daily_flip_view = self._payload_with_meta(
            snapshot,
            {
                "health": snapshot.get("health"),
                "today": {
                    "year": today.get("year") or 0,
                    "month_label": today.get("month_label") or "",
                    "weekday": today.get("weekday") or "",
                    "day": today.get("day") or 0,
                    "holiday": today.get("holiday") or "",
                },
                "footer": {
                    "text": today.get("holiday") or next_title or "Calendar day",
                    "tone": "special" if today.get("holiday") else ("accent" if next_title else "muted"),
                },
            },
        )

        special_view = self._payload_with_meta(
            snapshot,
            {
                "health": snapshot.get("health"),
                "item": {
                    "title": compact_text((special_item or {}).get("title"), 32) if special_item else "",
                    "kind": (special_item or {}).get("kind") if special_item else "",
                    "days_left": (special_item or {}).get("days_left") if special_item else None,
                    "date_label": (special_item or {}).get("date") or self._date_label((special_item or {}).get("target_at"), tz_name),
                    "label": str((special_item or {}).get("kind") or "").upper() if special_item else "",
                } if special_item else None,
            },
        )

        month_view = self._payload_with_meta(
            snapshot,
            {
                "health": snapshot.get("health"),
                "month": snapshot.get("month"),
                "footer": {
                    "text": compact_text((special_item or {}).get("title") or next_title or "Month overview", 32),
                },
            },
        )

        views = {
            "/calendar/v1/today-glance": today_view,
            "/calendar/v1/event-countdown": countdown_view,
            "/calendar/v1/daily-flip": daily_flip_view,
            "/calendar/v1/special-date": special_view,
            "/calendar/v1/month-card": month_view,
            "/calendar/v1/snapshot": snapshot,
        }
        meta_views = {
            "/calendar/v1/today-glance-meta": self._meta_for_view(today_view),
            "/calendar/v1/event-countdown-meta": self._meta_for_view(countdown_view),
            "/calendar/v1/daily-flip-meta": self._meta_for_view(daily_flip_view),
            "/calendar/v1/special-date-meta": self._meta_for_view(special_view),
            "/calendar/v1/month-card-meta": self._meta_for_view(month_view),
        }
        for path, meta in meta_views.items():
            views[path] = meta
        self.last_good_views = views
        self.snapshot_cache.set("views", views)
        return views, items


class CalendarHandler(BaseHTTPRequestHandler):
    server_version = "PixelCalendarGateway/1.0"

    @property
    def gateway(self) -> CalendarGateway:
        return self.server.gateway  # type: ignore[attr-defined]

    def log_message(self, fmt: str, *args) -> None:
        logging.info("%s - %s", self.address_string(), fmt % args)

    def _send_json(self, status: int, payload: Any) -> None:
        body = json.dumps(payload, ensure_ascii=True, indent=2).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path == "/health":
            self._send_json(HTTPStatus.OK, {"ok": True, "service": "calendar_snapshot_gateway"})
            return

        try:
            views, items = self.gateway.build_views()
        except Exception as exc:
            logging.exception("snapshot build failed")
            if self.gateway.last_good_views is not None:
                stale = copy.deepcopy(self.gateway.last_good_views.get(parsed.path or "/calendar/v1/snapshot") or self.gateway.last_good_views.get("/calendar/v1/snapshot") or {})
                stale["stale"] = True
                stale["error"] = str(exc)
                self._send_json(HTTPStatus.OK, stale)
                return
            self._send_json(HTTPStatus.BAD_GATEWAY, {"ok": False, "error": "snapshot_build_failed", "detail": str(exc)})
            return

        if parsed.path in views:
            self._send_json(HTTPStatus.OK, views[parsed.path])
            return
        if parsed.path == "/calendar/v1/items":
            self._send_json(HTTPStatus.OK, {"ok": True, "items": items})
            return

        self._send_json(HTTPStatus.NOT_FOUND, {"ok": False, "error": "route_not_found"})


def main() -> int:
    args = parse_args()
    logging.basicConfig(level=getattr(logging, str(args.log_level).upper(), logging.INFO), format="%(asctime)s %(levelname)s %(message)s")
    gateway = CalendarGateway(
        config_path=args.config,
        timeout_seconds=args.timeout,
        source_ttl=args.source_ttl,
        snapshot_ttl=args.snapshot_ttl,
        horizon_days=args.horizon_days,
    )
    server = ThreadingHTTPServer((args.host, args.port), CalendarHandler)
    server.gateway = gateway  # type: ignore[attr-defined]
    logging.info("calendar_snapshot_gateway listening on http://%s:%d", args.host, args.port)
    logging.info("config: %s", gateway.config_path)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        logging.info("calendar_snapshot_gateway shutting down")
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
