/* src/main.c – Stream loader for Koala (no KERNAL LOAD overwrite) */
#include <c64.h>
#include <cbm.h>
#include <conio.h>
#include <string.h>
#include <stdint.h>

#define DEVNO 8

/* --- SID helpers --- */
#define SID_BASE   ((volatile uint8_t*)0xD400)
#define SID_V1     (SID_BASE + 0)   /* 0..6  */
#define SID_V2     (SID_BASE + 7)   /* 7..13 */
#define SID_V3     (SID_BASE + 14)  /* 14..20 */
#define SID_FLT    (SID_BASE + 21)  /* 21..23 */
#define SID_MODEVOL (SID_BASE + 24) /* $D418 */

static uint8_t sid_prev_modevol = 0;
static uint16_t prng = 0xACE1; /* enkel PRNG for variasjon */

/* veldig enkel, rask PRNG (xorshift/LCG-ish) */
static uint16_t rnd16(void) {
    prng ^= prng << 7;
    prng ^= prng >> 9;
    prng ^= prng << 8;
    return prng;
}

/* Init: sett opp voice 3 til korte noise-bursts */
static void sid_progress_init(void) {
    sid_prev_modevol = SID_MODEVOL[0];
    SID_MODEVOL[0] = (sid_prev_modevol & 0xF0) | 0x07; /* volum ~7/15, ingen filter */

    /* ADSR for voice3: rask attack, middels decay, litt sustain, kort release */
    SID_V3[5] = 0x28;  /* AD: A=2, D=8 */
    SID_V3[6] = 0x64;  /* SR: S=6, R=4 */

    /* start med gate av, noise valgt */
    SID_V3[4] = 0x01;  /* NOISE (bit0), gate=0 */
}

/* Lag eit lite burst: varier frekvens, retrig gate */
static void sid_progress_tick(void) {
    uint16_t f = rnd16();

    /* frekvens for voice3 */
    SID_V3[0] = (uint8_t)(f & 0xFF);       /* FREQ LO */
    SID_V3[1] = (uint8_t)((f >> 8) & 0xFF);/* FREQ HI */

    /* rask retrigger: gate off -> on med NOISE */
    SID_V3[4] = 0x01;        /* NOISE, gate off */
    SID_V3[4] = 0x11;        /* NOISE + GATE (bit4) */
}

/* Skru av lyd og gjenopprett volum */
static void sid_progress_shutdown(void) {
    SID_V1[4] = 0x00; SID_V2[4] = 0x00; SID_V3[4] = 0x00;
    /* Gjenopprett heile MODEVOL (inkl. volum) slik det var */
    SID_MODEVOL[0] = sid_prev_modevol;
}

/* --- Legg til: hard mute av SID akkurat no --- */
static void sid_progress_mute_now(void) {
    /* Slå av gate på alle stemmer */
    SID_V1[4] = 0x00;
    SID_V2[4] = 0x00;
    SID_V3[4] = 0x00;

    /* (Valfritt, men effektivt): reset oscillator 3 momentant med TEST */
    SID_V3[4] = 0x08;   /* TEST=1 */
    SID_V3[4] = 0x00;   /* TEST=0 */

    /* Mastervolum til 0 (behald filter-bits) */
    SID_MODEVOL[0] &= 0xF0;
}




/* Koala memory layout (target addresses) */
#define ADR_BITMAP   ((uint8_t*)0x6000)  /* 8000 bytes */
#define ADR_SCREEN   ((uint8_t*)0x7F40)  /* 1000 bytes */
#define ADR_COLOR    ((uint8_t*)0x8328)  /* 1000 bytes */
#define ADR_BG       ((uint8_t*)0x8710)  /* 1 byte */

/* Liten border-puls: skift farge roleg over tid */
static void pulse_border_tick(void) {
    static unsigned char t = 0;
    VIC.bordercolor = (t >> 3) & 0x07; /* skift farge ca. kvar 8. tick */
    ++t;
}

