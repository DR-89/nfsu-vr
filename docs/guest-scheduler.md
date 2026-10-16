# Guest scheduler on Android

Android now selects guest execution through `kernel/guest_scheduler.c` instead
of the FIFO GIL queue. Native threads, stacks, guest register TLS and kernel
call stacks are retained. This is the first scheduler migration; it is not
a fiber implementation or a complete Xbox dispatcher.

## Execution policy

Exactly one registered thread owns the guest execution token. Threads have
HostCall, Ready, Running, Waiting, Suspended and Exited states. Selection uses
DISPATCH urgency, then the existing mapped guest priority, then FIFO order
among equal priorities.
Equal priorities share a 1 ms cooperative quantum. Higher priorities request
preemption at the next generated safe point. Explicit spin yields also use
the ready queue. There is no `sched_yield()` in this switching path and no
special quantum for the main thread.

Kernel host calls release the token as before. Blocking native waits notify
the scheduler, including their completion and timeout. Nested waits do not
reacquire twice. Priority changes and nested suspend/resume update registered
threads; suspension of running guest code takes effect at a safe point.
Native thread exit releases its scheduler record and execution token.

The Android game thread uses the platform thread wrapper with its existing
16 MiB stack, so priority and suspension refer to a real thread object.
Android platform threads detach their native stacks while reference counted
handles remain waitable.

DISPATCH execution reserves the virtual CPU across host calls. Ordinary guest
threads cannot run until that context lowers IRQL. Contended DISPATCH locks
release the guest token before blocking. Guest Rtl critical section fast paths
keep the token; contention releases it before the native lock wait.

## Remaining boundaries

Events and semaphores created after scheduler initialization now use the
handle dispatcher described below. Unshadowed guest-memory objects, mutants,
thread/file handles, objects created before initialization and mixed-object
wait sets still use their existing paths. Alertable multiple waits retain the
legacy APC polling path. The bridge still maps Xbox priorities into Win32
levels; preserving all distinct Xbox priority values remains a separate step.

Each guest thread still has a native thread. The selected native thread must
wake before it can execute; Android scheduling and migration delays can still
appear in `gil_handoff`. Strict guest priority can also expose guest polling
or priority inversion behavior. Device testing must cover menus, racing,
audio, loading and resume. Host tests establish synchronization behavior,
not improved FPS or complete guest compatibility.

## Device comparison

Default: `debug.nfsu.scheduler=1`. Startup contains
`[THREAD] guest scheduler: priority ready queue, native stacks`.
The old `debug.nfsu.gil_quantum_us` and `debug.nfsu.gil_eager` settings do not
select policy in this mode.

Use the same APK and fully restart the game process between modes:

```sh
adb shell setprop debug.nfsu.scheduler 1
# Restart through the launcher, open the same scene, collect poco-perf.log.
adb shell setprop debug.nfsu.scheduler 0
# Restart through the launcher and repeat: this selects the previous GIL.
```

Restore `1` after comparison. With performance counters enabled, compare main
thread `gil_queue`, `gil_handoff`, `preempt_wait`, frame pacing and audible
behavior, not just average FPS. No Android build or device validation was
performed for this change.

## Host checks

From the repository root:

```sh
cc -O1 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
  -Ixboxrecomp/src tests/guest_scheduler.c \
  xboxrecomp/src/kernel/guest_dispatcher.c \
  xboxrecomp/src/platform/win32_compat.c \
  -Wl,--gc-sections -pthread -lm -o /tmp/nfsu-guest-scheduler-test
/tmp/nfsu-guest-scheduler-test

cc -O1 -ffunction-sections -fdata-sections -Ixboxrecomp/src \
  -Ixboxrecomp/src/kernel tests/guest_scheduler_bridge.c \
  xboxrecomp/src/kernel/guest_scheduler.c \
  xboxrecomp/src/kernel/guest_dispatcher.c \
  xboxrecomp/src/platform/win32_compat.c \
  -Wl,--gc-sections -pthread -lm -o /tmp/nfsu-guest-scheduler-bridge-test
/tmp/nfsu-guest-scheduler-bridge-test
```

The first test covers priority and FIFO order, priority changes, event wait
handoff and timeout, equal priority quantum expiry, nested suspension,
exit cleanup and DISPATCH reservation. The second exercises the actual bridge
with eight workers and 16,000 nested entries, safe point/spin switching and
blocking host calls. Existing GIL handoff and timer signal regression tests
also remain applicable to the fallback and timer code.

## Scheduler attribution counters

When performance counters are enabled (`debug.nfsu.perf` is not `0`), the main
thread emits a bounded aggregate about every five seconds after releasing
its execution token. No per-switch log is written and logging occurs outside
the scheduler mutex. Scheduler policy is unchanged by these counters.

`SCHED5` reports total main scheduling wait and counts of actual switches
caused by spin, DISPATCH competition, higher priority, equal priority quantum,
suspension or other reasons. `SCHEDW` splits that wait by selected thread,
its priority, the main thread's priority, DISPATCH status and the reason the
main thread entered the queue. `reason=acquire` means entry/reentry rather
than an explicit yield. Priorities use the existing mapped Win32 levels.

