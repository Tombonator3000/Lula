/* USER32 dialog manager, message boxes and popup menus.
 *
 * Everything is drawn by the host straight onto the primary DirectDraw
 * surface, over the image the game showed last: the game calls
 * FlipToGDISurface and stops flipping while a dialog is up. The pixels under
 * a window are saved and put back when it closes, like CS_SAVEBITS.
 *
 * Dialogs come from the RT_DIALOG templates of WET.EXE and get real window
 * handles (host windows in user32.c), so GetDlgItem, SendMessageA and
 * SetDlgItemTextA work on them. The guest DLGPROC sees the Windows 95
 * protocol: WM_SETFONT, WM_INITDIALOG, WM_PAINT with BeginPaint ->
 * WM_ERASEBKGND -> WM_CTLCOLORDLG, WM_CTLCOLOR* whenever a control paints
 * (including after every list box selection change) and WM_COMMAND for
 * buttons, Enter (DM_GETDEFID, normally IDOK) and Esc (IDCANCEL). The modal
 * loop keeps dispatching WM_TIMER and the other messages of the game window
 * and routes mouse and keyboard input to the topmost dialog, menu or box.
 * Reference: docs/recomp/specs/user32-gdi32.md sections 11-13, Appendix B.
 *
 * LULA_MSGBOX_AUTO=1 answers message boxes with their default button
 * without showing them (unattended runs). */
#include "win32.h"
#include "dialog.h"
#include "../platform.h"

#include <ctype.h>
#include <strings.h>

/* ---------------------------------------------------------------- constants */
#define WM_DESTROY 0x0002
#define WM_SETFOCUS 0x0007
#define WM_KILLFOCUS 0x0008
#define WM_SETREDRAW 0x000b
#define WM_SETTEXT 0x000c
#define WM_GETTEXT 0x000d
#define WM_GETTEXTLENGTH 0x000e
#define WM_PAINT 0x000f
#define WM_CLOSE 0x0010
#define WM_QUIT 0x0012
#define WM_ERASEBKGND 0x0014
#define WM_SHOWWINDOW 0x0018
#define WM_NEXTDLGCTL 0x0028
#define WM_SETFONT 0x0030
#define WM_GETFONT 0x0031
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_SYSKEYDOWN 0x0104
#define WM_INITDIALOG 0x0110
#define WM_COMMAND 0x0111
#define WM_TIMER 0x0113
#define WM_INITMENUPOPUP 0x0117
#define WM_MENUSELECT 0x011f
#define WM_ENTERIDLE 0x0121
#define WM_CTLCOLORMSGBOX 0x0132
#define WM_CTLCOLOREDIT 0x0133
#define WM_CTLCOLORLISTBOX 0x0134
#define WM_CTLCOLORBTN 0x0135
#define WM_CTLCOLORDLG 0x0136
#define WM_CTLCOLORSCROLLBAR 0x0137
#define WM_CTLCOLORSTATIC 0x0138
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_LBUTTONDBLCLK 0x0203
#define WM_RBUTTONDOWN 0x0204
#define WM_RBUTTONDBLCLK 0x0206
#define WM_MBUTTONDOWN 0x0207
#define WM_ENTERMENULOOP 0x0211
#define WM_EXITMENULOOP 0x0212
#define DM_GETDEFID 0x0400
#define DM_SETDEFID 0x0401
#define DC_HASDEFID 0x534bu

#define EM_GETSEL 0x00b0
#define EM_SETSEL 0x00b1
#define EM_REPLACESEL 0x00c2
#define EM_LIMITTEXT 0x00c5
#define BM_GETCHECK 0x00f0
#define BM_SETCHECK 0x00f1
#define BM_SETSTYLE 0x00f4
#define BM_CLICK 0x00f5
#define LB_ADDSTRING 0x0180
#define LB_INSERTSTRING 0x0181
#define LB_DELETESTRING 0x0182
#define LB_RESETCONTENT 0x0184
#define LB_SETCURSEL 0x0186
#define LB_GETSEL 0x0187
#define LB_GETCURSEL 0x0188
#define LB_GETTEXT 0x0189
#define LB_GETTEXTLEN 0x018a
#define LB_GETCOUNT 0x018b
#define LB_GETTOPINDEX 0x018e
#define LB_FINDSTRING 0x018f
#define LB_SETTOPINDEX 0x0197
#define LB_GETITEMHEIGHT 0x01a1
#define LB_FINDSTRINGEXACT 0x01a2
#define LB_ERR 0xffffffffu

#define EN_SETFOCUS 0x0100u
#define EN_KILLFOCUS 0x0200u
#define EN_CHANGE 0x0300u
#define EN_UPDATE 0x0400u
#define LBN_SELCHANGE 1u

#define IDOK 1u
#define IDCANCEL 2u
#define IDABORT 3u
#define IDRETRY 4u
#define IDIGNORE 5u
#define IDYES 6u
#define IDNO 7u

#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_RETURN 0x0d
#define VK_SHIFT 0x10
#define VK_ESCAPE 0x1b
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_DELETE 0x2e

#define WS_POPUP 0x80000000u
#define WS_CHILD 0x40000000u
#define WS_VISIBLE 0x10000000u
#define WS_DISABLED 0x08000000u
#define WS_CAPTION 0x00c00000u
#define WS_BORDER 0x00800000u
#define WS_DLGFRAME 0x00400000u
#define WS_VSCROLL 0x00200000u
#define WS_SYSMENU 0x00080000u
#define WS_THICKFRAME 0x00040000u
#define WS_TABSTOP 0x00010000u
#define WS_EX_DLGMODALFRAME 0x00000001u
#define WS_EX_CLIENTEDGE 0x00000200u
#define WS_EX_STATICEDGE 0x00020000u
#define DS_SETFONT 0x40u
#define DS_MODALFRAME 0x80u
#define DS_CENTER 0x800u

#define BS_TYPEMASK 0xfu
#define BS_PUSHBUTTON 0u
#define BS_DEFPUSHBUTTON 1u
#define BS_CHECKBOX 2u
#define BS_AUTOCHECKBOX 3u
#define BS_RADIOBUTTON 4u
#define BS_3STATE 5u
#define BS_AUTO3STATE 6u
#define BS_GROUPBOX 7u
#define BS_AUTORADIOBUTTON 9u
#define SS_TYPEMASK 0x1fu
#define SS_CENTER 1u
#define SS_RIGHT 2u
#define SS_ICON 3u
#define SS_SIMPLE 0xbu
#define SS_LEFTNOWORDWRAP 0xcu
#define SS_NOPREFIX 0x80u
#define ES_PASSWORD 0x20u
#define ES_NUMBER 0x2000u
#define LBS_NOTIFY 0x1u
#define LBS_USETABSTOPS 0x80u
#define LBS_NOINTEGRALHEIGHT 0x100u
#define LBS_DISABLENOSCROLL 0x1000u

#define MF_GRAYED 0x1u
#define MF_DISABLED 0x2u
#define MF_BITMAP 0x4u
#define MF_CHECKED 0x8u
#define MF_POPUP 0x10u
#define MF_OWNERDRAW 0x100u
#define MF_SEPARATOR 0x800u
#define TPM_NONOTIFY 0x80u
#define TPM_RETURNCMD 0x100u

#define MK_LBUTTON 1u

/* COLOR_* indices (Windows 95 defaults in user32.c) */
#define C_ACTIVECAPTION 2
#define C_MENU 4
#define C_WINDOW 5
#define C_MENUTEXT 7
#define C_WINDOWTEXT 8
#define C_CAPTIONTEXT 9
#define C_HIGHLIGHT 13
#define C_HIGHLIGHTTEXT 14
#define C_3DFACE 15
#define C_3DSHADOW 16
#define C_GRAYTEXT 17
#define C_BTNTEXT 18
#define C_3DHILIGHT 20
#define C_3DDKSHADOW 21
#define C_3DLIGHT 22

#define CAPTION_H 18      /* SM_CYCAPTION 19 = 18 px bar + 1 px line */
#define SCROLL_W 16       /* SM_CXVSCROLL */
#define CARET_BLINK_MS 530
#define REPEAT_DELAY_MS 350
#define REPEAT_MS 50

static uint32_t sc(int index) { return win_sys_color(index); }
static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

/* MulDiv for the non-negative values used here (rounds halves up). */
static int muldiv(int a, int b, int c)
{
    long long p = (long long)a * b;
    return (int)(p >= 0 ? (p + c / 2) / c : -((-p + c / 2) / c));
}

/* ---------------------------------------------------------------- types */
enum { CK_STATIC, CK_BUTTON, CK_EDIT, CK_LISTBOX, CK_ICON, CK_OTHER };
enum { DRAG_NONE, DRAG_ITEMS, DRAG_THUMB, DRAG_UP, DRAG_DOWN, DRAG_PAGEUP, DRAG_PAGEDOWN };
enum { UI_DIALOG, UI_MENU };

typedef struct Ui Ui;

typedef struct Ctl {
    Ui *ui;
    uint32_t hwnd, id, style, exstyle;
    int kind;
    int x, y, w, h;            /* window rect in dialog client coordinates */
    char *text;
    uint32_t font;
    bool dirty;
    /* buttons */
    bool down, space;
    uint32_t check;
    /* edit */
    int caret, anchor, scroll, limit;
    /* list box */
    char **items;
    int count, cap, cur, top, item_h;
    int drag, drag_off;
    uint32_t repeat_at;
} Ctl;

typedef struct MenuItem { uint32_t flags, id; char *text; int y, h; } MenuItem;
typedef struct Menu { bool used; uint32_t handle; MenuItem *items; int count, cap; } Menu;

struct Ui {
    int kind;
    int wx, wy, ww, wh;        /* window rect on the screen */
    int cx, cy, cw, ch;        /* client rect on the screen */
    bool visible, ended, full_dirty;
    uint32_t paint_seq;
    /* pixels under the window and the pixels last shown there */
    uint32_t under_mem, under_seq;
    int ux, uy, uw, uh;
    uint16_t *under, *shown;
    bool shown_valid;
    /* dialog */
    uint32_t hwnd, owner, proc, style, exstyle, result;
    uint32_t font, font_bold;
    int bx, by, font_h;
    char title[128];
    Ctl *ctl;
    int nctl, focus, capture;
    uint32_t def_id;
    bool no_redraw, invalid, erase, msgbox;
    int icon;
    /* popup menu */
    Menu *menu;
    int hot, text_x;
    bool armed, picked;
    uint32_t chosen;
};

#define MAX_UI 16
static Ui *uis[MAX_UI];
static int nui;
static uint32_t ui_mem;            /* primary surface memory the windows were drawn on */
static uint32_t paint_counter;
static uint32_t last_present;
static int painting;
static bool shift_down;

/* ---------------------------------------------------------------- canvas */
static Surface *cv;
static int clip_x0, clip_y0, clip_x1, clip_y1;

static Surface *screen(void)
{
    Surface *s = ddraw_state()->primary;
    return s && s->mem ? s : NULL;
}

