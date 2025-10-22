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
#define CTRL_GATE  0x01    /* bit0 */
#define CTRL_TRI   0x10
#define CTRL_SAW   0x20
#define CTRL_PULSE 0x40
#define CTRL_NOISE 0x80

/* Enkel noteloop (C–E–G–C). Bruk faste tal i staden for sizeof for cc65-kompat. */
static const unsigned short notes[] = { 0x0456, 0x04A9, 0x050C, 0x058A };
#define NUM_NOTES 4

static unsigned char note_idx  = 0;
static unsigned char frame_div = 0;

/* --- global lyd-tilstand --- */
static unsigned char sid_enabled = 1;

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

    /* set ADSR/waveform osv. */
    sid_enabled = 1;
}

void sid_tick(void)
{
    /* C89: deklarasjonar fyrst */
    unsigned short f;

    /* C89: kode først – avbryt raskt om pauset */
    if (!sid_enabled) {
        return;
    }

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

/* midlertidig stillleik (bruk når du viser/streamar bildet) */
void sid_pause(void)
{
    /* stopp vidare oppdatering */
    sid_enabled = 0;

    /* slepp alle gate (utan å miste waveform-bits) */
    SID_V1_CTRL = (unsigned char)(SID_V1_CTRL & (unsigned char)~CTRL_GATE);
    /* Har du ikkje voice 2/3, går det fint å la desse stå – elles: */
#ifdef SID_V2_CTRL
    SID_V2_CTRL = (unsigned char)(SID_V2_CTRL & (unsigned char)~CTRL_GATE);
#endif
#ifdef SID_V3_CTRL
    SID_V3_CTRL = (unsigned char)(SID_V3_CTRL & (unsigned char)~CTRL_GATE);
#endif

    /* sett mastervolumet til 0 (bevarer øvre 4 bits om dei er brukt til filter) */
    SID_MASTER_VOL = (unsigned char)(SID_MASTER_VOL & 0xF0);
}

/* ta lyden tilbake */
void sid_resume(void)
{
    /* volum på igjen (15) */
    SID_MASTER_VOL = (unsigned char)((SID_MASTER_VOL & 0xF0) | 0x0F);

    /* slå på gate att for melodistemmer (voice 3 lar vi vere av til neste “drum”) */
    SID_V1_CTRL = (unsigned char)(SID_V1_CTRL | CTRL_GATE);
#ifdef SID_V2_CTRL
    SID_V2_CTRL = (unsigned char)(SID_V2_CTRL | CTRL_GATE);
#endif

    /* no får tick lov å gjere jobben si igjen */
    sid_enabled = 1;
}