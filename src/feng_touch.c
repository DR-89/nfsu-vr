#include "../xboxrecomp/include/nfsu_log.h"
#include "input_mode.h"
/* Native FEng touch engine. The guest thread owns geometry, gestures and
 * dispatch; menu_touch.c supplies the synchronized host event queue.
 * Shared geometry/camera policies are reused by the screen bindings below. */
#define RECOMP_GENERATED_CODE
#include "recomp_funcs.h"
#include "feng_touch.h"
#include "feng_touch_internal.h"
#include "touch_orbit.h"
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
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
static RECOMP_TLS TouchTarget s_touch_targets[32];
static RECOMP_TLS unsigned s_touch_target_count, s_touch_target_slot;
static RECOMP_TLS unsigned s_touch_collect_hit;
static RECOMP_TLS uint32_t s_touch_draw_object;

static TouchBounds s_touch_bounds[2048];
static unsigned s_touch_bounds_count;
static uint32_t s_back_caches[16], s_back_icons[8];
static unsigned s_back_cache_count, s_back_icon_count;
extern void sub_000F8B80_gen(void);

/* Undo touch-only guest edits when a physical pad takes ownership. Keep
 * the owner's identity so an object freed/replaced by FEng is not restored. */
static struct { uint32_t address, original, replacement, owner, hash; } s_touch_edits[8192];
static unsigned s_touch_edit_count;
static void touch_edit(uint32_t address, uint32_t value, uint32_t owner)
{
    for (unsigned i = 0; i < s_touch_edit_count; i++) {
        if (s_touch_edits[i].address == address && s_touch_edits[i].owner == owner &&
            s_touch_edits[i].hash == MEM32(owner + 0x10)) {
            s_touch_edits[i].replacement = value;
            MEM32(address) = value;
            return;
        }
    }
    if (s_touch_edit_count >= 8192) return;
    unsigned i = s_touch_edit_count++;
    s_touch_edits[i].address = address;
    s_touch_edits[i].original = MEM32(address);
    s_touch_edits[i].replacement = value;
    s_touch_edits[i].owner = owner;
    s_touch_edits[i].hash = MEM32(owner + 0x10);
    MEM32(address) = value;
}
static void touch_restore(void)
{
    while (s_touch_edit_count) {
        unsigned i = --s_touch_edit_count;
        uint32_t owner = s_touch_edits[i].owner;
        if (MEM32(owner + 0x10) != s_touch_edits[i].hash) continue;
        uint32_t address = s_touch_edits[i].address;
        if (MEM32(address) == s_touch_edits[i].replacement) {
            MEM32(address) = s_touch_edits[i].original;
            MEM32(owner + 0x1C) |= 0x02400000u; /* rebuild native caption cache */
        }
    }
}

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

int touch_object_bounds(uint32_t object, TouchBounds *out, unsigned depth)
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
            touch_bounds_include(out, &bounds);
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
uint32_t touch_active_screen(void)
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

uint32_t touch_rendered_named_hit(uint32_t hash)
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

uint32_t touch_name_hash(const char *name)
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
uint32_t touch_browser_arrow(uint32_t *message)
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

