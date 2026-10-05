/* File-backed differential checks for reconstructed resource readers.
 * Input TSV: kind, path, offset, mode, decoded, error, convert, palette.
 * Paths are relative to the repository or original/app. Signature rows put
 * the three ASCII signature bytes in the path column. Every run uses one
 * identical handle, reset to the same position before each implementation.
 */
#include "rt.h"

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#define FM_ADDR 0x20000000u
#define FM_SIZE 0x20000u
#define DST_ADDR 0x20100000u
#define DST_SIZE 0x1000000u
#define PAL_ADDR 0x21100000u
#define PAL_SIZE 0x1000u
#define STACK_SIZE 0x10000u
#define GUARD_SIZE 0x10000u

typedef struct Region {
    uint32_t address, size;
    uint8_t *before, *lifted, *reconstructed;
} Region;
static Region regions[5];
static uint64_t rng = 0x9e3779b97f4a7c15ull;

static uint32_t random32(void)
{
    rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
    return (uint32_t)(rng >> 16);
}

static void random_memory(uint32_t address, uint32_t size)
{
    for (uint32_t i = 0; i + 4 <= size; i += 4)
        W32(address + i, random32());
}

static void set_region(int i, uint32_t address, uint32_t size)
{
    Region *r = &regions[i];
    if (r->size != size) {
        free(r->before); free(r->lifted); free(r->reconstructed);
        r->before = malloc(size); r->lifted = malloc(size); r->reconstructed = malloc(size);
        if (!r->before || !r->lifted || !r->reconstructed)
            rt_fatal("resource check: out of memory");
    }
    r->address = address; r->size = size;
}

static void save_memory(int phase)
{
    for (size_t i = 0; i < sizeof regions / sizeof regions[0]; ++i) {
        Region *r = &regions[i];
        uint8_t *out = phase == 0 ? r->before : phase == 1 ? r->lifted : r->reconstructed;
        memcpy(out, g_mem + r->address, r->size);
    }
}

static void restore_memory(void)
{
    for (size_t i = 0; i < sizeof regions / sizeof regions[0]; ++i)
        memcpy(g_mem + regions[i].address, regions[i].before, regions[i].size);
}

static const ReconEntry *entry_for(uint32_t address)
{
    for (size_t i = 0; i < g_reconstructed_count; ++i)
        if (g_reconstructed[i].addr == address)
            return &g_reconstructed[i];
    rt_fatal("resource check: %08x is not reconstructed", address);
}

static uint32_t file_seek(Cpu *c, uint32_t handle, uint32_t position, uint32_t method)
{
    const uint32_t args[] = {handle, position, 0, method};
    return rt_guest_callv(c, R32(0x0048b26c), 4, args);
}

static uint32_t file_open(Cpu *c, uint32_t name)
{
    const uint32_t args[] = {name, 0x80000000u, 1, 0, 3, 0, 0};
    return rt_guest_callv(c, R32(0x0048b23c), 7, args);
}

static void file_close(Cpu *c, uint32_t handle)
{
    rt_guest_callv(c, R32(0x0048b238), 1, &handle);
}

static int compare(const ReconEntry *entry, const Cpu *a, const Cpu *b,
                   uint32_t return_a, uint32_t return_b,
                   uint32_t position_a, uint32_t position_b)
{
    int bad = 0;
#define COMPARE(field) do { if (a->field != b->field) { \
    fprintf(stderr, "  %s: lifted=%08x reconstructed=%08x\n", #field, a->field, b->field); \
    bad = 1; } } while (0)
    COMPARE(eax); COMPARE(ecx); COMPARE(edx); COMPARE(ebx);
    COMPARE(esp); COMPARE(ebp); COMPARE(esi); COMPARE(edi);
    COMPARE(df); COMPARE(last_error);
    uint32_t flags_a[] = {a->cf, a->pf, a->af, a->zf, a->sf, a->of};
    uint32_t flags_b[] = {b->cf, b->pf, b->af, b->zf, b->sf, b->of};
    const char *names[] = {"cf", "pf", "af", "zf", "sf", "of"};
    for (size_t i = 0; i < 6; ++i)
        if ((entry->flags_live & (1u << i)) && flags_a[i] != flags_b[i]) {
            fprintf(stderr, "  %s: lifted=%u reconstructed=%u\n", names[i], flags_a[i], flags_b[i]);
            bad = 1;
        }
    if (memcmp(&a->fpu, &b->fpu, sizeof a->fpu)) {
        fprintf(stderr, "  FPU state differs\n"); bad = 1;
    }
    if (return_a != return_b) {
        fprintf(stderr, "  return: lifted=%08x reconstructed=%08x\n", return_a, return_b); bad = 1;
    }
    if (position_a != position_b) {
        fprintf(stderr, "  file position: lifted=%08x reconstructed=%08x\n", position_a, position_b); bad = 1;
    }
    for (size_t i = 0; i < sizeof regions / sizeof regions[0]; ++i) {
        Region *r = &regions[i];
        uint32_t first = 0;
        /* As in fncheck, memory below the final ESP is no longer live. */
        if (i == 3 && a->esp >= r->address && a->esp <= r->address + r->size)
            first = a->esp - r->address;
        if (memcmp(r->lifted + first, r->reconstructed + first, r->size - first)) {
            for (uint32_t j = first; j < r->size; ++j)
                if (r->lifted[j] != r->reconstructed[j]) {
                    fprintf(stderr, "  memory %08x: lifted=%02x reconstructed=%02x\n",
                            r->address + j, r->lifted[j], r->reconstructed[j]);
                    break;
                }
            bad = 1;
        }
    }
    return bad;
}

