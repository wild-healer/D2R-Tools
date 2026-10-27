# Run Counter

D2RLoader plugin that counts your runs, times them, and logs the items you find in each one.

- **Runs**: a game counts as a run once you leave town; a game spent only in town is not a run: it is not saved, and gold it gained (selling items) is added to the previous run of the same character. Each run's time is split into farming time (out of town) and town time. Both clocks stop while the pause menu is open, which pauses the game offline.
- **Items**: everything you pick up is tracked by default, except potions, scrolls and other consumables; each quality can be turned off. An item counts when it shows up in your inventory, cursor, belt or Cube for the first time during a game. Items that first appear in a stash do not count, and neither does what appears in the Cube when you transmute. Nothing picked up, bought or gambled in town counts. Runewords are made, never dropped, so they are not counted. Uniques, sets and runes from Ist up are listed in the game panel's drops list.
- **Permanent stats**: runs and items are saved in `runs.csv` and `drops.csv` and loaded again at launch, so nothing resets when the game closes. Each run also records the character, its level, the net gold and whether the area was terrorized.
- **Panel**: a small panel drawn with the game's own UI shows the run number, this run's farming and town time, the average farming time, the items this run and the average items per run.
- **Stats panel**: the *Show Run Stats* control opens a panel with five tabs: *Overview*, *Equipment*, *Misc*, *Reset* and *Settings*. Escape, the X button or the same control closes it.

It uses D2RLoader's documented plugin services and patches no game code. It also reads one value from the game's item data, the real level of stackable items, through D2RLoader's native item access; if that value cannot be read, those items keep level 1. Tested on Steam (3.3.93787); the same DLL should work on Battle.net (3.3.93847).

## Install

