__asm__(".code16gcc\n");
#include "apps.h"

void cmd_poweroff(const char* args) {
    uint32_t result;

    // Step 1: Check APM exists (int 15h AX=5300h, BX=0000h)
    // AH = APM version (e.g. 0x01), BH='P', BL='M', CF=0 on success
    __asm__ __volatile__ (
        "movw $0x5300, %%ax\n\t"
        "xorw %%bx, %%bx\n\t"
        "int $0x15\n\t"
        "setc %%al\n\t"
        "movzbl %%al, %%eax\n\t"
        : "=a"(result)
        :
        : "ebx", "ecx", "edx"
    );

    if (result) {
        print_str("ERR: APM not available\r\n");
        return;
    }

    // Step 2: Connect APM real-mode interface (int 15h AX=5301h, BX=0000h)
    __asm__ __volatile__ (
        "movw $0x5301, %%ax\n\t"
        "xorw %%bx, %%bx\n\t"
        "int $0x15\n\t"
        "setc %%al\n\t"
        "movzbl %%al, %%eax\n\t"
        : "=a"(result)
        :
        : "ebx", "ecx", "edx"
    );

    if (result) {
        print_str("ERR: APM connect failed\r\n");
        return;
    }

    print_str("Shutting down...\r\n");

    // Step 3: Power off (int 15h AX=5307h, BX=0001h, CX=0003h)
    // BX=0001 = all devices, CX=0003 = system off
    __asm__ __volatile__ (
        "movw $0x5307, %%ax\n\t"
        "movw $0x0001, %%bx\n\t"
        "movw $0x0003, %%cx\n\t"
        "int $0x15\n\t"
        :
        :
        : "eax", "ebx", "ecx", "edx"
    );

    print_str("ERR: Power-off failed\r\n");
}
