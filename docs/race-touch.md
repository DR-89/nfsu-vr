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


## Layout editor (Android)

Controller → **TOUCH CONTROL LAYOUT** opens an Android editor over the game.
Drag any racing button, including MENU, to move it. Drag the toolbar by its
heading if it covers the area you want to use. Save applies and persists the
layout; Cancel, Android Back, or leaving the activity restores the previous
layout. Reset restores the original arrangement and requires Save to persist.
The editor works even when the race overlay is currently hidden.

`touch-controls.xml` is stored in `GameActivity.gameDirectory()`, beside
`nfsu1.log` (normally the app's external files directory). This is the writable
runtime directory, not the selected SAF directory containing game assets.
Version 1 records stable button IDs and normalized center coordinates in [0,1];
the optional `size` attribute sets each button scale from 0.5 to 2.0 (default 1).
Select a button in the editor, then use the size slider (50–200%).
Artwork and the touch hit area resize together; Cancel and Reset cover sizes too. Positions are clamped to the screen
when rendered. Missing or invalid files fall back to defaults. AtomicFile keeps
the previous file recoverable if a write is interrupted.

`TouchLayout` owns positions and the XML format; `TouchLayoutStore` owns Android
file persistence. The same `RaceControlsView` draws and places buttons in both
the editor and races, so editing does not duplicate button artwork or hit tests.
The native Controller row publishes a one-shot request; the Android UI thread
owns the editor. While editing, touch output and guest gamepad state are cleared
without disconnecting the controller.

Host XML check:

```sh
javac -d /tmp/nfsu-layout-test android/app/src/main/java/org/nfsu/android/TouchLayout.java tests/TouchLayoutTest.java
java -cp /tmp/nfsu-layout-test org.nfsu.android.TouchLayoutTest
```

Device checks still required: open the Controller entry by touch and gamepad,
move/save/restart, cancel/reset, background/resume, verify multi-touch in a race,
and check tablet and wide-phone placement.

### Named presets

The editor's **Пресеты** button opens a library with Save under name, Load,
Import XML, Export XML and Delete. Presets include positions and individual
button sizes. Names can contain Unicode and spaces (up to 48 characters);
path separators, control characters and reserved filename characters are rejected.
Existing names require confirmation before replacement.

The library lives in `touch-presets/<name>.xml` beside `nfsu1.log` and
`touch-controls.xml`. It reuses the same versioned XML codec and AtomicFile
persistence as the active layout. Files can also be copied into that directory.
The list includes recoverable `.xml.bak` files after interrupted writes.
Invalid presets produce an error and leave the preview untouched.

Load changes the editor preview. **Сохранить** on the main toolbar commits it
as the active layout; **Отмена** restores the layout from before opening the
editor. Saving a named preset writes a library snapshot independently of the
active layout. Import opens Android's document picker, validates the XML, previews
it and asks for a library name. Cancelling the name dialog leaves the imported
preview available without adding a library entry. Export writes a snapshot of
the current preview through Android's Create Document picker. No broad storage
permission is required. The editor remains open while either picker is active.

`TouchPresetStore` owns names and library files; `TouchPresetController` owns
preset dialogs and Android document exchange. The activity forwards picker
results and preserves the editing session across the picker pause/resume.

Host checks cover XML/size round trips, invalid XML, legacy files, unsafe names,
library sorting, duplicate backup entries and discovery of recoverable backups.
Actual Android AtomicFile writes, document-provider import/export, picker
cancellation and background/recreation behavior still require device validation.
