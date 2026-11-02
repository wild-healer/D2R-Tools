# Edge Clicks 1.0.1: release texts

## Patch notes

```
**Edge Clicks 1.0.1**

**New**

* Every setting can be changed in game with I Hate TOMLs and applies at once, enabled included.
* The download is now named edge-clicks.zip, without a version. It holds the DLL alone: the default settings are built in and written on first launch.

**Fixed**

* The plugin no longer reads the game's code through ReadProcessMemory, which made an antivirus flag the DLL.
```

## GitHub release

- Tag: `edge-clicks-v1.0.1`
- Title: `Edge Clicks 1.0.1`
- Asset: `edge-clicks.zip`

Body:

```markdown
**Edge Clicks** 1.0.1

### New
- Every setting can be changed in game with I Hate TOMLs and applies at once, enabled included.
- The download is now named `edge-clicks.zip`, without a version. It holds the DLL alone: the default settings are built in and written on first launch.

### Fixed
- The plugin no longer reads the game's code through ReadProcessMemory, which made an antivirus flag the DLL.

### Install
Load `edge-clicks.zip` in D2RMM, or copy `d2rl-edge-clicks.dll` to `<game>/d2rloader/plugins/`. Settings are unchanged.
```

# Edge Clicks 1.0.0

## Patch notes

```
**Edge Clicks 1.0.0**

**New**

* Clicking the black void past the edge of the map walks you toward the click, as in the classic graphics.
* A click behind a wall, or too far for the game to find a way, walks you as far toward it as the ground allows, sliding along the wall or going around it.
* log_moves setting: writes each corrected click to the plugin log.
```

## GitHub release

- Tag: `edge-clicks-v1.0.0`
- Title: `Edge Clicks 1.0.0`
- Asset: `edge-clicks-1.0.0.zip`

Body:

```markdown
**Edge Clicks** 1.0.0: clicks on the black void and past walls move you.

### Features
- **Click the void, still walk.** With the 3D graphics, a click on the black area past the edge of the map is ignored by the game. Now your character walks toward it, like in the classic graphics.
- **No more stopping dead at a wall.** When the spot you click is behind a wall or too far for the game to find a way, your character walks as far toward it as the ground allows, sliding along the wall or going around it.
- **Everything else unchanged.** Clicks the game already handles, attacks and monsters are left alone.

### Requirements
- D2RLoader 1.3.1 or later.
- Single player, or a TCP/IP game you host.
- Steam 3.3.93787 or 3.3.94995. The plugin checks the game code when it loads; on another build it stays inactive and says so in its log.

### Install
Load `edge-clicks-1.0.0.zip` in D2RMM, or copy `d2rl-edge-clicks.dll` to `<game>/d2rloader/plugins/`. Settings are in `d2rloader/config/edge-clicks.toml`, written on first launch.
```

## D2RLoader Hub

Name: `Edge Clicks`

Summary:

> Clicking the void past the map edge or a wall walks you as far toward the click as the ground allows.

Description:

```markdown
**Edge Clicks**: clicks on the black void and past walls move you.

### Features
- **Click the void, still walk.** With the 3D graphics, a click on the black area past the edge of the map is ignored by the game. Now your character walks toward it, like in the classic graphics.
- **No more stopping dead at a wall.** When the spot you click is behind a wall or too far for the game to find a way, your character walks as far toward it as the ground allows, sliding along the wall or going around it.
- **Everything else unchanged.** Clicks the game already handles, attacks and monsters are left alone.
- **I Hate TOMLs support.** Change the settings in game; they apply at once.

### Requirements
- D2RLoader 1.3.1 or later.
- Single player, or a TCP/IP game you host.
- Steam 3.3.93787 or 3.3.94995. The plugin checks the game code when it loads; on another build it stays inactive and says so in its log.

### Install
Load `edge-clicks.zip` in D2RMM, or copy `d2rl-edge-clicks.dll` to `<game>/d2rloader/plugins/`. Settings are in `d2rloader/config/edge-clicks.toml`, written on first launch, or in I Hate TOMLs.
```
