/* DirectDraw (DirectX 5 subset) over plain memory surfaces.
 *
 * All surfaces are 16-bit RGB565 system-memory surfaces in guest memory, so
 * the game's own blitters work on them unchanged. Whatever lands on the
 * primary surface is handed to the platform for display. */
#include "com.h"
#include "../platform.h"
#include "ddraw_internal.h"

#include <pthread.h>

#define DD_OK 0u
#define DDERR_INVALIDPARAMS E_INVALIDARG
#define DDERR_NOTFOUND 0x887600ffu
#define DDERR_UNSUPPORTED E_NOTIMPL
#define DDERR_GENERIC E_FAIL
#define DDERR_SURFACEBUSY 0x887601aeu
#define DDERR_NOTLOCKED 0x887602b0u

#define DDSD_CAPS 0x1u
#define DDSD_HEIGHT 0x2u
#define DDSD_WIDTH 0x4u
#define DDSD_PITCH 0x8u
#define DDSD_BACKBUFFERCOUNT 0x20u
#define DDSD_LPSURFACE 0x800u
#define DDSD_PIXELFORMAT 0x1000u
#define DDSD_CKSRCBLT 0x10000u

#define DDSCAPS_BACKBUFFER 0x4u
#define DDSCAPS_COMPLEX 0x8u
#define DDSCAPS_FLIP 0x10u
#define DDSCAPS_FRONTBUFFER 0x20u
#define DDSCAPS_OFFSCREENPLAIN 0x40u
#define DDSCAPS_PRIMARYSURFACE 0x200u
#define DDSCAPS_SYSTEMMEMORY 0x800u
#define DDSCAPS_VIDEOMEMORY 0x4000u
#define DDSCAPS_VISIBLE 0x8000u
#define DDSCAPS_LOCALVIDMEM 0x10000000u

#define DDBLT_COLORFILL 0x400u
#define DDBLT_KEYDEST 0x2000u
#define DDBLT_KEYDESTOVERRIDE 0x4000u
#define DDBLT_KEYSRC 0x8000u
#define DDBLT_KEYSRCOVERRIDE 0x10000u
#define DDBLTFAST_SRCCOLORKEY 0x1u
#define DDBLTFAST_DESTCOLORKEY 0x2u
#define DDCKEY_DESTBLT 0x2u
#define DDCKEY_SRCBLT 0x8u

static DDState dd;
static uint32_t vt_dd, vt_surf, vt_clip, vt_pal;
static pthread_mutex_t dd_lock = PTHREAD_MUTEX_INITIALIZER;

DDState *ddraw_state(void) { return &dd; }

Surface *ddraw_surface(uint32_t obj)
{
    void *h = com_host(obj);
    if (!h || ((Surface *)h)->magic != SURFACE_MAGIC)
        return NULL;
    return h;
}

/* ---------------------------------------------------------------- present */
void ddraw_present(void)
{
    Surface *p = dd.primary;
    if (!p || !p->mem)
        return;
    plat_present_rgb565((const uint16_t *)(g_mem + p->mem), (int)p->width, (int)p->height,
                        (int)p->pitch);
}

static void touched(Surface *s)
{
    if (s && s->primary)
        ddraw_present();
}

/* ---------------------------------------------------------------- helpers */
static void write_pixel_format(uint32_t pf)
{
    W32(pf + 0, 32);
    W32(pf + 4, 0x40);        /* DDPF_RGB */
    W32(pf + 8, 0);
    W32(pf + 12, 16);
    W32(pf + 16, 0xf800);
    W32(pf + 20, 0x07e0);
    W32(pf + 24, 0x001f);
    W32(pf + 28, 0);
}

static void fill_desc(Surface *s, uint32_t d)
{
    uint32_t size = R32(d);
    memset(g_mem + d, 0, size >= 108 ? 108 : 108);
    W32(d + 0, 108);
    W32(d + 4, DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PITCH | DDSD_PIXELFORMAT |
                   (s->has_ck_src ? DDSD_CKSRCBLT : 0) | (s->back ? DDSD_BACKBUFFERCOUNT : 0));
    W32(d + 8, s->height);
    W32(d + 12, s->width);
    W32(d + 16, s->pitch);
    W32(d + 20, s->back ? 1 : 0);
    W32(d + 64, s->ck_lo);
    W32(d + 68, s->ck_hi);
    write_pixel_format(d + 72);
    W32(d + 104, s->caps);
}

