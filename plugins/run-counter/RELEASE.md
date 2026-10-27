# Run Counter 2.3.1: release texts

## GitHub release

- Tag: `run-counter-v2.3.1`
- Title: `Run Counter 2.3.1`
- Asset: `run-counter-2.3.1.zip`

Body (changes since 2.3.0):

```markdown
**Run Counter 2.3.1** fixes two counting mistakes.

### Fixed
- Items picked up straight into the Cube were not counted. They count now; what appears in the Cube when you transmute still does not.
- Boss drops in a terror zone were listed as elite drops: the boss is raised above its usual level there, so its items did not match it. An item picked up after a boss kill in the same area is now the boss's when its level is at least the boss's.

### Install
Load `run-counter-2.3.1.zip` in D2RMM, or copy `d2rl-run-counter.dll` to `<game>/d2rloader/plugins/`. Stats and settings are kept. Drops already saved keep the source they were given.
```

The 2.3.0 notes (changes since 2.1.2) are in the git history of this file.

## Description (GitHub / Nexus, Markdown)

```markdown
**Run Counter** is a D2RLoader plugin that counts your runs, times them and keeps permanent stats on what you find. Bind its controls in game to open the panels.

### Features

* **Runs**: a game becomes a run when you leave town. Farming and town time are tracked separately, per run, per session and for all time, and stop while the pause menu is open.
* **Items**: everything you pick up outside town is tracked, by kind: uniques, sets, rares, magic, normal items, gems, runes from El, essences and keys, Worldstone shards, and consumables if you want them. Each kind can be turned off or kept out of the panel's drops list. Items from the stash, Cube transmute results, and items picked up again in the same game do not count.
* **Game panel** drawn with the game's own UI: run number, farming time, town time, average farming time, items this run, average items per run, and this game's latest drops below it.
* **Stats panel** (*Show Run Stats*): Overview, Equipment, Misc, Reset and Settings tabs, with selectors for area, character, terror zone and slot, item type or date, and colored bar charts. It includes the game in progress.
* **Settings tab**: change every setting in game; changes survive restarts and D2RMM installs.
* **Permanent stats**: `runs.csv` and `drops.csv` keep every run and item, are loaded again at launch and open in Excel. Each run records the character, its level, the net gold and whether the area was terrorized; each item, an estimate of what dropped it.

### Requirements

* D2RLoader 1.3.1 or later.
* Tested on Steam 3.3.93787. It patches no game code. It reads one value from the game's item data, the real level of stackable items, through D2RLoader's native item access. Battle.net 3.3.93847 should work too; if that value cannot be read there, stackable items keep level 1.
* Single player or TCP/IP.

### Install

Load `run-counter-2.3.1.zip` in D2RMM, or copy `d2rl-run-counter.dll` to `<game>/d2rloader/plugins/`. Then bind *Show/Hide Run Counter*, *Show Run Stats* and *New Run Counter Session* in Controls (category *Run Counter*); they have no default key.

### Console

`runs`, `runs-drops`, `runs-reset`, `runs-stats`, `runs-panel`, `runs-test`.

### Known limits

* Only items you pick up count; items left on the ground do not.
* What dropped an item and whether a run was in a terror zone are estimated from item levels. The terror zone's monster modifier is not tracked.
* Gold is the net change read from your save files, so purchases and repairs lower it.
* `panel = "overlay"` draws nothing on some setups.
```

## Nexus Mods

Name: `Run Counter (D2RLoader plugin)`

Summary (short description):

> Counts your runs, splits farming and town time, tracks what you pick up across sessions, and shows it in an in-game panel and a tabbed stats screen.

Changelog (BBCode):

```bbcode
[b]2.3.1[/b]
[list]
[*]Items picked up straight into the Cube are counted.
[*]Boss drops in a terror zone are credited to the boss instead of elites.
[/list]
[b]2.3.0[/b] (since 2.1.2)
[list]
[*]Settings tab: change every setting in game; changes are saved and survive restarts and D2RMM installs.
[*]Tracking by kind (uniques, sets, rares, magic, normal, gems, essences, shards, consumables), each Off, On or On without the panel list. Runes tracked from El, listed from Ist.
[*]Tracking active setting to pause recording.
[*]Drops list under the game panel, colored by quality or by a mod's item colors. Optional panel background.
[*]Game panel in one column with aligned values; average items per run instead of total items.
[*]Farming and town time stop on the pause menu.
[*]Reset tab: delete a single game.
[*]Stats tabs include the game in progress.
[*]Gems, runes and other stackable items get their real item level, so their drop source is estimated.
[*]Nothing picked up in town or from a stash counts; items picked up again in the same game are not counted twice.
[*]Games spent only in town are not saved; their gold goes to the previous run.
[*]Fixed: Delete everything kept the game in progress; clicking the stats panel moved your character.
[/list]
[b]2.1.2[/b] (since 1.0.0)
[list]
[*]Permanent stats: runs and items are saved and loaded again at launch, outside the folder D2RMM empties.
[*]Everything you pick up is tracked by default (track_all), consumables optional.
[*]Per run: character, level, net gold (including items sold) and terror zone. Per item: estimated drop source.
[*]New stats screen (Show Run Stats): Overview, Equipment, Misc and Reset tabs, with drop-down selectors for area, character, terror zone and item type, and colored bar charts with percentages.
[*]Misc tab lists all 33 runes, gems by kind and grade with totals, and charts by grade and type.
[*]Partial reset of the runs matching the selectors.
[*]New settings: track_all, consumables, data_folder, log_ui_messages. min_rune now only sets console announcements.
[/list]
[b]1.0.0[/b]
[list]
[*]First release.
[/list]
```
