#!/usr/bin/env python3
"""Builds mod/files/ (the D2RMM mod's binary files) from my extracted game data.

Each icon reuses the Skill Shrine symbol effect with its mesh, texture and glow
mask swapped for an item's. The effect file stores paths as length-prefixed
strings, so every new path has exactly the length of the one it replaces.

Minimap: the gold shrine cell (310) of the HD automap gets one variant per
icon, with the item's inventory picture inside the ring. The variants replace
automap cells no data file uses (see MINIMAP_CELLS), so the number of automap
tiles stays the same; the plugin hands those cells out for shrines.
Usage: tools_make_mod_files.py <game data folder>
"""
import os, shutil, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tools_dcc
from PIL import Image, ImageDraw, ImageFilter

data = sys.argv[1]
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'mod', 'files')

# Unused automap cells, in shrine code order of ICONS. Keep in sync with
# MinimapCell in src/plugin.cpp. All are in default13 (cells 1300-1399).
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

# World symbol taken from another model than the item: shrine code -> (model,
# albedo texture) under data/hd. The minimap keeps the item picture.
# Keep in sync with Icons in src/plugin.cpp.
BURNING_SKULL = ('vfx/meshes/missiles/firehead/firehead_lod0.model', 'vfx/meshes/missiles/firehead/firehead_alb.texture')
SYMBOL_MODELS = {19: BURNING_SKULL, 21: BURNING_SKULL, 22: BURNING_SKULL}

TEMPLATE = 'hd/vfx/particles/overlays/common/shrine_skill/vfx_shrine_skill_looping.particles'
MESH  = b'data/hd/vfx/meshes/lit_mesh/shrine_skill/shrine_skill.model'
ALB   = b'data/hd/vfx/meshes/lit_mesh/shrine_skill/shrine_skill_ALB.texture'
GLOW  = b'data/hd/vfx/textures/graphic/T_FX_Shrine_Skill_Glowmask_BlurB.texture'
GLOW_NEW = b'data/hd/vfx/textures/graphic/T_FX_Shrine_Ic_00_Glowmask_BlurB.texture'

def put(rel, src):
    dst = os.path.join(out, rel)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copyfile(src, dst)

def replace_string(blob, old, new):
    assert len(old) == len(new), (old, new)
    at = blob.find(bytes([len(old)]) + old)
    assert at >= 0 and blob.count(old) == 1, old
    return blob[:at + 1] + new + blob[at + 1 + len(old):]

template = open(os.path.join(data, TEMPLATE), 'rb').read()
put('hd/vfx/textures/graphic/t_fx_shrine_ic_00_glowmask_blurb.texture',
    os.path.join(data, 'hd/vfx/textures/mask/t_fx_radialgradient_mask_soft_02.texture'))
for code, (key, folder, stem, tex) in ICONS.items():
    tag = 'shrine_ic_%02d' % code
    blob = replace_string(template, MESH, ('data/hd/vfx/meshes/lit_mesh/%s/%s.model' % (tag, tag)).encode())
    blob = replace_string(blob, ALB, ('data/hd/vfx/meshes/lit_mesh/%s/%s_ALB.texture' % (tag, tag)).encode())
    blob = replace_string(blob, GLOW, GLOW_NEW)
    rel = 'hd/vfx/particles/overlays/common/shrine_icons/vfx_shrine_icon_%s.particles' % key
    os.makedirs(os.path.dirname(os.path.join(out, rel)), exist_ok=True)
    open(os.path.join(out, rel), 'wb').write(blob)
    item = os.path.join(data, 'hd/items', folder)
    model, albedo = SYMBOL_MODELS.get(code, ('items/%s/%s_lod0.model' % (folder, stem), 'items/%s/%s_alb.texture' % (folder, tex)))
    put('hd/vfx/meshes/lit_mesh/%s/%s_lod0.model' % (tag, tag), os.path.join(data, 'hd', model))
    put('hd/vfx/meshes/lit_mesh/%s/%s_alb.texture' % (tag, tag), os.path.join(data, 'hd', albedo))
    # Legacy graphics: the Skill Shrine animation, so the game finds a file.
    put('global/overlays/shrineicon%s.dcc' % key, os.path.join(data, 'global/overlays/shrineskill.dcc'))

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

def write_sprite(rel, header, image):
    dst = os.path.join(out, rel)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    open(dst, 'wb').write(header + image.tobytes())

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

def lowend(sheet, frames):
    """Half-size sheet in the lowend layout (34-pixel frames)."""
    small = Image.new('RGBA', (34 * frames, sheet.height // 2))
    for index in range(frames):
        frame, _ = sheet_frame(sheet, frames, index)
        small.paste(frame.resize((frame.width // 2, frame.height // 2), Image.LANCZOS), (index * 34, 0))
    return small

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

# Lowend sheets are half size. When the extracted data lacks one (default07),
# it is made from the full sheet.
lowend_header = read_sprite(os.path.join(ui, 'automap/default13/tiles.lowend.sprite'))[0]
for atlas in sorted({cell // 100 for cell, _ in icons}):
    header, sheet, frames = read_sprite(os.path.join(ui, 'automap/default%02d/tiles.sprite' % atlas))
    step = sheet.width // frames
    for cell, icon in icons:
        if cell // 100 == atlas:
            sheet.paste(icon, ((cell % 100) * step, 0))
    write_sprite('hd/global/ui/automap/default%02d/tiles.sprite' % atlas, header, sheet)
    small_path = os.path.join(ui, 'automap/default%02d/tiles.lowend.sprite' % atlas)
    if os.path.exists(small_path):
        small_header, small, small_frames = read_sprite(small_path)
        small_step = small.width // small_frames
        for cell, icon in icons:
            if cell // 100 == atlas:
                half = Image.new('RGBA', (small_step, small.height))
                half.paste(icon.resize((icon.width // 2, icon.height // 2), Image.LANCZOS), (0, 0))
                small.paste(half, ((cell % 100) * small_step, 0))
    else:
        small_header, small = lowend_header, lowend(sheet, frames)
    write_sprite('hd/global/ui/automap/default%02d/tiles.lowend.sprite' % atlas, small_header, small)

preview = Image.new('RGBA', (66 * len(icons), 128), (38, 40, 60, 255))
for index, (_, icon) in enumerate(icons):
    preview.alpha_composite(icon, (index * 66, 0))
preview.crop((0, 20, preview.width, 128)).save(os.path.join(os.path.dirname(out), '..', 'minimap-icons-preview.png'))
print('wrote', out)
