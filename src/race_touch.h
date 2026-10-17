#ifndef NFSU_RACE_TOUCH_H
#define NFSU_RACE_TOUCH_H
#include <stdint.h>
/* Bit order matches RaceControlsView. Only the guest input thread consumes. */
enum RaceTouchButton {
    RACE_MENU = 1u, RACE_GAS = 2u, RACE_BRAKE = 4u,
    RACE_HANDBRAKE = 8u, RACE_SHIFT_UP = 16u, RACE_SHIFT_DOWN = 32u,
    RACE_NITRO = 64u, RACE_LEFT = 128u, RACE_RIGHT = 256u,
    RACE_CAMERA = 512u
};
/* Controller menu requests are consumed on the Android UI thread. */
void race_touch_request_editor(void);
int race_touch_take_editor_request(void);
void race_touch_set_editing(int enabled);
int race_touch_editing(void);
/* Screen hold is independent of button-overlay visibility. */
void race_touch_enable_hold(int enabled);
void race_touch_set_hold(int down);
int race_touch_hold_enabled(void);
int race_touch_hold_active(void);
/* Guest publishes capabilities; Android only reads the resulting button mask. */
void race_touch_configure_features(int shifting, int nitro);
uint32_t race_touch_button_mask(void);
void race_touch_set(uint32_t mask);
void race_touch_cancel(void);
void race_touch_enable(int enabled);
uint32_t race_touch_consume(int racing);
int race_touch_visible(void);
#endif
