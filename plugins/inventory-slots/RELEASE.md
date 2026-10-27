# Inventory Slots 1.0.0: release texts

## Patch notes

```
**Inventory Slots 1.0.0**

**New**

* Four slots outside the inventory grid for the Horadric Cube, the two tomes and keys, each taking only its own item.
* An empty slot shows its item in gray.
* Items in the slots work as in the inventory: right-click, keys on chests and stack refills.
* A new cube, tome or key you buy, pick up or get as a reward goes straight into its empty slot.
* Click the slotted cube while holding an item to put the item inside.
* cube_x, cube_y, tp_tome_x, tp_tome_y, id_tome_x, id_tome_y, keys_x and keys_y settings: the position of each slot.
* show_frames setting: the game's equipment slot background behind each slot.
* show_art setting: the gray item in empty slots.
```

## GitHub release

- Tag: `inventory-slots-v1.0.0`
- Title: `Inventory Slots 1.0.0`
- Asset: `inventory-slots-1.0.0.zip`

Body:

```markdown
**Inventory Slots** 1.0.0: dedicated slots for the Horadric Cube, the two tomes and keys, outside the inventory grid.

### Features
- Four slots next to the inventory (by default right of the mana orb), outside the grid, so mods that change the inventory size keep working.
- Each slot takes only its own item; an empty slot shows its item in gray on the game's equipment slot background.
- Items in the slots work as in the inventory: right-click opens the cube or uses the tome, keys open chests, vendors and pickups refill the stacks.
- A new cube, tome or key you buy, pick up or get as a reward goes straight into its empty slot. Nothing else ever lands there.
- Click the slotted cube while holding an item to put it inside.
- Slot positions are settings.

### Requirements
- D2RLoader 1.3.1 or later.
- Single player, or a TCP/IP game you host.
- Steam 3.3.93787 or 3.3.94995. The plugin checks the game code when it loads; on another build it stays inactive and says so in its log.

### Install
Load `inventory-slots-1.0.0.zip` in D2RMM, or copy `d2rl-inventory-slots.dll` to `<game>/d2rloader/plugins/`. Settings are in `d2rloader/config/inventory-slots.toml`, written on first launch.

Take the items out of the slots before removing the plugin: without it, the game has no room for them.
```

## D2RLoader Hub

Name: `Inventory Slots`

Summary:

> Dedicated slots for the Horadric Cube, the Town Portal and Identify tomes and keys, outside the inventory grid.

Description: the GitHub release body above.
