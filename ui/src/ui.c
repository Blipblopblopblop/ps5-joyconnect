#include "ui.h"
#include "gfx.h"
#include "input.h"
#include "bt_mgr.h"
#include <string.h>
#include <stdio.h>

#define MARGIN   100
#define TITLE_Y  45
#define TITLE_S  5
#define SEP_Y    120
#define BODY_Y   160
#define ITEM_H   68
#define ITEM_S   4
#define HINT_Y   1008
#define HINT_S   2

/* -----------------------------------------------------------------------
 * Layout helpers
 * ----------------------------------------------------------------------- */
static void draw_title(const char *text) {
    gfx_draw_str(MARGIN, TITLE_Y, text, TITLE_S, COL_CYAN);
    gfx_fill_rect(MARGIN, SEP_Y, FRAME_W - MARGIN*2, 2, COL_BORDER);
}

static void draw_hint(const char *text) {
    gfx_draw_str(MARGIN, HINT_Y, text, HINT_S, COL_HINT);
}

static void draw_badge(int x, int y, const char *text, uint32_t bg, uint32_t fg) {
    int w = gfx_str_width(text, 2) + 16;
    gfx_fill_rect(x, y - 2, w, 22, bg);
    gfx_draw_str(x + 8, y, text, 2, fg);
}

static const char *jc_str(jc_state_t s) {
    switch (s) {
    case JC_SCANNING:  return "SCANNING";
    case JC_CONNECTED: return "CONNECTED";
    default:           return "NOT CONNECTED";
    }
}

static uint32_t jc_col(jc_state_t s) {
    switch (s) {
    case JC_SCANNING:  return COL_YELLOW;
    case JC_CONNECTED: return COL_GREEN;
    default:           return COL_DIM;
    }
}

static const char *hp_str(hp_state_t s) {
    switch (s) {
    case HP_CONNECTING: return "CONNECTING";
    case HP_CONNECTED:  return "CONNECTED";
    case HP_NO_SAVED:   return "NO SAVED ADDR";
    default:            return "DISCONNECTED";
    }
}

static uint32_t hp_col(hp_state_t s) {
    switch (s) {
    case HP_CONNECTING: return COL_YELLOW;
    case HP_CONNECTED:  return COL_GREEN;
    case HP_NO_SAVED:   return COL_RED;
    default:            return COL_DIM;
    }
}

/* -----------------------------------------------------------------------
 * Main menu
 * ----------------------------------------------------------------------- */
typedef struct { const char *label; screen_t dest; } main_item_t;
static const main_item_t main_items[] = {
    {"JOY-CON",  SCREEN_JOYCON},
    {"AUDIO",    SCREEN_AUDIO},
    {"STATUS",   SCREEN_STATUS},
};
#define MAIN_COUNT 3

static int g_main_sel = 0;

static void draw_main(void) {
    draw_title("JOYCONNECT UI");
    draw_hint("CROSS SELECT  CIRCLE BACK  OPTIONS EXIT");
    for (int i = 0; i < MAIN_COUNT; i++) {
        int y = BODY_Y + i * ITEM_H;
        if (i == g_main_sel) {
            gfx_fill_rect(MARGIN - 8, y - 8, FRAME_W - MARGIN*2 + 16, ITEM_H - 4, COL_SELECT);
            gfx_draw_str(MARGIN, y, main_items[i].label, ITEM_S, COL_WHITE);
        } else {
            gfx_draw_str(MARGIN, y, main_items[i].label, ITEM_S, COL_GREY);
        }
    }
    /* Quick status on main menu */
    bt_status_t st = bt_mgr_status();
    int sy = BODY_Y + MAIN_COUNT * ITEM_H + 40;
    gfx_fill_rect(MARGIN - 8, sy - 8, FRAME_W - MARGIN*2 + 16, 2, COL_BORDER);
    sy += 20;
    gfx_draw_str(MARGIN, sy, "LEFT JOY-CON:", HINT_S, COL_GREY);
    draw_badge(MARGIN + 200, sy - 2, jc_str(st.jc_l), COL_PANEL, jc_col(st.jc_l));
    sy += 30;
    gfx_draw_str(MARGIN, sy, "RIGHT JOY-CON:", HINT_S, COL_GREY);
    draw_badge(MARGIN + 200, sy - 2, jc_str(st.jc_r), COL_PANEL, jc_col(st.jc_r));
    sy += 30;
    gfx_draw_str(MARGIN, sy, "HEADPHONE:", HINT_S, COL_GREY);
    draw_badge(MARGIN + 200, sy - 2, hp_str(st.hp), COL_PANEL, hp_col(st.hp));
}

