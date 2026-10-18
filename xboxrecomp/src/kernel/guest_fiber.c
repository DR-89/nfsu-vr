/* Stackful continuations. Lifecycle stays explicit: a suspended C stack cannot
 * be discarded because it may contain live guest jump buffers and resources. */
#include "guest_fiber.h"
#include "recomp_context.h"
#include "platform/virtual_thread.h"
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <stddef.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>

#if defined(__aarch64__)
typedef struct { uint64_t x[12], sp, d[8]; } NativeContext;
_Static_assert(offsetof(NativeContext, sp) == 96, "AArch64 SP offset");
_Static_assert(offsetof(NativeContext, d) == 104, "AArch64 SIMD offset");
#elif defined(__x86_64__)
typedef struct { uint64_t rbx, rbp, r12, r13, r14, r15, sp, pc; } NativeContext;
_Static_assert(offsetof(NativeContext, sp) == 48, "x86-64 SP offset");
_Static_assert(offsetof(NativeContext, pc) == 56, "x86-64 PC offset");
#else
#error Unsupported guest fiber ABI
#endif
extern void xgf_native_swap(NativeContext *previous, const NativeContext *next);
extern void xgf_native_bootstrap(void);

typedef struct Submission {
    struct Submission *next;
    LPTHREAD_START_ROUTINE callback;
    void *argument;
    int priority, done, success;
    DWORD result;
    pthread_cond_t completed;
} Submission;

