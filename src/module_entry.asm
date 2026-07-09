bits 16

extern module_main
extern _bss_start
extern _bss_end

global _start
_start:
    ; Save kernel's stack (SS=0x1000) before changing segments.
    ; The return address from lcall is on the kernel's stack at SS:SP.
    push ss
    pop ax               ; AX = SS = 0x1000
    mov bx, sp           ; BX = SP (after o32 lcall pushed 8 bytes)

    ; Set up segment registers for the module's segment (0x2000)
    ; CRITICAL: DS must equal SS so that GCC's -m16 code correctly dereferences
    ; pointers to stack-allocated variables (like "line" in BASIC's input loop).
    mov cx, 0x2000
    mov ds, cx
    mov es, cx

    mov [saved_ss], ax
    mov [saved_sp], bx

    mov ss, cx
    mov sp, 0xFE00

    ; Zero BSS
    mov cx, _bss_end
    sub cx, _bss_start
    cmp cx, 0
    je .no_bss
    xor ax, ax
    mov di, _bss_start
    cld
    rep stosb
.no_bss:
    ; Call the module's main function
    o32 call module_main

    ; Restore kernel's segment registers and stack so retf can find the return address
    cli
    mov ss, [saved_ss]
    mov sp, [saved_sp]
    mov ax, 0x1000
    mov ds, ax
    mov es, ax
    sti
    o32 retf

section .data
saved_sp dw 0
saved_ss dw 0
