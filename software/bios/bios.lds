ENTRY(_start)

SECTIONS
{
   . = 0x80000000;
   .text.start : { *(.text.start) }
   . = 0x80000040;
   .text.api : { *(.text.api) }
   .text : { *(.text) }
   . = ALIGN(4);
   PROVIDE( __global_pointer$ = . + 0x800 );
   .data.data_start : { *(.data.data_start) }
   .data : { *(.data) }
   .bss : { *(.bss) }
   .rodata : { *(.rodata) }
   . = 0x80007000;
   .bss.core1_stack : { *(.bss.core1_stack) }
   . = 0x80007300;
   .bss.ohci_hcca : { *(.bss.ohci_hcca) }
   . = 0x80007400;
   .text.dhrystone_bin : { *(.data.dhrystone_bin) }
   . = 0x80007ffc;
   .data.biosmagic : { *(.data.biosmagic) }
}
