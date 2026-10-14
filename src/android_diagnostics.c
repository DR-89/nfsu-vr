/* Bounded host-side history. Never calls guest code or suspends guest threads. */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>

typedef struct {
    uint64_t start, duration, sequence;
    long tid;
    uint32_t operation, caller, args[4], result;
} DiagnosticEvent;
typedef struct { DiagnosticEvent frames[8]; unsigned depth; long tid; } DiagnosticThread;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static DiagnosticThread threads[32];
static DiagnosticEvent history[128];
static uint64_t sequence, printed, dropped;
static __thread int thread_slot = -1;
static __thread unsigned overflow_depth;

static uint64_t milliseconds(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static void retain(DiagnosticEvent event)
{
    event.sequence = ++sequence;
    history[(sequence - 1) % 128] = event;
}

void nfsu_diag_kernel(uint32_t operation, uint32_t caller,
                      const uint32_t args[4], uint32_t result, int entering)
{
    DiagnosticThread *t;
    uint64_t now = milliseconds();
    pthread_mutex_lock(&mutex);
    if (thread_slot < 0 && entering) {
        long tid = syscall(SYS_gettid);
        for (unsigned i = 0; i < 32; i++) {
            if (threads[i].tid == tid || !threads[i].depth) {
                thread_slot = (int)i;
                threads[i].tid = tid;
                break;
            }
        }
    }
    if (thread_slot < 0) { dropped++; pthread_mutex_unlock(&mutex); return; }
    t = &threads[thread_slot];
    /* An inactive slot may have been reused by another thread. */
    if (t->tid != syscall(SYS_gettid)) {
        thread_slot = -1;
        dropped++;
        pthread_mutex_unlock(&mutex);
        return;
    }
    if (entering) {
        if (t->depth == 8 || overflow_depth) { overflow_depth++; dropped++; }
        else {
            DiagnosticEvent *e = &t->frames[t->depth++];
            memset(e, 0, sizeof(*e));
            e->start = now; e->tid = t->tid; e->operation = operation; e->caller = caller;
            memcpy(e->args, args, sizeof(e->args));
        }
    } else if (overflow_depth) overflow_depth--;
    else if (t->depth) {
        DiagnosticEvent e = t->frames[--t->depth];
        e.duration = now - e.start; e.result = result;
        /* Every file operation, plus failed or slow waits/locks/signals.
         * A routine Sleep or successful lock would drown the useful history. */
        if (operation == 187 || operation == 190 || operation == 198 ||
            operation == 219 || operation == 226 || operation == 236 ||
            (int32_t)result < 0 || e.duration >= 100)
            retain(e);
        if (!t->depth) thread_slot = -1;
    }
    pthread_mutex_unlock(&mutex);
}

/* Audio callers supply sampled object/list links in args; no live RAM reads
 * from the observing host thread. */
void nfsu_diag_voice(uint32_t operation, uint32_t caller, uint32_t object,
                     uint32_t next, uint32_t prev, uint32_t parent)
{
    DiagnosticEvent e = {0};
    e.start = milliseconds(); e.tid = syscall(SYS_gettid);
    e.operation = operation; e.caller = caller;
    e.args[0] = object; e.args[1] = next; e.args[2] = prev; e.args[3] = parent;
    pthread_mutex_lock(&mutex); retain(e); pthread_mutex_unlock(&mutex);
}

void nfsu_diag_io(uint32_t operation, uint32_t caller, uint32_t handle,
                  int64_t offset, uint32_t bytes, uint32_t status)
{
    DiagnosticEvent e = {0};
    e.start = milliseconds(); e.tid = syscall(SYS_gettid);
    e.operation = 0x40000000u | operation; e.caller = caller;
    e.args[0] = handle; e.args[1] = (uint32_t)offset;
    e.args[2] = (uint32_t)((uint64_t)offset >> 32); e.args[3] = bytes;
    e.result = status;
    pthread_mutex_lock(&mutex); retain(e); pthread_mutex_unlock(&mutex);
}

static void report(int force)
{
    static uint64_t last;
    uint64_t now = milliseconds();
    DiagnosticEvent events[128], active[32];
    unsigned count = 0, pending = 0;
    uint64_t omitted, lost;
    if ((!force && now - last < 5000) || pthread_mutex_trylock(&mutex) != 0) return;
    uint64_t first = sequence > 128 ? sequence - 127 : 1;
    if (first <= printed) first = printed + 1;
    omitted = first > printed + 1 ? first - printed - 1 : 0;
    for (uint64_t n = first; n <= sequence; n++) events[count++] = history[(n - 1) % 128];
    printed = sequence; lost = dropped;
    for (unsigned i = 0; i < 32; i++) if (threads[i].depth)
        active[pending++] = threads[i].frames[threads[i].depth - 1];
    pthread_mutex_unlock(&mutex);
    last = now;
    fprintf(stderr, "[RUNTIME] t=%llu events=%u pending=%u overwritten=%llu dropped=%llu\n",
            (unsigned long long)now, count, pending,
            (unsigned long long)omitted, (unsigned long long)lost);
    for (unsigned i = 0; i < count; i++) {
        DiagnosticEvent *e = &events[i];
        fprintf(stderr, "[HISTORY] seq=%llu t=%llu tid=%ld op=%08X caller=%08X"
                " args=%08X,%08X,%08X,%08X result=%08X ms=%llu\n",
                (unsigned long long)e->sequence, (unsigned long long)e->start,
                e->tid, e->operation, e->caller, e->args[0], e->args[1],
                e->args[2], e->args[3], e->result, (unsigned long long)e->duration);
    }
    for (unsigned i = 0; i < pending; i++) {
        DiagnosticEvent *e = &active[i];
        fprintf(stderr, "[WAITSTATE] tid=%ld ordinal=%u caller=%08X"
                " args=%08X,%08X,%08X,%08X age_ms=%llu\n", e->tid,
                e->operation, e->caller, e->args[0], e->args[1], e->args[2],
                e->args[3], (unsigned long long)(now >= e->start ? now - e->start : 0));
    }
    fflush(stderr);
}

void nfsu_diag_poll(void) { report(0); }
void nfsu_diag_dump(void) { report(1); }
