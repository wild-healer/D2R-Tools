# Inventory Slots

D2RLoader plugin: four dedicated slots next to the inventory, for the Horadric
Cube, the Tome of Town Portal, the Tome of Identify and keys. They sit outside
the inventory grid (by default right of the mana orb, below the grid), so mods
that change the inventory size keep working.

- Each slot only takes its own item. An empty slot shows its item in gray, on
  the game's own equipment slot background.
- Items in the slots are still in your inventory for the game: right-click opens
  the cube or uses the tome, keys open chests, scrolls and keys refill their
  stack from vendors and pickups.
- A new cube, tome or key (bought, picked up, a quest reward, Ctrl+clicked from
  the stash) goes straight into its slot when the slot is empty.
- Click the slotted cube while holding an item to put the item inside, or drop a
  Town Portal / Identify scroll on its tome.
- Nothing else ever lands in the slots.

- Install: get Inventory Slots from the [D2RLoader Hub](https://d2rloader.net/hub/plugins/invetory-slots) and add it through D2RMM like the other plugins.
- Config (`d2rloader/config/inventory-slots.toml`): `enabled`, the position of
  each slot (`cube_x`/`cube_y`, `tp_tome_x`/`tp_tome_y`, `id_tome_x`/`id_tome_y`,
  `keys_x`/`keys_y`, in inventory panel units where one cell is 98), `show_frames`
  (the slot backgrounds), `show_art` (the gray item in empty slots).
- Take the items out of the slots before removing the plugin: without it, the
  game has no room for them.
- Works in single player and in games you host, not in games you join.
- Log: `mods/D2RMM/d2rloader/logs/inventory-slots.log`.

How it works (Steam 3.3.93787 / 94995 RVAs):
- The slots are real inventory cells (page 0). When the data tables load, each
  class's inventory grid (inventory.txt rows 0-4, 12-14 and their "2" twins)
  gets two more rows (bank array D2R+0x2A9A580, rows at bank+0x1588, gridY at
  +0x11). The slots are cells of those rows: cube (0, H) 2x2, Town Portal (2, H)
  1x2, Identify (3, H) 1x2, keys (4, H) 1x1, H = the class's own height.
- The free-spot searches (D2R+0x38D8F0, 0x38DBB0, 0x38DCC0, called only by
  FindSpot D2R+0x3865B0) see the grid without those rows, except for a cube, tome
  or key whose slot is empty: that search returns the slot. FindSpot's item size
  call (D2R+0x3866CA -> 0x371850) goes through a relay in an int3 gap
  (D2R+0x380FC1) so the plugin knows the item; the size function itself is left
  alone (Item Quality Backgrounds checks it). The client (purchase destination)
  and the server search the same way, so they agree.
- Four InventoryGridWidgets in a child layout of the inventory panel show one
  slot each. They share the stock grid's vtable (D2R+0x1CF3800), whose entries
  are wrapped: Draw (vt3) hides the items a widget must not show, item and cell
  rects (vt16, vt31) map the slot cell to the widget, valid cell and cell under
  the mouse (vt18, vt19) answer the slot cell. The slot widgets copy the stock
  grid's unit and its right-click use flags (+0x5C0, read by the right-click use
  D2R+0x161F20 through vt33).
- Behind each slot: the paperdoll slot sprite of its size (the tomes use the 2x4
  weapon slot at half size) and the item's art in gray, hidden while the slot is
  filled.

Every function's first 16 bytes are checked at load; otherwise the plugin stays
inactive.

Build: `D2RL_SDK=<PluginSDK checkout> sh build.sh` (MinGW-w64, SDK pinned to 03a5af9).
