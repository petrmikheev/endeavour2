savedcmd_drivers/endeavour/built-in.a := rm -f drivers/endeavour/built-in.a;  printf "drivers/endeavour/%s " earlycon.o | xargs riscv64-linux-gnu-ar cDPrST drivers/endeavour/built-in.a