static uint16_t to565(uint32_t c)
{
    uint32_t r = c & 0xff, g = (c >> 8) & 0xff, b = (c >> 16) & 0xff;
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static uint16_t *row_of(Surface *s, int y) { return (uint16_t *)(g_mem + s->mem + (uint32_t)y * s->pitch); }

static void set_clip(int x0, int y0, int x1, int y1)
{
    clip_x0 = imax(x0, 0);
    clip_y0 = imax(y0, 0);
    clip_x1 = cv ? imin(x1, (int)cv->width) : x1;
    clip_y1 = cv ? imin(y1, (int)cv->height) : y1;
}

static void fill(int x0, int y0, int x1, int y1, uint32_t color)
{
    if (!cv)
        return;
    x0 = imax(x0, clip_x0);
    y0 = imax(y0, clip_y0);
    x1 = imin(x1, clip_x1);
    y1 = imin(y1, clip_y1);
    if (x0 >= x1 || y0 >= y1)
        return;
    uint16_t p = to565(color);
    for (int y = y0; y < y1; y++) {
        uint16_t *r = row_of(cv, y);
        for (int x = x0; x < x1; x++)
            r[x] = p;
    }
}

static void pixel(int x, int y, uint32_t color) { fill(x, y, x + 1, y + 1, color); }

/* One-pixel 3D frame: top and left in tl, bottom and right in br. */
static void frame(int x0, int y0, int x1, int y1, uint32_t tl, uint32_t br)
{
    fill(x0, y0, x1 - 1, y0 + 1, tl);
    fill(x0, y0, x0 + 1, y1 - 1, tl);
    fill(x0, y1 - 1, x1, y1, br);
    fill(x1 - 1, y0, x1, y1 - 1, br);
}

static void edge_raised(int x0, int y0, int x1, int y1)
{
    frame(x0, y0, x1, y1, sc(C_3DLIGHT), sc(C_3DDKSHADOW));
    frame(x0 + 1, y0 + 1, x1 - 1, y1 - 1, sc(C_3DHILIGHT), sc(C_3DSHADOW));
}

static void edge_sunken(int x0, int y0, int x1, int y1)
{
    frame(x0, y0, x1, y1, sc(C_3DSHADOW), sc(C_3DHILIGHT));
    frame(x0 + 1, y0 + 1, x1 - 1, y1 - 1, sc(C_3DDKSHADOW), sc(C_3DLIGHT));
}

static void xor_pixel(int x, int y)
{
    if (cv && x >= clip_x0 && x < clip_x1 && y >= clip_y0 && y < clip_y1)
        row_of(cv, y)[x] ^= 0xffff;
}

/* Dotted focus rectangle, inverted like DrawFocusRect. */
static void focus_rect(int x0, int y0, int x1, int y1)
{
    for (int x = x0; x < x1; x += 2) {
        xor_pixel(x, y0);
        if (y1 - 1 > y0)
            xor_pixel(x, y1 - 1);
    }
    for (int y = y0 + 2; y < y1 - 1; y += 2) {
        xor_pixel(x0, y);
        if (x1 - 1 > x0)
            xor_pixel(x1 - 1, y);
    }
}

static void text_raw(uint32_t font, int x, int y, const char *s, int n, uint32_t color)
{
    if (!cv || n <= 0)
        return;
    int32_t c4[4] = {clip_x0, clip_y0, clip_x1, clip_y1};
    gdi_text(cv, font, x, y, s, n, color, c4);
}

/* Strip '&' prefixes ("&&" is a literal '&'); *ul gets the index of the
 * underlined character or -1. */
static int strip_prefix(const char *s, int n, char *out, int cap, int *ul)
{
    int o = 0;
    *ul = -1;
    for (int i = 0; i < n && o < cap - 1; i++) {
        if (s[i] == '&' && i + 1 < n) {
            i++;
            if (s[i] != '&' && *ul < 0)
                *ul = o;
        }
        out[o++] = s[i];
    }
    out[o] = 0;
    return o;
}

/* Width of a line, with '&' prefixes and tab stops (tabw > 0) applied. */
static int measure(uint32_t font, const char *s, int n, bool prefix, int tabw)
{
    char buf[1024];
    int ul;
    if (prefix) {
        n = strip_prefix(s, n, buf, sizeof buf, &ul);
        s = buf;
    }
    if (tabw <= 0)
        return gdi_text_width(font, s, n);
    int x = 0, start = 0;
    for (int i = 0; i <= n; i++) {
        if (i == n || s[i] == '\t') {
            x += gdi_text_width(font, s + start, i - start);
            if (i < n)
                x = (x / tabw + 1) * tabw;
            start = i + 1;
        }
    }
    return x;
}

static void draw_line(uint32_t font, int x, int y, const char *s, int n, uint32_t color, bool prefix,
                      int tabw)
{
    char buf[1024];
    int ul = -1;
    if (prefix) {
        n = strip_prefix(s, n, buf, sizeof buf, &ul);
        s = buf;
    }
    if (tabw > 0 && memchr(s, '\t', (size_t)n)) {
        int px = 0, start = 0;
        for (int i = 0; i <= n; i++) {
            if (i == n || s[i] == '\t') {
                text_raw(font, x + px, y, s + start, i - start, color);
                px += gdi_text_width(font, s + start, i - start);
                if (i < n)
                    px = (px / tabw + 1) * tabw;
                start = i + 1;
            }
        }
    } else {
        text_raw(font, x, y, s, n, color);
    }
    if (ul >= 0 && ul < n) {
        int ux = x + gdi_text_width(font, s, ul), uw = gdi_text_width(font, s + ul, 1);
        int uy = y + gdi_font_ascent(font) + 1;
        fill(ux, uy, ux + uw, uy + 1, color);
    }
}

typedef struct Line { int start, len; } Line;

/* Split text into lines at '\n' and, with maxw > 0, at spaces so that each
 * line fits (DT_WORDBREAK; a single long word is not broken). */
static int wrap_text(uint32_t font, const char *s, int maxw, bool prefix, int tabw, Line *lines, int cap)
{
    int n = (int)strlen(s), nl = 0, i = 0;
    while (i <= n && nl < cap) {
        int end = i;
        while (end < n && s[end] != '\n')
            end++;
        int len = end - i;
        if (len > 0 && s[end - 1] == '\r')
            len--;
        int start = i;
        while (maxw > 0 && measure(font, s + start, len - (start - i), prefix, tabw) > maxw && nl < cap) {
            int brk = -1;
            for (int j = start + 1; j < i + len; j++)
                if (s[j] == ' ' && measure(font, s + start, j - start, prefix, tabw) <= maxw)
                    brk = j;
            if (brk < 0)
                break;
            lines[nl++] = (Line){start, brk - start};
            start = brk;
            while (start < i + len && s[start] == ' ')
                start++;
        }
        if (nl < cap)
            lines[nl++] = (Line){start, i + len - start};
        if (end >= n)
            break;
        i = end + 1;
    }
    return nl;
}

static void present(void)
{
    ddraw_present();
    last_present = win_now_ms();
}

/* ---------------------------------------------------------------- lookup */
static Ui *ui_top(void) { return nui ? uis[nui - 1] : NULL; }

static int ui_index(Ui *u)
{
    for (int i = 0; i < nui; i++)
        if (uis[i] == u)
            return i;
    return -1;
}

static Ui *ui_of(uint32_t hwnd)
{
    for (int i = 0; i < nui; i++)
        if (uis[i]->kind == UI_DIALOG && uis[i]->hwnd == hwnd && hwnd)
            return uis[i];
    return NULL;
}

static Ctl *ctl_of(uint32_t hwnd)
{
    for (int i = 0; i < nui; i++)
        for (int j = 0; j < uis[i]->nctl; j++)
            if (uis[i]->ctl[j].hwnd == hwnd && hwnd)
                return &uis[i]->ctl[j];
    return NULL;
}

static Ctl *ctl_by_id(Ui *u, uint32_t id)
{
    for (int i = 0; u && i < u->nctl; i++)
        if (u->ctl[i].id == id)
            return &u->ctl[i];
    return NULL;
}

static int ctl_index(Ctl *k) { return (int)(k - k->ui->ctl); }

static bool ctl_enabled(const Ctl *k) { return (k->style & (WS_VISIBLE | WS_DISABLED)) == WS_VISIBLE; }

static bool is_push(const Ctl *k)
{
    uint32_t t = k->style & BS_TYPEMASK;
    return k->kind == CK_BUTTON && (t == BS_PUSHBUTTON || t == BS_DEFPUSHBUTTON);
}

/* Can the control take the focus? */
static bool focusable(const Ctl *k)
{
    if (!ctl_enabled(k))
        return false;
    if (k->kind == CK_BUTTON)
        return (k->style & BS_TYPEMASK) != BS_GROUPBOX;
    return k->kind == CK_EDIT || k->kind == CK_LISTBOX;
}

static bool ui_dirty(Ui *u)
{
    if (u->full_dirty)
        return true;
    for (int i = 0; i < u->nctl; i++)
        if (u->ctl[i].dirty)
            return true;
    return false;
}

/* Border of a control (the dialog manager turned WS_BORDER into a client edge). */
static int ctl_edge(const Ctl *k)
{
    return ((k->exstyle & WS_EX_CLIENTEDGE) ? 2 : 0) + ((k->exstyle & WS_EX_STATICEDGE) ? 1 : 0) +
           ((k->style & WS_BORDER) ? 1 : 0);
}

static void draw_ctl_edge(const Ctl *k, int x0, int y0, int x1, int y1)
{
    if (k->style & WS_BORDER) {
        frame(x0, y0, x1, y1, 0, 0);
        x0++, y0++, x1--, y1--;
    }
    if (k->exstyle & WS_EX_STATICEDGE) {
        frame(x0, y0, x1, y1, sc(C_3DSHADOW), sc(C_3DHILIGHT));
        x0++, y0++, x1--, y1--;
    }
    if (k->exstyle & WS_EX_CLIENTEDGE)
        edge_sunken(x0, y0, x1, y1);
}

/* ---------------------------------------------------------------- caret */
static struct {
    bool on, phase;
    int x, y, h;
    uint32_t mem, next;
} caret;

static void caret_xor(void)
{
    Surface *s = screen();
    if (!s || s->mem != caret.mem || caret.x < 0 || caret.x >= (int)s->width)
        return;
    for (int y = imax(caret.y, 0); y < imin(caret.y + caret.h, (int)s->height); y++)
        row_of(s, y)[caret.x] ^= 0xffff;
}

static void caret_hide(void)
{
    if (caret.on)
        caret_xor();
    caret.on = false;
}

static void caret_reset(void)
{
    caret.phase = true;
    caret.next = win_now_ms() + CARET_BLINK_MS;
}

static Ctl *focused_edit(void)
{
    Ui *u = ui_top();
    if (!u || u->kind != UI_DIALOG || !u->visible || u->ended || u->focus < 0)
        return NULL;
    Ctl *k = &u->ctl[u->focus];
    return k->kind == CK_EDIT ? k : NULL;
}

static void edit_text_origin(Ctl *k, int *x, int *y, int *x1)
{
    Ui *u = k->ui;
    int e = ctl_edge(k);
    int ih = k->h - 2 * e;
    *x = u->cx + k->x + e + 1 - k->scroll;
    *y = u->cy + k->y + e + imax(0, (ih - u->font_h) / 2);
    *x1 = u->cx + k->x + k->w - e - 1;
}

static void caret_show(void)
{
    caret_hide();
    Ctl *k = focused_edit();
    Surface *s = screen();
    if (!k || !s || !caret.phase)
        return;
    int x, y, x1;
    edit_text_origin(k, &x, &y, &x1);
    int cx = x + gdi_text_width(k->font, k->text, k->caret);
    int e = ctl_edge(k);
    int left = k->ui->cx + k->x + e;
    if (cx < left || cx >= x1 + 1)
        return;
    caret.x = cx;
    caret.y = y;
    caret.h = imin(k->ui->font_h, k->ui->cy + k->y + k->h - e - y);
    caret.mem = s->mem;
    caret_xor();
    caret.on = true;
}

/* ---------------------------------------------------------------- saved pixels */
static void region_copy(Surface *s, Ui *u, uint16_t *dst)
{
    for (int y = 0; y < u->uh; y++)
        memcpy(dst + y * u->uw, row_of(s, u->uy + y) + u->ux, (size_t)u->uw * 2);
}

static bool region_equals(Surface *s, Ui *u, const uint16_t *src)
{
    for (int y = 0; y < u->uh; y++)
        if (memcmp(src + y * u->uw, row_of(s, u->uy + y) + u->ux, (size_t)u->uw * 2))
            return false;
    return true;
}

static void region_put(Surface *s, Ui *u, const uint16_t *src)
{
    for (int y = 0; y < u->uh; y++)
        memcpy(row_of(s, u->uy + y) + u->ux, src + y * u->uw, (size_t)u->uw * 2);
}

static void under_free(Ui *u)
{
    free(u->under);
    free(u->shown);
    u->under = u->shown = NULL;
    u->shown_valid = false;
}

static void under_capture(Ui *u, Surface *s)
{
    under_free(u);
    u->ux = imax(u->wx, 0);
    u->uy = imax(u->wy, 0);
    u->uw = imax(0, imin(u->wx + u->ww, (int)s->width) - u->ux);
    u->uh = imax(0, imin(u->wy + u->wh, (int)s->height) - u->uy);
    size_t n = (size_t)u->uw * (size_t)u->uh;
    u->under = malloc(n * 2 + 2);
    u->shown = malloc(n * 2 + 2);
    region_copy(s, u, u->under);
    u->under_mem = s->mem;
    u->under_seq = paint_counter;
}

static bool rects_meet(const Ui *a, const Ui *b)
{
    return a->wx < b->wx + b->ww && b->wx < a->wx + a->ww && a->wy < b->wy + b->wh && b->wy < a->wy + a->wh;
}

/* Take a window off the screen: put back the saved pixels if nothing else
 * drew there in the meantime, otherwise let the windows below repaint. */
static void ui_unshow(Ui *u)
{
    if (!u->visible)
        return;
    u->visible = false;
    caret_hide();
    Surface *s = screen();
    bool restored = false;
    if (u->under && s && s->mem == u->under_mem && u->shown_valid && region_equals(s, u, u->shown)) {
        region_put(s, u, u->under);
        restored = true;
    }
    int at = ui_index(u);
    for (int i = 0; i < nui; i++) {
        Ui *o = uis[i];
        if (o == u || !o->visible || !rects_meet(o, u))
            continue;
        if (i > at) {
            o->full_dirty = true;       /* above: its saved pixels are stale now */
            under_free(o);
        } else if (!restored || o->paint_seq > u->under_seq) {
            o->full_dirty = true;
        }
    }
    under_free(u);
    if (restored) {
        for (int i = 0; i < nui; i++) {
            Ui *o = uis[i];
            if (o != u && o->visible && o->shown && o->under_mem == s->mem)
                region_copy(s, o, o->shown);
        }
        present();
    }
}

/* Something else drew over a waiting window (the game reacting to
 * WM_ACTIVATEAPP, say): paint it again, as Windows repaints on reactivation. */
static void check_overdraw(void)
{
    Surface *s = screen();
    if (!s || painting)
        return;
    bool had_caret = caret.on;
    caret_hide();
    for (int i = 0; i < nui; i++) {
        Ui *u = uis[i];
        if (u->visible && !u->ended && u->shown_valid && u->under_mem == s->mem && !region_equals(s, u, u->shown))
            u->full_dirty = true;
    }
    if (had_caret)
        caret_show();
}

/* ---------------------------------------------------------------- notifications */
static void notify(Cpu *c, Ctl *k, uint32_t code)
{
    win_send(c, k->ui->hwnd, WM_COMMAND, (code << 16) | (k->id & 0xffff), k->hwnd);
}

static void set_focus(Cpu *c, Ui *u, int idx, bool keyboard)
{
    if (u->focus == idx)
        return;
    int old = u->focus;
    u->focus = idx;
    bool push_changed = false;
    if (old >= 0) {
        Ctl *k = &u->ctl[old];
        k->dirty = true;
        k->down = k->space = false;
        push_changed |= is_push(k);
        if (k->kind == CK_EDIT) {
            k->anchor = k->caret;
            notify(c, k, EN_KILLFOCUS);
        }
    }
    if (idx >= 0) {
        Ctl *k = &u->ctl[idx];
        k->dirty = true;
        push_changed |= is_push(k);
        if (k->kind == CK_EDIT) {
            if (keyboard) {             /* the dialog manager selects all (EM_SETSEL 0,-1) */
                k->anchor = 0;
                k->caret = (int)strlen(k->text);
            }
            caret_reset();
            notify(c, k, EN_SETFOCUS);
        }
    }
    if (push_changed)
        for (int i = 0; i < u->nctl; i++)
            if (is_push(&u->ctl[i]))
                u->ctl[i].dirty = true;
}

/* The push button drawn with the default frame. */
static bool is_default_button(Ui *u, Ctl *k)
{
    if (!is_push(k))
        return false;
    if (u->focus >= 0 && is_push(&u->ctl[u->focus]))
        return &u->ctl[u->focus] == k;
    return (k->style & BS_TYPEMASK) == BS_DEFPUSHBUTTON;
}

static void button_click(Cpu *c, Ui *u, Ctl *k)
{
    uint32_t t = k->style & BS_TYPEMASK;
    if (t == BS_AUTOCHECKBOX)
        k->check = !k->check;
    else if (t == BS_AUTO3STATE)
        k->check = (k->check + 1) % 3;
    else if (t == BS_AUTORADIOBUTTON)
        k->check = 1;
    k->dirty = true;
    (void)u;
    notify(c, k, 0);   /* BN_CLICKED */
}

/* ---------------------------------------------------------------- text of controls */
static void set_text(Ctl *k, const char *s)
{
    if (k->text && strcmp(k->text, s) == 0)
        return;
    free(k->text);
    k->text = strdup(s);
    k->dirty = true;
    if (k->kind == CK_EDIT) {
        k->caret = k->anchor = 0;
        k->scroll = 0;
    }
}

/* ---------------------------------------------------------------- list boxes */
typedef struct SB { int x, y, w, h, arrow, track_y, track_h, thumb_y, thumb_h; } SB;

static void lb_inner(Ctl *k, int *x0, int *y0, int *x1, int *y1)
{
    int e = ctl_edge(k);
    *x0 = k->ui->cx + k->x + e;
    *y0 = k->ui->cy + k->y + e;
    *x1 = k->ui->cx + k->x + k->w - e;
    *y1 = k->ui->cy + k->y + k->h - e;
}

static int lb_visible(Ctl *k)
{
    int x0, y0, x1, y1;
    lb_inner(k, &x0, &y0, &x1, &y1);
    return imax(1, (y1 - y0) / k->item_h);
}

static bool lb_scrollbar(Ctl *k, SB *sb)
{
    if (!(k->style & WS_VSCROLL))
        return false;
    int vis = lb_visible(k);
    if (k->count <= vis && !(k->style & LBS_DISABLENOSCROLL))
        return false;
    int x0, y0, x1, y1;
    lb_inner(k, &x0, &y0, &x1, &y1);
    sb->w = SCROLL_W;
    sb->x = x1 - SCROLL_W;
    sb->y = y0;
    sb->h = y1 - y0;
    sb->arrow = imin(SCROLL_W, sb->h / 2);
    sb->track_y = sb->y + sb->arrow;
    sb->track_h = sb->h - 2 * sb->arrow;
    int range = k->count - vis;
    sb->thumb_h = 0;
    sb->thumb_y = sb->track_y;
    if (range > 0 && sb->track_h >= 8) {
        sb->thumb_h = imax(8, sb->track_h * vis / k->count);
        sb->thumb_y = sb->track_y + (sb->track_h - sb->thumb_h) * k->top / range;
    }
    return true;
}

static void lb_set_top(Ctl *k, int top)
{
    int maxtop = imax(0, k->count - lb_visible(k));
    top = imax(0, imin(top, maxtop));
    if (top != k->top) {
        k->top = top;
        k->dirty = true;
    }
}

static void lb_ensure_visible(Ctl *k, int i)
{
    int vis = lb_visible(k);
    if (i < 0)
        return;
    if (i < k->top)
        lb_set_top(k, i);
    else if (i >= k->top + vis)
        lb_set_top(k, i - vis + 1);
}

static void lb_select(Cpu *c, Ctl *k, int i, bool user)
{
    if (k->count == 0)
        return;
    i = imax(0, imin(i, k->count - 1));
    lb_ensure_visible(k, i);
    if (i == k->cur)
        return;
    k->cur = i;
    k->dirty = true;
    if (user && (k->style & LBS_NOTIFY))
        notify(c, k, LBN_SELCHANGE);
}

static void lb_insert(Ctl *k, int at, const char *s)
{
    if (k->count == k->cap) {
        k->cap = k->cap ? k->cap * 2 : 16;
        k->items = realloc(k->items, (size_t)k->cap * sizeof *k->items);
    }
    if (at < 0 || at > k->count)
        at = k->count;
    memmove(k->items + at + 1, k->items + at, (size_t)(k->count - at) * sizeof *k->items);
    k->items[at] = strdup(s);
    k->count++;
    if (k->cur >= at)
        k->cur++;
    k->dirty = true;
}

static uint32_t lb_message(Cpu *c, Ctl *k, uint32_t msg, uint32_t wp, uint32_t lp)
{
    int i = (int)wp;
    (void)c;
    switch (msg) {
    case LB_ADDSTRING:
        lb_insert(k, -1, lp ? gstr(lp) : "");
        return (uint32_t)(k->count - 1);
    case LB_INSERTSTRING:
        if (i < -1 || i > k->count)
            return LB_ERR;
        lb_insert(k, i, lp ? gstr(lp) : "");
        return (uint32_t)(i < 0 ? k->count - 1 : i);
    case LB_DELETESTRING:
        if (i < 0 || i >= k->count)
            return LB_ERR;
        free(k->items[i]);
        memmove(k->items + i, k->items + i + 1, (size_t)(k->count - i - 1) * sizeof *k->items);
        k->count--;
        if (k->cur == i)
            k->cur = -1;
        else if (k->cur > i)
            k->cur--;
        lb_set_top(k, k->top);
        k->dirty = true;
        return (uint32_t)k->count;
    case LB_RESETCONTENT:
        for (int j = 0; j < k->count; j++)
            free(k->items[j]);
        k->count = 0;
        k->cur = -1;
        k->top = 0;
        k->dirty = true;
        return 0;
    case LB_SETCURSEL:
        if (i == -1) {
            if (k->cur != -1)
                k->dirty = true;
            k->cur = -1;
            return LB_ERR;
        }
        if (i < 0 || i >= k->count)
            return LB_ERR;
        if (k->cur != i)
            k->dirty = true;
        k->cur = i;
        lb_ensure_visible(k, i);
        return (uint32_t)i;
    case LB_GETCURSEL:
        return k->cur < 0 ? LB_ERR : (uint32_t)k->cur;
    case LB_GETSEL:
        if (i < 0 || i >= k->count)
            return LB_ERR;
        return i == k->cur ? 1 : 0;
    case LB_GETTEXT:
        if (i < 0 || i >= k->count)
            return LB_ERR;
        if (lp)
            strcpy((char *)g_mem + lp, k->items[i]);
        return (uint32_t)strlen(k->items[i]);
    case LB_GETTEXTLEN:
        if (i < 0 || i >= k->count)
            return LB_ERR;
        return (uint32_t)strlen(k->items[i]);
    case LB_GETCOUNT:
        return (uint32_t)k->count;
    case LB_GETTOPINDEX:
        return (uint32_t)k->top;
    case LB_SETTOPINDEX:
        if (i < 0 || i >= imax(k->count, 1))
            return LB_ERR;
        lb_set_top(k, i);
        return 0;
    case LB_GETITEMHEIGHT:
        return (uint32_t)k->item_h;
    case LB_FINDSTRING:
    case LB_FINDSTRINGEXACT: {
        const char *s = lp ? gstr(lp) : "";
        size_t n = strlen(s);
        for (int j = 0; j < k->count; j++) {
            int at = (i + 1 + j) % k->count;
            if (i < -1 || i >= k->count)
                at = j;
            if (msg == LB_FINDSTRING ? strncasecmp(k->items[at], s, n) == 0 : strcasecmp(k->items[at], s) == 0)
                return (uint32_t)at;
        }
        return LB_ERR;
    }
    default:
        return 0;
    }
}

/* ---------------------------------------------------------------- painting controls */
static uint32_t ctl_colors(Cpu *c, Ctl *k, uint32_t msg, uint32_t *text, uint32_t *bk, uint32_t *mode)
{
    Ui *u = k->ui;
    int save[4] = {clip_x0, clip_y0, clip_x1, clip_y1};
    int32_t cl[4] = {clip_x0, clip_y0, clip_x1, clip_y1};
    uint32_t hdc = gdi_create_dc(cv, u->cx + k->x, u->cy + k->y, cl);
    uint32_t br = win_send(c, u->hwnd, msg, hdc, k->hwnd);
    gdi_dc_colors(hdc, text, bk, mode);
    gdi_delete_dc(hdc);
    clip_x0 = save[0], clip_y0 = save[1], clip_x1 = save[2], clip_y1 = save[3];
    uint32_t col;
    if (!gdi_brush_color(br, &col))
        col = sc(msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX ? C_WINDOW : C_3DFACE);
    return col;
}

static void paint_static(Cpu *c, Ctl *k, int X, int Y)
{
    Ui *u = k->ui;
    uint32_t type = k->style & SS_TYPEMASK;
    if (type == SS_ICON || type > SS_LEFTNOWORDWRAP)
        return;
    if (type >= 4 && type <= 9) {   /* SS_BLACKRECT .. SS_WHITEFRAME */
        static const int col[3] = {C_WINDOWTEXT, C_3DSHADOW, C_WINDOW};
        uint32_t color = sc(col[(type - 4) % 3]);
        if (type <= 6)
            fill(X, Y, X + k->w, Y + k->h, color);
        else
            frame(X, Y, X + k->w, Y + k->h, color, color);
        return;
    }
    uint32_t text, bk, mode;
    uint32_t br = ctl_colors(c, k, WM_CTLCOLORSTATIC, &text, &bk, &mode);
    int e = ctl_edge(k);
    draw_ctl_edge(k, X, Y, X + k->w, Y + k->h);
    set_clip(imax(clip_x0, X + e), imax(clip_y0, Y + e), imin(clip_x1, X + k->w - e),
             imin(clip_y1, Y + k->h - e));
    fill(X + e, Y + e, X + k->w - e, Y + k->h - e, br);
    if (k->style & WS_DISABLED)
        text = sc(C_GRAYTEXT);
    bool prefix = !(k->style & SS_NOPREFIX);
    int tabw = 8 * u->bx;
    int maxw = k->w - 2 * e;
    Line lines[32];
    int nl = wrap_text(k->font, k->text, type == SS_SIMPLE || type == SS_LEFTNOWORDWRAP ? 0 : maxw, prefix,
                       tabw, lines, 32);
    for (int i = 0; i < nl; i++) {
        const char *s = k->text + lines[i].start;
        int w = measure(k->font, s, lines[i].len, prefix, tabw);
        int lx = X + e;
        if (type == SS_CENTER)
            lx = X + e + (maxw - w) / 2;
        else if (type == SS_RIGHT)
            lx = X + k->w - e - w;
        int ly = Y + e + i * u->font_h;
        if (mode == 2)
            fill(lx, ly, lx + w, ly + u->font_h, bk);
        draw_line(k->font, lx, ly, s, lines[i].len, text, prefix, tabw);
    }
}

static void paint_check_mark(int x, int y, uint32_t color)
{
    static const char *mark[7] = {"......X", ".....XX", "X...XXX", "XX.XXX.", "XXXXX..", ".XXX...",
                                  "..X...."};
    for (int j = 0; j < 7; j++)
        for (int i = 0; i < 7; i++)
            if (mark[j][i] == 'X')
                pixel(x + i, y + j, color);
}

static void paint_button(Cpu *c, Ctl *k, int X, int Y)
{
    Ui *u = k->ui;
    uint32_t type = k->style & BS_TYPEMASK;
    uint32_t text, bk, mode;
    int W = k->w, H = k->h;
    bool enabled = ctl_enabled(k), focused = u->focus == ctl_index(k);
    uint32_t tcol = enabled ? sc(C_BTNTEXT) : sc(C_GRAYTEXT);
    int tw = measure(k->font, k->text, (int)strlen(k->text), true, 0);
    if (type == BS_GROUPBOX) {
        uint32_t br = ctl_colors(c, k, WM_CTLCOLORSTATIC, &text, &bk, &mode);
        int fy = Y + u->font_h / 2 - 1;
        frame(X, fy, X + W, Y + H, sc(C_3DSHADOW), sc(C_3DHILIGHT));
        frame(X + 1, fy + 1, X + W - 1, Y + H - 1, sc(C_3DHILIGHT), sc(C_3DSHADOW));
        if (k->text[0]) {
            fill(X + 6, Y, X + 6 + tw + 4, Y + u->font_h, br);
            draw_line(k->font, X + 8, Y, k->text, (int)strlen(k->text), enabled ? text : tcol, true, 0);
        }
        return;
    }
    if (!is_push(k)) {   /* check boxes and radio buttons */
        uint32_t br = ctl_colors(c, k, WM_CTLCOLORSTATIC, &text, &bk, &mode);
        fill(X, Y, X + W, Y + H, br);
        int by = Y + (H - 13) / 2;
        edge_sunken(X, by, X + 13, by + 13);
        fill(X + 2, by + 2, X + 11, by + 11, k->down ? sc(C_3DFACE) : sc(C_WINDOW));
        if (k->check)
            paint_check_mark(X + 3, by + 3, sc(C_WINDOWTEXT));
        int tx = X + 17, ty = Y + (H - u->font_h) / 2;
        draw_line(k->font, tx, ty, k->text, (int)strlen(k->text), enabled ? text : tcol, true, 0);
        if (focused)
            focus_rect(tx - 1, ty, tx + tw + 1, ty + u->font_h);
        return;
    }
    ctl_colors(c, k, WM_CTLCOLORBTN, &text, &bk, &mode);
    int x0 = X, y0 = Y, x1 = X + W, y1 = Y + H;
    bool pushed = k->down;
    if (is_default_button(u, k)) {
        frame(x0, y0, x1, y1, 0, 0);
        x0++, y0++, x1--, y1--;
    }
    if (pushed) {
        frame(x0, y0, x1, y1, sc(C_3DSHADOW), sc(C_3DSHADOW));
        fill(x0 + 1, y0 + 1, x1 - 1, y1 - 1, sc(C_3DFACE));
    } else {
        frame(x0, y0, x1, y1, sc(C_3DHILIGHT), sc(C_3DDKSHADOW));
        frame(x0 + 1, y0 + 1, x1 - 1, y1 - 1, sc(C_3DLIGHT), sc(C_3DSHADOW));
        fill(x0 + 2, y0 + 2, x1 - 2, y1 - 2, sc(C_3DFACE));
    }
    int tx = X + (W - tw) / 2 + (pushed ? 1 : 0), ty = Y + (H - u->font_h) / 2 + (pushed ? 1 : 0);
    if (!enabled)
        draw_line(k->font, tx + 1, ty + 1, k->text, (int)strlen(k->text), sc(C_3DHILIGHT), true, 0);
    draw_line(k->font, tx, ty, k->text, (int)strlen(k->text), enabled ? sc(C_BTNTEXT) : sc(C_3DSHADOW), true, 0);
    if (focused)
        focus_rect(X + 4, Y + 4, X + W - 4, Y + H - 4);
}

static void paint_edit(Cpu *c, Ctl *k, int X, int Y)
{
    Ui *u = k->ui;
    uint32_t text, bk, mode;
    uint32_t br = ctl_colors(c, k, WM_CTLCOLOREDIT, &text, &bk, &mode);
    int e = ctl_edge(k);
    draw_ctl_edge(k, X, Y, X + k->w, Y + k->h);
    set_clip(imax(clip_x0, X + e), imax(clip_y0, Y + e), imin(clip_x1, X + k->w - e),
             imin(clip_y1, Y + k->h - e));
    fill(X + e, Y + e, X + k->w - e, Y + k->h - e, br);
    if (k->style & WS_DISABLED)
        text = sc(C_GRAYTEXT);
    int n = (int)strlen(k->text);
    char *disp = k->text, stars[512];
    if (k->style & ES_PASSWORD) {
        n = imin(n, (int)sizeof stars - 1);
        memset(stars, '*', (size_t)n);
        stars[n] = 0;
        disp = stars;
    }
    int x, y, x1;
    edit_text_origin(k, &x, &y, &x1);
    int s0 = imin(k->anchor, k->caret), s1 = imax(k->anchor, k->caret);
    bool sel = u->focus == ctl_index(k) && s0 != s1;
    if (!sel) {
        text_raw(k->font, x, y, disp, n, text);
        return;
    }
    int xa = x + gdi_text_width(k->font, disp, s0), xb = x + gdi_text_width(k->font, disp, s1);
    text_raw(k->font, x, y, disp, s0, text);
    fill(xa, y, xb, y + u->font_h, sc(C_HIGHLIGHT));
    text_raw(k->font, xa, y, disp + s0, s1 - s0, sc(C_HIGHLIGHTTEXT));
    text_raw(k->font, xb, y, disp + s1, n - s1, text);
}

static void paint_arrow_button(int x0, int y0, int x1, int y1, bool up, bool pressed)
{
    if (pressed) {
        frame(x0, y0, x1, y1, sc(C_3DSHADOW), sc(C_3DSHADOW));
        fill(x0 + 1, y0 + 1, x1 - 1, y1 - 1, sc(C_3DFACE));
    } else {
        edge_raised(x0, y0, x1, y1);
        fill(x0 + 2, y0 + 2, x1 - 2, y1 - 2, sc(C_3DFACE));
    }
    int cx = (x0 + x1) / 2 - 1 + (pressed ? 1 : 0), cy = (y0 + y1) / 2 - 2 + (pressed ? 1 : 0);
    for (int i = 0; i < 4; i++) {
        int yy = up ? cy + i : cy + 3 - i;
        fill(cx - i, yy, cx + i + 1, yy + 1, sc(C_BTNTEXT));
    }
}

static void paint_listbox(Cpu *c, Ctl *k, int X, int Y)
{
    Ui *u = k->ui;
    uint32_t text, bk, mode;
    uint32_t br = ctl_colors(c, k, WM_CTLCOLORLISTBOX, &text, &bk, &mode);
    draw_ctl_edge(k, X, Y, X + k->w, Y + k->h);
    int x0, y0, x1, y1;
    lb_inner(k, &x0, &y0, &x1, &y1);
    SB sb;
    bool has_sb = lb_scrollbar(k, &sb);
    int ix1 = has_sb ? sb.x : x1;
    int save[4] = {clip_x0, clip_y0, clip_x1, clip_y1};
    set_clip(imax(clip_x0, x0), imax(clip_y0, y0), imin(clip_x1, ix1), imin(clip_y1, y1));
    fill(x0, y0, ix1, y1, br);
    if (k->style & WS_DISABLED)
        text = sc(C_GRAYTEXT);
    int tabw = (k->style & LBS_USETABSTOPS) ? 8 * u->bx : 0;
    bool focused = u->focus == ctl_index(k);
    for (int i = k->top; i < k->count; i++) {
        int y = y0 + (i - k->top) * k->item_h;
        if (y >= y1)
            break;
        bool selected = i == k->cur;
        /* ExtTextOut(ETO_OPAQUE) fills the item with the DC background colour. */
        fill(x0, y, ix1, y + k->item_h, selected ? sc(C_HIGHLIGHT) : bk);
        draw_line(k->font, x0 + 2, y, k->items[i], (int)strlen(k->items[i]),
                  selected ? sc(C_HIGHLIGHTTEXT) : text, false, tabw);
        if (selected && focused)
            focus_rect(x0, y, ix1, y + k->item_h);
    }
    if (focused && (k->count == 0 || k->cur < 0))
        focus_rect(x0, y0, ix1, y0 + k->item_h);
    clip_x0 = save[0], clip_y0 = save[1], clip_x1 = save[2], clip_y1 = save[3];
    if (!has_sb)
        return;
    paint_arrow_button(sb.x, sb.y, sb.x + sb.w, sb.y + sb.arrow, true, k->drag == DRAG_UP);
    paint_arrow_button(sb.x, sb.y + sb.h - sb.arrow, sb.x + sb.w, sb.y + sb.h, false, k->drag == DRAG_DOWN);
    /* Track: the Windows 95 checker of COLOR_3DHILIGHT and COLOR_3DFACE. */
    uint16_t a = to565(sc(C_3DHILIGHT)), b = to565(sc(C_3DFACE));
    for (int y = imax(sb.track_y, clip_y0); y < imin(sb.track_y + sb.track_h, clip_y1); y++) {
        uint16_t *r = row_of(cv, y);
        for (int x = imax(sb.x, clip_x0); x < imin(sb.x + sb.w, clip_x1); x++)
            r[x] = ((x + y) & 1) ? a : b;
    }
    if (sb.thumb_h) {
        edge_raised(sb.x, sb.thumb_y, sb.x + sb.w, sb.thumb_y + sb.thumb_h);
        fill(sb.x + 2, sb.thumb_y + 2, sb.x + sb.w - 2, sb.thumb_y + sb.thumb_h - 2, sc(C_3DFACE));
    }
}

/* Message box icons, drawn instead of loading the system icons. */
static void paint_icon(Ctl *k, int X, int Y)
{
    Ui *u = k->ui;
    int kind = u->icon;
    uint32_t fill_col = kind == 1 ? 0x0000ff : kind == 3 ? 0x00ffff : 0xffffff;
    uint32_t mark_col = kind == 1 ? 0xffffff : kind == 3 ? 0x000000 : 0xff0000;
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            bool in, edge;
            if (kind == 3) {   /* exclamation: triangle */
                int half = (y * 15) / 30;
                in = y >= 1 && y <= 30 && abs(x - 15) <= half;
                edge = in && (y == 30 || abs(x - 15) >= half - 1);
            } else {
                int dx = 2 * x - 31, dy = 2 * y - 31, d = dx * dx + dy * dy;
                in = d <= 30 * 30;
                edge = in && d >= 27 * 27;
            }
            if (in)
                pixel(X + x, Y + y, edge ? 0 : fill_col);
        }
    }
    if (kind == 1) {   /* hand: a white X */
        for (int i = 0; i < 12; i++) {
            fill(X + 10 + i, Y + 10 + i, X + 12 + i, Y + 11 + i, mark_col);
            fill(X + 20 - i, Y + 10 + i, X + 22 - i, Y + 11 + i, mark_col);
        }
        return;
    }
    const char *glyph = kind == 2 ? "?" : kind == 3 ? "!" : "i";
    int w = gdi_text_width(u->font_bold, glyph, 1);
    for (int dx = 0; dx < 2; dx++)
        text_raw(u->font_bold, X + 16 - w / 2 + dx - 1, Y + 9 + (kind == 3 ? 4 : 0), glyph, 1, mark_col);
}

