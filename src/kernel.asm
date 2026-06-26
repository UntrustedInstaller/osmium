bits 16
org 0x0000          ; Loaded by Stage 1 at segment 0x1000, offset 0x0000

; =====================================================================
;    KERNEL INITIALIZATION & CORE INTERRUPTS
; =====================================================================

kernel_init:
    cld

    ; Sync data segment registers to 0x1000
    mov ax, 0x1000
    mov ds, ax
    mov es, ax

    mov [boot_drive], dl

    ; Intensive backgrounds and disable blinking
    mov ax, 0x1003
    mov bl, 0x00 ; 0x00=Disable the blinking/enable intensive backgrounds
    int 0x10

    ; Initial screen clear and paint
    call cls

    ; Beep on boot
    mov ah, 0x0E
    mov al, 7
    int 0x10

    ; Print the OSMIUM banner
    mov si, msg_start
    call print_str
    call print_ram
    mov si, msg_end
    call print_str

    ; Clean VGA Palette Matrix Only (No text headers, no double spacing)
    call render_mtx
    mov si, newline_msg
    call print_str

    ; Print the initial boot "READY." prompt
    mov si, ready_msg
    call print_str

prompt:
    mov si, prompt_msg
    call print_str

    ; Reset command buffer index and length tracker before starting input loop
    mov word [cmd_idx], 0
    mov word [cmd_len], 0

cli_loop:
    ; Read character via BIOS keyboard services
    mov ah, 0x00
    int 0x16            ; Returns ASCII in AL, Scancode in AH

    ; Intercept up arrow scan code
    cmp ah, 0x48
    je handle_up_arrow

    ; Intercept left arrow scan code
    cmp ah, 0x4B
    je handle_left

    ; Intercept right arrow scan code
    cmp ah, 0x4D
    je handle_right

    ; Handle Enter (Carriage Return)
    cmp al, 13
    je handle_enter

    ; Handle Backspace
    cmp al, 8
    je handle_bkspc

; Check for buffer overflow safety (63 chars max + null terminator)
    cmp word [cmd_idx], 63
    jge cli_loop        ; Ignore extra typing if buffer full

    ; Echo character to screen
    mov ah, 0x0E
    mov bh, 0
    mov bl, [cur_col]
    int 0x10

    ; Save character to buffer
    mov bx, [cmd_idx]
    mov [cmd_buf + bx], al

    ; --- SYNCHRONIZE TRACKERS ---
    mov ax, [cmd_idx]
    cmp ax, [cmd_len]
    jne .skip_len_grow     ; If cursor is inside the word, don't grow total length
    inc word [cmd_len]     ; Expand string size safely

.skip_len_grow:
    inc word [cmd_idx]   ; Advance editing cursor forward
    jmp cli_loop

; =====================================================================
;    COMMAND LINE INTERFACE (CLI) LOOP & INTERCEPTS
; =====================================================================

handle_up_arrow:
.clear_loop:
    cmp word [cmd_idx], 0
    je .restore_data

    dec word [cmd_idx]

    mov ah, 0x0E
    mov al, 8
    int 0x10
    mov al, ' '
    int 0x10
    mov al, 8
    int 0x10
    jmp .clear_loop
.restore_data:
    mov si, hist_buf
    mov di, cmd_buf
    mov cx, 0
.copy_loop:
    lodsb
    stosb
    or al, al
    jz .done_copy
    inc cx
    jmp .copy_loop
.done_copy:
    mov [cmd_idx], cx
    mov [cmd_len], cx
    mov si, cmd_buf
    call print_str
    jmp cli_loop

handle_left:
    cmp word [cmd_idx], 0
    je cli_loop            ; If at index 0, don't cross prompt boundary

    dec word [cmd_idx]   ; Step back in RAM

    ; Physically move cursor left
    mov ah, 0x03
    mov bh, 0
    int 0x10               ; Get current row+column
    dec dl                 ; Decrement column
    mov ah, 0x02
    int 0x10               ; Set new cursor position
    jmp cli_loop

handle_right:
    ; Read current position vs length
    mov ax, [cmd_idx]
    cmp ax, [cmd_len]
    je cli_loop            ; If index is equal to total length, don't allow step-out

    inc word [cmd_idx]

    ; Physically move cursor right
    mov ah, 0x03
    mov bh, 0
    int 0x10               ; Get current row+column
    inc dl                 ; Increment column
    mov ah, 0x02
    int 0x10               ; Set new cursor position
    jmp cli_loop

