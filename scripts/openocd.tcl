# Requires https://github.com/riscv-collab/riscv-openocd
#
# Usage:
#   openocd -f openocd.tcl
#   gdb
#       symbol-file /path/to/vmlinux
#       target remote :3333

adapter driver ftdi
transport select jtag
ftdi vid_pid 0x0403 0x6010
ftdi channel 1
ftdi layout_init 0x08 0x0b

adapter speed 1000

jtag newtap ti60 tap -irlen 5 -expected-id 0x10660A79

target create cpu.0 riscv -chain-position ti60.tap

riscv set_bscan_tunnel_ir 11
riscv use_bscan_tunnel 6 1

init
halt
