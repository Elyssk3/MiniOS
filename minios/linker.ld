OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)

SECTIONS
{
  . = 1M;

  . = ALIGN(4);
  .text : { *(.multiboot) *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }
  .bss : { *(.bss*) }
}
