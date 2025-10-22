/* SID / AUDIO HELPERS (implementation) */
#include <stdint.h>
#include <c64.h>
#include "sid_audio.h"

/* --- SID register helpers --- */
#define SID_BASE   ((volatile uint8_t*)0xD400)
#define SID_V1     (SID_BASE + 0)    /* 0..6 */
#define SID_V2     (SID_BASE + 7)    /* 7..13 */
#define SID_V3     (SID_BASE + 14)   /* 14..20 */
#define SID_FLT    (SID_BASE + 21)   /* 21..23 */
#define SID_MODEVOL (SID_BASE + 24)  /* $D418 */

static uint8_t  sid_prev_modevol = 0;
static uint16_t prng = 0xACE1; /* simple PRNG seed for variation */

/* Very small, fast PRNG (xorshift/LCG-ish) */
static uint16_t rnd16(void) {
    prng ^= prng << 7;
    prng ^= prng >> 9;
    prng ^= prng << 8;
    return prng;
}

/* Init: set up voice 3 for short noise bursts */
void sid_progress_init(void) {
    sid_prev_modevol = SID_MODEVOL[0];
    SID_MODEVOL[0] = (sid_prev_modevol & 0xF0) | 0x07; /* volume ~7/15, no filter */

    /* ADSR for voice3: fast attack, medium decay, some sustain, short release */
    SID_V3[5] = 0x28;  /* AD: A=2, D=8 */
    SID_V3[6] = 0x64;  /* SR: S=6, R=4 */

    /* start with gate off, noise selected */
    SID_V3[4] = 0x01;  /* NOISE (bit0), gate=0 */
}

/* Emit a tiny burst: vary frequency, retrigger gate */
void sid_progress_tick(void) {
    uint16_t f = rnd16();

    /* frequency for voice3 */
    SID_V3[0] = (uint8_t)(f & 0xFF);        /* FREQ LO */
    SID_V3[1] = (uint8_t)((f >> 8) & 0xFF); /* FREQ HI */

    /* fast retrigger: gate off -> on with NOISE */
    SID_V3[4] = 0x01;        /* NOISE, gate off */
    SID_V3[4] = 0x11;        /* NOISE + GATE (bit4) */
}

/* Turn sound off and restore volume */
void sid_progress_shutdown(void) {
    SID_V1[4] = 0x00; SID_V2[4] = 0x00; SID_V3[4] = 0x00;
    /* Restore entire MODEVOL (incl. volume) as it was */
    SID_MODEVOL[0] = sid_prev_modevol;
}

/* Hard mute SID right now */
void sid_progress_mute_now(void) {
    /* Gate off on all voices */
    SID_V1[4] = 0x00;
    SID_V2[4] = 0x00;
    SID_V3[4] = 0x00;

    /* (Optional but effective): momentarily reset oscillator 3 with TEST */
    SID_V3[4] = 0x08;   /* TEST=1 */
    SID_V3[4] = 0x00;   /* TEST=0 */

    /* Master volume to 0 (preserve filter bits) */
    SID_MODEVOL[0] &= 0xF0;
}
