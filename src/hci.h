#pragma once
#include <stdint.h>
#include <stddef.h>

/* HCI packet types */
#define HCI_TYPE_CMD   0x01
#define HCI_TYPE_ACL   0x02
#define HCI_TYPE_SCO   0x03
#define HCI_TYPE_EVT   0x04

/* HCI opcodes we use (OGF:OCF) */
#define HCI_RESET                       0x0c03
#define HCI_SET_EVENT_MASK              0x0c01
#define HCI_WRITE_SCAN_ENABLE           0x0c1a
#define HCI_WRITE_CLASS_OF_DEVICE       0x0c24
#define HCI_WRITE_LOCAL_NAME            0x0c13
#define HCI_READ_BUFFER_SIZE            0x1005
#define HCI_READ_BD_ADDR                0x1009
#define HCI_INQUIRY                     0x0401
#define HCI_INQUIRY_CANCEL              0x0402
#define HCI_CREATE_CONNECTION           0x0405
#define HCI_DISCONNECT                  0x0406
#define HCI_ACCEPT_CONN_REQ             0x0409
#define HCI_REJECT_CONN_REQ             0x040a
#define HCI_LINK_KEY_REQ_REPLY          0x040b
#define HCI_LINK_KEY_REQ_NEG_REPLY      0x040c
#define HCI_PIN_CODE_REQ_REPLY          0x040d
#define HCI_AUTHENTICATION_REQ          0x0411
#define HCI_SET_CONNECTION_ENCRYPTION   0x0413
#define HCI_WRITE_VOICE_SETTING         0x0c26
#define HCI_HOST_BUFFER_SIZE            0x0c33

/* HCI commands — SSP and inquiry cancel */
#define HCI_INQUIRY_CANCEL              0x0402
#define HCI_WRITE_SIMPLE_PAIRING_MODE   0x0c56
#define HCI_IO_CAPABILITY_REPLY         0x042b
#define HCI_USER_CONFIRM_REPLY          0x042c
#define HCI_USER_CONFIRM_NEG_REPLY      0x042d

/* HCI event codes */
#define HCI_EVT_INQUIRY_COMPLETE        0x01
#define HCI_EVT_INQUIRY_RESULT          0x02
#define HCI_EVT_CONN_COMPLETE           0x03
#define HCI_EVT_CONN_REQUEST            0x04
#define HCI_EVT_DISCONNECT_COMPLETE     0x05
#define HCI_EVT_AUTH_COMPLETE           0x06
#define HCI_EVT_LINK_KEY_NOTIF          0x18
#define HCI_EVT_LINK_KEY_REQ            0x17
#define HCI_EVT_PIN_CODE_REQ            0x16
#define HCI_EVT_CMD_COMPLETE            0x0e
#define HCI_EVT_CMD_STATUS              0x0f
#define HCI_EVT_NUM_COMPLETED_PKTS      0x13
#define HCI_EVT_MAX_SLOTS_CHANGE        0x1b
#define HCI_EVT_INQUIRY_RESULT_WITH_RSSI 0x22
#define HCI_EVT_EXT_INQUIRY_RESULT      0x2f
#define HCI_EVT_IO_CAPABILITY_REQUEST   0x31
#define HCI_EVT_IO_CAPABILITY_RESPONSE  0x32
#define HCI_EVT_USER_CONFIRM_REQUEST    0x33
#define HCI_EVT_SIMPLE_PAIRING_COMPLETE 0x36

/* Max ACL credits we will use — leaves headroom for system DualSense traffic */
#define HCI_MAX_OUR_CREDITS             5

/* ACL buffer slots: 0-3 = event reads, 4-15 = ACL reads, 16-20 = ACL writes */
#define HCI_SLOT_EVT_START              0
#define HCI_SLOT_EVT_COUNT              4
#define HCI_SLOT_ACL_RD_START           4
#define HCI_SLOT_ACL_RD_COUNT           12
#define HCI_SLOT_ACL_WR_START           16
#define HCI_SLOT_ACL_WR_COUNT           8
#define HCI_TOTAL_SLOTS                 24

#define HCI_EVT_BUF_SIZE                260
#define HCI_ACL_BUF_SIZE                1100

/* BD_ADDR type */
typedef struct { uint8_t b[6]; } __attribute__((packed)) bdaddr_t;

static inline int bdaddr_eq(const bdaddr_t *a, const bdaddr_t *b) {
    return __builtin_memcmp(a->b, b->b, 6) == 0;
}
static inline int bdaddr_zero(const bdaddr_t *a) {
    for (int i = 0; i < 6; i++) if (a->b[i]) return 0;
    return 1;
}

/* HCI event callback: evt_code, param_buf, param_len */
typedef void (*hci_evt_cb_t)(uint8_t evt, const uint8_t *p, int len, void *ctx);

/* ACL data callback: handle (12-bit), pb_flag, data, data_len */
typedef void (*hci_acl_cb_t)(uint16_t handle, uint8_t pb, const uint8_t *data, int len, void *ctx);

int  hci_open(void);
void hci_close(void);

/* Send raw HCI command. Returns 1 on success. */
int  hci_cmd(uint16_t opcode, const void *params, int param_len);

/* Send ACL data to a connection handle. pb=2 for new L2CAP PDU, 1 for continuation */
int  hci_acl_write(uint16_t handle, uint8_t pb, const void *data, int data_len);

/* Register callbacks (only one of each). ctx passed back to callbacks. */
void hci_set_evt_cb(hci_evt_cb_t cb, void *ctx);
void hci_set_acl_cb(hci_acl_cb_t cb, void *ctx);

/* Secondary event callback — called for ALL events alongside the primary.
 * Used by bt_mgr for inquiry results and SSP pairing events. */
void hci_set_scan_cb(hci_evt_cb_t cb, void *ctx);

/* Must be called in a loop from a dedicated thread */
void hci_pump(void);

/* Issue a command and wait for CMD_COMPLETE event. Returns status byte. */
int  hci_sync(uint16_t opcode, const void *params, int param_len);

/* Credit management — called when we get NUM_COMPLETED_PKTS for our handles */
void hci_credit_return(int n);
int  hci_credit_wait(void);   /* blocks until credit available */

/* Utility: read own BD_ADDR */
int  hci_read_bdaddr(bdaddr_t *out);
