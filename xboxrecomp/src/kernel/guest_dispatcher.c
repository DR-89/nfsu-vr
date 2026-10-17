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
#include <stdio.h>

typedef struct DispatchObject {
    int semaphore, manual;
    LONG state, limit;
    volatile uint8_t *guest_header;
    int mutex, abandoned;
    unsigned recursion;
    DWORD owner_tid;
    void *owner_continuation;
    struct DispatchObject *mutex_next;
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
    const XgdWaitBackend *backend;
    void *continuation;
    uint64_t deadline;

} Waiter;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static Waiter *waiters;
static DispatchObject *mutexes;
static uint64_t order;
static const XgdWaitBackend *wait_backend;
int xgd_set_wait_backend(const XgdWaitBackend *backend)
{
    if (!backend || !backend->current || !backend->priority || !backend->prepare ||
        !backend->park || !backend->notify || !backend->yield) return 0;
    const XgdWaitBackend *expected = NULL;
    return __atomic_compare_exchange_n(&wait_backend, &expected, backend, 0,
        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) || expected == backend;
}
static uint64_t monotonic_ns(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) abort();
    return (uint64_t)t.tv_sec * 1000000000ull + t.tv_nsec;
}

static int mutex_owner(DispatchObject *o, void *continuation, DWORD tid)
{
    return o->recursion && o->owner_continuation == continuation && o->owner_tid == tid;
}
static int object_ready(DispatchObject *o, Waiter *w)
{
    return object_state(o) > 0 || (o->mutex &&
        mutex_owner(o, w->continuation, w->tid) && o->recursion < UINT32_MAX);
}
static int eligible(Waiter *w, int consume, DWORD *result)
{
    unsigned index = 0;
    if (w->all) {
        for (unsigned i = 0; i < w->count; i++)
            if (!object_ready(w->objects[i], w)) return 0;
    } else {
        for (; index < w->count; index++)
            if (object_ready(w->objects[index], w)) break;
        if (index == w->count) return 0;
    }
    DWORD abandoned = UINT32_MAX;
    for (unsigned i = w->all ? 0 : index; i < (w->all ? w->count : index + 1); i++) {
        DispatchObject *o = w->objects[i];
        if (o->mutex && o->abandoned && abandoned == UINT32_MAX) abandoned = i;
        if (consume) {
            if (o->mutex) {
                o->owner_tid = w->tid;
                o->owner_continuation = w->continuation;
                o->recursion++;
                o->abandoned = 0;
                object_set_state(o, 0);
            } else if (o->semaphore) object_set_state(o, object_state(o) - 1);
            else if (!o->manual) object_set_state(o, 0);
        }
    }
    *result = abandoned == UINT32_MAX ? WAIT_OBJECT_0 + (w->all ? 0 : index)
        : WAIT_ABANDONED_0 + (w->all ? 0 : abandoned);
    return 1;
}

static void complete(Waiter *w, DWORD result)
{
    w->result = result;
    w->done = 1;
    /* Publish Ready before waking the native stack. Selection no longer
     * depends on which pthread wins a condition-variable race. */
    if (w->continuation) w->backend->notify(w->continuation);
    else {
        xgs_wait_complete(w->tid);
        pthread_cond_signal(&w->wake);
    }
}

