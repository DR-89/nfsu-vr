#include "touch_orbit.h"
#include <math.h>

void touch_orbit_reset(TouchOrbit *orbit) { *orbit = (TouchOrbit){0}; }

TouchOrbitDelta touch_orbit_update(TouchOrbit *orbit, uint32_t owner,
    const MenuTouchSample *sample, int enabled, int control_captured, int blocked)
{
    TouchOrbitDelta delta = {0};
    if (sample->reset || !sample->active || owner != orbit->owner) orbit->owner = 0;
    if (enabled && sample->prepare) {
        orbit->owner = control_captured ? 0 : owner;
        orbit->start_x = orbit->x = sample->x;
        orbit->start_y = orbit->y = sample->y;
        orbit->moving = orbit->blocked = 0;
    }
    if (orbit->owner && sample->active && !sample->prepare) {
        if (fabsf(sample->x - orbit->start_x) + fabsf(sample->y - orbit->start_y) > .006f)
            orbit->moving = 1;
        if (orbit->moving && !blocked && !orbit->blocked) {
            delta.dx = sample->x - orbit->x;
            delta.dy = sample->y - orbit->y;
        }
        orbit->x = sample->x;
        orbit->y = sample->y;
    }
    orbit->blocked = blocked;
    delta.active = orbit->owner && !blocked && (sample->down || orbit->moving);
    return delta;
}
