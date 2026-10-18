#include "race_touch.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    race_touch_configure_features(1, 1); /* Existing input tests use manual + equipped NOS. */
    assert(!race_touch_visible());
    race_touch_set(RACE_GAS);
    assert(race_touch_consume(0) == 0);
    assert(race_touch_consume(1) == 0);
    assert(race_touch_visible());
    race_touch_set(RACE_GAS | RACE_LEFT | RACE_NITRO);
    assert(race_touch_consume(1) == (RACE_GAS | RACE_LEFT | RACE_NITRO));
    assert(race_touch_consume(1) == (RACE_GAS | RACE_LEFT | RACE_NITRO));
    race_touch_set(RACE_GAS);
    assert(race_touch_consume(1) == RACE_GAS);
    race_touch_set(0);
    assert(race_touch_consume(1) == 0);
    /* A quick gear tap between reports must survive, then release. */
    race_touch_set(RACE_SHIFT_UP);
    race_touch_set(0);
    assert(race_touch_consume(1) == RACE_SHIFT_UP);
    assert(race_touch_consume(1) == 0);
    race_touch_set(RACE_GAS | RACE_RIGHT);
    race_touch_cancel();
    assert(race_touch_consume(1) == 0);
    race_touch_set(RACE_MENU);
    assert(race_touch_consume(0) == 0);
    assert(!race_touch_visible());
    assert(race_touch_consume(1) == 0);
    race_touch_request_editor();
    assert(race_touch_take_editor_request());
    assert(!race_touch_take_editor_request());
    race_touch_set(RACE_GAS);
    race_touch_set_editing(1);
    race_touch_set(RACE_NITRO);
    assert(race_touch_editing());
    assert(race_touch_consume(1) == 0);
    assert(!race_touch_visible());
    race_touch_set_editing(0);
    assert(race_touch_consume(1) == 0);
    race_touch_set(RACE_GAS);
    assert(race_touch_consume(1) == RACE_GAS);
    race_touch_enable_hold(1);
    race_touch_set_hold(1);
    race_touch_enable(0); /* Overlay hidden: the screen gesture stays available. */
    assert(race_touch_hold_active());
    race_touch_set_editing(1);
    assert(!race_touch_hold_active());
    race_touch_set_editing(0);
    assert(!race_touch_hold_active());
    race_touch_set_hold(1);
    race_touch_enable_hold(0);
    race_touch_enable_hold(1);
    assert(!race_touch_hold_active()); /* Menu/mode transitions cannot resume a stale hold. */
    puts("race touch: OK");
    return 0;
}
