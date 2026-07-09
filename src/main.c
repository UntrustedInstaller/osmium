/* src/main.c */
__asm__(".code16gcc\n");

#include "app/types.h"
#include "app/apps.h"
#include "app/fs.h"

uint8_t cur_col = 0x1F;
extern uint8_t boot_drive;
extern void int60_handler(void);

// =====================================================================
//  HARDWARE ABSTRACTION LAYER
// =====================================================================
void outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__ ("outb %0, %1" : : "a"(val), "Nd"(port));
}

uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ __volatile__ ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

void print_str(const char* str) {
    __asm__ __volatile__ ("call asm_print_str" : : "S"(str) : "eax");
}

void print_char(char c) {
    __asm__ __volatile__ ("call asm_print_char" : : "a"(c) : "ebx");
}

static void pal_char(char c, uint8_t color) {
    __asm__ __volatile__ (
        "movb $0x09, %%ah\n\t"
        "movb $0x00, %%bh\n\t"
        "movw $1, %%cx\n\t"
        "int $0x10\n\t"
        "movb $0x03, %%ah\n\t"
        "int $0x10\n\t"
        "incb %%dl\n\t"
        "movb $0x02, %%ah\n\t"
        "int $0x10\n\t"
        :
        : "a"((uint8_t)c), "b"((0x00 << 8) | color)
        : "ecx", "edx", "memory"
    );
}

void print_pal_block(char c1, char c2, uint8_t color) {
    pal_char(c1, color);
    pal_char(c2, color);
}

uint16_t get_key(void) {
    uint16_t key;
    __asm__ __volatile__("movb $0x00, %%ah\n\tint $0x16" : "=a"(key));
    return key;
}

uint16_t get_mem_size(void) {
    uint16_t mem;
    __asm__ __volatile__ ("int $0x12" : "=a"(mem));
    return mem;
}

void clear_screen(void) {
    __asm__ __volatile__ ("call cls" : : : "eax", "ebx", "ecx", "edx");
}

void gotoxy(uint8_t col, uint8_t row) {
    __asm__ __volatile__ (
        "movb $0x02, %%ah\n\tmovb $0x00, %%bh\n\tint $0x10"
        : : "d"((row << 8) | col) : "eax"
    );
}

void get_cursor_rc(uint8_t* row, uint8_t* col) {
    uint16_t pos;
    __asm__ __volatile__ (
        "movb $0x03, %%ah\n\tmovb $0x00, %%bh\n\tint $0x10"
        : "=d"(pos)
        : : "eax", "ecx"
    );
    *col = pos & 0xFF;
    *row = (pos >> 8) & 0xFF;
}

uint8_t read_sector(uint16_t lba, void* buffer) {
    uint16_t result;
    __asm__ __volatile__ (
        "call asm_read_sector"
        : "=a"(result)
        : "a"(lba), "b"(buffer)
        : "ecx", "edx", "memory"
    );
    return (uint8_t)result;
}

uint8_t write_sector(uint16_t lba, void* buffer) {
    uint16_t result;
    __asm__ __volatile__ (
        "call asm_write_sector"
        : "=a"(result)
        : "a"(lba), "b"(buffer)
        : "ecx", "edx", "memory"
    );
    return (uint8_t)result;
}

uint8_t read_sectors(uint16_t lba, uint16_t count, void* buffer) {
    uint16_t result, cnt = count;
    __asm__ __volatile__ (
        "movw %2, %%cx\n\t"
        "call asm_read_sectors"
        : "=a"(result)
        : "a"(lba), "m"(cnt), "b"(buffer)
        : "ecx", "edx", "memory"
    );
    return (uint8_t)result;
}

// =====================================================================
//  MODULE LOADING SYSTEM
// =====================================================================
static void install_api(void) {
    uint16_t off = (uint16_t)(uint32_t)&int60_handler;
    __asm__ __volatile__(
        "cli\n\t"
        "xorw %%ax, %%ax\n\t"
        "movw %%ax, %%es\n\t"
        "movw %0, %%es:0x0180\n\t"
        "movw $0x1000, %%es:0x0182\n\t"
        "sti\n\t"
        :
        : "r"(off)
        : "ax", "memory"
    );
}

