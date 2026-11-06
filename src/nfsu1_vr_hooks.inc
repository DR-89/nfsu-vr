/* Included after recomp_funcs.h; guest access occurs only on the guest thread. */
#include "nfsu1_vr.h"
#include "quest_xr.h"
#include "quest_shifter.h"
#include "quest_cockpit_ui.h"
#include "quest_haptics.h"
#include "vr_music.h"
#include "quest_culling.h"
#include "quest_cockpit.h"
#include "quest_cockpit_vehicle.h"
#include "quest_frame_timing.h"
#include "nfsu1_timing.h"

static int nfsu1_vr_timing_mode(void)
{
    const char *fast = getenv("NFSU2_VR_60FPS");
    unsigned rate = quest_refresh_option(getenv("NFSU2_VR_REFRESH_HZ"), getenv("NFSU2_VR_72FPS"));
    return nfsu1_timing_mode(quest_xr_enabled(), MEM32(NFSU1_FLOW), rate, fast && *fast == '1');
}
/* Called only at the mapped lower-bound reads in 1410D0 and command decode
 * in 1D9EB0. Keep the game's maximum timestep and recording/playback intact. */
float nfsu1_vr_minimum(float original)
{
    return nfsu1_timing_minimum(original, nfsu1_vr_timing_mode());
}
float nfsu1_vr_carry(float elapsed, double rounded, unsigned int ticks)
{
    return nfsu1_timing_carry(elapsed, rounded, (double)(int32_t)ticks * MEMF(0x1FB030), nfsu1_vr_timing_mode());
}
unsigned int nfsu1_vr_present(unsigned int command)
{
    return nfsu1_timing_present(command, nfsu1_vr_timing_mode());
}

static uint32_t nfsu1_cockpit_car, nfsu1_grid_auto_car;
static uint32_t nfsu1_mirror_owner, nfsu1_mirror_mover;
static int nfsu1_vr_read(void *context, uint32_t address, void *out, size_t size)
{
    (void)context;
    if (!nfsu1_address(address, size)) return 0;
    memcpy(out, (const void *)XBOX_PTR(address), size);
    return 1;
}

static uint32_t nfsu1_guest_call(void (*function)(void),uint32_t self,uint32_t continuation,int argc,uint32_t arg,uint32_t second)
{
    uint32_t regs[]={eax,ecx,edx,ebx,esi,edi,esp,g_ebp,g_seh_ebp,g_fs_base};
    RecompXmm *xs[]={&g_xmm0,&g_xmm1,&g_xmm2,&g_xmm3,&g_xmm4,&g_xmm5,&g_xmm6,&g_xmm7},x[8];
    RecompMmx *ms[]={&g_mm0,&g_mm1,&g_mm2,&g_mm3,&g_mm4,&g_mm5,&g_mm6,&g_mm7},m[8];
    double fp[8];memcpy(fp,g_fp_stack,sizeof fp);
    int top=g_fp_top,cmp=g_fp_cmp,df=g_df;uint16_t cc=g_fp_cc,cw=g_fp_control_word;
    for(int i=0;i<8;++i){x[i]=*xs[i];m[i]=*ms[i];}
    ecx=self;if(argc==2)PUSH32(esp,second);if(argc)PUSH32(esp,arg);
    PUSH32(esp,continuation);function();
    uint32_t result=eax;
    eax=regs[0];ecx=regs[1];edx=regs[2];ebx=regs[3];esi=regs[4];edi=regs[5];esp=regs[6];
    g_ebp=regs[7];g_seh_ebp=regs[8];g_fs_base=regs[9];g_df=df;
    for(int i=0;i<8;++i){*xs[i]=x[i];*ms[i]=m[i];}
    memcpy(g_fp_stack,fp,sizeof fp);g_fp_top=top;g_fp_cmp=cmp;g_fp_cc=cc;g_fp_control_word=cw;
    return result;
}

#include "vr_replay.h"

