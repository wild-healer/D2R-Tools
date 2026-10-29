#!/usr/bin/env python3
"""Writes src/tables.inc: the grail categories and one entry per spawnable unique.

Usage: gen_tables.py <excel folder> <grail.html> <items.json> <unique_art.json> <item-names.json> <output .inc>

- excel: uniqueitems/weapons/armor/misc.txt (bases, sizes, ethereal).
- grail.html: the grail tracker's BASE_INFO and LOOT_LAYOUT, for the categories
  and the order of bases inside each one.
- items.json: data/hd/items/items.json, item code -> inventory sprite.
- unique_art.json: {"map": {"<name>|<code>": "items/..."}}, sprites of the
  uniques, read from the Item Dictionary mod's layouts.
- item-names.json: the game's (or mod's) item strings, for the names shown in
  game: a unique by its index, a base by its namestr. A trailing tier tag such
  as " (n)" is dropped.
Circlets are listed with Helms, after them.
"""
import csv, json, re, sys

excel, grail, items_json, art_json, names_json, out = sys.argv[1:7]

def rows(path):
    with open(path, newline='', encoding='utf-8', errors='replace') as f:
        return list(csv.DictReader(f, delimiter='\t'))

def c(text):
    return '"' + text.replace('\\', '\\\\').replace('"', '\\"') + '"'

def key(name):
    return re.sub(r'[^a-z0-9]', '', name.lower())

html = open(grail, encoding='utf-8').read()
start = html.index('const BASE_INFO = ') + len('const BASE_INFO = ')
base_info = json.loads(re.sub(r',(\s*[}\]])', r'\1', html[start:html.index('};', start) + 1]))
layout = re.findall(r"\{ group: '([^']+)', cats: \[([^\]]+)\] \}", html[html.index('const LOOT_LAYOUT'):])
tracker = [(group, cat) for group, cats in layout for cat in re.findall(r"'([^']+)'", cats)]
merged = {('Armor', 'Circlets'): ('Armor', 'Helms')}
categories = [c for c in tracker if c not in merged]
def category(group, cat):
    return categories.index(merged.get((group, cat), (group, cat)))
names = {}
for e in json.load(open(names_json, encoding='utf-8-sig')):
    names[e['Key']] = re.sub(r'\s*\((n|x|e)\)$', '', re.sub(r'\xffc.', '', e.get('enUS', ''))).strip()
by_code = {v['code']: v for v in base_info.values()}

folders = {0: 'weapon', 1: 'armor', 2: 'misc'}
bases = {}
for kind, name in ((0, 'weapons.txt'), (1, 'armor.txt'), (2, 'misc.txt')):
    for r in rows(f'{excel}/{name}'):
        if r.get('code'):
            bases[r['code']] = (kind, names.get(r.get('namestr') or r['code']) or r['name'].strip(), int(r['invwidth'] or 1), int(r['invheight'] or 1), r.get('nodurability') != '1')
assets = {}
for entry in json.load(open(items_json, encoding='utf-8')):
    for code, value in entry.items():
        assets.setdefault(code, value['asset'])
# Display name and sprite by (code, name without punctuation): the excel index
# drops apostrophes ("Iros Torch" for Iro's Torch).
art = {}
for k, sprite in json.load(open(art_json, encoding='utf-8'))['map'].items():
    name, code = k.rsplit('|', 1)
    art[(code, key(name))] = (name, sprite)

def sprite(code):
    return f'items/{folders[bases[code][0]]}/{assets[code]}' if code in assets else ''

# The excel index has typos ("Peasent Crown"): uniques without an exact match
# take the closest unclaimed dictionary name of the same base.
import difflib
uniques = [r for r in rows(f'{excel}/uniqueitems.txt') if r['*ID'] and r['disabled'] != '1' and r['spawnable'] == '1']
claimed = {(r['code'], key(r['index'])) for r in uniques if (r['code'], key(r['index'])) in art}
# The name shown in game matches the dictionary better than the excel index.
for r in uniques:
    shown = (r['code'], key(names.get(r['index'], '')))
    if (r['code'], key(r['index'])) not in art and shown in art and shown not in claimed:
        art[(r['code'], key(r['index']))] = art[shown]
        claimed.add(shown)
