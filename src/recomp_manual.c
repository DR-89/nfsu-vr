#include "input_mode.h"
/**
 * Manual function overrides and ICALL diagnostics
 *
 * This file provides:
 *   - recomp_lookup_manual()  : intercept specific Xbox VAs with hand-written code
 *   - recomp_icall_fail_log() : log when an indirect call target can't be resolved
 *   - ICALL trace ring buffer  : globals used by the RECOMP_ICALL macro
 *
 * The recomp pipeline generates an auto-dispatch table (recomp_lookup) that
 * resolves most function addresses. recomp_lookup_manual() is called FIRST,
 * giving you a chance to override any function with a custom implementation.
 *
 * Common reasons to add manual overrides:
 *   - Trace a function to understand call flow (wrap the generated version)
 *   - Fix a function the lifter translated incorrectly
 *   - Stub out a function that crashes (return early, set eax to a safe value)
 *   - Redirect a function to a native implementation (e.g., skip CRT init)
 *   - Intercept D3D/audio calls for custom rendering or sound
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* The generated register model (g_eax/g_esp are thread-local there) and the
 * XBOX_PTR/MEM32 accessors; the register names below are its macros. */
#define RECOMP_GENERATED_CODE
#include "recomp_funcs.h"

#ifdef __ANDROID__
#include <pthread.h>
#include <time.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "android_diagnostics.h"

/* Boundary snapshots, not asynchronous stack unwinding. The SDL host thread
 * can report them even when a guest worker or its GIL stops making progress. */
typedef struct {
    uint64_t changed_ms, ticks;
    uint32_t function, ordinal, caller, stack[16];
    unsigned count, running, active, requested;
    long tid;
} AudioDiagnostic;
static AudioDiagnostic audio_diag[2]; /* worker, shutdown caller */
static pthread_mutex_t audio_diag_mutex = PTHREAD_MUTEX_INITIALIZER;
static RECOMP_TLS int audio_diag_lane = -1;

static uint64_t audio_diag_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static void audio_diag_boundary(uint32_t function, unsigned running, int tick)
{
    if (!nfsu_diagnostics_enabled) return;
    AudioDiagnostic *d;
    if (audio_diag_lane < 0) return;
    pthread_mutex_lock(&audio_diag_mutex);
    d = &audio_diag[audio_diag_lane];
    d->function = function;
    d->ordinal = 0;
    d->changed_ms = audio_diag_ms();
    d->tid = syscall(SYS_gettid);
    d->running = running;
    d->active = MEM8(0x2DDCEC);
    d->requested = MEM8(0x2DDCED);
    if (tick) d->ticks++;
    d->count = 0;
    for (unsigned i = 0; i < 16; i++) {
        uint32_t address = esp + i * 4u;
        if (address < 0x10000u || address > 0x03FFFFFCu) break;
        d->stack[d->count++] = MEM32(address);
    }
    pthread_mutex_unlock(&audio_diag_mutex);
}

/* Called only for potentially blocking kernel operations on a tracked thread.
 * No guest register or stack changes, no signals, and no forced completion. */
void nfsu_audio_diag_kernel(uint32_t ordinal, uint32_t caller, int entering)
{
    if (!nfsu_diagnostics_enabled) return;
    AudioDiagnostic *d;
    if (audio_diag_lane < 0) return;
    pthread_mutex_lock(&audio_diag_mutex);
    d = &audio_diag[audio_diag_lane];
    d->ordinal = entering ? ordinal : 0;
    d->caller = caller;
    d->changed_ms = audio_diag_ms();
    if (entering) {
        d->count = 0;
        for (unsigned i = 0; i < 16; i++) {
            uint32_t address = esp + i * 4u;
            if (address < 0x10000u || address > 0x03FFFFFCu) break;
            d->stack[d->count++] = MEM32(address);
        }
    }
    pthread_mutex_unlock(&audio_diag_mutex);
}

void nfsu_audio_diag_poll(void)
{
    if (!nfsu_diagnostics_enabled) return;
    static uint64_t last;
    AudioDiagnostic snapshot[2];
    uint64_t now = audio_diag_ms();
    if (now - last < 5000) return;
    if (pthread_mutex_trylock(&audio_diag_mutex) != 0) return;
    memcpy(snapshot, audio_diag, sizeof(snapshot));
    pthread_mutex_unlock(&audio_diag_mutex);
    last = now;
    for (unsigned lane = 0; lane < 2; lane++) {
        AudioDiagnostic *d = &snapshot[lane];
        if (!d->tid) continue;
        fprintf(stderr, "[AUDTRACE] t=%llu lane=%s tid=%ld running=%u ticks=%llu"
                " function=%08X kernel=%u caller=%08X age_ms=%llu"
                " sampled_active=%u sampled_run=%u stack_words=",
                (unsigned long long)now, lane ? "shutdown" : "worker", d->tid,
                d->running, (unsigned long long)d->ticks, d->function,
                d->ordinal, d->caller,
                (unsigned long long)(now >= d->changed_ms ? now - d->changed_ms : 0),
                d->active, d->requested);
        for (unsigned i = 0; i < d->count; i++) fprintf(stderr, " %08X", d->stack[i]);
        fputc('\n', stderr);
    }
    fflush(stderr);
}
#endif

extern void sub_001A5C60_gen(void);
void sub_001A5C60(void)
{
#ifdef __ANDROID__
    int previous = audio_diag_lane;
    audio_diag_lane = 0;
    audio_diag_boundary(0x001A5C60u, 1, 0);
#endif
    sub_001A5C60_gen();
#ifdef __ANDROID__
    audio_diag_boundary(0x001A5C60u, 0, 0);
    audio_diag_lane = previous;
#endif
}

extern void sub_001A2760_gen(void);
void sub_001A2760(void)
{
#ifdef __ANDROID__
    audio_diag_boundary(0x001A2760u, 1, 1);
#endif
    sub_001A2760_gen();
#ifdef __ANDROID__
    audio_diag_boundary(0x001A5C60u, 1, 0);
#endif
}

extern void sub_001A6110_gen(void);
void sub_001A6110(void)
{
#ifdef __ANDROID__
    int previous = audio_diag_lane;
    audio_diag_lane = 1;
    audio_diag_boundary(0x001A6110u, 1, 0);
    if (nfsu_diagnostics_enabled) fprintf(stderr, "[AUDTRACE] shutdown entered\n");
#endif
    sub_001A6110_gen();
#ifdef __ANDROID__
    audio_diag_boundary(0x001A6110u, 0, 0);
    if (nfsu_diagnostics_enabled) fprintf(stderr, "[AUDTRACE] shutdown returned\n");
    audio_diag_lane = previous;
#endif
}

