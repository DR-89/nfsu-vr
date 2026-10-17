/* cc -std=gnu11 -O1 -Wall -Wextra -Werror -ffunction-sections
 * -fdata-sections -Ixboxrecomp/src tests/guest_scheduler.c
 * xboxrecomp/src/platform/win32_compat.c -Wl,--gc-sections -pthread -lm
 * -o /tmp/nfsu-guest-scheduler-test */
#include "../xboxrecomp/src/kernel/guest_scheduler.c"
#include <assert.h>
#include <stdatomic.h>
#include <sched.h>
#include <stdio.h>

static volatile LONG preempt;
static atomic_uint ids[8], finished;
static unsigned order[8], used, active;
static HANDLE event;
static DWORD main_id;

typedef struct { unsigned index; int priority; } Start;
static Start starts[8];

static void test_attribution(void)
{
    GuestThread main = {.tid = 1, .priority = 0, .state = XGS_READY};
    GuestThread worker = {.tid = 2, .host_tid = 20, .priority = 4, .executing = 1};
    main_thread = &main;
    owner = &worker;
    account(100);
    account(200); /* Work by the selected higher-priority thread. */
    worker.executing = 0;
    account(400); /* Selected, but not executing yet. */
    owner = &main;
    account(450); /* Main itself is selected, but still waking. */
    owner = NULL;
    worker.dispatch = 1;
    dispatch_owner = &worker;
    account(750); /* DISPATCH host work reserves the guest CPU. */
    dispatch_owner = NULL;
    account(790);
    main.suspended = 1;
    account(800);
    assert(stats.wait_ns == 700 && stats.used == 6);
    assert(stats.rows[0].ns == 100 && stats.rows[0].host_tid == 20);
    assert(stats.rows[1].ns == 200 && stats.rows[1].phase == 1);
    assert(stats.rows[2].ns == 50 && stats.rows[2].phase == 4);
    assert(stats.rows[3].ns == 300 && stats.rows[3].phase == 2);
    assert(stats.rows[4].ns == 40 && stats.rows[4].phase == 3);
    assert(stats.rows[5].ns == 10 && stats.rows[5].phase == 5);
    stats.used = XGS_STATS_ROWS;
    main.priority = 7; /* New bucket beyond the fixed capacity. */
    account(900);
    assert(stats.overflow_ns == 100 && stats.wait_ns == 800);
    owner = main_thread = NULL;
    memset(&stats, 0, sizeof(stats));
}

