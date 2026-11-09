# Merc Gear Models (1.0.0)

D2RLoader plugin: your mercenary wears what you give it. Its helm and weapon
are drawn with the real HD models your character uses (base items, uniques and
set items), its body follows its armor, and weapon colors such as Enchant's
red show on its weapon. It only changes what you see.

| Merc | Shows |
| --- | --- |
| Rogue | helm, bow, armor (light / medium pieces; heavy armor: the Amazon's whole heavy armor) |
| Desert merc | helm, spear or polearm, armor (light / medium body; heavy armor: the Barbarian's armor pieces over it) |
| Iron Wolf | helm (his own cap is hidden under it), sword, shield, armor (heavy armor: the Barbarian's armor pieces) |
| Barbarian | helm, both weapons, armor (light / medium / heavy; heavy armor: the Barbarian's whole heavy armor) |

## Settings

`d2rloader/config/merc-gear-models.toml`, or in game with I Hate TOMLs (both
apply at once):

- `enabled`: false gives every merc its normal look back.
- Per merc (`rogue`, `desert`, `iron_wolf`, `barbarian`), four switches, all
  on by default:
  - `<merc>.weapon`: weapon and shield models.
  - `<merc>.helm`: helm models.
  - `<merc>.armor`: the body armor look.
  - `<merc>.enchant`: item colors on the weapon, such as Enchant's red. Needs
    weapon on (I Hate TOMLs greys it out while weapon is off).

## Requirements

- D2RLoader 1.3.1 or later.
- Steam 3.3.93787 or 3.3.94995. The plugin checks the game code it uses when it
  loads; on another build it stays inactive and says so in its log.

## How it works

- Hook: GetComponentCode D2R+0x195200 picks each merc slot's look from its gear.
- The plugin serves its own copies of the four merc definitions: at load it
  rebuilds the game's own `<merc>.json` (kept as tables, not as a file) and adds one entity per helm and weapon the
  merc can use, attaching that item's HD model, and the class heavy armor
  bodies (`src/merc_build.h`, from the tables `tools/gen_defs.py` writes into
  `src/merc_adds.inc` from the game's HD data).
- Borrowed class armor is re-posed and its bones mapped onto the merc skeleton
  at load; meshes the merc shouldn't wear are collapsed in the skinning pass.
- Item colors: applied to the merc's item models after the game's unit visual
  update (D2R+0x754DB0).
- The plugin log (`d2rloader/logs/merc-gear-models.log`) gets one line per look change.
- Build: `sh build.sh` (MinGW-w64, D2RLoader PluginSDK 03a5af9).
