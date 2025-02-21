#!/bin/bash

stty -F /dev/ttyUSB0 115200 -icrnl -ixon -ixoff -opost -isig -icanon -echo cstopb -crtscts parenb
