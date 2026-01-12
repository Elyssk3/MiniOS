; Minimal Multiboot header + entry point

.section .multiboot
  .align 4
  .long 0x1BADB002        # magic
  .long 0x0                # flags
  .long -(0x1BADB002 + 0x0)

.text
.global _start
_start:
  call kernel_main
.hang:
  jmp .hang
