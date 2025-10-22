#ifndef SID_AUDIO_H
#define SID_AUDIO_H

#include <stdint.h>

// Kall frå main ved oppstart
void sid_init(void);

// Kallast av IRQ kvar frame (50 Hz)
void sid_tick(void);

// Pause/resume musikk (ikkje brukt her
void sid_pause(void);

// Resume musikk
void sid_resume(void);

#endif