for r in uniques:
    k = (r['code'], key(r['index']))
    if k in art: continue
    free = [n for (code, n) in art if code == r['code'] and (code, n) not in claimed]
    best = difflib.get_close_matches(key(r['index']), free, n=1, cutoff=0.5) or (free if len(free) == 1 else [])
    if best:
        art[k] = art[(r['code'], best[0])]
        claimed.add((r['code'], best[0]))

by_name = {}
for (code, n), (_, sprite_) in list(art.items()):
    by_name.setdefault(n, sprite_)
base_sprites = {sprite(code) for code in bases}
def name_of(r):
    return names.get(r['index']) or r['index']
entries, missing, unnamed = [], [], []
for r in rows(f'{excel}/uniqueitems.txt'):
    code = r['code']
    if not r['*ID'] or r['disabled'] == '1' or r['spawnable'] != '1' or code not in bases:
        continue
    kind, base, w, h, durable = bases[code]
    info = by_code.get(code)
    if not info or (info['group'], info['cat']) not in tracker:
        missing.append(r['index'])
        continue
    cat = category(info['group'], info['cat'])
    sub = tracker.index((info['group'], info['cat']))
    # Weapons and armor with durability can be ethereal; everything else cannot.
    eth = kind < 2 and durable
    ghost = sprite(code)
    _, own = art.get((code, key(r['index'])), (None, ghost))
    # The dictionary sometimes pairs a name with the wrong base: its art by
    # name, unless that art is another base's.
    own = by_name.get(key(name_of(r)), own)
    if own in base_sprites and own != ghost: own = ghost
    if (code, key(r['index'])) not in art: unnamed.append(r['index'])
    name = names.get(r['index']) or r['index']
    entries.append((cat, (sub, info['order']), int(r['*ID']), kind, w, h, eth, name, base, code, ghost, own))
entries.sort(key=lambda e: (e[0], e[1], e[2]))

icons = []
for i, (group, cat) in enumerate(categories):
    first = next((e for e in entries if e[0] == i), None)
    icons.append((group, cat, first[10] if first else '', first[4] if first else 1, first[5] if first else 1))

with open(out, 'w', encoding='utf-8') as f:
    f.write('// Generated by tools/gen_tables.py from the game\'s excel files, items.json, the grail\n')
    f.write('// tracker\'s categories and the Item Dictionary mod\'s unique names and sprites.\n')
    f.write('// { group, name, icon sprite, icon width, icon height } in the grail tracker\'s order.\n')
    f.write('const CategoryEntry Categories[] {\n')
    for group, cat, icon, w, h in icons:
        f.write(f'\t{{ {c(group)}, {c(cat)}, {c(icon)}, {w}, {h} }},\n')
    f.write('};\n\n')
    f.write('// { category, unique row, kind (0 weapon, 1 armor, 2 other), width, height, can be ethereal,\n')
    f.write('//   name, base, code, base sprite, unique sprite }, by category then base order.\n')
    f.write('const UniqueEntry Uniques[] {\n')
    for e in entries:
        cat, _, row, kind, w, h, eth, name, base, code, ghost, own = e
        f.write(f'\t{{ {cat}, {row}, {kind}, {w}, {h}, {str(eth).lower()}, {c(name)}, {c(base)}, {c(code)}, {c(ghost)}, {c(own)} }},\n')
    f.write('};\n\n')
    f.write('// { code, width, height } of every weapon, armor and misc base.\n')
    f.write('const BaseSize Bases[] {\n' + '\n'.join(f'\t{{ {c(code)}, {b[2]}, {b[3]} }},' for code, b in bases.items()) + '\n};\n')
print(len(entries), 'uniques in', len(categories), 'categories; no category:', missing, '; no unique art:', unnamed)
