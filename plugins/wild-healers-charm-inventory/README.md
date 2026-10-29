# Wild Healer's Charm Inventory

D2RLoader plugin that gives you a 10x4 charm inventory, the size of the inventory's grid, on the custom player page that [Player Page Host](../aaa-player-page-host) shares between plugins.

- **Charms only**: small, large and grand charms go in, anything else is refused. The charms in it give their bonuses like charms in the inventory.
- **Panel**: a game-style left panel with a "Charms" title, plain stone inside and the 10x4 grid of cells.
- **Opening it**: the charm button on the inventory's left edge, under Grail On!'s grail button; the "Open/close charms" action of the game's Controls menu (no key by default); or the `charms` console command.
- **Fixed position**: the panel sits where the game's left panels sit, written out rather than taken from `$LeftPanelAnchor`, so mods that move that anchor don't carry it off screen.
- Ctrl+click moves charms between the inventory and the open charm panel (the host does it).

Needs Player Page Host (`aaa-player-page-host`). The charms stay on the player page if the plugin is removed; `pagehost all` (a Player Page Host console command) shows the whole page. Requires D2RLoader 1.3.1 or later.

## Install

Download it from the [D2RLoader Hub](https://d2rloader.net/hub/plugins/wild-healers-charm-inventory). Load `wild-healers-charm-inventory-<version>.zip` (the DLL alone; its default settings are built in) in D2RMM next to Player Page Host, or copy `wild-healers-charm-inventory.dll` to `<game>/d2rloader/plugins/`. Remove the DLL to uninstall.

## Settings

`d2rloader/config/wild-healers-charm-inventory.toml`, written on first launch:

| Key | Default | Meaning |
| --- | --- | --- |
| `enabled` | `true` | Turns the plugin off without removing it. |

## Console

`charms` opens or closes the charm inventory.

## Credits

Charm icon "rune-stone" by Lorc, Delapouite and contributors, [game-icons.net](https://game-icons.net), CC BY 3.0. Panel frame cropped from the stash art Grail On! packages (`tools/make_art.py`). See `src/ART-CREDITS.txt`.
