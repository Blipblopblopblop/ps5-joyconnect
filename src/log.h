#pragma once
#include <stdarg.h>

void log_open(void);
void log_line(const char *fmt, ...);
void log_close(void);
void notify(const char *msg);
