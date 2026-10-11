# Module Development Guide

A **module** is a loadable flat binary that runs as a separate program under OsmiumOS. Modules are loaded at segment `0x2000`, entered via `lcall`, and call kernel services through `int 0x60`.

---

## 1. Quick Start

### Template

```c
__asm__(".code16gcc\n");
#include "api.h"       /* INT 60h wrappers (print, FS, etc.) */
#include "types.h"     /* uint8_t, uint16_t, constants */

void module_main(void) {
    /* Arguments passed by user at 0x2000:0xFC00 */
    const char* args = (const char*)MODULE_ARGS_OFFSET;

    print_str("Hello from my module!\r\n");
    print_str("Args: ");
    print_str(args);
    print_str("\r\n");
}
```

### Build & Run

```sh
# Compile module code
gcc -m16 -march=i386 -ffreestanding -nostdlib -Isrc/app \
    -c mymod.c -o mymod.o

# Assemble entry stub
nasm -f elf32 src/module_entry.asm -o mod_entry.o

# Link flat binary
ld -m elf_i386 -T src/module.ld mod_entry.o mymod.o \
    -o mymod.bin

# Copy onto floppy image
mcopy -i build/os.img mymod.bin ::MYMOD.BIN
```

Run it from the shell:

```
> mymod
Hello from my module!
Args:
> mymod hello world
Hello from my module!
Args: hello world
```

Or use `exec`:

```
> exec MYMOD
> exec MYMOD some args
```

---

## 2. API Reference (INT 60h)

The kernel installs a handler at IVT slot `0x60` (offset `0x0180`). Set `CX` = function number and call `int 0x60`. All register-based — no stack args.

### Display

| CX | Function | Call | Returns | Notes |
|----|----------|------|---------|-------|
| 0 | `print_str` | `ES:BX` = string | — | String must be null-terminated, in module segment |
| 1 | `print_char` | `AL` = character | — | Handles `\r`, `\n`, `\b` |
| 2 | `get_key` | — | `AX` = keycode | Blocks until keypress. `AL` = ASCII, `AH` = scancode |
| 3 | `clear_screen` | — | — | Clears to current colour attribute |
| 4 | `gotoxy` | `DL` = column, `DH` = row | — | 0-indexed (0..79, 0..24) |
| 7 | `get_cursor` | — | `DL` = row, `DH` = col | Note: **reversed** from `gotoxy` |
| 8 | `print_int` | `AX` = 16-bit value | — | Prints decimal, no leading zeros |
| 11 | `get_cur_col` | — | `AL` = current attribute | e.g. `0x1F` = white on blue |
| 12 | `set_cur_col` | `AL` = attribute | — | Affects future clears and writes |

### Disk I/O

| CX | Function | Call | Returns | Notes |
|----|----------|------|---------|-------|
| 5 | `read_sector` | `AX` = LBA, `ES:BX` = buffer | `AL` = 0 ok, 1 err | Single sector (512 bytes) |
| 6 | `write_sector` | `AX` = LBA, `ES:BX` = buffer | `AL` = 0 ok, 1 err | Single sector (512 bytes) |

### Filesystem

| CX | Function | Call | Returns | Notes |
|----|----------|------|---------|-------|
| 9 | `fs_read_file` | `ES:BX` = filename, `AX` = buffer offset, `DX` = max bytes | `AL` = 0 ok, 1 err | Filename in module segment, buffer in module segment |
| 10 | `fs_write_file` | `ES:BX` = filename, `AX` = data offset, `DX` = size | `AL` = 0 ok, 1 err | Filename in module segment, data in module segment |

Filenames are resolved against the shell's current directory, so a module
launched from `OS:\GAMES>` opens `LEVEL.DAT` in `\GAMES`. Prefix the name with
`\` (e.g. `\DATA.TXT`) to always use the root, and either `/` or `\` works as a
path separator.

### System Info

| CX | Function | Call | Returns | Notes |
|----|----------|------|---------|-------|
| 13 | `get_mem_size` | — | `AX` = RAM in KB | Total conventional + extended |
| 14 | `get_kernel_end` | — | `AX` = kernel footprint (bytes) | End of BSS, for "about" display |

### Sound

| CX | Function | Call | Returns | Notes |
|----|----------|------|---------|-------|
| 15 | `play_note` | `AX` = frequency in Hz, `DX` = duration in ms | — | Frequencies below 20 Hz stop the speaker |
| 16 | `error_chime` | — | — | Plays the standard error chime |
| 17 | `critical_chime` | — | — | Plays the standard critical chime |
| 18 | `question_chime` | — | — | Plays the standard question chime |

### Inline-asm Wrappers (api.h)

Use the wrappers in `src/app/api.h` instead of raw `int 0x60`:

```c
print_str("Hello\r\n");
print_char('!');

uint16_t key = get_key();           // AL = ASCII, AH = scancode
uint8_t ascii = key & 0xFF;

clear_screen();
gotoxy(10, 5);                      // column 10, row 5

uint8_t row, col;
get_cursor_rc(&row, &col);          // get cursor position

print_int(42);                      // prints "42"

