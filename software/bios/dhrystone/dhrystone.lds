ENTRY(_start)

SECTIONS
{
   . = 0x80007400;
   .text.startup : { *(.text.startup) }
   .text : { *(.text) }
   PROVIDE( __global_pointer$ = . + 0x800 );
   .data : { *(.data) }
   . = 0x80008000;
   .bss : { *(.bss) }
}
