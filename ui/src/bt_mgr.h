#pragma once
#include <stdint.h>
#include "../../src/hci.h"

#define BT_MAX_DEVICES 8
#define BT_NAME_MAX    48

typedef enum {
    JC_IDLE = 0,
    JC_SCANNING,
    JC_CONNECTED,
} jc_state_t;

typedef enum {
    HP_IDLE = 0,
    HP_SCANNING,
    HP_PAIRING,
    HP_CONNECTING,
    HP_CONNECTED,
    HP_NO_SAVED,
} hp_state_t;

typedef struct {
    bdaddr_t addr;
    char     name[BT_NAME_MAX];
    uint32_t cod;          /* class of device */
} bt_device_t;

typedef struct {
    jc_state_t  jc_l;
    jc_state_t  jc_r;
    hp_state_t  hp;
    char        hp_addr[20];      /* "AA:BB:CC:DD:EE:FF\0" */
    int         vpad_active;
    int         hci_ok;
    /* BT device scan results */
    bt_device_t devs[BT_MAX_DEVICES];
    int         dev_count;
    int         scan_done;        /* 1 = inquiry complete */
} bt_status_t;

int  bt_mgr_init(void);

void bt_mgr_scan_joycon_start(void);
void bt_mgr_scan_joycon_stop(void);

void bt_mgr_hp_scan_start(void);   /* run BT inquiry to find headphones */
void bt_mgr_hp_scan_stop(void);
void bt_mgr_hp_pair(int dev_idx);  /* pair+connect to found device */
void bt_mgr_hp_connect(void);      /* connect to saved address */
void bt_mgr_hp_disconnect(void);

bt_status_t bt_mgr_status(void);

void bt_mgr_shutdown(void);
