# Quickcast over HUD 1.0.0: release texts

## Patch notes

```
**Quickcast over HUD 1.0.0**

**New**

* With keyboard quick cast on, a skill key now casts while the cursor is over the empty art of the bottom bar, at the spot or the monster under the cursor.
* The bar's buttons, belt, orbs and skill buttons keep their keys, and mouse clicks on the bar work as before.
* enabled setting: turns it on or off, also in game with I Hate TOMLs.
```

## GitHub release

- Tag: `quickcast-over-hud-v1.0.0`
- Title: `Quickcast over HUD 1.0.0`
- Asset: `quickcast-over-hud.zip`

Body:

```markdown
**Quickcast over HUD** 1.0.0: quick cast works while the cursor is over the bottom bar.

### Features
- With keyboard quick cast on, the game ignores skill keys while the cursor is over the bottom bar, even over its empty art. With this plugin a quick cast there casts at the spot under the cursor, or at the monster under it.
- Only the bar's empty art is let through: its buttons, belt, orbs and skill buttons keep their keys.
- Mouse clicks on the bar are left as in the game.
- I Hate TOMLs support: turn it on or off in game, applies at once.

### Requirements
- D2RLoader 1.3.1 or later.
- Steam 3.3.93787 or 3.3.94995. The plugin checks the game code when it loads; on another build it stays inactive and says so in its log.

### Install
Load `quickcast-over-hud.zip` in D2RMM, or copy `d2rl-quickcast-over-hud.dll` to `<game>/d2rloader/plugins/`. The setting is in `d2rloader/config/quickcast-over-hud.toml`, written on first launch, or in I Hate TOMLs.
```

## D2RLoader Hub

Name: `Quickcast over HUD`

Summary:

> Quick cast works while the cursor is over the bottom bar's art, at the spot or monster under the cursor.

Description: the GitHub release body above.