uint8_t saved = get_cur_col();      // save current colour
set_cur_col(0x1F);                  // white on blue
clear_screen();
set_cur_col(saved);                 // restore

play_note(440, 250);                // Play A4 for 250 ms
error_chime();

// Read file from disk into module buffer
static char buf[512];
if (fs_read_file("DATA.TXT", buf, sizeof(buf)) == 0) {
    print_str(buf);
}

// Write data to disk
static const char data[] = "hello world\r\n";
fs_write_file("OUTPUT.TXT", data, 13);
```

The wrappers handle segment register setup (`ES` = DS = module segment) and clobber declarations. They work with GCC `-m16`.

---

## 3. Module Lifecycle

```
Shell prompt
  → user types "foo"
  → shell checks cmd_table[] (built-ins win)
  → no match → try_load_and_run("foo", args)
  → looks for FOO.BIN on FAT12 disk
  → found → load 22 sectors to 0x2000:0x0000
  → copy argument string to 0x2000:0xFC00
  → lcall $0x2000, $0x0000
    → module_entry.asm:
        1. Save kernel SS:SP
        2. Set DS=ES=SS=0x2000, SP=0xFE00
        3. Zero BSS (_bss_start .. _bss_end)
        4. Call module_main()
  → module runs, returns
  → module_entry.asm restores kernel SS:SP, DS:ES
  → retf back to shell
```

## 4. Entry Point

Your module must define `module_main`:

```c
void module_main(void);
```

No arguments are passed in registers. Access the command-line argument string through the pointer at segment `0x2000`, offset `0xFC00`:

```c
const char* args = (const char*)0xFC00;
// or use the constant:
const char* args = (const char*)MODULE_ARGS_OFFSET;
```

If the user ran `basic program.bas`, `args` points to `"program.bas"`.

---

## 5. Memory Model

| Range | Use |
|-------|-----|
| `0x2000:0x0000` – `0x2000:_bss_end` | Code, rodata, data, BSS |
| `0x2000:0xFC00` – `0x2000:FDFF` | Argument string (512 bytes max) |
| `0x2000:0xFE00` – `0x2000:FFFF` | Module stack (512 bytes) |

- The stack is in the module segment, so `DS == SS` — GCC `-m16` code can dereference stack pointers correctly.
- BSS is zeroed before `module_main` is called.
- Max module size: **22 sectors** (11,264 bytes). Change `MODULE_SECTORS` in `types.h` if you need more.

---

## 6. Build Integration

`build.sh` auto-discovers `src/app/*.c` and links them into the kernel, except
for the names listed in `LOADABLE_MODULES`. To ship a new loadable module:

1. Add its name to the `LOADABLE_MODULES` list near the top of the "Compiling
   application modules" section, so it is *not* linked into the kernel.
2. Add a `build_module "<name>"` call next to the existing ones. The helper
   builds `src/app/<name>.c` into `build/<name>.mod`:

   ```sh
   build_module() {
       local name="$1"
       gcc -m16 -march=i386 -ffreestanding -fno-pic -fno-PIE \
           -fno-stack-protector -nostdlib -Isrc/app \
           -c "src/app/${name}.c" -o "build/mod_${name}.o"
       nasm -f elf32 src/module_entry.asm -o "build/mod_entry_${name}.o"
       ld -m elf_i386 -T src/module.ld \
           "build/mod_entry_${name}.o" "build/mod_${name}.o" \
           -o "build/${name}.mod"
   }
   build_module "mymod"
   ```

3. Seed the flat binary onto the image (8.3 name, uppercased). The shell looks
   up the module by the command name the user types:

   ```sh
   mcopy -i build/os.img build/mymod.mod ::MYMOD.BIN
   ```

There is no `CFLAGS`/`modules.lst` indirection — the flags in `build_module`
are literal and match the kernel's compiler flags.

---

## 7. Limitations

- **No dynamic memory** — no `malloc`, no `free`. Allocate statically or on the stack.
- **Flat binary** — no ELF loader, no relocations, no dynamic linking.
- **16-bit real mode** — 64K segment limit, no protected mode features.
- **Module size** — hard limit of 22 sectors (11,264 bytes), set by `MODULE_SECTORS` in `types.h`.
- **No preemption** — modules run to completion. The kernel does not multitask.
- **No hardware access** — use the INT 60h API for all I/O. Direct port I/O will conflict with kernel state.

---

## 8. Existing Modules (Reference)

| Module | Source | Size | Features |
|--------|--------|------|----------|
| `BASIC.BIN` | `basic.c` | ~10.4 KB | Tiny BASIC interpreter (Palo Alto variant), SAVE/LOAD |
| `BRAINFUC.BIN` | `bf.c` | ~1.6 KB | Brainfuck interpreter, disk-based programs |
| `EDIT.BIN` | `editor.c` | ~4.0 KB | Full-screen text editor, file persistence |
| `SNAKE.BIN` | `snake.c` | ~2.6 KB | Snake game |

Read these for idiomatic examples of:
- File I/O via the FS API (`fs_read_file` / `fs_write_file`)
- Keyboard input with `get_key()`
- Screen management with `gotoxy`, `clear_screen`, `get_cursor_rc`
- Static buffers and stack-based strings
