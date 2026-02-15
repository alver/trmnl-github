#!/usr/bin/env python3
"""Scan encrypted images and build an encrypted manifest.json.

Usage:
    python update_manifest.py --key <hex> --images-dir <path> --output <path> [--refresh-rate 1800]

The manifest JSON format (before encryption):
{
    "version": 1,
    "refresh_rate": 1800,
    "updated_at": "2025-01-01T00:00:00Z",
    "screens": [
        {"name": "screen1", "filename": "screen1.enc", "size": 12345},
        ...
    ]
}
"""

import argparse
import json
import os
import sys
from datetime import datetime, timezone

try:
    from Crypto.Cipher import AES
    from Crypto.Util.Padding import pad
except ImportError:
    try:
        from Cryptodome.Cipher import AES
        from Cryptodome.Util.Padding import pad
    except ImportError:
        print("Error: pycryptodome is required. Install with: pip install pycryptodome", file=sys.stderr)
        sys.exit(1)


def encrypt(key: bytes, plaintext: bytes) -> bytes:
    iv = os.urandom(16)
    cipher = AES.new(key, AES.MODE_CBC, iv)
    ciphertext = cipher.encrypt(pad(plaintext, AES.block_size))
    return iv + ciphertext


def main():
    parser = argparse.ArgumentParser(description="Build encrypted manifest from image directory")
    parser.add_argument("--key", required=True, help="256-bit key as 64-char hex string")
    parser.add_argument("--images-dir", required=True, help="Directory containing .enc image files")
    parser.add_argument("--output", required=True, help="Output path for encrypted manifest")
    parser.add_argument("--refresh-rate", type=int, default=1800, help="Refresh rate in seconds (default 1800)")
    args = parser.parse_args()

    key = bytes.fromhex(args.key)
    if len(key) != 32:
        print("Error: key must be 32 bytes (64 hex chars)", file=sys.stderr)
        sys.exit(1)

    # Scan for .enc files
    images_dir = args.images_dir
    screens = []
    for fname in sorted(os.listdir(images_dir)):
        if fname.endswith(".enc"):
            fpath = os.path.join(images_dir, fname)
            size = os.path.getsize(fpath)
            name = os.path.splitext(fname)[0]
            screens.append({
                "name": name,
                "filename": fname,
                "size": size,
            })

    if not screens:
        print(f"Error: no .enc files found in {images_dir}", file=sys.stderr)
        sys.exit(1)

    manifest = {
        "version": 1,
        "refresh_rate": args.refresh_rate,
        "updated_at": datetime.now(timezone.utc).isoformat(),
        "screens": screens,
    }

    manifest_json = json.dumps(manifest, indent=2).encode("utf-8")
    print(f"Manifest: {len(screens)} screens, {len(manifest_json)} bytes JSON", file=sys.stderr)

    encrypted = encrypt(key, manifest_json)

    with open(args.output, "wb") as f:
        f.write(encrypted)

    print(f"Wrote encrypted manifest ({len(encrypted)} bytes) to {args.output}", file=sys.stderr)

    # Also write plaintext for debugging
    debug_path = args.output + ".debug.json"
    with open(debug_path, "w") as f:
        json.dump(manifest, f, indent=2)
    print(f"Wrote debug manifest to {debug_path}", file=sys.stderr)


if __name__ == "__main__":
    main()