#ifdef __ANDROID__
extern void nfsu_diag_voice(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern void nfsu_diag_dump(void);

static int audio_pointer_ok(uint32_t address, uint32_t size)
{
    uint32_t low = address & 0x7FFFFFFFu;
    return !(address & 3u) && low >= 0x10000u && low < 0x04000000u
        && size <= 0x04000000u - low;
}

static void audio_list_sample(uint32_t operation, uint32_t node, uint32_t parent)
{
    uint32_t next = 0, prev = 0, caller = 0;
    int invalid = !audio_pointer_ok(node, 8);
    if (audio_pointer_ok(esp, 4)) caller = MEM32(esp);
    if (!invalid) {
        next = MEM32(node); prev = MEM32(node + 4);
        invalid = !audio_pointer_ok(next, 8) || !audio_pointer_ok(prev, 8);
        if (!invalid) invalid = MEM32(next + 4) != node || MEM32(prev) != node;
    }
    nfsu_diag_voice(operation, caller, node, next, prev, parent);
    if (invalid) {
        fprintf(stderr, "[VOICE-LINK] invalid observed op=%08X node=%08X"
                " next=%08X prev=%08X parent=%08X caller=%08X\n",
                operation, node, next, prev, parent, caller);
        if (operation == 0x001E6921u && audio_pointer_ok(node - 0x4Cu, 0x6C)) {
            uint32_t object = node - 0x4Cu;
            fprintf(stderr, "[VOICE-LINK] object=%08X count=%u channel=%u"
                    " status=%04X owner=%08X\n", object, MEM8(object + 0x64),
                    MEM8(object + 0x65), MEM16(object + 0x12), MEM32(object + 0x68));
        }
        /* Emit retained context before the original code can fault. */
        nfsu_diag_dump();
    }
}
#endif

extern void sub_001E6921_gen(void);
void sub_001E6921(void)
{
#ifdef __ANDROID__
    uint32_t object = ecx;
    uint32_t parent = audio_pointer_ok(object, 12) ? MEM32(object + 8) : 0;
    audio_list_sample(0x001E6921u, object + 0x4Cu, parent);
#endif
    sub_001E6921_gen();
#ifdef __ANDROID__
    uint32_t node = object + 0x4Cu;
    if (nfsu_diagnostics_enabled) nfsu_diag_voice(0x801E6921u, 0, node,
        audio_pointer_ok(node, 8) ? MEM32(node) : 0,
        audio_pointer_ok(node, 8) ? MEM32(node + 4) : 0, parent);
#endif
}

extern void sub_001E2880_gen(void);
void sub_001E2880(void)
{
#ifdef __ANDROID__
    uint32_t node = audio_pointer_ok(esp, 8) ? MEM32(esp + 4) : 0;
    audio_list_sample(0x001E2880u, node, 0);
#endif
    sub_001E2880_gen();
#ifdef __ANDROID__
    /* Removed nodes need not have reciprocal links after unlink. Record the
     * bytes without applying the linked-node invariant to the removed node. */
    if (nfsu_diagnostics_enabled) nfsu_diag_voice(0x801E2880u, 0, node,
        audio_pointer_ok(node, 8) ? MEM32(node) : 0,
        audio_pointer_ok(node, 8) ? MEM32(node + 4) : 0, 0);
#endif
}

/* ── ICALL trace ring buffer ───────────────────────────────── */

/*
 * These globals are written by the RECOMP_ICALL macro (defined in
 * recomp_types.h) every time an indirect call is dispatched. When a
 * crash occurs, the VEH handler or recomp_icall_fail_log() can dump
 * the last 16 call targets to help you trace what happened.
 *
 * The runtime owns them: xbox_kernel defines all three in
 * src/kernel/xbox_memory_layout.c, and recomp_types.h declares them extern.
 * Declare, do not define -- a definition here as well is a duplicate symbol,
 * and a project copied from this template failed to link on all three:
 *
 *   xbox_memory_layout.obj : error LNK2005: g_icall_count already defined
 *                            in recomp_manual.obj
 */
extern volatile uint32_t g_icall_trace[16];
extern volatile uint32_t g_icall_trace_idx;
extern volatile uint64_t g_icall_count;

typedef void (*recomp_func_t)(void);

/* ── Register state (defined in xbox_memory_layout.c) ──────── */

extern ptrdiff_t g_xbox_mem_offset;

/* ── CRT memmove/memcpy ───────────────────────────────────────
 *
 * Two copies of the MSVC CRT memmove are linked (0x001B3640 and 0x002A9450;
 * memcpy is the same body). Their tail and backward-copy paths dispatch
 * through UnwindDown-style vectors indexed with a *negative* register -- the
 * table address in the instruction is the end of the table -- which the
 * lifter's forward table reader cannot follow, so those jumps were lowered
 * to indirect tail calls into the middle of the function and failed to
 * resolve: every copy whose length was not a multiple of 4, and every
 * overlapping backward copy, silently lost its tail.
 *
 * Replaced by the host memmove. cdecl (dst, src, count), returns dst, and
 * esi/edi are preserved, as the original restores them. Generated bodies are
 * skipped by tools.recomp --exclude-manual reading this file. */
static void crt_memmove(void)
{
    uint32_t dst = MEM32(esp + 4);
    uint32_t src = MEM32(esp + 8);
    uint32_t n   = MEM32(esp + 12);

    if (n)
        memmove((void *)XBOX_PTR(dst), (const void *)XBOX_PTR(src), n);
    eax = dst;
    esp += 4;   /* return address; cdecl, the caller pops the arguments */
}

void sub_001B3640(void) { crt_memmove(); }

/* ── D3D fence wait, D3D_BlockOnTime (0x001D0340) ────────────
 *
 * stdcall (fence, flags), ret 8. The stock XDK 5849 D3D keeps its device at
 * 0x002F77A0, reached through the global at 0x002F7798:
 *   +0x2C  write sequence, bumped as fences are inserted
 *   +0x30  pointer to the semaphore the GPU releases as it completes work
 * and this spins until *(+0x30) catches up with the fence it was asked for.
 *
 * Without the pushbuffer executor nothing ever releases the semaphore, so the
 * GPU is reported caught up. With it (RECOMP_PB_EXEC) that would let D3D
 * reuse ring space and vertex memory the executor has not read yet (see the
 * toolkit's d3d8ltcg-device-context.md), so instead the real semaphore is
 * handed to the executor once and the original wait runs against it. */
#define NFSU2_D3D_DEVICE_PTR 0x001DE9E8u
extern void nv2a_pb_set_semaphore_target(uint32_t guest_va);
extern void sub_001D0340_gen(void);

void sub_001D0340(void)
{
    static int exec = -1;
    uint32_t dev = MEM32(NFSU2_D3D_DEVICE_PTR);
    uint32_t sem = dev ? MEM32(dev + 0x30) : 0;

    if (exec < 0)
        exec = getenv("RECOMP_PB_EXEC") != NULL;
    if (exec) {
        static uint32_t registered;
        if (sem && sem != registered) {
            nv2a_pb_set_semaphore_target(sem);
            fprintf(stderr, "[D3D] GPU semaphore at 0x%08X\n", sem);
            registered = sem;
        }
        if (0 /* NFSU2 main-loop return addresses */) {
            /* The main loop (sub_000AEA90) calls BlockOnFence on the fence
             * of the frame it just built, right before Present: the whole
             * frame has to be through the GPU before the next one starts,
             * so the game and the pushbuffer executor take turns instead
             * of overlapping (Eden race: game waited ~50% of the time,
             * executor ~20% idle). RECOMP_FRAME_LAG=1 waits for the
             * previous frame's fence instead: one frame in flight, as on a
             * PC. Race frames checked on Linux, no corruption. */
            static int lag = -1;
            static uint32_t prev;
            if (lag < 0) {
                const char *e = getenv("RECOMP_FRAME_LAG");
                lag = e && *e == '1';
                fprintf(stderr, "[D3D] frame fence: %s\n",
                        lag ? "previous frame (RECOMP_FRAME_LAG=1)" : "this frame");
            }
            if (lag) {
                uint32_t mine = MEM32(esp + 4);
                if (prev)
                    MEM32(esp + 4) = prev;
                prev = mine;
            }
        }
        sub_001D0340_gen();
        return;
    }
    if (sem)
        MEM32(sem) = MEM32(dev + 0x2C);     /* "GPU" is always caught up */
    eax = 0;
    esp += 4 + 8;                           /* return address, 2 stdcall args */
}

/* ── D3D interrupt handlers at DISPATCH_LEVEL ────────────────
 *
 * D3D services the GPU from its DPC (0x002F2530) and, with the interrupt
 * masked, from its own busy-waits (BlockUntilIdle and friends) on the game
 * thread: both call the vblank handler (0x001D9BB0) and the PGRAPH handler
 * (0x001DA120), which queues flips. On the Xbox the two never interleave --
 * one is a DPC, the other runs with the interrupt off. Here the DPC runs on
 * the kernel timer thread, and lifted code hands over the guest lock at any
 * function entry, so the vblank count could move between D3D computing a
 * flip's target vblank and checking it. D3D retires a flip only when the
 * count *equals* its target, so that flip stayed pending forever and
 * PersistDisplay, which waits for pending flips, never returned: the hang
 * starting a Quick Race or Career. Run at DISPATCH_LEVEL (the dispatch lock,
 * kernel_hal.c), neither can be interrupted by the other.
 *
 * Raising blocks on the lock that the timer thread holds while it waits for
 * the guest lock, so the guest lock is let go for the raise, as a kernel
 * call would; already at DISPATCH (inside the DPC) there is nothing to take. */
extern uint8_t xbox_KfRaiseIrql(uint8_t irql);
extern void xbox_KfLowerIrql(uint8_t irql);
extern uint8_t xbox_CurrentIrql(void);
extern int xbox_gil_suspend(void);
extern void xbox_gil_resume(int depth);
extern void xbox_Nv2aVblankTaken(void);

static uint8_t d3d_isr_enter(void)
{
    uint8_t old = xbox_CurrentIrql();

    if (old < 2) {
        int d = xbox_gil_suspend();
        old = xbox_KfRaiseIrql(2);
        xbox_gil_resume(d);
    }
    return old;
}

static void d3d_isr_leave(uint8_t old)
{
    if (old < 2)
        xbox_KfLowerIrql(old);
}

/* Flips retired by a handler call: D3D's retire index, at +0x1BC of the
 * block both handlers take in ecx (their own reads index the pending-flip
 * slots from it). The pushbuffer executor's FLIP_STALL waits on these, as
 * PGRAPH waits on the retire's PGRAPH_INCREMENT write. */
extern void nv2a_pb_flip_retired(unsigned n);

/* Vblank handler, thiscall. It ends by writing PCRTC_INTR and spinning until
 * PMC_INTR bit 24 drops. Its callers (the DPC, D3D's busy-waits) have read
 * the bits before calling it, so this is the vblank being taken: clear them
 * now, and the spin ends at once instead of holding the guest lock until the
 * runtime's ack thread comes round. */
extern void sub_001D9BB0_gen(void);

void sub_001D9BB0(void)
{
    uint32_t blk = ecx;
    uint8_t old = d3d_isr_enter();
    /* Capture under the dispatch lock: raising IRQL can release the guest
     * lock and let another handler retire a flip. Reading before the raise
     * counted that flip again in our delta. */
    uint32_t head = MEM32(blk + 0x1BC);

    xbox_Nv2aVblankTaken();
    sub_001D9BB0_gen();
    nv2a_pb_flip_retired(MEM32(blk + 0x1BC) - head);
    d3d_isr_leave(old);
}

/* PGRAPH handler, thiscall, ecx = the device's hardware block (+0 =
 * 0xFD000000). Reads a software-method trap from PGRAPH_INTR / NSOURCE /
 * TRAPPED_ADDR / TRAPPED_DATA, acknowledges it by writing PGRAPH_INTR back,
 * and acts on it: NOP(5) sets the event BlockOnTime sleeps on, a flip trap
 * queues the flip (sub_002F2080). The acknowledge is write-1-to-clear on
 * hardware and a no-op on RAM. Left pending, every turn of a busy-wait took
 * the same trap again (tens of thousands of flips a second, until the flip
 * targets were millions of vblanks ahead); guessed from the handler's
 * PGRAPH_FIFO writes, a trap posted during a call was sometimes dropped (the
 * fence event never set). So the executor posts traps under a lock held here
 * around the call, and a call that found one reports it taken, which clears
 * it (nv2a_pb_exec.c). */
extern void sub_001DA120_gen(void);
extern void nv2a_pb_trap_lock(void);
extern void nv2a_pb_trap_unlock(void);
extern void nv2a_pb_trap_taken(void);

void sub_001DA120(void)
{
    volatile uint32_t *nv = (volatile uint32_t *)XBOX_PTR(0xFD000000u);
    uint32_t blk = ecx;
    uint8_t old = d3d_isr_enter();
    uint32_t nsource, head;

    nv2a_pb_trap_lock();
    head = MEM32(blk + 0x1BC);
    nsource = nv[0x400108 / 4];
    sub_001DA120_gen();
    nv2a_pb_flip_retired(MEM32(blk + 0x1BC) - head);   /* a flip queued on its vblank retires at once */
    if (nsource)
        nv2a_pb_trap_taken();
    nv2a_pb_trap_unlock();
    d3d_isr_leave(old);
}

/* ── DirectSound DSP command post (0x001E4CA1) ───────────────
 *
 * thiscall, ecx = the DSP-side object. Copies a command block into the GP
 * DSP's scratch area, stores the command code at scratch + 0x810, and spins
 * until the DSP zeroes it (this loop does re-read memory). The scratch block
 * is ***(this + 8 + 0x10), the same chain the runtime's RECOMP_DSP_ACK notes
 * describe. There is no DSP, so register the word with the runtime's
 * completion list before running the original body. */
extern int xbox_ApuDspAckWord(uint32_t va);
extern void sub_001E4CA1_gen(void);

void sub_001E4CA1(void)
{
    uint32_t obj = MEM32(ecx + 8);
    uint32_t blk = obj ? MEM32(obj + 0x10) : 0;
    uint32_t scratch = blk ? MEM32(blk) : 0;

    if (scratch)
        xbox_ApuDspAckWord(scratch + 0x810);
    sub_001E4CA1_gen();
}

/* ── DirectSound AC'97 channel reset (0x001EA5B2) ────────────
 *
 * thiscall, ecx = channel object. Sets RR (bit 1) in the channel's NABM
 * control byte, then waits for the controller to clear it -- except MSVC
 * hoisted the load out of the loop, so the original reads the byte once and
 * spins on the register copy forever unless the bit is already clear at that
 * one read:
 *
 *     mov  byte [eax+0xFEC0010B], 2
 *     mov  cl, [eax+0xFEC0010B]
 *     and  cl, 2
 *   L: test cl, cl
 *     jne  L
 *
 * On hardware the reset has completed by then. The Windows runtime answers it
 * with a write trap on the NABM page; there is no fault handling on POSIX or
 * Switch, so this is the same function with the reset completing at once.
 * Everything else -- the lock taken through sub_001E28B0/sub_001E28D2, the
 * buffer-descriptor base store, the 0xFEC0017C write for channel 1 and the
 * sub_001EA3BC re-arm -- is as the original does it. */
void sub_001EA5B2(void)
{
    uint32_t ebp_saved = g_ebp, frame, chan, nabm;

    PUSH32(esp, ebp_saved);
    frame = esp;
    g_ebp = frame;
    g_seh_ebp = frame;
    esp -= 8;
    MEM32(frame - 4) = 0;
    PUSH32(esp, esi);
    esi = ecx;
    chan = esi;

    ecx = frame - 8;
    PUSH32(esp, 0x001EA5C6u); RECOMP_ABI_CALL(0x001E28B0u, sub_001E28B0);

    nabm = MEM32(MEM32(chan) * 4 + 0x001EB200u);
    MEM8(nabm + 0xFEC0010Bu) = 2;
    MEM8(nabm + 0xFEC0010Bu) &= (uint8_t)~2u;   /* reset complete */
    eax = nabm;
    SET_LO8(ecx, 0);

    MEM32(nabm + 0xFEC00100u) = MEM32(chan + 0x1C);
    if (MEM32(chan) == 1)
        MEM32(0xFEC0017Cu) = MEM32(chan + 0x28);

    PUSH32(esp, 1);
    PUSH32(esp, 1);
    ecx = chan;
    PUSH32(esp, MEM8(chan + 0x24));
    PUSH32(esp, MEM8(chan + 0x25));
    eax = MEM8(chan + 0x25);
    g_ebp = frame;
    g_seh_ebp = frame;
    PUSH32(esp, 0x001EA619u); RECOMP_ABI_CALL(0x001EA3BCu, sub_001EA3BC);

    ecx = frame - 8;
    g_ebp = frame;
    g_seh_ebp = frame;
    PUSH32(esp, 0x001EA621u); RECOMP_ABI_CALL(0x001E28D2u, sub_001E28D2);

    POP32(esp, esi);
    esp = frame;
    POP32(esp, ebp_saved);
    g_ebp = ebp_saved;
    esp += 4;
}

/* Hor+ 16:9. NFSU1 has no widescreen mode (its only XGetVideoFlags caller,
 * 0x16EA50, reads the PAL-60 bit), so with the TV reported 16:9 the
 * presenter stretched the 4:3 picture. The view projection (sub_00018E60,
 * thiscall: ecx = the view's matrix block, arg = the view) builds
 *   m00 = cot(fov/2), m11 = cot(fov * H/W / 2)
 * from the render target's size, then sub_00018E40 forms view*proj, and the
 * frustum planes are taken from that product. Scaling m00 by 3/4 just
 * before the product widens the picture to 16:9 at the same vertical FOV,
 * culling included. Only 4:3 targets (race 640x480, its 320x240 copy, the
 * front end): the 128x128 environment-map faces stay square.
 * RECOMP_HORPLUS=0 restores the stretched 4:3 picture. */
extern int xbox_video_widescreen(void);
extern void sub_00018E60_gen(void);
extern void sub_00018E40_gen(void);
static uint32_t s_proj_view;
static int s_ui_aspect_active;

static int horplus_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("RECOMP_HORPLUS");
        on = xbox_video_widescreen() && !(e && e[0] == '0');
    }
    return on;
}