`tid` is the platform thread ID; `host_tid` matches the native Linux `tid` in
`PERF5`/`PERFK`. IDs and priority values are copied into buckets, so a thread
that exits during a window remains identifiable in the report.

The phases distinguish wall time, not CPU consumption:

- `run`: another token owner has resumed execution.
- `wake`: another owner is selected but has not resumed yet.
- `dispatch_host`: a DISPATCH context reserves execution during host work.
- `idle`: main is pending but no owner or reservation exists.
- `main_wake`: main is selected but has not resumed yet.
- `suspended`: main is explicitly suspended.

`selections` counts grants to a competitor in its `wake` bucket; its `run`
bucket reports the subsequent time. The sum of bucket `wait_ms` and summary
`overflow_ms` equals summary `wait_ms`. There are at most 64 buckets per
window; additional combinations contribute to overflow time. These windows
are independent of `PERF5` windows and include ongoing waits at boundaries.
The main thread's blocking kernel/object waits and its own execution are
excluded. A `run` interval can still include native OS descheduling or host
work performed while holding the token; it does not prove useful guest work.

The scheduler host test also checks exact attribution against synthetic
timestamps for every phase, fixed-table overflow, real spin/higher/quantum
classification, conservation of total wait time and report snapshot/reset.

## Handle dispatcher migration

`guest_dispatcher.c` owns event/semaphore state and a queue of wait transactions.
The platform handle remains reference counted and keeps its original identity,
including duplicates and Ke shadow lookups. The platform layer has an optional
backend interface; it does not import Xbox scheduler policy. With the scheduler
disabled the backend is not installed and existing POSIX operations remain.

Non-alertable waits consisting entirely of managed objects check and consume
under one dispatcher mutex. WaitAny returns the lowest ready index. WaitAll
consumes nothing until every object is ready; duplicate WaitAll objects fail.
On signal, eligible waiters are granted in current mapped guest priority order,
then enrollment order. Ineligible WaitAll requests do not reserve partial
resources. Manual events can grant all eligible waiters; auto events grant one;
semaphores consume one count per successful wait. Overflowing semaphore releases
fail without changing state. Pulse grants only currently enrolled eligible waits.

A blocking request is enrolled before releasing its guest execution token.
Signal or timeout fixes its result under the same dispatcher mutex, then
publishes Ready to the scheduler before waking its native stack. The grant
cannot be stolen by a different waiter racing to wake. The waiter resumes only
when it owns the guest execution token; nested GIL depth is unchanged. Timeout
uses a monotonic condition-variable deadline and is removed from the list.
Already-ready and zero-timeout waits do not park the calling guest thread.

The bridge retains execution for managed event set/reset/pulse, semaphore
release and single/multiple waits (Nt and Ke forms). Only a wait that really
blocks relinquishes execution. Other kernel calls retain their previous policy.
The fast-path argument probe accounts for the return address still present on
the guest stack. Pending thread starts are flushed before these calls.

Timer expiry already signals its shadow event with SetEvent, so managed timer
shadows use this dispatcher too. Timer arming, guest SignalState updates and
expiry remain serialized under g_timer_lock. DPC execution stays outside that
lock; timer scheduling itself has not moved into the dispatcher.

Performance accounting ends GIL-held time when parking, excludes object wait
from GIL contention, and counts queue/handoff time from Ready until execution.
Native threads/stacks remain, so this stage reduces unnecessary transfers but
does not remove Android wakeup latency or establish a measured FPS improvement.

Additional host checks:

```sh
cc -O1 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
  -Ixboxrecomp/src tests/guest_dispatcher.c \
  xboxrecomp/src/kernel/guest_scheduler.c \
  xboxrecomp/src/platform/win32_compat.c \
  -Wl,--gc-sections -pthread -lm -o /tmp/nfsu-guest-dispatcher-test
/tmp/nfsu-guest-dispatcher-test

cc -O1 -DNFSU_DISPATCHER_TIMER_TEST -ffunction-sections -fdata-sections \
  -Ixboxrecomp/src -Ixboxrecomp/src/kernel tests/kernel_timer_signal.c \
  xboxrecomp/src/kernel/guest_scheduler.c \
  xboxrecomp/src/kernel/guest_dispatcher.c \
  xboxrecomp/src/platform/win32_compat.c \
  -Wl,--gc-sections -pthread -lm -o /tmp/nfsu-dispatcher-timer-test
/tmp/nfsu-dispatcher-timer-test
```

The dispatcher test covers priority/FIFO grants, manual/auto/pulse events,
WaitAny indices, atomic immediate and blocked WaitAll, semaphore limits,
duplicate rejection, timeout cleanup and 2,000 event handoffs between two
guest threads. The bridge test additionally checks
managed fast-path argument resolution, no handoff for a ready event and GIL
nesting/accounting across a genuinely blocking event wait. The timer race test
runs both with the original platform events and with dispatcher-backed shadows.
