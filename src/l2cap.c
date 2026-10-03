#include "l2cap.h"
#include "hci.h"
#include "log.h"
#include <string.h>
#include "ps5types.h"

/* -----------------------------------------------------------------------
 * Channel table
 * -----------------------------------------------------------------------*/
typedef enum {
    CHAN_FREE = 0,
    CHAN_CONNECTING,
    CHAN_OPEN,
    CHAN_DISCONNECTING
} chan_state_t;

typedef struct {
    chan_state_t     state;
    uint16_t         acl_handle;
    uint16_t         local_cid;
    uint16_t         remote_cid;
    uint16_t         psm;
    l2cap_data_cb_t  data_cb;
    void            *data_ctx;
    /* for connect wait */
    volatile int     connect_done;
    volatile int     connect_result;
} l2cap_chan_t;

typedef struct {
    uint16_t         psm;
    l2cap_conn_cb_t  conn_cb;
    l2cap_data_cb_t  data_cb;
    void            *ctx;
} l2cap_listener_t;

static l2cap_chan_t       g_chans[L2CAP_MAX_CHANNELS];
static l2cap_listener_t   g_listeners[8];
static ScePthreadMutex    g_mtx;
static uint8_t            g_next_sig_id = 1;

/* Fragmentation reassembly: one buffer per ACL handle */
#define ACL_FRAG_BUF 2048
typedef struct {
    uint16_t handle;
    uint8_t  buf[ACL_FRAG_BUF];
    int      used;
    int      total;
} frag_t;
static frag_t g_frags[8];

/* -----------------------------------------------------------------------
 * Internal helpers
 * -----------------------------------------------------------------------*/
static l2cap_chan_t *find_chan_by_local(uint16_t cid) {
    for (int i = 0; i < L2CAP_MAX_CHANNELS; i++)
        if (g_chans[i].state != CHAN_FREE && g_chans[i].local_cid == cid)
            return &g_chans[i];
    return NULL;
}
static l2cap_chan_t *find_chan_by_remote(uint16_t acl, uint16_t rcid) {
    for (int i = 0; i < L2CAP_MAX_CHANNELS; i++)
        if (g_chans[i].acl_handle == acl && g_chans[i].remote_cid == rcid)
            return &g_chans[i];
    return NULL;
}
static l2cap_chan_t *alloc_chan(void) {
    for (int i = 0; i < L2CAP_MAX_CHANNELS; i++)
        if (g_chans[i].state == CHAN_FREE) return &g_chans[i];
    return NULL;
}
static uint16_t alloc_cid(void) {
    static uint16_t next_cid = 0x0040;
    return next_cid++;
}
static uint8_t next_sig_id(void) { return g_next_sig_id++; }

static l2cap_listener_t *find_listener(uint16_t psm) {
    for (int i = 0; i < 8; i++)
        if (g_listeners[i].psm == psm) return &g_listeners[i];
    return NULL;
}

/* Build and send an L2CAP signalling command on a given ACL handle */
static void send_sig(uint16_t acl_handle, uint8_t code, uint8_t id,
                     const void *payload, uint16_t plen) {
    uint8_t pkt[4 + 4 + 64];
    /* L2CAP header */
    uint16_t l2len = 4 + plen; /* sig header = 4 bytes */
    pkt[0] = l2len & 0xff;
    pkt[1] = l2len >> 8;
    pkt[2] = L2CAP_CID_SIGNALLING & 0xff;
    pkt[3] = L2CAP_CID_SIGNALLING >> 8;
    /* Signalling command */
    pkt[4] = code;
    pkt[5] = id;
    pkt[6] = plen & 0xff;
    pkt[7] = plen >> 8;
    if (plen && payload)
        memcpy(pkt + 8, payload, plen);
    if (!hci_credit_wait()) return;
    hci_acl_write(acl_handle, 0x2, pkt, 4 + 4 + plen);
}

