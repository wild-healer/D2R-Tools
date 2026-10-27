# Cube Fill

D2RLoader plugin that shows what is in the Horadric Cube on the cube in your inventory: the items' own pictures drawn small in their cube spots (default), or a small badge such as `7/12` (cells used out of 12) in a corner of its icon, and/or a mini map of the cube's 3x4 grid with each item's shape. The badge turns red when the cube is full and is hidden when it is empty.

## Install

Get Cube Fill from the [D2RLoader Hub](https://d2rloader.net/hub/plugins/cube-fill) and load it in D2RMM, or copy `d2rl-cube-fill.dll` to `<game>/d2rloader/plugins/`. Settings are written on first launch to `d2rloader/config/cube-fill.toml`:

| Key | Default | Meaning |
| --- | --- | --- |
| `enabled` | `true` | Turns the plugin off without removing it. |
| `display` | `"sprites"` | `"badge"`: the count. `"grid"`: a mini 3x4 map of the cube over its icon, taken cells filled and the cells of one item joined, so each item's shape shows. `"both"`: map and badge. `"sprites"`: the real item pictures drawn small in their cube spots over the icon. `"sprites-badge"`: pictures and badge. |
| `picture_background` | `50` | Darkness of the box behind the pictures, 25, 50 or 75 (%). |
| `show` | `"used"` | `"used"`: cells used out of 12 (`7/12`). `"free"`: free cells left (`5`). |
| `show_empty` | `false` | Also show the badge on an empty cube (`0/12`). |
| `show_full` | `true` | Show the badge on a full cube (red). |
| `corner` | `"bottom-right"` | `bottom-right`, `bottom-left`, `top-right` or `top-left`. |
| `font_size` | `30` | Text size, 12 to 60. |

## How it works

- The badges are a child layout of the stock player inventory panel (keyboard and mouse layout, HD), so they move and close with it. D2RLoader 1.3.1 cannot move widgets or change their text, so the layout has one badge group per spot the cube can sit on (27 in a 10x4 inventory) with one text per count, and the plugin shows the right one.
- The mini map is plain tiles: 12 cell tiles plus the joints between cells of one item (35 widgets per cube spot).
- `sprites` hooks the inventory grid draw (D2R+0x2C3570) and redraws the cube's items with the game's own item draw at a smaller scale. The hook checks the function bytes first and stays off on any other game build (Steam 3.3.93787 and 3.3.94995 share this code); the badge shows instead. It does not touch the functions Item Quality Backgrounds hooks.
- Widget handles are looked up the first time the cube sits on a spot and kept. The loader hands out about 250, so the plugin stops at 230: about 4 spots per game session with `both`, 5 with `grid`, 15 with `badge`. Past that, a cube moved to a new spot shows nothing until the game restarts (logged).
- Every 50 ms the plugin reads the inventory and the cube's items on the UI thread. Cells used = each item's size from its top-left cell.
- Sizes come from the game's item table when it can be read (single player or a TCP/IP game you host): the offset of invwidth/invheight in the compiled rows is found once from items of known size and logged. Otherwise the stock sizes built into the plugin (`src/sizes.inc`, from `tools/gen-sizes.py`) are used.

## Limits

- Inventory only: a cube in the stash shows nothing. Controller layout and the legacy (non-HD) graphics are not supported.
- Positions assume the stock inventory layout (grid at 93,897, 98-unit cells). A mod that moves the inventory grid moves the cube away from its badge.

## Checking it

`d2rloader/logs/cube-fill.log` (under `mods/<mod>/` when loaded as a mod plugin) says `active`, then `badges found in the inventory panel` and where item sizes come from.