static VrReplay nfsu1_recording;
static uint32_t nfsu1_restored_car, nfsu1_restored_mover, nfsu1_replay_owner;
static int nfsu1_replay_slot = -1, nfsu1_previous_stage;
static int nfsu1_stage(void)
{
    uint32_t director = MEM32(0x265b18);
    return nfsu1_address(director, 4) ? (int)MEM32(director) : 0;
}

static uint32_t nfsu1_player(void)
{
    uint32_t manager = MEM32(0x265b84);
    return MEM32(NFSU1_FLOW) == 6 && nfsu1_address(manager, 0x5e0) &&
           !MEM32(manager + 0x10) && nfsu1_address(MEM32(manager + 4), 0x534) ? manager : 0;
}

static uint32_t nfsu1_replay(void)
{
    uint32_t recorder = MEM32(0x2c8600);
    return nfsu1_address(recorder, 0x68) ? recorder : 0;
}

static VrReplayFrame *nfsu1_replay_frame(uint32_t recorder)
{
    return vr_replay_find(&nfsu1_recording, recorder, MEM32(recorder+0x64), MEM32(recorder+0x50),
                          MEM32(recorder+0x5c), MEM32(0x25315c));
}

static unsigned nfsu1_last_slot(void)
{
    return MEM8(0x273785) || MEM32(0x265bc4) ? 3 : 2;
}

static void nfsu1_restore_camera(uint32_t manager, uint32_t mover, uint32_t car)
{
    if (!manager || MEM32(manager+0x27c) != mover || MEM32(manager+4) != car ||
        nfsu1_replay_owner || (nfsu1_restored_mover == mover && nfsu1_restored_car == car) ||
        !quest_xr_cockpit_enabled()) return;
    int choice = quest_xr_camera_choice();
    unsigned last = nfsu1_last_slot(), slot = MEM32(0x295b68);
    if (choice < 0 && slot <= last) quest_xr_camera_remember((int)slot);
    if (choice >= 0) {
        unsigned desired = (unsigned)choice > last ? last : (unsigned)choice;
        unsigned mode = MEM8(manager+0x2c+desired);
        if (mode > 5 || mode == 3) mode = 0;
        if (slot != desired || MEM8(manager+0x24) != mode) {
            nfsu1_guest_call(sub_00038E50, manager, 0x3bba9, 1, mode, 0);
            MEM8(manager+0x24) = (uint8_t)mode;
            MEM32(0x295b68) = desired;
            MEM8(0x2737c8) = MEM8(0x295b58+4*desired);
        }
        quest_xr_cockpit_select(choice >= 5);
    }
    nfsu1_restored_car = car; nfsu1_restored_mover = mover;
}

/* Director states 5/6 own replay. The camera manager's case 17 constructs
 * the same 8FDB0 track mover used by the human driver (3CF2C). Leave its
 * cinematic sequence in charge until a camera click explicitly selects a view. */
static void nfsu1_replay_camera(uint32_t owner, uint32_t manager, unsigned steps)
{
    static const unsigned modes[] = {0, 1, 2, 4, 5};
    if (owner != nfsu1_replay_owner) {
        nfsu1_replay_slot = -1;
        nfsu1_restored_car = nfsu1_restored_mover = 0;
        nfsu1_replay_owner = owner;
        quest_xr_cockpit_select(!owner && quest_xr_camera_choice() >= 5);
    }
    if (!owner || !manager) return;
    unsigned count = quest_xr_cockpit_enabled() ? 6 : 5;
    if (steps) {
        unsigned previous = nfsu1_replay_slot < 0 ? 4 : (unsigned)nfsu1_replay_slot;
        nfsu1_replay_slot = (int)((previous + steps) % count);
    }
    if (nfsu1_replay_slot < 0) return;
    uint32_t mover = MEM32(manager+0x27c);
    if (!nfsu1_address(mover, 0x54) || MEM32(mover) != NFSU1_CAMERA_MOVER_VTABLE) {
        nfsu1_guest_call(sub_0003CAE0, manager, 0x3d7e9, 2, 17, 0);
        mover = MEM32(manager+0x27c);
    }
    if (!nfsu1_address(mover, 0x54) || MEM32(mover) != NFSU1_CAMERA_MOVER_VTABLE) return;
    int seated = nfsu1_replay_slot == 5;
    uint32_t mode = seated ? 2 : modes[nfsu1_replay_slot];
    if (MEM32(mover+0x40) != mode)
        nfsu1_guest_call(sub_000897E0, mover, 0x38e6a, 1, mode, 0);
    quest_xr_cockpit_select(seated);
}

