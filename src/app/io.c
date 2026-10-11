__asm__(".code16gcc\n");
#include "apps.h"

void cmd_echo(const char* args) {
    uint8_t flags;
    args = parse_switches(args, &flags);
    print_str(args ? args : "");
    if (!(flags & SW_N)) print_str("\r\n");
}

void cmd_hexdump(const char* args) {
    int count = 128;

    if (args && args[0] == '-' && (args[1] == 'n' || args[1] == 'N')) {
        const char* p = args + 2;
        while (*p == ' ') p++;
        int v = 0, any = 0;
        while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; any = 1; }
        if (any) count = v;
    }
    if (count < 1) count = 1;
    if (count > 128) count = 128;

    uint8_t buf[128];
    __asm__ __volatile__ (
        "cld\n\t"
        "pushw %%es\n\t"
        "pushw %%ds\n\t"
        "movw $0x1000, %%ax\n\t"
        "movw %%ax, %%es\n\t"
        "xorw %%ax, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "rep movsb\n\t"
        "popw %%ds\n\t"
        "popw %%es\n\t"
        : : "S"(0), "D"((uint16_t)(uint32_t)buf), "c"((uint16_t)count)
        : "ax", "memory"
    );
    print_str("Dumping IVT at 0x0000:0x0000:\r\n");
    hexdump(buf, count);
}