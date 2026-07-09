__asm__(".code16gcc\n");
#include "api.h"

#define MAX_SNAKE 200
#define TICK_US 150000

static uint8_t bx[MAX_SNAKE], by[MAX_SNAKE];
static int slen, sdir, snext;
static int growing, gameover;
static uint8_t fx, fy, otx, oty;
static uint16_t sscore;
static uint16_t rng_state;

static void poke_char(uint8_t col, uint8_t row, char c) {
    uint16_t off = (row * 80 + col) * 2;
    uint8_t attr = get_cur_col();
    uint16_t cw = ((uint16_t)attr << 8) | (uint8_t)c;
    __asm__ __volatile__(
        "pushw %%es\n\t"
        "movw $0xB800, %%bx\n\t"
        "movw %%bx, %%es\n\t"
        "movw %0, %%bx\n\t"
        "movw %1, %%es:(%%bx)\n\t"
        "popw %%es\n\t"
        : : "b"(off), "a"(cw) : "memory"
    );
}

static uint16_t rng(void) {
    rng_state ^= rng_state << 7;
    rng_state ^= rng_state >> 9;
    rng_state ^= rng_state << 8;
    return rng_state;
}

static int occ(int x, int y) {
    for (int i = 0; i < slen; i++)
        if (bx[i] == x && by[i] == y) return 1;
    return 0;
}

static void spawn_food(void) {
    do { fx = 1 + (rng() % 78); fy = 1 + (rng() % 23); } while (occ(fx, fy));
    gotoxy(fx, fy); print_char('$');
}

static int kbhit(void) {
    uint8_t v;
    __asm__ __volatile__ ("movb $0x01, %%ah\n\tint $0x16\n\tsetnz %0" : "=q"(v) : : "eax");
    return v;
}

static void tick(void) {
    if ((sdir == 0 && snext == 2) || (sdir == 2 && snext == 0) ||
        (sdir == 1 && snext == 3) || (sdir == 3 && snext == 1))
        snext = sdir;
    sdir = snext;

    uint8_t nhx = bx[0], nhy = by[0];
    switch (sdir) {
        case 0: if (nhy > 1) nhy--; else { gameover = 1; return; } break;
        case 1: if (nhx < 78) nhx++; else { gameover = 1; return; } break;
        case 2: if (nhy < 23) nhy++; else { gameover = 1; return; } break;
        case 3: if (nhx > 1) nhx--; else { gameover = 1; return; } break;
    }

    otx = bx[slen - 1]; oty = by[slen - 1];
    for (int i = slen - 1; i > 0; i--) { bx[i] = bx[i - 1]; by[i] = by[i - 1]; }

    for (int i = 1; i < slen; i++)
        if (bx[i] == nhx && by[i] == nhy) { gameover = 1; return; }

    bx[0] = nhx; by[0] = nhy;

    if (nhx == fx && nhy == fy) {
        bx[slen] = otx; by[slen] = oty; slen++; sscore += 10; growing = 1;
        spawn_food();
    } else growing = 0;

    if (!growing) { gotoxy(otx, oty); print_char(' '); }
    gotoxy(bx[1], by[1]); print_char('o');
    gotoxy(bx[0], by[0]); print_char('@');
    gotoxy(0, 0); print_str("Score: "); print_int(sscore);
}

void module_main(void) {
    clear_screen();

    slen = 3; sscore = 0; sdir = 1; snext = 1; growing = 0; gameover = 0;
    rng_state = 1;
    bx[0] = 12; by[0] = 12;
    bx[1] = 11; by[1] = 12;
    bx[2] = 10; by[2] = 12;

    for (int r = 0; r < 23; r++) { gotoxy(0, r); print_char('|'); gotoxy(79, r); print_char('|'); }
    for (int c = 1; c < 79; c++) { gotoxy(c, 0); print_char('='); }
    gotoxy(0, 23); print_char('|');
    gotoxy(79, 23); print_char('|');

    gotoxy(0, 24); print_char('|');
    for (int c = 1; c < 79; c++) { gotoxy(c, 24); print_char('='); }
    poke_char(79, 24, '|');

    gotoxy(25, 0); print_str("SNAKE");
    gotoxy(0, 24); print_str("Arrows=Move Q=Quit");

    spawn_food();
    gotoxy(bx[0], by[0]); print_char('@');
    gotoxy(bx[1], by[1]); print_char('o');
    gotoxy(bx[2], by[2]); print_char('o');

    while (!gameover) {
        while (kbhit()) {
            uint16_t k; __asm__ __volatile__ ("movb $0x00, %%ah\n\tint $0x16" : "=a"(k));
            uint8_t a = k & 0xFF, s = (k >> 8) & 0xFF;
            if (a == 0) {
                if (s == 0x48) snext = 0;
                else if (s == 0x4D) snext = 1;
                else if (s == 0x50) snext = 2;
                else if (s == 0x4B) snext = 3;
            } else if (a == 'q' || a == 'Q') { clear_screen(); return; }
        }

        uint32_t us = TICK_US;
        __asm__ __volatile__ ("movb $0x86, %%ah\n\tint $0x15" : : "c"((uint16_t)(us>>16)), "d"((uint16_t)(us&0xFFFF)) : "eax");
        tick();
    }

    gotoxy(30, 12); print_str("GAME OVER");
    gotoxy(30, 13); print_str("Score: "); print_int(sscore);
    gotoxy(26, 14); print_str("Press any key...");
    uint16_t k; __asm__ __volatile__ ("movb $0x00, %%ah\n\tint $0x16" : "=a"(k));
    clear_screen();
}
