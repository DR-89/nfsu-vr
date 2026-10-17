/* Native FEng touch engine. The guest thread owns geometry, gestures and
 * dispatch; menu_touch.c supplies the synchronized host event queue.
 * Shared geometry/camera policies are reused by the screen bindings below. */
#define RECOMP_GENERATED_CODE
#include "recomp_funcs.h"
#include "feng_touch.h"
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#define NFSU1_GAMEFLOW_STATE 0x00283434u
extern int xbox_video_widescreen(void);
/* The retail Xbox FEng retains its native mouse dispatcher (D8730), focus
 * notifications (E53C0) and object hit test (DDB60 -> DD880 -> DCDC0).
 * Only input acquisition is a no-op: cFEng vtable 1FF4D8, slot +44 = DB860.
 * Feed that exact call, leaving every other use of the shared no-op alone.
 * Rendered object geometry and native controller bindings drive activation. */
extern void sub_000D8B10_gen(void);
extern void sub_000DB860_gen(void);
extern void sub_000D8730_gen(void);
extern void sub_000DDB60(void);
extern void sub_000E53C0(void);
extern void sub_000D6D30(void);
extern void sub_000D7A00(void);
extern void sub_000DE540(void);
extern void sub_000D1AD0(void);
extern void sub_000C74C0(void);
extern void sub_000C7B40(void);
extern void sub_000C7A80(void);
#include "menu_touch.h"
static RECOMP_TLS short s_touch_x, s_touch_y;
static RECOMP_TLS unsigned s_touch_buttons, s_touch_poll, s_touch_prepare;
typedef struct {
    uint32_t package, target, message, fired, screen, armed, palette_entry, back, screen_control, list_entry, carousel, allow_swipe, swipe_only, magazine_slot, magazine_page, focus_only;
    short start_x, start_y;
    float travel;
} TouchTarget;
static RECOMP_TLS TouchTarget s_touch_targets[32];
static RECOMP_TLS unsigned s_touch_target_count, s_touch_target_slot;
static RECOMP_TLS unsigned s_touch_collect_hit;
static RECOMP_TLS uint32_t s_touch_draw_object;
typedef struct { uint32_t object; float left, top, right, bottom; } TouchBounds;
static TouchBounds s_touch_bounds[2048];
static unsigned s_touch_bounds_count;
static uint32_t s_back_caches[16], s_back_icons[8];
static unsigned s_back_cache_count, s_back_icon_count;
extern void sub_000F8B80_gen(void);

static atomic_int s_race_menu_active;
int feng_touch_race_menu_active(void)
{
    return atomic_load(&s_race_menu_active);
}
static int touch_menu_enabled(void)
{
    uint32_t flow = MEM32(NFSU1_GAMEFLOW_STATE);
    return flow == 3 || (flow == 6 && atomic_load(&s_race_menu_active));
}

/* QROptions' blinking arrow glyphs share one hash; preserve their last drawn
 * bounds only while the same modal and focused row own them. */
static TouchBounds s_qr_arrows[2];
static unsigned s_qr_arrow_count;
static uint32_t s_qr_screen, s_qr_focus;


static void touch_bounds_add(uint32_t object, float x, float y)
{
    if (!object || !isfinite(x) || !isfinite(y)) return;
    unsigned i;
    for (i = 0; i < s_touch_bounds_count; i++)
        if (s_touch_bounds[i].object == object) break;
    if (i == s_touch_bounds_count) {
        if (i == sizeof(s_touch_bounds) / sizeof(s_touch_bounds[0])) return;
        s_touch_bounds_count++;
        s_touch_bounds[i] = (TouchBounds){ object, x, y, x, y };
    }
    TouchBounds *bounds = &s_touch_bounds[i];
    bounds->left = fminf(bounds->left, x); bounds->right = fmaxf(bounds->right, x);
    bounds->top = fminf(bounds->top, y); bounds->bottom = fmaxf(bounds->bottom, y);
}

static int touch_has_message(uint32_t node, uint32_t message)
{
    for (unsigned i = 0; node && i < 1024; i++, node = MEM32(node + 4))
        if (MEM32(node + 0xC) == message) return 1;
    return 0;
}

static int touch_object_bounds(uint32_t object, TouchBounds *out, unsigned depth)
{
    if (!object || depth > 16) return 0;
    for (unsigned i = 0; i < s_touch_bounds_count; i++) {
        if (s_touch_bounds[i].object == object) { *out = s_touch_bounds[i]; return 1; }
    }
    if (MEM32(object + 0x18) != 5) return 0;
    int found = 0;
    uint32_t child = MEM32(object + 0x60);
    for (unsigned i = 0; child && i < 1024; i++, child = MEM32(child + 4)) {
        TouchBounds bounds;
        if (!touch_object_bounds(child, &bounds, depth + 1)) continue;
        if (!found) *out = bounds;
        else {
            out->left = fminf(out->left, bounds.left);
            out->right = fmaxf(out->right, bounds.right);
            out->top = fminf(out->top, bounds.top);
            out->bottom = fmaxf(out->bottom, bounds.bottom);
        }
        found = 1;
    }
    return found;
}

/* Retail footer labels use HELP while the adjacent icon uses
 * FENG_HELP_BUTTON. Keep native text layout/localization and replace its role. */
static int touch_back_label(uint32_t object)
{
    if (!object || MEM32(object + 0x18) != 2) return 0;
    uint32_t language = MEM32(object + 0x5C);
    if (language == 0x001335F0u) return 1; /* native/localized Back */
    if (MEM32(NFSU1_GAMEFLOW_STATE) == 6 && feng_touch_race_menu_active() &&
        language == 0x4F876B6Fu) return 1; /* pause footer Accept -> Back */
    if (language != 0x00169268u) return 0;
    switch (MEM32(object + 0x10)) {
    case 0xA87927BEu: case 0x936D3402u: case 0x0C39BA77u:
    case 0x0C39AA39u: case 0x0C39BAFBu: case 0x0C39BAB9u:
    case 0x0C39BF5Du: case 0x00169268u: case 0x5B9D84DBu:
        return 1;
    default: return 0;
    }
}
static int touch_help_icon(uint32_t object)
{
    return object && MEM32(object + 0x18) == 2 &&
        MEM32(object + 0x5C) == 0x349EB9C2u;
}
static int touch_pause_back_icon(uint32_t object)
{
    if (MEM32(NFSU1_GAMEFLOW_STATE) != 6 || !feng_touch_race_menu_active() ||
        MEM32(object + 0x18) != 2) return 0;
    uint32_t language = MEM32(object + 0x5C);
    return language == 0xE03C840Au || language == 0xE03C840Bu;
}
static uint32_t touch_back_hit(void)
{
    float x = s_touch_x + 320.0f, y = s_touch_y + 240.0f;
    TouchBounds area = {0};
    uint32_t target = 0, hit = 0;
    for (unsigned i = 0; i < s_touch_bounds_count; i++) {
        TouchBounds *b = &s_touch_bounds[i];
        int footer_icon = 0;
        for (unsigned icon = 0; icon < s_back_icon_count; icon++)
            if (s_back_icons[icon] == b->object) footer_icon = 1;
        if (!touch_back_label(b->object) && !touch_help_icon(b->object) &&
            !touch_pause_back_icon(b->object) && !footer_icon) continue;
        if (!target) area = *b;
        else {
            area.left = fminf(area.left, b->left);
            area.right = fmaxf(area.right, b->right);
            area.top = fminf(area.top, b->top);
            area.bottom = fmaxf(area.bottom, b->bottom);
        }
        target = b->object;
        if (x >= b->left && x < b->right && y >= b->top && y < b->bottom)
            hit = b->object;
    }
    /* Include the whole caption, icon and their gap, with finger tolerance. */
    if (target && x >= area.left - 16 && x < area.right + 16 &&
        y >= area.top - 16 && y < area.bottom + 16) return hit ? hit : target;
    return 0;
}
static uint32_t touch_active_screen(void)
{
    uint32_t reg = MEM32(0x002BCB9Cu);
    for (unsigned n = 0; reg && reg != 0x002BCB9Cu && n < 64; n++, reg = MEM32(reg)) {
        uint32_t package = MEM32(reg - 4 + 0x18);
        uint32_t screen = MEM32(reg - 4 + 0x14);
        if (package && screen && (MEM32(package + 0x1C) & 1)) {
            if (MEM32(screen) != 0x001FBBC8u) return screen;
            /* Pause options leaves its selector visible behind the child. */
            uint32_t child_reg = MEM32(0x002BCB9Cu);
            for (unsigned c = 0; child_reg && child_reg != 0x002BCB9Cu && c < 64;
                 c++, child_reg = MEM32(child_reg)) {
                uint32_t child = MEM32(child_reg - 4 + 0x14);
                uint32_t child_package = MEM32(child_reg - 4 + 0x18);
                if (child && child_package && MEM32(child) == 0x001FBC9Cu &&
                    (MEM32(child_package + 0x1C) & 1)) return child;
            }
            return screen;
        }
    }
    return 0;
}

