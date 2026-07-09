#!/bin/bash
set -e

echo "=============================================="
echo "               OSMIUM BUILDER                "
echo "=============================================="

# Ensure clean build workspaces exist
mkdir -p build
mkdir -p build/app

# 1. Assemble Stage 1 (Boot Sector) stays raw flat binary
echo "[*] Assembling Stage 1 (src/boot.asm)..."
nasm -f bin src/boot.asm -o build/boot.bin

# 2. Assemble Stage 2 Assembly Base into an ELF32 object layout
echo "[*] Assembling Stage 2 Assembly Base (src/kernel.asm)..."
nasm -f elf32 src/kernel.asm -o build/kernel_asm.o

# 3. Compile Core Kernel C Logic from src/
echo "[*] Compiling Kernel Core (src/main.c)..."
# -Isrc/app tells GCC to find types.h and apps.h inside src/app/
gcc -m16 -march=i386 -ffreestanding -fno-pic -fno-PIE -fno-stack-protector -nostdlib -Isrc/app -c src/main.c -o build/main_c.o

echo "[*] Compiling Filesystem Layer (src/fs.c)..."
gcc -m16 -march=i386 -ffreestanding -fno-pic -fno-PIE -fno-stack-protector -nostdlib -Isrc/app -c src/fs.c -o build/fs.o

# 4. Automatically discover and compile all modular files inside src/app/
echo "[*] Compiling application modules from src/app/..."
APP_OBJECTS=""

# Modules that are loadable (not linked into kernel)
LOADABLE_MODULES="snake bf editor basic testr"

for c_file in src/app/*.c; do
    base_name=$(basename "$c_file" .c)
    
    # Skip loadable modules — they get separate treatment
    skip=0
    for lm in $LOADABLE_MODULES; do
        if [ "$base_name" = "$lm" ]; then skip=1; break; fi
    done
    if [ "$skip" -eq 1 ]; then
        echo "    -> (skipped for kernel: $base_name.c — loadable module)"
        continue
    fi
    
    echo "    -> Compiling Module: $base_name.c"
    gcc -m16 -march=i386 -ffreestanding -fno-pic -fno-PIE -fno-stack-protector -nostdlib -Isrc/app -c "$c_file" -o "build/app/${base_name}.o"
    APP_OBJECTS="$APP_OBJECTS build/app/${base_name}.o"
done

# 5. Link everything together using parent-directory linker.ld script
echo "[*] Linking kernel..."
ld -m elf_i386 -T linker.ld build/kernel_asm.o build/main_c.o build/fs.o $APP_OBJECTS -o build/kernel.elf

# 6. Strip BSS from the binary — real-mode kernel has no loader to zero it
echo "[*] Stripping BSS from kernel binary..."
objcopy -O binary -j .text -j .rodata -j .data build/kernel.elf build/kernel.bin

# 7. Combine and Pad into build/os.img
echo "[*] Synthesizing final floppy disk image..."
cat build/boot.bin build/kernel.bin > build/os.img

# Ensure it fits a standard 1.44MB floppy
truncate -s 1474560 build/os.img

# Create a real FAT12 filesystem on the floppy image using mtools.
# -B uses our boot sector as template (keeps boot code),
# mformat updates the BPB fields to match the geometry below.
echo "[*] Creating FAT12 filesystem with mformat..."
mformat -i build/os.img -B build/boot.bin -R 73 -h 2 -t 80 -s 18 -c 1 :: 2>/dev/null
echo "[+] FAT12 filesystem created (72 reserved, 2 FATs, 224 root entries)."

# 8. Build loadable modules
echo "[*] Building loadable modules..."

build_module() {
    local name="$1"
    echo "    -> Building module: $name"
    
    gcc -m16 -march=i386 -ffreestanding -fno-pic -fno-PIE -fno-stack-protector -nostdlib -Isrc/app -c "src/app/${name}.c" -o "build/mod_${name}.o"
    nasm -f elf32 src/module_entry.asm -o "build/mod_entry_${name}.o"
    ld -m elf_i386 -T src/module.ld "build/mod_entry_${name}.o" "build/mod_${name}.o" -o "build/${name}.mod"
    
    echo "    -> ${name}.mod built ($(stat -c%s "build/${name}.mod") bytes)"
}

build_module "snake"
build_module "bf"
build_module "editor"
build_module "basic"

# 9. Seed files into the FAT12 filesystem
echo "[*] Seeding files into FAT12 filesystem..."

# Default theme config (byte 0 = theme 0 = 0x1F white-on-blue)
printf '\x1f' > build/config.bin

# Default BF "Hello World" program
printf '%s' '++++++++[>++++[>++>+++>+++>+<<<<-]>+>+>->>+[<]<-]>>.>---.+++++++..+++.>>.<-.<.+++.------.--------.>>+.>++.' > build/hello.bf

mcopy -i build/os.img build/config.bin ::CONFIG.BIN  2>/dev/null
mcopy -i build/os.img build/hello.bf  ::HELLO.BF     2>/dev/null
mcopy -i build/os.img build/snake.mod ::SNAKE.BIN    2>/dev/null
mcopy -i build/os.img build/bf.mod     ::BRAINFUC.BIN 2>/dev/null
mcopy -i build/os.img build/editor.mod ::EDIT.BIN     2>/dev/null
mcopy -i build/os.img build/basic.mod  ::BASIC.BIN     2>/dev/null
echo "[+] Files seeded: CONFIG.BIN, HELLO.BF, SNAKE.BIN, BRAINFUC.BIN, EDIT.BIN, BASIC.BIN"

echo "[+] Build complete: build/os.img created successfully!"
echo "----------------------------------------------"

# 7. QEMU test 
read -p "Launch QEMU? (y/N): " run_qemu
if [[ "$run_qemu" =~ ^[Yy]$ ]]; then
    echo "[*] Launching QEMU..."
    qemu-system-i386 -fda build/os.img
fi

echo "----------------------------------------------"

# 8. Floppy Disk Burner
read -p "Write to floppy? (y/N): " write_floppy
if [[ "$write_floppy" =~ ^[Yy]$ ]]; then
    echo ""
    echo "Current connected block devices:"
    lsblk -d -o NAME,SIZE,TYPE,MODEL
    echo ""
    
    read -p "Enter the target drive path carefully (e.g., /dev/sdX or /dev/fd0): " floppy_dev
    
    if [ -b "$floppy_dev" ]; then
        echo "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
        echo "WARNING: Writing to $floppy_dev will completely destroy"
        echo "all underlying data on that target drive."
        echo "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
        read -p "Are you absolutely, 100% sure you want to proceed? (y/N): " confirm
        
        if [[ "$confirm" =~ ^[Yy]$ ]]; then
            echo "[*] Writing to disk..."
            sudo dd if=build/os.img of="$floppy_dev" bs=512 status=progress
            sync
            echo "[+] Flash complete!"
        else
            echo "[-] Floppy write aborted safely..."
        fi
    else
        echo "ERR: $floppy_dev is not a valid block device."
    fi
fi