Get Run Counter from the [D2RLoader Hub](https://d2rloader.net/hub/creators/thewildhealer). Its zip holds the DLL and the default TOML at its root, ready to load in D2RMM.

To install by hand, copy `d2rl-run-counter.dll` to `<game>/d2rloader/plugins/` for every mod, or to `<game>/mods/<mod>/d2rloader/plugins/` for one mod only. Remove the DLL to uninstall.

On first launch, D2RLoader writes the default settings to `d2rloader/config/run-counter.toml`, which is the same as [run-counter.toml](run-counter.toml). Edit that copy:

| Key | Default | Meaning |
| --- | --- | --- |
| `enabled` | `true` | Turns the plugin off without removing it. |
| `tracking_active` | `true` | When `false`, games and items are not saved (pause recording). |
| `uniques`, `sets`, `rares`, `magic` | `true` | Qualities tracked. Crafted items never drop, so they never count. Like the other kinds below, `"no_overlay"` counts them without listing them in the game panel's drops list. |
| `normal` | `true` | Normal, superior and low quality items (bases). |
| `gems` | `true` | Gems. |
| `essences` | `true` | Essences, uber keys and the Ancients' statues. |
| `shards` | `true` | Worldstone shards. |
| `consumables` | `false` | Also tracks potions, scrolls, tomes, keys and arrows. |
| `min_rune` | `"El"` | Lowest rune tracked (`"El"` to `"Zod"`, or `"none"`). |
| `announce_rune` | `"Ist"` | Lowest rune shown in the game panel's drops list. |
| `codes` | `[]` | Item codes that always count, e.g. `["pk1", "pk2", "pk3"]` for keys. |
| `panel` | `"game"` | `"game"` draws the panel with the game's own UI. `"overlay"` uses D2RLoader's overlay instead, which shows nothing on some setups. `"off"` hides the panel. |
| `panel_background` | `false` | Translucent background behind the game panel and its drops list. |
| `colors` | `true` | Colors drop lines by quality in the overlay, or with the color a mod gives the item's name. |
| `overlay_x`, `overlay_y` | `0.01`, `0.30` | Top-left corner of the panel, as a fraction of the screen. |
| `text_scale` | `1.0` | Spacing of the game panel, or text size of the overlay. |
| `last_drops` | `3` | How many of this game's drops are listed under the panel (game panel and overlay). |
| `scan_interval_ms` | `500` | How often items are checked for new drops. |
| `bar_length` | `40` | Length of the bar charts in the stats tabs, in characters. |
| `log_ui_messages` | `false` | Writes the game's UI messages to `run-counter-ui.log`, to look for a way to detect terror zones. |
| `data_folder` | `""` | Folder of `runs.csv` and `drops.csv`. Empty means `<game>/d2rloader/run-counter/`. |

## In game

- **Controls menu** (category *Run Counter*): *Show/Hide Run Counter*, *Show Run Stats* and *New Run Counter Session*. They have no default key, so bind them there.
- **Console**: `runs` shows the totals and the last 10 runs, `runs-drops` lists this session's drops, `runs-reset` starts a new session, `runs-stats` opens or closes the stats panel, `runs-panel` opens the game panel right away and reports whether it opened, and `runs-test` draws a red test box in the middle of the screen for 15 seconds to check that plugin overlays show on your setup.
- A line is printed to the console each time a notable item is found.

A session lasts from game launch until you start a new one; it only changes the *This session* numbers. Everything else covers all saved runs.

### Stats tabs

- The Overview, Equipment and Misc tabs include the game in progress, with what you picked up so far; its character is the last one saved and its gold is counted when the game ends.
- **Overview**: all-time totals (runs, farming and town time, runs per hour, fastest and slowest run, items, gold), this session, this game, every area with its runs and averages, the last runs and the last notable finds.
- **Equipment** (anything that can be equipped): items, items per hour and per run, counts per rarity, ethereal and socketed shares, average item level, gold per hour, the latest uniques and sets, and bar charts for slot, tier, rarity, ethereal and who dropped them, plus items per area.
- **Misc** (runes, gems, charms, jewels, keys, essences and the rest): a bar per type, best rune, a chart of who dropped them, all 33 runes with their bars and shares, and a table of gems by kind and grade with totals, then charts by grade and by type.
- **Reset**: deletes the saved runs that match the selectors, or everything. *Delete everything* also clears the game in progress and starts again at run 1. The **x** beside each listed game deletes only that game (click it, then **ok?**). The button asks for a second click within 10 seconds.

- **Settings**: changes the settings of the config file (tracking on or off, qualities and categories tracked, runes tracked and announced, panel, position, spacing, background, drops listed, scan interval, bar length) with a button for each on/off setting and < and > for the others. *Tracking active* sits on its own at the top. Changes apply at once, except the panel type, position and spacing of the game panel, which apply after a restart. They are saved to `settings.toml` beside `runs.csv` and written into the config file too, so restarting the game or installing mods with D2RMM keeps them. `settings.toml` wins over the config file; delete it to edit the config file by hand again.

Clicks on the stats panel no longer reach the game, so clicking its background does not move your character.

The Equipment, Misc and Reset tabs have selectors for area, character and terror zone, plus slot (Equipment), item type (Misc) or date (Reset). Click a selector to list its values, then click the one you want.

The game has no pie chart widget, so repartitions are drawn as bar charts: one colored bar per part, with its count and share.

### What is estimated

- **Dropped by**: D2RLoader does not say which monster dropped an item, so it is estimated from the item level. Normal monsters drop at the area level, champions at +2, uniques and minions at +3, and bosses at their own level. An item picked up after a boss was killed in the same area is the boss's when its level is at least the boss's: in a terror zone bosses drop above their own level (Mephisto drops level 99 items). Gems, runes and other stackable items report item level 1, so their real level is read from the game's own item data (this needs D2RLoader's native access). Items that versions before 2.2.2 counted in town (gambled, bought) are marked *Town*.
- **Terrorized**: a run is terrorized when an item's level is above what the area can give without a terror zone. A run with no item drops cannot be told apart, and the terror zone's monster modifier is not available to plugins, so it is not tracked.
- **Gold**: no plugin service gives gold, so it is read from your save file and shared stash when a game starts and when it ends. Gold is the difference, so it includes gold picked up, items sold and repairs or purchases. Save files are only read, never changed.
- **Character**: the name of the save file written when you leave the game.

## Files

Both files are kept in `<game>/d2rloader/run-counter/`, outside the mod folder: D2RMM empties `mods/<mod>/` each time it installs mods. Set `data_folder` in the TOML to keep them elsewhere. Files that version 2.0.0 wrote beside the log are copied there on first launch.

