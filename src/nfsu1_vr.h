#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Xbox NTSC-U, default.xbe SHA-256 f4b3dc57...6b34b22. These are NFSU1
 * addresses, verified against the local executable, never NFSU2 offsets. */
#define NFSU1_FLOW 0x00283434u
#define NFSU1_VIEWS 0x002C5F20u
#define NFSU1_VIEW(id) (NFSU1_VIEWS + (id) * 0x60u)
#define NFSU1_CAMERA_MOVER_VTABLE 0x001F8FB8u
#define NFSU1_FENG_REGISTRY 0x002BCB9Cu
#define NFSU1_FRONTEND 0x002BCBB8u
#define NFSU1_PAUSE_SCREEN 0x001FD734u
#define NFSU1_COUNTDOWN_SCREEN 0x001F9404u

typedef int (*Nfsu1Read)(void *, uint32_t, void *, size_t);
static inline int nfsu1_address(uint32_t address, size_t size)
{
    uint64_t end = (uint64_t)address + size;
    return (address >= 0x10000u && end <= 0x04000000u) ||
           (address >= 0x80000000u && end <= 0x84000000u);
}

typedef struct {
    uint32_t camera, mover, manager, car, rear;
    int rear_view_empty;
} Nfsu1PlayerCamera;

/* Resolve the current player from the live main-view list, not a pointer
 * retained across pause, restart or car destruction. 8AEB0/862C0 link the
 * mover at +4; 3D755 sets its manager at +20. A rear mover at manager+288
 * is independent of the saved HUD option (A0140 can remove it on pause). */
static inline int nfsu1_player_camera(Nfsu1Read read, void *context, Nfsu1PlayerCamera *out)
{
    uint32_t flow, view[24], mover[12], manager[164], rear_head;
    memset(out, 0, sizeof *out);
    if (!read(context, NFSU1_FLOW, &flow, 4) || flow != 6 ||
        !read(context, NFSU1_VIEW(1), view, sizeof view) || view[1] != 1 ||
        !nfsu1_address(view[16], 0x28c) || view[17] == NFSU1_VIEW(1)+0x44 ||
        view[17] < 4 || !nfsu1_address(view[17]-4, sizeof mover) ||
        !read(context, view[17]-4, mover, sizeof mover) ||
        mover[0] != NFSU1_CAMERA_MOVER_VTABLE || mover[4] != 1 ||
        mover[6] != NFSU1_VIEW(1) || mover[7] != view[16] ||
        !nfsu1_address(mover[8], sizeof manager) ||
        !read(context, mover[8], manager, sizeof manager) || manager[4] != 0 ||
        manager[6] != 1 || manager[7] != 3 ||
        manager[0x27c/4] != view[17]-4 || manager[1] != mover[11] ||
        !nfsu1_address(mover[11], 0x534) ||
        !read(context, NFSU1_VIEW(3)+0x44, &rear_head, 4)) return 0;
    out->camera=view[16]; out->mover=view[17]-4; out->manager=mover[8];
    out->car=mover[11]; out->rear=manager[0x288/4];
    out->rear_view_empty=rear_head==NFSU1_VIEW(3)+0x44;
    return 1;
}

/* 12C420 selects the rendered pose using Car+530's view bit: rotation +90
 * and origin +50, or the alternate pair +4e0/+520. Car+60 is a different
 * position and cannot anchor the cockpit to the geometry being drawn. */
static inline int nfsu1_render_pose(Nfsu1Read read, void *context, uint32_t car,
                                  unsigned view, float rotation[12], float position[3])
{
    uint32_t flags;
    if(view>=32 || !nfsu1_address(car,0x534) ||
       !read(context,car+0x530,&flags,4))return 0;
    int alternate=(flags&(1u<<view))!=0;
    return read(context,car+(alternate?0x4e0:0x90),rotation,48) &&
           read(context,car+(alternate?0x520:0x50),position,12);
}

static inline int nfsu1_ui_name(const char *name, const char *expected)
{
    while (*name && *expected) {
        if ((*name++ | 32) != (*expected++ | 32)) return 0;
    }
    return *name == *expected;
}

/* DBDC0 passes frontend+8 to D7910. D7910 draws the active package chain
 * at manager+0xe4, following package+4; package+0xc is its filename.
 * The registry includes cached packages. Package+0x1c is a CONTROLLER MASK
 * (D7C70), not visibility: a real paused Quest capture has mask 4.
 * Return -1 on invalid data so the caller retains the complete menu plane. */
static inline int nfsu1_vr_menu(Nfsu1Read read, void *context, int *paused)
{
    uint32_t frontend, manager, node;
    int active = 0, pause = 0, overlay = 0;
    *paused = 0;
    if (!read(context, NFSU1_FRONTEND, &frontend, 4)) return -1;
    if (!frontend) return 0;
    if (!nfsu1_address(frontend, 12) ||
        !read(context, frontend + 8, &manager, 4) ||
        !nfsu1_address(manager, 0xe8) ||
        !read(context, manager + 0xe4, &node, 4)) return -1;
    for (unsigned count = 0; node && count < 64; ++count) {
        uint32_t fields[4];
        char name[64];
        if (!nfsu1_address(node, sizeof fields) ||
            !read(context, node, fields, sizeof fields) ||
            !nfsu1_address(fields[3], sizeof name)) return -1;
        unsigned i;
        for (i = 0; i < sizeof name; ++i) {
            if (!read(context, fields[3] + i, &name[i], 1)) return -1;
            if (!name[i]) break;
        }
        if (!i || i == sizeof name) return -1;
        int hud = i >= 4 && (name[0] | 32) == 'h' &&
                  (name[1] | 32) == 'u' && (name[2] | 32) == 'd' && name[3] == '_';
        if (!hud && !nfsu1_ui_name(name, "PreRaceStats.fng")) {
            active = 1;
            if (nfsu1_ui_name(name, "MU_Pause.fng")) pause = 1;
            else overlay = 1;
        }
        node = fields[1];
    }
    if (node) return -1;
    /* Options/dialogs may leave the parent pause package visible. They must
     * keep their own X action and must not open our settings panel. */
    *paused = pause && !overlay;
    return active;
}
