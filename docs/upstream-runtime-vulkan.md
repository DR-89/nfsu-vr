# Selected Switch runtime and Vulkan changes

Adapted from Anton Artemov's commit
[99ead072](https://github.com/cherepavel/nfsu1-android/commit/99ead072ecf9f18f93835191a1c0667ab1552b93).

The Android port takes four independent changes:

- POSIX `PulseEvent` wakes existing waiters without leaving a signal for later waiters.
- File disposition and `FILE_DELETE_ON_CLOSE` defer deletion until the handle object's final close; empty save directories use `rmdir`.
- Scalar SSE conversions use x86 rounding and integer-indefinite semantics on ARM64. Double register operands read the double lane. Gradle tracks the runtime template as a generation input.
- Vulkan reuses unchanged vertex constants for draws that cannot begin an instanced run. Cache validity includes the ring generation, invalidated at frame start and every ring reset. `RECOMP_VK_CONST_REUSE=0` disables reuse; `RECOMP_DRAW_STATS=1` reports reused sets.

Deletion follows the existing handle-object lifetime model; separately opened handles to the same path are not tracked as a single NT file object.

The threaded renderer, texture pool, early submissions, Switch scheduling, FPS overrides and HUD changes are outside this change.

## Host regression checks

From the repository root:

```sh
cc -O1 -ffunction-sections -fdata-sections -Ixboxrecomp/src tests/platform_events_files.c -Wl,--gc-sections -pthread -lm -o /tmp/platform_events_files
/tmp/platform_events_files
cd xboxrecomp
../android/app/build/recomp-python/bin/python -m unittest tools.recomp.test_scalar_sse_conversion tools.recomp.test_lifter_sse tools.recomp.test_runtime_helpers_defined
```

`tests/scalar_sse_conversion.c` also runs the real runtime header directly on ARM64 when compiled with the NDK.

The conversion test compares the portable branch against actual x86 SSE instructions, including invalid inputs, boundary values and rounding modes. Host tests and APK compilation do not prove rendering, profile deletion inside the game, or performance on Android hardware.

## Validation on 2026-10-15

- Final `./gradlew generateRecomp --rerun assembleDebug`: `BUILD SUCCESSFUL`.
- 20 focused Python tests passed; host timer-race regression passed.
- Event/delete-disposition and scalar SSE C tests passed on host and Retroid Pocket 4 Pro (ARM64 Android).
- APK installed on Retroid. Intro and a race rendered; Home/return retained the same game process and restored rendering after Android Surface recreation.
- Existing profiles were backed up and were not deleted during the check. Profile deletion through the game's UI, a complete race and comparative frame-time/FPS measurements remain untested.
