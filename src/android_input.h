#ifndef NFSU_ANDROID_INPUT_H
#define NFSU_ANDROID_INPUT_H
#include <stdint.h>
void android_input_poll(void);
int android_input_has_controller(void);
void android_input_set_touch(uint32_t mask);
void android_race_controls_poll(void);

/* Route player-one feedback to the Android vibrator on the native stack. */
void android_device_rumble(unsigned short left, unsigned short right);
#endif
