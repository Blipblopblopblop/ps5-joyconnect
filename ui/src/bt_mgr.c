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
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

typedef enum {
    CMD_NONE = 0,
    CMD_JC_SCAN_START,
    CMD_JC_SCAN_STOP,
    CMD_HP_SCAN_START,
    CMD_HP_SCAN_STOP,
    CMD_HP_PAIR,       /* g_pair_idx holds index into g_status.devs */
    CMD_HP_CONNECT,
    CMD_HP_DISCONNECT,
} bt_cmd_t;

static volatile bt_cmd_t g_cmd      = CMD_NONE;
static volatile int      g_pair_idx = 0;
static volatile int      g_running  = 0;
static ScePthread         g_pump_tid;
static ScePthread         g_cmd_tid;

static ScePthreadMutex g_mtx;
static bt_status_t     g_status;

/* -----------------------------------------------------------------------
 * Link-key storage — /data/joyconnect/linkkeys.bin
 * Each record: 6-byte BD_ADDR + 16-byte key = 22 bytes
 * ----------------------------------------------------------------------- */
#define KEY_FILE    "/data/joyconnect/linkkeys.bin"
#define KEY_RECORD  22

static int find_linkkey(const bdaddr_t *addr, uint8_t *key_out) {
    int fd = open(KEY_FILE, O_RDONLY);
    if (fd < 0) return 0;
    uint8_t rec[KEY_RECORD];
    int found = 0;
    while (read(fd, rec, KEY_RECORD) == KEY_RECORD) {
        if (memcmp(rec, addr->b, 6) == 0) {
            memcpy(key_out, rec + 6, 16);
            found = 1;
            break;
        }
    }
    close(fd);
    return found;
}