/* 118610 assigns category 2 to both EA TRAX lanes. The race playlist
 * starts at bit one; ABK sound zero is the title stinger. */
static void nfsu1_radio_update(uint32_t owner, int driving)
{
    static uint32_t previous_transport, paused_transport;
    static int pending = -1;
    uint32_t system = MEM32(0x2bef9c), controller = 0, transport = 0;
    if (nfsu1_address(system, 0x108) && MEM32(system+8) == 0x295b1c) {
        controller = MEM32(system+0x104);
        transport = nfsu1_music_transport(nfsu1_vr_read, NULL, controller);
    }
    if (transport != previous_transport || !driving) {
        paused_transport = 0; pending = -1;
    }
    previous_transport = transport;
    float volume = MEMF(0x295b24);
    if (!isfinite(volume) || volume < 0 || volume > 1) volume = 0;
    QuestRadioSong song = {0};
    int paused = 0;
    VrRadioPlayback playback = vr_music_update(driving, 1, volume, &song, &paused);
    int hold = playback != VR_RADIO_ORIGINAL || paused_transport != 0;
    vr_music_hold_stock(hold);
    int track = transport ? (int32_t)MEM32(controller+0x2ac) : -1;
    if (track < 0 || track >= 26) track = -1;
    vr_music_stock_track(!hold && owner ? track : -1, 1, 2);
    if (track == pending) pending = -1;
    if (playback != VR_RADIO_CUSTOM && track >= 0) {
        char *fields[] = {song.title, song.artist};
        for (int i = 0; i < 2; ++i) {
            uint32_t addr = MEM32(0x24f020+16*track+4*i);
            if (!nfsu1_address(addr, 64)) continue;
            const char *str = (const char *)XBOX_PTR(addr);
            size_t len = 0;
            while (len < 63 && str[len]) ++len;
            if (len >= 2 && str[0] == '"' && str[len-1] == '"') { ++str; len -= 2; }
            memcpy(fields[i], str, len);
        }
        paused = paused_transport != 0;
    }
    QuestRadioCommand cmd = quest_xr_radio_exchange(transport || playback == VR_RADIO_CUSTOM ? owner : 0,
                                                     volume, paused, &song);
    if (!owner || !cmd.action) return;
    if (cmd.action == QUEST_RADIO_VOLUME) {
        if (isfinite(cmd.volume) && cmd.volume >= 0 && cmd.volume <= 1 &&
            quest_radio_volume_changed(volume, cmd.volume)) {
            MEMF(0x295b24) = cmd.volume;
            if (transport) nfsu1_guest_call(sub_00118E40, transport, 0, 0, 0, 0);
        }
    } else if (playback == VR_RADIO_CUSTOM) {
        vr_music_command(cmd.action);
    } else if (transport && cmd.action == QUEST_RADIO_PLAY) {
        paused_transport = paused_transport ? 0 : transport;
        vr_music_hold_stock(paused_transport != 0);
    } else if (transport && (cmd.action == QUEST_RADIO_NEXT || cmd.action == QUEST_RADIO_PREV)) {
        int frontend = MEM32(NFSU1_FLOW) == 3 || MEM32(controller+0x1b0) == 8;
        uint32_t remaining = frontend ? 0x295b50 : 0x295b54;
        uint32_t enabled = MEM32(frontend ? 0x2befac : 0x2befb0);
        int next = quest_radio_track(pending >= 0 ? pending : track, enabled >> 1,
                                     cmd.action == QUEST_RADIO_NEXT ? 1 : -1);
        if (next >= 0 && nfsu1_address(MEM32(transport+0x320), 0x78)) {
            /* Retail consumes bits during shuffle. An explicit previous/next
             * may select an enabled song that was already consumed. */
            MEM32(remaining) |= 1u << (next+1);
            paused_transport = 0;
            vr_music_hold_stock(0);
            uint32_t state = controller+0x1b0, mode = MEM32(state+0xf0);
            uint32_t resume_mode = mode == 99 ? MEM32(state+0xf4) : mode;
            nfsu1_guest_call(sub_000FA940, system, 0, 1, (uint32_t)next, 0);
            /* FA940 queues an EA TRAX menu preview. In a race 1191D2
             * looks up the playlist slot, then adds the bank offset itself.
             * Resume the race mode before that async dispatch runs. */
            if (!frontend) {
                for (unsigned slot=0; slot<26; ++slot) {
                    if (MEM32(0x295d78+8*slot) == (unsigned)next) {
                        MEM32(state+0xf8) = slot;
                        break;
                    }
                }
                MEM32(state+0xf0) = resume_mode;
            } else if (mode == 99) {
                /* A second click before the first preview has started must
                 * preserve the mode which playback restores afterwards. */
                MEM32(state+0xf4) = resume_mode;
            }
            pending = next;
        }
    }
}

