#pragma once
#include <stdint.h>
#include "hci.h"

/* L2CAP PSMs */
#define L2CAP_PSM_HID_CTRL   0x0011
#define L2CAP_PSM_HID_INTR   0x0013
#define L2CAP_PSM_AVDTP      0x0019

/* L2CAP signalling CID */
#define L2CAP_CID_SIGNALLING 0x0001

/* L2CAP command codes */
#define L2CAP_CMD_REJ        0x01
#define L2CAP_CMD_CONN_REQ   0x02
#define L2CAP_CMD_CONN_RSP   0x03
#define L2CAP_CMD_CONF_REQ   0x04
#define L2CAP_CMD_CONF_RSP   0x05
#define L2CAP_CMD_DISC_REQ   0x06
#define L2CAP_CMD_DISC_RSP   0x07

/* Connection result codes */
#define L2CAP_CR_SUCCESS     0x0000
#define L2CAP_CR_PENDING     0x0001
#define L2CAP_CR_PSM_NOT_SUP 0x0002

/* Max concurrent channels */
#define L2CAP_MAX_CHANNELS   16

typedef void (*l2cap_data_cb_t)(uint16_t cid, const uint8_t *data, int len, void *ctx);
typedef void (*l2cap_conn_cb_t)(uint16_t cid, int connected, void *ctx);

/* Initialise L2CAP layer, hooks into HCI callbacks */
void l2cap_init(void);

/* Connect to remote PSM. Returns local CID (>0) or 0 on error.
 * Blocks until connected or timeout. */
uint16_t l2cap_connect(uint16_t acl_handle, uint16_t psm);

/* Send data on a CID */
int l2cap_send(uint16_t cid, const void *data, int len);

/* Register callbacks for a PSM (for incoming connections) */
void l2cap_listen(uint16_t psm, l2cap_conn_cb_t conn_cb, l2cap_data_cb_t data_cb, void *ctx);

/* Close a channel */
void l2cap_disconnect(uint16_t cid);

/* Called internally from HCI ACL callback */
void l2cap_on_acl(uint16_t acl_handle, uint8_t pb, const uint8_t *data, int len, void *ctx);
