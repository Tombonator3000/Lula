/* PE resources of the mapped image (string tables). */
#include "win32.h"

static uint32_t rsrc_base(void)
{
    uint32_t pe = g_image_base + R32(g_image_base + 0x3c);
    uint32_t rva = R32(pe + 24 + 96 + 2 * 8);   /* data directory 2: resources */
    return rva ? g_image_base + rva : 0;
}

/* Find the data entry for (type, id) using the first language. */
static uint32_t find_resource(uint32_t type, uint32_t id, uint32_t *size)
{
    uint32_t base = rsrc_base();
    if (!base)
        return 0;
    uint32_t dir = base;
    const uint32_t want[2] = {type, id};
    for (int level = 0; level < 3; level++) {
        uint32_t count = (uint32_t)R16(dir + 12) + R16(dir + 14);
        uint32_t entries = dir + 16, off = 0;
        bool found = false;
        for (uint32_t i = 0; i < count && !found; i++) {
            uint32_t name = R32(entries + 8 * i);
            if (level == 2 || (!(name & 0x80000000u) && name == want[level])) {
                off = R32(entries + 8 * i + 4);
                found = true;
            }
        }
        if (!found)
            return 0;
        if (off & 0x80000000u) {
            dir = base + (off & 0x7fffffffu);
            continue;
        }
        uint32_t data = base + off;
        *size = R32(data + 4);
        return g_image_base + R32(data);
    }
    return 0;
}

uint32_t res_string(uint32_t id, char *out, uint32_t cap)
{
    uint32_t size = 0;
    uint32_t p = find_resource(6, (id >> 4) + 1, &size);
    if (!p || !cap)
        return 0;
    for (uint32_t i = 0; i < (id & 15); i++)
        p += 2 + 2u * R16(p);
    uint32_t len = R16(p);
    uint32_t n = len < cap - 1 ? len : cap - 1;
    for (uint32_t i = 0; i < n; i++) {
        uint16_t ch = R16(p + 2 + 2 * i);
        out[i] = (char)(ch < 256 ? ch : '?');
    }
    out[n] = 0;
    return n;
}
