# Cube Overflow 1.1.1: release texts

## Patch notes

```
**Cube Overflow 1.1.1**

**New**

* I Hate TOMLs support: every setting can be changed in game and applies at once, without a restart.
* The zip holds the DLL alone, named cube-overflow.zip. Its default settings are built in.
```

## GitHub release

- Tag: `cube-overflow-v1.1.1`
- Title: `Cube Overflow 1.1.1`
- Asset: `cube-overflow.zip`

Body:

```markdown
**Cube Overflow** 1.1.1: settings in game with I Hate TOMLs.

### New
- I Hate TOMLs support: every setting can be changed in game and applies at once, without a restart.
- The zip holds the DLL alone, named `cube-overflow.zip`. Its default settings are built in.

### Features
- Pick up an item that has no room in your inventory and it goes into the Horadric Cube instead, at a free spot the game picks itself.
- Purchases, gambling and quest rewards with a full inventory go into the Cube too (`purchases_and_rewards`).
- Cursor pickups too (`cursor_pickups`): items the game would put on your cursor because the inventory is full.
- Nothing changes when there's room: stacking, potions to the belt and gold work as usual and come first.

### Requirements
- D2RLoader 1.3.1 or later. I Hate TOMLs is optional.
- The Horadric Cube must be in your inventory (not the stash).
- Single player, or a TCP/IP game you host. There, pickups and quest rewards work for every player, and purchases and gambling only for you. It does nothing in a game you joined.
- Steam 3.3.93787 or 3.3.94995. The plugin checks the game code when it loads; on another build it stays inactive and says so in its log.

### Install
Load `cube-overflow.zip` in D2RMM, or copy `d2rl-cube-overflow.dll` to `<game>/d2rloader/plugins/`. Settings are in `d2rloader/config/cube-overflow.toml`, written on first launch, or in I Hate TOMLs.

### Log
`d2rloader/logs/cube-overflow.log` lists every item sent to the Cube.
```

## D2RLoader Hub

Name: `Cube Overflow`

Version: `1.1.1`

Summary:

> When your inventory is full, pickups, purchases, gambling and quest rewards go into the cube instead.

Description: the GitHub release body above.

# Cube Overflow 1.1.0: patch notes

```
**Cube Overflow 1.1.0**

**New**

* purchases_and_rewards setting (on by default): items you buy, gamble or get as quest rewards with a full inventory go into the Cube. Purchases and gambling only work for you in a game you host, while quest rewards work for every player.

**Fixed**

* Picking up an item that goes into the Cube no longer plays the "failed pickup" sound with the item jumping up and falling back down.
```