static int nfsu1_manual_gears(uint32_t driver)
{
    uint32_t mode = MEM32(driver+0x284);
    return mode == 1 || mode == 3 || mode == 4;
}

static void nfsu1_apply_grid_gear(uint32_t driver, uint32_t car, int manual, int request)
{
    if (request != QUEST_SHIFT_NONE) {
        MEM32(driver+0x1d4) = (uint32_t)request;
    } else if (!manual && nfsu1_grid_auto_car == car) {
        /* Leaving the cockpit during the countdown cancels the preselection. */
        uint32_t previous = MEM32(driver+0x1d4);
        if (previous == QUEST_SHIFT_N || previous == QUEST_SHIFT_FIRST) MEM32(driver+0x1d4) = UINT32_MAX;
    }
    nfsu1_grid_auto_car = !manual && request != QUEST_SHIFT_NONE ? car : 0;
}

static void nfsu1_grid(uint32_t driver, uint32_t car, int count)
{
    int stage = nfsu1_stage(), manual = nfsu1_manual_gears(driver);
    int request = quest_xr_shifter_stage(car, stage, nfsu1_previous_stage, count, manual);
    nfsu1_previous_stage = stage;
    nfsu1_apply_grid_gear(driver, car, manual, request);
}

static int nfsu1_gear_count(uint32_t car)
{
    uint32_t spec = MEM32(car+0x20);
    if (!nfsu1_address(spec, 0x1cc)) return 0;
    int count = (int)MEM32(spec+0x1c8);
    return count >= 4 && count <= 6 ? count : 0;
}

static unsigned nfsu1_grid_gauge_mask(uint32_t manager)
{
    if (!quest_xr_enabled() || !quest_shifter_before_start(nfsu1_stage()) || nfsu1_replay_owner ||
        manager != nfsu1_player() || !manager || !quest_xr_cockpit_requested()) return 0;
    int access;
    return nfsu1_vr_menu(nfsu1_vr_read, NULL, &access) == 0 ? 0x40u : 0;
}

unsigned nfsu1_vr_gauge_gear(unsigned car)
{
    if (car == nfsu1_cockpit_car && car && !nfsu1_replay_owner && quest_shifter_before_start(nfsu1_stage())) {
        int gear = quest_xr_shifter_grid_request(car);
        if (gear != QUEST_SHIFT_NONE) return (unsigned)gear;
    }
    return MEM32(car+0x3d8);
}

/* 33B62 links the driver's input object at Car+30. Some controllers
 * embed the same HumanDriver at +250 (64DE0). The director can keep its
 * input update locked during the grid, while the film camera keeps running. */
static void nfsu1_grid_refresh(uint32_t car, int stage)
{
    if (!car || (!quest_shifter_before_start(stage) && !quest_xr_shifter_grid_car()) ||
        !nfsu1_address(car, 0x3e4)) return;
    uint32_t driver = MEM32(car+0x30);
    if (nfsu1_address(driver, 0x510) && MEM32(driver) == 0x1f7800) driver += 0x250;
    if (nfsu1_address(driver, 0x2bc) && MEM32(driver) == 0x1f7900 && MEM32(driver+0x26c) == car)
        nfsu1_grid(driver, car, nfsu1_gear_count(car));
}

