bits 16 ; Do I need to explain this?

; Set all those pesky global calls
global kernel_init
global asm_print_str
global asm_print_char
global cls

; Calling the shell function from the C file
extern iridium_main
extern cur_col

; BSS boundaries for zeroing
extern _bss_start
extern _bss_end

; For kernel memory footprint (about screen)
extern _kernel_end

; FS API helpers (src/main.c)
extern api_fs_read_file
extern api_fs_write_file

; System info
extern get_mem_size

; =====================================================================
;    KERNEL INITIALIZATION
; =====================================================================

kernel_init:
    cld

                        ; Sync data segment registers to 0x1000
    mov ax, 0x1000
    mov ds, ax
    mov es, ax

                        ; Sync Stack Segment to 0x1000
    mov ss, ax
    
                        ; Clear the entire 32-bit ESP/EBP registers to wipe BIOS garbage
    xor esp, esp        ; Zero out the full 32-bit register
    mov sp, 0xFFF0      ; Assign SP safely to the top of the 0x1000 segment boundary
    xor ebp, ebp        ; Zero out the full 32-bit EBP register
    mov bp, sp          ; Align base pointer with stack pointer

    mov [boot_drive], dl

                        ; Zero BSS — kernel binary no longer includes BSS
    xor al, al
    mov edi, _bss_start
    mov ecx, _bss_end
    sub ecx, edi
    rep stosb

                        ; Store kernel memory footprint (end of .bss)
    mov ax, _kernel_end
    mov [kernel_mem_end], ax

                        ; Intensive backgrounds and disable blinking (IBM PC leftovers)
    mov ax, 0x1003
    mov bl, 0x00        ; Disable blinking / enable intensive backgrounds
    int 0x10

                        ; Initial screen clear and paint
    call cls

                        ; Jump to the C shell
    call iridium_main

                        ; Safety net catch if C somehow crashes or returns
    cli
.halt:
    hlt
    jmp .halt


; =====================================================================
;    LOW-LEVEL HARDWARE UTILITIES (HAL)
; =====================================================================

asm_print_str:
    pusha
    push ds
    push es

    mov ax, 0x1000
    mov ds, ax
    mov es, ax

.loop:
    lodsb
    cmp al, 0
    je .done
    ; Route every character through asm_print_char so scroll
    ; logic is applied consistently in one place
    push si
    mov ah, al          ; Stash char
    ; Call the char routine inline to avoid segment weirdness
    mov al, ah
    call do_print_char
    pop si
    jmp .loop
.done:
    pop es
    pop ds
    popa
    ret

asm_print_char:
    push ax
    push bx
    push cx
    push dx

    mov ah, al              ; Preserve the character
    call do_print_char

    pop dx
    pop cx
    pop bx
    pop ax
    ret

do_print_char:
    push ax
    push bx
    push cx
    push dx

    cmp al, 13
    je .do_cr
    cmp al, 10
    je .do_lf
    cmp al, 8
    je .do_bs

    ; Normal character — use TTY output (handles cursor advance + scroll)
    mov ah, 0x0E
    mov bh, 0x00
    int 0x10
    jmp .exit

    ; Backspace
.do_bs:
    mov ah, 0x03
    mov bh, 0x00
    int 0x10
    test dl, dl
    jz .exit
    dec dl
    mov ah, 0x02
    mov bh, 0x00
    int 0x10
    jmp .exit

    ; Carriage Return
.do_cr:
    mov ah, 0x03
    mov bh, 0x00
    int 0x10
    mov dl, 0
    mov ah, 0x02
    mov bh, 0x00
    int 0x10
    jmp .exit

    ; Line Feed
.do_lf:
    mov ah, 0x03
    mov bh, 0x00
    int 0x10
    inc dh
    cmp dh, 25
    jl .lf_set
    call do_scroll
    mov dh, 24
.lf_set:
    mov ah, 0x02
    mov bh, 0x00
    int 0x10

.exit:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

do_scroll:
    push ax
    push bx
    push cx
    push dx

    mov ah, 0x06            ; Scroll up
    mov al, 1               ; Scroll 1 line
    mov bh, [cur_col]       ; Fill attribute  ← THIS is the fix
    mov cx, 0x0000          ; Top-left  (row 0, col 0)
    mov dx, 0x184F          ; Bottom-right (row 24, col 79)
    int 0x10

    pop dx
    pop cx
    pop bx
    pop ax
    ret

