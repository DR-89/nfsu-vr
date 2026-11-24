/*
  lg4ff-hid: Logitech G29 / G923 (PlayStation/PC) racing wheels through SDL's HIDAPI.

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

/*
  ALTERED SOURCE VERSION. The wheel identification, mode switch, range and autocenter
  commands come from SDL3's src/joystick/hidapi/SDL_hidapi_lg4ff.c (zlib, authors above),
  which ported them from the Linux kernel's drivers/hid/hid-lg4ff.c by Michal Malý and
  Simon Wood. This file reworks them into a standalone library on SDL's public HIDAPI
  and adds the G923 for PlayStation (046d:c266 / c267).
*/

#include "lg4ff_hid.h"

#ifdef LG4FF_SDL3
#include <SDL3/SDL_hidapi.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>
#define LG4FF_TICKS() ((uint32_t)SDL_GetTicks())
#define LG4FF_OPEN_PATH(path) SDL_hid_open_path(path)
#else
#include <SDL_hidapi.h>
#include <SDL_stdinc.h>
#include <SDL_timer.h>
#define LG4FF_TICKS() ((uint32_t)SDL_GetTicks())
#define LG4FF_OPEN_PATH(path) SDL_hid_open_path(path, 0)
#endif

#define LG4FF_VENDOR_LOGITECH 0x046d
#define LG4FF_ID_G29 0xc24f          /* G29, native mode */
#define LG4FF_ID_G923_PS 0xc266      /* G923 for PlayStation/PC, native mode */
#define LG4FF_ID_G923_PS_BOOT 0xc267 /* the same wheel as it powers up */
/* Compatibility modes a G29 may come up in (identified by release number) */
#define LG4FF_ID_WHEEL 0xc294
#define LG4FF_ID_DFP 0xc298
#define LG4FF_ID_G25 0xc299
#define LG4FF_ID_DFGT 0xc29a
#define LG4FF_ID_G27 0xc29b

#define LG4FF_RUMBLE_HALF_PERIOD_MS 20

struct LG4FF_Wheel
{
    SDL_hid_device *dev;
    uint16_t product_id;
    int range;
    LG4FF_State state;
    int rumble_level;  /* 0..127 */
    int rumble_phase;
    int rumble_playing;
    uint32_t rumble_toggle_ms;
};

int lg4ff_init(void)
{
    return SDL_hid_init();
}

void lg4ff_quit(void)
{
    SDL_hid_exit();
}

static int lg4ff_write(LG4FF_Wheel *wheel, uint8_t c0, uint8_t c1, uint8_t c2, uint8_t c3, uint8_t c4)
{
    const unsigned char cmd[7] = { c0, c1, c2, c3, c4, 0, 0 };
    return SDL_hid_write(wheel->dev, cmd, sizeof(cmd)) < 0 ? -1 : 0;
}

/* A G29 in a compatibility mode, from its release number (hid-lg4ff's lg4ff_identify) */
static int lg4ff_is_g29_in_compat_mode(uint16_t product_id, uint16_t release)
{
    switch (product_id) {
    case LG4FF_ID_WHEEL:
    case LG4FF_ID_DFP:
    case LG4FF_ID_G25:
    case LG4FF_ID_DFGT:
    case LG4FF_ID_G27:
        return (release & 0xfff8) == 0x1350 || (release & 0xff00) == 0x8900;
    default:
        return 0;
    }
}

/* Sends the native mode switch; the wheel then disconnects and re-enumerates */
static int lg4ff_switch_mode(const SDL_hid_device_info *info)
{
    SDL_hid_device *dev = LG4FF_OPEN_PATH(info->path);
    if (!dev) {
        return -1;
    }
    if (info->product_id == LG4FF_ID_G923_PS_BOOT) {
        /* Only accepted as output report 0x30 */
        const unsigned char cmd[8] = { 0x30, 0xf8, 0x09, 0x07, 0x01, 0x01, 0x00, 0x00 };
        SDL_hid_write(dev, cmd, sizeof(cmd));
    } else {
        const unsigned char cmd[7] = { 0xf8, 0x09, 0x05, 0x01, 0x01, 0x00, 0x00 };
        SDL_hid_write(dev, cmd, sizeof(cmd));
    }
    SDL_hid_close(dev);
    return 0;
}

/* A wheel that could not be opened (on Android: USB permission denied) is skipped until
   it disappears from the device list, so polling lg4ff_open() does not re-prompt the user */
static char lg4ff_failed_path[256];

