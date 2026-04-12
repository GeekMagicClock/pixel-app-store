# Team Schedule Plan

## Goal

Build a team-centric schedule app for upcoming fixtures and recent results.

Primary question:

`When is my team's next game and what happened recently?`

## App Identity

- App id: `team_schedule`
- Category: `sports`
- Priority: `P1`

## User Value

- Useful even when there is no live game
- Strong off-season and off-day value
- Great companion to the scoreboard app

## Recommended ESPN Endpoints

- Team schedule:
  - `https://site.api.espn.com/apis/site/v2/sports/{sport}/{league}/teams/{team_id}/schedule`
- Team detail fallback:
  - `https://site.api.espn.com/apis/site/v2/sports/{sport}/{league}/teams/{team_id}`
- Scoreboard fallback:
  - `https://site.api.espn.com/apis/site/v2/sports/{sport}/{league}/scoreboard`

## Core Screens

### 1. Next Game

Show:

- Team abbreviation
- Opponent abbreviation
- Home or away marker
- Local date
- Local time

### 2. Next 3 Games

Rotate one future event per frame.

### 3. Last Result

Show:

- Opponent
- Final score
- `W` or `L`
- Final marker

## Suggested 64x32 Layout

Frame A:

- Row 1: `ARS NEXT`
- Row 2: `vs TOT`
- Row 3: `APR 03`
- Row 4: `20:00`

Frame B:

- Row 1: `LAST`
- Row 2: `ARS 2-1`
- Row 3: `CHE`
- Row 4: `W FT`

## Config Keys

- `sports.api_base_url`
- `team_schedule.sport`
- `team_schedule.league`
- `team_schedule.team`
- `team_schedule.team_id`
- `team_schedule.ttl_ms`
- `team_schedule.rotate_ms`
- `team_schedule.include_last_result`
- `team_schedule.count`

## Event Selection Rules

Future bucket:

- scheduled events where start time is after now

Past bucket:

- completed events before now

Sort logic:

- future ascending by start time
- past descending by end time or event date

## Normalized Payload Shape

```json
{
  "generated_at": "2026-03-31T19:00:00Z",
  "sport": "soccer",
  "league": "eng.1",
  "team": {"abbr": "ARS", "name": "Arsenal"},
  "next_games": [
    {
      "event_id": "1",
      "label": "vs TOT",
      "date_label": "APR 03",
      "time_label": "20:00",
      "home": true
    }
  ],
  "last_result": {
    "event_id": "2",
    "label": "vs CHE",
    "score_label": "2-1",
    "outcome": "W",
    "status_label": "FT"
  }
}
```

## ESPN Field Mapping

The schedule response should provide:

- event id
- date
- competition
- opponent team
- home/away marker
- completed status
- score if available

Key fields are expected around:

- `.events[]`
- `.competitions[0]`
- `.competitors[]`
- `.status.type`

## Rendering Rules

- Do not show more than one event per frame
- Use compact labels:
  - `vs`
  - `@`
  - `NEXT`
  - `LAST`
- Always convert to local timezone before rendering

## MVP Scope

- Next 3 games
- Last result
- Date/time local formatting
- Home/away marker

## Phase 2

- Countdown hours until next game
- TV/broadcast line
- Series state for playoffs
- Rest days indicator

## Risks

- Team schedule payload shape may differ by sport
- Some events may lack final score in team schedule payload
- Team id lookup must be reliable

## Implementation Checklist

- Define team lookup strategy
- Build future/past split helper
- Build local date formatter
- Create two-frame rotation model
- Add empty-state for offseason

## Test Cases

- Team has no future games
- Team has no past games
- Team has one live game in schedule
- Team crosses midnight in local timezone
- Playoff series game labels
