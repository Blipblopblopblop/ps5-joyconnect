#pragma once

typedef enum {
    SCREEN_MAIN = 0,
    SCREEN_JOYCON,
    SCREEN_AUDIO,
    SCREEN_STATUS,
} screen_t;

void     ui_init(void);
int      ui_update(void);   /* returns 1 if redraw needed */
void     ui_draw(void);
screen_t ui_screen(void);
int      ui_wants_exit(void);
