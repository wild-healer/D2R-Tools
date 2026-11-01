# Wild Healer's Charm Inventory 1.0.3: release texts

## Patch notes

```
**Wild Healer's Charm Inventory 1.0.3**

**New**

* Charms in the normal inventory no longer give their bonuses: only the charm inventory's do.
* The charm inventory holds one of each unique charm: a second copy is refused, except when swapped with the first.
```

## Patch notes 1.0.2

```
**Wild Healer's Charm Inventory 1.0.2**

**Fixed**

* Elastic's antivirus no longer flags the DLL: the panel's art is built into the code, and the DLL is a quarter of the size.
```

## Patch notes 1.0.0

```
**Wild Healer's Charm Inventory 1.0.0**

**New**

* A 10x4 charm inventory on Player Page Host's player page. Only charms go in, and they give their bonuses.
* A game-style left panel with plain stone and the charm grid.
* A charm button on the inventory's left edge, under Grail On!'s grail button.
* Open/close charms action in the game's Controls menu, unbound by default.
* charms console command: opens or closes the charm inventory.
* The panel keeps the vanilla position when a mod moves the left panel anchor.
```

## GitHub release

- Tag: `wild-healers-charm-inventory-v1.0.3`
- Title: `Wild Healer's Charm Inventory 1.0.3`
- Asset: `wild-healers-charm-inventory-1.0.3.zip`

Body:

```markdown
**Wild Healer's Charm Inventory** 1.0.3: a 10x4 charm inventory on Player Page Host's player page.

### Features
- 10x4 cells, the size of the inventory's grid. Only charms go in, one of each unique charm (a second copy can only be swapped with the first), and only these charms give their bonuses: charms in the normal inventory don't.
- A game-style left panel: "Charms" title, plain stone and the grid.
- Opens from the charm button on the inventory's left edge (under Grail On!'s grail button), from the "Open/close charms" action of the Controls menu (no key by default), or with the `charms` console command.
- Ctrl+click moves charms between the inventory and the open panel.
- Fixed at the vanilla left panel position, so mods that move the left panel anchor don't carry it off screen.

### Requirements
- D2RLoader 1.3.1 or later.
- Player Page Host (`aaa-player-page-host`).

### Install
Load `wild-healers-charm-inventory-1.0.3.zip` in D2RMM next to Player Page Host, or copy `wild-healers-charm-inventory.dll` to `<game>/d2rloader/plugins/`. Settings are in `d2rloader/config/wild-healers-charm-inventory.toml`, written on first launch.
```

## D2RLoader Hub

Name: `Wild Healer's Charm Inventory`

Summary:

> A 10x4 charm inventory in a game-style panel, on Player Page Host's player page, opened from a button beside the inventory or a key.

Description: the GitHub release body above.
