# trmnl-github

Custom firmware overlay for [TRMNL](https://usetrmnl.com/) e-ink displays that replaces the cloud API with **GitHub Pages** as the image source, using **AES-256-CBC encryption**.

Instead of fetching images from the TRMNL server, the device downloads encrypted images from a static GitHub Pages site that you control.

## How it works

1. Device wakes from deep sleep, connects to WiFi
2. Downloads an encrypted manifest from your GitHub Pages URL
3. Decrypts manifest with a pre-shared AES key stored in NVS
4. Picks the next screen (round-robin) and downloads the encrypted BMP
5. Decrypts in PSRAM, renders on the e-paper display
6. Goes back to deep sleep for `refresh_rate` seconds

## Repository structure

This repo contains only the **overlay files** — custom code that gets layered on top of the official TRMNL firmware. The upstream firmware lives in the `upstream` branch (clean snapshots of official releases).

```
main branch (overlay):
  src/           — Custom firmware source (crypto, GitHub client, manifest parser)
  include/       — Headers for overlay modules
  test/          — Unit tests for crypto
  tools/         — Python utilities (key generation, image encryption, manifest builder)
  content-template/ — Template for your content repository
  platformio.ini — Extended with github_pages build environment
  build.sh       — Build script that merges upstream + overlay

upstream branch:
  (complete TRMNL firmware snapshot, e.g. v1.7.5)
```

## Prerequisites

- Python 3 (venv recommended)
- `pip install platformio esptool`

## Configure secrets

Copy the example secrets file and fill in your values:

```bash
cp src/secrets.h.example src/secrets.h
```

Then edit `src/secrets.h`:

| Define | Description |
|--------|-------------|
| `GITHUB_PAGES_MANIFEST_URL` | URL to your encrypted manifest file (built with `./tools/update_manifest.py`) |
| `GITHUB_PAGES_IMAGES_BASE` | Base URL for encrypted images (created by `./tools/encrypt_image.py`) |
| `GITHUB_PAGES_AES_KEY_HEX` | 64-character hex string of your 256-bit AES key (generate with `./tools/generate_key.py`) |

`src/secrets.h` is listed in `.gitignore` — never commit real keys or private URLs. The values in `secrets.h` serve as compile-time defaults; they can be overridden at runtime via NVS (see [Device configuration](#device-configuration) below).

## Build

The build script extracts the upstream firmware and layers your overlay files on top in a `.build` directory:

MacOS/Linux
```bash
./build.sh

cd .build
pio run -e github_pages            # compile
pio run -e github_pages -t upload  # flash to device
```

Windows
```bash
.\build.bat

cd .build
pio run -e github_pages            # compile
pio run -e github_pages -t upload  # flash to device
```

To run crypto unit tests:

```bash
cd .build
pio test -e native-crypto
```

Clean up with `rm -rf .build`.

## Device configuration

On first boot the device starts a WiFi captive portal for network setup. The following NVS preferences must be set:

| Key | Description |
|-----|-------------|
| `manifest_url` | Full URL to your `manifest.enc` on GitHub Pages |
| `aes_key_hex` | 64-character hex string of your 256-bit AES key |
| `images_base` | Base URL for image downloads (e.g. `https://user.github.io/content/images/`) |

## Content setup

See [`content-template/README.md`](content-template/README.md) for instructions on setting up your content repository with GitHub Actions for automated image generation and encryption.

Quick start:

```bash
# Generate an AES key
python tools/generate_key.py

# Encrypt a test image
python tools/encrypt_image.py encrypt input.bmp output.enc --key <hex-key>

# Build the manifest
python tools/update_manifest.py images/ manifest.enc --key <hex-key>
```

## Updating upstream

When a new TRMNL firmware version is released:

1. Download or checkout the new release
2. Replace the contents of the `upstream` branch:
   ```bash
   git checkout upstream
   # remove old files, copy new release files
   git add -A
   git commit -m "trmnl-firmware vX.Y.Z"
   git checkout main
   ```
3. Rebuild with `./build.sh` and test

## Hardware

Targets **Seeed Studio XIAO ESP32-S3 PLUS** (8MB Flash, 8MB PSRAM). PSRAM is required for image buffering.
