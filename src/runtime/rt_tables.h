/* Tables emitted by the recompiler (gen_tables.c). */
#pragma once
#include "rt_cpu.h"

typedef struct GuestEntry {
    uint32_t addr;
    GuestFn fn;
} GuestEntry;

typedef struct HostImport {
    uint32_t slot;          /* IAT slot address */
    const char *dll;        /* lower-case base name */
    const char *name;
    GuestFn fn;
} HostImport;

extern const char g_image_sha256[];
extern const uint32_t g_image_base, g_image_size, g_image_entry, g_stack_reserve;
extern const GuestEntry g_guest_entries[];
extern const size_t g_guest_entry_count;
extern const HostImport g_host_imports[];
extern const size_t g_host_import_count;
