/* USER32: one top-level window, a thread-safe message queue fed by the SDL
 * main loop, window procedures called back into guest code, timers and the
 * small amount of cursor/system state the game queries. */
#include "win32.h"
#include "../platform.h"

#include <pthread.h>
#include <stdarg.h>
#include <time.h>

#define WM_NULL 0x0000
#define WM_CREATE 0x0001
#define WM_DESTROY 0x0002
#define WM_SIZE 0x0005
#define WM_ACTIVATE 0x0006
#define WM_SETFOCUS 0x0007
#define WM_KILLFOCUS 0x0008
#define WM_PAINT 0x000f
#define WM_CLOSE 0x0010
#define WM_QUIT 0x0012
#define WM_ERASEBKGND 0x0014
#define WM_SHOWWINDOW 0x0018
#define WM_ACTIVATEAPP 0x001c
#define WM_SETCURSOR 0x0020
#define WM_NCCREATE 0x0081
#define WM_NCDESTROY 0x0082
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_CHAR 0x0102
#define WM_TIMER 0x0113
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_LBUTTONDBLCLK 0x0203
#define WM_RBUTTONDOWN 0x0204
#define WM_RBUTTONUP 0x0205
#define WM_RBUTTONDBLCLK 0x0206
#define WM_MBUTTONDOWN 0x0207
#define WM_MBUTTONUP 0x0208

/* ---------------------------------------------------------------- windows */
typedef struct WinClass { char name[64]; uint32_t wndproc, style, icon, cursor, brush; } WinClass;
typedef struct Window {
    uint32_t hwnd, wndproc, style, exstyle, parent, id;
    int32_t x, y, w, h;
    bool visible, invalid, destroyed;
    char title[128];
} Window;

static WinClass classes[16];
static int nclasses;
static Window windows[32];
static int nwindows;
static uint32_t main_hwnd;
static pthread_mutex_t win_lock = PTHREAD_MUTEX_INITIALIZER;

uint32_t win_main_hwnd(void) { return main_hwnd; }

static bool destroy_window(Cpu *c, uint32_t hwnd);

static Window *window_of(uint32_t hwnd)
{
    for (int i = 0; i < nwindows; i++)
        if (windows[i].hwnd == hwnd && !windows[i].destroyed)
            return &windows[i];
    return NULL;
}

/* ---------------------------------------------------------------- queue */
#define QCAP 1024
typedef struct QMsg { WinMsg m; int ch; } QMsg;
static QMsg queue[QCAP];
static int qlen;
static pthread_mutex_t q_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t q_cond = PTHREAD_COND_INITIALIZER;
static bool quit_posted;
static uint32_t quit_code;
static int32_t last_mouse_x, last_mouse_y;
static uint32_t mouse_buttons;     /* MK_LBUTTON 1, MK_RBUTTON 2, MK_MBUTTON 0x10 */

static uint32_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

static void q_push_locked(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp, int ch)
{
    if (qlen == QCAP) {
        /* The guest stopped reading; drop the oldest message. */
        memmove(queue, queue + 1, (QCAP - 1) * sizeof *queue);
        qlen--;
    }
    queue[qlen].m = (WinMsg){hwnd, msg, wp, lp, now_ms(), last_mouse_x, last_mouse_y};
    queue[qlen].ch = ch;
    qlen++;
    pthread_cond_broadcast(&q_cond);
}

void win_post_message(uint32_t hwnd, uint32_t msg, uint32_t wparam, uint32_t lparam)
{
    pthread_mutex_lock(&q_lock);
    q_push_locked(hwnd, msg, wparam, lparam, 0);
    pthread_mutex_unlock(&q_lock);
}

