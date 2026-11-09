# Builds the four merc HD unit definitions with one extra entity per item the
# merc can show (base items, uniques, sets), plus the table of variant codes the
# plugin may return. Writes the full definitions to <out> (for checking) and,
# with a fifth argument, src/merc_adds.inc: only what the plugin adds to each
# game definition, which it reads from the game at load (src/merc_build.h).
# Inputs: a folder with the game's data/hd files (character/enemy/*.json,
# items/items.json, uniques.json, sets.json, a list of item json paths) and
# the excel folder (weapons.txt, armor.txt, uniqueitems.txt, setitems.txt).
import csv, json, re, sys, copy, zlib

hd, excel, itemlist, out = sys.argv[1:5]
adds_out = sys.argv[5] if len(sys.argv) > 5 else None
adds = []  # C++ lines of the compact tables

def rows(name):
    return list(csv.DictReader(open(f'{excel}/{name}', encoding='latin-1'), delimiter='\t'))

existing = set(l.strip() for l in open(itemlist) if l.strip())   # "weapon/axe/axe.json"
def asset_path(asset):
    for root in ('weapon', 'armor'):
        p = f'{root}/{asset}.json'
        if p in existing:
            return 'data/hd/items/' + p
    return None

assets = {}
for entry in json.load(open(f'{hd}/items/items.json')):
    for code, v in entry.items():
        assets[code] = v['asset']
def keyed(path):
    out = {}
    for entry in json.load(open(path)):
        for k, v in entry.items():
            out[k] = v['normal']
    return out
uniq_assets = keyed(f'{hd}/items/uniques.json')
set_assets = keyed(f'{hd}/items/sets.json')

items = {}  # code -> type
for f in ('weapons.txt', 'armor.txt'):
    for r in rows(f):
        if r.get('code'):
            items[r['code']] = r['type']

def norm(name):
    return re.sub(r'[^a-z0-9_]', '', name.lower().replace(' ', '_').replace('-', '_'))

specials = []  # (prefix, id, base code, asset path)
for prefix, table, amap in (('u', 'uniqueitems.txt', uniq_assets), ('s', 'setitems.txt', set_assets)):
    for r in rows(table):
        if not r.get('*ID') or not r.get('code'): continue
        a = amap.get(norm(r['index']))
        p = asset_path(a) if a else None
        if p: specials.append((prefix, int(r['*ID']), r['code'], p))

HELMS = {'helm', 'circ'}
MERCS = {
    # name: classes, (slot, part, item types, bone, orient), head model when no helm, helm bone
    # Helm bones differ per skeleton: "mgl_helm" is resolved by the plugin's bone lookup hook to the first head bone found.
    'roguehire': ([271], [(6, 'lhand', {'bow'}, 'L_weapon_attach', '')], 'data/hd/character/enemy/roguehire/head_lit.model', 'mgl_helm'),
    'act2hire': ([338], [(5, 'rhand', {'spea', 'pole'}, '', 'L_weapon_attach')], 'data/hd/character/npc/act2guard1/head_lit.model', 'mgl_helm'),
    'act3hire': ([359], [(5, 'rhand', {'swor'}, 'R_weapon_attach', ''), (7, 'shield', {'shie', 'ashd', 'head'}, 'L_Weapon_Attach', '')], '', 'mgl_helm'),
    'act5hire1': ([560, 561], [(5, 'rhand', {'swor', 'axe'}, '', ''), (6, 'lhand', {'swor', 'axe'}, '', '')], '', 'helmet'),
}

DONORS = [('amazon', 'Ham'), ('assassin', 'Has'), ('barbarian', 'Hba'), ('druid', 'Hdr'), ('necromancer', 'Hne'),
          ('paladin', 'Hpa'), ('sorceress', 'Hso'), ('warlock', 'Hwa')]
HEAVY_PARTS = [('torso', 1), ('legs', 2), ('rarm', 3), ('larm', 4)]