static uint32_t touch_rendered_named_hit(uint32_t hash)
{
    float x = s_touch_x + 320.0f, y = s_touch_y + 240.0f;
    for (unsigned i = 0; i < s_touch_bounds_count; i++) {
        TouchBounds *bounds = &s_touch_bounds[i];
        uint32_t object = bounds->object;
        if (MEM32(object + 0x10) == hash &&
            x >= bounds->left && x < bounds->right &&
            y >= bounds->top && y < bounds->bottom) return object;
    }
    return 0;
}

static uint32_t touch_name_hash(const char *name)
{
    uint32_t hash = 0xFFFFFFFFu;
    for (; *name; name++) {
        unsigned char c = (unsigned char)*name;
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        hash = hash * 33 + c;
    }
    return hash;
}

/* Shared visible arrows used by Customize and Quick Race browsers. */
static uint32_t touch_browser_arrow(uint32_t *message)
{
    for (unsigned side = 0; side < 2; side++) {
        uint32_t object = touch_rendered_named_hit(side ? 0xDB157AC6u : 0x543398D3u);
        if (!object) object = touch_rendered_named_hit(touch_name_hash(
            side ? "select_arrow_right_button" : "select_arrow_left_button"));
        if (!object) continue;
        *message = side ? 0xB5971BF1u : 0x9120409Eu;
        return object;
    }
    return 0;
}

static uint32_t touch_rendered_object(uint32_t hash)
{
    for (unsigned i = 0; i < s_touch_bounds_count; i++)
        if (MEM32(s_touch_bounds[i].object + 0x10) == hash)
            return s_touch_bounds[i].object;
    return 0;
}

/* QROptionsScreen's AF610 handler accepts OptionNText, not the generic
 * OptionName_N convention. Select starts the race, so row taps must never
 * fall through to generic mouse activation. */
static int touch_quick_race_options(uint32_t screen, TouchTarget *target)
{
    if (MEM32(screen) != 0x001FB64Cu) return 0;
    for (unsigned i = 0; i < s_touch_bounds_count; i++) {
        uint32_t object = s_touch_bounds[i].object;
        if (MEM32(object + 0x18) != 2 ||
            (MEM32(object + 0x1C) & 0x04000000u) ||
            MEM32(object + 0x5C) != touch_name_hash("CONTINUE")) continue;
        if (touch_rendered_named_hit(MEM32(object + 0x10))) {
            target->target = object;
            target->message = 0x406415E3u;
            return 1;
        }
    }
    uint32_t direction = 0, arrow = 0;
    TouchBounds captured_arrow = {0};
    float touch_x = s_touch_x + 320.0f, touch_y = s_touch_y + 240.0f;
    for (unsigned i = 0; i < s_qr_arrow_count; i++) {
        TouchBounds *bounds = &s_qr_arrows[i];
        if (touch_x >= bounds->left - 12 && touch_x < bounds->right + 12 &&
            touch_y >= bounds->top - 12 && touch_y < bounds->bottom + 12) {
            arrow = bounds->object;
            captured_arrow = *bounds;
            break;
        }
    }
    if (!arrow) arrow = touch_browser_arrow(&direction);
    if (arrow && (MEM32(arrow + 0x1C) & 0x04000000u)) arrow = 0;
    /* Retain the shared base-arrow binding for layouts which supply it. */
    for (unsigned side = 0; !arrow && side < 2; side++) {
        uint32_t hash = side ? 0xAA68BFD4u : 0xAA68BFCEu;
        uint32_t object = touch_rendered_object(hash);
        TouchBounds bounds;
        float x = s_touch_x + 320.0f, y = s_touch_y + 240.0f;
        if (object && touch_object_bounds(object, &bounds, 0) &&
            x >= bounds.left - 8 && x < bounds.right + 8 &&
            y >= bounds.top - 8 && y < bounds.bottom + 8) {
            arrow = object;
            direction = side ? 0xB5971BF1u : 0x9120409Eu;
        }
    }
    for (unsigned row = 1; row <= 9; row++) {
        char name[32];
        snprintf(name, sizeof(name), "Option%uText", row);
        uint32_t label = touch_rendered_object(touch_name_hash(name));
        if (!label || (MEM32(label + 0x1C) & 0x04000000u)) continue;
        snprintf(name, sizeof(name), "Option%uData", row);
        uint32_t data = touch_rendered_object(touch_name_hash(name));
        TouchBounds row_bounds, arrow_bounds, value_bounds;
        if (!touch_object_bounds(label, &row_bounds, 0)) continue;
        float centre = (row_bounds.top + row_bounds.bottom) * .5f;
        float top = row_bounds.top - 16, bottom = row_bounds.bottom + 16;
        for (unsigned other = 1; other <= 9; other++) {
            if (other == row) continue;
            snprintf(name, sizeof(name), "Option%uText", other);
            uint32_t neighbour = touch_rendered_object(touch_name_hash(name));
            TouchBounds bounds;
            if (!neighbour || !touch_object_bounds(neighbour, &bounds, 0)) continue;
            float y = (bounds.top + bounds.bottom) * .5f;
            if (y < centre) top = fmaxf(top, (y + centre) * .5f);
            if (y > centre) bottom = fminf(bottom, (y + centre) * .5f);
        }
        if (data && touch_object_bounds(data, &value_bounds, 0)) {
            row_bounds.left = fminf(row_bounds.left, value_bounds.left);
            row_bounds.right = fmaxf(row_bounds.right, value_bounds.right);
        }
        float x = s_touch_x + 320.0f, y = s_touch_y + 240.0f;
        int row_hit = x >= row_bounds.left - 12 && x < row_bounds.right + 12 &&
            y >= top && y < bottom;
        int arrow_geometry = arrow && touch_object_bounds(arrow, &arrow_bounds, 0);
        if (captured_arrow.object) { arrow_bounds = captured_arrow; arrow_geometry = 1; }
        int arrow_row = arrow_geometry &&
            (arrow_bounds.top + arrow_bounds.bottom) * .5f >= top &&
            (arrow_bounds.top + arrow_bounds.bottom) * .5f < bottom;
        if (arrow ? arrow_row : row_hit) {
            if (captured_arrow.object && data && touch_object_bounds(data, &value_bounds, 0))
                direction = (captured_arrow.left + captured_arrow.right) <
                    (value_bounds.left + value_bounds.right) ? 0x9120409Eu : 0xB5971BF1u;
            target->target = label;
            target->message = direction ? direction : 1;
            target->focus_only = !direction;
            return 1;
        }
    }
    return 0;
}

