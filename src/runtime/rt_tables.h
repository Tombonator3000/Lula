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

/* Hand-reconstructed functions (src/reconstructed) and the generated code they
 * replace; flags_live is the EFLAGS mask callers read after the call. */
typedef struct ReconEntry {
    uint32_t addr;
    GuestFn reconstructed, lifted;
    uint32_t flags_live;
    const char *profile;
} ReconEntry;
extern const ReconEntry g_reconstructed[];
extern const size_t g_reconstructed_count;

/* Coverage counters, one per entry of g_guest_entries. */
extern uint32_t rt_cov[];
extern const uint32_t rt_cov_count;
void rt_coverage_dump(void);