static void save_linkkey(const bdaddr_t *addr, const uint8_t *key) {
    /* Read existing, replace if addr present, else append */
    int fd = open(KEY_FILE, O_RDONLY);
    uint8_t buf[KEY_RECORD * 16];
    int n = 0;
    if (fd >= 0) {
        uint8_t rec[KEY_RECORD];
        while (n < 16 && read(fd, rec, KEY_RECORD) == KEY_RECORD) {
            if (memcmp(rec, addr->b, 6) != 0) {
                memcpy(buf + n * KEY_RECORD, rec, KEY_RECORD);
                n++;
            }
        }
        close(fd);
    }
    memcpy(buf + n * KEY_RECORD,       addr->b, 6);
    memcpy(buf + n * KEY_RECORD + 6,   key, 16);
    n++;
    fd = open(KEY_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd >= 0) {
        write(fd, buf, (size_t)(n * KEY_RECORD));
        close(fd);
    }
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

static void save_hp_addr(const bdaddr_t *addr) {
    system("mkdir -p /data/joyconnect");
    int fd = open("/data/joyconnect/headphone.addr", O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd >= 0) { write(fd, addr->b, 6); close(fd); }
}

/* -----------------------------------------------------------------------
 * EIR parser — extract device name from Extended Inquiry Response
 * ----------------------------------------------------------------------- */
static void parse_eir(const uint8_t *eir, int eir_len, char *name, int name_max) {
    name[0] = '\0';
    int pos = 0;
    int have_short = 0;
    while (pos < eir_len - 1) {
        int len = eir[pos];
        if (len == 0) break;
        if (pos + len >= eir_len) break;
        uint8_t type = eir[pos + 1];
        if (type == 0x08 || type == 0x09) {
            int n = len - 1;
            if (n >= name_max) n = name_max - 1;
            if (type == 0x09 || !have_short) {
                memcpy(name, &eir[pos + 2], n);
                name[n] = '\0';
                have_short = 1;
            }
            if (type == 0x09) return;
        }
        pos += 1 + len;
    }
}

/* -----------------------------------------------------------------------
 * Scan callback — inquiry results + SSP pairing events
 * Called from hci_pump thread for every HCI event (alongside joycon callback)
 * ----------------------------------------------------------------------- */
static void scan_evt_cb(uint8_t evt, const uint8_t *p, int plen, void *ctx) {
    (void)ctx;

    /* ---- HCI_EVT_EXT_INQUIRY_RESULT (0x2F) — device name in EIR ---- */
    if (evt == HCI_EVT_EXT_INQUIRY_RESULT && plen >= 15) {
        bdaddr_t addr;
        memcpy(addr.b, p + 1, 6);                  /* p[0] = num_responses = 1 */
        uint32_t cod = (uint32_t)(p[9] | (p[10]<<8) | (p[11]<<16));
        const uint8_t *eir = p + 14;
        int eir_len = plen - 14;
        char name[BT_NAME_MAX] = {0};
        parse_eir(eir, eir_len, name, sizeof(name));
        if (!name[0]) {
            snprintf(name, sizeof(name), "%02X:%02X:%02X:%02X:%02X:%02X",
                     addr.b[5], addr.b[4], addr.b[3],
                     addr.b[2], addr.b[1], addr.b[0]);
        }
        scePthreadMutexLock(&g_mtx);
        /* Update existing entry or add new */
        int found = 0;
        for (int i = 0; i < g_status.dev_count; i++) {
            if (bdaddr_eq(&g_status.devs[i].addr, &addr)) {
                memcpy(g_status.devs[i].name, name, sizeof(name));
                g_status.devs[i].cod = cod;
                found = 1; break;
            }
        }
        if (!found && g_status.dev_count < BT_MAX_DEVICES) {
            g_status.devs[g_status.dev_count].addr = addr;
            memcpy(g_status.devs[g_status.dev_count].name, name, sizeof(name));
            g_status.devs[g_status.dev_count].cod = cod;
            g_status.dev_count++;
        }
        scePthreadMutexUnlock(&g_mtx);
    }

    /* ---- HCI_EVT_INQUIRY_RESULT_WITH_RSSI (0x22) — no EIR name ---- */
    if (evt == HCI_EVT_INQUIRY_RESULT_WITH_RSSI && plen >= 2) {
        int n = p[0];
        for (int i = 0; i < n && 1 + i*14 + 13 < plen; i++) {
            const uint8_t *r = p + 1 + i*14;
            bdaddr_t addr;
            memcpy(addr.b, r, 6);
            uint32_t cod = (uint32_t)(r[8] | (r[9]<<8) | (r[10]<<16));
            char name[BT_NAME_MAX];
            snprintf(name, sizeof(name), "%02X:%02X:%02X:%02X:%02X:%02X",
                     addr.b[5], addr.b[4], addr.b[3],
                     addr.b[2], addr.b[1], addr.b[0]);
            scePthreadMutexLock(&g_mtx);
            int found = 0;
            for (int j = 0; j < g_status.dev_count; j++) {
                if (bdaddr_eq(&g_status.devs[j].addr, &addr)) { found = 1; break; }
            }
            if (!found && g_status.dev_count < BT_MAX_DEVICES) {
                g_status.devs[g_status.dev_count].addr = addr;
                memcpy(g_status.devs[g_status.dev_count].name, name, sizeof(name));
                g_status.devs[g_status.dev_count].cod = cod;
                g_status.dev_count++;
            }
            scePthreadMutexUnlock(&g_mtx);
        }
    }

    /* ---- HCI_EVT_INQUIRY_COMPLETE (0x01) ---- */
    if (evt == HCI_EVT_INQUIRY_COMPLETE) {
        scePthreadMutexLock(&g_mtx);
        g_status.hp      = HP_IDLE;
        g_status.scan_done = 1;
        scePthreadMutexUnlock(&g_mtx);
        log_line("bt_mgr: inquiry complete, %d devices", g_status.dev_count);
    }

    /* ---- SSP: IO capability request — send NoInputNoOutput reply ---- */
    if (evt == HCI_EVT_IO_CAPABILITY_REQUEST && plen >= 6) {
        bdaddr_t addr;
        memcpy(addr.b, p, 6);
        uint8_t rep[9];
        memcpy(rep, addr.b, 6);
        rep[6] = 0x03;  /* IO_Capability: NoInputNoOutput */
        rep[7] = 0x00;  /* OOB_Data_Present: none */
        rep[8] = 0x04;  /* Auth_Requirements: general bonding, no MITM */
        hci_cmd(HCI_IO_CAPABILITY_REPLY, rep, 9);
        log_line("bt_mgr: SSP IO capability reply sent");
    }

    /* ---- SSP: User confirmation — auto-accept (Just Works) ---- */
    if (evt == HCI_EVT_USER_CONFIRM_REQUEST && plen >= 6) {
        bdaddr_t addr;
        memcpy(addr.b, p, 6);
        hci_cmd(HCI_USER_CONFIRM_REPLY, addr.b, 6);
        log_line("bt_mgr: SSP user confirm accepted");
    }

    /* ---- Link key notification — save it ---- */
    if (evt == HCI_EVT_LINK_KEY_NOTIF && plen >= 23) {
        bdaddr_t addr;
        memcpy(addr.b, p, 6);
        save_linkkey(&addr, p + 6);
        log_line("bt_mgr: link key saved");
    }

    /* ---- Link key request — reply with saved key or negative ---- */
    if (evt == HCI_EVT_LINK_KEY_REQ && plen >= 6) {
        bdaddr_t addr;
        memcpy(addr.b, p, 6);
        uint8_t key[16];
        if (find_linkkey(&addr, key)) {
            uint8_t rep[22];
            memcpy(rep, addr.b, 6);
            memcpy(rep + 6, key, 16);
            hci_cmd(HCI_LINK_KEY_REQ_REPLY, rep, 22);
        } else {
            hci_cmd(HCI_LINK_KEY_REQ_NEG_REPLY, addr.b, 6);
        }
    }

    /* ---- PIN code request (legacy) — send empty PIN ---- */
    if (evt == HCI_EVT_PIN_CODE_REQ && plen >= 6) {
        bdaddr_t addr;
        memcpy(addr.b, p, 6);
        uint8_t rep[23];
        memcpy(rep, addr.b, 6);
        rep[6] = 0;                /* PIN_Code_Length = 0 */
        memset(rep + 7, 0, 16);    /* PIN_Code = all zeros */
        hci_cmd(HCI_PIN_CODE_REQ_REPLY, rep, 23);
    }
}

/* -----------------------------------------------------------------------
 * Joy-Con callback
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
 * HCI pump thread — runs continuously
 * ----------------------------------------------------------------------- */
static void *pump_thread(void *arg) {
    (void)arg;
    struct timespec ts = {0, 100 * 1000};
    while (g_running) {
        hci_pump();
        nanosleep(&ts, NULL);
    }
    return NULL;
}

/* -----------------------------------------------------------------------
 * Command thread — handles blocking operations (a2dp_connect etc.)
 * ----------------------------------------------------------------------- */
static void *cmd_thread(void *arg) {
    (void)arg;
    struct timespec ts = {0, 5 * 1000 * 1000};  /* 5ms */

    while (g_running) {
        bt_cmd_t cmd = g_cmd;
        if (cmd == CMD_NONE) { nanosleep(&ts, NULL); continue; }
        g_cmd = CMD_NONE;

        switch (cmd) {
        case CMD_JC_SCAN_START:
            joycon_start(on_joycon, NULL);
            scePthreadMutexLock(&g_mtx);
            if (g_status.jc_l != JC_CONNECTED) g_status.jc_l = JC_SCANNING;
            if (g_status.jc_r != JC_CONNECTED) g_status.jc_r = JC_SCANNING;
            scePthreadMutexUnlock(&g_mtx);
            log_line("bt_mgr: Joy-Con scan started");
            break;

        case CMD_JC_SCAN_STOP:
            joycon_stop();
            scePthreadMutexLock(&g_mtx);
            if (g_status.jc_l == JC_SCANNING) g_status.jc_l = JC_IDLE;
            if (g_status.jc_r == JC_SCANNING) g_status.jc_r = JC_IDLE;
            scePthreadMutexUnlock(&g_mtx);
            break;

        case CMD_HP_SCAN_START: {
            scePthreadMutexLock(&g_mtx);
            g_status.dev_count = 0;
            g_status.scan_done = 0;
            g_status.hp = HP_SCANNING;
            scePthreadMutexUnlock(&g_mtx);
            /* Enable SSP + general inquiry scan */
            uint8_t ssp = 0x01;
            hci_sync(HCI_WRITE_SIMPLE_PAIRING_MODE, &ssp, 1);
            /* HCI_INQUIRY: LAP=0x9E8B33, length=10 (~12.8s), max_responses=0 */
            uint8_t inq[5] = {0x33, 0x8B, 0x9E, 10, 0};
            hci_cmd(HCI_INQUIRY, inq, 5);
            log_line("bt_mgr: BT inquiry started");
            break;
        }

        case CMD_HP_SCAN_STOP:
            hci_cmd(HCI_INQUIRY_CANCEL, NULL, 0);
            scePthreadMutexLock(&g_mtx);
            g_status.hp = HP_IDLE;
            g_status.scan_done = 1;
            scePthreadMutexUnlock(&g_mtx);
            break;

        case CMD_HP_PAIR: {
            int idx = g_pair_idx;
            scePthreadMutexLock(&g_mtx);
            if (idx < 0 || idx >= g_status.dev_count) {
                scePthreadMutexUnlock(&g_mtx);
                break;
            }
            bdaddr_t addr = g_status.devs[idx].addr;
            g_status.hp = HP_PAIRING;
            scePthreadMutexUnlock(&g_mtx);
            log_line("bt_mgr: pairing device %d", idx);
            scePthreadMutexLock(&g_mtx);
            g_status.hp = HP_CONNECTING;
            scePthreadMutexUnlock(&g_mtx);
            int ok = a2dp_connect(&addr);
            if (ok) save_hp_addr(&addr);
            scePthreadMutexLock(&g_mtx);
            g_status.hp = ok ? HP_CONNECTED : HP_IDLE;
            if (ok) {
                snprintf(g_status.hp_addr, sizeof(g_status.hp_addr),
                         "%02X:%02X:%02X:%02X:%02X:%02X",
                         addr.b[5], addr.b[4], addr.b[3],
                         addr.b[2], addr.b[1], addr.b[0]);
            }
            scePthreadMutexUnlock(&g_mtx);
            log_line("bt_mgr: pair %s", ok ? "ok" : "failed");
            break;
        }

        case CMD_HP_CONNECT: {
            bdaddr_t addr;
            memset(&addr, 0, sizeof(addr));
            if (!load_hp_addr(&addr)) {
                scePthreadMutexLock(&g_mtx);
                g_status.hp = HP_NO_SAVED;
                scePthreadMutexUnlock(&g_mtx);
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
            break;

        default:
            break;
        }
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

    /* Register SSP/inquiry scan callback */
    hci_set_scan_cb(scan_evt_cb, NULL);

    /* Enable SSP now so link-key requests work immediately */
    uint8_t ssp = 0x01;
    hci_sync(HCI_WRITE_SIMPLE_PAIRING_MODE, &ssp, 1);

    if (vpad_init())
        g_status.vpad_active = 1;

    /* Load saved HP address */
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
    scePthreadCreate(&g_pump_tid, NULL, pump_thread, NULL, "bt_pump");
    scePthreadDetach(g_pump_tid);
    scePthreadCreate(&g_cmd_tid,  NULL, cmd_thread,  NULL, "bt_cmd");
    scePthreadDetach(g_cmd_tid);
    return 1;
}

void bt_mgr_scan_joycon_start(void) { g_cmd = CMD_JC_SCAN_START; }
void bt_mgr_scan_joycon_stop(void)  { g_cmd = CMD_JC_SCAN_STOP;  }
void bt_mgr_hp_scan_start(void)     { g_cmd = CMD_HP_SCAN_START; }
void bt_mgr_hp_scan_stop(void)      { g_cmd = CMD_HP_SCAN_STOP;  }
void bt_mgr_hp_connect(void)        { g_cmd = CMD_HP_CONNECT;    }
void bt_mgr_hp_disconnect(void)     { g_cmd = CMD_HP_DISCONNECT; }

void bt_mgr_hp_pair(int dev_idx) {
    g_pair_idx = dev_idx;
    g_cmd      = CMD_HP_PAIR;
}

bt_status_t bt_mgr_status(void) {
    scePthreadMutexLock(&g_mtx);
    bt_status_t s = g_status;
    scePthreadMutexUnlock(&g_mtx);
    return s;
}

void bt_mgr_shutdown(void) {
    g_running = 0;
    hci_cmd(HCI_INQUIRY_CANCEL, NULL, 0);
    joycon_stop();
    a2dp_stop();
    vpad_cleanup();
    hci_close();
    log_close();
}
