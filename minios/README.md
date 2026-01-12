MiniOS — минимальная мультизагрузочная (Multiboot) ОС

Что в проекте:
- `boot/boot.S` — минимальный multiboot header и точка входа
- `kernel.c` — расширенное ядро: VGA консоль, PS/2 клавиатура (polling), минимальная командная оболочка (shell)
- `linker.ld` — linker script (выставляет начало на 1MiB)
- `Makefile` — сборка `kernel.bin` и `minios.iso` (через `grub-mkrescue`)
- `grub.cfg` — конфиг меню GRUB

Особенности ядра:
- Легкий VGA-консольный вывод (putc/puts/clear/cursor)
- Минимальный `kprintf` (поддерживает %s, %d, %u, %x, %c)
- PS/2 keyboard IRQ-driven ввод: scancode → ASCII (ring buffer)
- Простая оболочка (terminal) с командами: `help`, `clear`, `echo`, `version`
- Встроенная простая in-memory файловая система и команды: `ls`, `cat <file>`, `write <file> <text>`, `touch <file>`, `rm <file>`
- Простой текстовый редактор `nano <file>` (линейный, append-only, команды внутри редактора: `.help`, `.save`, `.wq`, `.quit`)

Сборка (рекомендуется выполнять в WSL/Ubuntu):
1) Установите зависимости (Debian/Ubuntu):
   sudo apt update && sudo apt install build-essential gcc-multilib nasm xorriso grub-pc-bin

2) Кросс-компилятор не обязателен — можно использовать системный gcc с `-m32`.
   Убедитесь, что установлены пакеты `gcc-multilib` и `libc6-dev-i386`.

3) Скомпилировать и собрать ISO:
   make
   make iso

4) Запуск в VirtualBox:
   - Создайте новую VM (тип: Other Linux), RAM 16-64MB достаточно
   - Подключите `minios.iso` как оптический диск и загрузитесь

Быстрая проверка в QEMU (альтернатива VirtualBox):
   qemu-system-i386 -cdrom minios.iso -m 64M

Отладка и примечания:
- Если экран пустой, убедитесь, что вы собрали `kernel.bin` без ошибок и что ISO создан корректно.
- Клавиатура использует PS/2 polling; в VirtualBox/QEMU это работает по умолчанию.
- Со временем можно добавить обработчики прерываний, таймер, драйверы, управление памятью и т.д.

License: Public domain (use and modify freely)

