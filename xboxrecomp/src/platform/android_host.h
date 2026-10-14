#ifndef NFSU_ANDROID_HOST_H
#define NFSU_ANDROID_HOST_H
#include <stdint.h>
#include <SDL.h>
#include <android/native_window.h>
/* A snapshot owns one ANativeWindow reference; release after use. */
typedef struct AndroidHostWindow {
    ANativeWindow *window;
    uint64_t generation;
    int width, height;
} AndroidHostWindow;
SDL_Window *android_host_window(void);
int android_host_acquire_window(AndroidHostWindow *out, int wait);
void android_host_release_window(AndroidHostWindow *window);
int android_host_video_init(void);
int android_host_run(void);
void android_host_game_finished(void);
#endif
