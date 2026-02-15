esptool.py --chip esp32s3 \
  --port /dev/cu.usbmodem1101 \
  --baud 921600 \
  write_flash \
  0x0000 ./.build/.pio/build/github_pages/bootloader.bin \
  0x8000 ./.build/.pio/build/github_pages/partitions.bin \
  0x10000 ./.build/.pio/build/github_pages/firmware.bin