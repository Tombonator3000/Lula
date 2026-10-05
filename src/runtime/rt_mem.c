/* Guest address space: a 4 GiB reservation with explicitly committed ranges. */
#include "rt.h"

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/mman.h>

uint8_t *g_mem;

#define PAGE 0x1000u
#define GRANULE 0x10000u            /* Windows allocation granularity */
#define PAGES (0x100000000ull / PAGE)

static uint8_t *committed;          /* one byte per guest page */
static pthread_mutex_t mem_lock = PTHREAD_MUTEX_INITIALIZER;

void rt_mem_init(void)
{
    size_t size = 0x100000000ull + 0x10000;   /* slack for unaligned top accesses */
    void *p = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED)
        rt_fatal("cannot reserve 4 GiB guest address space: %s", strerror(errno));
    g_mem = p;
    committed = calloc(PAGES, 1);
    if (!committed)
        rt_fatal("out of memory");
}

bool rt_mem_commit(uint32_t addr, uint32_t size)
{
    uint32_t start = addr & ~(PAGE - 1);
    uint64_t end = ((uint64_t)addr + size + PAGE - 1) & ~(uint64_t)(PAGE - 1);
    if (size == 0 || end > 0x100000000ull)
        return false;
    if (mprotect(g_mem + start, (size_t)(end - start), PROT_READ | PROT_WRITE) != 0)
        return false;
    for (uint64_t a = start; a < end; a += PAGE)
        committed[a / PAGE] = 1;
    return true;
}

void rt_mem_decommit(uint32_t addr, uint32_t size)
{
    uint32_t start = addr & ~(PAGE - 1);
    uint64_t end = ((uint64_t)addr + size + PAGE - 1) & ~(uint64_t)(PAGE - 1);
    if (end > 0x100000000ull || end <= start)
        return;
    /* Give the memory back and make the range fault again. */
    mmap(g_mem + start, (size_t)(end - start), PROT_NONE,
         MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0);
    for (uint64_t a = start; a < end; a += PAGE)
        committed[a / PAGE] = 0;
}

bool rt_mem_is_committed(uint32_t addr, uint32_t size)
{
    uint64_t end = (uint64_t)addr + (size ? size : 1);
    for (uint64_t a = addr & ~(PAGE - 1); a < end; a += PAGE)
        if (a >= 0x100000000ull || !committed[a / PAGE])
            return false;
    return true;
}

/* ---- low heap: bump allocator for objects that live for the whole run ---- */
static uint32_t low_next = RT_LOW_HEAP_BASE;

uint32_t rt_low_alloc(uint32_t size)
{
    pthread_mutex_lock(&mem_lock);
    uint32_t a = (low_next + 15) & ~15u;
    if (a + size > RT_LOW_HEAP_END) {
        pthread_mutex_unlock(&mem_lock);
        rt_fatal("low heap exhausted");
    }
    low_next = a + size;
    pthread_mutex_unlock(&mem_lock);
    if (!rt_mem_commit(a, size ? size : 1))
        rt_fatal("cannot commit low heap");
    memset(g_mem + a, 0, size);
    return a;
}

uint32_t rt_guest_strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    uint32_t a = rt_low_alloc((uint32_t)n);
    memcpy(g_mem + a, s, n);
    return a;
}

/* ---- host heap: first-fit free list in guest memory ---- */
typedef struct Block { uint32_t addr, size; bool used; } Block;
static Block *blocks;
static size_t nblocks, cap_blocks;

static void heap_init_once(void)
{
    if (blocks)
        return;
    cap_blocks = 256;
    blocks = calloc(cap_blocks, sizeof *blocks);
    blocks[0] = (Block){RT_HOST_HEAP_BASE, RT_HOST_HEAP_END - RT_HOST_HEAP_BASE, false};
    nblocks = 1;
}

uint32_t rt_heap_alloc(uint32_t size)
{
    size = (size + 15) & ~15u;
    if (size == 0)
        size = 16;
    pthread_mutex_lock(&mem_lock);
    heap_init_once();
    for (size_t i = 0; i < nblocks; i++) {
        if (blocks[i].used || blocks[i].size < size)
            continue;
        uint32_t a = blocks[i].addr;
        if (blocks[i].size > size) {
            if (nblocks == cap_blocks) {
                cap_blocks *= 2;
                blocks = realloc(blocks, cap_blocks * sizeof *blocks);
            }
            memmove(&blocks[i + 2], &blocks[i + 1], (nblocks - i - 1) * sizeof *blocks);
            blocks[i + 1] = (Block){a + size, blocks[i].size - size, false};
            nblocks++;
            blocks[i].size = size;
        }
        blocks[i].used = true;
        pthread_mutex_unlock(&mem_lock);
        if (!rt_mem_commit(a, size))
            rt_fatal("cannot commit host heap");
        memset(g_mem + a, 0, size);
        return a;
    }
    pthread_mutex_unlock(&mem_lock);
    rt_fatal("host heap exhausted (%u bytes)", size);
}

