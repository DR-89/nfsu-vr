# Guest scheduler on Android

By default Android selects guest execution through `kernel/guest_scheduler.c`
using native stacks. The sections below describe the migration stages. An
experimental single-native-thread title executor is now connected to game
startup, worker creation, managed kernel waits and interrupt delivery; see
"Experimental title executor" at the end for activation and remaining limits.

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
handle dispatcher described below. Valid inline guest events and semaphores
are adopted lazily, as described below. Mutants,
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
  xboxrecomp/src/kernel/recomp_context.c \
  xboxrecomp/src/kernel/kernel_thread.c \
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
  xboxrecomp/src/kernel/recomp_context.c \
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

## Inline guest object migration

Valid XDK inline event/semaphore headers (event size 4 LONGs, semaphore size
5 LONGs) are attached to dispatcher-backed Ke shadows on first kernel use.
The binding refers to the live header: signal, reset and successful wait
consumption update guest SignalState directly. A guest inline KeClearEvent
store clears that same state; there is no second host latch to get stale.
The event reset mode and semaphore limit are read from the live header, so
inline reinitialization of those fields is visible to subsequent operations.

All-managed wait sets containing these objects now use dispatcher transactions,
rather than the old 4 ms guest-object polling loop. A valid binding must remain
in mapped guest RAM for the object's lifetime. Waking waiters still requires
the kernel signal/release operation; arbitrary guest stores of a positive
SignalState are not a notification API. Unsupported object types and failed
shadow allocation retain the legacy path. The existing Ke shadow table still
owns mapping lifetime and capacity; cross-type object destruction/reuse is not
redesigned by this change.

The bridge regression covers lazy adoption before fast-path classification,
inline clears, auto-event consumption, manual-event reinitialization and live
semaphore limits. The dispatcher regression additionally covers a blocked wait
whose grant consumes the guest header state.

This remains a native-stack migration stage, not a single native executor.
Before replacing guest pthreads, runtime registers, FPU/SIMD, exception state
and kernel-local TLS must belong to explicitly saved guest contexts. Existing
native TLS cannot simply be shared between stackful guest continuations.

## Explicit architectural contexts

`recomp_context.h/.c` adds an explicit architectural context while preserving
the TLS register ABI consumed by generated code. Context switches save/load
all modeled general registers, FS and SEH frame pointers, DF, x87 stack/top/
control/status, MMX and SSE registers, native floating point environment and
errno. Normal instruction execution adds no register pointer indirection.

Native setjmp slots now belong to the current context, with a private default
context for each existing native thread. Generated setjmp/longjmp helper names
are unchanged. The jump buffers stay in place when another context is active;
register snapshots never copy them. Longjmp restores guest callee-saved
registers from guest RAM and discards inner slots as before.

Contexts are initialized before use and must stay at a fixed address until
their associated C continuations have ended. Activation binds a context to its
native thread; attempting to activate it from another thread or from a copied
struct fails. Reinitializing the current context also fails. A fresh context
starts with reset x87/control state, the supplied FS base, zero registers and
the host default floating environment. Initializing it preserves the caller's
current floating environment and errno.

This API switches architectural state, **not C stacks**. The optional executor
combines it with the stack switch described below. Default native scheduling
does not switch contexts. Register snapshots alone are insufficient to replace
guest pthreads; the executor also supplies identities, waitable handles and
context-owned kernel state. No device performance result is implied.

Host verification:

```sh
cc -O1 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
  -Ixboxrecomp/src tests/recomp_context.c \
  xboxrecomp/src/kernel/recomp_context.c \
  xboxrecomp/src/platform/win32_compat.c \
  -Wl,--gc-sections -pthread -lm -o /tmp/nfsu-recomp-context-test
/tmp/nfsu-recomp-context-test
```

The test switches two contexts on one native thread, restores the original
default context and verifies integer state, NaN payload preservation, FP
rounding/exception flags, errno, independent native jump-buffer addresses for
the same guest VA and an actual longjmp after context restoration. It also
checks rejection of copied/foreign-thread contexts and native TLS isolation.

