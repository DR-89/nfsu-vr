#include "input_mode.h"
#include <stdatomic.h>

/* One atomic word publishes preferences and hardware state together.
 * Low bits hold values; the remaining bits count state changes. */
#define CONTROLLER_PRESENT 1u
#define PREFERENCE_MASK 6u
#define PREFERENCE_OFF 2u
#define PREFERENCE_ON 4u
#define OVERLAY_REQUESTED 8u
#define REVISION_STEP 16u
static atomic_uint state;

static void change_state(unsigned mask, unsigned value)
{
    unsigned previous = atomic_load(&state);
    for (;;) {
        unsigned next = (previous & ~mask) | value;
        if (next == previous) return;
        next += REVISION_STEP;
        if (atomic_compare_exchange_weak(&state, &previous, next)) return;
    }
}

InputMode input_mode_get(void)
{
    unsigned value = atomic_load(&state);
    InputMode mode;
    mode.controller_present = (value & CONTROLLER_PRESENT) != 0;
    mode.preference = INPUT_SENSOR_AUTO;
    if ((value & PREFERENCE_MASK) == PREFERENCE_OFF) mode.preference = INPUT_SENSOR_OFF;
    if ((value & PREFERENCE_MASK) == PREFERENCE_ON) mode.preference = INPUT_SENSOR_ON;
    /* Never disable the only available input method. */
    mode.sensor_enabled = !mode.controller_present || mode.preference == INPUT_SENSOR_ON;
    mode.sensor_editable = mode.controller_present;
    mode.overlay_editable = mode.controller_present && mode.sensor_enabled;
    mode.overlay_requested = (value & OVERLAY_REQUESTED) != 0;
    mode.overlay_enabled = mode.sensor_enabled && mode.overlay_requested;
    mode.revision = value / REVISION_STEP;
    return mode;
}

int input_mode_sensor_enabled(void) { return input_mode_get().sensor_enabled; }

void input_mode_set_controller_present(int present)
{
    change_state(CONTROLLER_PRESENT, present ? CONTROLLER_PRESENT : 0);
}

void input_mode_configure(InputSensorPreference preference, int overlay)
{
    unsigned value = overlay ? OVERLAY_REQUESTED : 0;
    if (preference == INPUT_SENSOR_OFF) value |= PREFERENCE_OFF;
    if (preference == INPUT_SENSOR_ON) value |= PREFERENCE_ON;
    change_state(PREFERENCE_MASK | OVERLAY_REQUESTED, value);
}
