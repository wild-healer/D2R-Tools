# Compare Indicators 1.0.0: release texts

## Patch notes

```
**Compare Indicators 1.0.0**

**New**

* Indicators on the hovered item's tooltip lines while the game compares it with another item.
* Against another copy of the same unique, set item or runeword: (perfect +x), (perfect), (better +x), (same), (worse -x) and a red (lost perfect -x), from the game's own roll tables.
* any_items setting: against any other item, (better +x), (same), (worse -x), a green (new) on stats the compared item lacks, and a red (lost) on the compared item's stats the hovered one lacks.
* Runes, gems and jewels in sockets count in the comparison.
* Weapons compare with your weapon and shields with your shield. Two-handed items and dual wielding count against both hands.
* Works with any comparison, including Grail On!'s grail copy.
* uniques, sets and runewords settings: choose which items get roll indicators.
* compare-indicators console command: shows what the last hovered item was compared with.
```

## GitHub release

- Tag: `compare-indicators-v1.0.0`
- Title: `Compare Indicators 1.0.0`
- Asset: `compare-indicators-1.0.0.zip`

Body:

```markdown
**Compare Indicators** 1.0.0: indicators on an item's tooltip lines while the game compares it with another item.

### Features
- Against another copy of the same unique, set item or runeword: (perfect +x), (perfect), (better +x), (same), (worse -x), or a red (lost perfect -x) when the copy had the best roll. Rolls come from the game's own tables, so modded tables count.
- Against any other item (any_items, on by default): (better +x), (same), (worse -x), a green (new) for stats the compared item doesn't have, and a red (lost) on the compared item's stats the hovered one doesn't have.
- Runes, gems and jewels in sockets count. Perfect is judged on the item's own roll.
- Weapons compare with your weapon, shields and off-hands with your shield. Two-handed items count against both hands, and so do one-handed weapons for classes that dual wield.
- Works with whatever item the comparison shows, including one another plugin puts there, such as Grail On!'s grail copy.
- Item Roll Ranges keeps the indicators.
- `compare-indicators` console command: what the last hovered item was compared with.

### Requirements
- D2RLoader 1.3.1 or later.
- Steam 3.3.93787 or 3.3.94995. The plugin checks the game code when it loads; on another build it stays inactive and says so in its log.

### Install
Load `compare-indicators-1.0.0.zip` in D2RMM, or copy `d2rl-compare-indicators.dll` to `<game>/d2rloader/plugins/`. Settings are in `d2rloader/config/compare-indicators.toml`, written on first launch.
```

## D2RLoader Hub

Name: `Compare Indicators`

Summary:

> Indicators on tooltip lines while comparing items: perfect, better, worse, same, new and lost, sockets included.

Description: the GitHub release body above.
