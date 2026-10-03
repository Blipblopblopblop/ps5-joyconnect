#pragma once
/* Forward declarations for PS5 system APIs — no SDK headers needed */
#include <stdint.h>
#include <stddef.h>
#include <time.h>

/* ScePthread (PS5 pthreads wrapper, same ABI as POSIX pthreads) */
#include <pthread.h>
typedef pthread_t        ScePthread;
typedef pthread_mutex_t  ScePthreadMutex;
typedef pthread_cond_t   ScePthreadCond;
typedef pthread_attr_t   ScePthreadAttr;

static inline int scePthreadCreate(ScePthread *t, ScePthreadAttr *a, void *(*fn)(void*), void *arg, const char *name) {
    (void)name; return pthread_create(t, a, fn, arg);
}
static inline int scePthreadDetach(ScePthread t)                    { return pthread_detach(t); }
static inline int scePthreadMutexInit(ScePthreadMutex *m, void *a, void *n) {
    (void)n; return pthread_mutex_init(m, (const pthread_mutexattr_t *)a);
}
static inline int scePthreadMutexLock(ScePthreadMutex *m)           { return pthread_mutex_lock(m); }
static inline int scePthreadMutexUnlock(ScePthreadMutex *m)         { return pthread_mutex_unlock(m); }
static inline int scePthreadCondInit(ScePthreadCond *c, void *a)    { return pthread_cond_init(c, (const pthread_condattr_t *)a); }
static inline int scePthreadCondSignal(ScePthreadCond *c)           { return pthread_cond_signal(c); }
static inline int scePthreadCondTimedwait(ScePthreadCond *c, ScePthreadMutex *m, const struct timespec *ts) {
    return pthread_cond_timedwait(c, m, ts);
}

/* SceUserService */
typedef int32_t SceUserServiceUserId;
extern int sceUserServiceInitialize(void *params);
extern int sceUserServiceGetInitialUser(SceUserServiceUserId *userId);

/* ScePad virtual device */
extern int scePadInit(void);
extern int scePadVirtualDeviceAddDevice(SceUserServiceUserId userId, int type, int index, void *reserved, int *outHandle);
extern int scePadVirtualDeviceDeleteDevice(int handle);
extern int scePadVirtualDeviceInsertData(int handle, const void *data, int count);
