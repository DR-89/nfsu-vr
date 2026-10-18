/* Scheduler policy lives here, never in the POSIX object implementation. */
#include "../../include/nfsu_log.h"
#include "guest_scheduler.h"
#include "guest_dispatcher.h"
#include "platform/thread_observer.h"
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#ifdef __linux__
#include <unistd.h>
#include <sys/syscall.h>
#endif

typedef struct GuestThread {
    struct GuestThread *next;
    DWORD tid;
    unsigned long host_tid;
    int priority, dispatch, executing;
    unsigned suspended, wait_depth;
    int wait_released_token;
    uintptr_t wait_object;
    XgsState state;
    uint64_t ready_order, selected_ns, started_ns, wait_ready_ns;
    pthread_cond_t gate;
} GuestThread;

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static pthread_key_t thread_key;
static GuestThread *threads, *owner, *dispatch_owner;
static uint64_t ready_order;
/* Bounded aggregate diagnostics, protected by the scheduler mutex. */
#define XGS_STATS_ROWS 64
#define XGS_STATS_WINDOW 5000000000ull
typedef struct {
    DWORD tid;
    unsigned long host_tid;
    int priority, main_priority, dispatch, phase, reason;
    uint64_t ns, selections;
} XgsRow;
typedef struct {
    uint64_t start, last, wait_ns, overflow_ns;
    uint64_t causes[6];
    XgsRow rows[XGS_STATS_ROWS];
    unsigned used;
} XgsStats;
static XgsStats stats;
static int diagnostics;
static DWORD main_tid;
static GuestThread *main_thread;
static int main_wait_reason = 6;
static const char *reasons[] = {"spin", "dispatch", "higher", "quantum", "suspend", "other", "acquire"};
/* phase: executing, selected-but-not-awake, DISPATCH host reservation,
 * no owner, main wakeup, main suspended. These are wall times, not CPU. */
static const char *phases[] = {"run", "wake", "dispatch_host", "idle", "main_wake", "suspended"};
static volatile LONG *request;
static void (*wait_park_account)(void);
static void (*wait_resume_account)(uint64_t, uint64_t);
void xgs_wait_accounting(void (*park)(void), void (*resume)(uint64_t, uint64_t))
{ wait_park_account = park; wait_resume_account = resume; }
/* Round-robin among equal priorities, not a main-thread exception. */
static const uint64_t quantum_ns = 1000000;

static uint64_t now_ns(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) abort();
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

static int main_pending(void)
{
    return main_thread && (main_thread->state == XGS_READY ||
        main_thread->state == XGS_SUSPENDED ||
        (owner == main_thread && !main_thread->executing));
}

static XgsRow *stats_row(GuestThread *blocker, int phase)
{
    DWORD tid = blocker ? blocker->tid : 0;
    int priority = blocker ? blocker->priority : 0;
    int dispatch = blocker ? blocker->dispatch : 0;
    for (unsigned i = 0; i < stats.used; i++) {
        XgsRow *r = &stats.rows[i];
        if (r->tid == tid && r->priority == priority && r->dispatch == dispatch &&
            r->phase == phase && r->reason == main_wait_reason &&
            r->main_priority == main_thread->priority) return r;
    }
    if (stats.used == XGS_STATS_ROWS) return NULL;
    XgsRow *r = &stats.rows[stats.used++];
    r->tid = tid; r->host_tid = blocker ? blocker->host_tid : 0;
    r->priority = priority; r->dispatch = dispatch;
    r->main_priority = main_thread->priority; r->phase = phase;
    r->reason = main_wait_reason;
    return r;
}

static void account(uint64_t now)
{
    if (!stats.start) stats.start = now;
    if (stats.last && main_pending()) {
        uint64_t elapsed = now - stats.last;
        GuestThread *blocker = owner ? owner : dispatch_owner;
        int phase = owner ? (owner->executing ? 0 : 1) : (blocker ? 2 : 3);
        if (main_thread->suspended) phase = 5;
        else if (owner == main_thread) phase = 4;
        XgsRow *r = stats_row(blocker, phase);
        stats.wait_ns += elapsed;
        if (r) r->ns += elapsed;
        else stats.overflow_ns += elapsed;
    }
    stats.last = now;
}

static void scheduler_lock(void)
{
    pthread_mutex_lock(&mutex);
    if (diagnostics && main_pending()) account(now_ns());
}