static void paint_ctl(Cpu *c, Ctl *k)
{
    Ui *u = k->ui;
    if (!(k->style & WS_VISIBLE))
        return;
    int X = u->cx + k->x, Y = u->cy + k->y;
    set_clip(imax(X, u->cx), imax(Y, u->cy), imin(X + k->w, u->cx + u->cw), imin(Y + k->h, u->cy + u->ch));
    if (clip_x0 >= clip_x1 || clip_y0 >= clip_y1)
        return;
    switch (k->kind) {
    case CK_STATIC: paint_static(c, k, X, Y); break;
    case CK_BUTTON: paint_button(c, k, X, Y); break;
    case CK_EDIT: paint_edit(c, k, X, Y); break;
    case CK_LISTBOX: paint_listbox(c, k, X, Y); break;
    case CK_ICON: paint_icon(k, X, Y); break;
    default: break;
    }
}

/* ---------------------------------------------------------------- painting windows */
static int frame_width(uint32_t style, uint32_t exstyle)
{
    if (style & WS_THICKFRAME)
        return 4;
    if ((exstyle & WS_EX_DLGMODALFRAME) || (style & WS_DLGFRAME))
        return 3;
    return (style & WS_BORDER) ? 1 : 0;
}

static int client_edge(uint32_t exstyle)
{
    return ((exstyle & WS_EX_CLIENTEDGE) ? 2 : 0) + ((exstyle & WS_EX_STATICEDGE) ? 1 : 0);
}

