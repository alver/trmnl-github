# GitHub-Direct Firmware — Fix Backlog

## Phase 1 — Critical Safety Fixes ✅ (in progress)

- [x] **1a. Fix ghost image** ✅ DONE — Loading screen uses `bWait=false` so EPD is still physically
  refreshing when the next `display_show_image()` call arrives. Change to `bWait=true`, OR
  restructure so loading screen is only shown on GPIO wakeup (not timer). Timer wakeup
  should skip straight to download. Both renders counting against the "every 8th = full
  refresh" counter also makes ghosting timing unpredictable.

- [x] **1b. Add `resetDeviceCredentials()`** ✅ DONE — uses local Preferences handle so it works before preferences.begin() (SoftReset path) AND after (portal callback path) — Static function:
  `WifiCaptivePortal.resetSettings()` + `preferences.clear()` + `ESP.restart()`.
  Currently the device has no factory-reset path at all.

- [x] **1c. Wire SoftReset button** ✅ DONE — also wired DoubleClick to advance playlist_index — `case SoftReset:` must call `resetDeviceCredentials()`.
  Currently falls through silently — a 15s+ hold does nothing.

- [x] **1d. Register reset callback before startPortal** ✅ DONE —
  `WifiCaptivePortal.setResetSettingsCallback(resetDeviceCredentials)` must be called
  before `WifiCaptivePortal.startPortal()`. Without it the "Reset" button in the captive
  portal web UI has no effect.

---

## Phase 2 — Reliability Fixes

- [x] **2a. WiFi retry backoff** ✅ DONE — All WiFi failures currently sleep a flat 60s. Implement
  progressive backoff using `PREFERENCES_CONNECT_WIFI_RETRY_COUNT` in NVS:
  retry 1 → 60s, retry 2 → 180s, retry 3 → 300s, default → 900s.
  Reset counter on successful connect.

- [x] **2b. Manifest/image download retry backoff** ✅ DONE — Separate NVS counter for API/download
  failures. Same 60/180/300/900s pattern. Reset counter on successful download.

- [x] **2c. Stream read timeout in `github_client.cpp`** ✅ DONE — The streaming loop
  `while (bytes_read < content_size && stream->connected())` has no timeout guard.
  Add `millis()` check: if no bytes received for >5s, break and return nullptr.

---

## Phase 3 — Correctness Improvements

- [x] **3a. Image format handling** ✅ DONE — `parseBMPHeader()` is called unconditionally before
  `display_show_image()`. If users ever produce PNG images from tooling, they are rejected
  with `BMP_NOT_BMP` before the display driver can detect them. Fix: detect magic bytes
  first (`BM` = BMP, `0x89` = PNG) and only call `parseBMPHeader()` for BMPs; let
  `display_show_image()` handle PNG/JPEG natively.

- [x] **3b. Add `wait_for_serial()` after GPIO wakeup** ✅ DONE — On USB CDC boards with
  `WAIT_FOR_SERIAL` defined, wait up to 2s for the serial port to attach after button
  wakeup. Without this, early log lines after a button press are lost on USB CDC builds.

- [x] **3c. NTP sync after WiFi connect** ✅ DONE — Call `configTime(0, 0, "time.google.com",
  "time.cloudflare.com")` after WiFi connects. Not required (setInsecure skips cert
  validation) but fixes log timestamps and future-proofs against pinned certs. Log only;
  don't block on failure.

---

## Phase 4 — Extensibility

- [x] **4a. DoubleClick → force next screen** ✅ DONE — basic wiring done in Phase 1; Phase 4 fixed % 255 bug, added double_clicked flag to reset API retry counter after preferences.begin(), added prev→next index log — Currently DoubleClick logs "forcing refresh"
  and does nothing different. Wire it to advance (or reset) `playlist_index` in RTC memory
  so the next screen is shown instead of cycling normally. Could also be "restart playlist"
  (reset index to 0).

---

## Issue Reference Table

| # | Issue | Impact | Fix complexity |
|---|---|---|---|
| 1a | Ghost image (bWait=false on loading screen) | High — visual defect every N cycles | Low |
| 1b | Missing `resetDeviceCredentials()` | High — device unrecoverable | Low |
| 1c | SoftReset button does nothing | High — no factory reset path | Trivial |
| 1d | `setResetSettingsCallback` missing | Medium — portal Reset button broken | Trivial |
| 2a | Flat 60s WiFi retry (no backoff) | Medium — battery drain on WiFi loss | Medium |
| 2b | Flat 60s API retry (no backoff) | Medium — battery drain on CDN issues | Medium |
| 2c | Stream read no timeout | Low — rare hang | Low |
| 3a | PNG pre-validation rejects non-BMP | Low — affects PNG users | Low |
| 3b | wait_for_serial after GPIO wakeup | Low — debugging only | Trivial |
| 3c | NTP sync missing | Low — log timestamps wrong | Low |
| 4a | DoubleClick wired to nothing | Low — feature gap | Medium |