/* Called by the platform on the main thread. */
void user32_input_mouse(int x, int y, int button, bool down)
{
    pthread_mutex_lock(&q_lock);
    last_mouse_x = x;
    last_mouse_y = y;
    uint32_t bit = button == 1 ? 1u : button == 3 ? 2u : button == 2 ? 0x10u : 0u;
    if (button) {
        if (down)
            mouse_buttons |= bit;
        else
            mouse_buttons &= ~bit;
    }
    uint32_t msg = WM_MOUSEMOVE;
    if (button == 1) msg = down ? WM_LBUTTONDOWN : WM_LBUTTONUP;
    else if (button == 3) msg = down ? WM_RBUTTONDOWN : WM_RBUTTONUP;
    else if (button == 2) msg = down ? WM_MBUTTONDOWN : WM_MBUTTONUP;
    uint32_t lp = ((uint32_t)(uint16_t)y << 16) | (uint16_t)x;
    /* Coalesce consecutive mouse moves like Windows does. */
    if (msg == WM_MOUSEMOVE && qlen) {
        int last = qlen - 1;
        if (queue[last].m.message == WM_MOUSEMOVE) {
            queue[last].m.lparam = lp;
            queue[last].m.wparam = mouse_buttons;
            queue[last].m.x = x;
            queue[last].m.y = y;
            pthread_mutex_unlock(&q_lock);
            return;
        }
    }
    if (main_hwnd)
        q_push_locked(main_hwnd, msg, mouse_buttons, lp, 0);
    pthread_mutex_unlock(&q_lock);
}

void user32_input_key(int vk, bool down, int ch)
{
    if (!main_hwnd)
        return;
    if (vk == -1) {
        win_post_message(main_hwnd, WM_CLOSE, 0, 0);
        return;
    }
    if (vk == -2) {
        win_post_message(main_hwnd, WM_ACTIVATEAPP, down ? 1 : 0, 0);
        return;
    }
    pthread_mutex_lock(&q_lock);
    q_push_locked(main_hwnd, down ? WM_KEYDOWN : WM_KEYUP, (uint32_t)vk,
                  down ? 1u : 0xc0000001u, ch);
    pthread_mutex_unlock(&q_lock);
}

/* ---------------------------------------------------------------- timers */
typedef struct Timer { uint32_t hwnd, id, ms, proc, due; bool used; } Timer;
static Timer timers[32];

static bool due_timer(uint32_t hwnd_filter, WinMsg *out, bool remove)
{
    uint32_t t = now_ms();
    for (int i = 0; i < 32; i++) {
        Timer *tm = &timers[i];
        if (!tm->used || (hwnd_filter && tm->hwnd != hwnd_filter) || (int32_t)(t - tm->due) < 0)
            continue;
        *out = (WinMsg){tm->hwnd, WM_TIMER, tm->id, tm->proc, t, last_mouse_x, last_mouse_y};
        if (remove)
            tm->due = t + (tm->ms ? tm->ms : 1);
        return true;
    }
    return false;
}

static int32_t next_timer_wait(void)
{
    int32_t best = -1;
    uint32_t t = now_ms();
    for (int i = 0; i < 32; i++) {
        if (!timers[i].used)
            continue;
        int32_t d = (int32_t)(timers[i].due - t);
        if (d < 0)
            d = 0;
        if (best < 0 || d < best)
            best = d;
    }
    return best;
}

/* ---------------------------------------------------------------- dispatch */
static uint32_t call_wndproc(Cpu *c, uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp)
{
    Window *w = window_of(hwnd);
    if (!w || !w->wndproc)
        return 0;
    return rt_guest_call(c, w->wndproc, 4, hwnd, msg, wp, lp);
}

