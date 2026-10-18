# Custom cockpit and gloves

These assets are shared with [DR-89/nfsu2-vr](https://github.com/DR-89/nfsu2-vr). They were generated with Tripo for the VR project and fitted offline. They are not extracted game models or textures.

## Runtime files

- `runtime/cockpits/shared.ncpk`: the common cabin, 50,410 triangles and a 4096 × 4096 RGBA atlas. Includes dashboard, console, seats, floor, door interiors and an animated steering wheel.
- `runtime/cockpits/driver-gloves.nghv`: the custom glove model with open/grip poses and texture.
- Adjacent JSON files record source hashes and preparation metadata. The runtime reads the binary files.

Original generated GLBs, fit configuration and previews are retained here for asset editing. `source/tripo-full-cabin-textured.glb` is the current cabin source; `source/tripo-racing-glove.glb` is the glove source. Earlier cabin variants are reference material. Normal builds use the prepared runtime files and do not require Tripo or modeling tools.

## NFSU1 integration

The shared prototype is fitted to 20 stock cars by `quest/runtime/quest_cockpit_vehicle.h`; it is not an authentic interior for each car. The accepted geometry and fit are retained. The camera follows the original body's render origin, including its alternate pose.

The original game supplies exterior bodywork and mirror glass at runtime. The port identifies the glass and samples the game's rear camera onto it; those retail models and rear images are not included here. The original blue screen-HUD mirror frame is excluded from cockpit mirror RGB, and rear-camera ownership is restored after pause/resume.

`quest/runtime/quest_cockpit_hud.h` mounts the full NFSU1 turbo/tachometer cluster and minimap on the dashboard. The map follows a roughly 16 × 17 cm console plane. Pause menus use one complete plane, and seat settings immediately shift the common eye transform.

Quest testing confirms the accepted Golf fit and three mirrors, Eclipse HUD layout, stripe-free mirrors after seat adjustment, and cockpit/seat preview during pause. Broader car and tuning validation remains open; see [feature parity](../../FEATURE_PARITY.md).

## Format and checks

NCPK v2 contains a 64-byte header, interleaved position/normal/UV vertices, triangle indices and an RGBA atlas. The loader validates dimensions, bounds, finite values, indices and steering-shaft geometry. Gloves use their own validated NGHV format.

`tests/quest_cockpit_test.c` covers camera transforms, fitting, controls, HUD coverage/mounting, mirror classification and asset validation. Any fixtures or captures derived from the original game remain in ignored private build directories.
