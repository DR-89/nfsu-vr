<div align="center">

<img src="https://cdn2.steamgriddb.com/logo/e88a994b60847fd3f5bf29ddce531b13.png" alt="Need for Speed: Underground" width="520">

### Xbox static recompilation for Android

The original Xbox (NTSC-U) release of **Need for Speed: Underground**, lifted
instruction by instruction to C and running natively, with no emulator.

</div>

## Build for Android

The commands below are for Linux. The APK targets **ARM64**, Android 11 or
newer (API 30+).

### Requirements

- Git, Python 3 with `venv` and `pip`, and JDK 21 or newer.
- Android SDK with platform **37**, NDK **30.0.16248370**, and CMake **4.1.2**.
  Install these through Android Studio's SDK Manager or `sdkmanager`.
- The supported original Xbox **NTSC-U** `default.xbe` from your own game copy.
  Game data is not included in the repository or APK.

Gradle **9.8.0** is downloaded by the wrapper; Android Gradle Plugin **9.4.1**
is configured in the project. Run the build from a Git checkout: the launcher
displays the current commit as its build identifier.

### Prepare and build

From the repository root, initialize the pinned source dependencies once:

```sh
git submodule update --init --recursive
```

Open the repository root in Android Studio and select your JDK, or use the
command line. In `local.properties`, set your SDK and game executable
paths (this file is ignored by Git):

```properties
sdk.dir=/path/to/Android/Sdk
nfsu.xbe=/path/to/Need For Speed Underground/default.xbe
```

If `nfsu.xbe` is omitted, Gradle uses
`game/Need For Speed Underground/default.xbe` relative to the repository root.
Then build:

```sh
./gradlew assembleDebug
```

Gradle creates its own Python virtual environment, installs the pinned generator
dependencies, generates C sources from the XBE, and builds the native libraries
and APK. Python 3 with `venv` must be installed; no manual `pip` or preparation
script is needed. The first build needs network access for dependencies.
Generated sources and analysis files live under `android/app/build/` and are
regenerated when the XBE or generator inputs change. `clean` removes them.
Use `-Pnfsu.xbe=/path/to/default.xbe` to override the executable for one build,
and `-Pnfsu.python=/path/to/python3` to choose a Python interpreter.

The APK is written to `android/app/build/outputs/apk/debug/app-debug.apk`.
To install it on a connected device with USB debugging enabled:

```sh
adb install -r android/app/build/outputs/apk/debug/app-debug.apk
```

On first launch, select the extracted Xbox game folder containing `default.xbe`,
`CARS`, `TRACKS`, and the remaining disc files. Keep that folder available while
playing; it is read directly rather than copied into the app.

See [the Android README](android/README.md) for controls, graphics, and runtime
details.
