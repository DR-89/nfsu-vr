#ifndef NFSU_ANDROID_INPUT_TRACE_H
#define NFSU_ANDROID_INPUT_TRACE_H

#include <stdlib.h>
#include <string.h>
#include <time.h>

static inline int android_input_trace_enabled(void)
{
    const char *value = getenv("RECOMP_INPUT_TRACE");
    return value && strcmp(value, "1") == 0;
}

static inline unsigned long long android_input_trace_time(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (unsigned long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
#endif