- `settings.toml`: the settings changed in the *Settings* tab, read after the config file at launch.
- `runs.csv`: `run,start,end,seconds,farm_seconds,town_seconds,difficulty,area,levels,bosses,items,character,level,terrorized,gold`. Games spent only in town have run 0. The area is the last boss killed, or else the last area you entered outside town. Levels lists every area visited, in order.
- `drops.csv`: `run,time,area,name,quality,code,ilvl,ethereal,sockets,character,difficulty,terrorized,source,run_area`. The run is 0 for items picked up in a game where you never left town.

Files from version 1.x are converted on first launch; the originals are kept as `runs-1.x.csv` and `drops-1.x.csv`. A reset keeps the previous files as `runs-backup.csv` and `drops-backup.csv`.

## How drops are found

D2RLoader has no event for an item falling to the ground, so the plugin works from your items instead:

1. When the character is ready in a new game, every item it owns is recorded by its item seed (the baseline).
2. Every `scan_interval_ms`, and on each area change and when leaving the game, the plugin lists your items again. An item it has never seen is a new item, and it is recorded if its quality or code counts.
3. Items that first appear in the Cube are cube outputs (rune upgrades, crafts), and items that first appear in a stash were already yours (the game loads some stash pages after the baseline), so neither counts. If more than 12 countable items appear in one scan, they were loaded rather than found, and they are added to the baseline.

What this means in practice:

- Only items you pick up count. Items left on the ground do not.
- An item that leaves your items during a game (dropped, sold, used in the Cube) and comes back in the same game is not counted again. It is recognized by its type, quality, level and affixes, so dropping a rune and then finding an identical one of the same item level in that game counts the second as the first one coming back.
- Items bought or gambled from vendors, and anything else picked up in town, do not count.

Names come from the game's own strings, so they match your game language. The tables that link item rows to string keys (`src/tables.inc`) are generated from the game's excel files by `tools/gen_tables.py`.

Boss kills come from D2RLoader's monster-death event for monsters marked `boss` in MonStats (Andariel, Duriel, Mephisto, Diablo, Baal, Nihlathak, the Ubers…). Super uniques such as Pindleskin are not marked as bosses, so those runs are named after their area. Difficulty comes from the same event, so a run with no kills has no difficulty.

## How the game panel works

D2RLoader 1.3.1 can open a plugin panel and show or hide the pieces in it, but it cannot change their text. So the panel's layout holds one piece of text per character each number can show, and the plugin hides all but the right ones. The game hands a plugin about 250 of these pieces at most, so the panel shows numbers only (under 200 pieces) and stays open for the whole session; the show/hide control and leaving a game only hide it.

The latest drops under the panel are names, so they go in a small panel of their own, rebuilt the same way as the stats tabs (below) each time the list changes.

The stats panel works another way. D2R keeps a panel's widgets and text from the first time it loads it, and plugins can only register panels at load (64 at most). So the plugin registers 60 stats panels at load, and each time the stats are shown (a tab, a selector, a value) it writes a new layout with the current numbers, asks D2R to unload the panel it will use, and opens it; two panels take turns. If a probe widget shows that D2R kept the old layout anyway, each view takes a panel never opened before, until they run out. Text boxes show color codes as `*` instead of reading them, so every line and bar gets its color from its own widget. Buttons send a `PanelManager:ClosePanel:run-counter/Cmd-…` message that the plugin catches.

## Checking it

`d2rloader/logs/run-counter.log` lists which parts are active (item scans, panel, boss kills), whether the game panel registered, opened and showed its contents, whether the UI tick is running, how many items each baseline held per container, every run and every drop.

## Build

Run `sh build.sh` on Linux with MinGW-w64 (`x86_64-w64-mingw32-g++`, `windres`, `zip`). It clones the D2RLoader PluginSDK at commit `03a5af9` into `.sdk/`, or uses `D2RL_SDK=<path>` if you set it.

After a game patch, regenerate the tables from the game's excel folder:

```sh
python3 tools/gen_tables.py "<game>/data/global/excel" > src/tables.inc
```
