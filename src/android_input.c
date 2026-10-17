#include "input_mode.h"
/* Android's single player-one endpoint switches between physical SDL input
 * and a persistent SDL virtual touch controller. SDL's joystick lock owns all
 * handles, discovery, virtual state and packets, including concurrent polls. */
#include <SDL.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "android_input.h"
#include "race_touch.h"
#include "menu_touch.h"
#include "../xboxrecomp/src/input/xinput_xbox.h"

static SDL_GameController *touch;
static SDL_JoystickID touch_id = -1;
static SDL_GameController *pads[XBOX_MAX_CONTROLLERS];
static DWORD packets[XBOX_MAX_CONTROLLERS];
static atomic_int ready, physical;

static void touch_state_locked(uint32_t mask)
{
    if (!touch) return;
    SDL_Joystick *joystick = SDL_GameControllerGetJoystick(touch);
    for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++) {
        int down = (i == SDL_CONTROLLER_BUTTON_START && (mask & RACE_MENU)) ||
            (i == SDL_CONTROLLER_BUTTON_A && (mask & RACE_HANDBRAKE)) ||
            (i == SDL_CONTROLLER_BUTTON_B && (mask & RACE_NITRO)) ||
            (i == SDL_CONTROLLER_BUTTON_X && (mask & RACE_SHIFT_UP)) ||
            (i == SDL_CONTROLLER_BUTTON_Y && (mask & RACE_SHIFT_DOWN)) ||
            (i == SDL_CONTROLLER_BUTTON_DPAD_LEFT && (mask & RACE_LEFT)) ||
            (i == SDL_CONTROLLER_BUTTON_DPAD_RIGHT && (mask & RACE_RIGHT));
        SDL_JoystickSetVirtualButton(joystick, i, down ? SDL_PRESSED : SDL_RELEASED);
    }
    for (int i = 0; i < SDL_CONTROLLER_AXIS_MAX; i++) {
        Sint16 value = 0;
        if (i == SDL_CONTROLLER_AXIS_LEFTX)
            value = ((mask & RACE_RIGHT) ? 32767 : 0) - ((mask & RACE_LEFT) ? 32767 : 0);
        if (i == SDL_CONTROLLER_AXIS_TRIGGERLEFT) value = (mask & RACE_BRAKE) ? 32767 : -32768;
        if (i == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) value = (mask & RACE_GAS) ? 32767 : -32768;
        SDL_JoystickSetVirtualAxis(joystick, i, value);
    }
    SDL_GameControllerUpdate();
}

/* Preserve handles for attached pads. Never count our fallback as a physical
 * player or create a second guest device for the same physical controller. */
static void discover_locked(void)
{
    for (int slot = 0; slot < XBOX_MAX_CONTROLLERS; slot++) {
        if (pads[slot] && !SDL_GameControllerGetAttached(pads[slot])) {
            SDL_GameControllerClose(pads[slot]);
            pads[slot] = NULL;
        }
    }
    /* If player one's pad disappeared, promote the next available pad. */
    if (!pads[0]) {
        for (int slot = 1; slot < XBOX_MAX_CONTROLLERS; slot++) {
            if (pads[slot]) { pads[0] = pads[slot]; pads[slot] = NULL; break; }
        }
    }
    for (int index = 0; index < SDL_NumJoysticks(); index++) {
        SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(index);
        if (id == touch_id || id < 0 || !SDL_IsGameController(index)) continue;
        int found = 0, empty = -1;
        for (int slot = 0; slot < XBOX_MAX_CONTROLLERS; slot++) {
            if (!pads[slot]) { if (empty < 0) empty = slot; continue; }
            if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[slot])) == id) found = 1;
        }
        if (!found && empty >= 0) {
            pads[empty] = SDL_GameControllerOpen(index);
            if (pads[empty]) fprintf(stderr, "[INPUT] physical controller %s -> player %d\n",
                    SDL_GameControllerName(pads[empty]), empty + 1);
        }
    }
    int connected = pads[0] != NULL;
    input_mode_set_controller_present(connected);
    if (atomic_exchange(&physical, connected) != connected) {
        touch_state_locked(0);
        race_touch_enable(0);
        fprintf(stderr, "[INPUT] player 1 source: %s\n", connected ? "physical" : "touch");
    }
}

void xbox_InputInit(void)
{
    if (atomic_load(&ready)) return;
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) < 0) {
        fprintf(stderr, "[INPUT] SDL init failed: %s\n", SDL_GetError());
        return;
    }
    SDL_LockJoysticks();
    if (!atomic_load(&ready)) {
        SDL_VirtualJoystickDesc desc;
        SDL_zero(desc);
        desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
        desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
        desc.naxes = SDL_CONTROLLER_AXIS_MAX;
        desc.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
        desc.axis_mask = (1u << SDL_CONTROLLER_AXIS_MAX) - 1;
        desc.button_mask = (1u << SDL_CONTROLLER_BUTTON_MAX) - 1;
        desc.name = "NFS Underground Touch";
        int index = SDL_JoystickAttachVirtualEx(&desc);
        if (index >= 0) {
            touch_id = SDL_JoystickGetDeviceInstanceID(index);
            touch = SDL_GameControllerOpen(index);
        }
        if (!touch) fprintf(stderr, "[INPUT] virtual controller failed: %s\n", SDL_GetError());
        touch_state_locked(0);
        discover_locked();
        atomic_store(&ready, 1);
    }
    SDL_UnlockJoysticks();
}

void android_input_poll(void)
{
    if (!atomic_load(&ready)) return;
    SDL_LockJoysticks();
    SDL_GameControllerUpdate();
    discover_locked();
    SDL_UnlockJoysticks();
}

