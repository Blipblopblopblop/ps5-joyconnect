#pragma once
#include <stdint.h>

/* DualSense button bitmask (same layout used by scePadReadState) */
#define BTN_UP       0x00000010
#define BTN_RIGHT    0x00000020
#define BTN_DOWN     0x00000040
#define BTN_LEFT     0x00000080
#define BTN_L1       0x00000400
#define BTN_R1       0x00000800
#define BTN_TRIANGLE 0x00001000
#define BTN_CIRCLE   0x00002000
#define BTN_CROSS    0x00004000
#define BTN_SQUARE   0x00008000
#define BTN_OPTIONS  0x00080000

int  input_init(void);
void input_update(void);
int  input_just(uint32_t btn);   /* 1 if button newly pressed this frame */
int  input_held(uint32_t btn);   /* 1 if button is currently held */
