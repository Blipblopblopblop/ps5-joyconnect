#include "gfx.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define FRAME_BYTES UINT64_C(0x1000000)
#define MEM_BYTES   (FRAME_BYTES * 2)
#define MEM_ALIGN   0x200000
#define MEM_TYPE_WC 3
#define MAP_PROT    0x33
#define PIXEL_FMT   UINT64_C(0x8000000022000000)

typedef struct { void *data, *meta, *r0, *r1; } vbuf_t;
typedef struct { uint8_t _[80]; }               vattr_t;

/* PS5 syscalls — no header, declared manually */
extern int    sceVideoOutOpen(int uid, int bus, int idx, const void *p);
extern int    sceVideoOutSetFlipRate(int h, int r);
extern int    sceVideoOutSubmitFlip(int h, int buf, int mode, long arg);
extern int    sceVideoOutWaitVblank(int h);
extern void   sceVideoOutSetBufferAttribute2(vattr_t *a, uint64_t pf, uint32_t tiling,
                                             uint32_t w, uint32_t h, uint64_t opt,
                                             uint32_t dcc, uint64_t dccClr);
extern int    sceVideoOutRegisterBuffers2(int h, int setIdx, int startIdx,
                                          vbuf_t *bufs, int count, vattr_t *attr,
                                          int cat, void *opt);
extern int    sceKernelAllocateDirectMemory(long start, long end, size_t len,
                                            size_t align, int type, long *phys);
extern int    sceKernelMapDirectMemory(void **addr, size_t len, int prot, int flags,
                                       long phys, size_t align);
extern size_t sceKernelGetDirectMemorySize(void);
extern int    sceSystemServiceHideSplashScreen(void);

static int     g_video = -1;
static uint8_t *g_bufs[2];
static int     g_cur = 0;

/* -----------------------------------------------------------------------
 * Glyph table — 5×7 bitmap, space/0-9/A-Z plus punctuation
 * ----------------------------------------------------------------------- */
typedef struct { char c; uint8_t r[7]; } glyph_t;
static const glyph_t g_glyphs[] = {
    {' ', {0,0,0,0,0,0,0}},          {'0', {14,17,19,21,25,17,14}},
    {'1', {4,12,4,4,4,4,14}},        {'2', {14,17,1,2,4,8,31}},
    {'3', {30,1,1,14,1,1,30}},       {'4', {2,6,10,18,31,2,2}},
    {'5', {31,16,16,30,1,1,30}},     {'6', {14,16,16,30,17,17,14}},
    {'7', {31,1,2,4,8,8,8}},         {'8', {14,17,17,14,17,17,14}},
    {'9', {14,17,17,15,1,1,14}},     {'A', {14,17,17,31,17,17,17}},
    {'B', {30,17,17,30,17,17,30}},   {'C', {14,17,16,16,16,17,14}},
    {'D', {30,17,17,17,17,17,30}},   {'E', {31,16,16,30,16,16,31}},
    {'F', {31,16,16,30,16,16,16}},   {'G', {14,17,16,23,17,17,14}},
    {'H', {17,17,17,31,17,17,17}},   {'I', {31,4,4,4,4,4,31}},
    {'J', {7,2,2,2,18,18,12}},       {'K', {17,18,20,24,20,18,17}},
    {'L', {16,16,16,16,16,16,31}},   {'M', {17,27,21,21,17,17,17}},
    {'N', {17,25,21,19,17,17,17}},   {'O', {14,17,17,17,17,17,14}},
    {'P', {30,17,17,30,16,16,16}},   {'Q', {14,17,17,17,21,18,13}},
    {'R', {30,17,17,30,20,18,17}},   {'S', {15,16,16,14,1,1,30}},
    {'T', {31,4,4,4,4,4,4}},         {'U', {17,17,17,17,17,17,14}},
    {'V', {17,17,17,17,17,10,4}},    {'W', {17,17,17,21,21,21,10}},
    {'X', {17,17,10,4,10,17,17}},    {'Y', {17,17,10,4,4,4,4}},
    {'Z', {31,1,2,4,8,16,31}},       {'-', {0,0,0,31,0,0,0}},
    {':', {0,4,0,0,0,4,0}},          {'/', {0,1,2,4,8,16,0}},
    {'.', {0,0,0,0,0,4,0}},          {'!', {4,4,4,4,4,0,4}},
    {'(', {2,4,8,8,8,4,2}},          {')', {8,4,2,2,2,4,8}},
    {'[', {14,8,8,8,8,8,14}},        {']', {14,2,2,2,2,2,14}},
    {'#', {10,10,31,10,31,10,10}},   {'*', {0,21,14,31,14,21,0}},
    {'+', {0,4,4,31,4,4,0}},         {'<', {1,2,4,8,4,2,1}},
    {'>', {8,4,2,1,2,4,8}},          {'%', {17,9,4,4,4,18,17}},
};
#define N_GLYPHS ((int)(sizeof(g_glyphs)/sizeof(g_glyphs[0])))