/* Fetch the next message. wait_ms: 0 = poll, UINT32_MAX = forever. */
static bool get_message(Cpu *c, WinMsg *out, uint32_t hwnd, uint32_t lo, uint32_t hi,
                        bool remove, uint32_t wait_ms)
{
    (void)c;
    uint32_t start = now_ms();
    for (;;) {
        pthread_mutex_lock(&q_lock);
        for (int i = 0; i < qlen; i++) {
            WinMsg *m = &queue[i].m;
            if (hwnd && m->hwnd != hwnd)
                continue;
            if ((lo || hi) && (m->message < lo || m->message > hi))
                continue;
            *out = *m;
            int ch = queue[i].ch;
            if (remove) {
                memmove(queue + i, queue + i + 1, (size_t)(qlen - i - 1) * sizeof *queue);
                qlen--;
            }
            /* Remember the character for TranslateMessage. */
            if (out->message == WM_KEYDOWN)
                out->y = ch, out->x = -12345;
            pthread_mutex_unlock(&q_lock);
            return true;
        }
        if (quit_posted && (!lo && !hi)) {
            *out = (WinMsg){0, WM_QUIT, quit_code, 0, now_ms(), 0, 0};
            if (remove)
                quit_posted = false;
            pthread_mutex_unlock(&q_lock);
            return true;
        }
        pthread_mutex_unlock(&q_lock);
        /* Synthesised messages: WM_PAINT, then WM_TIMER. */
        if (!lo || (lo <= WM_PAINT && hi >= WM_PAINT)) {
            pthread_mutex_lock(&win_lock);
            for (int i = 0; i < nwindows; i++) {
                if (windows[i].invalid && !windows[i].destroyed && (!hwnd || windows[i].hwnd == hwnd)) {
                    *out = (WinMsg){windows[i].hwnd, WM_PAINT, 0, 0, now_ms(), last_mouse_x, last_mouse_y};
                    pthread_mutex_unlock(&win_lock);
                    return true;
                }
            }
            pthread_mutex_unlock(&win_lock);
        }
        if ((!lo || (lo <= WM_TIMER && hi >= WM_TIMER)) && due_timer(hwnd, out, remove))
            return true;
        uint32_t elapsed = now_ms() - start;
        if (wait_ms == 0 || (wait_ms != UINT32_MAX && elapsed >= wait_ms))
            return false;
        int32_t tw = next_timer_wait();
        uint32_t slice = wait_ms == UINT32_MAX ? 50 : wait_ms - elapsed;
        if (tw >= 0 && (uint32_t)tw < slice)
            slice = (uint32_t)tw;
        if (slice == 0)
            slice = 1;
        RT_BLOCKING_BEGIN();
        pthread_mutex_lock(&q_lock);
        if (qlen == 0 && !quit_posted) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += (long)slice * 1000000L;
            ts.tv_sec += ts.tv_nsec / 1000000000L;
            ts.tv_nsec %= 1000000000L;
            pthread_cond_timedwait(&q_cond, &q_lock, &ts);
        }
        pthread_mutex_unlock(&q_lock);
        RT_BLOCKING_END();
    }
}

static void write_msg(uint32_t p, const WinMsg *m)
{
    W32(p + 0, m->hwnd);
    W32(p + 4, m->message);
    W32(p + 8, m->wparam);
    W32(p + 12, m->lparam);
    W32(p + 16, m->time);
    /* MSG.pt; for WM_KEYDOWN the y slot carries the translated character
     * (x = -12345 marks it), which TranslateMessage reads back. */
    W32(p + 20, (uint32_t)m->x);
    W32(p + 24, (uint32_t)m->y);
}

WINAPI_FN(user32, GetMessageA)
{
    uint32_t pmsg = ARG(0);
    WinMsg m;
    get_message(c, &m, ARG(1), ARG(2), ARG(3), true, UINT32_MAX);
    write_msg(pmsg, &m);
    RET(4, m.message == WM_QUIT ? 0 : 1);
}

WINAPI_FN(user32, PeekMessageA)
{
    uint32_t pmsg = ARG(0);
    WinMsg m;
    bool got = get_message(c, &m, ARG(1), ARG(2), ARG(3), (ARG(4) & 1) != 0, 0);
    if (got)
        write_msg(pmsg, &m);
    RET(5, got ? 1 : 0);
}

