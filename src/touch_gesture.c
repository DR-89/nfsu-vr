#include "touch_gesture.h"
#include <math.h>

void touch_gesture_begin(TouchGesture *gesture, float x, float y)
{
    *gesture = (TouchGesture){ .start_x = x, .start_y = y };
}

TouchGestureResult touch_gesture_update(TouchGesture *gesture, float x, float y,
    int down, int prepare, TouchSwipeAxis axis, float swipe_distance, int swipe_only)
{
    float dx = x - gesture->start_x, dy = y - gesture->start_y;
    gesture->travel = fmaxf(gesture->travel, hypotf(dx, dy));
    if (down) gesture->armed = 1;
    if (down || prepare || !gesture->armed || gesture->fired) return TOUCH_GESTURE_WAIT;
    gesture->fired = 1;
    float along = axis == TOUCH_SWIPE_VERTICAL ? dy : dx;
    float across = axis == TOUCH_SWIPE_VERTICAL ? dx : dy;
    if (axis != TOUCH_SWIPE_NONE && fabsf(along) >= swipe_distance &&
        fabsf(along) > fabsf(across) * 1.25f)
        return along < 0 ? TOUCH_GESTURE_NEGATIVE : TOUCH_GESTURE_POSITIVE;
    if (swipe_only || gesture->travel >= 12) return TOUCH_GESTURE_CANCEL;
    return TOUCH_GESTURE_TAP;
}