void sub_00018E60(void)
{
    s_proj_view = MEM32(esp + 4);
    sub_00018E60_gen();
    s_proj_view = 0;
}

/* Game flow state: 3 in the front end, 4 loading, 6 racing (the game
 * itself tests `cmp [0x283434], 3`). */
#define NFSU1_GAMEFLOW_STATE 0x00283434u

/* Keep guest overrides here so the recompilation scanner protects their
 * original generated bodies. Gesture policy lives in the shared module. */
#ifdef __ANDROID__
#include "feng_touch.h"
#include "menu_touch.h"
#endif
extern void sub_000D8B10_gen(void);
extern void sub_000DB860_gen(void);
extern void sub_000D8730_gen(void);
extern void sub_000DDB60(void);
/* Native dependencies retained here for the recompiler name-protection scan. */
extern void sub_000B8310(void);
extern void sub_000D1AD0(void);
extern void sub_000C74C0(void);
extern void sub_000C7A80(void);
extern void sub_000C7B40(void);
extern void sub_000D6D30(void);
extern void sub_000D7A00(void);
extern void sub_000DE540(void);
extern void sub_000DF160(void);
extern void sub_000E53C0(void);
void nfsu_touch_hit(void)
{
#ifdef __ANDROID__
    feng_touch_hit();
#else
    sub_000DDB60();
#endif
}
void sub_000D8730(void)
{
#ifdef __ANDROID__
    feng_touch_dispatch();
#else
    sub_000D8730_gen();
#endif
}
void sub_000D8B10(void)
{
#ifdef __ANDROID__
    feng_touch_update();
#else
    sub_000D8B10_gen();
#endif
}
void sub_000DB860(void)
{
#ifdef __ANDROID__
    feng_touch_poll();
#else
    sub_000DB860_gen();
#endif
}

