/* Included after recomp_funcs.h; guest access occurs only on the guest thread. */
#include "nfsu1_vr.h"
#include "quest_xr.h"
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

static uint32_t nfsu1_cockpit_car;
static uint32_t nfsu1_mirror_owner, nfsu1_mirror_mover;
static int nfsu1_vr_read(void *context, uint32_t address, void *out, size_t size)
{
    (void)context;
    if (!nfsu1_address(address, size)) return 0;
    memcpy(out, (const void *)XBOX_PTR(address), size);
    return 1;
}

static void nfsu1_vr_publish_ui(void)
{
    int paused = 0, race = MEM32(NFSU1_FLOW) == 6;
    int menu = nfsu1_vr_menu(nfsu1_vr_read, NULL, &paused);
    static int previous = -1;
    int state = (race ? 4 : 0) | (menu != 0 ? 2 : 0) | paused;
    if (state != previous) {
        previous = state;
        fprintf(stderr, "[NFSU1 VR] UI race=%d menu=%d root-pause=%d (active package chain)\n", race, menu, paused);
    }
    quest_xr_driving_publish(race && menu == 0);
    quest_xr_cockpit_menu_publish(!race || menu != 0);
    quest_xr_pause_publish(race && paused);
    if (!race) {
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
    if (!main) return 0;
    nfsu1_cockpit_car = 0;
    int available = 0, active = 0;
    if (quest_xr_enabled() && MEM32(NFSU1_FLOW) == 6 && MEM32(esp) == 0x00090546u &&
        nfsu1_address(mover, 0x54) && MEM32(mover) == NFSU1_CAMERA_MOVER_VTABLE &&
        MEM32(mover + 0x10) == 1 && MEM32(mover + 0x1c) == camera && nfsu1_address(target, 64)) {
        uint32_t car = MEM32(mover + 0x2c);
        if (nfsu1_address(car, 0xd0)) {
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
            quest_xr_cockpit_vehicle_publish(index, windows);
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
    }
    quest_xr_cockpit_publish(available, active);
    return active;
}

/* Rear camera is owned by the player manager. Preserve all guest registers,
 * including x87 state, while using the title's ordinary attach/detach call. */
static void nfsu1_vr_rear_camera(uint32_t manager, int enabled)
{
    uint32_t regs[9] = {eax, ecx, edx, ebx, esi, edi, esp, g_ebp, g_seh_ebp};
    double fp[8]; memcpy(fp, g_fp_stack, sizeof fp);
    int top = g_fp_top, cmp = g_fp_cmp;
    uint16_t cc = g_fp_cc, cw = g_fp_control_word;
    ecx = manager;
    PUSH32(esp, enabled != 0); PUSH32(esp, 0);
    sub_0003B8C0();
    eax=regs[0]; ecx=regs[1]; edx=regs[2]; ebx=regs[3]; esi=regs[4]; edi=regs[5];
    esp=regs[6]; g_ebp=regs[7]; g_seh_ebp=regs[8];
    memcpy(g_fp_stack,fp,sizeof fp); g_fp_top=top; g_fp_cmp=cmp; g_fp_cc=cc; g_fp_control_word=cw;
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
    quest_xr_cockpit_vehicle_publish(index, windows);
    if (memcmp((const void *)XBOX_PTR(player.camera), matrix, sizeof matrix)) {
        uint32_t regs[9] = {eax, ecx, edx, ebx, esi, edi, esp, g_ebp, g_seh_ebp};
        double fp[8]; memcpy(fp, g_fp_stack, sizeof fp);
        int top = g_fp_top, cmp = g_fp_cmp;
        uint16_t cc = g_fp_cc, cw = g_fp_control_word;
        esp -= sizeof matrix;
        memcpy((void *)XBOX_PTR(esp), matrix, sizeof matrix);
        uint32_t target = esp;
        ecx = player.camera;
        PUSH32(esp, 0); PUSH32(esp, target); PUSH32(esp, 0);
        sub_00080850_gen();
        eax=regs[0]; ecx=regs[1]; edx=regs[2]; ebx=regs[3]; esi=regs[4]; edi=regs[5];
        esp=regs[6]; g_ebp=regs[7]; g_seh_ebp=regs[8];
        memcpy(g_fp_stack,fp,sizeof fp); g_fp_top=top; g_fp_cmp=cmp; g_fp_cc=cc; g_fp_control_word=cw;
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
    unsigned last = (MEM8(0x00273785u) || MEM32(0x00265BC4u)) ? 3 : 2;
    if (MEM32(0x00295B68u) != last) return 0;
    if (MEM32(manager + 0x1c) == 3 && !MEM32(manager + 0x288)) {
        nfsu1_vr_rear_camera(manager, 1);
        nfsu1_mirror_owner = manager; nfsu1_mirror_mover = MEM32(manager + 0x288);
    }
    quest_xr_cockpit_select(1);
    eax = MEM32(esp + 4);
    esp += 8;
    return 1;
}
