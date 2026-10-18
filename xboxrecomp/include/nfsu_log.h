#ifndef NFSU_LOG_H
#define NFSU_LOG_H
#include <stdarg.h>
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif

enum {
    NFS_LOG_LEVEL_TRACE = 10, NFS_LOG_LEVEL_DEBUG = 20,
    NFS_LOG_LEVEL_INFO = 40, NFS_LOG_LEVEL_NOTICE = 60,
    NFS_LOG_LEVEL_WARN = 80, NFS_LOG_LEVEL_ERROR = 100, NFS_LOG_LEVEL_FATAL = 120
};
#ifdef NFSU_USE_ZLOG
int nfs_log_enabled(const char *category, int level);
void nfs_log_emergency_mode(void);
int nfs_log_message(const char *category, int level, const char *file,
        const char *function, long line, const char *format, ...)
        __attribute__((format(printf, 6, 7)));
int nfs_log_stream(const char *category, int level, const char *file,
        const char *function, long line, const char *format, ...)
        __attribute__((format(printf, 6, 7)));
void nfs_log_vmessage(const char *category, int level, const char *file,
        const char *function, long line, const char *format, va_list args);
void nfs_log_vmessage_tagged(const char *category, int level, const char *file,
        const char *function, long line, const char *tag, const char *format, va_list args);
#define NFS_LOG_AT(category, level, ...) \
    (nfs_log_enabled(category, level) ? \
        nfs_log_message(category, level, __FILE__, __func__, __LINE__, __VA_ARGS__) : 0)
#define NFS_LOG_STREAM(category, level, ...) \
    (nfs_log_enabled(category, level) ? \
        nfs_log_stream(category, level, __FILE__, __func__, __LINE__, __VA_ARGS__) : 0)
#else
static inline void nfs_log_emergency_mode(void) {}
/* Standalone runtime tools and other platforms retain their stdio sink. */
#define NFS_LOG_AT(category, level, ...) fprintf(stderr, __VA_ARGS__)
#define NFS_LOG_STREAM(category, level, ...) fprintf(stderr, __VA_ARGS__)
#endif
#define NFS_LOG_TRACE(category, ...) NFS_LOG_AT(category, NFS_LOG_LEVEL_TRACE, __VA_ARGS__)
#define NFS_LOG_DEBUG(category, ...) NFS_LOG_AT(category, NFS_LOG_LEVEL_DEBUG, __VA_ARGS__)
#define NFS_LOG_INFO(category, ...) NFS_LOG_AT(category, NFS_LOG_LEVEL_INFO, __VA_ARGS__)
#define NFS_LOG_NOTICE(category, ...) NFS_LOG_AT(category, NFS_LOG_LEVEL_NOTICE, __VA_ARGS__)
#define NFS_LOG_WARN(category, ...) NFS_LOG_AT(category, NFS_LOG_LEVEL_WARN, __VA_ARGS__)
#define NFS_LOG_ERROR(category, ...) NFS_LOG_AT(category, NFS_LOG_LEVEL_ERROR, __VA_ARGS__)
#define NFS_LOG_FATAL(category, ...) NFS_LOG_AT(category, NFS_LOG_LEVEL_FATAL, __VA_ARGS__)
#ifdef __cplusplus
}
#endif
#endif
