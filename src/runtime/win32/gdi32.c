/* GDI32 text output onto DirectDraw surfaces (plus USER32 DrawTextA).
 *
 * The game draws its button labels with GetDC on the back buffer and
 * DrawTextA in a 14 px bold font. Fonts are rasterised with stb_truetype
 * from the metric-compatible Liberation fonts in assets/fonts. Set
 * LULA_TEXT_AA=0 for unsmoothed text like Windows 95 drew it. */
#include "win32.h"
#include "ddraw_internal.h"
#include "../platform.h"

#include <ctype.h>
#include <pthread.h>
#include <strings.h>
#include <unistd.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "../third_party/stb_truetype.h"

#define MAX_DC 16
#define MAX_FONT 32
#define HDC_BASE 0x00050000u
#define HFONT_BASE 0x00060000u
#define HSTOCK_BASE 0x00070000u
#define HBRUSH_BASE 0x00080000u

typedef struct FontFile { const char *file; unsigned char *data; stbtt_fontinfo info; bool ok; } FontFile;
static FontFile font_files[2] = {{"LiberationSans-Regular.ttf"}, {"LiberationSans-Bold.ttf"}};

typedef struct Font {
    bool used;
    int32_t height, width, weight;
    bool italic, underline;
    char face[32];
    FontFile *ff;
    float scale;
    int ascent, descent, line_gap;
} Font;

typedef struct DC {
    bool used;
    Surface *surf;
    uint32_t font, brush;
    uint32_t text_color, bk_color;
    uint32_t bk_mode;          /* 1 TRANSPARENT, 2 OPAQUE */
    uint32_t align;
} DC;

static Font fonts[MAX_FONT];
static DC dcs[MAX_DC];
static pthread_mutex_t gdi_lock = PTHREAD_MUTEX_INITIALIZER;
static int text_aa = -1;

/* ---------------------------------------------------------------- fonts */
static char *font_dir(void)
{
    static char dir[4096];
    if (dir[0])
        return dir;
    const char *env = getenv("LULA_FONT_DIR");
    if (env) {
        snprintf(dir, sizeof dir, "%s", env);
        return dir;
    }
    /* <repo>/assets/fonts next to <repo>/original/app. */
    snprintf(dir, sizeof dir, "%s/../../assets/fonts", rt_vfs_data_dir());
    return dir;
}

static FontFile *load_font_file(bool bold)
{
    FontFile *f = &font_files[bold ? 1 : 0];
    if (f->ok || f->data)
        return f->ok ? f : NULL;
    const char *candidates[] = {font_dir(), "/usr/share/fonts/truetype/liberation",
                                "/usr/share/fonts/liberation", NULL};
    for (int i = 0; candidates[i]; i++) {
        char path[4096];
        snprintf(path, sizeof path, "%s/%s", candidates[i], f->file);
        FILE *fp = fopen(path, "rb");
        if (!fp)
            continue;
        fseek(fp, 0, SEEK_END);
        long n = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        f->data = malloc((size_t)n);
        if (fread(f->data, 1, (size_t)n, fp) == (size_t)n &&
            stbtt_InitFont(&f->info, f->data, stbtt_GetFontOffsetForIndex(f->data, 0)))
            f->ok = true;
        fclose(fp);
        if (f->ok) {
            RT_INFO("font %s", path);
            return f;
        }
    }
    RT_WARN("font %s not found; text will not be drawn (set LULA_FONT_DIR)", f->file);
    f->data = (unsigned char *)"";
    return NULL;
}

static Font *font_of(uint32_t h)
{
    if (h < HFONT_BASE || h >= HFONT_BASE + MAX_FONT)
        return NULL;
    Font *f = &fonts[h - HFONT_BASE];
    return f->used ? f : NULL;
}

static uint32_t create_font(int32_t height, int32_t width, int32_t weight, bool italic,
                            bool underline, const char *face)
{
    pthread_mutex_lock(&gdi_lock);
    int idx = -1;
    for (int i = 1; i < MAX_FONT; i++)
        if (!fonts[i].used) {
            idx = i;
            break;
        }
    if (idx < 0) {
        pthread_mutex_unlock(&gdi_lock);
        return 0;
    }
    Font *f = &fonts[idx];
    memset(f, 0, sizeof *f);
    f->used = true;
    f->height = height;
    f->width = width;
    f->weight = weight;
    f->italic = italic;
    f->underline = underline;
    snprintf(f->face, sizeof f->face, "%s", face ? face : "");
    pthread_mutex_unlock(&gdi_lock);
    f->ff = load_font_file(weight >= 600);
    if (f->ff) {
        /* Negative height = em (character) height, positive = cell height. */
        f->scale = height < 0 ? stbtt_ScaleForMappingEmToPixels(&f->ff->info, (float)-height)
                              : stbtt_ScaleForPixelHeight(&f->ff->info, (float)(height ? height : 16));
        stbtt_GetFontVMetrics(&f->ff->info, &f->ascent, &f->descent, &f->line_gap);
    }
    return HFONT_BASE + (uint32_t)idx;
}

