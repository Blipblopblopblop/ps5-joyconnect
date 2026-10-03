#pragma once
#include <stdint.h>
#include "hci.h"

/* Joy-Con report 0x30 data — 48 bytes */
typedef struct {
    uint8_t  timer;          /* 0: 8-bit counter */
    uint8_t  bat_conn;       /* 1: battery|connection */
    uint8_t  btn[3];         /* 2-4: button bitmask */
    uint8_t  stick_l[3];     /* 5-7: left stick 12-bit LE */
    uint8_t  stick_r[3];     /* 8-10: right stick 12-bit LE */
    uint8_t  vibrator_input; /* 11 */
    /* IMU: 3 frames × 12 bytes (accel XYZ + gyro XYZ, each int16) */
    int16_t  imu[3][6];      /* 12-47 */
} __attribute__((packed)) joycon_report30_t;

/* Logical Joy-Con state (normalised) */
typedef struct {
    /* Buttons */
    int btn_y, btn_x, btn_b, btn_a;
    int btn_sr_r, btn_sl_r;
    int btn_r, btn_zr;
    int btn_minus, btn_plus;
    int btn_home, btn_capture;
    int btn_stick_r, btn_stick_l;
    int btn_sr_l, btn_sl_l;
    int btn_zl, btn_l;
    int btn_dpad_down, btn_dpad_up, btn_dpad_right, btn_dpad_left;
    /* Sticks: -32768 to +32767 */
    int16_t lx, ly, rx, ry;
    /* Gyro (deg/s * 100) and accel (g * 1000) for Just Dance */
    int16_t gyro_x, gyro_y, gyro_z;
    int16_t accel_x, accel_y, accel_z;
    /* Meta */
    int is_left;    /* 1 = Joy-Con L, 0 = Joy-Con R */
    int connected;
} joycon_state_t;

typedef void (*joycon_state_cb_t)(int idx, const joycon_state_t *state, void *ctx);

/* Start Joy-Con scan and connection loop.
 * Calls back whenever any Joy-Con state changes.
 * Returns 1 on success. */
int joycon_start(joycon_state_cb_t cb, void *ctx);

/* Get last known state for Joy-Con index (0=L, 1=R) */
int joycon_get_state(int idx, joycon_state_t *out);

/* Request Joy-Con to rumble */
void joycon_rumble(int idx, int low_amp, int high_amp);

void joycon_stop(void);
