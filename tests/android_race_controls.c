#include "input_mode.h"
/* Real SDL virtual devices + Android backend + actual USB report wrappers.
 * Exercises hotplug without a physical device or an Android runtime. */
#include <SDL.h>
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "android_input.h"
#include "race_touch.h"
#include "menu_touch.h"
#include "input/xinput_xbox.h"
#include "usb/usb_gamepad.h"

static uint32_t flow = 6;
int feng_touch_race_menu_active(void) { return 0; }
void *xbox_GetMemoryBase(void) { return &flow; }
ptrdiff_t xbox_GetMemoryOffset(void) { return (ptrdiff_t)(uintptr_t)&flow - 0x00283434u; }
DWORD GetTickCount(void) { return SDL_GetTicks(); }
int xbox_FramebufferKeyDown(int key) { (void)key; return 0; }

static SDL_Joystick *attach_pad(void)
{
    SDL_VirtualJoystickDesc desc;
    SDL_zero(desc);
    desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    desc.naxes = SDL_CONTROLLER_AXIS_MAX;
    desc.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
    desc.axis_mask = (1u << SDL_CONTROLLER_AXIS_MAX) - 1;
    desc.button_mask = (1u << SDL_CONTROLLER_BUTTON_MAX) - 1;
    desc.name = "Hotplug test physical source";
    int index = SDL_JoystickAttachVirtualEx(&desc);
    assert(index >= 0);
    SDL_Joystick *pad = SDL_JoystickOpen(index);
    assert(pad);
    SDL_JoystickSetVirtualAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT, -32768);
    SDL_JoystickSetVirtualAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, -32768);
    SDL_JoystickUpdate();
    return pad;
}

static void detach_pad(SDL_Joystick *pad)
{
    SDL_JoystickID id = SDL_JoystickInstanceID(pad);
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_JoystickGetDeviceInstanceID(i) == id) {
            assert(SDL_JoystickDetachVirtual(i) == 0);
            break;
        }
    }
    SDL_JoystickClose(pad);
    android_race_controls_poll();
}

static void report(uint8_t state[20])
{
    assert(usb_gamepad_report(0, state, 20) == 20);
}

