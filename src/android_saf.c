/* Read-only SAF filesystem bridge. Other paths use libc without interception.
 * Linker wrappers cover both the XBE loader and the Xbox file compatibility
 * layer; read/seek/fstat operate on provider-owned, seekable file descriptors. */
#define _GNU_SOURCE
#include <SDL.h>
#include <SDL_system.h>
#include <jni.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static jclass s_access;
static jmethodID s_open, s_stat, s_list;

JNIEXPORT void JNICALL Java_org_nfsu_android_SafAccess_bindNative(JNIEnv *env, jclass cls)
{
    s_access = (*env)->NewGlobalRef(env, cls);
    s_open = (*env)->GetStaticMethodID(env, cls, "openFd", "(Ljava/lang/String;)I");
    s_stat = (*env)->GetStaticMethodID(env, cls, "stat", "(Ljava/lang/String;)[J");
    s_list = (*env)->GetStaticMethodID(env, cls, "list", "(Ljava/lang/String;)[Ljava/lang/String;");
}

static const char *relative_path(const char *path)
{
    if (!path || strncmp(path, "/saf-game", 9) || (path[9] && path[9] != '/')) return NULL;
    return path[9] == '/' ? path + 10 : path + 9;
}

static JNIEnv *environment(void)
{
    if (!s_access) { errno = EACCES; return NULL; }
    return (JNIEnv *)SDL_AndroidGetJNIEnv();
}

static int java_failed(JNIEnv *env)
{
    if (!(*env)->ExceptionCheck(env)) return 0;
    (*env)->ExceptionClear(env);
    errno = ENOENT;
    return 1;
}

static int saf_open(const char *path, int flags)
{
    if ((flags & O_ACCMODE) != O_RDONLY || (flags & (O_CREAT | O_TRUNC))) {
        errno = EROFS; return -1;
    }
    JNIEnv *env = environment();
    if (!env) return -1;
    jstring name = (*env)->NewStringUTF(env, path);
    int fd = (*env)->CallStaticIntMethod(env, s_access, s_open, name);
    (*env)->DeleteLocalRef(env, name);
    if (java_failed(env)) return -1;
    if (fd >= 0 && lseek(fd, 0, SEEK_CUR) < 0) {
        close(fd); errno = ESPIPE; return -1;
    }
    return fd;
}

int __real_open(const char *, int, ...);
int __wrap_open(const char *path, int flags, ...)
{
    const char *relative = relative_path(path);
    if (relative) return saf_open(relative, flags);
    mode_t mode = 0;
    if ((flags & O_CREAT) || (flags & O_TMPFILE) == O_TMPFILE) {
        va_list args; va_start(args, flags); mode = va_arg(args, int); va_end(args);
    }
    return __real_open(path, flags, mode);
}
int __wrap_open64(const char *path, int flags, ...)
{
    mode_t mode = 0;
    if ((flags & O_CREAT) || (flags & O_TMPFILE) == O_TMPFILE) {
        va_list args; va_start(args, flags); mode = va_arg(args, int); va_end(args);
    }
    return __wrap_open(path, flags, mode);
}
int __wrap___open_2(const char *path, int flags) { return __wrap_open(path, flags, 0); }

FILE *__real_fopen(const char *, const char *);
FILE *__wrap_fopen(const char *path, const char *mode)
{
    const char *relative = relative_path(path);
    if (!relative) return __real_fopen(path, mode);
    if (mode[0] != 'r' || strchr(mode, '+')) { errno = EROFS; return NULL; }
    int fd = saf_open(relative, O_RDONLY);
    if (fd < 0) return NULL;
    FILE *file = fdopen(fd, mode);
    if (!file) close(fd);
    return file;
}
FILE *__wrap_fopen64(const char *path, const char *mode) { return __wrap_fopen(path, mode); }

