
TOOLCHAIN=../../../../endeavour-tools/rv32imac-linux-toolchain/bin/riscv32-unknown-linux-gnu-

OPTIONS=-march=rv32imc_zicsr_zifencei_zicbop -mabi=ilp32 -nostdlib -ffreestanding
OPTIONS+= -fno-zero-initialized-in-bss -I../../include -I. -T dhrystone.lds
OPTIONS+= -fomit-frame-pointer -fno-exceptions -fno-asynchronous-unwind-tables -fno-unwind-tables

dhrystone.bin: dhrystone.elf
	${TOOLCHAIN}objcopy -O binary dhrystone.elf dhrystone.bin

dhrystone.elf: dhry_1.c dhry_2.o strcmp.S asm.S dhry.h
	${TOOLCHAIN}gcc ${OPTIONS} -O3 -DTIME -w -o dhrystone.elf asm.S dhry_1.c dhry_2.o strcmp.S -Wl,--no-warn-rwx-segments

dhry_2.o: dhry_2.c dhry.h
	${TOOLCHAIN}gcc ${OPTIONS} -O3 -DTIME -w -c dhry_2.c

clean:
	rm -rf *.o dhrystone.bin dhrystone.elf

