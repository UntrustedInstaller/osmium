# Building OsmiumOS

## Prerequisites

- NASM (assembler)
- GCC with `-m16` support (i386 cross-target or multilib)
- GNU ld (BFD, ELF support)
- `mtools` (mformat, mcopy)
- QEMU (optional, for testing)

On Debian/Ubuntu:

```
sudo apt install nasm gcc-multilib mtools qemu-system-x86
```

## Build

```
./build.sh
```

That's it. The script:

1. Assembles `src/boot.asm` → flat binary boot sector
2. Assembles `src/kernel.asm` → ELF object
3. Compiles `src/main.c`, `src/fs.c`, and everything in `src/app/` (except loadable modules)
4. Links to ELF, then strips BSS into a flat binary
5. Concatenates boot sector + kernel → `build/os.img`
6. Uses `mformat` to write a real FAT12 filesystem on top (79 reserved sectors)
7. Builds the loadable modules (snake, brainfuck, editor, BASIC) and seeds config/boot files

### Disk layout

The first 79 sectors are always reserved: one boot sector plus the 78 sectors
the boot loader reads for the kernel. `build.sh` zero-pads the kernel to exactly
78 sectors, so the FAT always starts at sector 79 and an in-place
`flash-update.sh` works across kernel growth without moving your files.

The kernel must stay at or below **78 sectors (39,936 bytes)**. `build.sh`
fails with the actual size if it does not fit, and prints the kernel size plus
the remaining stack headroom on every build.

## Run

```
qemu-system-i386 -fda build/os.img
```

Or the build script will offer to launch QEMU automatically.

## Writing to a floppy

The build script asks at the end. Or manually:

```
dd if=build/os.img of=/dev/fd0 bs=512 status=progress
```

Yes you need root for that. Yes you should be careful. Yes it will destroy everything on that disk. You've been warned.

## What is this thing anyway

16-bit real mode, no protected mode, no UEFI, no drivers, no memory model worth mentioning. It boots off a FAT12 floppy, gives you a shell, and lets you run apps. That's the whole deal.