WINAPI_FN(user32, TranslateMessage)
{
    uint32_t p = ARG(0);
    if (R32(p + 4) == WM_KEYDOWN && (int32_t)R32(p + 20) == -12345) {
        int ch = (int)R32(p + 24);
        if (ch > 0)
            win_post_message(R32(p), WM_CHAR, (uint32_t)ch, R32(p + 12));
        RET(1, 1);
    }
    RET(1, 0);
}

WINAPI_FN(user32, DispatchMessageA)
{
    uint32_t p = ARG(0);
    uint32_t hwnd = R32(p), msg = R32(p + 4), wp = R32(p + 8), lp = R32(p + 12);
    uint32_t r;
    if (msg == WM_TIMER && lp) {
        /* TIMERPROC(hwnd, WM_TIMER, id, time) */
        r = rt_guest_call(c, lp, 4, hwnd, msg, wp, R32(p + 16));
    } else {
        r = call_wndproc(c, hwnd, msg, wp, lp);
        if (msg == WM_PAINT) {
            Window *w = window_of(hwnd);
            if (w)
                w->invalid = false;   /* a WndProc without BeginPaint would loop */
        }
    }
    RET(1, r);
}

WINAPI_FN(user32, MsgWaitForMultipleObjects)
{
    uint32_t count = ARG(0), handles = ARG(1), ms = ARG(3);
    uint32_t win_wait(Cpu *c, uint32_t h, uint32_t ms);
    uint32_t start = now_ms();
    for (;;) {
        for (uint32_t i = 0; i < count; i++)
            if (win_wait(c, R32(handles + 4 * i), 0) == 0)
                RET(5, i);
        WinMsg m;
        if (get_message(c, &m, 0, 0, 0, false, 0))
            RET(5, count);
        uint32_t el = now_ms() - start;
        if (ms != UINT32_MAX && el >= ms)
            RET(5, 0x102);
        uint32_t slice = ms == UINT32_MAX ? 5 : (ms - el < 5 ? ms - el : 5);
        if (count)
            win_wait(c, R32(handles), slice);
        else
            get_message(c, &m, 0, 0, 0, false, slice);
    }
}

WINAPI_FN(user32, PostMessageA)
{
    win_post_message(ARG(0), ARG(1), ARG(2), ARG(3));
    RET(4, 1);
}

WINAPI_FN(user32, SendMessageA)
{
    uint32_t r = call_wndproc(c, ARG(0), ARG(1), ARG(2), ARG(3));
    RET(4, r);
}

WINAPI_FN(user32, PostQuitMessage)
{
    pthread_mutex_lock(&q_lock);
    quit_posted = true;
    quit_code = ARG(0);
    pthread_cond_broadcast(&q_cond);
    pthread_mutex_unlock(&q_lock);
    RET(1, 0);
}

WINAPI_FN(user32, DefWindowProcA)
{
    uint32_t hwnd = ARG(0), msg = ARG(1);
    uint32_t r = 0;
    switch (msg) {
    case WM_NCCREATE:
        r = 1;
        break;
    case WM_CLOSE: {
        destroy_window(c, hwnd);
        break;
    }
    case WM_ERASEBKGND:
        r = 1;
        break;
    case WM_SETCURSOR:
        r = 0;
        break;
    default:
        break;
    }
    RET(4, r);
}

/* ---------------------------------------------------------------- classes, windows */
WINAPI_FN(user32, RegisterClassA)
{
    uint32_t wc = ARG(0);
    if (nclasses == 16)
        RET(1, 0);
    WinClass *k = &classes[nclasses++];
    k->style = R32(wc);
    k->wndproc = R32(wc + 4);
    k->icon = R32(wc + 20);
    k->cursor = R32(wc + 24);
    k->brush = R32(wc + 28);
    uint32_t name = R32(wc + 36);
    snprintf(k->name, sizeof k->name, "%s", name > 0xffff ? gstr(name) : "#atom");
    RT_INFO("RegisterClassA(%s, wndproc=%08x)", k->name, k->wndproc);
    RET(1, 0xc000u + (uint32_t)nclasses);
}