static void nfsu1_driver_update(void)
{
    uint32_t driver = ecx, manager = nfsu1_player();
    if (!manager || !nfsu1_address(driver, 0x2bc) || MEM32(driver+0x26c) != MEM32(manager+4)) {
        sub_00066820_gen(); return;
    }
    uint32_t car = MEM32(manager+4), recorder = nfsu1_replay();
    int playing = recorder && MEM32(recorder);
    VrReplayFrame *frame = recorder && (playing || MEM32(recorder+4)) ? nfsu1_replay_frame(recorder) : NULL;
    uint32_t before = MEM32(driver+0x1d4);
    if (playing && frame && frame->human_driver) MEM32(driver+0x1d4) = frame->gear_before;
    sub_00066820_gen();
    int raw_pedals = 0;
    if (playing) {
        if (frame && frame->human_driver) {
            MEM32(driver+0x1d4) = frame->gear_after;
            raw_pedals = frame->raw_pedals;
        }
    } else {
        int count = nfsu1_gear_count(car);
        nfsu1_grid(driver, car, count);
        if (car == nfsu1_cockpit_car || car == quest_xr_shifter_grid_car()) {
            int gear = (int)MEM32(car+0x3d8);
            int lock = MEM8(driver+0x278) ? QUEST_DRIVER_GRID_LOCK : QUEST_DRIVER_READY;
            if (MEM32(driver+0x270) == UINT32_MAX || MEM32(driver+0x27c)) lock = QUEST_DRIVER_NO_INPUT;
            int manual = nfsu1_manual_gears(driver);
            int request = quest_xr_shifter_driver(car, count, gear, manual, lock);
            if (request == QUEST_SHIFT_RESUME_AUTO) {
                nfsu1_grid_auto_car = 0;
                MEM32(driver+0x1d4) = UINT32_MAX;
            } else if (count && request >= 0 && request <= count+1 && lock == QUEST_DRIVER_READY) {
                raw_pedals = manual && (!request || !gear || MEM8(driver+0x2b0) || !MEM32(driver+0x1d4));
                MEM32(driver+0x1d4) = (uint32_t)request;
            } else if (count && car == quest_xr_shifter_grid_car() &&
                       (request == QUEST_SHIFT_N || request == QUEST_SHIFT_FIRST))
                MEM32(driver+0x1d4) = (uint32_t)request;
        }
        if (frame) {
            frame->human_driver = 1; frame->gear_before = before;
            frame->gear_after = MEM32(driver+0x1d4); frame->raw_pedals = (unsigned char)raw_pedals;
        }
    }
    if (raw_pedals) {
        MEM32(driver+0x1dc) = MEM32(driver+0x294);
        MEM32(driver+0x1e4) = MEM32(driver+0x29c);
        MEM32(car+0x3dc) = MEM32(driver+0x1dc);
    }
}

