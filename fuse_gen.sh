#!/bin/bash

MCU="t85"
BAUD="19200"

PROGRAMMER="$1"
PORT="$2"

if [ -z "$PROGRAMMER" ] || [ -z "$PORT" ]; then
    echo "Usage: $0 <programmer> <port>"
    echo "Example: $0 arduino /dev/ttyACM0"
    exit 1
fi

# 8 MHz internal oscillator, no clock divide
avrdude -c "$PROGRAMMER" -P "$PORT" -b "$BAUD" -p "$MCU" \
-U lfuse:w:0xE2:m \
-U hfuse:w:0xDD:m \
-U efuse:w:0xFF:m

# ./fuse_gen.sh arduino /dev/ttyACM0
# ./fuse_gen.sh usbasp usb
