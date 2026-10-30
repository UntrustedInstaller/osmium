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
    const char* usage;
};

// ---- Command switches (bit flags parsed by parse_switches) ----
#define SW_RECURSIVE   0x001   // -r
#define SW_FORCE       0x002   // -f
#define SW_PARENTS     0x004   // -p
#define SW_ALL         0x008   // -a
#define SW_N           0x010   // -n
#define SW_HUMAN       0x020   // -h
#define SW_LIST        0x040   // -l
#define SW_INSENSITIVE 0x100   // -i (grep)
#define SW_INVERT      0x200   // -v (grep)

// Peels leading "-x" switches (e.g. "-rf", "-r -f") off args, ORs the
// matching SW_* bits into *flags, and returns the first non-switch argument.
const char* parse_switches(const char* args, uint16_t* flags);

// Forward declarations for application entry points
void cmd_help(const char* args);
void cmd_clear(const char* args);
void cmd_rescan(const char* args);
void cmd_diskinfo(const char* args);
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
void cmd_df(const char* args);
void cmd_cat(const char* args);
void cmd_grep(const char* args);
void cmd_rm(const char* args);
void cmd_mv(const char* args);
void cmd_cp(const char* args);
void cmd_exec(const char* args);
void cmd_cd(const char* args);
void cmd_pwd(const char* args);
void cmd_mkdir(const char* args);
void cmd_rmdir(const char* args);

#endif