static uint8_t mod_buf[MODULE_SECTORS * 512];

static void call_module_buf(const char* args) {
    uint16_t buf_off, args_off;

    buf_off = (uint16_t)(uint32_t)mod_buf;
    uint16_t mod_size = sizeof(mod_buf);
    __asm__ __volatile__(
        "cld\n\t"
        "pushw %%es\n\t"
        "movw $0x2000, %%ax\n\t"
        "movw %%ax, %%es\n\t"
        "xorw %%di, %%di\n\t"
        "movw %0, %%si\n\t"
        "movw %1, %%cx\n\t"
        "rep movsb\n\t"
        "popw %%es\n\t"
        :
        : "r"(buf_off), "r"(mod_size)
        : "ax", "cx", "si", "di", "memory"
    );

    args_off = (uint16_t)(uint32_t)args;
    __asm__ __volatile__(
        "cld\n\t"
        "pushw %%es\n\t"
        "movw $0x2000, %%ax\n\t"
        "movw %%ax, %%es\n\t"
        "movw $0xFC00, %%di\n\t"
        "movw %0, %%si\n\t"
        "1:\n\t"
        "lodsb\n\t"
        "stosb\n\t"
        "testb %%al, %%al\n\t"
        "jnz 1b\n\t"
        "popw %%es\n\t"
        :
        : "r"(args_off)
        : "ax", "si", "di", "memory"
    );

    __asm__ __volatile__("lcall $0x2000, $0x0000" : : : "memory");
}

static int try_load_and_run(const char* name, const char* args);

static void cmd_brainfuck(const char* args) { try_load_and_run("brainfuck", args); }
static void cmd_edit(const char* args)      { try_load_and_run("edit", args); }
static void cmd_basic(const char* args)     { try_load_and_run("basic", args); }
static void cmd_snake(const char* args)     { try_load_and_run("snake", args); }

// =====================================================================
//  MODULE FS API BRIDGE (called from INT 60h CX=9,10)
// =====================================================================
// Called with DS=kernel seg, ES=module seg. We copy data across segments
// using rep movsb: DS:SI → ES:DI (read) or swapped (write).

uint8_t api_fs_read_file(const char* name, uint16_t dest_off, uint16_t max) {
    memset(mod_buf, 0, sizeof(mod_buf));
    if (fs_read_file(name, mod_buf, max)) return 1;

    __asm__ __volatile__ (
        "cld\n\t"
        "pushw %%es\n\t"
        "movw $0x2000, %%ax\n\t"
        "movw %%ax, %%es\n\t"
        "rep movsb\n\t"
        "popw %%es\n\t"
        :
        : "S"((uint16_t)(uint32_t)mod_buf), "D"(dest_off), "c"(max)
        : "ax", "memory"
    );
    return 0;
}

uint8_t api_fs_write_file(const char* name, uint16_t src_off, uint16_t size) {
    uint16_t copy_size = size < sizeof(mod_buf) ? size : sizeof(mod_buf);

    __asm__ __volatile__ (
        "cld\n\t"
        "pushw %%ds\n\t"
        "pushw %%es\n\t"
        "popw %%ds\n\t"           // DS = 0x2000 (module seg — source)
        "popw %%es\n\t"           // ES = 0x1000 (kernel seg — dest)
        "rep movsb\n\t"          // 0x2000:SI → 0x1000:DI
        "pushw %%ds\n\t"
        "pushw %%es\n\t"
        "popw %%ds\n\t"           // DS = 0x1000
        "popw %%es\n\t"           // ES = 0x2000
        :
        : "S"(src_off), "D"((uint16_t)(uint32_t)mod_buf), "c"(copy_size)
        : "memory"
    );

    return fs_write_file(name, mod_buf, size);
}

