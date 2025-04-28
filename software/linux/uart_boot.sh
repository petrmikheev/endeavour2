#!/bin/bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
EXT_DIR="$SCRIPT_DIR"/../../../endeavour2-ext

DTS=${1:-single_core.dts}
KERNEL=${2:-"$EXT_DIR"/linux-kernel/arch/riscv/boot/Image}
DTB=/tmp/endeavour2.dtb

dtc $DTS -o $DTB || exit 1

"$SCRIPT_DIR"/../../scripts/setup_uart.sh
sleep 0.5
echo "uart 80008000" $(stat -c%s $DTB) " # send DTB" > /dev/ttyUSB0
sleep 0.5
"$SCRIPT_DIR"/../../scripts/send_file_uart_12mhz.sh $DTB
sleep 0.5
echo "device_tree 80008000" > /dev/ttyUSB0
echo "uart 82000000" $(stat -c%s $KERNEL) " # send kernel" > /dev/ttyUSB0
sleep 0.5
"$SCRIPT_DIR"/../../scripts/send_file_uart_12mhz.sh $KERNEL
sleep 0.5
echo "crc32 82000000" $(stat -c%s $KERNEL) $(crc32 $KERNEL) " # check kernel CRC" > /dev/ttyUSB0
echo "boot 82000000" > /dev/ttyUSB0
