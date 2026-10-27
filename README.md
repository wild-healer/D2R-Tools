# D2R Tools

Tools for Diablo II: Resurrected (Reign of the Warlock): a Holy Grail tracker page, D2RLoader plugins, D2RMM mods and a D2RLoader memory patch. Each one has its own README with the full details and settings.

## Grail tracker

[`grail.html`](grail.html) is a single page, hosted at [d2r.wildhealer.fr](https://d2r.wildhealer.fr), that tracks your Chronicle and Holy Grail progress. It reads the Chronicle from your shared stash file, plans where each grail item goes in your stash (Extended Stash, vanilla 10x10 tabs or vanilla mules), and exports a loot filter profile that hides the sets and uniques you already have. Open it in a browser, nothing to install. [`deploy/`](deploy) holds the nginx config and update script for the site.

## Plugins

All plugins need [D2RLoader](https://github.com/D2RLoader) 1.3.1. They are tested on Steam 3.3.93787 and 3.3.94995, and on Battle.net 3.3.93847.

Every plugin, mod and patch is downloaded from the [D2RLoader Hub](https://d2rloader.net/hub/creators/thewildhealer). To install a plugin, add its zip through D2RMM like any other D2RLoader plugin, or copy its `d2rl-<name>.dll` to `<game>/d2rloader/plugins/`. Settings are written on first launch to `d2rloader/config/<name>.toml`.

| Plugin | Version | What it does |
| --- | --- | --- |
| [Cube Empty](plugins/cube-empty) | 1.0.0 | Middle-click the Horadric Cube in your inventory to move everything in it to your inventory. |
| [Cube Fill](plugins/cube-fill) | 1.0.0 | Shows what is in the cube on its inventory icon: the items drawn small, a count badge or a mini grid. |
| [Cube Overflow](plugins/cube-overflow) | 1.1.0 | When your inventory is full, pickups, purchases, gambling and quest rewards go into the cube instead. |
| [Edge Clicks](plugins/edge-clicks) | 1.0.0 | Clicking the void past the map edge or a wall walks you as far toward the click as the ground allows. |
| [Ethereal Ground Tag](plugins/ethereal-ground-tag) | 1.0.3 | Adds a tag such as `[Eth]` to the names of ethereal items on the ground. |
| [Quest Rewards](plugins/quest-rewards) | 1.0.0 | Flags characters on the character select screen that still have a quest reward waiting (imbue, socket, personalize...). Needs its D2RMM mod too. |
| [Quickcast Save Fix](plugins/quickcast-save-fix) | 1.0.0 | Saving during or right after a quick cast keeps your own left and right skills. |
| [Run Counter](plugins/run-counter) | 2.3.1 | Counts and times your runs, logs what you find in each, with an in-game panel and a stats window. |
| [Shrine Icons](plugins/shrine-icons) | 1.0.0 | Floating symbols over shrines that have none, and distinct shrine icons on the minimap. Needs its D2RMM mod too. |
| [Stash Tab Dropdown](plugins/stash-tab-dropdown) | 1.4.1 | A button next to the shared stash arrows opens a grid of all pages, to jump straight to one. |
| [Swap Cast](plugins/swap-cast) | 1.1.0 | Choose per skill which weapon set casts it (set I, set II, or one hand from each). |
| [Transition Immunity](plugins/transition-immunity) | 1.0.0 | After a loading screen, monsters can't hurt you until you move, for 60 seconds at most. |
| [Visual Clutter](plugins/visual-clutter) | 1.0.0 | Hides aura and shout visuals, Firestorm, Thunder Storm, charge orbs and the terror zone tint, without changing what they do. |

## D2RMM mods

Two plugins come with a D2RMM data mod holding their art, on the [D2RLoader Hub](https://d2rloader.net/hub/creators/thewildhealer) with the plugin. Extract the mod into D2RMM's `mods` folder, enable it and apply:

- [Quest Rewards](plugins/quest-rewards)
- [Shrine Icons](plugins/shrine-icons)

## Patches

- [Name Lock With Items Shown](patches/name-lock-items-shown) 1.0.0: fixes the hold-click target lock not working while item labels are shown. Get it from the [D2RLoader Hub](https://d2rloader.net/hub/creators/thewildhealer) and copy `name-lock-items-shown.json` to `<game>/d2rloader/patches/`.

## Building

Each plugin has a `build.sh` that cross-builds the DLL on Linux with MinGW-w64 and packs the release zip. It fetches the D2RLoader PluginSDK at the commit it was built against.
