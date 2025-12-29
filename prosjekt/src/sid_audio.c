#include "sid_audio.h"

#include <cbm.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>

/* SID master volum-register (bit 0-3 = volum) */
#define SID_MASTER_VOL (*(volatile unsigned char*)0xD418)

/* CIA1 Timer A register */
#define CIA1_TA_LO (*(volatile unsigned char*)0xDC04)
#define CIA1_TA_HI (*(volatile unsigned char*)0xDC05)
#define CIA1_ICR   (*(volatile unsigned char*)0xDC0D)
#define CIA1_CRA   (*(volatile unsigned char*)0xDC0E)

/* 985248 Hz / 6000 ≈ 164.2 -> bruk 164 for ~6 kHz sample-rate */
#define SAMPLE_TIMER_RELOAD 164u

static unsigned char* sample_bytes = 0;
static unsigned int   sample_size  = 0;

unsigned char sid_load_sample(const char* filename, unsigned char device)
{
    unsigned char rc;
    unsigned int capacity = 0;
    int byte;

    if (sample_bytes != 0) {
        free(sample_bytes);
        sample_bytes = 0;
        sample_size = 0;
    }

    rc = cbm_open(3, device, 0, (char*)filename);
    if (rc != 0 || cbm_k_chkin(3) != 0) {
        if (rc == 0) {
            cbm_k_clrch();
            cbm_close(3);
        }
        return 0;
    }

    /* PRG filer startar med 2 byte load-adresse – hopp over desse */
    (void)cbm_k_chrin();
    (void)cbm_k_chrin();

    {
        static const unsigned int report_step = 2048u;
        unsigned int next_report = report_step;

        cprintf(" LOADING SAMPLE...\r\n");

        while (1) {
            byte = cbm_k_chrin();
            if (byte < 0) {
                free(sample_bytes);
                sample_bytes = 0;
                sample_size = 0;
                break;
            }

            if (sample_size >= capacity) {
                unsigned int new_cap = (capacity == 0) ? 2048u : (capacity + 2048u);
                unsigned char* new_buf = (unsigned char*)realloc(sample_bytes, new_cap);
                if (!new_buf) {
                    free(sample_bytes);
                    sample_bytes = 0;
                    sample_size = 0;
                    break;
                }
                sample_bytes = new_buf;
                capacity = new_cap;
            }

            sample_bytes[sample_size++] = (unsigned char)byte;

            if (sample_size >= next_report) {
                cprintf("  %u bytes...\r\n", sample_size);
                next_report += report_step;
            }

            if (cbm_k_readst() & 0x40) {
                break;  /* EOI set etter at byte er levert */
            }
        }
    }

    cbm_k_clrch();
    cbm_close(3);

    return (sample_size > 0) ? 1 : 0;
}

static void wait_timer_a_tick(void)
{
    while (!(CIA1_ICR & 0x01)) {
        /* poll til Timer A underflow (ICR bit0) */
    }
}

void sid_play_sample(void)
{
    unsigned int i;
    unsigned char base_volume;
    unsigned char old_cra;
    unsigned char old_ta_lo;
    unsigned char old_ta_hi;

    if (sample_bytes == 0 || sample_size == 0) {
        return;
    }

    base_volume = (unsigned char)(SID_MASTER_VOL & 0xF0);
    old_cra = CIA1_CRA;
    old_ta_lo = CIA1_TA_LO;
    old_ta_hi = CIA1_TA_HI;

    CIA1_CRA = (unsigned char)(old_cra & ~0x01);   /* stopp Timer A */
    CIA1_TA_LO = (unsigned char)(SAMPLE_TIMER_RELOAD & 0xFF);
    CIA1_TA_HI = (unsigned char)(SAMPLE_TIMER_RELOAD >> 8);
    (void)CIA1_ICR;                                /* blank evt. flagg */
    CIA1_CRA = 0x11;                               /* start + force load */

    asm("php");
    asm("sei");

    for (i = 0; i < sample_size; ++i) {
        unsigned char byte = sample_bytes[i];

        wait_timer_a_tick();
        SID_MASTER_VOL = (unsigned char)(base_volume | (byte >> 4));

        wait_timer_a_tick();
        SID_MASTER_VOL = (unsigned char)(base_volume | (byte & 0x0F));
    }

    asm("plp");

    CIA1_CRA = (unsigned char)(old_cra & ~0x01);
    CIA1_TA_LO = old_ta_lo;
    CIA1_TA_HI = old_ta_hi;
    CIA1_CRA = old_cra;

    SID_MASTER_VOL = base_volume;
}

unsigned int sid_sample_size(void)
{
    return sample_size;
}
