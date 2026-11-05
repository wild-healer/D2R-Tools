# Cube Fill

D2RLoader plugin that shows what is in the Horadric Cube on the cube in your inventory: the items' own pictures drawn small in their cube spots (default), or a small badge such as `7/12` (cells used out of 12) in a corner of its icon, and/or a mini map of the cube's 3x4 grid with each item's shape. The badge turns red when the cube is full and is hidden when it is empty.

## Install

Download it from the [D2RLoader Hub](https://d2rloader.net/hub/plugins/cube-fill). Load `cube-fill.zip` (the DLL alone; its default settings are built in) in D2RMM, or copy `d2rl-cube-fill.dll` to `<game>/d2rloader/plugins/`. Settings are written on first launch to `d2rloader/config/cube-fill.toml`, and can be changed in game with [I Hate TOMLs](../i-hate-tomls): `display`, `picture_background`, `show_empty` and `show_full` apply at once, `show`, `corner` and `font_size` after a restart.

| Key | Default | Meaning |
| --- | --- | --- |
| `enabled` | `true` | Turns the plugin off without removing it. |
| `display` | `"sprites"` | `"badge"`: the count. `"grid"`: a mini 3x4 map of the cube over its icon, taken cells filled and the cells of one item joined, so each item's shape shows. `"both"`: map and badge. `"sprites"`: the real item pictures drawn small in their cube spots over the icon. `"sprites-badge"`: pictures and badge. |
| `picture_background` | `50` | Darkness of the box behind the pictures, 25, 50 or 75 (%). |
| `show` | `"used"` | `"used"`: cells used out of 12 (`7/12`). `"free"`: free cells left (`5`). |
| `show_empty` | `false` | Also show the cube's contents when it is empty (`0/12`). |
| `show_full` | `true` | Show the cube's contents when it is full (the badge in red). |
| `corner` | `"bottom-right"` | `bottom-right`, `bottom-left`, `top-right` or `top-left`. |
| `font_size` | `30` | Text size, 12 to 60. |

## How it works

- The badges are a child layout of the stock player inventory panel (keyboard and mouse layout, HD), so they move and close with it. D2RLoader 1.3.1 cannot move widgets or change their text, so the layout has one badge group per spot the cube can sit on (27 in a 10x4 inventory) with one text per count, and the plugin shows the right one.
- The mini map is plain tiles: 12 cell tiles plus the joints between cells of one item (35 widgets per cube spot). Map and badge sit in a group each, so the display can change while playing.
- The pictures come from a hook on the inventory grid draw (D2R+0x2C3570), set up whatever the display so it can be switched while playing. It redraws the cube's items with the game's own item draw at a smaller scale. The hook checks the function bytes first and stays off on any other game build (Steam 3.3.93787 and 3.3.94995 share this code); the badge shows instead. It does not touch the functions Item Quality Backgrounds hooks.
- The pictures follow the cube's real size, like Inventory Fill follows the inventory's: the plugin reads the cube's gridX/gridY from inventory.txt (rows "Transmogrify Box Page 1" and "Transmogrify Box2", logged as `cube size: WxH from inventory.txt`); if that table can't be read (a game you joined), it looks for the cube's grid among the inventory grids (the one holding the cube's items). A bigger cube gets smaller cells so it still fits on the icon. The badge and the mini map are made for the stock 3x4 cube, so with another size the pictures show in their place (logged as `cube grid: WxH`).
- Widget handles are looked up the first time the cube sits on a spot and kept. The loader hands out about 250, so the plugin stops at 230: about 4 spots per game session with `both`, 5 with `grid`, 15 with `badge`. Past that, a cube moved to a new spot shows nothing until the game restarts (logged).
- Every 50 ms the plugin reads the inventory and the cube's items on the UI thread. Cells used = each item's size from its top-left cell.
- Sizes come from the game's item table when it can be read (single player or a TCP/IP game you host): the offset of invwidth/invheight in the compiled rows is found once from items of known size and logged. Otherwise the stock sizes built into the plugin (`src/sizes.inc`, from `tools/gen-sizes.py`) are used.

## Limits

- Inventory only: a cube in the stash shows nothing. Controller layout and the legacy (non-HD) graphics are not supported.
- Positions assume the stock inventory layout (grid at 93,897, 98-unit cells). A mod that moves the inventory grid moves the cube away from its badge.

## Checking it

`d2rloader/logs/cube-fill.log` (under `mods/<mod>/` when loaded as a mod plugin) says `active`, then `badges found in the inventory panel` and where item sizes come from.
