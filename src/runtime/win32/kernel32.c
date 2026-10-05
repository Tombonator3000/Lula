/* KERNEL32 on POSIX: files through the VFS overlay, memory through the guest
 * arena, threads and waits through pthreads with the global lock released
 * while blocked. */
#include "win32.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------ errors/misc */
WINAPI_FN(kernel32, GetLastError) { RET(0, c->last_error); }

WINAPI_FN(kernel32, GetCommandLineA) { RET(0, rt_command_line()); }

WINAPI_FN(kernel32, GetVersion)
{
    /* Windows 98 (4.10, build 1998): bit 31 marks the Windows 9x family. */
    RET(0, 0xc7ce0a04u);
}

WINAPI_FN(kernel32, GetCurrentProcessId) { RET(0, 0x1234u); }
WINAPI_FN(kernel32, GetCurrentThreadId) { RET(0, c->thread_id); }
WINAPI_FN(kernel32, GetCurrentThread) { RET(0, 0xfffffffeu); }

WINAPI_FN(kernel32, GetTickCount)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    static uint64_t start;
    uint64_t ms = (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
    if (!start)
        start = ms - 60000;   /* pretend the machine has been up for a minute */
    RET(0, (uint32_t)(ms - start));
}

WINAPI_FN(kernel32, GetLocalTime)
{
    uint32_t st = ARG(0);
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm;
    localtime_r(&ts.tv_sec, &tm);
    W16(st + 0, (uint16_t)(tm.tm_year + 1900));
    W16(st + 2, (uint16_t)(tm.tm_mon + 1));
    W16(st + 4, (uint16_t)tm.tm_wday);
    W16(st + 6, (uint16_t)tm.tm_mday);
    W16(st + 8, (uint16_t)tm.tm_hour);
    W16(st + 10, (uint16_t)tm.tm_min);
    W16(st + 12, (uint16_t)tm.tm_sec);
    W16(st + 14, (uint16_t)(ts.tv_nsec / 1000000));
    RET(1, 0);
}

WINAPI_FN(kernel32, GetTimeZoneInformation)
{
    uint32_t tzi = ARG(0);
    if (tzi)
        memset(g_mem + tzi, 0, 172);
    RET(1, 0);   /* TIME_ZONE_ID_UNKNOWN */
}

WINAPI_FN(kernel32, GlobalMemoryStatus)
{
    uint32_t ms = ARG(0);
    W32(ms + 0, 32);
    W32(ms + 4, 20);                    /* load percent */
    W32(ms + 8, 256u << 20);            /* total physical */
    W32(ms + 12, 192u << 20);           /* available physical */
    W32(ms + 16, 512u << 20);
    W32(ms + 20, 448u << 20);
    W32(ms + 24, 2047u << 20);
    W32(ms + 28, 1536u << 20);
    RET(1, 0);
}

WINAPI_FN(kernel32, MultiByteToWideChar)
{
    uint32_t src = ARG(2), dst = ARG(4);
    int32_t len = (int32_t)ARG(3), cap = (int32_t)ARG(5);
    if (len < 0)
        len = (int32_t)strlen(gstr(src)) + 1;
    if (cap == 0)
        RET(6, (uint32_t)len);
    int32_t n = len < cap ? len : cap;
    for (int32_t i = 0; i < n; i++)
        W16(dst + 2u * (uint32_t)i, R8(src + (uint32_t)i));   /* Latin-1 */
    RET(6, (uint32_t)n);
}

/* ------------------------------------------------------------ modules */
WINAPI_FN(kernel32, GetModuleHandleA)
{
    const char *name = gstr(ARG(0));
    uint32_t h = HMODULE_EXE;
    if (name) {
        if (strncasecmp(name, "kernel32", 8) == 0) h = HMODULE_KERNEL32;
        else if (strncasecmp(name, "user32", 6) == 0) h = HMODULE_USER32;
        else if (strncasecmp(name, "gdi32", 5) == 0) h = HMODULE_GDI32;
        else if (strncasecmp(name, "winmm", 5) == 0) h = HMODULE_WINMM;
        else if (strncasecmp(name, "ddraw", 5) == 0) h = HMODULE_DDRAW;
        else if (strncasecmp(name, "dsound", 6) == 0) h = HMODULE_DSOUND;
        else if (strncasecmp(name, "wet", 3) == 0) h = HMODULE_EXE;
        else {
            RT_WARN("GetModuleHandleA(%s): unknown module", name);
            h = 0;
        }
    }
    RET(1, h);
}

