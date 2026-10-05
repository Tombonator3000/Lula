/* SDL2 host: window, presentation, input and audio.
 *
 * The main thread owns SDL. Guest threads hand over finished frames and read
 * input state; input events become Win32 messages in the guest queue.
 *
 * Test hooks (environment):
 *   LULA_HEADLESS=1         dummy video/audio drivers
 *   LULA_FRAMEDUMP=DIR      write presented frames as PPM (see LULA_FRAMEDUMP_MS)
 *   LULA_INPUT=FILE         scripted input: "<ms> move X Y | click X Y | rclick X Y |
 *                           key NAME | quit" one per line, times since start
 *   LULA_SCALE=N            initial window scale (default 2)
 */
#include "platform.h"
#include "rt.h"
#include "win32/win32.h"

#include <SDL.h>
#include <pthread.h>
#include <stdlib.h>
#include <strings.h>

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static Uint32 ev_frame, ev_window;
static pthread_mutex_t frame_lock = PTHREAD_MUTEX_INITIALIZER;
static uint16_t frame[PLAT_W * PLAT_H];
static bool frame_dirty, want_show, want_cursor = true, cursor_dirty;
static char want_title[256] = "Lula - The Sexy Empire";
static bool title_dirty = true;
static int mouse_x = PLAT_W / 2, mouse_y = PLAT_H / 2;
static volatile uint8_t key_down[256], key_pressed[256];
static int warp_x = -1, warp_y = -1;
static const char *dump_dir;
static uint32_t dump_interval = 1000, last_dump, dump_seq;
static SDL_AudioDeviceID audio_dev;
static int audio_rate;
static PlatMixFn mix_fn;
static void *mix_user;

uint32_t plat_ticks_ms(void) { return SDL_GetTicks(); }

void plat_present_rgb565(const uint16_t *pixels, int width, int height, int pitch_bytes)
{
    pthread_mutex_lock(&frame_lock);
    int w = width < PLAT_W ? width : PLAT_W, h = height < PLAT_H ? height : PLAT_H;
    for (int y = 0; y < h; y++)
        memcpy(frame + y * PLAT_W, (const uint8_t *)pixels + (size_t)y * (size_t)pitch_bytes,
               (size_t)w * 2);
    frame_dirty = true;
    pthread_mutex_unlock(&frame_lock);
    SDL_Event e = {0};
    e.type = ev_frame;
    SDL_PushEvent(&e);
}

static void wake(void)
{
    SDL_Event e = {0};
    e.type = ev_window;
    SDL_PushEvent(&e);
}

void plat_set_title(const char *title)
{
    snprintf(want_title, sizeof want_title, "%s", title);
    title_dirty = true;
    wake();
}

void plat_show_window(bool show) { want_show = show; wake(); }
void plat_show_cursor(bool show) { want_cursor = show; cursor_dirty = true; wake(); }

void plat_warp_mouse(int x, int y)
{
    warp_x = x;
    warp_y = y;
    mouse_x = x;
    mouse_y = y;
    wake();
}

void plat_get_mouse(int *x, int *y) { *x = mouse_x; *y = mouse_y; }

uint16_t plat_async_key_state(int vk)
{
    if (vk < 0 || vk > 255)
        return 0;
    uint16_t r = key_down[vk] ? 0x8000 : 0;
    if (key_pressed[vk]) {
        r |= 1;
        key_pressed[vk] = 0;
    }
    return r;
}

void plat_quit(int code)
{
    if (audio_dev)
        SDL_CloseAudioDevice(audio_dev);
    SDL_Quit();
    exit(code);
}

void rt_exit_process(uint32_t code)
{
    RT_INFO("ExitProcess(%u)", code);
    fflush(stdout);
    fflush(stderr);
    plat_quit((int)code);
    for (;;) {}
}

/* ---------------------------------------------------------------- audio */
static void audio_cb(void *user, Uint8 *stream, int len)
{
    (void)user;
    memset(stream, 0, (size_t)len);
    if (mix_fn)
        mix_fn((int16_t *)stream, len / 4, mix_user);
}

