// Shrine Icons: overlay.txt rows and HD overlay files for the shrine symbols
// that the Shrine Icons plugin attaches to shrines. The plugin finds the rows by
// their Filename ("ShrineIcon<Name>"), so they can go anywhere in the table;
// they are appended so no existing overlay changes number.
// [name, shrine code, scale that brings the item model to the size of the
// booster symbols (about 1.2 units across, model sizes from model_lod_desc.bin);
// fire, exploding and poison use the burning skull of the fire skull missile]
const icons = [
  ['refill', 1, 1.1],
  ['health', 2, 1.1],
  ['mana', 3, 1.1],
  ['portal', 17, 0.8],
  ['gem', 18, 1.85, 'upright'],
  ['fire', 19, 1.0],
  ['monster', 20, 1.6],
  ['exploding', 21, 1.0],
  ['poison', 22, 1.0],
];

const overlayFile = 'global\\excel\\overlay.txt';
const overlays = D2RMM.readTsv(overlayFile);
const template = overlays.rows.find((row) => row.overlay === 'shrine_skill');
let nextId = overlays.rows.length;
icons.forEach(([name]) => {
  const overlay = `shrine_icon_${name}`;
  if (overlays.rows.some((row) => row.overlay === overlay)) return;
  const filename = `ShrineIcon${name[0].toUpperCase()}${name.slice(1)}`;
  overlays.rows.push({ ...template, overlay, '*ID': String(nextId++), Filename: filename });
});
D2RMM.writeTsv(overlayFile, overlays);

const size = Number(config.size) || 1;
const height = Number(config.height);
// Item models keep the pose they have on the ground. The gem lies flat, so it
// is turned a quarter turn about X to stand up (the whole effect turns with it).
const quarterX = { x: Math.SQRT1_2, y: 0.0, z: 0.0, w: Math.SQRT1_2 };
const identity = { x: 0.0, y: 0.0, z: 0.0, w: 1.0 };
icons.forEach(([name, code, fit, pose]) => {
  const tag = `shrine_ic_${String(code).padStart(2, '0')}`;
  const particles = `data/hd/vfx/particles/overlays/common/shrine_icons/vfx_shrine_icon_${name}.particles`;
  D2RMM.writeJson(`hd\\overlays\\common\\shrine_icon_${name}.json`, {
    dependencies: {
      particles: [{ path: particles }],
      models: [{ path: `data/hd/vfx/meshes/lit_mesh/${tag}/${tag}.model` }],
      skeletons: [],
      animations: [],
      textures: [{ path: 'data/hd/vfx/textures/gradient/T_FX_AlphaRemapCurve_blinkfix.texture' }],
      physics: [],
      json: [],
      variantdata: [],
      objecteffects: [],
      other: [],
    },
    type: 'OverlayDefinition',
    name: `shrine_icon_${name}`,
    entities: [
      {
        type: 'Entity',
        name: 'entity_root',
        id: 1907000000 + code,
        components: [
          {
            type: 'VfxDefinitionComponent',
            name: 'entity_root_VfxDefinition',
            filename: particles,
            hardKillOnDestroy: true,
          },
          {
            type: 'TransformDefinitionComponent',
            name: 'entity_root_TransformDefinition',
            position: { x: 0.0, y: Number.isFinite(height) ? height : 14.0, z: 0.0 },
            orientation: pose === 'upright' ? quarterX : identity,
            scale: { x: size * fit, y: size * fit, z: size * fit },
            inheritOnlyPosition: false,
          },
        ],
      },
    ],
  });
});

// Effects, item models and textures under their new names, plus the legacy
// graphics animation each new overlay row names.
D2RMM.copyFile('files', '', true);