static Font *default_font(void)
{
    static uint32_t h;
    if (!h)
        h = create_font(-12, 0, 700, false, false, "System");
    return font_of(h);
}

/* ---------------------------------------------------------------- DCs */
static DC *dc_of(uint32_t h)
{
    if (h < HDC_BASE || h >= HDC_BASE + MAX_DC)
        return NULL;
    DC *d = &dcs[h - HDC_BASE];
    return d->used ? d : NULL;
}

static uint32_t new_dc(Surface *s)
{
    pthread_mutex_lock(&gdi_lock);
    for (int i = 1; i < MAX_DC; i++) {
        if (!dcs[i].used) {
            dcs[i] = (DC){true, s, 0, 0, 0x000000, 0xffffff, 2, 0};
            pthread_mutex_unlock(&gdi_lock);
            return HDC_BASE + (uint32_t)i;
        }
    }
    pthread_mutex_unlock(&gdi_lock);
    rt_fatal("out of device contexts");
}

uint32_t gdi_surface_dc(Surface *s)
{
    if (s->dc)
        return s->dc;
    s->dc = new_dc(s);
    return s->dc;
}

void gdi_release_surface_dc(Surface *s)
{
    DC *d = dc_of(s->dc);
    if (d)
        d->used = false;
    s->dc = 0;
}

/* BeginPaint on the window: draw onto the primary surface if there is one. */
uint32_t gdi_screen_dc(void)
{
    static uint32_t dc;
    if (!dc)
        dc = new_dc(NULL);
    DC *d = dc_of(dc);
    d->surf = ddraw_state()->primary;
    return dc;
}