struct XgfExecutor {
    NativeContext native;
    RecompContext *caller;
    uintptr_t owner;
    unsigned fibers;
    uint64_t sequence;
    uint64_t ready_generation;
    void (*native_call)(void *);
    void *native_argument;
    int on_native_callback;
    XgfFiber *active, *list;
    pthread_mutex_t wake_lock;
    pthread_cond_t wake;
    Submission *submissions, **submission_tail;
    unsigned submitters;
    int accepting;
};
struct XgfFiber {
    NativeContext native;
    RecompContext context;
    XgfExecutor *executor;
    XgfEntry entry;
    void *argument, *mapping;
    size_t mapping_size;
    XgfState state;
    int notified, priority;
    unsigned suspended;
    uint64_t ready_order;
    uint64_t slice_start;
    uint64_t preempt_generation;
    int preempt_valid, preempt_higher, preempt_equal;
    HANDLE thread_handle;
    DWORD exit_code;
    LPTHREAD_START_ROUTINE thread_entry;
    void *thread_argument;
    Submission *submission;
    int detached;
    XgfFiber *next;
};
static RECOMP_TLS XgfExecutor *running;
static RECOMP_TLS unsigned owner_anchor;
static uintptr_t owner_id(void) { return (uintptr_t)&owner_anchor; }
static uint64_t monotonic_ms(void);
static int on_owner(XgfExecutor *e) { return e && e->owner == owner_id(); }
static uint64_t next_order(XgfExecutor *e)
{ return __atomic_add_fetch(&e->sequence, 1, __ATOMIC_RELAXED); }
static void mark_ready(XgfFiber *f)
{
    uint64_t empty = 0;
    if (!__atomic_load_n(&f->ready_order, __ATOMIC_ACQUIRE))
        __atomic_compare_exchange_n(&f->ready_order, &empty, next_order(f->executor),
                                    0, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
}

XgfExecutor *xgf_executor_create(void)
{
    if (running) return NULL;
    XgfExecutor *e = calloc(1, sizeof(*e));
    if (e) {
        e->owner = owner_id();
        e->accepting = 1;
        e->submission_tail = &e->submissions;
        pthread_condattr_t attr;
        if (pthread_mutex_init(&e->wake_lock, NULL) || pthread_condattr_init(&attr) ||
            pthread_condattr_setclock(&attr, CLOCK_MONOTONIC) ||
            pthread_cond_init(&e->wake, &attr)) abort();
        pthread_condattr_destroy(&attr);
    }
    return e;
}
int xgf_executor_destroy(XgfExecutor *e)
{
    if (!on_owner(e) || running || e->active || e->fibers) return 0;
    pthread_mutex_lock(&e->wake_lock);
    int submitted = e->submissions != NULL || e->submitters;
    pthread_mutex_unlock(&e->wake_lock);
    if (submitted) return 0;
    pthread_cond_destroy(&e->wake);
    pthread_mutex_destroy(&e->wake_lock);
    free(e);
    return 1;
}

int xgf_executor_close(XgfExecutor *e)
{
    if (!on_owner(e) || running) return 0;
    pthread_mutex_lock(&e->wake_lock);
    e->accepting = 0;
    Submission *s = e->submissions;
    e->submissions = NULL; e->submission_tail = &e->submissions;
    while (s) {
        Submission *next = s->next;
        s->done = 1;
        pthread_cond_signal(&s->completed);
        s = next;
    }
    pthread_mutex_unlock(&e->wake_lock);
    return 1;
}

XgfFiber *xgf_create(XgfExecutor *e, XgfEntry entry, void *argument,
                     size_t stack_size, uint32_t fs_base)
{
    /* Child creation is local to the same executor; it does not switch the
     * active stack or publish work to another physical thread. */
    if (!on_owner(e) || (running && running != e) || !entry) return NULL;
    long page_result = sysconf(_SC_PAGESIZE);
    if (page_result <= 0) return NULL;
    size_t page = (size_t)page_result;
    if (!stack_size) stack_size = 16u * 1024u * 1024u;
    if (stack_size < 65536) stack_size = 65536;
    if (stack_size > SIZE_MAX - 3 * page) return NULL;
    stack_size = (stack_size + page - 1) / page * page;
    size_t mapping_size = stack_size + 2 * page;
    void *mapping = mmap(NULL, mapping_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED) return NULL;
    if (mprotect((char *)mapping + page, stack_size, PROT_READ | PROT_WRITE)) {
        munmap(mapping, mapping_size);
        return NULL;
    }
    XgfFiber *f = calloc(1, sizeof(*f));
    if (!f || !recomp_context_init(&f->context, fs_base)) {
        free(f); munmap(mapping, mapping_size);
        return NULL;
    }
    f->executor = e; f->entry = entry; f->argument = argument;
    f->mapping = mapping; f->mapping_size = mapping_size; f->state = XGF_NEW;
    f->ready_order = next_order(e);
    __atomic_add_fetch(&e->ready_generation, 1, __ATOMIC_RELEASE);
    uintptr_t top = ((uintptr_t)mapping + page + stack_size) & ~(uintptr_t)15;
#if defined(__aarch64__)
    f->native.x[0] = (uintptr_t)f; /* x19: bootstrap argument */
    f->native.x[11] = (uintptr_t)xgf_native_bootstrap; /* x30 */
    f->native.sp = top;
#else
    f->native.r12 = (uintptr_t)f;
    f->native.sp = top - 8;
    *(uintptr_t *)(top - 8) = 0; /* A bootstrap must never return. */
    f->native.pc = (uintptr_t)xgf_native_bootstrap;
#endif
    f->next = e->list; e->list = f;
    e->fibers++;
    return f;
}

XgfState xgf_resume(XgfExecutor *e, XgfFiber *f)
{
    if (!on_owner(e) || running || !f || f->executor != e || e->active ||
        __atomic_load_n(&f->suspended, __ATOMIC_ACQUIRE) ||
        (f->state != XGF_NEW && f->state != XGF_SUSPENDED &&
         !(f->state == XGF_WAITING && __atomic_load_n(&f->notified, __ATOMIC_ACQUIRE)))) return XGF_ERROR;
    e->caller = recomp_context_current();
    if (!recomp_context_switch(&f->context)) return XGF_ERROR;
    if (f->thread_handle && !host_virtual_thread_bind(f->thread_handle)) {
        if (!recomp_context_switch(e->caller)) abort();
        return XGF_ERROR;
    }
    __atomic_store_n(&f->notified, 0, __ATOMIC_RELEASE);
    f->state = XGF_RUNNING;
    f->slice_start = monotonic_ms();
    f->preempt_valid = 0;
    __atomic_store_n(&f->ready_order, 0, __ATOMIC_RELEASE);
    e->active = f;
    running = e;
    xgf_native_swap(&e->native, &f->native);
    while (e->native_call) {
        void (*callback)(void *) = e->native_call;
        void *argument = e->native_argument;
        e->native_call = NULL;
        e->on_native_callback = 1;
        callback(argument);
        e->on_native_callback = 0;
        xgf_native_swap(&e->native, &f->native);
    }
    if (f->thread_handle) {
        if (!host_virtual_thread_bind(NULL)) abort();
        if (f->state == XGF_EXITED) {
            if (!host_virtual_thread_complete(f->thread_handle, f->exit_code)) abort();
            f->thread_handle = NULL;
        }
    }
    if (f->state == XGF_EXITED && f->submission) {
        Submission *s = f->submission;
        f->submission = NULL;
        pthread_mutex_lock(&e->wake_lock);
        s->result = f->exit_code; s->success = s->done = 1;
        pthread_cond_signal(&s->completed);
        pthread_mutex_unlock(&e->wake_lock);
    }
    return xgf_state(f);
}

static int return_to_executor(XgfState state)
{
    XgfExecutor *e = running;
    if (!on_owner(e) || !e->active || e->on_native_callback) return 0;
    XgfFiber *f = e->active;
    if (!recomp_context_switch(e->caller)) return 0;
    f->state = state;
    if (state == XGF_SUSPENDED) mark_ready(f);
    e->active = NULL;
    running = NULL;
    xgf_native_swap(&f->native, &e->native);
    return 1;
}
int xgf_yield(void) { return return_to_executor(XGF_SUSPENDED); }
int xgf_native_call(void (*callback)(void *), void *argument)
{
    XgfExecutor *e = running;
    if (!e || e->on_native_callback || !callback) return 0;
    e->native_call = callback;
    e->native_argument = argument;
    /* Keep guest identity, context and tracked ownership. The root services
     * this call immediately without entering its scheduling/cleanup path. */
    xgf_native_swap(&e->active->native, &e->native);
    return 1;
}
int xgf_exit(void)
{
    if (!return_to_executor(XGF_EXITED)) return 0;
    abort();
}

__attribute__((noreturn, visibility("hidden")))
void xgf_fiber_entry(XgfFiber *f)
{
    f->entry(f->argument);
    /* Returning with native ownership still held is a programming error;
     * silently discarding this stack would corrupt lock ownership. */
    if (!xgf_exit()) abort();
    abort(); /* Exited fibers cannot be resumed. */
}
XgfState xgf_state(const XgfFiber *f)
{
    if (!f || !on_owner(f->executor)) return XGF_ERROR;
    return f->state == XGF_WAITING && __atomic_load_n(&f->notified, __ATOMIC_ACQUIRE)
        ? XGF_SUSPENDED : f->state;
}
int xgf_destroy(XgfFiber *f)
{
    if (!f || !on_owner(f->executor) || (running && running != f->executor) ||
        (f->state != XGF_NEW && f->state != XGF_EXITED)) return 0;
    if (munmap(f->mapping, f->mapping_size)) return 0;
    if (f->thread_handle && !host_virtual_thread_complete(f->thread_handle, 995)) abort();
    XgfFiber **link = &f->executor->list;
    while (*link != f) link = &(*link)->next;
    *link = f->next;
    f->executor->fibers--;
    __atomic_add_fetch(&f->executor->ready_generation, 1, __ATOMIC_RELEASE);
    free(f);
    return 1;
}

XgfFiber *xgf_current(void) { return running ? running->active : NULL; }
XgfExecutor *xgf_current_executor(void) { return running; }
int xgf_detach(XgfFiber *f)
{
    if (!f || !on_owner(f->executor)) return 0;
    f->detached = 1;
    return 1;
}
int xgf_priority(const XgfFiber *f)
{ return f ? __atomic_load_n(&f->priority, __ATOMIC_RELAXED) : 0; }
int xgf_set_priority(XgfFiber *f, int priority)
{
    if (!f || !on_owner(f->executor)) return 0;
    __atomic_store_n(&f->priority, priority, __ATOMIC_RELAXED);
    __atomic_add_fetch(&f->executor->ready_generation, 1, __ATOMIC_RELEASE);
    return 1;
}
int xgf_prepare_wait(XgfFiber *f)
{
    if (!f || f != xgf_current() || running->on_native_callback ||
        !recomp_context_can_switch(running->caller)) return 0;
    __atomic_store_n(&f->notified, 0, __ATOMIC_RELEASE);
    return 1;
}
int xgf_park(void)
{
    XgfFiber *f = xgf_current();
    if (!f) return 0;
    if (__atomic_exchange_n(&f->notified, 0, __ATOMIC_ACQ_REL)) return 1;
    return return_to_executor(XGF_WAITING);
}
void xgf_notify(XgfFiber *f)
{
    XgfExecutor *e = f->executor;
    pthread_mutex_lock(&e->wake_lock);
    mark_ready(f);
    __atomic_store_n(&f->notified, 1, __ATOMIC_RELEASE);
    __atomic_add_fetch(&e->ready_generation, 1, __ATOMIC_RELEASE);
    pthread_cond_signal(&e->wake);
    pthread_mutex_unlock(&e->wake_lock);
}
static int any_ready(XgfExecutor *e)
{
    if (e->submissions) return 1; /* Caller holds wake_lock. */
    for (XgfFiber *f = e->list; f; f = f->next) {
        if (__atomic_load_n(&f->suspended, __ATOMIC_ACQUIRE)) continue;
        XgfState state = xgf_state(f);
        if (state == XGF_NEW || state == XGF_SUSPENDED) return 1;
    }
    return 0;
}
int xgf_executor_wait(XgfExecutor *e, uint32_t ms)
{
    if (!on_owner(e) || running || e->active) return 0;
    struct timespec until;
    if (ms != UINT32_MAX) {
        clock_gettime(CLOCK_MONOTONIC, &until);
        until.tv_sec += ms / 1000;
        until.tv_nsec += (long)(ms % 1000) * 1000000;
        if (until.tv_nsec >= 1000000000) { until.tv_sec++; until.tv_nsec -= 1000000000; }
    }
    pthread_mutex_lock(&e->wake_lock);
    while (!any_ready(e)) {
        if (!ms) break;
        int rc = ms == UINT32_MAX ? pthread_cond_wait(&e->wake, &e->wake_lock)
            : pthread_cond_timedwait(&e->wake, &e->wake_lock, &until);
        if (rc == ETIMEDOUT) break;
        if (rc) abort();
    }
    int ready = any_ready(e);
    pthread_mutex_unlock(&e->wake_lock);
    return ready;
}

static uint64_t monotonic_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000 + (unsigned long)now.tv_nsec / 1000000;
}

