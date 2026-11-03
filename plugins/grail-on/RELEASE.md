# Grail On! 1.1.5: patch notes

```
**Grail On! 1.1.5**

**New**

* Only unidentified items: a Loot setting, on by default, so the loot filter only hides unidentified items.

**Fixed**

* The game no longer crashes when grail buttons, slots or the scroll bar are clicked quickly: quick clicks make one view, and each view waits for the last one to finish loading.
* The grail keeps at most 8 closed views ready instead of 40, so it holds far fewer of the game's widgets.
* The plugin no longer writes copies of the game's art into the logs folder.
* Jewels show their own picture instead of a perfect diamond, and stored jewels and charms show the picture they have.
* Items with Enhanced Damage or Enhanced Defense rolls are counted as perfect as soon as they go in, and ones already stored are checked again, so the loot filter hides their copies.
```

# Grail On! 1.1.4: patch notes

```
**Grail On! 1.1.4**

**Fixed**

* Some browsers and antivirus scanners flagged the download as a virus: the stash art is now built into the plugin's code instead of being unpacked with Windows' image tools, and the plugin no longer reads memory the way they distrust.
```

# Grail On! 1.1.3: patch notes

```
**Grail On! 1.1.3**

**New**

* I Hate TOMLs lists Grail On! with an Open button that opens the grail on its settings.

**Fixed**

* The game no longer crashes when the grail's scroll bar is dragged quickly.
* A ring or amulet just put in the grail shows its own picture at once instead of only after changing tabs.
* Stored items that no grail tile holds any more (a mod removed their entry, or an older version named it otherwise) come back to the inventory, else the personal stash, when the character comes in.
```

# Grail On! 1.1.2: patch notes

```
**Grail On! 1.1.2**

**Fixed**

* An item that shares its identity with a stored item (a copy made with an item editor, such as an ethereal copy of a normal one) is no longer destroyed: it goes to the inventory and cannot be stored.
* Items taken out of the grail just before the game closed unexpectedly come back into the grail instead of being lost.
```

# Grail On! 1.1.1: patch notes

```
**Grail On! 1.1.1**

**Fixed**

* Stored rings and amulets show their own picture in their tile again instead of the base's generic one.
```

# Grail On! 1.1.0: patch notes

```
**Grail On! 1.1.0**

**New**

* The grail speaks every game language, using the game's own texts first so mods that add translations fit in.
* language setting: the same as the game, or any of the game's languages.
* button_x and button_y settings: move the grail button in the inventory, 1 step per click, 10 with Shift and 100 with Ctrl.
* Misc tracking settings: each misc item type can be off, Checkbox (a click ticks its tiles) or Items (its items are stored), with a category each in a new Misc menu.
* Misc has its own stats page and counts in the overview.
* The misc tracking settings show some of each type's items on hover.
* Misc types the grail already tracks (amulets, rings, charms, jewels) and items nobody collects (potions, quivers, keys, scrolls, tomes, quest items, body parts) are not offered.
* Custom objectives: add your own in the settings, then tick them in a new Objectives menu; each has an icon picked from a list, can be switched off and moved up or down.
* Objectives have their own stats page and count in the overview.
* objectives setting: hides the Objectives menu and keeps the list; the menu only shows when there is an objective.
* Two objectives are there to start with: Find the easter egg in Grail On! and Find a 3/20/20 small charm.
* A search bar under the window finds items by name; items that do not match are dimmed, and Enter or the arrows on the line under it go to the next match.
* search_screen_x, search_screen_y and search_length settings: place the search bar on the screen (compact mode does not move it) and change its length.
* Middle click an item to show it in Grail On!, even with the grail closed; with the grail closed its tooltip says so.
* middle_click setting: gives middle clicks back to the game.
* Joined TCP/IP games: items go in and out of the grail through the hosting player's Player Page Host (1.0.2 or later); there is no comparison with stored items that are not on screen.
* Runeword categories a mod adds to the game's tables get their own category.
* The settings are split into General, Display, Loot, Misc tracking and Custom objectives pages, picked with icons in the category column.
* Perfect frame, Menu direction, Layout and Scrolling choices replace the old check boxes.
* The grail takes only 3x5 cells of the custom page; every tile shows its stored copy as a picture.

**Fixed**

* The game no longer crashes after scrolling the grail for a while.
* Mod item names with colour codes no longer show stray characters.
* Characters with non-Latin names (such as Chinese) now use their own grail, hardcore included.
* Game texts with gendered or plural forms no longer show their [ms] and [fp] tags.
* Misc categories are named after their items in every language, such as gems.
* Gems, runes and other simple items stay in their tile instead of going back to the inventory.
* A player who joined a TCP/IP game now sees their stored items in the grail.
```

# Grail On! 1.0.4: patch notes

```
**Grail On! 1.0.4**

**Fixed**

* Runewords are found again with mods that give runewords their own keys in the Runes table, such as D2R Reimagined.
* A mod's own runewords show their name in the game's language.
* Two runewords a mod gives the same key each get their own tile.
```