void plat_audio_start(int rate, PlatMixFn fn, void *user)
{
    if (audio_dev)
        return;
    SDL_AudioSpec want = {0}, have;
    want.freq = rate;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audio_cb;
    mix_fn = fn;
    mix_user = user;
    audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!audio_dev) {
        RT_WARN("no audio device: %s", SDL_GetError());
        return;
    }
    audio_rate = have.freq;
    SDL_PauseAudioDevice(audio_dev, 0);
}

int plat_audio_rate(void) { return audio_rate ? audio_rate : 22050; }
void plat_audio_lock(void) { if (audio_dev) SDL_LockAudioDevice(audio_dev); }
void plat_audio_unlock(void) { if (audio_dev) SDL_UnlockAudioDevice(audio_dev); }

/* ---------------------------------------------------------------- input */
static int vk_from_sdl(SDL_Keycode k)
{
    if (k >= 'a' && k <= 'z') return (int)(k - 'a' + 'A');
    if (k >= '0' && k <= '9') return (int)k;
    switch (k) {
    case SDLK_RETURN: case SDLK_KP_ENTER: return 0x0d;
    case SDLK_ESCAPE: return 0x1b;
    case SDLK_SPACE: return 0x20;
    case SDLK_BACKSPACE: return 0x08;
    case SDLK_TAB: return 0x09;
    case SDLK_LSHIFT: case SDLK_RSHIFT: return 0x10;
    case SDLK_LCTRL: case SDLK_RCTRL: return 0x11;
    case SDLK_LALT: case SDLK_RALT: return 0x12;
    case SDLK_PAUSE: return 0x13;
    case SDLK_CAPSLOCK: return 0x14;
    case SDLK_PAGEUP: return 0x21;
    case SDLK_PAGEDOWN: return 0x22;
    case SDLK_END: return 0x23;
    case SDLK_HOME: return 0x24;
    case SDLK_LEFT: return 0x25;
    case SDLK_UP: return 0x26;
    case SDLK_RIGHT: return 0x27;
    case SDLK_DOWN: return 0x28;
    case SDLK_INSERT: return 0x2d;
    case SDLK_DELETE: return 0x2e;
    case SDLK_F1: case SDLK_F2: case SDLK_F3: case SDLK_F4: case SDLK_F5: case SDLK_F6:
    case SDLK_F7: case SDLK_F8: case SDLK_F9: case SDLK_F10: case SDLK_F11: case SDLK_F12:
        return 0x70 + (int)(k - SDLK_F1);
    case SDLK_KP_PLUS: return 0x6b;
    case SDLK_KP_MINUS: return 0x6d;
    case SDLK_PLUS: case SDLK_EQUALS: return 0xbb;
    case SDLK_MINUS: return 0xbd;
    case SDLK_COMMA: return 0xbc;
    case SDLK_PERIOD: return 0xbe;
    default: return 0;
    }
}

/* Character a key produces (Latin-1), 0 for none. */
static int char_from_sdl(SDL_Keycode k, SDL_Keymod mod)
{
    bool shift = (mod & KMOD_SHIFT) != 0;
    if (k >= 'a' && k <= 'z') return shift != ((mod & KMOD_CAPS) != 0) ? (int)(k - 32) : (int)k;
    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) return '\r';
    if (k == SDLK_BACKSPACE) return 8;
    if (k == SDLK_ESCAPE) return 27;
    if (k == SDLK_TAB) return 9;
    if (k >= 32 && k < 127 && !shift) return (int)k;
    if (k >= '0' && k <= '9' && shift) return ")!\"#$%&/()"[k - '0'];
    if (k >= 32 && k < 256) return (int)k;
    return 0;
}

void user32_input_key(int vk, bool down, int ch);
void user32_input_mouse(int x, int y, int button, bool down);

static void key_event(int vk, bool down, int ch)
{
    if (vk <= 0 || vk > 255)
        return;
    if (down && !key_down[vk])
        key_pressed[vk] = 1;
    key_down[vk] = down;
    user32_input_key(vk, down, ch);
}

