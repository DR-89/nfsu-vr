#ifndef NFSU_TOUCH_GESTURE_H
#define NFSU_TOUCH_GESTURE_H

typedef enum { TOUCH_SWIPE_NONE, TOUCH_SWIPE_HORIZONTAL, TOUCH_SWIPE_VERTICAL } TouchSwipeAxis;
typedef enum { TOUCH_GESTURE_WAIT, TOUCH_GESTURE_TAP, TOUCH_GESTURE_NEGATIVE,
               TOUCH_GESTURE_POSITIVE, TOUCH_GESTURE_CANCEL } TouchGestureResult;
typedef struct {
    float start_x, start_y, travel;
    unsigned armed, fired;
} TouchGesture;

void touch_gesture_begin(TouchGesture *gesture, float x, float y);
/* One owner, one result on release. Dragging never falls back to a tap. */
TouchGestureResult touch_gesture_update(TouchGesture *gesture, float x, float y,
    int down, int prepare, TouchSwipeAxis axis, float swipe_distance, int swipe_only);
#endif
