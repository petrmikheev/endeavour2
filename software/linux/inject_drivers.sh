#!/bin/bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
DST=${1:-$SCRIPT_DIR/../../../endeavour2-ext/linux-kernel}

grep endeavour "${DST}"/drivers/Makefile > /dev/null || echo "obj-y += endeavour/" >> "${DST}"/drivers/Makefile
grep endeavour "${DST}"/Kconfig > /dev/null || echo 'source "drivers/endeavour/Kconfig"' >> "${DST}"/Kconfig
rm -rf "${DST}"/drivers/endeavour
cp -r "${SCRIPT_DIR}"/drivers "${DST}"/drivers/endeavour

# cross compile for riscv:
# make -j12 ARCH=riscv CROSS_COMPILE=riscv32-unknown-linux-gnu-
# make ARCH=riscv CROSS_COMPILE=/home/petya/endeavour2-ext/rv32gc-linux-toolchain/bin/riscv32-unknown-linux-gnu-
