
SRC = dhry_1.c dhry_2.c strcmp.S
HDR = dhry.h

build/dhrystone.bin: $(SRC) $(HDR)
	mkdir -p build
	../util/build.sh build/dhrystone $(SRC) -O3 -DTIME -I.

.PHONY runsim:
runsim: build/dhrystone.bin
	../util/runsim.sh build/dhrystone.bin $(ARGS)

clean:
	rm -rf build