static int caption_height(uint32_t style) { return (style & WS_CAPTION) == WS_CAPTION ? CAPTION_H + 1 : 0; }

static void paint_nonclient(Ui *u)
{
    set_clip(u->wx, u->wy, u->wx + u->ww, u->wy + u->wh);
    int x0 = u->wx, y0 = u->wy, x1 = u->wx + u->ww, y1 = u->wy + u->wh;
    int f = frame_width(u->style, u->exstyle);
    if (f >= 3) {
        edge_raised(x0, y0, x1, y1);
        for (int i = 2; i < f; i++)
            frame(x0 + i, y0 + i, x1 - i, y1 - i, sc(C_3DFACE), sc(C_3DFACE));
    } else if (f == 1) {
        frame(x0, y0, x1, y1, 0, 0);
    }
    x0 += f, y0 += f, x1 -= f, y1 -= f;
    if (caption_height(u->style)) {
        fill(x0, y0, x1, y0 + CAPTION_H, sc(C_ACTIVECAPTION));
        fill(x0, y0 + CAPTION_H, x1, y0 + CAPTION_H + 1, sc(C_3DFACE));
        int save = clip_x1;
        clip_x1 = imin(clip_x1, x1 - 2);
        text_raw(u->font_bold, x0 + 3, y0 + (CAPTION_H - u->font_h) / 2, u->title, (int)strlen(u->title),
                 sc(C_CAPTIONTEXT));
        clip_x1 = save;
    }
    int ce = client_edge(u->exstyle);
    int ex0 = u->cx - ce, ey0 = u->cy - ce, ex1 = u->cx + u->cw + ce, ey1 = u->cy + u->ch + ce;
    if (u->exstyle & WS_EX_CLIENTEDGE) {
        edge_sunken(ex0, ey0, ex1, ey1);
        ex0 += 2, ey0 += 2, ex1 -= 2, ey1 -= 2;
    }
    if (u->exstyle & WS_EX_STATICEDGE)
        frame(ex0, ey0, ex1, ey1, sc(C_3DSHADOW), sc(C_3DHILIGHT));
}