WINAPI_FN(user32, CreateWindowExA)
{
    uint32_t exstyle = ARG(0), pclass = ARG(1), ptitle = ARG(2), style = ARG(3);
    int32_t x = (int32_t)ARG(4), y = (int32_t)ARG(5), w = (int32_t)ARG(6), h = (int32_t)ARG(7);
    uint32_t parent = ARG(8), menu = ARG(9), inst = ARG(10), param = ARG(11);
    const char *cls = pclass > 0xffff ? gstr(pclass) : NULL;
    WinClass *k = NULL;
    for (int i = 0; i < nclasses; i++)
        if (cls ? strcasecmp(classes[i].name, cls) == 0 : 0xc000u + (uint32_t)i + 1 == pclass)
            k = &classes[i];
    if (!k) {
        RT_WARN("CreateWindowExA: unknown class %s", cls ? cls : "#atom");
        RET(12, 0);
    }
    pthread_mutex_lock(&win_lock);
    Window *win = &windows[nwindows];
    memset(win, 0, sizeof *win);
    win->hwnd = 0x00010010u + 4u * (uint32_t)nwindows;
    nwindows++;
    pthread_mutex_unlock(&win_lock);
    win->wndproc = k->wndproc;
    win->style = style;
    win->exstyle = exstyle;
    win->parent = parent;
    win->id = menu;
    win->x = x; win->y = y; win->w = w; win->h = h;
    snprintf(win->title, sizeof win->title, "%s", ptitle ? gstr(ptitle) : "");
    if (!parent && !main_hwnd) {
        main_hwnd = win->hwnd;
        if (win->title[0])
            plat_set_title(win->title);
    }
    RT_INFO("CreateWindowExA(%s, \"%s\", style=%08x, %dx%d) -> %08x", k->name, win->title,
            style, w, h, win->hwnd);
    /* CREATESTRUCTA for WM_NCCREATE/WM_CREATE. */
    uint32_t cs = rt_heap_alloc(48);
    W32(cs + 0, param);
    W32(cs + 4, inst);
    W32(cs + 8, menu);
    W32(cs + 12, parent);
    W32(cs + 16, (uint32_t)h);
    W32(cs + 20, (uint32_t)w);
    W32(cs + 24, (uint32_t)y);
    W32(cs + 28, (uint32_t)x);
    W32(cs + 32, style);
    W32(cs + 36, ptitle);
    W32(cs + 40, pclass);
    W32(cs + 44, exstyle);
    uint32_t hwnd = win->hwnd;
    if (!call_wndproc(c, hwnd, WM_NCCREATE, 0, cs)) {
        RT_WARN("WM_NCCREATE refused window creation");
    }
    int32_t created = (int32_t)call_wndproc(c, hwnd, WM_CREATE, 0, cs);
    rt_heap_free(cs);
    if (created == -1) {
        window_of(hwnd)->destroyed = true;
        RET(12, 0);
    }
    if (style & 0x10000000u) {   /* WS_VISIBLE */
        window_of(hwnd)->visible = true;
        plat_show_window(true);
    }
    RET(12, hwnd);
}

WINAPI_FN(user32, ShowWindow)
{
    uint32_t hwnd = ARG(0), cmd = ARG(1);
    Window *w = window_of(hwnd);
    bool was = w && w->visible;
    if (w) {
        w->visible = cmd != 0;
        if (hwnd == main_hwnd)
            plat_show_window(cmd != 0);
        if (cmd && !was) {
            call_wndproc(c, hwnd, WM_SHOWWINDOW, 1, 0);
            call_wndproc(c, hwnd, WM_ACTIVATEAPP, 1, 0);
            call_wndproc(c, hwnd, WM_ACTIVATE, 1, 0);
            call_wndproc(c, hwnd, WM_SETFOCUS, 0, 0);
            w = window_of(hwnd);
            if (w)
                w->invalid = true;
        }
    }
    RET(2, was ? 1 : 0);
}

