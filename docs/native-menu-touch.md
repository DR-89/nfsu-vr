# Native FEng touch input

Android touch input feeds the original Xbox FEng mouse path. There is no Android
overlay, per-screen rectangle list, text recognition, or simulated D-pad search.
The current scope is the front end (`GameFlow == 3`); racing controls, pause and
post-race screens are not enabled by this change.

## Executable evidence

Addresses refer to the original XBE used by this port:

- `0xD8B10`: FEng update. Byte `this+2` enables mouse polling and dispatch.
- `0x1FF4D8`: cFEng vtable; slot `+0x44` points to the shared `0xDB860`
  no-op (`ret 4`). Only its call returning to `0xD8B5E` receives touch data.
- Input packet: four signed 16-bit fields, X/Y/wheel/buttons. `0xF8D80`
  updates position, movement and previous/current buttons.
- `0xD8730`: native mouse dispatch. Uses each package's selectable object
  array (`+0x84`, count `+0x88`), skips disabled objects (`0x04000000`),
  and resolves hover, button edges and press capture.
- Vtable slot `+0x48` -> `0xDDB60` -> `0xDD880`: native point test.
  `0xDCDC0` obtains bounds, including grouped objects and text dimensions.
- `0xE53C0`: changes package focus (`+0x80`) and queues the original
  lost/gained-focus messages. Menu activation remains in original handlers.

The host queue preserves down/up edges between guest frames. Every new tap
first provides a hover update: native mouse dispatch processes button edges
before resolving hover, so sending down immediately could hit the old focus.
Moves coalesce. A single SDL finger owns the gesture; additional fingers are
ignored. Pause, focus loss, surface changes and queue overflow cancel input.
Host and guest exchange samples under a mutex; only the guest thread touches
guest memory and calls FEng.

Coordinates are normalized surface positions mapped through the presenter's
4:3/16:9 letterbox fit to FEng's centred 640x480 canvas, with the current
Menu Size scale inverted. All target bounds come from the game. Changes to
rendering/presentation must keep this conversion consistent.

## Rendered hit geometry

The Xbox descriptor-based hit test does not follow all render-time group
transforms. On the keyboard, a tap at the visible P centre selected L.
Android touch now uses bounds of the actual cached quads drawn by FEng:
`0xD9D00` supplies object identity and `0xF8B80` supplies its final vertices.
Bounds are rebuilt every menu render, including the cache-reuse path. Group
bounds are the union of their rendered descendants. Touch and cached vertices
use the same 640x480 canvas; Menu Size and presentation fit are inverted once.
No key positions, keyboard rows or screen-specific touch zones are stored.
Objects absent from the last render have no touch bounds. The legacy text
measurement hit path is not called for Android touches.

## Validation

Host queue/mapping regression test:

```sh
cc -std=c11 -Wall -Wextra -Werror -Isrc tests/menu_touch.c src/menu_touch.c \
  -pthread -lm -o /tmp/nfsu-menu-touch-test
/tmp/nfsu-menu-touch-test
```

Covers taps shorter than a guest frame, held/moving input, consecutive taps,
cancellation, orphan events, queue overflow, letterboxing and Menu Size.
Build the Android APK using the root `./gradlew assembleDebug`.

Native mouse support surviving in the executable does not establish that every
Xbox screen's assets contain usable mouse actions. Device validation must check
actual menu transitions, including dialogs and disabled items. `[touch]` log
lines report package flags, selectable counts and resulting focus hashes after
the hover update, without drawing diagnostic UI.

Gesture target correction: observe the result of the virtual hit test
(`0xDDB60`) during the hover update, using the rendered bounds described above.
Each active package keeps its own target for the gesture. The press validates
that the object is still selectable in that package and synchronizes focus
with `0xE53C0` before original activation. A miss is consumed instead of
invoking generic Select on the previous focus. This applies to keyboard keys,
menu rows and dialogs through the same object path, without button-name rules.

Regression test (synthetic cached quads, actual draw/hit/input wrappers):

```sh
cc -O1 -D__ANDROID__ -ffunction-sections -fdata-sections -Isrc \
  -Iandroid/app/build/generated/recomp tests/menu_touch_focus.c \
  src/recomp_manual.c src/menu_touch.c -Wl,--gc-sections -pthread -lm \
  -o /tmp/nfsu-menu-focus-test
/tmp/nfsu-menu-focus-test
```

It forces focus to the opposite button after hover, checks Yes and No target
selection at the press edge, verifies distinct rendered keyboard rows and stale-frame
removal, and verifies misses/disabled buttons do not
activate the previous selection. Guest stack and volatile registers are checked.

Final binary-dialog correction was built and installed on Retroid. A tap on
Yes while No was highlighted opened the profile-choice dialog; Load Profile
opened the list, and No on the PAVEL load confirmation cancelled back to the
main menu. A tap on the confirmation body did not activate either answer.
Tests used ADB touchscreen events. A separate visual defect remains: after a
body tap some unselected dialog text can disappear. Full visual correctness
of the retained Xbox mouse path is not established by these action checks.