/* WM_ERASEBKGND default: fill the client area with the WM_CTLCOLORDLG brush. */
static void erase_background(Cpu *c, Ui *u, uint32_t hdc)
{
    uint32_t br = win_send(c, u->hwnd, WM_CTLCOLORDLG, hdc, u->hwnd);
    uint32_t col;
    if (!gdi_brush_color(br, &col))
        col = sc(C_3DFACE);
    Surface *s = screen();
    if (!s || !u->visible)
        return;
    cv = s;
    set_clip(u->cx, u->cy, u->cx + u->cw, u->cy + u->ch);
    fill(u->cx, u->cy, u->cx + u->cw, u->cy + u->ch, col);
}

static void paint_menu(Ui *u);

static void paint_ui(Cpu *c, Ui *u)
{
    if (!u->under || u->under_mem != cv->mem)
        under_capture(u, cv);
    if (u->kind == UI_MENU) {
        paint_menu(u);
        u->full_dirty = false;
        u->paint_seq = ++paint_counter;
        return;
    }
    if (u->no_redraw) {
        u->full_dirty = false;
        for (int i = 0; i < u->nctl; i++)
            u->ctl[i].dirty = false;
        return;
    }
    if (u->full_dirty) {
        u->full_dirty = false;
        paint_nonclient(u);
        u->invalid = u->erase = true;
        win_send(c, u->hwnd, WM_PAINT, 0, 0);
        for (int i = 0; i < u->nctl; i++)
            u->ctl[i].dirty = true;
    }
    for (int i = 0; i < u->nctl && !u->ended; i++) {
        Ctl *k = &u->ctl[i];
        if (k->dirty) {
            k->dirty = false;
            cv = screen();
            if (!cv)
                return;
            paint_ctl(c, k);
        }
    }
    u->paint_seq = ++paint_counter;
}

/* Paint whatever changed, bottom window first, and show the result. */
static void paint_pending(Cpu *c)
{
    Surface *s = screen();
    if (!s || !nui || painting)
        return;
    painting++;
    cv = s;
    if (s->mem != ui_mem) {
        /* The game flipped or this is the first paint: draw everything anew. */
        ui_mem = s->mem;
        caret.on = false;
        for (int i = 0; i < nui; i++) {
            uis[i]->full_dirty = true;
            under_free(uis[i]);
        }
    }
    bool drew = false;
    for (int pass = 0; pass < 8; pass++) {
        int lo = -1;
        for (int i = 0; i < nui && lo < 0; i++)
            if (uis[i]->visible && !uis[i]->ended && ui_dirty(uis[i]))
                lo = i;
        if (lo < 0)
            break;
        if (!drew)
            caret_hide();
        drew = true;
        Ui *low = uis[lo];
        for (int i = lo; i < nui; i++) {
            Ui *u = uis[i];
            if (!u->visible || u->ended)
                continue;
            if (u != low && rects_meet(u, low))
                u->full_dirty = true;
            if (!ui_dirty(u))
                continue;
            cv = screen();
            if (!cv)
                break;
            paint_ui(c, u);
        }
    }
    cv = screen();
    if (drew && cv) {
        for (int i = 0; i < nui; i++) {
            Ui *u = uis[i];
            if (u->visible && u->under && u->under_mem == cv->mem) {
                region_copy(cv, u, u->shown);
                u->shown_valid = true;
            }
        }
        caret_show();
        present();
    }
    painting--;
}

/* ---------------------------------------------------------------- dialog window procedures */
static bool direct_result(uint32_t msg)
{
    return (msg >= WM_CTLCOLORMSGBOX && msg <= WM_CTLCOLORSTATIC) || msg == WM_INITDIALOG ||
           msg == 0x0039 || msg == 0x002e || msg == 0x002f || msg == 0x0037;
}

static void do_paint(Cpu *c, Ui *u)
{
    if (!u->invalid)
        return;
    u->invalid = false;
    if (u->erase) {
        u->erase = false;
        Surface *s = screen();
        int32_t cl[4] = {u->cx, u->cy, u->cx + u->cw, u->cy + u->ch};
        uint32_t hdc = gdi_create_dc(s, u->cx, u->cy, cl);
        win_send(c, u->hwnd, WM_ERASEBKGND, hdc, 0);
        gdi_delete_dc(hdc);
    }
}

static void next_tab(Cpu *c, Ui *u, bool back);

/* DefDlgProc. */
static uint32_t def_dlg_proc(Cpu *c, Ui *u, uint32_t msg, uint32_t wp, uint32_t lp)
{
    switch (msg) {
    case WM_ERASEBKGND:
        erase_background(c, u, wp);
        return 1;
    case WM_PAINT:
        do_paint(c, u);
        return 0;
    case WM_CTLCOLORMSGBOX: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG: case WM_CTLCOLORSCROLLBAR: case WM_CTLCOLORSTATIC: {
        /* DefWindowProc: system colours, "COLOR_xxx + 1" brush. */
        int bg = msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX ? C_WINDOW : C_3DFACE;
        gdi_dc_set_colors(wp, sc(C_WINDOWTEXT), sc(bg));
        return (uint32_t)bg + 1;
    }
    case DM_GETDEFID:
        return u->ended ? 0 : (DC_HASDEFID << 16) | (u->def_id & 0xffff);
    case DM_SETDEFID:
        u->def_id = wp;
        for (int i = 0; i < u->nctl; i++)
            if (is_push(&u->ctl[i]))
                u->ctl[i].dirty = true;
        return 1;
    case WM_SETREDRAW:
        u->no_redraw = !wp;
        if (wp)
            u->full_dirty = true;
        return 0;
    case WM_GETFONT:
        return u->font;
    case WM_SETTEXT:
        snprintf(u->title, sizeof u->title, "%s", lp ? gstr(lp) : "");
        u->full_dirty = true;
        return 1;
    case WM_GETTEXT:
        return put_gstr(lp, wp, u->title);
    case WM_GETTEXTLENGTH:
        return (uint32_t)strlen(u->title);
    case WM_CLOSE: {
        Ctl *k = ctl_by_id(u, IDCANCEL);
        win_send(c, u->hwnd, WM_COMMAND, IDCANCEL, k ? k->hwnd : 0);
        return 0;
    }
    case WM_NEXTDLGCTL:
        if (lp & 0xffff) {
            Ctl *k = ctl_of(wp);
            if (k && k->ui == u)
                set_focus(c, u, ctl_index(k), true);
        } else {
            next_tab(c, u, wp != 0);
        }
        return 0;
    case WM_COMMAND:
        if (u->msgbox && (lp || (wp & 0xffff) == IDCANCEL || (wp & 0xffff) == IDOK)) {
            uint32_t id = wp & 0xffff;
            if (id == IDCANCEL && !ctl_by_id(u, IDCANCEL))
                return 0;
            u->result = id;
            u->ended = true;
            ui_unshow(u);
        }
        return 0;
    default:
        return 0;
    }
}

static uint32_t dialog_wndproc(Cpu *c, uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp)
{
    Ui *u = ui_of(hwnd);
    if (!u)
        return 0;
    if (u->proc) {
        uint32_t r = rt_guest_call(c, u->proc, 4, hwnd, msg, wp, lp);
        if (direct_result(msg)) {
            if (r || msg == WM_INITDIALOG)
                return r;
        } else if (r) {
            return 0;   /* DWL_MSGRESULT, never set by the game */
        }
        u = ui_of(hwnd);
        if (!u)
            return 0;
    }
    return def_dlg_proc(c, u, msg, wp, lp);
}

static uint32_t control_wndproc(Cpu *c, uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp)
{
    Ctl *k = ctl_of(hwnd);
    if (!k)
        return 0;
    switch (msg) {
    case WM_SETTEXT:
        if (k->kind == CK_LISTBOX)
            return 1;
        set_text(k, lp ? gstr(lp) : "");
        if (k->kind == CK_EDIT) {
            notify(c, k, EN_UPDATE);
            notify(c, k, EN_CHANGE);
        }
        return 1;
    case WM_GETTEXT:
        return put_gstr(lp, wp, k->text);
    case WM_GETTEXTLENGTH:
        return (uint32_t)strlen(k->text);
    case WM_SETFONT:
        k->font = wp;
        if (lp)
            k->dirty = true;
        return 0;
    case WM_GETFONT:
        return k->font;
    case EM_GETSEL: {
        uint32_t s0 = (uint32_t)imin(k->anchor, k->caret), s1 = (uint32_t)imax(k->anchor, k->caret);
        if (wp)
            W32(wp, s0);
        if (lp)
            W32(lp, s1);
        return (s1 << 16) | (s0 & 0xffff);
    }
    case EM_SETSEL: {
        if (k->kind != CK_EDIT)
            return 0;
        int n = (int)strlen(k->text), a = (int)wp, b = (int)lp;
        if (a < 0) {
            k->anchor = k->caret;
        } else {
            if (b < 0 || b > n)
                b = n;
            k->anchor = imin(a, n);
            k->caret = b;
        }
        k->dirty = true;
        return 1;
    }
    case EM_LIMITTEXT:
        k->limit = wp ? (int)wp : 0x7ffe;
        return 0;
    case BM_GETCHECK:
        return k->check;
    case BM_SETCHECK:
        k->check = wp;
        k->dirty = true;
        return 0;
    case BM_SETSTYLE:
        k->style = (k->style & 0xffff0000u) | (wp & 0xffff);
        if (lp)
            k->dirty = true;
        return 0;
    case BM_CLICK:
        if (k->kind == CK_BUTTON && ctl_enabled(k))
            button_click(c, k->ui, k);
        return 0;
    default:
        if (k->kind == CK_LISTBOX && msg >= LB_ADDSTRING && msg <= LB_FINDSTRINGEXACT)
            return lb_message(c, k, msg, wp, lp);
        return 0;
    }
}

/* ---------------------------------------------------------------- templates */
static uint16_t rd16(uint32_t *p)
{
    uint16_t v = R16(*p);
    *p += 2;
    return v;
}

static uint32_t rd32(uint32_t *p)
{
    uint32_t v = R32(*p);
    *p += 4;
    return v;
}

static void rd_wstr(uint32_t *p, char *out, int cap)
{
    int n = 0;
    for (;;) {
        uint16_t ch = rd16(p);
        if (!ch)
            break;
        if (out && n < cap - 1)
            out[n++] = (char)(ch < 256 ? ch : '?');   /* Latin-1, like LoadStringA */
    }
    if (out)
        out[n] = 0;
}

/* sz_Or_Ord: the ordinal, or -1 with the string in out. */
static int rd_sz_or_ord(uint32_t *p, char *out, int cap)
{
    if (R16(*p) == 0xffff) {
        *p += 2;
        if (out)
            out[0] = 0;
        return rd16(p);
    }
    rd_wstr(p, out, cap);
    return -1;
}

static int class_kind(int ord, const char *name)
{
    if (ord >= 0)
        return ord == 0x80 ? CK_BUTTON : ord == 0x81 ? CK_EDIT : ord == 0x82 ? CK_STATIC
             : ord == 0x83 ? CK_LISTBOX : CK_OTHER;
    if (strcasecmp(name, "BUTTON") == 0) return CK_BUTTON;
    if (strcasecmp(name, "EDIT") == 0) return CK_EDIT;
    if (strcasecmp(name, "STATIC") == 0) return CK_STATIC;
    if (strcasecmp(name, "LISTBOX") == 0) return CK_LISTBOX;
    return CK_OTHER;
}

/* Place the window: client rect from the template (dialog units relative to
 * the owner client at 0,0, or centred with DS_CENTER), kept on the screen. */
static void place_window(Ui *u, int x, int y, int w, int h)
{
    int f = frame_width(u->style, u->exstyle), ce = client_edge(u->exstyle), cap = caption_height(u->style);
    u->cw = w;
    u->ch = h;
    u->ww = w + 2 * (f + ce);
    u->wh = h + 2 * (f + ce) + cap;
    u->wx = x - f - ce;
    u->wy = y - f - ce - cap;
    if (u->style & DS_CENTER) {
        u->wx = (PLAT_W - u->ww) / 2;
        u->wy = (PLAT_H - u->wh) / 2;
    }
    if (u->wx + u->ww > PLAT_W) u->wx = PLAT_W - u->ww;
    if (u->wy + u->wh > PLAT_H) u->wy = PLAT_H - u->wh;
    if (u->wx < 0) u->wx = 0;
    if (u->wy < 0) u->wy = 0;
    u->cx = u->wx + f + ce;
    u->cy = u->wy + f + ce + cap;
}

static Ctl *add_ctl(Ui *u, int kind, uint32_t id, uint32_t style, uint32_t exstyle, int x, int y, int w,
                    int h, const char *text)
{
    Ctl *k = &u->ctl[u->nctl++];
    memset(k, 0, sizeof *k);
    k->ui = u;
    k->kind = kind;
    k->id = id;
    k->style = style | WS_CHILD;
    k->exstyle = exstyle;
    /* Dialogs draw WS_BORDER controls with a 3D client edge (DS_3DLOOK). */
    if (k->style & WS_BORDER) {
        k->style &= ~WS_BORDER;
        k->exstyle |= WS_EX_CLIENTEDGE;
    }
    k->x = x, k->y = y, k->w = w, k->h = h;
    k->text = strdup(text ? text : "");
    k->font = u->font;
    k->cur = -1;
    k->limit = 0x7ffe;
    k->item_h = u->font_h;
    if (kind == CK_LISTBOX && !(k->style & LBS_NOINTEGRALHEIGHT)) {
        int e = ctl_edge(k), inner = h - 2 * e;
        if (inner > k->item_h)
            k->h = inner - inner % k->item_h + 2 * e;
    }
    if (kind == CK_BUTTON && (k->style & BS_TYPEMASK) == BS_DEFPUSHBUTTON && u->def_id == IDOK)
        u->def_id = id;
    return k;
}