LG4FF_Wheel *lg4ff_open(void)
{
    SDL_hid_device_info *list = SDL_hid_enumerate(LG4FF_VENDOR_LOGITECH, 0);
    SDL_hid_device_info *info;
    LG4FF_Wheel *wheel = NULL;
    int failed_present = 0;

    for (info = list; info; info = info->next) {
        if (info->path && lg4ff_failed_path[0] && SDL_strcmp(info->path, lg4ff_failed_path) == 0) {
            failed_present = 1;
        }
    }
    if (!failed_present) {
        lg4ff_failed_path[0] = '\0';
    }

    for (info = list; info && !wheel; info = info->next) {
        if (!info->path || (lg4ff_failed_path[0] && SDL_strcmp(info->path, lg4ff_failed_path) == 0)) {
            continue;
        }
        /* Interface 0 is the wheel and the only one with an output endpoint; the G923 also
           exposes other HID interfaces. -1 means the backend does not know. */
        if (info->interface_number > 0) {
            continue;
        }
        if (info->product_id == LG4FF_ID_G923_PS_BOOT ||
            lg4ff_is_g29_in_compat_mode(info->product_id, info->release_number)) {
            if (lg4ff_switch_mode(info) < 0) {
                SDL_strlcpy(lg4ff_failed_path, info->path, sizeof(lg4ff_failed_path));
            }
            continue;
        }
        if (info->product_id != LG4FF_ID_G29 && info->product_id != LG4FF_ID_G923_PS) {
            continue;
        }
        wheel = (LG4FF_Wheel *)SDL_calloc(1, sizeof(*wheel));
        if (!wheel) {
            break;
        }
        wheel->dev = LG4FF_OPEN_PATH(info->path);
        if (!wheel->dev) {
            SDL_strlcpy(lg4ff_failed_path, info->path, sizeof(lg4ff_failed_path));
            SDL_free(wheel);
            wheel = NULL;
            continue;
        }
        wheel->product_id = info->product_id;
    }
    SDL_hid_free_enumeration(list);
    if (!wheel) {
        return NULL;
    }

    /* Not fatal: Android's backend is always non-blocking and returns -1 here */
    SDL_hid_set_nonblocking(wheel->dev, 1);
    wheel->state.dpad = -1;
    wheel->range = 900;
    /* Start from a known state: no forces, no spring, full range */
    if (lg4ff_stop_forces(wheel) < 0 || lg4ff_set_autocenter(wheel, 0.0f) < 0 ||
        lg4ff_set_range(wheel, 900) < 0) {
        lg4ff_close(wheel);
        return NULL;
    }
    return wheel;
}

void lg4ff_close(LG4FF_Wheel *wheel)
{
    if (!wheel) {
        return;
    }
    if (wheel->dev) {
        lg4ff_write(wheel, 0x13, 0, 0, 0, 0); /* stop forces; fails harmlessly if unplugged */
        SDL_hid_close(wheel->dev);
    }
    SDL_free(wheel);
}

const char *lg4ff_name(const LG4FF_Wheel *wheel)
{
    return wheel->product_id == LG4FF_ID_G29 ? "Logitech G29" : "Logitech G923";
}

static void lg4ff_parse(LG4FF_Wheel *wheel, const uint8_t *d)
{
    /* G29 / G923 native report: hat in the low nibble of byte 0, 25 buttons from bit 4,
       steering 16 bit at 4..5, then throttle, brake, clutch (255 released) */
    LG4FF_State *s = &wheel->state;
    uint32_t buttons = ((uint32_t)d[0] >> 4) | ((uint32_t)d[1] << 4) | ((uint32_t)d[2] << 12) | ((uint32_t)d[3] << 20);
    int raw = (int)(d[4] | (d[5] << 8)) - 32768;
    int hat = d[0] & 0x0f;

    s->buttons = buttons & ((1u << 25) - 1);
    s->dpad = hat < 8 ? hat : -1;
    s->steering = raw < 0 ? raw / 32768.0f : raw / 32767.0f;
    s->throttle = (255 - d[6]) / 255.0f;
    s->brake = (255 - d[7]) / 255.0f;
    s->clutch = (255 - d[8]) / 255.0f;
}

