#ifndef APPS_H
#define APPS_H

#include "types.h"


// Modular command system pointers for all programs
typedef void (*cmd_func_t)(const char* args);

struct cli_command { 
    const char* name;
    uint8_t name_len;
    cmd_func_t function;
    const char* description;
};

// Forward declarations for application entry points
void cmd_help(const char* args);
void cmd_clear(const char* args);
void cmd_echo(const char* args);
void cmd_mem(const char* args);
void cmd_hexdump(const char* args);
void cmd_theme(const char* args);
void cmd_palette(const char* args);
void cmd_reboot(const char* args);
void cmd_date(const char* args);
void cmd_cpuinfo(const char* args);
void cmd_poweroff(const char* args);
void cmd_ls(const char* args);
void cmd_cat(const char* args);
void cmd_rm(const char* args);
void cmd_mv(const char* args);
void cmd_cp(const char* args);
void cmd_exec(const char* args);

#endif