static int try_load_and_run(const char* name, const char* args) {
    char fname[13];
    int has_dot = 0;

    for (int i = 0; name[i]; i++)
        if (name[i] == '.') { has_dot = 1; break; }

    if (!has_dot) {
        int i;
        for (i = 0; i < 8 && name[i]; i++) fname[i] = name[i];
        fname[i++] = '.'; fname[i++] = 'B'; fname[i++] = 'I'; fname[i++] = 'N';
        fname[i] = '\0';
    } else {
        int i;
        for (i = 0; i < 12 && name[i]; i++) fname[i] = name[i];
        fname[i] = '\0';
    }

    memset(mod_buf, 0, sizeof(mod_buf));
    if (fs_read_file(fname, mod_buf, sizeof(mod_buf))) return 1;
    call_module_buf(args);
    return 0;
}

void cmd_exec(const char* args) {
    if (!args || !args[0]) {
        print_str("ERR: Usage: exec <filename> [args]\r\n");
        return;
    }

    char name[13];
    int i = 0;
    while (args[i] && args[i] != ' ' && i < 12) {
        name[i] = args[i];
        i++;
    }
    name[i] = '\0';

    const char* p = args;
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;

    if (try_load_and_run(name, p)) {
        print_str("ERR: '");
        print_str(name);
        print_str("' not found\r\n");
    }
}

// =====================================================================
//  BOOT CHIME
// =====================================================================
static void play_note(uint16_t freq, uint16_t ms) {
    if (freq < 20) {
        outb(0x61, inb(0x61) & 0xFC);
        return;
    }

    uint16_t div = 1193182 / freq;

    outb(0x43, 0xB6);
    outb(0x42, div & 0xFF);
    outb(0x42, div >> 8);

    outb(0x61, (inb(0x61) & 0xFC) | 0x03);

    uint32_t us = (uint32_t)ms * 1000;
    __asm__ __volatile__ (
        "movb $0x86, %%ah\n\tint $0x15"
        : : "c"((uint16_t)(us >> 16)), "d"((uint16_t)(us & 0xFFFF))
        : "eax"
    );

    outb(0x61, inb(0x61) & 0xFC);
}

static void boot_chime(void) {
    play_note(262, 130);  // C4
    play_note(330, 130);  // E4
    play_note(392, 130);  // G4
    play_note(494, 130);  // B4
    play_note(587, 300);  // D5
}

// =====================================================================
//  UTILITIES & RENDERING
// =====================================================================
int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

