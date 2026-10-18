# Android logging with zlog

zlog is pinned as a Git submodule at `third_party/zlog` (commit
`92b4ad0f60856ae92198d080327bd2c4ed10cd7d`, upstream version 1.2.20).
Initialize dependencies with `git submodule update --init --recursive`.
The Android CMake build links the static target `zlog_s`; no system installation
or separate preparation script is needed. Other host platforms retain their
existing stdio sink through the same source macros.

## Settings

The runtime directory is normally
`/sdcard/Android/data/org.nfsu.android/files/`. If external app storage is
unavailable, the app uses its internal files directory. The selected Xbox game
folder is separate. Put `nfsu1x_env.txt` in the runtime directory and fully
restart the game after changing it. Use `KEY=VALUE`, blank lines and `#` comment
lines. Whitespace and CRLF are accepted; inline comments are not supported.
Android-owned paths, USB wiring and scheduler properties still follow the
platform boot configuration.

```ini
RECOMP_LOG_LEVEL=INFO
RECOMP_INPUT_TRACE=1
RECOMP_LOG_INPUT_LEVEL=TRACE
RECOMP_NO_LOG=0
```

Levels, from most detailed to most severe:

| Level | Purpose |
| --- | --- |
| TRACE | Individual touch events, reports and detailed traces |
| DEBUG | Internal diagnostic state and development details |
| INFO | Normal operation and performance summaries |
| NOTICE | Significant normal events such as logging startup |
| WARN | Recoverable problems or degraded operation |
| ERROR | Failed operations |
| FATAL | Unrecoverable failure; the logging call itself does not exit |
| OFF | Disable normal records for a category |

TRACE is explicitly registered as zlog level 10. DEBUG through FATAL use
zlog's built-in values. Environment settings also accept `WARNING` for `WARN`
and `CRITICAL` for `FATAL`. Settings are uppercase.

`RECOMP_LOG_LEVEL` defaults to INFO. Override a category with
`RECOMP_LOG_INPUT_LEVEL`, `RECOMP_LOG_GRAPHICS_LEVEL`, `RECOMP_LOG_AUDIO_LEVEL`,
`RECOMP_LOG_FILES_LEVEL`, `RECOMP_LOG_RUNTIME_LEVEL`,
`RECOMP_LOG_PERFORMANCE_LEVEL`, `RECOMP_LOG_KERNEL_LEVEL` or
`RECOMP_LOG_SYSTEM_LEVEL`. A threshold includes that level and all more severe
levels. Disabled messages skip formatting and argument evaluation in the new
macros.

`RECOMP_INPUT_TRACE=1` enables touch tracing and defaults the input category to
TRACE unless an explicit category threshold overrides it. To stop collecting
it, use `RECOMP_INPUT_TRACE=0` and restart. Existing source-side diagnostic
switches (`RECOMP_USB_TRACE`, `RECOMP_APU_TRACE`, `RECOMP_QUIET`, etc.) remain
in effect: the source must emit a message and its zlog threshold must allow it.
Many old flags enable by presence, including `=0`; remove those entries to
disable them. `RECOMP_NO_LOG=1` disables the old kernel logger source.
An explicit `XBOX_LOG_LEVEL` remains an additional kernel-source limit;
otherwise zlog controls kernel severity, including TRACE in release builds.

Without an explicit global threshold, `debug.nfsu.diagnostics=1` defaults
runtime to DEBUG and audio to TRACE to preserve expanded diagnostics.

On each launch, `logs/zlog.generated.conf` is generated from these settings.
For full control, copy `android/zlog.example.conf` as `zlog.conf` into the
runtime directory. This optional file replaces generated rules, including their
level thresholds. It must define TRACE if used. Invalid custom configuration
falls back to the root stderr log and reports the initialization failure there.
The startup NOTICE identifies the selected configuration.

## Files and retention