static const uint8_t *glyph_rows(char c) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    for (int i = 0; i < N_GLYPHS; i++)
        if (g_glyphs[i].c == c) return g_glyphs[i].r;
    return g_glyphs[0].r;
}

/* -----------------------------------------------------------------------
 * Tiled framebuffer helpers
 * ----------------------------------------------------------------------- */
static void flush_range(void *addr, size_t len) {
    uint8_t *at = (uint8_t *)addr;
    const uint8_t *end = at + len;
    for (; at < end; at += 64)
        __asm__ volatile("clflush (%0)" : : "r"(at) : "memory");
    __asm__ volatile("mfence" ::: "memory");
}

static size_t tiled_byte_offset(unsigned x, unsigned y) {
    uint32_t o = ((y<<4)&0x70U)   ^ ((y<<5)&0xf00U)  ^ ((y<<9)&0x1000U) ^
                 ((y<<8)&0x4000U) ^ ((x<<2)&0xcU)    ^ ((x<<5)&0x380U)  ^
                 ((x<<4)&0x400U)  ^ ((x<<6)&0x800U)  ^ ((x<<9)&0xa000U);
    uint32_t bpr = (FRAME_W + 127U) >> 7;
    uint32_t bi  = (y >> 7) * bpr + (x >> 7);
    return ((size_t)bi << 16) + (size_t)o;
}

static inline void put_pixel(uint8_t *fb, unsigned x, unsigned y, uint32_t col) {
    *(uint32_t *)(fb + tiled_byte_offset(x, y)) = col;
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */
int gfx_init(void) {
    (void)sceSystemServiceHideSplashScreen();

    g_video = sceVideoOutOpen(0xff, 0, 0, NULL);
    if (g_video < 0) return 0;

    size_t pool = sceKernelGetDirectMemorySize();
    if (pool < MEM_BYTES) return 0;

    long phys = 0;
    if (sceKernelAllocateDirectMemory(0, (long)pool, MEM_BYTES,
                                      MEM_ALIGN, MEM_TYPE_WC, &phys) < 0) return 0;
    void *mapped = NULL;
    if (sceKernelMapDirectMemory(&mapped, MEM_BYTES, MAP_PROT,
                                 0, phys, MEM_ALIGN) < 0) return 0;

    g_bufs[0] = (uint8_t *)mapped;
    g_bufs[1] = (uint8_t *)mapped + FRAME_BYTES;
    memset(mapped, 0, MEM_BYTES);
    flush_range(mapped, MEM_BYTES);

    vbuf_t  bufs[2] = {{g_bufs[0],NULL,NULL,NULL},{g_bufs[1],NULL,NULL,NULL}};
    vattr_t attr    = {{0}};
    sceVideoOutSetFlipRate(g_video, 0);
    sceVideoOutSetBufferAttribute2(&attr, PIXEL_FMT, 0, FRAME_W, FRAME_H, 0, 0, 0);
    if (sceVideoOutRegisterBuffers2(g_video, 0, 0, bufs, 2, &attr, 0, NULL) < 0) return 0;
    sceVideoOutSubmitFlip(g_video, 0, 1, 1);
    sceVideoOutWaitVblank(g_video);
    return 1;
}

void gfx_clear(uint32_t color) {
    uint8_t *fb = g_bufs[g_cur];
    for (unsigned y = 0; y < FRAME_H; y++)
        for (unsigned x = 0; x < FRAME_W; x++)
            put_pixel(fb, x, y, color);
}

void gfx_fill_rect(int x, int y, int w, int h, uint32_t color) {
    int x1 = x + w, y1 = y + h;
    if (x  < 0) x  = 0;
    if (y  < 0) y  = 0;
    if (x1 > FRAME_W) x1 = FRAME_W;
    if (y1 > FRAME_H) y1 = FRAME_H;
    uint8_t *fb = g_bufs[g_cur];
    for (int row = y; row < y1; row++)
        for (int col = x; col < x1; col++)
            put_pixel(fb, (unsigned)col, (unsigned)row, color);
}

void gfx_draw_str(int x, int y, const char *s, int scale, uint32_t color) {
    for (; *s; s++) {
        const uint8_t *rows = glyph_rows(*s);
        for (int row = 0; row < 7; row++)
            for (int col = 0; col < 5; col++)
                if (rows[row] & (1U << (4 - col)))
                    gfx_fill_rect(x + col*scale, y + row*scale, scale, scale, color);
        x += 6 * scale;
        if (x >= FRAME_W) return;
    }
}

int gfx_str_width(const char *s, int scale) {
    int n = 0;
    for (; *s; s++) n++;
    return n ? (n - 1) * 6 * scale + 5 * scale : 0;
}

void gfx_present(void) {
    flush_range(g_bufs[g_cur], FRAME_BYTES);
    sceVideoOutSubmitFlip(g_video, g_cur, 1, 0);
    sceVideoOutWaitVblank(g_video);
    g_cur ^= 1;
}