static unsigned char read_bytes_pulsed(uint8_t* dst, unsigned len) {
    unsigned i;
    for (i = 0; i < len; ++i) {
        int c = cbm_k_chrin();
        if (c < 0) return 0;
        dst[i] = (uint8_t)c;

        /* Pulser ca. kvar 64. byte */
        if ((i & 0x3F) == 0) {       /* ca. kvar 64. byte */
            pulse_border_tick();
            sid_progress_tick();      /* <<< lag eit lite lyd-burst */
        }

        if (cbm_k_readst() & 0x40) { /* EOI for tidleg? */
            return (i + 1 == len) ? 1 : 0;
        }
    }
    return 1;
}

/* Vent N frames (raster-synk) for jamn animasjon */
static void wait_frames(unsigned char frames) {
    volatile unsigned char *RASTER = (unsigned char*)0xD012;
    unsigned char start;
    /* cc65: ikkje blande deklarasjon/kode, så bruk lokale temp-variablar over */
    while (frames--) {
        start = *RASTER;
        while (*RASTER == start) { }
    }
}

/* Slå på bitmap-modus, blank skjerm (SCREEN/COLOR), sett bg/border */
static void init_bitmap_blank(unsigned char bg)
{
    unsigned i;

    /* Bank $4000–$7FFF (bitmap=$2000 in-bank = $6000 CPU) */
    CIA2.pra = (CIA2.pra & 0xFC) | 0x02;   /* $DD00 = ..10 (bank 2) */

    /* Skru av display mens vi set opp (bit 4 i $D011) */
    VIC.ctrl1 = (VIC.ctrl1 & ~0x10);       /* display OFF */
    VIC.ctrl2 = 0x18;                      /* multicolor on */
    VIC.addr  = 0x18;                      /* screen=$0400, bitmap=$2000 (in-bank) */

    VIC.bgcolor0    = bg;                  /* D021 */
    VIC.bordercolor = bg;                  /* D020 */

    /* Blank SCREEN ($4400 i bank 2) og COLOR RAM ($D800) */
    memset((void*)0x4400, 0x00, 1000);
    for (i = 0; i < 1000; ++i) {
        COLOR_RAM[i] = 0x00;
    }

    /* Slå på bitmap + display */
    VIC.ctrl1 = 0x3B;  /* bitmap + display ON (samme verdi som du brukte) */
}

/* Les 1000 byte SCREEN frå open fil, teikn rad-for-rad medan vi les */
static unsigned char read_screen_progress(unsigned char frames_per_row) {
    unsigned row, col;
    int ch;
    for (row = 0; row < 25; ++row) {
        for (col = 0; col < 40; ++col) {
            ch = cbm_k_chrin();
            if (ch < 0 || (cbm_k_readst() & 0x40)) return 0;  /* feil/EOI for tidleg */
            ADR_SCREEN[row*40 + col] = (uint8_t)ch;
            //((uint8_t*)0x4400)[row*40 + col] = (uint8_t)ch;    /* vis straks */
        }
        wait_frames(frames_per_row);   /* roleg sveip nedover */
    }
    return 1;
}

/* Les 1000 byte COLOR frå open fil, og vis rad komplett etter kvar rad */
static unsigned char read_color_progress(unsigned char frames_per_row) {
    unsigned row, col;
    int ch;
    for (row = 0; row < 25; ++row) {
        for (col = 0; col < 40; ++col) {
            ch = cbm_k_chrin();
            if (ch < 0 || (cbm_k_readst() & 0x40)) return 0;
            ADR_COLOR[row*40 + col] = (uint8_t)ch;
        }
        /* >>> Vis denne rada komplett no: */
        memcpy((void*)(0x4400 + row*40), ADR_SCREEN + row*40, 40);
        for (col = 0; col < 40; ++col) {
            COLOR_RAM[row*40 + col] = ADR_COLOR[row*40 + col] & 0x0F;
        }
        wait_frames(frames_per_row);
    }
    return 1;
}