static void mouse_button(int x, int y, int button, bool down)
{
    int vk = button == 1 ? 1 : button == 3 ? 2 : 4;
    if (down && !key_down[vk])
        key_pressed[vk] = 1;
    key_down[vk] = down;
    mouse_x = x;
    mouse_y = y;
    user32_input_mouse(x, y, button, down);
}

/* ---------------------------------------------------------------- script */
typedef struct ScriptEv { uint32_t t; char op[16]; int a, b; char name[32]; } ScriptEv;
static ScriptEv *script;
static int nscript, script_pos;

static void load_script(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        RT_WARN("cannot read input script %s", path);
        return;
    }
    char line[256];
    while (fgets(line, sizeof line, f)) {
        ScriptEv e = {0};
        int n = sscanf(line, "%u %15s %31s %d", &e.t, e.op, e.name, &e.b);
        if (n < 2 || line[0] == '#')
            continue;
        e.a = atoi(e.name);
        script = realloc(script, (size_t)(nscript + 1) * sizeof *script);
        script[nscript++] = e;
    }
    fclose(f);
}

static int vk_from_name(const char *n)
{
    static const struct { const char *name; int vk; } names[] = {
        {"RETURN", 0x0d}, {"ESCAPE", 0x1b}, {"SPACE", 0x20}, {"LEFT", 0x25}, {"UP", 0x26},
        {"RIGHT", 0x27}, {"DOWN", 0x28}, {"F1", 0x70}, {"TAB", 0x09}, {"BACK", 0x08}};
    for (size_t i = 0; i < sizeof names / sizeof *names; i++)
        if (strcasecmp(n, names[i].name) == 0)
            return names[i].vk;
    if (strlen(n) == 1)
        return toupper((unsigned char)n[0]);
    return 0;
}

static void run_script(uint32_t now)
{
    while (script_pos < nscript && script[script_pos].t <= now) {
        ScriptEv *e = &script[script_pos++];
        RT_INFO("script %u ms: %s %s %d", e->t, e->op, e->name, e->b);
        if (strcmp(e->op, "move") == 0) {
            mouse_x = e->a;
            mouse_y = e->b;
            user32_input_mouse(e->a, e->b, 0, false);
        } else if (strcmp(e->op, "click") == 0 || strcmp(e->op, "rclick") == 0) {
            int button = e->op[0] == 'r' ? 3 : 1;
            mouse_button(e->a, e->b, button, true);
            mouse_button(e->a, e->b, button, false);
        } else if (strcmp(e->op, "key") == 0) {
            int vk = vk_from_name(e->name);
            int ch = vk == 0x0d ? '\r' : vk == 0x1b ? 27 : vk == 0x20 ? ' ' :
                     (vk >= 'A' && vk <= 'Z') ? vk + 32 : (vk >= '0' && vk <= '9') ? vk : 0;
            key_event(vk, true, ch);
            key_event(vk, false, 0);
        } else if (strcmp(e->op, "dump") == 0) {
            last_dump = 0;
            dump_interval = 0;
        } else if (strcmp(e->op, "quit") == 0) {
            RT_INFO("script requested quit");
            plat_quit(0);
        }
    }
}

/* ---------------------------------------------------------------- loop */
static void dump_frame(uint32_t now)
{
    if (!dump_dir || (dump_interval && now - last_dump < dump_interval))
        return;
    last_dump = now;
    char path[4096];
    snprintf(path, sizeof path, "%s/frame_%05u_%07u.ppm", dump_dir, dump_seq++, now);
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    fprintf(f, "P6\n%d %d\n255\n", PLAT_W, PLAT_H);
    static uint8_t rgb[PLAT_W * PLAT_H * 3];
    for (int i = 0; i < PLAT_W * PLAT_H; i++) {
        uint16_t p = frame[i];
        uint8_t r = (uint8_t)((p >> 11) & 31), g = (uint8_t)((p >> 5) & 63), b = (uint8_t)(p & 31);
        rgb[3 * i] = (uint8_t)(r << 3 | r >> 2);
        rgb[3 * i + 1] = (uint8_t)(g << 2 | g >> 4);
        rgb[3 * i + 2] = (uint8_t)(b << 3 | b >> 2);
    }
    fwrite(rgb, 1, sizeof rgb, f);
    fclose(f);
}