void sub_00018E40(void)
{
    uint32_t v = s_proj_view;
    int fe;

    /* The 2D pass rebuilds view 0 with Menu Size on both axes and an
     * identity projection. Compensate the anamorphic 16:9 presentation
     * before forming its product; the 3D camera uses the other path below. */
    if (MEM32(esp) == 0x00016548u && horplus_on() &&
        MEM32(NFSU1_GAMEFLOW_STATE) == 6) {
        float m;
        memcpy(&m, (void *)XBOX_PTR(ecx + 0x40), sizeof m);
        m *= 0.75f;
        memcpy((void *)XBOX_PTR(ecx + 0x40), &m, sizeof m);
    }

    if (!v || MEM32(esp) != 0x0001902Au) {
        sub_00018E40_gen();
        return;
    }
    fe = MEM32(NFSU1_GAMEFLOW_STATE) == 3;

    if (horplus_on()) {
        uint32_t rt = MEM32(v + 0x58);
        uint32_t w = rt ? MEM32(rt + 0x1C) : 0, h = rt ? MEM32(rt + 0x20) : 0;
        if (w && w * 3 == h * 4) {
            /* The front end zooms instead (m11 x 4/3): its garage set
             * ends just past the 4:3 frame -- the tunnel mouth on the
             * right -- so it keeps the original width, minus top and
             * bottom. */
            uint32_t at = ecx + (fe ? 0x54 : 0x40);   /* m11 : m00 */
            float m;
            memcpy(&m, (void *)XBOX_PTR(at), 4);
            m *= fe ? 4.0f / 3.0f : 0.75f;
            memcpy((void *)XBOX_PTR(at), &m, 4);
        }
    }
    /* Front-end garage camera (views 1 and 4 share it) turned left by
     * RECOMP_FE_YAW degrees (default 5): rows 0 and 2 of the view matrix
     * at ecx (row-major, column vectors, eye +z forward) rotated about
     * the eye's y axis. sub_00018E60 copies the camera in afresh. */
    if (fe && (MEM32(v + 4) == 1 || MEM32(v + 4) == 4)) {
        static float yaw = -1000.0f;
        if (yaw == -1000.0f) {
            const char *e = getenv("RECOMP_FE_YAW");
            yaw = (e ? (float)atof(e) : 5.0f) * 3.14159265f / 180.0f;
        }
        if (yaw != 0.0f) {
            float m[16], c = cosf(yaw), s = sinf(yaw);
            int j;
            memcpy(m, (void *)XBOX_PTR(ecx), sizeof m);
            for (j = 0; j < 4; j++) {
                float r0 = m[j], r2 = m[8 + j];
                m[j]     = c * r0 + s * r2;
                m[8 + j] = -s * r0 + c * r2;
            }
            memcpy((void *)XBOX_PTR(ecx), m, sizeof m);
        }
    }
    sub_00018E40_gen();
}

/* The minimap renderer owns a screen-space centre (+0x68) and half
 * extent (+0x70). Its draw builds the map and markers from that centre. */
extern void sub_0014B510_gen(void);

void sub_0014B510(void)
{
    uint32_t centre = ecx + 0x68;
    uint32_t original = MEM32(centre);
    float x;

    memcpy(&x, &original, 4);
    if (s_ui_aspect_active && MEM32(NFSU1_GAMEFLOW_STATE) == 6) {
        if (x < 280.0f) x -= 640.0f / 6.0f;
        else if (x > 360.0f) x += 640.0f / 6.0f;
        memcpy((void *)XBOX_PTR(centre), &x, 4);
    }
    sub_0014B510_gen();
    MEM32(centre) = original;
}

/* Associate cached draw geometry with its actual FEng object, including
 * the reused-cache path. Group hit bounds are the union of drawn children. */
extern void sub_000D9D00_gen(void);
void sub_000D9D00(void)
{
#ifdef __ANDROID__
    uint32_t previous = feng_touch_draw_begin(MEM32(esp + 4));
#endif
    sub_000D9D00_gen();
#ifdef __ANDROID__
    feng_touch_draw_end(previous);
#endif
}

/* FEng renders the rest of the HUD through a cached circular list of
 * quads. This path is shared by sprites and text (one quad per glyph).
 * Select one anchor from the complete object's bounds, then translate all
 * of its quads equally. Editing FEng layout descriptors would miss cached
 * geometry; selecting an anchor for each glyph would split long strings. */
extern void sub_000F8B80_gen(void);
static float s_feng_shift;
static int s_feng_draw;
static int s_feng_menu_draw;

/* sub_00016460 draws FEng menus through sub_000DC090, then the racing
 * HUD through the callbacks at 0x265B8C. Game flow remains 6 for pause,
 * rewards and race results: it cannot distinguish those menus from HUD.
 * Undo the racing UI projection equally for every menu quad, including
 * glyphs, so menus retain their original stretched presentation. */
extern void sub_000DC090_gen(void);

#ifdef __ANDROID__
void nfsu_controller_settings_refresh(void);
#endif
void sub_000DC090(void)
{
    int previous = s_feng_menu_draw;
    s_feng_menu_draw = 1;
#ifdef __ANDROID__
    if (!previous) {
        nfsu_controller_settings_refresh();
        feng_touch_frame_begin();
    }
#endif
    sub_000DC090_gen();
#ifdef __ANDROID__
    if (!previous) feng_touch_frame_end();
#endif
    s_feng_menu_draw = previous;
}

void sub_000F8B80(void)
{
    uint32_t head = ecx + 0x18;
    uint32_t node = MEM32(head);
    float left = INFINITY, right = -INFINITY;
    float previous_shift = s_feng_shift;
    int previous_draw = s_feng_draw;
    unsigned count = 0;

#ifdef __ANDROID__
    if (s_feng_menu_draw && feng_touch_draw_cache(ecx)) {
        esp += 4; /* retain native hit geometry, omit obsolete Y glyph */
        return;
    }
#endif
    s_feng_shift = 0.0f;
    s_feng_draw = s_ui_aspect_active;
    if (s_feng_draw && !s_feng_menu_draw &&
        MEM32(NFSU1_GAMEFLOW_STATE) == 6) {
        while (node != head && node && count++ < 4096) {
            /* List node +0x10 is the quad, its xyz vertices start +0x10. */
            for (int i = 0; i < 4; i++) {
                float x;
                memcpy(&x, (void *)XBOX_PTR(node + 0x20 + i * 16), 4);
                left = fminf(left, x); right = fmaxf(right, x);
            }
            node = MEM32(node);
        }
        if (node == head && count) {
            float centre = (left + right) * 0.5f;
            if (centre < 280.0f) s_feng_shift = -640.0f / 6.0f;
            else if (centre > 360.0f) s_feng_shift = 640.0f / 6.0f;
        }
    }
    sub_000F8B80_gen();
    s_feng_shift = previous_shift;
    s_feng_draw = previous_draw;
}

/* The racing pass has a 0.75 horizontal projection. Undo it for every
 * menu quad to preserve the original stretched 4:3 menu, including text.
 * Only racing HUD objects receive an edge shift. Restore cached geometry. */
extern void sub_000192C0_gen(void);

void sub_000192C0(void)
{
    uint32_t quad = MEM32(esp + 4);
    float original_x[4];
    float shift = s_feng_shift;
    int i;
    int stretch = s_feng_menu_draw;

    if (!s_ui_aspect_active || (!stretch &&
        (!s_feng_draw || shift == 0.0f))) {
        sub_000192C0_gen();
        return;
    }
    for (i = 0; i < 4; i++) {
        float x;
        memcpy(&x, (void *)XBOX_PTR(quad + 0x10 + i * 16), 4);
        original_x[i] = x;
        x = stretch ? 320.0f + (x - 320.0f) * (4.0f / 3.0f) : x + shift;
        memcpy((void *)XBOX_PTR(quad + 0x10 + i * 16), &x, 4);
    }
    sub_000192C0_gen();
    for (i = 0; i < 4; i++)
        memcpy((void *)XBOX_PTR(quad + 0x10 + i * 16), &original_x[i], 4);
}

