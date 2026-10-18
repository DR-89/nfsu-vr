/* Shared native carousel adapter: screen bindings differ; capture does not. */
#define RECOMP_GENERATED_CODE
#include "recomp_funcs.h"
#include "feng_touch_internal.h"

/* Track selection owns a horizontal browser but no 3D preview camera.
 * B1630 constructs QRTrackSelectScreen (1FB848), browser +48 and selected
 * node +54. Keep it out of touch_preview_screen so map drags cannot orbit. */
uint32_t touch_carousel_screen(void)
{
    uint32_t screen = touch_active_screen();
    if (screen && (MEM32(screen) == 0x001FB848u ||
                   MEM32(screen) == 0x001FC12Cu)) return screen;
    return touch_preview_screen();
}

/* Only native layout details differ between car and tuning carousels.
 * Capture, arrow lookup, release dispatch and gesture policy are shared. */
typedef struct {
    uint32_t selected_node, activate_message, browser;
    uint32_t label_hashes[2];
    unsigned allow_swipe;
} TouchCarousel;

static int touch_carousel_binding(uint32_t screen, TouchCarousel *binding)
{
    if (screen != touch_carousel_screen() || MEM32(screen) == 0x001FDD74u) return 0;
    if (MEM32(screen) == 0x001FC12Cu) {
        /* UndergroundMapScreen: B9460 allocates the shared browser at +3C.
         * Its selected rendered node is browser +0C, not screen +48 (the
         * separate career event cursor). B95D0 handles Left/Right/Select. */
        uint32_t browser = MEM32(screen + 0x3C);
        if (!browser || MEM32(browser) != 0x001FF870u) return 0;
        *binding = (TouchCarousel){
            .selected_node = MEM32(browser + 0xC),
            .browser = browser,
            .activate_message = 0x406415E3u,
            .label_hashes = { touch_name_hash("INCOMING_TEXT"),
                              touch_name_hash("ACCEPT_MESSAGE_TEXT") },
            .allow_swipe = 1
        };
        return 1;
    }
    if (MEM32(screen) == 0x001FB848u) {
        /* B0840 updates the visible location name (03C8F634); B1880
         * accepts Select, and B0CE0 performs native Left/Right selection. */
        *binding = (TouchCarousel){
            .selected_node = MEM32(screen + 0x54),
            .activate_message = 0x406415E3u,
            .label_hashes = { 0x03C8F634u, 0 },
            .allow_swipe = 1
        };
        return 1;
    }
    if (MEM32(screen) == 0x001FB8F4u) {
        *binding = (TouchCarousel){
            .selected_node = MEM32(screen + 0x58),
            .activate_message = 0x406415E3u,
            .allow_swipe = 1
        };
        return 1;
    }
    /* MainMenuCarCustomize B9E80 and ChooseCareerCarScreen B7F60 construct
     * the same browser at +48, selected node +54. B9D70/B7DD0 accept
     * MouseClick/Select; B9CC0/B7D40 handle native Left/Right. */
    if (MEM32(screen + 0x48) == 0x001FF870u) {
        *binding = (TouchCarousel){
            .selected_node = MEM32(screen + 0x54),
            .activate_message = 0x0C407210u,
            .label_hashes = { 0xFAC7F994u, 0x4AB1E821u },
            .allow_swipe = 1
        };
        return 1;
    }
    return 0;
}

/* Shared boundary for carousel capture and camera movement. */
int touch_carousel_strip(uint32_t screen, float x, float y)
{
    TouchCarousel binding;
    if (!screen || !touch_carousel_binding(screen, &binding) || !binding.allow_swipe) return 0;
    uint32_t node = binding.selected_node;
    uint32_t object = node ? MEM32(node + 0xC) : 0;
    TouchBounds bounds;
    if (!object || !touch_object_bounds(object, &bounds, 0) ||
        bounds.bottom - bounds.top > 160) return 0;
    return x >= 0 && x < 640 && y >= 0 && y < bounds.bottom + 16;
}

int touch_carousel_capture(uint32_t screen, TouchTarget *target, float x, float y)
{
    TouchCarousel binding;
    if (!touch_carousel_binding(screen, &binding)) return 0;
    uint32_t message = 0;
    uint32_t object = touch_browser_arrow(&message);
    if (!object) {
        object = binding.selected_node ? MEM32(binding.selected_node + 0xC) : 0;
        TouchBounds bounds;
        if (!object || !touch_object_bounds(object, &bounds, 0) ||
            !touch_bounds_contains(&bounds, x, y))
            object = 0;
        if (object && binding.browser)
            target->list_entry = MEM32(binding.selected_node + 8);
        /* Some native browsers have a separate data cursor. Resolve a tap
         * on any rendered item here; synchronization happens only on tap. */
        uint32_t head = binding.browser + 4;
        uint32_t node = binding.browser ? MEM32(head) : 0;
        for (unsigned i = 0; !object && node && node != head && i < 64;
             i++, node = MEM32(node)) {
            uint32_t candidate = MEM32(node + 0xC);
            if (!candidate || (MEM32(candidate + 0x1C) & 0x04000000u) ||
                !touch_object_bounds(candidate, &bounds, 0) ||
                !touch_bounds_contains(&bounds, x, y))
                continue;
            object = candidate;
            target->list_entry = MEM32(node + 8);
        }
        for (unsigned i = 0; !object && i < 2; i++)
            if (binding.label_hashes[i]) object = touch_rendered_named_hit(binding.label_hashes[i]);
        message = binding.activate_message;
    }
    if (!object && binding.allow_swipe && binding.selected_node) {
        /* The selected icon is only a small part of the visible carousel strip.
         * Capture gaps and side logos from the canvas top through its rendered bottom.
         * A gap owns swipes only: a stationary tap must not open Customize. */
        uint32_t selected = MEM32(binding.selected_node + 0xC);
        if (touch_carousel_strip(screen, x, y)) {
            object = selected;
            message = binding.activate_message;
            target->swipe_only = 1;
        }
    }
    if (!object) return 0;
    target->target = object;
    target->message = message;
    target->carousel = 1;
    target->swipe_axis = binding.allow_swipe ? TOUCH_SWIPE_HORIZONTAL : TOUCH_SWIPE_NONE;
    target->swipe_distance = 20;
    touch_target_begin(target, screen, x - 320, y - 240);
    return 1;
}

/* The map browser has a separate native event cursor. Swipes dispatch
 * Left/Right directly; a tap synchronizes its event before Select. */
extern void sub_000DF160(void);
extern void sub_000B8310(void);
int touch_carousel_select(const TouchTarget *target)
{
    uint32_t list = MEM32(target->screen + 0x3C);
    uint32_t head = list + 4, node = list ? MEM32(head) : 0;
    for (unsigned i = 0; node && node != head && i < 64; i++, node = MEM32(node)) {
        if (MEM32(node + 8) != target->list_entry || MEM32(node + 0xC) != target->target)
            continue;
        MEM32(target->screen + 0x48) = target->list_entry;
        PUSH32(esp, target->list_entry);
        PUSH32(esp, 0);
        ecx = list;
        sub_000DF160();
        PUSH32(esp, 0);
        ecx = target->screen;
        sub_000B8310(); /* refresh route and event details */
        return 1;
    }
    return 0;
}