static void create_ctl_windows(Ui *u)
{
    for (int i = 0; i < u->nctl; i++) {
        Ctl *k = &u->ctl[i];
        k->hwnd = win_host_create(u->hwnd, k->id, k->style, k->exstyle, k->x, k->y, k->w, k->h,
                                  control_wndproc);
    }
}

static Ui *ui_new(int kind)
{
    Ui *u = calloc(1, sizeof *u);
    u->kind = kind;
    u->focus = u->capture = u->hot = -1;
    u->def_id = IDOK;
    return u;
}

static void ui_push(Ui *u)
{
    if (nui == MAX_UI)
        rt_fatal("too many nested dialogs");
    uis[nui++] = u;
}

static void ui_free(Ui *u)
{
    int at = ui_index(u);
    if (at >= 0) {
        memmove(uis + at, uis + at + 1, (size_t)(nui - at - 1) * sizeof *uis);
        nui--;
    }
    if (!nui)
        ui_mem = 0;
    under_free(u);
    for (int i = 0; i < u->nctl; i++) {
        Ctl *k = &u->ctl[i];
        win_host_destroy(k->hwnd);
        free(k->text);
        for (int j = 0; j < k->count; j++)
            free(k->items[j]);
        free(k->items);
    }
    if (u->hwnd)
        win_host_destroy(u->hwnd);
    free(u->ctl);
    free(u);
}

static Ui *dialog_create(Cpu *c, uint32_t tpl, uint32_t owner, uint32_t proc, uint32_t param, const char *name)
{
    uint32_t p = tpl;
    bool ex = R16(p) == 1 && R16(p + 2) == 0xffff;
    uint32_t style, exstyle;
    if (ex) {
        p += 8;   /* dlgVer, signature, helpID */
        exstyle = rd32(&p);
        style = rd32(&p);
    } else {
        style = rd32(&p);
        exstyle = rd32(&p);
    }
    int n = rd16(&p);
    int tx = (int16_t)rd16(&p), ty = (int16_t)rd16(&p), tcx = (int16_t)rd16(&p), tcy = (int16_t)rd16(&p);
    char tmp[512], face[64] = "MS Sans Serif";
    rd_sz_or_ord(&p, NULL, 0);                 /* menu */
    rd_sz_or_ord(&p, NULL, 0);                 /* class */
    Ui *u = ui_new(UI_DIALOG);
    rd_wstr(&p, u->title, sizeof u->title);
    int points = 8;
    if (style & DS_SETFONT) {
        points = rd16(&p);
        if (ex)
            p += 4;   /* weight, italic, charset */
        rd_wstr(&p, face, sizeof face);
    }
    if (style & DS_MODALFRAME)
        exstyle |= WS_EX_DLGMODALFRAME;
    u->style = style;
    u->exstyle = exstyle;
    u->owner = owner;
    u->proc = proc;
    u->font = gdi_dialog_font(face, points, 400, &u->bx, &u->by);
    int bbx, bby;
    u->font_bold = gdi_dialog_font(face, points, 700, &bbx, &bby);
    u->font_h = gdi_font_height(u->font);
    place_window(u, muldiv(tx, u->bx, 4), muldiv(ty, u->by, 8), muldiv(tcx, u->bx, 4), muldiv(tcy, u->by, 8));
    u->ctl = calloc((size_t)(n > 0 ? n : 1), sizeof *u->ctl);
    for (int i = 0; i < n; i++) {
        p = (p + 3) & ~3u;
        uint32_t istyle, iex;
        if (ex) {
            p += 4;   /* helpID */
            iex = rd32(&p);
            istyle = rd32(&p);
        } else {
            istyle = rd32(&p);
            iex = rd32(&p);
        }
        int x = (int16_t)rd16(&p), y = (int16_t)rd16(&p), w = (int16_t)rd16(&p), h = (int16_t)rd16(&p);
        uint32_t id = ex ? rd32(&p) : rd16(&p);
        char cls[64];
        int ord = rd_sz_or_ord(&p, cls, sizeof cls);
        rd_sz_or_ord(&p, tmp, sizeof tmp);         /* an ordinal title (icon id) shows nothing */
        p += rd16(&p);                              /* creation data */
        add_ctl(u, class_kind(ord, cls), id, istyle, iex, muldiv(x, u->bx, 4), muldiv(y, u->by, 8),
                muldiv(w, u->bx, 4), muldiv(h, u->by, 8), tmp);
    }
    RT_INFO("dialog %s: %d controls, client %d,%d %dx%d, font \"%s\" %d", name, n, u->cx, u->cy, u->cw, u->ch,
            face, points);
    u->hwnd = win_host_create(owner, 0, style & ~WS_VISIBLE, exstyle, u->wx, u->wy, u->ww, u->wh,
                              dialog_wndproc);
    ui_push(u);
    if (style & DS_SETFONT)
        win_send(c, u->hwnd, WM_SETFONT, u->font, 0);
    create_ctl_windows(u);
    return u;
}

/* WM_INITDIALOG, initial focus and showing the window. */
static void dialog_start(Cpu *c, Ui *u, uint32_t param)
{
    int first = -1;
    for (int i = 0; i < u->nctl && first < 0; i++)
        if ((u->ctl[i].style & WS_TABSTOP) && focusable(&u->ctl[i]))
            first = i;
    uint32_t r = win_send(c, u->hwnd, WM_INITDIALOG, first >= 0 ? u->ctl[first].hwnd : 0, param);
    if (u->ended)
        return;
    if (r && first >= 0)
        set_focus(c, u, first, true);
    u->visible = true;
    u->full_dirty = true;
    if (u->proc)
        win_send(c, u->hwnd, WM_SHOWWINDOW, 1, 0);
}

/* ---------------------------------------------------------------- dialog input */
static void next_tab(Cpu *c, Ui *u, bool back)
{
    if (!u->nctl)
        return;
    int start = u->focus >= 0 ? u->focus : (back ? 0 : u->nctl - 1);
    for (int step = 1; step <= u->nctl; step++) {
        int i = (start + (back ? -step : step) % u->nctl + u->nctl) % u->nctl;
        Ctl *k = &u->ctl[i];
        if ((k->style & WS_TABSTOP) && focusable(k)) {
            set_focus(c, u, i, true);
            return;
        }
    }
}

/* Arrow keys on buttons move to the next control of the group. */
static void next_in_group(Cpu *c, Ui *u, bool back)
{
    int start = u->focus >= 0 ? u->focus : 0;
    for (int step = 1; step < u->nctl; step++) {
        int i = (start + (back ? -step : step) % u->nctl + u->nctl) % u->nctl;
        if (focusable(&u->ctl[i])) {
            set_focus(c, u, i, true);
            return;
        }
    }
}

static int hit_ctl(Ui *u, int x, int y)
{
    for (int i = 0; i < u->nctl; i++) {
        Ctl *k = &u->ctl[i];
        if (!focusable(k))
            continue;
        int X = u->cx + k->x, Y = u->cy + k->y;
        if (x >= X && x < X + k->w && y >= Y && y < Y + k->h && x >= u->cx && y >= u->cy &&
            x < u->cx + u->cw && y < u->cy + u->ch)
            return i;
    }
    return -1;
}

static bool inside(Ctl *k, int x, int y)
{
    int X = k->ui->cx + k->x, Y = k->ui->cy + k->y;
    return x >= X && x < X + k->w && y >= Y && y < Y + k->h;
}

static int edit_pos_at(Ctl *k, int px)
{
    int x, y, x1;
    edit_text_origin(k, &x, &y, &x1);
    int n = (int)strlen(k->text), best = 0, bestd = 1 << 30;
    for (int i = 0; i <= n; i++) {
        int d = abs(x + gdi_text_width(k->font, k->text, i) - px);
        if (d < bestd)
            bestd = d, best = i;
    }
    return best;
}

/* Keep the caret inside the edit (ES_AUTOHSCROLL). */
static void edit_scroll(Ctl *k)
{
    int e = ctl_edge(k), width = k->w - 2 * e - 2;
    int cx = gdi_text_width(k->font, k->text, k->caret);
    if (cx - k->scroll > width - 1)
        k->scroll = cx - width + width / 4;
    if (cx < k->scroll)
        k->scroll = imax(0, cx - width / 4);
    if (k->scroll < 0)
        k->scroll = 0;
}

static void edit_delete(Ctl *k, int a, int b)
{
    int n = (int)strlen(k->text);
    a = imax(0, imin(a, n));
    b = imax(a, imin(b, n));
    memmove(k->text + a, k->text + b, (size_t)(n - b + 1));
    k->caret = k->anchor = a;
}

static bool edit_key(Cpu *c, Ctl *k, int vk, int ch)
{
    int n = (int)strlen(k->text);
    int s0 = imin(k->anchor, k->caret), s1 = imax(k->anchor, k->caret);
    bool changed = false, moved = true;
    switch (vk) {
    case VK_LEFT: k->caret = imax(0, k->caret - 1); break;
    case VK_RIGHT: k->caret = imin(n, k->caret + 1); break;
    case VK_HOME: k->caret = 0; break;
    case VK_END: k->caret = n; break;
    case VK_BACK:
        if (s0 != s1)
            edit_delete(k, s0, s1), changed = true;
        else if (k->caret > 0)
            edit_delete(k, k->caret - 1, k->caret), changed = true;
        break;
    case VK_DELETE:
        if (s0 != s1)
            edit_delete(k, s0, s1), changed = true;
        else if (k->caret < n)
            edit_delete(k, k->caret, k->caret + 1), changed = true;
        break;
    default:
        moved = false;
        if (ch < 32 || ch == 127)
            return false;
        if ((k->style & ES_NUMBER) && !isdigit(ch))
            return true;   /* MessageBeep */
        if (s0 != s1)
            edit_delete(k, s0, s1);
        n = (int)strlen(k->text);
        if (n >= k->limit)
            return true;
        k->text = realloc(k->text, (size_t)n + 2);
        memmove(k->text + k->caret + 1, k->text + k->caret, (size_t)(n - k->caret + 1));
        k->text[k->caret++] = (char)ch;
        k->anchor = k->caret;
        changed = true;
        break;
    }
    if (moved && !changed && !shift_down)
        k->anchor = k->caret;
    edit_scroll(k);
    k->dirty = true;
    caret_reset();
    if (changed) {
        notify(c, k, EN_UPDATE);
        notify(c, k, EN_CHANGE);
    }
    return true;
}

static bool lb_key(Cpu *c, Ctl *k, int vk, int ch)
{
    int vis = lb_visible(k), cur = k->cur;
    switch (vk) {
    case VK_UP: lb_select(c, k, cur < 0 ? 0 : cur - 1, true); return true;
    case VK_DOWN: lb_select(c, k, cur + 1, true); return true;
    case VK_PRIOR: lb_select(c, k, cur - (vis - 1), true); return true;
    case VK_NEXT: lb_select(c, k, cur < 0 ? vis - 1 : cur + vis - 1, true); return true;
    case VK_HOME: lb_select(c, k, 0, true); return true;
    case VK_END: lb_select(c, k, k->count - 1, true); return true;
    default:
        break;
    }
    if (ch > 32 && ch < 256 && k->count) {
        for (int j = 1; j <= k->count; j++) {
            int i = (cur + j) % k->count;
            if (i < 0)
                i += k->count;
            if (tolower((unsigned char)k->items[i][0]) == tolower(ch)) {
                lb_select(c, k, i, true);
                break;
            }
        }
        return true;
    }
    return false;
}

static void lb_scroll_action(Ctl *k)
{
    int vis = lb_visible(k);
    switch (k->drag) {
    case DRAG_UP: lb_set_top(k, k->top - 1); break;
    case DRAG_DOWN: lb_set_top(k, k->top + 1); break;
    case DRAG_PAGEUP: lb_set_top(k, k->top - imax(1, vis - 1)); break;
    case DRAG_PAGEDOWN: lb_set_top(k, k->top + imax(1, vis - 1)); break;
    default: break;
    }
}

static void lb_mouse(Cpu *c, Ctl *k, int x, int y, bool down)
{
    int x0, y0, x1, y1;
    lb_inner(k, &x0, &y0, &x1, &y1);
    SB sb;
    bool has_sb = lb_scrollbar(k, &sb);
    if (down) {
        k->drag = DRAG_NONE;
        if (has_sb && x >= sb.x) {
            if (y < sb.track_y)
                k->drag = DRAG_UP;
            else if (y >= sb.track_y + sb.track_h)
                k->drag = DRAG_DOWN;
            else if (sb.thumb_h && y < sb.thumb_y)
                k->drag = DRAG_PAGEUP;
            else if (sb.thumb_h && y >= sb.thumb_y + sb.thumb_h)
                k->drag = DRAG_PAGEDOWN;
            else if (sb.thumb_h) {
                k->drag = DRAG_THUMB;
                k->drag_off = y - sb.thumb_y;
            }
            lb_scroll_action(k);
            k->repeat_at = win_now_ms() + REPEAT_DELAY_MS;
            k->dirty = true;
            return;
        }
        k->drag = DRAG_ITEMS;
    }
    if (k->drag == DRAG_ITEMS) {
        int rel = y - y0;
        int i = rel < 0 ? k->top - 1 : k->top + rel / k->item_h;
        if (i < k->count || rel < 0)
            lb_select(c, k, i, true);
    } else if (k->drag == DRAG_THUMB && has_sb) {
        int range = k->count - lb_visible(k), span = sb.track_h - sb.thumb_h;
        if (range > 0 && span > 0)
            lb_set_top(k, ((y - k->drag_off - sb.track_y) * range + span / 2) / span);
    }
}

