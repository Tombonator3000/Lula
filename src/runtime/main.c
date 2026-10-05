/* Entry point of the recompiled game. */
#include "rt.h"

#include <signal.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <pthread.h>

static void usage(const char *argv0)
{
    fprintf(stderr,
            "Lula - The Sexy Empire, statically recompiled\n"
            "usage: %s [--data DIR] [--save DIR] [--mods DIR] [--trace] [--verbose] [-- GAME-ARGS]\n"
            "  --data DIR   game files (default: original/app next to the repository)\n"
            "  --save DIR   writable overlay for saves and settings (default: local/save)\n"
            "  --mods DIR   replacement assets, same layout as the game folder (read first)\n"
            "  --trace      log every Win32 call with its first arguments\n"
            "  game arguments are passed on, for example -novideo\n", argv0);
}

static void on_segv(int sig, siginfo_t *si, void *ctx)
{
    (void)ctx;
    uintptr_t addr = (uintptr_t)si->si_addr;
    fprintf(stderr, "lula[fatal] signal %d", sig);
    if (g_mem && addr >= (uintptr_t)g_mem && addr < (uintptr_t)g_mem + 0x100000000ull)
        fprintf(stderr, " at guest address %08x", (uint32_t)(addr - (uintptr_t)g_mem));
    fputc('\n', stderr);
    rt_dump_cpu(rt_cpu_current(), stderr);
    fflush(stderr);
    signal(sig, SIG_DFL);
    raise(sig);
}

/* Directory that contains the repository, found from the executable path. */
static char *repo_path(const char *argv0, const char *rel)
{
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0)
        snprintf(exe, sizeof exe, "%s", argv0);
    else
        exe[n] = 0;
    char *dir = strdup(exe);
    for (int up = 0; up < 6; up++) {
        char *slash = strrchr(dir, '/');
        if (!slash)
            break;
        *slash = 0;
        char probe[4096];
        snprintf(probe, sizeof probe, "%s/original/app/WET.EXE", dir);
        if (access(probe, R_OK) == 0) {
            char *out = malloc(strlen(dir) + strlen(rel) + 2);
            sprintf(out, "%s/%s", dir, rel);
            free(dir);
            return out;
        }
    }
    free(dir);
    return NULL;
}

typedef struct { int argc; char **argv; int status; } MainArgs;

static void *guest_main(void *arg)
{
    (void)arg;
    uint32_t tid;
    Cpu *c = rt_cpu_new_thread(&tid);
    rt_cpu_bind(c);
    rt_gil_acquire();
    /* WinMainCRTStartup takes no arguments and never returns: the runtime
     * leaves through ExitProcess. */
    rt_guest_call(c, g_image_entry, 0);
    RT_WARN("entry point returned (eax=%08x)", c->eax);
    rt_exit_process(c->eax);
}

int main(int argc, char **argv)
{
    const char *data = NULL, *save = NULL, *mods = getenv("LULA_MODS");
    char game_args[1024] = "";
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--data") == 0 && i + 1 < argc)
            data = argv[++i];
        else if (strcmp(argv[i], "--save") == 0 && i + 1 < argc)
            save = argv[++i];
        else if (strcmp(argv[i], "--mods") == 0 && i + 1 < argc)
            mods = argv[++i];
        else if (strcmp(argv[i], "--trace") == 0)
            rt_trace_imports = 1, rt_log_level = RT_LOG_INFO;
        else if (strcmp(argv[i], "--verbose") == 0)
            rt_log_level = rt_log_level < RT_LOG_INFO ? RT_LOG_INFO : RT_LOG_TRACE;
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            if (strcmp(argv[i], "--") == 0)
                continue;
            size_t n = strlen(game_args);
            snprintf(game_args + n, sizeof game_args - n, "%s%s", n ? " " : "", argv[i]);
        }
    }
    const char *env = getenv("LULA_LOG");
    if (env)
        rt_log_level = atoi(env);
    char *def_data = repo_path(argv[0], "original/app");
    char *def_save = repo_path(argv[0], "local/save");
    if (!data)
        data = def_data;
    if (!save)
        save = def_save ? def_save : "lula-save";
    if (!data) {
        usage(argv[0]);
        fprintf(stderr, "game data not found; pass --data DIR\n");
        return 2;
    }

    struct sigaction sa = {0};
    sa.sa_sigaction = on_segv;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);

    rt_mem_init();
    rt_vfs_init(data, save);
    rt_vfs_set_mods(mods);
    char exe[4096];
    char *exe_host = rt_vfs_resolve_read("WET.EXE");
    snprintf(exe, sizeof exe, "%s", exe_host ? exe_host : "WET.EXE");
    free(exe_host);
    if (!rt_load_image(exe))
        return 1;
    rt_set_command_line(game_args);
    rt_platform_init();

    /* Guest code runs on a thread with a large host stack: guest recursion
     * becomes host recursion. */
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 256u << 20);
    pthread_t th;
    if (pthread_create(&th, &attr, guest_main, NULL) != 0)
        rt_fatal("cannot start guest thread");
    rt_platform_run();      /* host event loop on the main thread */
    return 0;
}