static void nfsu1_vr_publish_ui(void)
{
    int settings_access = QUEST_SETTINGS_CLOSED, race = MEM32(NFSU1_FLOW) == 6;
    int menu = nfsu1_vr_menu(nfsu1_vr_read, NULL, &settings_access);
    int paused = settings_access != QUEST_SETTINGS_CLOSED;
    static int previous = -1;
    int state = (race ? 8 : 0) | (menu != 0 ? 4 : 0) | settings_access;
    if (state != previous) {
        previous = state;
        fprintf(stderr, "[NFSU1 VR] UI race=%d menu=%d settings-access=%d (active package chain)\n", race, menu, settings_access);
    }
    uint32_t manager = race ? MEM32(0x265b84) : 0;
    uint32_t car = nfsu1_address(manager, 8) ? MEM32(manager + 4) : 0;
    uint32_t spec = nfsu1_address(car, 0x3e4) ? MEM32(car + 0x20) : 0;
    float engine = nfsu1_address(spec, 0x1fc) ? quest_engine_rumble(
        MEMF(car + 0x3e0), MEMF(spec + 0x1f0), MEMF(spec + 0x1f8), MEMF(car + 0x3dc)) : 0;
    int stage = race ? nfsu1_stage() : 0;
    uint32_t owner = race && (stage == 5 || stage == 6) ? nfsu1_replay() : 0;
    int driving = race && menu == 0 && !owner;
    quest_xr_driving_publish(driving, 0, driving ? engine : 0);
    int ui = owner && !paused && menu >= 0 ? QUEST_UI_REPLAY :
             (race && menu == 0 ? QUEST_UI_HUD : QUEST_UI_MENU);
    unsigned steps = quest_xr_cockpit_menu_publish(ui, ui == QUEST_UI_REPLAY ? owner : 0);
    if (quest_xr_enabled()) {
        nfsu1_replay_camera(owner, nfsu1_player(), steps);
        if (!owner) nfsu1_grid_refresh(car, stage);
        if (ui == QUEST_UI_REPLAY) quest_xr_take_replay_toggle(owner);
        int radio = driving || ui == QUEST_UI_REPLAY;
        nfsu1_radio_update(radio && quest_xr_cockpit_requested() ? car : 0, radio);
    }
    quest_xr_settings_access_publish(settings_access);
    if (!race) {
        if (nfsu1_previous_stage) quest_xr_shifter_stage(0, 0, 0, 0, 0);
        nfsu1_previous_stage = 0;
        nfsu1_grid_auto_car = 0;
        nfsu1_restored_car = nfsu1_restored_mover = 0;
        nfsu1_cockpit_car = 0;
        nfsu1_mirror_owner = nfsu1_mirror_mover = 0;
        quest_xr_cockpit_select(0);
        quest_xr_cockpit_publish(0, 0);
    }
}

static void nfsu1_vr_cull(uint32_t root, uint32_t view)
{
    if (!quest_xr_enabled() || !nfsu1_address(view, 0x60) ||
        !nfsu1_address(root, 0x120) || MEM32(NFSU1_FLOW) != 6) return;
    uint32_t id = MEM32(view + 4), camera = MEM32(view + 0x40);
    if ((id != 1 && id != 4) || !camera || camera != MEM32(NFSU1_VIEW(1) + 0x40)) return;
    float planes[6][4], world[6][4], matrix[16];
    if (quest_xr_culling_planes(planes)) {
        memcpy(matrix, (const void *)XBOX_PTR(root), sizeof matrix);
        if (quest_culling_world(world, planes, matrix))
            memcpy((void *)XBOX_PTR(root + 0xc0), world, sizeof world);
    }
}

