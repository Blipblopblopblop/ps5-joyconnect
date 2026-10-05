/*
 * PS5 app entry point — C translation of the boilerplate app_crt.cpp.
 * Sets IEEE FP mode, initialises the platform runtime, runs constructors,
 * calls main, and exits.
 */
#include <stdint.h>
#include <stddef.h>

/* Provided by libps5platform.a */
extern void ps5_fp_ieee(void);

/* Provided by libSceLibcInternal (system libc) */
extern void _init_env(void *process_parameters);
extern int  atexit(void (*fn)(void));
extern void exit(int status) __attribute__((noreturn));

/* Our application entry point (aliased from main) */
extern int main(int argc, char **argv, char **envp);

/* Init/fini arrays — weak so they're optional */
typedef void (*InitFn)(void);
extern InitFn __preinit_array_start[] __attribute__((weak));
extern InitFn __preinit_array_end[]   __attribute__((weak));
extern InitFn __init_array_start[]    __attribute__((weak));
extern InitFn __init_array_end[]      __attribute__((weak));
extern InitFn __fini_array_start[]    __attribute__((weak));
extern InitFn __fini_array_end[]      __attribute__((weak));

static void run_array(InitFn *first, InitFn *last) {
    if (!first || !last) return;
    while (first != last) (*first++)();
}

static void run_array_rev(InitFn *first, InitFn *last) {
    if (!first || !last) return;
    while (last != first) (*--last)();
}

__attribute__((weak)) void catchReturnFromMain(int status) { (void)status; }

void _init(void) {
    run_array(__preinit_array_start, __preinit_array_end);
    run_array(__init_array_start,    __init_array_end);
}

void _fini(void) {
    run_array_rev(__fini_array_start, __fini_array_end);
}

__attribute__((visibility("default")))
__attribute__((noreturn))
void _start(void *process_parameters, void (*loader_teardown)(void)) {
    const int argc = *(const int *)process_parameters;
    char **argv = (char **)((uint8_t *)process_parameters + sizeof(uint64_t));

    ps5_fp_ieee();
    _init_env(process_parameters);
    if (loader_teardown) atexit(loader_teardown);
    atexit(_fini);
    _init();
    int status = main(argc, argv, NULL);
    catchReturnFromMain(status);
    exit(status);
}