static Surface *new_surface(uint32_t w, uint32_t h, uint32_t caps)
{
    Surface *s = calloc(1, sizeof *s);
    s->magic = SURFACE_MAGIC;
    s->width = w;
    s->height = h;
    s->pitch = (w * 2 + 3) & ~3u;
    s->caps = caps;
    s->mem = rt_heap_alloc(s->pitch * h + 16);
    s->refs = 1;
    s->obj = com_new(vt_surf, s);
    return s;
}

static bool clip_rect(int32_t *x, int32_t *y, int32_t *w, int32_t *h, int32_t maxw, int32_t maxh,
                      int32_t *sx, int32_t *sy)
{
    if (*x < 0) { *sx -= *x; *w += *x; *x = 0; }
    if (*y < 0) { *sy -= *y; *h += *y; *y = 0; }
    if (*x + *w > maxw) *w = maxw - *x;
    if (*y + *h > maxh) *h = maxh - *y;
    return *w > 0 && *h > 0;
}

static void read_rect(uint32_t r, Surface *s, int32_t out[4])
{
    if (r) {
        out[0] = (int32_t)R32(r);
        out[1] = (int32_t)R32(r + 4);
        out[2] = (int32_t)R32(r + 8);
        out[3] = (int32_t)R32(r + 12);
    } else {
        out[0] = out[1] = 0;
        out[2] = (int32_t)s->width;
        out[3] = (int32_t)s->height;
    }
}

/* Copy src rectangle to dst at (dx, dy) with optional source/dest color keys. */
static void blit(Surface *dst, int32_t dx, int32_t dy, Surface *src, int32_t sr[4],
                 bool use_src_key, uint32_t src_lo, uint32_t src_hi,
                 bool use_dst_key, uint32_t dst_lo, uint32_t dst_hi)
{
    int32_t sx = sr[0], sy = sr[1], w = sr[2] - sr[0], h = sr[3] - sr[1];
    /* Clip against the source first, then the destination. */
    int32_t ddx = dx, ddy = dy;
    if (!clip_rect(&sx, &sy, &w, &h, (int32_t)src->width, (int32_t)src->height, &ddx, &ddy))
        return;
    dx += sx - sr[0];
    dy += sy - sr[1];
    if (!clip_rect(&dx, &dy, &w, &h, (int32_t)dst->width, (int32_t)dst->height, &sx, &sy))
        return;
    bool overlap = dst->mem == src->mem && dy > sy;
    for (int32_t row = 0; row < h; row++) {
        int32_t r = overlap ? h - 1 - row : row;
        uint16_t *dp = (uint16_t *)(g_mem + dst->mem + (uint32_t)(dy + r) * dst->pitch) + dx;
        const uint16_t *sp = (const uint16_t *)(g_mem + src->mem + (uint32_t)(sy + r) * src->pitch) + sx;
        if (!use_src_key && !use_dst_key) {
            memmove(dp, sp, (size_t)w * 2);
            continue;
        }
        for (int32_t i = 0; i < w; i++) {
            uint16_t p = sp[i];
            if (use_src_key && p >= src_lo && p <= src_hi)
                continue;
            if (use_dst_key && !(dp[i] >= dst_lo && dp[i] <= dst_hi))
                continue;
            dp[i] = p;
        }
    }
}