WINAPI_FN(kernel32, LoadLibraryA)
{
    const char *name = gstr(ARG(0));
    RT_INFO("LoadLibraryA(%s)", name ? name : "(null)");
    /* Same answer as GetModuleHandleA: every DLL the game uses is built in. */
    return h_kernel32_GetModuleHandleA(c);
}

static const char *module_name(uint32_t h)
{
    switch (h) {
    case HMODULE_KERNEL32: return "kernel32";
    case HMODULE_USER32: return "user32";
    case HMODULE_GDI32: return "gdi32";
    case HMODULE_WINMM: return "winmm";
    case HMODULE_DDRAW: return "ddraw";
    case HMODULE_DSOUND: return "dsound";
    case HMODULE_OLE32: return "ole32";
    case HMODULE_COMDLG32: return "comdlg32";
    default: return NULL;
    }
}

/* Host functions reachable through GetProcAddress but not imported. */
uint32_t h_kernel32_Sleep(Cpu *c);
uint32_t h_winmm_timeGetTime(Cpu *c);
static const struct { const char *dll, *name; GuestFn fn; } extra_exports[] = {
    {"kernel32", "Sleep", h_kernel32_Sleep},
    {"winmm", "timeGetTime", h_winmm_timeGetTime},
};

uint32_t win_proc_address(const char *dll, const char *name)
{
    for (size_t i = 0; i < g_host_import_count; i++)
        if (strcasecmp(g_host_imports[i].dll, dll) == 0 && strcmp(g_host_imports[i].name, name) == 0)
            return R32(g_host_imports[i].slot);
    for (size_t i = 0; i < sizeof extra_exports / sizeof *extra_exports; i++)
        if (strcasecmp(extra_exports[i].dll, dll) == 0 && strcmp(extra_exports[i].name, name) == 0) {
            char *full = malloc(strlen(dll) + strlen(name) + 2);
            sprintf(full, "%s.%s", dll, name);
            return rt_register_thunk(full, extra_exports[i].fn);
        }
    return 0;
}

WINAPI_FN(kernel32, GetProcAddress)
{
    const char *dll = module_name(ARG(0));
    uint32_t pname = ARG(1);
    uint32_t addr = 0;
    if (dll && pname > 0xffff)
        addr = win_proc_address(dll, gstr(pname));
    if (!addr)
        RT_WARN("GetProcAddress(%s, %s) not available", dll ? dll : "?",
                pname > 0xffff ? gstr(pname) : "#ordinal");
    RET(2, addr);
}

WINAPI_FN(kernel32, GetModuleFileNameA)
{
    char path[256];
    snprintf(path, sizeof path, "%s\\WET.EXE", rt_vfs_game_dir_dos());
    RET(3, put_gstr(ARG(1), ARG(2), path));
}

WINAPI_FN(kernel32, GetCurrentDirectoryA)
{
    const char *dir = rt_vfs_game_dir_dos();
    uint32_t len = ARG(0), buf = ARG(1);
    if (len <= strlen(dir))
        RET(2, strlen(dir) + 1);
    RET(2, put_gstr(buf, len, dir));
}

/* ------------------------------------------------------------ environment */
WINAPI_FN(kernel32, GetEnvironmentStrings)
{
    static uint32_t block;
    if (!block) {
        static const char env[] = "COMSPEC=C:\\WINDOWS\\COMMAND.COM\0PATH=C:\\WINDOWS;C:\\WINDOWS\\COMMAND\0"
                                  "TEMP=C:\\WINDOWS\\TEMP\0TMP=C:\\WINDOWS\\TEMP\0windir=C:\\WINDOWS\0\0";
        block = rt_low_alloc(sizeof env);
        memcpy(g_mem + block, env, sizeof env);
    }
    RET(0, block);
}

/* ------------------------------------------------------------ console (GUI process: none) */
/* Pseudo handles for the Watcom runtime's stdout/stderr, so its fatal error
 * messages reach the host terminal. */
