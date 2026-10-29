#!/usr/bin/env python3
# Grail On! showcase images: in-game screenshots on the vanilla panel art,
# texts in the game's fonts. Usage: compose.py <screens dir> <out dir>
import sys, os
from PIL import Image, ImageDraw, ImageFont, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
SCREENS, OUT = sys.argv[1], sys.argv[2]
W, H = 1920, 1080
GOLD = (199, 179, 119)
TEXT = (214, 208, 196)
GREY = (150, 145, 135)

def font(size, name='exocet.otf'):
    return ImageFont.truetype(os.path.join(HERE, 'fonts', name), size)

def panel():
    # The vanilla panel (1162x1507) scaled to the image height, then widened:
    # corners and borders kept, the middle stretched.
    src = Image.open(os.path.join(HERE, 'panel_bg.png')).convert('RGBA')
    s = H / src.height
    p = src.resize((round(src.width * s), H), Image.LANCZOS)
    cut = 150  # the corner ornaments stay whole
    left, right = p.crop((0, 0, cut, H)), p.crop((p.width - cut, 0, p.width, H))
    mid = p.crop((cut, 0, p.width - cut, H)).resize((W - 2 * cut, H), Image.LANCZOS)
    out = Image.new('RGBA', (W, H))
    out.paste(left, (0, 0)), out.paste(mid, (cut, 0)), out.paste(right, (W - cut, 0))
    return out

TOP, BOTTOM, SIDE = 104, 1032, 58  # the panel's inner area (under the title band)

def screen(n):
    return Image.open(os.path.join(SCREENS, '%02d.png' % n)).convert('RGBA')

def place(img, shot, box, at, scale=None):
    """Crops `box` (x0, y0, x1, y1) of `shot`, scales it (or fits it in
    at=(x, y, w, h), centred) and draws it with a dark rim and a shadow."""
    crop = shot.crop(box)
    x, y, w, h = at
    if scale is None:
        scale = min(w / crop.width, h / crop.height)
    cw, ch = round(crop.width * scale), round(crop.height * scale)
    crop = crop.resize((cw, ch), Image.LANCZOS)
    px, py = x + (w - cw) // 2, y + (h - ch) // 2
    shadow = Image.new('RGBA', (cw + 40, ch + 40), (0, 0, 0, 0))
    ImageDraw.Draw(shadow).rectangle((20, 20, cw + 20, ch + 20), fill=(0, 0, 0, 170))
    shadow = shadow.filter(ImageFilter.GaussianBlur(10))
    img.alpha_composite(shadow, (px - 14, py - 10))
    img.alpha_composite(crop, (px, py))
    d = ImageDraw.Draw(img)
    d.rectangle((px - 1, py - 1, px + cw, py + ch), outline=(12, 11, 10, 255), width=2)
    d.rectangle((px - 3, py - 3, px + cw + 2, py + ch + 2), outline=(110, 98, 70, 255), width=1)
    return px, py, cw, ch

def text(img, xy, s, size, color=TEXT, anchor='mm', name='exocet.otf', shadow=True):
    d = ImageDraw.Draw(img)
    f = font(size, name)
    if shadow:
        d.text((xy[0] + 2, xy[1] + 2), s, font=f, fill=(0, 0, 0, 220), anchor=anchor)
    d.text(xy, s, font=f, fill=color, anchor=anchor)

