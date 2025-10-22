/* src/main.c – Stream loader for Koala (no KERNAL LOAD overwrite) */
#include <c64.h>
#include <cbm.h>
#include <conio.h>
#include <string.h>
#include <stdint.h>

#include "sid_audio.h"
#include "gfx_helpers.h"

#define DEVNO 8

/* Read 'len' bytes into dst, with subtle border/SID progress feedback */
static unsigned char read_bytes_pulsed(uint8_t* dst, unsigned len) {
    unsigned i;
    for (i = 0; i < len; ++i) {
        int c = cbm_k_chrin();
        if (c < 0) return 0;
        dst[i] = (uint8_t)c;

        /* Pulse roughly every 64 bytes */
        if ((i & 0x3F) == 0) {       /* ~every 64th byte */
            pulse_border_tick();
            sid_progress_tick();      /* create a small sound burst */
        }

        if (cbm_k_readst() & 0x40) { /* EOI too early? */
            return (i + 1 == len) ? 1 : 0;
        }
    }
    return 1;
}

/* Stream-load Koala and start rendering as soon as the bitmap (8000) is ready.
   frames_per_row controls how slowly it “rolls” downward (1–6 works well). */
static unsigned char stream_load_koala_progressive(const char* name, unsigned char frames_per_row)
{
    unsigned char rc;
    uint8_t lo, hi;
    uint8_t bg;

    sid_progress_init();

    /* Open and probe optional $6000 PRG header */
    rc = cbm_open(2, DEVNO, 2, (char*)name);
    if (rc != 0 || cbm_k_chkin(2) != 0) {
        if (rc == 0) { cbm_k_clrch(); cbm_close(2); }
        return 0;
    }

    /* Read the first two bytes (possible PRG load address) */
    lo = (uint8_t)cbm_k_chrin(); if (cbm_k_readst() & 0x40) { cbm_k_clrch(); cbm_close(2); return 0; }
    hi = (uint8_t)cbm_k_chrin(); if (cbm_k_readst() & 0x40) { cbm_k_clrch(); cbm_close(2); return 0; }

    if (!(lo == 0x00 && hi == 0x60)) {
        /* RAW: reopen from the start */
        cbm_k_clrch();
        cbm_close(2);
        rc = cbm_open(2, DEVNO, 2, (char*)name);
        if (rc != 0 || cbm_k_chkin(2) != 0) {
            if (rc == 0) { cbm_k_clrch(); cbm_close(2); }
            return 0;
        }
    }

    /* 1) Read BITMAP (8000) – must be ready before we show anything */
    if (!read_bytes_pulsed(ADR_BITMAP, 8000)) { sid_progress_shutdown(); cbm_k_clrch(); cbm_close(2); return 0; }

    /* Stop all sound before toggling display and drawing */
    sid_progress_mute_now();

    /* 2) Turn on bitmap mode with an empty screen – start showing now */
    init_bitmap_blank(0);  /* temporary bg=0; set correct bg at the end */

    /* 3) Read SCREEN (1000) – store only (reveal happens during color phase) */
    if (!read_screen_progress(frames_per_row)) { sid_progress_shutdown(); cbm_k_clrch(); cbm_close(2); return 0; }

    /* 4) Read COLOR (1000) – fill Color RAM (and copy screen row) as we go */
    if (!read_color_progress(frames_per_row))  { sid_progress_shutdown(); cbm_k_clrch(); cbm_close(2); return 0; }

    /* 5) Read background (1) – allow EOI on the final byte */
    if (!read_bytes_pulsed(ADR_BG, 1)) { sid_progress_shutdown(); cbm_k_clrch(); cbm_close(2); return 0; }
    bg = *ADR_BG;
    VIC.bgcolor0    = bg;
    VIC.bordercolor = bg;

    sid_progress_shutdown();
    cbm_k_clrch();
    cbm_close(2);
    return 1;
}

/* Print text slowly, char-by-char, with an optional pause between each */
static void slowprint(const char* s, unsigned char frames_per_char)
{
    unsigned i;
    for (i = 0; s[i] != 0; ++i) {
        cputc(s[i]);                 /* output one character */
        wait_frames(frames_per_char);/* wait a little */
    }
}

void main(void) {
    unsigned i;

    /* --- Startup: black screen, white text --- */
    bordercolor(COLOR_BLACK); bgcolor(COLOR_BLACK);
    textcolor(COLOR_WHITE); clrscr();

    /* --- Small “boot”-style intro --- */
    textcolor(COLOR_GRAY3);
    slowprint("\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n             VREID PRESENTS\r\n", 400);

    textcolor(COLOR_WHITE);
    slowprint("\r\n\r\n                C64 INTRO\r\n", 400);

    textcolor(COLOR_LIGHTBLUE);
    slowprint("\r\n\r\n       STREAMING LOGO FROM DISK...\r\n", 400);

    /* Simple pre-roll border pulse (40 frames) */
    for (i = 0; i < 40; ++i) {
        VIC.bordercolor = (i >> 1) & 0x07;
        wait_frames(1);
    }

    /* Progressive streaming + display of the logo */
    if (!stream_load_koala_progressive("vreid.koa", 4)) {   /* higher = slower roll */
        cprintf(" FAIL.\r\n");
        while (1) { }
    }

    /* Done – dwell here briefly */
    wait_video_seconds(5);

    /* Close any open I/O, just in case */
    cbm_k_clrch();
    cbm_close(2);

    /* Normal text mode + bank 0 */
    CIA2.pra = (CIA2.pra & 0xFC) | 0x03;
    *(uint8_t*)0x01 = 0x37;
    VIC.ctrl1 = 0x1B;
    VIC.ctrl2 = 0x08;

    /* Lower/Upper mode + charset at $1800 */
    __asm__("jsr $e541");                 /* CHR$(14) */
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
        static unsigned char i = 0;
        if (kbhit()) {       /* conio: any key pressed? */
            cgetc();         /* read and drain */
            break;
        }
        VIC.bordercolor = i & 7;  /* small “pulse” */
        wait_frames(3);
        i++;
    }

    /* Full KERNAL reset → like power-on (without power cycling) */
    __asm__(
        "sei         \n"   /* mask IRQ for a safe jump */
        "lda #$37    \n"   /* map in IO + KERNAL + BASIC */
        "sta $01     \n"
        "jmp ($fffc) \n"   /* jump via reset vector */
    );
}