int xgf_invoke(XgfExecutor *e, LPTHREAD_START_ROUTINE callback,
               void *argument, int priority, DWORD *result)
{
    if (!e || on_owner(e) || running || !callback || !result) return 0;
    Submission s = {.callback = callback, .argument = argument, .priority = priority};
    if (pthread_cond_init(&s.completed, NULL)) return 0;
    pthread_mutex_lock(&e->wake_lock);
    if (!e->accepting) {
        pthread_mutex_unlock(&e->wake_lock);
        pthread_cond_destroy(&s.completed);
        return 0;
    }
    e->submitters++;
    *e->submission_tail = &s;
    e->submission_tail = &s.next;
    __atomic_add_fetch(&e->ready_generation, 1, __ATOMIC_RELEASE);
    pthread_cond_signal(&e->wake);
    while (!s.done) pthread_cond_wait(&s.completed, &e->wake_lock);
    e->submitters--;
    pthread_mutex_unlock(&e->wake_lock);
    pthread_cond_destroy(&s.completed);
    if (s.success) *result = s.result;
    return s.success;
}

static void import_submissions(XgfExecutor *e)
{
    pthread_mutex_lock(&e->wake_lock);
    Submission *list = e->submissions;
    e->submissions = NULL; e->submission_tail = &e->submissions;
    pthread_mutex_unlock(&e->wake_lock);
    while (list) {
        Submission *s = list;
        list = s->next;
        XgfFiber *f;
        HANDLE h = xgf_create_thread(e, s->callback, s->argument, 0, 0, 0, NULL, &f);
        if (h) {
            f->submission = s;
            f->detached = 1;
            xgf_set_priority(f, s->priority);
            CloseHandle(h); /* The executor keeps its runtime reference. */
        } else {
            pthread_mutex_lock(&e->wake_lock);
            s->done = 1;
            pthread_cond_signal(&s->completed);
            pthread_mutex_unlock(&e->wake_lock);
        }
    }
}

