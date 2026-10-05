/* Guest path mapping.
 *
 * The game sees itself installed in C:\LULA. Reads look in the save overlay
 * first and then in the read-only game data (original/app); writes always go
 * to the overlay, so the original files are never modified. Lookups are
 * case-insensitive like on Windows. Any other drive letter (the CD-ROM) maps
 * to the same tree, because the repack keeps the CD files in the game folder.
 */
#include "rt.h"

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static char *data_dir, *save_dir;
static const char game_dir_dos[] = "C:\\LULA";

const char *rt_vfs_game_dir_dos(void) { return game_dir_dos; }
const char *rt_vfs_data_dir(void) { return data_dir; }
const char *rt_vfs_save_dir(void) { return save_dir; }

static void mkdirs(const char *path)
{
    char tmp[4096];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

void rt_vfs_init(const char *data, const char *save)
{
    data_dir = realpath(data, NULL);
    if (!data_dir)
        rt_fatal("game data directory %s not found", data);
    mkdirs(save);
    save_dir = realpath(save, NULL);
    if (!save_dir)
        rt_fatal("cannot create save directory %s", save);
    RT_INFO("game data: %s (read-only), saves and settings: %s", data_dir, save_dir);
}

/* Normalise a guest path to a relative path with '/' separators.
 * Returns false when the path escapes the game directory. */
static bool guest_relative(const char *guest, char *out, size_t cap)
{
    char tmp[1024];
    const char *p = guest;
    if (strlen(guest) >= sizeof tmp)
        return false;
    if (((p[0] | 0x20) >= 'a' && (p[0] | 0x20) <= 'z') && p[1] == ':') {
        /* Absolute: drop C:\LULA (or just the drive for other drives). */
        size_t n = strlen(game_dir_dos);
        if (strncasecmp(p, game_dir_dos, n) == 0 && (p[n] == '\\' || p[n] == '/' || p[n] == 0))
            p += n;
        else
            p += 2;
    }
    snprintf(tmp, sizeof tmp, "%s", p);
    for (char *q = tmp; *q; q++)
        if (*q == '\\')
            *q = '/';
    /* Resolve "." and ".." component by component. */
    char *parts[128];
    int np = 0;
    for (char *tok = strtok(tmp, "/"); tok; tok = strtok(NULL, "/")) {
        if (strcmp(tok, ".") == 0 || !*tok)
            continue;
        if (strcmp(tok, "..") == 0) {
            if (np == 0)
                return false;
            np--;
            continue;
        }
        if (np == 128)
            return false;
        parts[np++] = tok;
    }
    out[0] = 0;
    size_t len = 0;
    for (int i = 0; i < np; i++) {
        int w = snprintf(out + len, cap - len, "%s%s", i ? "/" : "", parts[i]);
        if (w < 0 || (size_t)w >= cap - len)
            return false;
        len += (size_t)w;
    }
    return true;
}

/* Find rel under root case-insensitively. Returns malloc'd path or NULL. */
static char *find_ci(const char *root, const char *rel, bool want_parent_only)
{
    char cur[4096];
    snprintf(cur, sizeof cur, "%s", root);
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s", rel);
    char *save = NULL;
    char *tok = strtok_r(tmp, "/", &save);
    while (tok) {
        char *next = strtok_r(NULL, "/", &save);
        if (!next && want_parent_only) {
            size_t n = strlen(cur);
            snprintf(cur + n, sizeof cur - n, "/%s", tok);
            return strdup(cur);
        }
        DIR *d = opendir(cur);
        if (!d)
            return NULL;
        struct dirent *e;
        bool found = false;
        while ((e = readdir(d))) {
            if (strcasecmp(e->d_name, tok) == 0) {
                size_t n = strlen(cur);
                snprintf(cur + n, sizeof cur - n, "/%s", e->d_name);
                found = true;
                break;
            }
        }
        closedir(d);
        if (!found)
            return NULL;
        tok = next;
    }
    return strdup(cur);
}

char *rt_vfs_resolve_read(const char *guest)
{
    char rel[1024];
    if (!guest_relative(guest, rel, sizeof rel))
        return NULL;
    if (!*rel)
        return strdup(save_dir);
    char *p = find_ci(save_dir, rel, false);
    if (!p)
        p = find_ci(data_dir, rel, false);
    return p;
}

static bool copy_file(const char *from, const char *to)
{
    FILE *in = fopen(from, "rb");
    if (!in)
        return false;
    FILE *out = fopen(to, "wb");
    if (!out) {
        fclose(in);
        return false;
    }
    char buf[65536];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0)
        ok &= fwrite(buf, 1, n, out) == n;
    fclose(in);
    ok &= fclose(out) == 0;
    return ok;
}

char *rt_vfs_resolve_write(const char *guest, bool copy_existing)
{
    char rel[1024];
    if (!guest_relative(guest, rel, sizeof rel) || !*rel)
        return NULL;
    char *existing = find_ci(save_dir, rel, false);
    if (existing)
        return existing;
    /* Reuse the case of existing overlay directories, create the rest. */
    char dir_rel[1024];
    snprintf(dir_rel, sizeof dir_rel, "%s", rel);
    char *slash = strrchr(dir_rel, '/');
    const char *leaf = rel;
    char *dir = NULL;
    if (slash) {
        *slash = 0;
        leaf = slash + 1;
        dir = find_ci(save_dir, dir_rel, false);
        if (!dir) {
            /* Take the spelling from the game data if that directory exists. */
            char *orig = find_ci(data_dir, dir_rel, false);
            char made[4096];
            if (orig)
                snprintf(made, sizeof made, "%s%s", save_dir, orig + strlen(data_dir));
            else
                snprintf(made, sizeof made, "%s/%s", save_dir, dir_rel);
            free(orig);
            mkdirs(made);
            dir = strdup(made);
        }
    } else {
        dir = strdup(save_dir);
    }
    char *path = malloc(strlen(dir) + strlen(leaf) + 2);
    sprintf(path, "%s/%s", dir, leaf);
    free(dir);
    if (copy_existing) {
        char *orig = find_ci(data_dir, rel, false);
        if (orig) {
            if (!copy_file(orig, path))
                RT_WARN("copy-on-write of %s failed", orig);
            free(orig);
        }
    }
    return path;
}