/* ── Options -> Camera: car reflections and picture rows ──────
 *
 * The port's own settings, as NFSU2 has them under Options -> Video, live
 * on the Camera screen: Display already has seven rows and no room for
 * more, Camera has two (Favorite Drive Camera, Jump Cameras).
 *
 * All option screens are one class on MU_Options.fng; the Options menu
 * leaves the chosen one in [0x2BBB54] (1 = Camera) and the screen calls
 * its setup: sub_000C2A70 for Camera. A row N (1..10) is the package's
 * OptionName_N / OptionData_N / LeftArrow_N / RightArrow_N objects:
 * sub_000C1D30(N, data, select button) shows it and makes it selectable,
 * sub_000C1E10(N, 0) adds the arrows. Pad left/right on a row call
 * this+0x3C+4*(N-1) (thiscall, the message, ret 4; dispatch at
 * 0x000C281C), Favorite Drive Camera's being sub_000C0C90: step a value,
 * rewrite the texts with sub_000DD490(package, object name, text) and
 * play the arrow sound (sub_000DDDE0 / sub_000DDD60). Rows 3..9 are added
 * the same way after the original setup; their handlers are our own, at
 * unused int3 bytes after sub_000C0C20 (ROW_HANDLER_VA, recomp_lookup_manual).
 * Texts are our own strings in guest memory -- the language files are
 * Huffman-packed, and dd490 takes any text.
 *
 * Car Reflections Off: the Vulkan renderer's cube stages sample black
 * (nv2a_vk_cube_maps; the GL renderer never drew them) and the race
 * renderer sub_00016050 skips the six "EnvMap %d" cube-face views (ids
 * 10..15, view(id) = 0x2C5F20 + id*0x60, drawn only while byte +8 is set):
 * the bytes are cleared for the call and put back after it.
 *
 * Vulkan build only: Resolution Scale 1x/1.5x/2x/2.5x (nv2a_vk_scale_pct,
 * applied at the next flip; without a saved value RECOMP_GL_SCALE rules
 * and the row shows the nearest step), Anti-Aliasing = FXAA
 * (nv2a_vk_fxaa), Anisotropic Off..16x (nv2a_vk_aniso), Square Pixels
 * (nv2a_vk_square: 4/3 the columns in 16:9). The renderer's env switches
 * still force each off.
 *
 * Kept in nfsu1x_options.txt (sdmc:/switch/nfsu1x/ on the Switch, the
 * working directory elsewhere), not in the profile; loaded at boot
 * (nfsu1_options_load, main.c). */
#ifdef __SWITCH__
#  define NFSU1_OPTIONS_FILE "sdmc:/switch/nfsu1x/nfsu1x_options.txt"
#else
#  define NFSU1_OPTIONS_FILE "nfsu1x_options.txt"
#endif
#define FE_PAD_LEFT     0x9120409Eu
#define FE_PAD_RIGHT    0xB5971BF1u
#define ROW_HANDLER_VA  0x000C0C83u     /* + row index: int3 padding */
#define ROW_FIRST       3               /* rows 1-2 are the game's */
#define ENVMAP_VIEW(id) (0x002C5F20u + (uint32_t)(id) * 0x60u)

#ifdef NFSU2_VULKAN
extern volatile int nv2a_vk_cube_maps;
extern volatile int nv2a_vk_scale_pct;
extern volatile int nv2a_vk_fxaa;
extern volatile int nv2a_vk_aniso;
extern volatile int nv2a_vk_square;
#endif
extern uint32_t xbox_ContiguousAlloc(uint32_t size, uint32_t alignment);
extern void sub_000C2A70_gen(void);
extern void sub_00016050_gen(void);

static const int s_scale_pct[] = { 100, 150, 200, 250 };
static const int s_aniso_lvl[] = { 1, 2, 4, 8, 16 };

static int s_blur = 1;                  /* native blur enabled by default */
static int s_fps60;                     /* experimental; default 30 FPS */
static int s_refl = 1;                  /* 0 off, 1 on */
static int s_scale_idx = 1;             /* into s_scale_pct: 1.5x */
static int s_scale_saved;               /* nfsu1x_options.txt had scale= */
static int s_fxaa = 1;                  /* 0 off, 1 on */
static int s_aniso_idx = 4;             /* into s_aniso_lvl: 16x */
static int s_square = 1;                /* 0 off, 1 on */

typedef struct {
    const char *label;
    const char *const *texts;
    int n;
    int *value;
    void (*changed)(void);
} OptRow;

static void refl_changed(void)
{
#ifdef NFSU2_VULKAN
    nv2a_vk_cube_maps = s_refl;
#endif
}

#ifdef NFSU2_VULKAN
static void scale_changed(void)
{
    nv2a_vk_scale_pct = s_scale_pct[s_scale_idx];
    s_scale_saved = 1;
}

static void picture_changed(void)
{
    nv2a_vk_fxaa = s_fxaa;
    nv2a_vk_aniso = s_aniso_lvl[s_aniso_idx];
    nv2a_vk_square = s_square;
}
#endif

static const char *const s_onoff[] = { "Off", "On" };
#ifdef NFSU2_VULKAN
static const char *const s_scale_texts[] = { "1x", "1.5x", "2x", "2.5x" };
static const char *const s_aniso_texts[] = { "Off", "2x", "4x", "8x", "16x" };
#endif
static const char *const s_fps_texts[] = { "30 FPS", "60 FPS" };

static void frame_rate_changed(void)
{
    /* Applied by the race render wrapper on the next frame. */
}

static const OptRow s_rows[] = {
    { "Car Reflections:", s_onoff, 2, &s_refl, refl_changed },
#ifdef NFSU2_VULKAN
    { "Resolution Scale:", s_scale_texts, 4, &s_scale_idx, scale_changed },
    { "Anti-Aliasing:", s_onoff, 2, &s_fxaa, picture_changed },
    { "Anisotropic:", s_aniso_texts, 5, &s_aniso_idx, picture_changed },
    { "Square Pixels:", s_onoff, 2, &s_square, picture_changed },
#endif
    { "Frame Rate:", s_fps_texts, 2, &s_fps60, frame_rate_changed },
    { "Blur/Bloom:", s_onoff, 2, &s_blur, frame_rate_changed },
};
#define N_ROWS ((int)(sizeof s_rows / sizeof s_rows[0]))

static void options_log(void)
{
    fprintf(stderr, "[options] blur %s\n", s_blur ? "on" : "off");
    fprintf(stderr, "[options] race frame rate %d FPS (experimental 60 FPS)\n",
            s_fps60 ? 60 : 30);
#ifdef NFSU2_VULKAN
    fprintf(stderr, "[options] car reflections %s, resolution scale %d%%%s, anti-aliasing %s,"
            " anisotropic %dx, square pixels %s\n",
            s_refl ? "on" : "off", s_scale_pct[s_scale_idx],
            nv2a_vk_scale_pct ? "" : " (RECOMP_GL_SCALE)", s_fxaa ? "on" : "off",
            s_aniso_lvl[s_aniso_idx], s_square ? "on" : "off");
#else
    fprintf(stderr, "[options] car reflections %s\n", s_refl ? "on" : "off");
#endif
}

void nfsu1_options_load(void)
{
#ifdef __ANDROID__
    InputMode saved_mode = input_mode_get();
    int sensor_preference = saved_mode.preference;
    int race_overlay = saved_mode.overlay_requested;
#endif
    char line[128];
    FILE *f = fopen(NFSU1_OPTIONS_FILE, "r");
    int i;

    if (f) {
        while (fgets(line, sizeof line, f)) {
#ifdef __ANDROID__
            if (!strncmp(line, "sensor_mode=", 12)) {
                int mode = atoi(line + 12);
                sensor_preference = mode >= -1 && mode <= 1 ? mode : -1;
                continue;
            }
            if (!strncmp(line, "race_overlay=", 13)) {
                race_overlay = atoi(line + 13) == 1;
                continue;
            }
#endif
            if (!strncmp(line, "reflections=", 12))
                s_refl = line[12] != '0';
            else if (!strncmp(line, "blur=", 5))
                s_blur = line[5] != '0';
            else if (!strncmp(line, "fps=", 4))
                s_fps60 = atoi(line + 4) == 60;
            else if (!strncmp(line, "fxaa=", 5))
                s_fxaa = line[5] != '0';
            else if (!strncmp(line, "square=", 7))
                s_square = line[7] != '0';
            else if (!strncmp(line, "aniso=", 6)) {
                int a = atoi(line + 6);
                for (i = 0; i < 5; i++)
                    if (s_aniso_lvl[i] == a)
                        s_aniso_idx = i;
            }
            else if (!strncmp(line, "scale=", 6)) {
                int pct = atoi(line + 6);
                for (i = 0; i < 4; i++)
                    if (s_scale_pct[i] == pct) {
                        s_scale_idx = i;
                        s_scale_saved = 1;
                    }
            }
        }
        fclose(f);
    }
#ifdef __ANDROID__
    input_mode_configure(sensor_preference, race_overlay);
#endif
    refl_changed();
#ifdef NFSU2_VULKAN
    {
        const char *e = getenv("RECOMP_GL_SCALE");
        if (!s_scale_saved && e && *e) {
            /* RECOMP_GL_SCALE stays in charge until the option is changed;
             * the row shows the nearest step. */
            double k = strtod(e, NULL), best = 1e9;
            for (i = 0; i < 4; i++) {
                double d = fabs(k * 100.0 - s_scale_pct[i]);
                if (d < best) { best = d; s_scale_idx = i; }
            }
        } else {
            nv2a_vk_scale_pct = s_scale_pct[s_scale_idx];
        }
    }
    picture_changed();
#endif
    options_log();
}

