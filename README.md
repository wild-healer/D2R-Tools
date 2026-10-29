# D2R Tools

Tools for Diablo II: Resurrected (Reign of the Warlock): a Holy Grail tracker page, D2RLoader plugins, D2RMM mods and a D2RLoader memory patch. Each one has its own README with the full details and settings.

## Grail tracker

[`grail.html`](grail.html) is a single page, hosted at [d2r.wildhealer.fr](https://d2r.wildhealer.fr), that tracks your Chronicle and Holy Grail progress. It reads the Chronicle from your shared stash file, plans where each grail item goes in your stash (Extended Stash, vanilla 10x10 tabs or vanilla mules), and exports a loot filter profile that hides the sets and uniques you already have. Open it in a browser, nothing to install. [`deploy/`](deploy) holds the nginx config and update script for the site.

## Plugins

All plugins need [D2RLoader](https://d2rloader.net/) 1.3.1. They are tested on Steam 3.3.93787 and 3.3.94995, and on Battle.net 3.3.93847.

Every plugin, mod and patch is downloaded from its page on the D2RLoader Hub, linked from its name below. To install a plugin, add its zip through D2RMM like any other D2RLoader plugin, or copy its `d2rl-<name>.dll` to `<game>/d2rloader/plugins/`. Settings are written on first launch to `d2rloader/config/<name>.toml`.

| Plugin | What it does |
| --- | --- |
| [Compare Indicators](https://d2rloader.net/hub/plugins/compare-indicators) | Indicators on tooltip lines while comparing items: perfect, better, worse, same, new and lost, sockets included. |
| [Cube Empty](https://d2rloader.net/hub/plugins/cube-empty) | Middle-click the Horadric Cube in your inventory to move everything in it to your inventory. |
| [Cube Fill](https://d2rloader.net/hub/plugins/cube-fill) | Shows what is in the cube on its inventory icon: the items drawn small, a count badge or a mini grid. |
| [Cube Overflow](https://d2rloader.net/hub/plugins/cube-overflow) | When your inventory is full, pickups, purchases, gambling and quest rewards go into the cube instead. |
| [Edge Clicks](https://d2rloader.net/hub/plugins/edge-clicks) | Clicking the void past the map edge or a wall walks you as far toward the click as the ground allows. |
| [Ethereal Ground Tag](https://d2rloader.net/hub/plugins/ethereal-ground-tag) | Adds a tag such as `[Eth]` to the names of ethereal items on the ground. |
| [Grail On!](https://d2rloader.net/hub/plugins/grail-on) | Your holy grail inside the game: every unique, set item and runeword in its own tile, with stats, perfect frames, grail compare and a loot filter. |
| [Inventory Slots](https://d2rloader.net/hub/plugins/invetory-slots) | Dedicated slots for the Horadric Cube, the two tomes and keys next to the inventory, outside its grid. |
| [Orb Values](https://d2rloader.net/hub/plugins/orb-values) | Life and mana numbers always shown in the middle of the orbs, without the "Life:" and "Mana:" prefixes. |
| [Perfect Frame](https://d2rloader.net/hub/plugins/perfect-frame) | A golden frame around the tooltip of perfect uniques, set items and runewords, a red one when every roll is at its worst. |
| [Player Page Host](https://d2rloader.net/hub/plugins/aaa-player-page-host) | Shares the custom player page between plugins: each one reserves an area of it, and the host packs them. Used by other plugins, nothing visible on its own. |
| [Quest Rewards](https://d2rloader.net/hub/plugins/quest-rewards) | Flags characters on the character select screen that still have a quest reward waiting (imbue, socket, personalize...). Needs its D2RMM mod too. |
| [Quickcast Save Fix](https://d2rloader.net/hub/plugins/quickcast-save-fix-1-0-0) | Saving during or right after a quick cast keeps your own left and right skills. |
| [Run Counter](https://d2rloader.net/hub/plugins/run-counter) | Counts and times your runs, logs what you find in each, with an in-game panel and a stats window. |
| [Shrine Icons](https://d2rloader.net/hub/plugins/shrine-icons) | Floating symbols over shrines that have none, and distinct shrine icons on the minimap. Needs its D2RMM mod too. |
| [Stash Tab Dropdown](https://d2rloader.net/hub/plugins/stash-tab-dropdown) | A button next to the shared stash arrows opens a grid of all pages, to jump straight to one. |
| [Swap Cast](https://d2rloader.net/hub/plugins/swap-cast) | Choose per skill which weapon set casts it (set I, set II, or one hand from each). |
| [Transition Immunity](https://d2rloader.net/hub/plugins/transition-immunity) | After a loading screen, monsters can't hurt you until you move, for 60 seconds at most. |
| [Un-Anya-lyse](https://d2rloader.net/hub/plugins/un-anya-lyse) | Anya gets a "Remove Personalization" line: put a personalized item in her window and it loses its name. Free, and her quest reward stays. |
| [Visual Clutter](https://d2rloader.net/hub/plugins/visual-clutter) | Hides aura and shout visuals, Firestorm, Thunder Storm, charge orbs and the terror zone tint, without changing what they do. |
| [Wild Healer's Charm Inventory](https://d2rloader.net/hub/plugins/wild-healers-charm-inventory) | A 10x4 charm inventory in a game-style panel on the player page, opened from a button beside the inventory or a key. Needs Player Page Host. |

## D2RMM mods

Two plugins come with a D2RMM data mod holding their art, on the same hub page as the plugin. Extract the mod into D2RMM's `mods` folder, enable it and apply:

- [Quest Rewards](https://d2rloader.net/hub/plugins/quest-rewards)
- [Shrine Icons](https://d2rloader.net/hub/plugins/shrine-icons)

## Patches

- [Name Lock With Items Shown](https://d2rloader.net/hub/plugins/name-lock-with-items-shown): fixes the hold-click target lock not working while item labels are shown. Copy `name-lock-items-shown.json` to `<game>/d2rloader/patches/`.

## Building

Each plugin has a `build.sh` that cross-builds the DLL on Linux with MinGW-w64 and packs the release zip. It fetches the D2RLoader PluginSDK at the commit it was built against.
