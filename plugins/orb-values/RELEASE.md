# Orb Values 1.0.1: release texts

## Patch notes

```
**Orb Values 1.0.1**

**New**

* Every setting can be changed in game with I Hate TOMLs and applies at once, enabled included. Turned off, the orbs behave as in the stock game again.
* The download is now named orb-values.zip, without a version. It holds the DLL alone: the default settings are built in and written on first launch.

**Fixed**

* The plugin no longer reads the game's code through ReadProcessMemory, and is built like the other plugins to avoid antivirus false positives.
* The numbers no longer write their position to the plugin log.
```

## GitHub release

- Tag: `orb-values-v1.0.1`
- Title: `Orb Values 1.0.1`
- Asset: `orb-values.zip`

Body:

```markdown
**Orb Values** 1.0.1

### New
- Every setting can be changed in game with I Hate TOMLs and applies at once, enabled included. Turned off, the orbs behave as in the stock game again.
- The download is now named `orb-values.zip`, without a version. It holds the DLL alone: the default settings are built in and written on first launch.

### Fixed
- The plugin no longer reads the game's code through ReadProcessMemory, and is built like the other plugins to avoid antivirus false positives.
- The numbers no longer write their position to the plugin log.

### Install
Load `orb-values.zip` in D2RMM, or copy `d2rl-orb-values.dll` to `<game>/d2rloader/plugins/`. Settings are unchanged.
```

## D2RLoader Hub

Name: `Orb Values`

Summary:

> Life and mana numbers always shown in the middle of the orbs, without the "Life:" and "Mana:" prefixes.

Description:

```markdown
**Orb Values**: life and mana numbers always shown in the middle of the orbs, without their prefix.

### Features
- The numbers are always visible: no more hovering or clicking the orbs. The game still hides them under its own panels, as before.
- Clicking an orb does nothing anymore. The game's own show/hide setting is left untouched, so turning the plugin off brings back the old behavior.
- Only `123 / 456` is left: the "Life:" and "Mana:" prefixes are removed, in every language.
- Each number is centered in its orb, drawn over the liquid, whatever its length.
- offset_x / offset_y settings to nudge both numbers.
- **I Hate TOMLs support.** Change the settings in game; they apply at once.

### Requirements
- D2RLoader 1.3.1 or later.
- Steam 3.3.93787 or 3.3.94995. The plugin checks the game code when it loads; on another build it stays inactive and says so in its log.
- Made for the mouse and keyboard HUD.

### Install
Load `orb-values.zip` in D2RMM, or copy `d2rl-orb-values.dll` to `<game>/d2rloader/plugins/`. Settings are in `d2rloader/config/orb-values.toml`, written on first launch, or in I Hate TOMLs.
```

# Orb Values 1.0.0

## Patch notes

```
**Orb Values 1.0.0**

**New**

* Life and mana numbers are always shown, without hovering or clicking the orbs.
* Clicking an orb no longer hides or shows the numbers.
* The "Life:" and "Mana:" prefixes are removed, in every language.
* Each number sits in the middle of its orb, over the liquid, and stays centered whatever its length.
* offset_x and offset_y settings: move both numbers from the middle of their orb, in HUD units.
```

## GitHub release

- Tag: `orb-values-v1.0.0`
- Title: `Orb Values 1.0.0`
- Asset: `orb-values-1.0.0.zip`

Body:

```markdown
**Orb Values** 1.0.0: life and mana numbers always shown in the middle of the orbs, without their prefix.

### Features
- The numbers are always visible: no more hovering or clicking the orbs. The game still hides them under its own panels, as before.
- Clicking an orb does nothing anymore. The game's own show/hide setting is left untouched, so removing the plugin brings back the old behavior.
- Only `123 / 456` is left: the "Life:" and "Mana:" prefixes are removed, in every language.
- Each number is centered in its orb, drawn over the liquid, whatever its length.
- offset_x / offset_y settings to nudge both numbers.

### Requirements
- D2RLoader 1.3.1 or later.
- Steam 3.3.93787 or 3.3.94995. The plugin checks the game code when it loads; on another build it stays inactive and says so in its log.
- Made for the mouse and keyboard HUD.

### Install
Load `orb-values-1.0.0.zip` in D2RMM, or copy `d2rl-orb-values.dll` to `<game>/d2rloader/plugins/`. Settings are in `d2rloader/config/orb-values.toml`, written on first launch.
```

## D2RLoader Hub

Name: `Orb Values`

Summary:

> Life and mana numbers always shown in the middle of the orbs, without the "Life:" and "Mana:" prefixes.

Description: the GitHub release body above.
