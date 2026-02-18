# TRMNL Firmware — Complete Codebase Specification

## Table of Contents

1. [High-Level Architecture](#1-high-level-architecture)
2. [Boot & Execution Flow](#2-boot--execution-flow)
3. [Project Structure](#3-project-structure)
4. [Build System (PlatformIO)](#4-build-system-platformio)
5. [Entry Point — `main.cpp`](#5-entry-point--maincpp)
6. [QA Testing — `qa.cpp`](#6-qa-testing--qacpp)
7. [Business Logic — `bl.cpp`](#7-business-logic--blcpp)
8. [Display Driver — `display.cpp`](#8-display-driver--displaycpp)
9. [API Client Layer](#9-api-client-layer)
10. [WiFi & Captive Portal](#10-wifi--captive-portal)
11. [Persistent Storage](#11-persistent-storage)
12. [Filesystem (SPIFFS)](#12-filesystem-spiffs)
13. [Logging System](#13-logging-system)
14. [Button Handling](#14-button-handling)
15. [Image Processing](#15-image-processing)
16. [Sleep & Power Management](#16-sleep--power-management)
17. [OTA Firmware Updates](#17-ota-firmware-updates)
18. [Special Functions](#18-special-functions)
19. [Multi-Board Support](#19-multi-board-support)
20. [Partition Tables](#20-partition-tables)
21. [Data Types & Enumerations](#21-data-types--enumerations)
22. [Complete File Reference](#22-complete-file-reference)

---

## 1. High-Level Architecture

The TRMNL firmware is an **embedded C++ application** for ESP32 microcontrollers that drives a battery-powered, WiFi-connected e-ink display. The device operates in a **wake-sleep cycle**:

```
┌──────────┐    ┌──────────┐    ┌───────────┐    ┌─────────┐    ┌───────────┐
│  Wake Up  │───▶│  Connect  │───▶│  Fetch    │───▶│ Display │───▶│   Deep    │
│  (Timer/  │    │  WiFi     │    │  API +    │    │ Image   │    │   Sleep   │
│   GPIO)   │    │           │    │  Image    │    │         │    │           │
└──────────┘    └──────────┘    └───────────┘    └─────────┘    └───────────┘
                                                                    │
                                                                    ▼
                                                              (repeat cycle)
```

The firmware does NOT run a persistent loop. Instead:
1. The ESP32 wakes from deep sleep (timer or button press).
2. Everything happens inside `bl_init()` (called from `setup()`).
3. The device connects to WiFi, contacts the TRMNL API, downloads/displays an image.
4. The device enters deep sleep for a configurable duration (default 900 seconds / 15 minutes).
5. `bl_process()` / `loop()` are effectively empty — the device never reaches them (it sleeps or restarts first).

**Key frameworks:** Arduino + ESP-IDF (dual-framework), PlatformIO build system.

---

## 2. Boot & Execution Flow

The complete boot-to-sleep sequence in `bl_init()`:

```
1.  Serial.begin(115200)
2.  pins_init()                    — Configure GPIO for button interrupt
3.  readBatteryVoltage()           — Read ADC before WiFi (WiFi increases noise)
4.  Check wakeup reason:
    ├── GPIO wakeup → read_button_presses()
    │   ├── LongPress   → Reset WiFi credentials
    │   ├── DoubleClick → Set double_click flag (special function)
    │   ├── ShortPress  → Normal refresh
    │   ├── SoftReset   → resetDeviceCredentials() → ESP.restart()
    │   └── NoAction    → Continue normally
    └── Timer/other wakeup → Skip button reading
5.  preferences.begin("data")     — Open NVS namespace
6.  If double_click: read & execute special function from NVS
7.  display_init()                — Initialize e-ink display controller
8.  filesystem_init()             — Mount SPIFFS
9.  If NOT timer wakeup: show TRMNL logo
10. WiFi.mode(WIFI_STA)
11. Connect WiFi:
    ├── Saved credentials → WifiCaptivePortal.autoConnect()
    │   ├── Success → Continue
    │   └── Failure → Show WIFI_FAILED message → wifiErrorDeepSleep()
    └── No saved credentials → WifiCaptivePortal.startPortal()
        ├── User connects → Continue
        └── Timeout → Show WIFI_FAILED → wifiErrorDeepSleep()
12. setClock()                    — Sync NTP time
13. Check for API key + Friendly ID:
    └── Missing → getDeviceCredentials() → performApiSetup() + downloadSetupImage()
14. submitStoredLogs()            — Send any buffered error logs
15. downloadAndShow()             — Main API call + image download + display
16. Handle retry logic (exponential backoff: 15s → 30s → 60s → 900s)
17. submitStoredLogs()            — Final log submission
18. Handle error messages on display
19. display_sleep()               — Put e-ink controller in deep sleep
20. If OTA update needed: checkAndPerformFirmwareUpdate() → ESP.restart()
21. goToSleep()                   — WiFi off, SPIFFS unmount, configure GPIO wakeup, esp_deep_sleep_start()
```

---

## 3. Project Structure

```
original/
├── platformio.ini              — Build configuration, board environments, dependencies
├── sdkconfig.defaults          — ESP-IDF configuration (TLS, WiFi roaming, SNTP)
├── min_spiffs.csv              — Partition table for ESP32-C3 boards (4MB flash)
├── trmnl_x_partitions.csv      — Partition table for TRMNL X (16MB flash)
├── include/
│   ├── config.h                — Firmware version, timing constants, board pin defs
│   ├── pins.h                  — GPIO init function declaration
│   ├── bl.h                    — Business logic API (bl_init, bl_process, logging)
│   ├── display.h               — Display API (init, show_image, show_msg, sleep)
│   ├── types.h                 — Error enums (https_request_err_e)
│   ├── button.h                — Button press detection API
│   ├── filesystem.h            — SPIFFS file operations API
│   ├── qa.h                    — QA test functions
│   ├── preferences_persistence.h — NVS persistence wrapper class
│   ├── nicoclean_8.h           — Font data (small, for OG display)
│   ├── Inter_18.h              — Font data (medium, for TRMNL X)
│   ├── Roboto_Black_24.h       — Font data (large, for QA results)
│   └── api-client/
│       ├── display.h           — /api/display client (fetchApiDisplay)
│       ├── setup.h             — /api/setup client (fetchApiSetup)
│       └── submit_log.h        — /api/log client (submitLogToApi)
├── src/
│   ├── main.cpp                — Arduino entry (setup/loop, calls bl_init/bl_process)
│   ├── bl.cpp                  — Core business logic (~2300 lines)
│   ├── display.cpp             — E-ink display driver (~2000 lines)
│   ├── qa.cpp                  — Factory QA self-test
│   ├── pins.cpp                — GPIO pin initialization
│   ├── button.cpp              — Button press classification logic
│   ├── filesystem.cpp          — SPIFFS file operations implementation
│   ├── app_logger.cpp          — Log routing (serial / store / submit)
│   ├── preferences_persistence.cpp — NVS Preferences wrapper impl
│   └── api-client/
│       ├── display.cpp         — /api/display HTTP client
│       ├── setup.cpp           — /api/setup HTTP client
│       └── submit_log.cpp      — /api/log HTTP client (POST)
├── lib/
│   ├── trmnl/
│   │   ├── include/
│   │   │   ├── api_types.h             — API data structures (requests/responses)
│   │   │   ├── api_response_parsing.h  — JSON response parsers
│   │   │   ├── api_request_serialization.h — Log request serialization
│   │   │   ├── http_client.h           — withHttp() template (HTTP client helper)
│   │   │   ├── special_function.h      — Special function enum & parser
│   │   │   ├── stored_logs.h           — Buffered log storage class
│   │   │   ├── serialize_log.h         — Log JSON serialization
│   │   │   ├── persistence_interface.h — Abstract persistence interface
│   │   │   ├── bmp.h                   — BMP header parsing
│   │   │   ├── png.h                   — PNG error enum wrapper
│   │   │   ├── png_flip.h             — Bitmap vertical flip
│   │   │   ├── string_utils.h         — String formatting utilities
│   │   │   ├── trmnl_log.h            — Logging macros (Log_info, Log_error, etc.)
│   │   │   └── logging_parcers.h      — Wake reason / special function string parsers
│   │   └── src/
│   │       ├── parse_response_api_display.cpp — JSON→ApiDisplayResponse
│   │       ├── parse_response_api_setup.cpp   — JSON→ApiSetupResponse
│   │       ├── serialize_log.cpp              — LogWithDetails→JSON
│   │       ├── serialize_request_api_log.cpp  — Wraps log in {"logs":[...]}
│   │       ├── bmp.cpp                        — BMP header validation
│   │       ├── png_flip.cpp                   — Vertical image flip for BMPs
│   │       ├── special_function.cpp           — String↔enum mapping
│   │       ├── stored_logs.cpp                — NVS-backed log ring buffer
│   │       ├── logging_parsers.cpp            — Wakeup reason / SF string formatters
│   │       ├── string_utils.cpp               — format_message_truncated()
│   │       └── test_logger.cpp                — Test-only logger stub
│   └── wificaptive/
│       └── src/
│           ├── WifiCaptive.h       — WiFi captive portal class
│           ├── WifiCaptive.cpp     — Portal implementation
│           ├── WifiCaptivePage.h   — HTML/CSS for captive portal web UI
│           └── wifi-types.h        — WiFi credential structs
├── scripts/
│   ├── git_version.py          — PlatformIO build script: appends git hash to FW version
│   ├── flash_app.sh            — Flash firmware via esptool
│   ├── flash_merged.sh         — Flash merged firmware binary
│   ├── flatten_config.py       — Configuration flattening utility
│   └── merge_firmware.sh       — Merge firmware partitions
└── test/                       — Unit tests (PlatformIO Unity + ArduinoFake)
```

---

## 4. Build System (PlatformIO)

### Global Settings (`[env]`)
- **Framework:** `arduino, espidf` (dual-framework)
- **Platform:** `espressif32@6.12.0`
- **Static analysis:** ClangTidy with bugprone, portability, and google checks
- **Monitor:** 115200 baud with ESP32 exception decoder
- **Upload speed:** 460800
- **Build type:** debug

### Common Dependencies
| Library | Version | Purpose |
|---------|---------|---------|
| ArduinoJson | 7.4.2 | JSON parsing/serialization |
| PNGdec | ^1.1.6 | PNG image decoding |
| JPEGDEC | ^1.8.4 | JPEG image decoding |
| bb_epaper | ^2.0.5 | E-paper display driver (OG boards) |
| ESPAsyncWebServer | 3.7.7 | Captive portal web server |
| Arduino-Log | (git) | Serial logging library |
| FastEPD | 1.3.0 | Fast e-paper driver (TRMNL X only) |
| ArduinoFake | ^0.4.0 | Arduino API mocking (test only) |

### Board Environments

| Environment | Board | MCU | Display | Key Build Flags |
|-------------|-------|-----|---------|-----------------|
| `trmnl` (default) | esp32-c3-devkitc-02 | ESP32-C3 | 7.5" 2-color | `BOARD_TRMNL`, 80MHz CPU |
| `trmnl_4clr` | esp32-c3-devkitc-02 | ESP32-C3 | 7.5" 4-color (BWYR) | `BOARD_TRMNL_4CLR`, USB CDC |
| `xteink_x4` | esp32-c3-devkitc-02 | ESP32-C3 | 4.26" | `BOARD_XTEINK_X4` |
| `TRMNL_X` | esp32s3_n16r8 | ESP32-S3 | 10.3" (1872x1404) | `BOARD_TRMNL_X`, 240MHz, PSRAM, FastEPD |
| `seeed_xiao_esp32c3` | seeed_xiao_esp32c3 | ESP32-C3 | 7.5" | `BOARD_SEEED_XIAO_ESP32C3` |
| `seeed_xiao_esp32s3` | seeed_xiao_esp32s3 | ESP32-S3 | 7.5" | `BOARD_SEEED_XIAO_ESP32S3` |
| `waveshare-esp32-driver` | esp32dev | ESP32 | 7.5" | `BOARD_WAVESHARE_ESP32_DRIVER` |
| `TRMNL_7inch5_OG_DIY_Kit` | seeed_xiao_esp32s3 | ESP32-S3 | 7.5" | `BOARD_XIAO_EPAPER_DISPLAY` |
| `TRMNL_7inch5_OG_DIY_Kit_3CLR` | seeed_xiao_esp32s3 | ESP32-S3 | 7.5" 3-color | `BOARD_XIAO_EPAPER_DISPLAY_3CLR` |
| `seeed_reTerminal_E1001` | seeed_xiao_esp32s3 | ESP32-S3 | Spectra6 | `BOARD_SEEED_RETERMINAL_E1001` |
| `seeed_reTerminal_E1002` | seeed_xiao_esp32s3 | ESP32-S3 | Spectra6 | `BOARD_SEEED_RETERMINAL_E1002` |
| `local` | (extends trmnl) | ESP32-C3 | 7.5" | USB CDC, `WAIT_FOR_SERIAL`, `DO_NOT_LIGHT_SLEEP` |
| `native` | native | Host PC | — | Test environment, ArduinoFake |
| `native-windows` | native | Windows | — | Test environment for Windows |

### Build Script: `git_version.py`
When `custom_include_git_hash = true`, appends `+<short-hash>` to the firmware version string via `FW_VERSION_SUFFIX`. E.g., `1.7.5+abc1234`.

---

## 5. Entry Point — `main.cpp`

```cpp
void setup() {
    bool testPassed = checkIfAlreadyPassed();  // Check NVS "qa" namespace
    if (!testPassed) {
        startQA();                              // Run factory QA test
    }
    esp_ota_mark_app_valid_cancel_rollback();   // Mark OTA partition as valid
    bl_init();                                  // All business logic runs here
}

void loop() {
    bl_process();  // Empty — device sleeps before reaching this
}
```

The entire firmware lifecycle happens inside `bl_init()`. The device enters deep sleep at the end and never reaches `loop()`.

---

## 6. QA Testing — `qa.cpp`

Factory quality assurance test that runs ONCE on the first boot of each device:

1. **Check if already passed:** Reads `preferencesQA.getBool("testPassed")` from NVS namespace `"qa"`.
2. **Scan for "TRMNL_QA" network:** If not found, marks test as passed and continues to normal boot.
3. **If QA network found:**
   - Displays "Starting QA test" message
   - Measures initial temperature and voltage (1000 ADC samples each)
   - Runs a 7-second CPU + WiFi stress test (`loadCPUAndRadio()`)
   - Measures final temperature and voltage
   - **Pass condition:** Temperature rise < 3°C
   - Displays results on screen (voltage diff, temp diff, PASS/FAIL)
   - Waits for button press to continue
4. **Saves `testPassed = true`** so QA never runs again.

The button (PIN_INTERRUPT) can abort QA at any time via ISR (`onBtnPress`).

---

## 7. Business Logic — `bl.cpp`

This is the central module (~2300 lines). Key sections:

### 7.1 Global State Variables

```cpp
ApiDisplayResult apiDisplayResult;     // Last API response
uint8_t *buffer = nullptr;            // Image download buffer
char filename[1024];                   // Image URL from API
char binUrl[1024];                     // Firmware update URL
char message_buffer[128];              // Status message for display
bool status = false;                   // Need to download new image
bool update_firmware = false;          // OTA update pending
bool reset_firmware = false;           // Device reset requested
bool double_click = false;             // Double-click detected on wake
bool send_log = false;                 // Log submission pending
bool log_retry = false;                // Currently in retry mode
esp_sleep_wakeup_cause_t wakeup_reason;
MSG current_msg = NONE;                // Current message being displayed
SPECIAL_FUNCTION special_function = SF_NONE;
RTC_DATA_ATTR uint8_t need_to_refresh_display = 1;  // Survives deep sleep
Preferences preferences;              // NVS handle
```

### 7.2 `bl_init()` — Main Initialization (see Section 2 for full flow)

### 7.3 `bl_process()` — Empty

```cpp
void bl_process(void) { }
```

### 7.4 `loadApiDisplayInputs()` — Prepare API Request

Gathers all device state into `ApiDisplayInputs`:
- `baseUrl` — from NVS or default `https://trmnl.app`
- `apiKey` — from NVS
- `friendlyId` — from NVS
- `refreshRate` — from NVS (default 900s)
- `macAddress` — `WiFi.macAddress()`
- `batteryVoltage` — measured at boot
- `firmwareVersion` — compile-time constant
- `rssi` — current WiFi signal strength
- `displayWidth`, `displayHeight` — from display driver
- `model` — board-specific string (e.g., "og", "x", "seeed_esp32s3")
- `specialFunction` — if double-click triggered one

### 7.5 `downloadAndShow()` — Core API + Image Pipeline

1. **DNS resolution** of API hostname (up to 5 attempts with 2s delay)
2. **`fetchApiDisplay()`** — GET `/api/display` with device headers
3. **`handleApiDisplayResponse()`** — Process JSON response:
   - `status: 0` — Normal: extract `image_url`, `refresh_rate`, `update_firmware`, `reset_firmware`, `special_function`
   - `status: 202` — Device not registered → sleep 5s and retry
   - `status: 500` — Server error → reset credentials
4. **Filename check** — Compare new filename with stored one (skip download if unchanged)
5. **Image download** via `withHttp()`:
   - Sets timeouts (15s default, or `image_url_timeout` from API)
   - Adds auth headers (`ID`, `Access-Token`) if image is on same server as API
   - Handles HTTP redirects (307/308)
   - Downloads entire response into memory (`https.getString()`)
   - Max image size: 90KB (OG) or 750KB (TRMNL X with PSRAM)
6. **Image format detection:**
   - PNG: magic bytes `0x89504E47`
   - JPEG: magic bytes `0xFFD8`
   - BMP: magic bytes `BM`
7. **File management:**
   - Renames `/current.{bmp,png}` → `/last.{bmp,png}`
   - Saves new image as `/current.{bmp,png}`
8. **Display rendering** via `display_show_image()`
9. **WiFi disconnect** after image download to save power

### 7.6 `handleApiDisplayResponse()` — Response Processing

Handles both normal and special-function responses:

**Normal flow (`special_function == SF_NONE`):**
- Extracts `image_url`, `firmware_url`, `refresh_rate`, `reset_firmware`
- Checks filename against stored one (avoids re-rendering same image)
- Handles `empty_state` filename (plugin not attached yet)
- Updates NVS refresh rate if changed

**Special function flow:**
Each special function (identify, sleep, add_wifi, restart_playlist, rewind, send_to_me, guest_mode) has its own handling — see Section 18.

### 7.7 `performApiSetup()` — First-Time Device Registration

Called when API key or Friendly ID is missing:
1. `fetchApiSetup()` — GET `/api/setup` with MAC address + firmware version
2. Response handling:
   - `200` — Success: saves `api_key` and `friendly_id` to NVS, extracts setup image URL
   - `404` — MAC not registered: shows registration message, sleeps
   - Other — Error: shows error message

### 7.8 `downloadSetupImage()` — Download Initial Setup Image

Downloads the BMP image returned in the `/api/setup` response (always DISPLAY_BMP_IMAGE_SIZE = 48062 bytes). Saves it to `/logo.bmp` for use as the device logo.

### 7.9 `checkAndPerformFirmwareUpdate()` — OTA Updates

1. Downloads `.bin` firmware from `firmware_url`
2. Uses ESP32 OTA API (`Update.begin()`, `Update.writeStream()`, `Update.end()`)
3. Shows FW_UPDATE / FW_UPDATE_SUCCESS / FW_UPDATE_FAILED messages
4. Device restarts after update

### 7.10 `goToSleep()` — Deep Sleep Entry

1. Submit any remaining logs
2. WiFi disconnect + WiFi off
3. SPIFFS unmount
4. Read sleep duration from NVS (default 900 seconds)
5. Save current timestamp to NVS for `time_since_sleep` calculation
6. Configure timer wakeup: `esp_sleep_enable_timer_wakeup()`
7. Configure GPIO wakeup (board-specific):
   - ESP32: `esp_sleep_enable_ext1_wakeup()` with bitmask
   - ESP32-C3: `esp_deep_sleep_enable_gpio_wakeup()`
   - ESP32-S3: `esp_sleep_enable_ext0_wakeup()`
8. `esp_deep_sleep_start()`

### 7.11 `wifiErrorDeepSleep()` — WiFi Retry Backoff

Progressive retry with increasing sleep times:
- Retry 1: 60 seconds
- Retry 2: 180 seconds (3 min)
- Retry 3: 300 seconds (5 min)
- After 3: 900 seconds (15 min, normal interval)

### 7.12 API Retry Backoff

When API calls fail, exponential backoff:
- Retry 1: 15 seconds
- Retry 2: 30 seconds
- Retry 3: 60 seconds
- After 3: 900 seconds (normal interval)

### 7.13 Logging Functions

- `logWithAction()` — Creates a `LogWithDetails` struct with device status, serializes to JSON, then either stores locally or submits to API
- `submitLogString()` — POSTs log JSON to `/api/log`
- `storeLogString()` — Saves log to NVS ring buffer
- `submitStoredLogs()` — Gathers all stored logs and POSTs them

### 7.14 Helper Functions

- `setClock()` — NTP sync using configurable server (default `time.google.com`, fallback `time.cloudflare.com`)
- `readBatteryVoltage()` — 8-sample ADC average, multiplied by 2 (voltage divider). Returns `4.2f` if `FAKE_BATTERY_VOLTAGE` defined.
- `writeSpecialFunction()` — Saves special function enum to NVS
- `saveCurrentFileName()` — Saves current image filename to NVS
- `checkCurrentFileName()` — Compares filename with stored one
- `storedLogoOrDefault()` — Tries to load branded logo from top of flash memory; falls back to compiled-in logo
- `showMessageWithLogo()` — Display a status message overlaid on the logo image

---

## 8. Display Driver — `display.cpp`

### 8.1 Display Library Selection

The display library is selected at compile time:
- **`bb_epaper` (BBEPAPER)** — Used by all boards EXCEPT TRMNL X
- **`FastEPD` (FASTEPD)** — Used only by TRMNL X

### 8.2 Display Profiles

For `bb_epaper` boards, display profiles control e-ink waveform selection:

```cpp
const DISPLAY_PROFILE dpList[4] = {
    {EP75_800x480, EP75_800x480_4GRAY},           // default
    {EP75_800x480_GEN2, EP75_800x480_4GRAY_GEN2}, // profile "a" — fast + 4-gray
    {EP75_800x480, EP75_800x480_4GRAY_V2},         // profile "b" — darker grays
};
```

The profile is stored in NVS (`temp_profile`) and can be changed by the server.

### 8.3 Key Display Functions

#### `display_init()`
Initializes the e-ink display controller:
- Reads temperature profile from NVS
- For BB_EPAPER: sets panel type, initializes SPI I/O via `bbep.initIO()`
- For FastEPD: `bbep.initPanel(BB_PANEL_EPDIY_V7_16)` with 1872x1404 resolution

#### `display_show_image(uint8_t *image_buffer, int data_size, bool bWait)`
Main image rendering function. Detects image format and delegates:

1. **PNG detection:** Magic bytes `0x89504E47` → `png_to_epd()`
2. **JPEG detection:** Magic bytes `0xFFD8` → `jpeg_to_epd()`
3. **G5 compressed:** `BB_BITMAP_MARKER` → `bbep.loadG5Image()`
4. **Raw BMP:** `flip_image()` + `bbep.setBuffer()` (skip 62-byte header)

Refresh mode selection:
- 1-bpp images → `REFRESH_PARTIAL` (fast, no flicker)
- 2-bpp (4-gray) images → `REFRESH_FULL` (slow, full waveform)
- Every 8th partial → forced `REFRESH_FULL` (prevents ghosting)
- Refresh rate ≥ 30 min → `REFRESH_FAST` (middle ground)
- Multi-color displays → always `REFRESH_FULL`
- Non-waiting (loading screen) → `REFRESH_PARTIAL`

#### `png_to_epd(const uint8_t *pPNG, int iDataSize)`
PNG decoding pipeline:
1. Opens PNG from RAM buffer
2. Checks dimensions match display (allows rotation for portrait images)
3. For 1-bpp or 2-bpp with only 2 colors → uses partial refresh
4. For 4-gray → decodes two planes (PLANE_0 and PLANE_1) separately
5. Callback `png_draw()` writes each scanline directly to e-paper framebuffer

#### `jpeg_to_epd(const uint8_t *pJPEG, int iDataSize)`
JPEG decoding:
- For BB_EPAPER: decodes as 1-bit dithered
- For FastEPD: decodes as 4-bit dithered
- Uses `jpeg_draw()` callback to write MCU blocks to framebuffer

#### `display_show_msg(uint8_t *image_buffer, MSG message_type, ...)`
Shows status/error messages overlaid on the logo:
- Loads logo image into framebuffer
- Draws text using `bbep.setCursor()`, `bbep.print()`
- Supports center-aligned multiline text via `Paint_DrawMultilineText()`
- Different message types: WiFi connect, WiFi failed, API errors, FW update, QA, etc.
- Fonts: `nicoclean_8` (OG), `Inter_18` (TRMNL X), `Roboto_Black_24` (QA results)
- Some messages include QR codes (G5 compressed, stored in `wifi_connect_qr.h`, `wifi_failed_qr.h`)

#### `display_read_file(const char *filename, int *file_size)`
Reads an image file from SPIFFS into a malloc'd buffer. Returns pointer and size.

#### `display_sleep(void)` / `display_sleep(uint32_t u32Millis)`
- No-arg version: puts the e-ink controller into deep sleep
- With millis: light sleep for power-efficient delays (unless `DO_NOT_LIGHT_SLEEP` is defined)

### 8.4 Image Color Processing

#### `ReduceBpp()` — Bit depth reduction
Converts multi-bit images to 1-bpp or 2-bpp:
- Handles truecolor (24/32-bit), indexed palette, grayscale
- For 1-bpp: threshold at bit 7 (128)
- For 2-bpp: maps to 4 gray levels

#### Color Matching (multi-color displays)
- `GetBWRPixel()` — Maps RGB to Black/White/Red (3-color displays)
- `GetBWYRPixel()` — Maps RGB to Black/White/Yellow/Red (4-color displays)
- `GetSpectraPixel()` — Maps RGB to 6 Spectra6 colors via pre-computed palette lookup

#### `png_count_colors()` — Color counting
Decodes a 2-bpp PNG and counts unique colors (2-4). If only 2 colors are present, the image can use partial (non-flickering) refresh instead of full.

---

## 9. API Client Layer

### 9.1 HTTP Client Helper — `withHttp()`

Template function in `http_client.h` that manages HTTP client lifecycle:

```cpp
template <typename Callback, typename ReturnType>
ReturnType withHttp(const String &url, Callback callback)
```

1. Detects HTTPS vs HTTP from URL
2. Creates `WiFiClientSecure` (with `setInsecure()` — no cert validation) or `WiFiClient`
3. Initializes `HTTPClient` with `begin(*client, url)`
4. Calls the callback with `HTTPClient*` and error code
5. Cleans up: `https.end()`, `client->stop()`, `delete client`

### 9.2 `/api/setup` — Device Registration

**File:** `src/api-client/setup.cpp`

**Request:** `GET {baseUrl}/api/setup`

**Headers:**
| Header | Value |
|--------|-------|
| ID | WiFi MAC address |
| Content-Type | application/json |
| FW-Version | Firmware version string |
| Model | Device model string |

**Response JSON:**
```json
{
    "status": 200,
    "api_key": "...",
    "friendly_id": "ABCDEF",
    "image_url": "https://...",
    "message": "..."
}
```

**Status codes:**
- `200` — Success, credentials returned
- `404` — MAC address not registered on server

### 9.3 `/api/display` — Content Fetch

**File:** `src/api-client/display.cpp`

**Request:** `GET {baseUrl}/api/display`

**Headers:**
| Header | Value |
|--------|-------|
| ID | WiFi MAC address |
| Content-Type | application/json |
| Access-Token | API key |
| Refresh-Rate | Current refresh interval (seconds) |
| Battery-Voltage | Battery voltage (float) |
| FW-Version | Firmware version string |
| Model | Device model string |
| RSSI | WiFi signal strength (dBm) |
| temperature-profile | "true" |
| Width | Display width (pixels) |
| Height | Display height (pixels) |
| special_function | "true" (only if special function active) |

**Response JSON:**
```json
{
    "status": 0,
    "image_url": "https://...",
    "image_url_timeout": 30,
    "filename": "unique-content-id",
    "update_firmware": false,
    "maximum_compatibility": false,
    "firmware_url": "",
    "refresh_rate": 900,
    "temperature_profile": "default",
    "reset_firmware": false,
    "special_function": "none",
    "action": ""
}
```

**Status codes in JSON (not HTTP status):**
- `0` — Success, new content available
- `202` — Device not yet registered/paired on server
- `500` — Server error, device should reset

### 9.4 `/api/log` — Log Submission

**File:** `src/api-client/submit_log.cpp`

**Request:** `POST {baseUrl}/api/log`

**Headers:**
| Header | Value |
|--------|-------|
| ID | WiFi MAC address |
| Accept | application/json, */* |
| Access-Token | API key |
| Content-Type | application/json |

**Body:**
```json
{
    "logs": [
        {
            "created_at": 1234567890,
            "id": 42,
            "message": "Error message",
            "source_line": 123,
            "source_path": "/src/bl.cpp",
            "wifi_signal": -65,
            "wifi_status": "WL_CONNECTED",
            "refresh_rate": 900,
            "sleep_duration": 905,
            "firmware_version": "1.7.5",
            "special_function": "none",
            "battery_voltage": 4.1,
            "wake_reason": "timer",
            "free_heap_size": 45000,
            "max_alloc_size": 30000,
            "retry": 2
        }
    ]
}
```

---

## 10. WiFi & Captive Portal

### 10.1 WifiCaptive Class

**File:** `lib/wificaptive/src/WifiCaptive.h`

The `WifiCaptivePortal` singleton manages WiFi connections:

**Key methods:**
- `isSaved()` — Returns `true` if any WiFi credentials are stored in NVS
- `autoConnect()` — Scans networks, matches against saved credentials (up to 5), connects to the one with best RSSI
- `startPortal()` — Starts a captive portal (AP mode with DNS server):
  - Creates access point named "TRMNL" (open, no password)
  - Runs `ESPAsyncWebServer` with DNS redirect
  - Shows web UI for WiFi SSID/password entry
  - Supports WPA2-Enterprise (username/identity/password)
  - Supports static IP configuration
  - Returns `true` on successful connection, `false` on timeout
- `resetSettings()` — Clears all saved WiFi credentials from NVS
- `connect(WifiCredentials)` — Directly connect to a specific network

**Credential storage:**
- Up to `WIFI_MAX_SAVED_CREDS` (5) WiFi networks saved
- Each credential stored as NVS keys: `wifi_N_ssid`, `wifi_N_pswd`, `wifi_N_ent`, etc.
- Last used index stored as `wifi_last_index`
- Connection timeout: 15 seconds
- Max connection attempts per network: 3

### 10.2 WiFi Connection Flow

```
isSaved()?
├── YES → autoConnect()
│         ├── Scan for networks
│         ├── Match against saved credentials
│         ├── Sort by signal strength
│         ├── Try each match (up to 3 attempts)
│         └── Return success/failure
└── NO  → Show "Connect to TRMNL WiFi" message
          → startPortal()
          ├── Create AP "TRMNL"
          ├── Start DNS + web server
          ├── User enters credentials via phone/browser
          ├── Try connection
          └── Return success/failure
```

---

## 11. Persistent Storage

### 11.1 NVS (Non-Volatile Storage) via Preferences

The firmware uses ESP32's NVS flash partition via the `Preferences` library.

**Namespace:** `"data"` (main application data)

| Key | Type | Default | Purpose |
|-----|------|---------|---------|
| `api_key` | String | `""` | Device API key (from `/api/setup`) |
| `friendly_id` | String | `""` | Device friendly ID (from `/api/setup`) |
| `api_url` | String | `"https://trmnl.app"` | API base URL |
| `refresh_rate` | UInt | 900 | Sleep duration in seconds |
| `temp_profile` | UInt | 0 | Display temperature profile (0-3) |
| `log_*` | String | — | Buffered log entries (ring buffer) |
| `log_head` | UInt | — | Log ring buffer head pointer |
| `log_id` | UInt | 1 | Auto-incrementing log ID |
| `plugin` | Bool | false | Whether a plugin is attached |
| `sf` | UInt | 0 | Special function (enum value) |
| `filename` | String | `""` | Currently displayed image filename |
| `last_sleep` | UInt | 0 | Timestamp of last sleep (for duration calc) |
| `retry_count` | Int | 1 | API connection retry counter |
| `wifi_retry` | Int | 1 | WiFi connection retry counter |

**Namespace:** `"qa"` (QA test data)

| Key | Type | Purpose |
|-----|------|---------|
| `testPassed` | Bool | Whether QA test has been completed |

**Namespace:** `"wifi"` (managed by WifiCaptive)

| Key pattern | Type | Purpose |
|-------------|------|---------|
| `wifi_N_ssid` | String | WiFi SSID (N = 0-4) |
| `wifi_N_pswd` | String | WiFi password |
| `wifi_N_ent` | String | Enterprise auth flag |
| `wifi_N_username` | String | Enterprise username |
| `wifi_N_identity` | String | Enterprise identity |
| `wifi_N_sip` | String | Static IP |
| `wifi_N_sgw` | String | Static gateway |
| `wifi_N_ssn` | String | Static subnet |
| `wifi_N_dns1` | String | Static DNS 1 |
| `wifi_N_dns2` | String | Static DNS 2 |
| `wifi_N_usip` | String | Use static IP flag |
| `wifi_last_index` | Int | Last connected network index |

### 11.2 Persistence Interface

Abstract `Persistence` class (`persistence_interface.h`) provides testable interface:
- `readString()`, `writeString()`, `readUint()`, `writeUint()`, `readBool()`, `writeBool()`, `readUChar()`, `writeUChar()`
- `recordExists()`, `clear()`, `remove()`

`PreferencesPersistence` implements this with ESP32 `Preferences`.

### 11.3 StoredLogs — Log Ring Buffer

`StoredLogs` class manages buffered logs in NVS:
- Split into "old" and "new" slots (5 each, total 10 from `LOG_MAX_NOTES_NUMBER`)
- `store_log()` — Writes log JSON string to next NVS slot
- `gather_stored_logs()` — Reads all stored logs into comma-separated string
- `clear_stored_logs()` — Removes all log keys from NVS
- Tracks overwrite count for diagnostics

---

## 12. Filesystem (SPIFFS)

**File:** `src/filesystem.cpp`

SPIFFS (SPI Flash File System) is used for image caching.

### Functions:
| Function | Purpose |
|----------|---------|
| `filesystem_init()` | Mount SPIFFS (with auto-format on first use) |
| `filesystem_deinit()` | Unmount SPIFFS |
| `filesystem_read_from_file()` | Read file into buffer |
| `filesystem_write_to_file()` | Write buffer to file (with chunk writing) |
| `filesystem_file_exists()` | Check if file exists |
| `filesystem_file_delete()` | Delete a file |
| `filesystem_file_rename()` | Rename a file |
| `list_files()` | Log all files and their sizes |

### File Layout on SPIFFS:
| File | Purpose |
|------|---------|
| `/current.bmp` or `/current.png` | Currently displayed image |
| `/last.bmp` or `/last.png` | Previously displayed image (for rewind) |
| `/logo.bmp` | Device setup logo (from `/api/setup`) |

### Write Error Handling:
If a write fails mid-chunk, the entire SPIFFS is formatted (`SPIFFS.format()`) and the partial write is abandoned.

---

## 13. Logging System

### 13.1 Architecture

Three-tier logging system:

```
Log_info("message")              Log_error_submit("message")
        │                                │
        ▼                                ▼
    log_impl()                       log_impl()
        │                                │
        ├── Serial.println()             ├── Serial.println()
        │                                │
        ├── If level >= ERROR:           ├── logWithAction(SUBMIT_OR_STORE)
        │   logWithAction(STORE)         │   ├── Try submitLogString()
        │   └── storedLogs.store_log()   │   │   └── POST /api/log
        │                                │   └── If fail: storeLogString()
        └── (end)                        │       └── storedLogs.store_log()
                                         └── (end)
```

### 13.2 Log Macros (`trmnl_log.h`)

| Macro | Level | Mode |
|-------|-------|------|
| `Log_verbose(fmt, ...)` | VERBOSE | Store only (serial + NVS if ERROR+) |
| `Log_info(fmt, ...)` | INFO | Store only |
| `Log_error(fmt, ...)` | ERROR | Store only |
| `Log_fatal(fmt, ...)` | FATAL | Store only |
| `Log_verbose_submit(fmt, ...)` | VERBOSE | Submit or store |
| `Log_info_submit(fmt, ...)` | INFO | Submit or store |
| `Log_error_submit(fmt, ...)` | ERROR | Submit or store (immediate API call) |
| `Log_fatal_submit(fmt, ...)` | FATAL | Submit or store |
| `Log_info_serial(fmt, ...)` | INFO | Serial only (no storage) |

### 13.3 Log Serialization

Each log entry is serialized to JSON with `DeviceStatusStamp`:
- WiFi RSSI, WiFi status
- Refresh rate, sleep duration
- Firmware version, special function
- Battery voltage, wakeup reason
- Free heap, max alloc size
- Source file, line number, timestamp
- Log ID (auto-incrementing)

---

## 14. Button Handling

**File:** `src/button.cpp`

The device has a single physical button (`PIN_INTERRUPT`) that serves multiple functions based on press duration and pattern:

### Press Classification:
| Duration | Result | Action |
|----------|--------|--------|
| < 50ms | NoAction | Ignored (debounce) |
| 50ms - 1000ms (single) | ShortPress | Normal content refresh |
| 50ms - 1000ms (double) | DoubleClick | Execute special function |
| 1000ms - 5000ms | DoubleClick* | Treated as long-ish press |
| 5000ms - 15000ms | LongPress | Reset WiFi credentials |
| > 15000ms | SoftReset | Full device reset (credentials + WiFi) |

*Note: The code classifies presses >1s as DoubleClick, which is a naming inconsistency. It's actually a "medium press."

### Detection Algorithm:
1. On GPIO wakeup, `read_button_presses()` is called
2. Waits for button release, measuring press duration
3. If short (<1s), waits 800ms for a second press
4. Second press within 800ms → DoubleClick
5. No second press → ShortPress

### Button Timing Constants:
```cpp
#define BUTTON_HOLD_TIME 5000           // Long press threshold
#define BUTTON_MEDIUM_HOLD_TIME 1000    // Medium press threshold
#define BUTTON_SOFT_RESET_TIME 15000    // Soft reset threshold
#define BUTTON_DOUBLE_CLICK_WINDOW 800  // Double-click window
```

---

## 15. Image Processing

### 15.1 Supported Formats

| Format | Detection | Max Size | Notes |
|--------|-----------|----------|-------|
| PNG | `0x89504E47` magic | 90KB (OG) / 750KB (X) | Primary format, 1-8 bpp |
| JPEG | `0xFFD8` magic | Same | Dithered to 1-bit or 4-bit |
| BMP | `BM` header | 48062 bytes exactly | 800x480, 1-bpp only |
| G5 | `BB_BITMAP_MARKER` | — | bb_epaper compressed format |

### 15.2 PNG Processing Pipeline

1. `png_to_epd()` opens PNG from RAM
2. Checks dimensions (must match display or be rotated 90°)
3. Determines bit depth and color count
4. For 1-bpp or 2-color images:
   - Uses `REFRESH_PARTIAL` (non-flickering)
   - Writes to PLANE_0
   - For non-default temp profiles: also writes inverted to PLANE_1
5. For 4-gray images:
   - Uses `REFRESH_FULL`
   - Decodes twice: once for PLANE_0 (low bits), once for PLANE_1 (high bits)
6. Special decoders for multi-color:
   - `png_draw_4clr()` — 4-color (BWYR) mapping
   - `png_draw_6clr()` — 6-color (Spectra6) mapping

### 15.3 BMP Processing

`parseBMPHeader()` validates:
- File signature: `BM`
- Dimensions: exactly 800x480
- Bit depth: exactly 1-bpp
- Image data size: exactly 48000 bytes
- Color table: exactly 2 entries
- Checks color scheme (standard vs. reversed)

BMP images are stored bottom-up; `flip_image()` corrects this before display.

---

## 16. Sleep & Power Management

### 16.1 Deep Sleep

```cpp
esp_sleep_enable_timer_wakeup(time_to_sleep * 1000000ULL);  // microseconds
esp_deep_sleep_start();
```

During deep sleep:
- All RAM is lost (except `RTC_DATA_ATTR` variables like `need_to_refresh_display`)
- WiFi is off
- Only RTC peripherals remain powered
- GPIO can trigger wakeup

### 16.2 Light Sleep

Used during display operations (while waiting for e-paper refresh):
```cpp
void display_sleep(uint32_t u32Millis) {
    esp_sleep_enable_timer_wakeup(u32Millis * 1000L);
    esp_light_sleep_start();
}
```
More power-efficient than `delay()`. Can be disabled with `DO_NOT_LIGHT_SLEEP` flag.

### 16.3 Wake Sources

| Source | Code | Trigger |
|--------|------|---------|
| Timer | `ESP_SLEEP_WAKEUP_TIMER` | Sleep duration expired |
| GPIO (ESP32-C3) | `ESP_SLEEP_WAKEUP_GPIO` | Button press |
| EXT0 (ESP32-S3) | `ESP_SLEEP_WAKEUP_EXT0` | Button press |
| EXT1 (ESP32) | `ESP_SLEEP_WAKEUP_EXT1` | Button press |
| Undefined | `ESP_SLEEP_WAKEUP_UNDEFINED` | First boot / reset |

### 16.4 Sleep Duration Configuration

- Default: 900 seconds (15 minutes)
- Set by server via `refresh_rate` in `/api/display` response
- Overridden during retry backoff (15s → 30s → 60s → normal)
- Plugin not attached: 5 seconds
- Device not registered: 5 seconds

### 16.5 Battery Voltage Reading

Read BEFORE WiFi initialization (WiFi increases ADC noise):
```cpp
analogRead(PIN_BATTERY);              // Initialize ADC
for (8 samples) adc += analogReadMilliVolts(PIN_BATTERY);
voltage = (adc / 8) * 2 / 1000.0;    // Voltage divider compensation
```

Boards without battery ADC (`FAKE_BATTERY_VOLTAGE`): returns constant 4.2V.

---

## 17. OTA Firmware Updates

### Flow:
1. Server sets `update_firmware: true` and `firmware_url: "https://..."` in `/api/display` response
2. After image display, `checkAndPerformFirmwareUpdate()` is called
3. Downloads `.bin` file via HTTPS
4. Uses ESP32 `Update` library:
   ```cpp
   Update.begin(contentLength);
   Update.writeStream(https->getStream());
   Update.end(true);
   ```
5. On next boot, `esp_ota_mark_app_valid_cancel_rollback()` confirms the new firmware

### OTA Safety:
- Two OTA partitions (`app0`, `app1`) enable rollback
- `esp_ota_mark_app_valid_cancel_rollback()` runs in `setup()` after QA passes
- If new firmware crashes before reaching this call, ESP32 rolls back automatically

---

## 18. Special Functions

Special functions are triggered by double-clicking the button. The function to execute is stored in NVS and communicated to the server on the next API call.

| Function | Enum | Server Action | Device Behavior |
|----------|------|---------------|-----------------|
| None | `SF_NONE` (0) | — | Normal operation |
| Identify | `SF_IDENTIFY` (1) | `"identify"` | Downloads and shows identify image |
| Sleep | `SF_SLEEP` (2) | `"sleep"` | Updates refresh rate, goes to sleep |
| Add WiFi | `SF_ADD_WIFI` (3) | `"add_wifi"` | Opens captive portal to add another network |
| Restart Playlist | `SF_RESTART_PLAYLIST` (4) | `"restart_playlist"` | Downloads fresh content |
| Rewind | `SF_REWIND` (5) | `"rewind"` | Shows previous image from `/last.{bmp,png}` |
| Send to Me | `SF_SEND_TO_ME` (6) | `"send_to_me"` | Re-displays current image from `/current.{bmp,png}` |
| Guest Mode | `SF_GUEST_MODE` (7) | `"guest_mode"` | Downloads guest mode content with custom refresh rate |

The server controls which special function is assigned via the `special_function` field in the `/api/display` response.

---

## 19. Multi-Board Support

### 19.1 Board-Specific Configuration (`config.h`)

| Board | `DEVICE_MODEL` | `PIN_INTERRUPT` | `PIN_BATTERY` | Notes |
|-------|-----------------|-----------------|---------------|-------|
| TRMNL OG | `"og"` | 2 | 3 | Default board |
| TRMNL 4CLR | `"og"` | 2 | 3 | 4-color e-ink |
| XTEINK X4 | `"XTEINK_X4"` | 3 | 0 | 4.26" display |
| TRMNL X | `"x"` | 0 | 3 | 10.3" display, PSRAM |
| Waveshare | `"waveshare"` | 33 | 3 | `FAKE_BATTERY_VOLTAGE` |
| Seeed XIAO C3 | `"seeed_esp32c3"` | 9 (boot) | 3 | `FAKE_BATTERY_VOLTAGE`, special WiFi reset |
| Seeed XIAO S3 | `"seeed_esp32s3"` | 0 (boot) | 3 | `FAKE_BATTERY_VOLTAGE` |
| DIY Kit | `"xiao_epaper_display"` | 5 (KEY3) | 1 | Battery switch on pin 6 |
| reTerminal E1001 | `"reTerminal E1001"` | 3 (green) | 1 | Battery switch on pin 21 |
| reTerminal E1002 | `"reTerminal E1002"` | 3 (green) | 1 | Spectra6 6-color |

### 19.2 Display Panel Types

| Board Define | bb_epaper Panel Type | Resolution | Colors |
|--------------|---------------------|------------|--------|
| Default (TRMNL OG) | `EP75_800x480` | 800x480 | 2 (B/W) or 4-gray |
| `BOARD_XTEINK_X4` | `EP426_800x480` | 800x480 | 2 or 4-gray |
| `BOARD_TRMNL_4CLR` | `EP75YR_800x480` | 800x480 | 4 (B/W/Y/R) |
| `BOARD_XIAO_EPAPER_DISPLAY_3CLR` | `EP75R_800x480` | 800x480 | 3 (B/W/R) |
| `BOARD_SEEED_RETERMINAL_E1002` | `EP73_SPECTRA_800x480` | 800x480 | 6 (Spectra6) |
| `BOARD_TRMNL_X` | FastEPD `BB_PANEL_EPDIY_V7_16` | 1872x1404 | 16 gray |

### 19.3 GPIO Wakeup Configuration (Board-Specific)

```cpp
#if CONFIG_IDF_TARGET_ESP32
  esp_sleep_enable_ext1_wakeup(bitmask, ESP_EXT1_WAKEUP_ALL_LOW);
#elif CONFIG_IDF_TARGET_ESP32C3
  esp_deep_sleep_enable_gpio_wakeup(1 << PIN_INTERRUPT, ESP_GPIO_WAKEUP_GPIO_LOW);
#elif CONFIG_IDF_TARGET_ESP32S3
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_INTERRUPT, 0);
#endif
```

### 19.4 Seeed XIAO ESP32-C3 Special Handling

This board's boot button (pin 9) cannot be used as RTC wakeup source. Instead:
- Use the reset button to wake
- Special WiFi reset procedure: press reset, then press boot button within 2 seconds and hold for 5 seconds

---

## 20. Partition Tables

### 20.1 `min_spiffs.csv` — ESP32-C3 Boards (4MB Flash)

| Name | Type | SubType | Offset | Size |
|------|------|---------|--------|------|
| nvs | data | nvs | 0x9000 | 20KB |
| otadata | data | ota | 0xE000 | 8KB |
| app0 | app | ota_0 | 0x10000 | 1856KB |
| app1 | app | ota_1 | 0x1E0000 | 1856KB |
| spiffs | data | spiffs | 0x3B0000 | 256KB |
| coredump | data | coredump | 0x3F0000 | 64KB |

**Total:** 4MB. Two OTA slots for safe updates. 256KB SPIFFS for image cache.

### 20.2 `trmnl_x_partitions.csv` — TRMNL X (16MB Flash)

| Name | Type | SubType | Offset | Size |
|------|------|---------|--------|------|
| nvs | data | nvs | 0xE000 | 20KB |
| otadata | data | ota | 0x13000 | 8KB |
| app0 | app | ota_0 | 0x20000 | 3072KB |
| app1 | app | ota_1 | 0x320000 | 3072KB |
| spiffs | data | spiffs | 0x620000 | ~9.7MB |

**Total:** 16MB. Larger app partitions and ~10MB SPIFFS for bigger images.

---

## 21. Data Types & Enumerations

### 21.1 Error Codes (`types.h`)

```cpp
enum https_request_err_e {
    HTTPS_NO_ERR,               // No error (initial state)
    HTTPS_RESET,                // Server requested device reset
    HTTPS_NO_REGISTER,          // Device not registered (202)
    HTTPS_SUCCESS,              // Successful image download
    HTTPS_CLIENT_FAILED,        // HTTP client creation failed
    HTTPS_REQUEST_FAILED,       // HTTP request failed
    HTTPS_UNABLE_TO_CONNECT,    // Cannot connect to server
    HTTPS_RESPONSE_CODE_INVALID,// Unexpected HTTP response code
    HTTPS_JSON_PARSING_ERR,     // JSON deserialization failed
    HTTPS_WRONG_IMAGE_SIZE,     // Image size invalid
    HTTPS_WRONG_IMAGE_FORMAT,   // Image format invalid
    HTTPS_IMAGE_FILE_TOO_BIG,   // Image exceeds MAX_IMAGE_SIZE
    HTTPS_PLUGIN_NOT_ATTACHED,  // No plugin assigned to device
    HTTPS_BAD_CLIENT,           // Bad HTTP client state
    HTTPS_OUT_OF_MEMORY         // malloc failed for image buffer
};
```

### 21.2 Display Messages (`display.h`)

```cpp
enum MSG {
    NONE, FRIENDLY_ID, WIFI_CONNECT, WIFI_FAILED, WIFI_WEAK,
    WIFI_INTERNAL_ERROR, API_ERROR, API_REQUEST_FAILED, API_SIZE_ERROR,
    API_UNABLE_TO_CONNECT, API_SETUP_FAILED, API_IMAGE_DOWNLOAD_ERROR,
    API_FIRMWARE_UPDATE_ERROR, FW_UPDATE, QA_START, FW_UPDATE_FAILED,
    FW_UPDATE_SUCCESS, MSG_FORMAT_ERROR, MSG_TOO_BIG, MAC_NOT_REGISTERED,
    TEST, FILL_WHITE
};
```

### 21.3 Button Results (`button.h`)

```cpp
enum ButtonPressResult {
    LongPress,    // 5-15 seconds
    DoubleClick,  // Two presses within 800ms OR medium press (1-5s)
    ShortPress,   // Single short press
    SoftReset,    // >15 seconds
    NoAction      // Too short / debounce
};
```

### 21.4 Special Functions (`special_function.h`)

```cpp
enum SPECIAL_FUNCTION {
    SF_NONE, SF_IDENTIFY, SF_SLEEP, SF_ADD_WIFI,
    SF_RESTART_PLAYLIST, SF_REWIND, SF_SEND_TO_ME, SF_GUEST_MODE
};
```

### 21.5 API Response Structures (`api_types.h`)

```cpp
struct ApiDisplayResponse {
    ApiDisplayOutcome outcome;
    String error_detail;
    uint64_t status;           // 0=OK, 202=not registered, 500=error
    String image_url;
    uint32_t image_url_timeout;
    String filename;
    bool update_firmware;
    bool maximum_compatibility;
    String firmware_url;
    uint64_t refresh_rate;
    uint32_t temp_profile;
    bool reset_firmware;
    SPECIAL_FUNCTION special_function;
    String action;
};

struct ApiSetupResponse {
    ApiSetupOutcome outcome;
    uint16_t status;           // HTTP status code
    String api_key;
    String friendly_id;
    String image_url;
    String message;
};
```

### 21.6 Brand Data (`display.h`)

```cpp
typedef struct theBrand {
    char name[16];           // Brand name
    char api_url[128];       // Custom API URL
    uint8_t u8Images[3952];  // G5-compressed logo images
} BRAND;
```

Stored at the top 4KB of flash memory. If present, overrides the default compiled-in logos.

---

## 22. Complete File Reference

### Constants Summary

| Constant | Value | Defined In |
|----------|-------|------------|
| `FW_VERSION_STRING` | `"1.7.5"` | `config.h` |
| `SLEEP_TIME_TO_SLEEP` | 900 (15 min) | `config.h` |
| `SLEEP_TIME_WHILE_NOT_CONNECTED` | 5 | `config.h` |
| `SLEEP_TIME_WHILE_PLUGIN_NOT_ATTACHED` | 5 | `config.h` |
| `DEFAULT_IMAGE_SIZE` | 48000 | `config.h` |
| `DISPLAY_BMP_IMAGE_SIZE` | 48062 | `config.h` |
| `MAX_IMAGE_SIZE` | 90000 (OG) / 750000 (X) | `config.h` |
| `SERVER_MAX_RETRIES` | 3 | `config.h` |
| `API_BASE_URL` | `"https://trmnl.app"` | `config.h` |
| `LOG_MAX_NOTES_NUMBER` | 10 | `config.h` |
| `WIFI_CONNECTION_RSSI` | -100 | `config.h` |
| `WIFI_MAX_SAVED_CREDS` | 5 | `WifiCaptive.h` |
| `CONNECTION_TIMEOUT` | 15000 | `WifiCaptive.h` |

### ESP-IDF SDK Config (`sdkconfig.defaults`)

- FreeRTOS tick rate: 1000 Hz
- Flash: 4MB
- TLS: Full mbedTLS with PSK, RSA, ECDHE, ECDSA
- WiFi: 11k/v support, scan cache, MBO, roaming, 11r, WNM, RRM
- SNTP: up to 3 servers, DHCP-provided NTP support
