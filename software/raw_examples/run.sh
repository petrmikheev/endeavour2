#!/bin/bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
SEND_FILE=$SCRIPT_DIR/../../scripts/send_file_uart_12mhz.sh
PORT=/dev/ttyUSB0

echo "uart 80008000 $(stat -c%s $1)" > $PORT
sleep 0.5
$SEND_FILE $1
sleep 0.5
echo "run 80008000" > $PORT