/* Option arrows name the row whose native handler must receive Left/Right.
 * The controller-layout screen uses one unnumbered data field instead. */
static int touch_screen_control(uint32_t screen, uint32_t objects, uint32_t count,
                                TouchTarget *target)
{
    if (screen != touch_active_screen()) return 0;
    if (MEM32(screen) == 0x001FC078u) { /* UndergroundBriefScreen, B8F60 */
        uint32_t package = MEM32(screen + 0xC);
        for (unsigned i = 0; i < count; i++) {
            uint32_t object = MEM32(objects + i * 4);
            uint32_t hash = object ? MEM32(object + 0x10) : 0;
            if (hash != 0x0014DD31u && hash != 0x6BAA2200u &&
                hash != 0x0016821Eu) continue; /* EASY, MEDIUM, HARD */
            TouchBounds area;
            if ((MEM32(object + 0x1C) & 0x04000000u) ||
                !touch_object_bounds(object, &area, 0)) continue;
            /* B8B10 shows these three reward captions beneath the choices.
             * Join the rendered caption in this column to its label, including
             * the gap, then add finger tolerance around the whole button. */
            for (unsigned j = 0; j < s_touch_bounds_count; j++) {
                TouchBounds *reward = &s_touch_bounds[j];
                uint32_t reward_hash = MEM32(reward->object + 0x10);
                if (reward_hash != 0x64DDFDFFu && reward_hash != 0x22AD620Eu &&
                    reward_hash != 0x3434E0ACu) continue;
                if (MEM32(reward->object + 0x1C) & 0x04000000u) continue;
                float centre = (reward->left + reward->right) * .5f;
                if (centre < area.left || centre >= area.right) continue;
                area.left = fminf(area.left, reward->left);
                area.right = fmaxf(area.right, reward->right);
                area.bottom = fmaxf(area.bottom, reward->bottom);
            }
            float x = s_touch_x + 320.0f, y = s_touch_y + 240.0f;
            if (x < area.left - 12 || x >= area.right + 12 ||
                y < area.top - 8 || y >= area.bottom + 8) continue;
            target->target = object;
            target->message = 0x0C407210u;
            /* Capture selection before the native mouse hover changes it.
             * The decision lasts for this entire gesture, until release. */
            target->focus_only = object != MEM32(package + 0x80);
            return 1;
        }
    }
    if (touch_quick_race_options(screen, target)) return 1;
    if (MEM32(screen) == 0x001FBC9Cu) { /* nested pause settings, B65E0 */
        float x = s_touch_x + 320.0f, y = s_touch_y + 240.0f;
        for (unsigned row = 1; row <= 8; row++) {
            char name[32]; snprintf(name, sizeof(name), "OptionName_%u", row);
            uint32_t label = touch_rendered_object(touch_name_hash(name));
            TouchBounds area;
            if (!label || (MEM32(label + 0x1C) & 0x04000000u) ||
                !touch_object_bounds(label, &area, 0)) continue;
            float centre = (area.top + area.bottom) * .5f;
            for (unsigned i = 0; i < s_touch_bounds_count; i++) {
                TouchBounds *b = &s_touch_bounds[i];
                if (MEM32(b->object + 0x18) == 2 &&
                    !(MEM32(b->object + 0x1C) & 0x04000000u) &&
                    b->top <= centre && b->bottom >= centre) {
                    area.left = fminf(area.left, b->left);
                    area.right = fmaxf(area.right, b->right);
                }
            }
            if (x >= area.left - 16 && x < area.right + 16 &&
                y >= area.top - 12 && y < area.bottom + 12) {
                target->target = label;
                snprintf(name, sizeof(name), "LeftArrow_%u", row);
                target->message = touch_rendered_named_hit(touch_name_hash(name)) ?
                    0x9120409Eu : 0xB5971BF1u;
                return 1;
            }
        }
    }

    if (MEM32(screen) == 0x001FD734u) { /* PauseMenu, C0040 */
        float x = s_touch_x + 320.0f, y = s_touch_y + 240.0f;
        float left = INFINITY, right = -INFINITY;
        for (unsigned i = 0; i < s_touch_bounds_count; i++) {
            TouchBounds *b = &s_touch_bounds[i];
            if (MEM32(b->object + 0x10) > 4 || MEM32(b->object + 0x18) != 2 ||
                (MEM32(b->object + 0x1C) & 0x04000000u)) continue;
            left = fminf(left, b->left); right = fmaxf(right, b->right);
        }
        for (unsigned i = 0; i < s_touch_bounds_count; i++) {
            TouchBounds *b = &s_touch_bounds[i];
            if (MEM32(b->object + 0x10) > 4 || MEM32(b->object + 0x18) != 2 ||
                (MEM32(b->object + 0x1C) & 0x04000000u)) continue;
            if (x >= left - 16 && x < right + 16 &&
                y >= b->top - 12 && y < b->bottom + 12) {
                target->target = b->object;
                target->message = 0x0C407210u; /* native row click, not Select */
                return 1;
            }
        }
    }

    /* Common screen constructor DE440 stores mouse_over_u/d at +2C/+30.
     * Use the original Up/Down messages (DE5F0), including native endpoints. */
    for (unsigned down = 0; down < 2; down++) {
        uint32_t object = MEM32(screen + 0x2C + down * 4);
        TouchBounds bounds;
        float x = s_touch_x + 320.0f, y = s_touch_y + 240.0f;
        if (!object || MEM32(object + 0x10) != (down ? 0xAA68BFC6u : 0xAA68BFD7u) ||
            (MEM32(object + 0x1C) & 0x04000000u) ||
            !touch_object_bounds(object, &bounds, 0) ||
            x < bounds.left || x >= bounds.right || y < bounds.top || y >= bounds.bottom)
            continue;
        target->target = object;
        target->message = down ? 0x911C0A4Bu : 0x72619778u;
        return 1;
    }
    if (MEM32(screen) == 0x001FEA8Cu) { /* UndergroundMagazineScreen */
        /* D1AD0 paints eight slots at page+slot; D24E0 activates page+cursor.
         * Covers, locks and their groups share the same native slot number. */
        for (unsigned slot = 0; slot < 8; slot++) {
            const uint32_t hashes[] = { 0xBBE88515u + slot,
                0xF1E6EA37u + slot, 0x9DB42DF9u + slot };
            for (unsigned part = 0; part < 3; part++) {
                uint32_t object = touch_rendered_named_hit(hashes[part]);
                if (!object) continue;
                uint32_t page = MEM32(screen + 0x40);
                if (page >= 24 || page + slot >= MEM32(screen + 0x48)) continue;
                target->target = object;
                target->magazine_slot = slot + 1;
                target->magazine_page = page;
                target->message = 0x406415E3u;
                return 1;
            }
        }
    }
    if (MEM32(screen) == 0x001FC12Cu) { /* Underground race map */
        uint32_t list = MEM32(screen + 0x3C);
        uint32_t head = list + 4, node = list ? MEM32(head) : 0;
        for (unsigned i = 0; node && node != head && i < 64; i++, node = MEM32(node)) {
            uint32_t object = MEM32(node + 0xC);
            TouchBounds bounds;
            float x = s_touch_x + 320.0f, y = s_touch_y + 240.0f;
            if (!object || !touch_object_bounds(object, &bounds, 0) ||
                x < bounds.left || x >= bounds.right || y < bounds.top || y >= bounds.bottom)
                continue;
            target->target = object;
            target->list_entry = MEM32(node + 8);
            target->message = 0x406415E3u; /* activate the synchronized event */
            return 1;
        }
    }
    for (unsigned row = 1; row <= 10; row++) {
        for (unsigned side = 0; side < 2; side++) {
            char name[32];
            snprintf(name, sizeof(name), "%sArrow_%u", side ? "Right" : "Left", row);
            if (!touch_rendered_named_hit(touch_name_hash(name))) continue;
            snprintf(name, sizeof(name), "OptionName_%u", row);
            uint32_t hash = touch_name_hash(name);
            for (unsigned i = 0; objects && count <= 4096 && i < count; i++) {
                uint32_t object = MEM32(objects + i * 4);
                if (!object || MEM32(object + 0x10) != hash ||
                    (MEM32(object + 0x1C) & 0x04000000u)) continue;
                target->target = object;
                target->message = side ? 0xB5971BF1u : 0x9120409Eu;
                return 1;
            }
        }
    }
    if (MEM32(screen) == 0x001FDBC8u) { /* OptionsController */
        for (unsigned side = 0; side < 2; side++) {
            const char *names[2] = { side ? "Select_arrow_right" : "Select_arrow_left",
                side ? "select_arrow_right_button" : "select_arrow_left_button" };
            uint32_t base_arrow = MEM32(screen + 0x24 + side * 4);
            TouchBounds bounds;
            float x = s_touch_x + 320.0f, y = s_touch_y + 240.0f;
            int base_hit = base_arrow && touch_object_bounds(base_arrow, &bounds, 0) &&
                x >= bounds.left && x < bounds.right && y >= bounds.top && y < bounds.bottom;
            if (!base_hit && !touch_rendered_named_hit(touch_name_hash(names[0])) &&
                !touch_rendered_named_hit(touch_name_hash(names[1]))) continue;
            target->target = touch_rendered_object(0xEBFB7CC5u);
            if (!target->target) return 0;
            target->message = side ? 0xB5971BF1u : 0x9120409Eu;
            return 1;
        }
    }
    return 0;
}

