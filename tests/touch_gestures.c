/* cc -std=c11 -Wall -Wextra -Werror -Isrc tests/touch_gestures.c
 * src/touch_gesture.c src/touch_orbit.c -lm -o /tmp/nfsu-touch-gestures-test */
#include <assert.h>
#include <stdio.h>
#include "touch_gesture.h"
#include "touch_orbit.h"

int main(void)
{
    TouchGesture gesture;
    touch_gesture_begin(&gesture, 100, 100);
    assert(touch_gesture_update(&gesture, 100, 100, 1, 0,
        TOUCH_SWIPE_HORIZONTAL, 20, 0) == TOUCH_GESTURE_WAIT);
    assert(touch_gesture_update(&gesture, 70, 100, 0, 0,
        TOUCH_SWIPE_HORIZONTAL, 20, 0) == TOUCH_GESTURE_NEGATIVE);
    assert(touch_gesture_update(&gesture, 70, 100, 0, 0,
        TOUCH_SWIPE_HORIZONTAL, 20, 0) == TOUCH_GESTURE_WAIT); /* exactly once */

    touch_gesture_begin(&gesture, 100, 100);
    touch_gesture_update(&gesture, 100, 100, 1, 0, TOUCH_SWIPE_VERTICAL, 24, 0);
    assert(touch_gesture_update(&gesture, 100, 130, 0, 0,
        TOUCH_SWIPE_VERTICAL, 24, 0) == TOUCH_GESTURE_POSITIVE);
    touch_gesture_begin(&gesture, 100, 100);
    touch_gesture_update(&gesture, 100, 100, 1, 0, TOUCH_SWIPE_HORIZONTAL, 20, 1);
    assert(touch_gesture_update(&gesture, 100, 100, 0, 0,
        TOUCH_SWIPE_HORIZONTAL, 20, 1) == TOUCH_GESTURE_CANCEL); /* gap tap */
    touch_gesture_begin(&gesture, 100, 100);
    touch_gesture_update(&gesture, 100, 100, 1, 0, TOUCH_SWIPE_HORIZONTAL, 20, 0);
    touch_gesture_update(&gesture, 100, 140, 1, 0, TOUCH_SWIPE_HORIZONTAL, 20, 0);
    assert(touch_gesture_update(&gesture, 100, 100, 0, 0,
        TOUCH_SWIPE_HORIZONTAL, 20, 0) == TOUCH_GESTURE_CANCEL); /* dragged back */

    TouchOrbit orbit = {0};
    MenuTouchSample sample = {.x = .2f, .y = .5f, .active = 1, .down = 1, .prepare = 1};
    touch_orbit_update(&orbit, 1, &sample, 1, 0, 0);
    sample.prepare = 0; sample.x = .3f;
    TouchOrbitDelta delta = touch_orbit_update(&orbit, 1, &sample, 1, 0, 0);
    assert(delta.active && delta.dx > .09f);
    sample.x = .5f;
    delta = touch_orbit_update(&orbit, 1, &sample, 1, 0, 1);
    assert(!delta.active && delta.dx == 0); /* inside carousel */
    sample.x = .7f;
    delta = touch_orbit_update(&orbit, 1, &sample, 1, 0, 0);
    assert(delta.dx == 0); /* re-entry cannot jump */
    delta = touch_orbit_update(&orbit, 2, &sample, 1, 0, 0);
    assert(!delta.active && !orbit.owner); /* modal/screen changed */
    sample.prepare = 1;
    touch_orbit_update(&orbit, 2, &sample, 1, 1, 0);
    sample.prepare = 0; sample.x = .9f;
    delta = touch_orbit_update(&orbit, 2, &sample, 1, 1, 0);
    assert(!delta.active); /* a control keeps gesture ownership */
    puts("shared swipe lifecycle and preview orbit ownership: OK");
}
