/* Magazine screen binding; gesture recognition is shared with carousels. */
#define RECOMP_GENERATED_CODE
#include "recomp_funcs.h"
#include "feng_touch_internal.h"
#include <math.h>

extern void sub_000D1AD0(void);

/* Slot tap and swipe-only gaps use one pass over the same rendered grid. */
int touch_magazine_capture(uint32_t screen, TouchTarget *target, float x, float y)
{
    uint32_t page = MEM32(screen + 0x40), count = MEM32(screen + 0x48);
    uint32_t anchor = 0, hit = 0;
    unsigned hit_slot = 0;
    TouchBounds grid = {0};
    if (page >= 24) return 0;
    for (unsigned slot = 0; slot < 8 && page + slot < count; slot++) {
        const uint32_t hashes[] = { 0xBBE88515u + slot,
            0xF1E6EA37u + slot, 0x9DB42DF9u + slot };
        for (unsigned part = 0; part < 3; part++) {
            uint32_t object = touch_rendered_object(hashes[part]);
            TouchBounds bounds;
            if (!object || (MEM32(object + 0x1C) & 0x04000000u) ||
                !touch_object_bounds(object, &bounds, 0)) continue;
            if (!anchor) grid = bounds;
            else {
                touch_bounds_include(&grid, &bounds);
            }
            anchor = object;
            if (!hit && touch_bounds_contains(&bounds, x, y)) {
                hit = object;
                hit_slot = slot + 1;
            }
        }
    }
    if (!anchor || !touch_bounds_contains(&grid, x, y))
        return 0;
    target->target = hit ? hit : anchor;
    target->message = 0x406415E3u;
    target->magazine_slot = hit_slot;
    target->magazine_page = page;
    target->magazine_swipe = 1;
    target->swipe_axis = TOUCH_SWIPE_VERTICAL;
    target->swipe_distance = 24;
    target->swipe_only = !hit;
    return 1;
}

int touch_magazine_select(const TouchTarget *target)
{
    uint32_t screen = target->screen;
    if (MEM32(screen) != 0x001FEA8Cu ||
        MEM32(screen + 0x40) != target->magazine_page ||
        target->magazine_page + target->magazine_slot - 1 >= MEM32(screen + 0x48)) return 0;
    MEM32(screen + 0x3C) = target->magazine_slot - 1;
    PUSH32(esp, 0);
    ecx = screen;
    sub_000D1AD0(); /* refresh native highlight, description and lock UI */
    return 1;
}
