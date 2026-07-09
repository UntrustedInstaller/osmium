__asm__(".code16gcc\n");
#include "api.h"

#define EDITOR_SECTORS 8
#define EDITOR_MAX_SIZE (EDITOR_SECTORS * 512)
#define EDITOR_FILE "EDITOR.TXT"

static char ed_buf[EDITOR_MAX_SIZE];
static int ed_size;
static int ed_cursor;
static int ed_scroll;
static int ed_modified;
static const char* ed_filename;

static void* my_memset(void* s, int c, int n) {
    uint8_t* p = (uint8_t*)s;
    while (n--) *p++ = (uint8_t)c;
    return s;
}

static int line_start(int pos) {
    while (pos > 0 && ed_buf[pos - 1] != '\n') pos--;
    return pos;
}

static int line_end(int pos) {
    while (pos < ed_size && ed_buf[pos] != '\n') pos++;
    return pos;
}

static void clear_line_remainder(int start_col) {
    if (start_col >= 80) return;
    int count = 80 - start_col;
    uint8_t col = get_cur_col();
    __asm__ __volatile__ (
        "movw %0, %%cx\n\t"
        "movb $0x09, %%ah\n\t"
        "movb $0x00, %%bh\n\t"
        "int $0x10\n\t"
        :
        : "g"((uint16_t)count), "a"((0x09 << 8) | ' '), "b"((uint16_t)(uint8_t)col)
        : "cx", "dx", "memory"
    );
}

static int ensure_visible(void) {
    int old_scroll = ed_scroll;
    for (int limit = 0; limit < 100; limit++) {
        int line = 2;
        int p = ed_scroll;
        while (p < ed_cursor && p < ed_size) {
            if (ed_buf[p] == '\n') line++;
            p++;
        }
        if (line < 2 && ed_scroll > 0) {
            int ns = ed_scroll;
            while (ns > 0 && ed_buf[ns - 1] != '\n') ns--;
            if (ns > 0) ns--;
            ed_scroll = ns;
        } else if (line >= 24) {
            if (ed_scroll >= ed_size) break;
            while (ed_scroll < ed_size && ed_buf[ed_scroll] != '\n') ed_scroll++;
            if (ed_scroll < ed_size) ed_scroll++;
        } else break;
    }
    return ed_scroll != old_scroll;
}

static void ed_render_line(int r, int* p) {
    gotoxy(0, r);
    int c = 0;
    while (*p < ed_size && ed_buf[*p] != '\n' && c < 80) {
        print_char(ed_buf[*p]); (*p)++; c++;
    }
    clear_line_remainder(c);
    if (*p < ed_size && ed_buf[*p] == '\n') (*p)++;
}

static void ed_render_status(void) {
    gotoxy(0, 0);
    print_str("TEXT EDITOR");
    if (ed_modified) {
        gotoxy(28, 0);
        print_str("[Modified]");
    }

    int ln = 1, ls = 0;
    for (int i = 0; i < ed_cursor && i < ed_size; i++) {
        if (ed_buf[i] == '\n') { ln++; ls = i + 1; }
    }
    gotoxy(52, 0);
    print_str("Ln: ");
    print_int(ln);
    gotoxy(62, 0);
    print_str("Col: ");
    print_int(ed_cursor - ls + 1);

    gotoxy(0, 24);
    print_str("F2=Save  Ctrl+Q=Quit  INS");
}

static void ed_set_cursor(void) {
    int sy = 2;
    int sp = ed_scroll;
    while (sp < ed_cursor && sp < ed_size) {
        if (ed_buf[sp] == '\n') sy++;
        sp++;
    }
    int sx = ed_cursor - line_start(ed_cursor);
    if (sx > 79) sx = 79;
    if (sy > 23) sy = 23;
    gotoxy(sx, sy);
}

static void ed_render_all(void) {
    int p = ed_scroll;
    for (int r = 2; r < 24; r++) ed_render_line(r, &p);
    ed_render_status();
    ed_set_cursor();
}

static void ed_render_from(int pos) {
    while (pos > 0 && ed_buf[pos - 1] != '\n') pos--;

    int p = ed_scroll;
    int start_row = -1;
    for (int r = 2; r < 24; r++) {
        int le = p;
        while (le < ed_size && ed_buf[le] != '\n') le++;
        if (pos == p || (pos > p && pos <= le)) { start_row = r; break; }
        p = le + 1;
        if (p >= ed_size) break;
    }
    if (start_row < 0) { ed_render_all(); return; }

    p = pos;
    for (int r = start_row; r < 24; r++) ed_render_line(r, &p);
    ed_render_status();
    ed_set_cursor();
}