handle_bkspc:
    ; Guard against backspacing past the prompt start
    cmp word [cmd_idx], 0
    je cli_loop

    ; Decrement buffer index
    dec word [cmd_idx]
    mov bx, [cmd_idx]
    mov byte [cmd_buf + bx], 0  ; Clear character in buffer

    ; Destructive backspace sequence on screen
    mov ah, 0x0E
    mov al, 8
    int 0x10            ; Move cursor back
    mov al, ' '
    int 0x10            ; Overwrite character with space
    mov al, 8
    int 0x10            ; Move cursor back again
    jmp cli_loop

handle_enter:
    ; Print a newline sequence
    mov si, newline_msg
    call print_str

    ; Null-terminate the command buffer string
    mov bx, [cmd_len]
    mov byte [cmd_buf + bx], 0

    ; Skip processing if the user just pressed enter on an empty line
    cmp word [cmd_len], 0
    je prompt

    call save_hist

; =====================================================================
;    COMMAND PARSER & EXECUTION TARGETS
; =====================================================================
    
    ; Check "echo " (Inline peek)
    cmp byte [cmd_buf], 'e'
    jne .not_echo
    cmp byte [cmd_buf + 1], 'c'
    jne .not_echo
    cmp byte [cmd_buf + 2], 'h'
    jne .not_echo
    cmp byte [cmd_buf + 3], 'o'
    jne .not_echo
    cmp byte [cmd_buf + 4], ' '
    je exe_echo

.not_echo:

    ; Check "theme " (Inline peek)
    cmp byte [cmd_buf], 't'
    jne .not_theme
    cmp byte [cmd_buf + 1], 'h'
    jne .not_theme
    cmp byte [cmd_buf + 2], 'e'
    jne .not_theme
    cmp byte [cmd_buf + 3], 'm'
    jne .not_theme
    cmp byte [cmd_buf + 4], 'e'
    jne .not_theme
    cmp byte [cmd_buf + 5], ' '
    je exe_theme

.not_theme:

    ; Check "help"
    mov si, cmd_buf
    mov di, cmd_help
    call strcmp
    jc exe_hlp

    ; Check "clear"
    mov si, cmd_buf
    mov di, cmd_clear
    call strcmp
    jc exe_clear

    ; Check "greeting"
    mov si, cmd_buf
    mov di, cmd_greeting
    call strcmp
    jc exe_greet

    ; Check "palette"
    mov si, cmd_buf
    mov di, cmd_pal
    call strcmp
    jc exe_pal

    ; Check "reboot"
    mov si, cmd_buf
    mov di, cmd_reboot
    call strcmp
    jc exe_reboot

    ; Check "mem"
    mov si, cmd_buf
    mov di, cmd_mem
    call strcmp
    jc exe_mem

    ; Check "edit"
    mov si, cmd_buf
    mov di, cmd_edit
    call strcmp
    jc exe_edit

    ; Check "dir"
    mov si, cmd_buf
    mov di, cmd_dir
    call strcmp
    jc exe_dir

    ; Check "hexdump"
    mov si, cmd_buf
    mov di, cmd_dump
    call strcmp
    jc exe_dump

    ; Unknown command fallback
    mov si, unknown_msg
    call print_str
    jmp prompt


; --- COMMAND EXECUTION TARGETS ---

exe_hlp:
    mov si, help_text
    call print_str
    jmp prompt

exe_clear:
    call cls
    jmp prompt

exe_greet:
    mov si, msg_start
    call print_str
    call print_ram
    mov si, msg_end
    call print_str
    jmp prompt

exe_pal:
    call render_palette
    jmp prompt

exe_theme:
    mov si, cmd_buf
    add si, 6          ; Ignore "theme " to reach the theme number
    mov al, [si]       ; Grab the ASCII digit (0-4)

    ;Bad digit checks, fail on wrong number
    cmp al, '0'
    jl .bad_theme
    cmp al, '4'
    jg .bad_theme

    sub al, '0'         ; Convert ASCII ('0'-'4') to raw number index (0-4)

    mov bx, theme_table ; Point BX to the theme array
    xlat                ; Look up byte at bx+al and store back to al

    mov [cur_col], al ; Overwrite system's active color variable

    call cls ;Clear screen instantly to apply the new theme

    ;Re-render welcome header so the screen isn't lonely
    mov si, msg_start
    call print_str
    call print_ram
    mov si, msg_end
    call print_str

