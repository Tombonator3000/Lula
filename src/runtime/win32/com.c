#include "com.h"

#include <pthread.h>

#define MAX_OBJECTS 4096
static void *objects[MAX_OBJECTS];
static pthread_mutex_t com_lock = PTHREAD_MUTEX_INITIALIZER;

/* Stubs need to know their own name and argument count; keep a table that
 * the generic stub looks up by thunk address. */
typedef struct Stub { uint32_t thunk; const char *name; int nargs; } Stub;
static Stub stubs[1024];
static int nstubs;

static uint32_t com_stub(Cpu *c)
{
    /* The thunk address that brought us here is not passed in; recover it
     * from the vtable slot the caller used is not possible either, so each
     * stub gets its own C trampoline below. */
    (void)c;
    return 0;
}

#define STUB_COUNT 64
static uint32_t stub_dispatch(Cpu *c, int idx)
{
    Stub *s = &stubs[idx];
    RT_WARN("COM method %s is not implemented (this=%08x)", s->name, rt_arg(c, 0));
    return rt_ret_stdcall(c, s->nargs, E_NOTIMPL);
}

#define T(n) static uint32_t stub_##n(Cpu *c) { return stub_dispatch(c, n); }
#define T8(n) T(n##0) T(n##1) T(n##2) T(n##3) T(n##4) T(n##5) T(n##6) T(n##7)
T8(0) T8(1) T8(2) T8(3) T8(4) T8(5) T8(6) T8(7)
#undef T
#define T(n) stub_##n,
#define T8(n) T(n##0) T(n##1) T(n##2) T(n##3) T(n##4) T(n##5) T(n##6) T(n##7)
static GuestFn stub_fns[STUB_COUNT] = {T8(0) T8(1) T8(2) T8(3) T8(4) T8(5) T8(6) T8(7)};

uint32_t com_vtable(const char *iface, const ComMethod *methods, int count)
{
    (void)com_stub;
    uint32_t vt = rt_heap_alloc(4u * (uint32_t)count);
    for (int i = 0; i < count; i++) {
        char *name = malloc(strlen(iface) + strlen(methods[i].name) + 3);
        sprintf(name, "%s::%s", iface, methods[i].name);
        GuestFn fn = methods[i].fn;
        if (!fn) {
            pthread_mutex_lock(&com_lock);
            int idx = nstubs++;
            pthread_mutex_unlock(&com_lock);
            if (idx >= STUB_COUNT)
                rt_fatal("too many unimplemented COM methods");
            stubs[idx] = (Stub){0, name, methods[i].nargs};
            fn = stub_fns[idx];
        }
        W32(vt + 4u * (uint32_t)i, rt_register_thunk(name, fn));
    }
    return vt;
}

uint32_t com_new(uint32_t vtable, void *host)
{
    pthread_mutex_lock(&com_lock);
    uint32_t idx = 0;
    for (uint32_t i = 1; i < MAX_OBJECTS; i++) {
        if (!objects[i]) {
            idx = i;
            break;
        }
    }
    if (!idx)
        rt_fatal("too many COM objects");
    objects[idx] = host;
    pthread_mutex_unlock(&com_lock);
    uint32_t obj = rt_heap_alloc(16);
    W32(obj, vtable);
    W32(obj + 4, idx);
    W32(obj + 8, 0x4c554c41u);   /* "ALUL" marker */
    return obj;
}

void *com_host(uint32_t obj)
{
    if (!obj || R32(obj + 8) != 0x4c554c41u)
        return NULL;
    uint32_t idx = R32(obj + 4);
    return idx < MAX_OBJECTS ? objects[idx] : NULL;
}

void com_set_vtable(uint32_t obj, uint32_t vtable) { W32(obj, vtable); }

void com_free(uint32_t obj)
{
    if (!obj || R32(obj + 8) != 0x4c554c41u)
        return;
    uint32_t idx = R32(obj + 4);
    pthread_mutex_lock(&com_lock);
    if (idx < MAX_OBJECTS)
        objects[idx] = NULL;
    pthread_mutex_unlock(&com_lock);
    W32(obj + 8, 0);
    rt_heap_free(obj);
}

void guid_str(uint32_t a, char out[40])
{
    if (!a) {
        snprintf(out, 40, "(null)");
        return;
    }
    snprintf(out, 40, "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", R32(a), R16(a + 4),
             R16(a + 6), R8(a + 8), R8(a + 9), R8(a + 10), R8(a + 11), R8(a + 12), R8(a + 13),
             R8(a + 14), R8(a + 15));
}