void rt_heap_free(uint32_t addr)
{
    if (!addr)
        return;
    pthread_mutex_lock(&mem_lock);
    for (size_t i = 0; i < nblocks; i++) {
        if (blocks[i].addr != addr || !blocks[i].used)
            continue;
        blocks[i].used = false;
        if (i + 1 < nblocks && !blocks[i + 1].used) {
            blocks[i].size += blocks[i + 1].size;
            memmove(&blocks[i + 1], &blocks[i + 2], (nblocks - i - 2) * sizeof *blocks);
            nblocks--;
        }
        if (i > 0 && !blocks[i - 1].used) {
            blocks[i - 1].size += blocks[i].size;
            memmove(&blocks[i], &blocks[i + 1], (nblocks - i - 1) * sizeof *blocks);
            nblocks--;
        }
        break;
    }
    pthread_mutex_unlock(&mem_lock);
}

/* ---- VirtualAlloc arena: 64 KiB granules, reserve/commit like Windows ---- */
#define MEM_COMMIT 0x1000u
#define MEM_RESERVE 0x2000u
#define MEM_DECOMMIT 0x4000u
#define MEM_RELEASE 0x8000u
#define VGRANULES ((RT_VALLOC_END - RT_VALLOC_BASE) / GRANULE)

static uint32_t vreserved[VGRANULES];   /* granule -> size of reservation starting here, or 0 */
static uint8_t vused[VGRANULES];

uint32_t rt_valloc(uint32_t addr, uint32_t size, uint32_t type, uint32_t protect)
{
    (void)protect;
    if (size == 0)
        return 0;
    pthread_mutex_lock(&mem_lock);
    if (addr == 0 || (type & MEM_RESERVE)) {
        uint32_t need = (size + GRANULE - 1) / GRANULE;
        uint32_t start = UINT32_MAX;
        if (addr) {
            uint32_t g = (addr - RT_VALLOC_BASE) / GRANULE;
            if (addr >= RT_VALLOC_BASE && addr < RT_VALLOC_END && g + need <= VGRANULES) {
                bool free_run = true;
                for (uint32_t i = 0; i < need; i++)
                    free_run &= !vused[g + i];
                if (free_run)
                    start = g;
            }
        } else {
            for (uint32_t g = 0, run = 0; g < VGRANULES; g++) {
                run = vused[g] ? 0 : run + 1;
                if (run == need) {
                    start = g + 1 - need;
                    break;
                }
            }
        }
        if (start == UINT32_MAX) {
            pthread_mutex_unlock(&mem_lock);
            return 0;
        }
        for (uint32_t i = 0; i < need; i++)
            vused[start + i] = 1;
        vreserved[start] = need * GRANULE;
        addr = RT_VALLOC_BASE + start * GRANULE;
    } else if (addr < RT_VALLOC_BASE || addr >= RT_VALLOC_END ||
               !vused[(addr - RT_VALLOC_BASE) / GRANULE]) {
        pthread_mutex_unlock(&mem_lock);
        return 0;
    }
    pthread_mutex_unlock(&mem_lock);
    if (type & MEM_COMMIT) {
        uint32_t lo = addr & ~(PAGE - 1);
        uint32_t hi = (addr + size + PAGE - 1) & ~(PAGE - 1);
        /* Newly committed pages read as zero, already committed ones keep data. */
        for (uint32_t p = lo; p < hi; p += PAGE) {
            if (!committed[p / PAGE]) {
                if (!rt_mem_commit(p, PAGE))
                    return 0;
                memset(g_mem + p, 0, PAGE);
            }
        }
    }
    return addr;
}

bool rt_vfree(uint32_t addr, uint32_t size, uint32_t type)
{
    if (addr < RT_VALLOC_BASE || addr >= RT_VALLOC_END)
        return false;
    uint32_t g = (addr - RT_VALLOC_BASE) / GRANULE;
    if (type & MEM_RELEASE) {
        pthread_mutex_lock(&mem_lock);
        uint32_t span = vreserved[g];
        if (!span || size != 0) {
            pthread_mutex_unlock(&mem_lock);
            return false;
        }
        for (uint32_t i = 0; i < span / GRANULE; i++)
            vused[g + i] = 0;
        vreserved[g] = 0;
        pthread_mutex_unlock(&mem_lock);
        rt_mem_decommit(addr, span);
        return true;
    }
    if (type & MEM_DECOMMIT) {
        rt_mem_decommit(addr, size);
        return true;
    }
    return false;
}