.bad_theme:
    jmp prompt

exe_echo:
    mov si, cmd_buf ; Move the buffer to si
    add si, 5          ; Add 5 to pointer (Ignore "echo ")
    call print_str

    mov si, newline_msg
    call print_str
    jmp prompt

exe_reboot:
    jmp 0xFFFF:0x0000

exe_mem:
    call print_ram
    mov si, newline_msg
    call print_str
    jmp prompt

exe_dir:
    pusha                ; Save all caller registers for structural safety
    call lod_dir_sect    ; Grab sector 18
    mov bx, dir_buf
    mov cx, 32           ; Max 32 entries to check
    xor dx, dx           ; Did we even find any files?

.print_loop:
    cmp byte [bx], 0     ; Is the entry empty?
    je .skip_entry       ; Not allocated? ignore.

    inc dx               ; Mark that we found a file

    ; Print 12 byte filename character by character
    push cx              ; Save outer loop counter (32)
    mov si, bx
    mov cx, 12

.print_name:
    lodsb
    push cx
    call print_char
    pop cx
    loop .print_name
    pop cx               ; Restore outer loop counter cleanly

    ; Newline after displaying details
    push cx              ; <--- CRITICAL: Protect CX from BIOS scroll wipe!
    push si
    mov si, newline_msg
    call print_str
    pop si
    pop cx               ; <--- CRITICAL: Restore CX safely!

.skip_entry:
    add bx, 16            ; Move to next 16 byte record
    loop .print_loop

    cmp dx, 0             ; Did our file counter stay 0?
    jnz .done             ; If dx is NOT zero, we found files! Skip to done.

    mov si, empty_dir_msg ; If dx IS zero, load the empty directory string...
    call print_str        ; ...and print it!

.done:
    popa                 ; Restore all registers cleanly
    jmp prompt

exe_dump:
    pusha
    mov si, 0x0000     ; Start by dumping offset 0x0000
    mov cx, 16         ; Dump 16 lines (16 lines of 16 bytes = 256 bytes)

.line_loop:
    push cx            ; Save line counter

    ; Begin printing current offset address
    mov ax, si
    call print_hex_word
    mov al, ':'
    call print_char
    mov al, ' '
    call print_char

    ; Print 16 hex bytes, side by side.
    mov cx, 16
.byte_loop:
    mov al, [si]        ; Grab byte from pointer
    call print_hex_byte ; Print two-digit hex representation
    mov al, ' '
    call print_char     ; Space between 
    inc si              ; Move to next byte
    loop .byte_loop

    push si
    mov si, newline_msg
    call print_str
    pop si
    
    pop cx             ; Restore counter
    loop .line_loop

    popa
    jmp prompt

; --- HEX FUNCTIONS ---
print_hex_byte:
    push ax
    push bx

    ;Upper
    shr al, 4          ; Shift down to get 4 high bits
    mov bx, hex_digits
    xlat               ; Look up ASCII character in hex_digits string
    call print_char

    ;Lower
    pop ax             ; Get original byte
    push ax
    and al, 0x0F       ; Mask out 4 high bits
    mov bx, hex_digits
    xlat
    call print_char

    pop bx
    pop ax
    ret

print_hex_word:
    ;Prints 16-bit word in ax as 4 hex digits
    push ax
    mov al, ah         ; Print high first
    call print_hex_byte
    pop ax             ; Print low next
    call print_hex_byte
    ret

; =====================================================================
;  UTILITY HARDWARE & RENDERING FUNCTIONS
; =====================================================================

