#include "joycon.h"
#include "hci.h"
#include "l2cap.h"
#include "log.h"
#include <string.h>
#include <stdlib.h>
#include "ps5types.h"

/* Joy-Con BT class of device: 0x002508 (Gamepad, Peripheral Major) */
#define JOYCON_COD_MASK 0xfff

/* Max simultaneously connected Joy-Cons */
#define MAX_JOYCON 2

typedef struct {
    bdaddr_t       addr;
    uint16_t       acl_handle;
    uint16_t       ctrl_cid;   /* L2CAP_PSM_HID_CTRL */
    uint16_t       intr_cid;   /* L2CAP_PSM_HID_INTR */
    int            is_left;
    joycon_state_t state;
    int            active;
} joycon_dev_t;

static joycon_dev_t g_jcs[MAX_JOYCON];
static joycon_state_cb_t g_cb;
static void *g_cb_ctx;
static ScePthreadMutex g_mtx;
static volatile int g_running;

/* Joy-Con subcommand to enable full report 0x30 with IMU */
static void joycon_set_mode(joycon_dev_t *jc) {
    /* HID output report: type 0xa2, report-id 0x01 (rumble+subcmd), subcmd 0x03 (input report mode), arg 0x30 */
    uint8_t pkt[12] = {
        0xa2,       /* HID data | output */
        0x01,       /* report id: rumble+subcommand */
        0x00,       /* sequence number */
        0x00, 0x01, 0x40, 0x40,  /* rumble L: neutral */
        0x00, 0x01, 0x40, 0x40,  /* rumble R: neutral */
        0x03        /* subcmd: set input report mode */
    };
    /* Append arg byte: 0x30 = full report with IMU */
    uint8_t arg = 0x30;
    uint8_t full[13];
    memcpy(full, pkt, 12);
    full[12] = arg;
    l2cap_send(jc->intr_cid, full, 13);
}

/* Enable IMU (subcmd 0x40, arg 0x01) */
static void joycon_enable_imu(joycon_dev_t *jc) {
    uint8_t pkt[13] = {
        0xa2, 0x01, 0x00,
        0x00, 0x01, 0x40, 0x40,
        0x00, 0x01, 0x40, 0x40,
        0x40, 0x01
    };
    l2cap_send(jc->intr_cid, pkt, 13);
}

/* -----------------------------------------------------------------------
 * Parse Joy-Con Report 0x30
 * -----------------------------------------------------------------------*/
static void parse_report30(joycon_dev_t *jc, const uint8_t *p, int len) {
    if (len < 13) return;
    const joycon_report30_t *r = (const joycon_report30_t *)p;
    joycon_state_t *s = &jc->state;

    if (jc->is_left) {
        /* Left Joy-Con buttons (byte 2: down,up,right,left; byte 4 shared) */
        s->btn_dpad_down  = (r->btn[1] >> 0) & 1;
        s->btn_dpad_up    = (r->btn[1] >> 1) & 1;
        s->btn_dpad_right = (r->btn[1] >> 2) & 1;
        s->btn_dpad_left  = (r->btn[1] >> 3) & 1;
        s->btn_minus      = (r->btn[1] >> 4) & 1;
        s->btn_stick_l    = (r->btn[1] >> 6) & 1;
        s->btn_zl         = (r->btn[2] >> 6) & 1;
        s->btn_l          = (r->btn[2] >> 7) & 1;
        s->btn_sl_l       = (r->btn[1] >> 5) & 1;
        s->btn_sr_l       = (r->btn[2] >> 4) & 1;
        s->btn_capture    = (r->btn[2] >> 5) & 1;
        /* Left stick */
        uint16_t lx = r->stick_l[0] | ((r->stick_l[1] & 0xf) << 8);
        uint16_t ly = (r->stick_l[1] >> 4) | (r->stick_l[2] << 4);
        s->lx = (int16_t)(lx - 2048) * 16;
        s->ly = -(int16_t)(ly - 2048) * 16;
    } else {
        /* Right Joy-Con buttons (byte 0) */
        s->btn_y  = (r->btn[0] >> 0) & 1;
        s->btn_x  = (r->btn[0] >> 1) & 1;
        s->btn_b  = (r->btn[0] >> 2) & 1;
        s->btn_a  = (r->btn[0] >> 3) & 1;
        s->btn_sr_r = (r->btn[0] >> 4) & 1;
        s->btn_sl_r = (r->btn[0] >> 5) & 1;
        s->btn_r    = (r->btn[0] >> 6) & 1;
        s->btn_zr   = (r->btn[0] >> 7) & 1;
        s->btn_plus      = (r->btn[2] >> 1) & 1;
        s->btn_home      = (r->btn[2] >> 4) & 1;
        s->btn_stick_r   = (r->btn[2] >> 2) & 1;
        /* Right stick */
        uint16_t rx = r->stick_r[0] | ((r->stick_r[1] & 0xf) << 8);
        uint16_t ry = (r->stick_r[1] >> 4) | (r->stick_r[2] << 4);
        s->rx = (int16_t)(rx - 2048) * 16;
        s->ry = -(int16_t)(ry - 2048) * 16;
    }

    /* IMU: use first frame */
    if (len >= 48) {
        s->accel_x = r->imu[0][0];
        s->accel_y = r->imu[0][1];
        s->accel_z = r->imu[0][2];
        s->gyro_x  = r->imu[0][3];
        s->gyro_y  = r->imu[0][4];
        s->gyro_z  = r->imu[0][5];
    }

    s->is_left    = jc->is_left;
    s->connected  = 1;

    int idx = jc->is_left ? 0 : 1;
    if (g_cb) g_cb(idx, s, g_cb_ctx);
}

