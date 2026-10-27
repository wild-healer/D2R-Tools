# Shrine Icons

Tells shrines apart before you walk up to them. Two parts, both needed, both on the [D2RLoader Hub](https://d2rloader.net/hub/plugins/shrine-icons):

- The D2RLoader plugin (DLL + `shrine-icons.toml`).
- The "Shrine Icons" D2RMM mod with the art.

## What it does

- **Floating symbols** over the shrines the game leaves bare, like booster
  shrines already have: refilling (rejuvenation potion), portal (town portal
  scroll), gem (amethyst), monster (skull), and a burning skull for the
  dangerous fire, exploding and poison shrines. Health and mana shrines get none:
  their coloured pool already shows them. The symbol goes away when the shrine is
  used and comes back when it recharges, the game handles it like a booster
  symbol.
- **A minimap icon per shrine type** instead of the shared gold ring: the item
  picture for the shrines above (torch, exploding potion and gas potion keep fire,
  exploding and poison apart), and the classic floating symbol for the booster
  shrines (armor, combat, the four resists, skill, mana recharge, stamina,
  experience).

## How

- The plugin fills the empty entries of the game's per-shrine-type symbol table
  (0x1CDA280) with the booster shrine function and the mod's overlay rows, found
  by their Filename once the data tables load.
- The symbols are the Skill Shrine effect with its mesh swapped for an item
  model. The renderer culls a model missing from `data/hd/model_lod_desc.bin`,
  so the plugin hooks its lookup (0xF75AF0) and answers for the copies with the
  bounds of the models they copy.
- The plugin hooks the automap's add-cell function (0xD52B0) and hands out a
  cell per shrine type instead of 310. The mod paints those cells into automap
  cells no data file uses (default13 1332-1389, default07 700-716); the number of
  automap tiles stays the same, so saved maps stay valid. The game fits each
  cell's visible art to the cell width, so the pictures fill the cell.
- Every native address is checked byte for byte before anything is written; a
  mismatch leaves the game untouched and says so in the log.

## Settings

Plugin (`d2rloader/config/shrine-icons.toml`): `symbols`, `minimap`, `boosters`,
`shimmer`, one switch per shrine type, `enabled`. Mod (D2RMM): symbol size and
height.

## Build

`sh build.sh` (MinGW-w64) builds the DLL and both zips. `mod/files` is not in
the repo: `python3 tools_make_mod_files.py <extracted game data folder>`
regenerates it (item models and textures, effects, automap sheets; the classic
symbols are decoded by `tools_dcc.py`).
