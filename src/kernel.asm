bits 16
org 0x0000          ; Loaded by Stage 1 at segment 0x1000, offset 0x0000

kernel_init:
    ; Sync data segment registers to 0x1000
    mov ax, 0x1000
    mov ds, ax
    mov es, ax

    ; Initial screen clear and paint
    call clear_screen_action

    ; Beep on boot
    mov ah, 0x0E
    mov al, 7
    int 0x10

    ; Print the OSMIUM banner
    mov si, welcome_msg_start
    call print_string
    call print_ram
    mov si, welcome_msg_end
    call print_string

    ; Clean VGA Palette Matrix Only (No text headers, no double spacing)
    call render_matrix_only
    mov si, newline_msg
    call print_string

    ; Print the initial boot "READY." prompt
    mov si, ready_msg
    call print_string

main_prompt:
    mov si, prompt_msg
    call print_string

    ; Reset command buffer index and length tracker before starting input loop
    mov word [cmd_index], 0
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
    je handle_left_arrow

    ; Intercept right arrow scan code
    cmp ah, 0x4D
    je handle_right_arrow

    ; Handle Enter (Carriage Return)
    cmp al, 13
    je handle_enter

    ; Handle Backspace
    cmp al, 8
    je handle_backspace

; Check for buffer overflow safety (63 chars max + null terminator)
    cmp word [cmd_index], 63
    jge cli_loop        ; Ignore extra typing if buffer full

    ; Echo character to screen
    mov ah, 0x0E
    mov bh, 0
    mov bl, [current_color]
    int 0x10

    ; Save character to buffer
    mov bx, [cmd_index]
    mov [cmd_buffer + bx], al

    ; --- SYNCHRONIZE TRACKERS ---
    mov ax, [cmd_index]
    cmp ax, [cmd_len]
    jne .skip_len_grow     ; If cursor is inside the word, don't grow total length
    inc word [cmd_len]     ; Expand string size safely

.skip_len_grow:
    inc word [cmd_index]   ; Advance editing cursor forward
    jmp cli_loop

; --- CLI INTERCEPT HANDLERS ---

handle_up_arrow:
.clear_loop:
    cmp word [cmd_index], 0
    je .restore_data

    dec word [cmd_index]

    mov ah, 0x0E
    mov al, 8
    int 0x10
    mov al, ' '
    int 0x10
    mov al, 8
    int 0x10
    jmp .clear_loop
.restore_data:
    mov si, history_buffer
    mov di, cmd_buffer
    mov cx, 0
.copy_loop:
    lodsb
    stosb
    or al, al
    jz .done_copy
    inc cx
    jmp .copy_loop
.done_copy:
    mov [cmd_index], cx
    mov [cmd_len], cx
    mov si, cmd_buffer
    call print_string
    jmp cli_loop

handle_left_arrow:
    cmp word [cmd_index], 0
    je cli_loop            ; If at index 0, don't cross prompt boundary

    dec word [cmd_index]   ; Step back in RAM

    ; Physically move cursor left
    mov ah, 0x03
    mov bh, 0
    int 0x10               ; Get current row+column
    dec dl                 ; Decrement column
    mov ah, 0x02
    int 0x10               ; Set new cursor position
    jmp cli_loop

handle_right_arrow:
    ; Read current position vs length
    mov ax, [cmd_index]
    cmp ax, [cmd_len]
    je cli_loop            ; If index is equal to total length, don't allow step-out

    inc word [cmd_index]

    ; Physically move cursor right
    mov ah, 0x03
    mov bh, 0
    int 0x10               ; Get current row+column
    inc dl                 ; Increment column
    mov ah, 0x02
    int 0x10               ; Set new cursor position
    jmp cli_loop

handle_backspace:
    ; Guard against backspacing past the prompt start
    cmp word [cmd_index], 0
    je cli_loop

    ; Decrement buffer index
    dec word [cmd_index]
    mov bx, [cmd_index]
    mov byte [cmd_buffer + bx], 0  ; Clear character in buffer

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
    call print_string

    ; Null-terminate the command buffer string
    mov bx, [cmd_len]
    mov byte [cmd_buffer + bx], 0

    ; Skip processing if the user just pressed enter on an empty line
    cmp word [cmd_len], 0
    je main_prompt

    call save_history

    ; --- COMMAND PARSER ---
    
    ; Check "echo " (Inline peek)
    cmp byte [cmd_buffer], 'e'
    jne .not_echo
    cmp byte [cmd_buffer + 1], 'c'
    jne .not_echo
    cmp byte [cmd_buffer + 2], 'h'
    jne .not_echo
    cmp byte [cmd_buffer + 3], 'o'
    jne .not_echo
    cmp byte [cmd_buffer + 4], ' '
    je execute_echo

