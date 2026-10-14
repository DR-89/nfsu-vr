/* Android paths and persistent diagnostics. SDL owns the Java/native entry. */
#include <SDL.h>
#include <android/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

int android_host_boot(int argc, char **argv)
{
    char save[1024], log_path[1024], cache[1024];
    if (argc < 3) {
        __android_log_print(ANDROID_LOG_ERROR, "NFSU", "Missing game and runtime paths");
        return 0;
    }
    if (chdir(argv[2]) != 0)
        return 0;
    snprintf(save, sizeof(save), "%s/save", argv[2]);
    snprintf(log_path, sizeof(log_path), "%s/nfsu1.log", argv[2]);
    snprintf(cache, sizeof(cache), "%s/progcache.bin", argv[2]);
    mkdir(save, 0755);
    /* Also repair directories created by earlier builds with owner-only access. */
    if (chmod(save, 0755) != 0)
        __android_log_print(ANDROID_LOG_WARN, "NFSU", "Cannot update save directory permissions: %s", save);
    if (!freopen(log_path, "w", stdout) || dup2(fileno(stdout), fileno(stderr)) < 0)
        return 0;
    setenv("NFSU2_GAME_DIR", argv[1], 1);
    setenv("NFSU2_SAVE_DIR", save, 1);
    setenv("RECOMP_PROG_CACHE", cache, 1);
    setenv("RECOMP_GIL_EAGER", "1", 0);
    setenv("RECOMP_CULL", "0", 0);
    setenv("RECOMP_VK_INSTANCE", "0", 0);
    setenv("RECOMP_INPUT_DIAG", "1", 0);
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
