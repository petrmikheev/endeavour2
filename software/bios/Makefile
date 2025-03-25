sources = asm.S main.c util.c bench.c charmap.S dhry_1.o dhry_2.o dhrystone/strcmp.S dhrystone/strcpy.S
#sdcard.c console.c

TOOLCHAIN=../../../endeavour-tools/rv32imac-linux-toolchain/bin/riscv32-unknown-linux-gnu-
OPTIONS=-mabi=ilp32 -nostdlib -ffreestanding -I../include -T bios.lds
BASE_ARCH=-march=rv32imc_zicsr_zifencei_zicbop
BENCH_ARCH=-march=rv32imc_zicsr_zifencei_zicbop_zba_zbb_zbc_zbs

# zicsr		Control and status register access extension.
# zifencei	Instruction-fetch fence extension.
# zba		Address calculation extension.
# zbb		Basic bit manipulation extension.
# zbc		Carry-less multiplication extension.
# zbs		Single-bit operation extension.
# zicbop	Cache-block prefetch extension.

.PHONY: all
all: bios.bin microloader.bin

bios.elf: $(sources) bench_impl.o ../include/endeavour2/defs.h ../include/endeavour2/bios.h
	${TOOLCHAIN}gcc ${OPTIONS} ${BASE_ARCH} -Wl,--no-warn-rwx-segments -Os -o bios.elf $(sources) bench_impl.o

bios.bin: bios.elf
	${TOOLCHAIN}objcopy -O binary bios.elf bios.bin

bench_impl.o: bench_impl.c
	${TOOLCHAIN}gcc ${OPTIONS} ${BENCH_ARCH} -O3 -c bench_impl.c

dhry_1.o: dhrystone/dhry_1.c ../include/endeavour2/bios.h
	${TOOLCHAIN}gcc ${OPTIONS} ${BASE_ARCH} -O3 -DTIME -Idhrystone -w -c dhrystone/dhry_1.c

dhry_2.o: dhrystone/dhry_2.c
	${TOOLCHAIN}gcc ${OPTIONS} ${BASE_ARCH} -O3 -DTIME -Idhrystone -w -c dhrystone/dhry_2.c

.PHONE: objdump
objdump: bios.elf
	${TOOLCHAIN}objdump -d bios.elf

ter-u16.bdf:
	wget http://downloads.sourceforge.net/project/terminus-font/terminus-font-4.49/terminus-font-4.49.1.tar.gz
	tar xvzf terminus-font-4.49.1.tar.gz
	ln -sf terminus-font-4.49.1/ter-u16n.bdf ter-u16.bdf

charmap.S: ter-u16.bdf
	python3 gen_charmap.py ter-u16.bdf > charmap.S

microloader.bin: microloader.S ../include/endeavour2/defs.h
	${TOOLCHAIN}gcc -march=rv32im_zicsr_zifencei -mabi=ilp32 -nostdlib -ffreestanding -Os -o microloader.elf microloader.S -I../include -Wl,--section-start=.text=0x40000000
	${TOOLCHAIN}objcopy -O binary microloader.elf microloader.bin
	../../scripts/gen_hex.py microloader.bin 2 > microloader.mem

.PHONY: clean
clean:
	rm -rf *.bin *.elf *.mem *.o dhrystone/*.o

.PHONY: clean_charmap
clean_charmap:
	terminus-font-* ter-u16.bdf charmap.S


