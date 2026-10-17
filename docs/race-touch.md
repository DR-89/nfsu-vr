# Android racing touch controls

`RaceControlsView` draws nine vector HUD panels over the SDL surface. MENU is
at the upper left; steering and shifting are at the lower left, pedals,
handbrake and nitro at the lower right. The layout scales with the surface,
uses separate pointer IDs, highlights held buttons and permits sliding between
controls. It does not use the generated concept sheet as a texture.

`android_input.c` supplies the Android implementation of the Xbox input API.
It creates a persistent SDL virtual game controller named `NFS Underground Touch`.
Guest player one always uses port zero: its source is the first attached physical
SDL controller, or the virtual touch controller when no physical pad is present.
The touch fallback never occupies a second guest slot. Additional physical pads
can still occupy additional guest ports.

Controller discovery runs during input polling and in the host event loop,
including when the game is paused. Detached handles close and newly attached
controllers open by instance ID. Switching sources clears pending/held touch
input and virtual axes. Buttons hide when a physical player-one controller is
present and return when it disconnects. The SDL joystick lock protects discovery,
handles, state updates and rumble; controller availability also has an atomic
snapshot for the UI policy. The first guest controller stays connected through
source changes and exposes capabilities through the normal input API.

The Android-only USB wrappers prepare the virtual SDL state before either the
interrupt report or GET_REPORT samples it. Diagnostic state probes therefore
cannot consume a short tap. CMake selects this project-local Android backend;
no xboxrecomp submodule edits are required.

| Control | Xbox input |
| --- | --- |
| MENU | Start |
| Gas | Right trigger, 255 |
| Brake/reverse | Left trigger, 255 |
| Handbrake | A, 255 |
| Shift up | X, 255 |
| Shift down | Y, 255 |
| Nitro | B, 255 |
| Left/right | Left stick X, +/-32767, plus D-pad for drag lane changes |

These bindings target the standard controller layout; in-game reassignment
also changes the meaning of these inputs. Shifting requires manual transmission
or drag mode; nitro requires an equipped/available nitrous system.

Visibility follows the retail game-flow value at `0x00283434`: state 6 enables
controls only without a physical controller; front-end/loading states disable them. State 6 also covers in-race
pause/results screens: this first version does not distinguish those substates
or add touchscreen navigation to their native menus. MENU sends Start.

`race_touch.c` protects the held mask with a mutex and latches short taps until
an actual USB report consumes them. Diagnostic controller probes do not consume
them. Focus loss, pause, surface changes, pointer cancellation and leaving flow 6
clear held and pending input. UI visibility refresh runs every 100 ms on the
Android UI thread, only while the activity is resumed.

Validation performed:

- `tests/race_touch.c`: concurrent control masks, held/released state, short tap,
  cancellation and flow transitions.
- `tests/android_race_controls.c`: actual SDL virtual devices, Android input
  backend and real USB reports. Checks cold start with/without an attached pad,
  repeated attach/detach, guest player-one assignment, no duplicate player two,
  visibility changes, capabilities, both USB endpoints, short taps and no stale
  accelerator after switching. Only guest flow and platform clock/key reads are
  mocked. This uses host SDL and does not replace an Android device test.
- NDK ARM64 C syntax checks with the project's existing compile flags.
- Java compilation against Android 37 and the existing compiled SDL classes.

`./gradlew assembleDebug lintDebug` completed with BUILD SUCCESSFUL on
2026-10-16. The new APK is `android/app/build/outputs/apk/debug/app-debug.apk`.
Installation of this revision on the connected device was declined, so its
visual appearance and guest-port correction have not been validated in a new
run.

## Guest USB slot correction

Read-only diagnostics of the installed preceding APK confirmed that the SDL
backend was present and returned success. OHCI reported pad 1 at HC0 port 1
(one-based), but guest availability at `0x260EA0` was `[0, 0, 1, 0]` and the
retail connection table at `0x2C8638` was `FF FF 41 FF`. Thus the device was
visible in Xbox slot 2 (player three), while player one remained disconnected.
The controller check `0x130840` tests this connection table for `FF`.

The HC1 attempt did not work: its subsequent device log contained no device
arrival and reported that the selected controller never became operational.
The retail XPP routine `0x1EF01B` explicitly computes `(root_port - 1) XOR 2` at
`0x1EF040`. Therefore root ports 1,2,3,4 map to Xbox slots 2,3,0,1.

Android boot now sets `RECOMP_USB_HC=0`, `RECOMP_USB_NDP=4`,
`RECOMP_USB_PORT=2`, `RECOMP_USB_PORT2=3`. Player one is on the third root-hub
port of the active HC0, and a second physical controller is on the fourth.
All four hub ports must be declared to avoid clamping the selected port back
to index one. Neither the guest connection flags nor the disconnect dialog
are patched.

`tests/usb_player_ports.c` executes the actual generated retail XPP routine
against synthetic root nodes and verifies all four mappings. It passed:

```sh
cc -O1 -ffunction-sections -fdata-sections -Iandroid/app/build/generated/recomp \
  tests/usb_player_ports.c android/app/build/generated/recomp/recomp_0051.c \
  -Wl,--gc-sections -lm -o /tmp/nfsu-usb-player-ports-test
/tmp/nfsu-usb-player-ports-test
```

This APK passed assembly and lint. Its installation was declined, so a new
device run confirming the corrected guest slot and race continuation remains
pending.

All nine HUD panels now omit their outer cyan stroke; icon strokes and the
translucent pressed/unpressed fills remain.

Host integration test (adjust the SDL library and JDK paths for the host):

```sh
cc -std=c11 -Wall -Wextra -Werror -pthread -Isrc -Ixboxrecomp/src \
  -Ithird_party/SDL/include \
  -I/usr/lib/jvm/jdk-25.0.4-oracle-x64/include \
  -I/usr/lib/jvm/jdk-25.0.4-oracle-x64/include/linux \
  tests/android_race_controls.c src/android_input.c src/android_race_controls.c \
  src/race_touch.c src/menu_touch.c src/input_mode.c xboxrecomp/src/usb/usb_gamepad.c /usr/lib64/libSDL2-2.0.so.0 \
  -Wl,--wrap=usb_gamepad_report -Wl,--wrap=usb_gamepad_control \
  -o /tmp/nfsu-race-hotplug-test
/tmp/nfsu-race-hotplug-test
/tmp/nfsu-race-hotplug-test startup-connected
```
