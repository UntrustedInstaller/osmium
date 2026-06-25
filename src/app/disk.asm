; =====================================================================
;  FILE SYSTEM INTERFACE
; =====================================================================

lod_dir_sect:
    pusha
    mov ah, 0x02            ; BIOS Read
    mov al, 1               ; Read 1 sector (directory index)
    mov ch, 1               ; Cylinder 1 (Moved up to prevent kernel overlap!)
    mov cl, 15              ; Vacant sector 15 reserved for directory map
    mov dh, 0               ; Head 0 (still no head? :[ )
    mov dl, [boot_drive]    ; Force correct drive into dl

    mov bx, dir_buf         ; Ram destination allocation
    int 0x13
    popa
    ret

save_to_dsk:
    ; Input: si is the filename
    ; Input: di is the data (txt_buf)
    pusha

    ; 1. Load current directory into RAM first so we have something to scan
    call lod_dir_sect       
    mov bx, dir_buf         
    mov cx, 32              ; Loop limit to 32 potential files

.scan_loop:
    cmp byte [bx], 0        ; Is the first byte 0?
    je .found_slot
    add bx, 16              ; Move pointer to next entry
    loop .scan_loop

    ; If we land here, disk directory is full!
    popa
    ret

.found_slot:
    push bx                 ; Save the valid empty entry slot address we found!
    
    ; 2. Copy filename into the empty directory slot
    mov di, bx              ; di targets empty directory
    mov cx, 12              ; Hard 12 byte limit for filename
.copy_name:
    lodsb
    stosb
    or al, al
    jz .pad_spaces          ; If we hit the null terminator early, pad out the entry
    loop .copy_name
    jmp .name_done

.pad_spaces:
    ; If the name is shorter than our 12 byte limit, pad the rest with spaces
    dec di                  ; Step back to the null byte
    mov al, ' '
.pad_loop:
    stosb
    loop .pad_loop

.name_done:
    pop bx                  ; Restore the valid entry slot address into BX

    ; 3. NOW calculate the disk sector based on the real slot pointer in BX
    mov ax, bx
    sub ax, dir_buf         ; Get byte offset of current entry from beginning of buffer
    shr ax, 4               ; Divide by 16 to get the raw entry index (0-31)

    ; Save the raw slot index to byte 12 of the directory entry structure
    mov byte [bx + 12], al
    mov byte [bx + 13], 1   ; Size is 1 sector

    ; 4. Commit the text editor document buffer to its data sector
    ; MANUAL TRACK STEPPING LOGIC:
    ; We leave Head 0 entirely for the system/directory. Files start on Head 1!
    mov cx, ax              ; Copy our raw slot index (0-31) into CX
    inc cl                  ; Make it 1-indexed for floppy sectors (1-18)

    cmp cl, 18              ; Does it fit on the current track?
    jbe .fits_on_head1

    ; If slot index + 1 > 18, manually step to the next track (Cylinder 1, Head 0)
    sub cl, 18              ; Wrap sector number back down to 1-14
    mov ch, 1               ; Move up to Cylinder 1
    mov dh, 0               ; Head 0 (back to no head? :[ )
    jmp .write_now

.fits_on_head1:
    mov ch, 0               ; Stay on Cylinder 0
    mov dh, 1               ; Side 2 / Head 1 (WE FINALLY GOT HEAD?! :O)

.write_now:
    mov ah, 0x03            ; Tells the BIOS to write the sector
    mov al, 1               ; Write 1 sector
    mov dl, [boot_drive]    ; Force destination drive
    mov bx, txt_buf         ; Source of text workspace
    int 0x13
    jc write_error          ; If BIOS fails to write data, catch it

    ; 5. Finally, save the updated master directory map back to Cylinder 1, Sector 15
    mov ah, 0x03            ; Again, tells the BIOS to write to sector
    mov al, 1               ; Write 1 sector
    mov ch, 1               ; Cylinder 1
    mov cl, 15              ; Sector 15 (Entry map)
    mov dh, 0               ; Head 0 (We gave the directory a safe home out of the way! :D)
    mov dl, [boot_drive]    ; Again, force destination drive
    mov bx, dir_buf         ; Source buffer targeting the live directory data
    int 0x13
    jc write_error

    popa                    ; Success! Restore registers
    ret                     ; Return safely back to the editor

write_error:
    ; If things break, this prevents a silent failure hang
    ; Flash a bright red exclamation mark in the upper corner to alert
    mov ah, 0x0E
    mov al, '!'
    mov bl, 0x0C            ; THIS is the color attribute
    xor bh, bh
    int 0x10

    popa
    ret