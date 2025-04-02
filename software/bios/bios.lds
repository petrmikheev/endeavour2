ENTRY(_start)

SECTIONS
{
   . = 0x80000000;
   .text.startup : { *(.text.startup) }
   .text : { *(.text) }
   PROVIDE( __global_pointer$ = . + 0x800 );
   .rodata : { *(.rodata) }
   .data : { *(.data) }
   .bss : { *(.bss) }
   . = 0x80007300;
   .bss.ohci_hcca : { *(.bss.ohci_hcca) }
   . = 0x80007400;
   .text.dhrystone_bin : { *(.data.dhrystone_bin) }
   . = 0x80007ffc;
   .data.biosmagic : { *(.data.biosmagic) }
}
