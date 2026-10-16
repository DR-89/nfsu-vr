/* Android paths and persistent diagnostics. SDL owns the Java/native entry. */
#include <SDL.h>
#include <SDL_vulkan.h>
#include <android/native_window_jni.h>
#include <pthread.h>
#include "../xboxrecomp/src/platform/android_host.h"
#include <android/log.h>
#include <sys/system_properties.h>
#include "android_diagnostics.h"
#include "../xboxrecomp/include/nfsu_perf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "menu_touch.h"
#include "race_touch.h"
#include "android_input.h"

int android_host_boot(int argc, char **argv)
{
    char save[1024], log_path[1024], previous_log[1024], cache[1024];
    if (argc < 3) {
        __android_log_print(ANDROID_LOG_ERROR, "NFSU", "Missing game and runtime paths");
        return 0;
    }
    if (chdir(argv[2]) != 0)
        return 0;
    snprintf(save, sizeof(save), "%s/save", argv[2]);
    snprintf(log_path, sizeof(log_path), "%s/nfsu1.log", argv[2]);
    snprintf(previous_log, sizeof(previous_log), "%s/nfsu1.previous.log", argv[2]);
    snprintf(cache, sizeof(cache), "%s/progcache.bin", argv[2]);
    mkdir(save, 0755);
    /* Also repair directories created by earlier builds with owner-only access. */
    if (chmod(save, 0755) != 0)
        __android_log_print(ANDROID_LOG_WARN, "NFSU", "Cannot update save directory permissions: %s", save);
    if (access(log_path, F_OK) == 0 && rename(log_path, previous_log) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, "NFSU", "Cannot preserve previous log");
        return 0;
    }
    if (!freopen(log_path, "w", stdout) || dup2(fileno(stdout), fileno(stderr)) < 0)
        return 0;
    setenv("RECOMP_QUIET", "1", 0);
    /* One APK supports both sides of the diagnostic-overhead comparison. */
    char diagnostics[PROP_VALUE_MAX] = {0};
    __system_property_get("debug.nfsu.diagnostics", diagnostics);
    nfsu_diagnostics_enabled = strcmp(diagnostics, "1") == 0;
    fprintf(stderr, "[ANDROID] detailed diagnostics=%d\n", nfsu_diagnostics_enabled);
    char perf_mode[PROP_VALUE_MAX] = {0};
    __system_property_get("debug.nfsu.perf", perf_mode);
    nfsu_perf_enabled = strcmp(perf_mode, "0") != 0;
    fprintf(stderr, "[ANDROID] performance counters=%d\n", nfsu_perf_enabled);
    setenv("NFSU2_GAME_DIR", argv[1], 1);
    setenv("NFSU2_SAVE_DIR", save, 1);
    setenv("RECOMP_PROG_CACHE", cache, 1);
    /* Read once before guest threads start; the runtime caches this mode. */
    char gil_eager[PROP_VALUE_MAX] = {0};
    __system_property_get("debug.nfsu.gil_eager", gil_eager);
    if (*gil_eager && strcmp(gil_eager, "0") != 0 && strcmp(gil_eager, "1") != 0)
        fprintf(stderr, "[ANDROID] invalid debug.nfsu.gil_eager=%s; using 1\n", gil_eager);
    const char *gil_mode = strcmp(gil_eager, "0") == 0 ? "0" : "1";
    setenv("RECOMP_GIL_EAGER", gil_mode, 1);
    fprintf(stderr, "[ANDROID] GIL eager=%s (debug.nfsu.gil_eager)\n", gil_mode);
    char gil_quantum[PROP_VALUE_MAX] = {0};
    __system_property_get("debug.nfsu.gil_quantum_us", gil_quantum);
    setenv("RECOMP_GIL_MAIN_QUANTUM_US", *gil_quantum ? gil_quantum : "0", 1);
    char scheduler[PROP_VALUE_MAX] = {0};
    __system_property_get("debug.nfsu.scheduler", scheduler);
    setenv("RECOMP_GUEST_SCHEDULER", strcmp(scheduler, "0") == 0 ? "0" : "1", 1);
    fprintf(stderr, "[ANDROID] guest scheduler=%s\n", getenv("RECOMP_GUEST_SCHEDULER"));
    setenv("RECOMP_CULL", "0", 0);
    setenv("RECOMP_VK_INSTANCE", "0", 0);
    if (nfsu_diagnostics_enabled) setenv("RECOMP_INPUT_DIAG", "1", 1);
    else unsetenv("RECOMP_INPUT_DIAG");
    /* Retail XPP 1EF01B maps (root_port - 1) XOR 2 to Xbox player slot.
     * HC0 port index 2 is player one; HC1 is not started by this title. */
    setenv("RECOMP_USB_HC", "0", 1);
    setenv("RECOMP_USB_NDP", "4", 1);
    setenv("RECOMP_USB_PORT", "2", 1);
    setenv("RECOMP_USB_PORT2", "3", 1);
    setenv("RECOMP_WATCHDOG_SECS", "25", 0);
    setenv("RECOMP_WATCHDOG_KEEP", "1", 0);
    setenv("RECOMP_STALL_DUMP_SECS", "10", 0);
    setenv("RECOMP_PEEK", "0x283434,0x1DE94C,0x1E11C0,[0x1DE9E8]+0x1BC,[0x1DE9E8]+0x1C0,[0x1DE9E8]+0x1C4,[0x1DE9E8]+0x174,[0x1DE9E8]+0x178,[0x1DE9E8]+0x180,[0x1DE9E8]+0x184,[0x1DE9E8]+0x2C,[0x1DE9E8]+0x30", 0);
    setenv("RECOMP_GL_SCALE", "1", 0);
    setenv("RECOMP_VK_ANISO", "4", 0);
    SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");
    __android_log_print(ANDROID_LOG_INFO, "NFSU", "Booting from %s; log %s", argv[1], log_path);
    return 1;
}

