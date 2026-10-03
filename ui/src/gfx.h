#pragma once
#include <stdint.h>

#define FRAME_W 1920
#define FRAME_H 1080

/* RGBA8-SRGB: alpha=byte3, blue=byte2, green=byte1, red=byte0 */
#define GFX_RGB(r,g,b)      ((uint32_t)0xFF000000u|((uint32_t)(b)<<16)|((uint32_t)(g)<<8)|(uint32_t)(r))
#define GFX_RGBA(r,g,b,a)   (((uint32_t)(a)<<24)|((uint32_t)(b)<<16)|((uint32_t)(g)<<8)|(uint32_t)(r))

/* Colour palette */
#define COL_BG      GFX_RGB(10,  13,  25)
#define COL_PANEL   GFX_RGB(23,  31,  48)
#define COL_BORDER  GFX_RGB(40,  50,  80)
#define COL_WHITE   GFX_RGB(255,255,255)
#define COL_GREY    GFX_RGB(160,160,160)
#define COL_DIM     GFX_RGB(80,  80, 100)
#define COL_CYAN    GFX_RGB(0,  220, 220)
#define COL_GREEN   GFX_RGB(0,  200, 100)
#define COL_RED     GFX_RGB(220, 60,  60)
#define COL_YELLOW  GFX_RGB(220,200,   0)
#define COL_SELECT  GFX_RGB(30, 100, 190)
#define COL_HINT    GFX_RGB(100,130,180)

int  gfx_init(void);
void gfx_clear(uint32_t color);
void gfx_fill_rect(int x, int y, int w, int h, uint32_t color);
void gfx_draw_str(int x, int y, const char *s, int scale, uint32_t color);
int  gfx_str_width(const char *s, int scale);
void gfx_present(void);