# Grail On! 1.0.3: patch notes

```
**Grail On! 1.0.3**

**New**

* swap_columns setting: the category icons go in the left column and the menu icons in the right one.

**Fixed**

* With compact on, the menu icons are in the left column and the category icons in the right one, as with compact off.
```

# Grail On! 1.0.2: patch notes

```
**Grail On! 1.0.2**

**New**

* Runewords for gloves, belts and boots, from mods that allow them, get their own categories in the Runewords menu. Runewords on any other item type go to an Other category.
* compact setting: the grail window sits against the screen's left edge, with its menus on its right.

**Fixed**

* Runewords are still found when a mod changes the game's first runewords or their runes.
* Runewords a mod turns off are no longer listed.
```

# Grail On! 1.0.1: patch notes

```
**Grail On! 1.0.1**

**New**

* Open/close the grail keybind in the game's Controls menu, unbound by default.

**Fixed**

* The grail window no longer moves off screen with mods that move the game's left panel anchor.
```

# Grail On! 1.0.0: release texts

## Patch notes

```
**Grail On! 1.0.0**

**New**

* A grail page inside the game, opened with the grail button on the inventory's left edge: every unique, set item and runeword has its own tile.
* Drop or Ctrl+click a grail item on the page and it goes straight to its tile. Only one copy of each item may be stored.
* Uniques sorted by item type, set items by set with class sets under their class, runewords by socket count. Ethereal uniques get their own tile.
* Stats pages: an overview and one page per kind, with what is found, stored and perfect.
* Perfect copies get a gold frame around their tile.
* compare setting: hovering a grail item while the grail is open shows its stored copy as the game's comparison, always or with Shift.
* Loot settings: hide uniques, ethereal uniques and set items on the ground once the grail has them, or has them perfect.
* Spoiler settings: unfound entries show their base item's name and picture until found.
* Goal settings: choose which kinds count, and whether finding or perfect is the goal.
* Lists, names and pictures come from the running game, so modded items are there.
* Shares the custom page with other plugins through Player Page Host.
* grailon forget console command: entries found but no longer stored go back to never found.
```

## GitHub release

- Tag: `grail-on-v1.0.0`
- Title: `Grail On! 1.0.0`
- Asset: `grail-on-1.0.0.zip`
- Images: the seven showcase images (`showcase/images/1-grail-on.png` to `7-your-rules.png`), dragged into the body in order.

Body:

```markdown
**Grail On!** 1.0.0: your holy grail inside the game. Every unique, set item and runeword has its own tile, and the items you find go straight into it.

### Features
- Open the grail with the grail button on the inventory's left edge. Uniques are sorted by item type, set items by set (class sets with their class), runewords by socket count. Uniques that can be ethereal get a second tile.
- Drop or Ctrl+click a grail item on the page: it goes straight to its own tile, wherever that is. Only one copy of each item may be stored. Click a copy to take it out.
- An empty runeword tile can hold a plain or superior base with the right sockets.
- Stats: an overview and a page per kind, with found, stored and perfect counts.
- A stored copy with every best roll gets a gold frame.
- While the grail is open, hovering a grail item anywhere shows its stored copy as the game's comparison, always or with Shift.
- Loot filter: hide ground uniques, ethereal uniques and set items once the grail has them, or has them perfect.
- Spoilers: unfound entries show their base item until found, or their own name and picture if you prefer.
- Goals: choose which kinds count and whether you are after finding them or perfect copies.
- Everything comes from the running game's tables and files, so modded uniques, sets and runewords are there.
- Real items sit on the page only while the grail is open, so removing the plugin leaves your characters clean.

### Requirements
- D2RLoader 1.3.1 or later.
- Steam 3.3.93787 or 3.3.94995. The plugin checks the game code when it loads; on another build the hooked parts stay off and its log says so.
- Optional: Player Page Host, to share the custom page with other plugins. Without it, Grail On! needs the custom page for itself (no Charm Inventory beside it).

### Install
Load `grail-on-1.0.0.zip` in D2RMM, or copy `d2rl-grail-on.dll` to `<game>/d2rloader/plugins/`. Settings are on the grail's settings page and saved in `Saved Games\Diablo II Resurrected\grail-storage\grail-on.toml`. The grail itself is in the mod's save folder (`grail-softcore.dat`, `grail-hardcore.dat`).

### Showcase
Colored item backgrounds in the images come from Item Quality Background by YupGoolg, sockets from Always Show Sockets by PyreFly, the tooltip frame from Perfect Frame and the better/worse markers from Compare Indicators. Several mods change the game UI in the screenshots.

### Credits
Icons by Lorc, Delapouite and contributors, game-icons.net, CC BY 3.0.
```

## D2RLoader Hub

Name: `Grail On!`

Summary:

> Your holy grail inside the game: every unique, set item and runeword in its own tile, with stats, perfect frames, grail compare and a loot filter.

Description: the GitHub release body above. Images: the seven showcase images, in order.
