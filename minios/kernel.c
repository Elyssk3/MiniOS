/* MiniOS kernel: VGA console, keyboard polling, minimal shell */

typedef unsigned int uint32_t;
typedef int int32_t;
typedef unsigned short uint16_t;
typedef unsigned char uint8_t;

/* I/O ports */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* VGA text mode */
enum { VGA_WIDTH = 80, VGA_HEIGHT = 25 };
static uint16_t *vga_buffer = (uint16_t *)0xB8000;
static uint8_t term_row = 0;
static uint8_t term_col = 0;
static uint8_t term_color = 0x07; /* light gray on black */

static void update_cursor(void) {
    uint16_t pos = term_row * VGA_WIDTH + term_col;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static void vga_putat(char c, uint8_t color, uint8_t row, uint8_t col) {
    const uint16_t idx = row * VGA_WIDTH + col;
    vga_buffer[idx] = (uint16_t)c | ((uint16_t)color << 8);
}

static void vga_scroll(void) {
    for (int r = 0; r < VGA_HEIGHT - 1; ++r) {
        for (int c = 0; c < VGA_WIDTH; ++c) {
            vga_buffer[r * VGA_WIDTH + c] = vga_buffer[(r + 1) * VGA_WIDTH + c];
        }
    }
    /* clear last line */
    const uint16_t blank = (uint16_t)' ' | ((uint16_t)term_color << 8);
    for (int c = 0; c < VGA_WIDTH; ++c) vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + c] = blank;
}

static void vga_putc(char c) {
    if (c == '\n') {
        term_col = 0;
        if (++term_row == VGA_HEIGHT) { term_row = VGA_HEIGHT - 1; vga_scroll(); }
    } else if (c == '\r') {
        term_col = 0;
    } else if (c == '\b') {
        if (term_col > 0) {
            --term_col;
            vga_putat(' ', term_color, term_row, term_col);
        }
    } else {
        vga_putat(c, term_color, term_row, term_col);
        if (++term_col >= VGA_WIDTH) { term_col = 0; if (++term_row == VGA_HEIGHT) { term_row = VGA_HEIGHT - 1; vga_scroll(); } }
    }
    update_cursor();
}

static void vga_puts(const char *s) {
    for (int i = 0; s[i]; ++i) vga_putc(s[i]);
}

static void vga_clear(void) {
    const uint16_t blank = (uint16_t)' ' | ((uint16_t)term_color << 8);
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; ++i) vga_buffer[i] = blank;
    term_row = term_col = 0;
    update_cursor();
}

/* Minimal integer -> string helpers */
static void kputu(uint32_t val, int base) {
    char buf[33]; int i = 0;
    if (val == 0) { vga_putc('0'); return; }
    while (val) {
        uint32_t d = val % base;
        buf[i++] = (d < 10) ? ('0' + d) : ('a' + d - 10);
        val /= base;
    }
    while (i--) vga_putc(buf[i]);
}

static void kputi(int32_t val, int base) {
    if (val < 0) { vga_putc('-'); kputu((uint32_t)(-val), base); }
    else kputu((uint32_t)val, base);
}

/* minimal printf: supports %s, %d, %u, %x, %c */
static void kprintf(const char *fmt, ... ) {
    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    for (int i = 0; fmt[i]; ++i) {
        if (fmt[i] != '%') { vga_putc(fmt[i]); continue; }
        ++i;
        char f = fmt[i];
        if (f == 's') { const char *s = __builtin_va_arg(args, const char*); vga_puts(s ? s : "(null)"); }
        else if (f == 'd') { kputi(__builtin_va_arg(args, int), 10); }
        else if (f == 'u') { kputu(__builtin_va_arg(args, unsigned int), 10); }
        else if (f == 'x') { kputu(__builtin_va_arg(args, unsigned int), 16); }
        else if (f == 'c') { char c = (char)__builtin_va_arg(args, int); vga_putc(c); }
        else { vga_putc('%'); vga_putc(f); }
    }
    __builtin_va_end(args);
}

/* Simple scancode -> ASCII (set 1) for main keys. Non-exhaustive. */
static const char scancode_map[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=','\b', /* 0x0 */
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n', 0, /* 0x10 */
    'a','s','d','f','g','h','j','k','l',';','\'', '`', 0,'\\','z','x', /* 0x20 */
    'c','v','b','n','m',',','.','/', 0,'*', 0,' ', /* 0x30 */
};