static int nfsu1_vr_camera_begin(uint32_t camera, uint32_t mover, uint32_t target, float saved[16])
{
    int main = camera && camera == MEM32(NFSU1_VIEW(1) + 0x40);
    if (!main && nfsu1_cockpit_car && camera == MEM32(NFSU1_VIEW(3)+0x40) &&
        MEM32(esp) == 0x87644 && nfsu1_address(esi, 0x30) && MEM32(esi+0x1c) == camera &&
        MEM32(esi+0x2c) == nfsu1_cockpit_car && nfsu1_address(target, 64) && quest_xr_cockpit_requested()) {
        float rotation[12], position[3], zero[3] = {0}, front[16], rear[16];
        QuestMirrorCamera source;
        memcpy(rear, (const void *)XBOX_PTR(target), sizeof rear);
        if (nfsu1_render_pose(nfsu1_vr_read, NULL, nfsu1_cockpit_car, 1, rotation, position) &&
            quest_cockpit_view(front, rotation, position, zero) && quest_mirror_camera(&source, front, rear)) {
            memcpy(saved, rear, sizeof rear);
            MEM16(camera+0xc4) = 0x6800;
            MEMF(camera+0xbc) = .18f;
            MEMF(camera+0xc0) = QUEST_MIRROR_REAR_FAR;
            quest_xr_mirror_camera(nfsu1_cockpit_car, &source, rear);
            memcpy((void *)XBOX_PTR(target), rear, sizeof rear);
            return 1;
        }
    }
    if (!main) return 0;
    int race = quest_xr_enabled() && MEM32(NFSU1_FLOW) == 6;
    int grid = race && !nfsu1_replay_owner && quest_shifter_before_start(nfsu1_stage());
    if (grid && quest_xr_camera_choice() >= 5) quest_xr_cockpit_select(1);
    uint32_t car = 0;
    if (race && nfsu1_address(target, 64)) {
        if (MEM32(esp) == 0x00090546u && nfsu1_address(mover, 0x54) &&
            MEM32(mover) == NFSU1_CAMERA_MOVER_VTABLE && MEM32(mover+0x10) == 1 &&
            MEM32(mover+0x1c) == camera) {
            car = MEM32(mover+0x2c);
            if (nfsu1_address(car, 0x534)) nfsu1_restore_camera(nfsu1_player(), mover, car);
        } else if (grid && quest_xr_cockpit_requested()) {
            /* During the countdown the film camera owns the main view. */
            uint32_t manager = nfsu1_player();
            if (manager) car = MEM32(manager+4);
        }
    }
    nfsu1_cockpit_car = 0;
    int available = 0, active = 0;
    if (nfsu1_address(car, 0x534)) {
        char name[33] = {0};
        uint32_t type = MEM32(car + 0xc);
        if (nfsu1_address(type, 32)) memcpy(name, (const void *)XBOX_PTR(type), 32);
        int index = quest_cockpit_vehicle_index(name);
        const QuestCockpitVehicle *fit = quest_cockpit_vehicle(index);
        float rotation[12], position[3], matrix[16];
        available = nfsu1_render_pose(nfsu1_vr_read, NULL, car, 1, rotation, position) &&
                    quest_cockpit_view(matrix, rotation, position, fit->seat);
        uint32_t windows[QUEST_COCKPIT_WINDOWS];
        quest_cockpit_window_textures(nfsu1_vr_read, NULL, car, windows);
        quest_xr_cockpit_vehicle_publish(car, index, windows);
        if (available && quest_xr_cockpit_requested()) {
            memcpy(saved, (const void *)XBOX_PTR(target), 64);
            memcpy((void *)XBOX_PTR(target), matrix, 64);
            MEM32(camera + 0x280) = 0;
            nfsu1_cockpit_car = car;
            active = 1;
        }
        static uint32_t reported;
        if (reported != car) {
            reported = car;
            fprintf(stderr, "[NFSU1 VR] driving camera=%08x car=%08x type=%s fit=%s valid=%d\n",
                    camera, car, name, fit->name, available);
        }
    }
    quest_xr_cockpit_publish(available, active);
    return active;
}

/* Rear camera is owned by the player manager. Preserve all guest registers,
 * including x87 state, while using the title's ordinary attach/detach call. */
static void nfsu1_vr_rear_camera(uint32_t manager, int enabled)
{
    nfsu1_guest_call(sub_0003B8C0, manager, 0, 1, enabled != 0, 0);
}

/* The retail HUD option transition destroys the rear mover when pausing.
 * Reconcile the VR-owned camera at the render boundary, after UI changes,
 * without recreating a healthy camera or replacing someone else's view. */
static void nfsu1_vr_prepare_rear(void)
{
    Nfsu1PlayerCamera player;
    if (!quest_xr_enabled() || !quest_xr_cockpit_requested() ||
        !nfsu1_player_camera(nfsu1_vr_read, NULL, &player)) return;
    if (!player.rear && player.rear_view_empty) {
        nfsu1_vr_rear_camera(player.manager, 1);
        nfsu1_mirror_owner = player.manager;
        nfsu1_mirror_mover = MEM32(player.manager + 0x288);
        fprintf(stderr, "[NFSU1 VR] cockpit rear camera restored: manager=%08x mover=%08x\n",
                nfsu1_mirror_owner, nfsu1_mirror_mover);
    }
}

