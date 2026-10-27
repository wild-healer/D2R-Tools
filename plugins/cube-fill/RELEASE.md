# Cube Fill 1.0.0: release texts

## GitHub release

- Tag: `cube-fill-v1.0.0`
- Title: `Cube Fill 1.0.0`
- Asset: `cube-fill-1.0.0.zip`

Body:

```markdown
First release of **Cube Fill**, a D2RLoader plugin that shows what is in your Horadric Cube right on the cube in your inventory.

### Features
- **See inside the Cube.** The items in the Cube are drawn small over its icon, with their real pictures, in their exact cube spots. Free cells show as dim tiles.
- **Or a count.** `display = "badge"` shows a small "7/12" (cells used out of 12) in a corner of the icon instead, red when the Cube is full. `"sprites-badge"` shows both.
- **Or a mini map.** `"grid"` draws a 3x4 tile map with each item's shape, `"both"` adds the badge.
- **Adjustable.** Darkness of the box behind the pictures (`picture_background`), badge corner, size, used or free cells.

### Requirements
- D2RLoader 1.3.1 or later.
- HD graphics, keyboard and mouse layout. The Cube must be in your inventory (not the stash).
- Pictures: Steam 3.3.93787 or 3.3.94995. The plugin checks the game code when it loads; on another build it shows the badge instead and says so in its log.

### Install
Load `cube-fill-1.0.0.zip` in D2RMM, or copy `d2rl-cube-fill.dll` to `<game>/d2rloader/plugins/`. Settings are in `d2rloader/config/cube-fill.toml`, written on first launch. Edit that copy, not the one in the zip, and restart the game after changing it.

### Log
`d2rloader/logs/cube-fill.log` says which display is active.
```

## Nexus Mods

Name: `Cube Fill (D2RLoader plugin)`

Summary (short description):

> See the items inside your Horadric Cube right on its inventory icon.

Description (BBCode):

```bbcode
[size=5][b]Cube Fill[/b][/size]
A D2RLoader plugin that shows what is in your Horadric Cube right on the cube in your inventory.

[size=4][b]Features[/b][/size]
[list]
[*][b]See inside the Cube.[/b] The items in the Cube are drawn small over its icon, with their real pictures, in their exact cube spots. Free cells show as dim tiles.
[*][b]Or a count.[/b] A small "7/12" badge (cells used out of 12) in a corner of the icon, red when the Cube is full.
[*][b]Or a mini map.[/b] A 3x4 tile map with each item's shape.
[/list]

[size=4][b]Requirements[/b][/size]
[list]
[*]D2RLoader 1.3.1 or later
[*]HD graphics, keyboard and mouse layout; the Cube in your inventory (not the stash)
[*]Item pictures: Steam 3.3.93787 or 3.3.94995. The plugin checks the game code when it loads and shows the badge instead on other builds.
[/list]

[size=4][b]Installation[/b][/size]
Load the zip with D2RMM, or copy [i]d2rl-cube-fill.dll[/i] to [i]<game>/d2rloader/plugins/[/i].
Settings are in [i]d2rloader/config/cube-fill.toml[/i], written on first launch.

[size=4][b]Settings[/b][/size]
[list]
[*][b]display[/b]: "sprites" (default) = item pictures; "sprites-badge" = pictures and badge; "badge" = count only; "grid" = tile map; "both" = tile map and badge
[*][b]picture_background[/b]: 25, 50 (default) or 75 = darkness of the box behind the pictures
[*][b]show[/b]: "used" (7/12) or "free" (5)
[*][b]show_empty[/b] / [b]show_full[/b]: badge on an empty / full Cube
[*][b]corner[/b], [b]font_size[/b]: badge position and size
[*][b]enabled[/b]: false turns the plugin off
[/list]
```
