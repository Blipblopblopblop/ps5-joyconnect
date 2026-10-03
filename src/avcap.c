#include "avcap.h"
#include "log.h"
#include <string.h>
#include <dlfcn.h>

/* -----------------------------------------------------------------------
 * libSceAvcap2 dynamic load — same NID-based approach as ghost-toothAPI.
 * The library is loaded at runtime since it's not in PayloadSDK stubs.
 * -----------------------------------------------------------------------*/
typedef int (*fn_avcap_init_t)(void);
typedef int (*fn_avcap_open_t)(int, int, int, int, void **);
typedef int (*fn_avcap_start_t)(void *);
typedef int (*fn_avcap_stop_t)(void *);
typedef int (*fn_avcap_close_t)(void *);
typedef int (*fn_avcap_read_t)(void *, void *, int);

static void *g_lib;
static void *g_ctx;
static fn_avcap_init_t  fn_init;
static fn_avcap_open_t  fn_open;
static fn_avcap_start_t fn_start;
static fn_avcap_stop_t  fn_stop;
static fn_avcap_close_t fn_close;
static fn_avcap_read_t  fn_read;

int avcap_open(int sample_rate, int channels) {
    g_lib = dlopen("libSceAvcap2.sprx", RTLD_NOW);
    if (!g_lib) {
        /* Try loading via kernel_dynlib_load as ghost-toothAPI does */
        g_lib = dlopen("/system/priv/lib/libSceAvcap2.sprx", RTLD_NOW);
        if (!g_lib) {
            log_line("avcap: cannot load libSceAvcap2.sprx: %s", dlerror());
            return 0;
        }
    }

#define LOAD(sym, ptr) \
    ptr = (typeof(ptr))dlsym(g_lib, #sym); \
    if (!ptr) { log_line("avcap: missing " #sym); return 0; }

    LOAD(sceAvcap2Initialize, fn_init);
    LOAD(sceAvcap2Open,       fn_open);
    LOAD(sceAvcap2Start,      fn_start);
    LOAD(sceAvcap2Stop,       fn_stop);
    LOAD(sceAvcap2Close,      fn_close);
    LOAD(sceAvcap2ReadAudio,  fn_read);
#undef LOAD

    fn_init();

    /* Open: format=2 (PCM16), sample_rate, channels, bufFrames=960 */
    int ret = fn_open(2, sample_rate, channels, 960, &g_ctx);
    if (ret < 0) {
        log_line("avcap: open failed: 0x%08x", ret);
        return 0;
    }

    fn_start(g_ctx);
    log_line("avcap: opened %dHz %dch", sample_rate, channels);
    return 1;
}

int avcap_read(int16_t *buf, int frames) {
    if (!g_ctx || !fn_read) return -1;
    return fn_read(g_ctx, buf, frames);
}

void avcap_close(void) {
    if (g_ctx) {
        fn_stop(g_ctx);
        fn_close(g_ctx);
        g_ctx = NULL;
    }
    if (g_lib) {
        dlclose(g_lib);
        g_lib = NULL;
    }
}
