![OsmiumOS](OsmiumOS.svg)

# OsmiumOS

A scratch-built 16-bit real-mode operating system written in **NASM assembly** and **C**.

OsmiumOS boots from a FAT12 floppy disk into an interactive shell with built-in commands and loadable user-space modules. It is a from-scratch educational OS with no external dependencies, no standard library, and no existing kernel code — just the BIOS, a floppy controller, and a lot of inline assembly.

## Features

- **Real-mode 16-bit x86** — no protected mode, no UEFI, no memory model
- **FAT12 filesystem** — read files from disk via BIOS INT 13h
- **Interactive shell** — commands: `help`, `clear`, `dir`, `echo`, `theme`, `mem`, `hexdump`, `reboot`, and more
- **Loadable modules** — snake, brainfuck interpreter, text editor, BASIC interpreter
- **Interrupt-driven module API** — user modules communicate with the kernel through INT 60h
- **Theme system** — 5 color schemes for the CLI

## Quick start

```bash
git clone https://github.com/UntrustedInstaller/osmium.git
cd osmium
./build.sh
```

Dependencies: `nasm`, `gcc` (with `-m16` support), `ld`, `mtools`, `qemu-system-i386` (optional).

## Project structure

```
├── build.sh            — Full build pipeline
├── src/
│   ├── boot.asm        — Stage 1 boot sector
│   ├── kernel.asm      — Stage 2 entry & assembly services
│   ├── main.c          — Kernel main loop & command dispatch
│   ├── fs.c            — FAT12 filesystem layer
│   ├── module_entry.asm— Loadable module entry point
│   ├── module.ld       — Linker script for loadable modules
│   └── app/            — Built-in apps & loadable modules
├── docs/
│   ├── BUILD.md        — Detailed build & run instructions
│   ├── KERNEL.md       — Kernel internals
│   └── MODULE.md       — Writing loadable modules
└── linker.ld           — Kernel ELF linker script
```

## License

GNU General Public License v3.0. See [LICENSE](LICENSE).

---

*Built from the BIOS up, one `int` at a time.*
