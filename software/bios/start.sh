#!/bin/bash

(sleep 0.5 ; cat bios.bin > /dev/ttyUSB0) & minicom