#define STD_IN_HANDLE 0xf0u
#define STD_OUT_HANDLE 0xf1u
#define STD_ERR_HANDLE 0xf2u

WINAPI_FN(kernel32, GetStdHandle)
{
    int32_t which = (int32_t)ARG(0);
    RET(1, which == -10 ? STD_IN_HANDLE : which == -11 ? STD_OUT_HANDLE : which == -12 ? STD_ERR_HANDLE : 0);
}
WINAPI_FN(kernel32, SetStdHandle) { RET(2, WIN_TRUE); }
WINAPI_FN(kernel32, GetConsoleMode) { win_set_error(c, ERROR_INVALID_HANDLE); RET(2, WIN_FALSE); }
WINAPI_FN(kernel32, SetConsoleMode) { win_set_error(c, ERROR_INVALID_HANDLE); RET(2, WIN_FALSE); }
WINAPI_FN(kernel32, SetConsoleCtrlHandler) { RET(2, WIN_TRUE); }
WINAPI_FN(kernel32, PeekConsoleInputA) { win_set_error(c, ERROR_INVALID_HANDLE); RET(4, WIN_FALSE); }
WINAPI_FN(kernel32, ReadConsoleInputA) { win_set_error(c, ERROR_INVALID_HANDLE); RET(4, WIN_FALSE); }

WINAPI_FN(kernel32, WriteConsoleA)
{
    uint32_t buf = ARG(1), n = ARG(2), written = ARG(3);
    fwrite(g_mem + buf, 1, n, stderr);
    if (written)
        W32(written, n);
    RET(5, WIN_TRUE);
}

/* ------------------------------------------------------------ files */
typedef struct FileObj { int fd; char *path; bool console; } FileObj;

#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u

WINAPI_FN(kernel32, CreateFileA)
{
    const char *name = gstr(ARG(0));
    uint32_t access = ARG(1), disp = ARG(4);
    bool want_write = (access & GENERIC_WRITE) != 0;
    char *host = NULL;
    if (!name) {
        win_set_error(c, ERROR_INVALID_PARAMETER);
        RET(7, INVALID_HANDLE_VALUE);
    }
    char *existing = rt_vfs_resolve_read(name);
    struct stat st;
    bool exists = existing && stat(existing, &st) == 0 && S_ISREG(st.st_mode);
    if (existing && stat(existing, &st) == 0 && S_ISDIR(st.st_mode)) {
        free(existing);
        win_set_error(c, ERROR_ACCESS_DENIED);
        RET(7, INVALID_HANDLE_VALUE);
    }
    int flags = want_write ? ((access & GENERIC_READ) ? O_RDWR : O_WRONLY) : O_RDONLY;
    switch (disp) {
    case 1: /* CREATE_NEW */
        if (exists) { free(existing); win_set_error(c, ERROR_FILE_EXISTS); RET(7, INVALID_HANDLE_VALUE); }
        flags |= O_CREAT | O_EXCL;
        break;
    case 2: /* CREATE_ALWAYS */
        flags |= O_CREAT | O_TRUNC;
        break;
    case 3: /* OPEN_EXISTING */
        if (!exists) { free(existing); win_set_error(c, ERROR_FILE_NOT_FOUND); RET(7, INVALID_HANDLE_VALUE); }
        break;
    case 4: /* OPEN_ALWAYS */
        flags |= O_CREAT;
        break;
    case 5: /* TRUNCATE_EXISTING */
        if (!exists) { free(existing); win_set_error(c, ERROR_FILE_NOT_FOUND); RET(7, INVALID_HANDLE_VALUE); }
        flags |= O_TRUNC;
        break;
    default:
        free(existing);
        win_set_error(c, ERROR_INVALID_PARAMETER);
        RET(7, INVALID_HANDLE_VALUE);
    }
    if (want_write || (flags & O_CREAT)) {
        /* Writes never touch the original data: open the overlay copy. */
        bool keep = (disp == 3 || disp == 4) && exists;
        host = rt_vfs_resolve_write(name, keep);
        if (flags & O_CREAT && !want_write)
            flags = (flags & ~O_ACCMODE) | O_RDWR;
    } else {
        host = existing;
        existing = NULL;
    }
    free(existing);
    if (!host) {
        win_set_error(c, ERROR_PATH_NOT_FOUND);
        RET(7, INVALID_HANDLE_VALUE);
    }
    int fd = open(host, flags, 0644);
    if (fd < 0) {
        win_set_error(c, errno == ENOENT ? ERROR_FILE_NOT_FOUND : ERROR_ACCESS_DENIED);
        RT_INFO("CreateFileA(%s) -> %s failed: %s", name, host, strerror(errno));
        free(host);
        RET(7, INVALID_HANDLE_VALUE);
    }
    FileObj *fo = calloc(1, sizeof *fo);
    fo->fd = fd;
    fo->path = host;
    uint32_t h = rt_handle_new(H_FILE, fo);
    win_set_error(c, (disp == 2 || disp == 4) && exists ? ERROR_ALREADY_EXISTS : 0);
    RT_INFO("CreateFileA(%s, %s) -> %08x", name, want_write ? "write" : "read", h);
    RET(7, h);
}

