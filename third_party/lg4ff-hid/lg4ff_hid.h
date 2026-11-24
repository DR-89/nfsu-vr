/*
  lg4ff-hid: Logitech G29 / G923 (PlayStation/PC) racing wheels through SDL's HIDAPI,
  with force feedback, for games on Android (Meta Quest), Linux, macOS and Windows.

  Copyright (C) 2025 Simon Wood <simon@mungewell.org>
  Copyright (C) 2025 Michal Malý <madcatxster@devoid-pointer.net>
  Copyright (C) 2025 Katharine Chui <katharine.chui@gmail.com>
  Copyright (C) 2026 lg4ff-hid contributors

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#ifndef LG4FF_HID_H
#define LG4FF_HID_H

/*
  Two files, no dependencies besides SDL2 (2.0.18 or newer) or SDL3.
  Define LG4FF_SDL3 when building against SDL3; SDL2 is the default.

  The wheel is opened directly through SDL_hid_*, so it does not need a patched SDL and
  is independent of SDL's joystick/gamepad layer. On Android SDL's Java HIDDeviceManager
  does the USB work (SDLActivity sets it up), and the first open shows the system USB
  permission dialog.

  Typical use:

      lg4ff_init();
      LG4FF_Wheel *wheel = lg4ff_open();          // blocking, see below; NULL = not yet
      lg4ff_set_range(wheel, 900);
      lg4ff_set_autocenter(wheel, 0.3f);
      ...
      LG4FF_State state;                           // every frame
      if (lg4ff_update(wheel, &state) < 0) { lg4ff_close(wheel); wheel = NULL; }
      steer(state.steering); throttle(state.throttle); ...
      lg4ff_set_constant_force(wheel, -0.2f);      // pull to the left

  Call everything for one wheel from one thread at a time.
*/

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LG4FF_Wheel LG4FF_Wheel;

/* Bits of LG4FF_State.buttons, named after the PlayStation wheel's labels. Bits 12..18
   (Driving Force Shifter) and 21..22 (dial) are reported but were not verified. */
enum
{
    LG4FF_BUTTON_CROSS = 1u << 0,
    LG4FF_BUTTON_SQUARE = 1u << 1,
    LG4FF_BUTTON_CIRCLE = 1u << 2,
    LG4FF_BUTTON_TRIANGLE = 1u << 3,
    LG4FF_BUTTON_PADDLE_RIGHT = 1u << 4, /* R1 */
    LG4FF_BUTTON_PADDLE_LEFT = 1u << 5,  /* L1 */
    LG4FF_BUTTON_R2 = 1u << 6,
    LG4FF_BUTTON_L2 = 1u << 7,
    LG4FF_BUTTON_SHARE = 1u << 8,
    LG4FF_BUTTON_OPTIONS = 1u << 9,
    LG4FF_BUTTON_R3 = 1u << 10,
    LG4FF_BUTTON_L3 = 1u << 11,
    LG4FF_BUTTON_PLUS = 1u << 19,
    LG4FF_BUTTON_MINUS = 1u << 20,
    LG4FF_BUTTON_ENTER = 1u << 23,
    LG4FF_BUTTON_PS = 1u << 24
};

typedef struct
{
    float steering;   /* -1 full left .. +1 full right, over the current range */
    float throttle;   /* 0 released .. 1 fully pressed */
    float brake;      /* 0 .. 1 */
    float clutch;     /* 0 .. 1 */
    uint32_t buttons; /* LG4FF_BUTTON_* */
    int dpad;         /* -1 centered, else 0 up, 1 up-right, 2 right ... 7 up-left */
} LG4FF_State;

/* SDL_hid_init(). Returns 0 on success. */
int lg4ff_init(void);
/* SDL_hid_exit(). Close all wheels first. */
void lg4ff_quit(void);

/*
  Opens the first supported wheel, or returns NULL.
  A G923 for PlayStation starts in a compatibility mode (046d:c267). lg4ff_open() then
  sends it the switch to native mode and returns NULL; the wheel re-enumerates as
  046d:c266 within about a second, so keep calling lg4ff_open() (e.g. once a second).
  Blocking: on Android each open of a not yet permitted USB device waits until the user
  answers the permission dialog (SDL waits up to 60 s), so call it from a worker thread
  if your frame loop must not stall. A wheel that fails to open (permission denied) is
  not tried again until it is unplugged and reconnected, so polling never re-prompts.
*/
LG4FF_Wheel *lg4ff_open(void);
void lg4ff_close(LG4FF_Wheel *wheel);

/* "Logitech G923" or "Logitech G29" */
const char *lg4ff_name(const LG4FF_Wheel *wheel);

/*
  Reads every pending input report into *state (the last known state when nothing new
  arrived) and drives the rumble emulation. Call once per frame.
  Returns 1 when new input arrived, 0 when not, -1 when the wheel is gone (then close it).
*/
int lg4ff_update(LG4FF_Wheel *wheel, LG4FF_State *state);

/* Force feedback. Each returns 0 on success, -1 if the write failed. */

/* Rotation lock to lock, 40..900 degrees. */
int lg4ff_set_range(LG4FF_Wheel *wheel, int degrees);
int lg4ff_get_range(const LG4FF_Wheel *wheel);
/* The wheel's built-in centering spring, 0 off .. 1 full strength. */
int lg4ff_set_autocenter(LG4FF_Wheel *wheel, float strength);
/* Constant force, -1 full left .. +1 full right; 0 releases it. Lasts until changed. */
int lg4ff_set_constant_force(LG4FF_Wheel *wheel, float force);
/* Stops the constant force and rumble (the autocenter spring stays). */
int lg4ff_stop_forces(LG4FF_Wheel *wheel);
/*
  Gamepad-style rumble, 0 off .. 1 strongest: lg4ff_update() renders it as a 25 Hz
  square wave of the constant force. Replaces any constant force while active.
*/
int lg4ff_set_rumble(LG4FF_Wheel *wheel, float strength);

#ifdef __cplusplus
}
#endif

#endif /* LG4FF_HID_H */