int xgf_should_preempt(void)
{
    XgfFiber *self = xgf_current();
    if (!self || running->on_native_callback) return 0;
    if (__atomic_load_n(&self->suspended, __ATOMIC_ACQUIRE)) return 1;
    XgfExecutor *e = self->executor;
    /* Only the owner changes the list. Native notifications, priority changes
     * and suspend/resume publish a generation after updating their metadata.
     * Cache the competitor classification until that generation changes; a
     * quiet safe point needs neither a mutex/list walk nor a clock read when
     * there is no equal-priority competitor. Never clear a native notification.
     */
    uint64_t generation = __atomic_load_n(&e->ready_generation, __ATOMIC_ACQUIRE);
    if (!self->preempt_valid || self->preempt_generation != generation) {
        pthread_mutex_lock(&e->wake_lock);
        int higher = e->submissions != NULL;
        pthread_mutex_unlock(&e->wake_lock);
        int equal = 0, priority = xgf_priority(self);
        for (XgfFiber *f = e->list; f; f = f->next) {
            if (f == self || __atomic_load_n(&f->suspended, __ATOMIC_ACQUIRE)) continue;
            XgfState state = xgf_state(f);
            if (state != XGF_NEW && state != XGF_SUSPENDED) continue;
            int other_priority = xgf_priority(f);
            if (other_priority > priority) higher = 1;
            if (other_priority == priority) equal = 1;
        }
        self->preempt_higher = higher;
        self->preempt_equal = equal;
        self->preempt_generation = generation;
        self->preempt_valid = 1;
    }
    return self->preempt_higher ||
        (self->preempt_equal && monotonic_ms() > self->slice_start);
}

