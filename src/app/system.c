__asm__(".code16gcc\n");
#include "apps.h"
#include "fs.h"

extern uint8_t boot_drive;
extern uint16_t asm_drive_type(void);

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

static const char* drive_type_str(uint8_t t) {
    if (t == 0x00) return "no drive";
    if (t == 0x01) return "diskette, no change line";
    if (t == 0x02) return "diskette, change line";
    if (t == 0x03) return "fixed disk";
    return "unknown";
}

static const char* media_state_str(uint8_t s) {
    if (s == 0) return "no readable disk";
    if (s == 1) return "matches cached volume";
    if (s == 2) return "readable disk differs from cache";
    return "rejected disk (no cache)";
}

// FAT volume serials display as two little-endian words, high word first.
static void print_volume_serial(const uint8_t* s) {
    print_hex_byte(s[3]); print_hex_byte(s[2]);
    print_char('-');
    print_hex_byte(s[1]); print_hex_byte(s[0]);
}

void cmd_diskinfo(const char* args) {
    uint16_t dt = asm_drive_type();

    print_str("Drive    ");
    if (boot_drive < 0x80) {
        print_char('A' + (boot_drive & 0x7F));
        print_char(':');
    } else {
        print_str("HD");
        print_int(boot_drive - 0x80);
    }
    print_str("  (BIOS 0x");
    print_hex_byte(boot_drive);
    print_str(")\r\n");

    print_str("BIOS     ");
    if ((dt & 0xFF) == 0) print_str(drive_type_str((dt >> 8) & 0xFF));
    else print_str("type query failed");
    print_str("\r\n");

    print_str("Media    ");
    print_str(media_state_str(fs_disk_state()));
    print_str("\r\n");

    print_str("Volume   ");
    if (fs_initialized) {
        print_str("FAT12, serial ");
        print_volume_serial(fs.volume_serial);
        print_str(", ");
        print_int(fs.reserved_sectors);
        print_str(" reserved, ");
        print_int(fs.num_fats);
        print_str(" FATs, ");
        print_int(fs.total_clusters);
        print_str(" clusters\r\n");
    } else {
        print_str("(none cached)\r\n");
    }

    print_str("CWD      OS:");
    if (fs_get_cwd_path()[0] == 0) print_char('\\');
    else print_str(fs_get_cwd_path());
    print_str("\r\n");
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