uint32_t touch_rendered_object(uint32_t hash)
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
static uint32_t s_difficulty_screen, s_difficulty_choice;

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
            uint32_t focus = MEM32(package + 0x80);
            uint32_t selected = focus ? MEM32(focus + 0x10) : 0;
            if (s_difficulty_screen == screen && s_difficulty_choice)
                selected = s_difficulty_choice;
            target->focus_only = hash != selected;
            return 1;
        }
    }
    if (touch_quick_race_options(screen, target)) return 1;
    if (MEM32(screen) == 0x001FBC9Cu) { /* nested pause settings, B65E0 */
        float x = s_touch_x + 320.0f, y = s_touch_y + 240.0f;
        for (unsigned row = 1; row <= 8; row++) {
            char name[32]; snprintf(name, sizeof(name), "OptionName_%u", row);
            uint32_t hash = touch_name_hash(name);
            uint32_t label = touch_rendered_object(hash);
            // Prefer the rendered row title. Some package variants instead
            // expose only a child quad; keep that fallback for their hit area.
            for (unsigned i = 0; !label && objects && count <= 4096 && i < count; i++) {
                uint32_t object = MEM32(objects + i * 4);
                if (object && MEM32(object + 0x10) == hash) label = object;
            }
            snprintf(name, sizeof(name), "OptionText_%u", row);
            uint32_t caption = touch_rendered_object(touch_name_hash(name));
            TouchBounds area;
            if (!label || (MEM32(label + 0x1C) & 0x04000000u)) continue;
            if (!touch_object_bounds(label, &area, 0) &&
                (!caption || !touch_object_bounds(caption, &area, 0))) continue;
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
            !touch_bounds_contains(&bounds, x, y))
            continue;
        target->target = object;
        target->message = down ? 0x911C0A4Bu : 0x72619778u;
        return 1;
    }
    if (MEM32(screen) == 0x001FEA8Cu && touch_magazine_capture(screen, target, s_touch_x + 320.0f, s_touch_y + 240.0f)) return 1;
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
        if (MEM32(0x002BBB54u) == 3 &&
                touch_rendered_named_hit(touch_name_hash("OptionName_7"))) {
            target->target = touch_rendered_object(touch_name_hash("OptionName_7"));
            target->message = 0x0C407210u;
            return target->target != 0;
        }
        for (unsigned side = 0; side < 2; side++) {
            const char *names[2] = { side ? "Select_arrow_right" : "Select_arrow_left",
                side ? "select_arrow_right_button" : "select_arrow_left_button" };
            uint32_t base_arrow = MEM32(screen + 0x24 + side * 4);
            TouchBounds bounds;
            float x = s_touch_x + 320.0f, y = s_touch_y + 240.0f;
            int base_hit = base_arrow && touch_object_bounds(base_arrow, &bounds, 0) &&
                touch_bounds_contains(&bounds, x, y);
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
uint32_t touch_preview_screen(void)
{
    uint32_t reg = MEM32(0x002BCB9Cu);
    for (unsigned n = 0; reg && reg != 0x002BCB9Cu && n < 64; n++, reg = MEM32(reg)) {
        uint32_t package = MEM32(reg - 4 + 0x18);
        uint32_t screen = MEM32(reg - 4 + 0x14);
        if (!package || !screen || !(MEM32(package + 0x1C) & 1)) continue;
        uint32_t vtable = MEM32(screen);
        uint32_t handler = vtable ? MEM32(vtable + 4) : 0;
        if (vtable == 0x001FDD74u || vtable == 0x001FB8F4u ||
            vtable == 0x001FC1ACu || vtable == 0x001FBFCCu ||
            vtable == 0x001FC26Cu || vtable == 0x001FC278u)
            return screen; /* ColorPicker, QuickRaceCarSelect, MainMenuCarCustomize,
                            * ChooseCareerCarScreen */
        return handler >= 0x000EB000u && handler < 0x000F7000u &&
            MEM32(screen + 0x48) == 0x001FF870u ? screen : 0;
    }
    return 0;
}
static RECOMP_TLS TouchOrbit s_touch_orbit;

/* Profile rows and native dialog buttons need finger-sized hit bounds.
 * Keep geometry tied to rendered objects, and divide adjacent targets at
 * their gap midpoint so the extra tolerance cannot steal a neighbour. */
static int touch_selection_bounds(uint32_t object, uint32_t package, TouchBounds *area)
{
    if (!object || (MEM32(object + 0x1C) & 0x04000000u) ||
        !touch_object_bounds(object, area, 0)) return 0;
    uint32_t screen = touch_active_screen();
    uint32_t hash = MEM32(object + 0x10);
    int profile = screen && MEM32(screen) == 0x001FC384u &&
        MEM32(screen + 0x3C) == 0x000BD7E0u;
    int dialog = hash >= 0xB8A7C6CBu && hash <= 0xB8A7C6CFu; /* BUTTON0..4 */
    if (!profile && !dialog) return 1;
    TouchBounds original = *area;
    area->left -= 20; area->right += 20;
    area->top -= 12; area->bottom += 12;
    uint32_t objects = MEM32(package + 0x84), count = MEM32(package + 0x88);
    for (unsigned i = 0; objects && i < count && i < 4096; i++) {
        uint32_t neighbour = MEM32(objects + i * 4);
        TouchBounds other;
        if (!neighbour || neighbour == object ||
            (MEM32(neighbour + 0x1C) & 0x04000000u) ||
            !touch_object_bounds(neighbour, &other, 0)) continue;
        if (other.top < original.bottom && other.bottom > original.top) {
            if (other.right <= original.left)
                area->left = fmaxf(area->left, (other.right + original.left) * .5f);
            if (other.left >= original.right)
                area->right = fminf(area->right, (other.left + original.right) * .5f);
        }
        if (other.left < original.right && other.right > original.left) {
            if (other.bottom <= original.top)
                area->top = fmaxf(area->top, (other.bottom + original.top) * .5f);
            if (other.top >= original.bottom)
                area->bottom = fminf(area->bottom, (other.top + original.bottom) * .5f);
        }
    }
    return 1;
}

/* Observe the result of the native virtual hit test. Calling its generated
 * body directly avoids recursion through recomp_lookup_manual. */
void feng_touch_hit(void)
{
    if (!input_mode_sensor_enabled()) { sub_000DDB60(); return; }
    uint32_t object = MEM32(esp + 12);
    if (s_touch_poll) {
        TouchBounds bounds = {0};
        float x = (float)s_touch_x + 320.0f, y = (float)s_touch_y + 240.0f;
        int hit = touch_selection_bounds(object,
            s_touch_targets[s_touch_target_slot].package, &bounds) &&
            touch_bounds_contains(&bounds, x, y);
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
/* All paint categories use ColorPicker; B14 remains owned by the native
 * controller, including availability, price and confirmation dialogs. */
static uint32_t s_paint_screen, s_paint_entry, s_paint_color;
static uint64_t s_paint_tap_ms;

static void touch_paint_reset(void)
{
    s_paint_screen = s_paint_entry = s_paint_color = 0;
}

static int touch_paint_capture(uint32_t screen, TouchTarget *target, float x, float y)
{
    if (screen != touch_preview_screen() || MEM32(screen) != 0x001FDD74u) return 0;
    for (unsigned i = 0; i < 60; i++) {
        uint32_t entry = screen + 0x58 + i * 0x2C;
        uint32_t object = MEM32(entry + 0xC);
        TouchBounds bounds;
        if (!MEM32(entry + 0x10) || !object ||
            !touch_object_bounds(object, &bounds, 0) ||
            x < bounds.left || x >= bounds.right ||
            y < bounds.top || y >= bounds.bottom) continue;
        target->target = object;
        target->message = 1; /* palette selection, no purchase/accept */
        target->palette_entry = entry;
        touch_target_begin(target, screen, s_touch_x, s_touch_y);
        return 1;
    }
    return 1; /* Empty palette cells must not fall through to native mouse input. */
}

static void touch_paint_select(const TouchTarget *target)
{
    uint32_t entry = target->palette_entry, screen = target->screen;
    uint32_t row = MEM32(entry + 0x24), column = MEM32(entry + 0x28);
    if (row >= 6 || column >= 10 || !MEM32(entry + 0x10) ||
        MEM32(entry + 0xC) != target->target) { touch_paint_reset(); return; }
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint64_t ms = (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
    uint32_t color = MEM32(entry + 0x10);
    int accept = s_paint_screen == screen && s_paint_entry == entry &&
        s_paint_color == color && ms - s_paint_tap_ms <= 350 &&
        MEM32(screen + 0xB08) == row && MEM32(screen + 0xB0C) == column;
    s_paint_screen = screen; s_paint_entry = entry; s_paint_color = color;
    s_paint_tap_ms = ms;
    MEM32(screen + 0xB08) = row;
    MEM32(screen + 0xB0C) = column;
    PUSH32(esp, 0); ecx = screen; sub_000C74C0();
    PUSH32(esp, 0); ecx = screen; sub_000C7B40();
    PUSH32(esp, 100);
    PUSH32(esp, 0); ecx = screen; sub_000C7A80();
    MEM8(screen + 0xAFC) = 1;
    MEM32(screen + 0xAF8) = MEM32(0x002CA448u);
    if (accept) {
        touch_paint_reset();
        PUSH32(esp, target->package);
        PUSH32(esp, 1);
        PUSH32(esp, target->target);
        PUSH32(esp, 0x406415E3u); /* Native Select preserves locks and purchase flow. */
        PUSH32(esp, 0); ecx = screen; sub_000DE540();
    }
}


void feng_touch_dispatch(void)
{
    if (!input_mode_sensor_enabled()) { sub_000D8730_gen(); return; }
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
                    target->back = 1;
                    touch_target_begin(target, screen, s_touch_x, s_touch_y);
                    continue;
                }
            }
            if (touch_carousel_capture(screen, &s_touch_targets[slot],
                    s_touch_x + 320.0f, s_touch_y + 240.0f)) continue;
            if (touch_screen_control(screen, objects, count, &s_touch_targets[slot])) {
                TouchTarget *target = &s_touch_targets[slot];
                target->screen_control = 1;
                touch_target_begin(target, screen, s_touch_x, s_touch_y);
                continue;
            }
            /* Horizontal browsers draw their arrows in a shared backdrop
             * package. Route those visible objects to the active browser. */
            if (touch_paint_capture(screen, &s_touch_targets[slot],
                    s_touch_x + 320.0f, s_touch_y + 240.0f)) continue;
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
        int fire = s_touch_buttons && !s_touch_prepare && !target->gesture.fired;
        uint32_t message = target->message;
        if (target->screen) {
            TouchSwipeAxis axis = target->swipe_axis;
            TouchGestureResult result = touch_gesture_update(&target->gesture,
                s_touch_x, s_touch_y, s_touch_buttons, s_touch_prepare,
                axis, target->swipe_distance, target->swipe_only);
            if (target->palette_entry && result == TOUCH_GESTURE_CANCEL) touch_paint_reset();
            fire = result == TOUCH_GESTURE_TAP || result == TOUCH_GESTURE_NEGATIVE ||
                result == TOUCH_GESTURE_POSITIVE;
            if (fire) {
                if (result == TOUCH_GESTURE_NEGATIVE)
                    message = axis == TOUCH_SWIPE_VERTICAL ? 0x911C0A4Bu : 0xB5971BF1u;
                if (result == TOUCH_GESTURE_POSITIVE)
                    message = axis == TOUCH_SWIPE_VERTICAL ? 0x72619778u : 0x9120409Eu;
                uint32_t current_screen = (target->back || target->screen_control) ?
                    touch_active_screen() :
                    target->carousel ? touch_carousel_screen() : touch_preview_screen();
                if (target->screen != current_screen) fire = 0;
                if (target->magazine_swipe &&
                    MEM32(target->screen + 0x40) != target->magazine_page) fire = 0;
                target->gesture.fired = 1;
            }
        }
        if (fire) {
            target->gesture.fired = 1;
            uint32_t saved_eax = eax, saved_edx = edx, saved_esp = esp;
            if (target->palette_entry) {
                touch_paint_select(target);
            } else if (target->screen) {
                int valid = 1;
                if (target->magazine_slot && message == 0x406415E3u)
                    valid = touch_magazine_select(target);
                if (valid && target->list_entry && message == target->message)
                    valid = touch_carousel_select(target);
                if (!valid) {
                    eax = saved_eax; edx = saved_edx; ecx = feng; esp = saved_esp + 8;
                    return;
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
                if (MEM32(target->screen) == 0x001FC078u && target->screen_control) {
                    s_difficulty_screen = target->screen;
                    s_difficulty_choice = MEM32(target->target + 0x10);
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
                PUSH32(esp, target->target);
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
                    !touch_selection_bounds(object, package, &bounds) ||
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

static int s_touch_was_enabled = 1;

void feng_touch_update(void)
{
    if (!input_mode_sensor_enabled()) {
        if (s_touch_was_enabled) {
            touch_restore();
            memset((void *)XBOX_PTR(ecx + 0xC), 0, 0x20);
            MEM32(ecx + 0x94) = 0;
            s_touch_was_enabled = 0;
        }
        s_touch_poll = s_touch_prepare = s_touch_buttons = s_touch_target_count = 0;
        touch_orbit_reset(&s_touch_orbit);
        touch_paint_reset();
        s_difficulty_screen = s_difficulty_choice = 0;
        s_touch_bounds_count = s_qr_arrow_count = s_back_cache_count = s_back_icon_count = 0;
        atomic_store(&s_race_menu_active, 0);
        sub_000D8B10_gen();
        return;
    }
    s_touch_was_enabled = 1;
    uint32_t feng = ecx;
    uint8_t mouse_enabled = MEM8(feng + 2);
    MenuTouchSample sample = menu_touch_sample();
    float menu_size;
    memcpy(&menu_size, (void *)XBOX_PTR(0x00295B00u), sizeof(menu_size));
    /* Racing HUD uses separate callbacks; an active FEng screen in flow 6
     * is a menu, including the pause confirmation and nested options. */
    uint32_t active_screen = touch_active_screen();
    if (active_screen != s_difficulty_screen || MEM32(active_screen) != 0x001FC078u) {
        s_difficulty_screen = 0;
        s_difficulty_choice = 0;
    }
    /* PreRaceStats owns a FEng screen during countdown, but is not pause. */
    atomic_store(&s_race_menu_active, MEM32(NFSU1_GAMEFLOW_STATE) == 6 &&
        active_screen && MEM32(active_screen) != 0x001F9404u);
    int enabled = touch_menu_enabled() &&
        menu_touch_position(&sample, xbox_video_widescreen(), menu_size,
                            &s_touch_x, &s_touch_y);
    if (sample.reset || !touch_menu_enabled() || active_screen != s_paint_screen) touch_paint_reset();
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
    if (sample.prepare) {
        int palette = 0;
        for (unsigned i = 0; i < s_touch_target_count; i++)
            if (s_touch_targets[i].palette_entry) palette = 1;
        if (!palette) touch_paint_reset();
    }
    s_touch_poll = 0;
    uint32_t preview = enabled ? touch_preview_screen() : 0;
    int control_captured = 0;
    for (unsigned i = 0; i < s_touch_target_count; i++)
        if (s_touch_targets[i].target) control_captured = 1;
    int blocked = touch_carousel_strip(s_touch_orbit.owner,
        s_touch_x + 320.0f, s_touch_y + 240.0f);
    TouchOrbitDelta orbit = touch_orbit_update(&s_touch_orbit, preview, &sample,
        enabled, control_captured, blocked);
    if (orbit.active) touch_camera_orbit(orbit.dx, orbit.dy);
    if (enabled && sample.prepare) {
        unsigned count = 0;
        for (uint32_t package = MEM32(feng + 0xE4); package && count++ < 8;
             package = MEM32(package + 4)) {
            uint32_t focus = MEM32(package + 0x80);
            NFS_LOG_INFO("input", "[touch] package=%08X flags=%08X selectable=%u focus=%08X\n",
                    package, MEM32(package + 0x1C), MEM32(package + 0x88),
                    focus ? MEM32(focus + 0x10) : 0);
        }
    }
}

void feng_touch_poll(void)
{
    if (input_mode_sensor_enabled() && s_touch_poll && MEM32(esp) == 0x000D8B5Eu) {
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
    if (!input_mode_sensor_enabled()) { touch_restore(); return; }
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
    if (!input_mode_sensor_enabled()) return previous;
    s_touch_draw_object = object;
    if (MEM32(NFSU1_GAMEFLOW_STATE) == 6 && feng_touch_race_menu_active() &&
        MEM32(object + 0x18) == 2) {
        uint32_t language = MEM32(object + 0x5C);
        if (language == 0x4F876B6Fu || language == 0xE03C840Au) {
            touch_edit(object + 0x5C, language == 0x4F876B6Fu ? 0x001335F0u : 0xE03C840Bu, object);
            MEM32(object + 0x1C) |= 0x02400000u;
        }
    }

    if (MEM32(NFSU1_GAMEFLOW_STATE) == 3 &&
        MEM32(touch_active_screen()) == 0x001FB64Cu &&
        MEM32(object + 0x18) == 2 &&
        MEM32(object + 0x5C) == touch_name_hash("MODE_OPTIONS_TITLE")) {
        /* Reuse the native modal heading as a visible localized confirmation.
         * Its cached glyph bounds supply the touch target. */
        touch_edit(object + 0x5C, touch_name_hash("CONTINUE"), object);
        MEM32(object + 0x1C) |= 0x02400000u;
    }

    if (touch_menu_enabled() && touch_back_label(object) &&
        MEM32(object + 0x5C) == 0x00169268u) {
        touch_edit(object + 0x5C, 0x001335F0u, object);
        MEM32(object + 0x1C) |= 0x02400000u;
    }
    return previous;
}

void feng_touch_draw_end(uint32_t previous) { s_touch_draw_object = previous; }

int feng_touch_draw_cache(uint32_t cache)
{
    if (!input_mode_sensor_enabled() || !s_touch_draw_object) return 0;
    uint32_t head = cache + 0x18, entry = MEM32(head);
    for (unsigned visited = 0; entry && entry != head && visited < 4096; visited++) {
        for (unsigned i = 0; i < 4; i++) {
            float x, y;
            memcpy(&x, (void *)XBOX_PTR(entry + 0x20 + i * 16), 4);
            memcpy(&y, (void *)XBOX_PTR(entry + 0x24 + i * 16), 4);
            touch_bounds_add(s_touch_draw_object, x, y);
            /* Cached quad RGBA array starts at quad+80 (entry+90), as
             * consumed by 192C0/192A0. Keep Back fully opaque and white. */
            if (touch_back_label(s_touch_draw_object))
                touch_edit(entry + 0x90 + i * 4, 0xFFFFFFFFu, s_touch_draw_object);
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
    if (!input_mode_sensor_enabled()) return;
    uint32_t saved_eax = eax, saved_ecx = ecx, saved_edx = edx, saved_esp = esp;
    for (unsigned i = 0; i < s_back_cache_count; i++) {
        ecx = s_back_caches[i];
        PUSH32(esp, 0);
        sub_000F8B80_gen();
        esp = saved_esp;
    }
    eax = saved_eax; ecx = saved_ecx; edx = saved_edx; esp = saved_esp;
}