print_str:
.loop:
    lodsb
    or al, al
    jz .done

    ; --- BIOS SCROLL BACKGROUND COLOR FIX ---
    cmp al, 10              ; Is it a Line Feed?
    jne .not_lf
    
    push ax                 ; Save the character
    mov ah, 0x03            ; BIOS: Read current cursor position
    mov bh, 0
    int 0x10                ; Returns current row in DH, column in DL
    pop ax                  ; Restore the character
    
    cmp dh, 24              ; Are we on the very bottom row of the terminal?
    jne .not_lf
    
    ; Perform the scroll manually using our system color theme.
    push dx                 ; Save our current column coordinate (DL)
    mov ah, 0x06            ; BIOS: Scroll Window Up
    mov al, 1               ; Scroll up by exactly 1 line
    mov bh, [cur_col] ; Force the blank row to inherit our theme color
    mov ch, 0               ; Upper left row: 0
    mov cl, 0               ; Upper left column: 0
    mov dh, 24              ; Lower right row: 24
    mov dl, 79              ; Lower right column: 79
    int 0x10
    pop dx                  ; Restore our original column coordinate
    
    ; Position the cursor back on the bottom row at its original column
    mov ah, 0x02            ; BIOS: Set cursor position
    mov bh, 0
    mov dh, 24              ; Lock to the bottom row
    int 0x10
    jmp .loop               ; Skip default BIOS rendering to avoid the black row bug

.not_lf:
    call print_char
    jmp .loop
.done:
    ret

print_char:
    ;Direct character printing, keeping core registers intact
    push ax
    push bx
    mov ah, 0x0E
    mov bh, 0
    mov bl, [cur_col]
    int 0x10
    pop bx
    pop ax
    ret

cls:
    mov ah, 0x06        ; Scroll up function (clears window)
    mov al, 0           ; Entire screen
    mov bh, [cur_col]
    mov ch, 0           ; Top row
    mov cl, 0           ; Left col
    mov dh, 24          ; Bottom row
    mov dl, 79          ; Right col
    int 0x10

    ; Reset cursor to top-left (0,0)
    mov ah, 0x02
    mov bh, 0
    mov dh, 0
    mov dl, 0
    int 0x10
    ret

print_ram:
    pusha

    ; Hardware fetch
    int 0x12

    xor bx, bx   ; Clear bx to 0 to store digit count
    mov cx, 10   ; Move our divisor to cx

.math_loop:
    xor dx, dx   ; Clear dx register
    div cx       ; Divide ax by 10, quotient to ax, remainder to dx

    add dl, '0'  ; Convert raw remainder (0-9) to ASCII equivalent
    push dx      ; Push the character to recall later
    inc bx       ; Increment the digit count

    cmp ax, 0    ; Anything left in ax?
    jne .math_loop

.print_loop:
    pop dx       ; Recall topmost character of the stack into dx
    mov al, dl   ; Move character value to al, where the BIOS looks
    mov ah, 0x0E ; Teletype print subcommand
    int 0x10     ; Trigger video interrupt to render

    dec bx       ; Subtract from digit count
    jnz .print_loop

    popa         ; Go back to original register states
    ret

render_palette:
    pusha        ; Save registers for caller safety
    
    mov si, palette_header
    call print_str

    call render_mtx

    ; Push cursor down past the banner block (Exclusive to standard palette command)
    mov si, newline_msg
    call print_str
    mov si, newline_msg
    call print_str

    popa                       ; Restore registers
    ret

render_mtx:
    pusha
    mov byte [color_idx], 0    ; Start at color 0 (Black)

.color_loop:
    ; 1. Construct the attribute byte
    mov al, [color_idx]
    shl al, 4                  ; Shift index to bits 4-7 (Background Color)
    or al, 0x0F                ; Set bits 0-3 to High-Intensity White text (0x0F)
    mov bl, al                 ; BL = Full custom Attribute byte

    ; 2. Determine Hex Character representation
    mov al, [color_idx]
    cmp al, 10
    jl .is_digit
    add al, 'A' - 10           ; Convert values 10-15 to 'A'-'F'
    jmp .render
.is_digit:
    add al, '0'                ; Convert values 0-9 to '0'-'9'

