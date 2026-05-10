# Agent Rules For App Store Publishing

These rules are mandatory for any AI agent working in this repository.

## Non-negotiable Publishing Policy

1. App binaries and assets (`*.zip`, `*.png`, thumbnails) MUST be published to GitHub app-store repo only.
2. OTA host (`ota.geekmagic.cc`) MUST store index files only:
   - `apps-index.json`
   - `apps-index.json.gz`
3. Agents MUST NOT upload app package files (`*.zip`) or thumbnails (`*.png`) to OTA host.

## Required URL Policy In Index

1. `zip_url` and `thumbnail_url` in index entries MUST point to GitHub raw URLs.
2. `default_index_url` served to devices can point to OTA index URL.

## Firmware OTA Publishing Policy

1. Firmware binary files (`*.bin`) MUST be published to GitHub app-store repo only (under `firmware/<channel>/`).
2. OTA host (`ota.geekmagic.cc`) MUST store firmware manifest only:
   - `firmware.json`
3. Device OTA checks MUST read manifest from OTA host and then download binary from GitHub URL in manifest.

## Script Usage Policy

1. Prefer using repository scripts that enforce the policy:
   - `scripts/S2_publish_beta.sh`
   - `scripts/publish_app_store_all.sh`
2. If changing publishing scripts, agents must preserve this policy and update this file when behavior changes.

## Version Bump Policy (Required)

1. Any publish action to `beta` or `stable` MUST include a version increase for every app being published.
2. Agents MUST NOT use no-bump publish mode (e.g. `--allow-no-bump`) for official publish actions.
3. If version is unchanged, agents must stop, bump version first, then publish.

## Settings Asset Policy (Required)

1. App delivery payloads (device push packages and published zip artifacts) MUST include `settings.html.gz` only.
2. Raw `settings.html` MUST NOT be uploaded to device install session and MUST NOT be included in published app zip payload.
3. If `settings.html` exists in source tree, agents must ensure matching `settings.html.gz` is regenerated from latest source before push/publish.

## Documentation Asset Policy (Required)

1. Published app zip payloads MUST NOT include documentation files.
2. The following are forbidden in published app zip payloads:
   - `*.md`
   - `*.markdown`
   - any file under `doc/` or `docs/` directories
3. If packaging scripts include such files, agents must fix the scripts before publishing.

## Preflight Checklist (Required)

Before any publish action, agents must verify:

1. No OTA upload command includes `*.zip` or `*.png`.
2. Generated index `zip_url`/`thumbnail_url` target GitHub raw domain.
3. OTA upload step includes only:
   - `apps-index.json`
   - `apps-index.json.gz`
4. Published app zip inspection confirms no forbidden doc assets (`*.md`, `*.markdown`, `doc/`, `docs/`).

If any check fails, agents must stop and fix scripts before publishing.