static void options_save(void)
{
    FILE *f = fopen(NFSU1_OPTIONS_FILE, "w");
    if (!f) {
        fprintf(stderr, "[options] cannot write " NFSU1_OPTIONS_FILE "\n");
        return;
    }
    fprintf(f, "reflections=%d\nfps=%d\nblur=%d\n", s_refl, s_fps60 ? 60 : 30, s_blur);
    if (s_scale_saved)
        fprintf(f, "scale=%d\n", s_scale_pct[s_scale_idx]);
#ifdef NFSU2_VULKAN
    fprintf(f, "fxaa=%d\naniso=%d\nsquare=%d\n", s_fxaa, s_aniso_lvl[s_aniso_idx], s_square);
#endif
#ifdef __ANDROID__
    InputMode mode = input_mode_get();
    fprintf(f, "sensor_mode=%d\nrace_overlay=%d\n", mode.preference, mode.overlay_requested);
#endif
    fclose(f);
}

/* Our texts in guest memory, each copied once. */
static uint32_t guest_str(const char *s)
{
    static struct { const char *s; uint32_t va; } cache[64];
    static uint32_t pool, used;
    size_t n = strlen(s) + 1;
    int i;

    for (i = 0; i < 64 && cache[i].s; i++)
        if (cache[i].s == s)
            return cache[i].va;
    if (i == 64)
        return 0;
    if (!pool) {
        pool = xbox_ContiguousAlloc(0x800u, 16);
        if (!pool)
            return 0;
    }
    if (used + n > 0x800u)
        return 0;
    memcpy((void *)XBOX_PTR(pool + used), s, n);
    cache[i].s = s;
    cache[i].va = pool + used;
    used += (uint32_t)n;
    return cache[i].va;
}

/* sub_000DD490(package, object name, text), cdecl. */
static void fe_set_text(uint32_t screen, const char *object, const char *text)
{
    uint32_t name = guest_str(object), str = guest_str(text);

    if (!name || !str)
        return;
    PUSH32(esp, str);
    PUSH32(esp, name);
    PUSH32(esp, MEM32(screen + 0xCu));
    PUSH32(esp, ROW_HANDLER_VA);
    sub_000DD490();
    esp += 12;
}

static const char *const s_name_obj[] = {
    "OptionName_3", "OptionName_4", "OptionName_5", "OptionName_6", "OptionName_7", "OptionName_8", "OptionName_9",
};
static const char *const s_data_obj[] = {
    "OptionData_3", "OptionData_4", "OptionData_5", "OptionData_6", "OptionData_7", "OptionData_8", "OptionData_9",
};

static void opt_row_text(uint32_t screen, int i)
{
    fe_set_text(screen, s_name_obj[i], s_rows[i].label);
    fe_set_text(screen, s_data_obj[i], s_rows[i].texts[*s_rows[i].value]);
}

/* Camera setup (thiscall, ecx = options screen, ret): the game's two rows,
 * then ours. */
void sub_000C2A70(void)
{
    uint32_t screen = ecx;
    int i;

    sub_000C2A70_gen();
    for (i = 0; i < N_ROWS; i++) {
        int row = ROW_FIRST + i;
        PUSH32(esp, 0);                     /* no select button */
        PUSH32(esp, 1);                     /* value text */
        PUSH32(esp, (uint32_t)row);
        ecx = screen;
        PUSH32(esp, 0x000C2A99u);
        sub_000C1D30();                     /* thiscall, pops its 3 */
        PUSH32(esp, 0);                     /* arrows, not -/+ */
        PUSH32(esp, (uint32_t)row);
        ecx = screen;
        PUSH32(esp, 0x000C2AB3u);
        sub_000C1E10();                     /* thiscall, pops its 2 */
        MEM32(screen + 0x3Cu + 4u * (uint32_t)(row - 1)) = ROW_HANDLER_VA + (uint32_t)i;
        opt_row_text(screen, i);
    }
}

#ifdef __ANDROID__
extern void sub_000C2B80_gen(void);
#include "race_touch.h"
#define SENSOR_HANDLER_VA (ROW_HANDLER_VA + 7u)
/* Retail FEng: DC340 finds an object, DC220 toggles its selectable state,
 * and DCD30 changes its color (including native cache invalidation). BF958
 * uses the same disable/color path with FF606060 for unavailable entries. */
extern void sub_000DC340(void);
extern void sub_000DC220(void);
extern void sub_000DCD30(void);
typedef struct {
    uint32_t object[4], color[4];
} NativeOptionRow;
static NativeOptionRow s_controller_touch_rows[2];

static uint32_t fe_object_hash(const char *name)
{
    uint32_t hash = 0xFFFFFFFFu;
    for (; *name; name++) {
        unsigned char c = (unsigned char)*name;
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        hash = hash * 33 + c;
    }
    return hash;
}

static void native_option_row_enabled(NativeOptionRow *state, uint32_t screen,
                                     unsigned row, int enabled)
{
    static const char *const parts[] = { "OptionName", "OptionData", "LeftArrow", "RightArrow" };
    uint32_t package = MEM32(screen + 0xC);
    for (unsigned i = 0; i < 4; i++) {
        char name[32];
        snprintf(name, sizeof(name), "%s_%u", parts[i], row);
        uint32_t hash = fe_object_hash(name);
        PUSH32(esp, hash); PUSH32(esp, package); PUSH32(esp, 0);
        sub_000DC340();
        esp += 8;
        uint32_t object = eax;
        if (!object) continue;
        if (state->object[i] != object) {
            uint32_t color = MEM32(object + 0x2C);
            if (!color) continue;
            state->object[i] = object;
            state->color[i] = (MEM32(color) & 255u) |
                ((MEM32(color + 4) & 255u) << 8) |
                ((MEM32(color + 8) & 255u) << 16) |
                ((MEM32(color + 12) & 255u) << 24);
        }
        PUSH32(esp, enabled); PUSH32(esp, hash); PUSH32(esp, package); PUSH32(esp, 0);
        sub_000DC220();
        esp += 12;
        PUSH32(esp, enabled ? state->color[i] : 0xFF606060u);
        PUSH32(esp, object); PUSH32(esp, 0);
        sub_000DCD30();
        esp += 8;
    }
}

