#ifndef API_H
#define API_H

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;

#define MODULE_ARGS ((const char*)0xFC00)
#define MODULE_SEGMENT 0x2000

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