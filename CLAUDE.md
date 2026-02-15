# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

TRMNL Firmware is embedded C++ firmware for the TRMNL e-ink display device — a battery-powered, WiFi-connected ESP32 microcontroller that fetches and renders images from a remote server. Uses PlatformIO with the Arduino + ESP-IDF framework.

## Build Commands

```bash
# Build default (trmnl, ESP32-C3)
pio run

# Build specific board
pio run -e TRMNL_X

# Run tests (macOS/Linux)
pio test -e native -v

# Run tests (Windows)
pio test -e native-windows -v

# Run a single test suite
pio test -e native -v --filter test_parse_api_display

# Static analysis (ClangTidy)
pio check --skip-packages --fail-on-defect high

# Upload to device
pio run --target upload
```

## Architecture

The firmware is structured as a state machine driven by `bl.cpp` (business logic):

- **`src/bl.cpp`** (~2300 lines) — Core state machine. Handles WiFi connection, API calls, OTA firmware updates, deep sleep scheduling, and wake reason detection. Entry points: `bl_init()` and `bl_process()` called from `src/main.cpp`.
- **`src/display.cpp`** (~2000 lines) — E-ink display driver. Handles PNG/BMP rendering, font/overlay drawing, and board-specific display initialization.
- **`lib/trmnl/src/`** — Reusable library code: API response parsing (`parse_response_api_display.cpp`, `parse_response_api_setup.cpp`), log serialization, PNG flipping, BMP handling, string utilities.
- **`include/config.h`** — Firmware version, device profiles, timing constants, pin assignments. Single source of truth for hardware config.
- **`include/pins.h`** — GPIO pin mappings per board variant.
- **`test/`** — 8 unit test suites using PlatformIO's Unity framework with ArduinoFake for mocking Arduino APIs. Tests run natively (no hardware needed).

## API Flow

The device communicates with the TRMNL server at two endpoints:
1. **`GET /api/setup`** — Called on first boot (no API key yet); exchanges MAC address for `api_key` + `friendly_id`.
2. **`GET /api/display`** — Called each wake cycle; returns `image_url`, `refresh_rate`, `update_firmware`, `reset_firmware` flags.
3. **`POST /api/log`** — Called when errors are detected in API responses.

## Multi-Board Support

Board variants are selected via PlatformIO environments in `platformio.ini`. Key environments:
- `trmnl` — TRMNL OG, ESP32-C3, 2-color e-ink (default)
- `TRMNL_X` — ESP32-S3, 7.5" display, uses `FastEPD` library
- `seeed_xiao_esp32c3`, `seeed_xiao_esp32s3`, `waveshare-esp32-driver` — Third-party hardware
- DIY kit and reTerminal variants

Board-specific behavior is gated with preprocessor macros (e.g., `BOARD_TRMNL_4CLR`, `BOARD_TRMNL_X`) defined in `include/config.h`.

## Key Dependencies

- `bb_epaper` — E-paper display driver (TRMNL OG)
- `FastEPD` — Fast e-paper driver (TRMNL_X only)
- `PNGdec` / `JPEGDEC` — Image decoding
- `ArduinoJson@7.x` — JSON parsing for API responses
- `ESPAsyncWebServer` — Captive portal for WiFi setup
- `ArduinoFake` — Arduino API mocking in native tests

## Partition Tables

- `min_spiffs.csv` — Used for TRMNL OG (ESP32-C3)
- `trmnl_x_partitions.csv` — Used for TRMNL X (ESP32-S3)

SPIFFS is used for image caching and persistent log storage.
