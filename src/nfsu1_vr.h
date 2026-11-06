#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "../quest/runtime/quest_cockpit_ui.h"

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

/* The transport owns the controller's embedded music state at +1b0. */
static inline uint32_t nfsu1_music_transport(Nfsu1Read read, void *context, uint32_t controller)
{
    uint32_t transport, header[2];
    if (!nfsu1_address(controller, 0x2b0) || !read(context, controller+0x14, &transport, 4) ||
        !nfsu1_address(transport, 0x618) || !read(context, transport, header, sizeof header) ||
        header[0] != 0x203500 || header[1] != controller+0x1b0) return 0;
    return transport;
}

/* The music bank and ambience both use the streamed float decoder (1AF3C0).
 * At 1AF510 +30 is the stream slot and +38 the channel. 1A86B0 resolves
 * the slot to its ring. Its callback state points back to the file reader.
 * Identify the AST by its owned filename before taking samples or pausing. */
static inline int nfsu1_music_channel(Nfsu1Read read, void *context, uint32_t decoder)
{
    uint32_t slot, ring, state, reader, file, back, handle, state_handle;
    unsigned char channel, slots;
    char path[sizeof "d:\\sound\\streams\\STRM_Music_UG0_MB.ast"];
    if (!nfsu1_address(decoder, 0x39) ||
        !read(context,decoder+0x30,&slot,4) || !read(context,decoder+0x38,&channel,1) ||
        !read(context,0x2ddab6,&slots,1) || slot>=slots || channel>1 ||
        !read(context,0x2de0b4+4*slot,&ring,4) || !nfsu1_address(ring,0x60) ||
        !read(context,ring,&handle,4) || handle==UINT32_MAX ||
        !read(context,ring+0x5c,&state,4) || !nfsu1_address(state,12) ||
        !read(context,state+4,&state_handle,4) || handle!=state_handle ||
        !read(context,state,&reader,4) || !nfsu1_address(reader,4) ||
        !read(context,reader,&file,4) || !nfsu1_address(file,0x88+sizeof path) ||
        !read(context,file+0x34,&back,4) || back!=reader ||
        !read(context,file+0x88,path,sizeof path) || path[sizeof path-1]) return -1;
    static const char music[]="d:\\sound\\streams\\strm_music_ug0_mb.ast";
    for (size_t i=0;i<sizeof music-1;++i)
        if ((path[i]|32)!=(music[i]|32)) return -1;
    return channel;
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
static inline int nfsu1_vr_menu(Nfsu1Read read, void *context, int *settings_access)
{
    uint32_t frontend, manager, node;
    int active = 0, pause = 0, options = 0, overlay = 0;
    *settings_access = QUEST_SETTINGS_CLOSED;
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
        /* The countdown draws the EA TRAX chyron over the race HUD. */
        int chyron = nfsu1_ui_name(name, "Chyron.fng") || nfsu1_ui_name(name, "Chyron_alpine.fng") ||
                     nfsu1_ui_name(name, "Chyron_audiobahn.fng") || nfsu1_ui_name(name, "Chyron_kenwood.fng");
        if (!hud && !chyron && !nfsu1_ui_name(name, "PreRaceStats.fng") &&
            !nfsu1_ui_name(name, "drag_HUD_test.fng") && !nfsu1_ui_name(name, "2PHudTop.fng") &&
            !nfsu1_ui_name(name, "2PHudBot.fng")) {
            active = 1;
            if (nfsu1_ui_name(name, "MU_Pause.fng")) pause = 1;
            else if (nfsu1_ui_name(name, "MU_Options.fng") || nfsu1_ui_name(name, "MU_OptionsSelector.fng") ||
                     nfsu1_ui_name(name, "MU_PauseOptions.fng") || nfsu1_ui_name(name, "MU_PauseOptionsSelector.fng") ||
                     nfsu1_ui_name(name, "MU_OptionsHUD.fng"))
                options = 1;
            else if (!nfsu1_ui_name(name, "MU_Main.fng") && !nfsu1_ui_name(name, "background.fng") &&
                     !nfsu1_ui_name(name, "GarageMain.fng"))
                overlay = 1;
        }
        node = fields[1];
    }
    if (node) return -1;
    /* Parent packages can stay in the active chain behind the options.
     * Dialogs and controller bindings must retain exclusive input. */
    if (!overlay && options) *settings_access = QUEST_SETTINGS_OPTIONS;
    else if (!overlay && pause) *settings_access = QUEST_SETTINGS_PAUSE;
    return active;
}
