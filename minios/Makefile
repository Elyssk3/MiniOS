# Minimal Makefile for building a multiboot kernel and ISO

# You can use i686-elf-gcc cross-compiler or system gcc with -m32 (on Linux)
CC ?= i686-elf-gcc
AS ?= i686-elf-as
LD ?= i686-elf-ld

CFLAGS = -m32 -ffreestanding -O2 -Wall -Wextra
LDFLAGS = -m elf_i386 -T linker.ld -nostdlib

.PHONY: all clean iso
all: kernel.bin

boot/boot.o: boot/boot.S
	$(AS) -32 -o $@ $^

boot/irq.o: boot/irq.S
	$(AS) -32 -o $@ $^

kernel.o: kernel.c
	$(CC) $(CFLAGS) -c -o $@ $^

kernel.bin: boot/boot.o boot/irq.o kernel.o
	$(LD) $(LDFLAGS) -o $@ $^

iso: kernel.bin grub.cfg
	mkdir -p iso/boot/grub
	cp kernel.bin iso/boot/kernel.bin
	cp grub.cfg iso/boot/grub/
	grub-mkrescue -o minios.iso iso

# Quick run (requires qemu-system-i386 installed)
run: iso
	qemu-system-i386 -cdrom minios.iso -m 64M

clean:
	rm -f *.bin *.o
	rm -rf iso minios.iso
