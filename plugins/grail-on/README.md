# Grail On!

D2RLoader plugin that puts your holy grail inside the game: every unique, set item and runeword has its own tile on a grail page you open from the inventory, and the items you find go straight into it.

- **Your grail, in game**: open it with the grail button on the left edge of the inventory. Menus along the top and a category column on the left sort uniques by item type, set items by set (class sets with their class), and runewords by item type (gloves, belts and boots too when a mod gives them runewords), then socket count. Uniques that can be ethereal get a second tile for their ethereal copy.
- **Drop it in**: drop a grail item on the page, or Ctrl+click it, and it goes straight to its own tile, wherever that tile is. Only one copy of each item may be stored: a twin of a stored copy stays out. Take a copy out by clicking it, or Ctrl+click it to send it to your inventory.
- **Runeword bases**: a plain or superior base with the right socket count can wait in an empty runeword tile it can be made into.
- **Stats**: an overview and one page per kind (uniques, ethereal uniques, set items, runewords) show what you have found, stored and have perfect, with ring charts and per category bars.
- **Perfect copies**: a stored copy whose variable rolls are all the best gets a gold frame around its tile. Items with nothing that rolls count as perfect.
- **Compare**: while the grail is open, hovering a grail item anywhere shows its stored copy as the game's comparison tooltip, always or only with Shift.
- **Loot filter**: hide uniques, ethereal uniques and set items on the ground once the grail has them, or has them perfect.
- **Spoilers**: unfound entries show their base item's name and picture until you find them, or their own if you choose.
- **Follows your mod**: the lists, names and pictures come from the running game's own tables and files, so modded uniques, sets and runewords are there.

The page holds real items only while the grail is open; the grail file keeps everything else. Removing the plugin leaves no grail items on the page, so your characters load without it.

Hooks the game's tooltip, comparison and loot drawing code; it checks that code when it loads and switches those parts off on other builds. Tested on Steam 3.3.94995 (3.3.93787 has the same code). Requires D2RLoader 1.3.1 or later.

## Player Page Host

D2RLoader 1.3.1 allows one custom page. With [Player Page Host](https://d2rloader.net/hub/plugins/aaa-player-page-host) installed, Grail On! takes a 16x12 area of the host's page, so other plugins that use the host can share it. Without it, Grail On! uses the custom page on its own, and another plugin that needs the custom page (such as Charm Inventory) cannot run beside it.

## Install

Download it from the [D2RLoader Hub](https://d2rloader.net/hub/plugins/grail-on). Load `grail-on-<version>.zip` (the DLL alone) in D2RMM, or copy `d2rl-grail-on.dll` to `<game>/d2rloader/plugins/`. Remove the DLL to uninstall.

## Files

- Settings: `Saved Games\Diablo II Resurrected\grail-storage\grail-on.toml`, written by the settings page. Kept outside the mod, so reinstalling keeps them.
- Grail: `grail-softcore.dat` and `grail-hardcore.dat` in the active mod's save folder (`Saved Games\Diablo II Resurrected\mods\<mod>\`). Softcore and hardcore characters each share one grail per mod.

## Settings

All of them are on the grail's settings page (the gear icon) and take effect at once.

| Key | Default | Meaning |
| --- | --- | --- |
| `unique_names`, `set_item_names`, `runeword_names` | `false` | Unfound entries show their own name. Off: their base item's name, or "Unknown runeword", until found. |
| `set_names` | `false` | Every set's name shows. Off: "Unknown set" until one of its items is found. |
| `unique_art`, `set_item_art` | `false` | Unfound entries show their own picture. Off: their base item's picture until found. |
| `runeword_runes` | `false` | Unfound runewords show their runes. Off: blank stones until found. |
| `goal_uniques`, `goal_ethereal`, `goal_sets`, `goal_runewords` | `find` | `none`, `find` or `perfect`: the kinds the overview counts, and what each stats page's centre shows. |
| `compare` | `always` | The grail copy beside a hovered grail item while the grail is open: `none`, `shift` or `always`. |
| `perfect_frames` | `true` | Perfect copies get a gold frame around their tile. |
| `frame_animations` | `true` | The perfect frame shimmers. Off: it stays still. |
| `scroll_animations` | `true` | The mouse wheel scrolls smoothly. Off: a row at a time. |
| `compact` | `false` | The window sits against the screen's left edge, with its menus on its right. |
| `swap_columns` | `false` | The category icons go in the left column and the menu icons in the right one. |
| `loot_uniques`, `loot_ethereal`, `loot_sets` | `all` | `all`, `found` or `perfect`: ground items hidden once the grail has them, or has them perfect. |
| `loot_ambiguous` | `false` | Unidentified ground items are hidden by what they really are. Off: only when every unique or set item of their base would be hidden. |

## Keybind

"Open/close the grail" in the game's Controls menu, under Grail On!. Unbound by default.

## Console

- `grailon`: open or close the grail.
- `grailon forget`: entries found but no longer stored go back to never found.
- `grailon rolls`: each stored copy's rolls against the best.

## Credits

Icons by Lorc, Delapouite and contributors, [game-icons.net](https://game-icons.net), CC BY 3.0 (credits embedded in the DLL).