## Kernel and platform state ownership

The active context now owns bridge GIL nesting/no-preempt/main-role flags,
quantum and held-time timestamps, guest stack ownership, kernel caller and
dispatch slot, vblank ISR flag and the counted path buffer. HAL IRQL and the
DISPATCH-held flag also belong to that context. Kernel busy-slot caching and
busy depth are context-owned. Existing pthread execution uses the default
context automatically; generated CPU register accesses are unchanged.

A context switch also saves/restores platform GetLastError, in addition to
errno and the floating environment. The native thread ID, thread handle,
scheduler pthread_key and native mutex/critical-section ownership remain native.
They are not copied or fabricated by a context switch. Contexts initialized
on a native thread do not create new thread handles or refcounts.

For this migration stage, switching is rejected if either context has a GIL
nesting depth, no-preempt scope, vblank ISR, raised IRQL, DISPATCH lock, kernel
busy depth, active native kernel bridge frame or tracked guest RTL critical
section. The kernel dispatch wrapper counts frames even for clock fast paths.
A rejected switch leaves CPU state, context pointer and last-error unchanged.
These restrictions are deliberately stronger than the final guest scheduler:
only audited managed waits may suspend native kernel frames. The optional
executor grants a continuation wait scope for KeDelay, Ke/Nt object waits and
RTL Enter. This scope permits frame/busy bookkeeping to survive suspension;
GIL, no-preempt, native-backed guest CS and IRQL ownership still forbid it. Exception unwinding across kernel frames also requires coordinated
kernel busy/lock cleanup; the snapshot API does not replace that unwinding.

The real HAL/RTL/thread host test raises and lowers DISPATCH, enters nested
RTL critical sections and changes kernel busy depth. It verifies switch
rejection while held, subsequent success after release, context-owned cached
state, isolated last-error and unchanged native thread identity. The bridge
test checks a real thunk entry, with no GIL held, to establish that native
kernel-frame tracking itself rejects a switch; it separately checks GIL nesting.

```sh
cc -O1 -ffunction-sections -fdata-sections -Ixboxrecomp/src \
  -Ixboxrecomp/src/kernel tests/kernel_context.c \
  xboxrecomp/src/kernel/recomp_context.c \
  xboxrecomp/src/kernel/kernel_hal.c \
  xboxrecomp/src/kernel/kernel_thread.c \
  xboxrecomp/src/kernel/kernel_rtl.c \
  xboxrecomp/src/platform/win32_compat.c \
  -Wl,--gc-sections -pthread -lm -o /tmp/nfsu-kernel-context-test
/tmp/nfsu-kernel-context-test
```

The default scheduler remains on pthreads. The optional executor adds the
virtual lifecycle below. Native profiling TLS is shared by all continuations;
the old per-pthread GIL breakdown does not describe executor scheduling. No
APK build or device validation has been performed for this ownership change.

## Stackful continuation mechanism

`guest_fiber.c` and `guest_fiber_switch.S` provide explicit resume/yield on
separate C stacks, with ARM64 AAPCS64 and Linux x86-64 SysV switch routines.
CMake includes these files only for the supported Linux/Android architectures.
A host CMake/ASM smoke build was checked separately; the game APK was not built.

Each continuation owns a fixed-address RecompContext, a stack mapping with
inaccessible pages at both ends, an entry function and lifecycle state. The
stack defaults to 16 MiB, with a minimum of 64 KiB and page-size rounding.
Resume saves the caller context and machine continuation, then loads the
selected guest context and native stack. Yield restores the caller and returns
through its original C frame. ARM64 preserves x19-x30, SP and the required
low 64 bits of v8-v15; SysV preserves RBX/RBP/R12-R15, SP and continuation PC.
Guest register state and native FP control/status remain in RecompContext.