/* -----------------------------------------------------------------------
 * L2CAP HID interrupt channel data callback
 * -----------------------------------------------------------------------*/
static void on_hid_data(uint16_t cid, const uint8_t *data, int len, void *ctx) {
    joycon_dev_t *jc = (joycon_dev_t *)ctx;
    if (len < 2) return;
    /* data[0] = 0xa1 (HID input), data[1] = report ID */
    if (data[0] == 0xa1 && data[1] == 0x30)
        parse_report30(jc, data + 2, len - 2);
}

/* -----------------------------------------------------------------------
 * HCI event handler
 * -----------------------------------------------------------------------*/
static void on_hci_evt(uint8_t evt, const uint8_t *p, int len, void *ctx);

/* Connect to a Joy-Con whose address we know */
static void connect_joycon(const bdaddr_t *addr, int is_left) {
    /* Check not already connected */
    for (int i = 0; i < MAX_JOYCON; i++)
        if (g_jcs[i].active && bdaddr_eq(&g_jcs[i].addr, addr)) return;

    /* Find free slot */
    joycon_dev_t *jc = NULL;
    for (int i = 0; i < MAX_JOYCON; i++)
        if (!g_jcs[i].active) { jc = &g_jcs[i]; break; }
    if (!jc) return;

    memset(jc, 0, sizeof(*jc));
    jc->addr    = *addr;
    jc->is_left = is_left;
    jc->active  = 1;

    /* HCI Create Connection */
    uint8_t params[13];
    memcpy(params, addr->b, 6);
    /* Packet type: DM1|DH1|DM3|DH3|DM5|DH5 = 0xcc18 */
    params[6] = 0x18; params[7] = 0xcc;
    params[8] = 0x01;  /* page scan repetition mode R1 */
    params[9] = 0x00;
    params[10] = 0x00; params[11] = 0x00; /* clock offset */
    params[12] = 0x01; /* allow role switch */
    hci_cmd(HCI_CREATE_CONNECTION, params, 13);

    log_line("joycon: create_conn to %02x:%02x:%02x:%02x:%02x:%02x left=%d",
             addr->b[5], addr->b[4], addr->b[3],
             addr->b[2], addr->b[1], addr->b[0], is_left);
}

