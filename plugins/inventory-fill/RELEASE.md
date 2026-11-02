# Inventory Fill 1.0.0: release texts

## Patch notes

```
**Inventory Fill 1.0.0**

**New**

* Your inventory is shown as a small grid on the skill bar, over the demon that holds the mana orb, while the inventory is closed.
* Items are drawn with their own pictures in their spots, and free cells as dark tiles.
* The grid follows your inventory's size, and leaves out the rows Inventory Slots adds.
* display setting: item pictures, or plain filled cells.
* background setting: darkness of the box behind the grid, 25, 50 or 75 percent.
* scale setting: size of the grid.
* position setting: center of the grid on the skill bar.
* Every setting can be changed in game with I Hate TOMLs and applies at once.
```

## GitHub release

- Tag: `inventory-fill-v1.0.0`
- Title: `Inventory Fill 1.0.0`
- Asset: `inventory-fill.zip`

Body:

```markdown
**Inventory Fill** 1.0.0: your inventory on the skill bar, over the mana demon, while the inventory is closed.

### Features
- A small grid of your inventory: items with their own pictures in their spots, free cells as dark tiles, so you see the room left at a glance.
- Follows your inventory's real size (inventory mods just work); the rows Inventory Slots adds are left out.
- Hides as soon as the inventory opens.
- Settings: display (pictures or cells), background darkness, scale, position.
- I Hate TOMLs support: change any setting in game and see it at once.

### Requirements
- D2RLoader 1.3.1 or later.
- Steam 3.3.93787 or 3.3.94995. The plugin checks the game code when it loads; on another build it stays inactive and says so in its log.

### Install
Load `inventory-fill.zip` in D2RMM, or copy `d2rl-inventory-fill.dll` to `<game>/d2rloader/plugins/`. Settings are in `d2rloader/config/inventory-fill.toml`, written on first launch, or in I Hate TOMLs.
```

## D2RLoader Hub

Name: `Inventory Fill`

Summary:

> Your inventory as a small grid on the skill bar, over the mana demon, while the inventory is closed.

Description: the GitHub release body above.
