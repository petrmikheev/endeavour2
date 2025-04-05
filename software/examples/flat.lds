ENTRY(_start)

SECTIONS
{
   . = 0x80008000;
   .text.startup : { *(.text.startup) }
   .text : { *(.text) }
   . = ALIGN(4);
   PROVIDE( __global_pointer$ = . + 0x800 );
   .data : { *(.data) }
   .sbss : { *(.sbss) }
   .rodata : { *(.rodata) }
   .bss : { *(.bss) }
   . = ALIGN(16);
   PROVIDE( __stack_pointer$ = . + 8192 );
}