/* Strøymlast Koala og start utteikning straks bitmap (8000) er klar.
   frames_per_row styrer kor sakte det “rullar” nedover (1–6 er fint). */
static unsigned char stream_load_koala_progressive(const char* name, unsigned char frames_per_row)
{
    unsigned char rc;
    uint8_t lo, hi;
    uint8_t bg;

    sid_progress_init();

    /* Opne og sjekk ev. $6000-PRG-header */
    rc = cbm_open(2, DEVNO, 2, (char*)name);
    if (rc != 0 || cbm_k_chkin(2) != 0) {
        if (rc == 0) { cbm_k_clrch(); cbm_close(2); }
        return 0;
    }

    /* Les dei to første byta (mogleg PRG-load address) */
    lo = (uint8_t)cbm_k_chrin(); if (cbm_k_readst() & 0x40) { cbm_k_clrch(); cbm_close(2); return 0; }
    hi = (uint8_t)cbm_k_chrin(); if (cbm_k_readst() & 0x40) { cbm_k_clrch(); cbm_close(2); return 0; }

    if (!(lo == 0x00 && hi == 0x60)) {
        /* RAW: reopen frå start */
        cbm_k_clrch();
        cbm_close(2);
        rc = cbm_open(2, DEVNO, 2, (char*)name);
        if (rc != 0 || cbm_k_chkin(2) != 0) {
            if (rc == 0) { cbm_k_clrch(); cbm_close(2); }
            return 0;
        }
    }

    /* 1) Les BITMAP (8000) – må vere klar før vi viser noko */
    if (!read_bytes_pulsed(ADR_BITMAP, 8000)) { sid_progress_shutdown(); cbm_k_clrch(); cbm_close(2); return 0; }

    /* >>> Stopp all lyd før vi slår av/på displayet og teiknar */
    sid_progress_mute_now();

    /* 2) Slå på bitmap-modus med tom skjerm – start vising no */
    init_bitmap_blank(0);  /* midlertidig bg=0; blir sett korrekt til slutt */

    /* 3) Les SCREEN (1000) – om du ønskjer éin-pass vising,
          la read_screen_progress KUN lagre (ikkje teikne), og
          la read_color_progress teikne begge pr. rad. */
    if (!read_screen_progress(frames_per_row)) { sid_progress_shutdown();cbm_k_clrch(); cbm_close(2); return 0; }

    /* 4) Les COLOR (1000) – fyll Color RAM (og ev. skjermrad) medan vi les */
    if (!read_color_progress(frames_per_row))  { sid_progress_shutdown();cbm_k_clrch(); cbm_close(2); return 0; }

    /* 5) Les bakgrunn (1) – VIKTIG: tillat EOI på siste byte!
          Bruk read_bytes() som aksepterer EOI akkurat på slutt. */
    if (!read_bytes_pulsed(ADR_BG, 1)) { sid_progress_shutdown();cbm_k_clrch(); cbm_close(2); return 0; }
    bg = *ADR_BG;
    VIC.bgcolor0    = bg;
    VIC.bordercolor = bg;

    sid_progress_shutdown();
    cbm_k_clrch();
    cbm_close(2);
    return 1;
}


/* Skriv tekst sakte, teikn for teikn, med valfri pause mellom kvar */
static void slowprint(const char* s, unsigned char frames_per_char)
{
    unsigned i;
    for (i = 0; s[i] != 0; ++i) {
        cputc(s[i]);          /* skriv eitt teikn */
        wait_frames(frames_per_char);  /* vent litt */
    }
}