static uint32_t number(const char *s, unsigned line)
{
    char *end;
    errno = 0;
    unsigned long value = strtoul(s, &end, 0);
    if (errno || !*s || *end || value > UINT32_MAX)
        rt_fatal("resource check: invalid number in TSV line %u", line);
    return (uint32_t)value;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s REPOSITORY_ROOT FIXTURES.tsv\n", argv[0]);
        return 2;
    }
    char data[4096], exe[4096];
    if (snprintf(data, sizeof data, "%s/original/app", argv[1]) >= (int)sizeof data ||
        snprintf(exe, sizeof exe, "%s/original/app/WET.EXE", argv[1]) >= (int)sizeof exe)
        return 2;
    FILE *fixtures = fopen(argv[2], "r");
    if (!fixtures) { perror(argv[2]); return 2; }
    char save[] = "/tmp/lula-resource-check-XXXXXX";
    if (!mkdtemp(save)) { perror("mkdtemp"); fclose(fixtures); return 2; }
    rt_mem_init();
    rt_vfs_init(data, save);
    rt_vfs_set_mods(argv[1]);
    if (!rt_load_image(exe)) return 2;
    if (!rt_mem_commit(FM_ADDR, FM_SIZE) || !rt_mem_commit(DST_ADDR, DST_SIZE) ||
        !rt_mem_commit(PAL_ADDR, PAL_SIZE)) return 2;
    uint32_t tid;
    Cpu *c = rt_cpu_new_thread(&tid);
    rt_cpu_bind(c);
    rt_gil_acquire();
    uint32_t thread_data = rt_low_alloc(256), filename = rt_low_alloc(1024);
    W32(thread_data, c->stack_limit);
    W32(0x0048a694, thread_data); /* Watcom __GetThreadPtr, required by stack checks */
    set_region(0, FM_ADDR, FM_SIZE);
    set_region(2, PAL_ADDR, PAL_SIZE);
    set_region(3, c->stack_base - STACK_SIZE, STACK_SIZE);
    set_region(4, g_image_base, g_image_size);

    const char *kinds[] = {"signature", "ngs-seek", "ngs-read", "rle", "tbf"};
    const uint32_t addresses[] = {0x00442431, 0x0043ebd3, 0x00440041, 0x0043f7c1, 0x0043e8eb};
    unsigned counts[5] = {0}, failures = 0, line = 0;
    char *text = NULL;
    size_t capacity = 0;
    while (getline(&text, &capacity, fixtures) >= 0) {
        line++;
        text[strcspn(text, "\r\n")] = 0;
        if (!*text || *text == '#' || strncmp(text, "kind\t", 5) == 0) continue;
        char *fields[8], *p = text;
        for (int i = 0; i < 8; ++i) {
            fields[i] = p;
            char *tab = strchr(p, '\t');
            if (i < 7 && !tab) rt_fatal("resource check: TSV line %u needs 8 columns", line);
            if (i == 7 && tab) rt_fatal("resource check: TSV line %u has extra columns", line);
            if (tab) { *tab = 0; p = tab + 1; }
        }
        int kind = -1;
        for (int i = 0; i < 5; ++i) if (strcmp(fields[0], kinds[i]) == 0) kind = i;
        if (kind < 0) rt_fatal("resource check: unknown kind in TSV line %u", line);
        uint32_t offset = number(fields[2], line), mode = number(fields[3], line);
        uint32_t decoded = number(fields[4], line), error = number(fields[5], line);
        uint32_t convert = number(fields[6], line), palette = number(fields[7], line);
        if (strlen(fields[1]) >= 1024 || decoded > DST_SIZE - GUARD_SIZE - 64)
            rt_fatal("resource check: oversized path/buffer in TSV line %u", line);
        c->esp = c->stack_base - 256;
        strcpy((char *)g_mem + filename, fields[1]);
        uint32_t handle = 0;
        if (kind != 0) {
            handle = file_open(c, filename);
            if (handle == UINT32_MAX || !handle)
                rt_fatal("resource check: cannot open TSV line %u path %s", line, fields[1]);
        } else if (strlen(fields[1]) < 3) {
            rt_fatal("resource check: signature needs three bytes in TSV line %u", line);
        }
        /* NGS does not take a decoded-length argument, but its fixture's
         * decoded field declares the payload size for memory comparison. */
        uint32_t destination_size = decoded + GUARD_SIZE + 64;
        set_region(1, DST_ADDR, destination_size);
        random_memory(FM_ADDR, FM_SIZE);
        random_memory(DST_ADDR, destination_size);
        random_memory(PAL_ADDR, PAL_SIZE);
        random_memory(c->stack_base - STACK_SIZE, STACK_SIZE);
        uint32_t *registers[] = {&c->eax, &c->ecx, &c->edx, &c->ebx, &c->ebp, &c->esi, &c->edi};
        for (size_t i = 0; i < 7; ++i) *registers[i] = random32();
        c->cf = random32() & 1; c->pf = random32() & 1; c->af = random32() & 1;
        c->zf = random32() & 1; c->sf = random32() & 1; c->of = random32() & 1; c->df = 0;
        c->last_error = random32();
        rt_fpu_init(c);
        for (size_t i = 0; i < 8; ++i) c->fpu.st[i] = (double)(int32_t)random32() / 1024.0;
        c->fpu.top = random32() & 7;
        c->esp = c->stack_base - 256;
        c->eax = FM_ADDR + (random32() & 0x3ff);
        c->edx = handle;
        if (kind == 0) {
            /* Preserve an optional fourth byte too, so fixture variants
             * prove that the bounded comparison ignores that byte. */
            memcpy(g_mem + FM_ADDR + 0x800, fields[1], strlen(fields[1]) + 1);
            c->edx = FM_ADDR + 0x800;
        } else if (kind == 1) c->ebx = mode;
        else if (kind == 2) c->ebx = DST_ADDR + 32;
        else if (kind == 3) {
            c->ebx = (c->ebx & 0xffff0000u) | (mode & 0xffffu);
            c->ecx = decoded;
            PUSH32(DST_ADDR + 32);
        } else {
            c->ebx = DST_ADDR + 32;
            c->ecx = palette ? PAL_ADDR + 32 : 0;
        }
        W16(0x00455030, (uint16_t)error);
        W32(0x0048a68c, convert);
        Cpu start = *c;
        save_memory(0);
        const ReconEntry *entry = entry_for(addresses[kind]);
        if (handle) file_seek(c, handle, offset, 0);
        restore_memory(); *c = start;
        PUSH32(RT_MAGIC_RET);
        uint32_t return_a = entry->lifted(c);
        Cpu after_a = *c;
        save_memory(1);
        uint32_t position_a = handle ? file_seek(c, handle, 0, 1) : 0;
        if (handle) file_seek(c, handle, offset, 0);
        restore_memory(); *c = start;
        PUSH32(RT_MAGIC_RET);
        uint32_t return_b = entry->reconstructed(c);
        Cpu after_b = *c;
        save_memory(2);
        uint32_t position_b = handle ? file_seek(c, handle, 0, 1) : 0;
        if (compare(entry, &after_a, &after_b, return_a, return_b, position_a, position_b)) {
            fprintf(stderr, "%08x mismatch: TSV line %u (%s, offset %u, mode %u, decoded %u)\n",
                    entry->addr, line, kinds[kind], offset, mode, decoded);
            failures++;
        }
        counts[kind]++;
        restore_memory(); *c = start;
        if (handle) file_close(c, handle);
        if (failures >= 10) break;
    }
    int input_error = ferror(fixtures);
    unsigned total = 0;
    for (int i = 0; i < 5; ++i) total += counts[i];
    free(text); fclose(fixtures);
    for (int i = 0; i < 5; ++i) printf("%08x %s: %u fixtures\n", addresses[i], kinds[i], counts[i]);
    printf("resource check: %u mismatches\n", failures);
    rmdir(save);
    if (!total) fprintf(stderr, "resource check: no fixture rows were read\n");
    return failures || input_error || !total ? 1 : 0;
}