/* SDL's main thread owns video/events. Java publishes native window lifetime;
 * the GPU thread consumes retained snapshots, never SDL's mutable driverdata. */
static pthread_mutex_t host_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t host_changed = PTHREAD_COND_INITIALIZER;
static AndroidHostWindow host_window;
static SDL_Window *host_sdl_window;
static int host_active = 1;
static int host_finished;

JNIEXPORT void JNICALL Java_org_nfsu_android_GameActivity_nativeSurfaceChanged(
        JNIEnv *env, jclass cls, jobject surface, jint width, jint height)
{
    (void)cls;
    ANativeWindow *window = surface ? ANativeWindow_fromSurface(env, surface) : NULL;
    pthread_mutex_lock(&host_mutex);
    ANativeWindow *old = host_window.window;
    host_window.window = window;
    host_window.width = width;
    host_window.height = height;
    menu_touch_cancel();
    race_touch_cancel();
    if (old != window) host_window.generation++;
    fprintf(stderr, "[ANDROID] window generation=%llu size=%dx%d available=%d\n",
            (unsigned long long)host_window.generation, width, height, window != NULL);
    pthread_cond_broadcast(&host_changed);
    pthread_mutex_unlock(&host_mutex);
    if (old) ANativeWindow_release(old);
}

SDL_Window *android_host_window(void) { return host_sdl_window; }

int android_host_acquire_window(AndroidHostWindow *out, int wait)
{
    pthread_mutex_lock(&host_mutex);
    while (wait && !host_finished
           && (!host_active || !host_window.window || host_window.width <= 0 || host_window.height <= 0))
        pthread_cond_wait(&host_changed, &host_mutex);
    int available = !host_finished && host_active && host_window.window
        && host_window.width > 0 && host_window.height > 0;
    memset(out, 0, sizeof(*out));
    if (available) {
        *out = host_window;
        ANativeWindow_acquire(out->window);
    }
    pthread_mutex_unlock(&host_mutex);
    return available;
}

void android_host_release_window(AndroidHostWindow *window)
{
    if (window->window) ANativeWindow_release(window->window);
    memset(window, 0, sizeof(*window));
}

int android_host_video_init(void)
{
    SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE, "0");
    SDL_SetHint(SDL_HINT_VIDEO_EXTERNAL_CONTEXT, "1");
    // Resizable SDL windows otherwise override the manifest with FULL_USER.
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) return 0;
    host_sdl_window = SDL_CreateWindow("NFS Underground", SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED, 1280, 720,
            SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    return host_sdl_window != NULL;
}

void android_host_game_finished(void)
{
    pthread_mutex_lock(&host_mutex);
    host_finished = 1;
    pthread_cond_broadcast(&host_changed);
    pthread_mutex_unlock(&host_mutex);
}

int android_host_run(void)
{
    extern void nfsu_audio_diag_poll(void);
    extern void nfsu_diag_poll(void);
    SDL_FingerID touch_finger = 0;
    SDL_TouchID touch_device = 0;
    int touching = 0;
    for (;;) {
        SDL_Event e;
        nfsu_audio_diag_poll();
        nfsu_diag_poll();
        android_race_controls_poll();
        pthread_mutex_lock(&host_mutex);
        int finished = host_finished;
        pthread_mutex_unlock(&host_mutex);
        if (finished) return 0;
        if (!SDL_WaitEventTimeout(&e, 50)) continue;
        if (e.type == SDL_QUIT) _exit(0);
        if (e.type == SDL_FINGERDOWN || e.type == SDL_FINGERMOTION || e.type == SDL_FINGERUP) {
            if (e.type == SDL_FINGERDOWN && !touching) {
                touching = 1;
                touch_finger = e.tfinger.fingerId;
                touch_device = e.tfinger.touchId;
            } else if (e.type == SDL_FINGERDOWN) continue;
            if (!touching || e.tfinger.fingerId != touch_finger ||
                e.tfinger.touchId != touch_device) continue;
            int width, height;
            SDL_GetWindowSize(host_sdl_window, &width, &height);
            enum MenuTouchEvent event = e.type == SDL_FINGERDOWN ? MENU_TOUCH_DOWN :
                e.type == SDL_FINGERUP ? MENU_TOUCH_UP : MENU_TOUCH_MOVE;
            menu_touch_event(event, e.tfinger.x, e.tfinger.y, width, height);
            if (e.type == SDL_FINGERUP) touching = 0;
        }
        if (e.type == SDL_APP_WILLENTERBACKGROUND ||
            (e.type == SDL_WINDOWEVENT && (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST ||
                                         e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED))) {
            touching = 0;
            menu_touch_cancel();
            race_touch_cancel();
        }
        if (e.type == SDL_APP_DIDENTERBACKGROUND || e.type == SDL_APP_DIDENTERFOREGROUND) {
            int active = e.type == SDL_APP_DIDENTERFOREGROUND;
            pthread_mutex_lock(&host_mutex);
            host_active = active;
            touching = 0;
            menu_touch_cancel();
            race_touch_cancel();
            fprintf(stderr, "[ANDROID] foreground=%d\n", active);
            pthread_cond_broadcast(&host_changed);
            pthread_mutex_unlock(&host_mutex);
        }
    }
}