static FileObj *file_of(uint32_t h)
{
    HandleObj *o = rt_handle_get(h, H_FILE);
    return o ? o->ptr : NULL;
}

WINAPI_FN(kernel32, ReadFile)
{
    FileObj *fo = file_of(ARG(0));
    uint32_t buf = ARG(1), n = ARG(2), pread = ARG(3);
    if (!fo) {
        win_set_error(c, ERROR_INVALID_HANDLE);
        RET(5, WIN_FALSE);
    }
    uint32_t total = 0;
    while (total < n) {
        ssize_t r = read(fo->fd, g_mem + buf + total, n - total);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            break;
        total += (uint32_t)r;
    }
    if (pread)
        W32(pread, total);
    RT_TRACE("ReadFile(%s, %u) -> %u", fo->path, n, total);
    RET(5, WIN_TRUE);
}

WINAPI_FN(kernel32, WriteFile)
{
    uint32_t h = ARG(0), buf = ARG(1), n = ARG(2), pwritten = ARG(3);
    FileObj *fo = file_of(h);
    if (h == STD_OUT_HANDLE || h == STD_ERR_HANDLE) {
        fwrite(g_mem + buf, 1, n, stderr);
        if (pwritten)
            W32(pwritten, n);
        RET(5, WIN_TRUE);
    }
    if (!fo) {
        win_set_error(c, ERROR_INVALID_HANDLE);
        RET(5, WIN_FALSE);
    }
    ssize_t w = write(fo->fd, g_mem + buf, n);
    if (pwritten)
        W32(pwritten, w > 0 ? (uint32_t)w : 0);
    RET(5, w == (ssize_t)n ? WIN_TRUE : WIN_FALSE);
}

WINAPI_FN(kernel32, SetFilePointer)
{
    FileObj *fo = file_of(ARG(0));
    int64_t dist = (int32_t)ARG(1);
    uint32_t phigh = ARG(2), method = ARG(3);
    if (!fo) {
        win_set_error(c, ERROR_INVALID_HANDLE);
        RET(4, 0xffffffffu);
    }
    if (phigh)
        dist = (int64_t)(((uint64_t)R32(phigh) << 32) | (uint32_t)ARG(1));
    int whence = method == 0 ? SEEK_SET : method == 1 ? SEEK_CUR : SEEK_END;
    off_t pos = lseek(fo->fd, (off_t)dist, whence);
    if (pos < 0) {
        win_set_error(c, ERROR_INVALID_PARAMETER);
        RET(4, 0xffffffffu);
    }
    if (phigh)
        W32(phigh, (uint32_t)((uint64_t)pos >> 32));
    RT_TRACE("SetFilePointer(%s, %lld, %u) -> %lld", fo->path, (long long)dist, method, (long long)pos);
    win_set_error(c, 0);
    RET(4, (uint32_t)pos);
}

WINAPI_FN(kernel32, GetFileType)
{
    uint32_t h = ARG(0);
    if (h == STD_IN_HANDLE || h == STD_OUT_HANDLE || h == STD_ERR_HANDLE)
        RET(1, 2u);   /* FILE_TYPE_CHAR */
    RET(1, file_of(h) ? 1u /* FILE_TYPE_DISK */ : 0u);
}