int __real_stat(const char *, struct stat *);
int __wrap_stat(const char *path, struct stat *st)
{
    const char *relative = relative_path(path);
    if (!relative) return __real_stat(path, st);
    JNIEnv *env = environment();
    if (!env) return -1;
    jstring name = (*env)->NewStringUTF(env, relative);
    jlongArray result = (jlongArray)(*env)->CallStaticObjectMethod(env, s_access, s_stat, name);
    (*env)->DeleteLocalRef(env, name);
    if (java_failed(env) || !result) return -1;
    jlong values[2];
    (*env)->GetLongArrayRegion(env, result, 0, 2, values);
    (*env)->DeleteLocalRef(env, result);
    memset(st, 0, sizeof(*st));
    st->st_mode = (values[0] ? S_IFDIR : S_IFREG) | 0444;
    st->st_size = values[1];
    st->st_nlink = 1;
    st->st_blksize = 4096;
    st->st_blocks = (values[1] + 511) / 512;
    return 0;
}
int __wrap_stat64(const char *path, struct stat *st) { return __wrap_stat(path, st); }

typedef struct SafDirectory {
    char **names;
    size_t count, index;
    struct dirent entry;
    struct SafDirectory *next;
} SafDirectory;
static SafDirectory *s_directories;
static pthread_mutex_t s_directory_lock = PTHREAD_MUTEX_INITIALIZER;

DIR *__real_opendir(const char *);
DIR *__wrap_opendir(const char *path)
{
    const char *relative = relative_path(path);
    if (!relative) return __real_opendir(path);
    JNIEnv *env = environment();
    if (!env) return NULL;
    jstring name = (*env)->NewStringUTF(env, relative);
    jobjectArray result = (jobjectArray)(*env)->CallStaticObjectMethod(env, s_access, s_list, name);
    (*env)->DeleteLocalRef(env, name);
    if (java_failed(env) || !result) return NULL;
    SafDirectory *dir = calloc(1, sizeof(*dir));
    if (!dir) { (*env)->DeleteLocalRef(env, result); errno = ENOMEM; return NULL; }
    dir->count = (*env)->GetArrayLength(env, result);
    dir->names = calloc(dir->count ? dir->count : 1, sizeof(*dir->names));
    if (!dir->names) { free(dir); (*env)->DeleteLocalRef(env, result); errno = ENOMEM; return NULL; }
    for (size_t i = 0; i < dir->count; i++) {
        jstring item = (*env)->GetObjectArrayElement(env, result, i);
        const char *text = (*env)->GetStringUTFChars(env, item, NULL);
        dir->names[i] = strdup(text);
        (*env)->ReleaseStringUTFChars(env, item, text);
        (*env)->DeleteLocalRef(env, item);
        if (!dir->names[i]) {
            for (size_t j = 0; j < i; j++) free(dir->names[j]);
            free(dir->names); free(dir); (*env)->DeleteLocalRef(env, result); errno = ENOMEM; return NULL;
        }
    }
    (*env)->DeleteLocalRef(env, result);
    pthread_mutex_lock(&s_directory_lock);
    dir->next = s_directories;
    s_directories = dir;
    pthread_mutex_unlock(&s_directory_lock);
    return (DIR *)dir;
}

struct dirent *__real_readdir(DIR *);
struct dirent *__wrap_readdir(DIR *directory)
{
    pthread_mutex_lock(&s_directory_lock);
    SafDirectory *dir = s_directories;
    while (dir && (DIR *)dir != directory) dir = dir->next;
    if (!dir) { pthread_mutex_unlock(&s_directory_lock); return __real_readdir(directory); }
    struct dirent *entry = NULL;
    if (dir->index < dir->count) {
        memset(&dir->entry, 0, sizeof(dir->entry));
        dir->entry.d_ino = dir->index + 1;
        snprintf(dir->entry.d_name, sizeof(dir->entry.d_name), "%s", dir->names[dir->index++]);
        entry = &dir->entry;
    }
    pthread_mutex_unlock(&s_directory_lock);
    return entry;
}
struct dirent *__wrap_readdir64(DIR *directory) { return __wrap_readdir(directory); }

int __real_closedir(DIR *);
int __wrap_closedir(DIR *directory)
{
    pthread_mutex_lock(&s_directory_lock);
    SafDirectory **slot = &s_directories;
    while (*slot && (DIR *)*slot != directory) slot = &(*slot)->next;
    SafDirectory *dir = *slot;
    if (dir) *slot = dir->next;
    pthread_mutex_unlock(&s_directory_lock);
    if (!dir) return __real_closedir(directory);
    for (size_t i = 0; i < dir->count; i++) free(dir->names[i]);
    free(dir->names);
    free(dir);
    return 0;
}
