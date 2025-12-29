#include "sid_audio.h"

/* --------------------------------------------------------------------------
   SID register (voice 1) og kontrollbitar
   -------------------------------------------------------------------------- */
#define SID_BASE         0xD400
#define SID_V1_FREQ_LO (*(volatile unsigned char*)(SID_BASE + 0x00))
#define SID_V1_FREQ_HI (*(volatile unsigned char*)(SID_BASE + 0x01))
#define SID_V1_PW_LO   (*(volatile unsigned char*)(SID_BASE + 0x02))
#define SID_V1_PW_HI   (*(volatile unsigned char*)(SID_BASE + 0x03))
#define SID_V1_CTRL    (*(volatile unsigned char*)(SID_BASE + 0x04))
#define SID_V1_AD      (*(volatile unsigned char*)(SID_BASE + 0x05))
#define SID_V1_SR      (*(volatile unsigned char*)(SID_BASE + 0x06))
#define SID_MASTER_VOL (*(volatile unsigned char*)(SID_BASE + 0x18))

/* CTRL-bitar */
#define CTRL_GATE   0x01
#define CTRL_TRI    0x10
#define CTRL_SAW    0x20
#define CTRL_PULSE  0x40
#define CTRL_NOISE  0x80

/* Kor mange gonger vi skal forlenge kvar notelengd.
    1 = original fart, 2 = 2x lengre (halv fart), 3 = 3x lengre, osv. */
static unsigned char tempo_mult = 3;

/* Fast note- og gap-lengd (i frames før tempo_mult blir brukt) */
#define NOTE_LENGTH_FRAMES 16
#define NOTE_GAP_FRAMES     4

/* PAL master clock ~985248 Hz (vi brukar ferdigrekna toneord nedanfor) */

/* --------------------------------------------------------------------------
   Melodi
    - Verdiane i 'melody_words' er 16-bit SID-frekvensord for PAL.
    - 0 = pause (gate off).
   -------------------------------------------------------------------------- */

/* Enkle tonar (C4, E4, G4, C5) for PAL, typiske verdier:
   C4 ≈ 0x0456, E4 ≈ 0x04A9, G4 ≈ 0x050C, C5 ≈ 0x058A */
static const unsigned short melody_words[] = {
    0x0456, /* C4 */
    0x0471, /* D4 */
    0x04A9, /* E4 */
    0x04C7, /* F4 */
    0x050C, /* G4 */
    0x0550, /* A4 */
    0x05A7, /* H/B4 */
    0x06B0  /* C5+12 (ekte oktav over første C) */
};

#define MELODY_COUNT (sizeof(melody_words) / sizeof(melody_words[0]))

/* --------------------------------------------------------------------------
   Intern tilstand
   -------------------------------------------------------------------------- */
static unsigned char sid_enabled = 1;     /* påverkar sid_tick() */
static unsigned char note_idx    = 0;     /* peikar på noverande note */
static unsigned char ticks_left  = 0;     /* frames att for aktiv note */
static unsigned char gap_ticks   = 0;     /* små pausar for å retrigge ADSR */
static unsigned char current_is_rest = 0; /* sporar om aktiv note er pause */

/* --------------------------------------------------------------------------
   Hjelpefunksjonar
   -------------------------------------------------------------------------- */
static void sid_set_freq_word(unsigned short w)
{
    SID_V1_FREQ_LO = (unsigned char)(w & 0xFF);
    SID_V1_FREQ_HI = (unsigned char)(w >> 8);
}

static void sid_gate_on(void)
{
    /* Trygg og tydeleg lead-lyd */
    SID_V1_CTRL = (unsigned char)(CTRL_SAW | CTRL_GATE);
}

static void sid_gate_off(void)
{
    /* Gate av, men lat SAW stå på (berre for pausar) */
    SID_V1_CTRL = (unsigned char)(CTRL_SAW);
}

/* --------------------------------------------------------------------------
   Public API
   -------------------------------------------------------------------------- */
void sid_init(void)
{
    /* C89: alle deklarasjonar fyrst */
    /* (ingen lokale variablar nødvendig her) */

    /* Mastervolum fullt (nedre nibble = volum) */
    SID_MASTER_VOL = 0x0F; 

    /* Envelope for lead: rask attack, kort release for jamn puls */
    SID_V1_AD    = 0x28;   /* Attack=2, Decay=8 */
    SID_V1_SR    = 0xF2;   /* Sustain=15, Release=2 */

    /* Pulse width ~50% (relevant når PULSE er aktiv) */
    SID_V1_PW_LO = 0x00;
    SID_V1_PW_HI = 0x08;

    /* Start med gate av, så første note kan “tendres” reint */
    sid_gate_off();

    /* Startposisjon i melodien */
    note_idx   = 0;
    ticks_left = 0;
    gap_ticks  = 0;
    current_is_rest = 0;
    sid_enabled = 1;
}

void sid_tick(void)
{
    /* C89: deklarasjonar fyrst */
    unsigned short w;

    if (!sid_enabled) {
        return;
    }

    if (ticks_left > 0) {
        --ticks_left;
        if (ticks_left == 0) {
            if (!current_is_rest) {
                gap_ticks = NOTE_GAP_FRAMES;
                sid_gate_off();
            } else {
                gap_ticks = 0;
            }
        }
        return;
    }

    if (gap_ticks > 0) {
        --gap_ticks;
        if (gap_ticks > 0) {
            return;
        }
        /* gap ferdig – fall gjennom for å trigge neste note same frame */
    }

    /* Loop melodien */
    if (note_idx >= MELODY_COUNT) {
        note_idx = 0;
    }

    /* Hent note */
    w = melody_words[note_idx];
    current_is_rest = (unsigned char)(w == 0);

    if (current_is_rest) {
        sid_gate_off();
        sid_set_freq_word(0);
    } else {
        sid_set_freq_word(w);
        sid_gate_on();
    }

    ++note_idx;

    ticks_left = (unsigned char)(NOTE_LENGTH_FRAMES * tempo_mult);
    if (ticks_left == 0) {
        ticks_left = 1;
    }
}

void sid_pause(void)
{
    sid_enabled = 0;
    sid_gate_off();          /* konstante skriv */
    SID_MASTER_VOL = 0x00;   /* volum av */
}

void sid_resume(void)
{
    SID_MASTER_VOL = 0x0F;   /* volum på */
    sid_gate_off();
    note_idx = 0;
    ticks_left = 0;
    gap_ticks = 0;
    current_is_rest = 0;
    sid_enabled = 1;
}
