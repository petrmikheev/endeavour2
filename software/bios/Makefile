sources = asm.S main.c sbi.c util.c bench.c memtest.c readline.c sound.c time.c display.c sdcard.c usb.c console.c ext2.c charmap.S

TOOLCHAIN=../../../endeavour2-ext/rv32gc-linux-toolchain/bin/riscv32-unknown-linux-gnu-

OPTIONS=-nostdlib -ffreestanding -I../include -T bios.lds
OPTIONS+= -fomit-frame-pointer -fno-exceptions -fno-asynchronous-unwind-tables -fno-unwind-tables

BASE_ARCH=-march=rv32imfc_zicsr_zifencei_zicbop -mabi=ilp32f
BENCH_ARCH=${BASE_ARCH}

# zicsr		Control and status register access extension.
# zifencei	Instruction-fetch fence extension.
# zba		Address calculation extension.
# zbb		Basic bit manipulation extension.
# zbc		Carry-less multiplication extension.
# zbs		Single-bit operation extension.
# zicbop	Cache-block prefetch extension.

.PHONY: all
all: bios.bin microloader.bin

bios.elf: $(sources) bios_internal.h ext2.h bench_impl.o memtest_impl.o dhrystone/dhrystone.bin ../include/endeavour2/raw/defs.h ../include/endeavour2/raw/bios_defs.h
	${TOOLCHAIN}gcc ${OPTIONS} ${BASE_ARCH} -Wl,--no-warn-rwx-segments -Os -o bios.elf $(sources) bench_impl.o memtest_impl.o

bios.bin: bios.elf
	${TOOLCHAIN}objcopy -O binary bios.elf bios.bin

bench_impl.o: bench_impl.c
	${TOOLCHAIN}gcc ${OPTIONS} ${BENCH_ARCH} -O3 -c bench_impl.c

memtest_impl.o: memtest_impl.c
	${TOOLCHAIN}gcc ${OPTIONS} ${BASE_ARCH} -O3 -c memtest_impl.c

dhrystone/dhrystone.bin:
	cd dhrystone && make

.PHONE: objdump
objdump: bios.elf
	${TOOLCHAIN}objdump -d bios.elf

ter-u16.bdf:
	wget http://downloads.sourceforge.net/project/terminus-font/terminus-font-4.49/terminus-font-4.49.1.tar.gz
	tar xvzf terminus-font-4.49.1.tar.gz
	ln -sf terminus-font-4.49.1/ter-u16n.bdf ter-u16.bdf

charmap.S: ter-u16.bdf
	python3 gen_charmap.py ter-u16.bdf > charmap.S

microloader.bin: microloader.S ../include/endeavour2/raw/defs.h
	${TOOLCHAIN}gcc -march=rv32im_zicsr_zifencei -mabi=ilp32 -nostdlib -ffreestanding -Os -o microloader.elf microloader.S -I../include -Wl,--section-start=.text=0x40000000
	${TOOLCHAIN}objcopy -O binary microloader.elf microloader.bin
	../../scripts/gen_hex.py microloader.bin 2 > microloader.mem

.PHONY: clean
clean:
	rm -rf *.bin *.elf *.mem *.o

.PHONY: clean_charmap
clean_charmap:
	terminus-font-* ter-u16.bdf charmap.S


