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
import tools_render
import numpy as np
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
# The same shrines drawn from their HD floating symbols (the default; the
# classic ones stay as a setting): the symbol's model rendered from the front
# with its albedo texture, tinted where the game colours a grey model with its
# effect; lightning resist is a flat picture in the game, mana recharge a glow
# (drawn here as a blue orb); fire resist and mana recharge sit in a flame (a
# particle effect in the game, drawn here). Cells: unused frames of default07, in code order.
# Keep in sync with BoosterHdCell in src/plugin.cpp.
BOOSTER_HD_CELLS = [718, 719, 720, 721, 722, 723, 724, 725, 726, 727]
LIT = 'hd/vfx/meshes/lit_mesh/'
BOOSTER_HD = [  # (model, albedo, tint) under LIT; None = drawn another way
    ('shrine_armor/shrine_armor_lod0.model', 'shrine_armor/shrine_armor_alb.texture', None),
    ('shrine_combat/shrine_combat_lod0.model', 'shrine_combat/shrine_combat_alb.texture', None),
    ('shrine_resist_fire/shrine_resist_fire_lod0.model', 'shrine_resist_fire/shrine_fire_resist_alb.texture', 'fire'),
    ('shrine_resist_cold/shrine_resist_cold_lod0.model', 'shrine_resist_cold/shrine_resist_cold_alb.texture', (140, 200, 255)),
    None,  # lightning
    ('shrine_resist_poison/shrine_resist_poison_lod0.model', 'shrine_resist_poison/shrine_resist_poison_alb.texture', (120, 230, 90)),
    ('shrine_skill/shrine_skill_lod0.model', 'shrine_skill/shrine_skill_alb.texture', (255, 225, 150)),
    None,  # mana recharge
    ('shrine_stamina/shrine_stamina_lod0.model', 'shrine_stamina/shrine_stamina_alb.texture', (255, 225, 150), 90),  # seen from the side, as in game
    ('shrine_experience/shrine_experience_lod0.model', 'shrine_experience/shrine_experience_alb.texture', 'cross'),
]

def rgba(array):
    return Image.fromarray((np.clip(array, 0, 1) * 255).astype('uint8'), 'RGBA')

def bc4(path):
    blob = open(os.path.join(data, path), 'rb').read()
    width, height = struct.unpack_from('<II', blob, 8)
    length, rel = struct.unpack_from('<II', blob, 0x24)
    return np.asarray(Image.frombytes('L', (width, height), blob[0x28 + rel:0x28 + rel + length], 'bcn', 4)).astype(float) / 255

def flame(size, inner, outer, core):
    """A three-tongued flame (the effects' flame noise for the flicker and heat),
    for the symbols whose flame is a particle effect."""
    noise = bc4('hd/vfx/textures/noise/T_FX_Noise_Flamelike_Noise_Tile_Mono.texture')
    y, x = np.mgrid[0:size, 0:size] / size
    n = np.asarray(Image.fromarray((noise * 255).astype('uint8')).resize((size, size))) / 255.0
    x = x + 0.06 * (n - 0.5) * (1 - y)
    def drop(cx, top, bottom, width):
        u = np.clip((y - top) / (bottom - top), 0, 1)
        half = np.where(u < 0.62, width * (u / 0.62) ** 1.4, width * np.sqrt(np.clip(1 - ((u - 0.62) / 0.38) ** 2, 0, 1)))
        half = np.where((y < top) | (y > bottom), 0, half)
        return np.clip((half - np.abs(x - cx)) / 0.025, 0, 1), np.clip(1 - np.abs(x - cx) / np.maximum(half, 1e-3), 0, 1) * u ** 0.6
    tongues = [drop(0.5, 0.0, 0.98, 0.40), drop(0.30, 0.22, 0.95, 0.24), drop(0.70, 0.18, 0.95, 0.24)]
    mask = np.max([t[0] for t in tongues], 0)
    heat = np.clip(np.max([t[1] for t in tongues], 0) + 0.35 * (n - 0.5), 0, 1)[..., None]
    colour = np.array(outer) * (1 - heat) + np.array(inner) * heat
    colour = colour * (1 - heat ** 4) + np.array(core) * heat ** 4
    return np.dstack([colour, mask])

