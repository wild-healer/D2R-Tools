# Enchant Everyone 1.0.0: release texts

## Patch notes

```
**Enchant Everyone 1.0.0**

**New**

* Casting Enchant also enchants every friendly unit around you: yourself, your party, mercenaries, summons and revives.
* radius_yards setting: how far around you Enchant reaches, 20 yards by default.
* players setting: turn off to leave the other players of your party out.
* minions setting: turn off to leave mercenaries, summons and revives out.
* Every setting can be changed in game with I Hate TOMLs and applies at once.
```

## GitHub release

- Tag: `enchant-everyone-v1.0.0`
- Title: `Enchant Everyone 1.0.0`
- Asset: `enchant-everyone.zip`

Body:

```markdown
**Enchant Everyone** 1.0.0

First release.

### New
- Casting Enchant also enchants every friendly unit around you: yourself, your party, mercenaries, summons and revives.
- `radius_yards`: how far around you Enchant reaches (20 yards by default, 1 to 25).
- `players` and `minions`: leave party members, or mercenaries/summons/revives, out.
- Every setting can be changed in game with I Hate TOMLs and applies at once.

### Install
Load `enchant-everyone.zip` in D2RMM, or copy `d2rl-enchant-everyone.dll` to `<game>/d2rloader/plugins/`.
```

## D2RLoader Hub

Name: `Enchant Everyone`

Summary:

> Casting Enchant enchants every friendly unit around you: you, your party, mercenaries and summons.

Description:

```markdown
**Enchant Everyone**: one Enchant for the whole army.

### Features
- **Everyone around you.** When you cast Enchant, your target gets it as usual, and so does every friendly unit near you: yourself, party members, your mercenary, summons and revives, and your party's mercenaries and summons.
- **One cast, one mana cost.** Mana is spent once, for the cast itself.
- **Only friends.** Enemies and town NPCs never get it. Other players get it only when they are in your party, like the game's own Enchant.
- **Adjustable radius.** 20 yards by default, from 1 to 25.
- **I Hate TOMLs support.** Change the settings in game; they apply at once.

### Requirements
- D2RLoader 1.3.1 or later.
- Single player, or a TCP/IP game you host. It does nothing in a game you joined: the host's game decides who gets Enchant.
- Steam 3.3.94995. It uses the game's own code, checked when the plugin loads; on another build it stays inactive and says so in its log.

### Install
Load `enchant-everyone.zip` in D2RMM, or copy `d2rl-enchant-everyone.dll` to `<game>/d2rloader/plugins/`. Settings are in `d2rloader/config/enchant-everyone.toml`, written on first launch.

### Settings
- `enabled`: false turns the plugin off.
- `radius_yards`: how far around the caster Enchant reaches (20).
- `players`: false leaves the other players of your party out.
- `minions`: false leaves mercenaries, summons and revives out.

### Log
`d2rloader/logs/enchant-everyone.log` has one line per cast: how many more units it enchanted.
```