/* Complete a Joy-Con connection after ACL link is established */
static void finish_connect(joycon_dev_t *jc) {
    /* L2CAP HID control channel */
    jc->ctrl_cid = l2cap_connect(jc->acl_handle, L2CAP_PSM_HID_CTRL);
    if (!jc->ctrl_cid) {
        log_line("joycon: l2cap ctrl connect failed");
        jc->active = 0;
        return;
    }
    /* L2CAP HID interrupt channel */
    jc->intr_cid = l2cap_connect(jc->acl_handle, L2CAP_PSM_HID_INTR);
    if (!jc->intr_cid) {
        log_line("joycon: l2cap intr connect failed");
        jc->active = 0;
        return;
    }

    /* Register data callback on interrupt channel */
    l2cap_listen(L2CAP_PSM_HID_INTR, NULL, on_hid_data, jc);

    log_line("joycon: connected ctrl=0x%04x intr=0x%04x", jc->ctrl_cid, jc->intr_cid);
    notify(jc->is_left ? "Joy-Con L connected" : "Joy-Con R connected");

    /* Configure Joy-Con for full input mode with IMU */
    joycon_enable_imu(jc);
    joycon_set_mode(jc);
}

static void on_hci_evt(uint8_t evt, const uint8_t *p, int len, void *ctx) {
    (void)ctx;

    if (evt == HCI_EVT_CONN_COMPLETE && len >= 11) {
        uint8_t status  = p[0];
        uint16_t handle = (uint16_t)(p[1] | (p[2] << 8)) & 0x0fff;
        const bdaddr_t *addr = (const bdaddr_t *)(p + 3);
        if (status != 0) {
            log_line("joycon: CONN_COMPLETE error status=0x%02x", status);
            return;
        }
        /* Find the Joy-Con with this address */
        for (int i = 0; i < MAX_JOYCON; i++) {
            if (g_jcs[i].active && bdaddr_eq(&g_jcs[i].addr, addr)) {
                g_jcs[i].acl_handle = handle;
                finish_connect(&g_jcs[i]);
                return;
            }
        }
    }

    if (evt == HCI_EVT_CONN_REQUEST && len >= 10) {
        const bdaddr_t *addr = (const bdaddr_t *)p;
        uint32_t cod = (uint32_t)(p[6] | (p[7] << 8) | (p[8] << 16));
        uint8_t  link_type = p[9];
        int is_joycon = ((cod >> 8) & 0x1f) == 5 && link_type == 1; /* peripheral class */
        if (is_joycon) {
            /* Accept the incoming connection from a Joy-Con */
            uint8_t params[7];
            memcpy(params, addr->b, 6);
            params[6] = 0x01; /* remain slave */
            hci_cmd(HCI_ACCEPT_CONN_REQ, params, 7);
            log_line("joycon: accepting CONN_REQ from %02x:%02x:%02x:%02x:%02x:%02x",
                     addr->b[5],addr->b[4],addr->b[3],addr->b[2],addr->b[1],addr->b[0]);
            /* Pre-fill address in a slot */
            for (int i = 0; i < MAX_JOYCON; i++) {
                if (!g_jcs[i].active) {
                    memset(&g_jcs[i], 0, sizeof(g_jcs[i]));
                    g_jcs[i].addr   = *addr;
                    g_jcs[i].active = 1;
                    g_jcs[i].is_left = (i == 0); /* heuristic; fixed after link key exchange */
                    break;
                }
            }
        }
        /* Non-Joy-Con Connection_Request: deliberately NOT sending Accept or Reject.
         * The system BT daemon handles it via its own HCI access path. */
    }

    if (evt == HCI_EVT_DISCONNECT_COMPLETE && len >= 4) {
        uint16_t handle = (uint16_t)(p[1] | (p[2] << 8)) & 0x0fff;
        for (int i = 0; i < MAX_JOYCON; i++) {
            if (g_jcs[i].active && g_jcs[i].acl_handle == handle) {
                log_line("joycon: disconnected slot %d", i);
                g_jcs[i].state.connected = 0;
                if (g_cb) g_cb(i, &g_jcs[i].state, g_cb_ctx);
                g_jcs[i].active = 0;
                /* Trigger reconnect */
                bdaddr_t addr = g_jcs[i].addr;
                int is_left   = g_jcs[i].is_left;
                memset(&g_jcs[i], 0, sizeof(g_jcs[i]));
                /* Wait a moment then try again */
                struct timespec ts = {2, 0};
                nanosleep(&ts, NULL);
                connect_joycon(&addr, is_left);
            }
        }
    }
}