/* Pausing can stop the driving-camera update. Refresh its pose and the XR
 * active timestamp at view construction, which still runs in every pause
 * frame. Use the ordinary setter so its derived position/basis stay in sync;
 * seat adjustment remains in the common XR eye transform. */
static void nfsu1_vr_render_view(uint32_t view)
{
    if (view != NFSU1_VIEW(1) || !quest_xr_enabled() || !quest_xr_cockpit_requested()) return;
    Nfsu1PlayerCamera player;
    float rotation[12], position[3], matrix[16];
    int valid = nfsu1_player_camera(nfsu1_vr_read, NULL, &player);
    if (!valid && quest_shifter_before_start(nfsu1_stage()) && !nfsu1_replay_owner) {
        uint32_t manager = nfsu1_player();
        if (manager) {
            player.car = MEM32(manager+4);
            player.camera = MEM32(view+0x40);
            valid = nfsu1_address(player.camera, 0x284);
        }
    }
    char name[33] = {0};
    if (valid) {
        uint32_t type = MEM32(player.car + 0xc);
        if (nfsu1_address(type, 32)) memcpy(name, (const void *)XBOX_PTR(type), 32);
    }
    int index = quest_cockpit_vehicle_index(name);
    valid = valid && nfsu1_render_pose(nfsu1_vr_read, NULL, player.car, 1, rotation, position) &&
            quest_cockpit_view(matrix, rotation, position, quest_cockpit_vehicle(index)->seat);
    nfsu1_cockpit_car = valid ? player.car : 0;
    quest_xr_cockpit_publish(valid, valid);
    if (!valid) return;
    uint32_t windows[QUEST_COCKPIT_WINDOWS];
    quest_cockpit_window_textures(nfsu1_vr_read, NULL, player.car, windows);
    quest_xr_cockpit_vehicle_publish(player.car, index, windows);
    if (memcmp((const void *)XBOX_PTR(player.camera), matrix, sizeof matrix)) {
        esp -= sizeof matrix;
        memcpy((void *)XBOX_PTR(esp), matrix, sizeof matrix);
        nfsu1_guest_call(sub_00080850_gen, player.camera, 0, 2, esp, 0);
        esp += sizeof matrix;
    }
    MEM32(player.camera + 0x280) = 0;
    static int previous_menu = -1;
    int menu = quest_xr_cockpit_menu_active();
    if (menu != previous_menu) {
        previous_menu = menu;
        fprintf(stderr, "[NFSU1 VR] cockpit render pose: menu=%d car=%08x rear=%08x\n",
                menu, player.car, player.rear);
    }
}

static int nfsu1_vr_cycle(void)
{
    uint32_t manager = ecx;
    if (!quest_xr_enabled() || !quest_xr_cockpit_enabled() || MEM32(NFSU1_FLOW) != 6 ||
        MEM32(esp) != 0x0003F586u || !nfsu1_address(manager, 0x28c)) return 0;
    uint32_t player = MEM32(manager + 0x10), mover = MEM32(manager + 0x27c);
    if (player != 0 || !nfsu1_address(mover, 0x54) ||
        MEM32(mover) != NFSU1_CAMERA_MOVER_VTABLE) return 0;
    if (quest_xr_cockpit_requested()) {
        quest_xr_cockpit_select(0);
        if (nfsu1_mirror_owner == manager && nfsu1_mirror_mover && MEM32(manager + 0x288) == nfsu1_mirror_mover)
            nfsu1_vr_rear_camera(manager, 0);
        nfsu1_mirror_owner = nfsu1_mirror_mover = 0;
        return 0;
    }
    unsigned last = nfsu1_last_slot();
    if (MEM32(0x00295B68u) != last) return 0;
    if (MEM32(manager + 0x1c) == 3 && !MEM32(manager + 0x288)) {
        nfsu1_vr_rear_camera(manager, 1);
        nfsu1_mirror_owner = manager; nfsu1_mirror_mover = MEM32(manager + 0x288);
    }
    quest_xr_camera_remember(5);
    quest_xr_cockpit_select(1);
    eax = MEM32(esp + 4);
    esp += 8;
    return 1;
}