static void dlg_mouse(Cpu *c, Ui *u, uint32_t msg, int x, int y, uint32_t buttons)
{
    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) {
        int i = hit_ctl(u, x, y);
        if (i < 0)
            return;
        Ctl *k = &u->ctl[i];
        set_focus(c, u, i, false);
        u->capture = i;
        if (k->kind == CK_BUTTON) {
            k->down = true;
            k->dirty = true;
        } else if (k->kind == CK_EDIT) {
            k->caret = k->anchor = edit_pos_at(k, x);
            k->dirty = true;
            caret_reset();
        } else if (k->kind == CK_LISTBOX) {
            lb_mouse(c, k, x, y, true);
        }
        return;
    }
    if (u->capture < 0)
        return;
    Ctl *k = &u->ctl[u->capture];
    if (msg == WM_MOUSEMOVE) {
        if (k->kind == CK_BUTTON) {
            bool in = inside(k, x, y) && (buttons & MK_LBUTTON);
            if (in != k->down) {
                k->down = in;
                k->dirty = true;
            }
        } else if (k->kind == CK_EDIT && (buttons & MK_LBUTTON)) {
            int pos = edit_pos_at(k, x);
            if (pos != k->caret) {
                k->caret = pos;
                edit_scroll(k);
                k->dirty = true;
            }
        } else if (k->kind == CK_LISTBOX && (buttons & MK_LBUTTON)) {
            lb_mouse(c, k, x, y, false);
        }
        return;
    }
    if (msg == WM_LBUTTONUP) {
        u->capture = -1;
        if (k->kind == CK_BUTTON) {
            bool click = k->down && inside(k, x, y);
            k->down = false;
            k->dirty = true;
            if (click)
                button_click(c, u, k);
        } else if (k->kind == CK_LISTBOX) {
            if (k->drag != DRAG_ITEMS && k->drag != DRAG_NONE)
                k->dirty = true;
            k->drag = DRAG_NONE;
        }
    }
}

static void dlg_key(Cpu *c, Ui *u, int vk, int ch, bool down)
{
    Ctl *f = u->focus >= 0 ? &u->ctl[u->focus] : NULL;
    if (!down) {
        if (vk == VK_SPACE && f && f->space) {
            f->space = false;
            f->down = false;
            f->dirty = true;
            button_click(c, u, f);
        }
        return;
    }
    switch (vk) {
    case VK_TAB:
        next_tab(c, u, shift_down);
        return;
    case VK_RETURN:
        if (f && is_push(f)) {
            button_click(c, u, f);
        } else {
            uint32_t r = win_send(c, u->hwnd, DM_GETDEFID, 0, 0);
            uint32_t id = (r >> 16) == DC_HASDEFID ? (r & 0xffff) : IDOK;
            Ctl *d = ctl_by_id(u, id);
            if (d && !ctl_enabled(d))
                return;
            win_send(c, u->hwnd, WM_COMMAND, id, d ? d->hwnd : 0);
        }
        return;
    case VK_ESCAPE: {
        Ctl *k = ctl_by_id(u, IDCANCEL);
        if (u->msgbox && !k) {
            if (u->nctl && ctl_by_id(u, IDOK) && !ctl_by_id(u, IDNO))
                win_send(c, u->hwnd, WM_COMMAND, IDOK, ctl_by_id(u, IDOK)->hwnd);
            return;
        }
        win_send(c, u->hwnd, WM_COMMAND, IDCANCEL, k ? k->hwnd : 0);
        return;
    }
    default:
        break;
    }
    if (!f)
        return;
    if (f->kind == CK_EDIT) {
        edit_key(c, f, vk, ch);
    } else if (f->kind == CK_LISTBOX) {
        lb_key(c, f, vk, ch);
    } else if (f->kind == CK_BUTTON) {
        if (vk == VK_SPACE) {
            f->space = f->down = true;
            f->dirty = true;
        } else if (vk >= VK_LEFT && vk <= VK_DOWN) {
            next_in_group(c, u, vk == VK_LEFT || vk == VK_UP);
        }
    }
}

/* ---------------------------------------------------------------- popup menus */
#define MAX_MENUS 16
#define MENU_HANDLE_BASE 0x000c0010u
static Menu menus[MAX_MENUS];

static Menu *menu_of(uint32_t h)
{
    for (int i = 0; i < MAX_MENUS; i++)
        if (menus[i].used && menus[i].handle == h && h)
            return &menus[i];
    return NULL;
}

static bool item_selectable(const MenuItem *it) { return !(it->flags & MF_SEPARATOR); }
static bool item_enabled(const MenuItem *it) { return !(it->flags & (MF_GRAYED | MF_DISABLED | MF_SEPARATOR)); }

static Ui *menu_open(Menu *m, int x, int y, uint32_t owner)
{
    Ui *u = ui_new(UI_MENU);
    int bx, by;
    u->menu = m;
    u->owner = owner;
    u->font = gdi_dialog_font("MS Sans Serif", 8, 400, &bx, &by);
    u->font_h = gdi_font_height(u->font);
    u->text_x = 18;
    int item_h = u->font_h + 4, maxw = 0, yy = 3;
    for (int i = 0; i < m->count; i++) {
        MenuItem *it = &m->items[i];
        it->y = yy;
        it->h = (it->flags & MF_SEPARATOR) ? 9 : item_h;
        yy += it->h;
        if (it->text)
            maxw = imax(maxw, measure(u->font, it->text, (int)strlen(it->text), true, 0));
    }
    u->ww = 3 + u->text_x - 3 + maxw + 18 + 3;
    u->wh = yy + 3;
    /* TPM_LEFTALIGN | TPM_TOPALIGN at (x, y), kept on the screen. */
    u->wx = imax(0, imin(x, PLAT_W - u->ww));
    u->wy = imax(0, imin(y, PLAT_H - u->wh));
    u->cx = u->wx, u->cy = u->wy, u->cw = u->ww, u->ch = u->wh;
    u->visible = true;
    u->full_dirty = true;
    return u;
}

static void paint_menu(Ui *u)
{
    Menu *m = u->menu;
    int x0 = u->wx, y0 = u->wy, x1 = u->wx + u->ww, y1 = u->wy + u->wh;
    set_clip(x0, y0, x1, y1);
    edge_raised(x0, y0, x1, y1);
    fill(x0 + 2, y0 + 2, x1 - 2, y1 - 2, sc(C_MENU));
    for (int i = 0; i < m->count; i++) {
        MenuItem *it = &m->items[i];
        int iy = y0 + it->y;
        if (it->flags & MF_SEPARATOR) {
            int ly = iy + it->h / 2 - 1;
            fill(x0 + 4, ly, x1 - 4, ly + 1, sc(C_3DSHADOW));
            fill(x0 + 4, ly + 1, x1 - 4, ly + 2, sc(C_3DHILIGHT));
            continue;
        }
        /* MF_GRAYED items are drawn grey; MF_DISABLED alone looks normal. */
        bool hot = i == u->hot, grayed = (it->flags & MF_GRAYED) != 0;
        if (hot)
            fill(x0 + 3, iy, x1 - 3, iy + it->h, sc(C_HIGHLIGHT));
        uint32_t col = hot ? (grayed ? sc(C_GRAYTEXT) : sc(C_HIGHLIGHTTEXT)) : sc(C_MENUTEXT);
        int ty = iy + (it->h - u->font_h) / 2, tx = x0 + u->text_x;
        if (it->flags & MF_CHECKED)
            paint_check_mark(x0 + 7, iy + (it->h - 7) / 2, grayed && !hot ? sc(C_3DSHADOW) : col);
        if (!it->text)
            continue;
        int n = (int)strlen(it->text);
        if (grayed && !hot) {   /* embossed grey text */
            draw_line(u->font, tx + 1, ty + 1, it->text, n, sc(C_3DHILIGHT), true, 0);
            draw_line(u->font, tx, ty, it->text, n, sc(C_3DSHADOW), true, 0);
        } else {
            draw_line(u->font, tx, ty, it->text, n, col, true, 0);
        }
    }
}

static int menu_hit(Ui *u, int x, int y)
{
    if (x < u->wx + 3 || x >= u->wx + u->ww - 3 || y < u->wy || y >= u->wy + u->wh)
        return -1;
    for (int i = 0; i < u->menu->count; i++) {
        MenuItem *it = &u->menu->items[i];
        if (y >= u->wy + it->y && y < u->wy + it->y + it->h)
            return item_selectable(it) ? i : -1;
    }
    return -1;
}

static bool in_window(Ui *u, int x, int y)
{
    return x >= u->wx && x < u->wx + u->ww && y >= u->wy && y < u->wy + u->wh;
}

static void menu_set_hot(Ui *u, int i)
{
    if (i != u->hot) {
        u->hot = i;
        u->full_dirty = true;
    }
}

static void menu_choose(Ui *u, int i)
{
    if (i < 0 || !item_enabled(&u->menu->items[i]))
        return;
    u->chosen = u->menu->items[i].id;
    u->picked = true;
    u->ended = true;
}

static void menu_input(Ui *u, const WinMsg *m)
{
    int x = (int16_t)(m->lparam & 0xffff), y = (int16_t)(m->lparam >> 16);
    switch (m->message) {
    case WM_MOUSEMOVE:
        menu_set_hot(u, menu_hit(u, x, y));
        break;
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
        if (!in_window(u, x, y)) {
            u->ended = true;   /* a click elsewhere cancels */
        } else if (m->message != WM_RBUTTONDOWN && m->message != WM_RBUTTONDBLCLK) {
            menu_set_hot(u, menu_hit(u, x, y));
            u->armed = true;
        }
        break;
    case WM_LBUTTONUP: {
        int i = menu_hit(u, x, y);
        /* The release of the click that opened the menu selects nothing. */
        if (i >= 0 && i == u->hot)
            menu_choose(u, i);
        break;
    }
    case WM_KEYDOWN: case WM_SYSKEYDOWN: {
        int vk = (int)m->wparam, n = u->menu->count;
        if (vk == VK_ESCAPE || m->message == WM_SYSKEYDOWN) {
            u->ended = true;
        } else if ((vk == VK_UP || vk == VK_DOWN) && n) {
            int i = u->hot;
            for (int step = 0; step < n; step++) {
                i = vk == VK_DOWN ? (i + 1) % n : (i <= 0 ? n - 1 : i - 1);
                if (item_selectable(&u->menu->items[i]))
                    break;
            }
            menu_set_hot(u, i);
        } else if (vk == VK_RETURN) {
            if (u->hot < 0)
                u->ended = true;
            else
                menu_choose(u, u->hot);
        }
        break;
    }
    default:
        break;
    }
}

/* ---------------------------------------------------------------- modal loop */
static void route_input(Cpu *c, Ui *u, const WinMsg *m)
{
    uint32_t msg = m->message;
    if (msg == WM_KEYDOWN || msg == WM_KEYUP) {
        if (m->wparam == VK_SHIFT)
            shift_down = msg == WM_KEYDOWN;
    }
    if (u->kind == UI_MENU) {
        menu_input(u, m);
        return;
    }
    if (msg >= WM_MOUSEMOVE && msg <= 0x0209) {
        dlg_mouse(c, u, msg, (int16_t)(m->lparam & 0xffff), (int16_t)(m->lparam >> 16), m->wparam);
    } else if (msg == WM_KEYDOWN || msg == WM_KEYUP) {
        int ch = m->x == WIN_KEYCHAR_MARK ? m->y : 0;
        dlg_key(c, u, (int)m->wparam, ch, msg == WM_KEYDOWN);
    }
}

static void ui_timers(Cpu *c)
{
    (void)c;
    uint32_t now = win_now_ms();
    Ui *u = ui_top();
    if (u && u->kind == UI_DIALOG && u->capture >= 0) {
        Ctl *k = &u->ctl[u->capture];
        if (k->kind == CK_LISTBOX && k->drag >= DRAG_UP && (int32_t)(now - k->repeat_at) >= 0) {
            lb_scroll_action(k);
            k->repeat_at = now + REPEAT_MS;
        }
    }
    if (focused_edit() && (int32_t)(now - caret.next) >= 0) {
        caret.phase = !caret.phase;
        caret.next = now + CARET_BLINK_MS;
        if (caret.phase)
            caret_show();
        else
            caret_hide();
        present();
    }
    /* Keep presenting while a modal window waits (frame dumps, window resizes). */
    if (now - last_present >= 250) {
        check_overdraw();
        present();
    }
}

/* Run until u ends. Input goes to the topmost window; everything else is
 * dispatched as usual (WM_TIMER and WM_PAINT of the game window). */
