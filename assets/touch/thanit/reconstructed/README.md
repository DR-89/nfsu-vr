# Reconstructed full buttons

Candidate artwork following the supplied ThanitMay_71 reference.
These are NOT lossless extractions of complete original buttons.

Steering, camera, brake and nitro use the previously extracted source vector
symbols. Circular backgrounds, rings and glow are reconstructed SVG artwork.
Accelerator and pause symbols are redrawn to remove flattened scene bands.
Transparent PNG exports are 512 x 512. preview.png uses a dark background.

Both user-converted SVG files were inspected: their content remains fragmented
into paths, clipping paths and embedded raster images rather than isolated full
button groups. ThanitMay_27 is a different two-player layout.

Run generate.py from the repository root to regenerate SVG files. PNGs were
rendered using ImageMagick. No Android integration, build or device check yet.

## Shift paddles

shift_down.svg / shift_up.svg are extracted source vector paths and gradients
from the LEFT player's turquoise controls in ThanitMay_27.svg, rather than
redrawn approximations. Transparent PNG exports are 256 x 384; see
shifts-preview.png. Scene background and separate shadow fragments are excluded.
extract_shifts.py reproduces the extraction from the supplied source SVG path.

## Handbrake

Original vector handbrake artwork matching the reconstructed circular controls.
handbrake.svg / .png: full button, transparent 512 x 512.
handbrake-symbol.svg / .png: separate lever symbol, transparent 256 x 256.
handbrake-preview.png: dark background for viewing only.
Run draw_handbrake.py from the repository root to regenerate SVGs.

## Correct brake / handbrake assignment

brake.svg / brake.png and Android touch_brake.png use the original wide brake
pedal. generate.py regenerates this artwork from the extracted source symbol.
handbrake.svg / handbrake.png and Android touch_handbrake.png use the classic
mechanical lever with an end release button. draw_handbrake.py regenerates it.
The earlier lever assigned to the ordinary brake was a mistake, now corrected.
Native mappings remain: brake index 2, handbrake index 3.
No build or device validation was run for this correction.
