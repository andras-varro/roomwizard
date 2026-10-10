#!/bin/bash
# Render distro/arca-logo.svg to preview PNGs: large, and small sizes upscaled
# (nearest neighbour) so the loss of detail is visible. Run in WSL.
set -e
cd "$(dirname "$0")"
mkdir -p preview
rsvg-convert -w 512 -h 512 arca-logo.svg -o preview/logo-512.png
for s in 96 32 16; do
  rsvg-convert -w "$s" -h "$s" -b '#2D2D3C' arca-logo.svg -o "preview/logo-$s.png"
done
python3 - <<'EOF'
from PIL import Image
tiles = [Image.open(f"preview/logo-{s}.png").convert("RGB").resize((256, 256), Image.NEAREST)
         for s in (96, 32, 16)]
sheet = Image.new("RGB", (256 * 3 + 40, 256), (20, 20, 28))
for i, t in enumerate(tiles):
    sheet.paste(t, (i * 276, 0))
sheet.save("preview/small-sizes.png")
EOF
