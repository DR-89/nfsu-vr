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
cc -std=c11 -Wall -Wextra -Werror -Isrc tests/menu_touch.c src/menu_touch.c src/input_mode.c \
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

ChooseCareerCarScreen (vtable 1FBFCC), shown after the first Underground win,
uses the garage carousel and preview gesture path. B7F60 constructs the browser
at +48 (selected node +54); B7DD0 accepts MouseClick/Select and B7D40 handles
Left/Right. The shared binding supplies rendered arrows, selection, strip swipes,
inert gap taps and exclusive camera orbit below the strip. The host fixture
checks this screen alongside both garage entry paths and Quick Race.
Physical-device acceptance remains pending.

```sh
cc -O1 -D__ANDROID__ -ffunction-sections -fdata-sections -Isrc \
  -Iandroid/app/build/generated/recomp tests/menu_touch_focus.c \
  src/recomp_manual.c src/feng_touch.c src/menu_touch.c src/input_mode.c \
  src/touch_gesture.c src/touch_orbit.c src/feng_touch_carousel.c \
  src/feng_touch_camera.c src/feng_touch_magazines.c -Wl,--gc-sections -pthread -lm \
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
the active browser's directional action. The selected browser node +0C
points directly to its FEng icon object. Its separate category-name text
fields are FAC7F994 / 4AB1E821. Visible category labels can carry the retail
noninteractive flag; their rendered geometry still supplies the touch target.

Garage controls capture the gesture and dispatch on release through the
native screen entry DE540, using the active browser controller. A short tap
on an icon or label invokes MouseClick; an arrow tap invokes its direction.
A horizontal swipe of at least 20 logical pixels selects next (swipe left)
or previous (swipe right), once per gesture. Movement cancels tap activation;
vertical/ambiguous drags do not open a category. This avoids dependence on
retail mouse-button edges or script availability in nested menus.

An empty-space gesture on a typed Customize browser directly changes the
SelectCarCameraMover pitch/yaw (+40/+44), proportional to finger displacement:
180 degrees per screen width (horizontal direction reversed at the user's
request) and 90 degrees per screen height. It does not
emulate stick speed, use a stick dead zone or integrate elapsed frame time.
Native pitch limits (85..140 degrees) remain. Manual rotation clears native
angular velocity and refreshes the idle-orbit timestamp. Control gestures
cannot own the camera. Modal screens, leaving Customize and lifecycle cancel
clear gesture ownership. Nested Customize browsers share the gesture binding.

The user rejected the initial stick-emulation prototype; that implementation
was removed, including its input wrapper and linker change. The user confirmed
direct rotation, directional arrows and rotation in the expanded garage
context. After correcting icon/text selection, native screen dispatch and
adding swipe/tap discrimination, the final APK built and installed. The user
confirmed the resulting controls work on Retroid.
Tests cover proportional reversible motion, stationary-finger stability,
leaving the garage, nested browser identity, shared arrows, icon activation
separate label activation, both swipe directions, vertical-drag cancellation,
release-only activation and no repeats while held. Queue/mapping and UI-aspect
checks passed as well. Not every purchasable part or garage screen was tested.

### ColorPicker palette and inspection, 2026-10-16

ColorPicker (vtable 1FDD74, native input C8A10) owns 60 swatch records,
starting at screen+58 with stride 2C. Each record supplies its FEng sprite
(+0C), color data (+10), row (+24) and column (+28). Touch captures a
populated swatch using that sprite's current rendered bounds. On a short
release it validates the same record, updates native cursor coordinates
B08/B0C and invokes C74C0, C7B40 and C7A80(100). Mode 100 is the game's
preview path with native lock checks; mode 101/Accept is never synthesized.
A swatch drag cancels selection and cannot rotate the preview or swipe a
category. Empty cells cannot select the previously focused color.

The active ColorPicker also owns empty-space camera gestures, using the
same direct pitch/yaw path as the garage. Palette and camera ownership are
exclusive. Regression tests cover populated and empty cells, preview-only
mode, release selection, swatch-drag cancellation and camera rotation in
ColorPicker. The root APK build and the touch/geometry/UI-aspect checks pass.
At the user's request the running game was not restarted and this APK was
not installed: physical palette selection and paint-camera validation remain
pending on the new build.

### Underground event map, footer and options touch, 2026-10-16

Race Map (`MU_UG_MapScreen.fng`, vtable 1FC12C) owns a native scroll list
at screen+3C and a separate selected event at +48. Each scroll node maps
its event data (+08) to its rendered FEng group (+0C). A tap captures that
pair, validates membership again on release, selects the event through
DF160 and refreshes route/details through B8310 before native Select.
This bypasses dependence on the package's selectable-object array without
introducing fixed map coordinates. Drag gestures do not activate an event.

Numbered `LeftArrow_N` / `RightArrow_N` objects dispatch Left/Right with
`OptionName_N` as the event object, so the original options controller
updates the touched row. Disabled rows and arrows absent from the current
render cannot change values. OptionsController (vtable 1FDBC8) receives its
layout field (EBFB7CC5) when either native base arrow or Select_arrow control
is touched; its handler ignores directional messages aimed at an arrow object.
These controls dispatch once on release and do not use garage swipe handling.

Back accepts native localized Back labels regardless of their object name.
Its hit area joins the caption, retained icon and gap, with eight logical
pixels of tolerance. A visible caption remains usable when no Help icon is
rendered. Existing Help-to-Back replacement still uses native localization.

Synthetic regression coverage includes event cursor synchronization, options
row targeting, disabled/hidden controls, controller-layout targeting and Back
caption-only hits. Device acceptance must separately check the career Drag
entry, the complete Back caption and both directions in both options screens.


### Shared FEng touch module and Quick Race car preview

`src/menu_touch.c` owns the synchronized host queue and surface mapping.
`src/feng_touch.c` owns rendered bounds, gesture capture, movement cancellation,
release dispatch and direct preview-camera rotation. Native screen bindings
reuse these policies and provide the event object/direction expected by the
original controller. `src/recomp_manual.c` retains short guest/render hooks
and dependency declarations for the recompiler's name-protection scan.

QuickRaceCarSelect (vtable 1FB8F4) uses the shared preview camera and arrow
geometry. Its arrows send Left/Right to its original B25F0 controller; tapping
the selected car's rendered scroll item sends Select. Camera drags start in
empty space, cannot activate a control and stop when the active screen changes.
Synthetic regressions cover both arrows, selected-car activation, proportional
rotation and stationary-finger stability through the shared implementation.

The shared-module APK passed the root `assembleDebug` build and was installed
on Retroid with `adb install -r`, preserving data. Queue/mapping, native
geometry/gesture regressions and UI-aspect checks passed after extraction.
The user confirmed the preceding career/Back/options correction works.
Quick Race car selection and rotation in the shared-module build still need
physical touch acceptance; installation and synthetic tests are not that proof.


Carousel capture now uses one `TouchCarousel` binding and one shared handler
for both Quick Race cars and Customize/tuning. A binding supplies the selected
node, native activation message, optional label identities and swipe policy.
Arrow lookup, rendered bounds, gesture capture, movement cancellation, release
activation and screen validation follow the same code path. Tuning retains
horizontal swipes; car-arrow drags retain cancellation. Tests exercise both
bindings, both directions, release-only behavior and their distinct drag policies.

### Vertical screen arrows

The common screen constructor DE440 stores mouse_over_u at +2C (AA68BFD7)
and mouse_over_d at +30 (AA68BFC6). Touch binds their rendered bounds to
native Up (72619778) and Down (911C0A4B), dispatched to the active screen
on release. The original controller owns scrolling and endpoint behavior.
Hidden/disabled arrows and dragged gestures cannot scroll. Synthetic checks
cover both messages, release-only activation, hidden/disabled arrows and drag
cancellation. APK assembly passed; Magazines physical-device acceptance is
still pending.

### Magazines cover selection

UndergroundMagazineScreen (1FEA8C) uses page offset +40 and visible-slot cursor
+3C; Select in D24E0 opens their sum, independently of FEng focus. Capture
rendered cover/lock/group identities (F1E6EA37/9DB42DF9/BBE88515 plus slot),
then synchronize the cursor and refresh D1AD0 before native Select on release.
A page change during the gesture cancels activation. Native lock handling stays
in D24E0. Synthetic tests reproduce Autopress slot 3 versus old Option slot 6
on page 8 and check page-change cancellation. Device acceptance is pending.

### Car Customization car browser

MainMenuCarCustomize (vtable 1FC1AC, package MU_MainMenuCarCustomize.fng)
is now recognized as a preview owner. Its B9E80 constructor creates the
horizontal browser at +48, selected node +54. B9D70 accepts MouseClick/Select;
Left/Right go through B9CC0. The existing carousel binding supplies rendered
arrow/item/label hit areas, release-only taps and horizontal swipes. Empty-space
drags use the shared direct camera orbit; captured controls cannot rotate it.
No additional hooks in recomp_manual.c or fixed screen rectangles are needed.

Synthetic geometry/gesture regressions pass for both arrows, selected-item
activation, both swipe directions, release-only dispatch, proportional camera
rotation and stationary-finger stability on this screen. Queue/mapping tests
also pass. APK assembly was blocked by the read-only Gradle cache; the requested
permission to run the build outside the sandbox was declined. A new APK and
physical-device validation remain pending.

Car Customization carousel gaps now capture swipe-only gestures across the
640-wide FEng canvas, from the canvas top to the selected logo's rendered bottom plus
16 logical pixels of finger tolerance. Full-height objects (over 160 pixels)
do not define a strip. Exact arrow/item taps retain their native actions;
gap taps do not activate Customize. Gesture ownership is fixed at touch-down,
so a carousel swipe cannot turn into a camera drag. Synthetic checks cover
small logos, gap swipes in both directions, gap taps and camera movement below
the strip. Physical touch acceptance and a rebuilt APK remain pending.

Camera drags in Car Customization now check the same rendered carousel-strip
boundary on every sample. Entering the strip freezes pitch/yaw; moving within
it cannot rotate or switch cars. Returning below it reanchors the finger before
resuming rotation, avoiding an accumulated delta jump. Synthetic regressions
cover crossing, panel movement, re-entry and resumed proportional motion.
APK assembly and physical-device verification remain pending.

The strip policy is now selected through TouchCarousel.allow_swipe rather than
the MainMenuCarCustomize vtable. Underground's typed garage browsers therefore
share full-width swipe capture and the per-sample camera boundary with the
main-menu browser. The same gap/swipe/camera-crossing regression runs for both
bindings. Quick Race retains its existing arrow-drag policy. Device comparison
of both entry paths and APK assembly remain pending.

## Quick Race: Select Location

The location carousel has a separate typed controller, QRTrackSelectScreen
(vtable `0x1FB848`, constructor `0xB1630`, message handler `0xB1880`). Its
horizontal browser is constructed by `0xDF700` at screen `+0x48`, with the
selected node at `+0x54`. `0xB0840` updates the location-name object hash
`0x03C8F634`; `0xB0CE0` handles Left/Right and refreshes the selected route.
The previous carousel binding required a car-preview screen and excluded it.

A separate carousel-screen resolver now recognizes this controller without
enabling garage camera orbit. Visible arrows send native Left/Right; the
selected thumbnail and location caption send native Select. Horizontal swipes
in the rendered header strip change location on release. Empty strip taps do
not select, and map drags cannot rotate a hidden preview camera. Back keeps the
shared native Cancel path. Gesture release checks that the same typed screen
is still active.

The synthetic regression fails on the prior feng_touch.c at the location-arrow
assertion and passes with the fix. It covers both arrows, thumbnail/caption
activation, both swipe directions, empty strip taps, Back without selectable
rows, screen changes during a held tap and no hidden-camera rotation. The full
existing native-menu regression and host queue/mapping tests also pass. Android
ARM64 syntax validation passes. A rebuilt APK and physical-device verification
remain pending; the earlier Gradle cache escalation was declined.

## Quick Race: Mode Options

QROptionsScreen (`0x1FB64C`, native handler `0xAF610`) uses `OptionNText`
with `OptionNData`, rather than the generic `OptionName_N` option rows.
Its Select handler starts the race; generic mouse activation on a parameter
therefore confirmed the screen instead of changing the value.

Touching a rendered row label/value now sends Right to that row's native text
object and synchronizes focus. Visible shared/base arrows send Left/Right to
the focused row. Release-only dispatch, movement cancellation and disabled-row
checks apply. Unbound modal body taps are consumed, preventing accidental starts.
The native localized Mode Options heading is relabeled Continue and supplies
an explicit Select target through its rendered text bounds; Back retains Cancel.
No fixed screen zones or Android overlay controls are introduced.

Synthetic rendered-geometry tests cover row/value taps, both arrows, disabled
rows, explicit confirmation and empty-body misses. Queue/mapping and native
menu regressions pass. Android APK assembly passes; physical touch usability,
the heading layout and actual race startup remain pending device acceptance.

The heading binding uses the retail language key MODE_OPTIONS_TITLE
(F5599FF0), whose English value is "Mode Options". MODE_OPTIONS (3825DB8F)
is a different entry and did not change the visible header in the first build.
The synthetic fixture now uses the actual heading key. The controller identity
1FB64C was confirmed from the running device registry while this modal was open.

Quick Race row taps now select focus only; they do not change a value.
QROptionsScreen calls DE420 rather than DE440, so its +24/+28 fields cannot
supply arrow pointers. Bind rendered mouse_over_l/r hashes AA68BFCE/AA68BFD4
directly, with eight logical pixels of touch tolerance. The row under the
rendered arrow receives Left/Right, independently of stale native focus. The regression uses
these retail arrow hashes with no base-arrow pointers and checks that label/
value taps do not dispatch a value change. Physical arrow acceptance is pending.

Live QR diagnostics supersede the earlier arrow-name assumption: both visible
blinking glyphs use hash 13E9F3A9 and the modal's +24/+28 pointers are zero.
Capture their distinct rendered objects, resolve direction relative to the
selected row's rendered value, and retain the geometry across blink frames.
Changing modal/focus clears this geometry. Row selection now joins label,
value and gap, adds horizontal/vertical finger tolerance bounded by neighbouring
rows, and flushes native queued focus scripts without sending Select/Right.
Tests cover identical glyph hashes, both directions, hidden blink frames,
focus-change invalidation, gap selection, focus-script flush and no value change
on row taps. Device acceptance of the revised interaction remains pending.

## Race pause menu

GameFlow remains 6 during pause. Enable menu touch when this state has an
active FEng screen; the racing HUD uses separate callbacks. Publish this mode
atomically to android_race_controls so its buttons release and stop intercepting
menu taps. With no active FEng screen, race controls return and menu touch cancels.
This also keeps nested options and restart/quit confirmation screens touchable.

PauseMenu vtable 1FD734 routes C0040 MouseClick (0C407210) using numeric object
identities 0..4. Select is ignored. Capture rendered text rows, use their shared
horizontal extent plus finger tolerance, and send MouseClick on release. Disabled
and missing rows cannot activate; existing screen-change/drag checks still apply.
Synthetic tests cover flow 6, Restart/Quit identities, short-label row extension,
misses, disabled rows and leaving pause. Device acceptance remains separate.

PreRaceStatsScreen (1F9404) is an active FEng countdown screen, not pause:
exclude it from race_menu_active so the driving overlay stays visible before
control handoff. Regression checks countdown versus pause visibility state.
In racing menus, localized ACCEPT (4F876B6F) becomes BACK (1335F0), and
FENG_BUTTON0 (E03C840A) becomes FENG_BUTTON1 (E03C840B). The shared Cancel
hit target includes the complete caption/icon/gap with 16 logical pixels of
finger tolerance. Tests cover caption/icon replacement, gap taps and touches
outside the glyph edge. Physical countdown and settings-return acceptance
remain pending on this revision.

### Visible Back over nested menus

Back cached quads now use opaque white RGBA (entry+90, four vertices; retail
192C0/192A0 consumes this array). Defer their native cache draw until the end
of DC090 so modal dimming cannot obscure the inherited footer. Keep its original
rendered geometry for input. Suppress the retained controller glyph, including
script-generated small text immediately left of Back, while retaining hit bounds.
This applies to front-end Back as well as pause.

If both the pause selector (1FBBC8) and its settings child (1FBC9C) are active,
Back belongs to the child. B65E0 ignores generic Cancel: F2D0D887 closes this
child through its original package/selector update path. Other screens retain
Cancel. The child settings row's label/value/gap uses a broad rendered union
and sends native Right to OptionName_N, including Controller/Vibration.
Tests cover simultaneous parent/child activation, the actual close event,
white cache colors and a gap tap on the controller setting. Device visual and
nested-return acceptance remain pending for this revision.

## Underground Race Map touch

UndergroundMapScreen (1FC12C, handler B95D0) uses the shared track carousel
capture and release path. B9460 allocates its browser at screen+3C; the selected
rendered node is browser+0C. Screen+48 is a separate career event cursor.
Rendered arrows dispatch native Left/Right, the thumbnail and INCOMING_TEXT /
ACCEPT_MESSAGE_TEXT dispatch Select, and Back uses the existing Cancel footer.
Gaps in the rendered upper carousel strip accept swipes only. Map drags cannot
orbit a retained garage camera. Synthetic coverage runs both this screen and
Quick Race track selection through arrows, Select, swipes, gap taps, Back and
screen-change cancellation. Device acceptance remains pending.

## Underground difficulty selection

UndergroundBriefScreen (1FC078, B8F60) handles EASY (14DD31), MEDIUM
(6BAA2200) and HARD (16821E) as release-only touch targets. Capture the
package focus before native hover: a different difficulty updates FEng focus
and flushes its scripts without activation. A tap on the already selected
difficulty dispatches native MouseClick with the rendered button object.
B8F60 passes it to B88C0, which calls B8880 to read the object's +10 hash
and map it to difficulty 0/1/2 before storing it at 29C460. Passing a raw
number here is incorrect. Retain the last completed touch choice while this
screen is active so transient native hover focus cannot lose confirmation. This decision
is fixed for the gesture, so hover cannot turn selection into confirmation.
Dragging or leaving the screen cancels the action. Controller input retains
its native behavior. Host regression covers each difficulty and both stages;
physical-device acceptance remains pending.

Difficulty hit areas join each rendered label to its visible reward caption
(64DDFDFF / 22AD620E / 3434E0AC) in the same column, including the empty gap.
The union has 12 horizontal and 8 vertical logical pixels of finger tolerance.
Taps on amounts, gaps and beside digits share the same select/confirm policy;
hidden reward captions do not extend the hit area. Host coverage exercises all
three difficulties with separate label and reward quads.

## Physical controller disables touch mode

input_mode_get() publishes one consistent policy snapshot. Android SDL controller
discovery reports physical controller presence, excluding the virtual touch device.
The policy combines it with the saved Sensor Mode preference.
Mode changes drop queued gestures. The race HUD and virtual touch buttons use
the same mode; connecting a pad immediately releases and hides them.
Disabled guest hooks call the original generated input bodies. Draw hooks skip
bounds collection, Back/footer replay, glyph suppression and Quick Race Continue
replacement. Recorded caption/color edits are restored on the guest thread and
native caption caches are marked dirty. Controller mode retains the rendering
corrections already present on main. Host coverage checks hotplug and startup
with a pad attached, discarded touch queues, and restoration of native Help.
Android device/runtime validation remains pending.

## Controller touch settings

Options -> Controller (category 3, native setup C2B80) retains its four native
rows and adds SENSOR MODE and RACE BUTTON OVERLAY as rows 5/6. Sensor Mode
starts with automatic controller detection until manually changed; its visible
value is the effective On/Off mode. With no physical pad, it is forced On and
labelled locked, and its handler rejects changes. A saved Off becomes effective
again when a pad reconnects. Race Button Overlay defaults Off and is editable
only while Sensor Mode is On; otherwise it displays Unavailable and ignores
input. Both preferences are stored in nfsu1x_options.txt. The native row handlers
and rendered arrows serve both controller and touch input.

### Input responsibilities

- `input_mode.c/.h`: preference, controller presence, overlay request, effective
  values and revision; one atomic publication, no SDL or guest-memory dependency.
- `menu_touch.c`: position queue and gesture lifecycle; a policy revision change
  discards stale gestures while holding the queue mutex.
- `android_input.c`: physical SDL discovery, state reads, rumble and player slots.
  The physical controller remains player one; active touch buttons can supplement
  its buttons/triggers and steering without changing device routing.
- `android_race_controls.c`: racing/overlay visibility and virtual touch reports.
- `feng_touch.c`: native menu gestures and reversible touch-only rendering edits.
- `recomp_manual.c`: native Controller menu adapter and preferences file. Reads
  the policy directly, with no second copy of the saved preferences. Refreshes
  the registered settings screen after policy revisions on the guest thread.

Host regressions cover physical and sensor input together, hiding overlay while
retaining physical input, startup with a pad, reconnects, preference persistence,
queue cancellation and the existing FEng gesture behavior. They do not establish
Android UI layout or hardware acceptance.

## Shared gesture components

`touch_gesture` recognizes a tap or one directional swipe on release. Bindings
choose the axis, threshold and whether taps are allowed. Cancellation after a
drag, one-shot dispatch and finger ownership are independent of native screens.
Both horizontal browsers and the vertical magazine grid use this implementation.

`feng_touch_carousel` supplies the native bindings and common rendered capture
for garages, Quick Race cars/tracks, first career car selection and Underground
race selection. Screen-specific data includes the selected-node location,
activation event, extra captions and optional separate native data cursor.
The map no longer has a second selection hit-test in generic screen controls.
A swipe never synchronizes or activates the tapped map item.

`touch_orbit` owns drag state, control exclusion, blocked-panel crossings and
screen changes. `feng_touch_camera` only applies the resulting deltas to the
native camera. A preview-capable screen calls this shared pair; track/map screens
do not opt into orbit. `feng_touch_magazines` binds rendered slots and cursor
refresh, using the same swipe lifecycle as horizontal carousels.

`touch_geometry` shares containment and bounds union. `feng_touch_internal.h`
is a private contract between guest adapters; external hooks stay in
`feng_touch.h`. New screens should add native binding data, reuse these
components, and avoid copying gesture/update code.

Focused engine tests without a guest runtime:

```sh
cc -std=c11 -Wall -Wextra -Werror -Isrc tests/touch_gestures.c \
  src/touch_gesture.c src/touch_orbit.c -lm -o /tmp/nfsu-touch-gestures-test
/tmp/nfsu-touch-gestures-test
```