static void stretch(Surface *dst, int32_t dr[4], Surface *src, int32_t sr[4], bool key,
                    uint32_t lo, uint32_t hi)
{
    int32_t dw = dr[2] - dr[0], dh = dr[3] - dr[1], sw = sr[2] - sr[0], sh = sr[3] - sr[1];
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0)
        return;
    for (int32_t y = 0; y < dh; y++) {
        int32_t ty = dr[1] + y, fy = sr[1] + (int32_t)((int64_t)y * sh / dh);
        if (ty < 0 || ty >= (int32_t)dst->height || fy < 0 || fy >= (int32_t)src->height)
            continue;
        uint16_t *dp = (uint16_t *)(g_mem + dst->mem + (uint32_t)ty * dst->pitch);
        const uint16_t *sp = (const uint16_t *)(g_mem + src->mem + (uint32_t)fy * src->pitch);
        for (int32_t x = 0; x < dw; x++) {
            int32_t tx = dr[0] + x, fx = sr[0] + (int32_t)((int64_t)x * sw / dw);
            if (tx < 0 || tx >= (int32_t)dst->width || fx < 0 || fx >= (int32_t)src->width)
                continue;
            uint16_t p = sp[fx];
            if (key && p >= lo && p <= hi)
                continue;
            dp[tx] = p;
        }
    }
}

static void fill(Surface *dst, int32_t r[4], uint16_t color)
{
    int32_t x = r[0], y = r[1], w = r[2] - r[0], h = r[3] - r[1], sx = 0, sy = 0;
    if (!clip_rect(&x, &y, &w, &h, (int32_t)dst->width, (int32_t)dst->height, &sx, &sy))
        return;
    for (int32_t row = 0; row < h; row++) {
        uint16_t *dp = (uint16_t *)(g_mem + dst->mem + (uint32_t)(y + row) * dst->pitch) + x;
        for (int32_t i = 0; i < w; i++)
            dp[i] = color;
    }
}

/* ================================================================ IDirectDraw */
#define THIS ARG(0)

static uint32_t dd_QueryInterface(Cpu *c)
{
    char g[40];
    guid_str(ARG(1), g);
    RT_INFO("IDirectDraw::QueryInterface(%s)", g);
    W32(ARG(2), THIS);
    dd.refs++;
    RET(3, DD_OK);
}

static uint32_t dd_AddRef(Cpu *c) { RET(1, ++dd.refs); }
static uint32_t dd_Release(Cpu *c) { RET(1, dd.refs ? --dd.refs : 0); }

static uint32_t dd_CreateClipper(Cpu *c)
{
    static int dummy;
    W32(ARG(2), com_new(vt_clip, &dummy));
    RET(4, DD_OK);
}

static uint32_t dd_CreatePalette(Cpu *c)
{
    static int dummy;
    W32(ARG(3), com_new(vt_pal, &dummy));
    RET(5, DD_OK);
}

static uint32_t dd_CreateSurface(Cpu *c)
{
    uint32_t d = ARG(1), out = ARG(2);
    uint32_t flags = R32(d + 4);
    uint32_t caps = (flags & DDSD_CAPS) ? R32(d + 104) : 0;
    Surface *s;
    if (caps & DDSCAPS_PRIMARYSURFACE) {
        s = new_surface(dd.width, dd.height, caps | DDSCAPS_FRONTBUFFER | DDSCAPS_VISIBLE |
                        DDSCAPS_VIDEOMEMORY | DDSCAPS_LOCALVIDMEM);
        s->primary = true;
        dd.primary = s;
        uint32_t backs = (flags & DDSD_BACKBUFFERCOUNT) ? R32(d + 20) : 0;
        if (backs) {
            Surface *b = new_surface(dd.width, dd.height, (caps & ~(DDSCAPS_PRIMARYSURFACE |
                                     DDSCAPS_FRONTBUFFER | DDSCAPS_VISIBLE)) | DDSCAPS_BACKBUFFER);
            b->is_back = true;
            s->back = b;
            if (backs > 1)
                RT_WARN("CreateSurface: %u back buffers requested, using 1", backs);
        }
    } else {
        uint32_t w = (flags & DDSD_WIDTH) ? R32(d + 12) : dd.width;
        uint32_t h = (flags & DDSD_HEIGHT) ? R32(d + 8) : dd.height;
        if (w == 0 || h == 0 || w > 8192 || h > 8192)
            RET(4, DDERR_INVALIDPARAMS);
        s = new_surface(w, h, (caps ? caps : DDSCAPS_OFFSCREENPLAIN) | DDSCAPS_SYSTEMMEMORY);
        if (flags & DDSD_CKSRCBLT) {
            s->has_ck_src = true;
            s->ck_lo = R32(d + 64);
            s->ck_hi = R32(d + 68);
        }
    }
    RT_INFO("IDirectDraw::CreateSurface(flags=%x caps=%x %ux%u) -> %08x%s", flags, caps,
            s->width, s->height, s->obj, s->back ? " with back buffer" : "");
    W32(out, s->obj);
    RET(4, DD_OK);
}