/* Send L2CAP data on a channel */
int l2cap_send(uint16_t cid, const void *data, int len) {
    scePthreadMutexLock(&g_mtx);
    l2cap_chan_t *c = find_chan_by_local(cid);
    if (!c || c->state != CHAN_OPEN) {
        scePthreadMutexUnlock(&g_mtx);
        return 0;
    }
    uint16_t acl  = c->acl_handle;
    uint16_t rcid = c->remote_cid;
    scePthreadMutexUnlock(&g_mtx);

    uint8_t pkt[4 + 1024];
    uint16_t l2len = (uint16_t)len;
    pkt[0] = l2len & 0xff;
    pkt[1] = l2len >> 8;
    pkt[2] = rcid & 0xff;
    pkt[3] = rcid >> 8;
    memcpy(pkt + 4, data, len);
    if (!hci_credit_wait()) return 0;
    return hci_acl_write(acl, 0x2, pkt, 4 + len);
}

/* -----------------------------------------------------------------------
 * Signalling command handlers
 * -----------------------------------------------------------------------*/
static void handle_conn_req(uint16_t acl, uint8_t id, const uint8_t *p, int len) {
    if (len < 4) return;
    uint16_t psm  = (uint16_t)(p[0] | (p[1] << 8));
    uint16_t scid = (uint16_t)(p[2] | (p[3] << 8));
    log_line("l2cap: CONN_REQ psm=0x%04x scid=0x%04x", psm, scid);

    l2cap_listener_t *lsn = find_listener(psm);
    if (!lsn) {
        /* Send reject: PSM not supported */
        uint8_t rsp[8] = { 0, 0, scid & 0xff, scid >> 8, L2CAP_CR_PSM_NOT_SUP & 0xff, L2CAP_CR_PSM_NOT_SUP >> 8, 0, 0 };
        send_sig(acl, L2CAP_CMD_CONN_RSP, id, rsp, 8);
        return;
    }

    scePthreadMutexLock(&g_mtx);
    l2cap_chan_t *c = alloc_chan();
    if (!c) {
        scePthreadMutexUnlock(&g_mtx);
        return;
    }
    c->state       = CHAN_CONNECTING;
    c->acl_handle  = acl;
    c->local_cid   = alloc_cid();
    c->remote_cid  = scid;
    c->psm         = psm;
    c->data_cb     = lsn->data_cb;
    c->data_ctx    = lsn->ctx;
    uint16_t lcid  = c->local_cid;
    scePthreadMutexUnlock(&g_mtx);

    /* Accept */
    uint8_t rsp[8];
    rsp[0] = lcid & 0xff; rsp[1] = lcid >> 8;
    rsp[2] = scid & 0xff; rsp[3] = scid >> 8;
    rsp[4] = L2CAP_CR_SUCCESS & 0xff; rsp[5] = 0;
    rsp[6] = 0; rsp[7] = 0; /* status = no further info */
    send_sig(acl, L2CAP_CMD_CONN_RSP, id, rsp, 8);

    /* Send config request (empty = accept defaults) */
    uint8_t creq[4] = { scid & 0xff, scid >> 8, 0, 0 };
    send_sig(acl, L2CAP_CMD_CONF_REQ, next_sig_id(), creq, 4);
}

static void handle_conn_rsp(uint16_t acl, uint8_t id, const uint8_t *p, int len) {
    if (len < 8) return;
    uint16_t dcid   = (uint16_t)(p[0] | (p[1] << 8));  /* local CID we allocated */
    uint16_t scid   = (uint16_t)(p[2] | (p[3] << 8));  /* remote CID */
    uint16_t result = (uint16_t)(p[4] | (p[5] << 8));
    (void)id;
    log_line("l2cap: CONN_RSP dcid=0x%04x scid=0x%04x result=%d", dcid, scid, result);

    scePthreadMutexLock(&g_mtx);
    l2cap_chan_t *c = find_chan_by_local(dcid);
    if (!c) { scePthreadMutexUnlock(&g_mtx); return; }

    if (result == L2CAP_CR_PENDING) {
        scePthreadMutexUnlock(&g_mtx);
        return; /* wait for final response */
    }

    if (result != L2CAP_CR_SUCCESS) {
        c->connect_result = result;
        c->connect_done   = 1;
        c->state          = CHAN_FREE;
        scePthreadMutexUnlock(&g_mtx);
        return;
    }

    c->remote_cid = scid;
    scePthreadMutexUnlock(&g_mtx);

    /* Send configuration request */
    uint8_t creq[4] = { scid & 0xff, scid >> 8, 0, 0 };
    send_sig(acl, L2CAP_CMD_CONF_REQ, next_sig_id(), creq, 4);
}

