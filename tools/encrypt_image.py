#!/usr/bin/env python3
"""Encrypt or decrypt files using AES-256-CBC with PKCS7 padding.

Wire format: [16-byte IV][ciphertext with PKCS7 padding]

Usage:
    python encrypt_image.py --key <hex> --input <file> --output <file>
    python encrypt_image.py --key <hex> --input <file> --output <file> --decrypt
"""

import argparse
import os
import sys

try:
    from Crypto.Cipher import AES
    from Crypto.Util.Padding import pad, unpad
except ImportError:
    try:
        from Cryptodome.Cipher import AES
        from Cryptodome.Util.Padding import pad, unpad
    except ImportError:
        print("Error: pycryptodome is required. Install with: pip install pycryptodome", file=sys.stderr)
        sys.exit(1)


def encrypt(key: bytes, plaintext: bytes) -> bytes:
    iv = os.urandom(16)
    cipher = AES.new(key, AES.MODE_CBC, iv)
    ciphertext = cipher.encrypt(pad(plaintext, AES.block_size))
    return iv + ciphertext


def decrypt(key: bytes, data: bytes) -> bytes:
    if len(data) < 32:
        raise ValueError("Data too short (need at least IV + one block)")
    iv = data[:16]
    ciphertext = data[16:]
    cipher = AES.new(key, AES.MODE_CBC, iv)
    return unpad(cipher.decrypt(ciphertext), AES.block_size)


def main():
    parser = argparse.ArgumentParser(description="AES-256-CBC file encrypt/decrypt")
    parser.add_argument("--key", required=True, help="256-bit key as 64-char hex string")
    parser.add_argument("--input", required=True, help="Input file path")
    parser.add_argument("--output", required=True, help="Output file path")
    parser.add_argument("--decrypt", action="store_true", help="Decrypt instead of encrypt")
    args = parser.parse_args()

    key = bytes.fromhex(args.key)
    if len(key) != 32:
        print("Error: key must be 32 bytes (64 hex chars)", file=sys.stderr)
        sys.exit(1)

    with open(args.input, "rb") as f:
        data = f.read()

    if args.decrypt:
        result = decrypt(key, data)
        print(f"Decrypted {len(data)} -> {len(result)} bytes", file=sys.stderr)
    else:
        result = encrypt(key, data)
        print(f"Encrypted {len(data)} -> {len(result)} bytes", file=sys.stderr)

    with open(args.output, "wb") as f:
        f.write(result)


if __name__ == "__main__":
    main()
