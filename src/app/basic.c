__asm__(".code16gcc\n");
#include "api.h"

#define MAX_PROG 2048
#define MAX_LINE 256
#define NUM_VARS 26

typedef short int16_t;
static char program[MAX_PROG];
static char load_buf[4096];
static int prog_len;
static int16_t vars[NUM_VARS];
static int ip;
static int stop_flag;

static void* my_memset(void* s, int c, int n) {
    uint8_t* p = (uint8_t*)s;
    while (n--) *p++ = (uint8_t)c;
    return s;
}

static int next_line(int pos) {
    pos += 2;
    while (pos < prog_len && program[pos]) pos++;
    if (pos < prog_len) pos++;
    return pos;
}

static int find_line_num(int num) {
    int pos = 0;
    while (pos < prog_len) {
        uint16_t ln = (uint8_t)program[pos] << 8 | (uint8_t)program[pos + 1];
        if (ln == num) return pos;
        if (ln > num) return -1;
        pos = next_line(pos);
    }
    return -1;
}

static int add_line(int num, const char* text) {
    int pos = 0;
    while (pos < prog_len) {
        uint16_t ln = (uint8_t)program[pos] << 8 | (uint8_t)program[pos + 1];
        if (ln == num) {
            int end = next_line(pos);
            int rest = prog_len - end;
            int tlen = 0;
            while (text[tlen]) tlen++;
            int new_len = 2 + tlen + 1;
            int delta = new_len - (end - pos);
            if (delta > 0 && prog_len + delta > MAX_PROG) return 1;
            for (int i = 0; i < rest; i++) program[pos + new_len + i] = program[end + i];
            program[pos] = (num >> 8) & 0xFF;
            program[pos + 1] = num & 0xFF;
            for (int i = 0; i < tlen; i++) program[pos + 2 + i] = text[i];
            program[pos + 2 + tlen] = '\0';
            prog_len += delta;
            return 0;
        }
        if (ln > num) {
            int rest = prog_len - pos;
            int tlen = 0;
            while (text[tlen]) tlen++;
            int new_len = 2 + tlen + 1;
            if (prog_len + new_len > MAX_PROG) return 1;
            for (int i = rest - 1; i >= 0; i--) program[pos + new_len + i] = program[pos + i];
            program[pos] = (num >> 8) & 0xFF;
            program[pos + 1] = num & 0xFF;
            for (int i = 0; i < tlen; i++) program[pos + 2 + i] = text[i];
            program[pos + 2 + tlen] = '\0';
            prog_len += new_len;
            return 0;
        }
        pos = next_line(pos);
    }
    int tlen = 0;
    while (text[tlen]) tlen++;
    int new_len = 2 + tlen + 1;
    if (prog_len + new_len > MAX_PROG) return 1;
    program[pos] = (num >> 8) & 0xFF;
    program[pos + 1] = num & 0xFF;
    for (int i = 0; i < tlen; i++) program[pos + 2 + i] = text[i];
    program[pos + 2 + tlen] = '\0';
    prog_len += new_len;
    return 0;
}

static void del_line(int num) {
    int pos = find_line_num(num);
    if (pos < 0) return;
    int end = next_line(pos);
    int rest = prog_len - end;
    for (int i = 0; i < rest; i++) program[pos + i] = program[end + i];
    prog_len -= (end - pos);
}

static void clear_prog(void) {
    prog_len = 0;
    for (int i = 0; i < NUM_VARS; i++) vars[i] = 0;
    ip = 0;
    stop_flag = 0;
}

