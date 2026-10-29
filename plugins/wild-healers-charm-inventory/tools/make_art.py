# Builds the panel's packaged art (the same as Run Counter's stats window) from
# the stash frame Grail On! ships
# (Expanded Stash Plus's stash, 1687x1801, 16x16 cells of 98):
#   src/art/frame.png (1162x1507, the size of the game's left panels) and its
#   half-size frame.lowend.png: the stash's corners, edges and gold inner line,
#   cropped (never scaled) to the new size, the grid replaced by dark stone;
#   src/art/slot.png: one empty cell (98x98), tiled under the charm grid.
# Usage: python3 make_art.py <stash.png> <art folder>
import random, sys
from pathlib import Path
from PIL import Image

stash = Image.open(sys.argv[1]).convert("RGBA")
art = Path(sys.argv[2])
W, H = 1162, 1507
sw, sh = stash.size
# Inside the gold line: x 59..1627, y 178..1744 on the stash.
left, right, top, bottom = 59, sw - 1628, 178, sh - 1745
corner = 160  # the top and bottom corners keep their ornaments

out = Image.new("RGBA", (W, H))
# The dark stone inside, with the grain of the game's panels.
random.seed(7)
stone = Image.new("RGBA", (W - left - right, H - top - bottom))
px = stone.load()
coarse = {}
for y in range(stone.height):
    for x in range(stone.width):
        k = (x // 3, y // 3)
        if k not in coarse:
            coarse[k] = random.random()
        n = 0.6 * random.random() + 0.4 * coarse[k] - 0.5
        v = [max(0, min(255, int(c + n * 12))) for c in (18, 18, 19)]
        px[x, y] = (v[0], v[1], v[2], 255)
out.paste(stone, (left, top))
# Top band (frame and title band) and bottom frame: corners, then the middle cropped.
for (sy, h, dy) in ((0, top, 0), (sh - bottom, bottom, H - bottom)):
    out.paste(stash.crop((0, sy, corner, sy + h)), (0, dy))
    out.paste(stash.crop((sw - corner, sy, sw, sy + h)), (W - corner, dy))
    mid = W - 2 * corner
    x0 = (sw - mid) // 2
    out.paste(stash.crop((x0, sy, x0 + mid, sy + h)), (corner, dy))
# Side edges with the gold line, cropped from the middle rows.
span = H - top - bottom
y0 = top + 40
out.paste(stash.crop((0, y0, left, y0 + span)), (0, top))
out.paste(stash.crop((sw - right, y0, sw, y0 + span)), (W - right, top))
out.save(art / "frame.png", optimize=True)
out.resize((W // 2, H // 2), Image.LANCZOS).save(art / "frame.lowend.png", optimize=True)
# One cell: the first one of the grid.
stash.crop((left, top, left + 98, top + 98)).save(art / "slot.png")
print("frame", out.size)
