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
6. Uses `mformat` to write a real FAT12 filesystem on top
7. Builds loadable modules (snake) and seeds config/boot files

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