/* Close any kernel object. */
typedef struct SyncObj {
    pthread_mutex_t m;
    pthread_cond_t cv;
    bool manual, signaled;
    uint32_t owner, count;       /* mutex owner thread and recursion */
    bool is_mutex, is_thread;
} SyncObj;

WINAPI_FN(kernel32, CloseHandle)
{
    uint32_t h = ARG(0);
    HandleObj *o = rt_handle_get(h, H_FREE);
    if (!o) {
        win_set_error(c, ERROR_INVALID_HANDLE);
        RET(1, WIN_FALSE);
    }
    if (o->kind == H_FILE) {
        FileObj *fo = o->ptr;
        close(fo->fd);
        free(fo->path);
        free(fo);
    }
    /* Synchronisation objects stay allocated: another thread may still wait. */
    rt_handle_close(h);
    RET(1, WIN_TRUE);
}

WINAPI_FN(kernel32, DeleteFileA)
{
    const char *name = gstr(ARG(0));
    char *p = rt_vfs_resolve_read(name);
    bool ok = false;
    if (p && strncmp(p, rt_vfs_save_dir(), strlen(rt_vfs_save_dir())) == 0)
        ok = unlink(p) == 0;
    else if (p)
        RT_WARN("DeleteFileA(%s): file is part of the read-only game data; kept", name);
    free(p);
    win_set_error(c, ok ? 0 : ERROR_FILE_NOT_FOUND);
    RET(1, ok ? WIN_TRUE : WIN_FALSE);
}

WINAPI_FN(kernel32, MoveFileA)
{
    const char *from = gstr(ARG(0)), *to = gstr(ARG(1));
    char *src = rt_vfs_resolve_write(from, true);
    char *dst = rt_vfs_resolve_write(to, false);
    bool ok = src && dst && rename(src, dst) == 0;
    free(src);
    free(dst);
    win_set_error(c, ok ? 0 : ERROR_FILE_NOT_FOUND);
    RET(2, ok ? WIN_TRUE : WIN_FALSE);
}

/* ------------------------------------------------------------ INI files */
static char *ini_path(const char *file, bool write)
{
    char guest[512];
    if (strchr(file, '\\') || strchr(file, '/') || strchr(file, ':'))
        snprintf(guest, sizeof guest, "%s", file);
    else
        snprintf(guest, sizeof guest, "C:\\WINDOWS\\%s", file);
    return write ? rt_vfs_resolve_write(guest, true) : rt_vfs_resolve_read(guest);
}

static char *slurp(const char *path, size_t *len)
{
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f) {
        *len = 0;
        return calloc(1, 1);
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    *len = fread(buf, 1, (size_t)n, f);
    buf[*len] = 0;
    fclose(f);
    return buf;
}

static void trim(char *s)
{
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1]))
        *--e = 0;
    char *b = s;
    while (isspace((unsigned char)*b))
        b++;
    memmove(s, b, strlen(b) + 1);
}

uint32_t win_ini_get(const char *file, const char *section, const char *key,
                     const char *def, char *out, uint32_t cap)
{
    char *path = ini_path(file, false);
    size_t len;
    char *text = slurp(path, &len);
    free(path);
    bool in = false;
    uint32_t result = UINT32_MAX;
    for (char *line = strtok(text, "\r\n"); line; line = strtok(NULL, "\r\n")) {
        char tmp[1024];
        snprintf(tmp, sizeof tmp, "%s", line);
        trim(tmp);
        if (tmp[0] == '[') {
            char *end = strchr(tmp, ']');
            if (end)
                *end = 0;
            in = strcasecmp(tmp + 1, section) == 0;
            continue;
        }
        char *eq = strchr(tmp, '=');
        if (!in || !eq)
            continue;
        *eq = 0;
        trim(tmp);
        if (strcasecmp(tmp, key) == 0) {
            char *v = eq + 1;
            trim(v);
            snprintf(out, cap, "%s", v);
            result = (uint32_t)strlen(out);
            break;
        }
    }
    free(text);
    if (result == UINT32_MAX) {
        snprintf(out, cap, "%s", def ? def : "");
        result = (uint32_t)strlen(out);
    }
    return result;
}

