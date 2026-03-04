EXT=../../../../endeavour2-ext

CC=${EXT}/rv32gc-linux-toolchain/bin/riscv32-unknown-linux-gnu-gcc
STAGING=${EXT}/buildroot-2025.02/output/staging

CFLAGS=-O3 -march=rv32gc_zicsr_zifencei_zba -mabi=ilp32d

CFLAGS+= -I../../include
CFLAGS+= -I${STAGING}/usr/include/xorg
CFLAGS+= -fvisibility=hidden
CFLAGS+= -I${STAGING}/usr/include/pixman-1
CFLAGS+= -I${STAGING}/usr/include

e2fbdev.so: e2fbdev.c compat-api.h
	$(CC) -fPIC -shared ${CFLAGS} e2fbdev.c -o e2fbdev_drv.so -Wl,-z,now
