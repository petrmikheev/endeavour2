TOOLCHAIN=../../../endeavour2-ext/rv32gc-linux-toolchain/bin/riscv32-unknown-linux-gnu-

textwm2: textwm.c textwm.h utf8.c utf8.h tty.c tty.h input.c input.h
	${TOOLCHAIN}gcc -o textwm2 textwm.c utf8.c tty.c input.c -I../include

.PHONY: clean
clean:
	rm -f textwm2
