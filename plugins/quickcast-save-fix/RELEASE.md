# Quickcast Save Fix 1.0.0: release texts

## Patch notes

```
**Quickcast Save Fix 1.0.0**

**New**

* First release. Saving during or right after a quick cast (Save and Exit mid-cast) now keeps your own left and right skills instead of the hotkey skill.
* log_fixes setting: writes a line in the D2RLoader log each time a save gets your skills back.
```

## GitHub release

- Tag: `quickcast-save-fix-v1.0.0`
- Title: `Quickcast Save Fix 1.0.0`
- Asset: `quickcast-save-fix-1.0.0.zip`

Body:

```markdown
**Quickcast Save Fix** 1.0.0: Save and Exit during a quick cast no longer saves the hotkey skill on your mouse button.

### What it fixes
With Quick Cast on, a skill hotkey puts its skill on your left or right click for the cast, and the game only puts your own skill back on your next mouse click. Saving before that click (Save and Exit mid-cast, or right after a cast) wrote the hotkey skill as your left/right skill. The plugin gives the save your own skills.

### Requirements
- D2RLoader 1.3.1 or later.
- Single player, or a TCP/IP game you host.
- Steam 3.3.93787 or 3.3.94995. The plugin checks the game code when it loads; on another build it stays inactive and says so in its log.

### Install
Load `quickcast-save-fix-1.0.0.zip` in D2RMM, or copy `d2rl-quickcast-save-fix.dll` to `<game>/d2rloader/plugins/`.
```

## Nexus Mods

Version: `1.0.0`

Name: `Quickcast Save Fix (D2RLoader plugin)`

Summary (short description):

> Save and Exit during a quick cast keeps your own left and right skills instead of saving the hotkey skill.

Description (BBCode):

```bbcode
[size=5][b]Quickcast Save Fix[/b][/size]
A D2RLoader plugin that fixes Quick Cast replacing your left/right skill when you Save and Exit during a cast.

[size=4][b]The bug[/b][/size]
With Quick Cast on, a skill hotkey puts its skill on your left or right click for the cast, and the game only puts your own skill back on your next mouse click. Any save before that click (Save and Exit in the middle of a cast, or right after one) wrote the hotkey skill as your left/right skill.

[size=4][b]The fix[/b][/size]
[list]
[*]The save gets the skill the game was going to put back, so you come back with your own left and right skills.
[*]A save while you keep playing doesn't disturb the cast.
[*]Works alongside Swap Cast.
[/list]

[size=4][b]Requirements[/b][/size]
[list]
[*]D2RLoader 1.3.1 or later
[*]Single player, or a TCP/IP game you host
[*]Steam 3.3.93787 or 3.3.94995. The plugin checks the game code when it loads and stays inactive on other builds.
[/list]

[size=4][b]Installation[/b][/size]
Load the zip with D2RMM, or copy [i]d2rl-quickcast-save-fix.dll[/i] to [i]<game>/d2rloader/plugins/[/i].
Settings are in [i]d2rloader/config/quickcast-save-fix.toml[/i], written on first launch.

[size=4][b]Settings[/b][/size]
[list]
[*][b]log_fixes[/b]: true (default) = a line in the D2RLoader log each time a save gets your skills back
[*][b]enabled[/b]: false turns the plugin off
[/list]
```