static uint32_t s_controller_settings_screen;
static unsigned s_controller_settings_revision;
static void controller_touch_text(uint32_t screen)
{
    InputMode mode = input_mode_get();
    s_controller_settings_screen = screen;
    s_controller_settings_revision = mode.revision;
    fe_set_text(screen, "OptionName_5", "SENSOR MODE:");
    fe_set_text(screen, "OptionData_5", mode.sensor_enabled ? "On" : "Off");
    fe_set_text(screen, "OptionName_6", "RACE BUTTON OVERLAY:");
    fe_set_text(screen, "OptionData_6", mode.overlay_requested ? "On" : "Off");
    native_option_row_enabled(&s_controller_touch_rows[0], screen, 5, mode.sensor_editable);
    native_option_row_enabled(&s_controller_touch_rows[1], screen, 6, mode.overlay_editable);
}
void sub_000C2B80(void)
{
    uint32_t screen = ecx;
    sub_000C2B80_gen();
    memset(s_controller_touch_rows, 0, sizeof(s_controller_touch_rows));
    for (unsigned row = 5; row <= 6; row++) {
        PUSH32(esp, 0); PUSH32(esp, 1); PUSH32(esp, row);
        ecx = screen; PUSH32(esp, 0); sub_000C1D30();
        PUSH32(esp, 0); PUSH32(esp, row);
        ecx = screen; PUSH32(esp, 0); sub_000C1E10();
        MEM32(screen + 0x3C + 4 * (row - 1)) = SENSOR_HANDLER_VA + row - 5;
    }
    PUSH32(esp, 1); PUSH32(esp, 0); PUSH32(esp, 7);
    ecx = screen; PUSH32(esp, 0); sub_000C1D30();
    MEM32(screen + 0x54) = SENSOR_HANDLER_VA + 2;
    fe_set_text(screen, "OptionName_7", "TOUCH CONTROL LAYOUT");
    controller_touch_text(screen);
}
static void touch_layout_input(void)
{
    uint32_t msg = MEM32(esp + 4);
    /* Native options route Select as MouseClick, alongside arrow events. */
    if (msg == 0x0C407210u || msg == FE_PAD_LEFT || msg == FE_PAD_RIGHT)
        race_touch_request_editor();
    esp += 8;
}
static void controller_touch_input(unsigned row)
{
    uint32_t screen = ecx, msg = MEM32(esp + 4);
    if (msg == FE_PAD_LEFT || msg == FE_PAD_RIGHT) {
        InputMode mode = input_mode_get();
        InputSensorPreference preference = mode.preference;
        int overlay = mode.overlay_requested;
        if (row == 0) {
            if (!mode.sensor_editable) { esp += 8; return; }
            preference = mode.sensor_enabled ? INPUT_SENSOR_OFF : INPUT_SENSOR_ON;
        } else {
            if (!mode.overlay_editable) { esp += 8; return; }
            overlay = !overlay;
        }
        input_mode_configure(preference, overlay);
        options_save();
        controller_touch_text(screen);
    }
    esp += 8;
}
/* Called on the guest render thread. Refresh only on a policy change and
 * only while this exact Controller screen is still registered. */
void nfsu_controller_settings_refresh(void)
{
    InputMode mode = input_mode_get();
    if (!s_controller_settings_screen || mode.revision == s_controller_settings_revision ||
        MEM32(0x2BBB54) != 3) return;
    uint32_t reg = MEM32(0x2BCB9C);
    for (unsigned n = 0; reg && reg != 0x2BCB9C && n < 64; n++, reg = MEM32(reg)) {
        if (MEM32(reg - 4 + 0x14) != s_controller_settings_screen) continue;
        uint32_t package = MEM32(reg - 4 + 0x18);
        if (!package || !(MEM32(package + 0x1C) & 1)) continue;
        uint32_t saved_eax = eax, saved_ecx = ecx, saved_edx = edx, saved_esp = esp;
        controller_touch_text(s_controller_settings_screen);
        eax = saved_eax; ecx = saved_ecx; edx = saved_edx; esp = saved_esp;
        return;
    }
}
static void sensor_mode_input(void) { controller_touch_input(0); }
static void race_overlay_input(void) { controller_touch_input(1); }
#endif

/* Row input (thiscall, the message, ret 4), as sub_000C0C90: left/right
 * step the value (wrapping), then the texts and the arrow sound. */
static void opt_row_input(int i)
{
    uint32_t screen = ecx, msg = MEM32(esp + 4);
    const OptRow *o = &s_rows[i];

    if (msg == FE_PAD_LEFT || msg == FE_PAD_RIGHT) {
        *o->value = (*o->value + (msg == FE_PAD_RIGHT ? 1 : o->n - 1)) % o->n;
        o->changed();
        options_save();
        options_log();
        opt_row_text(screen, i);
        PUSH32(esp, 0xBu);
        PUSH32(esp, 0xAu);
        PUSH32(esp, msg);
        PUSH32(esp, ROW_HANDLER_VA);
        sub_000DDDE0();
        esp += 12;
        PUSH32(esp, eax);
        PUSH32(esp, MEM32(screen + 0xCu));
        PUSH32(esp, ROW_HANDLER_VA);
        sub_000DDD60();
        esp += 8;
    }
    esp += 8;
}

static void opt_row_input_0(void) { opt_row_input(0); }
#ifdef NFSU2_VULKAN
static void opt_row_input_1(void) { opt_row_input(1); }
static void opt_row_input_2(void) { opt_row_input(2); }
static void opt_row_input_3(void) { opt_row_input(3); }
static void opt_row_input_4(void) { opt_row_input(4); }
static void opt_row_input_5(void) { opt_row_input(5); }
static void opt_row_input_6(void) { opt_row_input(6); }
#else
static void opt_row_input_1(void) { opt_row_input(1); }
static void opt_row_input_2(void) { opt_row_input(2); }
#endif

/* Race render (cdecl, no arguments): cube-face views off for the call. */
void sub_00016050(void)
{
    uint8_t keep[6];
    uint32_t native_blur = MEM32(0x00260CC0u);
    uint32_t blur_limit = MEM32(0x0021C0B4u);
    int n;

    /* 11100/11140/11150 set the native blur request. 286E0 smooths it
     * through 264FA4. 286E0 adds speed blur and clamps its intensity to
     * 21C0B4 before drawing. Zero the cap as well to disable both sources while
     * disabled, preserving the scripted request for re-enabling. */
    if (!s_blur) {
        MEM32(0x00260CC0u) = 0;
        MEM32(0x00264FA4u) = 0;
        MEM32(0x0021C0B4u) = 0;
    }

    /* NFSU1 XDK presentation interval (0x1DE94C): initialization in
     * sub_000156F0 requests 0x80000002, and D3D uses the low bits as
     * the vblank count per flip. Keep its immediate-when-late flag and
     * change only the normal one/two-vblank intervals. Do not alter the
     * 60 Hz kernel clock, simulation time, or special D3D intervals.
     * Reapply at each race render so device resets cannot lose the option. */
    {
        uint32_t interval = MEM32(0x001DE94Cu);
        uint32_t count = interval & 0x7FFFFFFFu;
        if (count == 1 || count == 2)
            MEM32(0x001DE94Cu) = (interval & 0x80000000u) | (s_fps60 ? 1u : 2u);
    }

    if (s_refl) {
        sub_00016050_gen();
        if (!s_blur) {
            MEM32(0x00260CC0u) = native_blur;
            MEM32(0x0021C0B4u) = blur_limit;
        }
        return;
    }
    for (n = 0; n < 6; n++) {
        keep[n] = MEM8(ENVMAP_VIEW(10 + n) + 8u);
        MEM8(ENVMAP_VIEW(10 + n) + 8u) = 0;
    }
    sub_00016050_gen();
    for (n = 0; n < 6; n++)
        MEM8(ENVMAP_VIEW(10 + n) + 8u) = keep[n];
    if (!s_blur) {
        MEM32(0x00260CC0u) = native_blur;
        MEM32(0x0021C0B4u) = blur_limit;
    }
}

/* ── Menu Size: full screen by default ───────────────────────
 *
 * Options -> Display -> Menu Size (0.84..1.0, the game's default 0.92,
 * float at 0x295B00, kept in the profile) shrinks the front end toward
 * the centre for TV overscan: the FE render sub_00016460 scales view 0
 * by it. The loading screen's backdrop is about 680 wide and sits 13
 * pixels right of centre, so at 0.92 its left edge comes on screen and a
 * strip of the previous frame (the garage car) shows there; movies get
 * borders. A handheld or a modern TV has no overscan, so the default is
 * taken as 1.0: before each FE frame 0.92 becomes 1.0 unless the slider
 * (sub_000C1860, Menu Size's row input) set it this session. */
#define NFSU1_MENU_SIZE 0x00295B00u

extern void sub_00016460_gen(void);
extern void sub_000C1860_gen(void);
static uint32_t s_menu_size_user;       /* bits the slider last stored */

void sub_00016460(void)
{
    uint32_t v = MEM32(NFSU1_MENU_SIZE);
    int previous = s_ui_aspect_active;
    float f;

    memcpy(&f, &v, 4);
    if (v != s_menu_size_user && f > 0.919f && f < 0.921f)
        MEM32(NFSU1_MENU_SIZE) = 0x3F800000u;   /* 1.0f */
    s_ui_aspect_active = horplus_on() && MEM32(NFSU1_GAMEFLOW_STATE) == 6;
    sub_00016460_gen();
    s_ui_aspect_active = previous;
}

void sub_000C1860(void)
{
    sub_000C1860_gen();
    s_menu_size_user = MEM32(NFSU1_MENU_SIZE);
}

