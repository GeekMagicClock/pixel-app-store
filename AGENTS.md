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

## Preflight Checklist (Required)

Before any publish action, agents must verify:

1. No OTA upload command includes `*.zip` or `*.png`.
2. Generated index `zip_url`/`thumbnail_url` target GitHub raw domain.
3. OTA upload step includes only:
   - `apps-index.json`
   - `apps-index.json.gz`

If any check fails, agents must stop and fix scripts before publishing.