void xgs_diagnostics(int enabled)
{
    scheduler_lock();
    diagnostics = enabled;
    memset(&stats, 0, sizeof(stats));
    pthread_mutex_unlock(&mutex);
}

void xgs_mark_main(void)
{
    scheduler_lock();
    main_tid = GetCurrentThreadId();
    for (GuestThread *t = threads; t; t = t->next)
        if (t->tid == main_tid) main_thread = t;
    pthread_mutex_unlock(&mutex);
}

void xgs_poll(void)
{
    /* Only main prints, after releasing its execution token. No I/O under
     * the scheduler mutex and no clock/row traversal when counters are off. */
    if (!diagnostics || GetCurrentThreadId() != main_tid) return;
    XgsStats snapshot;
    uint64_t end;
    scheduler_lock();
    end = now_ns();
    account(end);
    if (end - stats.start < XGS_STATS_WINDOW) {
        pthread_mutex_unlock(&mutex);
        return;
    }
    snapshot = stats;
    memset(&stats, 0, sizeof(stats));
    stats.start = stats.last = end;
    pthread_mutex_unlock(&mutex);
    NFS_LOG_DEBUG("runtime", "[SCHED5] main_tid=%u window_ms=%.1f wait_ms=%.2f overflow_ms=%.2f"
        " spin=%llu dispatch=%llu higher=%llu quantum=%llu suspend=%llu other=%llu\n",
        main_tid, (end - snapshot.start) / 1e6, snapshot.wait_ns / 1e6,
        snapshot.overflow_ns / 1e6,
        (unsigned long long)snapshot.causes[0], (unsigned long long)snapshot.causes[1],
        (unsigned long long)snapshot.causes[2], (unsigned long long)snapshot.causes[3],
        (unsigned long long)snapshot.causes[4], (unsigned long long)snapshot.causes[5]);
    for (unsigned i = 0; i < snapshot.used; i++) {
        XgsRow *r = &snapshot.rows[i];
        NFS_LOG_DEBUG("runtime", "[SCHEDW] main_tid=%u tid=%u host_tid=%lu priority=%d main_priority=%d"
            " dispatch=%d phase=%s reason=%s wait_ms=%.2f selections=%llu\n", main_tid, r->tid, r->host_tid, r->priority,
            r->main_priority, r->dispatch, phases[r->phase], reasons[r->reason], r->ns / 1e6,
            (unsigned long long)r->selections);
    }
}

static GuestThread *find(DWORD tid)
{
    for (GuestThread *t = threads; t; t = t->next) if (t->tid == tid) return t;
    return NULL;
}

static int before(GuestThread *a, GuestThread *b)
{
    if (!b) return 1;
    if (a->dispatch != b->dispatch) return a->dispatch > b->dispatch;
    if (a->priority != b->priority) return a->priority > b->priority;
    return a->ready_order < b->ready_order;
}

static GuestThread *best_ready(void)
{
    GuestThread *best = NULL;
    for (GuestThread *t = threads; t; t = t->next) {
        if (t->state != XGS_READY || t->suspended) continue;
        if (dispatch_owner && t != dispatch_owner) continue;
        if (before(t, best)) best = t;
    }
    return best;
}

static void update_request(void)
{
    GuestThread *best = best_ready();
    int needed = owner && (owner->suspended || (best &&
        (best->dispatch > owner->dispatch ||
         (best->dispatch == owner->dispatch && best->priority >= owner->priority))));
    if (request) InterlockedExchange(request, needed);
}

static void choose(void)
{
    if (!owner) {
        owner = best_ready();
        if (owner) {
            owner->state = XGS_RUNNING;
            owner->executing = 0;
            if (owner->dispatch) dispatch_owner = owner;
            owner->selected_ns = now_ns();
            if (diagnostics && main_pending() && owner != main_thread) {
                XgsRow *r = stats_row(owner, 1);
                if (r) r->selections++;
            }
            pthread_cond_signal(&owner->gate);
        }
    }
    update_request();
}

static void ready(GuestThread *t)
{
    if (diagnostics && t == main_thread) account(now_ns());
    t->state = t->suspended ? XGS_SUSPENDED : XGS_READY;
    t->ready_order = ++ready_order;
}