static uint32_t dd_EnumDisplayModes(Cpu *c)
{
    uint32_t ctx = ARG(3), cb = ARG(4);
    static const uint32_t modes[][3] = {{640, 480, 16}, {800, 600, 16}, {1024, 768, 16},
                                        {640, 480, 32}, {800, 600, 32}, {640, 480, 8}};
    uint32_t d = rt_heap_alloc(108);
    for (size_t i = 0; i < sizeof modes / sizeof *modes; i++) {
        memset(g_mem + d, 0, 108);
        W32(d, 108);
        W32(d + 4, DDSD_WIDTH | DDSD_HEIGHT | DDSD_PITCH | DDSD_PIXELFORMAT);
        W32(d + 8, modes[i][1]);
        W32(d + 12, modes[i][0]);
        W32(d + 16, modes[i][0] * modes[i][2] / 8);
        write_pixel_format(d + 72);
        W32(d + 84, modes[i][2]);
        if (modes[i][2] != 16) {
            W32(d + 88, modes[i][2] == 32 ? 0xff0000 : 0);
            W32(d + 92, modes[i][2] == 32 ? 0x00ff00 : 0);
            W32(d + 96, modes[i][2] == 32 ? 0x0000ff : 0);
            if (modes[i][2] == 8)
                W32(d + 76, 0x20);   /* DDPF_PALETTEINDEXED8 */
        }
        if (rt_guest_call(c, cb, 2, d, ctx) == 0)
            break;
    }
    rt_heap_free(d);
    RET(5, DD_OK);
}

static uint32_t dd_GetCaps(Cpu *c)
{
    for (int i = 1; i <= 2; i++) {
        uint32_t caps = ARG(i);
        if (!caps)
            continue;
        uint32_t size = R32(caps);
        memset(g_mem + caps + 4, 0, size > 4 && size < 1024 ? size - 4 : 312);
        W32(caps + 4, 0x40u | 0x200u | 0x400000u | 0x04000000u);   /* BLT, STRETCH, COLORKEY, COLORFILL */
        W32(caps + 12, 0x200u);    /* DDCKEYCAPS_SRCBLT */
        W32(caps + 60, 8u << 20);  /* dwVidMemTotal */
        W32(caps + 64, 6u << 20);  /* dwVidMemFree */
    }
    RET(3, DD_OK);
}

static uint32_t dd_GetDisplayMode(Cpu *c)
{
    uint32_t d = ARG(1);
    memset(g_mem + d, 0, 108);
    W32(d, 108);
    W32(d + 4, DDSD_WIDTH | DDSD_HEIGHT | DDSD_PITCH | DDSD_PIXELFORMAT);
    W32(d + 8, dd.height);
    W32(d + 12, dd.width);
    W32(d + 16, dd.width * 2);
    write_pixel_format(d + 72);
    RET(2, DD_OK);
}

static uint32_t dd_RestoreDisplayMode(Cpu *c) { RET(1, DD_OK); }

static uint32_t dd_SetCooperativeLevel(Cpu *c)
{
    RT_INFO("IDirectDraw::SetCooperativeLevel(hwnd=%08x, flags=%x)", ARG(1), ARG(2));
    dd.coop = ARG(2);
    RET(3, DD_OK);
}

static uint32_t dd_SetDisplayMode(Cpu *c)
{
    uint32_t w = ARG(1), h = ARG(2), bpp = ARG(3);
    RT_INFO("IDirectDraw::SetDisplayMode(%ux%ux%u)", w, h, bpp);
    if (bpp != 16) {
        RT_WARN("SetDisplayMode: %u bpp refused, only 16-bit RGB565 is supported", bpp);
        RET(4, DDERR_UNSUPPORTED);
    }
    dd.width = w;
    dd.height = h;
    /* IDirectDraw2::SetDisplayMode has two more arguments (refresh, flags). */
    uint32_t vt = R32(THIS);
    RET(vt == vt_dd && dd.v2 ? 6 : 4, DD_OK);
}

