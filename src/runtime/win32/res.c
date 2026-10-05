/* PE resources of the mapped image (string tables and dialog templates). */
#include "win32.h"
#include "dialog.h"

#include <ctype.h>

static uint32_t rsrc_base(void)
{
    uint32_t pe = g_image_base + R32(g_image_base + 0x3c);
    uint32_t rva = R32(pe + 24 + 96 + 2 * 8);   /* data directory 2: resources */
    return rva ? g_image_base + rva : 0;
}

/* Does a directory entry name (IMAGE_RESOURCE_DIR_STRING_U) equal name? */
static bool name_equals(uint32_t base, uint32_t entry_name, const char *name)
{
    uint32_t p = base + (entry_name & 0x7fffffffu);
    uint32_t len = R16(p);
    if (strlen(name) != len)
        return false;
    for (uint32_t i = 0; i < len; i++) {
        uint16_t ch = R16(p + 2 + 2 * i);
        if (ch > 0x7f || toupper(ch) != toupper((unsigned char)name[i]))
            return false;
    }
    return true;
}

/* Find the data entry for (type, name) using the first language. A NULL
 * name_str looks up the integer id instead. */
static uint32_t find_resource_ex(uint32_t type, uint32_t id, const char *name_str, uint32_t *size)
{
    uint32_t base = rsrc_base();
    if (!base)
        return 0;
    uint32_t dir = base;
    for (int level = 0; level < 3; level++) {
        uint32_t count = (uint32_t)R16(dir + 12) + R16(dir + 14);
        uint32_t entries = dir + 16, off = 0;
        bool found = false;
        for (uint32_t i = 0; i < count && !found; i++) {
            uint32_t name = R32(entries + 8 * i);
            bool match;
            if (level == 2)
                match = true;
            else if (level == 1 && name_str)
                match = (name & 0x80000000u) && name_equals(base, name, name_str);
            else
                match = !(name & 0x80000000u) && name == (level == 0 ? type : id);
            if (match) {
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

static uint32_t find_resource(uint32_t type, uint32_t id, uint32_t *size)
{
    return find_resource_ex(type, id, NULL, size);
}

/* Look up a resource by a guest LPCSTR name: MAKEINTRESOURCE, "#123" or a
 * string. Returns the guest address of the data (0 if missing). */
uint32_t res_find(uint32_t type, uint32_t name, uint32_t *size)
{
    *size = 0;
    if (name < 0x10000)
        return find_resource(type, name, size);
    const char *s = gstr(name);
    if (s[0] == '#')
        return find_resource(type, (uint32_t)strtoul(s + 1, NULL, 10), size);
    return find_resource_ex(type, 0, s, size);
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