def in_flame(ball, inner, outer, core, size=200):
    """`ball` (a 200 px symbol) shrunk into the foot of a flame."""
    small = ball.resize((int(size * 0.58), int(size * 0.58)), Image.LANCZOS)
    canvas = Image.new('RGBA', (size, size))
    canvas.alpha_composite(small, ((size - small.width) // 2, 80))
    front = np.asarray(canvas).astype(float) / 255
    out = flame(size, inner, outer, core)
    a = front[..., 3:4]
    out[..., :3] = front[..., :3] * a + out[..., :3] * (1 - a)
    out[..., 3] = np.maximum(out[..., 3], front[..., 3])
    return rgba(out)

def hd_symbol(index):
    """The HD floating symbol of booster shrine `index` (code order), 200 px."""
    size = 200
    if index == 4:  # lightning: the effect's bolt picture (BC4, one channel)
        blob = open(os.path.join(data, 'hd/vfx/textures/graphic/T_FX_Shrine_Resist_Lightning.texture'), 'rb').read()
        width, height = struct.unpack_from('<II', blob, 8)
        length, rel = struct.unpack_from('<II', blob, 0x24)
        a = np.asarray(Image.frombytes('L', (width, height), blob[0x28 + rel:0x28 + rel + length], 'bcn', 4)).astype(float) / 255
        return rgba(np.dstack([a, a * 0.92, a * 0.47, a]))
    if index == 7:  # mana recharge: a lit blue orb
        y, x = np.mgrid[0:size, 0:size]
        r = np.hypot(x - size / 2, y - size / 2) / (size / 2)
        lit = np.clip(1 - np.hypot(x - size * 0.4, y - size * 0.38) / (size * 0.5), 0, 1)
        orb = rgba(np.dstack([(40 + 150 * lit ** 2) / 255, (90 + 150 * lit ** 1.5) / 255, (200 + 55 * lit) / 255, (r < 0.92) * 1.0]))
        return in_flame(orb, (0.3, 0.6, 1.0), (0.05, 0.08, 0.5), (0.85, 0.95, 1.0))
    model, albedo, tint, *view = BOOSTER_HD[index]
    image = tools_render.render(tools_render.meshes(os.path.join(data, LIT, model)), tools_render.texture(os.path.join(data, LIT, albedo)), view[0] if view else 0, 0, size)
    a = np.asarray(image).astype(float) / 255
    lum = a[..., :3].mean(-1)
    if tint == 'fire':  # the grey ball as a fireball: hot centre, red rim
        solid = a[..., 3] > 0
        lum = (lum - lum[solid].min()) / (np.ptp(lum[solid]) + 1e-6)
        y, x = np.mgrid[0:size, 0:size]
        rim = np.clip(np.hypot(x - size / 2, y - size / 2) / (size / 2), 0, 1)
        heat = np.clip(0.55 * lum + 0.6 * (1 - rim), 0, 1)
        a[..., :3] = np.dstack([0.55 + 0.6 * heat, heat ** 1.2 * 0.95, heat ** 3 * 0.6])
        return in_flame(rgba(a), (1.0, 0.6, 0.1), (0.75, 0.1, 0.0), (1.0, 0.95, 0.6))
    elif tint == 'cross':  # gold cross on a dark plaque
        cross = np.clip((lum - 0.6) / 0.15, 0, 1)[..., None]
        gold, dark = np.array([1.0, 0.82, 0.35]), np.array([0.22, 0.17, 0.10])
        a[..., :3] = dark * (0.6 + 0.6 * lum[..., None]) * (1 - cross) + gold * (0.75 + 0.35 * lum[..., None]) * cross
    elif tint:
        a[..., :3] = lum[..., None] * np.array(tint) / 255 * 1.5
    return rgba(a)

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
for index, cell in enumerate(BOOSTER_HD_CELLS):
    icons.append((cell, minimap_icon(stock, hd_symbol(index))))

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