static uint32_t dd_SetDisplayMode2(Cpu *c)
{
    uint32_t w = ARG(1), h = ARG(2), bpp = ARG(3);
    RT_INFO("IDirectDraw2::SetDisplayMode(%ux%ux%u)", w, h, bpp);
    if (bpp != 16)
        RET(6, DDERR_UNSUPPORTED);
    dd.width = w;
    dd.height = h;
    RET(6, DD_OK);
}

static uint32_t dd_WaitForVerticalBlank(Cpu *c)
{
    /* Real hardware blocks until the next refresh; approximate 60 Hz. */
    static uint32_t last;
    uint32_t now = plat_ticks_ms();
    if (now - last < 16) {
        RT_BLOCKING_BEGIN();
        struct timespec ts = {0, (long)(16 - (now - last)) * 1000000L};
        nanosleep(&ts, NULL);
        RT_BLOCKING_END();
    }
    last = plat_ticks_ms();
    RET(3, DD_OK);
}

static uint32_t dd_FlipToGDISurface(Cpu *c) { RET(1, DD_OK); }

static uint32_t dd_GetAvailableVidMem(Cpu *c)
{
    if (ARG(2)) W32(ARG(2), 8u << 20);
    if (ARG(3)) W32(ARG(3), 6u << 20);
    RET(4, DD_OK);
}

static uint32_t dd_GetMonitorFrequency(Cpu *c) { if (ARG(1)) W32(ARG(1), 60); RET(2, DD_OK); }

static uint32_t dd_GetVerticalBlankStatus(Cpu *c)
{
    if (ARG(1))
        W32(ARG(1), (plat_ticks_ms() % 16) < 2);
    RET(2, DD_OK);
}

static uint32_t dd_GetScanLine(Cpu *c)
{
    if (ARG(1))
        W32(ARG(1), (plat_ticks_ms() % 16) * 525 / 16);
    RET(2, DD_OK);
}

static const ComMethod dd_methods[] = {
    {"QueryInterface", dd_QueryInterface, 3}, {"AddRef", dd_AddRef, 1}, {"Release", dd_Release, 1},
    {"Compact", NULL, 1}, {"CreateClipper", dd_CreateClipper, 4},
    {"CreatePalette", dd_CreatePalette, 5}, {"CreateSurface", dd_CreateSurface, 4},
    {"DuplicateSurface", NULL, 3}, {"EnumDisplayModes", dd_EnumDisplayModes, 5},
    {"EnumSurfaces", NULL, 5}, {"FlipToGDISurface", dd_FlipToGDISurface, 1},
    {"GetCaps", dd_GetCaps, 3}, {"GetDisplayMode", dd_GetDisplayMode, 2},
    {"GetFourCCCodes", NULL, 3}, {"GetGDISurface", NULL, 2},
    {"GetMonitorFrequency", dd_GetMonitorFrequency, 2}, {"GetScanLine", dd_GetScanLine, 2},
    {"GetVerticalBlankStatus", dd_GetVerticalBlankStatus, 2}, {"Initialize", NULL, 2},
    {"RestoreDisplayMode", dd_RestoreDisplayMode, 1},
    {"SetCooperativeLevel", dd_SetCooperativeLevel, 3},
    {"SetDisplayMode", dd_SetDisplayMode, 4},
    {"WaitForVerticalBlank", dd_WaitForVerticalBlank, 3},
    {"GetAvailableVidMem", dd_GetAvailableVidMem, 4},
};

/* ================================================================ IDirectDrawSurface */
#define SURF ddraw_surface(THIS)

static uint32_t s_QueryInterface(Cpu *c)
{
    char g[40];
    guid_str(ARG(1), g);
    RT_INFO("IDirectDrawSurface::QueryInterface(%s)", g);
    Surface *s = SURF;
    if (s)
        s->refs++;
    W32(ARG(2), THIS);
    RET(3, DD_OK);
}

