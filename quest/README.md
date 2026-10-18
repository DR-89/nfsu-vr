# NFS Underground VR on Quest 3

Development application: `org.nfsu1.quest`, native library `nfsu1_quest`. This project is separate from the upstream phone application in `android/`.

The APK boots on Quest 3. The user confirmed Quick Race starts with picture and controls. The pause VR Settings entry is confirmed visible and usable; settings survive restart. See [feature parity](../FEATURE_PARITY.md); the copied NFSU2 renderer is not proof that every feature works in NFSU1.

Build from the repository root with `tools/build-quest.ps1`, supplying SDK 35, NDK 29.0.14206865, Gradle 8.11.1 and JDK 17+. Output: `quest/app/build/outputs/apk/debug/app-debug.apk`.

Install with `adb install -r <apk>`. The first-run importer accepts the supported Xbox USA disc archive/image or extracted directory and checks every expected file. Existing saves are preserved. The local development installation used ADB to copy the already validated data into app-private `files/game`.

Touch: Start/menu pauses, right stick click cycles cameras including cockpit, both stick clicks recenter. Left stick steers, right/left triggers accelerate/brake. A nitro, B handbrake and right stick up/down manual shifts use the shared driving mapping and need individual NFSU1 validation. Grips at the wheel rim provide motion steering. The confirmed pause entry is `[X] VR SETTINGS`; it opens sensitivity, wheel rotation, seat height/distance and resolution settings.

The virtual wheel compensates NFSU1's original 24% stick deadzone only while it owns steering (including its released, latched angle). Stick takeover, menus and gamepads retain their normal input. The small downstream center filter remains. With 180° rotation and sensitivity 2, the retail-path test now produces steering events at ±2°, where the old packets remained neutral even at ±10°. `tools/make-steering-fixture.py` extracts private test fixtures from the supported executable; `nfsu1_steering_test` runs 196,852 checks through the actual retail filter, packing and scanner on Windows and Quest. The update is installed; the user's driving-feel confirmation is pending.

The original game draws a blue HUD mirror frame into the 256×64 rear-camera image. Reusing that image on physical glass exposed the frame as blue stripes in all three mirrors, reported after moving the seat. The cockpit renderer now omits only the identified flat RGB decoration pass; rear scenery, lights, the alpha mask and exterior-camera HUD are retained. Seat transforms and interior geometry are unchanged. Draw-scope regression checks pass on Windows and Quest. After changing the seat position, the user confirms that all three mirrors are free of blue stripes on Quest.

Pausing previously stopped the driving-camera updates long enough for the XR cockpit state to expire, hiding the interior and its seat preview. The original HUD option transition could also destroy the VR-created rear camera without restoring it on resume. The render boundary now resolves the current player from the live view/manager chain, refreshes the cockpit pose and active state, and recreates a missing rear camera only when its view is empty. Camera-cycle exit retains ownership checks. Camera lifecycle tests cover repeated pause/resume, restart, teardown, non-player cameras and foreign rear views; 62 title checks pass on both hosts. The updated APK is installed. The user confirms that the cockpit stays visible while paused, seat adjustment is immediately visible, and all three mirrors continue working after pause/resume on Quest.

Logs currently use app-private `files/nfsu2.log` (legacy runtime filename): `adb shell run-as org.nfsu1.quest cat files/nfsu2.log`. Developer options go in app-private `quest-env.txt`; the runtime still recognizes shared `NFSU2_*`/`RECOMP_*` names.

The cockpit HUD now uses NFSU1-specific regions in `quest_cockpit_hud.h`. The complete instrument cluster (including the left turbo dial) stays behind the wheel; the full minimap is mounted on a roughly 16×17 cm plane following the center console. The previous NFSU2 boundaries split both widgets onto the windshield. All 307,200 source pixels are covered exactly once, with mounting/UV checks across all 21 fit profiles passing on Windows and Quest. The full pause menu is retained. The updated APK is installed. A live Eclipse capture shows the full map on the console and the complete turbo/tachometer cluster on the instrument plane, with no split widgets on the windshield. The user confirms that the complete instrument display and map placement on the center console now fit correctly in the Eclipse on Quest.

The accepted [performance profile](performance.env) is compiled into Quest
build defaults. App-private `quest-env.txt` overrides individual options.
It enables multiview (one CPU draw for both eyes), fixed foveation (central
65% at full shading, outer region 2×2), shader specialization, immutable
dynamic descriptors, compact vertex bindings, attachment load clears and
verified bulk command uploads. State caching and instancing remain off.
The 72 Hz display request does not promise 72 game FPS.

With 1680×1760 per eye, a 64-second Golf run recorded 33.7–61.3 FPS,
44.73 FPS weighted by elapsed time, versus 24.78 FPS in the earlier
2550×2640 sample. These are different routes/settings, not a controlled
percentage. The user confirms noticeably smoother play and correct graphics.
The GPU multiview test checks 768 pixels; shader and compact-binding tests
pass. The final installed APK also passes all 768 live scalar/bulk command
comparisons. Its additional bulk-upload FPS effect has not been measured.
Stable 72 FPS remains open. Analyze complete tracked/unpaused windows with
`python tools/analyze-quest-performance.py <log> --output <summary.json>`.

Quest now uses the shared masked-address texture decoder for swizzled ARGB,
XRGB and paletted textures. It preserves the existing upload and cache rules,
compares the first 64 textures of each format against the original decoder,
and falls back on any pixel mismatch. `NFSU2_VR_TEXTURE_DECODE=0` restores the
original path. The common test passes 822 cases / 92,252,156 pixels on Quest
and Windows, including unaligned input and protected memory boundaries.
The installed update's first 64 live ARGB and 64 paletted textures were
pixel-identical: 178.667/12.285 ms and 75.033/4.541 ms original/masked decoding
totals. Ten complete unpaused Golf windows at the current 2550×2640 setting
measured 20.6–29.5 FPS (median interval rate 23.5). Decoder timing alone is not
game FPS; a controlled before/after race comparison and stable VR frame rate
remain open.

The user confirms normal speed and rendering after the timing integration. The 2940×3080 eye preset measured about 18 FPS in a race. The requested 1680×1760 preset is saved and logged for both eyes; initial unpaused Golf samples measured 22–23 FPS with roughly 30–35 ms GPU elapsed time. Different driving samples do not establish a controlled speedup. Pause/menu samples must not count as driving performance. Stable VR frame rate remains open.

Stock geometry checks select both outside mirrors on all 20 cars (82 material batches) and 18 center mirrors across 1,077 stock submeshes, on Windows and Quest. Focus/Tiburon center glass remains unresolved. These checks validate geometry selection, not the actual live rear-camera image. Audio, HUD, saves, long sessions and individual controls still need hardware validation.

The Golf GTI test at 1680×1760 felt smoother to the user. The initial mirror and cabin-alignment failure came from using Car+0x60, approximately 20 cm forward and 28 cm above the rendered origin. The camera now follows the render pose selected by retail 12C420 (ordinary +0x90/+0x50 or alternate +0x4e0/+0x520). Thirty-five title/memory/pose checks pass on Windows and Quest. The user reports a clear improvement and explicitly accepts the existing interior; keep its model and fit. Updated logs select 12/30/30 center/left/right mirror glass vertices with a valid rear image. The user confirms that all three show the rear view. The subsequent FPS-boost request uses 1680×1760 per eye, with the same wheel and seat settings.