WINAPI_FN(user32, UpdateWindow)
{
    uint32_t hwnd = ARG(0);
    Window *w = window_of(hwnd);
    if (w && w->invalid) {
        call_wndproc(c, hwnd, WM_PAINT, 0, 0);
        w = window_of(hwnd);
        if (w)
            w->invalid = false;
    }
    RET(1, 1);
}

static bool destroy_window(Cpu *c, uint32_t hwnd)
{
    Window *w = window_of(hwnd);
    if (!w)
        return false;
    call_wndproc(c, hwnd, WM_DESTROY, 0, 0);
    call_wndproc(c, hwnd, WM_NCDESTROY, 0, 0);
    w = window_of(hwnd);
    if (w)
        w->destroyed = true;
    for (int i = 0; i < 32; i++)
        if (timers[i].hwnd == hwnd)
            timers[i].used = false;
    return true;
}

WINAPI_FN(user32, DestroyWindow)
{
    RET(1, destroy_window(c, ARG(0)) ? 1 : 0);
}

WINAPI_FN(user32, InvalidateRect)
{
    Window *w = window_of(ARG(0));
    if (w)
        w->invalid = true;
    else if (!ARG(0))
        for (int i = 0; i < nwindows; i++)
            windows[i].invalid = true;
    RET(3, 1);
}

WINAPI_FN(user32, GetClientRect)
{
    Window *w = window_of(ARG(0));
    uint32_t r = ARG(1);
    W32(r, 0);
    W32(r + 4, 0);
    W32(r + 8, w && w->w > 0 && w->w < 4096 ? (uint32_t)w->w : PLAT_W);
    W32(r + 12, w && w->h > 0 && w->h < 4096 ? (uint32_t)w->h : PLAT_H);
    RET(2, 1);
}

uint32_t gdi_screen_dc(void);

WINAPI_FN(user32, BeginPaint)
{
    uint32_t hwnd = ARG(0), ps = ARG(1);
    Window *w = window_of(hwnd);
    if (w)
        w->invalid = false;
    uint32_t dc = gdi_screen_dc();
    memset(g_mem + ps, 0, 64);
    W32(ps, dc);
    W32(ps + 4, 0);
    W32(ps + 16, PLAT_W);
    W32(ps + 20, PLAT_H);
    RET(2, dc);
}

WINAPI_FN(user32, EndPaint) { RET(2, 1); }

WINAPI_FN(user32, GetActiveWindow) { RET(0, main_hwnd); }

/* ---------------------------------------------------------------- timers */
WINAPI_FN(user32, SetTimer)
{
    uint32_t hwnd = ARG(0), id = ARG(1), ms = ARG(2), proc = ARG(3);
    Timer *slot = NULL;
    for (int i = 0; i < 32; i++)
        if (timers[i].used && timers[i].hwnd == hwnd && timers[i].id == id)
            slot = &timers[i];
    for (int i = 0; !slot && i < 32; i++)
        if (!timers[i].used)
            slot = &timers[i];
    if (!slot)
        RET(4, 0);
    if (!hwnd && !id)
        id = 0x100 + (uint32_t)(slot - timers);
    *slot = (Timer){hwnd, id, ms < 10 ? 10 : ms, proc, now_ms() + (ms < 10 ? 10 : ms), true};
    RT_INFO("SetTimer(hwnd=%08x, id=%u, %u ms, proc=%08x)", hwnd, id, ms, proc);
    RET(4, id);
}

WINAPI_FN(user32, KillTimer)
{
    uint32_t hwnd = ARG(0), id = ARG(1);
    for (int i = 0; i < 32; i++)
        if (timers[i].used && timers[i].hwnd == hwnd && timers[i].id == id) {
            timers[i].used = false;
            RET(2, 1);
        }
    RET(2, 0);
}