static void handle_conf_req(uint16_t acl, uint8_t id, const uint8_t *p, int len) {
    if (len < 4) return;
    uint16_t dcid = (uint16_t)(p[0] | (p[1] << 8));
    /* Accept with empty config */
    uint8_t rsp[6];
    rsp[0] = dcid & 0xff; rsp[1] = dcid >> 8;
    rsp[2] = 0; rsp[3] = 0; /* flags */
    rsp[4] = 0; rsp[5] = 0; /* result = success */
    send_sig(acl, L2CAP_CMD_CONF_RSP, id, rsp, 6);

    /* Mark channel open after we get conf req AND conf rsp */
    scePthreadMutexLock(&g_mtx);
    l2cap_chan_t *c = find_chan_by_local(dcid);
    if (c && c->state == CHAN_CONNECTING) {
        c->state          = CHAN_OPEN;
        c->connect_result = 0;
        c->connect_done   = 1;
        log_line("l2cap: channel 0x%04x open (incoming cfg)", dcid);
    }
    scePthreadMutexUnlock(&g_mtx);
}

static void handle_conf_rsp(uint16_t acl, uint8_t id, const uint8_t *p, int len) {
    if (len < 6) return;
    uint16_t scid   = (uint16_t)(p[0] | (p[1] << 8));
    uint16_t result = (uint16_t)(p[4] | (p[5] << 8));
    (void)acl; (void)id;

    scePthreadMutexLock(&g_mtx);
    l2cap_chan_t *c = find_chan_by_remote(acl, scid);
    if (c && c->state == CHAN_CONNECTING && result == 0) {
        c->state          = CHAN_OPEN;
        c->connect_result = 0;
        c->connect_done   = 1;
        log_line("l2cap: channel 0x%04x open (outgoing cfg rsp)", c->local_cid);
    }
    scePthreadMutexUnlock(&g_mtx);
}

static void handle_disc_req(uint16_t acl, uint8_t id, const uint8_t *p, int len) {
    if (len < 4) return;
    uint16_t dcid = (uint16_t)(p[0] | (p[1] << 8));
    uint16_t scid = (uint16_t)(p[2] | (p[3] << 8));

    uint8_t rsp[4] = { dcid & 0xff, dcid >> 8, scid & 0xff, scid >> 8 };
    send_sig(acl, L2CAP_CMD_DISC_RSP, id, rsp, 4);

    scePthreadMutexLock(&g_mtx);
    l2cap_chan_t *c = find_chan_by_local(dcid);
    if (c) { c->state = CHAN_FREE; memset(c, 0, sizeof(*c)); }
    scePthreadMutexUnlock(&g_mtx);
}

static void dispatch_signalling(uint16_t acl, const uint8_t *p, int len) {
    while (len >= 4) {
        uint8_t  code = p[0];
        uint8_t  id   = p[1];
        uint16_t clen = (uint16_t)(p[2] | (p[3] << 8));
        if (4 + clen > (uint16_t)len) break;
        const uint8_t *payload = p + 4;

        switch (code) {
        case L2CAP_CMD_CONN_REQ:  handle_conn_req(acl, id, payload, clen); break;
        case L2CAP_CMD_CONN_RSP:  handle_conn_rsp(acl, id, payload, clen); break;
        case L2CAP_CMD_CONF_REQ:  handle_conf_req(acl, id, payload, clen); break;
        case L2CAP_CMD_CONF_RSP:  handle_conf_rsp(acl, id, payload, clen); break;
        case L2CAP_CMD_DISC_REQ:  handle_disc_req(acl, id, payload, clen); break;
        default: break;
        }
        len -= 4 + clen;
        p   += 4 + clen;
    }
}

/* -----------------------------------------------------------------------
 * ACL reassembly & dispatch
 * -----------------------------------------------------------------------*/