/* Keyboard buffer for IRQ-driven input */
#define KBUF_SIZE 256
static volatile char kbuf[KBUF_SIZE];
static volatile int kbuf_head = 0;
static volatile int kbuf_tail = 0;

/* Forward declaration for assembly stub */
extern void irq1_entry(void);

/* PIC remap and IDT setup (minimal) */
struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t flags;
    uint16_t offset_high;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr idtp;

static void idt_set_gate(uint8_t n, uint32_t handler) {
    idt[n].offset_low = handler & 0xFFFF;
    idt[n].selector = 0x08; /* kernel code segment */
    idt[n].zero = 0;
    idt[n].flags = 0x8E; /* present, ring0, 32-bit interrupt gate */
    idt[n].offset_high = (handler >> 16) & 0xFFFF;
}

static void idt_init(void) {
    for (int i = 0; i < 256; ++i) {
        idt[i].offset_low = 0;
        idt[i].selector = 0;
        idt[i].zero = 0;
        idt[i].flags = 0;
        idt[i].offset_high = 0;
    }
    idtp.limit = sizeof(idt) - 1;
    idtp.base = (uint32_t)&idt;
}

static void idt_load(void) {
    __asm__ volatile ("lidt (%0)" : : "r"(&idtp));
}

static void pic_remap(void) {
    uint8_t a1 = inb(0x21);
    uint8_t a2 = inb(0xA1);

    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    outb(0x21, 4);
    outb(0xA1, 2);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);

    outb(0x21, a1);
    outb(0xA1, a2);
}

static void pic_unmask_keyboard(void) {
    uint8_t mask = inb(0x21);
    mask &= ~(1 << 1); /* clear bit 1 (IRQ1 keyboard) */
    outb(0x21, mask);
}

/* Called from assembly stub (irq1_entry) */
void keyboard_handler(void) {
    uint8_t sc = inb(0x60);
    /* ignore key release */
    if (sc & 0x80) return;
    char c = 0;
    if (sc < sizeof(scancode_map)) c = scancode_map[sc];
    if (!c) return;
    int next = (kbuf_head + 1) % KBUF_SIZE;
    if (next != kbuf_tail) { kbuf[kbuf_head] = c; kbuf_head = next; }
}

/* IRQ-based getchar: blocks (busy-wait) until character available */
static char keyboard_getchar_irq(void) {
    while (kbuf_head == kbuf_tail) {}
    char c = kbuf[kbuf_tail];
    kbuf_tail = (kbuf_tail + 1) % KBUF_SIZE;
    return c;
}

/* Simple line reader (uses IRQ-driven getchar) */
#define INPUT_BUF 128
static void read_line(char *buf, int bufsize) {
    int idx = 0;
    while (1) {
        char c = keyboard_getchar_irq();
        if (c == '\n' || c == '\r') { vga_putc('\n'); buf[idx] = '\0'; return; }
        else if (c == '\b') {
            if (idx > 0) { --idx; vga_putc('\b'); }
        } else {
            if (idx < bufsize - 1) { buf[idx++] = c; vga_putc(c); }
        }
    }
}

/* --- Tiny in-memory filesystem --- */
#define MAX_FILES 16
#define MAX_NAME 16
#define MAX_FILE_SIZE 512

struct file_entry {
    char name[MAX_NAME];
    int used;
    int size;
    char data[MAX_FILE_SIZE];
};

static struct file_entry files[MAX_FILES];

static void fs_init(void) {
    for (int i = 0; i < MAX_FILES; ++i) files[i].used = 0;
    /* create a welcome file */
    const char *w = "welcome: This is MiniOS (in-memory FS)\n";
    int idx = -1;
    for (int i = 0; i < MAX_FILES; ++i) if (!files[i].used) { idx = i; break; }
    if (idx >= 0) {
        files[idx].used = 1;
        int n = 0; while (w[n] && n < MAX_FILE_SIZE) { files[idx].data[n] = w[n]; ++n; }
        files[idx].size = n;
        /* name it "welcome" */
        int j; for (j = 0; j < MAX_NAME - 1 && "welcome"[j]; ++j) files[idx].name[j] = "welcome"[j];
        files[idx].name[j] = '\0';
    }
}

static int fs_find(const char *name) {
    for (int i = 0; i < MAX_FILES; ++i) if (files[i].used) {
        int k = 0; while (k < MAX_NAME && files[i].name[k] && name[k] && files[i].name[k] == name[k]) ++k;
        if (files[i].name[k] == '\0' && name[k] == '\0') return i;
    }
    return -1;
}

