/* Shared between ddraw.c and gdi32.c. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define SURFACE_MAGIC 0x53555246u

typedef struct Surface {
    uint32_t magic;
    uint32_t obj;                 /* guest COM object */
    uint32_t width, height, pitch;
    uint32_t mem;                 /* guest address of RGB565 pixels */
    uint32_t caps, refs, locked;
    bool primary, is_back;
    struct Surface *back;         /* attached back buffer of a flip chain */
    bool has_ck_src, has_ck_dst;
    uint32_t ck_lo, ck_hi, ckd_lo, ckd_hi;
    uint32_t dc;                  /* GDI DC while GetDC is active */
} Surface;

typedef struct DDState {
    uint32_t obj, refs, coop;
    uint32_t width, height;
    bool v2;
    Surface *primary;
} DDState;

DDState *ddraw_state(void);
Surface *ddraw_surface(uint32_t obj);
void ddraw_present(void);