static void acquire_locked(GuestThread *t)
{
    if (t == main_thread) main_wait_reason = 6;
    ready(t);
    choose();
    while (owner != t || t->suspended) pthread_cond_wait(&t->gate, &mutex);
    if (diagnostics) account(now_ns());
    t->started_ns = now_ns();
    t->executing = 1;
}

static void destroy_thread(void *value)
{
    GuestThread *t = value;
    if (!t) return;
    scheduler_lock();
    if (owner == t) owner = NULL;
    if (dispatch_owner == t) dispatch_owner = NULL;
    GuestThread **link = &threads;
    while (*link && *link != t) link = &(*link)->next;
    if (*link) *link = t->next;
    if (main_thread == t) main_thread = NULL;
    t->state = XGS_EXITED;
    choose();
    pthread_mutex_unlock(&mutex);
    pthread_cond_destroy(&t->gate);
    free(t);
}

static void make_key(void)
{
    if (pthread_key_create(&thread_key, destroy_thread) != 0) abort();
}

static GuestThread *self(void) { return pthread_getspecific(thread_key); }

static void observe_wait_begin(DWORD tid, uintptr_t object)
{
    (void)tid;
    GuestThread *t = self();
    if (!t) return;
    scheduler_lock();
    if (t->wait_depth++ == 0) {
        t->wait_object = object;
        t->wait_released_token = owner == t;
        t->wait_ready_ns = 0;
        if (t->wait_released_token && wait_park_account) wait_park_account();
        if (owner == t) owner = NULL;
        t->executing = 0;
        t->state = XGS_WAITING;
        choose();
    }
    pthread_mutex_unlock(&mutex);
}

static void observe_wait_end(DWORD tid)
{
    (void)tid;
    GuestThread *t = self();
    if (!t) return;
    scheduler_lock();
    if (!t->wait_depth) abort();
    if (--t->wait_depth == 0) {
        t->wait_object = 0;
        if (t->wait_released_token) {
            if (t->state == XGS_WAITING) {
                t->wait_ready_ns = now_ns();
                acquire_locked(t);
            }
            else {
                /* Dispatcher may already have published Ready and granted
                 * this token before the native waiter wakes. */
                while (owner != t || t->suspended) pthread_cond_wait(&t->gate, &mutex);
                if (diagnostics) account(now_ns());
                t->started_ns = now_ns();
                t->executing = 1;
            }
            if (wait_resume_account) {
                uint64_t selected = t->selected_ns > t->wait_ready_ns
                    ? t->selected_ns : t->wait_ready_ns;
                wait_resume_account(selected - t->wait_ready_ns, t->started_ns - selected);
            }
        } else t->state = XGS_HOST_CALL;
        t->wait_released_token = 0;
    }
    pthread_mutex_unlock(&mutex);
}

void xgs_wait_begin(uintptr_t object) { observe_wait_begin(GetCurrentThreadId(), object); }
void xgs_wait_end(void) { observe_wait_end(GetCurrentThreadId()); }

static void observe_priority(DWORD tid, int priority)
{
    scheduler_lock();
    GuestThread *t = find(tid);
    if (t) { t->priority = priority; choose(); }
    pthread_mutex_unlock(&mutex);
}

static void observe_suspend(DWORD tid, unsigned count)
{
    scheduler_lock();
    GuestThread *t = find(tid);
    if (t) {
        t->suspended = count;
        if (count && owner == t && !t->executing) {
            owner = NULL;
            t->state = XGS_SUSPENDED;
        }
        if (count && t->state == XGS_READY) t->state = XGS_SUSPENDED;
        else if (!count && t->state == XGS_SUSPENDED && !t->wait_depth) ready(t);
        choose();
    }
    pthread_mutex_unlock(&mutex);
}

static void observe_exit(DWORD tid) { (void)tid; xgs_unregister(); }
static const HostThreadObserver observer = {
    observe_wait_begin, observe_wait_end, observe_priority, observe_suspend, observe_exit
};

void xgs_init(volatile LONG *preempt_request)
{
    pthread_once(&once, make_key);
    request = preempt_request;
    host_set_thread_observer(&observer);
#ifdef __linux__
    xgd_init();
#endif
}