### Retroid Pocket 4 Pro, 2026-10-15

- Root `assembleDebug`: `BUILD SUCCESSFUL`; installed over the existing app
  with `adb install -r`, preserving data.
- Direct taps opened Main Menu -> Options -> Camera. Tapping the Car Reflections
  label selected its row. After Home and returning to the game, tapping
  Resolution Scale selected that row; the previous highlight moved correctly.
- Camera's visible left arrow did **not** change Car Reflections. Xbox settings
  need additional native object/event binding for directional controls. No
  setting value was changed by that test.
- Queue/mapping test and the existing `tests/ui_aspect.c` regression passed.
- This is a proof of native menu selection, not complete touch-only navigation:
  return/help controls, title-screen entry, all dialogs, car selection,
  directional settings, pause/results and racing controls remain unverified or
  unimplemented. Device tests used ADB-injected touchscreen events; physical
  finger usability and fast multi-finger interaction were not validated.

### Rendered keyboard validation, 2026-10-15

On Retroid, the original build reproduced P -> L. With rendered bounds, taps
on visible P, L, Q and Z produced exactly `PLQZ`. A tap in the empty input
area left that string unchanged. The visible BACK key deleted one character
per tap. Test text was cleared without creating a profile. Tests used ADB
touchscreen events; physical-finger edge accuracy across all keys and screen
aspect ratios still requires broader device testing. APK build, cached-geometry
regressions, touch queue/mapping and existing UI aspect checks passed.

### Profile list activation, 2026-10-16

The profile list controller (`BC750` delegating to `BD7E0`) ignores the
object MouseClick message. It owns a separate data cursor from FEng focus.
Touch activation resolves the rendered row through its native list cells,
synchronizes the data cursor against the visible data window, and queues
Select for that package. Queue route -2 dispatches the action to the screen;
route -5 only requests feedback through the screen's input-feedback virtual
method, which explains a click sound without confirmation. A regression
covers this list even when an object MouseClick script is present.

The corrected APK was built and installed on Retroid. The user confirmed
physical touch activation now works ("заработало!"). Earlier apparent success
was caused by a physical Select press and is not counted as touch validation.
Queue/mapping, rendered-bounds/focus/profile routing and UI-aspect regressions
all passed. Multiple saved profiles and scrolling remain untested on device.

### Native screen arrows, 2026-10-16

The common screen base stores its `mouse_over_l` and `mouse_over_r` objects
at +0x24/+0x28 (hashes AA68BFCE/AA68BFD4). Statistics renders these objects
but exposes zero selectable rows. Touch now captures a visible arrow by its
native identity and cached rendered bounds, then queues Left (9120409E) or
Right (B5971BF1) through screen-action route -2. The original Statistics
controller D3720 performs the switch, redraw and endpoint handling.
No overlay or fixed screen rectangle is used. Other screens sharing these
native base arrows use the same binding; their behavior still needs testing.

Regression checks cover both directions with zero selectable rows, one action
per tap, misses and an arrow absent from the current rendered frame. The APK
built successfully and was installed on Retroid; the user confirmed that the
arrows work on the Statistics screen. Physical input from the user overlapped
ADB checks, so screenshots alone are not treated as isolated direction tests.

### Customize touch inspection, 2026-10-16

Customize browsers use a native horizontal-browser instance (vtable 1FF870)
at screen+48. Their visible `Select_arrow_left/right` objects are rendered
by the shared HorizontalBrowserBack package; the screen's own mouse_over
markers are hidden. Bind the visible arrow identity and rendered quad to
the active browser's directional action. The selected browser node +0C points directly to its FEng icon object. Its
separate category-name text fields are FAC7F994 / 4AB1E821. Tapping the
icon or text queues the controller's MouseClick action through route -2.

An empty-space gesture on a typed Customize browser directly changes the
SelectCarCameraMover pitch/yaw (+40/+44), proportional to finger displacement:
180 degrees per screen width (horizontal direction reversed at the user's request) and 90 degrees per screen height. It does not
emulate stick speed, use a stick dead zone or integrate elapsed frame time.
Native pitch limits (85..140 degrees) remain. Manual rotation clears native
angular velocity and refreshes the idle-orbit timestamp. Control gestures
cannot own the camera. Modal screens, leaving Customize and lifecycle cancel
clear gesture ownership. Nested Customize browsers share the gesture binding.

The user rejected the initial stick-emulation prototype; that implementation
was removed, including its input wrapper and linker change. The user confirmed
direct rotation, directional arrows and rotation in the expanded garage
context. The final icon/text-label correction built and installed;
physical confirmation for that final category click correction is pending.
Tests cover proportional reversible motion, stationary-finger stability,
leaving the garage, nested browser identity, shared arrows, icon activation
and separate label activation. Queue/mapping and UI-aspect checks also run.
