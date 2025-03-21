ENTRY(_start)

SECTIONS
{
   . = 0x80000000;
   .text.startup : { *(.text.startup) }
   .text : { *(.text) }
   PROVIDE( __global_pointer$ = . + 0x800 );
   .data : { *(.data) }
   .bss : { *(.bss) }
   . = 0x80007ffc;
   .data.biosmagic : { *(.data.biosmagic) }
}
