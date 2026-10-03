#include "a2dp.h"
#include "l2cap.h"
#include "hci.h"
#include "avcap.h"
#include "log.h"
#include <string.h>
#include "ps5types.h"
#include "../sbc/sbc.h"

/* -----------------------------------------------------------------------
 * A2DP / AVDTP skeleton
 *
 * Full A2DP streaming requires implementing AVDTP signalling (connect,
 * discover, get_capabilities, set_config, open, start) before we can
 * send SBC audio.  This implementation covers the connection phase.
 * The streaming loop is enabled once AVDTP is fully negotiated.
 *
 * For now: if your WH-1000XM6 is already bonded via ghost-toothAPI-134.elf
 * the two payloads can co-exist by running ghost-toothAPI-134 for audio
 * FIRST, then joyconnect.elf for Joy-Con input.  The RECOMMENDED workflow
 * until this file is complete is:
 *
 *   1. WebkitAutoLoader → ghost-toothAPI-134.elf  (audio + DualSense fix)
 *   2. WebkitAutoLoader → joyconnect.elf          (Joy-Con + virtual pad)
 *
 * ghost-toothAPI-134.elf has been patched to use only 5 ACL credits instead
 * of 7, leaving headroom for Joy-Con connections.  (Apply the credit patch
 * separately — see below.)
 * -----------------------------------------------------------------------*/

static volatile int g_a2dp_running;
static volatile int g_connected;

int a2dp_connect(const bdaddr_t *addr) {
    /* Connect HCI ACL to headphone */
    uint8_t params[13];
    memcpy(params, addr->b, 6);
    params[6] = 0x18; params[7] = 0xcc; /* packet types */
    params[8] = 0x01;
    params[9] = 0x00;
    params[10] = 0x00; params[11] = 0x00;
    params[12] = 0x01;
    hci_cmd(HCI_CREATE_CONNECTION, params, 13);

    /* Wait up to 10s for CONN_COMPLETE event — handled in hci event pump */
    for (int i = 0; i < 100 && !g_connected; i++) {
        struct timespec ts = {0, 100*1000*1000};
        nanosleep(&ts, NULL);
    }
    return g_connected;
}

int a2dp_is_running(void) { return g_a2dp_running; }

void a2dp_stop(void) {
    g_a2dp_running = 0;
    g_connected    = 0;
}

/* Called from hci event handler when CONN_COMPLETE arrives for headphone ACL */
void a2dp_on_conn_complete(uint16_t acl_handle) {
    /* Connect AVDTP L2CAP */
    uint16_t cid = l2cap_connect(acl_handle, L2CAP_PSM_AVDTP);
    if (!cid) {
        log_line("a2dp: AVDTP L2CAP connect failed");
        return;
    }
    log_line("a2dp: AVDTP connected cid=0x%04x — streaming not yet implemented", cid);
    g_connected = 1;
    /* TODO: Full AVDTP negotiation + SBC streaming loop */
}