cls:
    pusha
    mov ah, 0x06        ; Scroll up window function
    mov al, 0           ; Clear entire screen
    mov bh, [cur_col]   ; Use active color attribute to paint background
    mov cx, 0x0000      ; Top-left corner (Row 0, Col 0)
    mov dx, 0x184F      ; Bottom-right corner (Row 24, Col 79)
    int 0x10            ; Call BIOS video interrupt

    ; Reset cursor position back to 0,0
    mov ah, 0x02
    mov bh, 0
    mov dh, 0
    mov dl, 0
    int 0x10
    popa
    ret


; =====================================================================
;    DISK I/O HAL
; =====================================================================

global asm_read_sector
global asm_write_sector

; Read one sector via BIOS int 13h
; Input:  AX = LBA, BX = buffer offset (within DS)
; Output: AX = 0 success, 1 failure
asm_read_sector:
    push cx
    push dx
    push bx

    call lba_to_chs

    pop bx
    push es
    push ds
    pop es
    mov ah, 0x02
    mov al, 1
    mov dl, [boot_drive]
    int 0x13

    mov ax, 0
    jnc .read_ok
    mov ax, 1
.read_ok:
    pop es
    pop dx
    pop cx
    ret

; Write one sector via BIOS int 13h
; Input:  AX = LBA, BX = buffer offset (within DS)
; Output: AX = 0 success, 1 failure
asm_write_sector:
    push cx
    push dx
    push bx

    call lba_to_chs

    pop bx
    push es
    push ds
    pop es
    mov ah, 0x03
    mov al, 1
    mov dl, [boot_drive]
    int 0x13

    mov ax, 0
    jnc .write_ok
    mov ax, 1
.write_ok:
    pop es
    pop dx
    pop cx
    ret

; Read multiple consecutive sectors via BIOS int 13h
; Input:  AX = LBA, CX = count, ES:BX = buffer
; Output: AL = 0 success, 1 error
global asm_read_sectors
asm_read_sectors:
    test cx, cx
    jz .ars_zero
    push si
    push di
    push bp
    push es             ; save caller's ES (module segment)

    push ds
    pop es              ; ES = DS = kernel segment

    mov si, ax          ; SI = current LBA
    mov di, cx          ; DI = remaining count

.ars_loop:
    test di, di
    jz .ars_done_ok

    mov ax, si
    call lba_to_chs      ; CH=cyl, CL=sector, DH=head

    ; Sectors remaining on this track = 19 - CL
    mov al, 19
    sub al, cl
    jz .ars_done_err

    mov ah, 0
    cmp ax, di
    jbe .ars_count_set
    mov ax, di
.ars_count_set:
    push ax              ; save batch count

    mov ah, 0x02
    mov dl, [boot_drive]
    int 0x13
    jc .ars_pop_err

    pop ax               ; AX = batch count

    sub di, ax           ; remaining -= batch

    ; Advance buffer: batch * 512
    push ax
    mov cx, 512
    mul cx               ; DX:AX = batch * 512
    add bx, ax           ; buffer += batch * 512
    pop ax               ; AX = batch count

    ; Advance LBA by batch
    add si, ax

    jmp .ars_loop

.ars_done_ok:
    xor al, al
    jmp .ars_done

.ars_pop_err:
    pop ax               ; clean up batch count from stack
.ars_done_err:
    mov al, 1

.ars_done:
    pop es              ; restore caller's ES
    pop bp
    pop di
    pop si
    ret

.ars_zero:
    xor al, al
    ret

; Convert LBA to CHS for 1.44MB floppy (80 cyl, 2 heads, 18 sect/track)
; Input:  AX = LBA (0-2879)
; Output: CH = cylinder, CL = sector, DH = head
; Preserves: DL (drive number)
lba_to_chs:
    push ax
    push bx
    push dx

    xor dx, dx
    mov bx, 18
    div bx             ; AX = LBA/18, DX = LBA%18

    mov bx, ax         ; BX = LBA/18
    mov cl, dl
    add cl, 1          ; CL = sector (1-18)

    mov ax, bx
    xor dx, dx
    mov bx, 2
    div bx             ; AX = cylinder, DX = head

    mov ch, al         ; CH = cylinder (0-79)
    mov bx, dx         ; BX = head
    pop dx             ; DX = original (DL preserved)
    mov dh, bl         ; DH = head

    pop bx
    pop ax
    ret

