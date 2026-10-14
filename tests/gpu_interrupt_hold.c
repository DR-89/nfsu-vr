/* Force an ack pass to sample "not held" before a trap is published.
 * cc -O1 -ffunction-sections -fdata-sections -Ixboxrecomp/src
 *    -Ixboxrecomp/src/kernel tests/gpu_interrupt_hold.c
 *    xboxrecomp/src/platform/win32_compat.c -Wl,--gc-sections -pthread -lm
 */
#include "platform/win32_compat.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/mman.h>
#include <time.h>

static LONG observed_compare(volatile LONG *, LONG, LONG);
#define InterlockedCompareExchange observed_compare
#include "../xboxrecomp/src/kernel/xbox_memory_layout.c"
#undef InterlockedCompareExchange

static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static int sampled, release_ack, posted;
static volatile uint32_t *registers;

static LONG observed_compare(volatile LONG *p, LONG value, LONG expected)
{
    LONG old = InterlockedCompareExchange(p, value, expected);
    if (p == &g_nv2a_intr_hold && !old && !sampled) {
        pthread_mutex_lock(&gate);
        sampled = 1;
        pthread_cond_broadcast(&changed);
        while (!release_ack)
            pthread_cond_wait(&changed, &gate);
        pthread_mutex_unlock(&gate);
    }
    return old;
}

static void *ack_thread(void *unused)
{
    (void)unused;
    nv2a_ack_flags(registers);
    return NULL;
}

static void *post_thread(void *unused)
{
    (void)unused;
    xbox_Nv2aHoldInterrupts(1);
    registers[0x400100 / 4] = 1;
    registers[0x100 / 4] = 0x1000;
    pthread_mutex_lock(&gate);
    posted = 1;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&gate);
    return NULL;
}

int main(void)
{
    pthread_t ack, post;
    registers = mmap(NULL, 0x700000, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(registers != MAP_FAILED);
    assert(!pthread_create(&ack, NULL, ack_thread, NULL));
    pthread_mutex_lock(&gate);
    while (!sampled)
        pthread_cond_wait(&changed, &gate);
    assert(!pthread_create(&post, NULL, post_thread, NULL));
    /* Old code publishes while the stale ack pass is blocked. Fixed code
     * waits for that pass; allow it to finish after this bounded wait. */
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_nsec += 100000000;
    if (deadline.tv_nsec >= 1000000000) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000;
    }
    while (!posted)
        if (pthread_cond_timedwait(&changed, &gate, &deadline))
            break;
    release_ack = 1;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&gate);
    pthread_join(ack, NULL);
    pthread_join(post, NULL);
    assert(registers[0x400100 / 4] == 1);
    assert(registers[0x100 / 4] == 0x1000);

    xbox_Nv2aHoldInterrupts(1); /* nested holds still protect PGRAPH */
    xbox_Nv2aHoldInterrupts(0);
    registers[0x100 / 4] |= 0x01000000;
    registers[0x600100 / 4] = 1;
    nv2a_ack_flags(registers);
    assert(registers[0x400100 / 4] == 1);
    assert(registers[0x100 / 4] == 0x1000);
    assert(registers[0x600100 / 4] == 0);
    xbox_Nv2aHoldInterrupts(0);
    nv2a_ack_flags(registers);
    assert(registers[0x400100 / 4] == 0);
    assert(registers[0x100 / 4] == 0);
    munmap((void *)registers, 0x700000);
    puts("GPU interrupt hold regression: passed");
    return 0;
}
