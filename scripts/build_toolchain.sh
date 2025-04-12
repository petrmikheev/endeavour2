#!/bin/bash

# Deps for debian/ubuntu
# sudo apt-get install autoconf automake autotools-dev curl python3 python3-pip libmpc-dev libmpfr-dev libgmp-dev gawk build-essential bison flex texinfo gperf libtool patchutils bc zlib1g-dev libexpat-dev ninja-build git cmake libglib2.0-dev libslirp-dev

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
EXT_DIR=$SCRIPT_DIR/../../endeavour2-ext

mkdir -p $EXT_DIR
cd $EXT_DIR

git clone https://github.com/riscv/riscv-gnu-toolchain
cd riscv-gnu-toolchain
./configure --prefix=$EXT_DIR/rv32gc-linux-toolchain --with-arch=rv32gc --with-abi=ilp32d
make linux-native
