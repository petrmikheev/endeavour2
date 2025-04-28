#TOOLCHAIN=../../../endeavour2-ext/rv32gc-linux-toolchain/bin/riscv32-unknown-linux-gnu-
#OPTIONS= -I../include -march=rv32gc_zicsr_zifencei_zicbop -mabi=ilp32d -O3

TOOLCHAIN=../../../endeavour-tools/rv32imac-linux-toolchain/bin/riscv32-unknown-linux-gnu-
OPTIONS= -I../include -march=rv32imac_zicsr_zifencei_zicbop -mabi=ilp32 -O3

display_demo.elf : display_demo.c

%.elf : %.c
	${TOOLCHAIN}gcc ${OPTIONS} $< -o $@

.PHONY: clean
clean:
	rm -f *.elf
