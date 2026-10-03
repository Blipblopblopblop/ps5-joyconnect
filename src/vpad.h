#pragma once
#include <stdint.h>
#include "joycon.h"

/* Initialise virtual pad subsystem (calls scePadVirtualDeviceAddDevice) */
int  vpad_init(void);

/* Called on Joy-Con state change. Merges L+R Joy-Con into a virtual DualSense
 * and calls scePadVirtualDeviceInsertData. */
void vpad_on_joycon(int idx, const joycon_state_t *state);

void vpad_cleanup(void);
