# Kernel Internals

## Memory Layout (16-bit Real Mode)

```
0x0000 - 0x7BFF  BIOS / IVT / BDA / free
0x7C00 - 0x7DFF  Boot sector (loaded by BIOS)
0x7E00 - 0x7FFF  Stack (grows down from 0x7C00 during boot)
0x1000:0x0000     Kernel segment (loaded by bootloader)
  - 0x0000        kernel_init entry point
  - 0x0000-...    .text, .rodata, .data
  - 0x????
  - BSS (zeroed at startup)
  - Stack at top of segment (SP = 0xFFF0)
0x2000:0x0000     Module load segment (lcall target)
   - 0x0000        module_main entry
   - 0xFE00        Module stack (top, grows down; DS=SS required by -m16)
   - 0xFC00        Argument string (copied by kernel before call)
0x10000+           Free / available to BIOS
```

## Boot Sequence

1. BIOS loads sector 0 (boot.bin) to `0x7C00`
2. Bootloader reads 78 sectors (the kernel) to `0x1000:0x0000`
3. Jumps to `0x1000:0x0000` → `kernel_init`
4. `kernel_init` sets DS=ES=SS=0x1000, zeros BSS, clears screen
5. Calls `iridium_main()` (the C shell)
6. Shell initializes filesystem, plays boot chime, shows prompt

## Filesystem (FAT12)

Standard 1.44MB floppy geometry:

- 80 cylinders, 2 heads, 18 sectors/track
- 73 reserved sectors (boot + kernel occupy most of these)
- 2 FATs, 9 sectors each
- 224 root directory entries (14 sectors)
- Data area: ~2847 clusters, 1 sector per cluster

The FAT and root directory are cached in kernel BSS at boot time. Writes go through `flush_fat()` and `flush_root()` which write both FAT copies.

## Command Dispatch

```
user input → shell parses cmd_buf
  → matches cmd_table[] → calls built-in function
  → no match → try_load_and_run() → loads <NAME>.BIN from disk
  → no file → "Unknown command"
```

Commands can be added to `cmd_table[]` in `main.c`. Update `help_sections[]` to keep the `help` grouping correct.

## Module API (INT 60h)

The kernel installs an interrupt handler at IVT slot 0x60 (offset 0x0180). Modules set CX = function number and call `int 0x60`. The handler runs in kernel segment context.

API functions (see MODULE.md for full table):

| CX | Function |
|----|----------|
| 0–4 | Display: print_str, print_char, get_key, clear_screen, gotoxy |
| 5–6 | Disk: read_sector, write_sector |
| 7–8 | Utility: get_cursor, print_int |
| 9–10 | **Filesystem**: fs_read_file, fs_write_file (module-safe, copies via rep movsb through mod_buf) |
| 11–12 | Colour: get_cur_col, set_cur_col |
| 13–14 | System: get_mem_size, get_kernel_end |

Calls 9–10 are the most complex: they copy the filename from the module segment to a kernel buffer, call the C FS functions, and copy data across segments via `rep movsb` through the kernel's `mod_buf` staging area. See `api_fs_read_file()` / `api_fs_write_file()` in `src/main.c`.

## Notable Files

| File | What it does |
|------|-------------|
| `src/boot.asm` | Stage 1 bootloader — loads kernel, jumps to it |
| `src/kernel.asm` | Stage 2 entry, HAL (print, disk I/O, scroll), INT 60h handler (CX=0-14), multi-sector read |
| `src/main.c` | Shell, command table, module loading, HAL wrappers, FS API bridge (api_fs_read/write_file) |
| `src/fs.c` | FAT12 driver — read/write/delete/rename/copy/list, multi-sector read |
| `src/module_entry.asm` | Module stub — saves kernel SS:SP, sets DS=SS=0x2000, zeroes BSS, calls module_main() |
| `linker.ld` | Kernel linker script — ELF, BSS symbols exported |
