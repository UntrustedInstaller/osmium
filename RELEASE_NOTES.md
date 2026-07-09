# OsmiumOS M5 — "Osmium's Periodic Neighbor"

M5 is the final release of the 16-bit C migration line. The code returns to Osmium now. Iridium continues as a 32-bit thing elsewhere.

---

## What's in the can

- **22 commands.** If you type it and it's not a command, the shell looks for `NAME.BIN` on disk. This is called "autodispatch" and it makes you feel powerful.

- **Loadable module system.** Apps are flat binaries loaded at `0x2000:0x0000`. The kernel finds them on the FAT12 disk, loads them, and calls `module_main()`. You can write your own — see `docs/MODULE.md`.

- **FAT12 with teeth.** `ls`, `cat`, `rm`, `mv`, `cp`. Multi-sector reads. Files are read and written through the kernel's INT 60h API. BASIC programs can `SAVE` and `LOAD`. The editor persists files.

- **BASIC interpreter.** Palo Alto Tiny BASIC variant. Line-numbered, `PRINT`, `INPUT`, `IF/THEN`, `FOR/NEXT`, `GOTO`, `GOSUB`, `LET`, variables A–Z. Works now — the DS≠SS bug that made `PRINT "HELLO"` silently fail was the last real blocker.

- **Full-screen text editor.** Arrow keys, insert, visual mode. Saves to disk. It's no Vim but it also doesn't require a PhD to exit.

- **Brainfuck interpreter.** Because every OS needs one. Reads `HELLO.BF` by default or any `.BF` file you throw at it.

- **Snake game.** WASD controls. Food exists. You are a snake. This is the complete experience.

---

## The Kitchen Sink (all commands)

```
help       clear      echo       date
mem        hexdump    cpuinfo    exec
ls         cat        rm         mv         cp
palette    theme      about      brainfuck  edit
basic      snake
reboot     poweroff
```

---

## What broke to make this work

- **DS≠SS segment mismatch.** The module entry stub left SS pointing at the kernel's stack while setting DS to the module segment. GCC `-m16` assumes DS==SS, so stack pointers dereferenced through DS read from the wrong segment. This is why BASIC read empty lines and the editor corrupted its own state. Fixed by saving kernel SS:SP, setting SS=DS=0x2000, and restoring on return.

- **Modules truncated at 2560 bytes.** `MODULE_SECTORS` was 5. BASIC alone is 9776 bytes. Changed to 22. Modules now fit.

- **Operand numbering bug in `read_sectors`.** The inline asm used `%1` for the sector count — but `%0` is the output, `%1` is the LBA, `%2` is the count. This loaded LBA as the sector count and hung the boot. Fixed.

- **Kernel tail corruption.** The bootloader read through FAT sectors into kernel BSS. Zeroed at startup so it's harmless, but it wasn't *correct*.

- **API clobber declarations.** Every `int 0x60` wrapper in `api.h` now lists what it actually clobbers. This matters more than you'd think.

---

## The API (INT 60h)

| CX | What it does |
|----|-------------|
| 0  | `print_str` — ES:BX = string |
| 1  | `print_char` — AL = char |
| 2  | `get_key` — AX = keycode (AL ASCII, AH scancode) |
| 3  | `clear_screen` |
| 4  | `gotoxy` — DL = col, DH = row |
| 5  | `read_sector` — AX = LBA, ES:BX = buf → AL=0 ok |
| 6  | `write_sector` — AX = LBA, ES:BX = buf → AL=0 ok |
| 7  | `get_cursor` → DL = row, DH = col |
| 8  | `print_int` — AX = value |
| 9  | `fs_read_file` — ES:BX = name, AX = dest, DX = max → AL=0 ok |
| 10 | `fs_write_file` — ES:BX = name, AX = src, DX = size → AL=0 ok |
| 11 | `get_cur_col` → AL = attribute |
| 12 | `set_cur_col` — AL = attribute |
| 13 | `get_mem_size` → AX = KB |
| 14 | `get_kernel_end` → AX = BSS end offset |

---

## Build it

```sh
git clone https://github.com/untrustedinstaller/iridium
cd iridium
./build.sh
```

Needs `nasm`, `gcc-multilib`, `mtools`, `qemu-system-x86`.

Output: `build/os.img` — a 1.44 MB floppy that boots to an `OS:>` prompt.

---

## Known issues

- The bootloader reads 78 sectors (LBA 1–78) which overlaps 6 FAT sectors into kernel BSS. The kernel zeros BSS on startup so it works, but it's not *clean*.
- FAT12 uses 8.3 uppercase filenames. No long filenames. This is a feature if you're nostalgic.
- The about screen's boot animation assumes default colours. Custom themes may look weird.
- No multitasking. Modules run to completion. The OS waits for you.

---

## The future

The 16-bit line ends here. This code gets folded back into Osmium. A 32-bit IridiumOS is in early prototyping — protected mode, IMGUI, FAT32, the works. See `design-docs.md` if that shipped with your build, or the `iridium32` directory.

---

*Pouring 0x0D cups of coffee... counting RAM... 639. 640. Wait. 639.*