/* The active typed Customize browser owns the preview gesture. A modal
 * registered ahead of it takes ownership and disables camera touch. */
static uint32_t touch_preview_screen(void)
{
    uint32_t reg = MEM32(0x002BCB9Cu);
    for (unsigned n = 0; reg && reg != 0x002BCB9Cu && n < 64; n++, reg = MEM32(reg)) {
        uint32_t package = MEM32(reg - 4 + 0x18);
        uint32_t screen = MEM32(reg - 4 + 0x14);
        if (!package || !screen || !(MEM32(package + 0x1C) & 1)) continue;
        uint32_t vtable = MEM32(screen);
        uint32_t handler = vtable ? MEM32(vtable + 4) : 0;
        if (vtable == 0x001FDD74u || vtable == 0x001FB8F4u ||
            vtable == 0x001FC1ACu || vtable == 0x001FBFCCu)
            return screen; /* ColorPicker, QuickRaceCarSelect, MainMenuCarCustomize,
                            * ChooseCareerCarScreen */
        return handler >= 0x000EB000u && handler < 0x000F7000u &&
            MEM32(screen + 0x48) == 0x001FF870u ? screen : 0;
    }
    return 0;
}
/* Track selection owns a horizontal browser but no 3D preview camera.
 * B1630 constructs QRTrackSelectScreen (1FB848), browser +48 and selected
 * node +54. Keep it out of touch_preview_screen so map drags cannot orbit. */
static uint32_t touch_carousel_screen(void)
{
    uint32_t screen = touch_active_screen();
    if (screen && (MEM32(screen) == 0x001FB848u ||
                   MEM32(screen) == 0x001FC12Cu)) return screen;
    return touch_preview_screen();
}

/* Only native layout details differ between car and tuning carousels.
 * Capture, arrow lookup, release dispatch and gesture policy are shared. */
