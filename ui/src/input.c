#include "input.h"
#include <stdint.h>
#include <string.h>

/* ScePad read structure — 128 bytes, buttons at offset 0 */
typedef struct {
    uint32_t buttons;
    uint8_t  lx, ly, rx, ry;
    uint8_t  l2, r2;
    uint8_t  _pad[118];
} __attribute__((packed)) pad_read_t;

_Static_assert(sizeof(pad_read_t) == 128, "pad_read_t must be 128 bytes");

/* PS5 pad API */
extern int scePadInit(void);
extern int scePadOpen(int userId, int type, int index, void *param);
extern int scePadReadState(int handle, pad_read_t *data);

/* Userservice */
typedef uint32_t SceUserServiceUserId;
extern int sceUserServiceGetInitialUser(SceUserServiceUserId *uid);
extern int sceUserServiceInitialize(void *param);

static int      g_handle = -1;
static uint32_t g_prev   = 0;
static uint32_t g_cur    = 0;

int input_init(void) {
    scePadInit();
    sceUserServiceInitialize(NULL);

    SceUserServiceUserId uid = 0xfe;
    sceUserServiceGetInitialUser(&uid);

    g_handle = scePadOpen((int)uid, 0, 0, NULL);
    return g_handle >= 0;
}

void input_update(void) {
    if (g_handle < 0) return;
    pad_read_t data;
    memset(&data, 0, sizeof(data));
    if (scePadReadState(g_handle, &data) >= 0) {
        g_prev = g_cur;
        g_cur  = data.buttons;
    }
}

int input_just(uint32_t btn) {
    return ((g_cur & btn) && !(g_prev & btn)) ? 1 : 0;
}

int input_held(uint32_t btn) {
    return (g_cur & btn) ? 1 : 0;
}