static int fs_create(const char *name) {
    if (fs_find(name) >= 0) return -1; /* already exists */
    for (int i = 0; i < MAX_FILES; ++i) if (!files[i].used) {
        files[i].used = 1; files[i].size = 0; int j=0; while (j < MAX_NAME - 1 && name[j]) { files[i].name[j] = name[j]; ++j; } files[i].name[j] = '\0'; return i;
    }
    return -1; /* no space */
}

static int fs_write(const char *name, const char *data, int len) {
    int idx = fs_find(name);
    if (idx < 0) idx = fs_create(name);
    if (idx < 0) return -1;
    int n = 0; while (n < len && n < MAX_FILE_SIZE) { files[idx].data[n] = data[n]; ++n; }
    files[idx].size = n; return n;
}

static int fs_read_to_console(const char *name) {
    int idx = fs_find(name);
    if (idx < 0) return -1;
    for (int i = 0; i < files[idx].size; ++i) vga_putc(files[idx].data[i]);
    return files[idx].size;
}

static void fs_list(void) {
    kprintf("Files:\n");
    for (int i = 0; i < MAX_FILES; ++i) if (files[i].used) {
        kprintf("  %s (%d bytes)\n", files[i].name, files[i].size);
    }
}

static int fs_remove(const char *name) {
    int idx = fs_find(name);
    if (idx < 0) return -1;
    files[idx].used = 0; return 0;
}

/* Simple nano-like line editor (very small): append-only editor. Commands start with '.' */
static void nano_edit(const char *filename) {
    char buf[MAX_FILE_SIZE];
    int len = 0;
    int idx = fs_find(filename);
    if (idx >= 0) {
        len = files[idx].size;
        if (len > MAX_FILE_SIZE) len = MAX_FILE_SIZE;
        for (int i = 0; i < len; ++i) buf[i] = files[idx].data[i];
    }
    kprintf("--- nano: editing %s (max %d bytes) ---\n", filename, MAX_FILE_SIZE);
    kprintf("Commands: .help .save .wq .quit\n");
    if (len > 0) {
        kprintf("--- current contents ---\n");
        for (int i = 0; i < len; ++i) vga_putc(buf[i]);
        kprintf("--- end ---\n");
    }

    char line[INPUT_BUF];
    while (1) {
        kprintf("edit> ");
        read_line(line, INPUT_BUF);
        if (line[0] == '\0') continue;
        if (line[0] == '.') {
            /* command */
            if (line[1] == '\0') continue;
            if (line[1] == 'h' && line[2] == 'e' && line[3] == 'l' && line[4] == 'p' && line[5] == '\0') {
                kprintf("Editor commands:\n");
                kprintf("  .help - show this message\n");
                kprintf("  .save - save to file\n");
                kprintf("  .wq   - write and quit\n");
                kprintf("  .quit - quit without saving\n");
                continue;
            }
            if (line[1] == 's' && line[2] == 'a' && line[3] == 'v' && line[4] == 'e' && line[5] == '\0') {
                int written = fs_write(filename, buf, len);
                if (written < 0) kprintf("Save failed\n"); else kprintf("Saved %d bytes\n", written);
                continue;
            }
            if (line[1] == 'w' && line[2] == 'q' && line[3] == '\0') {
                int written = fs_write(filename, buf, len);
                if (written < 0) kprintf("Save failed\n"); else kprintf("Saved %d bytes\n", written);
                break;
            }
            if (line[1] == 'q' && line[2] == '\0') { kprintf("Quit without saving\n"); break; }
            kprintf("Unknown editor command: %s\n", line);
            continue;
        }
        /* append line and newline */
        int i = 0;
        while (line[i]) {
            if (len < MAX_FILE_SIZE) buf[len++] = line[i++]; else { kprintf("Buffer full\n"); break; }
        }
        if (len < MAX_FILE_SIZE) { buf[len++] = '\n'; } else { kprintf("Buffer full, no newline\n"); }
    }

    kprintf("Exiting editor\n");
}

/* helper: skip leading spaces */
static char *skip_spaces(char *s) { while (*s == ' ') ++s; return s; }

