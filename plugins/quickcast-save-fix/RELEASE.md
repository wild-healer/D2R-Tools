# Quickcast Save Fix 1.0.1: release texts

## Patch notes

```
**Quickcast Save Fix 1.0.1**

**New**

* Every setting can be changed in game with I Hate TOMLs and applies at once, enabled included.
* The download is now named quickcast-save-fix.zip, without a version. It holds the DLL alone: the default settings are built in and written on first launch.

**Fixed**

* The plugin no longer reads the game's code through ReadProcessMemory, which made an antivirus flag the DLL.
```

## GitHub release

- Tag: `quickcast-save-fix-v1.0.1`
- Title: `Quickcast Save Fix 1.0.1`
- Asset: `quickcast-save-fix.zip`

Body:

```markdown
**Quickcast Save Fix** 1.0.1

### New
- Every setting can be changed in game with I Hate TOMLs and applies at once, enabled included.
- The download is now named `quickcast-save-fix.zip`, without a version. It holds the DLL alone: the default settings are built in and written on first launch.

### Fixed
- The plugin no longer reads the game's code through ReadProcessMemory, which made an antivirus flag the DLL.

### Install
Load `quickcast-save-fix.zip` in D2RMM, or copy `d2rl-quickcast-save-fix.dll` to `<game>/d2rloader/plugins/`. Settings are unchanged.
```

# Quickcast Save Fix 1.0.0

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

## D2RLoader Hub

Name: `Quickcast Save Fix`

Summary:

> Saving during or right after a quick cast keeps your own left and right skills.

Description:

```markdown
**Quickcast Save Fix**: Save and Exit during a quick cast no longer saves the hotkey skill on your mouse button.

### The bug
With Quick Cast on, a skill hotkey puts its skill on your left or right click for the cast, and the game only puts your own skill back on your next mouse click. Any save before that click (Save and Exit in the middle of a cast, or right after one) wrote the hotkey skill as your left/right skill.

### The fix
- **Your own skills are saved.** The save gets the skill the game was going to put back, so you come back with your own left and right skills.
- **Casts carry on.** A save while you keep playing doesn't disturb the cast.
- **Works alongside Swap Cast.**
- **I Hate TOMLs support.** Change the settings in game; they apply at once.

### Requirements
- D2RLoader 1.3.1 or later.
- Single player, or a TCP/IP game you host.
- Steam 3.3.93787 or 3.3.94995. The plugin checks the game code when it loads; on another build it stays inactive and says so in its log.

### Install
Load `quickcast-save-fix.zip` in D2RMM, or copy `d2rl-quickcast-save-fix.dll` to `<game>/d2rloader/plugins/`. Settings are in `d2rloader/config/quickcast-save-fix.toml`, written on first launch, or in I Hate TOMLs.
```
