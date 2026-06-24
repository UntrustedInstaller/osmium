#!/bin/bash

set -e

echo "=============================================="
echo "                 OS BUILDER                   "
echo "=============================================="

# Ensure the build directory actually exists before writing to it
mkdir -p build

# 1. Assemble Stage 1 (Boot Sector) from src/ to build/
echo "[*] Assembling Stage 1 (src/boot.asm)..."
nasm -f bin src/boot.asm -o build/boot.bin

# 2. Assemble Stage 2 (Kernel / CLI) from src/ to build/
echo "[*] Assembling Stage 2 (src/kernel.asm)..."
nasm -f bin src/kernel.asm -o build/kernel.bin

# 3. Combine and Pad into build/os.img
echo "[*] Synthesizing floppy disk image (build/os.img)..."
cat build/boot.bin build/kernel.bin > build/combined.tmp

# Create a blank 1.44MB template in the build folder
dd if=/dev/zero of=build/os.img bs=1024 count=1440 status=none
# Inject our code into the template
dd if=build/combined.tmp of=build/os.img conv=notrunc status=none

# Clean up intermediate files, leaving just the final image and binaries
rm build/combined.tmp

echo "[+] Success! Compiled image generated as 'build/os.img'"
echo "----------------------------------------------"

# 4. Interactive QEMU Emulation Selection
read -p "Would you like to test your OS in QEMU right now? (y/N): " run_qemu
if [[ "$run_qemu" =~ ^[Yy]$ ]]; then
    echo "[*] Launching QEMU..."
    qemu-system-i386 -fda build/os.img
fi

echo "----------------------------------------------"

# 5. Interactive Physical Floppy Disk Burner
read -p "Would you like to write this build to a physical floppy disk? (y/N): " write_floppy
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
            echo "[-] Floppy write aborted safely."
        fi
    else
        echo "[-] Error: '$floppy_dev' is not a valid block device. Aborting."
    fi
fi

echo "=============================================="
echo "               Build Process End              "
echo "=============================================="