XgfRunResult xgf_executor_run(XgfExecutor *e, uint32_t budget_ms, XgfPoll poll)
{
    if (!on_owner(e) || running || e->active) return XGF_RUN_ERROR;
    uint64_t deadline = budget_ms == UINT32_MAX ? UINT64_MAX : monotonic_ms() + budget_ms;
    for (;;) {
        import_submissions(e);
        for (XgfFiber *f = e->list, *next; f; f = next) {
            next = f->next;
            if (f->state == XGF_EXITED && f->detached && !xgf_destroy(f)) abort();
        }
        /* Dispatcher callbacks may notify under their own lock. Never call
         * them while holding wake_lock: notify takes that mutex too. */
        uint32_t wait_ms = poll ? poll() : UINT32_MAX;
        XgfFiber *best = NULL;
        unsigned live = 0;
        for (XgfFiber *f = e->list; f; f = f->next) {
            XgfState state = xgf_state(f);
            if (state == XGF_EXITED) continue;
            live++;
            if (__atomic_load_n(&f->suspended, __ATOMIC_ACQUIRE)) continue;
            if (state != XGF_NEW && state != XGF_SUSPENDED) continue;
            mark_ready(f);
            if (!best || xgf_priority(f) > xgf_priority(best) ||
                (xgf_priority(f) == xgf_priority(best) &&
                 __atomic_load_n(&f->ready_order, __ATOMIC_ACQUIRE) <
                 __atomic_load_n(&best->ready_order, __ATOMIC_ACQUIRE)))
                best = f;
        }
        if (!live) return XGF_RUN_DONE;
        uint64_t now = monotonic_ms();
        if (now >= deadline) return XGF_RUN_PENDING;
        if (best) {
            if (xgf_resume(e, best) == XGF_ERROR) return XGF_RUN_ERROR;
            if (best->state == XGF_EXITED && best->detached && !xgf_destroy(best)) abort();
        } else {
            uint64_t remaining = deadline - now;
            if (remaining < wait_ms) wait_ms = (uint32_t)remaining;
            xgf_executor_wait(e, wait_ms);
        }
    }
}

static void thread_wake(XgfFiber *f)
{
    pthread_mutex_lock(&f->executor->wake_lock);
    pthread_cond_signal(&f->executor->wake);
    pthread_mutex_unlock(&f->executor->wake_lock);
}
static void thread_priority(void *thread, int priority)
{
    XgfFiber *f = thread;
    __atomic_store_n(&f->priority, priority, __ATOMIC_RELAXED);
    __atomic_add_fetch(&f->executor->ready_generation, 1, __ATOMIC_RELEASE);
    thread_wake(f);
}
static void thread_suspend(void *thread, unsigned count)
{
    XgfFiber *f = thread;
    __atomic_store_n(&f->suspended, count, __ATOMIC_RELEASE);
    __atomic_add_fetch(&f->executor->ready_generation, 1, __ATOMIC_RELEASE);
    thread_wake(f);
}
static void thread_exit(void *thread, DWORD code)
{
    XgfFiber *f = thread;
    if (f != xgf_current()) abort();
    f->exit_code = code;
    if (!xgf_exit()) abort();
}
static void thread_entry(void *unused)
{
    (void)unused;
    XgfFiber *f = xgf_current();
    f->exit_code = f->thread_entry(f->thread_argument);
}
HANDLE xgf_create_thread(XgfExecutor *e, LPTHREAD_START_ROUTINE entry,
                         void *argument, size_t stack_size, uint32_t fs_base,
                         DWORD flags, LPDWORD tid, XgfFiber **fiber)
{
    static const HostVirtualThreadOps ops = {thread_priority, thread_suspend, thread_exit};
    if (fiber) *fiber = NULL;
    if (!entry || (flags & ~CREATE_SUSPENDED)) return NULL;
    XgfFiber *f = xgf_create(e, thread_entry, NULL, stack_size, fs_base);
    if (!f) return NULL;
    f->thread_entry = entry;
    f->thread_argument = argument;
    f->suspended = (flags & CREATE_SUSPENDED) ? 1 : 0;
    f->thread_handle = host_virtual_thread_create(&ops, f, flags, tid);
    if (!f->thread_handle) { xgf_destroy(f); return NULL; }
    if (fiber) *fiber = f;
    return f->thread_handle;
}
