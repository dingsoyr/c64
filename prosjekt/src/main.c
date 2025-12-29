/* src/main.c – Stream loader for Koala (IRQ-basert musikk, inline-ASM i funksjon) */
#include <c64.h>
#include <cbm.h>
#include <conio.h>
#include <string.h>
#include <stdint.h>

#include "sid_audio.h"
#include "gfx_helpers.h"

#define DEVNO 8

#define LOAD_TEXT_ROW 22
#define LOAD_TEXT_TOTAL_UNITS 666

/* --- VIC-II og IRQ-vektor --- */
#define VIC_RASTER       (*(volatile unsigned char*)0xD012)
#define VIC_CTRL1        (*(volatile unsigned char*)0xD011)
#define VIC_IRQ_ENABLE   (*(volatile unsigned char*)0xD01A)
#define VIC_IRQ_FLAG     (*(volatile unsigned char*)0xD019)
#define IRQV_LO          (*(volatile unsigned char*)0x0314)
#define IRQV_HI          (*(volatile unsigned char*)0x0315)

static unsigned char load_text_active = 0;
static unsigned short load_text_total = 0;
static unsigned short load_text_displayed = 0;

static unsigned char utoa10(unsigned short value, char* dest)
{
    char tmp[6];
    unsigned char len = 0;
    unsigned char i = 0;

    if (value == 0) {
        dest[0] = '0';
        return 1;
    }

    while (value > 0) {
        tmp[len++] = (char)('0' + (value % 10));
        value = (unsigned short)(value / 10);
    }

    while (len > 0) {
        dest[i++] = tmp[--len];
    }

    return i;
}

static void loading_text_clear_row(void)
{
    uint8_t* screen = (uint8_t*)0x0400 + (unsigned short)LOAD_TEXT_ROW * 40u;
    unsigned char i;
    for (i = 0; i < 40; ++i) {
        screen[i] = 0x20;
    }
}

static void loading_text_render(unsigned short current)
{
    char msg[24];
    unsigned char idx = 0;
    unsigned char col;
    const char prefix[] = "LOADING ";
    unsigned char i;

    for (i = 0; prefix[i] != 0; ++i) {
        msg[idx++] = prefix[i];
    }
    idx += utoa10(current, msg + idx);
    msg[idx++] = ' ';
    msg[idx++] = 'O';
    msg[idx++] = 'F';
    msg[idx++] = ' ';
    idx += utoa10(load_text_total, msg + idx);
    msg[idx] = 0;

    if (idx > 40) {
        idx = 40;
        msg[idx] = 0;
    }

    loading_text_clear_row();
    col = (unsigned char)((40 - idx) / 2);
    cputsxy(col, LOAD_TEXT_ROW, msg);
}

static void loading_text_start(unsigned short total_units)
{
    if (total_units == 0) {
        total_units = 1;
    }
    load_text_total = total_units;
    load_text_displayed = 0;
    load_text_active = 1;
    loading_text_render(0);
}

static void loading_text_update(unsigned short current_units)
{
    if (!load_text_active) {
        return;
    }

    if (current_units > load_text_total) {
        current_units = load_text_total;
    }

    if (current_units == load_text_displayed) {
        return;
    }

    load_text_displayed = current_units;
    loading_text_render(current_units);
}

static void loading_text_finish(void)
{
    if (load_text_active) {
        loading_text_update(load_text_total);
    }
    load_text_active = 0;
    load_text_total = 0;
    load_text_displayed = 0;
}

/* ---------- Lesehjelp ---------- */
static unsigned char read_bytes_pulsed(uint8_t* dst, unsigned len) {
    unsigned i;
    for (i = 0; i < len; ++i) {
        int c = cbm_k_chrin();
        if (c < 0) return 0;
        dst[i] = (uint8_t)c;

        if (load_text_active && len != 0) {
            unsigned long acc = (unsigned long)(i + 1) * load_text_total + (unsigned long)(len - 1);
            unsigned short units = (unsigned short)(acc / len);
            loading_text_update(units);
        }

        if ((i & 0x0F) == 0) {  /* pulser ca. kvar 16. byte for tydlegare indikator */
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

    loading_text_start(LOAD_TEXT_TOTAL_UNITS);
    if (!read_bytes_pulsed(ADR_BITMAP, 8000)) { cbm_k_clrch(); cbm_close(2); loading_text_finish(); return 0; }
    loading_text_finish();
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
    clrscr();

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

    /* Last og spel av sample */
    if (!sid_load_sample("vreid.bin", DEVNO)) {
        cprintf(" SAMPLE LOAD FAIL.\r\n");
        while (1) { }
    }
    sid_play_sample();

    // wait_video_seconds(5);

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
