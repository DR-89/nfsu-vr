/* Guest wait transactions. No polling, host event race, or partial WaitAll
 * consumption: object state and waiter grants share this lock. */
#include "guest_dispatcher.h"
#include "guest_scheduler.h"
#include "platform/dispatcher_backend.h"
#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <stdint.h>

typedef struct {
    int semaphore, manual;
    LONG state, limit;
    volatile uint8_t *guest_header;
} DispatchObject;
static LONG object_state(DispatchObject *o)
{
    if (!o->guest_header) return o->state;
    /* Inline reinitialization can reuse an address with a different event
     * reset mode or semaphore limit; read the live dispatcher header. */
    unsigned type = o->guest_header[0];
    o->semaphore = type == 5;
    o->manual = type == 0;
    if (o->semaphore) o->limit = *(volatile LONG *)(o->guest_header + 16);
    return *(volatile LONG *)(o->guest_header + 4);
}
static void object_set_state(DispatchObject *o, LONG state)
{
    if (o->guest_header) *(volatile LONG *)(o->guest_header + 4) = state;
    else o->state = state;
}
typedef struct Waiter {
    struct Waiter *next;
    void *const *objects;
    unsigned count;
    int all, done, priority;
    DWORD tid, result;
    uint64_t order;
    pthread_cond_t wake;
} Waiter;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static Waiter *waiters;
static uint64_t order;

static int eligible(Waiter *w, int consume, DWORD *result)
{
    unsigned index = 0;
    if (w->all) {
        for (unsigned i = 0; i < w->count; i++)
            if (object_state(w->objects[i]) <= 0) return 0;
    } else {
        for (; index < w->count; index++)
            if (object_state(w->objects[index]) > 0) break;
        if (index == w->count) return 0;
    }
    if (consume) {
        for (unsigned i = w->all ? 0 : index; i < (w->all ? w->count : index + 1); i++) {
            DispatchObject *o = w->objects[i];
            if (o->semaphore) object_set_state(o, object_state(o) - 1);
            else if (!o->manual) object_set_state(o, 0);
        }
    }
    *result = WAIT_OBJECT_0 + (w->all ? 0 : index);
    return 1;
}

static void complete(Waiter *w, DWORD result)
{
    w->result = result;
    w->done = 1;
    /* Publish Ready before waking the native stack. Selection no longer
     * depends on which pthread wins a condition-variable race. */
    xgs_wait_complete(w->tid);
    pthread_cond_signal(&w->wake);
}

static void dispatch(void)
{
    for (;;) {
        Waiter *best = NULL;
        DWORD ignored, result;
        int best_priority = 0;
        for (Waiter *w = waiters; w; w = w->next) {
            if (w->done || !eligible(w, 0, &ignored)) continue;
            int priority = xgs_thread_priority(w->tid, w->priority);
            if (!best || priority > best_priority ||
                (priority == best_priority && w->order < best->order)) {
                best = w; best_priority = priority;
            }
        }
        if (!best) return;
        eligible(best, 1, &result);
        complete(best, result);
    }
}

static void *create(int semaphore, int manual, LONG initial, LONG limit)
{
    if (initial < 0 || (semaphore && (limit <= 0 || initial > limit))) return NULL;
    DispatchObject *o = calloc(1, sizeof(*o));
    if (o) {
        o->semaphore = semaphore; o->manual = manual;
        o->state = semaphore ? initial : !!initial; o->limit = limit;
    }
    return o;
}

static void destroy(void *object)
{
    pthread_mutex_lock(&lock);
    for (Waiter *w = waiters; w; w = w->next)
        for (unsigned i = 0; !w->done && i < w->count; i++)
            if (w->objects[i] == object) complete(w, WAIT_FAILED);
    pthread_mutex_unlock(&lock);
    free(object);
}

static BOOL dispatcher_signal(void *object, LONG count, PLONG previous, int pulse)
{
    DispatchObject *o = object;
    pthread_mutex_lock(&lock);
    LONG state = object_state(o);
    if (o->semaphore && (state < 0 || state > o->limit || count <= 0 || count > o->limit - state)) {
        pthread_mutex_unlock(&lock);
        return FALSE;
    }
    if (previous) *previous = state;
    object_set_state(o, o->semaphore ? state + count : 1);
    dispatch();
    if (pulse) object_set_state(o, 0);
    pthread_mutex_unlock(&lock);
    return TRUE;
}

static BOOL reset(void *object)
{
    pthread_mutex_lock(&lock);
    object_set_state(object, 0);
    pthread_mutex_unlock(&lock);
    return TRUE;
}

static DWORD wait_objects(unsigned count, void *const *objects, int all, DWORD ms)
{
    if (!count || count > MAXIMUM_WAIT_OBJECTS) return WAIT_FAILED;
    for (unsigned i = 0; i < count; i++) {
        if (!objects[i]) return WAIT_FAILED;
        for (unsigned j = 0; all && j < i; j++)
            if (objects[i] == objects[j]) return WAIT_FAILED;
    }
    Waiter w = {.objects = objects, .count = count, .all = all,
        .tid = GetCurrentThreadId(), .priority = GetThreadPriority(GetCurrentThread())};
    struct timespec until;
    if (ms != INFINITE) {
        clock_gettime(CLOCK_MONOTONIC, &until);
        until.tv_sec += ms / 1000;
        until.tv_nsec += (long)(ms % 1000) * 1000000;
        if (until.tv_nsec >= 1000000000) { until.tv_sec++; until.tv_nsec -= 1000000000; }
    }
    pthread_mutex_lock(&lock);
    if (eligible(&w, 1, &w.result)) {
        pthread_mutex_unlock(&lock);
        return w.result;
    }
    if (!ms) { pthread_mutex_unlock(&lock); return WAIT_TIMEOUT; }
    pthread_condattr_t attr;
    if (pthread_condattr_init(&attr) || pthread_condattr_setclock(&attr, CLOCK_MONOTONIC) ||
        pthread_cond_init(&w.wake, &attr)) abort();
    pthread_condattr_destroy(&attr);
    w.order = ++order;
    w.next = waiters;
    waiters = &w;
    xgs_wait_begin((uintptr_t)objects[0]);
    while (!w.done) {
        int rc = ms == INFINITE ? pthread_cond_wait(&w.wake, &lock)
            : pthread_cond_timedwait(&w.wake, &lock, &until);
        if (rc == ETIMEDOUT && !w.done) complete(&w, WAIT_TIMEOUT);
        else if (rc && rc != ETIMEDOUT) abort();
    }
    Waiter **link = &waiters;
    while (*link != &w) link = &(*link)->next;
    *link = w.next;
    pthread_mutex_unlock(&lock);
    pthread_cond_destroy(&w.wake);
    xgs_wait_end();
    return w.result;
}

static BOOL bind_header(void *object, volatile void *header)
{
    if (!header) return FALSE;
    pthread_mutex_lock(&lock);
    DispatchObject *o = object;
    /* Bind only before publishing a new shadow handle to guest callers. */
    o->guest_header = header;
    pthread_mutex_unlock(&lock);
    return TRUE;
}

static const HostDispatcherBackend backend = {
    create, destroy, dispatcher_signal, reset, wait_objects, bind_header
};
void xgd_init(void) { host_set_dispatcher_backend(&backend); }
