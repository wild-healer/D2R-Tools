# Shrine Icons

Tells shrines apart before you walk up to them. One D2RLoader plugin, no mod needed, on the [D2RLoader Hub](https://d2rloader.net/hub/plugins/shrine-icons): `shrine-icons.zip` holds the DLL, with its config and minimap art built in.

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
  exploding and poison apart), and the HD floating symbol for the booster
  shrines (armor, combat, the four resists, skill, mana recharge, stamina,
  experience), or their classic symbol with `classic_boosters`.

## How

- The plugin points the empty entries of the game's per-shrine-type symbol table
  (0x1CDA280) at a function of its own that does what the booster shrine
  function does, as the settings say. A setting change runs it again on every
  shrine the client knows, so it shows at once.
- The symbols are overlay rows the plugin appends to the game's overlay table
  (copies of shrine_skill). The HD client asks 0x662A90 for an overlay's file (its two
  calls, 0x1255F8 and 0x7629DA, go through the plugin);
  the plugin answers its rows with OverlayDefinitions of its own, served as
  D2RLoader resources under `.../d2rloader/shrine-icons/`. They float the Skill
  Shrine effect with its mesh swapped for an item model; the effect, meshes and
  textures are the game's own files, read at startup and served under new
  names.
- The renderer culls a model missing from `data/hd/model_lod_desc.bin`, so the
  plugin hooks its lookup (0xF75AF0) and answers for the copies with the bounds
  of the models they copy.
- The automap tileset loader's sprite load (call at 0xD4724) goes through the
  plugin, which answers `default13` and `default07` with copies of the game's
  sheets that have the minimap icons painted into cells no data file uses
  (default13 1332-1389, default07 700-716). The plugin hooks the automap's
  add-cell function (0xD52B0) and hands out those cells instead of 310; the
  number of automap tiles stays the same, so saved maps stay valid. The game fits
  each cell's visible art to the cell width, so the pictures fill the cell.
- The automap draw reads each tile's cell at 0xD717E; a jump there to a small
  stub maps it through a table that shows a shrine's own cell or the gold ring,
  so minimap setting changes apply to shrines already on the map too.
- Every native address is checked byte for byte before anything is written; a
  mismatch leaves that part of the game untouched and says so in the log.

## Settings

`d2rloader/config/shrine-icons.toml`, or in game with I Hate TOMLs, where every
setting applies at once: `symbols`, `minimap`, `boosters`, `classic_boosters`,
`shimmer`, `size`,
`height`, one switch per shrine type, `enabled`.

## Build

`sh build.sh` (MinGW-w64) builds the DLL and `shrine-icons.zip`. The minimap art
is `src/minimap_cells.bin`, built into the DLL;
`python3 tools_make_minimap.py <extracted game data folder>` regenerates it
(the HD booster icons are rendered from the HD symbol models by
`tools_render.py`, which reads the Granny `.model` files with `tools_gr2.py`; the
classic symbols are decoded by `tools_dcc.py`).

The gem symbol's mesh is `src/gem_turned_lod0.model`, also built into the DLL:
the perfect amethyst is modelled lying down and the symbol effect ignores the
overlay's orientation, so `python3 tools_turn_model.py <game>/data/hd/items/misc/gem/perfect_amethyst/perfect_amethyst_lod0.model src/gem_turned_lod0.model`
turns the mesh itself a quarter turn (tip down, top to the camera).
