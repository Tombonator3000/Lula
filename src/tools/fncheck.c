/* fncheck: compare hand-reconstructed functions with the recompiled originals.
 *
 *   build/game/lula-fncheck [--iterations N] [--seed S] [ADDRESS ...]
 *
 * For every function marked RT_RECONSTRUCTED (or the given addresses), random
 * machine states are built from the RT_CHECK profile. Both versions run from
 * the same state; registers, the EFLAGS bits callers read, x87 state and the
 * scratch, stack and image memory must end up identical.
 */
#include "rt.h"

#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>

#define SCRATCH_A 0x20000000u
#define SCRATCH_B 0x20100000u
#define SCRATCH_SIZE 0x10000u
#define STACK_CHECK 0x10000u
#define MAGIC 0xf00ffff0u

static uint64_t rng;
static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (uint32_t)(rng >> 16);
}

typedef struct Region { uint32_t addr, size; uint8_t *copy; } Region;
static Region regions[4];
static int nregions;

static void add_region(uint32_t addr, uint32_t size)
{
    regions[nregions++] = (Region){addr, size, malloc(size)};
}

static void snapshot(void)
{
    for (int i = 0; i < nregions; i++)
        memcpy(regions[i].copy, g_mem + regions[i].addr, regions[i].size);
}

static void restore(void)
{
    for (int i = 0; i < nregions; i++)
        memcpy(g_mem + regions[i].addr, regions[i].copy, regions[i].size);
}

static uint8_t **save_after(void)
{
    uint8_t **out = calloc((size_t)nregions, sizeof *out);
    for (int i = 0; i < nregions; i++) {
        out[i] = malloc(regions[i].size);
        memcpy(out[i], g_mem + regions[i].addr, regions[i].size);
    }
    return out;
}

static uint32_t *reg_of(Cpu *c, const char *name)
{
    static const char *names[] = {"eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"};
    uint32_t *regs[] = {&c->eax, &c->ecx, &c->edx, &c->ebx, &c->esp, &c->ebp, &c->esi, &c->edi};
    for (int i = 0; i < 8; i++)
        if (strcmp(names[i], name) == 0)
            return regs[i];
    return NULL;
}

static uint32_t value_for(const char *kind)
{
    unsigned lo, hi;
    if (strcmp(kind, "ptr") == 0)
        return (rnd() & 1 ? SCRATCH_A : SCRATCH_B) + (rnd() % (SCRATCH_SIZE / 2));
    if (sscanf(kind, "size(%u,%u)", &lo, &hi) == 2)
        return lo + rnd() % (hi - lo + 1);
    if (strcmp(kind, "int") == 0) {
        static const uint32_t edge[] = {0, 1, 2, 0x7f, 0x80, 0xff, 0x7fff, 0x8000, 0xffff,
                                        0x7fffffff, 0x80000000u, 0xffffffffu};
        return rnd() & 1 ? edge[rnd() % 12] : rnd();
    }
    return rnd();
}

static void build_state(Cpu *c, const char *profile, uint32_t stack_top)
{
    uint32_t *regs[] = {&c->eax, &c->ecx, &c->edx, &c->ebx, &c->ebp, &c->esi, &c->edi};
    for (int i = 0; i < 7; i++)
        *regs[i] = rnd();
    c->cf = rnd() & 1; c->pf = rnd() & 1; c->af = rnd() & 1;
    c->zf = rnd() & 1; c->sf = rnd() & 1; c->of = rnd() & 1;
    c->df = 0;
    rt_fpu_init(c);
    c->esp = stack_top - 256;
    for (uint32_t i = 0; i < 64; i++)
        W32(c->esp + 4 * i, rnd());
    for (uint32_t a = SCRATCH_A; a < SCRATCH_A + SCRATCH_SIZE; a += 4)
        W32(a, rnd());
    for (uint32_t a = SCRATCH_B; a < SCRATCH_B + SCRATCH_SIZE; a += 4)
        W32(a, rnd());
    char buf[256];
    snprintf(buf, sizeof buf, "%s", profile ? profile : "");
    for (char *tok = strtok(buf, " "); tok; tok = strtok(NULL, " ")) {
        char *colon = strchr(tok, ':');
        if (!colon)
            continue;
        *colon = 0;
        uint32_t v = value_for(colon + 1);
        uint32_t *r = reg_of(c, tok);
        if (r)
            *r = v;
        else if (strncmp(tok, "arg", 3) == 0)
            W32(c->esp + 4 * (uint32_t)atoi(tok + 3), v);   /* esp+0 is the return slot */
    }
}

