#pragma once

typedef void (*AppUpdateReloadCallback)(void);
typedef void (*AppUpdateSwitchCallback)(const char* app_id, unsigned app_id_len);

// Start HTTP app-update server.
// - PUT /api/apps/<app_id>/main.lua
// - PUT /api/apps/<app_id>/manifest.json
// - PUT /api/apps/<app_id>/ui.json
// - POST /api/apps/reload
// - POST /api/apps/switch/<app_id>
// - POST /api/apps/switch?app_id=<app_id>
// - DELETE /api/apps/<app_id>              (uninstall app directory recursively)
// - DELETE /api/apps/<app_id>/<file>       (delete single file)
// - GET /api/firmware
// - POST /api/firmware/ota                 (raw firmware image body)
// - POST /api/system/reboot
// - GET /f.html                            (web system/apps UI)
// - GET /app.html                          (compat alias to /f.html)
//
// The callback is invoked after a successful file update or explicit reload request.
bool AppUpdateServerStart(AppUpdateReloadCallback reload_cb, AppUpdateSwitchCallback switch_cb);