bool win_ini_set(const char *file, const char *section, const char *key, const char *value)
{
    char *path = ini_path(file, true);
    if (!path)
        return false;
    size_t len;
    char *text = slurp(path, &len);
    size_t cap = len + strlen(section) + strlen(key) + (value ? strlen(value) : 0) + 64;
    char *out = malloc(cap);
    size_t o = 0;
    bool in = false, done = false, seen_section = false;
    char *save = NULL;
    for (char *line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char tmp[1024];
        snprintf(tmp, sizeof tmp, "%s", line);
        trim(tmp);
        if (tmp[0] == '[') {
            if (in && !done && value) {
                o += (size_t)snprintf(out + o, cap - o, "%s=%s\r\n", key, value);
                done = true;
            }
            char name[1024];
            snprintf(name, sizeof name, "%s", tmp + 1);
            char *end = strchr(name, ']');
            if (end)
                *end = 0;
            in = strcasecmp(name, section) == 0;
            seen_section |= in;
        } else if (in && !done) {
            char *eq = strchr(tmp, '=');
            if (eq) {
                *eq = 0;
                trim(tmp);
                if (strcasecmp(tmp, key) == 0) {
                    if (value)
                        o += (size_t)snprintf(out + o, cap - o, "%s=%s\r\n", key, value);
                    done = true;
                    continue;
                }
            }
        }
        size_t n = strlen(line);
        if (o + n + 3 > cap) {
            cap = (o + n + 3) * 2;
            out = realloc(out, cap);
        }
        memcpy(out + o, line, n);
        o += n;
        if (!n || line[n - 1] != '\r')
            out[o++] = '\r';
        out[o++] = '\n';
    }
    if (!done && value) {
        if (o + strlen(section) + strlen(key) + strlen(value) + 16 > cap) {
            cap = o + strlen(section) + strlen(key) + strlen(value) + 16;
            out = realloc(out, cap);
        }
        if (!seen_section)
            o += (size_t)snprintf(out + o, cap - o, "[%s]\r\n", section);
        o += (size_t)snprintf(out + o, cap - o, "%s=%s\r\n", key, value);
    }
    FILE *f = fopen(path, "wb");
    bool ok = f && fwrite(out, 1, o, f) == o;
    if (f)
        fclose(f);
    free(out);
    free(text);
    free(path);
    return ok;
}

WINAPI_FN(kernel32, GetPrivateProfileStringA)
{
    const char *sec = gstr(ARG(0)), *key = gstr(ARG(1)), *def = gstr(ARG(2));
    uint32_t buf = ARG(3), size = ARG(4);
    const char *file = gstr(ARG(5));
    if (!sec || !key || !file) {
        /* Section/key enumeration is not used by the game. */
        RT_WARN("GetPrivateProfileStringA enumeration not supported");
        if (size >= 2) { W8(buf, 0); W8(buf + 1, 0); }
        RET(6, 0);
    }
    char tmp[1024];
    uint32_t n = win_ini_get(file, sec, key, def, tmp, sizeof tmp);
    (void)n;
    RET(6, put_gstr(buf, size, tmp));
}

WINAPI_FN(kernel32, GetPrivateProfileIntA)
{
    const char *sec = gstr(ARG(0)), *key = gstr(ARG(1)), *file = gstr(ARG(3));
    int32_t def = (int32_t)ARG(2);
    char tmp[64];
    char defs[32];
    snprintf(defs, sizeof defs, "%d", def);
    win_ini_get(file, sec, key, defs, tmp, sizeof tmp);
    RET(4, (uint32_t)strtol(tmp, NULL, 10));
}

WINAPI_FN(kernel32, WritePrivateProfileStringA)
{
    const char *sec = gstr(ARG(0)), *key = gstr(ARG(1)), *val = gstr(ARG(2)), *file = gstr(ARG(3));
    bool ok = sec && key && file && win_ini_set(file, sec, key, val);
    RET(4, ok ? WIN_TRUE : WIN_FALSE);
}