int strncmp(const char* s1, const char* s2, int n) {
    while (n > 0 && *s1 && (*s1 == *s2)) { s1++; s2++; n--; }
    if (n == 0) return 0;
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

void print_int(uint16_t val) {
    char buf[6]; int i = 5; buf[i] = '\0';
    if (val == 0) { print_char('0'); return; }
    while (val > 0) { buf[--i] = (val % 10) + '0'; val /= 10; }
    print_str(&buf[i]);
}

void render_pal_mtx(void) {
    static const char hex_chars[] = "0123456789ABCDEF";
    uint8_t old_theme_col = cur_col;
    for (int i = 0; i < 16; i++) {
        uint8_t attrib = (i << 4) | 0x0F;
        cur_col = attrib;
        print_pal_block(hex_chars[i], hex_chars[i], attrib);
    }
    print_pal_block(' ', ' ', old_theme_col);
    cur_col = old_theme_col;
}

void print_hex_byte(uint8_t byte) {
    print_char("0123456789ABCDEF"[(byte>>4) & 0x0F]);
    print_char("0123456789ABCDEF"[byte & 0x0F]);
}

void print_hex_word(uint16_t word) {
    print_hex_byte((word >> 8) & 0xFF);
    print_hex_byte(word & 0xFF);
}

void hexdump(const void* addr, int count) {
    const uint8_t* ptr = (const uint8_t*)addr;
    for (int i = 0; i < count; i += 16) {
        print_hex_word((uint16_t)i);
        print_str(": ");
        for (int j = 0; j < 16; j++) {
            if (i + j < count) { print_hex_byte(ptr[i + j]); print_char(' '); }
            else print_str("   ");
        }
        print_str(" | ");
        for (int j = 0; j < 16; j++) {
            if (i + j < count) {
                uint8_t ch = ptr[i + j];
                if (ch >= 32 && ch <= 126) print_char((char)ch);
                else print_char('.');
            } else print_char(' ');
        }
        print_str("|\r\n");
    }
}

// =====================================================================
//  THEME PERSISTENCE
// =====================================================================
static const uint8_t theme_colors[] = {0x1F, 0x02, 0x06, 0x04, 0x0F};

void load_theme(void) {
    cur_col = 0x1F;

    uint8_t config[8];
    memset(config, 0, sizeof(config));

    if (fs_read_file(CONFIG_FILE, config, sizeof(config))) return;

    uint8_t theme_num = config[0];
    if (theme_num > 4) return;
    cur_col = theme_colors[theme_num];
}

// =====================================================================
//  COMMAND TABLE
// =====================================================================
static const struct cli_command cmd_table[] = {
    // ---- General ----
    {"help",     4, cmd_help,    "List all commands"},
    {"clear",    5, cmd_clear,   "Clear the screen"},
    {"echo",     4, cmd_echo,    "Repeat user input"},
    {"date",     4, cmd_date,    "Show date and time"},
    {"mem",      3, cmd_mem,     "Show available RAM"},
    {"hexdump",  7, cmd_hexdump, "Dump system memory"},
    {"cpuinfo",  7, cmd_cpuinfo, "CPU vendor and features"},
    {"exec",     4, cmd_exec,    "Run .BIN from disk by name"},

    // ---- Files ----
    {"ls",       2, cmd_ls,      "List files on disk"},
    {"cat",      3, cmd_cat,     "View a text file"},
    {"rm",       2, cmd_rm,      "Remove a file"},
    {"mv",       2, cmd_mv,      "Rename a file"},
    {"cp",       2, cmd_cp,      "Copy a file"},

    // ---- Apps ----
    {"palette",  7, cmd_palette, "Show color palette"},
    {"theme",    5, cmd_theme,   "Change color scheme"},
    {"brainfuck", 9, cmd_brainfuck, "Run Brainfuck code"},
    {"edit",     4, cmd_edit,    "Text editor"},
    {"basic",    5, cmd_basic,   "BASIC interpreter"},
    {"snake",    5, cmd_snake,   "Play Snake game"},

    // ---- System ----
    {"reboot",   6, cmd_reboot,  "Reboot the system"},
    {"poweroff", 8, cmd_poweroff,"Shut down the system"},
};

#define CMD_COUNT (sizeof(cmd_table) / sizeof(struct cli_command))

static const uint8_t help_sections[] = {8, 13, 20};

static void more_prompt(int* count) {
    (*count)++;
    if (*count >= 23) {
        print_str("--more--");
        get_key();
        print_str("\r        \r");
        *count = 0;
    }
}

void cmd_help(const char* args) {
    int si = 0, lines = 0;
    print_str("  -- General --\r\n");
    more_prompt(&lines);
    for (int i = 0; i < CMD_COUNT; i++) {
        if (si < 3 && i == help_sections[si]) {
            si++;
            print_str("\r\n  -- ");
            if (si == 1) print_str("Files");
            else if (si == 2) print_str("Apps");
            else if (si == 3) print_str("System");
            print_str(" --\r\n");
            more_prompt(&lines);
            more_prompt(&lines);
        }
        print_str("  ");
        print_str(cmd_table[i].name);
        for (int s = 0; s < (10 - cmd_table[i].name_len); s++) print_char(' ');
        print_str(" - ");
        print_str(cmd_table[i].description);
        print_str("\r\n");
        more_prompt(&lines);
    }
}

// =====================================================================
//  SHELL COMMAND-LINE REDRAW
// =====================================================================
#define HIST_SIZE 5
#define CMD_MAX 63

static void flash_red(void) {
    play_note(1000, 30);

    uint8_t row, col;
    get_cursor_rc(&row, &col);

    uint16_t char_attr;
    __asm__ __volatile__ (
        "movb $0x08, %%ah\n\t"
        "movb $0x00, %%bh\n\t"
        "int $0x10\n\t"
        : "=a"(char_attr)
        :
        : "ebx"
    );

    __asm__ __volatile__ (
        "movb $0x09, %%ah\n\t"
        "movb $0x00, %%bh\n\t"
        "movw $1, %%cx\n\t"
        "int $0x10\n\t"
        :
        : "a"((0x09 << 8) | 0xDB), "b"((0x00 << 8) | 0xCC)
        : "ecx"
    );

    uint32_t us = 30000;
    __asm__ __volatile__ (
        "movb $0x86, %%ah\n\tint $0x15"
        :
        : "c"((uint16_t)(us >> 16)), "d"((uint16_t)(us & 0xFFFF))
        : "eax"
    );

    __asm__ __volatile__ (
        "movb $0x09, %%ah\n\t"
        "movb $0x00, %%bh\n\t"
        "movw $1, %%cx\n\t"
        "int $0x10\n\t"
        :
        : "a"((0x09 << 8) | (char_attr & 0xFF)), "b"((0x00 << 8) | ((char_attr >> 8) & 0xFF))
        : "ecx"
    );
}

static void redraw_cmdline(uint8_t srow, const char* buf, int len, int pos) {
    gotoxy(0, srow);
    print_str("OS:>");
    for (int i = 0; i < len; i++) print_char(buf[i]);

    int remain = 80 - (4 + len);
    if (remain > 0) {
        uint8_t attr = cur_col;
        __asm__ __volatile__ (
            "movb $0x09, %%ah\n\t"
            "movb $0x00, %%bh\n\t"
            "int $0x10\n\t"
            :
            : "a"((0x09 << 8) | ' '), "b"((0x00 << 8) | attr), "c"((uint16_t)remain)
            : "memory"
        );
    }

    gotoxy(4 + pos, srow);
}

static void buf_insert(char* buf, int* len, int* pos, char c) {
    if (*len >= CMD_MAX) return;
    for (int i = *len; i > *pos; i--) buf[i] = buf[i - 1];
    buf[*pos] = c;
    (*len)++;
    (*pos)++;
}

static void buf_delete(char* buf, int* len, int* pos) {
    if (*pos <= 0) return;
    for (int i = *pos - 1; i < *len; i++) buf[i] = buf[i + 1];
    (*len)--;
    (*pos)--;
}

static void buf_delete_at(char* buf, int* len, int* pos) {
    if (*pos >= *len) return;
    for (int i = *pos; i < *len; i++) buf[i] = buf[i + 1];
    (*len)--;
}

// =====================================================================
//  FILESYSTEM COMMANDS
// =====================================================================
void cmd_ls(const char* args) {
    fs_list_dir();
}

void cmd_cat(const char* args) {
    if (!args || args[0] == '\0') {
        print_str("ERR: Usage: cat <filename>\r\n");
        return;
    }

    char buf[513];
    memset(buf, 0, sizeof(buf));

    if (fs_read_file(args, buf, 512)) {
        print_str("ERR: File not found\r\n");
        return;
    }

    print_str(buf);
    print_str("\r\n");
}

void cmd_rm(const char* args) {
    if (!args || args[0] == '\0') {
        print_str("ERR: Usage: rm <filename>\r\n");
        return;
    }

    if (fs_delete_file(args)) {
        print_str("ERR: File not found\r\n");
        return;
    }

    print_str("Deleted.\r\n");
}

void cmd_mv(const char* args) {
    if (!args || args[0] == '\0') {
        print_str("ERR: Usage: mv <source> <dest>\r\n");
        return;
    }

    char src[13], dst[13];
    int i = 0;
    while (args[i] && args[i] != ' ' && i < 12) { src[i] = args[i]; i++; }
    src[i] = '\0';

    if (args[i] != ' ') {
        print_str("ERR: Usage: mv <source> <dest>\r\n");
        return;
    }
    while (args[i] == ' ') i++;

    int j = 0;
    while (args[i] && args[i] != ' ' && j < 12) { dst[j] = args[i]; i++; j++; }
    dst[j] = '\0';

    if (fs_rename(src, dst)) {
        print_str("ERR: '");
        print_str(src);
        print_str("' not found\r\n");
    } else {
        print_str("Renamed.\r\n");
    }
}

void cmd_cp(const char* args) {
    if (!args || args[0] == '\0') {
        print_str("ERR: Usage: cp <source> <dest>\r\n");
        return;
    }

    char src[13], dst[13];
    int i = 0;
    while (args[i] && args[i] != ' ' && i < 12) { src[i] = args[i]; i++; }
    src[i] = '\0';

    if (args[i] != ' ') {
        print_str("ERR: Usage: cp <source> <dest>\r\n");
        return;
    }
    while (args[i] == ' ') i++;

    int j = 0;
    while (args[i] && args[i] != ' ' && j < 12) { dst[j] = args[i]; i++; j++; }
    dst[j] = '\0';

    uint8_t r = fs_copy(src, dst, mod_buf, sizeof(mod_buf));
    if (r == 1) {
        print_str("ERR: '");
        print_str(src);
        print_str("' not found\r\n");
    } else if (r == 2) {
        print_str("ERR: File too large to copy\r\n");
    } else {
        print_str("Copied.\r\n");
    }
}

// =====================================================================
//  IRIDIUM SHELL
// =====================================================================
void iridium_main() {
    install_api();
    boot_chime();
    clear_screen();
    print_str("OsmiumOS\r\n");
    print_int(get_mem_size());
    print_str("KB RAM available.\r\n");

    render_pal_mtx();
    print_str("\r\n");

    load_theme();

    char cmd_buf[CMD_MAX + 1];
    int cmd_idx, cur_pos;

    char history[HIST_SIZE][CMD_MAX + 1];
    int hist_count = 0;
    int hist_next = 0;
    char hist_temp[CMD_MAX + 1];
    int hist_cur = -1;

    while (1) {
        cmd_buf[0] = '\0';
        cmd_idx = 0;
        cur_pos = 0;

        print_str("OS:>");

        uint8_t shell_row, shell_col;
        get_cursor_rc(&shell_row, &shell_col);

        while (1) {
            uint16_t key = get_key();
            uint8_t ascii = key & 0xFF;
            uint8_t scan = (key >> 8) & 0xFF;

            if (ascii == 13) {
                print_str("\r\n");
                cmd_buf[cmd_idx] = '\0';
                break;
            }
            else if (ascii == 8) {
                if (cur_pos > 0) {
                    buf_delete(cmd_buf, &cmd_idx, &cur_pos);
                    redraw_cmdline(shell_row, cmd_buf, cmd_idx, cur_pos);
                }
            }
            else if (ascii == 0) {
                if (scan == 0x48) {
                    if (hist_count == 0) continue;
                    if (hist_cur == -1) {
                        int i = 0;
                        while (i < cmd_idx && i < CMD_MAX) {
                            hist_temp[i] = cmd_buf[i];
                            i++;
                        }
                        hist_temp[i] = '\0';
                        hist_cur = 0;
                    } else if (hist_cur < hist_count - 1) {
                        hist_cur++;
                    } else continue;

                    int idx = (hist_next - 1 - hist_cur + HIST_SIZE) % HIST_SIZE;
                    int i = 0;
                    while (history[idx][i] && i < CMD_MAX) {
                        cmd_buf[i] = history[idx][i];
                        i++;
                    }
                    cmd_idx = i;
                    cur_pos = cmd_idx;
                    cmd_buf[cmd_idx] = '\0';
                    redraw_cmdline(shell_row, cmd_buf, cmd_idx, cur_pos);
                }
                else if (scan == 0x50) {
                    if (hist_cur == -1) continue;
                    if (hist_cur == 0) {
                        hist_cur = -1;
                        int i = 0;
                        while (hist_temp[i] && i < CMD_MAX) {
                            cmd_buf[i] = hist_temp[i];
                            i++;
                        }
                        cmd_idx = i;
                        cur_pos = cmd_idx;
                        cmd_buf[cmd_idx] = '\0';
                        redraw_cmdline(shell_row, cmd_buf, cmd_idx, cur_pos);
                    } else {
                        hist_cur--;
                        int idx = (hist_next - 1 - hist_cur + HIST_SIZE) % HIST_SIZE;
                        int i = 0;
                        while (history[idx][i] && i < CMD_MAX) {
                            cmd_buf[i] = history[idx][i];
                            i++;
                        }
                        cmd_idx = i;
                        cur_pos = cmd_idx;
                        cmd_buf[cmd_idx] = '\0';
                        redraw_cmdline(shell_row, cmd_buf, cmd_idx, cur_pos);
                    }
                }
                else if (scan == 0x4B) {
                    if (cur_pos > 0) {
                        cur_pos--;
                        gotoxy(4 + cur_pos, shell_row);
                    }
                }
                else if (scan == 0x4D) {
                    if (cur_pos < cmd_idx) {
                        cur_pos++;
                        gotoxy(4 + cur_pos, shell_row);
                    }
                }
                else if (scan == 0x47) {
                    cur_pos = 0;
                    gotoxy(4, shell_row);
                }
                else if (scan == 0x4F) {
                    cur_pos = cmd_idx;
                    gotoxy(4 + cur_pos, shell_row);
                }
                else if (scan == 0x53) {
                    if (cur_pos < cmd_idx) {
                        buf_delete_at(cmd_buf, &cmd_idx, &cur_pos);
                        redraw_cmdline(shell_row, cmd_buf, cmd_idx, cur_pos);
                    }
                }
            }
            else if (ascii >= 32 && ascii <= 126) {
                if (cmd_idx < CMD_MAX) {
                    buf_insert(cmd_buf, &cmd_idx, &cur_pos, (char)ascii);
                    redraw_cmdline(shell_row, cmd_buf, cmd_idx, cur_pos);
                } else {
                    flash_red();
                }
            }
        }

        if (cmd_idx == 0) {
            hist_cur = -1;
            continue;
        }

        if (hist_count == 0 || strcmp(cmd_buf, history[(hist_next - 1 + HIST_SIZE) % HIST_SIZE]) != 0) {
            int i = 0;
            while (cmd_buf[i] && i < CMD_MAX) {
                history[hist_next][i] = cmd_buf[i];
                i++;
            }
            history[hist_next][i] = '\0';
            hist_next = (hist_next + 1) % HIST_SIZE;
            if (hist_count < HIST_SIZE) hist_count++;
        }

        int command_executed = 0;
        for (int i = 0; i < CMD_COUNT; i++) {
            uint8_t name_len = cmd_table[i].name_len;

            if (strcmp(cmd_buf, cmd_table[i].name) == 0) {
                cmd_table[i].function("");
                command_executed = 1;
                break;
            }
            else if (strncmp(cmd_buf, cmd_table[i].name, name_len) == 0 && cmd_buf[name_len] == ' ') {
                cmd_table[i].function(cmd_buf + name_len + 1);
                command_executed = 1;
                break;
            }
        }

        if (!command_executed) {
            char modname[13];
            int i = 0;
            while (cmd_buf[i] && cmd_buf[i] != ' ' && i < 12) {
                modname[i] = cmd_buf[i];
                i++;
            }
            modname[i] = '\0';

            const char* p = cmd_buf;
            while (*p && *p != ' ') p++;
            while (*p == ' ') p++;

            if (try_load_and_run(modname, p)) {
                print_str("ERR: Unknown shell command. Type 'help'\r\n");
            }
        }

        hist_cur = -1;
    }
}
