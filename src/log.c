#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* PS5 notification — raw JSON API (see PayloadSDK notify sample) */
extern int sceNotificationSend(int userId, int isLogged, const char *payload);
#define SCE_NOTIFICATION_SYSTEM_USER 0xFE

static FILE *g_logfile;

void log_open(void) {
    system("mkdir -p /data/joyconnect");
    g_logfile = fopen("/data/joyconnect/joyconnect.log", "w");
}

void log_line(const char *fmt, ...) {
    if (!g_logfile) return;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    fprintf(g_logfile, "[%ld.%03ld] ", ts.tv_sec, ts.tv_nsec / 1000000);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_logfile, fmt, ap);
    va_end(ap);
    fputc('\n', g_logfile);
    fflush(g_logfile);
}

void log_close(void) {
    if (g_logfile) {
        fclose(g_logfile);
        g_logfile = NULL;
    }
}

void notify(const char *msg) {
    char payload[512];
    snprintf(payload, sizeof(payload),
        "{\"rawData\":{\"viewTemplateType\":\"InteractiveToastTemplateB\","
        "\"channelType\":\"Downloads\",\"useCaseId\":\"IDC\","
        "\"isImmediate\":true,\"priority\":100,"
        "\"viewData\":{\"message\":{\"body\":\"JoyConnect: %s\"}}}}",
        msg);
    sceNotificationSend(SCE_NOTIFICATION_SYSTEM_USER, 1, payload);
    log_line("notify: %s", msg);
}
