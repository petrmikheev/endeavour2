#!/bin/bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

echo "stty raw -echo" > /dev/ttyUSB0
sleep 0.5
echo "dd bs=1 count=$(stat -c%s $1) of=$2" > /dev/ttyUSB0
cat $1 > /dev/ttyUSB0
echo "stty -raw echo" > /dev/ttyUSB0