static void wait_state(DWORD tid, XgsState state)
{
    uint64_t deadline = now_ns() + 2000000000ull;
    while (xgs_state(tid) != state) { assert(now_ns() < deadline); sched_yield(); }
}
static DWORD wait_id(unsigned index)
{
    uint64_t deadline = now_ns() + 2000000000ull;
    unsigned id;
    while (!(id = atomic_load(&ids[index]))) { assert(now_ns() < deadline); sched_yield(); }
    return id;
}
static void *ordered(void *arg)
{
    Start *s = arg;
    DWORD id = GetCurrentThreadId();
    atomic_store(&ids[s->index], id);
    xgs_acquire(id, s->priority, 0);
    assert(active++ == 0);
    order[used++] = s->index;
    assert(--active == 0);
    xgs_release(0);
    xgs_unregister();
    return NULL;
}
static DWORD WINAPI blocked(void *unused)
{
    (void)unused;
    DWORD id = GetCurrentThreadId();
    atomic_store(&ids[0], id);
    xgs_acquire(id, GetThreadPriority(GetCurrentThread()), 0);
    assert(WaitForSingleObject(event, INFINITE) == WAIT_OBJECT_0);
    assert(xgs_state(id) == XGS_RUNNING);
    atomic_store(&finished, 1);
    xgs_release(0);
    return 0;
}
static DWORD WINAPI parked(void *unused)
{
    (void)unused;
    DWORD id = GetCurrentThreadId();
    atomic_store(&ids[0], id);
    xgs_acquire(id, 0, 0);
    atomic_store(&finished, 1);
    /* Deliberately exit while holding the token: ExitThread must relinquish it. */
    ExitThread(0);
    return 0;
}
static DWORD WINAPI timeout_wait(void *unused)
{
    (void)unused;
    DWORD id = GetCurrentThreadId();
    xgs_acquire(id, 0, 0);
    assert(WaitForSingleObject(event, 1) == WAIT_TIMEOUT);
    assert(xgs_state(id) == XGS_RUNNING);
    xgs_release(0);
    return 0;
}
int main(void)
{
    xgs_init(&preempt);
    test_attribution();
    xgs_diagnostics(1);
    xgs_mark_main();
    main_id = GetCurrentThreadId();
    xgs_acquire(main_id, 0, 0);
    assert(!xgs_should_preempt());
    pthread_t workers[3];
    int priorities[] = {-1, 4, 4};
    for (unsigned i = 0; i < 3; i++) {
        starts[i] = (Start){i, priorities[i]};
        assert(!pthread_create(&workers[i], NULL, ordered, &starts[i]));
        wait_state(wait_id(i), XGS_READY);
    }
    assert(xgs_should_preempt());
    xgs_release(0);
    for (unsigned i = 0; i < 3; i++) assert(!pthread_join(workers[i], NULL));
    assert(used == 3 && order[0] == 1 && order[1] == 2 && order[2] == 0);

    atomic_store(&ids[0], 0);
    event = CreateEventW(NULL, FALSE, FALSE, NULL);
    HANDLE h = CreateThread(NULL, 0, blocked, NULL, 0, NULL);
    DWORD id = wait_id(0);
    wait_state(id, XGS_WAITING);
    xgs_acquire(main_id, 0, 0);
    SetEvent(event);
    wait_state(id, XGS_READY);
    assert(!atomic_load(&finished));
    /* Equal-priority competition: only an expired shared quantum preempts. */
    /* Force expiry for a deterministic decision without timing sleeps. */
    pthread_mutex_lock(&mutex);
    owner->started_ns = now_ns() - quantum_ns - 1;
    pthread_mutex_unlock(&mutex);
    assert(xgs_should_preempt());
    XgsAcquire switched = xgs_yield();
    assert(switched.switched && atomic_load(&finished));
    xgs_release(0);
    assert(WaitForSingleObject(h, 2000) == WAIT_OBJECT_0);
    CloseHandle(h); CloseHandle(event);

    xgs_acquire(main_id, 0, 0);
    atomic_store(&ids[1], 0);
    starts[1] = (Start){1, 0};
    assert(!pthread_create(&workers[1], NULL, ordered, &starts[1]));
    wait_state(wait_id(1), XGS_READY);
    pthread_mutex_lock(&mutex);
    owner->started_ns = now_ns() - quantum_ns - 1;
    pthread_mutex_unlock(&mutex);
    assert(xgs_should_preempt() && xgs_yield_reason(0).switched);
    assert(stats.causes[3] == 1);
    xgs_release(0);
    assert(!pthread_join(workers[1], NULL));

    event = CreateEventW(NULL, FALSE, FALSE, NULL);
    h = CreateThread(NULL, 0, timeout_wait, NULL, 0, NULL);
    assert(WaitForSingleObject(h, 2000) == WAIT_OBJECT_0);
    CloseHandle(h); CloseHandle(event);

    atomic_store(&ids[0], 0); atomic_store(&finished, 0);
    xgs_acquire(main_id, 0, 0);
    h = CreateThread(NULL, 0, parked, NULL, 0, NULL);
    id = wait_id(0); wait_state(id, XGS_READY);
    SetThreadPriority(h, 4);
    pthread_mutex_lock(&mutex);
    assert(find(id)->priority == 4);
    pthread_mutex_unlock(&mutex);
    assert(xgs_should_preempt());
    SuspendThread(h); SuspendThread(h);
    assert(xgs_state(id) == XGS_SUSPENDED);
    xgs_release(0);
    ResumeThread(h);
    assert(xgs_state(id) == XGS_SUSPENDED && !atomic_load(&finished));
    ResumeThread(h);
    assert(WaitForSingleObject(h, 2000) == WAIT_OBJECT_0);
    assert(atomic_load(&finished));
    CloseHandle(h);
    xgs_acquire(main_id, 0, 1);
    xgs_release(1); /* Raised execution context reserves the virtual CPU. */
    atomic_store(&ids[0], 0); used = 0;
    starts[0] = (Start){0, 15};
    assert(!pthread_create(&workers[0], NULL, ordered, &starts[0]));
    wait_state(wait_id(0), XGS_READY);
    xgs_acquire(main_id, 0, 1);
    assert(!xgs_should_preempt());
    xgs_dispatch_changed(0);
    assert(xgs_should_preempt());
    assert(xgs_yield_reason(0).switched);
    xgs_release(0);
    assert(!pthread_join(workers[0], NULL));
    assert(used == 1);
    xgs_unregister();
    assert(!owner && !threads);
    uint64_t attributed = stats.overflow_ns;
    for (unsigned i = 0; i < stats.used; i++) attributed += stats.rows[i].ns;
    assert(stats.wait_ns > 0 && attributed == stats.wait_ns);
    assert(stats.causes[0] > 0);
    assert(stats.causes[2] > 0);
    /* Exercise snapshot/reset and output outside the lock without sleeping. */
    stats.start = now_ns() - XGS_STATS_WINDOW - 1;
    xgs_poll();
    assert(stats.used == 0 && stats.wait_ns == 0);
    xgs_diagnostics(0);
    puts("PASS: priority/FIFO, host wait handoff, equal-priority quantum, nested suspend/resume, exit cleanup, DISPATCH reservation");
    return 0;
}