Executor operations are bound to one native thread. Fibers are created NEW,
become RUNNING when resumed, SUSPENDED on yield, WAITING on a managed wait and
EXITED when the entry returns.
An exited fiber cannot be resumed. Only NEW and EXITED fibers may be destroyed;
suspended C frames are not silently discarded. An executor cannot be destroyed
while it owns fibers. Nested resume, foreign-thread operations and unsafe
context boundaries fail. Returning from an entry while it still owns a tracked
lock/GIL/IRQL scope is a programming error and aborts rather than abandoning
native ownership. Virtual thread entries support normal ExitThread; arbitrary
cancellation of suspended stacks remains forbidden.

Title threads may now opt into this mechanism. The title executor prepares
managed events before device initialization, runs the main entry on a virtual
thread, creates virtual workers and routes USB ISR callbacks onto its owner.
Only audited kernel waits permit switching their C frames. These host tests
are not FPS or device evidence. Blocking host I/O remains synchronous.
Sanitizer stack-switch hooks and platform stack-hardening configurations have
not been validated.

```sh
cc -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
  -Ixboxrecomp/src tests/guest_fiber.c \
  xboxrecomp/src/kernel/guest_fiber.c \
  xboxrecomp/src/kernel/guest_fiber_switch.S \
  xboxrecomp/src/kernel/recomp_context.c \
  xboxrecomp/src/platform/win32_compat.c \
  -Wl,--gc-sections -pthread -lm -o /tmp/nfsu-guest-fiber-test
/tmp/nfsu-guest-fiber-test
```

The Linux x86-64 test alternates two real C stacks for 2,000 yields on one
native thread, preserving eight levels of nested locals, CPU/FP/kernel context,
errno/last-error and the root context. Each stack uses the same guest jmp-buffer
VA, then successfully longjmps after being resumed. The test also covers return
and destruction, discarded unused stacks, size overflow, rejected nested and
foreign-thread operations, rejection while a guest lock is held and refusal to
destroy suspended stacks. The ARM64 assembly was cross-assembled for generic
AArch64 Linux to check instruction syntax; ARM64 execution and Android runtime
remain untested.

## Dispatcher continuation parking

`guest_fiber_dispatcher.c` supplies an optional immutable wait backend installed
explicitly with `xgf_use_dispatcher_waits()`, including during experimental
title executor preparation. A native-only waiter continues using the existing
condition-variable implementation even when the adapter is installed.

A fiber waiting on managed event/semaphore objects enrolls its wait transaction
under the dispatcher mutex and prepares a notification latch. The mutex is
released before parking the C continuation as WAITING. Signal fixes the result,
consumes the object transaction and latches a notification. The executor then
sees that continuation as ready to resume. No pthread wait or native scheduler
handoff is required between cooperating guest continuations. A signal arriving
between enrollment and parking is retained; park returns immediately in that
case. Priority selection reads each continuation's current priority rather
than the physical thread's shared ID.

Only notification uses cross-native-thread synchronization. Host completion
threads may latch a notification and signal the executor's wake condition;
they do not resume a guest stack or mutate its lifecycle state. The root can
sleep in `xgf_executor_wait()` when nothing is ready. Its ready predicate is
checked under the same wake mutex used by notifiers, including notifications
that arrive before the root starts sleeping. Stack destruction remains
forbidden until its wait transaction has returned and its entry has exited.

The executor driver must call `xgd_poll_timeouts()` outside executor locks on
each iteration. It expires continuation wait transactions under the dispatcher
mutex and returns milliseconds to the next deadline. The root can use that
value as its wake timeout. Signals also expire elapsed continuation deadlines
before granting resources. A timed-out waiter cannot consume a later signal.
Native waiters continue to use their own monotonic condition deadlines.

Sleep(0) and SwitchToThread yield the guest continuation. Positive Sleep uses
a dispatcher deadline, allowing the other continuations to execute. An unsafe
boundary fails before enrolling an event/semaphore waiter. Unsupported native
object waits, mixed sets and alertable waits fail explicitly while executing a
fiber (mutexes created inside a continuation are supported); they cannot silently block the only native executor or reuse native
mutex ownership across guest contexts. An unsafe positive Sleep is a programming
error. Native callers retain their existing behavior. These limitations are
reported as `[EXECUTOR] unsupported wait` instead of blocking the native executor.

