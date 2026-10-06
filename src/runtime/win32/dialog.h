/* Interfaces between user32.c, gdi32.c, res.c and the host-drawn dialog
 * manager, message boxes and popup menus in dialog.c. */
#pragma once
#include "win32.h"
#include "ddraw_internal.h"

/* ---- user32.c: host windows and the message queue ---- */
/* Window procedure implemented by the host (dialogs and their controls). */
typedef uint32_t (*WinHostProc)(Cpu *c, uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp);
uint32_t win_host_create(uint32_t parent, uint32_t id, uint32_t style, uint32_t exstyle,
                         int x, int y, int w, int h, WinHostProc proc);
void win_host_destroy(uint32_t hwnd);
/* GetMessage/PeekMessage core; wait_ms 0 = poll, UINT32_MAX = forever. The
 * character of a WM_KEYDOWN is in m->y when m->x == WIN_KEYCHAR_MARK. */
#define WIN_KEYCHAR_MARK (-12345)
bool win_get_message(Cpu *c, WinMsg *out, uint32_t hwnd, uint32_t lo, uint32_t hi, bool remove,
                     uint32_t wait_ms);
uint32_t win_dispatch(Cpu *c, const WinMsg *m);
uint32_t win_send(Cpu *c, uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp);
void win_post_quit(uint32_t code);
uint32_t win_sys_color(int index);
uint32_t win_now_ms(void);
/* Show the host cursor while a modal host UI runs, even if the game hid it. */
void win_modal_cursor(bool enter);
/* Queue a WM_MOUSEMOVE at the current cursor position (window under the
 * cursor changed). */
void win_post_mouse_update(void);

/* ---- dialog.c hooks used by user32.c ---- */
/* BeginPaint/EndPaint on a dialog window; 0/false if hwnd is not one. */
uint32_t dlg_begin_paint(Cpu *c, uint32_t hwnd, uint32_t ps);
bool dlg_end_paint(Cpu *c, uint32_t hwnd, uint32_t ps);

/* ---- gdi32.c: fonts, DCs and brushes for host-drawn windows ---- */
uint32_t gdi_dialog_font(const char *face, int points, int weight, int *base_x, int *base_y);
int gdi_font_height(uint32_t hfont);
int gdi_font_ascent(uint32_t hfont);
int gdi_text_width(uint32_t hfont, const char *s, int n);
void gdi_text(Surface *surf, uint32_t hfont, int x, int y, const char *s, int n, uint32_t colorref,
              const int32_t clip[4]);
uint32_t gdi_create_dc(Surface *surf, int org_x, int org_y, const int32_t clip[4]);
void gdi_delete_dc(uint32_t hdc);
void gdi_dc_set_colors(uint32_t hdc, uint32_t text, uint32_t bk);
void gdi_dc_colors(uint32_t hdc, uint32_t *text, uint32_t *bk, uint32_t *mode);
uint32_t gdi_solid_brush(uint32_t colorref);
bool gdi_brush_color(uint32_t hbrush, uint32_t *colorref);

/* ---- res.c ---- */
uint32_t res_find(uint32_t type, uint32_t name, uint32_t *size);
uint32_t res_string(uint32_t id, char *out, uint32_t cap);
