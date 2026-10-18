/* cc -Wall -Wextra -Werror tests/android_env.c src/android_env.c -o /tmp/nfsu-env-test */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../src/android_env.h"
#include "../src/android_input_trace.h"

int main(void)
{
    char path[] = "/tmp/nfsu-env-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    FILE *file = fdopen(fd, "w");
    assert(file);
    fputs("# comment\r\n\n RECOMP_INPUT_TRACE = 1 \r\n"
          "invalid line\nBAD KEY=1\n=empty\n", file);
    for (int i = 0; i < 1100; i++) fputc('X', file);
    fputs("RECOMP_INPUT_TRACE=0\nRECOMP_INPUT_DIAG=1\n", file);
    assert(fclose(file) == 0);
    setenv("RECOMP_INPUT_TRACE", "0", 1);
    assert(!android_input_trace_enabled());
    android_env_load(path);
    assert(android_input_trace_enabled());
    assert(strcmp(getenv("RECOMP_INPUT_DIAG"), "1") == 0);
    assert(getenv("BAD KEY") == NULL);
    assert(unlink(path) == 0);
    android_env_load(path); /* Missing optional file leaves settings intact. */
    assert(android_input_trace_enabled());
    setenv("RECOMP_INPUT_TRACE", "0", 1);
    assert(!android_input_trace_enabled());
    puts("Android env parsing and explicit trace enable: PASS");
    return 0;
}