```sh
cc -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
  -Ixboxrecomp/src tests/guest_fiber_dispatcher.c \
  xboxrecomp/src/kernel/guest_fiber.c \
  xboxrecomp/src/kernel/guest_fiber_switch.S \
  xboxrecomp/src/kernel/guest_fiber_dispatcher.c \
  xboxrecomp/src/kernel/guest_dispatcher.c \
  xboxrecomp/src/kernel/guest_scheduler.c \
  xboxrecomp/src/kernel/recomp_context.c \
  xboxrecomp/src/platform/win32_compat.c \
  -Wl,--gc-sections -pthread -lm -o /tmp/nfsu-fiber-dispatcher-test
/tmp/nfsu-fiber-dispatcher-test
```

The host test passes 1,000 producer/consumer messages on a single native thread,
atomic blocked WaitAll, monotonic timeouts, expiry-before-signal, live priority
grants, an external native completion wake, notification-before-park, zero and
positive Sleep, and rejection/cleanup of unsafe or unsupported waits. Native
dispatcher, standalone fiber, bridge and timer regression tests also pass.
Android execution and FPS still require device validation. The default title
execution path remains on native threads; the mode below is explicit opt-in.


## Experimental title executor

Set `debug.nfsu.executor=1` before restarting the app. The default is zero;
`debug.nfsu.scheduler` continues to control the native fallback only.

```sh
adb shell setprop debug.nfsu.executor 1
adb shell am force-stop org.nfsu.android
```

Open the game manually. Startup must contain both
`[ANDROID] guest executor=1` and `[EXECUTOR] enabled: one native executor`.
Use the same main menu after profile selection, then the same race. Revert
with `adb shell setprop debug.nfsu.executor 0` and restart the app.

Preemption queries cache whether another ready continuation has higher or equal
priority. Creation/removal, native notifications, callback submissions, priority
changes and suspend/resume invalidate that cache through an atomic generation.
The cache resets on each resume. Unchanged queries avoid the wake mutex and
ready-list traversal, and read the quantum clock only for equal-priority
competition. Notification state is published before its generation update so a
sleeping high-priority continuation cannot be hidden by a stale cached result.
The cooperative quantum and priority selection policy are unchanged. Host
regressions cover cache invalidation, native wake, and equal-priority expiry;
Android performance and thermal improvements require a repeated race test.

The root selects the highest mapped priority, FIFO among equal ready threads,
with a cooperative 1 ms slice. Managed waits park their C stacks until resource
completion or a monotonic deadline. Suspend/resume and priority changes use
virtual handle metadata; the shared native thread is never suspended or exited.
Normal entry return and PsTerminateSystemThread complete waitable thread handles
before detached worker stacks are reaped. Creator-side pending start publication
is preserved. The initial inline PsCreate call removes its outer thunk's busy
bookkeeping while running the title's main loop and restores it when that loop
returns.

Guest recursive mutexes use continuation/virtual-thread ownership. WaitAll
checks the entire set before consuming anything. A terminating virtual owner
abandons its mutexes; the next waiter gets WAIT_ABANDONED. RTL Enter/TryEnter/
Leave share one shadow table, fixing the previous separate TryEnter table.
Native-backed guest critical sections continue to prevent context switching.

The timer/IRQ/DPC pump runs as a high-priority virtual thread. It waits on the
managed IRQ event and raises DISPATCH only around ISR/DPC delivery, where guest
switches remain forbidden. The native OHCI device thread submits synchronous
callbacks to the executor; the callback uses the submitting device's TIB while
that device waits. The device never resumes a guest C stack itself. Callback
completion releases its native submitter and the root reaps the finished stack.
No new TIB is allocated for each OHCI callback.

