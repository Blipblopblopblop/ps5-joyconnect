#include "vpad.h"
#include "log.h"
#include <string.h>
#include "ps5types.h"

/* -----------------------------------------------------------------------
 * ScePad virtual device API — declared manually since PayloadSDK has stubs
 * but no headers for these functions.
 *
 * Signatures inferred from PS5 homebrew reverse-engineering (Ghostpad, etc.)
 * -----------------------------------------------------------------------*/
int scePadInit(void);
int scePadVirtualDeviceAddDevice(int userId, int type, int index, void *reserved, int *outHandle);
int scePadVirtualDeviceDeleteDevice(int handle);
/* data = 120-byte DualSense pad sample; count = number of samples (1-16) */
int scePadVirtualDeviceInsertData(int handle, const void *data, int count);

/* DualSense button bitmask constants (PS5 SDK) */
#define PAD_BTN_SELECT      0x00000001
#define PAD_BTN_L3          0x00000002
#define PAD_BTN_R3          0x00000040
#define PAD_BTN_START       0x00000008  /* Options */
#define PAD_BTN_UP          0x00000010
#define PAD_BTN_RIGHT       0x00000020
#define PAD_BTN_DOWN        0x00000040
#define PAD_BTN_LEFT        0x00000080
#define PAD_BTN_L2          0x00000100  /* full digital */
#define PAD_BTN_R2          0x00000200
#define PAD_BTN_L1          0x00000400
#define PAD_BTN_R1          0x00000800
#define PAD_BTN_TRIANGLE    0x00001000
#define PAD_BTN_CIRCLE      0x00002000
#define PAD_BTN_CROSS       0x00004000
#define PAD_BTN_SQUARE      0x00008000
#define PAD_BTN_TOUCHPAD    0x00100000
#define PAD_BTN_CREATE      0x00020000
#define PAD_BTN_OPTIONS     0x00080000
#define PAD_BTN_MUTE        0x04000000

/* 120-byte DualSense sample layout (from radio_input.c analysis) */
typedef struct {
    uint32_t buttons;       /* +0: button bitmask */
    uint8_t  lx, ly;        /* +4,5: left stick (0=left/down, 255=right/up, 128=center) */
    uint8_t  rx, ry;        /* +6,7: right stick */
    uint8_t  l2, r2;        /* +8,9: trigger analog (0-255) */
    uint8_t  _unk[2];       /* +10 */
    /* Motion sensors at +16 */
    int16_t  accel_x;       /* +16 */
    int16_t  accel_y;       /* +18 */
    int16_t  accel_z;       /* +20 */
    int16_t  gyro_x;        /* +22 */
    int16_t  gyro_y;        /* +24 */
    int16_t  gyro_z;        /* +26 */
    uint8_t  _pad[48];      /* +28..+75 */
    uint8_t  connected;     /* +76: non-zero = connected */
    uint8_t  _pad2[47];     /* +73..+119 */
} __attribute__((packed)) pad_sample_t;

_Static_assert(sizeof(pad_sample_t) == 120, "pad sample must be 120 bytes");

static int g_handle = -1;

/* Accumulated state from both Joy-Cons */
static joycon_state_t g_state_l;
static joycon_state_t g_state_r;

int vpad_init(void) {
    scePadInit();

    /* Get the first logged-in user */
    SceUserServiceUserId userId;
    if (sceUserServiceGetInitialUser(&userId) < 0) {
        log_line("vpad: sceUserServiceGetInitialUser failed");
        userId = 0xfe; /* fallback: system user */
    }

    /* type=5 = virtual device, index=0 */
    int ret = scePadVirtualDeviceAddDevice(userId, 5, 0, NULL, &g_handle);
    if (ret < 0) {
        log_line("vpad: AddDevice failed: 0x%08x", ret);
        return 0;
    }
    log_line("vpad: virtual DualSense added, handle=%d", g_handle);
    return 1;
}

static void push_state(void) {
    if (g_handle < 0) return;

    pad_sample_t s;
    memset(&s, 0, sizeof(s));

    /* Map Joy-Con L buttons → DualSense left side */
    if (g_state_l.connected) {
        if (g_state_l.btn_dpad_up)    s.buttons |= PAD_BTN_UP;
        if (g_state_l.btn_dpad_down)  s.buttons |= PAD_BTN_DOWN;
        if (g_state_l.btn_dpad_left)  s.buttons |= PAD_BTN_LEFT;
        if (g_state_l.btn_dpad_right) s.buttons |= PAD_BTN_RIGHT;
        if (g_state_l.btn_l)          s.buttons |= PAD_BTN_L1;
        if (g_state_l.btn_zl)         s.buttons |= PAD_BTN_L2;
        if (g_state_l.btn_minus)      s.buttons |= PAD_BTN_CREATE;
        if (g_state_l.btn_stick_l)    s.buttons |= PAD_BTN_L3;
        if (g_state_l.btn_capture)    s.buttons |= PAD_BTN_TOUCHPAD;
        /* Left stick */
        s.lx = (uint8_t)((g_state_l.lx / 256) + 128);
        s.ly = (uint8_t)(128 - (g_state_l.ly / 256));
        /* ZL as analog trigger */
        s.l2 = g_state_l.btn_zl ? 255 : 0;
    } else {
        s.lx = 128; s.ly = 128;
    }

    /* Map Joy-Con R buttons → DualSense right side */
    if (g_state_r.connected) {
        if (g_state_r.btn_a)        s.buttons |= PAD_BTN_CIRCLE;
        if (g_state_r.btn_b)        s.buttons |= PAD_BTN_CROSS;
        if (g_state_r.btn_x)        s.buttons |= PAD_BTN_TRIANGLE;
        if (g_state_r.btn_y)        s.buttons |= PAD_BTN_SQUARE;
        if (g_state_r.btn_r)        s.buttons |= PAD_BTN_R1;
        if (g_state_r.btn_zr)       s.buttons |= PAD_BTN_R2;
        if (g_state_r.btn_plus)     s.buttons |= PAD_BTN_OPTIONS;
        if (g_state_r.btn_stick_r)  s.buttons |= PAD_BTN_R3;
        if (g_state_r.btn_home)     s.buttons |= PAD_BTN_MUTE;
        /* Right stick */
        s.rx = (uint8_t)((g_state_r.rx / 256) + 128);
        s.ry = (uint8_t)(128 - (g_state_r.ry / 256));
        s.r2 = g_state_r.btn_zr ? 255 : 0;

        /* Gyro/accel from Right Joy-Con (primary for Just Dance) */
        s.accel_x = g_state_r.accel_x;
        s.accel_y = g_state_r.accel_y;
        s.accel_z = g_state_r.accel_z;
        s.gyro_x  = g_state_r.gyro_x;
        s.gyro_y  = g_state_r.gyro_y;
        s.gyro_z  = g_state_r.gyro_z;
    } else {
        s.rx = 128; s.ry = 128;
    }

    s.connected = (g_state_l.connected || g_state_r.connected) ? 0x10 : 0;

    int ret = scePadVirtualDeviceInsertData(g_handle, &s, 1);
    if (ret < 0)
        log_line("vpad: InsertData failed: 0x%08x", ret);
}

void vpad_on_joycon(int idx, const joycon_state_t *state) {
    if (idx == 0)
        g_state_l = *state;
    else if (idx == 1)
        g_state_r = *state;
    push_state();
}

void vpad_cleanup(void) {
    if (g_handle >= 0) {
        scePadVirtualDeviceDeleteDevice(g_handle);
        g_handle = -1;
    }
}
