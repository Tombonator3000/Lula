/* Map WET.EXE into guest memory the way the Windows loader would, then point
 * every import slot at the matching host function. The generated code was
 * produced from one exact file; the SHA-256 check refuses any other. */
#include "rt.h"

#include <stdlib.h>

/* ---- SHA-256 (FIPS 180-4) ---- */
typedef struct { uint32_t h[8]; uint64_t len; uint8_t buf[64]; size_t n; } Sha256;

static uint32_t ror32(uint32_t x, int r) { return (x >> r) | (x << (32 - r)); }

static void sha_block(Sha256 *s, const uint8_t *p)
{
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t w[64], a[8];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ror32(w[i - 15], 7) ^ ror32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ror32(w[i - 2], 17) ^ ror32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    memcpy(a, s->h, sizeof a);
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = a[7] + (ror32(a[4], 6) ^ ror32(a[4], 11) ^ ror32(a[4], 25)) +
                      ((a[4] & a[5]) ^ (~a[4] & a[6])) + k[i] + w[i];
        uint32_t t2 = (ror32(a[0], 2) ^ ror32(a[0], 13) ^ ror32(a[0], 22)) +
                      ((a[0] & a[1]) ^ (a[0] & a[2]) ^ (a[1] & a[2]));
        memmove(a + 1, a, 7 * sizeof *a);
        a[4] += t1;
        a[0] = t1 + t2;
    }
    for (int i = 0; i < 8; i++)
        s->h[i] += a[i];
}

static void sha256_hex(const uint8_t *data, size_t len, char out[65])
{
    Sha256 s = {{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c,
                 0x1f83d9ab, 0x5be0cd19}, 0, {0}, 0};
    size_t i = 0;
    for (; i + 64 <= len; i += 64)
        sha_block(&s, data + i);
    uint8_t tail[128] = {0};
    size_t rest = len - i;
    memcpy(tail, data + i, rest);
    tail[rest] = 0x80;
    size_t total = rest + 9 <= 64 ? 64 : 128;
    uint64_t bits = (uint64_t)len * 8;
    for (int b = 0; b < 8; b++)
        tail[total - 1 - b] = (uint8_t)(bits >> (8 * b));
    sha_block(&s, tail);
    if (total == 128)
        sha_block(&s, tail + 64);
    for (int b = 0; b < 8; b++)
        snprintf(out + 8 * b, 9, "%08x", s.h[b]);
}

/* ---- PE mapping ---- */
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

bool rt_load_image(const char *exe_path)
{
    FILE *f = fopen(exe_path, "rb");
    if (!f) {
        RT_WARN("cannot open %s", exe_path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc((size_t)size);
    if (!data || fread(data, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(data);
        return false;
    }
    fclose(f);

    char hex[65];
    sha256_hex(data, (size_t)size, hex);
    if (strcmp(hex, g_image_sha256) != 0) {
        RT_WARN("%s has SHA-256 %s, but this build was recompiled from %s", exe_path, hex,
                g_image_sha256);
        free(data);
        return false;
    }

    uint32_t pe = le32(data + 0x3c);
    uint16_t nsec = le16(data + pe + 6);
    uint16_t optsize = le16(data + pe + 20);
    const uint8_t *opt = data + pe + 24;
    uint32_t base = le32(opt + 28), image_size = le32(opt + 56), header_size = le32(opt + 60);
    if (base != g_image_base || image_size != g_image_size)
        rt_fatal("unexpected PE layout");
    if (!rt_mem_commit(base, image_size))
        rt_fatal("cannot commit image memory");
    memset(g_mem + base, 0, image_size);
    memcpy(g_mem + base, data, header_size);
    const uint8_t *sec = opt + optsize;
    for (int i = 0; i < nsec; i++, sec += 40) {
        uint32_t va = le32(sec + 12), raw_size = le32(sec + 16), raw_off = le32(sec + 20);
        if (raw_off && raw_size)
            memcpy(g_mem + base + va, data + raw_off, raw_size);
    }
    free(data);

    for (size_t i = 0; i < g_host_import_count; i++) {
        const HostImport *imp = &g_host_imports[i];
        char *name = malloc(strlen(imp->dll) + strlen(imp->name) + 2);
        sprintf(name, "%s.%s", imp->dll, imp->name);
        W32(imp->slot, rt_register_thunk(name, imp->fn));
    }
    RT_INFO("mapped %s at %08x (%u bytes), %zu imports, %zu recompiled functions", exe_path,
            base, image_size, g_host_import_count, g_guest_entry_count);
    return true;
}

/* ---- command line ---- */
static uint32_t cmdline_addr;

void rt_set_command_line(const char *args)
{
    char buf[1024];
    snprintf(buf, sizeof buf, "%s\\WET.EXE%s%s", rt_vfs_game_dir_dos(), args && *args ? " " : "",
             args ? args : "");
    cmdline_addr = rt_guest_strdup(buf);
}

uint32_t rt_command_line(void)
{
    if (!cmdline_addr)
        rt_set_command_line("");
    return cmdline_addr;
}