.not_echo:
    ; Check "help"
    mov si, cmd_buffer
    mov di, cmd_help
    call strcmp
    jc execute_help

    ; Check "clear"
    mov si, cmd_buffer
    mov di, cmd_clear
    call strcmp
    jc execute_clear

    ; Check "greeting"
    mov si, cmd_buffer
    mov di, cmd_greeting
    call strcmp
    jc execute_greeting

    ; Check "palette"
    mov si, cmd_buffer
    mov di, cmd_palette
    call strcmp
    jc execute_palette

    ; Check "reboot"
    mov si, cmd_buffer
    mov di, cmd_reboot
    call strcmp
    jc execute_reboot

    ; Check "mem"
    mov si, cmd_buffer
    mov di, cmd_mem
    call strcmp
    jc execute_mem

    ; Unknown command fallback
    mov si, unknown_msg
    call print_string
    jmp main_prompt


; --- COMMAND EXECUTION TARGETS ---

execute_help:
    mov si, help_text
    call print_string
    jmp main_prompt

execute_clear:
    call clear_screen_action
    jmp main_prompt

execute_greeting:
    mov si, welcome_msg_start
    call print_string
    call print_ram
    mov si, welcome_msg_end
    call print_string
    jmp main_prompt

execute_palette:
    call render_palette
    jmp main_prompt

execute_echo:
    mov si, cmd_buffer ; Move the buffer to si
    add si, 5          ; Add 5 to pointer (Ignore "echo ")
    call print_string

    mov si, newline_msg
    call print_string
    jmp main_prompt

execute_reboot:
    jmp 0xFFFF:0x0000

execute_mem:
    call print_ram
    mov si, newline_msg
    call print_string
    jmp main_prompt


; --- UTILITY FUNCTIONS ---

print_string:
.loop:
    lodsb
    or al, al
    jz .done

    ; --- BIOS SCROLL BACKGROUND COLOR FIX ---
    cmp al, 10              ; Is it a Line Feed (LF)?
    jne .not_lf
    
    push ax                 ; Save the character
    mov ah, 0x03            ; BIOS: Read current cursor position
    mov bh, 0
    int 0x10                ; Returns current row in DH, column in DL
    pop ax                  ; Restore the character
    
    cmp dh, 24              ; Are we on the very bottom row of the terminal?
    jne .not_lf
    
    ; Intercept! Perform the scroll manually using our system color theme.
    push dx                 ; Save our current column coordinate (DL)
    mov ah, 0x06            ; BIOS: Scroll Window Up
    mov al, 1               ; Scroll up by exactly 1 line
    mov bh, [current_color] ; Force the blank row to inherit our theme color
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
    mov ah, 0x0E
    mov bh, 0
    mov bl, [current_color]
    int 0x10
    jmp .loop
.done:
    ret

clear_screen_action:
    mov ah, 0x06        ; Scroll up function (clears window)
    mov al, 0           ; Entire screen
    mov bh, [current_color]
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

    mov bx, 0    ; Clear bx to 0 to store digit count
    mov cx, 10   ; Move our divisor to cx

.math_loop:
    mov dx, 0    ; Clear dx register
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
    pusha                      ; Save registers for caller safety
    
    mov si, palette_header
    call print_string

    call render_matrix_only

    ; Push cursor down past the banner block (Exclusive to standard palette command)
    mov si, newline_msg
    call print_string
    mov si, newline_msg
    call print_string

    popa                       ; Restore registers
    ret

render_matrix_only:
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

save_history:
    pusha
    mov si, cmd_buffer
    mov di, history_buffer
.loop:
    lodsb
    stosb
    or al, al
    jnz .loop
    popa
    ret


; --- SYSTEM DATA REALM ---

; Core UI Variables
current_color   db 0x1F   ; Default: 0x1F (White on Blue)
color_idx       db 0      ; Loop counter for color matrix generation

; System Strings
welcome_msg_start     db "OSMIUM OS V0.0 - ", 0
welcome_msg_end       db "KB RAM AVAILABLE", 13, 10, 0
ready_msg       db "READY.", 13, 10, 0
prompt_msg      db "OS:>", 0
newline_msg     db 13, 10, 0
unknown_msg     db "ERR: UNKNOWN COMMAND. TYPE 'help'", 13, 10, 0
palette_header  db "VGA 16-COLOR MATRIX SYSTEM REGISTRY:", 13, 10, 0

help_text:
    db "AVAILABLE COMMANDS:", 13, 10
    db "  help     - DISPLAYS SYSTEM COMMAND REGISTRY", 13, 10
    db "  clear    - FLUSHES THE TERMINAL INTERFACE", 13, 10
    db "  greeting - RE-RENDERS SYSTEM INFORMATION BANNER", 13, 10
    db "  palette  - RENDERS INTERFACE COLOR TEST MATRIX", 13, 10
    db "  echo     - REPEATS USER INPUT", 13, 10
    db "  mem      - RENDERS AMOUNT OF MEMORY AVAILABLE", 13, 10
    db "  reboot   - REBOOTS THE SYSTEM", 13, 10, 0

; Command String Definitions for Token Matching
cmd_help        db "help", 0
cmd_clear       db "clear", 0
cmd_greeting    db "greeting", 0
cmd_palette     db "palette", 0
cmd_echo        db "echo", 0
cmd_mem         db "mem", 0
cmd_reboot      db "reboot", 0

; Command Buffer Storage RAM Allocation
cmd_index       dw 0
cmd_len         dw 0
cmd_buffer      times 64 db 0
history_buffer  times 64 db 0