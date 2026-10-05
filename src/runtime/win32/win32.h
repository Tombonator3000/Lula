/* Shared helpers for the Win32 API implementations. */
#pragma once
#include "../rt.h"

#include <stdlib.h>

#define WIN_TRUE 1u
#define WIN_FALSE 0u
#define INVALID_HANDLE_VALUE 0xffffffffu

#define ERROR_SUCCESS 0u
#define ERROR_FILE_NOT_FOUND 2u
#define ERROR_PATH_NOT_FOUND 3u
#define ERROR_ACCESS_DENIED 5u
#define ERROR_INVALID_HANDLE 6u
#define ERROR_NOT_ENOUGH_MEMORY 8u
#define ERROR_HANDLE_EOF 38u
#define ERROR_FILE_EXISTS 80u
#define ERROR_INVALID_PARAMETER 87u
#define ERROR_ALREADY_EXISTS 183u
#define ERROR_CALL_NOT_IMPLEMENTED 120u

#define HMODULE_EXE 0x00400000u
#define HMODULE_KERNEL32 0x7f000000u
#define HMODULE_USER32 0x7f100000u
#define HMODULE_GDI32 0x7f200000u
#define HMODULE_WINMM 0x7f300000u
#define HMODULE_DDRAW 0x7f400000u
#define HMODULE_DSOUND 0x7f500000u
#define HMODULE_OLE32 0x7f600000u
#define HMODULE_COMDLG32 0x7f700000u

static inline void win_set_error(Cpu *c, uint32_t e) { c->last_error = e; }

/* Guest string to host (NULL for a null pointer). */
static inline const char *gstr(uint32_t a) { return a ? (const char *)(g_mem + a) : NULL; }

/* Copy a host string into a guest buffer of cap bytes, always terminated.
 * Returns the number of characters copied, without the terminator. */
static inline uint32_t put_gstr(uint32_t dst, uint32_t cap, const char *src)
{
    if (!dst || !cap)
        return 0;
    size_t n = strlen(src);
    if (n >= cap)
        n = cap - 1;
    memcpy(g_mem + dst, src, n);
    g_mem[dst + n] = 0;
    return (uint32_t)n;
}

/* Declare a host implementation of an import with its stdcall arity. */
#define WINAPI_FN(dll, name) uint32_t h_##dll##_##name(Cpu *c)
#define ARG(i) rt_arg(c, (i))
#define RET(nargs, v) return rt_ret_stdcall(c, (nargs), (uint32_t)(v))

/* Exports reachable through GetProcAddress (kernel32.c). */
uint32_t win_proc_address(const char *dll, const char *name);

/* Window message queue (user32.c), used by other modules and the platform. */
typedef struct WinMsg {
    uint32_t hwnd, message, wparam, lparam, time;
    int32_t x, y;
} WinMsg;
void win_post_message(uint32_t hwnd, uint32_t msg, uint32_t wparam, uint32_t lparam);
uint32_t win_main_hwnd(void);

/* INI files (kernel32.c). */
uint32_t win_ini_get(const char *file, const char *section, const char *key,
                     const char *def, char *out, uint32_t cap);
bool win_ini_set(const char *file, const char *section, const char *key, const char *value);
