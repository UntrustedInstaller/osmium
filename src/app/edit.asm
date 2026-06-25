; =====================================================================
;    TEXT EDITOR APPLICATION 
; =====================================================================

exe_edit:
    ; Reset data coordinate tracking positions every time app initiates
    mov byte [ed_row], 0
    mov byte [ed_col], 0
    mov word [txt_buf_len], 0

    ; Clear put the 512 byte workspace buffer
    mov di, txt_buf
    mov cx, 512
    xor al, al
    cld
    rep stosb

    call cls     ; Clear screen to render the app

    ; Begin menu bar
    mov ah, 0x02
    xor bh, bh
    mov dh, 24                   ; Row 24
    mov dl, 0                    ; Column 0
    int 0x10

    ; Grey background strip for menu bar
    mov ah, 0x09                 ; Write character/attribute
    mov al, ' '                  ; Obligatory space
    xor bh, bh
    mov bl, 0x71                 ; Blue on grey
    mov cx, 80                   ; Apply down all 80 columns
    int 0x10                     ; Trigger drawing vector!

    ; Shortcuts over gray strip
    mov si, ed_stat

.draw_txt:
    lodsb
    or al, al
    jz .txt_done

    mov ah, 0x0E
    xor bh, bh
    mov bl, 0x71
    int 0x10
    jmp .draw_txt

.txt_done:
    ; Reset cursor to Row 0, Column 0
    mov ah, 0x02
    xor bh, bh
    xor dx, dx
    int 0x10

ed_loop:
    ;Main loop
    mov ah, 0x00
    int 0x16

    cmp ah, 0x3B    ; Is it F1?
    je .exit

    cmp al, 8       ; Or maybe it's backspace?
    je .handle_bkspc

    cmp al, 13      ; Ohhhh maybe it's enter?
    je .handle_enter

    cmp word [txt_buf_len], 511
    jge ed_loop ; Sets hard 511-byte limit, if it's larger, drop the character

    ; Reset if at column 79
    cmp byte [ed_col], 79
    jge ed_loop

    ; Render time!
    mov ah, 0x0E
    xor bh, bh      ; Page 0
    mov bl, [cur_col] ; Match users active theme
    int 0x10

    ; Commit character to text buffer
    mov bx, [txt_buf_len]
    mov [txt_buf + bx], al
    inc word [txt_buf_len]

    ; Move tracking cursor right
    inc byte [ed_col]
    jmp ed_loop

.handle_bkspc:
    ; Guard loop condition: Can't backspace if line left margin is hit
    cmp byte [ed_col], 0
    je ed_loop

    dec byte [ed_col]          ; Backtrack logical tracking pointer
    dec word [txt_buf_len]     ; Pull storage size counter backward
    
    mov bx, [txt_buf_len]      ; Zero-out index slot inside buffer matrix
    mov byte [txt_buf + bx], 0

    ; Physical destructive rendering sequence via BIOS
    mov ah, 0x0E
    mov al, 8
    int 0x10                       ; Backward shift
    mov al, ' '
    int 0x10                       ; Overwrite space blank
    mov al, 8
    int 0x10                       ; Reset final cursor lock
    jmp ed_loop

.handle_enter:
    ; Keep track of newline
    mov bx, [txt_buf_len]
    cmp bx, 510                     ; Leave room for both CR and LF bytes
    jge ed_loop                 

    mov byte [txt_buf + bx], 13     ; Append carriage return
    inc word [txt_buf_len]

    mov bx, [txt_buf_len]           ; Get updated tracker index
    mov byte [txt_buf + bx], 10     ; Append line feed
    inc word [txt_buf_len]

    ; Vertical boundaries
    cmp byte [ed_row], 23       
    jge ed_loop                 

    ; Coord tracker updates
    inc byte [ed_row]           
    mov byte [ed_col], 0        

    ; Update the user's cursor now
    mov ah, 0x02
    xor bh, bh
    mov dh, [ed_row]
    mov dl, [ed_col]
    int 0x10

    jmp ed_loop

.exit:
    ; Reposition cursor to (24,0) which overwrites status bar
    mov ah, 0x02
    xor bh, bh
    mov dh, 24
    mov dl, 0
    int 0x10

    ; Print "SAVE AS: "
    mov si, save_msg
.print_prompt:
    lodsb
    or al, al
    jz short .input_init
    mov ah, 0x0E
    xor bh, bh
    mov bl, 0x71
    int 0x10
    jmp .print_prompt

.input_init:
    ; Init local counter for filename (Max 11 characters + null = 12 bytes)
    xor cx, cx      ; This is what will keep track of the length

.input_loop:
    mov ah, 0x00
    int 0x16        ; Get keypress

    cmp al, 13      ; Is it enter?
    je short .do_save

    cmp al, 8       ; Perchance backspace?
    je short .prompt_bkspc
    
    cmp cx, 11      ; Limit that ho to 11 characters
    jge .input_loop

    ; Save valid character to command buffer
    ; Can reuse temporary offset or dedicated space
    mov bx, cx
    mov [filename_buf + bx], al
    inc cx

    ; Echo character to the status bar
    mov ah, 0x0E
    xor bh, bh
    mov bl, 0x71
    int 0x10
    jmp .input_loop

.prompt_bkspc:
    cmp cx, 0
    je .input_loop
    dec cx
    mov bx, cx
    mov byte [filename_buf + bx], 0

    ; Display the backspace
    mov ah, 0x0E
    mov al, 8
    int 0x10
    mov al, ' ' 
    int 0x10
    mov al, 8
    int 0x10
    jmp .input_loop

.do_save:
    ; Null terminate file buffer
    mov bx, cx
    mov byte [filename_buf + bx], 0

    ; Typed nothing? Don't save.
    cmp cx, 0
    je short .quit

    ; Set up registers and write to disk
    mov si, filename_buf    ;   si points to the filename
    mov di, txt_buf         ;   di points to the text (workspace)
    call save_to_dsk

.quit:
    call cls
    jmp prompt

; Local strings
save_msg db " SAVE AS: ", 0