void rt_platform_init(void)
{
    if (getenv("LULA_HEADLESS")) {
        setenv("SDL_VIDEODRIVER", "dummy", 0);
        setenv("SDL_AUDIODRIVER", "dummy", 0);
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0)
        rt_fatal("SDL_Init: %s", SDL_GetError());
    ev_frame = SDL_RegisterEvents(2);
    ev_window = ev_frame + 1;
    int scale = getenv("LULA_SCALE") ? atoi(getenv("LULA_SCALE")) : 2;
    if (scale < 1)
        scale = 1;
    window = SDL_CreateWindow(want_title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              PLAT_W * scale, PLAT_H * scale, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
    if (!window)
        rt_fatal("SDL_CreateWindow: %s", SDL_GetError());
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);
    if (!renderer)
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer)
        rt_fatal("SDL_CreateRenderer: %s", SDL_GetError());
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, getenv("LULA_SMOOTH") ? "linear" : "nearest");
    SDL_RenderSetLogicalSize(renderer, PLAT_W, PLAT_H);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
                                PLAT_W, PLAT_H);
    dump_dir = getenv("LULA_FRAMEDUMP");
    if (getenv("LULA_FRAMEDUMP_MS"))
        dump_interval = (uint32_t)atoi(getenv("LULA_FRAMEDUMP_MS"));
    if (getenv("LULA_INPUT"))
        load_script(getenv("LULA_INPUT"));
}

void rt_platform_shutdown(void) { plat_quit(0); }

void rt_platform_run(void)
{
    bool shown = false;
    for (;;) {
        SDL_Event e;
        int got = SDL_WaitEventTimeout(&e, 10);
        uint32_t now = SDL_GetTicks();
        run_script(now);
        while (got) {
            if (e.type == SDL_QUIT) {
                user32_input_key(-1, true, 0);    /* WM_CLOSE to the main window */
            } else if (e.type == SDL_MOUSEMOTION) {
                mouse_x = e.motion.x;
                mouse_y = e.motion.y;
                user32_input_mouse(e.motion.x, e.motion.y, 0, false);
            } else if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
                mouse_button(e.button.x, e.button.y, e.button.button, e.type == SDL_MOUSEBUTTONDOWN);
            } else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
                SDL_Keycode k = e.key.keysym.sym;
                if (e.type == SDL_KEYDOWN && k == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT)) {
                    Uint32 fs = SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP;
                    SDL_SetWindowFullscreen(window, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                } else {
                    key_event(vk_from_sdl(k), e.type == SDL_KEYDOWN,
                              e.type == SDL_KEYDOWN ? char_from_sdl(k, e.key.keysym.mod) : 0);
                }
            } else if (e.type == SDL_WINDOWEVENT) {
                if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
                    user32_input_key(-2, true, 0);
                else if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
                    user32_input_key(-2, false, 0);
            }
            got = SDL_PollEvent(&e);
        }
        if (title_dirty) {
            title_dirty = false;
            SDL_SetWindowTitle(window, want_title);
        }
        if (want_show && !shown) {
            SDL_ShowWindow(window);
            shown = true;
        }
        if (cursor_dirty) {
            cursor_dirty = false;
            SDL_ShowCursor(want_cursor ? SDL_ENABLE : SDL_DISABLE);
        }
        if (warp_x >= 0) {
            int ww, wh;
            SDL_GetWindowSize(window, &ww, &wh);
            SDL_WarpMouseInWindow(window, warp_x * ww / PLAT_W, warp_y * wh / PLAT_H);
            warp_x = warp_y = -1;
        }
        pthread_mutex_lock(&frame_lock);
        bool dirty = frame_dirty;
        if (dirty) {
            SDL_UpdateTexture(texture, NULL, frame, PLAT_W * 2);
            frame_dirty = false;
            dump_frame(now);
        }
        pthread_mutex_unlock(&frame_lock);
        if (dirty) {
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, NULL, NULL);
            SDL_RenderPresent(renderer);
        }
    }
}
