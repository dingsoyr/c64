/* VIC / GRAPHICS HELPERS (implementation) */
#include <stdint.h>
#include <string.h>
#include <c64.h>
#include <cbm.h>
#include "gfx_helpers.h"

/* Small border pulse: slowly change color over time */
void pulse_border_tick(void) {
    static unsigned char t = 0;
    VIC.bordercolor = (t >> 3) & 0x07; /* change color roughly every 8th tick */
    ++t;
}

/* Wait N frames (raster-synced) for smooth animation */
void wait_frames(unsigned char frames) {
    volatile unsigned char *RASTER = (unsigned char*)0xD012;
    unsigned char start;
    while (frames--) {
        start = *RASTER;
        while (*RASTER == start) { }
    }
}

/* Turn on bitmap mode, blank SCREEN/COLOR, set bg/border */
void init_bitmap_blank(unsigned char bg)
{
    unsigned i;

    /* Bank $4000–$7FFF (bitmap=$2000 in-bank = $6000 CPU) */
    CIA2.pra = (CIA2.pra & 0xFC) | 0x02;   /* $DD00 = ..10 (bank 2) */

    /* Disable display while setting up (bit 4 in $D011) */
    VIC.ctrl1 = (VIC.ctrl1 & ~0x10);       /* display OFF */
    VIC.ctrl2 = 0x18;                      /* multicolor on */
    VIC.addr  = 0x18;                      /* screen=$0400, bitmap=$2000 (in-bank) */

    VIC.bgcolor0    = bg;                  /* D021 */
    VIC.bordercolor = bg;                  /* D020 */

    /* Blank SCREEN ($4400 in bank 2) and COLOR RAM ($D800) */
    memset((void*)0x4400, 0x00, 1000);
    for (i = 0; i < 1000; ++i) {
        COLOR_RAM[i] = 0x00;
    }

    /* Enable bitmap + display */
    VIC.ctrl1 = 0x3B;  /* bitmap + display ON (same value as before) */
}

/* Read 1000 bytes of SCREEN from the open file, store row-by-row.
   (Pure store; on-screen reveal is handled in read_color_progress.) */
unsigned char read_screen_progress(unsigned char frames_per_row) {
    unsigned row, col;
    int ch;
    for (row = 0; row < 25; ++row) {
        for (col = 0; col < 40; ++col) {
            ch = cbm_k_chrin();
            if (ch < 0 || (cbm_k_readst() & 0x40)) return 0;  /* error/EOI too early */
            ADR_SCREEN[row*40 + col] = (uint8_t)ch;
            // ((uint8_t*)0x4400)[row*40 + col] = (uint8_t)ch; /* draw immediately (optional) */
        }
        wait_frames(frames_per_row);   /* gentle sweep downward */
    }
    return 1;
}

/* Read 1000 bytes of COLOR from the open file, and reveal each row on screen after it’s read */
unsigned char read_color_progress(unsigned char frames_per_row) {
    unsigned row, col;
    int ch;
    for (row = 0; row < 25; ++row) {
        for (col = 0; col < 40; ++col) {
            ch = cbm_k_chrin();
            if (ch < 0 || (cbm_k_readst() & 0x40)) return 0;
            ADR_COLOR[row*40 + col] = (uint8_t)ch;
        }
        /* Reveal this row now: copy SCREEN row and apply COLOR RAM for the row */
        memcpy((void*)(0x4400 + row*40), ADR_SCREEN + row*40, 40);
        for (col = 0; col < 40; ++col) {
            COLOR_RAM[row*40 + col] = ADR_COLOR[row*40 + col] & 0x0F;
        }
        wait_frames(frames_per_row);
    }
    return 1;
}

/* True video frames (PAL/NTSC): wait for start of a new frame (raster==0 with MSB=1) */
void wait_video_frames(unsigned int frames) {
    volatile unsigned char* RASTER  = (unsigned char*)0xD012; /* low 8 bits */
    volatile unsigned char* CTRL1   = (unsigned char*)0xD011; /* bit7 = MSB of raster */

    while (frames--) {
        /* 1) Leave “start-of-frame” in case we happen to be there now */
        while ( (*CTRL1 & 0x80) && (*RASTER == 0) ) { /* spin */ }

        /* 2) Wait for the NEXT start-of-frame (raster MSB=1 and low=0) */
        while ( !((*CTRL1 & 0x80) && (*RASTER == 0)) ) { /* spin */ }
    }
}

/* Convenience: about S seconds on PAL (50 Hz) */
void wait_video_seconds(unsigned int s) {
    unsigned int i;
    for (i = 0; i < s; ++i) wait_video_frames(50);
}
