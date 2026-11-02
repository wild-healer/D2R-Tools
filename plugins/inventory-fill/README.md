# Inventory Fill

D2RLoader plugin that shows your inventory on the skill bar while the inventory panel is closed. A small grid sits over the demon that holds the mana orb, so you see at a glance how much room is left.

- **Item pictures**: each item is drawn small, with its own picture, in its inventory spot. Free cells are dark tiles. A "cells" display shows taken cells as plain filled tiles instead.
- **Any inventory size**: the grid follows your inventory's real size, so mods that resize the inventory just work. The extra rows that Inventory Slots adds are left out, since they aren't inventory room.
- **Out of the way**: it hides as soon as the inventory opens, and the game's own panels draw over it.
- **I Hate TOMLs**: every setting can be changed in game, and the grid updates at once.

Draws through the game's own skill bar and item drawing code. It checks that code when it loads and stays inactive on other builds. Tested on Steam 3.3.94995 (3.3.93787 has the same code). Requires D2RLoader 1.3.1 or later.

## Install

Download it from the [D2RLoader Hub](https://d2rloader.net/hub/plugins/inventory-fill). Load `inventory-fill.zip` (the DLL alone; its default settings are built in) in D2RMM, or copy `d2rl-inventory-fill.dll` to `<game>/d2rloader/plugins/`. Remove the DLL to uninstall.

## Settings

`d2rloader/config/inventory-fill.toml`, written on first launch, or the plugin's page in I Hate TOMLs:

| Key | Default | Meaning |
| --- | --- | --- |
| `enabled` | `true` | Shows or hides the grid. |
| `display` | `"sprites"` | `"sprites"`: item pictures. `"cells"`: taken cells filled, without pictures. |
| `background` | `50` | Darkness of the box behind the grid: 25, 50 or 75 (percent). |
| `scale` | `1.00` | Size of the grid: 1 = 22 skill bar units per cell. |
| `position` | `[2710, 290]` | Center of the grid, in skill bar units (the bar is 2952 wide; the demon's body is around the default). |
