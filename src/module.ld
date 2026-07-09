ENTRY(_start)
OUTPUT_FORMAT("binary")

SECTIONS
{
    . = 0x0000;

    .text : SUBALIGN(2) {
        *(.text)
        *(.text.*)
    }

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

    /DISCARD/ : {
        *(.note*)
        *(.eh_frame)
        *(.comment)
    }
}
