#pragma once
#include <math.h>
#include <stdint.h>

/* NFSU1 decorates its 256x64 rear-camera target with the blue 2D mirror
 * frame, then writes a separate alpha mask. Physical cockpit glass needs
 * the undecorated RGB image. Recognize only the inspected RGB frame pass;
 * retain scenery, lights, the alpha pass and the normal exterior-camera HUD.
 * Camera constants are row-major, as stored by the guest vertex program. */
static inline int quest_mirror_frame_draw(int cockpit, uint32_t width, uint32_t height,
    int eye, uint32_t vertices, uint32_t program, const uint32_t *regs,
    const float *camera)
{
    if (!cockpit || width != 256 || height != 64 || eye != -1 ||
        vertices != 4 || program != 0 || !camera ||
        regs[0x30c/4] || regs[0x35c/4] || !regs[0x304/4] ||
        regs[0x344/4] != 0x302 || regs[0x348/4] != 0x303 ||
        regs[0x358/4] != 0x00010101 || regs[0x1e70/4] != 1 ||
        regs[0x32c/4] || regs[0x308/4]) return 0;
    for (unsigned i = 0; i < 16; ++i) {
        float expected = i % 5 == 0 ? 1.0f : 0.0f;
        if (!isfinite(camera[i]) || fabsf(camera[i] - expected) > .00001f) return 0;
    }
    return 1;
}
