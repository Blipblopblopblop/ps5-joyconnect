#pragma once
#include <stdint.h>
#include "hci.h"

/* Start A2DP streaming to a headphone at addr.
 * Runs in background thread, blocks until connected or timeout (10s).
 * Returns 1 if connected, 0 on failure. */
int  a2dp_connect(const bdaddr_t *addr);

/* Returns 1 if A2DP is actively streaming */
int  a2dp_is_running(void);

void a2dp_stop(void);