/* -----------------------------------------------------------------------
 * Joy-Con screen
 * ----------------------------------------------------------------------- */
typedef struct { const char *label; int cmd; } jc_item_t;
#define JC_SCAN_START 0
#define JC_SCAN_STOP  1
#define JC_BACK       2
static const jc_item_t jc_items[] = {
    {"START SCANNING", JC_SCAN_START},
    {"STOP SCANNING",  JC_SCAN_STOP},
    {"BACK",           JC_BACK},
};
#define JC_COUNT 3

static int g_jc_sel = 0;

static void draw_joycon(void) {
    draw_title("JOY-CON");
    draw_hint("CROSS SELECT  CIRCLE BACK");
    bt_status_t st = bt_mgr_status();

    int y = BODY_Y;
    gfx_draw_str(MARGIN, y, "LEFT JOY-CON", ITEM_S, COL_GREY);
    draw_badge(MARGIN + 330, y - 2, jc_str(st.jc_l), COL_PANEL, jc_col(st.jc_l));
    y += ITEM_H;
    gfx_draw_str(MARGIN, y, "RIGHT JOY-CON", ITEM_S, COL_GREY);
    draw_badge(MARGIN + 330, y - 2, jc_str(st.jc_r), COL_PANEL, jc_col(st.jc_r));

    y += ITEM_H + 20;
    gfx_fill_rect(MARGIN, y, FRAME_W - MARGIN*2, 2, COL_BORDER);
    y += 20;

    for (int i = 0; i < JC_COUNT; i++, y += ITEM_H) {
        if (i == g_jc_sel) {
            gfx_fill_rect(MARGIN - 8, y - 8, 600, ITEM_H - 4, COL_SELECT);
            gfx_draw_str(MARGIN, y, jc_items[i].label, ITEM_S, COL_WHITE);
        } else {
            gfx_draw_str(MARGIN, y, jc_items[i].label, ITEM_S, COL_GREY);
        }
    }

    gfx_draw_str(MARGIN, y + 30, "HOLD SL+SR ON EACH JOY-CON TO PAIR", HINT_S, COL_HINT);
}

/* -----------------------------------------------------------------------
 * Audio screen — three modes: action menu, scan results, pairing
 * ----------------------------------------------------------------------- */

/* Sub-modes */
#define HP_MODE_MENU  0   /* default action menu */
#define HP_MODE_SCAN  1   /* showing scan results */

static int g_hp_mode    = HP_MODE_MENU;
static int g_hp_sel     = 0;   /* selection in action menu */
static int g_scan_sel   = 0;   /* selection in device list */

/* Action menu */
typedef struct { const char *label; int cmd; } hp_item_t;
#define HP_ACT_SCAN       0
#define HP_ACT_CONNECT    1
#define HP_ACT_DISCONNECT 2
#define HP_ACT_BACK       3
static const hp_item_t hp_items[] = {
    {"SCAN FOR HEADPHONES", HP_ACT_SCAN},
    {"CONNECT SAVED",       HP_ACT_CONNECT},
    {"DISCONNECT",          HP_ACT_DISCONNECT},
    {"BACK",                HP_ACT_BACK},
};
#define HP_COUNT 4

