#pragma once
#include <stdint.h>

typedef enum {
    JC_IDLE = 0,
    JC_SCANNING,
    JC_CONNECTED,
} jc_state_t;

typedef enum {
    HP_IDLE = 0,
    HP_CONNECTING,
    HP_CONNECTED,
    HP_NO_SAVED,
} hp_state_t;

typedef struct {
    jc_state_t jc_l;
    jc_state_t jc_r;
    hp_state_t hp;
    char       hp_addr[20];   /* "AA:BB:CC:DD:EE:FF\0" */
    int        vpad_active;
    int        hci_ok;
} bt_status_t;

/* Called from main — starts HCI + background thread */
int  bt_mgr_init(void);

/* UI commands (thread-safe) */
void bt_mgr_scan_start(void);
void bt_mgr_scan_stop(void);
void bt_mgr_hp_connect(void);
void bt_mgr_hp_disconnect(void);

/* Status snapshot (thread-safe) */
bt_status_t bt_mgr_status(void);

void bt_mgr_shutdown(void);
