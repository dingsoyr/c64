#ifndef SID_AUDIO_H
#define SID_AUDIO_H

#include <stdint.h>

/* Last sampledata frå disk. Returnerer 1 ved suksess */
unsigned char sid_load_sample(const char* filename, unsigned char device);

/* Spel av sampledataen (blokkerande). Ignorerer om ingen sample er lasta */
void sid_play_sample(void);

/* Hjelp til feilsøking: storleiken på sist lasta sample i byte */
unsigned int sid_sample_size(void);

#endif