; =====================================================================
;    KERNEL DATA & VARIABLE STORAGE
; =====================================================================

global boot_drive
boot_drive  db 0

; Staging buffer for filenames passed via module FS API (CX=9,10)
api_fname   times 13 db 0

global kernel_mem_end
kernel_mem_end dw 0

; INT 60h handler stack save area (accessed via CS override before DS is set)
saved_handler_ss  dw 0
saved_handler_sp  dw 0
saved_handler_ax  dw 0

; =====================================================================
;    INT 60H API HANDLER — Module-to-Kernel API
; =====================================================================
; Called by loadable modules via int 60h.
; Module must set ES = module segment before calling (for pointer args).
;
; CX = function number
;   0 = print_str     (ES:BX = string)
;   1 = print_char    (AL = char)
;   2 = get_key       → AX = keycode
;   3 = clear_screen
;   4 = gotoxy         (DL = col, DH = row)
;   5 = read_sector   (AX = LBA, ES:BX = buf) → AL=0 ok, 1 err
;   6 = write_sector  (AX = LBA, ES:BX = buf) → AL=0 ok, 1 err
;   7 = get_cursor     → (DL = row, DH = col)
;   8 = print_int     (AX = value)
;   9 = fs_read_file  (ES:BX = name, AX = dest_off, DX = max) → AL=0 ok, 1 err
;  10 = fs_write_file (ES:BX = name, AX = src_off, DX = size) → AL=0 ok, 1 err
;  11 = get_cur_col   → AL = current colour attribute (0x1F etc.)
;  12 = set_cur_col   (AL = attribute)
;  13 = get_mem_size  → AX = RAM in KB
;  14 = get_kernel_end → AX = kernel memory end (size in bytes)
; =====================================================================
global int60_handler
int60_handler:
    push ds
    push si
    push di
    push es

    ; Save module's SS:SP and AX before switching to kernel stack.
    ; CS=0x1000 (kernel code), so CS-override reaches the variables.
    mov [cs:saved_handler_ss], ss
    mov [cs:saved_handler_sp], sp
    mov [cs:saved_handler_ax], ax

    ; Switch to kernel stack so that DS=SS=0x1000.
    ; GCC -m16 generates code that assumes DS == SS; CPU C functions
    ; that take addresses of stack variables (fs_read_file etc.) will
    ; dereference pointers through DS, so DS must equal the stack segment.
    mov ax, 0x1000
    mov ds, ax
    mov ss, ax
    mov sp, 0xFC00

    mov ax, [saved_handler_ax]

    cmp cx, 0
    je .print_str
    cmp cx, 1
    je .print_char
    cmp cx, 2
    je .get_key
    cmp cx, 3
    je .clear_screen
    cmp cx, 4
    je .gotoxy
    cmp cx, 5
    je .read
    cmp cx, 6
    je .write
    cmp cx, 7
    je .get_cursor
    cmp cx, 8
    je .print_int
    cmp cx, 9
    je .fs_read
    cmp cx, 10
    je .fs_write
    cmp cx, 11
    je .get_cur_col
    cmp cx, 12
    je .set_cur_col
    cmp cx, 13
    je .get_mem_size
    cmp cx, 14
    je .get_kernel_end
    jmp .done

.print_str:
    ; ES:BX = string (module set ES = module seg before int 60h)
    mov si, bx
    cld
.ps_loop:
    mov al, [es:si]
    cmp al, 0
    je .done
    push si
    call do_print_char
    pop si
    inc si
    jmp .ps_loop

.print_char:
    call do_print_char
    jmp .done

.get_key:
    mov ah, 0x00
    int 0x16
    ; AX = keycode
    jmp .done

.clear_screen:
    call cls
    jmp .done

.gotoxy:
    mov ah, 0x02
    mov bh, 0
    int 0x10
    jmp .done

