# Android ARM64 port

This is the Android host for the Xbox NFS Underground static recompilation.
The target device for bring-up is Retroid Pocket 4 Pro (Android 13, Mali-G77).

## Build

Dependencies are pinned Git submodules: SDL 2.32.10 and glslang 15.1.0.
`xboxrecomp` remains vendored because it includes the game's maintained fixes.

```sh
git submodule update --init --recursive
python3 -m venv .venv
.venv/bin/python -m pip install -r tools/requirements-recomp.txt
JAVA_HOME=/path/to/jdk-21 ANDROID_HOME=/path/to/Android/Sdk tools/build_android.sh
```

Install SDK platform 37, NDK 30.0.16248370 and CMake 4.1.2 through the SDK manager.
The wrapper pins Gradle 9.8.0; AGP is 9.4.1. Generated C is excluded from Git.
The generator expects the supported Xbox `default.xbe` under
`game/Need For Speed Underground/`; set `NFSU1_XBE` for another location.
Use `REGENERATE=1` after changing generator inputs. No game data is packaged
in the APK.

Output: `android/app/build/outputs/apk/debug/app-debug.apk`.

## Play

1. Install the APK on an ARM64 Android device (Android 11 or newer).
2. Keep the extracted disc in an ordinary folder on internal storage, an SD
   card or a USB drive. The folder must contain `default.xbe`, `CARS`,
   `FRONTEND`, `GLOBAL`, `LANGUAGES`, `MOVIES`, `SOUND` and `TRACKS`.
3. Tap **Выбрать папку игры**, choose this folder and grant read access.
4. Tap **Играть**. SDL reads the built-in or connected controller.

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
