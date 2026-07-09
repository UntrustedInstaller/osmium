__asm__(".code16gcc\n");
#include "apps.h"

void cmd_clear(const char* args) {
    clear_screen();
}

void cmd_mem(const char* args) {
    print_str("Memory available: ");
    print_int(get_mem_size());
    print_str(" KB RAM.\r\n");
}

void cmd_reboot(const char* args) {
    print_str("Rebooting...\r\n");
    __asm__ __volatile__ ("ljmp $0xFFFF, $0x0000");
}
