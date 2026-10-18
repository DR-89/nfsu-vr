/* Include the implementation to coordinate waiters without timing guesses.
 * cc -O1 -ffunction-sections -fdata-sections -Ixboxrecomp/src tests/platform_events_files.c -Wl,--gc-sections -pthread -lm -o /tmp/platform_events_files
 */
#include "../xboxrecomp/src/platform/win32_compat.c"
#include <assert.h>
#ifdef __ANDROID__
#define TEST_TEMP "/data/local/tmp/"
#else
#define TEST_TEMP "/tmp/"
#endif

static void *wait_event(void *h)
{
    return (void *)(uintptr_t)WaitForSingleObject(h, 2000);
}

static void check_pulse(int manual)
{
    HANDLE h = CreateEventW(NULL, manual, FALSE, NULL);
    w32_object *o = obj_from(h);
    pthread_t threads[2];
    for (int i = 0; i < 2; i++) assert(!pthread_create(&threads[i], NULL, wait_event, h));
    for (;;) {
        pthread_mutex_lock(&o->lock);
        int ready = o->ev_waiters == 2;
        pthread_mutex_unlock(&o->lock);
        if (ready) break;
        sched_yield();
    }
    assert(PulseEvent(h));
    int woke = 0;
    for (int i = 0; i < 2; i++) {
        void *result;
        assert(!pthread_join(threads[i], &result));
        woke += (uintptr_t)result == WAIT_OBJECT_0;
    }
    assert(woke == (manual ? 2 : 1));
    assert(WaitForSingleObject(h, 0) == WAIT_TIMEOUT);
    assert(SetEvent(h));
    assert(PulseEvent(h)); /* no waiter: clear even an existing signal */
    assert(WaitForSingleObject(h, 0) == WAIT_TIMEOUT);
    CloseHandle(h);
}

int main(void)
{
    check_pulse(0);
    check_pulse(1);
    char dir[] = TEST_TEMP "nfsu-delete-XXXXXX";
    assert(mkdtemp(dir));
    HANDLE h = w32_open_handle(open(dir, O_RDONLY | O_DIRECTORY), dir);
    w32_handle_delete_on_close(h, 1);
    assert(access(dir, F_OK) == 0);
    HANDLE duplicate;
    assert(DuplicateHandle(NULL, h, NULL, &duplicate, 0, FALSE, 0));
    CloseHandle(h);
    assert(access(dir, F_OK) == 0);
    CloseHandle(duplicate);
    assert(access(dir, F_OK) != 0);
    char file[] = TEST_TEMP "nfsu-delete-file-XXXXXX";
    int fd = mkstemp(file);
    assert(fd >= 0);
    h = w32_open_handle(fd, file);
    w32_handle_delete_on_close(h, 1);
    w32_handle_delete_on_close(h, 0);
    CloseHandle(h);
    assert(access(file, F_OK) == 0);
    assert(!unlink(file));
    puts("event pulses and delete disposition: PASS");
    return 0;
}
