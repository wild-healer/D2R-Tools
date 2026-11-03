#!/usr/bin/env python3
"""Builds src/minimap_cells.bin, the minimap art built into the plugin, from my
extracted game data.

Minimap: the gold shrine cell (310) of the HD automap gets one variant per
icon, with the item's inventory picture (or, for booster shrines, the classic
floating symbol) over the ring's diamond. The plugin paints these variants into
automap cells no data file uses (see MINIMAP_CELLS and BOOSTER_CELLS) of its
copy of the game's sheet when the game loads it, so the number of automap
tiles stays the same; the plugin hands those cells out for shrines.

minimap_cells.bin: "SIC1", u32 count, then per frame u16 cell, u8 lowend (1 =
the half-size sheet), u8 0, u16 width, u16 height, width * height RGBA bytes.
Usage: tools_make_minimap.py <game data folder>
"""
import os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tools_dcc
from PIL import Image, ImageDraw, ImageFilter

data = sys.argv[1]
here = os.path.dirname(os.path.abspath(__file__))

# Unused automap cells, in shrine code order of ICONS. Keep in sync with
# Icons in src/plugin.cpp. All are in default13 (cells 1300-1399).
MINIMAP_CELLS = [1332, 1335, 1358, 1359, 1369, 1386, 1387, 1388, 1389]
SHRINE_CELL = 310

# shrine code -> (key, item folder under data/hd/items, item file stem, texture stem)
ICONS = {
    1:  ('refill',    'misc/potion/full_rejuv_potion',    'full_rejuv_potion',    'misc_full_rejuv_potion'),
    2:  ('health',    'misc/potion/full_healing_potion',  'full_healing_potion',  'misc_full_healing_potion'),
    3:  ('mana',      'misc/potion/full_mana_potion',     'full_mana_potion',     'misc_full_mana_potion'),
    17: ('portal',    'misc/scroll/town_portal_scroll',   'town_portal_scroll',   'misc_town_portal_scroll'),
    18: ('gem',       'misc/gem/perfect_amethyst',        'perfect_amethyst',     'misc_perfect_amethyst'),
    19: ('fire',      'misc/torch/torch',                 'torch',                'misc_torch'),
    20: ('monster',   'misc/gem/perfect_skull',           'perfect_skull',        'misc_perfect_skull'),
    21: ('exploding', 'weapon/potion/exploding_potion',   'exploding_potion',     'exploding_potion'),
    22: ('poison',    'weapon/potion/choking_gas_potion', 'choking_gas_potion',   'choking_gas_potion'),
}

# ---- minimap ----

# Booster shrines (codes 6-15) already float a symbol in the world but share the
# gold cell on the map too; they get an icon each as well: the first frame of
# their classic floating symbol (global/overlays/*.dcc), drawn as the game shows
# it, dark glow included.
# Cells: unused frames of default07, in code order. Keep in sync with
# BoosterCell in src/plugin.cpp.
BOOSTER_CELLS = [700, 701, 702, 703, 704, 705, 706, 707, 715, 716]
BOOSTER_SYMBOLS = ['shrinearmor', 'shrinecombat', 'shrineresistfire', 'shrineresistcold', 'shrineresistlightning',
                   'shrineresistpoison', 'shrineskill', 'shrinemanaregen', 'shrinestamina', 'shrineexperience']

def read_sprite(path):
    blob = open(path, 'rb').read()
    frame_w, = struct.unpack('<H', blob[6:8])
    width, height = struct.unpack('<II', blob[8:16])
    frames, = struct.unpack('<I', blob[0x14:0x18])
    return blob[:0x28], Image.frombytes('RGBA', (width, height), blob[0x28:0x28 + width * height * 4]), frames

def sheet_frame(image, frames, index):
    step = image.width // frames
    return image.crop((index * step, 0, (index + 1) * step, image.height)), step

