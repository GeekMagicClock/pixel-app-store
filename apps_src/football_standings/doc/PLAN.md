# Sports Standings Plan

## Goal

Build a standings-focused app optimized for rank awareness on a 64x32 display.

Primary question:

`Where does my team stand right now?`

## App Identity

- App id: `sports_standings`
- Category: `sports`
- Priority: `P1`

## User Value

- Quick rank check without opening phone
- See top contenders in one glance
- Follow own team with nearby rivals

## Recommended ESPN Endpoints

- Standings:
  - `https://site.api.espn.com/apis/v2/sports/{sport}/{league}/standings`

Important note:

- Use `/apis/v2/`
- Do not use `/apis/site/v2/` for standings

## Supported Modes

### 1. Top Table

Show top 4 or top 6 entries for a league or conference.

Best for:

- EPL
- La Liga
- Bundesliga
- NBA conference
- NFL division

### 2. Around My Team

Show the selected team plus 2 neighbors above and below.

Best for:

- Dense standings
- Relegation races
- Playoff cutoff tracking

## Display Models

### Soccer

Recommended row format:

- `1 ARS 70`
- `2 LIV 68`
- `3 MCI 67`
- `4 AVL 59`

Alternative row format:

- `1 ARS +39`
- `2 LIV +38`

### NFL / NBA / NHL / MLB

Recommended row format:

- `1 BOS 58-21`
- `2 MIL 54-25`

Or compressed:

- `1 DAL .714`
- `2 PHI .688`

## Paging Strategy

- One page holds 4 rows comfortably
- Use 2 to 5 second rotation
- Repeat current team page more often than generic pages

## Config Keys

- `sports.api_base_url`
- `sports_standings.sport`
- `sports_standings.league`
- `sports_standings.team`
- `sports_standings.mode`
- `sports_standings.group`
- `sports_standings.ttl_ms`
- `sports_standings.rotate_ms`
- `sports_standings.rows_per_page`

## Recommended Modes By Sport

- Soccer:
  - `top`
  - `around_team`
- NFL:
  - `division`
  - `conference`
  - `around_team`
- NBA:
  - `conference`
  - `around_team`
- MLB:
  - `division`
  - `wildcard_lite` in phase 2
- NHL:
  - `conference`
  - `around_team`

## Normalized Payload Shape

```json
{
  "generated_at": "2026-03-31T19:00:00Z",
  "sport": "soccer",
  "league": "eng.1",
  "group_label": "Overall",
  "mode": "around_team",
  "focus_team": "ARS",
  "rows": [
    {"rank": 1, "team": "ARS", "value": "70", "secondary": "+39", "highlight": true},
    {"rank": 2, "team": "LIV", "value": "68", "secondary": "+38"},
    {"rank": 3, "team": "MCI", "value": "67", "secondary": "+35"}
  ]
}
```

## ESPN Field Mapping

Important standing data usually lives in:

- `.children[]`
- `.standings.entries[]`
- `.team.abbreviation`
- `.stats[]`

Common stat names to extract:

- `points`
- `wins`
- `losses`
- `draws`
- `gamesPlayed`
- `winPercent`
- `gamesBehind`
- `pointDifferential`

## Derived Display Values

By sport:

- Soccer:
  - primary `points`
  - secondary `goal difference`
- NFL:
  - primary `record`
  - secondary `win percent`
- NBA:
  - primary `record`
  - secondary `games behind`
- NHL:
  - primary `points`
  - secondary `record`
- MLB:
  - primary `record`
  - secondary `games behind`

## Rendering Rules

- Highlight selected team using accent color
- Alternate row brightness for scanability
- Keep header very short:
  - `EPL`
  - `NBA E`
  - `NFL NFC`
- Never show more than two metrics per row

## MVP Scope

- One standings feed
- Top-table mode
- Around-team mode
- Group filtering if ESPN response is grouped

## Phase 2

- Relegation or playoff cutoff colors
- Wild card specific screens
- Trend arrows
- Magic number style display for late season

## Risks

- Different sports expose standings stats differently
- Some groups are nested and require sport-specific flattening
- Soccer leagues may include split tables or phase tables

## Implementation Checklist

- Define standings parser abstraction
- Create sport-specific metric extractor
- Add group selector support
- Add page builder
- Add highlight logic for selected team

## Test Cases

- League with single overall table
- League with multiple conferences
- Team not found in standings
- Missing secondary stat
- Late-season tie on primary metric
