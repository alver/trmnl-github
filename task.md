# Prompt for Claude Code

Copy everything below the line and paste as your first message to Claude Code in the forked repo directory.

---

I have a forked repo of https://github.com/usetrmnl/trmnl-firmware — the open-source firmware for the TRMNL e-ink display device. This is a PlatformIO project targeting ESP32-based boards with a 7.5" Waveshare e-paper display (800×480).

My device is the **TRMNL DIY Kit** which uses a **Seeed Studio XIAO ESP32-S3 PLUS** (8MB Flash, 8MB PSRAM).

## What I want to build

I want to **replace the stock client-server communication** with a much simpler architecture:

**Instead of the TRMNL server**, the device will fetch encrypted images directly from a **public GitHub Pages** site. No server, no API keys, no subscriptions.

The flow is:
1. Device wakes from deep sleep
2. Connects to WiFi
3. Downloads an encrypted `manifest.enc` from a GitHub Pages URL via HTTPS
4. Decrypts it with AES-256-CBC (key stored in device firmware/NVS)
5. Parses the JSON manifest to find image URLs and refresh rate
6. Downloads the encrypted image file (`.enc`)
7. Decrypts it in PSRAM → gets a BMP (800×480, 1-bit monochrome)
8. Displays it on the e-paper
9. Goes to deep sleep for `refresh_rate` seconds

**Encryption is required** because the images will contain private information (calendar, personal data). The GitHub repo is public, but all content files are AES-256-CBC encrypted. Only the device (and GitHub Actions with GitHub Secrets that generate the content) know the key.

## Encryption scheme

- **AES-256-CBC** with PKCS7 padding
- Each `.enc` file format: `[16-byte random IV][ciphertext]`
- Key: 32 random bytes, stored in firmware NVS (or hardcoded for dev)
- ESP32-S3 has hardware AES acceleration via mbedTLS (built into ESP-IDF)
- Same key is used by Python scripts (pycryptodome) from GitHub secrets to encrypt on the GitHub Actions side

## Manifest format

`manifest.enc` decrypts to JSON:
```json
{
  "version": 1,
  "refresh_rate": 1800,
  "updated_at": "2026-02-11T12:00:00Z",
  "screens": [
    {
      "name": "weather",
      "filename": "a1b2c3d4e5f6.enc",
      "size": 48256
    },
    {
      "name": "calendar",
      "filename": "f6e5d4c3b2a1.enc",
      "size": 48128
    }
  ]
}
```

The device cycles through screens sequentially (playlist index stored in RTC memory to survive deep sleep).

## Content repository structure (separate GitHub repo)

```
trmnl-content/           (public repo, served via GitHub Pages)
├── manifest.enc         (encrypted JSON)
├── images/
│   ├── a1b2c3d4.enc    (encrypted BMP files)
│   └── f6e5d4c3.enc
└── scripts/
    ├── generate_weather.py
    ├── render_and_encrypt.py
    └── requirements.txt
```

## What you need to do

### Step 1: Study the existing codebase

Before writing any code, thoroughly read and understand:
- `src/bl.cpp` — the main business logic (this is what we're replacing)
- `src/display.cpp` and `include/display.h` — display abstraction (we keep this)
- `lib/esp32-waveshare-epd/` — e-paper hardware driver (we keep this entirely)
- `lib/trmnl/` — BMP/PNG parsers (we keep this)
- `platformio.ini` — existing board configurations, find the XIAO ESP32-S3 target
- `boards/` — pin definitions for XIAO ESP32-S3
- `include/config.h` — existing config structure
- `src/api-client/` — existing server communication (we're replacing this)

Understand the data flow: how does an image go from HTTP download → BMP parsing → pixel buffer → e-paper display? Trace this path through the code.

### Step 2: Plan the changes

Create `PLAN.md` with your analysis of:
- Which files we keep unchanged (display driver, BMP parser, etc.)
- Which files we modify (display.cpp may need minor edits to remove server dependencies)
- Which files we delete (api-client/, server-specific logic in bl.cpp)
- Which new files we create
- Any potential issues with the existing code structure

**Present this plan to me before proceeding.**

### Step 3: Implement the firmware changes

After plan approval:

1. **Create `src/crypto.cpp` + `include/crypto.h`**: AES-256-CBC decryption using mbedTLS (`mbedtls/aes.h`). IMPORTANT: `mbedtls_aes_crypt_cbc()` modifies the IV buffer in-place — always copy IV before calling. Use PKCS7 unpadding.

2. **Create `src/github_client.cpp` + `include/github_client.h`**: HTTPS GET using `WiFiClientSecure`. Download files into PSRAM buffers (`heap_caps_malloc(size, MALLOC_CAP_SPIRAM)`). For prototype, `setInsecure()` is OK for TLS; later pin the root CA cert.

3. **Create `src/manifest.cpp` + `include/manifest.h`**: Parse decrypted manifest JSON using ArduinoJson. Extract screen list, refresh_rate, updated_at.

4. **Rewrite `src/main.cpp`** (replaces `bl.cpp` as entry point): Implement the wake → WiFi → fetch manifest → decrypt → fetch image → decrypt → display → sleep cycle. Keep WiFi captive portal for initial setup. Store playlist index in `RTC_DATA_ATTR`. Store config (WiFi creds, manifest URL, AES key) in NVS via `Preferences`.

5. **Clean up**: Remove `src/api-client/` directory, remove TRMNL server references from any kept files. Update `platformio.ini` if needed.

### Step 4: Create Python tooling

In a `tools/` directory:

1. **`tools/generate_key.py`**: Generate 32 random bytes, output as hex and optionally as C header
2. **`tools/encrypt_image.py`**: Encrypt/decrypt files with AES-256-CBC (pycryptodome). Format: `[IV][ciphertext]`. Support `--decrypt` flag for testing.
3. **`tools/update_manifest.py`**: Scan encrypted images, build manifest JSON, encrypt it

### Step 5: Create content repo template

Create a `content-template/` directory in this repo with:
1. Example `scripts/render_and_encrypt.py` — generates a simple test BMP (800×480, 1-bit, with "Hello TRMNL" text), encrypts it, builds manifest
2. Example `.github/workflows/update_screens.yml` — GitHub Actions workflow that runs on schedule
3. `README.md` explaining how to set up the content repo

### Step 6: Verify

- Ensure the PlatformIO project compiles without errors for the XIAO ESP32-S3 target
- Write a native test (`test/test_crypto.cpp`) that verifies encrypt→decrypt round-trip using known test vectors
- Run `pio run` to verify build
- Run `pio test -e native` for crypto tests
- Test Python tools: generate key → encrypt a test BMP → decrypt it → verify files match

## Key constraints

- **Keep all upstream display/driver code working** — don't break what works
- ESP32-S3 with PSRAM: large buffers (images, HTTP responses) MUST use `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)`
- BMP format for the display is strict: 800×480, 1-bit, palette `(0,0,0,0)` and `(255,255,255,0)`
- The project must remain a valid PlatformIO project
- License: GPL-3.0 (matching upstream)
- All new code, comments, documentation in English

## What NOT to do

- Don't rewrite the e-paper display driver — it's hundreds of lines of hardware-specific code that works
- Don't rewrite the BMP parser — it's already tested with the display
- Don't add unnecessary dependencies — ArduinoJson is fine, avoid heavy frameworks
- Don't implement a local web server for content — the whole point is GitHub Pages
- Don't use `localStorage`/browser APIs — this is embedded C++ firmware, not a web app

Start with Step 1. Read the codebase thoroughly and report your findings before writing any code.