static void draw_audio(void) {
    bt_status_t st = bt_mgr_status();

    draw_title("BLUETOOTH AUDIO");

    /* Status bar */
    int y = BODY_Y;
    gfx_draw_str(MARGIN, y, "SAVED:", HINT_S, COL_GREY);
    gfx_draw_str(MARGIN + 110, y, st.hp_addr, HINT_S, COL_WHITE);
    y += 28;
    gfx_draw_str(MARGIN, y, "STATUS:", HINT_S, COL_GREY);
    draw_badge(MARGIN + 110, y - 2, hp_str(st.hp), COL_PANEL, hp_col(st.hp));
    y += 28;
    gfx_fill_rect(MARGIN, y, FRAME_W - MARGIN*2, 2, COL_BORDER);
    y += 14;

    if (g_hp_mode == HP_MODE_MENU) {
        draw_hint("CROSS SELECT  CIRCLE BACK");
        for (int i = 0; i < HP_COUNT; i++, y += 56) {
            if (i == g_hp_sel) {
                gfx_fill_rect(MARGIN - 8, y - 6, 700, 52, COL_SELECT);
                gfx_draw_str(MARGIN, y, hp_items[i].label, ITEM_S, COL_WHITE);
            } else {
                gfx_draw_str(MARGIN, y, hp_items[i].label, ITEM_S, COL_GREY);
            }
        }
    } else {
        /* HP_MODE_SCAN — device list */
        if (st.hp == HP_SCANNING) {
            draw_hint("CIRCLE CANCEL SCAN");
            gfx_draw_str(MARGIN, y, "SCANNING...", ITEM_S, COL_YELLOW);
            y += ITEM_H;
        } else {
            draw_hint("CROSS PAIR  CIRCLE BACK  SQUARE RESCAN");
        }
        if (st.dev_count == 0 && st.scan_done) {
            gfx_draw_str(MARGIN, y, "NO DEVICES FOUND", ITEM_S, COL_DIM);
        } else {
            for (int i = 0; i < st.dev_count; i++, y += 56) {
                int sel = (i == g_scan_sel) && (st.hp != HP_SCANNING);
                if (sel) {
                    gfx_fill_rect(MARGIN - 8, y - 6, FRAME_W - MARGIN*2 + 16, 52, COL_SELECT);
                }
                uint32_t nc = sel ? COL_WHITE : COL_GREY;
                /* Device name (uppercase) */
                gfx_draw_str(MARGIN, y, st.devs[i].name, ITEM_S, nc);
                /* Class badge */
                uint32_t maj = (st.devs[i].cod >> 8) & 0x1F;
                const char *badge = (maj == 0x04) ? "AUDIO" :
                                    (maj == 0x02) ? "PHONE" : "DEVICE";
                draw_badge(FRAME_W - MARGIN - 200, y - 2, badge, COL_PANEL, COL_HINT);
            }
        }
        if (st.hp == HP_PAIRING || st.hp == HP_CONNECTING) {
            gfx_draw_str(MARGIN, HINT_Y - 30, "PAIRING...", ITEM_S, COL_YELLOW);
        }
    }
}

/* -----------------------------------------------------------------------
 * Status screen
 * ----------------------------------------------------------------------- */