/* ---------------------------------------------------------------- input state */
WINAPI_FN(user32, GetAsyncKeyState)
{
    RET(1, plat_async_key_state((int)ARG(0)));
}

static int cursor_count;

WINAPI_FN(user32, ShowCursor)
{
    cursor_count += ARG(0) ? 1 : -1;
    plat_show_cursor(cursor_count >= 0);
    RET(1, (uint32_t)cursor_count);
}

WINAPI_FN(user32, SetCursor) { RET(1, 0); }

WINAPI_FN(user32, LoadCursorA) { RET(2, 0x00020000u | (ARG(1) & 0xffff)); }

WINAPI_FN(user32, LoadIconA) { RET(2, 0x00030000u | (ARG(1) & 0xffff)); }

WINAPI_FN(user32, SetCursorPos)
{
    plat_warp_mouse((int)ARG(0), (int)ARG(1));
    pthread_mutex_lock(&q_lock);
    last_mouse_x = (int32_t)ARG(0);
    last_mouse_y = (int32_t)ARG(1);
    pthread_mutex_unlock(&q_lock);
    RET(2, 1);
}

/* The game window covers the whole 640x480 "screen": client = screen. */
WINAPI_FN(user32, ClientToScreen) { RET(2, 1); }
WINAPI_FN(user32, ScreenToClient) { RET(2, 1); }

WINAPI_FN(user32, GetSystemMetrics)
{
    uint32_t i = ARG(0);
    uint32_t v = 0;
    switch (i) {
    case 0: v = PLAT_W; break;      /* SM_CXSCREEN */
    case 1: v = PLAT_H; break;      /* SM_CYSCREEN */
    case 4: v = 0; break;           /* SM_CYCAPTION (fullscreen popup) */
    case 5: case 6: v = 1; break;   /* SM_CXBORDER/SM_CYBORDER */
    case 7: case 8: v = 3; break;   /* SM_CXDLGFRAME */
    case 13: case 14: v = 32; break;/* SM_CXCURSOR */
    case 19: v = 1; break;          /* SM_MOUSEPRESENT */
    case 32: case 33: v = 4; break; /* SM_CXFRAME */
    case 43: v = 2; break;          /* SM_CMOUSEBUTTONS */
    default: RT_INFO("GetSystemMetrics(%u) -> 0", i); break;
    }
    RET(1, v);
}

static uint32_t syscolors[32] = {
    0xc0c0c0, 0x808000, 0x800000, 0x808080, 0xc0c0c0, 0xffffff, 0x000000, 0x000000,
    0x000000, 0xffffff, 0xc0c0c0, 0xc0c0c0, 0x808080, 0x800000, 0xffffff, 0xc0c0c0,
    0x808080, 0x808080, 0x000000, 0xc0c0c0, 0xffffff, 0x000000, 0xc0c0c0, 0x000000,
    0xe1ffff, 0, 0, 0, 0, 0, 0, 0};

WINAPI_FN(user32, GetSysColor) { RET(1, ARG(0) < 32 ? syscolors[ARG(0)] : 0); }

WINAPI_FN(user32, SetSysColors)
{
    uint32_t n = ARG(0), idx = ARG(1), vals = ARG(2);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t k = R32(idx + 4 * i);
        if (k < 32)
            syscolors[k] = R32(vals + 4 * i);
    }
    RET(3, 1);
}

WINAPI_FN(user32, MessageBeep) { RET(1, 1); }

WINAPI_FN(user32, MessageBoxA)
{
    const char *text = gstr(ARG(1)), *cap = gstr(ARG(2));
    uint32_t type = ARG(3);
    RT_WARN("MessageBox \"%s\": %s (type %x)", cap ? cap : "", text ? text : "", type);
    /* IDOK for OK boxes, IDYES/IDOK otherwise; the host cannot ask yet. */
    uint32_t r = (type & 0xf) == 4 || (type & 0xf) == 3 ? 6 : 1;
    RET(4, r);
}

WINAPI_FN(user32, SetScrollInfo) { RET(4, 0); }

