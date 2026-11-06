# Inventory Slots 1.0.4: release texts

## Patch notes

```
**Inventory Slots 1.0.4**

**Fixed**

* I Hate TOMLs now finds the plugin's Hub page for its update check.
```

## GitHub release

- Tag: `inventory-slots-v1.0.4`
- Title: `Inventory Slots 1.0.4`
- Asset: `inventory-slots.zip`

Body:

```markdown
**Inventory Slots** 1.0.4: dedicated slots for the Horadric Cube, the two tomes and keys, outside the inventory grid.

### Features
- Four slots next to the inventory (by default right of the mana orb), outside the grid, so mods that change the inventory size keep working.
- Each slot takes only its own item; an empty slot shows its item in gray on the game's equipment slot background.
- Items in the slots work as in the inventory: right-click opens the cube or uses the tome, keys open chests, vendors and pickups refill the stacks.
- A new cube, tome or key you buy, pick up or get as a reward goes straight into its empty slot. Nothing else ever lands there, and Sort Inventory leaves the slots alone.
- **I Hate TOMLs support.** Change every setting in game, slot positions included; changes apply at once. Its update check finds the plugin on the Hub.

### Requirements
- D2RLoader 1.3.1 or later.
- Single player, or a TCP/IP game you host.
- Steam 3.3.93787 or 3.3.94995. The plugin checks the game code when it loads; on another build it stays inactive and says so in its log.

### Install
Load `inventory-slots.zip` in D2RMM, or copy `d2rl-inventory-slots.dll` to `<game>/d2rloader/plugins/`. Settings are in `d2rloader/config/inventory-slots.toml`, written on first launch, or in I Hate TOMLs. Existing settings are kept.

Take the items out of the slots before removing the plugin: without it, the game has no room for them.
```

## D2RLoader Hub

Name: `Inventory Slots`

Summary:

> Dedicated slots for the Horadric Cube, the Town Portal and Identify tomes and keys, outside the inventory grid.

Description: the GitHub release body above.