void l2cap_on_acl(uint16_t acl_handle, uint8_t pb, const uint8_t *data, int len, void *ctx) {
    frag_t *f = NULL;
    for (int i = 0; i < 8; i++) {
        if (g_frags[i].handle == acl_handle || g_frags[i].handle == 0) {
            f = &g_frags[i];
            break;
        }
    }
    if (!f) return;

    if (pb == 0x2) {
        /* New PDU — start of L2CAP frame */
        f->handle = acl_handle;
        f->used   = 0;
        f->total  = 0;
    }

    if (f->used + len > ACL_FRAG_BUF) return;
    memcpy(f->buf + f->used, data, len);
    f->used += len;

    if (f->used < 4) return;  /* need at least L2CAP header */

    if (f->total == 0)
        f->total = 4 + (int)(f->buf[0] | (f->buf[1] << 8));

    if (f->used < f->total) return;  /* wait for more fragments */

    /* Complete L2CAP PDU */
    uint16_t l2_len = (uint16_t)(f->buf[0] | (f->buf[1] << 8));
    uint16_t cid    = (uint16_t)(f->buf[2] | (f->buf[3] << 8));
    const uint8_t *payload = f->buf + 4;

    if (cid == L2CAP_CID_SIGNALLING) {
        dispatch_signalling(acl_handle, payload, l2_len);
    } else {
        scePthreadMutexLock(&g_mtx);
        l2cap_chan_t *c = find_chan_by_local(cid);
        if (c && c->state == CHAN_OPEN && c->data_cb) {
            l2cap_data_cb_t cb = c->data_cb;
            void *ctx = c->data_ctx;
            scePthreadMutexUnlock(&g_mtx);
            cb(cid, payload, l2_len, ctx);
        } else {
            scePthreadMutexUnlock(&g_mtx);
        }
    }

    f->used  = 0;
    f->total = 0;
}

/* -----------------------------------------------------------------------
 * Public API
 * -----------------------------------------------------------------------*/
void l2cap_init(void) {
    memset(g_chans, 0, sizeof(g_chans));
    memset(g_listeners, 0, sizeof(g_listeners));
    memset(g_frags, 0, sizeof(g_frags));
    scePthreadMutexInit(&g_mtx, NULL, NULL);
    /* Wire HCI ACL callback */
    hci_set_acl_cb(l2cap_on_acl, NULL);
}

uint16_t l2cap_connect(uint16_t acl_handle, uint16_t psm) {
    scePthreadMutexLock(&g_mtx);
    l2cap_chan_t *c = alloc_chan();
    if (!c) { scePthreadMutexUnlock(&g_mtx); return 0; }
    c->state         = CHAN_CONNECTING;
    c->acl_handle    = acl_handle;
    c->local_cid     = alloc_cid();
    c->remote_cid    = 0;
    c->psm           = psm;
    c->connect_done  = 0;
    c->connect_result = -1;
    uint16_t lcid    = c->local_cid;
    scePthreadMutexUnlock(&g_mtx);

    uint8_t req[4];
    req[0] = psm & 0xff; req[1] = psm >> 8;
    req[2] = lcid & 0xff; req[3] = lcid >> 8;
    send_sig(acl_handle, L2CAP_CMD_CONN_REQ, next_sig_id(), req, 4);

    /* Wait for connect_done */
    for (int i = 0; i < 100; i++) {
        struct timespec ts = {0, 50*1000*1000}; /* 50ms */
        nanosleep(&ts, NULL);
        if (c->connect_done) break;
    }

    if (!c->connect_done || c->connect_result != 0) {
        scePthreadMutexLock(&g_mtx);
        c->state = CHAN_FREE;
        scePthreadMutexUnlock(&g_mtx);
        return 0;
    }
    return lcid;
}

void l2cap_listen(uint16_t psm, l2cap_conn_cb_t conn_cb, l2cap_data_cb_t data_cb, void *ctx) {
    for (int i = 0; i < 8; i++) {
        if (!g_listeners[i].psm) {
            g_listeners[i].psm     = psm;
            g_listeners[i].conn_cb = conn_cb;
            g_listeners[i].data_cb = data_cb;
            g_listeners[i].ctx     = ctx;
            return;
        }
    }
}

void l2cap_disconnect(uint16_t cid) {
    scePthreadMutexLock(&g_mtx);
    l2cap_chan_t *c = find_chan_by_local(cid);
    if (!c || c->state != CHAN_OPEN) { scePthreadMutexUnlock(&g_mtx); return; }
    uint16_t acl  = c->acl_handle;
    uint16_t rcid = c->remote_cid;
    c->state = CHAN_DISCONNECTING;
    scePthreadMutexUnlock(&g_mtx);

    uint8_t req[4] = { rcid & 0xff, rcid >> 8, cid & 0xff, cid >> 8 };
    send_sig(acl, L2CAP_CMD_DISC_REQ, next_sig_id(), req, 4);
}
