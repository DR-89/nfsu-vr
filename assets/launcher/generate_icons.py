"""Regenerate Android launcher mipmaps from the supplied 256px icon."""
from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parents[2]
source = Image.open(root / 'assets/launcher/ic_launcher_source.png').convert('RGBA')
resources = root / 'android/app/src/main/res'
for density, size in (('mdpi', 48), ('hdpi', 72), ('xhdpi', 96),
                      ('xxhdpi', 144), ('xxxhdpi', 192)):
    folder = resources / ('mipmap-' + density)
    source.resize((size, size), Image.Resampling.LANCZOS).save(folder / 'ic_launcher.png')
    # 108dp adaptive canvas; keep the supplied artwork inside its central 66dp
    # safe zone so round and squircle launcher masks retain the disc/caliper.
    canvas_size = size * 108 // 48
    artwork_size = round(canvas_size * 66 / 108)
    foreground = Image.new('RGBA', (canvas_size, canvas_size), (0, 0, 0, 0))
    artwork = source.resize((artwork_size, artwork_size), Image.Resampling.LANCZOS)
    offset = (canvas_size - artwork_size) // 2
    foreground.alpha_composite(artwork, (offset, offset))
    foreground.save(folder / 'ic_launcher_foreground.png')
