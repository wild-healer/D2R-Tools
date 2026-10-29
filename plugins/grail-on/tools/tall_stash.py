# Builds the grail view's frame from Expanded Stash Plus's stash
# (src/art/stash.png, 1687x1801): the view is 18 cells tall, so the frame
# grows by two cells (the view takes the old title band), and the title goes under it.
# Rows of the original: 0-65 top frame, 65-170 title band, 170-177 the grid's
# top edge, 177-1745 the 16 cells, 1745-1753 the grid's bottom edge, 1753-1801
# bottom frame. Writes src/art/stash_tall.png (1687x1829) and its low-end copy
# (a third of the size, like the original's).
import sys
from pathlib import Path
from PIL import Image

art = Path(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "src" / "art")
old = Image.open(art / "stash.png").convert("RGBA")
w = old.width
# Trimmed so the window stays above the skill bar: 25 rows of frame at the top
# and at the bottom; the title is drawn under the frame.
# The two extra cells repeat rows from the middle, so the side borders' end
# pieces (above the bottom frame) stay at the bottom.
parts = [(0, 25), (170, 177), (177, 961), (765, 961), (961, 1745), (1745, 1753), (1776, 1801)]
h = sum(b - a for a, b in parts)
new = Image.new("RGBA", (w, h))
y = 0
for a, b in parts:
    new.paste(old.crop((0, a, w, b)), (0, y))
    y += b - a
new.save(art / "stash_tall.png")
new.resize((round(w / 3), round(h / 3)), Image.LANCZOS).save(art / "stash_tall.lowend.png")
print(new.size)
