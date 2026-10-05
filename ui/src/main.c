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
#include <sys/stat.h>

#define LOCKFILE "/data/joyconnect/ui.lock"

static int acquire_lock(void) {
    int fd = open(LOCKFILE, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) return 0;
    close(fd);
    return 1;
}
static void release_lock(void) { unlink(LOCKFILE); }

int main(void) {
    /* system() spawns a shell which is blocked in proper PS5 app sandbox */
    mkdir("/data/joyconnect", 0777);

    notify("JC: init...");

    if (!acquire_lock()) {
        notify("JC: already running");
        return 0;
    }

    log_open();
    log_line("JoyConnectUI starting");
    notify("JC: gfx...");

    if (!gfx_init()) {
        notify("JC: gfx FAIL");
        log_line("gfx_init failed");
        release_lock();
        return 1;
    }

    notify("JC: input...");
    if (!input_init()) {
        log_line("input_init failed (will continue without pad navigation)");
    }

    notify("JC: bt...");
    if (!bt_mgr_init()) {
        log_line("bt_mgr_init failed");
        notify("JC: bt FAIL");
        release_lock();
        return 1;
    }

    notify("JC: ready!");
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