int android_input_has_controller(void) { return atomic_load(&physical); }

void android_input_set_touch(uint32_t mask)
{
    if (!atomic_load(&ready)) return;
    SDL_LockJoysticks();
    touch_state_locked(input_mode_sensor_enabled() ? mask : 0);
    SDL_UnlockJoysticks();
}

static SDL_GameController *controller_locked(DWORD port)
{
    return port == 0 && !pads[0] ? touch : pads[port];
}

static void read_gamepad(SDL_GameController *pad, XBOX_GAMEPAD *gamepad)
{
    static const SDL_GameControllerButton digital[] = {
        SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
        SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
        SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_BACK,
        SDL_CONTROLLER_BUTTON_LEFTSTICK, SDL_CONTROLLER_BUTTON_RIGHTSTICK
    };
    for (int i = 0; i < 8; i++)
        if (SDL_GameControllerGetButton(pad, digital[i])) gamepad->wButtons |= 1u << i;
    static const SDL_GameControllerButton analog[] = {
        SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X,
        SDL_CONTROLLER_BUTTON_Y, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER
    };
    for (int i = 0; i < 6; i++)
        gamepad->bAnalogButtons[i] = SDL_GameControllerGetButton(pad, analog[i]) ? 255 : 0;
    gamepad->bAnalogButtons[XBOX_BUTTON_LTRIGGER] =
        SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) >> 7;
    gamepad->bAnalogButtons[XBOX_BUTTON_RTRIGGER] =
        SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) >> 7;
    gamepad->sThumbLX = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
    gamepad->sThumbLY = -1 - SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
    gamepad->sThumbRX = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTX);
    gamepad->sThumbRY = -1 - SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTY);
}

DWORD xbox_InputGetState(DWORD port, XBOX_INPUT_STATE *state)
{
    if (port >= XBOX_MAX_CONTROLLERS || !state) return ERROR_DEVICE_NOT_CONNECTED;
    android_input_poll();
    SDL_LockJoysticks();
    SDL_GameController *pad = controller_locked(port);
    memset(state, 0, sizeof(*state));
    if (!pad || !SDL_GameControllerGetAttached(pad)) {
        SDL_UnlockJoysticks();
        return ERROR_DEVICE_NOT_CONNECTED;
    }
    state->dwPacketNumber = ++packets[port];
    if (race_touch_editing()) {
        SDL_UnlockJoysticks();
        return ERROR_SUCCESS;
    }
    read_gamepad(pad, &state->Gamepad);
    /* Overlay input supplements player one; it never replaces the pad or
     * changes connection/capability/rumble routing. */
    if (port == 0 && pad != touch && touch && input_mode_get().overlay_enabled) {
        XBOX_GAMEPAD sensor = {0};
        read_gamepad(touch, &sensor);
        state->Gamepad.wButtons |= sensor.wButtons;
        for (int i = 0; i < 8; i++)
            if (sensor.bAnalogButtons[i] > state->Gamepad.bAnalogButtons[i])
                state->Gamepad.bAnalogButtons[i] = sensor.bAnalogButtons[i];
        if (abs(sensor.sThumbLX) > abs(state->Gamepad.sThumbLX))
            state->Gamepad.sThumbLX = sensor.sThumbLX;
        if (abs(sensor.sThumbLY) > abs(state->Gamepad.sThumbLY))
            state->Gamepad.sThumbLY = sensor.sThumbLY;
    }
    SDL_UnlockJoysticks();
    return ERROR_SUCCESS;
}

BOOL xbox_InputIsConnected(DWORD port)
{
    if (port >= XBOX_MAX_CONTROLLERS) return FALSE;
    android_input_poll();
    SDL_LockJoysticks();
    SDL_GameController *pad = controller_locked(port);
    BOOL connected = pad && SDL_GameControllerGetAttached(pad);
    SDL_UnlockJoysticks();
    return connected;
}

DWORD xbox_InputGetCapabilities(DWORD port, DWORD flags, XBOX_INPUT_CAPABILITIES *caps)
{
    (void)flags;
    if (!caps || !xbox_InputIsConnected(port)) return ERROR_DEVICE_NOT_CONNECTED;
    memset(caps, 0, sizeof(*caps));
    caps->Type = 1;
    caps->SubType = 1;
    caps->Gamepad.wButtons = 0xFF;
    memset(caps->Gamepad.bAnalogButtons, 255, sizeof(caps->Gamepad.bAnalogButtons));
    caps->Gamepad.sThumbLX = caps->Gamepad.sThumbLY = 32767;
    caps->Gamepad.sThumbRX = caps->Gamepad.sThumbRY = 32767;
    caps->Vibration.wLeftMotorSpeed = caps->Vibration.wRightMotorSpeed = 65535;
    return ERROR_SUCCESS;
}

DWORD xbox_InputSetState(DWORD port, const XBOX_VIBRATION *vibration)
{
    if (port >= XBOX_MAX_CONTROLLERS || !vibration) return ERROR_DEVICE_NOT_CONNECTED;
    android_input_poll();
    SDL_LockJoysticks();
    SDL_GameController *pad = controller_locked(port);
    BOOL connected = pad && SDL_GameControllerGetAttached(pad);
    if (connected && pad != touch)
        SDL_GameControllerRumble(pad, vibration->wLeftMotorSpeed,
                                 vibration->wRightMotorSpeed, 1000);
    /* Missing haptics is not a controller disconnection. */
    SDL_UnlockJoysticks();
    return connected ? ERROR_SUCCESS : ERROR_DEVICE_NOT_CONNECTED;
}