/* ------------------------------------------------------------ memory */
WINAPI_FN(kernel32, VirtualAlloc)
{
    uint32_t a = rt_valloc(ARG(0), ARG(1), ARG(2), ARG(3));
    if (!a)
        win_set_error(c, ERROR_NOT_ENOUGH_MEMORY);
    RET(4, a);
}

WINAPI_FN(kernel32, VirtualFree)
{
    RET(3, rt_vfree(ARG(0), ARG(1), ARG(2)) ? WIN_TRUE : WIN_FALSE);
}

/* ------------------------------------------------------------ TLS (slots in the TEB) */
static uint32_t tls_used[2];

WINAPI_FN(kernel32, TlsAlloc)
{
    for (uint32_t i = 0; i < 64; i++) {
        if (!(tls_used[i / 32] & (1u << (i % 32)))) {
            tls_used[i / 32] |= 1u << (i % 32);
            RET(0, i);
        }
    }
    RET(0, 0xffffffffu);
}

WINAPI_FN(kernel32, TlsFree)
{
    uint32_t i = ARG(0);
    if (i < 64)
        tls_used[i / 32] &= ~(1u << (i % 32));
    RET(1, WIN_TRUE);
}

WINAPI_FN(kernel32, TlsGetValue)
{
    uint32_t i = ARG(0);
    win_set_error(c, 0);
    RET(1, i < 64 ? R32(c->fs_base + 0xe10 + 4 * i) : 0);
}

WINAPI_FN(kernel32, TlsSetValue)
{
    uint32_t i = ARG(0);
    if (i < 64)
        W32(c->fs_base + 0xe10 + 4 * i, ARG(1));
    RET(2, i < 64 ? WIN_TRUE : WIN_FALSE);
}

/* ------------------------------------------------------------ synchronisation */
static SyncObj *sync_new(bool manual, bool signaled)
{
    SyncObj *s = calloc(1, sizeof *s);
    pthread_mutex_init(&s->m, NULL);
    pthread_cond_init(&s->cv, NULL);
    s->manual = manual;
    s->signaled = signaled;
    return s;
}

static SyncObj *sync_of(uint32_t h)
{
    HandleObj *o = rt_handle_get(h, H_FREE);
    if (!o || (o->kind != H_EVENT && o->kind != H_MUTEX && o->kind != H_THREAD))
        return NULL;
    return o->ptr;
}

WINAPI_FN(kernel32, CreateEventA)
{
    SyncObj *s = sync_new(ARG(1) != 0, ARG(2) != 0);
    RET(4, rt_handle_new(H_EVENT, s));
}

WINAPI_FN(kernel32, SetEvent)
{
    SyncObj *s = sync_of(ARG(0));
    if (!s)
        RET(1, WIN_FALSE);
    pthread_mutex_lock(&s->m);
    s->signaled = true;
    pthread_cond_broadcast(&s->cv);
    pthread_mutex_unlock(&s->m);
    RET(1, WIN_TRUE);
}

WINAPI_FN(kernel32, CreateMutexA)
{
    SyncObj *s = sync_new(false, true);
    s->is_mutex = true;
    if (ARG(1)) {
        s->signaled = false;
        s->owner = c->thread_id;
        s->count = 1;
    }
    win_set_error(c, 0);
    RET(3, rt_handle_new(H_MUTEX, s));
}

WINAPI_FN(kernel32, ReleaseMutex)
{
    SyncObj *s = sync_of(ARG(0));
    if (!s || !s->is_mutex)
        RET(1, WIN_FALSE);
    pthread_mutex_lock(&s->m);
    bool ok = s->owner == c->thread_id && s->count > 0;
    if (ok && --s->count == 0) {
        s->owner = 0;
        s->signaled = true;
        pthread_cond_broadcast(&s->cv);
    }
    pthread_mutex_unlock(&s->m);
    RET(1, ok ? WIN_TRUE : WIN_FALSE);
}

