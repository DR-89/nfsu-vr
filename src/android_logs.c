/* zlog owns levels, formatting, file destinations and size rotation. */
#include "android_logs.h"
#include "../xboxrecomp/include/nfsu_log.h"
#include "zlog.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *names[] = {
    "system", "input", "graphics", "audio", "files", "runtime", "performance", "kernel"
};
#define CATEGORY_COUNT (sizeof(names) / sizeof(names[0]))
static zlog_category_t *categories[CATEGORY_COUNT];
static atomic_int ready;
static volatile sig_atomic_t emergency;
static int crash_fd = -1;

void nfs_log_emergency_mode(void) { emergency = 1; }

static unsigned category_index(const char *name)
{
    for (unsigned i = 0; i < CATEGORY_COUNT; i++)
        if (!strcmp(name, names[i])) return i;
    return 0;
}

static const char *level_setting(const char *value, const char *fallback)
{
    static const char *levels[] = {"TRACE", "DEBUG", "INFO", "NOTICE", "WARN", "ERROR", "FATAL", "OFF"};
    if (value) {
        if (!strcmp(value, "WARNING")) return "WARN";
        if (!strcmp(value, "CRITICAL")) return "FATAL";
        for (unsigned i = 0; i < sizeof(levels) / sizeof(levels[0]); i++)
            if (!strcmp(value, levels[i])) return levels[i];
    }
    return fallback;
}

static int default_config(void)
{
    FILE *file = fopen("logs/zlog.generated.conf", "w");
    if (!file) return 0;
    fputs("[global]\nstrict init = true\nuse_writer_thread = false\n"
          "buffer min = 4KB\nbuffer max = 2MB\nfile perms = 644\n"
          "rotate lock file = logs/.zlog.lock\n"
          "[levels]\nTRACE = 10, LOG_DEBUG\nOFF = 254, LOG_EMERG\n"
          "[formats]\nstandard = \"%d(%F %T).%ms %-6V [%c] [tid:%k] %f:%L %m%n\"\n"
          "[rules]\n", file);
    const char *global = level_setting(getenv("RECOMP_LOG_LEVEL"), "INFO");
    for (unsigned i = 0; i < CATEGORY_COUNT; i++) {
        char variable[80];
        snprintf(variable, sizeof(variable), "RECOMP_LOG_%s_LEVEL", names[i]);
        for (char *p = variable; *p; p++) *p = (char)toupper((unsigned char)*p);
        const char *fallback = global;
        const char *trace = getenv("RECOMP_INPUT_TRACE");
        if (!strcmp(names[i], "input") && trace && !strcmp(trace, "1")) fallback = "TRACE";
        const char *level = level_setting(getenv(variable), fallback);
        fprintf(file, "%s.%s \"logs/%s.log\", 8MB * 3 ~ \"logs/%s.%%d(%%Y-%%m-%%d).#r.log\"; standard\n"
                "%s.%s \"logs/all.log\", 32MB * 3 ~ \"logs/all.%%d(%%Y-%%m-%%d).#r.log\"; standard\n"
                "%s.%s \"nfsu1.log\"; standard\n",
                names[i], level, names[i], names[i], names[i], level, names[i], level);
    }
    int failed = ferror(file);
    if (fclose(file) != 0) failed = 1;
    return !failed;
}