static void modal_loop(Cpu *c, Ui *u)
{
    win_modal_cursor(true);
    bool idle_sent = false;
    while (!u->ended) {
        paint_pending(c);
        if (u->ended)
            break;
        WinMsg m;
        bool got = win_get_message(c, &m, 0, 0, 0, true, 0);
        if (!got) {
            if (!idle_sent && u->kind == UI_DIALOG && u->owner) {
                idle_sent = true;   /* WM_ENTERIDLE(MSGF_DIALOGBOX) to the owner */
                win_send(c, u->owner, WM_ENTERIDLE, 0, u->hwnd);
                continue;
            }
            got = win_get_message(c, &m, 0, 0, 0, true, 100);
        }
        ui_timers(c);
        if (!got)
            continue;
        idle_sent = false;
        uint32_t msg = m.message;
        if ((msg >= WM_KEYDOWN && msg <= 0x0109) || (msg >= WM_MOUSEMOVE && msg <= 0x020d)) {
            Ui *top = ui_top();
            if (top && top->visible && !top->ended)
                route_input(c, top, &m);
        } else if (msg == WM_QUIT) {
            win_post_quit(m.wparam);
            u->ended = true;
            if (u->kind == UI_DIALOG)
                ui_unshow(u);
        } else {
            win_dispatch(c, &m);
        }
    }
    win_modal_cursor(false);
    win_post_mouse_update();
}

/* ---------------------------------------------------------------- USER32 dialog API */
WINAPI_FN(user32, DialogBoxParamA)
{
    uint32_t name = ARG(1), owner = ARG(2), proc = ARG(3), param = ARG(4);
    char label[64];
    if (name > 0xffff)
        snprintf(label, sizeof label, "%s", gstr(name));
    else
        snprintf(label, sizeof label, "#%u", name);
    uint32_t size;
    uint32_t tpl = res_find(5, name, &size);   /* RT_DIALOG, always from the EXE */
    if (!tpl) {
        RT_WARN("DialogBoxParamA: no dialog template %s", label);
        RET(5, 0xffffffffu);
    }
    Ui *u = dialog_create(c, tpl, owner, proc, param, label);
    dialog_start(c, u, param);
    if (!u->ended)
        modal_loop(c, u);
    uint32_t r = u->result;
    ui_unshow(u);
    win_send(c, u->hwnd, WM_DESTROY, 0, 0);
    ui_free(u);
    if (nui)
        paint_pending(c);
    RT_INFO("DialogBoxParamA(%s) -> %d", label, (int32_t)r);
    RET(5, r);
}

WINAPI_FN(user32, EndDialog)
{
    Ui *u = ui_of(ARG(0));
    if (!u)
        RET(2, 0);
    u->result = ARG(1);
    u->ended = true;
    if (u->capture >= 0)
        u->ctl[u->capture].down = false;
    ui_unshow(u);
    RET(2, 1);
}

WINAPI_FN(user32, GetDlgItem)
{
    Ctl *k = ctl_by_id(ui_of(ARG(0)), ARG(1));
    RET(2, k ? k->hwnd : 0);
}

WINAPI_FN(user32, SetDlgItemTextA)
{
    Ctl *k = ctl_by_id(ui_of(ARG(0)), ARG(1));
    if (!k)
        RET(3, 0);
    control_wndproc(c, k->hwnd, WM_SETTEXT, 0, ARG(2));
    RET(3, 1);
}

WINAPI_FN(user32, GetDlgItemTextA)
{
    uint32_t buf = ARG(2), cap = ARG(3);
    Ctl *k = ctl_by_id(ui_of(ARG(0)), ARG(1));
    if (!k) {
        if (buf && cap)
            W8(buf, 0);
        RET(4, 0);
    }
    RET(4, put_gstr(buf, cap, k->text));
}

/* Unsigned unless bSigned; blanks around the number are allowed, anything
 * else (or nothing, or an overflow) fails with *lpTranslated = FALSE. */
WINAPI_FN(user32, GetDlgItemInt)
{
    uint32_t ptrans = ARG(2), is_signed = ARG(3);
    Ctl *k = ctl_by_id(ui_of(ARG(0)), ARG(1));
    const char *s = k ? k->text : "";
    bool ok = false, neg = false;
    unsigned long long v = 0;
    while (*s == ' ' || *s == '\t')
        s++;
    if (is_signed && (*s == '-' || *s == '+'))
        neg = *s++ == '-';
    if (isdigit((unsigned char)*s)) {
        ok = true;
        while (isdigit((unsigned char)*s)) {
            v = v * 10 + (unsigned)(*s++ - '0');
            if (v > 0xffffffffull)
                ok = false;
        }
        while (*s == ' ' || *s == '\t')
            s++;
        if (*s)
            ok = false;
        if (is_signed && v > (neg ? 0x80000000ull : 0x7fffffffull))
            ok = false;
    }
    if (ptrans)
        W32(ptrans, ok ? 1 : 0);
    if (!k || !ok)
        RET(4, 0);
    RET(4, neg ? (uint32_t)-(int64_t)v : (uint32_t)v);
}

uint32_t dlg_begin_paint(Cpu *c, uint32_t hwnd, uint32_t ps)
{
    Ui *u = ui_of(hwnd);
    if (!u)
        return 0;
    Surface *s = screen();
    int32_t cl[4] = {u->cx, u->cy, u->cx + u->cw, u->cy + u->ch};
    uint32_t hdc = gdi_create_dc(s, u->cx, u->cy, cl);
    bool erased = true;
    if (u->erase) {
        u->erase = false;
        erased = win_send(c, hwnd, WM_ERASEBKGND, hdc, 0) != 0;
    }
    u->invalid = false;
    memset(g_mem + ps, 0, 64);
    W32(ps, hdc);
    W32(ps + 4, erased ? 0 : 1);   /* fErase */
    W32(ps + 16, (uint32_t)u->cw);
    W32(ps + 20, (uint32_t)u->ch);
    return hdc;
}

bool dlg_end_paint(Cpu *c, uint32_t hwnd, uint32_t ps)
{
    (void)c;
    if (!ui_of(hwnd))
        return false;
    gdi_delete_dc(R32(ps));
    return true;
}

/* ---------------------------------------------------------------- MessageBoxA */
static uint32_t message_box(Cpu *c, uint32_t owner, const char *text, const char *caption, uint32_t type)
{
    static const struct { int n; uint32_t id[3]; const char *label[3]; } sets[6] = {
        {1, {IDOK}, {"OK"}},
        {2, {IDOK, IDCANCEL}, {"OK", "Cancel"}},
        {3, {IDABORT, IDRETRY, IDIGNORE}, {"&Abort", "&Retry", "&Ignore"}},
        {3, {IDYES, IDNO, IDCANCEL}, {"&Yes", "&No", "Cancel"}},
        {2, {IDYES, IDNO}, {"&Yes", "&No"}},
        {2, {IDRETRY, IDCANCEL}, {"&Retry", "Cancel"}},
    };
    int set = (int)(type & 0xf) < 6 ? (int)(type & 0xf) : 0;
    int nb = sets[set].n, def = (int)((type >> 8) & 3);
    if (def >= nb)
        def = 0;
    const char *env = getenv("LULA_MSGBOX_AUTO");
    if ((env && *env && *env != '0') || !screen())
        return sets[set].id[def];

    Ui *u = ui_new(UI_DIALOG);
    u->msgbox = true;
    u->owner = owner;
    u->style = WS_POPUP | WS_CAPTION | DS_MODALFRAME | DS_CENTER;
    u->exstyle = WS_EX_DLGMODALFRAME;
    snprintf(u->title, sizeof u->title, "%s", caption);
    u->font = gdi_dialog_font("MS Sans Serif", 8, 400, &u->bx, &u->by);
    int bbx, bby;
    u->font_bold = gdi_dialog_font("MS Sans Serif", 8, 700, &bbx, &bby);
    u->font_h = gdi_font_height(u->font);
    uint32_t icon = (type >> 4) & 7;
    u->icon = icon >= 1 && icon <= 4 ? (int)icon : 0;

    /* Layout: icon, word-wrapped text, a centred row of 75x23 buttons. */
    const int margin = 12, bw = 75, bh = 23, gap = 6;
    Line lines[32];
    int nl = wrap_text(u->font, text, 420, false, 8 * u->bx, lines, 32);
    int tw = 0;
    for (int i = 0; i < nl; i++)
        tw = imax(tw, measure(u->font, text + lines[i].start, lines[i].len, false, 8 * u->bx));
    int th = nl * u->font_h;
    int tx = margin + (u->icon ? 32 + margin : 0);
    int content_h = imax(th, u->icon ? 32 : 0);
    int ty = margin + (content_h - th) / 2;
    int row_w = nb * bw + (nb - 1) * gap;
    int cw = imax(tx + tw + margin, row_w + 2 * margin);
    cw = imax(cw, gdi_text_width(u->font_bold, u->title, (int)strlen(u->title)) + 40);
    int by = margin + content_h + margin;
    int ch = by + bh + margin - 2;
    place_window(u, 0, 0, cw, ch);

    u->ctl = calloc((size_t)nb + 2, sizeof *u->ctl);
    if (u->icon)
        add_ctl(u, CK_ICON, 0xfffe, WS_VISIBLE, 0, margin, margin + (content_h - 32) / 2, 32, 32, "");
    add_ctl(u, CK_STATIC, 0xffff, WS_VISIBLE | SS_NOPREFIX, 0, tx, ty, tw + 1, th, text);
    int bx = (cw - row_w) / 2;
    for (int i = 0; i < nb; i++)
        add_ctl(u, CK_BUTTON, sets[set].id[i],
                WS_VISIBLE | WS_TABSTOP | (i == def ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON), 0,
                bx + i * (bw + gap), by, bw, bh, sets[set].label[i]);
    u->hwnd = win_host_create(owner, 0, u->style, u->exstyle, u->wx, u->wy, u->ww, u->wh, dialog_wndproc);
    ui_push(u);
    create_ctl_windows(u);
    set_focus(c, u, u->nctl - nb + def, true);
    u->visible = true;
    u->full_dirty = true;
    modal_loop(c, u);
    uint32_t r = u->result;
    ui_unshow(u);
    ui_free(u);
    if (nui)
        paint_pending(c);
    return r;
}

WINAPI_FN(user32, MessageBoxA)
{
    const char *text = gstr(ARG(1)), *cap = gstr(ARG(2));
    uint32_t type = ARG(3);
    RT_WARN("MessageBox \"%s\": %s (type %x)", cap ? cap : "", text ? text : "", type);
    uint32_t r = message_box(c, ARG(0), text ? text : "", cap ? cap : "Error", type);
    RT_INFO("MessageBox -> %u", r);
    RET(4, r);
}

/* ---------------------------------------------------------------- menus API */
WINAPI_FN(user32, CreatePopupMenu)
{
    static uint32_t next = MENU_HANDLE_BASE;
    for (int i = 0; i < MAX_MENUS; i++) {
        if (!menus[i].used) {
            memset(&menus[i], 0, sizeof menus[i]);
            menus[i].used = true;
            menus[i].handle = next;
            next += 4;
            RET(0, menus[i].handle);
        }
    }
    RET(0, 0);
}

WINAPI_FN(user32, AppendMenuA)
{
    Menu *m = menu_of(ARG(0));
    uint32_t flags = ARG(1), id = ARG(2), item = ARG(3);
    if (!m)
        RET(4, 0);
    if (m->count == m->cap) {
        m->cap = m->cap ? m->cap * 2 : 16;
        m->items = realloc(m->items, (size_t)m->cap * sizeof *m->items);
    }
    MenuItem *it = &m->items[m->count++];
    memset(it, 0, sizeof *it);
    it->flags = flags;
    it->id = id;
    if (!(flags & (MF_SEPARATOR | MF_BITMAP | MF_OWNERDRAW)) && item)
        it->text = strdup(gstr(item));
    RET(4, 1);
}

WINAPI_FN(user32, DestroyMenu)
{
    Menu *m = menu_of(ARG(0));
    if (!m)
        RET(1, 0);
    for (int i = 0; i < m->count; i++)
        free(m->items[i].text);
    free(m->items);
    memset(m, 0, sizeof *m);
    RET(1, 1);
}

/* Modal popup menu. A chosen item is posted as WM_COMMAND(id, 0) to the
 * owner; with flags 0 the result is TRUE whether or not an item was chosen. */
WINAPI_FN(user32, TrackPopupMenu)
{
    uint32_t hmenu = ARG(0), flags = ARG(1), owner = ARG(5);
    int x = (int32_t)ARG(2), y = (int32_t)ARG(3);
    Menu *m = menu_of(hmenu);
    if (!m || !m->count)
        RET(7, 0);
    bool notify_owner = !(flags & TPM_NONOTIFY);
    if (notify_owner) {
        win_send(c, owner, WM_ENTERMENULOOP, 1, 0);
        win_send(c, owner, WM_INITMENUPOPUP, hmenu, 0);
    }
    Ui *u = menu_open(m, x, y, owner);
    ui_push(u);
    if (screen())
        modal_loop(c, u);
    uint32_t id = u->chosen;
    bool picked = u->picked;
    ui_unshow(u);
    ui_free(u);
    if (nui)
        paint_pending(c);
    if (notify_owner) {
        win_send(c, owner, WM_MENUSELECT, 0xffff0000u, 0);
        win_send(c, owner, WM_EXITMENULOOP, 1, 0);
    }
    RT_INFO("TrackPopupMenu at %d,%d -> %s %u", x, y, picked ? "command" : "cancelled", id);
    if (flags & TPM_RETURNCMD)
        RET(7, picked ? id : 0);
    if (picked)
        win_post_message(owner, WM_COMMAND, id & 0xffff, 0);
    RET(7, 1);
}
