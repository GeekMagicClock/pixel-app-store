# Calendar App Endpoints

The calendar apps are independent at the app layer.

They do not rely on one mandatory mega-payload anymore. Each app reads its own
endpoint and its own config key. A backend may still reuse internal aggregation
logic, but that is an implementation detail behind the API.

## Common Device Config

Recommended:

- `calendar.api_base_url`

Optional per-app override:

- `today_glance.url`
- `event_countdown.url`
- `daily_flip_calendar.url`
- `special_date_beacon.url`
- `calendar_month_card.url`

Optional per-app transport tuning:

- `today_glance.ttl_ms`
- `event_countdown.ttl_ms`
- `daily_flip_calendar.ttl_ms`
- `special_date_beacon.ttl_ms`
- `calendar_month_card.ttl_ms`

Fallback shared transport tuning:

- `calendar.ttl_ms`
- `calendar.timeout_ms`
- `calendar.max_body`

If `calendar.api_base_url` is:

`http://HOST:8791/calendar/v1`

then the apps resolve to:

- `today_glance` -> `/today-glance`
- `event_countdown` -> `/event-countdown`
- `daily_flip_calendar` -> `/daily-flip`
- `special_date_beacon` -> `/special-date`
- `calendar_month_card` -> `/month-card`

## Endpoints

### 1. Today Glance

- `GET /calendar/v1/today-glance`

```json
{
  "generated_at": "2026-03-28T09:02:10+00:00",
  "timezone": "Europe/London",
  "today": {
    "weekday": "SAT",
    "month_label": "MAR",
    "day": 28,
    "holiday": ""
  },
  "summary": {
    "events_total": 3,
    "tasks_due_today": 1,
    "free_until": "14:30"
  },
  "next": {
    "title": "Product Review",
    "kind": "event",
    "minutes_left": 327,
    "time_label": "14:30",
    "source_label": "WORK",
    "all_day": false
  },
  "primary": {
    "title": "Product Review"
  },
  "secondary": {
    "text": "Free until 14:30"
  }
}
```

### 2. Event Countdown

- `GET /calendar/v1/event-countdown`

```json
{
  "generated_at": "2026-03-28T09:02:10+00:00",
  "timezone": "Europe/London",
  "item": {
    "title": "Product Review",
    "kind": "event",
    "minutes_left": 327,
    "days_left": null,
    "time_label": "14:30",
    "date_label": "2026-03-28",
    "all_day": false,
    "label": "WORK"
  }
}
```

### 3. Daily Flip Calendar

- `GET /calendar/v1/daily-flip`

```json
{
  "generated_at": "2026-03-28T09:02:10+00:00",
  "timezone": "Europe/London",
  "today": {
    "year": 2026,
    "month_label": "MAR",
    "weekday": "SAT",
    "day": 28,
    "holiday": ""
  },
  "footer": {
    "text": "Product Review",
    "tone": "accent"
  }
}
```

### 4. Special Date Beacon

- `GET /calendar/v1/special-date`

```json
{
  "generated_at": "2026-03-28T09:02:10+00:00",
  "timezone": "Europe/London",
  "item": {
    "title": "Mom Birthday",
    "kind": "anniversary",
    "days_left": 5,
    "date_label": "2026-04-02",
    "label": "ANNIVERSARY"
  }
}
```

### 5. Calendar Month Card

- `GET /calendar/v1/month-card`

```json
{
  "generated_at": "2026-03-28T09:02:10+00:00",
  "timezone": "Europe/London",
  "month": {
    "year": 2026,
    "month": 3,
    "month_label": "MAR",
    "first_weekday": 0,
    "days": 31,
    "today": 28,
    "busy_map": [0, 1, 0, 2, 3, 1, 0]
  },
  "footer": {
    "text": "Mom Birthday"
  }
}
```

## Debug Views

- `GET /calendar/v1/items`
- `GET /calendar/v1/snapshot`

These exist for debugging and inspection. Apps should not depend on them.
