__asm__(".code16gcc\n");
#include "apps.h"
#include "fs.h"

void cmd_clear(const char* args) {
    clear_screen();
}

void cmd_rescan(const char* args) {
    uint8_t r = fs_reload();

    if (r == 0) {
        print_str("Filesystem re-read from disk; back at root.\r\n");
        return;
    }

    error_chime();
    if (r == 1)      print_str("ERR: No disk, or sector 0 unreadable\r\n");
    else if (r == 2) {
        print_str("ERR: Unsupported filesystem (need 512B FAT12, 2 FATs)\r\n");
        print_str("Cached volume dropped; insert a supported disk and run rescan.\r\n");
    } else {
        print_str("ERR: Could not read FAT or root directory\r\n");
        print_str("Cached volume dropped; run rescan to retry.\r\n");
    }
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
