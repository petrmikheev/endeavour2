TOOLCHAIN=../../../endeavour2-ext/rv32gc-linux-toolchain/bin/riscv32-unknown-linux-gnu-

OPTIONS=-mabi=ilp32 -nostdlib -ffreestanding -I../include -T flat.lds
OPTIONS+= -fomit-frame-pointer -fno-exceptions -fno-asynchronous-unwind-tables -fno-unwind-tables
OPTIONS+= -fno-zero-initialized-in-bss -Wl,--no-warn-rwx-segments
OPTIONS+= -march=rv32imc_zicsr_zifencei_zicbop -O3

sd.bin: sd.c
hello_world.bin : hello_world.c
#hello_world.elf : hello_world.c

%.elf : %.c
	${TOOLCHAIN}gcc ${OPTIONS} start.S $< -o $@

%.bin : %.elf
	${TOOLCHAIN}objcopy -O binary $< $@

.PHONY: clean
clean:
	rm -f *.bin *.elf
