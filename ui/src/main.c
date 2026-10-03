#include "gfx.h"
#include "input.h"
#include "bt_mgr.h"
#include "ui.h"
#include "../../src/log.h"
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>

#define LOCKFILE "/data/joyconnect/ui.lock"

static int acquire_lock(void) {
    int fd = open(LOCKFILE, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) return 0;
    close(fd);
    return 1;
}
static void release_lock(void) { unlink(LOCKFILE); }

int main(void) {
    system("mkdir -p /data/joyconnect");

    if (!acquire_lock()) {
        notify("JoyConnectUI already running");
        return 0;
    }

    log_open();
    log_line("JoyConnectUI starting");

    if (!gfx_init()) {
        notify("JoyConnectUI: VideoOut init failed");
        log_line("gfx_init failed");
        release_lock();
        return 1;
    }

    if (!input_init()) {
        log_line("input_init failed (will continue without pad navigation)");
    }

    if (!bt_mgr_init()) {
        log_line("bt_mgr_init failed");
        notify("JoyConnectUI: BT init failed");
        release_lock();
        return 1;
    }

    notify("JoyConnectUI ready");
    ui_init();

    /* Initial draw */
    ui_draw();
    gfx_present();

    struct timespec ts = {0, 16 * 1000 * 1000};  /* ~60fps poll */

    while (!ui_wants_exit()) {
        input_update();
        if (ui_update()) {
            ui_draw();
            gfx_present();
        }
        nanosleep(&ts, NULL);
    }

    bt_mgr_shutdown();
    release_lock();
    log_line("JoyConnectUI exit");
    return 0;
}
