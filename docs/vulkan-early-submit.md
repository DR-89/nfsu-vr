# Vulkan early submissions

Adapted from Anton Artemov's [99ead072](https://github.com/cherepavel/nfsu1-android/commit/99ead072ecf9f18f93835191a1c0667ab1552b93), independently of threaded recording.

Each frame owns eight command buffers. After N non-instanced batches, recording can submit at a natural render-pass boundary; a continuing pass tolerates 2N batches. At most seven early submissions use a fresh command buffer each, leaving the last buffer for remaining drawing and presentation. All submits use the same queue. The final submit carries the frame fence; early submits do not acquire/present swapchain images or signal presentation semaphores.

Android's submission point differs from upstream: it is before the current draw's ring reservation and vertex upload/binding. Android binds its vertex buffers before `begin_rendering`; switching command buffers there would lose the binding. Pipeline/vertex caches use the command-buffer generation, dynamic state is invalidated, and the next pass sets viewport/scissor again.

Ring memory, descriptor pools, framebuffers, pooled image ranges and garbage are not reset or reclaimed on an early submit. Normal frame reuse waits on the final fence. Ring overflow submits the current buffer with that fence, waits, then resets resources and restarts at command buffer zero. Render-target pointers are cleared on both kinds of split. Barriers on the same queue establish access dependencies across command buffers; command-buffer submission alone does not replace image/buffer barriers.

Configuration is read once per game process:

- `RECOMP_VK_KICK=N`: threshold in batches, 0 disables early submissions.
- Android: `debug.nfsu.vk_kick=N` when the environment variable is absent.
- `[vkkick]` every 300 frames reports the selected threshold and cumulative early submissions.

For example:

```sh
adb shell setprop debug.nfsu.vk_kick 300
adb shell am force-stop org.nfsu.android
adb shell am start -n org.nfsu.android/.LauncherActivity
```

Clear the temporary property override after testing:

```sh
adb shell "setprop debug.nfsu.vk_kick ''"
```

## Regression

```sh
cc -O1 -Ithird_party/SDL/src/video/khronos tests/vulkan_early_submit.c -o /tmp/vulkan_early_submit
/tmp/vulkan_early_submit
```

The regression includes the production submission helpers with a mock Vulkan driver. It verifies thresholds, disabled/invalid configuration, fresh command buffers, the seven-submit cap, cache invalidation, resource retention, and waiting before resetting resources when the ring overflows. It also runs on Retroid ARM64 with undefined-behavior checks in trap mode. These mock-driver tests do not prove GPU execution or visual correctness; the APK is separately checked on the actual Mali driver.

The fence and barrier scopes were checked against the [Vulkan synchronization specification](https://docs.vulkan.org/spec/latest/chapters/synchronization.html): a queue-submit fence covers earlier commands in submission order, and an outside-render-pass pipeline barrier can include commands from earlier command buffers on that queue.

## Retroid validation, 2026-10-15

The default threshold remains 300 batches; zero disables the optimization. The final root `./gradlew assembleDebug` completed with `BUILD SUCCESSFUL`. The submission/ring-flush regression passed on host and Retroid ARM64 with `-fsanitize=undefined -fsanitize-trap=all` (mock Vulkan driver).

Actual Mali-G77 testing used the same APK and retained settings: experimental 60 FPS, render scale 200%, reflections and FXAA enabled, anisotropy 16, texture pool enabled. Intro/loading/racing rendered at thresholds 0, 300 and 600. Diagnostic counters confirmed no early submissions at 0, roughly 3–5 per racing frame at 300, and roughly 2 per racing frame at 600. Home/return at threshold 600 preserved PID 25867, recreated Surface presentation, and restored the race image. The temporary debug-property override was cleared, and the final APK was installed with default settings.

Thirty-second SurfaceFlinger samples, using deduplicated actual-presentation timestamps newer than the start of each sample:

| Metric | Off (0) | 300 | 600 |
| --- | ---: | ---: | ---: |
| Sampled frames | 1288 | 1350 | 1331 |
| Mean presented FPS | 43.35 | 45.54 | 44.80 |
| Mean interval | 23.07 ms | 21.96 ms | 22.32 ms |
| Median interval | 16.67 ms | 16.66 ms | 16.66 ms |
| p95 interval | 33.33 ms | 33.33 ms | 33.33 ms |
| p99 interval | 49.97 ms | 33.34 ms | 33.34 ms |
| Maximum interval | 49.99 ms | 66.65 ms | 49.99 ms |
| Intervals above 50 ms | 0 | 1 | 0 |
| Process PSS start/end | 593.3 / 638.9 MiB | 571.2 / 568.0 MiB | 519.9 / 527.5 MiB |

These single runs do not establish a sustained FPS improvement or memory saving. Race segments, camera/traffic state, resource sets, warm caches and thermal conditions were not identical or controlled. Measurements cover presentation pacing rather than isolated CPU/GPU execution time. The numerical differences are preliminary; the original threshold 300 is retained rather than treating this small sample as robust tuning. An initial control sample that read the previous process's stale log was discarded; the replacement sample required the new process's threshold to appear before race detection.

Long-session stress, validation-layer coverage, other GPUs and a controlled repeated benchmark remain untested. Ring overflow after early submissions was exercised by the mock regression, not forced on the actual GPU.