/* Wait with the global lock released. Returns WAIT_OBJECT_0 or WAIT_TIMEOUT. */
uint32_t win_wait(Cpu *c, uint32_t h, uint32_t ms)
{
    SyncObj *s = sync_of(h);
    if (!s)
        return 0xffffffffu;   /* WAIT_FAILED */
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    if (ms != 0xffffffffu) {
        deadline.tv_sec += ms / 1000;
        deadline.tv_nsec += (long)(ms % 1000) * 1000000L;
        if (deadline.tv_nsec >= 1000000000L) {
            deadline.tv_sec++;
            deadline.tv_nsec -= 1000000000L;
        }
    }
    RT_BLOCKING_BEGIN();
    pthread_mutex_lock(&s->m);
    uint32_t result = 0;
    for (;;) {
        if (s->is_mutex && s->owner == c->thread_id) {
            s->count++;
            break;
        }
        if (s->signaled) {
            if (s->is_mutex) {
                s->signaled = false;
                s->owner = c->thread_id;
                s->count = 1;
            } else if (!s->manual && !s->is_thread) {
                s->signaled = false;
            }
            break;
        }
        if (ms == 0) {
            result = 0x102;
            break;
        }
        int r = ms == 0xffffffffu ? pthread_cond_wait(&s->cv, &s->m)
                                  : pthread_cond_timedwait(&s->cv, &s->m, &deadline);
        if (r == ETIMEDOUT) {
            result = 0x102;   /* WAIT_TIMEOUT */
            break;
        }
    }
    pthread_mutex_unlock(&s->m);
    RT_BLOCKING_END();
    return result;
}

WINAPI_FN(kernel32, WaitForSingleObject)
{
    RET(2, win_wait(c, ARG(0), ARG(1)));
}

/* ------------------------------------------------------------ threads */
typedef struct ThreadStart { Cpu *cpu; uint32_t start, param; SyncObj *done; } ThreadStart;

static void *thread_main(void *arg)
{
    ThreadStart *ts = arg;
    rt_cpu_bind(ts->cpu);
    rt_gil_acquire();
    uint32_t code = rt_guest_call(ts->cpu, ts->start, 1, ts->param);
    (void)code;
    rt_gil_release();
    pthread_mutex_lock(&ts->done->m);
    ts->done->signaled = true;
    pthread_cond_broadcast(&ts->done->cv);
    pthread_mutex_unlock(&ts->done->m);
    rt_cpu_free(ts->cpu);
    free(ts);
    return NULL;
}

WINAPI_FN(kernel32, CreateThread)
{
    uint32_t start = ARG(2), param = ARG(3), flags = ARG(4), ptid = ARG(5);
    if (flags & 4)
        RT_WARN("CreateThread: CREATE_SUSPENDED is not supported; starting immediately");
    uint32_t tid;
    ThreadStart *ts = calloc(1, sizeof *ts);
    ts->cpu = rt_cpu_new_thread(&tid);
    ts->start = start;
    ts->param = param;
    ts->done = sync_new(true, false);
    ts->done->is_thread = true;
    uint32_t h = rt_handle_new(H_THREAD, ts->done);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 64u << 20);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t th;
    if (pthread_create(&th, &attr, thread_main, ts) != 0)
        rt_fatal("CreateThread failed");
    if (ptid)
        W32(ptid, tid);
    RT_INFO("CreateThread(start=%08x, param=%08x) -> tid %u", start, param, tid);
    RET(6, h);
}

WINAPI_FN(kernel32, ExitThread)
{
    /* Unwind to thread_main: the guest frames are abandoned. */
    RT_INFO("ExitThread(%u) on thread %u", ARG(0), c->thread_id);
    c->eax = ARG(0);
    c->esp += 8;
    return RT_MAGIC_RET;
}

WINAPI_FN(kernel32, ExitProcess)
{
    rt_exit_process(ARG(0));
}

WINAPI_FN(kernel32, RtlUnwind)
{
    /* Watcom's exception dispatcher unwinds handler frames itself before
     * calling this; with no OS-level handlers to run, only pop the arguments. */
    uint32_t frame = ARG(0);
    RT_INFO("RtlUnwind(frame=%08x, ip=%08x)", frame, ARG(1));
    if (frame && frame != 0xffffffffu) {
        /* Remove every registration record below the target frame. */
        uint32_t head = R32(c->fs_base);
        while (head != 0xffffffffu && head != frame && head < frame)
            head = R32(head);
        W32(c->fs_base, head);
    }
    RET(4, 0);
}