/* -----------------------------------------------------------------------
 * Inquiry to find Joy-Cons
 * -----------------------------------------------------------------------*/
static void *scan_thread(void *arg) {
    (void)arg;
    log_line("joycon: scan thread started");

    /* Make ourselves connectable/discoverable */
    uint8_t scan_en = 0x03;
    hci_cmd(HCI_WRITE_SCAN_ENABLE, &scan_en, 1);

    while (g_running) {
        /* If any slot is empty, run inquiry */
        int need_jc = 0;
        for (int i = 0; i < MAX_JOYCON; i++)
            if (!g_jcs[i].active) { need_jc = 1; break; }

        if (need_jc) {
            /* HCI Inquiry: LAP=0x9e8b33 (GIAC), length=5s (0x08), max_responses=4 */
            uint8_t inq[5] = { 0x33, 0x8b, 0x9e, 0x08, 0x04 };
            hci_cmd(HCI_INQUIRY, inq, 5);
            log_line("joycon: inquiry started");
        }

        struct timespec ts = {5, 0};
        nanosleep(&ts, NULL);
    }
    return NULL;
}

/* -----------------------------------------------------------------------
 * Inquiry result handler (called from HCI event callback above in main.c)
 * -----------------------------------------------------------------------*/
void joycon_on_inquiry_result(const bdaddr_t *addr, uint32_t cod) {
    /* Joy-Con CoD: 0x002508 (Gamepad) — check Major Device Class = Peripheral (0x05)
     * and Minor = Gamepad (0x01) */
    uint8_t major = (cod >> 8) & 0x1f;
    uint8_t minor = (cod >> 2) & 0x3f;
    int is_joycon_class = (major == 0x05) && (minor == 0x02 || minor == 0x01);
    if (!is_joycon_class) return;

    /* Heuristic: JoyConL vs R — they pair in order */
    int found_l = 0;
    for (int i = 0; i < MAX_JOYCON; i++) {
        if (g_jcs[i].active) {
            if (g_jcs[i].is_left) found_l = 1;
            (void)found_l; found_l = 1; /* both slots */
        }
    }
    int is_left = !found_l; /* take L first, then R */
    connect_joycon(addr, is_left);
}

/* -----------------------------------------------------------------------
 * Public API
 * -----------------------------------------------------------------------*/
int joycon_start(joycon_state_cb_t cb, void *ctx) {
    g_cb     = cb;
    g_cb_ctx = ctx;
    g_running = 1;
    memset(g_jcs, 0, sizeof(g_jcs));
    scePthreadMutexInit(&g_mtx, NULL, NULL);

    hci_set_evt_cb(on_hci_evt, NULL);

    ScePthread tid;
    if (scePthreadCreate(&tid, NULL, scan_thread, NULL, "jc_scan") < 0)
        return 0;
    scePthreadDetach(tid);
    return 1;
}

int joycon_get_state(int idx, joycon_state_t *out) {
    if (idx < 0 || idx >= MAX_JOYCON) return 0;
    *out = g_jcs[idx].state;
    return g_jcs[idx].active;
}

void joycon_rumble(int idx, int low_amp, int high_amp) {
    if (idx < 0 || idx >= MAX_JOYCON || !g_jcs[idx].active) return;
    uint8_t pkt[11] = {
        0xa2, 0x10, 0x00, /* HID output, report 0x10, sequence */
        /* Left rumble: freq_lo, amp_lo, freq_hi, amp_hi (simplified) */
        0x00, (uint8_t)(low_amp & 0xff), 0x40, (uint8_t)(high_amp & 0xff),
        0x00, (uint8_t)(low_amp & 0xff), 0x40, (uint8_t)(high_amp & 0xff)
    };
    l2cap_send(g_jcs[idx].intr_cid, pkt, 10);
}

void joycon_stop(void) {
    g_running = 0;
    uint8_t scan_en = 0x00;
    hci_cmd(HCI_WRITE_SCAN_ENABLE, &scan_en, 1);
}
