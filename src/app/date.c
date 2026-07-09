__asm__(".code16gcc\n");
#include "apps.h"

static uint8_t bcd_to_dec(uint8_t bcd) {
    return ((bcd >> 4) & 0x0F) * 10 + (bcd & 0x0F);
}

static void print_dec2(uint8_t val) {
    print_char('0' + (val / 10));
    print_char('0' + (val % 10));
}

void cmd_date(const char* args) {
    uint16_t cx, dx;
    uint8_t carry;

    // int 1Ah AH=02h: get CMOS time
    // Returns: CH=hour, CL=minute, DH=second
    __asm__ __volatile__ (
        "movb $0x02, %%ah\n\t"
        "int $0x1A\n\t"
        "setc %0\n\t"
        : "=q"(carry), "=c"(cx), "=d"(dx)
        :
        : "eax"
    );

    if (carry) {
        print_str("ERR: CMOS clock not available\r\n");
        return;
    }

    uint8_t hours   = bcd_to_dec((cx >> 8) & 0xFF);
    uint8_t minutes = bcd_to_dec(cx & 0xFF);
    uint8_t seconds = bcd_to_dec((dx >> 8) & 0xFF);

    // int 1Ah AH=04h: get CMOS date
    // Returns: CH=century, CL=year, DH=month, DL=day
    __asm__ __volatile__ (
        "movb $0x04, %%ah\n\t"
        "int $0x1A\n\t"
        "setc %0\n\t"
        : "=q"(carry), "=c"(cx), "=d"(dx)
        :
        : "eax"
    );

    if (carry) {
        print_str("ERR: CMOS clock not available\r\n");
        return;
    }

    uint8_t century = bcd_to_dec((cx >> 8) & 0xFF);
    uint8_t year    = bcd_to_dec(cx & 0xFF);
    uint8_t month   = bcd_to_dec((dx >> 8) & 0xFF);
    uint8_t day     = bcd_to_dec(dx & 0xFF);

    // Print ISO format:  YYYY-MM-DD HH:MM:SS
    print_int(century * 100 + year);
    print_char('-');
    print_dec2(month);
    print_char('-');
    print_dec2(day);
    print_char(' ');
    print_dec2(hours);
    print_char(':');
    print_dec2(minutes);
    print_char(':');
    print_dec2(seconds);
    print_str("\r\n");
}
