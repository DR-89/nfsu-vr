/* Host-only test: cc -Wall -Wextra -Werror -pthread tests/android_diagnostics.c
 * -o /tmp/nfsu-diagnostics-test && /tmp/nfsu-diagnostics-test */
#include <assert.h>
#include "../src/android_diagnostics.c"

static void *produce(void *unused)
{
    uint32_t args[4] = {1, 2, 3, 4};
    (void)unused;
    for (unsigned i = 0; i < 1000; i++) {
        nfsu_diag_kernel(219, 123, args, 0, 1);
        nfsu_diag_kernel(236, 124, args, 0, 1);
        nfsu_diag_kernel(236, 124, args, 0, 0);
        nfsu_diag_kernel(219, 123, args, 0, 0);
    }
    return NULL;
}

int main(void)
{
    pthread_t workers[8];
    for (unsigned i = 0; i < 8; i++) assert(!pthread_create(&workers[i], NULL, produce, NULL));
    for (unsigned i = 0; i < 8; i++) assert(!pthread_join(workers[i], NULL));
    assert(sequence == 16000 && dropped == 0);
    for (unsigned i = 0; i < 32; i++) assert(threads[i].depth == 0);
    for (uint64_t i = sequence - 127; i <= sequence; i++) {
        DiagnosticEvent e = history[(i - 1) % 128];
        assert(e.sequence == i && (e.operation == 219 || e.operation == 236));
        assert(e.args[3] == 4 && e.result == 0);
    }
    uint32_t args[4] = {0};
    for (unsigned i = 0; i < 10; i++) nfsu_diag_kernel(219, 0, args, 0, 1);
    for (unsigned i = 0; i < 10; i++) nfsu_diag_kernel(219, 0, args, 0, 0);
    assert(overflow_depth == 0 && dropped == 2 && thread_slot == -1);
    puts("concurrent producers, nested calls, ring wrap, overflow: PASS");
    return 0;
}