def title(img, s):
    text(img, (W // 2, 66), s, 50, GOLD)  # between the top ridge and the inner panel

NOTE_LINE = 26

def caption(img, lines, y=None, size=30, notes=()):
    """Caption lines, then the credit notes in small grey text under them."""
    for i, line in enumerate(notes):
        text(img, (W // 2, BOTTOM - 10 - (len(notes) - 1 - i) * NOTE_LINE), line, 19, GREY)
    bottom = BOTTOM - 26 - (len(notes) * NOTE_LINE + 6 if notes else 0)
    y = y or bottom - (len(lines) - 1) * (size + 10)
    for i, line in enumerate(lines):
        text(img, (W // 2, y + i * (size + 10)), line, size, TEXT)

def save(img, name):
    img.convert('RGB').save(os.path.join(OUT, name), optimize=True)

GRAIL = (120, 0, 962, 912)          # the grail window, without the menu columns
GRAIL_MENUS = (0, 0, 962, 912)      # with them

def cover():
    img = panel()
    title(img, 'Grail On!')
    place(img, screen(4), (0, 0, 1800, 912), (SIDE + 20, TOP + 20, W - 2 * SIDE - 40, 780))
    caption(img, ['Your holy grail, inside the game'], notes=['Colored backgrounds only with Item Quality Background by YupGoolg', 'Several mods modified the game UI in this screenshot.'])
    save(img, '1-grail-on.png')

def drop_in():
    img = panel()
    title(img, 'Drop it in')
    half = (W - 2 * SIDE - 60) // 2
    a = place(img, screen(9), (115, 0, 1772, 1062), (SIDE + 20, TOP + 60, half, 720))
    b = place(img, screen(10), (115, 0, 1772, 1062), (SIDE + 40 + half, TOP + 60, half, 720))
    text(img, (a[0] + a[2] // 2, a[1] - 30), 'Before', 30, GOLD)
    text(img, (b[0] + b[2] // 2, b[1] - 30), 'After', 30, GOLD)
    caption(img, ['Drop or Ctrl+click a grail item: it goes straight to its own tile', 'Only one copy may be stored'], notes=['Several mods modified the game UI in this screenshot.'])
    save(img, '2-drop-it-in.png')

def everything():
    img = panel()
    title(img, 'Everything is there')
    shots = [(4, 'Uniques'), (5, 'Ethereal'), (6, 'Sets'), (8, 'Runewords')]
    w = (W - 2 * SIDE - 80) // 2
    for i, (n, name) in enumerate(shots):
        x = SIDE + 20 + (i % 2) * (w + 40)
        y = TOP + 50 + (i // 2) * 374
        r = place(img, screen(n), (120, 0, 962, 500), (x, y, w, 330))
        text(img, (r[0] + r[2] // 2, r[1] - 26), name, 30, GOLD)
    caption(img, ['Every unique, set item and runeword, with ethereal copies', 'Read from the game, so it follows your mod'], notes=['Colored backgrounds only with Item Quality Background by YupGoolg', 'Sockets shown only with Always Show Sockets by PyreFly'])
    save(img, '3-everything-is-there.png')

def stats():
    img = panel()
    title(img, 'Know where you stand')
    half = (W - 2 * SIDE - 60) // 2
    place(img, screen(11), GRAIL_MENUS, (SIDE + 20, TOP + 20, half, 800))
    place(img, screen(12), GRAIL_MENUS, (SIDE + 40 + half, TOP + 20, half, 800))
    caption(img, ['Overall progress and every category at a glance'])
    save(img, '4-know-where-you-stand.png')

def perfect():
    img = panel()
    title(img, 'Perfect copies')
    main = place(img, screen(13), GRAIL_MENUS, (SIDE + 100, TOP + 20, 1000, 800))
    # Gloom's Trap, a perfect copy: its frame up close.
    x = main[0] + main[2] + 80
    zoom = place(img, screen(13), (462, 222, 618, 312), (x, TOP + 200, W - SIDE - 60 - x, 420))
    text(img, (zoom[0] + zoom[2] // 2, zoom[1] - 34), 'Perfect', 34, GOLD)
    caption(img, ['A copy with every best roll gets a gold frame'], notes=['Tooltip frame only with Perfect Frame by yours truly'])
    save(img, '5-perfect-copies.png')

def compare():
    img = panel()
    title(img, 'Compare')
    place(img, screen(14), (115, 0, 1772, 912), (SIDE + 20, TOP + 20, W - 2 * SIDE - 40, 800))
    caption(img, ['Hover any item you find: your grail copy shows beside it'], notes=['Worse/Better tooltips only with Compare Indicators by yours truly'])
    save(img, '6-compare.png')

def rules():
    img = panel()
    title(img, 'Your rules')
    left = place(img, screen(15), GRAIL, (SIDE + 20, TOP + 20, 820, 800))
    x = left[0] + left[2] + 50
    w = W - SIDE - 20 - x
    off = place(img, screen(2), (120, 0, 962, 600), (x, TOP + 60, w, 340))
    on = place(img, screen(3), (120, 0, 962, 600), (x, TOP + 460, w, 340))
    text(img, (off[0] + off[2] // 2, TOP + 32), 'Spoilers off', 30, GOLD)
    text(img, (on[0] + on[2] // 2, TOP + 432), 'Spoilers on', 30, GOLD)
    caption(img, ['Spoilers, goals and loot filtering'], notes=['Colored backgrounds only with Item Quality Background by YupGoolg'])
    save(img, '7-your-rules.png')

os.makedirs(OUT, exist_ok=True)
for make in (cover, drop_in, everything, stats, perfect, compare, rules):
    make()