static uint32_t s_AddRef(Cpu *c)
{
    Surface *s = SURF;
    RET(1, s ? ++s->refs : 0);
}

static uint32_t s_Release(Cpu *c)
{
    Surface *s = SURF;
    if (!s)
        RET(1, 0);
    uint32_t r = --s->refs;
    if (r == 0 && !s->is_back) {
        if (s == dd.primary)
            dd.primary = NULL;
        if (s->back) {
            rt_heap_free(s->back->mem);
            com_free(s->back->obj);
            free(s->back);
        }
        rt_heap_free(s->mem);
        com_free(s->obj);
        free(s);
    }
    RET(1, r);
}

static uint32_t s_Blt(Cpu *c)
{
    Surface *dst = SURF;
    uint32_t pdr = ARG(1), psrc = ARG(2), psr = ARG(3), flags = ARG(4), fx = ARG(5);
    if (!dst)
        RET(6, DDERR_INVALIDPARAMS);
    int32_t dr[4], sr[4];
    read_rect(pdr, dst, dr);
    if (flags & DDBLT_COLORFILL) {
        fill(dst, dr, (uint16_t)R32(fx + 80));
        touched(dst);
        RET(6, DD_OK);
    }
    Surface *src = ddraw_surface(psrc);
    if (!src)
        RET(6, DDERR_INVALIDPARAMS);
    read_rect(psr, src, sr);
    bool key = false;
    uint32_t lo = 0, hi = 0;
    if (flags & DDBLT_KEYSRCOVERRIDE) {
        key = true;
        lo = R32(fx + 92);
        hi = R32(fx + 96);
    } else if ((flags & DDBLT_KEYSRC) && src->has_ck_src) {
        key = true;
        lo = src->ck_lo;
        hi = src->ck_hi;
    }
    if (dr[2] - dr[0] == sr[2] - sr[0] && dr[3] - dr[1] == sr[3] - sr[1])
        blit(dst, dr[0], dr[1], src, sr, key, lo, hi, false, 0, 0);
    else
        stretch(dst, dr, src, sr, key, lo, hi);
    touched(dst);
    RET(6, DD_OK);
}

static uint32_t s_BltFast(Cpu *c)
{
    Surface *dst = SURF;
    int32_t x = (int32_t)ARG(1), y = (int32_t)ARG(2);
    Surface *src = ddraw_surface(ARG(3));
    uint32_t psr = ARG(4), flags = ARG(5);
    if (!dst || !src)
        RET(6, DDERR_INVALIDPARAMS);
    int32_t sr[4];
    read_rect(psr, src, sr);
    blit(dst, x, y, src, sr, (flags & DDBLTFAST_SRCCOLORKEY) && src->has_ck_src, src->ck_lo,
         src->ck_hi, (flags & DDBLTFAST_DESTCOLORKEY) && dst->has_ck_dst, dst->ckd_lo, dst->ckd_hi);
    touched(dst);
    RET(6, DD_OK);
}

static uint32_t s_Flip(Cpu *c)
{
    Surface *s = SURF;
    if (!s || !s->back)
        RET(3, DDERR_GENERIC);
    uint32_t t = s->mem;
    s->mem = s->back->mem;
    s->back->mem = t;
    ddraw_present();
    RET(3, DD_OK);
}

static uint32_t s_GetAttachedSurface(Cpu *c)
{
    Surface *s = SURF;
    if (!s || !s->back)
        RET(3, DDERR_NOTFOUND);
    s->back->refs++;
    W32(ARG(2), s->back->obj);
    RET(3, DD_OK);
}

static uint32_t s_GetCaps(Cpu *c)
{
    Surface *s = SURF;
    W32(ARG(1), s ? s->caps : 0);
    RET(2, DD_OK);
}

static uint32_t s_GetColorKey(Cpu *c)
{
    Surface *s = SURF;
    if (!s || !(ARG(1) & DDCKEY_SRCBLT) || !s->has_ck_src)
        RET(3, DDERR_NOTFOUND);
    W32(ARG(2), s->ck_lo);
    W32(ARG(2) + 4, s->ck_hi);
    RET(3, DD_OK);
}