static int is_digit(char c) { return c >= '0' && c <= '9'; }
static int is_letter(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
static char to_upper(char c) { return (c >= 'a' && c <= 'z') ? c - 0x20 : c; }

static void skip_spaces(const char** s) {
    while (**s == ' ') (*s)++;
}

static int expr(const char** s);

static int factor(const char** s) {
    skip_spaces(s);
    if (**s == '(') {
        (*s)++;
        int v = expr(s);
        skip_spaces(s);
        if (**s == ')') (*s)++;
        return v;
    }
    if (**s == '-') {
        (*s)++;
        return -factor(s);
    }
    if (is_letter(**s)) {
        char c = to_upper(**s);
        (*s)++;
        if (c >= 'A' && c <= 'Z') return vars[c - 'A'];
        return 0;
    }
    int v = 0;
    while (is_digit(**s)) {
        v = v * 10 + (**s - '0');
        (*s)++;
    }
    return v;
}

static int term(const char** s) {
    int v = factor(s);
    while (1) {
        skip_spaces(s);
        if (**s == '*') { (*s)++; v *= factor(s); }
        else if (**s == '/') { (*s)++; int d = factor(s); if (d) v /= d; }
        else break;
    }
    return v;
}

static int expr(const char** s) {
    int v = term(s);
    while (1) {
        skip_spaces(s);
        if (**s == '+') { (*s)++; v += term(s); }
        else if (**s == '-') { (*s)++; v -= term(s); }
        else break;
    }
    return v;
}

static int relop(const char** s) {
    skip_spaces(s);
    int a = expr(s);
    skip_spaces(s);
    int op = 0;
    if (**s == '=') { op = 1; (*s)++; }
    else if (**s == '<') {
        (*s)++;
        if (**s == '=') { op = 4; (*s)++; }
        else if (**s == '>') { op = 6; (*s)++; }
        else op = 2;
    }
    else if (**s == '>') {
        (*s)++;
        if (**s == '=') { op = 5; (*s)++; }
        else op = 3;
    }
    if (!op) return a;
    int b = expr(s);
    switch (op) {
        case 1: return a == b;
        case 2: return a < b;
        case 3: return a > b;
        case 4: return a <= b;
        case 5: return a >= b;
        case 6: return a != b;
    }
    return 0;
}

static const char* match_word(const char* s, const char* word) {
    const char* p = s;
    skip_spaces(&p);
    const char* w = word;
    while (*w && to_upper(*p) == *w) { p++; w++; }
    if (*w) return 0;
    if (*p && *p != ' ' && *p != '\0' && *p != '"') return 0;
    return p;
}

static int parse_line_num(const char** s) {
    skip_spaces(s);
    int n = 0;
    const char* p = *s;
    while (is_digit(*p)) { n = n * 10 + (*p - '0'); p++; }
    if (p == *s) return -1;
    *s = p;
    return n;
}

static const char* cmd_print(const char* s) {
    skip_spaces(&s);
    if (*s == '"') {
        s++;
        while (*s && *s != '"') { print_char(*s); s++; }
        if (*s == '"') s++;
    } else {
        print_int(expr(&s));
    }
    while (*s == ',' || *s == ';') {
        s++;
        skip_spaces(&s);
        if (*s == '"') {
            s++;
            while (*s && *s != '"') { print_char(*s); s++; }
            if (*s == '"') s++;
        } else {
            print_int(expr(&s));
        }
    }
    print_str("\r\n");
    return s;
}

static const char* cmd_input(const char* s) {
    skip_spaces(&s);
    if (is_letter(*s)) {
        char c = to_upper(*s); s++;
        print_str("? ");
        char buf[16];
        int bi = 0;
        while (1) {
            uint16_t key = get_key();
            uint8_t ascii = key & 0xFF;
            if (ascii == 13 || ascii == 10) {
                buf[bi] = '\0';
                print_str("\r\n");
                break;
            }
            if (ascii == 8 && bi > 0) { bi--; print_str("\b \b"); }
            else if (ascii >= '0' && ascii <= '9' && bi < 15) { buf[bi++] = ascii; print_char(ascii); }
            else if (ascii == '-' && bi == 0) { buf[bi++] = ascii; print_char(ascii); }
        }
        int v = 0, sign = 1, i = 0;
        if (buf[0] == '-') { sign = -1; i = 1; }
        while (buf[i]) { v = v * 10 + (buf[i] - '0'); i++; }
        if (c >= 'A' && c <= 'Z') vars[c - 'A'] = v * sign;
    }
    return s;
}

static const char* cmd_let(const char* s) {
    skip_spaces(&s);
    if (is_letter(*s)) {
        char c = to_upper(*s); s++;
        skip_spaces(&s);
        if (*s == '=') s++;
        vars[c - 'A'] = expr(&s);
    }
    return s;
}

static int kbhit(void) {
    uint8_t v;
    __asm__ __volatile__ ("movb $0x01, %%ah\n\tint $0x16\n\tsetnz %0" : "=q"(v) : : "eax");
    return v;
}

static void run_prog_from(int start_ip) {
    ip = start_ip;
    stop_flag = 0;
    while (ip < prog_len && !stop_flag) {
        if (kbhit()) {
            uint16_t key;
            __asm__ __volatile__ ("movb $0x00, %%ah\n\tint $0x16" : "=a"(key));
            if ((key & 0xFF) == 3) {
                print_str("^C\r\n");
                stop_flag = 1;
                break;
            }
        }
        int curr = ip;
        const char* s = program + curr + 2;

        skip_spaces(&s);
        const char* rest;

        if ((rest = match_word(s, "PRINT"))) { cmd_print(rest); }
        else if ((rest = match_word(s, "LET"))) { cmd_let(rest); }
        else if ((rest = match_word(s, "IF"))) {
            int cond = relop(&rest);
            skip_spaces(&rest);
            if ((rest = match_word(rest, "THEN"))) {
                skip_spaces(&rest);
            }
            int line = parse_line_num(&rest);
            if (cond && line > 0) {
                int new_ip = find_line_num(line);
                if (new_ip >= 0) ip = new_ip;
                else { print_str("?GOTO NOT FOUND\r\n"); stop_flag = 1; }
            }
        }
        else if ((rest = match_word(s, "GOTO"))) {
            skip_spaces(&rest);
            int line = parse_line_num(&rest);
            if (line > 0) {
                int new_ip = find_line_num(line);
                if (new_ip >= 0) ip = new_ip;
                else { print_str("?GOTO NOT FOUND\r\n"); stop_flag = 1; }
            }
        }
        else if ((rest = match_word(s, "INPUT"))) { cmd_input(rest); }
        else if ((rest = match_word(s, "REM"))) { }
        else if ((rest = match_word(s, "END"))) { stop_flag = 1; }
        else {
            int has_let = 0;
            const char* t = s;
            skip_spaces(&t);
            if (is_letter(*t)) {
                char c = *t;
                const char* t2 = t + 1;
                skip_spaces(&t2);
                if (*t2 == '=') has_let = 1;
            }
            if (has_let) cmd_let(s);
        }

        if (ip == curr) ip = next_line(curr);
    }
}

static void run_prog(void) { run_prog_from(0); }

static void cmd_list(void) {
    int pos = 0;
    while (pos < prog_len) {
        uint16_t ln = (uint8_t)program[pos] << 8 | (uint8_t)program[pos + 1];
        print_int(ln);
        print_char(' ');
        int end = next_line(pos);
        const char* text = program + pos + 2;
        while (*text) { print_char(*text); text++; }
        print_str("\r\n");
        pos = end;
    }
}

static void auto_load(const char* fname) {
    clear_prog();
    my_memset(load_buf, 0, sizeof(load_buf));
    if (fs_read_file(fname, (uint8_t*)load_buf, sizeof(load_buf) - 1)) {
        print_str("?LOAD FAILED\r\n");
        return;
    }
    int i = 0;
    while (load_buf[i]) {
        int line_num = 0;
        while (load_buf[i] >= '0' && load_buf[i] <= '9') {
            line_num = line_num * 10 + (load_buf[i] - '0');
            i++;
        }
        while (load_buf[i] == ' ') i++;
        char text[MAX_LINE];
        int ti = 0;
        while (load_buf[i] && load_buf[i] != '\r' && load_buf[i] != '\n' && ti < MAX_LINE - 1) {
            text[ti++] = load_buf[i++];
        }
        text[ti] = '\0';
        if (line_num > 0) add_line(line_num, text);
        while (load_buf[i] == '\r' || load_buf[i] == '\n') i++;
    }
    print_str("LOADED.\r\n");
    run_prog();
}

void module_main(void) {
    const char* args = MODULE_ARGS;
    if (args && args[0]) {
        auto_load(args);
    }

    char line[MAX_LINE];
    while (1) {
        print_str("\r\nREADY.\r\n> ");

        int li = 0;
        my_memset(line, 0, MAX_LINE);
        while (1) {
            uint16_t key = get_key();
            uint8_t ascii = key & 0xFF;
            uint8_t scan = (key >> 8) & 0xFF;

            if (ascii == 13 || ascii == 10) {
                print_str("\r\n");
                break;
            }
            if (ascii == 8 && li > 0) { li--; print_str("\b \b"); }
            else if (ascii >= 32 && ascii <= 126 && li < MAX_LINE - 1) {
                line[li++] = ascii;
                print_char(ascii);
            }
        }

        const char* s = line;
        skip_spaces(&s);
        if (*s == '\0') continue;

        const char* rest;
        if ((rest = match_word(s, "BYE"))) { break; }
        else if ((rest = match_word(s, "RUN"))) { run_prog(); }
        else if ((rest = match_word(s, "LIST"))) { cmd_list(); }
        else if ((rest = match_word(s, "CLEAR"))) { clear_prog(); }
        else if ((rest = match_word(s, "PRINT"))) { cmd_print(rest); }
        else if ((rest = match_word(s, "INPUT"))) { cmd_input(rest); }
        else if ((rest = match_word(s, "LET"))) { cmd_let(rest); }
        else if ((rest = match_word(s, "REM"))) { }
        else if ((rest = match_word(s, "SAVE"))) {
            skip_spaces(&rest);
            char fname[64];
            int fi = 0;
            if (*rest == '"') {
                rest++;
                while (*rest && *rest != '"' && fi < 63) fname[fi++] = *rest++;
            } else {
                while (*rest && *rest != ' ' && fi < 63) fname[fi++] = *rest++;
            }
            fname[fi] = '\0';
            int out_len = 0;
            int pos = 0;
            while (pos < prog_len) {
                uint16_t ln = (uint8_t)program[pos] << 8 | (uint8_t)program[pos + 1];
                char num_buf[16];
                int ni = 0, n = ln;
                if (n == 0) { num_buf[ni++] = '0'; }
                else { while (n) { num_buf[ni++] = '0' + (n % 10); n /= 10; } }
                for (int j = ni - 1; j >= 0; j--) load_buf[out_len++] = num_buf[j];
                load_buf[out_len++] = ' ';
                int end = next_line(pos);
                for (int j = pos + 2; j < end && program[j]; j++) load_buf[out_len++] = program[j];
                load_buf[out_len++] = '\r';
                load_buf[out_len++] = '\n';
                if (out_len > (int)sizeof(load_buf) - 20) break;
                pos = end;
            }
            load_buf[out_len] = '\0';
            if (fs_write_file(fname, (uint8_t*)load_buf, out_len)) {
                print_str("?SAVE FAILED\r\n");
            } else {
                print_str("SAVED.\r\n");
            }
        }
        else if ((rest = match_word(s, "GOTO"))) {
            skip_spaces(&rest);
            int line = parse_line_num(&rest);
            if (line > 0) {
                int new_ip = find_line_num(line);
                if (new_ip >= 0) {
                    run_prog_from(new_ip);
                } else { print_str("?GOTO not found\r\n"); }
            }
        }
        else if ((rest = match_word(s, "LOAD"))) {
            skip_spaces(&rest);
            char fname[64];
            int fi = 0;
            if (*rest == '"') {
                rest++;
                while (*rest && *rest != '"' && fi < 63) fname[fi++] = *rest++;
            } else {
                while (*rest && *rest != ' ' && fi < 63) fname[fi++] = *rest++;
            }
            fname[fi] = '\0';
            if (fname[0]) {
                clear_prog();
                my_memset(load_buf, 0, sizeof(load_buf));
                if (fs_read_file(fname, (uint8_t*)load_buf, sizeof(load_buf) - 1)) {
                    print_str("?LOAD FAILED\r\n");
                } else {
                    int i = 0;
                    while (load_buf[i]) {
                        int line_num = 0;
                        while (load_buf[i] >= '0' && load_buf[i] <= '9') {
                            line_num = line_num * 10 + (load_buf[i] - '0');
                            i++;
                        }
                        while (load_buf[i] == ' ') i++;
                        char text[MAX_LINE];
                        int ti = 0;
                        while (load_buf[i] && load_buf[i] != '\r' && load_buf[i] != '\n' && ti < MAX_LINE - 1) {
                            text[ti++] = load_buf[i++];
                        }
                        text[ti] = '\0';
                        if (line_num > 0) add_line(line_num, text);
                        while (load_buf[i] == '\r' || load_buf[i] == '\n') i++;
                    }
                    print_str("LOADED.\r\n");
                }
            }
        }
        else {
            int line_num = parse_line_num(&s);
            if (line_num > 0) {
                skip_spaces(&s);
                if (*s) {
                    add_line(line_num, s);
                } else {
                    del_line(line_num);
                }
            } else {
                const char* t = s;
                skip_spaces(&t);
                if (is_letter(*t)) {
                    const char* t2 = t + 1;
                    skip_spaces(&t2);
                    if (*t2 == '=') cmd_let(s);
                    else print_str("?WHAT\r\n");
                } else {
                    print_str("?WHAT\r\n");
                }
            }
        }
    }
}
