#ifndef SID_AUDIO_H
#define SID_AUDIO_H

#include <stdint.h>

// Kall frå main ved oppstart
void sid_init(void);

// Kallast av IRQ kvar frame (50 Hz)
void sid_tick(void);

#endif
