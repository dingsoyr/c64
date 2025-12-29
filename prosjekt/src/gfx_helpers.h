#ifndef GFX_HELPERS_H
#define GFX_HELPERS_H

#include <stdint.h>

/* Koala memory layout (target addresses) */
#define ADR_BITMAP   ((uint8_t*)0x6000)  /* 8000 bytes */
#define ADR_SCREEN   ((uint8_t*)0x7F40)  /* 1000 bytes */
#define ADR_COLOR    ((uint8_t*)0x8328)  /* 1000 bytes */
#define ADR_BG       ((uint8_t*)0x8710)  /* 1 byte */

/* VIC / GRAPHICS HELPERS (declarations) */
void pulse_border_tick(void);
void wait_frames(unsigned char frames);
void draw_progress_bar(unsigned char row, unsigned char col, unsigned char width,
					   unsigned short current, unsigned short total);
void init_bitmap_blank(unsigned char bg);
unsigned char read_screen_progress(unsigned char frames_per_row);
unsigned char read_color_progress(unsigned char frames_per_row);

/* Video-timed waits */
void wait_video_frames(unsigned int frames);
void wait_video_seconds(unsigned int s);

#endif /* GFX_HELPERS_H */
