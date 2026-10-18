#define _GNU_SOURCE
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include "../src/android_logs.h"
#include "../xboxrecomp/include/nfsu_log.h"

static char *read_log(const char *path)
{
    FILE *file = fopen(path, "rb");
    assert(file);
    assert(!fseek(file, 0, SEEK_END));
    long length = ftell(file);
    assert(length >= 0);
    rewind(file);
    char *text = calloc((size_t)length + 1, 1);
    assert(text);
    assert(fread(text, 1, (size_t)length, file) == (size_t)length);
    fclose(file);
    return text;
}

static void *producer(void *arg)
{
    for (int i = 0; i < 100; i++)
        NFS_LOG_DEBUG("input", "worker=%ld sample=%d", (long)arg, i);
    return NULL;
}

static void emergency_signal(int sig)
{
    (void)sig;
    nfs_log_emergency_mode();
    NFS_LOG_FATAL("system", "fatal-signal-marker\n");
}

static void run_session(int number)
{
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        if (number > 1) assert(!rename("nfsu1.log", "nfsu1.previous.log"));
        assert(freopen("nfsu1.log", "w", stdout));
        assert(dup2(fileno(stdout), STDERR_FILENO) >= 0);
        int flags = fcntl(STDOUT_FILENO, F_GETFL);
        assert(fcntl(STDOUT_FILENO, F_SETFL, flags | O_APPEND) >= 0);
        setenv("RECOMP_LOG_LEVEL", number == 3 ? "WARNING" : "TRACE", 1);
        if (number == 7) {
            assert(!android_logs_start());
            NFS_LOG_ERROR("system", "directory-conflict-fallback");
            _exit(0);
        }
        if (number == 4) {
            FILE *file = fopen("zlog.conf", "w");
            assert(file);
            fputs("invalid config", file);
            fclose(file);
            assert(!android_logs_start());
            NFS_LOG_ERROR("system", "fallback-marker");
            _exit(0);
        }
        if (number == 5) {
            FILE *file = fopen("zlog.conf", "w");
            assert(file);
            fputs("[global]\nfile perms = 644\n[formats]\nplain = \"%m%n\"\n[rules]\ninput.DEBUG \"logs/size.log\", 1KB * 2 ~ \"logs/size.%d(%Y-%m-%d).#r.log\"; plain\n", file);
            fclose(file);
        }
        if (number == 6) {
            assert(!unlink("zlog.conf"));
            setenv("RECOMP_LOG_LEVEL", "OFF", 1);
            setenv("RECOMP_LOG_INPUT_LEVEL", "DEBUG", 1);
        }
        assert(android_logs_start());
        mode_t unchanged_mask = umask(0027);
        assert(unchanged_mask == 0027); /* Logger must not change process policy. */
        if (number == 6) {
            NFS_LOG_TRACE("input", "off-test-trace");
            NFS_LOG_DEBUG("input", "category-override-debug");
            NFS_LOG_FATAL("system", "off-test-system");
            _exit(0);
        }
        if (number == 5) {
            char payload[201];
            memset(payload, 'r', 200); payload[200] = 0;
            for (int i = 0; i < 100; i++) NFS_LOG_DEBUG("input", "size-item=%d %s", i, payload);
            /* Create a fresh live file after the last size boundary. */
            NFS_LOG_DEBUG("input", "size-live-after-rotation");
            _exit(0);
        }
        if (number == 3) {
            int side_effect = 0;
            NFS_LOG_DEBUG("input", "filtered-debug=%d", ++side_effect);
            NFS_LOG_TRACE("input", "filtered-trace");
            NFS_LOG_INFO("input", "filtered-info");
            assert(side_effect == 0);
            NFS_LOG_WARN("input", "visible-warning");
            NFS_LOG_ERROR("input", "visible-error");
            NFS_LOG_FATAL("input", "visible-fatal");
            signal(SIGUSR1, emergency_signal);
            raise(SIGUSR1);
            _exit(0);
        }
        NFS_LOG_INFO("system", "session=%d", number);
        NFS_LOG_TRACE("input", "trace-marker");
        NFS_LOG_DEBUG("input", "debug-marker");
        NFS_LOG_INFO("input", "info-marker");
        NFS_LOG_NOTICE("input", "notice-marker");
        NFS_LOG_WARN("input", "warning-marker");
        NFS_LOG_ERROR("input", "error-marker");
        NFS_LOG_FATAL("input", "fatal-marker");
        NFS_LOG_STREAM("input", NFS_LOG_LEVEL_TRACE, "fragment-");
        NFS_LOG_STREAM("input", NFS_LOG_LEVEL_TRACE, "complete\n");
        NFS_LOG_INFO("graphics", "renderer");
        NFS_LOG_INFO("audio", "voice");
        NFS_LOG_INFO("files", "music");
        NFS_LOG_INFO("runtime", "snapshot");
        NFS_LOG_INFO("performance", "counters");
        NFS_LOG_INFO("kernel", "kernel-marker");
        char *long_message = malloc(20001);
        assert(long_message);
        memset(long_message, 'x', 20000);
        long_message[20000] = 0;
        NFS_LOG_INFO("input", "long=%s", long_message);
        free(long_message);
        pthread_t threads[4];
        for (long i = 0; i < 4; i++) assert(!pthread_create(&threads[i], NULL, producer, (void *)i));
        for (int i = 0; i < 4; i++) assert(!pthread_join(threads[i], NULL));
        NFS_LOG_STREAM("system", NFS_LOG_LEVEL_INFO, "partial tail");
        android_logs_flush();
        _exit(0);
    }
    int status;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

