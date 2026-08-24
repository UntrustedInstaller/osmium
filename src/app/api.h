#ifndef API_H
#define API_H

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;

#define MODULE_ARGS ((const char*)0xFC00)
#define MODULE_SEGMENT 0x2000

// Think it'd be fun to add an int21h translator for DOS binary compatibility?
// Keep our .BIN programs and DOS .EXE/.COM programs seperate as to distinguish to both the user and the kernel which is which
// Getting some sort of DOS compatibility layer would be fun, and as a bonus it gets more code running on the OS. 
// Granted... DOS is a mess that I don't have the willpower to take a machete to, AND some things that DOS programs want
// are definitely NOT implemented into our API. 
// I'll throw some pseudocode here for now just so the idea is there.
// static inline void dos_translate(uint16_t int21h_func, uint16_t ax, uint16_t bx, uint16_t cx, uint16_t dx) {
//     ==THIS IS WHERE IT STOPS BEING CODE==
//     *check if the program is .COM/EXE or our own .BIN*
//     *.COM/EXE? Intercept int21h calls and translate them to our own API calls*
//     *Once it's translated, throw it back to the DOS program, allowing it to think "Hey, I'm running on DOS!" and let it go on its merry way
//     *Our own .BIN? forgive and forget.
// }
//     I feel like though, doing this would require an implementation of most of the DOS API into ours, on top of the fact of consistently having to
//     intercept and report faux DOS versions back to programs, or work around crude version checks that would otherwise crash both the OS and the program.
//     Too much work? Maybe. Cool? absolutely. Will I do it? Remains to be seen.
//     Granted this would by no means allow for full compatibility, GUI apps would probably just shatter the OS, pre-9X versions of Windows would make Osmium cry.
//     Hi github, I'm totally adding useful code to the OS instead of a ton of pseudocode for things that I will more than likely procrastinate on for months on end.
//     I promise I'll get to it. I might. I might not. I'll atleast try, eventually..?
//     Once I muster the willpower and courage to do this I'll probably split the DOS-compat version into a new branch, because I don't want to meddle with the stable main brainch
//     Stable? Who am I kidding? There's probably some unprotected memory access in the most trivial part of the code that will detonate the kernel if you so much as blink at it.
//     I'm POSITIVE that Osmium is held together with duct tape and tears, my own code reads like a foreign language that I have to consult the ancient texts to understand.
//     So where those issues may be? Your guess is as good as mine. But I'm pretty sure they're there.
//     Anyways, back to actually programming instead of ranting into C comments about how I don't know what I'm doing and how I should probably be doing something else instead of this.
// -Untrusted

static inline void print_str(const char* s) {
    uint16_t off = (uint16_t)(uint32_t)s;
    __asm__ __volatile__(
        "pushw %%es\n\t"
        "pushw %%ds\n\t"
        "popw %%es\n\t"
        "int $0x60\n\t"
        "popw %%es\n\t"
        : : "c"(0), "b"(off) : "ax", "dx", "memory"
    );
}

static inline void print_char(char c) {
    __asm__ __volatile__("int $0x60" : : "c"(1), "a"((uint8_t)c) : "memory");
}

static inline uint16_t get_key(void) {
    uint16_t k;
    __asm__ __volatile__("int $0x60" : "=a"(k) : "c"(2) : "dx", "memory");
    return k;
}

static inline void clear_screen(void) {
    __asm__ __volatile__("int $0x60" : : "c"(3) : "memory");
}

static inline void gotoxy(uint8_t col, uint8_t row) {
    __asm__ __volatile__("int $0x60" : : "c"(4), "d"((row << 8) | col) : "ax", "memory");
}

static inline uint8_t read_sector(uint16_t lba, void* buffer) {
    uint8_t status;
    uint16_t buf_off = (uint16_t)(uint32_t)buffer;
    __asm__ __volatile__(
        "pushw %%es\n\t"
        "pushw %%ds\n\t"
        "popw %%es\n\t"
        "int $0x60\n\t"
        "popw %%es\n\t"
        : "=a"(status) : "c"(5), "a"(lba), "b"(buf_off) : "dx", "memory"
    );
    return status;
}

static inline uint8_t write_sector(uint16_t lba, void* buffer) {
    uint8_t status;
    uint16_t buf_off = (uint16_t)(uint32_t)buffer;
    __asm__ __volatile__(
        "pushw %%es\n\t"
        "pushw %%ds\n\t"
        "popw %%es\n\t"
        "int $0x60\n\t"
        "popw %%es\n\t"
        : "=a"(status) : "c"(6), "a"(lba), "b"(buf_off) : "dx", "memory"
    );
    return status;
}

static inline void get_cursor_rc(uint8_t* row, uint8_t* col) {
    uint16_t rc;
    __asm__ __volatile__("int $0x60" : "=d"(rc) : "c"(7) : "ax", "memory");
    *row = (rc >> 8) & 0xFF;
    *col = rc & 0xFF;
}

static inline void print_int(uint16_t val) {
    __asm__ __volatile__("int $0x60" : : "c"(8), "a"(val) : );
}

static inline uint8_t get_cur_col(void) {
    uint8_t c;
    __asm__ __volatile__("int $0x60" : "=a"(c) : "c"(11) : );
    return c;
}

static inline void set_cur_col(uint8_t attr) {
    __asm__ __volatile__("int $0x60" : : "c"(12), "a"(attr) : );
}

static inline uint16_t get_mem_size(void) {
    uint16_t sz;
    __asm__ __volatile__("int $0x60" : "=a"(sz) : "c"(13) : "dx", "memory");
    return sz;
}

static inline uint16_t get_kernel_end(void) {
    uint16_t end;
    __asm__ __volatile__("int $0x60" : "=a"(end) : "c"(14) : );
    return end;
}

static inline uint8_t fs_read_file(const char* name, void* buf, uint16_t max) {
    uint8_t status;
    uint16_t name_off = (uint16_t)(uint32_t)name;
    uint16_t buf_off = (uint16_t)(uint32_t)buf;
    __asm__ __volatile__(
        "pushw %%es\n\t"
        "pushw %%ds\n\t"
        "popw %%es\n\t"
        "int $0x60\n\t"
        "popw %%es\n\t"
        : "=a"(status)
        : "c"(9), "b"(name_off), "a"(buf_off), "d"(max)
        : "memory"
    );
    return status;
}

static inline uint8_t fs_write_file(const char* name, const void* data, uint16_t size) {
    uint8_t status;
    uint16_t name_off = (uint16_t)(uint32_t)name;
    uint16_t data_off = (uint16_t)(uint32_t)data;
    __asm__ __volatile__(
        "pushw %%es\n\t"
        "pushw %%ds\n\t"
        "popw %%es\n\t"
        "int $0x60\n\t"
        "popw %%es\n\t"
        : "=a"(status)
        : "c"(10), "b"(name_off), "a"(data_off), "d"(size)
        : "memory"
    );
    return status;
}

#endif