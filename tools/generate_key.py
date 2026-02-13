#!/usr/bin/env python3
"""Generate a random 256-bit AES key and print as hex."""

import os
import sys

def main():
    key = os.urandom(32)
    hex_key = key.hex()
    print(hex_key)

    if "--header" in sys.argv:
        print(f'\n// C header format:')
        byte_str = ", ".join(f"0x{b:02x}" for b in key)
        print(f'static const uint8_t aes_key[32] = {{{byte_str}}};')

if __name__ == "__main__":
    main()