/* ── Model binding nodes: no unlink through fill ─────────────
 *
 * A 24-byte node {next, prev, name hash, solid, tables, count} binds a
 * model to its solid: sub_0011BC50 links it into the solid's list (+0x88)
 * or the unbound list at 0x2C25D0, sub_0011BC20 (thiscall, ecx = node,
 * ret) unlinks it when [8] or [0xC] is set, then clears both. A console
 * run (2026-10-11) crashed after a finished race and its autosave: the
 * race teardown (sub_00035C10 -> sub_0012E4A0 -> destructor sub_0012CB20,
 * 120 nodes at +0xE6C, element destructor 0x3ACC0 -> here) met a node
 * whose prev was 0xAAAAAAAA, the allocator's fill (sub_001243E0), and
 * wrote through it. Seen once. A node whose neighbours are outside guest
 * RAM is now cleared without the unlink, and logged; other broken links
 * are logged and unlinked as before. */
extern void sub_0011BC20_gen(void);

static int node_ptr_ok(uint32_t a)
{
    return !(a & 3u) && ((a >= 0x00010000u && a < 0x04000000u) ||
                         (a >= 0x80000000u && a < 0x84000000u));
}

void sub_0011BC20(void)
{
    static int logged;
    uint32_t n = ecx;
    uint32_t next, prev;

    if (!node_ptr_ok(n) || !(MEM32(n + 8u) | MEM32(n + 0xCu))) {
        sub_0011BC20_gen();
        return;
    }
    next = MEM32(n);
    prev = MEM32(n + 4u);
    if (node_ptr_ok(next) && node_ptr_ok(prev)) {
        if ((MEM32(prev) != n || MEM32(next + 4u) != n) && logged < 32) {
            logged++;
            fprintf(stderr, "[node] %08X: next %08X (prev %08X) prev %08X (next %08X),"
                    " hash %08X solid %08X, from %08X, state %u\n", n, next,
                    MEM32(next + 4u), prev, MEM32(prev), MEM32(n + 8u),
                    MEM32(n + 0xCu), MEM32(esp), MEM32(NFSU1_GAMEFLOW_STATE));
        }
        sub_0011BC20_gen();
        return;
    }
    if (logged < 32) {
        logged++;
        fprintf(stderr, "[node] %08X: next %08X prev %08X hash %08X solid %08X,"
                " from %08X, state %u: links outside RAM, cleared without unlink\n",
                n, next, prev, MEM32(n + 8u), MEM32(n + 0xCu), MEM32(esp),
                MEM32(NFSU1_GAMEFLOW_STATE));
    }
    MEM32(n + 0x8u) = 0;
    MEM32(n + 0xCu) = 0;
    MEM32(n + 0x10u) = 0;
    MEM32(n + 0x14u) = 0;
    esp += 4;                                   /* ret */
}

/* ── Manual function overrides ─────────────────────────────── */

/*
 * Return a function pointer to override the given Xbox VA, or NULL
 * to fall through to the auto-generated dispatch table.
 *
 * This is called on every indirect call (RECOMP_ICALL) and every
 * direct call through the dispatch table, so keep it fast. A chain
 * of if-statements on uint32_t compiles to a simple comparison
 * sequence; for large override tables, consider a sorted array
 * with binary search.
 *
 * Examples of common override patterns:
 *
 *   // Trace wrapper: log entry/exit around the generated function
 *   extern void sub_00012345(void);
 *   static void traced_sub_00012345(void) {
 *       fprintf(stderr, "[TRACE] sub_00012345 entered, eax=0x%08X\n", g_eax);
 *       sub_00012345();
 *       fprintf(stderr, "[TRACE] sub_00012345 returned, eax=0x%08X\n", g_eax);
 *   }
 *
 *   // Stub: skip a function entirely (return 0 in eax)
 *   static void stub_00067890(void) {
 *       g_eax = 0;
 *   }
 *
 *   // Fix: replace a broken lifted function with correct C
 *   static void fixed_sub_000ABCDE(void) {
 *       // Read arguments from stack/registers per calling convention
 *       uint32_t arg1 = g_ecx;
 *       uint32_t arg2 = MEM32(g_esp + 4);
 *       // ... correct implementation ...
 *       g_eax = result;
 *   }
 */
recomp_func_t recomp_lookup_manual(uint32_t xbox_va)
{
#ifdef __ANDROID__
    if (xbox_va == 0x000DDB60u) return nfsu_touch_hit;
    if (xbox_va == SENSOR_HANDLER_VA) return sensor_mode_input;
    if (xbox_va == SENSOR_HANDLER_VA + 1) return race_overlay_input;
    if (xbox_va == SENSOR_HANDLER_VA + 2) return touch_layout_input;
#endif
    /*
     * TODO: Add your overrides here. Examples:
     *
     * if (xbox_va == 0x00012345) return traced_sub_00012345;
     * if (xbox_va == 0x00067890) return stub_00067890;
     * if (xbox_va == 0x000ABCDE) return fixed_sub_000ABCDE;
     */

    if (xbox_va - ROW_HANDLER_VA < (uint32_t)N_ROWS) {
        static const recomp_func_t row_input[] = {
            opt_row_input_0,
#ifdef NFSU2_VULKAN
            opt_row_input_1, opt_row_input_2, opt_row_input_3, opt_row_input_4,
            opt_row_input_5, opt_row_input_6,
#else
            opt_row_input_1, opt_row_input_2,
#endif
        };
        return row_input[xbox_va - ROW_HANDLER_VA];
    }
    return (recomp_func_t)0;
}

/* ── ICALL failure logging ─────────────────────────────────── */

/*
 * Called when RECOMP_ICALL cannot resolve a target address.
 * This usually means one of:
 *   - A vtable dispatch to an address not in the dispatch table
 *   - A function pointer loaded from uninitialized or corrupt memory
 *   - A kernel thunk address that the bridge doesn't handle
 *
 * During early bring-up you will see many of these. Most are harmless
 * (the ICALL macro pops the dummy return address and continues).
 * Focus on the ones that cause crashes or incorrect behavior.
 */
void recomp_icall_fail_log(uint32_t va)
{
    fprintf(stderr, "[ICALL] Failed to resolve VA 0x%08X (total calls: %llu)\n",
            va, (unsigned long long)g_icall_count);

    /* Dump last 16 call targets from the ring buffer */
    fprintf(stderr, "  Recent ICALL targets:\n");
    for (int i = 0; i < 16; i++) {
        int idx = (g_icall_trace_idx - 16 + i) & 15;
        if (g_icall_trace[idx])
            fprintf(stderr, "    [%2d] 0x%08X\n", i, g_icall_trace[idx]);
    }
    fflush(stderr);
}

/* An indirect call whose target is not code: a null or wild function pointer.
 *
 * Skipping these is right -- calling a data address is worse -- but skipping
 * them *silently* is not. They almost always arrive inside a loop, so the
 * symptom is a hang with no output rather than a diagnosable null vtable call.
 *
 * Rate-limited per address: a spin can produce millions of these, and the
 * useful information is which addresses occur, not how often.
 */
void recomp_icall_not_code_log(uint32_t va)
{
    enum { SLOTS = 16 };
    static uint32_t seen[SLOTS];
    static uint64_t hits[SLOTS];
    static int count;
    int i;

    for (i = 0; i < count; i++)
        if (seen[i] == va)
            break;
    if (i == count) {
        if (count == SLOTS)
            return;
        seen[count] = va;
        hits[count] = 0;
        count++;
    }
    hits[i]++;
    /* Report at 1, 10, 100, 1000 ... rather than once. A single line says a
     * wild pointer was skipped; the progression says it is being skipped in a
     * loop, which is the difference between a curiosity and the reason the
     * title is hung. */
    {
        uint64_t n = hits[i];
        while (n >= 10 && n % 10 == 0)
            n /= 10;
        if (n != 1)
            return;
    }
    fprintf(stderr, "[ICALL] target 0x%08X is not code -- skipped %llu time(s) "
                    "(null or wild function pointer, at call #%llu)\n",
            va, (unsigned long long)hits[i],
            (unsigned long long)g_icall_count);
    fflush(stderr);
}

/* ── Untranslated instructions ───────────────────────────────────────────
 *
 * The lifter emits RECOMP_UNIMPL(text, va) at every instruction it has no
 * translation for, in place of the bare comment it used to leave. The
 * instruction is still a no-op; this only stops the omission being silent.
 * RECOMP_UNIMPL_TRAP=1 aborts at the first hit, at the guest address of the
 * cause rather than wherever the damage surfaces. */
#include <stdlib.h>

void recomp_unimpl(const char *text, uint32_t va)
{
    static int printed;
    const char *trap = getenv("RECOMP_UNIMPL_TRAP");
    int stop = trap && *trap && *trap != '0';

    if (printed < 50 || stop) {
        printed++;
        fprintf(stderr,
                "[UNIMPL] untranslated instruction REACHED: `%s` at 0x%08X"
                " (a no-op; set RECOMP_UNIMPL_TRAP=1 to stop here)\n",
                text, va);
        fflush(stderr);
    }
    if (stop) abort();
}