typedef struct {
    uint32_t selected_node, activate_message;
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
static int touch_carousel_strip(uint32_t screen)
{
    TouchCarousel binding;
    if (!screen || !touch_carousel_binding(screen, &binding) || !binding.allow_swipe) return 0;
    uint32_t node = binding.selected_node;
    uint32_t object = node ? MEM32(node + 0xC) : 0;
    TouchBounds bounds;
    if (!object || !touch_object_bounds(object, &bounds, 0) ||
        bounds.bottom - bounds.top > 160) return 0;
    float x = s_touch_x + 320.0f, y = s_touch_y + 240.0f;
    return x >= 0 && x < 640 && y >= 0 && y < bounds.bottom + 16;
}

static int touch_carousel_capture(uint32_t screen, TouchTarget *target)
{
    TouchCarousel binding;
    if (!touch_carousel_binding(screen, &binding)) return 0;
    uint32_t message = 0;
    uint32_t object = touch_browser_arrow(&message);
    if (!object) {
        object = binding.selected_node ? MEM32(binding.selected_node + 0xC) : 0;
        TouchBounds bounds;
        float x = s_touch_x + 320.0f, y = s_touch_y + 240.0f;
        if (!object || !touch_object_bounds(object, &bounds, 0) ||
            x < bounds.left || x >= bounds.right || y < bounds.top || y >= bounds.bottom)
            object = 0;
        for (unsigned i = 0; !object && i < 2; i++)
            if (binding.label_hashes[i]) object = touch_rendered_named_hit(binding.label_hashes[i]);
        message = binding.activate_message;
    }
    if (!object && binding.allow_swipe && binding.selected_node) {
        /* The selected icon is only a small part of the visible carousel strip.
         * Capture gaps and side logos from the canvas top through its rendered bottom.
         * A gap owns swipes only: a stationary tap must not open Customize. */
        uint32_t selected = MEM32(binding.selected_node + 0xC);
        if (touch_carousel_strip(screen)) {
            object = selected;
            message = binding.activate_message;
            target->swipe_only = 1;
        }
    }
    if (!object) return 0;
    target->target = object;
    target->message = message;
    target->screen = screen;
    target->carousel = 1;
    target->allow_swipe = binding.allow_swipe;
    target->start_x = s_touch_x;
    target->start_y = s_touch_y;
    return 1;
}

static uint32_t touch_preview_camera(void)
{
    uint32_t reg = MEM32(0x002BCB9Cu);
    for (unsigned n = 0; reg && reg != 0x002BCB9Cu && n < 64; n++, reg = MEM32(reg)) {
        uint32_t screen = MEM32(reg - 4 + 0x14);
        if (!screen || MEM32(screen) != 0x001FB0CCu) continue;
        uint32_t camera = MEM32(screen + 0x5C);
        if (camera && MEM32(camera) == 0x001F8E90u) return camera;
    }
    return 0;
}
static void touch_camera_orbit(float dx, float dy)
{
    uint32_t camera = touch_preview_camera();
    if (!camera || MEM32(camera + 0x2C) == 2) return; /* scripted transition */
    float pitch, yaw;
    memcpy(&pitch, (void *)XBOX_PTR(camera + 0x40), 4);
    memcpy(&yaw, (void *)XBOX_PTR(camera + 0x44), 4);
    if (!isfinite(pitch) || !isfinite(yaw)) return;
    pitch = fmaxf(85, fminf(140, pitch + dy * 90));
    yaw = fmodf(yaw + dx * 180 + 360, 360);
    memcpy((void *)XBOX_PTR(camera + 0x40), &pitch, 4);
    memcpy((void *)XBOX_PTR(camera + 0x44), &yaw, 4);
    MEM32(camera + 0x2C) = 3;
    MEM32(camera + 0x180) = MEM32(camera + 0x184) = MEM32(camera + 0x188) = 0;
    /* Treat touch as manual inspection, resetting the native idle orbit. */
    MEM32(0x002B7570u) = MEM32(0x002CA448u);
    MEM32(0x00247D38u) = 0;
}
static RECOMP_TLS uint32_t s_touch_camera_screen;
static RECOMP_TLS float s_touch_camera_x, s_touch_camera_y;
static RECOMP_TLS float s_touch_camera_start_x, s_touch_camera_start_y;
static RECOMP_TLS int s_touch_camera_moving, s_touch_camera_blocked;


/* Observe the result of the native virtual hit test. Calling its generated
 * body directly avoids recursion through recomp_lookup_manual. */
void feng_touch_hit(void)
{
    uint32_t object = MEM32(esp + 12);
    if (s_touch_poll) {
        TouchBounds bounds = {0};
        float x = (float)s_touch_x + 320.0f, y = (float)s_touch_y + 240.0f;
        int hit = touch_object_bounds(object, &bounds, 0) &&
            x >= bounds.left && x < bounds.right && y >= bounds.top && y < bounds.bottom;
        eax = (eax & 0xFFFFFF00u) | (unsigned)hit;
        esp += 16;
    } else {
        sub_000DDB60();
    }
    if (s_touch_collect_hit && (eax & 0xFF))
        s_touch_targets[s_touch_target_slot].target = object;
}

/* Activate the object found under this gesture in the active package. This
 * applies to menu rows, keyboard keys and dialogs, without screen-name rules. */
void feng_touch_dispatch(void)
{
    uint32_t feng = ecx, package = MEM32(esp + 4);
    uint32_t objects = MEM32(package + 0x84), count = MEM32(package + 0x88);
    int active = s_touch_poll && (MEM32(package + 0x1C) & 1);
    int interactive = active && objects && count && count <= 4096;
    unsigned slot;
    for (slot = 0; slot < s_touch_target_count; slot++)
        if (s_touch_targets[slot].package == package) break;
    if (active && s_touch_prepare && slot == s_touch_target_count && slot < 32) {
        s_touch_targets[slot] = (TouchTarget){ .package = package };
        s_touch_target_count++;
    }
    /* The common screen base owns mouse_over_l/r even when the package
     * has no selectable rows (for example Statistics). Use these native
     * object identities and their rendered geometry, never screen zones. */
    if (active && s_touch_prepare && slot < s_touch_target_count) {
        uint32_t reg = MEM32(0x002BCB9Cu);
        for (unsigned n = 0; reg && reg != 0x002BCB9Cu && n < 64; n++, reg = MEM32(reg)) {
            uint32_t screen = MEM32(reg - 4 + 0x14);
            if (MEM32(reg - 4 + 0x18) != package || !screen) continue;
            if (screen == touch_active_screen()) {
                uint32_t object = touch_back_hit();
                if (object) {
                    TouchTarget *target = &s_touch_targets[slot];
                    target->target = object;
                    target->message = MEM32(screen) == 0x001FBC9Cu ?
                        0xF2D0D887u : 0x911AB364u; /* child closes via native back event */
                    target->screen = screen;
                    target->back = 1;
                    target->start_x = s_touch_x;
                    target->start_y = s_touch_y;
                    continue;
                }
            }
            if (touch_carousel_capture(screen, &s_touch_targets[slot])) continue;
            if (touch_screen_control(screen, objects, count, &s_touch_targets[slot])) {
                TouchTarget *target = &s_touch_targets[slot];
                target->screen = screen;
                target->screen_control = 1;
                target->start_x = s_touch_x;
                target->start_y = s_touch_y;
                continue;
            }
            /* Horizontal browsers draw their arrows in a shared backdrop
             * package. Route those visible objects to the active browser. */
            if (screen == touch_preview_screen() && MEM32(screen) == 0x001FDD74u) {
                for (unsigned i = 0; i < 60; i++) {
                    uint32_t entry = screen + 0x58 + i * 0x2C;
                    uint32_t object = MEM32(entry + 0xC);
                    TouchBounds bounds;
                    float x = s_touch_x + 320.0f, y = s_touch_y + 240.0f;
                    if (!MEM32(entry + 0x10) || !object ||
                        !touch_object_bounds(object, &bounds, 0) ||
                        x < bounds.left || x >= bounds.right ||
                        y < bounds.top || y >= bounds.bottom) continue;
                    TouchTarget *target = &s_touch_targets[slot];
                    target->target = object;
                    target->message = 1; /* palette selection, no purchase/accept */
                    target->screen = screen;
                    target->palette_entry = entry;
                    target->start_x = s_touch_x;
                    target->start_y = s_touch_y;
                    break;
                }
                continue;
            }
            for (unsigned side = 0; side < 2; side++) {
                uint32_t object = MEM32(screen + 0x24 + side * 4);
                TouchBounds bounds;
                uint32_t hash = side ? 0xAA68BFD4u : 0xAA68BFCEu;
                if (!object || MEM32(object + 0x10) != hash ||
                    (MEM32(object + 0x1C) & 0x04000000u) ||
                    !touch_object_bounds(object, &bounds, 0)) continue;
                float x = s_touch_x + 320.0f, y = s_touch_y + 240.0f;
                if (x < bounds.left || x >= bounds.right ||
                    y < bounds.top || y >= bounds.bottom) continue;
                s_touch_targets[slot].target = object;
                s_touch_targets[slot].message = side ? 0xB5971BF1u : 0x9120409Eu;
            }
        }
    }
    if (active && MEM32(touch_active_screen()) == 0x001FB64Cu &&
        (slot >= s_touch_target_count || !s_touch_targets[slot].message)) {
        esp += 8;
        return; /* Only explicit row controls and Continue can act in this modal. */
    }
    if (active && slot < s_touch_target_count && s_touch_targets[slot].message) {
        TouchTarget *target = &s_touch_targets[slot];
        int fire = s_touch_buttons && !s_touch_prepare && !target->fired;
        uint32_t message = target->message;
        if (target->screen) {
            float dx = s_touch_x - target->start_x, dy = s_touch_y - target->start_y;
            target->travel = fmaxf(target->travel, hypotf(dx, dy));
            if (s_touch_buttons) target->armed = 1;
            fire = !s_touch_buttons && !s_touch_prepare && target->armed && !target->fired;
            if (fire) {
                if (target->carousel && target->allow_swipe && fabsf(dx) >= 20 && fabsf(dx) > fabsf(dy) * 1.25f)
                    message = dx > 0 ? 0x9120409Eu : 0xB5971BF1u;
                else if (target->swipe_only || target->travel >= 12) fire = 0; /* moved, not a tap */
                uint32_t current_screen = (target->back || target->screen_control) ?
                    touch_active_screen() :
                    target->carousel ? touch_carousel_screen() : touch_preview_screen();
                if (target->screen != current_screen) fire = 0;
                target->fired = 1;
            }
        }
        if (fire) {
            target->fired = 1;
            uint32_t saved_eax = eax, saved_edx = edx, saved_esp = esp;
            if (target->palette_entry) {
                uint32_t entry = target->palette_entry, screen = target->screen;
                uint32_t row = MEM32(entry + 0x24), column = MEM32(entry + 0x28);
                if (row < 6 && column < 10 && MEM32(entry + 0x10) &&
                    MEM32(entry + 0xC) == target->target) {
                    MEM32(screen + 0xB08) = row;
                    MEM32(screen + 0xB0C) = column;
                    PUSH32(esp, 0);
                    ecx = screen;
                    sub_000C74C0(); /* cursor, current sample and native lock UI */
                    PUSH32(esp, 0);
                    ecx = screen;
                    sub_000C7B40();
                    PUSH32(esp, 100); /* preview only; 101 is the accept path */
                    PUSH32(esp, 0);
                    ecx = screen;
                    sub_000C7A80();
                    MEM8(screen + 0xAFC) = 1;
                    MEM32(screen + 0xAF8) = MEM32(0x002CA448u);
                }
            } else if (target->screen) {
                if (target->magazine_slot) {
                    uint32_t screen = target->screen;
                    if (MEM32(screen) != 0x001FEA8Cu ||
                        MEM32(screen + 0x40) != target->magazine_page ||
                        target->magazine_page + target->magazine_slot - 1 >= MEM32(screen + 0x48)) {
                        eax = saved_eax; edx = saved_edx; ecx = feng; esp = saved_esp + 8;
                        return;
                    }
                    MEM32(screen + 0x3C) = target->magazine_slot - 1;
                    PUSH32(esp, 0);
                    ecx = screen;
                    sub_000D1AD0(); /* native highlight, description and lock UI */
                }
                if (target->list_entry) {
                    uint32_t list = MEM32(target->screen + 0x3C);
                    uint32_t head = list + 4, node = list ? MEM32(head) : 0;
                    int valid = 0;
                    for (unsigned i = 0; node && node != head && i < 64; i++, node = MEM32(node)) {
                        if (MEM32(node + 8) == target->list_entry &&
                            MEM32(node + 0xC) == target->target) { valid = 1; break; }
                    }
                    if (!valid) {
                        eax = saved_eax; edx = saved_edx; ecx = feng; esp = saved_esp + 8;
                        return;
                    }
                    MEM32(target->screen + 0x48) = target->list_entry;
                    PUSH32(esp, target->list_entry);
                    PUSH32(esp, 0);
                    ecx = list;
                    sub_000DF160(); /* native scroll selection */
                    PUSH32(esp, 0);
                    ecx = target->screen;
                    sub_000B8310(); /* refresh route and event details */
                }
                if ((MEM32(target->screen) == 0x001FC078u && !target->back) ||
                    (MEM32(target->screen) == 0x001FB64Cu &&
                    message != 0x406415E3u && message != 0x911AB364u)) {
                    PUSH32(esp, 1);
                    PUSH32(esp, target->target);
                    PUSH32(esp, feng);
                    PUSH32(esp, 0);
                    ecx = package;
                    sub_000E53C0(); /* arrows subsequently belong to this row */
                }
                if (target->focus_only) {
                    PUSH32(esp, 0);
                    ecx = feng;
                    sub_000D7A00(); /* apply queued focus scripts before returning */
                    eax = saved_eax; edx = saved_edx; ecx = feng; esp = saved_esp + 8;
                    return;
                }
                /* Dispatch to the same active screen entry used by native
                 * package notifications, independent of mouse script focus. */
                PUSH32(esp, package);
                PUSH32(esp, 1);
                uint32_t parameter = target->target;
                if (MEM32(target->screen) == 0x001FC078u &&
                    target->screen_control && message == 0x0C407210u) {
                    /* B8F60 forwards this argument to B88C0 as the numeric
                     * race difficulty, not an FEng object pointer. Native
                     * button scripts perform this translation for mouse input. */
                    uint32_t hash = MEM32(target->target + 0x10);
                    parameter = hash == 0x0016821Eu ? 2u :
                        hash == 0x6BAA2200u ? 1u : 0u;
                }
                PUSH32(esp, parameter);
                PUSH32(esp, message);
                PUSH32(esp, 0);
                ecx = target->screen;
                sub_000DE540();
            } else {
                PUSH32(esp, 1);
                PUSH32(esp, (uint32_t)-2);
                PUSH32(esp, package);
                PUSH32(esp, target->target);
                PUSH32(esp, message);
                PUSH32(esp, 0);
                ecx = feng;
                sub_000D6D30();
                PUSH32(esp, 0);
                ecx = feng;
                sub_000D7A00();
            }
            eax = saved_eax; edx = saved_edx; ecx = feng; esp = saved_esp;
        }
        esp += 8;
        return;
    }
    if (interactive && s_touch_prepare && slot < s_touch_target_count) {
        s_touch_target_slot = slot;
        s_touch_collect_hit = 1;
        /* Some lists start with no native FEng focus. The Xbox mouse path
         * exits before scanning candidates in that state. Bootstrap only
         * from a rendered, enabled selectable object under this gesture. */
        if (!MEM32(package + 0x80)) {
            float x = s_touch_x + 320.0f, y = s_touch_y + 240.0f;
            for (uint32_t i = 0; i < count; i++) {
                uint32_t object = MEM32(objects + i * 4);
                TouchBounds bounds;
                if (!object || (MEM32(object + 0x1C) & 0x04000000u) ||
                    !touch_object_bounds(object, &bounds, 0) ||
                    x < bounds.left || x >= bounds.right ||
                    y < bounds.top || y >= bounds.bottom) continue;
                uint32_t saved_eax = eax, saved_edx = edx, saved_esp = esp;
                PUSH32(esp, 1);
                PUSH32(esp, object);
                PUSH32(esp, feng);
                PUSH32(esp, 0);
                ecx = package;
                sub_000E53C0();
                eax = saved_eax; edx = saved_edx; ecx = feng; esp = saved_esp;
                s_touch_targets[slot].target = object;
                break;
            }
        }
    }
    if (interactive && (MEM16(feng + 0x1A) & 1) && !(MEM16(feng + 0x18) & 1)) {
        uint32_t target = slot < s_touch_target_count ? s_touch_targets[slot].target : 0;
        int member = 0;
        for (uint32_t i = 0; target && i < count; i++)
            if (MEM32(objects + i * 4) == target) { member = 1; break; }
        if (!member || (MEM32(target + 0x1C) & 0x04000000u)) {
            MEM32(feng + 0x94) = 0;
            esp += 8; /* A miss must not invoke generic Select on old focus. */
            return;
        }
        if (target != MEM32(package + 0x80)) {
            uint32_t saved_eax = eax, saved_edx = edx, saved_esp = esp;
            PUSH32(esp, 1);
            PUSH32(esp, target);
            PUSH32(esp, feng);
            PUSH32(esp, 0);
            ecx = package;
            sub_000E53C0();
            eax = saved_eax; edx = saved_edx; ecx = feng; esp = saved_esp;
        }
        /* This retail list has a separate data cursor: FEng focus alone
         * does not select a profile, and its controller ignores MouseClick. */
        int profile_select = 0;
        uint32_t registry = MEM32(0x002BCB9Cu);
        for (unsigned n = 0; registry && registry != 0x002BCB9Cu && n < 64; n++) {
            uint32_t record = registry - 4;
            uint32_t screen = MEM32(record + 0x14);
            if (MEM32(record + 0x18) == package && screen &&
                MEM32(screen) == 0x001FC384u && MEM32(screen + 0x3C) == 0x000BD7E0u) {
                uint32_t row = MEM32(screen + 0x88);
                uint32_t data = MEM32(screen + 0x9C);
                for (unsigned r = 0; row && row != screen + 0x88 &&
                     data && data != screen + 0x90 && r < 5; r++) {
                    uint32_t cell = MEM32(row + 8);
                    for (unsigned c = 0; cell && cell != row + 8 && c < 16; c++) {
                        if (MEM32(cell + 8) == target) {
                            MEM32(screen + 0x98) = data;
                            MEM32(screen + 0xA0) = row;
                            profile_select = 1;
                            break;
                        }
                        cell = MEM32(cell);
                    }
                    if (profile_select) break;
                    row = MEM32(row);
                    data = MEM32(data);
                }
            }
            registry = MEM32(registry);
        }
        /* Retail Xbox screens may expose a selectable row without mouse
         * scripts. Pad dispatch still notifies the screen controller in
         * that case; mouse dispatch silently drops the activation. */
        if (profile_select || (!touch_has_message(MEM32(target + 0x3C), 0x0C407210u) &&
            !touch_has_message(MEM32(package + 0x50), 0x406415E3u))) {
            uint32_t saved_eax = eax, saved_edx = edx, saved_esp = esp;
            PUSH32(esp, 1);
            PUSH32(esp, (uint32_t)-2);
            PUSH32(esp, package);
            PUSH32(esp, target);
            PUSH32(esp, 0x406415E3u);
            PUSH32(esp, 0);
            ecx = feng;
            sub_000D6D30();
            eax = saved_eax; edx = saved_edx; ecx = feng; esp = saved_esp + 8;
            return;
        }
    }
    sub_000D8730_gen();
    s_touch_collect_hit = 0;
}

void feng_touch_update(void)
{
    uint32_t feng = ecx;
    uint8_t mouse_enabled = MEM8(feng + 2);
    MenuTouchSample sample = menu_touch_sample();
    float menu_size;
    memcpy(&menu_size, (void *)XBOX_PTR(0x00295B00u), sizeof(menu_size));
    /* Racing HUD uses separate callbacks; an active FEng screen in flow 6
     * is a menu, including the pause confirmation and nested options. */
    uint32_t active_screen = touch_active_screen();
    /* PreRaceStats owns a FEng screen during countdown, but is not pause. */
    atomic_store(&s_race_menu_active, MEM32(NFSU1_GAMEFLOW_STATE) == 6 &&
        active_screen && MEM32(active_screen) != 0x001F9404u);
    int enabled = touch_menu_enabled() &&
        menu_touch_position(&sample, xbox_video_widescreen(), menu_size,
                            &s_touch_x, &s_touch_y);
    if (sample.reset || (!enabled && sample.active)) {
        memset((void *)XBOX_PTR(feng + 0xC), 0, 0x20);
        MEM32(feng + 0x94) = 0; /* mouse press capture, not controller focus */
    }
    if (!touch_menu_enabled()) menu_touch_cancel();
    if (enabled && sample.prepare) {
        /* Ensure hover is resolved even for a tap at (0,0), or a second tap
         * at the same position after controller navigation changed focus. */
        MEM32(feng + 0xC) = 0xFFFF8000u;
        MEM32(feng + 0x10) = 0xFFFF8000u;
    }
    s_touch_buttons = enabled && sample.down ? 1 : 0;
    s_touch_poll = enabled;
    s_touch_prepare = sample.prepare;
    if (sample.prepare || sample.reset) s_touch_target_count = 0;
    MEM8(feng + 2) = enabled;
    sub_000D8B10_gen();
    MEM8(feng + 2) = mouse_enabled;
    s_touch_poll = 0;
    uint32_t garage = enabled ? touch_preview_screen() : 0;
    if (sample.reset || !sample.active || garage != s_touch_camera_screen)
        s_touch_camera_screen = 0;
    if (enabled && sample.prepare) {
        int control = 0;
        for (unsigned i = 0; i < s_touch_target_count; i++)
            if (s_touch_targets[i].target) control = 1;
        s_touch_camera_screen = control ? 0 : garage;
        s_touch_camera_start_x = s_touch_camera_x = sample.x;
        s_touch_camera_start_y = s_touch_camera_y = sample.y;
        s_touch_camera_moving = 0;
        s_touch_camera_blocked = 0;
    }
    float camera_dx = 0, camera_dy = 0;
    int camera_blocked = touch_carousel_strip(s_touch_camera_screen);
    if (s_touch_camera_screen && sample.active && !sample.prepare) {
        if (fabsf(sample.x - s_touch_camera_start_x) +
            fabsf(sample.y - s_touch_camera_start_y) > 0.006f)
            s_touch_camera_moving = 1;
        if (s_touch_camera_moving && !camera_blocked && !s_touch_camera_blocked) {
            camera_dx = sample.x - s_touch_camera_x;
            camera_dy = sample.y - s_touch_camera_y;
        }
        s_touch_camera_x = sample.x;
        s_touch_camera_y = sample.y;
    }
    s_touch_camera_blocked = camera_blocked;
    if (s_touch_camera_screen && !camera_blocked && (sample.down || s_touch_camera_moving))
        touch_camera_orbit(camera_dx, camera_dy);
    if (enabled && sample.prepare) {
        unsigned count = 0;
        for (uint32_t package = MEM32(feng + 0xE4); package && count++ < 8;
             package = MEM32(package + 4)) {
            uint32_t focus = MEM32(package + 0x80);
            fprintf(stderr, "[touch] package=%08X flags=%08X selectable=%u focus=%08X\n",
                    package, MEM32(package + 0x1C), MEM32(package + 0x88),
                    focus ? MEM32(focus + 0x10) : 0);
        }
    }
}

void feng_touch_poll(void)
{
    if (s_touch_poll && MEM32(esp) == 0x000D8B5Eu) {
        uint32_t output = MEM32(esp + 4);
        MEM16(output) = (uint16_t)s_touch_x;
        MEM16(output + 2) = (uint16_t)s_touch_y;
        MEM16(output + 4) = 0; /* wheel */
        MEM16(output + 6) = (uint16_t)s_touch_buttons;
        esp += 8; /* thiscall, ret 4 */
        return;
    }
    sub_000DB860_gen();
}


void feng_touch_frame_begin(void)
{
    s_back_cache_count = 0;
    s_back_icon_count = 0;
    /* Retained Xbox footer icons can use script-generated text rather than
     * a language key. Identify the small glyph immediately left of Back. */
    for (unsigned i = 0; i < s_touch_bounds_count; i++) {
        TouchBounds *caption = &s_touch_bounds[i];
        if (!touch_back_label(caption->object)) continue;
        for (unsigned j = 0; j < s_touch_bounds_count && s_back_icon_count < 8; j++) {
            TouchBounds *icon = &s_touch_bounds[j];
            if (icon->object == caption->object || MEM32(icon->object + 0x18) != 2 ||
                touch_back_label(icon->object) || icon->right - icon->left > 40 ||
                icon->bottom - icon->top > 48) continue;
            if (icon->right <= caption->left && icon->right >= caption->left - 48 &&
                icon->bottom >= caption->top && icon->top <= caption->bottom)
                s_back_icons[s_back_icon_count++] = icon->object;
        }
    }

    uint32_t screen = touch_active_screen();
    uint32_t focus = 0;
    uint32_t reg = MEM32(0x002BCB9Cu);
    for (unsigned n = 0; reg && reg != 0x002BCB9Cu && n < 64; n++, reg = MEM32(reg)) {
        if (MEM32(reg - 4 + 0x14) == screen) {
            uint32_t package = MEM32(reg - 4 + 0x18);
            if (package) focus = MEM32(package + 0x80);
            break;
        }
    }
    if (screen != s_qr_screen || focus != s_qr_focus || MEM32(screen) != 0x001FB64Cu)
        s_qr_arrow_count = 0;
    s_qr_screen = screen; s_qr_focus = focus;
    s_touch_bounds_count = 0;
}

uint32_t feng_touch_draw_begin(uint32_t object)
{
    uint32_t previous = s_touch_draw_object;
    s_touch_draw_object = object;
    if (MEM32(NFSU1_GAMEFLOW_STATE) == 6 && feng_touch_race_menu_active() &&
        MEM32(object + 0x18) == 2) {
        uint32_t language = MEM32(object + 0x5C);
        if (language == 0x4F876B6Fu || language == 0xE03C840Au) {
            MEM32(object + 0x5C) = language == 0x4F876B6Fu ? 0x001335F0u : 0xE03C840Bu;
            MEM32(object + 0x1C) |= 0x02400000u;
        }
    }

    if (MEM32(NFSU1_GAMEFLOW_STATE) == 3 &&
        MEM32(touch_active_screen()) == 0x001FB64Cu &&
        MEM32(object + 0x18) == 2 &&
        MEM32(object + 0x5C) == touch_name_hash("MODE_OPTIONS_TITLE")) {
        /* Reuse the native modal heading as a visible localized confirmation.
         * Its cached glyph bounds supply the touch target. */
        MEM32(object + 0x5C) = touch_name_hash("CONTINUE");
        MEM32(object + 0x1C) |= 0x02400000u;
    }

    if (touch_menu_enabled() && touch_back_label(object) &&
        MEM32(object + 0x5C) == 0x00169268u) {
        MEM32(object + 0x5C) = 0x001335F0u;
        MEM32(object + 0x1C) |= 0x02400000u;
    }
    return previous;
}

void feng_touch_draw_end(uint32_t previous) { s_touch_draw_object = previous; }

int feng_touch_draw_cache(uint32_t cache)
{
    if (!s_touch_draw_object) return 0;
    uint32_t head = cache + 0x18, entry = MEM32(head);
    for (unsigned visited = 0; entry && entry != head && visited < 4096; visited++) {
        for (unsigned i = 0; i < 4; i++) {
            float x, y;
            memcpy(&x, (void *)XBOX_PTR(entry + 0x20 + i * 16), 4);
            memcpy(&y, (void *)XBOX_PTR(entry + 0x24 + i * 16), 4);
            touch_bounds_add(s_touch_draw_object, x, y);
            /* Cached quad RGBA array starts at quad+80 (entry+90), as
             * consumed by 192C0/192A0. Keep Back fully opaque and white. */
            if (touch_back_label(s_touch_draw_object)) MEM32(entry + 0x90 + i * 4) = 0xFFFFFFFFu;
        }
        entry = MEM32(entry);
    }
    if (MEM32(s_qr_screen) == 0x001FB64Cu &&
        MEM32(s_touch_draw_object + 0x10) == 0x13E9F3A9u) {
        TouchBounds bounds;
        if (touch_object_bounds(s_touch_draw_object, &bounds, 0)) {
            unsigned slot;
            for (slot = 0; slot < s_qr_arrow_count; slot++)
                if (s_qr_arrows[slot].object == s_touch_draw_object) break;
            if (slot < 2) {
                s_qr_arrows[slot] = bounds;
                if (slot == s_qr_arrow_count) s_qr_arrow_count++;
            }
        }
    }
    if (touch_menu_enabled() && touch_back_label(s_touch_draw_object)) {
        if (s_back_cache_count < 16) s_back_caches[s_back_cache_count++] = cache;
        return 1; /* draw above modal dimming panels at the end of FEng */
    }
    for (unsigned i = 0; i < s_back_icon_count; i++)
        if (s_back_icons[i] == s_touch_draw_object) return 1;
    return touch_menu_enabled() && (touch_help_icon(s_touch_draw_object) ||
        touch_pause_back_icon(s_touch_draw_object));
}

/* Replay only the native Back text after all menu layers, preserving guest
 * registers and cached geometry. This also keeps the inherited footer usable
 * when a nested settings panel dims its parent package. */
void feng_touch_frame_end(void)
{
    uint32_t saved_eax = eax, saved_ecx = ecx, saved_edx = edx, saved_esp = esp;
    for (unsigned i = 0; i < s_back_cache_count; i++) {
        ecx = s_back_caches[i];
        PUSH32(esp, 0);
        sub_000F8B80_gen();
        esp = saved_esp;
    }
    eax = saved_eax; ecx = saved_ecx; edx = saved_edx; esp = saved_esp;
}
