/* Regression for reservation protections and exact-address mapping refusal. */
#include "platform/win32_compat.h"
#include <assert.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    void *guard = VirtualAlloc(NULL, page, MEM_RESERVE, PAGE_NOACCESS);
    assert(guard);
    pid_t child = fork();
    assert(child >= 0);
    if (!child) { *(volatile unsigned char *)guard = 1; _exit(0); }
    int status;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV);
    assert(VirtualFree(guard, 0, MEM_RELEASE));

    unsigned char *owned = VirtualAlloc(NULL, page, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(owned);
    owned[0] = 0x5a;
    assert(!VirtualAlloc(owned, page, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    assert(owned[0] == 0x5a);
    assert(VirtualFree(owned, 0, MEM_RELEASE));

    size_t span_size = (size_t)UINT64_C(0x100000000);
    unsigned char *span = VirtualAlloc(NULL, span_size, MEM_RESERVE, PAGE_NOACCESS);
    assert(span);
    unsigned char *high = span + 0xfd000000u;
    assert(VirtualFree(high, page, MEM_RELEASE));
    assert(VirtualAlloc(high, page, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE) == high);
    high[0] = 0xa5;
    assert(high[0] == 0xa5);
    assert(VirtualFree(high, 0, MEM_RELEASE));
    assert(VirtualFree(span, span_size, MEM_RELEASE));
    puts("platform memory regression passed");
    return 0;
}
