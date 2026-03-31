# Calendar Onboarding

You do not need to edit the apps themselves.

For the first usable version, you only need to provide a small amount of data:

## Required

1. Timezone
2. One API base URL or explicit app URLs
3. At least one data source for the gateway:
   - ICS calendar URL, or
   - local/static tasks JSON, or
   - static special dates

## Optional But Recommended

1. Work calendar ICS
2. Personal calendar ICS
3. Task source
4. Birthdays / anniversaries / holidays

## Fastest Path

If you want the quickest possible first run:

1. Copy [calendar_local.template.json](/Users/ifeng/develop/project/esp32-pixel/python/calendar_local.template.json)
   to your own file, for example `python/calendar_local.json`
2. Copy [calendar_tasks.template.json](/Users/ifeng/develop/project/esp32-pixel/python/calendar_tasks.template.json)
   to `python/calendar_tasks.json`
3. Fill:
   - timezone
   - tasks
   - static special dates
4. Run the gateway
5. Point the device to `calendar.api_base_url`

## Device-Side Keys To Set

Recommended single setting:

- `calendar.api_base_url`

Optional explicit per-app URLs:

- `today_glance.url`
- `event_countdown.url`
- `daily_flip_calendar.url`
- `special_date_beacon.url`
- `calendar_month_card.url`

Optional shared transport tuning:

- `calendar.ttl_ms` optional
- `calendar.timeout_ms` optional
- `calendar.max_body` optional

Example:

`http://YOUR_COMPUTER_IP:8791/calendar/v1`

## Information I Need From You If You Want Me To Wire Real Sources

Send any of these that you already have:

1. Your target timezone
   Example: `Europe/London`
2. Work calendar ICS URL
3. Personal calendar ICS URL
4. Task source type
   Example: `Todoist`, `TickTick export`, `custom JSON`, `manual local file`
5. Important recurring dates
   Example: birthdays, anniversaries, payday, bill day
6. Which app should be the default home screen
   Suggested: `today_glance`

## Minimum Manual Fill Example

You can start with only:

1. timezone
2. 1-5 static tasks
3. 1-5 static special dates

That is enough to test all 5 calendar apps before integrating live calendars.