def minimap_icon(stock, picture):
    """The item picture as wide as the cell, outlined in dark and gold so it
    reads as a shrine marker, over the diamond of cell 310 (the shrine's spot).
    The game scales each cell's visible art to the cell width, so a ring around
    the picture would only make the picture smaller."""
    icon = Image.new('RGBA', stock.size)
    icon.alpha_composite(stock.crop((0, 92, stock.width, stock.height)), (0, 92))
    picture = picture.crop(picture.getchannel('A').getbbox())
    scale = min((stock.width - 8) / picture.width, 84 / picture.height)
    picture = picture.resize((max(1, round(picture.width * scale)), max(1, round(picture.height * scale))), Image.LANCZOS)
    face = Image.new('RGBA', stock.size)
    face.alpha_composite(picture, (round((stock.width - picture.width) / 2), round(90 - picture.height)))
    alpha = face.getchannel('A').point(lambda a: 255 if a > 40 else 0)
    gold = alpha.filter(ImageFilter.MaxFilter(5))
    dark = gold.filter(ImageFilter.MaxFilter(3))
    for mask, colour in ((dark, (16, 8, 0)), (gold, (236, 178, 32))):
        icon.alpha_composite(Image.merge('RGBA', tuple(Image.new('L', face.size, c) for c in colour) + (mask,)))
    icon.alpha_composite(face)
    return icon

def classic_icon(stock, picture):
    """A classic symbol sprite as wide as the cell over the diamond of cell 310.
    Its dark glow stays: it is what makes it stand out on the map."""
    icon = Image.new('RGBA', stock.size)
    icon.alpha_composite(stock.crop((0, 92, stock.width, stock.height)), (0, 92))
    picture = picture.crop(picture.getchannel('A').getbbox())
    scale = min((stock.width - 4) / picture.width, 88 / picture.height)
    picture = picture.resize((round(picture.width * scale), round(picture.height * scale)), Image.LANCZOS)
    icon.alpha_composite(picture, ((stock.width - picture.width) // 2, 92 - picture.height))
    return icon

ui = os.path.join(data, 'hd/global/ui')
_, stock_sheet, stock_frames = read_sprite(os.path.join(ui, 'automap/default%02d/tiles.sprite' % (SHRINE_CELL // 100)))
stock, _ = sheet_frame(stock_sheet, stock_frames, SHRINE_CELL % 100)
icons = []
for (code, (key, folder, stem, tex)), cell in zip(ICONS.items(), MINIMAP_CELLS):
    _, picture, _ = read_sprite(os.path.join(ui, 'items', folder.rsplit('/', 1)[0], stem + '.sprite'))
    icons.append((cell, minimap_icon(stock, picture)))
palette = tools_dcc.load_palette(os.path.join(data, 'global/palette/act1/pal.dat'))
for name, cell in zip(BOOSTER_SYMBOLS, BOOSTER_CELLS):
    _, frames = tools_dcc.decode(os.path.join(data, 'global/overlays', name + '.dcc'))
    icons.append((cell, classic_icon(stock, tools_dcc.to_rgba(frames[0], palette))))

# Frames as the game sheets hold them: 66 x 128 (full) and 34 x 64 (lowend,
# the icon at half size in the frame's top left, as the game's lowend sheets).
blob = b''
count = 0
for cell, icon in icons:
    half = Image.new('RGBA', (34, icon.height // 2))
    half.paste(icon.resize((icon.width // 2, icon.height // 2), Image.LANCZOS), (0, 0))
    for low, image in ((0, icon), (1, half)):
        blob += struct.pack('<HBBHH', cell, low, 0, image.width, image.height) + image.tobytes()
        count += 1
open(os.path.join(here, 'src', 'minimap_cells.bin'), 'wb').write(b'SIC1' + struct.pack('<I', count) + blob)

preview = Image.new('RGBA', (66 * len(icons), 128), (38, 40, 60, 255))
for index, (_, icon) in enumerate(icons):
    preview.alpha_composite(icon, (index * 66, 0))
preview.crop((0, 20, preview.width, 128)).save(os.path.join(here, 'minimap-icons-preview.png'))
print('wrote', os.path.join(here, 'src', 'minimap_cells.bin'))
