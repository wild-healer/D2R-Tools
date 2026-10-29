# Compare Indicators

D2RLoader plugin that adds indicators to an item's tooltip lines while the game compares it with another item (the comparison window, with Shift).

- **Same item**: when the compared item is another copy of the same unique, set item or runeword, each line that can roll ends with an indicator against the copy's roll: (perfect +x), (perfect), (better +x), (same), (worse -x), or a red (lost perfect -x) when the copy had the best roll. Rolls come from the game's own tables, so modded tables count. Defense gets one when it can vary (base roll, Enhanced Defense or flat defense).
- **Any item** (`any_items`, on by default): against any other compared item, whatever its rarity, the hovered item's lines get (better +x), (same), (worse -x) or a green (new) when the compared item doesn't have that stat. The compared item's lines that the hovered one doesn't have get a red (lost). Requirements -x% counts lower as better.
- **Sockets**: runes, gems and jewels socketed in either item count in the values. Perfect is still judged on the item's own roll, so a same-item compare also marks the stats that differ only because of what is socketed.
- **Weapons and shields**: only the item the hovered one would replace gets compared: a weapon against your weapon, a shield or off-hand against your shield. A two-handed item (hovered or worn) counts against both hands, and so does a one-handed weapon when the class can dual wield.
- **Any comparison**: the compared item is whatever the comparison window shows, the worn one or one another plugin puts there instead, such as Grail On!'s grail copy.
- Nothing shows without a comparison. Unidentified items and worn items get no indicators on their own tooltip. Indicators are added to the finished tooltip text, so Item Roll Ranges keeps them.

Hooks the game's tooltip and comparison text builders; it checks that code when it loads and stays inactive on other builds. Tested on Steam 3.3.94995 (3.3.93787 has the same code). Requires D2RLoader 1.3.1 or later.

## Install

Download it from the [D2RLoader Hub](https://d2rloader.net/hub/plugins/compare-indicators). Load `compare-indicators-<version>.zip` (the DLL alone; its default settings are built in) in D2RMM, or copy `d2rl-compare-indicators.dll` to `<game>/d2rloader/plugins/`. Remove the DLL to uninstall.

## Settings

`d2rloader/config/compare-indicators.toml`, written on first launch:

| Key | Default | Meaning |
| --- | --- | --- |
| `enabled` | `true` | Turns the plugin off without removing it. |
| `uniques`, `sets`, `runewords` | `true` | Which items get roll indicators against another copy of themselves. |
| `any_items` | `true` | Indicators against any compared item: better, worse, same, new and lost. |

## Console

`compare-indicators` prints what the last hovered item was compared with, each marked stat's values, and what is socketed in both items.