int main(int argc, char **argv)
{
    assert(!input_mode_get().overlay_enabled);
    input_mode_configure(-1, 1); /* enable overlay for existing racing-input checks */
    (void)argv;
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    assert(SDL_Init(SDL_INIT_GAMECONTROLLER) == 0);
    SDL_Joystick *pad = argc > 1 ? attach_pad() : NULL;
    xbox_InputInit();
    android_race_controls_poll();
    XBOX_INPUT_STATE input;
    XBOX_INPUT_CAPABILITIES caps;
    assert(xbox_InputIsConnected(0));
    assert(xbox_InputGetState(0, &input) == ERROR_SUCCESS);
    assert(xbox_InputGetCapabilities(0, 0, &caps) == ERROR_SUCCESS);
    assert(caps.Type == 1 && caps.SubType == 1);
    XBOX_VIBRATION vibration = {100, 200};
    assert(xbox_InputSetState(0, &vibration) == ERROR_SUCCESS);
    assert(!xbox_InputIsConnected(1));
    assert(xbox_InputGetCapabilities(1, 0, &caps) == ERROR_DEVICE_NOT_CONNECTED);
    if (pad) {
        assert(android_input_has_controller());
        assert(!input_mode_sensor_enabled());
        assert(!race_touch_visible());
        detach_pad(pad);
    }
    assert(!android_input_has_controller());
    assert(input_mode_sensor_enabled());
    assert(race_touch_visible());
    uint8_t state[20];
    race_touch_set(RACE_GAS | RACE_LEFT | RACE_NITRO);
    report(state);
    assert(state[4 + XBOX_BUTTON_RTRIGGER] == 255);
    assert(state[4 + XBOX_BUTTON_B] == 255);
    assert(state[12] == 1 && state[13] == 128);
    assert(state[2] & XBOX_GAMEPAD_DPAD_LEFT);
    /* Touch exists in SDL/XInput, not just patched USB bytes. */
    assert(xbox_InputGetState(0, &input) == ERROR_SUCCESS);
    assert(input.Gamepad.bAnalogButtons[XBOX_BUTTON_B] == 255);
    assert(input.Gamepad.sThumbLX == -32767);
    race_touch_set(0);
    report(state);
    race_touch_set(RACE_SHIFT_UP);
    race_touch_set(0);
    /* Diagnostic state reads must not consume the short tap. */
    xbox_InputGetState(0, &input);
    UsbSetup setup = {0xA1, 1, 0, 0, 20};
    assert(usb_gamepad_control(0, &setup, state, 20) == 20);
    assert(state[4 + XBOX_BUTTON_X] == 255);
    report(state);
    assert(state[4 + XBOX_BUTTON_X] == 0);
    race_touch_set(RACE_CAMERA);
    race_touch_set(0); /* A quick camera tap survives until the next report. */
    report(state);
    assert(state[4 + XBOX_BUTTON_WHITE] == 255);
    assert(!(state[2] & XBOX_GAMEPAD_START));
    report(state);
    assert(state[4 + XBOX_BUTTON_WHITE] == 0);
    for (int cycle = 0; cycle < 3; cycle++) {
        race_touch_set(RACE_GAS);
        report(state);
        pad = attach_pad();
        android_race_controls_poll();
        assert(android_input_has_controller());
        assert(!input_mode_sensor_enabled());
        assert(!race_touch_visible());
        assert(xbox_InputIsConnected(0));
        assert(!xbox_InputIsConnected(1)); /* fallback is never player two */
        assert(xbox_InputSetState(0, &vibration) == ERROR_SUCCESS);
        SDL_JoystickSetVirtualButton(pad, SDL_CONTROLLER_BUTTON_A, SDL_PRESSED);
        SDL_JoystickSetVirtualAxis(pad, SDL_CONTROLLER_AXIS_LEFTX, 12345);
        race_touch_set(RACE_NITRO); /* hidden touch cannot affect the physical pad */
        report(state);
        assert(state[4 + XBOX_BUTTON_A] == 255);
        assert(state[4 + XBOX_BUTTON_B] == 0);
        assert(state[4 + XBOX_BUTTON_RTRIGGER] == 0);
        assert(state[12] == (12345 & 255) && state[13] == (12345 >> 8));
        /* The editor suppresses physical input without unplugging player one. */
        race_touch_set_editing(1);
        report(state);
        assert(xbox_InputIsConnected(0));
        assert(state[4 + XBOX_BUTTON_A] == 0);
        assert(state[12] == 0 && state[13] == 0);
        race_touch_set_editing(0);
        report(state);
        assert(state[4 + XBOX_BUTTON_A] == 255);
        /* Manually enabling sensor+overlay keeps the physical pad live. */
        input_mode_configure(INPUT_SENSOR_ON, 1);
        android_race_controls_poll();
        race_touch_set(RACE_NITRO);
        report(state);
        assert(state[4 + XBOX_BUTTON_A] == 255); /* physical */
        assert(state[4 + XBOX_BUTTON_B] == 255); /* touch */
        assert(state[12] == (12345 & 255) && state[13] == (12345 >> 8));
        input_mode_configure(INPUT_SENSOR_ON, 0);
        android_race_controls_poll();
        report(state);
        assert(state[4 + XBOX_BUTTON_A] == 255 && state[4 + XBOX_BUTTON_B] == 0);
        input_mode_configure(INPUT_SENSOR_AUTO, 1);
        detach_pad(pad);
        assert(!android_input_has_controller());
        assert(input_mode_sensor_enabled());
        assert(race_touch_visible());
        assert(xbox_InputIsConnected(0));
        report(state);
        assert(state[4 + XBOX_BUTTON_A] == 0);
        assert(state[4 + XBOX_BUTTON_RTRIGGER] == 0); /* no stale gas */
        race_touch_set(RACE_BRAKE | RACE_HANDBRAKE | RACE_SHIFT_DOWN | RACE_MENU);
        report(state);
        assert(state[4 + XBOX_BUTTON_LTRIGGER] == 255);
        assert(state[4 + XBOX_BUTTON_A] == 255);
        assert(state[4 + XBOX_BUTTON_Y] == 255);
        assert(state[2] & XBOX_GAMEPAD_START);
    }
    flow = 3;
    android_race_controls_poll();
    assert(!race_touch_visible());
    report(state);
    assert(state[2] == 0 && state[4 + XBOX_BUTTON_A] == 0);
    assert(xbox_InputGetState(4, &input) == ERROR_DEVICE_NOT_CONNECTED);
    assert(xbox_InputGetState(0, NULL) == ERROR_DEVICE_NOT_CONNECTED);
    SDL_Quit();
    puts("SDL touch controller / player-one hotplug / USB endpoints: OK");
    return 0;
}
