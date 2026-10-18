#ifndef NFSU_TOUCH_ORBIT_H
#define NFSU_TOUCH_ORBIT_H
#include <stdint.h>
#include "menu_touch.h"

typedef struct {
    uint32_t owner;
    float x, y, start_x, start_y;
    int moving, blocked;
} TouchOrbit;
typedef struct { float dx, dy; int active; } TouchOrbitDelta;

void touch_orbit_reset(TouchOrbit *orbit);
/* Normalized coordinates; a control owns the entire gesture once captured.
 * Crossing a blocked panel freezes orbit; re-entry has no accumulated jump. */
TouchOrbitDelta touch_orbit_update(TouchOrbit *orbit, uint32_t owner,
    const MenuTouchSample *sample, int enabled, int control_captured, int blocked);
#endif