.read:
    ; AX = LBA, ES:BX = buffer
    ; ES already = module segment (set by module before int 60h)
    mov si, ax
    mov di, bx
    mov ax, si
    call lba_to_chs
    mov bx, di
    mov ah, 0x02
    mov al, 1
    mov dl, [boot_drive]   ; DS = kernel segment → correct
    int 0x13
    mov al, 0
    jnc .read_ok
    mov al, 1
.read_ok:
    jmp .done

.write:
    mov si, ax
    mov di, bx
    mov ax, si
    call lba_to_chs
    mov bx, di
    mov ah, 0x03
    mov al, 1
    mov dl, [boot_drive]
    int 0x13
    mov al, 0
    jnc .write_ok
    mov al, 1
.write_ok:
    jmp .done

.get_cursor:
    mov ah, 0x03
    mov bh, 0
    int 0x10
    ; DL = col, DH = row
    jmp .done

.get_cur_col:
    mov al, [cur_col]
    jmp .done

.set_cur_col:
    mov [cur_col], al
    jmp .done

.get_mem_size:
    o32 call get_mem_size
    jmp .done

.get_kernel_end:
    mov ax, [kernel_mem_end]
    jmp .done

.print_int:
    ; AX = value
    push bx
    push cx
    push dx
    xor cx, cx
    mov bx, 10
.pi_loop:
    xor dx, dx
    div bx
    push dx
    inc cx
    cmp ax, 0
    jne .pi_loop
.pi_out:
    pop dx
    add dl, '0'
    mov al, dl
    call do_print_char
    loop .pi_out
    pop dx
    pop cx
    pop bx
    jmp .done

; --------------------------------------------------------------------
;  FS API for modules — CX=9 (read file), CX=10 (write file)
; --------------------------------------------------------------------
; Called with:
;   ES:BX = filename (in module segment)
;   AX    = buffer offset within module segment (dest for read, src for write)
;   DX    = max bytes (read) or size (write)
; Returns:
;   AL = 0 success, 1 error

.fs_read:
    push ax
    push dx
    push bx

    ; Copy filename from ES:BX to api_fname (up to 12 chars + null)
    mov si, bx
    mov di, api_fname
    cld
.fr_cp:
    mov al, [es:si]
    cmp al, 0
    je .fr_cp_dn
    cmp di, api_fname + 12
    je .fr_cp_dn
    mov [di], al
    inc si
    inc di
    jmp .fr_cp
.fr_cp_dn:
    mov byte [di], 0

    pop bx
    pop dx              ; DX = max
    pop ax              ; AX = dest_off

    ; api_fs_read_file(name, dest_off, max)
    ; Push args right-to-left (32-bit cdecl ABI with -m16)
    xor ecx, ecx
    mov cx, dx
    push ecx            ; arg 3: max
    xor ecx, ecx
    mov cx, ax
    push ecx            ; arg 2: dest_off
    xor ecx, ecx
    mov cx, api_fname
    push ecx            ; arg 1: name
    o32 call api_fs_read_file
    add sp, 12          ; pop args
    jmp .done

.fs_write:
    push ax
    push dx
    push bx

    mov si, bx
    mov di, api_fname
    cld
.fw_cp:
    mov al, [es:si]
    cmp al, 0
    je .fw_cp_dn
    cmp di, api_fname + 12
    je .fw_cp_dn
    mov [di], al
    inc si
    inc di
    jmp .fw_cp
.fw_cp_dn:
    mov byte [di], 0

    pop bx
    pop dx              ; DX = size
    pop ax              ; AX = src_off

    ; api_fs_write_file(name, src_off, size)
    xor ecx, ecx
    mov cx, dx
    push ecx            ; arg 3: size
    xor ecx, ecx
    mov cx, ax
    push ecx            ; arg 2: src_off
    xor ecx, ecx
    mov cx, api_fname
    push ecx            ; arg 1: name
    o32 call api_fs_write_file
    add sp, 12
    jmp .done

.done:
    ; Restore module's stack (module SS:SP saved at handler entry)
    cli
    mov ss, [saved_handler_ss]
    mov sp, [saved_handler_sp]
    sti

    pop es
    pop di
    pop si
    pop ds
    iret