/* Ekte video-frames (PAL/NTSC) ved å vente på start av ny frame (raster==0 med MSB=1) */
static void wait_video_frames(unsigned int frames) {
    volatile unsigned char* RASTER  = (unsigned char*)0xD012; /* low 8 bits */
    volatile unsigned char* CTRL1   = (unsigned char*)0xD011; /* bit7 = MSB of raster */

    while (frames--) {
        /* 1) Forlat ev. “start-of-frame” om vi tilfeldigvis står der no */
        while ( (*CTRL1 & 0x80) && (*RASTER == 0) ) { /* spin */ }

        /* 2) Vent til NESTE start-of-frame (raster MSB=1 og low=0) */
        while ( !((*CTRL1 & 0x80) && (*RASTER == 0)) ) { /* spin */ }
    }
}

/* Komfort: ca S sekund på PAL (50 Hz) */
static void wait_video_seconds(unsigned int s) {
    unsigned int i;
    for (i = 0; i < s; ++i) wait_video_frames(50);
}


// void main(void) {
//     bordercolor(COLOR_BLACK); bgcolor(COLOR_BLACK);
//     textcolor(COLOR_WHITE); clrscr();

//     cprintf("Koala loader (dev %u)\r\n", (unsigned)DEVNO);
//     cprintf("\r\n--- DIRECTORY ($) ---\r\n");
//     print_dir(DEVNO);
//     cprintf("\r\n--- END DIRECTORY ---\r\n");

//     cprintf("\r\nStreaming \"vreid.koa\"...");
//     if (!stream_load_koala_progressive("vreid.koa", 4)) {   /* 2 = roleg rull */
//         cprintf(" FAIL.\r\n");
//         while (1) { }
//     }
//     cprintf(" OK\r\nImage displayed.\r\n");    
// }


void main(void) {
    unsigned i;

    /* --- Oppstart: svart skjerm, kvit tekst --- */
    bordercolor(COLOR_BLACK); bgcolor(COLOR_BLACK);
    textcolor(COLOR_WHITE); clrscr();

    /* --- Liten "boot"-intro i Vreid-stil --- */
    textcolor(COLOR_GRAY3);
    slowprint("\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n             VREID PRESENTS\r\n", 400);

    textcolor(COLOR_WHITE);
    slowprint("\r\n\r\n                C64 INTRO\r\n", 400);

    textcolor(COLOR_LIGHTBLUE);
    slowprint("\r\n\r\n       STREAMING LOGO FROM DISK...\r\n", 400);

    /* Enkel border-puls i forkant (40 frames) */
    for (i = 0; i < 40; ++i) {
        VIC.bordercolor = (i >> 1) & 0x07;
        wait_frames(1);
    }

    // /* --- Progressiv streaming + vising av logo --- */
    // cprintf("\r\nStreaming \"vreid.koa\" (slow roll)...");
    if (!stream_load_koala_progressive("vreid.koa", 4)) {   /* høgare tal = tregare rull */
        cprintf(" FAIL.\r\n");
        while (1) { }
    }

    /* ferdig – stå her */
    wait_video_seconds(5); 
    // wait_frames(10000);
    // while (!kbhit()) {
    //     wait_frames(1);        // lite CPU-spinn, ~50Hz
    // }
    // cgetc(); // tøm tast    

    /* Lukk I/O om noko står ope */
    cbm_k_clrch();
    cbm_close(2);

    /* Normal tekstmodus + bank 0 */
    CIA2.pra = (CIA2.pra & 0xFC) | 0x03;
    *(uint8_t*)0x01 = 0x37;
    VIC.ctrl1 = 0x1B;
    VIC.ctrl2 = 0x08;

    /* Lower/Upper-modus + charset $1800 */
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
        if (kbhit()) {       /* conio-funksjon: sjekk om tast trykt */
            cgetc();         /* les og tøm */
            break;
        }
        VIC.bordercolor = i & 7;  /* litt “pulse” */
        wait_frames(3);
        i++;
    }

    /* Full KERNAL-reset → som power-on (utan å slå av/på) */
    __asm__(
        "sei         \n"   /* steng IRQ for trygg hopp */
        "lda #$37    \n"   /* map inn IO + KERNAL + BASIC */
        "sta $01     \n"
        "jmp ($fffc) \n"   /* hopp via reset-vektoren */
    );
}
