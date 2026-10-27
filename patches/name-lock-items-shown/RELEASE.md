# Name Lock With Items Shown 1.0.0: release texts

## GitHub release

- Tag: `name-lock-items-shown-v1.0.0`
- Title: `Name Lock With Items Shown 1.0.0`
- Assets: `name-lock-items-shown.json`, `name-lock-items-shown-1.0.0.zip`

Body:

```markdown
First release of **Name Lock With Items Shown**, a D2RLoader memory patch that keeps the hold-click target lock on monsters while item labels are shown.

### Fixed (vanilla behaviour)
- With Show Items toggled on, clicking and holding on a monster no longer keeps you attacking it once the cursor moves off. The game cleared the lock every time it repeated the action while labels were shown. Now the lock holds: melee follows the monster, ranged moves into range and spells stay on target.
- Clicking item labels still picks items up as before.

### Requirements
- D2RLoader 1.3.1 or later (memory patch support).
- Steam 3.3.93787 (tested). On other builds the patch's byte check fails and D2RLoader skips it, leaving the game unchanged.

### Install
Copy `name-lock-items-shown.json` to `<game>/d2rloader/patches/`, then start the game. The D2RLoader log lists it as `[PATCH] Name Lock With Items Shown`. Delete the file to undo the patch.

### How it works
One byte at RVA `0xFE4FD`: the conditional jump that skips "clear the lock when labels are shown and the target is not an item" becomes an unconditional jump.
```

## Nexus Mods

Name: `Name Lock With Items Shown (D2RLoader patch)`

Summary (short description):

> Keeps the hold-click target lock on monsters working while Show Items is toggled on.

Description (BBCode):

```bbcode
[size=5][b]Name Lock With Items Shown[/b][/size]
A D2RLoader memory patch, with no plugin and no DLL, that fixes a vanilla D2R behaviour: the hold-click target lock stops working while item labels are shown.

[size=4][b]The problem[/b][/size]
Click a monster and keep the button held, and your character keeps attacking or casting at it even after the cursor moves off. Melee follows it, ranged moves into range, spells stay on target. With [i]Show Items[/i] toggled on, that lock never holds: the game clears it every time it repeats the action while labels are shown.

[size=4][b]The fix[/b][/size]
[list]
[*]The lock now holds while item labels are shown, whether Show Items is toggled or held.
[*]Clicking item labels still picks items up as before.
[*]One changed byte. The patch checks the original bytes first and is skipped on any game build it doesn't match.
[/list]

[size=4][b]Requirements[/b][/size]
[list]
[*]D2RLoader 1.3.1 or later
[*]Steam 3.3.93787 (tested). Other builds: the patch is skipped and the game is left unchanged.
[/list]

[size=4][b]Installation[/b][/size]
Copy [i]name-lock-items-shown.json[/i] to [i]<game>/d2rloader/patches/[/i]. The D2RLoader log lists it as [i][PATCH] Name Lock With Items Shown[/i].
To uninstall, delete the file.
```
