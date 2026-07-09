__asm__(".code16gcc\n");
#include "api.h"

#define TAPE_SIZE 1024
#define BF_FILE "HELLO.BF"

static unsigned char tape[TAPE_SIZE];
static int ptr;
static const char* prog;
static int pc;

static void* my_memset(void* s, int c, int n) {
    uint8_t* p = (uint8_t*)s;
    while (n--) *p++ = (uint8_t)c;
    return s;
}

static int match_fwd(void) {
    int depth = 1;
    int p = pc + 1;
    while (prog[p] && depth > 0) {
        if (prog[p] == '[') depth++;
        else if (prog[p] == ']') depth--;
        p++;
    }
    return p - 1;
}

static int match_bwd(void) {
    int depth = 1;
    int p = pc - 1;
    while (p >= 0 && depth > 0) {
        if (prog[p] == ']') depth++;
        else if (prog[p] == '[') depth--;
        p--;
    }
    return p + 1;
}

static void bf_interpret(const char* program) {
    for (int i = 0; i < TAPE_SIZE; i++) tape[i] = 0;
    ptr = TAPE_SIZE / 2;
    pc = 0;
    prog = program;

    while (prog[pc]) {
        char c = prog[pc];
        if (c == '>' && ptr < TAPE_SIZE - 1) ptr++;
        else if (c == '<' && ptr > 0) ptr--;
        else if (c == '+') tape[ptr]++;
        else if (c == '-') tape[ptr]--;
        else if (c == '.') print_char(tape[ptr]);
        else if (c == ',') {
            uint16_t key = get_key();
            tape[ptr] = (char)(key & 0xFF);
        }
        else if (c == '[' && tape[ptr] == 0) pc = match_fwd();
        else if (c == ']' && tape[ptr] != 0) pc = match_bwd();
        pc++;
    }
}

static uint8_t bf_load_saved(void) {
    static char buf[512];
    my_memset(buf, 0, sizeof(buf));
    if (fs_read_file(BF_FILE, (uint8_t*)buf, sizeof(buf) - 1)) return 0;
    for (int i = 0; i < 512; i++) {
        if (buf[i] == '+' || buf[i] == '-' || buf[i] == '<' || buf[i] == '>' ||
            buf[i] == '[' || buf[i] == ']' || buf[i] == '.' || buf[i] == ',') {
            bf_interpret(buf);
            print_str("\r\n");
            return 1;
        }
    }
    return 0;
}

void module_main(void) {
    const char* args = MODULE_ARGS;

    if (args[0] == '\0') {
        if (!bf_load_saved()) {
            print_str("BF: No saved program. Use 'edit HELLO.BF' to create one.\r\n");
        }
        return;
    }

    int has_dot = 0;
    for (const char* p = args; *p; p++) {
        if (*p == '.') { has_dot = 1; break; }
    }

    if (has_dot) {
        static char buf[512];
        my_memset(buf, 0, sizeof(buf));
        if (fs_read_file(args, (uint8_t*)buf, sizeof(buf) - 1)) {
            print_str("BF: File not found\r\n");
            return;
        }
        bf_interpret(buf);
        print_str("\r\n");
    } else {
        bf_interpret(args);
        print_str("\r\n");
    }
}