XgsAcquire xgs_acquire(DWORD tid, int priority, int dispatch)
{
    uint64_t start = now_ns();
    GuestThread *t = self();
    scheduler_lock();
    if (!t) {
        t = calloc(1, sizeof(*t));
        if (!t || pthread_cond_init(&t->gate, NULL) != 0) abort();
        t->tid = tid;
#ifdef __linux__
        t->host_tid = (unsigned long)syscall(SYS_gettid);
#endif
        t->priority = priority;
        host_current_thread_state(&t->priority, &t->suspended);
        t->state = XGS_HOST_CALL;
        t->next = threads;
        threads = t;
        if (tid == main_tid) main_thread = t;
        if (pthread_setspecific(thread_key, t) != 0) abort();
    }
    if (owner == t) abort();
    t->dispatch = dispatch;
    acquire_locked(t);
    XgsAcquire result = {t->selected_ns - start, t->started_ns - t->selected_ns, 1};
    pthread_mutex_unlock(&mutex);
    return result;
}

void xgs_release(int dispatch)
{
    GuestThread *t = self();
    scheduler_lock();
    if (!t || owner != t) abort();
    t->dispatch = dispatch;
    if (dispatch) dispatch_owner = t;
    t->state = XGS_HOST_CALL;
    t->executing = 0;
    owner = NULL;
    choose();
    pthread_mutex_unlock(&mutex);
}

int xgs_should_preempt(void)
{
    GuestThread *t = self();
    scheduler_lock();
    GuestThread *best = best_ready();
    int result = t && owner == t && !t->dispatch && (t->suspended || (best &&
        (best->dispatch || best->priority > t->priority ||
         (best->priority == t->priority && now_ns() - t->started_ns >= quantum_ns))));
    pthread_mutex_unlock(&mutex);
    return result;
}

XgsAcquire xgs_yield_reason(int spin)
{
    uint64_t start = now_ns();
    GuestThread *t = self();
    XgsAcquire result = {0, 0, 0};
    scheduler_lock();
    if (!t || owner != t) abort();
    if (!t->dispatch) {
        GuestThread *best = best_ready();
        int reason = spin ? 0 : t->suspended ? 4 :
            best && best->dispatch ? 1 :
            best && best->priority > t->priority ? 2 :
            best && best->priority == t->priority ? 3 : 5;
        if (t == main_thread) main_wait_reason = reason;
        ready(t);
        t->executing = 0;
        owner = NULL;
        choose();
        result.switched = owner != t;
        if (diagnostics && t == main_thread && result.switched) stats.causes[reason]++;
        while (owner != t || t->suspended) pthread_cond_wait(&t->gate, &mutex);
        if (diagnostics) account(now_ns());
        t->started_ns = now_ns();
        t->executing = 1;
        if (result.switched) {
            result.queued_ns = t->selected_ns - start;
            result.handoff_ns = t->started_ns - t->selected_ns;
        }
    }
    pthread_mutex_unlock(&mutex);
    return result;
}

void xgs_dispatch_changed(int raised)
{
    GuestThread *t = self();
    if (!t) return;
    scheduler_lock();
    t->dispatch = raised;
    if (raised && owner == t) dispatch_owner = t;
    else if (!raised && dispatch_owner == t) dispatch_owner = NULL;
    choose();
    pthread_mutex_unlock(&mutex);
}

void xgs_unregister(void)
{
    GuestThread *t = self();
    if (!t) return;
    pthread_setspecific(thread_key, NULL);
    destroy_thread(t);
}

XgsState xgs_state(DWORD tid)
{
    scheduler_lock();
    GuestThread *t = find(tid);
    XgsState result = t ? t->state : XGS_EXITED;
    pthread_mutex_unlock(&mutex);
    return result;
}

int xgs_current_priority(void)
{
    GuestThread *t = self();
    scheduler_lock();
    int result = t ? t->priority : 0;
    pthread_mutex_unlock(&mutex);
    return result;
}

XgsAcquire xgs_yield(void) { return xgs_yield_reason(1); }

void xgs_wait_complete(DWORD tid)
{
    scheduler_lock();
    GuestThread *t = find(tid);
    if (t && t->wait_depth && t->wait_released_token && t->state == XGS_WAITING) {
        t->wait_ready_ns = now_ns();
        ready(t);
        choose();
    }
    pthread_mutex_unlock(&mutex);
}

int xgs_thread_priority(DWORD tid, int fallback)
{
    scheduler_lock();
    GuestThread *t = find(tid);
    int priority = t ? t->priority : fallback;
    pthread_mutex_unlock(&mutex);
    return priority;
}
