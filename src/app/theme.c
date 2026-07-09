__asm__(".code16gcc\n");
#include "apps.h"
#include "fs.h"

extern uint8_t cur_col;

static const uint8_t theme_colors[] = {0x1F, 0x02, 0x06, 0x04, 0x0F};

void cmd_theme(const char* args) {
    if (!args || args[0] == '\0') {
        print_str("ERR: Select theme 0-4\r\n");
        return;
    }

    char choice = args[0];
    uint8_t num;
    if (choice == '0')      { num = 0; cur_col = theme_colors[0]; }
    else if (choice == '1') { num = 1; cur_col = theme_colors[1]; }
    else if (choice == '2') { num = 2; cur_col = theme_colors[2]; }
    else if (choice == '3') { num = 3; cur_col = theme_colors[3]; }
    else if (choice == '4') { num = 4; cur_col = theme_colors[4]; }
    else {
        print_str("ERR: Select theme 0-4\r\n");
        return;
    }

    uint8_t config[8];
    memset(config, 0, sizeof(config));
    config[0] = num;
    if (fs_write_file(CONFIG_FILE, config, 1)) {
        print_str("ERR: Failed to save theme\r\n");
        return;
    }

    clear_screen();
    print_str("OsmiumOS\r\n");
    print_int(get_mem_size());
    print_str("KB RAM available.\r\n");
    render_pal_mtx();
    print_str("\r\n");
}

void cmd_palette(const char* args) {
    render_pal_mtx();
    print_str("\r\n");
}
