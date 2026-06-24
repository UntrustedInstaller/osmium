[org 0x7c00]
bits 16

start:
    xor ax, ax          ; Clear AX
    mov ds, ax          ; Set Data Segment to 0
    mov es, ax          ; Set Extra Segment to 0
    mov ss, ax          ; Set Stack Segment to 0
    mov sp, 0x7c00      ; Set Stack Pointer safely below bootloader

    ; Reset disk drive
    mov ah, 0x00
    int 0x13

    ; Read Stage 2 from disk
    mov ah, 0x02        ; BIOS read sectors function
    mov al, 4           ; Number of sectors to read (increase as your CLI grows)
    mov ch, 0           ; Cylinder 0
    mov cl, 2           ; Sector 2 (Sector 1 is Stage 1)
    mov dh, 0           ; Head 0
    ; dl is automatically set to the boot drive number by the BIOS on startup

    ; Set destination buffer to 0x1000:0x0000
    mov bx, 0x1000
    mov es, bx
    mov bx, 0x0000

    int 0x13
    jc disk_error       ; Jump if carry flag is set (error)

    ; Jump to Stage 2 / Kernel!
    jmp 0x1000:0x0000

disk_error:
    ; (Hang or print error message here)
    jmp $

times 510-($-$$) db 0
dw 0xaa55