# Calendar Gateway Setup

The gateway may aggregate multiple calendar/task sources internally, but it
publishes independent endpoint views for each app. Apps stay decoupled.

## Run

```bash
python3 python/calendar_snapshot_gateway.py \
  --config python/calendar_snapshot.example.json \
  --host 0.0.0.0 \
  --port 8791
```

Endpoints:

- `GET /health`
- `GET /calendar/v1/today-glance`
- `GET /calendar/v1/event-countdown`
- `GET /calendar/v1/daily-flip`
- `GET /calendar/v1/special-date`
- `GET /calendar/v1/month-card`
- `GET /calendar/v1/items`
- `GET /calendar/v1/snapshot` debug only

## Point Device Apps At The Gateway

Recommended:

- `calendar.api_base_url = "http://YOUR_HOST:8791/calendar/v1"`

Optional explicit per-app URLs:

- `today_glance.url`
- `event_countdown.url`
- `daily_flip_calendar.url`
- `special_date_beacon.url`
- `calendar_month_card.url`

Optional:

- `today_glance.ttl_ms`
- `event_countdown.ttl_ms`
- `daily_flip_calendar.ttl_ms`
- `special_date_beacon.ttl_ms`
- `calendar_month_card.ttl_ms`
- `calendar.ttl_ms`
- `calendar.timeout_ms`
- `calendar.max_body`

## Supported Source Types

### 1. ICS

```json
{
  "type": "ics",
  "name": "work",
  "url": "https://calendar.example.com/work.ics",
  "default_priority": 85
}
```

Notes:

- Supports `VEVENT`
- Supports `VTODO`
- Supports basic `RRULE` expansion for `DAILY`, `WEEKLY`, `MONTHLY`, `YEARLY`
- Supports `EXDATE`

### 2. Todoist

```json
{
  "type": "todoist",
  "name": "todoist",
  "token": "YOUR_TOKEN",
  "default_priority": 80
}
```

Optional fields:

- `filter`
- `project_id`
- `section_id`
- `label`
- `base_url`
- `timeout`
- `max_body`

Default base URL:

- `https://api.todoist.com/api/v1`

### 3. JSON

Expected formats:

- top-level array of items
- object with `items`

Normalized item example:

```json
{
  "items": [
    {
      "id": "task-42",
      "title": "Ship v2",
      "kind": "deadline",
      "due_at": "2026-04-04T17:00:00+01:00",
      "priority": 95,
      "countdown": true
    }
  ]
}
```

### 4. Static

Good for birthdays, anniversaries, public holidays, paydays.

```json
{
  "type": "static",
  "name": "special",
  "items": [
    {
      "title": "Mom Birthday",
      "kind": "anniversary",
      "month": 4,
      "day": 2,
      "priority": 100,
      "special": true,
      "countdown": true
    }
  ]
}
```

## Recommended Deployment Pattern

1. Run this gateway on a machine already on the same LAN as the display.
2. Feed it one or more ICS URLs plus an optional task JSON feed.
3. Keep business logic here:
   title trimming, source merging, special dates, countdown priority.
4. Keep app-facing JSON dedicated per app.
5. Keep the device passive and read-only.