/* command runner */
static void run_command(char *line) {
    char *p = skip_spaces(line);
    if (p[0] == '\0') return;
    if (p[0]=='h' && p[1]=='e' && p[2]=='l' && p[3]=='p' && p[4]=='\0') {
        kprintf("Available commands:\n");
        kprintf("  help           - show this message\n");
        kprintf("  clear          - clear the screen\n");
        kprintf("  echo <text>    - echo text\n");
        kprintf("  version        - show kernel version\n");
        kprintf("  ls             - list files\n");
        kprintf("  cat <file>     - show file contents\n");
        kprintf("  write <file> <text> - write text to file (overwrite)\n");
        kprintf("  touch <file>   - create empty file\n");
        kprintf("  rm <file>      - remove file\n");
        kprintf("  nano <file>    - edit/create a file with simple editor\n");
        return;
    }
    /* nano editor: nano <file> */
    if (p[0]=='n' && p[1]=='a' && p[2]=='n' && p[3]=='o' && (p[4]=='\0' || p[4]==' ')) { char *arg = skip_spaces(p+4); if (*arg) { nano_edit(arg); } else kprintf("Usage: nano <file>\n"); return; }
    if (p[0]=='c' && p[1]=='l' && p[2]=='e' && p[3]=='a' && p[4]=='r' && (p[5]=='\0' || p[5]==' ')) { vga_clear(); return; }
    if (p[0]=='v' && p[1]=='e' && p[2]=='r' && p[3]=='s' && p[4]=='i' && p[5]=='o' && p[6]=='n' && (p[7]=='\0' || p[7]==' ')) { kprintf("MiniOS version 0.2\n"); return; }
    if (p[0]=='e' && p[1]=='c' && p[2]=='h' && p[3]=='o' && (p[4]=='\0' || p[4]==' ')) {
        char *arg = skip_spaces(p+4); kprintf("%s\n", arg); return; }
    /* ls */
    if (p[0]=='l' && p[1]=='s' && (p[2]=='\0' || p[2]==' ')) { fs_list(); return; }
    /* cat */
    if (p[0]=='c' && p[1]=='a' && p[2]=='t' && p[3]==' '){ char *arg = skip_spaces(p+4); if (*arg) { if (fs_read_to_console(arg) < 0) kprintf("No such file: %s\n", arg); else kprintf("\n"); } else kprintf("Usage: cat <file>\n"); return; }
    /* touch */
    if (p[0]=='t' && p[1]=='o' && p[2]=='u' && p[3]=='c' && p[4]=='h' && (p[5]==' ')) { char *arg = skip_spaces(p+6); if (*arg) { if (fs_create(arg) < 0) kprintf("Cannot create file: %s\n", arg); } else kprintf("Usage: touch <file>\n"); return; }
    /* rm */
    if (p[0]=='r' && p[1]=='m' && p[2]==' '){ char *arg = skip_spaces(p+3); if (*arg) { if (fs_remove(arg) < 0) kprintf("No such file: %s\n", arg); } else kprintf("Usage: rm <file>\n"); return; }
    /* write */
    if (p[0]=='w' && p[1]=='r' && p[2]=='i' && p[3]=='t' && p[4]=='e' && p[5]==' '){
        char *arg = skip_spaces(p+6);
        if (!*arg) { kprintf("Usage: write <file> <text>\n"); return; }
        /* file name is first token */
        char fname[MAX_NAME]; int fi = 0;
        while (*arg && *arg != ' ' && fi < MAX_NAME - 1) { fname[fi++] = *arg++; }
        fname[fi] = '\0';
        arg = skip_spaces(arg);
        if (!*fname) { kprintf("Invalid file name\n"); return; }
        if (!*arg) { kprintf("No text provided\n"); return; }
        int len = 0; while (arg[len]) ++len;
        int written = fs_write(fname, arg, len);
        if (written < 0) kprintf("Failed to write file\n"); else kprintf("Wrote %d bytes to %s\n", written, fname);
        return;
    }
    kprintf("Unknown command: %s\n", p);
}

/* Install PIC and IDT for keyboard IRQ */
static void interrupts_install(void) {
    /* reset keyboard buffer */
    kbuf_head = kbuf_tail = 0;
    pic_remap();
    idt_init();
    idt_set_gate(0x21, (uint32_t)irq1_entry);
    idt_load();
    pic_unmask_keyboard();
    /* enable interrupts */
    __asm__ volatile ("sti");
}

void kernel_main(void) {
    vga_clear();
    interrupts_install();
    fs_init();
    kprintf("MiniOS v0.3 - terminal + tiny FS\n");
    kprintf("Type 'help' for commands.\n\n");

    char line[INPUT_BUF];
    for (;;) {
        kprintf("mini> ");
        read_line(line, INPUT_BUF);
        if (line[0] == '\0') continue;
        run_command(line);
    }
}