static void ed_insert(char c) {
    if (ed_size >= EDITOR_MAX_SIZE - 1) return;
    for (int i = ed_size; i > ed_cursor; i--) ed_buf[i] = ed_buf[i - 1];
    ed_buf[ed_cursor] = c;
    ed_size++;
    ed_cursor++;
    ed_modified = 1;
}

static void ed_backspace(void) {
    if (ed_cursor <= 0) return;
    for (int i = ed_cursor - 1; i < ed_size; i++) ed_buf[i] = ed_buf[i + 1];
    ed_size--;
    ed_cursor--;
    ed_modified = 1;
}

static void ed_delete(void) {
    if (ed_cursor >= ed_size) return;
    for (int i = ed_cursor; i < ed_size; i++) ed_buf[i] = ed_buf[i + 1];
    ed_size--;
    ed_modified = 1;
}

static void cur_left(void) {
    if (ed_cursor > 0) ed_cursor--;
}

static void cur_right(void) {
    if (ed_cursor < ed_size) ed_cursor++;
}

static void cur_up(void) {
    if (ed_cursor <= 0) { ed_cursor = 0; return; }
    int cs = line_start(ed_cursor);
    if (cs == 0) { ed_cursor = 0; return; }
    int ps = line_start(cs - 1);
    int col = ed_cursor - cs;
    ed_cursor = ps + col;
    int pe = line_end(ps);
    if (ed_cursor > pe) ed_cursor = pe;
}

static void cur_down(void) {
    if (ed_cursor >= ed_size) { ed_cursor = ed_size; return; }
    int ce = line_end(ed_cursor);
    if (ce >= ed_size) { ed_cursor = ed_size; return; }
    int ns = ce + 1;
    int cs = line_start(ed_cursor);
    int col = ed_cursor - cs;
    ed_cursor = ns + col;
    int ne = line_end(ns);
    if (ed_cursor > ne) ed_cursor = ne;
}

static void ed_load(const char* fname) {
    ed_size = 0;
    if (fs_read_file(fname, (uint8_t*)ed_buf, EDITOR_MAX_SIZE)) return;
    while (ed_size < EDITOR_MAX_SIZE && ed_buf[ed_size]) ed_size++;
}

static void ed_save(const char* fname) {
    ed_buf[ed_size] = '\0';
    if (fs_write_file(fname, (uint8_t*)ed_buf, (uint32_t)ed_size)) {
        print_str("ERR: Failed to save file\r\n");
    }
}

void module_main(void) {
    const char* args = MODULE_ARGS;

    clear_screen();

    for (int i = 0; i < EDITOR_MAX_SIZE; i++) ed_buf[i] = 0;
    ed_size = 0;
    ed_cursor = 0;
    ed_scroll = 0;
    ed_modified = 0;

    ed_filename = (args && args[0]) ? args : EDITOR_FILE;
    ed_load(ed_filename);

    ed_render_all();

    while (1) {
        uint16_t key = get_key();
        uint8_t ascii = key & 0xFF;
        uint8_t scan = (key >> 8) & 0xFF;

        if (ascii == 19 || (ascii == 0 && scan == 0x3C)) {
            ed_save(ed_filename);
            ed_modified = 0;
            ed_render_status();
            ed_set_cursor();
        } else if (ascii == 17 || ascii == 27) {
            break;
        } else if (ascii == 13 || ascii == 10) {
            int render_pos = line_start(ed_cursor);
            ed_insert('\n');
            if (ensure_visible()) { ed_render_all(); }
            else { ed_render_from(render_pos); }
        } else if (ascii == 8) {
            int render_pos = ed_cursor > 0 ? line_start(ed_cursor - 1) : 0;
            ed_backspace();
            if (ensure_visible()) { ed_render_all(); }
            else { ed_render_from(render_pos); }
        } else if (ascii == 0) {
            if (scan == 0x48) { cur_up(); ed_set_cursor(); }
            else if (scan == 0x50) { cur_down(); ed_set_cursor(); }
            else if (scan == 0x4B) { cur_left(); ed_set_cursor(); }
            else if (scan == 0x4D) { cur_right(); ed_set_cursor(); }
            else if (scan == 0x47) { ed_cursor = line_start(ed_cursor); ed_set_cursor(); }
            else if (scan == 0x4F) { ed_cursor = line_end(ed_cursor); ed_set_cursor(); }
            else if (scan == 0x53) {
                int render_pos = line_start(ed_cursor);
                ed_delete();
                if (ensure_visible()) { ed_render_all(); }
                else { ed_render_from(render_pos); }
            }
        } else if (ascii >= 32 && ascii <= 126) {
            int render_pos = line_start(ed_cursor);
            ed_insert((char)ascii);
            if (ensure_visible()) { ed_render_all(); }
            else { ed_render_from(render_pos); }
        }
    }

    clear_screen();
}
