# Game Summary Plan

## Goal

Build a focused single-game detail app for live monitoring.

Primary question:

`What is happening inside this game right now?`

## App Identity

- App id: `game_summary`
- Category: `sports`
- Priority: `P2`

## Product Position

This is not a league app.

It is a companion app for users who want one tracked game on screen during the event.

## Recommended ESPN Endpoints

- Friendly summary endpoint:
  - `https://site.api.espn.com/apis/site/v2/sports/{sport}/{league}/summary?event={event_id}`
- Rich game package endpoint:
  - `https://cdn.espn.com/core/{league_path}/game?xhr=1&gameId={event_id}`

Use order:

1. `summary` for MVP
2. `cdn game package` for sport-specific enhancements

## Supported MVP Sports

- Soccer
- NFL
- NBA

## Display Modes

### Soccer

Show:

- `ARS 2-1 CHE`
- minute or phase
- red card or penalty marker if present

Optional phase 2 fields:

- possession
- shots on target

### NFL

Show:

- score
- quarter/time
- possession
- down and distance
- ball spot

Example:

- `DAL 17-14 PHI`
- `Q4 08:21`
- `3rd & 7`
- `PHI 42`

### NBA

Show:

- score
- quarter/time
- bonus or run indicator in phase 2

Example:

- `LAL 98-94 BOS`
- `Q4 03:12`

## Config Keys

- `sports.api_base_url`
- `game_summary.sport`
- `game_summary.league`
- `game_summary.event_id`
- `game_summary.team`
- `game_summary.auto_pick_live`
- `game_summary.ttl_ms`
- `game_summary.rotate_ms`

## Event Resolution

Preferred:

- explicit `event_id`

Fallback:

- choose current live event for configured team from scoreboard

## Normalized Payload Shape

```json
{
  "generated_at": "2026-03-31T19:00:00Z",
  "sport": "football",
  "league": "nfl",
  "event_id": "401772123",
  "headline": "DAL 17-14 PHI",
  "status_label": "Q4 08:21",
  "detail_1": "3RD & 7",
  "detail_2": "PHI 42",
  "home_abbr": "PHI",
  "away_abbr": "DAL",
  "home_score": 14,
  "away_score": 17
}
```

## ESPN Field Mapping

Likely sources:

- score and status from `summary.header`
- team lines from `boxscore.teams`
- situation from `drives` or `situation`
- soccer incidents from notes or situation fields

The exact parser should be sport-specific.

## Rendering Rules

- Max 4 lines
- Top line must always be score headline
- Never attempt to render full box score on device
- Fallback cleanly if advanced fields are missing

## MVP Scope

- One event
- Score
- Status short detail
- One sport-specific detail line if available

## Phase 2

- Soccer cards and scorers
- NFL possession and red-zone marker
- NBA leading scorer
- MLB inning state
- NHL power play indicator

## Risks

- `summary` payload shape differs substantially by sport
- Live detail fields are more volatile than scoreboard fields
- CDN package can be larger and less device-friendly

## Implementation Checklist

- Add event resolution helper
- Add summary fetch path
- Define per-sport parsers
- Add safe fallback screen
- Add stale-data badge for ended network fetches

## Test Cases

- Live soccer match
- Live NFL game with situation
- Live NBA game
- Scheduled event without live detail
- Final event with summary only
