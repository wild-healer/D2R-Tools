# Shrine Icons 1.0.0: release texts

## GitHub release

- Tag: `shrine-icons-v1.0.0`
- Title: `Shrine Icons 1.0.0`
- Assets: `shrine-icons-1.0.0.zip` (plugin), `shrine-icons-mod-1.0.0.zip` (mod)

Body:

```markdown
**Shrine Icons comes in two parts, and you need both:** a D2RLoader plugin (`shrine-icons-1.0.0.zip`) and a D2RMM mod (`shrine-icons-mod-1.0.0.zip`). The mod holds the art; the plugin puts it on the shrines.

First release of **Shrine Icons**: tell shrines apart before you walk up to them.

### Features
- **Floating symbols on the shrines that have none**, like booster shrines:
  - Refilling Shrine: rejuvenation potion
  - Portal Shrine: town portal scroll
  - Gem Shrine: amethyst
  - Monster Shrine: skull
  - Fire, Exploding and Poison Shrines: a burning skull (stay away)
  - Health and Mana Shrines get none, their coloured pool already shows them.
- **Behaves like the booster symbols.** It disappears when the shrine is used and comes back when it recharges, with the same faint shimmer.
- **A minimap icon for every shrine type** instead of the same gold ring for all:
  - the item picture for the shrines above (torch, exploding potion and gas potion tell fire, exploding and poison apart)
  - the classic floating symbol for the booster shrines (armor, combat, the four resists, skill, mana recharge, stamina, experience)
- **Settings** to turn off the symbols, the minimap icons, the booster icons, the shimmer or any shrine type. The mod has a symbol size and height.

### Requirements
- D2RLoader (plugin with native hooks) and D2RMM.
- Steam 3.3.93787 / 3.3.94995. On another game build the plugin checks every address first and stays inactive, saying so in its log.

### Install
1. Load `shrine-icons-1.0.0.zip` in D2RMM, or copy `d2rl-shrine-icons.dll` to `<game>/d2rloader/plugins/`.
2. Extract `shrine-icons-mod-1.0.0.zip` into D2RMM's `mods` folder (it holds a `Shrine Icons` folder), enable it and apply.

Settings are in `d2rloader/config/shrine-icons.toml`, written on first launch. Edit that copy, not the one in the zip, and restart the game after changing it.

### Known limits
- The mod replaces the HD automap sheets `default07` and `default13` (it repaints cells no data file uses). Another mod that replaces those sheets conflicts with it.
- Shrines already revealed on the map keep their old icon until the next game.
- Legacy (classic) graphics show the Skill Shrine animation for the new symbols.

### Log
`d2rloader/logs/shrine-icons.log` says which symbols were set and whether the mod was found.
```

## Nexus Mods

Name: `Shrine Icons (D2RLoader plugin + D2RMM mod)`

Summary (short description):

> Plugin + mod: floating symbols over the shrines that have none (refill, portal, gem, monster, and a burning skull on fire/exploding/poison) and a minimap icon for every shrine type.

Description (BBCode):

```bbcode
[size=5][b]Shrine Icons[/b][/size]
[b]Two parts, both needed:[/b] a D2RLoader plugin ([i]shrine-icons-1.0.0.zip[/i]) and a D2RMM mod ([i]shrine-icons-mod-1.0.0.zip[/i]). The mod holds the art; the plugin puts it on the shrines.

Tell shrines apart before you walk up to them.

[size=4][b]Features[/b][/size]
[list]
[*][b]Floating symbols on the shrines that have none[/b], like booster shrines: rejuvenation potion (Refilling), town portal scroll (Portal), amethyst (Gem), skull (Monster), and a burning skull on the dangerous Fire, Exploding and Poison shrines. Health and Mana shrines get none, their pool already shows them.
[*][b]Behaves like the booster symbols:[/b] gone when the shrine is used, back when it recharges, with the same faint shimmer.
[*][b]A minimap icon for every shrine type[/b] instead of the same gold ring: item pictures for the shrines above, the classic floating symbol for booster shrines (armor, combat, resists, skill, mana recharge, stamina, experience).
[*][b]Settings[/b] to turn off symbols, minimap icons, booster icons, shimmer or any shrine type; symbol size and height in the mod.
[/list]

[size=4][b]Requirements[/b][/size]
[list]
[*]D2RLoader and D2RMM
[*]Steam 3.3.93787 / 3.3.94995 (on other builds the plugin stays inactive and logs why)
[/list]

[size=4][b]Installation[/b][/size]
[list=1]
[*]Load [i]shrine-icons-1.0.0.zip[/i] in D2RMM, or copy [i]d2rl-shrine-icons.dll[/i] to [i]<game>/d2rloader/plugins/[/i].
[*]Extract [i]shrine-icons-mod-1.0.0.zip[/i] into D2RMM's [i]mods[/i] folder, enable [i]Shrine Icons[/i] and apply.
[/list]
Settings are in [i]d2rloader/config/shrine-icons.toml[/i], written on first launch.

[size=4][b]Settings[/b][/size]
[list]
[*][b]symbols[/b]: floating symbols (default true)
[*][b]minimap[/b]: minimap icon per shrine type (default true)
[*][b]boosters[/b]: minimap icons for booster shrines too (default true)
[*][b]shimmer[/b]: booster shimmer on the shrines with a new symbol (default true)
[*][b]refill, health, mana, portal, gem, fire, monster, exploding, poison[/b]: per shrine type (default true)
[*][b]enabled[/b]: false turns the plugin off
[*]Mod (D2RMM): [b]Symbol size[/b] (1 = booster symbol size), [b]Symbol height[/b] (14 = booster height)
[/list]

[size=4][b]Known limits[/b][/size]
[list]
[*]The mod replaces the HD automap sheets default07 and default13; another mod replacing them conflicts.
[*]Shrines already revealed on the map keep their old icon until the next game.
[*]Legacy graphics show the Skill Shrine animation for the new symbols.
[/list]
```
