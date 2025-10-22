#include "sid_audio.h"

/* SID base + voice 1 register */
#define SID_BASE       0xD400
#define SID_V1_FREQ_LO (*(volatile unsigned char*)(SID_BASE+0x00))
#define SID_V1_FREQ_HI (*(volatile unsigned char*)(SID_BASE+0x01))
#define SID_V1_PW_LO   (*(volatile unsigned char*)(SID_BASE+0x02))
#define SID_V1_PW_HI   (*(volatile unsigned char*)(SID_BASE+0x03))
#define SID_V1_CTRL    (*(volatile unsigned char*)(SID_BASE+0x04))
#define SID_V1_AD      (*(volatile unsigned char*)(SID_BASE+0x05))
#define SID_V1_SR      (*(volatile unsigned char*)(SID_BASE+0x06))
#define SID_MASTER_VOL (*(volatile unsigned char*)(SID_BASE+0x18))

/* Enkel noteloop (C–E–G–C). Bruk faste tal i staden for sizeof for cc65-kompat. */
static const unsigned short notes[] = { 0x0456, 0x04A9, 0x050C, 0x058A };
#define NUM_NOTES 4

static unsigned char note_idx  = 0;
static unsigned char frame_div = 0;

void sid_init(void)
{
    /* Mastervolum */
    SID_MASTER_VOL = 0x0F;

    /* ADSR og PW (PW er relevant om du byter til pulse seinare) */
    SID_V1_AD    = 0x28;    /* Attack=2, Decay=8 */
    SID_V1_SR    = 0x88;    /* Sustain=8, Release=8 */
    SID_V1_PW_LO = 0x00;
    SID_V1_PW_HI = 0x08;

    /* Start med triangle + gate on (bit4=triangle, bit0=gate) */
    SID_V1_CTRL  = 0x11;
}

void sid_tick(void)
{
    /* C89: deklarasjonar fyrst */
    unsigned short f;

    /* Del ned tempoet litt: byt note ca. kvar 6. frame (~8.3 Hz) */
    frame_div++;
    if (frame_div < 6) {
        return;
    }
    frame_div = 0;

    f = notes[note_idx];
    SID_V1_FREQ_LO = (unsigned char)(f & 0xFF);
    SID_V1_FREQ_HI = (unsigned char)(f >> 8);

    note_idx++;
    if (note_idx >= NUM_NOTES) {
        note_idx = 0;
    }
}