/* ---------------------------------------------------------------- drawing */
static uint16_t rgb565(uint32_t colorref)
{
    uint32_t r = colorref & 0xff, g = (colorref >> 8) & 0xff, b = (colorref >> 16) & 0xff;
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static uint16_t blend565(uint16_t dst, uint16_t src, int a)
{
    if (a >= 255)
        return src;
    if (a <= 0)
        return dst;
    int dr = dst >> 11, dg = (dst >> 5) & 63, db = dst & 31;
    int sr = src >> 11, sg = (src >> 5) & 63, sb = src & 31;
    int r = dr + (sr - dr) * a / 255, g = dg + (sg - dg) * a / 255, b = db + (sb - db) * a / 255;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static int text_width(Font *f, const char *s, int n)
{
    if (!f || !f->ff)
        return n * 7;
    float x = 0;
    for (int i = 0; i < n; i++) {
        int adv, lsb;
        int ch = (unsigned char)s[i];
        stbtt_GetCodepointHMetrics(&f->ff->info, ch, &adv, &lsb);
        x += (float)adv * f->scale;
        if (i + 1 < n)
            x += f->scale * (float)stbtt_GetCodepointKernAdvance(&f->ff->info, ch, (unsigned char)s[i + 1]);
    }
    return (int)(x + 0.5f);
}

static int line_height(Font *f)
{
    if (!f || !f->ff)
        return 14;
    return (int)((float)(f->ascent - f->descent) * f->scale + 0.5f);
}

/* Draw a run of text with its top-left cell corner at (x, y). Clip to clip[4]. */
static void draw_text(DC *d, int x, int y, const char *s, int n, const int32_t clip[4])
{
    Surface *surf = d->surf;
    Font *f = font_of(d->font);
    if (!f)
        f = default_font();
    if (!surf || !f || !f->ff)
        return;
    if (text_aa < 0)
        text_aa = getenv("LULA_TEXT_AA") ? atoi(getenv("LULA_TEXT_AA")) : 1;
    uint16_t color = rgb565(d->text_color);
    int cx0 = clip ? clip[0] : 0, cy0 = clip ? clip[1] : 0;
    int cx1 = clip ? clip[2] : (int)surf->width, cy1 = clip ? clip[3] : (int)surf->height;
    if (cx0 < 0) cx0 = 0;
    if (cy0 < 0) cy0 = 0;
    if (cx1 > (int)surf->width) cx1 = (int)surf->width;
    if (cy1 > (int)surf->height) cy1 = (int)surf->height;
    int lh = line_height(f);
    if (d->bk_mode == 2) {
        int w = text_width(f, s, n);
        uint16_t bk = rgb565(d->bk_color);
        for (int yy = y; yy < y + lh; yy++) {
            if (yy < cy0 || yy >= cy1)
                continue;
            uint16_t *row = (uint16_t *)(g_mem + surf->mem + (uint32_t)yy * surf->pitch);
            for (int xx = x; xx < x + w; xx++)
                if (xx >= cx0 && xx < cx1)
                    row[xx] = bk;
        }
    }
    int baseline = y + (int)((float)f->ascent * f->scale + 0.5f);
    float pen = (float)x;
    for (int i = 0; i < n; i++) {
        int ch = (unsigned char)s[i];
        int adv, lsb, x0, y0, x1, y1;
        stbtt_GetCodepointHMetrics(&f->ff->info, ch, &adv, &lsb);
        float sub = pen - (float)(int)pen;
        stbtt_GetCodepointBitmapBoxSubpixel(&f->ff->info, ch, f->scale, f->scale, sub, 0, &x0, &y0, &x1, &y1);
        int gw = x1 - x0, gh = y1 - y0;
        if (gw > 0 && gh > 0 && gw < 256 && gh < 256) {
            static unsigned char bmp[256 * 256];
            stbtt_MakeCodepointBitmapSubpixel(&f->ff->info, bmp, gw, gh, gw, f->scale, f->scale, sub, 0, ch);
            for (int by = 0; by < gh; by++) {
                int py = baseline + y0 + by;
                if (py < cy0 || py >= cy1)
                    continue;
                uint16_t *row = (uint16_t *)(g_mem + surf->mem + (uint32_t)py * surf->pitch);
                for (int bx = 0; bx < gw; bx++) {
                    int px = (int)pen + x0 + bx;
                    if (px < cx0 || px >= cx1)
                        continue;
                    int a = bmp[by * gw + bx];
                    if (!text_aa)
                        a = a >= 128 ? 255 : 0;
                    row[px] = blend565(row[px], color, a);
                }
            }
        }
        pen += (float)adv * f->scale;
        if (i + 1 < n)
            pen += f->scale * (float)stbtt_GetCodepointKernAdvance(&f->ff->info, ch, (unsigned char)s[i + 1]);
    }
    if (f->underline) {
        int uy = baseline + 1;
        if (uy >= cy0 && uy < cy1) {
            uint16_t *row = (uint16_t *)(g_mem + surf->mem + (uint32_t)uy * surf->pitch);
            for (int xx = x; xx < (int)pen; xx++)
                if (xx >= cx0 && xx < cx1)
                    row[xx] = color;
        }
    }
}

/* ---------------------------------------------------------------- GDI32 */
WINAPI_FN(gdi32, CreateFontIndirectA)
{
    uint32_t lf = ARG(0);
    char face[33];
    memcpy(face, g_mem + lf + 28, 32);
    face[32] = 0;
    uint32_t h = create_font((int32_t)R32(lf), (int32_t)R32(lf + 4), (int32_t)R32(lf + 16),
                             R8(lf + 20) != 0, R8(lf + 21) != 0, face);
    RT_INFO("CreateFontIndirectA(\"%s\", height %d, weight %d) -> %08x", face, (int32_t)R32(lf),
            (int32_t)R32(lf + 16), h);
    RET(1, h);
}

WINAPI_FN(gdi32, CreateSolidBrush) { RET(1, HBRUSH_BASE | (ARG(0) & 0xffff)); }

WINAPI_FN(gdi32, DeleteObject)
{
    Font *f = font_of(ARG(0));
    if (f)
        f->used = false;
    RET(1, 1);
}

WINAPI_FN(gdi32, GetStockObject) { RET(1, HSTOCK_BASE + ARG(0)); }

WINAPI_FN(gdi32, SelectObject)
{
    DC *d = dc_of(ARG(0));
    uint32_t obj = ARG(1), prev = 0;
    if (d) {
        if (font_of(obj) || (obj >= HSTOCK_BASE && obj < HSTOCK_BASE + 32 &&
                             (obj - HSTOCK_BASE >= 10 && obj - HSTOCK_BASE <= 17))) {
            prev = d->font;
            d->font = font_of(obj) ? obj : 0;
        } else {
            prev = d->brush;
            d->brush = obj;
        }
    }
    RET(2, prev ? prev : HSTOCK_BASE + 13);
}

WINAPI_FN(gdi32, SetBkColor)
{
    DC *d = dc_of(ARG(0));
    uint32_t prev = d ? d->bk_color : 0;
    if (d)
        d->bk_color = ARG(1);
    RET(2, prev);
}

WINAPI_FN(gdi32, SetBkMode)
{
    DC *d = dc_of(ARG(0));
    uint32_t prev = d ? d->bk_mode : 0;
    if (d)
        d->bk_mode = ARG(1);
    RET(2, prev);
}

WINAPI_FN(gdi32, SetTextAlign)
{
    DC *d = dc_of(ARG(0));
    uint32_t prev = d ? d->align : 0;
    if (d)
        d->align = ARG(1);
    RET(2, prev);
}

WINAPI_FN(gdi32, SetTextColor)
{
    DC *d = dc_of(ARG(0));
    uint32_t prev = d ? d->text_color : 0;
    if (d)
        d->text_color = ARG(1);
    RET(2, prev);
}

/* MM_TEXT mapping: device and logical coordinates are the same. */
WINAPI_FN(gdi32, DPtoLP) { RET(3, 1); }

WINAPI_FN(gdi32, TextOutA)
{
    DC *d = dc_of(ARG(0));
    int32_t x = (int32_t)ARG(1), y = (int32_t)ARG(2);
    const char *s = gstr(ARG(3));
    int n = (int)ARG(4);
    if (d && s) {
        Font *f = font_of(d->font);
        if (!f)
            f = default_font();
        /* TA_CENTER 6, TA_RIGHT 2, TA_BOTTOM 8, TA_BASELINE 24 */
        if ((d->align & 6) == 6)
            x -= text_width(f, s, n) / 2;
        else if (d->align & 2)
            x -= text_width(f, s, n);
        if ((d->align & 24) == 24 && f && f->ff)
            y -= (int)((float)f->ascent * f->scale + 0.5f);
        else if (d->align & 8)
            y -= line_height(f);
        draw_text(d, x, y, s, n, NULL);
    }
    RET(5, 1);
}

/* USER32 DrawTextA: DT_CENTER 1, DT_RIGHT 2, DT_VCENTER 4, DT_BOTTOM 8,
 * DT_WORDBREAK 0x10, DT_SINGLELINE 0x20, DT_NOCLIP 0x100, DT_CALCRECT 0x400. */
WINAPI_FN(user32, DrawTextA)
{
    DC *d = dc_of(ARG(0));
    const char *s = gstr(ARG(1));
    int32_t n = (int32_t)ARG(2);
    uint32_t pr = ARG(3), fmt = ARG(4);
    if (!s || !pr)
        RET(5, 0);
    if (n < 0)
        n = (int32_t)strlen(s);
    Font *f = d ? font_of(d->font) : NULL;
    if (!f)
        f = default_font();
    int32_t r[4] = {(int32_t)R32(pr), (int32_t)R32(pr + 4), (int32_t)R32(pr + 8), (int32_t)R32(pr + 12)};
    RT_TRACE("DrawTextA(\"%.*s\", rect %d,%d,%d,%d, fmt %x, surface %08x)", n, s, r[0], r[1], r[2], r[3],
             fmt, d && d->surf ? d->surf->obj : 0);
    int lh = line_height(f);
    int maxw = r[2] - r[0];
    /* Break into lines. */
    int starts[64], lens[64], nl = 0;
    int i = 0;
    while (i < n && nl < 64) {
        int start = i, last_space = -1, j = i;
        while (j < n && s[j] != '\n' && s[j] != '\r') {
            if (s[j] == ' ')
                last_space = j;
            if ((fmt & 0x10) && !(fmt & 0x20) && text_width(f, s + start, j - start + 1) > maxw &&
                last_space > start) {
                j = last_space;
                break;
            }
            j++;
        }
        starts[nl] = start;
        lens[nl] = j - start;
        nl++;
        if (j < n && s[j] == '\r') j++;
        if (j < n && (s[j] == '\n' || s[j] == ' ')) j++;
        i = j;
        if (fmt & 0x20)
            break;
    }
    int total_h = nl * lh;
    if (fmt & 0x400) {
        int w = 0;
        for (int k = 0; k < nl; k++) {
            int lw = text_width(f, s + starts[k], lens[k]);
            if (lw > w)
                w = lw;
        }
        W32(pr + 8, (uint32_t)(r[0] + w));
        W32(pr + 12, (uint32_t)(r[1] + total_h));
        RET(5, (uint32_t)total_h);
    }
    int y = r[1];
    if ((fmt & 0x20) && (fmt & 4))
        y = r[1] + ((r[3] - r[1]) - lh) / 2;
    else if ((fmt & 0x20) && (fmt & 8))
        y = r[3] - lh;
    const int32_t *clip = (fmt & 0x100) ? NULL : r;
    for (int k = 0; k < nl && d; k++) {
        int lw = text_width(f, s + starts[k], lens[k]);
        int x = r[0];
        if (fmt & 1)
            x = r[0] + (maxw - lw) / 2;
        else if (fmt & 2)
            x = r[2] - lw;
        draw_text(d, x, y + k * lh, s + starts[k], lens[k], clip);
    }
    RET(5, (uint32_t)total_h);
}
