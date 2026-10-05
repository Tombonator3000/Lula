/* Host platform (SDL2). Called from guest threads unless noted. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define PLAT_W 640
#define PLAT_H 480

/* Copy one finished RGB565 frame for display (any thread). */
void plat_present_rgb565(const uint16_t *pixels, int width, int height, int pitch_bytes);
void plat_set_title(const char *title);
void plat_show_window(bool show);
void plat_show_cursor(bool show);
void plat_warp_mouse(int x, int y);
void plat_get_mouse(int *x, int *y);
/* Async key state: bit 15 = down, bit 0 = pressed since last query. */
uint16_t plat_async_key_state(int vk);
uint32_t plat_ticks_ms(void);
void plat_quit(int code);

/* Audio: the mixer callback runs on the audio thread and must not touch
 * guest code. */
typedef void (*PlatMixFn)(int16_t *stereo, int frames, void *user);
void plat_audio_start(int rate, PlatMixFn fn, void *user);
void plat_audio_lock(void);
void plat_audio_unlock(void);
int plat_audio_rate(void);