static void lg4ff_update_rumble(LG4FF_Wheel *wheel)
{
    uint32_t now = LG4FF_TICKS();

    if (wheel->rumble_level > 0) {
        if (!wheel->rumble_playing || (uint32_t)(now - wheel->rumble_toggle_ms) >= LG4FF_RUMBLE_HALF_PERIOD_MS) {
            int level = wheel->rumble_level;
            wheel->rumble_phase = !wheel->rumble_phase;
            wheel->rumble_toggle_ms = now;
            wheel->rumble_playing = 1;
            lg4ff_write(wheel, 0x11, 0x08, (uint8_t)(0x80 + (wheel->rumble_phase ? level : -level)), 0x80, 0);
        }
    } else if (wheel->rumble_playing) {
        wheel->rumble_playing = 0;
        lg4ff_write(wheel, 0x13, 0, 0, 0, 0);
    }
}

int lg4ff_update(LG4FF_Wheel *wheel, LG4FF_State *state)
{
    uint8_t data[64];
    int size, got = 0;

    while ((size = SDL_hid_read_timeout(wheel->dev, data, sizeof(data), 0)) > 0) {
        if (size >= 9) {
            lg4ff_parse(wheel, data);
            got = 1;
        }
    }
    if (size < 0) {
        return -1;
    }
    lg4ff_update_rumble(wheel);
    if (state) {
        *state = wheel->state;
    }
    return got;
}

int lg4ff_set_range(LG4FF_Wheel *wheel, int degrees)
{
    if (degrees < 40) {
        degrees = 40;
    }
    if (degrees > 900) {
        degrees = 900;
    }
    if (lg4ff_write(wheel, 0xf8, 0x81, (uint8_t)(degrees & 0xff), (uint8_t)(degrees >> 8), 0) < 0) {
        return -1;
    }
    wheel->range = degrees;
    return 0;
}

int lg4ff_get_range(const LG4FF_Wheel *wheel)
{
    return wheel->range;
}

/* hid-lg4ff's lg4ff_set_autocenter_default */
int lg4ff_set_autocenter(LG4FF_Wheel *wheel, float strength)
{
    uint32_t magnitude, expand_a, expand_b;

    if (lg4ff_write(wheel, 0xf5, 0, 0, 0, 0) < 0) { /* disable */
        return -1;
    }
    if (!(strength > 0.0f)) {
        return 0;
    }
    magnitude = strength >= 1.0f ? 65535 : (uint32_t)(strength * 65535.0f);
    if (magnitude <= 0xaaaa) {
        expand_a = 0x0c * magnitude;
        expand_b = 0x80 * magnitude;
    } else {
        expand_a = (0x0c * 0xaaaa) + 0x06 * (magnitude - 0xaaaa);
        expand_b = (0x80 * 0xaaaa) + 0xff * (magnitude - 0xaaaa);
    }
    expand_a >>= 1;
    if (lg4ff_write(wheel, 0xfe, 0x0d, (uint8_t)(expand_a / 0xaaaa), (uint8_t)(expand_a / 0xaaaa), (uint8_t)(expand_b / 0xaaaa)) < 0) {
        return -1;
    }
    return lg4ff_write(wheel, 0x14, 0, 0, 0, 0); /* enable */
}

int lg4ff_set_constant_force(LG4FF_Wheel *wheel, float force)
{
    int level;

    wheel->rumble_level = 0;
    wheel->rumble_playing = 0;
    if (force < -1.0f) {
        force = -1.0f;
    }
    if (force > 1.0f) {
        force = 1.0f;
    }
    level = (int)(force * 127.0f + (force < 0 ? -0.5f : 0.5f));
    if (level == 0) {
        return lg4ff_write(wheel, 0x13, 0, 0, 0, 0);
    }
    /* Slot 1, constant force; 0x80 is no force, higher pulls to the right */
    return lg4ff_write(wheel, 0x11, 0x08, (uint8_t)(0x80 + level), 0x80, 0);
}

int lg4ff_stop_forces(LG4FF_Wheel *wheel)
{
    wheel->rumble_level = 0;
    wheel->rumble_playing = 0;
    return lg4ff_write(wheel, 0x13, 0, 0, 0, 0);
}

int lg4ff_set_rumble(LG4FF_Wheel *wheel, float strength)
{
    int level = strength > 0.0f ? (strength >= 1.0f ? 127 : (int)(strength * 127.0f + 0.5f)) : 0;
    wheel->rumble_level = level;
    if (level == 0 && wheel->rumble_playing) {
        /* Stop now rather than on the next lg4ff_update(), which may not come soon */
        wheel->rumble_playing = 0;
        return lg4ff_write(wheel, 0x13, 0, 0, 0, 0);
    }
    return 0;
}