static void draw_status(void) {
    draw_title("STATUS");
    draw_hint("CIRCLE BACK");
    bt_status_t st = bt_mgr_status();

    int y = BODY_Y;
    gfx_draw_str(MARGIN, y, "HCI:", ITEM_S, COL_GREY);
    gfx_draw_str(MARGIN + 200, y, st.hci_ok ? "OK" : "FAILED", ITEM_S,
                 st.hci_ok ? COL_GREEN : COL_RED);
    y += ITEM_H;
    gfx_draw_str(MARGIN, y, "LEFT JOY-CON:", ITEM_S, COL_GREY);
    gfx_draw_str(MARGIN + 400, y, jc_str(st.jc_l), ITEM_S, jc_col(st.jc_l));
    y += ITEM_H;
    gfx_draw_str(MARGIN, y, "RIGHT JOY-CON:", ITEM_S, COL_GREY);
    gfx_draw_str(MARGIN + 400, y, jc_str(st.jc_r), ITEM_S, jc_col(st.jc_r));
    y += ITEM_H;
    gfx_draw_str(MARGIN, y, "HEADPHONE:", ITEM_S, COL_GREY);
    gfx_draw_str(MARGIN + 400, y, hp_str(st.hp), ITEM_S, hp_col(st.hp));
    y += ITEM_H;
    gfx_draw_str(MARGIN, y, "VIRTUAL PAD:", ITEM_S, COL_GREY);
    gfx_draw_str(MARGIN + 400, y, st.vpad_active ? "ACTIVE" : "INACTIVE", ITEM_S,
                 st.vpad_active ? COL_GREEN : COL_DIM);
    y += ITEM_H;
    gfx_draw_str(MARGIN, y, "SAVED ADDR:", ITEM_S, COL_GREY);
    gfx_draw_str(MARGIN + 400, y, st.hp_addr, ITEM_S, COL_WHITE);
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */
static screen_t g_screen  = SCREEN_MAIN;
static int      g_dirty   = 1;
static int      g_exit    = 0;

void ui_init(void) {
    g_screen = SCREEN_MAIN;
    g_dirty  = 1;
    g_exit   = 0;
}

int ui_update(void) {
    int dirty = 0;

    switch (g_screen) {
    case SCREEN_MAIN:
        if (input_just(BTN_UP)) {
            g_main_sel = (g_main_sel > 0) ? g_main_sel - 1 : MAIN_COUNT - 1;
            dirty = 1;
        }
        if (input_just(BTN_DOWN)) {
            g_main_sel = (g_main_sel + 1) % MAIN_COUNT;
            dirty = 1;
        }
        if (input_just(BTN_CROSS)) {
            g_screen = main_items[g_main_sel].dest;
            if (g_screen == SCREEN_AUDIO) g_hp_mode = HP_MODE_MENU;
            dirty = 1;
        }
        if (input_just(BTN_OPTIONS)) {
            g_exit = 1;
        }
        break;

    case SCREEN_JOYCON:
        if (input_just(BTN_UP)) {
            g_jc_sel = (g_jc_sel > 0) ? g_jc_sel - 1 : JC_COUNT - 1;
            dirty = 1;
        }
        if (input_just(BTN_DOWN)) {
            g_jc_sel = (g_jc_sel + 1) % JC_COUNT;
            dirty = 1;
        }
        if (input_just(BTN_CROSS)) {
            switch (jc_items[g_jc_sel].cmd) {
            case JC_SCAN_START: bt_mgr_scan_joycon_start(); dirty = 1; break;
            case JC_SCAN_STOP:  bt_mgr_scan_joycon_stop();  dirty = 1; break;
            case JC_BACK:       g_screen = SCREEN_MAIN; dirty = 1; break;
            }
        }
        if (input_just(BTN_CIRCLE)) {
            g_screen = SCREEN_MAIN;
            dirty = 1;
        }
        /* Redraw periodically while scanning to show updated status */
        {
            static int tick = 0;
            tick++;
            if (tick >= 30) { tick = 0; dirty = 1; }
        }
        break;

    case SCREEN_AUDIO: {
        bt_status_t ast = bt_mgr_status();
        if (g_hp_mode == HP_MODE_MENU) {
            if (input_just(BTN_UP)) {
                g_hp_sel = (g_hp_sel > 0) ? g_hp_sel - 1 : HP_COUNT - 1;
                dirty = 1;
            }
            if (input_just(BTN_DOWN)) {
                g_hp_sel = (g_hp_sel + 1) % HP_COUNT;
                dirty = 1;
            }
            if (input_just(BTN_CROSS)) {
                switch (hp_items[g_hp_sel].cmd) {
                case HP_ACT_SCAN:
                    g_hp_mode = HP_MODE_SCAN;
                    g_scan_sel = 0;
                    bt_mgr_hp_scan_start();
                    dirty = 1;
                    break;
                case HP_ACT_CONNECT:    bt_mgr_hp_connect();    dirty = 1; break;
                case HP_ACT_DISCONNECT: bt_mgr_hp_disconnect(); dirty = 1; break;
                case HP_ACT_BACK:       g_screen = SCREEN_MAIN; dirty = 1; break;
                }
            }
            if (input_just(BTN_CIRCLE)) {
                g_screen = SCREEN_MAIN;
                dirty = 1;
            }
        } else {
            /* Scan mode */
            if (input_just(BTN_UP)) {
                if (ast.dev_count > 0)
                    g_scan_sel = (g_scan_sel > 0) ? g_scan_sel - 1 : ast.dev_count - 1;
                dirty = 1;
            }
            if (input_just(BTN_DOWN)) {
                if (ast.dev_count > 0)
                    g_scan_sel = (g_scan_sel + 1) % ast.dev_count;
                dirty = 1;
            }
            if (input_just(BTN_CROSS) && ast.dev_count > 0 && ast.hp != HP_SCANNING) {
                bt_mgr_hp_pair(g_scan_sel);
                dirty = 1;
            }
            if (input_just(BTN_SQUARE)) {
                /* Rescan */
                g_scan_sel = 0;
                bt_mgr_hp_scan_start();
                dirty = 1;
            }
            if (input_just(BTN_CIRCLE)) {
                bt_mgr_hp_scan_stop();
                g_hp_mode = HP_MODE_MENU;
                dirty = 1;
            }
        }
        {
            static int tick2 = 0;
            tick2++;
            if (tick2 >= 15) { tick2 = 0; dirty = 1; }
        }
        break;
    }

    case SCREEN_STATUS:
        if (input_just(BTN_CIRCLE) || input_just(BTN_CROSS)) {
            g_screen = SCREEN_MAIN;
            dirty = 1;
        }
        {
            static int tick3 = 0;
            tick3++;
            if (tick3 >= 30) { tick3 = 0; dirty = 1; }
        }
        break;
    }

    if (g_dirty) { g_dirty = 0; dirty = 1; }
    return dirty;
}

void ui_draw(void) {
    gfx_clear(COL_BG);
    switch (g_screen) {
    case SCREEN_MAIN:   draw_main();    break;
    case SCREEN_JOYCON: draw_joycon();  break;
    case SCREEN_AUDIO:  draw_audio();   break;
    case SCREEN_STATUS: draw_status();  break;
    }
}

screen_t ui_screen(void)    { return g_screen; }
int      ui_wants_exit(void) { return g_exit; }