int main(void)
{
    char directory[] = "/tmp/nfsu-zlog-XXXXXX";
    assert(mkdtemp(directory));
    assert(!chdir(directory));
    umask(0027);
    run_session(1);
    struct stat permissions;
    assert(!stat("logs", &permissions));
    assert((permissions.st_mode & 0777) == 0750);
    const char *permission_files[] = {"logs/all.log", "logs/input.log", "logs/crash.log", "logs/zlog.generated.conf", "nfsu1.log"};
    for (unsigned i = 0; i < sizeof(permission_files)/sizeof(permission_files[0]); i++) {
        assert(!stat(permission_files[i], &permissions));
        assert((permissions.st_mode & 0777) == 0640);
    }
    char *first = read_log("logs/all.log");
    FILE *fixture = fopen("logs/retained.previous.log", "w");
    assert(fixture); fputs("old-file-marker", fixture); fclose(fixture);
    assert(!chmod("logs/retained.previous.log", 0600));
    fixture = fopen("logs/retained.log.0", "w");
    assert(fixture); fputs("size-archive-marker", fixture); fclose(fixture);
    run_session(2);
    char path[1024];
    char *appended = read_log("logs/all.log");
    assert(strlen(appended) > strlen(first));
    assert(!memcmp(first, appended, strlen(first))); /* zlog appends across starts. */
    free(appended); free(first);
    char *previous = read_log("logs/retained.previous.log");
    assert(!strcmp(previous, "old-file-marker")); free(previous);
    assert(!stat("logs/retained.previous.log", &permissions));
    assert((permissions.st_mode & 0777) == 0600); /* No startup chmod/move. */
    previous = read_log("logs/retained.log.0");
    assert(!strcmp(previous, "size-archive-marker")); free(previous);
    char *all = read_log("logs/all.log");
    char *legacy = read_log("nfsu1.log");
    /* Destinations can interleave differently across producer threads. */
    assert(strlen(all) > strlen(legacy));
    for (char *p = legacy; *p;) {
        char *end = strchr(p, '\n');
        assert(end);
        char saved = end[1]; end[1] = 0;
        assert(strstr(all, p));
        end[1] = saved; p = end + 1;
    }
    assert(strstr(all, "partial tail"));
    free(legacy);
    free(all);
    char *input = read_log("logs/input.log");
    const char *levels[] = {"TRACE", "DEBUG", "INFO", "NOTICE", "WARN", "ERROR", "FATAL"};
    for (unsigned i = 0; i < sizeof(levels)/sizeof(levels[0]); i++) assert(strstr(input, levels[i]));
    assert(strstr(input, "fragment-complete\n"));
    assert(strstr(input, "android_logs.c:"));
    assert(strstr(input, "[tid:"));
    assert(!strstr(input, "renderer"));
    char *long_start = strstr(input, "long=");
    assert(long_start);
    long_start += 5;
    size_t x = strspn(long_start, "x");
    assert(x == 20000);
    int count = 0;
    for (char *p = input; (p = strstr(p, "worker=")); p++) count++;
    assert(count == 800);
    free(input);
    const char *files[] = {"graphics", "audio", "files", "runtime", "performance", "kernel", "system"};
    const char *markers[] = {"renderer", "voice", "music", "snapshot", "counters", "kernel-marker", "session=2"};
    for (int i = 0; i < 7; i++) {
        char path[128];
        snprintf(path, sizeof(path), "logs/%s.log", files[i]);
        char *text = read_log(path);
        assert(strstr(text, markers[i]));
        free(text);
    }
    run_session(3);
    input = read_log("logs/input.log");
    assert(!strstr(input, "filtered-"));
    assert(strstr(input, "visible-warning"));
    assert(strstr(input, "visible-error"));
    assert(strstr(input, "visible-fatal"));
    free(input);
    char *crash = read_log("logs/crash.log");
    assert(strstr(crash, "fatal-signal-marker"));
    free(crash);
    run_session(4);
    legacy = read_log("nfsu1.log");
    assert(strstr(legacy, "fallback-marker"));
    free(legacy);
    run_session(5);
    char *size_log = read_log(access("logs/size.log", F_OK) == 0 ? "logs/size.log" : "logs/size.log.0");
    assert(strstr(size_log, "size-live-after-rotation"));
    free(size_log);
    DIR *logs = opendir("logs");
    assert(logs);
    int archive_count = 0;
    struct dirent *entry;
    while ((entry = readdir(logs)))
        if (!strncmp(entry->d_name, "size.", 5)) {
            snprintf(path, sizeof(path), "logs/%s", entry->d_name);
            assert(!stat(path, &permissions));
            assert((permissions.st_mode & 0777) == 0640);
            if (strcmp(entry->d_name, "size.log")) {
                assert(strlen(entry->d_name) > 16);
                assert(entry->d_name[9] == '-' && entry->d_name[12] == '-');
                archive_count++;
            }
        }
    closedir(logs);
    assert(archive_count > 0 && archive_count <= 2);
    run_session(6);
    input = read_log("logs/input.log");
    assert(strstr(input, "category-override-debug"));
    assert(!strstr(input, "off-test-"));
    free(input);
    all = read_log("logs/all.log");
    assert(!strstr(all, "off-test-"));
    free(all);
    assert(!rename("logs", "saved-last-session"));
    fixture = fopen("logs", "w");
    assert(fixture); fputs("preserve-existing-file", fixture); fclose(fixture);
    run_session(7);
    previous = read_log("logs");
    assert(!strcmp(previous, "preserve-existing-file")); free(previous);
    previous = read_log("nfsu1.log");
    assert(strstr(previous, "directory-conflict-fallback")); free(previous);
    puts("zlog: levels, aliases, filters, OFF/overrides, fragments, long messages, threads, native dated size rotation, append across starts, directory conflict and signal fallback: PASS");
    return 0;
}
