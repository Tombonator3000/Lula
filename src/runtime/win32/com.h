/* Minimal COM objects that guest code can call through their vtables.
 *
 * A guest-visible object is 8 bytes of guest memory: the vtable pointer and a
 * host object index. Each vtable entry is a host thunk address, so a guest
 * 'call [eax+0x14]' lands in the matching C method. Methods are stdcall with
 * 'this' as the first stack argument. */
#pragma once
#include "win32.h"

#define S_OK 0u
#define E_NOINTERFACE 0x80004002u
#define E_NOTIMPL 0x80004001u
#define E_FAIL 0x80004005u
#define E_INVALIDARG 0x80070057u
#define E_OUTOFMEMORY 0x8007000eu

typedef struct ComMethod { const char *name; GuestFn fn; int nargs; } ComMethod;

/* Build a guest vtable for interface methods (order as in the C++ header).
 * A NULL fn creates a stub that logs and returns E_NOTIMPL. */
uint32_t com_vtable(const char *iface, const ComMethod *methods, int count);

uint32_t com_new(uint32_t vtable, void *host);
void *com_host(uint32_t obj);
void com_set_vtable(uint32_t obj, uint32_t vtable);
void com_free(uint32_t obj);

static inline bool guid_eq(uint32_t a, const uint8_t g[16]) { return a && memcmp(g_mem + a, g, 16) == 0; }
void guid_str(uint32_t a, char out[40]);
