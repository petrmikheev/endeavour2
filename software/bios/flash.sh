#!/bin/bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
SEND_FILE=$SCRIPT_DIR/../../scripts/send_file_uart_12mhz.sh
PORT=/dev/ttyUSB0
IMAGE=${1:-bios.bin}

echo "uart 80008000 $(stat -c%s $IMAGE)" > $PORT
sleep 0.5
$SEND_FILE $IMAGE
sleep 0.5
echo "flash_bios 80008000 $(crc32 $IMAGE)" > $PORT