int android_logs_start(void)
{
    if (atomic_load(&ready)) return 0;
    if (mkdir("logs", 0755) != 0 && errno != EEXIST) return 0;
    if (!default_config()) return 0;
    setenv("ZLOG_PROFILE_ERROR", "logs/zlog.error.log", 0);
    crash_fd = open("logs/crash.log", O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    const char *config = access("zlog.conf", F_OK) == 0 ? "zlog.conf" : "logs/zlog.generated.conf";
    if (zlog_init(config) != 0) {
        fprintf(stderr, "[ANDROID] zlog init failed: %s; using stderr\n", config);
        if (crash_fd >= 0) close(crash_fd);
        crash_fd = -1;
        return 0;
    }
    for (unsigned i = 0; i < CATEGORY_COUNT; i++) {
        categories[i] = zlog_get_category(names[i]);
        if (!categories[i]) {
            zlog_fini();
            if (crash_fd >= 0) close(crash_fd);
            crash_fd = -1;
            return 0;
        }
    }
    atomic_store(&ready, 1);
    NFS_LOG_NOTICE("system", "zlog %s initialized, config=%s", zlog_version(), config);
    return 1;
}

int nfs_log_enabled(const char *category, int level)
{
    if (emergency) return 1;
    if (!atomic_load(&ready)) return 1;
    return zlog_level_enabled(categories[category_index(category)], level);
}

static void emit(const char *category, int level, const char *file,
        const char *function, long line, const char *tag, const char *text, size_t length)
{
    while (length && text[length - 1] == '\n') length--;
    if (!atomic_load(&ready)) {
        if (tag) fprintf(stderr, "[%s] ", tag);
        fprintf(stderr, "[%s] %.*s\n", category, (int)length, text);
        return;
    }
    if (tag)
        zlog(categories[category_index(category)], file, strlen(file), function,
                strlen(function), line, level, "[%s] %.*s", tag, (int)length, text);
    else
        zlog(categories[category_index(category)], file, strlen(file), function,
                strlen(function), line, level, "%.*s", (int)length, text);
}

static char *format_message(char *local, size_t capacity, const char *format, va_list args)
{
    va_list copy;
    va_copy(copy, args);
    int length = vsnprintf(local, capacity, format, copy);
    va_end(copy);
    if (length < 0) return NULL;
    if ((size_t)length < capacity) return local;
    if (length > 2 * 1024 * 1024) return NULL;
    char *text = malloc((size_t)length + 1);
    if (text) vsnprintf(text, (size_t)length + 1, format, args);
    return text;
}

void nfs_log_vmessage_tagged(const char *category, int level, const char *file,
        const char *function, long line, const char *tag, const char *format, va_list args)
{
    if (emergency) {
        /* No zlog locks or heap allocation on the fatal signal path. Existing
         * crash formatting remains best effort, with a bounded stack buffer. */
        char text[4096];
        int length = vsnprintf(text, sizeof(text), format, args);
        if (length > 0) {
            size_t count = (size_t)length < sizeof(text) ? (size_t)length : sizeof(text)-1;
            ssize_t result = write(STDERR_FILENO, text, count);
            (void)result;
            if (crash_fd >= 0) { result = write(crash_fd, text, count); (void)result; }
        }
        return;
    }
    if (!nfs_log_enabled(category, level)) return;
    android_logs_flush();
    char local[4096];
    char *text = format_message(local, sizeof(local), format, args);
    if (text) {
        emit(category, level, file, function, line, tag, text, strlen(text));
        if (text != local) free(text);
    }
}

void nfs_log_vmessage(const char *category, int level, const char *file,
        const char *function, long line, const char *format, va_list args)
{
    nfs_log_vmessage_tagged(category, level, file, function, line, NULL, format, args);
}

int nfs_log_message(const char *category, int level, const char *file,
        const char *function, long line, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    nfs_log_vmessage(category, level, file, function, line, format, args);
    va_end(args);
    return 0;
}

/* Old dump loops supply category/level explicitly. No message-text guessing. */
static _Thread_local struct {
    char text[16384];
    size_t length;
    const char *category, *file, *function;
    int level;
    long line;
} partial;

void android_logs_flush(void)
{
    if (partial.length) {
        emit(partial.category, partial.level, partial.file, partial.function,
                partial.line, NULL, partial.text, partial.length);
        partial.length = 0;
    }
}

int nfs_log_stream(const char *category, int level, const char *file,
        const char *function, long line, const char *format, ...)
{
    if (emergency) {
        va_list args;
        va_start(args, format);
        nfs_log_vmessage(category, level, file, function, line, format, args);
        va_end(args);
        return 0;
    }
    if (!nfs_log_enabled(category, level)) return 0;
    if (partial.length && strcmp(partial.category, category)) android_logs_flush();
    char local[4096];
    va_list args;
    va_start(args, format);
    char *text = format_message(local, sizeof(local), format, args);
    va_end(args);
    if (!text) return 0;
    size_t length = strlen(text);
    for (size_t i = 0; i < length; i++) {
        if (!partial.length) {
            partial.category = category; partial.level = level; partial.file = file;
            partial.function = function; partial.line = line;
        }
        if (level > partial.level) partial.level = level;
        partial.text[partial.length++] = text[i];
        if (text[i] == '\n' || partial.length == sizeof(partial.text)) android_logs_flush();
    }
    if (text != local) free(text);
    return (int)length;
}
