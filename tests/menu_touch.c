#include "input_mode.h"
/* cc -std=c11 -Wall -Wextra -Werror -Isrc tests/menu_touch.c
 * src/menu_touch.c src/input_mode.c -pthread -lm -o /tmp/nfsu-menu-touch-test */
#include "menu_touch.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static MenuTouchSample next(unsigned active, unsigned down, unsigned prepare)
{
    MenuTouchSample sample = menu_touch_sample();
    assert(sample.active == active);
    assert(sample.down == down);
    assert(sample.prepare == prepare);
    return sample;
}

int main(void)
{
    assert(input_mode_get().overlay_requested);
    assert(input_mode_get().overlay_enabled);
    short x, y;
    menu_touch_cancel();
    assert(menu_touch_sample().reset);
    assert(!menu_touch_sample().reset);

    /* A complete tap received between updates still yields hover/down/up. */
    menu_touch_event(MENU_TOUCH_DOWN, .75f, .25f, 1280, 720);
    menu_touch_event(MENU_TOUCH_UP, .75f, .25f, 1280, 720);
    MenuTouchSample sample = next(1, 0, 1);
    assert(menu_touch_position(&sample, 1, 1, &x, &y));
    assert(x == 160 && y == -120);
    next(1, 1, 0);
    next(1, 0, 0);
    next(0, 0, 0);

    /* Held finger, coalesced movement, and a second rapid tap. */
    menu_touch_event(MENU_TOUCH_DOWN, .5f, .5f, 1280, 720);
    next(1, 0, 1);
    menu_touch_event(MENU_TOUCH_MOVE, .6f, .5f, 1280, 720);
    menu_touch_event(MENU_TOUCH_MOVE, .7f, .5f, 1280, 720);
    next(1, 1, 0);
    assert(next(1, 1, 0).x == .7f);
    next(1, 1, 0);
    menu_touch_event(MENU_TOUCH_UP, .7f, .5f, 1280, 720);
    menu_touch_event(MENU_TOUCH_DOWN, .2f, .3f, 1280, 720);
    menu_touch_event(MENU_TOUCH_UP, .2f, .3f, 1280, 720);
    next(1, 0, 0);
    next(1, 0, 1);
    next(1, 1, 0);
    next(1, 0, 0);
    next(0, 0, 0);

    /* Lifecycle cancellation discards queued presses and orphan move/up. */
    menu_touch_event(MENU_TOUCH_DOWN, .5f, .5f, 1280, 720);
    next(1, 0, 1);
    next(1, 1, 0);
    menu_touch_cancel();
    menu_touch_event(MENU_TOUCH_MOVE, .6f, .5f, 1280, 720);
    menu_touch_event(MENU_TOUCH_UP, .6f, .5f, 1280, 720);
    assert(next(0, 0, 0).reset);
    next(0, 0, 0);

    /* Match presentation letterboxing and invert Menu Size about centre. */
    sample = (MenuTouchSample){ .x = .75f, .y = .25f, .width = 1280,
        .height = 720, .active = 1 };
    assert(menu_touch_position(&sample, 1, .8f, &x, &y));
    assert(x == 200 && y == -150);
    sample.x = .01f;
    assert(!menu_touch_position(&sample, 0, 1, &x, &y));
    sample.x = .5f;
    assert(menu_touch_position(&sample, 0, 1, &x, &y));
    assert(x == 0 && y == -120);
    sample.width = sample.height = 1000;
    sample.y = .01f;
    assert(!menu_touch_position(&sample, 1, 1, &x, &y));
    assert(!menu_touch_position(&sample, 1, NAN, &x, &y));

    /* An overloaded queue cancels rather than leaving a held mouse button. */
    for (int i = 0; i < 40; i++) {
        menu_touch_event(MENU_TOUCH_DOWN, .5f, .5f, 1280, 720);
        menu_touch_event(MENU_TOUCH_UP, .5f, .5f, 1280, 720);
    }
    assert(menu_touch_sample().reset);
    menu_touch_cancel();
    menu_touch_event(MENU_TOUCH_DOWN, NAN, .5f, 1280, 720);
    assert(next(0, 0, 0).reset);
    /* Connecting a physical pad drops a queued tap and ignores new touches.
     * Disconnecting starts idle rather than replaying the old gesture. */
    menu_touch_event(MENU_TOUCH_DOWN, .5f, .5f, 1280, 720);
    input_mode_set_controller_present(1);
    assert(!input_mode_sensor_enabled());
    menu_touch_event(MENU_TOUCH_DOWN, .5f, .5f, 1280, 720);
    menu_touch_event(MENU_TOUCH_UP, .5f, .5f, 1280, 720);
    assert(next(0, 0, 0).reset);
    input_mode_set_controller_present(0);
    assert(input_mode_sensor_enabled());
    assert(next(0, 0, 0).reset);
    /* Manual settings never disable the only available input. */
    input_mode_configure(0, 0);
    assert(input_mode_sensor_enabled() && !input_mode_get().overlay_enabled);
    input_mode_set_controller_present(1); /* physical controller arrives */
    assert(!input_mode_sensor_enabled());
    input_mode_configure(1, 0);
    assert(input_mode_sensor_enabled() && !input_mode_get().overlay_enabled);
    input_mode_configure(1, 1);
    assert(input_mode_get().overlay_enabled);
    input_mode_configure(0, 1);
    assert(!input_mode_sensor_enabled() && !input_mode_get().overlay_enabled);
    input_mode_set_controller_present(0); /* physical controller removed */
    assert(input_mode_sensor_enabled() && input_mode_get().overlay_enabled);
    input_mode_configure(-1, 0);
    puts("menu touch queue and presentation mapping: OK");
}
