#ifndef XBOX_GUEST_EXECUTOR_H
#define XBOX_GUEST_EXECUTOR_H
#include <stdint.h>
/* Explicit experimental mode, prepared before device threads are started. */
int xbox_guest_executor_prepare(void);
int xbox_guest_executor_run(void (*entry)(void), uint32_t stack, uint32_t fs_base);
/* Device-side guest callback routing. Zero means native mode or already on
 * the executor. A routed call returns one and stores the callback result. */
int xbox_guest_executor_invoke(int (*callback)(void *), void *argument, int *result);
int xbox_guest_executor_native_call(void (*callback)(void *), void *argument);
#endif
