#!/bin/bash

PORT=/dev/ttyUSB0

setserial $PORT spd_cust
setserial $PORT divisor 5
stty -F $PORT 38400 2> /dev/null
cat $1 > $PORT
stty -F $PORT 115200 2> /dev/null