.render:
    ; 3. Draw character and attribute to screen via BIOS
    mov ah, 0x09               ; Write Character/Attribute function
    mov bh, 0                  ; Display Page 0
    mov cx, 2                  ; Draw 2 copies side-by-side to make a wide block
    int 0x10

    ; 4. Advance the cursor manually (since ah=0x09 doesn't auto-advance)
    mov ah, 0x03               ; Read current cursor coordinates
    mov bh, 0
    int 0x10                   ; Returns current row in DH, column in DL
    add dl, 2                  ; Advance right by 2 units to match our block width
    mov ah, 0x02               ; Apply updated cursor location
    int 0x10

    ; Loop iteration processing
    inc byte [color_idx]
    cmp byte [color_idx], 16
    jl .color_loop

    popa
    ret

strcmp:
    ; Inputs: SI = String A, DI = String B
    ; Outputs: Carry Flag (CF) set if equal, cleared if not equal
    push si
    push di
.loop:
    mov al, [si]
    mov bl, [di]
    cmp al, bl
    jne .not_equal
    or al, al           ; Reached end of both strings successfully?
    jz .equal
    inc si
    inc di
    jmp .loop
.not_equal:
    pop di
    pop si
    clc                 ; Clear carry (not equal)
    ret
.equal:
    pop di
    pop si
    stc                 ; Set carry (equal)
    ret

save_hist:
    pusha
    mov si, cmd_buf
    mov di, hist_buf
.loop:
    lodsb
    stosb
    or al, al
    jnz .loop
    popa
    ret

; =====================================================================
;   SYSTEM DATA REALM (STRINGS & BUFFERS)
; =====================================================================

; Core System Variables
cur_col         db 0x1F   ; Default: 0x1F (White on Blue)
color_idx       db 0      ; Loop counter for color matrix generation
boot_drive      db 0      ; Store drive ID

; System Strings
msg_start       db "OSMIUM OS V0.0.1 - ", 0
msg_end         db "KB RAM AVAILABLE", 13, 10, 0
ready_msg       db "READY.", 13, 10, 0
prompt_msg      db "OS:>", 0
newline_msg     db 13, 10, 0
unknown_msg     db "ERR: UNKNOWN COMMAND. TYPE 'help'", 13, 10, 0
palette_header  db "VGA 16-COLOR MATRIX SYSTEM REGISTRY:", 13, 10, 0
empty_dir_msg   db " 0 FILE(S) FOUND. DIRECTORY IS EMPTY", 13, 10, 0

help_text:
    db "AVAILABLE COMMANDS:", 13, 10
    db "  help     - DISPLAYS SYSTEM COMMAND REGISTRY", 13, 10
    db "  clear    - FLUSHES THE TERMINAL INTERFACE", 13, 10
    db "  greeting - RE-RENDERS SYSTEM INFORMATION BANNER", 13, 10
    db "  palette  - RENDERS INTERFACE COLOR TEST MATRIX", 13, 10
    db "  theme    - CYCLE AVAILABLE THEMES (0-4)", 13, 10
    db "  echo     - REPEATS USER INPUT", 13, 10
    db "  edit     - OPEN TEXT EDITING APPLICATION", 13, 10
    db "  dir      - LISTS ALL FILES IN THE DIRECTORY", 13, 10
    db "  mem      - RENDERS AMOUNT OF MEMORY AVAILABLE", 13, 10
    db "  hexdump  - DUMPS 256 BYTES OF KERNEL MEMORY", 13, 10
    db "  reboot   - REBOOTS THE SYSTEM", 13, 10, 0

; Command String Definitions for Token Matching
cmd_help        db "help", 0
cmd_clear       db "clear", 0
cmd_greeting    db "greeting", 0
cmd_pal         db "palette", 0
cmd_echo        db "echo", 0
cmd_edit        db "edit", 0
cmd_mem         db "mem", 0
cmd_dir         db "dir", 0
cmd_reboot      db "reboot", 0
cmd_dump        db "hexdump", 0

; Theme table
theme_table db 0x1F ; Index 0, Default theme
            db 0x02 ; Index 1, Green on Black
            db 0x06 ; Index 2, Amber on Black
            db 0x70 ; Index 3, White on Black
            db 0x04 ; Index 4, Red on Black
theme_table_end:

; Misc String Definitions
hex_digits      db "0123456789ABCDEF"
ed_stat         db " F1: Save & Exit | OsmiumOS Text Editor ", 0

; Buffer Storage RAM Allocation
cmd_idx      dw 0
cmd_len      dw 0
cmd_buf      times 64 db 0
hist_buf     times 64 db 0
txt_buf      times 512 db 0
txt_buf_len  dw 0
filename_buf times 12 db 0
dir_buf      times 512 db 0
ed_row       db 0
ed_col       db 0

jmp prompt ; Stops boot from affecting apps

; =====================================================================
;   EXTERNAL APPLICATION MODULES
; =====================================================================
%include "src/app/edit.asm"
%include "src/app/disk.asm"