/* ---------------------------------------------------------------- strings */
WINAPI_FN(user32, LoadStringA)
{
    uint32_t id = ARG(1), buf = ARG(2), cap = ARG(3);
    uint32_t res_string(uint32_t id, char *out, uint32_t cap);
    char tmp[1024];
    uint32_t n = res_string(id, tmp, sizeof tmp);
    if (!n) {
        if (cap)
            W8(buf, 0);
        RET(4, 0);
    }
    RET(4, put_gstr(buf, cap, tmp));
}

/* wsprintfA: cdecl, Windows printf subset (%d %i %u %x %X %s %c %ld %lu %lx and
 * width/precision/flags; no floating point). */
uint32_t win_format(char *out, size_t cap, const char *fmt, uint32_t args)
{
    size_t o = 0;
    for (const char *p = fmt; *p && o + 1 < cap; p++) {
        if (*p != '%') {
            out[o++] = *p;
            continue;
        }
        char spec[32] = "%";
        size_t s = 1;
        p++;
        if (*p == '%') {
            out[o++] = '%';
            continue;
        }
        while (*p && strchr("-+ #0", *p) && s < 20)
            spec[s++] = *p++;
        while (*p >= '0' && *p <= '9' && s < 24)
            spec[s++] = *p++;
        if (*p == '.') {
            spec[s++] = *p++;
            while (*p >= '0' && *p <= '9' && s < 28)
                spec[s++] = *p++;
        }
        if (*p == 'l' || *p == 'h')
            p++;
        char conv = *p;
        if (!conv)
            break;
        char tmp[512];
        int n = 0;
        switch (conv) {
        case 'd': case 'i':
            spec[s++] = 'd'; spec[s] = 0;
            n = snprintf(tmp, sizeof tmp, spec, (int32_t)R32(args));
            args += 4;
            break;
        case 'u': case 'x': case 'X': case 'o':
            spec[s++] = conv; spec[s] = 0;
            n = snprintf(tmp, sizeof tmp, spec, R32(args));
            args += 4;
            break;
        case 'c':
            spec[s++] = 'c'; spec[s] = 0;
            n = snprintf(tmp, sizeof tmp, spec, (int)(R32(args) & 0xff));
            args += 4;
            break;
        case 's': {
            spec[s++] = 's'; spec[s] = 0;
            uint32_t sp = R32(args);
            args += 4;
            n = snprintf(tmp, sizeof tmp, spec, sp ? gstr(sp) : "(null)");
            break;
        }
        default:
            tmp[0] = conv;
            tmp[1] = 0;
            n = 1;
            break;
        }
        if (n < 0)
            n = 0;
        for (int i = 0; i < n && (size_t)i < sizeof tmp - 1 && o + 1 < cap; i++)
            out[o++] = tmp[i];
    }
    out[o] = 0;
    return (uint32_t)o;
}

WINAPI_FN(user32, wsprintfA)
{
    uint32_t buf = ARG(0);
    const char *fmt = gstr(ARG(1));
    char tmp[1025];
    uint32_t n = win_format(tmp, sizeof tmp, fmt, c->esp + 12);
    memcpy(g_mem + buf, tmp, n + 1);
    return rt_ret_cdecl(c, n);
}

/* ---------------------------------------------------------------- menus and dialogs
 * The final game has no visible menu bar; dialogs are implemented once their
 * use is mapped (docs/recomp/specs/user32-gdi32.md). */
WINAPI_FN(user32, LoadMenuA) { RET(2, 0x00040001u); }
WINAPI_FN(user32, CreatePopupMenu) { RET(0, 0x00040002u); }
WINAPI_FN(user32, AppendMenuA) { RET(4, 1); }
WINAPI_FN(user32, DestroyMenu) { RET(1, 1); }
WINAPI_FN(user32, SetMenu) { RET(2, 1); }
WINAPI_FN(user32, TrackPopupMenu) { RET(7, 0); }
