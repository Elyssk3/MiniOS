/* IRQ1 (keyboard) entry stub */

.text
.code32
.global irq1_entry
.type irq1_entry, @function
irq1_entry:
    pusha
    push %ds
    push %es
    /* set data segments to kernel data selector (0x10) */
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    /* call C handler */
    call keyboard_handler
    pop %es
    pop %ds
    popa
    /* send EOI to master PIC */
    movb $0x20, %al
    outb %al, $0x20
    iret
