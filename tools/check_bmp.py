import sys

def parse_bmp_header(data: bytes) -> tuple[str, bool]:
    """
    Parses a BMP file header.

    Returns:
        (error_code, reversed_flag)
        error_codes: 'BMP_NO_ERR', 'BMP_NOT_BMP', 'BMP_BAD_SIZE',
                     'BMP_COLOR_SCHEME_FAILED', 'BMP_INVALID_OFFSET'
    """
    reversed_flag = False

    # Check BMP signature
    if len(data) < 62:
        return 'BMP_NOT_BMP', reversed_flag

    if data[0] != ord('B') or data[1] != ord('M'):
        return 'BMP_NOT_BMP', reversed_flag

    # Parse header fields (little-endian)
    width               = int.from_bytes(data[18:22], 'little')
    height              = int.from_bytes(data[22:26], 'little')
    bits_per_pixel      = int.from_bytes(data[28:30], 'little')
    compression_method  = int.from_bytes(data[30:34], 'little')
    image_data_size     = int.from_bytes(data[34:38], 'little')
    color_table_entries = int.from_bytes(data[46:50], 'little')

    if color_table_entries == 0:
        color_table_entries = (1 << bits_per_pixel)

    if (width != 800 or height != 480 or bits_per_pixel != 1
            or image_data_size != 48000 or color_table_entries != 2):
        return 'BMP_BAD_SIZE', reversed_flag

    data_offset = int.from_bytes(data[10:14], 'little')

    # Log BMP header info
    print(
        f"[INFO] BMP Header Information:\n"
        f"  Width:               {width}\n"
        f"  Height:              {height}\n"
        f"  Bits per Pixel:      {bits_per_pixel}\n"
        f"  Compression Method:  {compression_method}\n"
        f"  Image Data Size:     {image_data_size}\n"
        f"  Color Table Entries: {color_table_entries}\n"
        f"  Data Offset:         {data_offset}"
    )

    if data_offset > 54:
        color_table_size = color_table_entries * 4  # Each entry is 4 bytes

        # Log color table entries
        print("[INFO] Color table")
        for i in range(0, color_table_size, 4):
            b = data[54 + i]
            r = data[55 + i]
            g = data[56 + i]
            a = data[57 + i]
            print(f"[INFO] Color {i // 4 + 1}: B-{b}, R-{r}, G-{g}, A-{a}")

        # Standard: color 0 = black (0,0,0,0), color 1 = white (255,255,255,0)
        if (data[54:58] == bytes([0, 0, 0, 0]) and
                data[58:62] == bytes([255, 255, 255, 0])):
            print("[INFO] Color scheme standard")
            reversed_flag = False

        # Reversed: color 0 = white (255,255,255,0), color 1 = black (0,0,0,0)
        elif (data[54:58] == bytes([255, 255, 255, 0]) and
                data[58:62] == bytes([0, 0, 0, 0])):
            print("[INFO] Color scheme reversed")
            reversed_flag = True

        else:
            print("[INFO] Color scheme damaged")
            return 'BMP_COLOR_SCHEME_FAILED', reversed_flag

        return 'BMP_NO_ERR', reversed_flag

    else:
        return 'BMP_INVALID_OFFSET', reversed_flag


def main():
    filename = sys.argv[1] if len(sys.argv) > 1 else 'image.bmp'

    try:
        with open(filename, 'rb') as f:
            data = f.read()
    except FileNotFoundError:
        print(f"Error: File '{filename}' not found.")
        sys.exit(1)
    except IOError as e:
        print(f"Error reading file: {e}")
        sys.exit(1)

    if not data:
        print("Error: File is empty.")
        sys.exit(1)

    error_code, reversed_flag = parse_bmp_header(data)

    messages = {
        'BMP_NO_ERR':              f"BMP parsed successfully. Reversed color scheme: {reversed_flag}",
        'BMP_NOT_BMP':             "Error: Not a BMP file.",
        'BMP_BAD_SIZE':            "Error: BMP dimensions or format mismatch (expected 800x480, 1bpp, 48000 bytes).",
        'BMP_COLOR_SCHEME_FAILED': "Error: Unrecognized color scheme in color table.",
        'BMP_INVALID_OFFSET':      "Error: Invalid data offset (no color table found).",
    }

    print(messages.get(error_code, f"Unknown error: {error_code}"))
    sys.exit(0 if error_code == 'BMP_NO_ERR' else 1)


if __name__ == '__main__':
    main()