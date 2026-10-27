# Edge Clicks 1.0.0: release texts

## GitHub release

- Tag: `edge-clicks-v1.0.0`
- Title: `Edge Clicks 1.0.0`
- Asset: `edge-clicks-1.0.0.zip`

Body:

```markdown
First release of **Edge Clicks**, a D2RLoader plugin that makes clicks on the black void and past walls move you.

### Features
- **Click the void, still walk.** With the 3D graphics, a click on the black area past the edge of the map is ignored by the game. Now your character walks toward it, like in the classic graphics.
- **No more stopping dead at a wall.** When the spot you click is behind a wall or too far for the game to find a way, your character walks as far toward it as the ground allows, sliding along the wall or going around it.
- **Everything else unchanged.** Clicks the game already handles, attacks and monsters are left alone.

### Requirements
- D2RLoader 1.3.1 or later.
- Single player, or a TCP/IP game you host.
- Steam 3.3.93787 or 3.3.94995. The plugin checks the game code when it loads; on another build it stays inactive and says so in its log.

### Install
Load `edge-clicks-1.0.0.zip` in D2RMM, or copy `d2rl-edge-clicks.dll` to `<game>/d2rloader/plugins/`. Settings are in `d2rloader/config/edge-clicks.toml`, written on first launch. Edit that copy, not the one in the zip, and restart the game after changing it.

### Log
`d2rloader/logs/edge-clicks.log` lists every click the plugin corrected.
```

## Nexus Mods

Name: `Edge Clicks (D2RLoader plugin)`

Summary (short description):

> Clicks on the black void or past a wall move you as far as the ground allows.

Description (BBCode):

```bbcode
[size=5][b]Edge Clicks[/b][/size]
A D2RLoader plugin that makes clicks on the black void and past walls move you.

[size=4][b]Features[/b][/size]
[list]
[*][b]Click the void, still walk.[/b] With the 3D graphics, a click on the black area past the edge of the map is ignored by the game. Now your character walks toward it, like in the classic graphics.
[*][b]No more stopping dead at a wall.[/b] When the spot you click is behind a wall or too far for the game to find a way, your character walks as far toward it as the ground allows, sliding along the wall or going around it.
[*][b]Everything else unchanged.[/b] Clicks the game already handles, attacks and monsters are left alone.
[/list]

[size=4][b]Requirements[/b][/size]
[list]
[*]D2RLoader 1.3.1 or later
[*]Single player, or a TCP/IP game you host
[*]Steam 3.3.93787 or 3.3.94995. The plugin checks the game code when it loads and stays inactive on other builds.
[/list]

[size=4][b]Installation[/b][/size]
Load the zip with D2RMM, or copy [i]d2rl-edge-clicks.dll[/i] to [i]<game>/d2rloader/plugins/[/i].
Settings are in [i]d2rloader/config/edge-clicks.toml[/i], written on first launch.

[size=4][b]Settings[/b][/size]
[list]
[*][b]log_moves[/b]: true (default) = write each corrected click to the log
[*][b]enabled[/b]: false turns the plugin off
[/list]

[size=4][b]Log[/b][/size]
[i]d2rloader/logs/edge-clicks.log[/i] lists every click the plugin corrected.
```
