ENTRY(kernel_init)
OUTPUT_FORMAT("elf32-i386")

SECTIONS
{
    /* Execute flat at segment offset 0x0000 */
    . = 0x0000;

    .text : {
        *kernel_asm.o(.text)
        *(.text)
        *(.text.*)
    }

    /* Force data sections to follow code immediately with NO page alignment */
    .rodata : SUBALIGN(2) {
        *(.rodata)
        *(.rodata.*)
    }

    .data : SUBALIGN(2) {
        *(.data)
        *(.data.*)
    }

    _bss_start = .;
    .bss : SUBALIGN(2) {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }
    _bss_end = .;
    _kernel_end = .;

    /DISCARD/ : {
        *(.note*)
        *(.eh_frame)
        *(.comment)
    }
}