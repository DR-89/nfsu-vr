# NFSU2 VR feature parity tracking

Target: every applicable VR/host feature in the local `nfsu2 - vr port` reference, on Windows PCVR and Quest 3. Imported source alone is not acceptance. NFSU2-specific gameplay (SMS, open-world shops and its extra cars) is not NFSU1 content.

| Feature | NFSU1 implementation and evidence | Still required |
|---|---|---|
| Native Quest ARM64 game | APK boots; user confirms Quick Race picture and controls | Broader game modes, saves, long sessions |
| Windows x64 host, data importer | Builds; 43 importer assertions pass | Actual PCVR headset session, deferred by user while standalone is prioritized (desktop title screen renders on RTX 3050) |
| OpenXR stereo, 6DoF, multiview | Shared renderer integrated; multiview now enabled on Quest. GPU test: 768 pixels pass for stereo depth, textures, feedback, presentation and mono rendering. User accepts the optimized Golf image | View correctness in different race modes and PCVR |
| Tracked culling/projection | NFSU1 primary-view mapping; shared numerical tests pass | Head-turn edge cases and cockpit verification |
| Cinema menus, videos, HUD | Shared composition integrated; Quest start screen inspected. NFSU1-specific cockpit crops retain the entire tachometer/turbo cluster and map, with console mounting; complete 640×480 pixel coverage and all fit profiles pass on both hosts. User confirms the complete instrument display and correct console map placement in the Eclipse on Quest | Other race modes and notifications; PCVR visual validation |
| Extra cockpit camera slot | NFSU1 camera/cycle/manager mapped; selection logged in Quick Race | Camera transitions, replay and all race types |
| Fitted interior and original car body | Profiles derived from all 20 NFSU1 stock cars; body-retention hook implemented. Golf camera origin corrected to the retail render pose. User reports a clear improvement and explicitly accepts the existing interior; retain its model and fit | Other cars/tuned parts; no Golf-specific replacement requested |
| Remove crossed driver cards | 1,077 original submeshes extracted; 20 labelled driver cards | Classifier passed on Windows and Quest; live visual confirmation remains |
| Glass/tint removal | NFSU1 six window slots and texture ownership mapped; synthetic tests pass on both hosts | Real draw captures and live glass/tint visibility |
| Three mirrors on original geometry | 82 side batches select both mirrors on all 20 stock cars; 1,077 stock submeshes select 18 center mirrors on Windows and Quest. After the render-origin fix, Golf logs select 12/30/30 center/left/right glass vertices; user confirms all three show the rear view. The blue HUD frame is excluded from cockpit mirror RGB; user confirms all three remain stripe-free after seat adjustment | Other cars/tuned parts and PCVR visual validation; Focus/Tiburon center glass unresolved |
| Motion wheel, one/two hands, gloves | Shared implementation and numerical tests ported; wheel-only compensation for NFSU1's 24% stick deadzone passes 196,852 retail-input checks on Windows and Quest; Quest update installed | User confirmation of corrected small-angle response and stable center; remaining glove/grip cases |
| Touch/gamepad controls, camera, gears | General Touch driving confirmed; shared input implementation ported | Nitro/handbrake/gears/rear view and gamepad validation |
| Vibration | Shared input/output and haptic tests pass | NFSU1 game effect and controller validation |
| Recenter | Shared implementation present | Hardware test |
| Pause VR Settings | Active package chain fixed; user confirms visible and usable on Quest; persistence/reload logged. Render-boundary cockpit refresh and rear-camera recovery added for pause/resume; lifecycle checks pass on both hosts. User confirms the cockpit remains visible, seat adjustment previews immediately, and all three mirrors work after pause/resume on Quest | PC headset validation and nested-dialog input checks |
| Wheel ratio/sensitivity, seat height/distance | Shared settings/transformation tests pass; menu access and saved sensitivity/seat changes confirmed on Quest | Wheel feel and all seat limits in live races |
| Resolution presets, save/restart | The FPS-boost request restores the earlier 1680×1760 preference from 2550×2640. Both eye buffers verified after restart; wheel and seat fields preserved | Performance at each preset and PC end-to-end test |
| 60/72/80/90/120 Hz and simulation pacing | NFSU1 flip/minimum-step/carry integration implemented; 46,293 actual lifted-code checks pass on both hosts; user confirms normal speed and rendering on Quest | Sustainable frame rate and higher-refresh hardware validation |
| Fast swizzled texture decoding | PCVR decoder shared with Quest; 822 cases and 92,252,156 pixels pass on each host, including protected memory boundaries. Updated APK installed; 64 live ARGB and 64 paletted textures are pixel-identical (178.667/12.285 ms and 75.033/4.541 ms original/masked totals). User confirms correct textures and mirrors | XRGB live verification and controlled performance comparison; decoder timing alone is not game FPS |
| Quest render performance profile | Multiview, fixed foveation, single-instance/fragment specialization, immutable dynamic descriptors, compact bindings and load clears enabled; user reports noticeably smoother play with correct graphics. Final APK also enables bulk uploads: all 768 live scalar/bulk packet comparisons pass. Profile is compiled into Quest defaults, with private options retaining precedence | Sustained 72 FPS, broader race/tuned-car coverage and controlled isolation of each optimization |
| Audio | Native Xbox APU integrated | User confirmation and long-run checks |
| Import, data integrity, notices | Strict NFSU1 manifest; separate app/storage identity | Quest importer: 32 tests pass; distributable packaging remains |

The 2940×3080 race run measured about 18 FPS. Twelve complete unpaused Golf windows at 1680×1760 measured 17.1–30.2 FPS (median interval rate 23.05 FPS), after excluding menu-transition windows. The user reports smoother play. Routes and views are not controlled comparisons; stable VR performance remains open. Runtime driving statistics now also exclude pause/menu intervals directly.

These are development observations from 2026-10-18, not a release checklist marked complete. The Switch repository is evaluated as a source of fixes; the newer Android base remains the primary host/core.

After sharing the fast decoder with Quest, ten complete unpaused Golf windows
at the user's current 2550×2640 setting measured 20.6–29.5 FPS (median interval
rate 23.5). This route/view was not matched to the earlier lower-resolution run,
so it does not establish an end-to-end speedup. GPU elapsed time remained high.

The subsequent FPS-boost run uses the accepted performance profile and
1680×1760 per eye. Twenty-four complete, unpaused Golf windows span about
64.39 seconds: 33.7–61.3 FPS, median window 45.4, elapsed-weighted 44.73 FPS;
the worst recorded frame is 36.3 ms. The pre-boost 2550×2640 log has 18 windows
over 87.17 seconds at an elapsed-weighted 24.78 FPS. These different routes and
resolutions do not establish a controlled percentage. Multiview removes the
right-eye snapshot work; sampled GPU elapsed time falls from roughly 38–43 ms
to around 19 ms. The final APK additionally disables the packet-survey gate,
allowing bulk uploads; its 768 live comparisons pass, but their additional FPS
effect has not been measured. Known failed state-cache/instancing trials stay off.
