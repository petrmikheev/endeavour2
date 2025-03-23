sources = asm.S main.c util.c charmap.S
#sdcard.c console.c

TOOLCHAIN=riscv64-unknown-elf-
OPTIONS=-march=rv32im -mabi=ilp32 -nostdlib -ffreestanding -I../include -Os -T bios.lds

.PHONY: all
all: bios.bin microloader.bin

bios.elf: $(sources) ../include/endeavour2/defs.h ../include/endeavour2/bios.h
	${TOOLCHAIN}gcc ${OPTIONS} -o bios.elf $(sources)

bios.bin: bios.elf
	${TOOLCHAIN}objcopy -O binary bios.elf bios.bin

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
	${TOOLCHAIN}gcc -march=rv32im -mabi=ilp32 -nostdlib -ffreestanding -Os -o microloader.elf microloader.S -I../include -Wl,--section-start=.text=0x40000000
	${TOOLCHAIN}objcopy -O binary microloader.elf microloader.bin
	../../scripts/gen_hex.py microloader.bin 2 > microloader.mem

.PHONY: clean
clean:
	rm -rf *.bin *.elf *.mem terminus-font-* ter-u16.bdf charmap.S