def entity(name, eid, part, variant, model, attach, bone, orient, y=0.0):
    comps = [{"type": "UnitPartComponent", "name": f"component_part_{name}", "part": part, "variant": variant},
             {"type": "ModelDefinitionComponent", "name": f"component_model_{name}", "filename": model, "visibleLayers": 1,
              "lightMask": 19, "shadowMask": 3, "ghostShadows": False, "floorModel": False,
              "terrainBlendEnableYUpBlend": False, "terrainBlendMode": 1}]
    if attach:
        comps.append({"type": "UnitAttachmentComponent", "name": f"entity_{name}_UnitAttachment", "filename": attach,
                      "overrideBoneName": bone, "orientTowardBoneName": orient,
                      "attachmentTransform": {"type": "Transform", "name": f"entity_{name}_UnitAttachment_attachmentTransform",
                                              "translation": {"x": 0.0, "y": y, "z": 0.0}, "rotation": {"x": 0.0, "y": 0.0, "z": 0.0},
                                              "scale": {"x": 1.0, "y": 1.0, "z": 1.0}},
                      "attachmentStateMachineFilename": "", "part": part, "variant": variant})
    return {"type": "Entity", "name": f"entity_{name}", "id": eid, "components": comps}

def choices(types):
    out = []  # (variant, asset path)
    for code, t in sorted(items.items()):
        if t in types and code in assets:
            p = asset_path(assets[code])
            if p: out.append((code, p))
    for prefix, i, base, p in specials:
        if items.get(base) in types:
            # Variants are matched on 3 characters: '#'/'$' + base 36 id never collides with item codes.
            digits = '0123456789abcdefghijklmnopqrstuvwxyz'
            out.append((('#' if prefix == 'u' else '$') + digits[i // 36] + digits[i % 36], p))
    return out

table = []  # (class, slot, variant)
lines = ['// Generated by tools/gen_defs.py. Do not edit.', '']
for merc, (classes, weapon_slots, head_model, helm_bone) in MERCS.items():
    d = json.load(open(f'{hd}/character/enemy/{merc}.json'))
    ents = d['entities']
    base_count = len(ents)
    base_deps = {k: len(d.get('dependencies', {}).get(k, [])) for k in ('models', 'textures')}
    used = {e['id'] for e in ents}
    def new_id(seed):
        i = zlib.crc32(seed.encode())
        while i in used: i = (i + 1) & 0xffffffff
        used.add(i); return i
    # Iron Wolf weapon entity has no part: give it one so its long sword can be swapped.
    for e in ents:
        if merc == 'act3hire' and e['name'] == 'entity_rhand':
            e['components'].insert(0, {"type": "UnitPartComponent", "name": "component_part_rhand_lsd", "part": "rhand", "variant": "lsd"})
        if merc == 'act3hire' and e['name'] == 'entity_shield':
            e['components'].insert(0, {"type": "UnitPartComponent", "name": "component_part_shield_buc", "part": "shield", "variant": "buc"})
    for slot, part, types, bone, orient in weapon_slots:
        if slot in (6, 7) and merc != 'roguehire':
            # nothing in that hand: an empty variant, so the merc's default left weapon or shield isn't drawn
            ents.append(entity(f'{part}_nil_mgl', new_id(merc + part + 'nil'), part, 'nil', '', None, '', ''))
            for c in classes: table.append((c, slot, 'nil'))
        for variant, path in choices(types):
            name = f'{part}_{variant}_mgl'
            # Act 2 polearms/spears: grip offset placeholder, set from the config at load.
            y = 7777.0 if merc == 'act2hire' else 0.0
            ents.append(entity(name, new_id(merc + name), part, variant, '', path, bone, orient, y))
            for c in classes: table.append((c, slot, variant))
    # Heavy armor looks borrowed from the player classes (variant 'H' + 2 letters); bones bind by name,
    # the plugin maps donor bones the merc skeleton lacks. Picked per merc in the settings.
    deps = d.setdefault('dependencies', {})
    have_models = {x['path'] for x in deps.setdefault('models', [])}
    have_tex = {x['path'] for x in deps.setdefault('textures', [])}
    # The merc's own medium (else light) armor parts: shown under the borrowed class pieces (same part/variant).
    med = {}
    for e in ents:
        pc = next((c for c in e['components'] if c.get('type') == 'UnitPartComponent'), None)
        mc = next((c for c in e['components'] if c.get('type') == 'ModelDefinitionComponent'), None)
        if pc and mc and pc.get('variant') == 'med' and mc.get('filename'): med[pc['part']] = mc['filename']
    for e in ents:  # no medium part: the merc's own light one
        pc = next((c for c in e['components'] if c.get('type') == 'UnitPartComponent'), None)
        mc = next((c for c in e['components'] if c.get('type') == 'ModelDefinitionComponent'), None)
        if pc and mc and pc.get('variant') == 'lit' and mc.get('filename') and pc['part'] not in med: med[pc['part']] = mc['filename']
    for donor, code in DONORS:
        for part, slot in HEAVY_PARTS:
            if part in med and merc != 'act5hire1':  # he wears the class's whole armor (the Rogue keeps her body: her face is in it)
                ents.append(entity(f'{part}_{code}_med_mgl', new_id(merc + part + code + 'med'), part, code, med[part], None, '', ''))
            m = f"data/hd/character/player/{donor}/{part}_hvy.model"
            name = f'{part}_{code}_mgl'
            ents.append(entity(name, new_id(merc + name), part, code, m, None, '', ''))
            for c in classes: table.append((c, slot, code))
            if m not in have_models: deps['models'].append({'path': m}); have_models.add(m)
        for x in json.load(open(f"{hd}/character/player/{donor}.json"))['dependencies'].get('textures', []):
            low = x['path'].lower()
            if ('armorhvy' in low or 'body' in low or 'ktint' in low) and x['path'] not in have_tex:
                deps['textures'].append(x); have_tex.add(x['path'])
    # helms on the bare head: no head mesh (hood/hair) under the helm, like the act 5 merc's own helms
    have_lit_head = any(c.get('type') == 'UnitPartComponent' and c.get('part') == 'head' and c.get('variant') == 'lit'
                        for e in ents for c in e['components'])
    if not have_lit_head:
        ents.append(entity('head_lit_mgl', new_id(merc + 'headlit'), 'head', 'lit', head_model, None, '', ''))
    for c in classes: table.append((c, 0, 'lit'))
    for variant, path in choices(HELMS):
        name = f'head_{variant}_mgl'
        ents.append(entity(name, new_id(merc + name), 'head', variant, '', path, helm_bone, ''))
        for c in classes: table.append((c, 0, variant))
    mi = list(MERCS).index(merc)
    def q(v): return json.dumps(v)
    for e in ents[base_count:]:
        pc = next(c for c in e['components'] if c['type'] == 'UnitPartComponent')
        mc = next(c for c in e['components'] if c['type'] == 'ModelDefinitionComponent')
        ac = next((c for c in e['components'] if c['type'] == 'UnitAttachmentComponent'), None)
        name = e['name'][len('entity_'):]
        grip = 'true' if ac and ac['attachmentTransform']['translation']['y'] == 7777.0 else 'false'
        adds.append(('ent', mi, e['id'], name, pc['part'], pc['variant'], mc['filename'], ac['filename'] if ac else '',
                     ac['overrideBoneName'] if ac else '', ac['orientTowardBoneName'] if ac else '', grip))
    for kind in ('models', 'textures'):
        for x in d['dependencies'][kind][base_deps[kind]:]:
            adds.append(('dep', mi, kind, x['path']))
    text = json.dumps(d, separators=(',', ':'))
    lines.append(f'constexpr char Def_{merc}[] = R"MGLJSON({text})MGLJSON";')
lines.append('')
lines.append('struct MercVariant { std::uint32_t classId; std::uint8_t slot; char variant[5]; };')
lines.append('constexpr MercVariant Variants[] = {')
for c, s, v in sorted(set(table)):
    lines.append(f'\t{{ {c}, {s}, "{v}" }},')
lines.append('};')
lines.append('struct MercDef { const char* name; const char* json; std::size_t size; };')
lines.append('constexpr MercDef Defs[] = {')
for merc in MERCS:
    lines.append(f'\t{{ "{merc}", Def_{merc}, sizeof(Def_{merc}) - 1 }},')
lines.append('};')
open(out, 'w').write('\n'.join(lines) + '\n')
if adds_out:
    # All text lives in one pool of NUL-ended strings; the tables hold offsets into it (no pointers to relocate).
    def cstr(t):
        return '"' + ''.join(c if 32 <= ord(c) < 127 and c not in '\\"?' else ('\\' + c if c in '\\"' else '\\?' if c == '?' else f'\\x{ord(c):02x}""') for c in t) + '"'
    pool, at_of, size = [], {}, [0]
    def off(t):
        if t not in at_of:
            at_of[t] = size[0]; pool.append(t); size[0] += len(t.encode('latin-1')) + 1
        return at_of[t]
    ents_l = [f'\t{{ {a[1]}, {a[2]}u, {", ".join(str(off(x)) for x in a[3:10])}, {a[10]} }},' for a in adds if a[0] == 'ent']
    deps_l = [f'\t{{ {a[1]}, {"true" if a[2] == "textures" else "false"}, {off(a[3])} }},' for a in adds if a[0] == 'dep']
    # The game's own <merc>.json, kept exactly: its strings and what lies between them.
    pieces = []
    for mi, merc in enumerate(MERCS):
        text = open(f'{hd}/character/enemy/{merc}.json', encoding='latin-1').read()
        at = 0
        for m in re.finditer(r'"((?:\\.|[^"\\])*)"', text):
            pieces.append(f'\t{{ {off(text[at:m.start()])}, {off(m.group(1))} }},')
            at = m.end()
        pieces.append(f'\t{{ {off(text[at:])}, NoText }},')
    al = ['// Generated by tools/gen_defs.py. Do not edit.', '// What the plugin adds to each merc definition, and the game definition it extends.', '']
    al.append('constexpr const char* MercNames[] = { ' + ', '.join(json.dumps(m) for m in MERCS) + ' };')
    al.append('struct MercVariant { std::uint16_t classId; std::uint8_t slot; char variant[5]; };')
    al.append('constexpr MercVariant Variants[] = {')
    for c, sl, v in sorted(set(table)): al.append(f'\t{{ {c}, {sl}, "{v}" }},')
    al.append('};')
    al.append('using TextAt = std::uint16_t;')
    al.append('constexpr TextAt NoText = 0xFFFF;')
    al.append('struct AddedEntity { std::uint8_t merc; std::uint32_t id; TextAt name, part, variant, model, attach, bone, orient; bool grip; };')
    al.append('constexpr AddedEntity AddedEntities[] = {'); al += ents_l; al.append('};')
    al.append('struct AddedDependency { std::uint8_t merc; bool texture; TextAt path; };')
    al.append('constexpr AddedDependency AddedDependencies[] = {'); al += deps_l; al.append('};')
    al.append('struct BasePiece { TextAt gap, string; }; // each merc in turn, its last piece without a string')
    al.append('constexpr BasePiece BasePieces[] = {'); al += pieces; al.append('};')
    al.append('constexpr char TextPool[] =')
    al += [f'\t{cstr(x)} "\\0"' for x in pool]
    al.append(';')
    assert size[0] < 0xFFFF, 'text pool too big for 16-bit offsets'
    open(adds_out, 'w').write('\n'.join(al) + '\n')
print('variants', len(set(table)), 'specials', len(specials))
