#include "bt_mgr.h"
#include "../../src/hci.h"
#include "../../src/l2cap.h"
#include "../../src/joycon.h"
#include "../../src/vpad.h"
#include "../../src/a2dp.h"
#include "../../src/avcap.h"
#include "../../src/log.h"
#include "../../src/ps5types.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

typedef enum {
    CMD_NONE = 0,
    CMD_SCAN_START,
    CMD_SCAN_STOP,
    CMD_HP_CONNECT,
    CMD_HP_DISCONNECT,
} bt_cmd_t;

static volatile bt_cmd_t g_cmd       = CMD_NONE;
static volatile int      g_running   = 0;
static ScePthread         g_tid;

static ScePthreadMutex g_mtx;
static bt_status_t     g_status;

/* -----------------------------------------------------------------------
 * Joy-Con callback — update status
 * ----------------------------------------------------------------------- */
static void on_joycon(int idx, const joycon_state_t *state, void *ctx) {
    (void)ctx;
    vpad_on_joycon(idx, state);

    scePthreadMutexLock(&g_mtx);
    jc_state_t s = state->connected ? JC_CONNECTED : JC_IDLE;
    if (idx == 0)      g_status.jc_l = s;
    else if (idx == 1) g_status.jc_r = s;
    scePthreadMutexUnlock(&g_mtx);
}

/* -----------------------------------------------------------------------
 * Saved headphone address
 * ----------------------------------------------------------------------- */
static int load_hp_addr(bdaddr_t *out) {
    int fd = open("/data/joyconnect/headphone.addr", O_RDONLY);
    if (fd < 0) return 0;
    int n = (int)read(fd, out->b, 6);
    close(fd);
    return n == 6 && !bdaddr_zero(out);
}

/* -----------------------------------------------------------------------
 * BT background thread
 * ----------------------------------------------------------------------- */
static void *bt_thread(void *arg) {
    (void)arg;
    struct timespec ts = {0, 100 * 1000};

    while (g_running) {
        hci_pump();

        bt_cmd_t cmd = g_cmd;
        if (cmd != CMD_NONE) {
            g_cmd = CMD_NONE;
            switch (cmd) {
            case CMD_SCAN_START:
                joycon_start(on_joycon, NULL);
                scePthreadMutexLock(&g_mtx);
                if (g_status.jc_l != JC_CONNECTED) g_status.jc_l = JC_SCANNING;
                if (g_status.jc_r != JC_CONNECTED) g_status.jc_r = JC_SCANNING;
                scePthreadMutexUnlock(&g_mtx);
                log_line("bt_mgr: Joy-Con scan started");
                break;

            case CMD_SCAN_STOP:
                joycon_stop();
                scePthreadMutexLock(&g_mtx);
                if (g_status.jc_l == JC_SCANNING) g_status.jc_l = JC_IDLE;
                if (g_status.jc_r == JC_SCANNING) g_status.jc_r = JC_IDLE;
                scePthreadMutexUnlock(&g_mtx);
                log_line("bt_mgr: Joy-Con scan stopped");
                break;

            case CMD_HP_CONNECT: {
                bdaddr_t addr;
                memset(&addr, 0, sizeof(addr));
                if (!load_hp_addr(&addr)) {
                    scePthreadMutexLock(&g_mtx);
                    g_status.hp = HP_NO_SAVED;
                    scePthreadMutexUnlock(&g_mtx);
                    log_line("bt_mgr: no saved headphone address");
                    break;
                }
                scePthreadMutexLock(&g_mtx);
                g_status.hp = HP_CONNECTING;
                scePthreadMutexUnlock(&g_mtx);
                int ok = a2dp_connect(&addr);
                scePthreadMutexLock(&g_mtx);
                g_status.hp = ok ? HP_CONNECTED : HP_IDLE;
                scePthreadMutexUnlock(&g_mtx);
                log_line("bt_mgr: hp connect %s", ok ? "ok" : "failed");
                break;
            }

            case CMD_HP_DISCONNECT:
                a2dp_stop();
                scePthreadMutexLock(&g_mtx);
                g_status.hp = HP_IDLE;
                scePthreadMutexUnlock(&g_mtx);
                log_line("bt_mgr: hp disconnected");
                break;

            default:
                break;
            }
        }

        nanosleep(&ts, NULL);
    }
    return NULL;
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */
int bt_mgr_init(void) {
    memset(&g_status, 0, sizeof(g_status));
    scePthreadMutexInit(&g_mtx, NULL, "bt_mgr");

    sceUserServiceInitialize(NULL);

    if (!hci_open()) {
        log_line("bt_mgr: HCI open failed");
        return 0;
    }
    hci_sync(HCI_RESET, NULL, 0);
    l2cap_init();

    if (vpad_init())
        g_status.vpad_active = 1;

    /* Load saved HP address into status string */
    bdaddr_t addr;
    memset(&addr, 0, sizeof(addr));
    if (load_hp_addr(&addr)) {
        snprintf(g_status.hp_addr, sizeof(g_status.hp_addr),
                 "%02X:%02X:%02X:%02X:%02X:%02X",
                 addr.b[5], addr.b[4], addr.b[3],
                 addr.b[2], addr.b[1], addr.b[0]);
    } else {
        snprintf(g_status.hp_addr, sizeof(g_status.hp_addr), "NONE");
    }
    g_status.hci_ok = 1;

    g_running = 1;
    scePthreadCreate(&g_tid, NULL, bt_thread, NULL, "bt_mgr");
    scePthreadDetach(g_tid);
    return 1;
}

void bt_mgr_scan_start(void)     { g_cmd = CMD_SCAN_START; }
void bt_mgr_scan_stop(void)      { g_cmd = CMD_SCAN_STOP;  }
void bt_mgr_hp_connect(void)     { g_cmd = CMD_HP_CONNECT; }
void bt_mgr_hp_disconnect(void)  { g_cmd = CMD_HP_DISCONNECT; }

bt_status_t bt_mgr_status(void) {
    scePthreadMutexLock(&g_mtx);
    bt_status_t s = g_status;
    scePthreadMutexUnlock(&g_mtx);
    return s;
}

void bt_mgr_shutdown(void) {
    g_running = 0;
    joycon_stop();
    a2dp_stop();
    vpad_cleanup();
    hci_close();
    log_close();
}
