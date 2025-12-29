/* src/main.c – Stream loader for Koala (IRQ-basert musikk, inline-ASM i funksjon) */
#include <c64.h>
#include <cbm.h>
#include <conio.h>
#include <string.h>
#include <stdint.h>

#include "sid_audio.h"
#include "gfx_helpers.h"

#define DEVNO 8

/* --- VIC-II og IRQ-vektor --- */
#define VIC_RASTER       (*(volatile unsigned char*)0xD012)
#define VIC_CTRL1        (*(volatile unsigned char*)0xD011)
#define VIC_IRQ_ENABLE   (*(volatile unsigned char*)0xD01A)
#define VIC_IRQ_FLAG     (*(volatile unsigned char*)0xD019)
#define IRQV_LO          (*(volatile unsigned char*)0x0314)
#define IRQV_HI          (*(volatile unsigned char*)0x0315)

/* Framoverdeklarasjonar */
static void install_irq(void);
void irq_handler(void);   /* definert lenger nede */

/* ---------- Lesehjelp ---------- */
static unsigned char read_bytes_pulsed(uint8_t* dst, unsigned len) {
    unsigned i;
    for (i = 0; i < len; ++i) {
        int c = cbm_k_chrin();
        if (c < 0) return 0;
        dst[i] = (uint8_t)c;

        if ((i & 0x3F) == 0) {  /* ~kvar 64. byte: litt border-pulse */
            pulse_border_tick();
        }

        if (cbm_k_readst() & 0x40) {
            return (i + 1 == len) ? 1 : 0;
        }
    }
    return 1;
}

/* ---------- Stream-loader ---------- */
static unsigned char stream_load_koala_progressive(const char* name, unsigned char frames_per_row)
{
    unsigned char rc;
    uint8_t lo, hi;
    uint8_t bg;

    rc = cbm_open(2, DEVNO, 2, (char*)name);
    if (rc != 0 || cbm_k_chkin(2) != 0) {
        if (rc == 0) { cbm_k_clrch(); cbm_close(2); }
        return 0;
    }

    lo = (uint8_t)cbm_k_chrin(); if (cbm_k_readst() & 0x40) { cbm_k_clrch(); cbm_close(2); return 0; }
    hi = (uint8_t)cbm_k_chrin(); if (cbm_k_readst() & 0x40) { cbm_k_clrch(); cbm_close(2); return 0; }

    if (!(lo == 0x00 && hi == 0x60)) {
        cbm_k_clrch();
        cbm_close(2);
        rc = cbm_open(2, DEVNO, 2, (char*)name);
        if (rc != 0 || cbm_k_chkin(2) != 0) {
            if (rc == 0) { cbm_k_clrch(); cbm_close(2); }
            return 0;
        }
    }

    if (!read_bytes_pulsed(ADR_BITMAP, 8000)) { cbm_k_clrch(); cbm_close(2); return 0; }
    init_bitmap_blank(0);
    if (!read_screen_progress(frames_per_row)) { cbm_k_clrch(); cbm_close(2); return 0; }
    if (!read_color_progress(frames_per_row))  { cbm_k_clrch(); cbm_close(2); return 0; }

    if (!read_bytes_pulsed(ADR_BG, 1)) { cbm_k_clrch(); cbm_close(2); return 0; }
    bg = *ADR_BG;
    VIC.bgcolor0    = bg;
    VIC.bordercolor = bg;

    cbm_k_clrch();
    cbm_close(2);
    return 1;
}

/* ---------- Teksthjelp ---------- */
static void slowprint(const char* s, unsigned char frames_per_char)
{
    unsigned i;
    for (i = 0; s[i] != 0; ++i) {
        cputc(s[i]);
        wait_frames(frames_per_char);
    }
}

/* ---------- MAIN ---------- */
void main(void) {
    unsigned i;

    /* Intro */
    bordercolor(COLOR_BLACK); bgcolor(COLOR_BLACK);
    textcolor(COLOR_WHITE); clrscr();

    textcolor(COLOR_GRAY3);
    slowprint("\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n             VREID PRESENTS\r\n", 400);

    textcolor(COLOR_WHITE);
    slowprint("\r\n\r\n                C64 INTRO\r\n", 400);

    textcolor(COLOR_LIGHTBLUE);
    slowprint("\r\n\r\n       STREAMING LOGO FROM DISK...\r\n", 400);

    for (i = 0; i < 40; ++i) {
        VIC.bordercolor = (i >> 1) & 0x07;
        wait_frames(1);
    }

    if (!stream_load_koala_progressive("vreid.koa", 4)) {
        cprintf(" FAIL.\r\n");
        while (1) { }
    }

    /* Start musikken først etter at logoen er klar */
    sid_init();
    install_irq();

    wait_video_seconds(5);

    cbm_k_clrch();
    cbm_close(2);

    /* Normal tekstmodus + bank 0 */
    CIA2.pra = (CIA2.pra & 0xFC) | 0x03;
    *(uint8_t*)0x01 = 0x37;
    VIC.ctrl1 = 0x1B;
    VIC.ctrl2 = 0x08;

    __asm__("jsr $e541");
    VIC.addr  = (VIC.addr & 0xF0) | 0x07; /* screen=$0400, charset=$1800 */

    bordercolor(COLOR_BLACK);
    bgcolor(COLOR_BLACK);
    textcolor(COLOR_WHITE);
    clrscr();

    textcolor(COLOR_GRAY3);
    slowprint("\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n          END OF TRANSMISSION\r\n", 400);

    textcolor(COLOR_WHITE);
    slowprint("\r\n\r\n        PRESS ANY KEY TO REBOOT\r\n", 400);

    textcolor(COLOR_LIGHTBLUE);
    slowprint("\r\n\r\n         THANK YOU FOR WATCHING\r\n", 400);

    while (1) {
        static unsigned char j = 0;
        if (kbhit()) {
            cgetc();
            break;
        }
        VIC.bordercolor = j & 7;
        wait_frames(3);
        j++;
    }

    __asm__(
        "sei         \n"
        "lda #$37    \n"
        "sta $01     \n"
        "jmp ($fffc) \n"
    );
}

/* ---------- IRQ-oppsett ---------- */
static void install_irq(void)
{
    __asm__("sei");  /* disable IRQ globalt */

    /* Rasterline 0, og sørg for <256-compare (bit7=0) */
    VIC_RASTER = 0x00;
    VIC_CTRL1 = (unsigned char)(VIC_CTRL1 & 0x7F);

    /* Pek KERNAL IRQ-vektoren til vår handler */
    IRQV_LO = (unsigned char)((unsigned)irq_handler & 0xFF);
    IRQV_HI = (unsigned char)(((unsigned)irq_handler >> 8) & 0xFF);

    /* Tøm evt. ventande IRQ-flagg og slå på raster-IRQ */
    VIC_IRQ_FLAG   = 0xFF;
    VIC_IRQ_ENABLE |= 0x01;   /* bit0 = raster */

    __asm__("cli");  /* enable IRQ igjen */
}

/* ---------- IRQ-handler (C-funksjon med inline ASM i kroppen) ---------- */
void irq_handler(void)
{
    asm("pha");
    asm("txa");
    asm("pha");
    asm("tya");
    asm("pha");

    asm("jsr _sid_tick");   /* kall C-rutina di */

    asm("lda #$ff");
    asm("sta $d019");       /* kvitt raster-IRQ */

    asm("pla");
    asm("tay");
    asm("pla");
    asm("tax");
    asm("pla");

    asm("jmp $ea31");       /* kjed vidare til KERNAL IRQ */
}