| File under `logs/` | Content |
| --- | --- |
| `all.log` | Combined structured records |
| `input.log` | Touch, controller, USB/OHCI, rumble |
| `graphics.log` | Vulkan, GPU, NV2A, D3D, shaders |
| `audio.log` | APU, audio, voice, DirectSound |
| `files.log` | File access, SAF, disc filesystem |
| `runtime.log` | Scheduler, GIL, threads, waits, diagnostic history |
| `performance.log` | PERF counters and FPS |
| `kernel.log` | Xbox kernel logger, preserving its subsystem tag and severity |
| `system.log` | Startup, options and general application messages |
| `crash.log` | Emergency fatal-signal output, bypassing zlog |
| `zlog.error.log` | Internal zlog errors, when present |

`logs/` is a permanent directory. Startup does not rename directories, archive
sessions, scan for available suffixes or move existing log files. zlog appends
to the current files across launches and owns file rotation through its rules.

Default category rules rotate at 8 MiB with up to three archives, and the
combined file at 32 MiB. Archive templates include the rotation date and rolling
number, for example `input.2026-10-18.0.log` and `all.2026-10-18.0.log`.
The date is an archive timestamp, not a game-session identifier. A fresh game
launch does not force a file boundary. Configure size/count/archive templates
in `zlog.conf` if different retention is needed.

zlog requests file mode `0644` through `file perms = 644`. The crash file
also requests `0644`, and the directory is created with requested mode `0755`.
The app does not change the process umask or chmod log files/folders. Actual
permissions and external access depend on Android and the inherited umask.
MTP readability is not guaranteed by these creation settings.

Existing dated directories from older builds are left untouched. The normal
rotation rules do not clean up these old directories or unrelated old archives.
`crash.log` and zlog's own error log append independently of the structured
file-rotation rules.

The root `nfsu1.log` remains a physical compatibility copy, and its previous
launch remains `nfsu1.previous.log`. It has no in-session size limit, to retain
existing raw stdout/stderr and emergency output. The category and combined
files together use roughly three times the space of a single log, before
retention. Raw third-party stdio and Android logcat messages are not silently
assigned a guessed category: raw stdio remains in the root compatibility log;
Android logcat remains separate.

Each normal record includes wall-clock milliseconds, severity, category,
native thread ID and source file/line. Per-destination ordering may differ
between concurrent producers; timestamps and thread IDs help correlate them.
The input trace additionally includes monotonic timestamps and Android event
uptime.

## Source API and crash handling

`xboxrecomp/include/nfsu_log.h` defines `NFS_LOG_TRACE`, `NFS_LOG_DEBUG`,
`NFS_LOG_INFO`, `NFS_LOG_NOTICE`, `NFS_LOG_WARN`, `NFS_LOG_ERROR` and
`NFS_LOG_FATAL`. Call sites supply a category and severity explicitly. Existing
native messages have been migrated to this API; no tag-content classifier or
replacement of global stdout/stderr FILE pointers remains.

A small `NFS_LOG_STREAM` adapter preserves old loops that assemble a dump
across several calls. Partial lines are thread-local and emitted at newline,
a full 16 KiB chunk, an explicit flush or before a direct record. A final
unfinished dump can be lost on abrupt termination. Ordinary direct records
are synchronous; the default configuration disables the zlog writer thread.

The fatal-signal handler switches to a bounded, best-effort emergency path
that avoids zlog locks and heap allocation, writing directly to stderr and
`crash.log`. Existing crash formatting/backtrace code is not claimed to be
fully async-signal-safe. System-level Android crash reports still require
logcat. This fallback bypasses normal level filtering.

## Collect a reproduction

```sh
adb push nfsu1x_env.txt /sdcard/Android/data/org.nfsu.android/files/nfsu1x_env.txt
# Fully restart the game, then reproduce.
adb pull /sdcard/Android/data/org.nfsu.android/files/logs
adb pull /sdcard/Android/data/org.nfsu.android/files/nfsu1.log
```

For steering failures, share `input.log`, available `all.log` archives and the
recording. The trace covers pointer IDs/coordinates, hit buttons, hold ownership,
mask submission/cancellation and final merged player-one controller state.
Negative steer is left; positive steer is right. This establishes the host
input state, not the game's physics response.

Host regression tests cover all seven severities, filtering and skipped
arguments, categories, partial/long messages, concurrent producers, append across
launches, native dated size rotation, invalid-config fallback and emergency
signal output. Android ARM64 library compilation and syntax checks do not replace APK
assembly or device validation.
