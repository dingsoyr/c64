#ifndef SID_AUDIO_H
#define SID_AUDIO_H

#include <stdint.h>

/* SID / AUDIO HELPERS (declarations) */
void sid_progress_init(void);
void sid_progress_tick(void);
void sid_progress_shutdown(void);
void sid_progress_mute_now(void);

#endif /* SID_AUDIO_H */
