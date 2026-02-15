#!/usr/bin/env python3
"""Generate a test BMP image and encrypt it for the TRMNL device.

Creates a 800x480 1-bit BMP with a test pattern, then encrypts it.

Usage:
    python render_and_encrypt.py --key <hex> --output-dir <path>
"""

import argparse
import os
import struct
import sys

# Add parent tools dir to path for encrypt_image
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))

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


def create_test_bmp(width=800, height=480) -> bytes:
    """Create a 1-bit BMP with a checkerboard test pattern."""
    row_size = (width + 31) // 32 * 4  # rows padded to 4-byte boundary
    image_data_size = row_size * height

    # BMP header (14 bytes)
    bmp_header = struct.pack("<2sIHHI",
        b"BM",
        14 + 40 + 8 + image_data_size,  # file size
        0,  # reserved
        0,  # reserved
        14 + 40 + 8,  # offset to pixel data
    )

    # DIB header (40 bytes - BITMAPINFOHEADER)
    dib_header = struct.pack("<IiiHHIIiiII",
        40,  # header size
        width,
        height,
        1,  # color planes
        1,  # bits per pixel
        0,  # compression (none)
        image_data_size,
        3780,  # X pixels per meter
        3780,  # Y pixels per meter
        2,  # colors in table
        2,  # important colors
    )

    # Color table: black first, white second (standard scheme)
    color_table = struct.pack("<BBBB BBBB",
        0, 0, 0, 0,       # color 0: black
        255, 255, 255, 0,  # color 1: white
    )

    # Image data: checkerboard pattern (BMP is bottom-up)
    pixels = bytearray(image_data_size)
    block_size = 32  # pixels per checker square
    for y in range(height):
        for x_byte in range(row_size):
            byte_val = 0
            for bit in range(8):
                px = x_byte * 8 + bit
                if px < width:
                    # Checkerboard: alternate black/white in blocks
                    checker_x = px // block_size
                    checker_y = y // block_size
                    if (checker_x + checker_y) % 2 == 0:
                        byte_val |= (1 << (7 - bit))  # white
            pixels[y * row_size + x_byte] = byte_val

    return bmp_header + dib_header + color_table + bytes(pixels)


def main():
    parser = argparse.ArgumentParser(description="Generate and encrypt test BMP")
    parser.add_argument("--key", required=True, help="256-bit key as 64-char hex string")
    parser.add_argument("--output-dir", required=True, help="Output directory for encrypted images")
    parser.add_argument("--name", default="test_pattern", help="Image name (default: test_pattern)")
    args = parser.parse_args()

    key = bytes.fromhex(args.key)
    if len(key) != 32:
        print("Error: key must be 32 bytes (64 hex chars)", file=sys.stderr)
        sys.exit(1)

    os.makedirs(args.output_dir, exist_ok=True)

    bmp_data = create_test_bmp()
    print(f"Generated test BMP: {len(bmp_data)} bytes", file=sys.stderr)

    # Save unencrypted for debugging
    debug_path = os.path.join(args.output_dir, f"{args.name}.bmp")
    with open(debug_path, "wb") as f:
        f.write(bmp_data)
    print(f"Wrote debug BMP to {debug_path}", file=sys.stderr)

    # Encrypt
    encrypted = encrypt(key, bmp_data)
    enc_path = os.path.join(args.output_dir, f"{args.name}.enc")
    with open(enc_path, "wb") as f:
        f.write(encrypted)
    print(f"Wrote encrypted image ({len(encrypted)} bytes) to {enc_path}", file=sys.stderr)


if __name__ == "__main__":
    main()