uint32_t gdi_surface_dc(Surface *s);
void gdi_release_surface_dc(Surface *s);

static uint32_t s_GetDC(Cpu *c)
{
    Surface *s = SURF;
    if (!s)
        RET(2, DDERR_INVALIDPARAMS);
    W32(ARG(1), gdi_surface_dc(s));
    RET(2, DD_OK);
}

static uint32_t s_ReleaseDC(Cpu *c)
{
    Surface *s = SURF;
    if (s) {
        gdi_release_surface_dc(s);
        touched(s);
    }
    RET(2, DD_OK);
}

static uint32_t s_GetPixelFormat(Cpu *c)
{
    write_pixel_format(ARG(1));
    RET(2, DD_OK);
}

static uint32_t s_GetSurfaceDesc(Cpu *c)
{
    Surface *s = SURF;
    if (!s)
        RET(2, DDERR_INVALIDPARAMS);
    fill_desc(s, ARG(1));
    RET(2, DD_OK);
}

static uint32_t s_IsLost(Cpu *c) { RET(1, DD_OK); }
static uint32_t s_Restore(Cpu *c) { RET(1, DD_OK); }

static uint32_t s_Lock(Cpu *c)
{
    Surface *s = SURF;
    uint32_t pr = ARG(1), d = ARG(2);
    if (!s)
        RET(5, DDERR_INVALIDPARAMS);
    fill_desc(s, d);
    uint32_t addr = s->mem;
    if (pr)
        addr += R32(pr + 4) * s->pitch + R32(pr) * 2;
    W32(d + 4, R32(d + 4) | DDSD_LPSURFACE);
    W32(d + 36, addr);
    s->locked++;
    RET(5, DD_OK);
}

static uint32_t s_Unlock(Cpu *c)
{
    Surface *s = SURF;
    if (!s)
        RET(2, DDERR_INVALIDPARAMS);
    if (s->locked)
        s->locked--;
    touched(s);
    RET(2, DD_OK);
}

static uint32_t s_SetClipper(Cpu *c) { RET(2, DD_OK); }
static uint32_t s_SetPalette(Cpu *c) { RET(2, DD_OK); }

static uint32_t s_SetColorKey(Cpu *c)
{
    Surface *s = SURF;
    uint32_t flags = ARG(1), ck = ARG(2);
    if (!s)
        RET(3, DDERR_INVALIDPARAMS);
    if (flags & DDCKEY_SRCBLT) {
        s->has_ck_src = ck != 0;
        if (ck) {
            s->ck_lo = R32(ck);
            s->ck_hi = R32(ck + 4) < s->ck_lo ? s->ck_lo : R32(ck + 4);
        }
    }
    if (flags & DDCKEY_DESTBLT) {
        s->has_ck_dst = ck != 0;
        if (ck) {
            s->ckd_lo = R32(ck);
            s->ckd_hi = R32(ck + 4) < s->ckd_lo ? s->ckd_lo : R32(ck + 4);
        }
    }
    RET(3, DD_OK);
}

static uint32_t s_GetBltStatus(Cpu *c) { RET(2, DD_OK); }
static uint32_t s_GetFlipStatus(Cpu *c) { RET(2, DD_OK); }
static uint32_t s_PageLock(Cpu *c) { RET(2, DD_OK); }
static uint32_t s_PageUnlock(Cpu *c) { RET(2, DD_OK); }

