# Cube Empty 1.0.0: release texts

## GitHub release

- Tag: `cube-empty-v1.0.0`
- Title: `Cube Empty 1.0.0`
- Asset: `cube-empty-1.0.0.zip`

Body:

```markdown
First release of **Cube Empty**, a D2RLoader plugin that empties the Horadric Cube into your inventory with one click.

### Features
- **Middle-click the Cube** in your inventory: everything inside moves to free spots in your inventory. The Cube doesn't need to be open.
- **Items that don't fit stay in the Cube**, and your character says "impossible", so you know something is left.
- **Choose the button**: middle, back or forward side button (`button`).
- **Click anywhere** instead of on the Cube if you prefer (`require_hover = false`).

### Requirements
- D2RLoader 1.3.1 or later.
- Single player, or a TCP/IP game you host. Items can't be moved in a game you joined.
- Tested on Steam 3.3.94995. Moving items uses only D2RLoader's item calls and works on any build. The "impossible" voice line uses the game's own sound call, checked when the plugin loads (Steam 3.3.93787 and 3.3.94995); on other builds the character stays silent and the Cube still empties.

### Install
Load `cube-empty-1.0.0.zip` in D2RMM, or copy `d2rl-cube-empty.dll` to `<game>/d2rloader/plugins/`. Settings are in `d2rloader/config/cube-empty.toml`, written on first launch. Edit that copy, not the one in the zip, and restart the game after changing it.

### Known limits
- The click counts as "on the Cube" while the Cube's tooltip is showing, or the mouse is still close to where it appeared.
- Items are placed by the game's automatic placement, top-left first, the same as picking them up.
```

## Nexus Mods

Name: `Cube Empty (D2RLoader plugin)`

Summary (short description):

> Middle-click the Horadric Cube to move everything inside it into your inventory.

Description (BBCode):

```bbcode
[size=5][b]Cube Empty[/b][/size]
A D2RLoader plugin that empties the Horadric Cube into your inventory with one click.

[size=4][b]Features[/b][/size]
[list]
[*][b]Middle-click the Cube[/b] in your inventory: everything inside moves to free spots in your inventory. The Cube doesn't need to be open.
[*][b]Items that don't fit stay in the Cube[/b], and your character says "impossible", so you know something is left.
[*][b]Choose the button:[/b] middle, back or forward side button.
[*][b]Click anywhere[/b] instead of on the Cube if you prefer.
[/list]

[size=4][b]Requirements[/b][/size]
[list]
[*]D2RLoader 1.3.1 or later
[*]Single player, or a TCP/IP game you host (items can't be moved in a game you joined)
[*]Tested on Steam 3.3.94995. Moving items works on any build; the "impossible" voice line needs Steam 3.3.93787 or 3.3.94995, elsewhere the character stays silent.
[/list]

[size=4][b]Installation[/b][/size]
Load the zip with D2RMM, or copy [i]d2rl-cube-empty.dll[/i] to [i]<game>/d2rloader/plugins/[/i].
Settings are in [i]d2rloader/config/cube-empty.toml[/i], written on first launch.

[size=4][b]Settings[/b][/size]
[list]
[*][b]button[/b]: "middle" (default), "back" or "forward"
[*][b]require_hover[/b]: true (default) = only with the mouse over the Cube; false = a click anywhere in game empties it
[*][b]enabled[/b]: false turns the plugin off
[/list]

[size=4][b]Known limits[/b][/size]
[list]
[*]The click counts as "on the Cube" while the Cube's tooltip is showing, or the mouse is still close to where it appeared.
[*]Items are placed by the game's automatic placement, top-left first.
[/list]
```