static int compare(const ReconEntry *e, Cpu *a, Cpu *b, uint8_t **ma, uint8_t **mb, uint32_t ra_a,
                   uint32_t ra_b)
{
    int bad = 0;
#define CMP(field) if (a->field != b->field) { printf("  %-4s lifted %08x reconstructed %08x\n", #field, a->field, b->field); bad = 1; }
    if (ra_a != ra_b) {
        printf("  returned to %08x vs %08x\n", ra_a, ra_b);
        bad = 1;
    }
    CMP(eax) CMP(ecx) CMP(edx) CMP(ebx) CMP(esp) CMP(ebp) CMP(esi) CMP(edi) CMP(df)
    static const char *fn[] = {"cf", "pf", "af", "zf", "sf", "of"};
    uint32_t fa[] = {a->cf, a->pf, a->af, a->zf, a->sf, a->of};
    uint32_t fb[] = {b->cf, b->pf, b->af, b->zf, b->sf, b->of};
    for (int i = 0; i < 6; i++)
        if ((e->flags_live >> i & 1) && fa[i] != fb[i]) {
            printf("  %s lifted %u reconstructed %u\n", fn[i], fa[i], fb[i]);
            bad = 1;
        }
    if (a->fpu.top != b->fpu.top || memcmp(a->fpu.st, b->fpu.st, sizeof a->fpu.st) != 0) {
        printf("  x87 state differs\n");
        bad = 1;
    }
    for (int i = 0; i < nregions; i++) {
        for (uint32_t k = 0; k < regions[i].size; k++) {
            /* Memory below the final stack pointer is free; pushes leave
             * different garbage there and nobody may read it. */
            if (regions[i].addr + k < a->esp && regions[i].addr + k >= a->esp - STACK_CHECK)
                continue;
            if (ma[i][k] != mb[i][k]) {
                printf("  memory %08x lifted %02x reconstructed %02x\n", regions[i].addr + k, ma[i][k], mb[i][k]);
                bad = 1;
                break;
            }
        }
    }
    return bad;
}

static char *repo_path(const char *rel)
{
    char exe[4096 - 32];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0)
        return NULL;
    exe[n] = 0;
    for (int up = 0; up < 6; up++) {
        char *slash = strrchr(exe, '/');
        if (!slash)
            break;
        *slash = 0;
        char probe[4096];
        snprintf(probe, sizeof probe, "%s/original/app/WET.EXE", exe);
        if (access(probe, R_OK) == 0) {
            char *out = malloc(strlen(exe) + strlen(rel) + 2);
            sprintf(out, "%s/%s", exe, rel);
            return out;
        }
    }
    return NULL;
}

int main(int argc, char **argv)
{
    unsigned iterations = 2000;
    rng = 0x9e3779b97f4a7c15ull;
    uint32_t only[64];
    int nonly = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc)
            iterations = (unsigned)atoi(argv[++i]);
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
            rng = strtoull(argv[++i], NULL, 0) | 1;
        else if (nonly < 64)
            only[nonly++] = (uint32_t)strtoul(argv[i], NULL, 16);
    }
    rt_mem_init();
    char *data = repo_path("original/app");
    char tmp[] = "/tmp/lula-fncheck-XXXXXX";
    if (!data || !mkdtemp(tmp)) {
        fprintf(stderr, "game data not found\n");
        return 2;
    }
    rt_vfs_init(data, tmp);
    char *exe = rt_vfs_resolve_read("WET.EXE");
    if (!exe || !rt_load_image(exe))
        return 2;
    rt_mem_commit(SCRATCH_A, SCRATCH_SIZE);
    rt_mem_commit(SCRATCH_B, SCRATCH_SIZE);
    uint32_t tid;
    Cpu *c = rt_cpu_new_thread(&tid);
    rt_cpu_bind(c);
    rt_gil_acquire();
    add_region(SCRATCH_A, SCRATCH_SIZE);
    add_region(SCRATCH_B, SCRATCH_SIZE);
    add_region(c->stack_base - STACK_CHECK, STACK_CHECK);
    add_region(g_image_base, g_image_size);

    int failures = 0, checked = 0;
    for (size_t i = 0; i < g_reconstructed_count; i++) {
        const ReconEntry *e = &g_reconstructed[i];
        bool wanted = nonly == 0;
        for (int k = 0; k < nonly; k++)
            wanted |= only[k] == e->addr;
        if (!wanted)
            continue;
        checked++;
        unsigned bad = 0;
        for (unsigned it = 0; it < iterations && bad < 3; it++) {
            Cpu start;
            build_state(c, e->profile, c->stack_base);
            start = *c;
            snapshot();
            c->esp -= 4;
            W32(c->esp, MAGIC);
            uint32_t ra_a = e->lifted(c);
            Cpu after_a = *c;
            uint8_t **mem_a = save_after();
            restore();
            *c = start;
            c->esp -= 4;
            W32(c->esp, MAGIC);
            uint32_t ra_b = e->reconstructed(c);
            Cpu after_b = *c;
            uint8_t **mem_b = save_after();
            restore();
            *c = start;
            if (compare(e, &after_a, &after_b, mem_a, mem_b, ra_a, ra_b)) {
                printf("%08x: mismatch in iteration %u (eax=%08x edx=%08x ebx=%08x ecx=%08x)\n", e->addr,
                       it, start.eax, start.edx, start.ebx, start.ecx);
                bad++;
            }
            for (int r = 0; r < nregions; r++) {
                free(mem_a[r]);
                free(mem_b[r]);
            }
            free(mem_a);
            free(mem_b);
        }
        printf("%08x: %s (%u iterations, profile \"%s\")\n", e->addr, bad ? "FAIL" : "ok", iterations,
               e->profile ? e->profile : "");
        failures += bad != 0;
    }
    printf("%d reconstructed functions checked, %d failed\n", checked, failures);
    return failures ? 1 : 0;
}
