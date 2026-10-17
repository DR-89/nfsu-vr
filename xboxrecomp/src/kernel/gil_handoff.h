/* POSIX GIL waiters. All queue operations use the GIL critical section.
 * A waiter lives on its owner's stack until its ticket is served. */
#ifndef XBOX_GIL_HANDOFF_H
#define XBOX_GIL_HANDOFF_H

#include <pthread.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

typedef struct GilWaiter {
    struct GilWaiter *next;
    pthread_cond_t native;
    CONDITION_VARIABLE wake;
    uint64_t granted_ns; /* Written by the preceding owner under s_gil_cs. */
} GilWaiter;

typedef struct { GilWaiter *head, *tail; } GilWaitQueue;

static void gil_waiter_enqueue(GilWaitQueue *q, GilWaiter *w)
{
    if (pthread_cond_init(&w->native, NULL) != 0) abort();
    /* POSIX SleepConditionVariableCS/WakeConditionVariable borrow Ptr.
     * Supply stack storage instead of their lazy heap allocation. */
    w->wake.Ptr = &w->native;
    w->next = NULL;
    w->granted_ns = 0;
    if (q->tail) q->tail->next = w;
    else q->head = w;
    q->tail = w;
}

static void gil_waiter_remove_head(GilWaitQueue *q, GilWaiter *w)
{
    if (q->head != w) abort();
    q->head = w->next;
    if (!q->head) q->tail = NULL;
    /* The owner is awake with the queue lock held. No other thread waits
     * on this condition, and the preceding signal has already returned. */
    if (pthread_cond_destroy(&w->native) != 0) abort();
}

static void gil_waiter_wake_head(GilWaitQueue *q)
{
    if (q->head) WakeConditionVariable(&q->head->wake);
}

#endif
