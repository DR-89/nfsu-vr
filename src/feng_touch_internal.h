#ifndef NFSU_FENG_TOUCH_INTERNAL_H
#define NFSU_FENG_TOUCH_INTERNAL_H
/* Private guest-side adapter contract. Public hooks remain in feng_touch.h. */
#include <stdint.h>
#include "touch_gesture.h"
#include "touch_geometry.h"
typedef struct {
    uint32_t package, target, message, screen;
    uint32_t palette_entry, palette_channel, palette_part, list_entry;
    uint32_t magazine_slot, magazine_page;
    unsigned back, screen_control, carousel;
    unsigned vinyl_layer, carousel_sync;
    unsigned swipe_only, magazine_swipe, focus_only;
    TouchSwipeAxis swipe_axis;
    float swipe_distance;
    TouchGesture gesture;
} TouchTarget;

static inline void touch_target_begin(TouchTarget *target, uint32_t screen, float x, float y)
{
    target->screen = screen;
    touch_gesture_begin(&target->gesture, x, y);
}

uint32_t touch_active_screen(void);
uint32_t touch_preview_screen(void);
uint32_t touch_name_hash(const char *name);
int touch_object_bounds(uint32_t object, TouchBounds *out, unsigned depth);
uint32_t touch_rendered_object(uint32_t hash);
uint32_t touch_rendered_named_hit(uint32_t hash);
uint32_t touch_browser_arrow(uint32_t *message);
uint32_t touch_carousel_screen(void);
int touch_carousel_strip(uint32_t screen, float x, float y);
int touch_carousel_capture(uint32_t screen, TouchTarget *target, float x, float y);
int touch_carousel_select(const TouchTarget *target);
int touch_magazine_capture(uint32_t screen, TouchTarget *target, float x, float y);
int touch_magazine_select(const TouchTarget *target);
void touch_camera_orbit(float dx, float dy);
#endif
