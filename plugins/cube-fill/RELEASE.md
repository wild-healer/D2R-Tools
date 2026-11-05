# Cube Fill 1.1.0: release texts

## Patch notes

```
**Cube Fill 1.1.0**

**New**

* The item pictures follow the cube's real size, read from the game's inventory.txt, so cube mods that resize the cube (6x4 and others) just work. A bigger cube gets smaller cells so it still fits on the icon.

**Fixed**

* With a cube that isn't 3x4, the badge and the mini map no longer show wrong counts; the item pictures show in their place.
```

## GitHub release

- Tag: `cube-fill-v1.1.0`
- Title: `Cube Fill 1.1.0`
- Asset: `cube-fill.zip`

Body:

```markdown
**Cube Fill** 1.1.0: what is in your Horadric Cube, right on the cube in your inventory.

### Features
- **See inside the Cube.** The items in the Cube are drawn small over its icon, with their real pictures, in their exact cube spots. Free cells show as dim tiles.
- **Or a count.** A small "7/12" badge (cells used out of 12) in a corner of the icon, red when the Cube is full. Pictures and badge can be shown together.
- **Any cube size.** The pictures follow the cube's real size, so mods that resize the cube just work.
- **Or a mini map.** A 3x4 tile map with each item's shape, with or without the badge.
- **Adjustable.** Darkness of the box behind the pictures, badge corner, size, used or free cells.
- **I Hate TOMLs support.** Change the settings in game; the display and background apply at once.

### Requirements
- D2RLoader 1.3.1 or later.
- HD graphics, keyboard and mouse layout. The Cube must be in your inventory (not the stash).
- Pictures: Steam 3.3.93787 or 3.3.94995. The plugin checks the game code when it loads; on another build it shows the badge instead and says so in its log.

### Install
Load `cube-fill.zip` in D2RMM, or copy `d2rl-cube-fill.dll` to `<game>/d2rloader/plugins/`. Settings are in `d2rloader/config/cube-fill.toml`, written on first launch, or in I Hate TOMLs.
```

## D2RLoader Hub

Name: `Cube Fill`

Summary:

> Shows what is in the cube on its inventory icon: the items drawn small, a count badge or a mini grid.

Description: the GitHub release body above.
