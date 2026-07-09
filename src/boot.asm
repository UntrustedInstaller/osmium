[org 0x7c00]
bits 16

; =====================================================================
;  BIOS PARAMETER BLOCK
; =====================================================================
jmp short boot_start
nop

db "OSMIUM  "
dw 512
db 1
dw 72
db 2
dw 224
dw 2880
db 0xF0
dw 9
dw 18
dw 2
dd 0
dd 0

db 0
db 0
db 0x29
dd 0x12345678
db "OSMIUMOS   "
db "FAT12   "

boot_start:
    xor ax, ax          ; Clear AX
    mov ds, ax          ; Set Data Segment to 0
    mov es, ax          ; Set Extra Segment to 0
    mov ss, ax          ; Set Stack Segment to 0
    mov sp, 0x7c00      ; Set Stack Pointer safely below bootloader

    ; Reset disk drive
    mov ah, 0x00
    int 0x13

    mov di, 3           ; 3 attempts to boot

.read_loop:
    push di             ; Save attempt counter

    ; Set destination buffer segment once
    mov bx, 0x1000
    mov es, bx
    xor bx, bx          ; ES:BX = 0x1000:0x0000

    ; Read head 0, sectors 2-18 (17 sectors)
    mov ah, 0x02
    mov al, 17
    mov ch, 0
    mov cl, 2
    mov dh, 0
    ; dl set by BIOS on boot
    int 0x13
    jc .read_fail

    ; Read head 1, sectors 1-18 (18 sectors) into 0x1000:0x2200
    mov ah, 0x02
    mov al, 18
    mov ch, 0
    mov cl, 1
    mov dh, 1
    mov bx, 0x2200      ; Buffer after first 17 sectors (17*512 = 0x2200)
    int 0x13
    jc .read_fail

    ; Read cylinder 1, head 0, sectors 1-18 into 0x1000:0x4600
    mov ah, 0x02
    mov al, 18
    mov ch, 1
    mov cl, 1
    mov dh, 0
    mov bx, 0x4600      ; Buffer after 35 sectors (35*512 = 0x4600)
    int 0x13
    jc .read_fail

    ; Read cylinder 1, head 1, sectors 1-18 (LBA 54-71) into 0x1000:0x6A00
    mov ah, 0x02
    mov al, 18
    mov ch, 1
    mov cl, 1
    mov dh, 1
    mov bx, 0x6A00      ; Buffer after 53 sectors (53*512 = 0x6A00)
    int 0x13
    jc .read_fail

    ; Read cylinder 2, head 0, sectors 1-7 (LBA 72-78) into 0x1000:0x8E00
    mov ah, 0x02
    mov al, 7
    mov ch, 2
    mov cl, 1
    mov dh, 0
    mov bx, 0x8E00      ; Buffer after 71 sectors (71*512 = 0x8E00)
    int 0x13
    jc .read_fail
    jmp .success

.read_fail:
    xor ax, ax          ; Reset disk
    int 0x13

    pop di
    dec di
    jnz .read_loop

    jmp dsk_err

.success:
    pop di
    jmp 0x1000:0x0000

dsk_err:
    mov ah, 0x0e
    mov al, 'E'
    int 0x10
.halt:
    cli
    hlt
    jmp .halt

times 510-($-$$) db 0
dw 0xaa55