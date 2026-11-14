# Wild Healer's Charm Inventory

D2RLoader plugin that gives you a charm inventory (10x4 by default, the size of the inventory's grid; up to 10x12) on the custom player page that [Player Page Host](../aaa-player-page-host) shares between plugins.

- **Charms only**: small, large and grand charms go in, anything else is refused. The charms in it give their bonuses; charms in the normal inventory don't (unless `inventory_charms` is on), Charm Dedup loaded or not (your own character only: other players in a TCP/IP game you host keep the game's rule).
- **One of each unique charm**: a second Annihilus, Torch, Gheed's or Sunder charm of the same kind is refused, dropped or Ctrl+clicked. Dropping it onto the first one swaps them.
- **Panel**: a game-style left panel (the character screen's background with its stat boxes covered by plain stone) with a "Charms" title and the grid of cells, centred.
- **Opening it**: the charm button on the inventory's left edge, under Grail On!'s grail button; the "Open/close charms" action of the game's Controls menu (no key by default); or the `charms` console command.
- **Fixed position**: the panel sits where the game's left panels sit, written out rather than taken from `$LeftPanelAnchor`, so mods that move that anchor don't carry it off screen.
- Ctrl+click moves charms between the inventory and the open charm panel (the host does it).

Needs Player Page Host (`aaa-player-page-host`). The charms stay on the player page if the plugin is removed; `pagehost all` (a Player Page Host console command) shows the whole page. Requires D2RLoader 1.3.1 or later.

## Install

Download it from the [D2RLoader Hub](https://d2rloader.net/hub/plugins/wild-healer-s-charm-inventory). Load `wild-healers-charm-inventory.zip` (the DLL alone; its default settings are built in) in D2RMM next to Player Page Host, or copy `wild-healers-charm-inventory.dll` to `<game>/d2rloader/plugins/`. Remove the DLL to uninstall.

## Settings

`d2rloader/config/wild-healers-charm-inventory.toml`, written on first launch, or in game with [I Hate TOMLs](../i-hate-tomls). Every setting applies at once:

| Key | Default | Meaning |
| --- | --- | --- |
| `enabled` | `true` | Off: no button or panel, and charms in the normal inventory give their bonuses again. Charms left in the charm inventory stay there, without bonuses. |
| `inventory_charms` | `false` | On: charms in the normal inventory give their bonuses too. |
| `columns` | `10` | Width of the charm inventory in cells, 1 to 10. |
| `rows` | `4` | Height of the charm inventory in cells, 1 to 12. Take the charms out of the cells you remove before making it smaller (I Hate TOMLs refuses the change otherwise). |

## Console

`charms` opens or closes the charm inventory.

## Credits

The button, panel, cells and grid hook come from the [D2R GUI](../../lib/d2r-gui) library, compiled into the DLL. Charm icon "rune-stone" by Lorc, Delapouite and contributors, [game-icons.net](https://game-icons.net), CC BY 3.0, from the library's icon set. See `src/ART-CREDITS.txt`.
