#!/bin/bash

MCU="t85"
BAUD="19200"
HEX="main.hex"

PROGRAMMER="$1"
PORT="$2"

if [ -z "$PROGRAMMER" ] || [ -z "$PORT" ]; then
    echo "Usage: $0 <programmer> <port>"
    echo "Example: $0 arduino /dev/ttyACM0"
    exit 1
fi

avrdude -c "$PROGRAMMER" -P "$PORT" -b "$BAUD" -p "$MCU" \
-U flash:w:"$HEX"

# ./flash_gen.sh arduino /dev/ttyACM0
# ./flash_gen.sh usbasp usb
