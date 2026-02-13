# TRMNL Content Repository Template

This template creates a GitHub Pages site that serves encrypted images to a TRMNL e-ink display running the custom GitHub Pages firmware.

## Setup

1. **Create a new repo** from this template
2. **Enable GitHub Pages** in Settings > Pages (deploy from `main` branch, root `/`)
3. **Generate an AES key:**
   ```bash
   python tools/generate_key.py
   ```
4. **Add the key as a repository secret:** Settings > Secrets > `AES_KEY_HEX`
5. **Flash the same key** to the TRMNL device via NVS preferences

## How It Works

1. GitHub Actions runs on a schedule (every 6 hours by default)
2. `scripts/render_and_encrypt.py` generates BMP images and encrypts them with AES-256-CBC
3. `tools/update_manifest.py` builds an encrypted manifest listing all screens
4. Both are committed to the repo and served via GitHub Pages
5. The TRMNL device fetches `manifest.enc`, decrypts it, picks the next screen, fetches and decrypts the image, displays it, then sleeps

## File Structure

```
manifest.enc          # Encrypted manifest (fetched by device)
images/
  screen1.enc         # Encrypted BMP images
  screen2.enc
scripts/
  render_and_encrypt.py  # Your image generation + encryption script
.github/workflows/
  update_screens.yml     # Scheduled GitHub Actions workflow
```

## Customization

Edit `scripts/render_and_encrypt.py` to generate your own images. The device expects **800x480 1-bit BMP** images. You can use Pillow, Cairo, or any image library to render content (weather, calendar, quotes, etc.) and then encrypt with the shared AES key.

## Manifest Format

The decrypted manifest is JSON:
```json
{
  "version": 1,
  "refresh_rate": 1800,
  "updated_at": "2025-01-01T00:00:00Z",
  "screens": [
    {"name": "screen1", "filename": "screen1.enc", "size": 48080}
  ]
}
```

- `refresh_rate`: seconds between device wakes (default 1800 = 30 min)
- `screens`: ordered playlist; the device cycles through them round-robin