static void dispatch(void)
{
    if (__atomic_load_n(&wait_backend, __ATOMIC_ACQUIRE)) {
        uint64_t now = monotonic_ns();
        for (Waiter *w = waiters; w; w = w->next)
            if (w->continuation && !w->done && w->deadline && w->deadline <= now)
                complete(w, WAIT_TIMEOUT);
    }
    for (;;) {
        Waiter *best = NULL;
        DWORD ignored, result;
        int best_priority = 0;
        for (Waiter *w = waiters; w; w = w->next) {
            if (w->done || !eligible(w, 0, &ignored)) continue;
            int priority = w->continuation ? w->backend->priority(w->continuation)
                : xgs_thread_priority(w->tid, w->priority);
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

static void *create(int kind, int manual, LONG initial, LONG limit)
{
    int semaphore = kind == 1;
    if (kind < 0 || kind > 2) return NULL;
    if (initial < 0 || (semaphore && (limit <= 0 || initial > limit))) return NULL;
    DispatchObject *o = calloc(1, sizeof(*o));
    if (o) {
        o->semaphore = semaphore; o->manual = manual;
        o->state = semaphore ? initial : !!initial; o->limit = limit;
        if (kind == 2) {
            o->mutex = 1; o->state = !initial;
            if (initial) {
                const XgdWaitBackend *b = __atomic_load_n(&wait_backend, __ATOMIC_ACQUIRE);
                o->owner_continuation = b ? b->current() : NULL;
                o->owner_tid = GetCurrentThreadId();
                o->recursion = 1;
            }
            pthread_mutex_lock(&lock);
            o->mutex_next = mutexes; mutexes = o;
            pthread_mutex_unlock(&lock);
        }
    }
    return o;
}

static void destroy(void *object)
{
    pthread_mutex_lock(&lock);
    for (Waiter *w = waiters; w; w = w->next)
        for (unsigned i = 0; !w->done && i < w->count; i++)
            if (w->objects[i] == object) complete(w, WAIT_FAILED);
    DispatchObject *o = object;
    if (o->mutex) {
        DispatchObject **link = &mutexes;
        while (*link != o) link = &(*link)->mutex_next;
        *link = o->mutex_next;
    }
    pthread_mutex_unlock(&lock);
    free(object);
}

static BOOL dispatcher_signal(void *object, LONG count, PLONG previous, int pulse)
{
    DispatchObject *o = object;
    pthread_mutex_lock(&lock);
    LONG state = object_state(o);
    if (o->mutex) {
        const XgdWaitBackend *b = __atomic_load_n(&wait_backend, __ATOMIC_ACQUIRE);
        if (!mutex_owner(o, b ? b->current() : NULL, GetCurrentThreadId())) {
            pthread_mutex_unlock(&lock);
            SetLastError(ERROR_NOT_OWNER);
            return FALSE;
        }
        if (!--o->recursion) {
            o->owner_tid = 0; o->owner_continuation = NULL;
            object_set_state(o, 1);
            dispatch();
        }
        pthread_mutex_unlock(&lock);
        return TRUE;
    }
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
    w.backend = __atomic_load_n(&wait_backend, __ATOMIC_ACQUIRE);
    if (w.backend) w.continuation = w.backend->current();
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
    if (w.continuation) {
        if (!w.backend->prepare(w.continuation)) {
            pthread_mutex_unlock(&lock);
            static volatile LONG reports;
            if (InterlockedIncrement(&reports) <= 8)
                fprintf(stderr, "[EXECUTOR] unsupported wait: unsafe kernel boundary (virtual tid %u)\n", w.tid);
            SetLastError(ERROR_NOT_SUPPORTED);
            return WAIT_FAILED;
        }
        if (ms != INFINITE)
            w.deadline = (uint64_t)until.tv_sec * 1000000000ull + until.tv_nsec;
    } else {
        pthread_condattr_t attr;
        if (pthread_condattr_init(&attr) || pthread_condattr_setclock(&attr, CLOCK_MONOTONIC) ||
            pthread_cond_init(&w.wake, &attr)) abort();
        pthread_condattr_destroy(&attr);
    }
    w.order = ++order;
    w.next = waiters;
    waiters = &w;
    if (!w.continuation) xgs_wait_begin((uintptr_t)objects[0]);
    while (!w.done) {
        if (w.continuation) {
            pthread_mutex_unlock(&lock);
            if (!w.backend->park()) abort(); /* Preparation guaranteed a safe boundary. */
            pthread_mutex_lock(&lock);
        } else {
            int rc = ms == INFINITE ? pthread_cond_wait(&w.wake, &lock)
                : pthread_cond_timedwait(&w.wake, &lock, &until);
            if (rc == ETIMEDOUT && !w.done) complete(&w, WAIT_TIMEOUT);
            else if (rc && rc != ETIMEDOUT) abort();
        }
    }
    Waiter **link = &waiters;
    while (*link != &w) link = &(*link)->next;
    *link = w.next;
    pthread_mutex_unlock(&lock);
    if (!w.continuation) {
        pthread_cond_destroy(&w.wake);
        xgs_wait_end();
    }
    return w.result;
}

static BOOL bind_header(void *object, volatile void *header)
{
    if (!header) return FALSE;
    pthread_mutex_lock(&lock);
    DispatchObject *o = object;
    if (o->mutex) { pthread_mutex_unlock(&lock); return FALSE; }
    /* Bind only before publishing a new shadow handle to guest callers. */
    o->guest_header = header;
    pthread_mutex_unlock(&lock);
    return TRUE;
}

static int native_wait_allowed(void)
{
    const XgdWaitBackend *backend = __atomic_load_n(&wait_backend, __ATOMIC_ACQUIRE);
    return !backend || !backend->current();
}
static int dispatcher_sleep(DWORD ms)
{
    const XgdWaitBackend *backend = __atomic_load_n(&wait_backend, __ATOMIC_ACQUIRE);
    if (!backend || !backend->current()) return 0;
    if (!ms) { (void)backend->yield(); return 1; }
    DispatchObject delay = {0};
    void *object = &delay;
    if (wait_objects(1, &object, 0, ms) != WAIT_TIMEOUT) abort();
    return 1;
}
static void thread_exit(DWORD tid)
{
    pthread_mutex_lock(&lock);
    for (DispatchObject *o = mutexes; o; o = o->mutex_next) {
        if (!o->recursion || o->owner_tid != tid) continue;
        o->owner_tid = 0; o->owner_continuation = NULL;
        o->recursion = 0; o->abandoned = 1;
        object_set_state(o, 1);
    }
    dispatch();
    pthread_mutex_unlock(&lock);
}
static const HostDispatcherBackend backend = {
    create, destroy, dispatcher_signal, reset, wait_objects, bind_header,
    native_wait_allowed, dispatcher_sleep, thread_exit
};
void xgd_init(void) { host_set_dispatcher_backend(&backend); }

uint32_t xgd_poll_timeouts(void)
{
    pthread_mutex_lock(&lock);
    uint64_t now = monotonic_ns(), next = UINT64_MAX;
    for (Waiter *w = waiters; w; w = w->next) {
        if (!w->continuation || w->done || !w->deadline) continue;
        if (w->deadline <= now) complete(w, WAIT_TIMEOUT);
        else if (w->deadline < next) next = w->deadline;
    }
    pthread_mutex_unlock(&lock);
    if (next == UINT64_MAX) return UINT32_MAX;
    uint64_t ms = (next - now + 999999) / 1000000;
    return ms >= UINT32_MAX ? UINT32_MAX - 1 : (uint32_t)ms;
}
