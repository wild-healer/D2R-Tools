# Cube Overflow 1.1.0: release texts

## GitHub release

- Tag: `cube-overflow-v1.1.0`
- Title: `Cube Overflow 1.1.0`
- Asset: `cube-overflow-1.1.0.zip`

Body:

```markdown
**Cube Overflow** 1.1.0: purchases, gambling and quest rewards overflow into the Horadric Cube too, and overflow pickups no longer bounce.

### New
- **Purchases and gambling** (`purchases_and_rewards`, on by default): buy or gamble with a full inventory and the item goes into the Cube.
- **Quest rewards** (same option): a reward that would drop at your feet because the inventory is full goes into the Cube.

### Fixed
- Picking up an item that goes into the Cube no longer plays the "failed pickup" sound with the item jumping up and falling back down.

### Requirements
- D2RLoader 1.3.1 or later.
- The Horadric Cube must be in your inventory (not the stash).
- Single player, or a TCP/IP game you host. There, pickups and quest rewards work for every player, and purchases and gambling only for you. It does nothing in a game you joined.
- Steam 3.3.93787 or 3.3.94995. The plugin checks the game code when it loads; on another build it stays inactive and says so in its log.

### Install
Load `cube-overflow-1.1.0.zip` in D2RMM, or copy `d2rl-cube-overflow.dll` to `<game>/d2rloader/plugins/`. Settings are in `d2rloader/config/cube-overflow.toml`, written on first launch. Edit that copy, not the one in the zip, and restart the game after changing it. An existing config keeps working: the new option defaults to on.

### Log
`d2rloader/logs/cube-overflow.log` lists every item sent to the Cube.
```

## Nexus Mods

Version: `1.1.0`

Summary (short description):

> Items you pick up, buy, gamble or get as quest rewards with a full inventory go into the Horadric Cube.

Changelog (1.1.0):

```
- New: purchases and gambling with a full inventory go into the Cube (purchases_and_rewards, on by default)
- New: quest rewards with a full inventory go into the Cube instead of dropping (same option)
- Fixed: overflow pickups no longer play the failed pickup sound with the item bouncing
```

Description (BBCode):

```bbcode
[size=5][b]Cube Overflow[/b][/size]
A D2RLoader plugin that sends items into your Horadric Cube when your inventory is full.

[size=4][b]Features[/b][/size]
[list]
[*][b]Full inventory? Into the Cube.[/b] Pick up an item that has no room in your inventory and it goes into the Cube instead, at a free spot the game picks itself.
[*][b]Purchases, gambling and quest rewards too:[/b] buy, gamble or receive a quest reward with a full inventory and it lands in the Cube.
[*][b]Nothing changes when there's room.[/b] Stacking, potions to the belt and gold work as usual and come first.
[*][b]Full Cube too?[/b] The game behaves as in the base game ("no room").
[*][b]Cursor pickups too:[/b] items the game would put on your cursor because the inventory is full also go into the Cube.
[/list]

[size=4][b]Requirements[/b][/size]
[list]
[*]D2RLoader 1.3.1 or later
[*]The Horadric Cube in your inventory (not the stash)
[*]Single player, or a TCP/IP game you host (pickups and quest rewards for every player, purchases and gambling for you only); does nothing in a game you joined
[*]Steam 3.3.93787 or 3.3.94995. The plugin checks the game code when it loads and stays inactive on other builds.
[/list]

[size=4][b]Installation[/b][/size]
Load the zip with D2RMM, or copy [i]d2rl-cube-overflow.dll[/i] to [i]<game>/d2rloader/plugins/[/i].
Settings are in [i]d2rloader/config/cube-overflow.toml[/i], written on first launch.

[size=4][b]Settings[/b][/size]
[list]
[*][b]cursor_pickups[/b]: true (default) = cursor pickups with a full inventory go to the Cube too; false = only inventory pickups
[*][b]purchases_and_rewards[/b]: true (default) = purchases, gambling and quest rewards with a full inventory go to the Cube too
[*][b]enabled[/b]: false turns the plugin off
[/list]

[size=4][b]Log[/b][/size]
[i]d2rloader/logs/cube-overflow.log[/i] lists every item sent to the Cube.
```
