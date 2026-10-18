/* Native preview camera adapter. Gesture ownership lives in touch_orbit.c. */
#define RECOMP_GENERATED_CODE
#include "recomp_funcs.h"
#include "feng_touch_internal.h"
#include <math.h>
#include <string.h>

static uint32_t touch_preview_camera(void)
{
    uint32_t reg = MEM32(0x002BCB9Cu);
    for (unsigned n = 0; reg && reg != 0x002BCB9Cu && n < 64; n++, reg = MEM32(reg)) {
        uint32_t screen = MEM32(reg - 4 + 0x14);
        if (!screen || MEM32(screen) != 0x001FB0CCu) continue;
        uint32_t camera = MEM32(screen + 0x5C);
        if (camera && MEM32(camera) == 0x001F8E90u) return camera;
    }
    return 0;
}
void touch_camera_orbit(float dx, float dy)
{
    uint32_t camera = touch_preview_camera();
    if (!camera || MEM32(camera + 0x2C) == 2) return; /* scripted transition */
    float pitch, yaw;
    memcpy(&pitch, (void *)XBOX_PTR(camera + 0x40), 4);
    memcpy(&yaw, (void *)XBOX_PTR(camera + 0x44), 4);
    if (!isfinite(pitch) || !isfinite(yaw)) return;
    pitch = fmaxf(85, fminf(140, pitch + dy * 90));
    yaw = fmodf(yaw + dx * 180 + 360, 360);
    memcpy((void *)XBOX_PTR(camera + 0x40), &pitch, 4);
    memcpy((void *)XBOX_PTR(camera + 0x44), &yaw, 4);
    MEM32(camera + 0x2C) = 3;
    MEM32(camera + 0x180) = MEM32(camera + 0x184) = MEM32(camera + 0x188) = 0;
    /* Treat touch as manual inspection, resetting the native idle orbit. */
    MEM32(0x002B7570u) = MEM32(0x002CA448u);
    MEM32(0x00247D38u) = 0;
}
