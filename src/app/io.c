__asm__(".code16gcc\n");
#include "apps.h"

void cmd_echo(const char* args) {
    print_str(args);
    print_str("\r\n");
}

void cmd_hexdump(const char* args) {
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
        : : "S"(0), "D"((uint16_t)(uint32_t)buf), "c"(128)
        : "ax", "memory"
    );
    print_str("Dumping IVT at 0x0000:0x0000:\r\n");
    hexdump(buf, sizeof(buf));
}