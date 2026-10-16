# Vulkan image memory pool

Adapted from Anton Artemov's [99ead072](https://github.com/cherepavel/nfsu1-android/commit/99ead072ecf9f18f93835191a1c0667ab1552b93). This change includes only image memory pooling; it does not introduce the threaded renderer or early submissions.

Optimal-tiled textures, color targets and depth targets up to 16 MiB share 32 MiB device-local blocks of the same memory type. Suballocations follow `VkMemoryRequirements.alignment`. Buffers and linear images never share these blocks, so they do not introduce buffer/image-granularity conflicts.

Android adaptations:

- At most eight blocks (256 MiB reserved in total), and at most one eighth of the associated heap. Standalone image allocations remain available beyond that limit.
- Image requirements are queried with Vulkan 1.1 `vkGetImageMemoryRequirements2`. Images requiring or preferring dedicated memory bypass the pool and receive `VkMemoryDedicatedAllocateInfo`.
- A pooled image carries a null standalone memory handle. Its range returns to the pool only after the frame fence and image destruction in `garbage_free`. Failure to create an image view releases the range immediately because the image has not been submitted.
- Empty blocks return their memory to the driver. Allocation of host metadata is checked before changing free ranges. Failed pool allocation/binding falls back to a standalone allocation.
- `[texpool]` diagnostics every 300 frames report cumulative pooled bindings, block allocations, standalone images, current reserved bytes and current live bytes.

These limits bound pool reservation, not total renderer memory. A fragmented pool can retain unused ranges while standalone allocations continue.

## Disable and compare

`RECOMP_VK_TEX_POOL=0` disables pooling on desktop; Android can use a debug property without rebuilding:

```sh
adb shell setprop debug.nfsu.vk_tex_pool 0
adb shell am force-stop org.nfsu.android
adb shell am start -n org.nfsu.android/.LauncherActivity
```

Set the property to `1` to enable. The setting is read once per game process, with the environment variable taking precedence. Clear the property to restore the enabled default:

```sh
adb shell "setprop debug.nfsu.vk_tex_pool ''"
```

## Allocator regression

```sh
cc -O1 -g -Ithird_party/SDL/src/video/khronos tests/vulkan_image_pool.c -o /tmp/vulkan_image_pool
/tmp/vulkan_image_pool
```

The test includes the actual allocator with a mock Vulkan driver. It exercises alignment gaps, range merging, metadata/device-allocation/binding failure, the block and heap limits, disable and large-image fallback, full hash-table collision chains and fragmented reuse. The test checks that all allocated blocks and metadata are released. It also runs on Retroid ARM64 with Clang's undefined-behavior checks in trap mode.

## Retroid validation, 2026-10-15

`./gradlew assembleDebug` completed with `BUILD SUCCESSFUL`. The allocator test passed on the host and on Retroid Pocket 4 Pro ARM64 with `-fsanitize=undefined -fsanitize-trap=all`. Host AddressSanitizer was unavailable in the installed toolchain; it was not run.

The installed APK was tested with the pool both off and on using the debug property. Settings were retained: experimental 60 FPS, scale 200%, reflections on, FXAA on, anisotropy 16. Intro, loading and racing rendered. Home/return during a race retained process 20564 and restored full-screen rendering after Surface recreation. The temporary property override was cleared afterward; the pool remains enabled by default.

Allocation counters during the enabled run reached 887 pooled images backed by eight block allocations, plus 65 standalone images after reaching the 256 MiB pool cap. Live pool bytes were 255.8 MiB at that point; subsequent snapshots fell to 216.0 MiB as images were reclaimed. Cumulative bindings are not a count of simultaneously live images. The allocator regression separately verifies that completely empty blocks are freed.

Two approximately 25-second SurfaceFlinger samples produced:

| Metric | Pool off | Pool on |
| --- | ---: | ---: |
| Presented frames sampled | 1205 | 1250 |
| Mean presentation interval | 22.72 ms | 21.73 ms |
| Median interval | 16.66 ms | 16.66 ms |
| p95 interval | 33.33 ms | 33.33 ms |
| p99 interval | 49.98 ms | 33.34 ms |
| Maximum interval | 66.64 ms | 66.65 ms |
| Intervals above 50 ms | 1 | 2 |
| Process PSS at sample start/end | 526.6 / 556.0 MiB | 639.0 / 592.3 MiB |

Samples deduplicated actual-presentation timestamps from the SurfaceFlinger latency ring, which also includes frames just before sampling began. **These are observations, not an established performance improvement:** the race segments and live resource sets differed, runs were sequential, and thermal state was not controlled. The PSS difference cannot be attributed solely to pooling. Presentation intervals also do not isolate CPU/GPU render time.

A comparable startup burst uploading 69 textures (31.0 MiB) took 160 ms without the pool and 155 ms with it. That single measurement shows no substantial established reduction in upload stalls; read/decode/upload work remains. More controlled repeated captures are needed before claiming faster loading or higher FPS. The confirmed result is fewer driver memory allocations, a bounded pool, successful fallback at its limit, and working rendering/lifecycle on this device.

A subsequent race loaded in the same process: counters reached 1536 pooled bindings with the same eight block allocations and 192 standalone images, with 236.3 MiB live in the pool. This exercises reuse across a race reload rather than only a fresh application launch. Long-session stability, all tracks/devices and a controlled repeated performance benchmark remain outside this validation.