SAF open/stat/directory calls enter ART/JNI on the pthread's original stack.
An ART-attached thread records its native stack bounds; calling Java directly
from an mmap-backed guest continuation is unsafe. A synchronous native-stack
call preserves the current guest context, identity and kernel ownership, runs
the host callback immediately, and returns to the same continuation. It never
selects another guest or grants a kernel wait scope. Yield/park are rejected
inside that host callback. SAF Java exceptions are described before clearing
so a Java failure is not reduced to an unexplained ENOENT in the game log.

Known limits before considering this a replacement for the default backend:

- Android/ARM64 game execution and FPS are unverified. Android API 30 syntax
  checks cover the changed C sources, including main and Android settings;
  they do not establish linking, APK assembly or runtime correctness.
- Native/mixed object waits and alertable object waits are unsupported in this
  mode and log a bounded diagnostic before returning WAIT_FAILED. Do not
  interpret a run with those diagnostics as a valid performance comparison.
- Synchronous host file I/O still stalls the executor. It has not been moved to
  a completion worker pool. General APC delivery and exception unwinding across
  arbitrary kernel callbacks remain outside the supported wait scope.
- Native per-thread profiling is not per-guest-thread profiling in this mode.
  Default `[SCHED5]`/`[SCHEDW]` wake breakdowns apply to the native backend.
- Live guest stacks are never forcibly discarded. The title run waits for all
  live entries, including its timer pump, rather than freeing guest memory
  underneath them. Process shutdown still owns native device-thread lifetime.

Host regression commands (from the repository root):

```sh
cc -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
  -Ixboxrecomp/src tests/guest_executor.c \
  xboxrecomp/src/kernel/guest_fiber.c \
  xboxrecomp/src/kernel/guest_fiber_switch.S \
  xboxrecomp/src/kernel/guest_fiber_dispatcher.c \
  xboxrecomp/src/kernel/guest_dispatcher.c \
  xboxrecomp/src/kernel/guest_scheduler.c \
  xboxrecomp/src/kernel/recomp_context.c \
  xboxrecomp/src/platform/win32_compat.c \
  -Wl,--gc-sections -pthread -lm -o /tmp/nfsu-guest-executor-test
/tmp/nfsu-guest-executor-test

cc -O2 -ffunction-sections -fdata-sections \
  -Ixboxrecomp/src -Ixboxrecomp/src/kernel tests/guest_kernel_executor.c \
  xboxrecomp/src/kernel/guest_fiber.c \
  xboxrecomp/src/kernel/guest_fiber_switch.S \
  xboxrecomp/src/kernel/guest_fiber_dispatcher.c \
  xboxrecomp/src/kernel/guest_dispatcher.c \
  xboxrecomp/src/kernel/guest_scheduler.c \
  xboxrecomp/src/kernel/recomp_context.c \
  xboxrecomp/src/kernel/kernel_rtl.c \
  xboxrecomp/src/kernel/kernel_hal.c \
  xboxrecomp/src/kernel/kernel_thread.c \
  xboxrecomp/src/kernel/kernel_sync.c \
  xboxrecomp/src/platform/win32_compat.c \
  -Wl,--gc-sections -pthread -lm -o /tmp/nfsu-guest-kernel-executor-test
/tmp/nfsu-guest-kernel-executor-test
```

These exercise 1,000 virtual-thread messages, priority/FIFO, child creation and
join, completion/duplicate handle lifetime, deadlines, suspend/resume and 100
native callback submissions. The kernel test drives actual thunk dispatch,
contended recursive RTL locks, TryEnter/Leave, Ke waits and delays, owner
abandonment, title boot and its inline handoff, actual bridge worker creation,
PsTerminateSystemThread, pending-start publication and native ISR marshalling.
All are host behavior checks, not evidence that the Android title is faster.

The executor test also checks 100 synchronous native-stack calls against
pthread_getattr_np stack bounds, with active kernel/GIL/native-lock guards,
preserved virtual identity and errno/last-error propagation. It verifies that
the callback cannot yield or park. This is not an ART/device runtime test.