static const ComMethod surf_methods[] = {
    {"QueryInterface", s_QueryInterface, 3}, {"AddRef", s_AddRef, 1}, {"Release", s_Release, 1},
    {"AddAttachedSurface", NULL, 2}, {"AddOverlayDirtyRect", NULL, 2}, {"Blt", s_Blt, 6},
    {"BltBatch", NULL, 4}, {"BltFast", s_BltFast, 6}, {"DeleteAttachedSurface", NULL, 3},
    {"EnumAttachedSurfaces", NULL, 3}, {"EnumOverlayZOrders", NULL, 4}, {"Flip", s_Flip, 3},
    {"GetAttachedSurface", s_GetAttachedSurface, 3}, {"GetBltStatus", s_GetBltStatus, 2},
    {"GetCaps", s_GetCaps, 2}, {"GetClipper", NULL, 2}, {"GetColorKey", s_GetColorKey, 3},
    {"GetDC", s_GetDC, 2}, {"GetFlipStatus", s_GetFlipStatus, 2}, {"GetOverlayPosition", NULL, 3},
    {"GetPalette", NULL, 2}, {"GetPixelFormat", s_GetPixelFormat, 2},
    {"GetSurfaceDesc", s_GetSurfaceDesc, 2}, {"Initialize", NULL, 3}, {"IsLost", s_IsLost, 1},
    {"Lock", s_Lock, 5}, {"ReleaseDC", s_ReleaseDC, 2}, {"Restore", s_Restore, 1},
    {"SetClipper", s_SetClipper, 2}, {"SetColorKey", s_SetColorKey, 3},
    {"SetOverlayPosition", NULL, 3}, {"SetPalette", s_SetPalette, 2}, {"Unlock", s_Unlock, 2},
    {"UpdateOverlay", NULL, 6}, {"UpdateOverlayDisplay", NULL, 2},
    {"UpdateOverlayZOrder", NULL, 3}, {"GetDDInterface", NULL, 2},
    {"PageLock", s_PageLock, 2}, {"PageUnlock", s_PageUnlock, 2}, {"SetSurfaceDesc", NULL, 3},
};

/* ================================================================ clipper, palette */
static uint32_t generic_AddRef(Cpu *c) { RET(1, 1); }
static uint32_t generic_Release(Cpu *c) { RET(1, 0); }
static uint32_t clip_SetHWnd(Cpu *c) { RET(3, DD_OK); }

static const ComMethod clip_methods[] = {
    {"QueryInterface", NULL, 3}, {"AddRef", generic_AddRef, 1}, {"Release", generic_Release, 1},
    {"GetClipList", NULL, 4}, {"GetHWnd", NULL, 2}, {"Initialize", NULL, 3},
    {"IsClipListChanged", NULL, 2}, {"SetClipList", NULL, 3}, {"SetHWnd", clip_SetHWnd, 3},
};

static uint32_t pal_SetEntries(Cpu *c) { RET(5, DD_OK); }

static const ComMethod pal_methods[] = {
    {"QueryInterface", NULL, 3}, {"AddRef", generic_AddRef, 1}, {"Release", generic_Release, 1},
    {"GetCaps", NULL, 2}, {"GetEntries", NULL, 5}, {"Initialize", NULL, 4},
    {"SetEntries", pal_SetEntries, 5},
};

/* ================================================================ entry */
WINAPI_FN(ddraw, DirectDrawCreate)
{
    uint32_t out = ARG(1);
    pthread_mutex_lock(&dd_lock);
    if (!vt_dd) {
        vt_dd = com_vtable("IDirectDraw", dd_methods, (int)(sizeof dd_methods / sizeof *dd_methods));
        vt_surf = com_vtable("IDirectDrawSurface", surf_methods,
                             (int)(sizeof surf_methods / sizeof *surf_methods));
        vt_clip = com_vtable("IDirectDrawClipper", clip_methods,
                             (int)(sizeof clip_methods / sizeof *clip_methods));
        vt_pal = com_vtable("IDirectDrawPalette", pal_methods,
                            (int)(sizeof pal_methods / sizeof *pal_methods));
        dd.width = PLAT_W;
        dd.height = PLAT_H;
    }
    pthread_mutex_unlock(&dd_lock);
    static int ddhost;
    if (!dd.obj)
        dd.obj = com_new(vt_dd, &ddhost);
    dd.refs++;
    W32(out, dd.obj);
    RT_INFO("DirectDrawCreate -> %08x", dd.obj);
    RET(3, DD_OK);
}

/* Silence the unused helper when IDirectDraw2 is not requested. */
static void __attribute__((unused)) unused(void) { (void)dd_SetDisplayMode2; }
