# Orb Values 1.0.0: release texts

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
