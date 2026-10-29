# Perfect Frame 1.0.0: release texts

## Patch notes

```
**Perfect Frame 1.0.0**

**New**

* A golden frame around the tooltip of a perfect unique, set item or runeword: every roll at its best, by the game's own tables.
* A red frame when every roll is at its worst.
* Frames show in the inventory, stash, cube, on equipped items and in the comparison window, including Grail On!'s comparison.
* Uniques and set items also need the best base defense roll; a worn item's per-level defense doesn't count.
* Items with nothing that rolls are always perfect. Unidentified items get no frame.
* uniques, sets and runewords settings: choose which items can get a frame.
* color and antiperfect_color settings: the two frame colours.
* antiperfect setting: turns the red frame off.
* thickness setting: frame width in pixels, or automatic from the window height.
* perfect-frame console command: shows why the last hovered and compared items are perfect or not.
```

## GitHub release

- Tag: `perfect-frame-v1.0.0`
- Title: `Perfect Frame 1.0.0`
- Asset: `perfect-frame-1.0.0.zip`

Body:

```markdown
**Perfect Frame** 1.0.0: a golden frame around the tooltip of perfect uniques, set items and runewords, and a red one when every roll is at its worst.

### Features
- Perfect = every property that can roll is at its best, read from the game's own tables, so modded tables count.
- Uniques and set items with defense also need the best base defense roll (+1 with Enhanced Defense, x1.5 when ethereal). A worn item's per-level defense doesn't count.
- Items with nothing that rolls are always perfect. Unidentified items never get a frame.
- Red frame when every roll is at its worst (antiperfect, on by default).
- Works in the inventory, stash, cube, on equipped items and in the comparison window, including comparisons shown by other plugins such as Grail On!.
- `perfect-frame` console command: each rolled stat as item/best, to see why an item is or isn't perfect.

### Requirements
- D2RLoader 1.3.1 or later.
- Steam 3.3.93787 or 3.3.94995. The plugin checks the game code when it loads; on another build it stays inactive and says so in its log.

### Install
Load `perfect-frame-1.0.0.zip` in D2RMM, or copy `d2rl-perfect-frame.dll` to `<game>/d2rloader/plugins/`. Settings are in `d2rloader/config/perfect-frame.toml`, written on first launch.
```

## D2RLoader Hub

Name: `Perfect Frame`

Summary:

> A golden frame around the tooltip of perfect uniques, set items and runewords, and a red one when every roll is at its worst.

Description: the GitHub release body above.
