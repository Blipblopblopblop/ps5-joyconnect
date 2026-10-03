#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include "ps5types.h"

#include "log.h"
#include "hci.h"
#include "l2cap.h"
#include "joycon.h"
#include "vpad.h"
#include "avcap.h"
#include "a2dp.h"

/* -----------------------------------------------------------------------
 * Lock file — prevent double-start
 * -----------------------------------------------------------------------*/
#define LOCKFILE "/data/joyconnect/joyconnect.lock"

static int acquire_lock(void) {
    system("mkdir -p /data/joyconnect");
    int fd = open(LOCKFILE, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) return 0;
    close(fd);
    return 1;
}
static void release_lock(void) { unlink(LOCKFILE); }

/* -----------------------------------------------------------------------
 * HCI pump thread
 * -----------------------------------------------------------------------*/
static volatile int g_running = 1;

static void *hci_thread(void *arg) {
    (void)arg;
    while (g_running) {
        hci_pump();
        /* Yield to avoid spinning tight */
        struct timespec ts = {0, 100 * 1000}; /* 100µs */
        nanosleep(&ts, NULL);
    }
    return NULL;
}

/* -----------------------------------------------------------------------
 * Joy-Con state callback → virtual pad
 * -----------------------------------------------------------------------*/
static void on_joycon(int idx, const joycon_state_t *state, void *ctx) {
    (void)ctx;
    vpad_on_joycon(idx, state);
}

/* -----------------------------------------------------------------------
 * Saved headphone address
 * Stored in /data/joyconnect/headphone.addr (6 bytes raw BD_ADDR)
 * -----------------------------------------------------------------------*/
static int load_headphone_addr(bdaddr_t *out) {
    int fd = open("/data/joyconnect/headphone.addr", O_RDONLY);
    if (fd < 0) return 0;
    int n = read(fd, out->b, 6);
    close(fd);
    return n == 6 && !bdaddr_zero(out);
}


/* -----------------------------------------------------------------------
 * Entry point
 * -----------------------------------------------------------------------*/
int main(void) {
    if (!acquire_lock()) {
        /* Already running — send notification and exit */
        notify("already running");
        return 0;
    }

    log_open();
    log_line("JoyConnect starting");
    notify("JoyConnect starting...");

    sceUserServiceInitialize(NULL);

    /* Init HCI */
    if (!hci_open()) {
        notify("HCI open failed");
        log_line("HCI open failed");
        release_lock();
        return 1;
    }

    /* Reset controller */
    hci_sync(HCI_RESET, NULL, 0);

    /* Init L2CAP */
    l2cap_init();

    /* Init virtual pad */
    if (!vpad_init()) {
        notify("vpad init failed");
        log_line("vpad init failed");
        /* Non-fatal: Joy-Con input won't work in games but we continue */
    }

    /* Start HCI pump thread */
    ScePthread hci_tid;
    scePthreadCreate(&hci_tid, NULL, hci_thread, NULL, "hci_pump");
    scePthreadDetach(hci_tid);

    /* Start Joy-Con scan + connect */
    if (!joycon_start(on_joycon, NULL)) {
        notify("joycon_start failed");
    }

    notify("Joy-Cons scanning — pair now");
    log_line("Joy-Con scan active, waiting for controllers");

    /* A2DP: try to connect saved headphone address */
    bdaddr_t hp_addr;
    memset(&hp_addr, 0, sizeof(hp_addr));
    if (load_headphone_addr(&hp_addr)) {
        log_line("A2DP: trying saved headphone %02x:%02x:%02x:%02x:%02x:%02x",
                 hp_addr.b[5], hp_addr.b[4], hp_addr.b[3],
                 hp_addr.b[2], hp_addr.b[1], hp_addr.b[0]);
        if (a2dp_connect(&hp_addr)) {
            notify("Headphone connected");
        }
    } else {
        log_line("A2DP: no saved headphone — pair a headphone to save address");
        notify("No headphone saved. Run ghost-toothAPI-134.elf first to pair.");
    }

    /* Main loop: just keep the payload alive */
    while (g_running) {
        struct timespec ts = {1, 0};
        nanosleep(&ts, NULL);
    }

    /* Cleanup */
    joycon_stop();
    avcap_close();
    a2dp_stop();
    vpad_cleanup();
    hci_close();
    log_line("JoyConnect stopped");
    log_close();
    release_lock();
    return 0;
}
