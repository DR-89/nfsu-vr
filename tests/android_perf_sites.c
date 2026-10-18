/* cc -std=gnu11 -Wall -Wextra -Werror -pthread tests/android_perf_sites.c
 * -o /tmp/nfsu-perf-sites-test */
#define NFSU_PERF_HOST_TEST 1
#include "../src/android_perf.c"
#include <assert.h>
#include <pthread.h>

static void record(uint32_t ordinal, uint32_t caller, uint64_t wait)
{
    uint64_t before = nfsu_perf_gil_wait_sum();
    nfsu_perf_add(NFSU_PERF_GIL_WAIT, wait);
    nfsu_perf_kernel_resume(ordinal, caller, 1000, before);
}

static void *isolated(void *unused)
{
    (void)unused;
    assert(perf.resumes.count == 0);
    nfsu_perf_begin();
    record(234, 0x1234, 50);
    assert(perf.resumes.count == 1 && perf.resumes.wait_ns == 50);
    return NULL;
}

int main(void)
{
    nfsu_perf_enabled = 1;
    nfsu_perf_begin();
    record(234, 0x1234, 25);
    record(234, 0x1234, 50);
    record(234, 0x5678, 75);
    unsigned used = 0;
    for (int i = 0; i < PERF_KERNEL_SITES; i++) {
        PerfKernel *s = &perf.sites[i];
        if (!s->count) continue;
        used++;
        if (s->caller == 0x1234) {
            assert(s->count == 2 && s->wait_ns == 75 && s->wait_max == 50);
            assert(s->kernel_ns == 2000);
        } else assert(s->caller == 0x5678 && s->count == 1 && s->wait_ns == 75);
    }
    assert(used == 2 && perf.resumes.count == 3 && perf.resumes.wait_ns == 150);
    pthread_t worker;
    assert(!pthread_create(&worker, NULL, isolated, NULL));
    assert(!pthread_join(worker, NULL));
    assert(perf.resumes.count == 3); /* Other thread has separate storage. */

    for (unsigned i = 0; i < PERF_KERNEL_SITES + 10; i++) record(i, 0xABC000 + i, 10);
    uint64_t calls = perf.overflow.count, waits = perf.overflow.wait_ns;
    for (int i = 0; i < PERF_KERNEL_SITES; i++) {
        calls += perf.sites[i].count;
        waits += perf.sites[i].wait_ns;
    }
    assert(perf.overflow.count > 0 && calls == perf.resumes.count && waits == perf.resumes.wait_ns);
    nfsu_perf_enabled = 0;
    record(0, 0, 100);
    assert(perf.resumes.count == calls);
    nfsu_perf_enabled = 1;
    perf.window -= 5000000000ull;
    nfsu_perf_poll();
    assert(!perf.resumes.count && !perf.overflow.count);
    for (int i = 0; i < PERF_KERNEL_SITES; i++) assert(!perf.sites[i].count);
    puts("PASS: call-site aggregation, wait delta, TLS isolation, overflow, disabled mode, report reset");
    return 0;
}
