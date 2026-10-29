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
