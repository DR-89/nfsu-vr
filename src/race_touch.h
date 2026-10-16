#ifndef NFSU_RACE_TOUCH_H
#define NFSU_RACE_TOUCH_H
#include <stdint.h>
/* Bit order matches RaceControlsView. Only the guest input thread consumes. */
enum RaceTouchButton {
    RACE_MENU = 1u, RACE_GAS = 2u, RACE_BRAKE = 4u,
    RACE_HANDBRAKE = 8u, RACE_SHIFT_UP = 16u, RACE_SHIFT_DOWN = 32u,
    RACE_NITRO = 64u, RACE_LEFT = 128u, RACE_RIGHT = 256u
};
void race_touch_set(uint32_t mask);
void race_touch_cancel(void);
void race_touch_enable(int enabled);
uint32_t race_touch_consume(int racing);
int race_touch_visible(void);
#endif
