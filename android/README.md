# Android ARM64 port

This is the Android host for the Xbox NFS Underground static recompilation.
The target device for bring-up is Retroid Pocket 4 Pro (Android 13, Mali-G77).

## Build

Use JDK 21 or newer, Python 3 with `venv`, and Android SDK platform 37,
NDK 30.0.16248370, and CMake 4.1.2. Gradle 9.8.0 and AGP 9.4.1 are pinned.
Initialize the SDL and glslang submodules once:

```sh
git submodule update --init --recursive
```

Set `sdk.dir` and `nfsu.xbe` in `local.properties` at the repository root:

```properties
sdk.dir=/path/to/Android/Sdk
nfsu.xbe=/path/to/Need For Speed Underground/default.xbe
```

Build from the repository root, or open the root directory in Android Studio:

```sh
./gradlew assembleDebug
```

Gradle installs Python dependencies in its own virtual environment, runs the
recompilation pipeline, and builds the APK. The default XBE location is
`game/Need For Speed Underground/default.xbe` relative to the repository root.
Override it with `-Pnfsu.xbe=/path/to/default.xbe`; use
`-Pnfsu.python=/path/to/python3` for a different Python interpreter.
Generation tracks its inputs and writes under `android/app/build/`, so `clean` removes
all generated Android sources. No game data is packaged in the APK.

Output: `android/app/build/outputs/apk/debug/app-debug.apk`.

## Experimental 60 FPS

In the in-game **Options -> Camera** menu, set **Frame Rate** to **60 FPS**.
The default is **30 FPS**; the choice is saved in `nfsu1x_options.txt`
alongside the other port options and applies at the next race frame.
This changes NFSU1's D3D presentation interval from two vblanks to one,
while keeping the kernel clock and simulation timing unchanged. Actual frame
rate depends on the device and rendering settings. Race speed, timers,
animations and audio at 60 FPS still require device validation. If GPU flips
stop for 10 seconds after startup, the diagnostic watchdog writes up to two
guest-state snapshots into `nfsu1.log` without terminating the game. Loading
screens and background pauses can also trigger these snapshots.

## Play

1. Install the APK on an ARM64 Android device (Android 11 or newer).
2. Keep the extracted disc in an ordinary folder on internal storage, an SD
   card or a USB drive. The folder must contain `default.xbe`, `CARS`,
   `FRONTEND`, `GLOBAL`, `LANGUAGES`, `MOVIES`, `SOUND` and `TRACKS`.
3. Tap **Выбрать папку игры**, choose this folder and grant read access.
4. The game starts automatically after the folder passes validation. Later app
   launches validate the saved folder and open the game directly. If the folder
   is missing or unreadable, the folder-selection menu appears.
   SDL reads the built-in or connected controller.

The app does not import/copy the selected game folder. SAF resolves document
IDs and opens seekable read-only descriptors; regular native read/seek then
works directly on the original files. Case-insensitive metadata lookup matches
Xbox paths. Providers that return non-seekable streams are unsupported. Keep
the selected drive connected while playing. Saves, settings and shader caches
live under the app's external files directory, separately from the disc data.

## Graphics

The Android Vulkan renderer targets **Vulkan 1.1**, using ordinary render
passes/framebuffers, per-frame descriptor pools and pipelines containing vertex,
rasterizer, depth and stencil state. It does not require dynamic rendering,
push descriptors or extended dynamic state. The existing desktop/Switch Vulkan
1.3 path is retained. Cube maps, mipmaps and FXAA use the same shader and texture
implementation. Mobile BC/DXT support is optional; the existing CPU decode path
is used when the driver lacks it.

The Android baseline is commit `9d94d5c`, with 3D/HUD aspect corrections
retained. 3D rendering keeps its widescreen correction. Only the racing HUD
keeps proportional text, icons and gauges, with objects and the minimap moved
towards the screen edges. All menus retain the original 4:3 layout stretched
across the widescreen output, including text, buttons and background pieces.
Menu draws in racing state (pause, rewards and results) undo the HUD projection
for every quad equally. Loading screens retain their original projection.
`RECOMP_HORPLUS=0` disables the corrections. The guest draw checks in
`tests/ui_aspect.c` cover coordinates, source restoration, menu stretching,
HUD scope restoration and projection selection. They do not establish visual
correctness or race stability on a device. The revised menu behavior still
needs device validation.

The Android host owns the SDL window and services events on SDL's main thread,
independently of guest flips. GameActivity is the sole owner of fullscreen policy, using WindowInsetsController
and a decor layout that does not shrink for system bars. SDL does not request
fullscreen window style.
Java surface callbacks publish retained native-window references, a window
lifetime generation and its dimensions. The Vulkan renderer consumes snapshots;
window replacement recreates the surface and swapchain, while size changes only
rebuild the swapchain. It waits when no active window is available. Presentation
semaphores belong to swapchain images. GameActivity is a singleTask because the
SDL/native runtime is process-global; launcher re-entry returns to that instance.
The acquisition timeout only bounds a pause racing a window snapshot. Screen
lock/unlock, launcher re-entry and app switching still require device validation.

## Diagnostics and validation

The persistent native log is `nfsu1.log` in
`/sdcard/Android/data/org.nfsu.android/files/`. For example:

```sh
adb logcat -s NFSU SDL SDL/APP AndroidRuntime
adb pull /sdcard/Android/data/org.nfsu.android/files/nfsu1.log
```

The game has a separate process, so its native shutdown does not kill the
launcher. A successful APK build or installation does not establish a working
race, correct rendering, sound, saves or performance. Device validation is
required, including selecting a folder on removable storage and reopening the
app with its persisted permission. Touchscreen controls are not implemented.

## Verified on 2026-10-14

The APK was built, signed, installed and run on the user's Retroid Pocket 4 Pro.
The actual driver reports Mali-G77 MC9 / Vulkan 1.1.177. The selected game tree
was on the removable volume `FEDE-A117`, under `Games/Need For Speed Underground`.
The selection survived APK updates; XBE and game assets were read through SAF.

Verified by device logs/screenshots: movies, upright fullscreen output, main
menu, profile creation and a Quick Race with the car, track and HUD visible.
The user confirmed controller input, driving and sound. `PAVEL.nfs` was created
under the app's `save/UserData/45410047/` tree. Reloading that profile after a
fresh restart and Career mode have not been checked. FPS, prolonged stability
and 16 KB-page device operation have not been measured.

One stop on the title screen after skipping movies was reported; it did not
reproduce during the subsequent menu/race run. Its cause remains unresolved.
This first debug APK retains input diagnostics and one non-terminating guest
snapshot at 25 seconds. The message "no exit after 25s" is a snapshot label,
not a detected hang or a forced shutdown.

Checks passed: `assembleDebug`, `lintDebug`, APK signing verification and
`zipalign -c -P 16 4`. A POSIX platform-memory regression covers no-access
reservations, occupied-address refusal and carving a high window from a 4 GB
reservation:

```sh
cc -std=gnu11 -O1 -I xboxrecomp/src tests/platform_memory.c \
  xboxrecomp/src/platform/win32_compat.c -pthread -lm -o /tmp/nfsu-memory-test
/tmp/nfsu-memory-test
```

Local evidence is in the ignored `build/validation/` directory.
