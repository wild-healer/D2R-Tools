# Perfect Frame

D2RLoader plugin that draws a golden frame around the tooltip of a perfect unique, set item or runeword, and a red frame when every roll is at its worst.

- **Perfect**: every property that can roll is at its best, by the game's own tables, so modded tables count. Uniques and set items with defense also need the best base defense roll (+1 with Enhanced Defense, x1.5 when ethereal). Sockets count up to what the base can hold. An item with nothing that rolls is always perfect.
- **Worst rolls**: the red frame marks an item whose every roll is at its lowest. Items with nothing that rolls never get it.
- **Everywhere**: inventory, stash, cube, equipped items, and the comparison window (Shift), including comparisons other plugins show, such as Grail On!'s.
- Unidentified items get no frame. A worn item's per-level defense doesn't count.

Hooks the game's tooltip panel and tooltip text builders; it checks that code when it loads and stays inactive on other builds. Tested on Steam 3.3.94995 (3.3.93787 has the same code). Requires D2RLoader 1.3.1 or later.

## Install

Download it from the [D2RLoader Hub](https://d2rloader.net/hub/plugins/perfect-frame). Load `perfect-frame-<version>.zip` (the DLL alone; its default settings are built in) in D2RMM, or copy `d2rl-perfect-frame.dll` to `<game>/d2rloader/plugins/`. Remove the DLL to uninstall.

## Settings

`d2rloader/config/perfect-frame.toml`, written on first launch:

| Key | Default | Meaning |
| --- | --- | --- |
| `enabled` | `true` | Turns the plugin off without removing it. |
| `uniques`, `sets`, `runewords` | `true` | Which items can get a frame. |
| `color` | `"226,182,76"` | Colour of the perfect frame, as red, green, blue (0 to 255). |
| `antiperfect` | `true` | Red frame on items whose every roll is at its worst. |
| `antiperfect_color` | `"180,20,20"` | Colour of that frame. |
| `thickness` | `0` | Frame thickness in screen pixels; `0` picks it from the window height (3 at 1080p, 6 at 4K). |

## Console

`perfect-frame` prints why the last hovered and the last compared item are perfect or not: each rolled stat as `stat=item/best`, `!` when below its best.
