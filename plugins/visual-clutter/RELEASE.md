# Visual Clutter 1.0.3: release texts

## Patch notes

```
**Visual Clutter 1.0.3**

**Fixed**

* Visual Clutter works on game 3.4.95041. The versions setting is removed; an old config that still has it loads fine.
* In I Hate TOMLs, "Other players" is now "Allies", and the other rows and columns say "your merc" and "your summons".
* Any merc that is not yours counts as an ally, including extra mercs a plugin gives other players.
* With several mercs, another merc's aura on one of your mercs counts as from an ally, so From allies on Your merc hides it.
* Auras no longer go back to counting as a unit's own after the game redraws them, for example when you change a setting or move to another area.

**Removed**

* versions setting: the plugin checks the game's code at load instead.
```

## GitHub release

- Tag: `visual-clutter-v1.0.3`
- Title: `Visual Clutter 1.0.3`
- Asset: `visual-clutter.zip`

Body:

```markdown
**Visual Clutter** 1.0.3

### Fixed
- Visual Clutter works on game 3.4.95041. The versions setting is removed; an old config that still has it loads fine.
- In I Hate TOMLs, "Other players" is now "Allies", and the other rows and columns say "your merc" and "your summons".
- Any merc that is not yours counts as an ally, including extra mercs a plugin gives other players.
- With several mercs, another merc's aura on one of your mercs counts as from an ally, so From allies on Your merc hides it.
- Auras no longer go back to counting as a unit's own after the game redraws them, for example when you change a setting or move to another area.

### Removed
- `versions` setting: the plugin checks the game's code at load instead.

### Install
Load `visual-clutter.zip` in D2RMM, or copy `d2rl-visual-clutter.dll` to `<game>/d2rloader/plugins/`. Settings are unchanged.
```

# Visual Clutter 1.0.2: release texts

## Patch notes

```
**Visual Clutter 1.0.2**

**Fixed**

* Hiding Firestorm now hides Hellfire Torch's Firestorm, which the game makes with Diablo's fire wall missiles, not the druid skill's.
* Plugins that check the start of the game's overlay function before using it, like LTBL, now load next to Visual Clutter.
```

## GitHub release

- Tag: `visual-clutter-v1.0.2`
- Title: `Visual Clutter 1.0.2`
- Asset: `visual-clutter.zip`

Body:

```markdown
**Visual Clutter** 1.0.2

### Fixed
- Hiding Firestorm now hides Hellfire Torch's Firestorm, which the game makes with Diablo's fire wall missiles, not the druid skill's.
- Plugins that check the start of the game's overlay function before using it, like LTBL, now load next to Visual Clutter.

### Install
Load `visual-clutter.zip` in D2RMM, or copy `d2rl-visual-clutter.dll` to `<game>/d2rloader/plugins/`. Settings are unchanged.
```

# Visual Clutter 1.0.1: release texts

## Patch notes

```
**Visual Clutter 1.0.1**

**New**

* Every setting can be changed in game with I Hate TOMLs and applies at once, enabled included.
* The aura, shout, skill and charge orb choices show in I Hate TOMLs as tables of check boxes, one row per kind of unit and one column per caster (needs I Hate TOMLs 1.0.1).
* Auras and shouts already on units follow a change at once: newly hidden ones lose their look, newly shown ones get it back.
* The terror zone tint clears or comes back at once when you change hide_terror_zone_tint inside a terror zone.
* Charge orbs already drawn go away when you hide them; orbs you show again come back with the next charge.
* The download is now named visual-clutter.zip, without a version. It holds the DLL alone: the default settings are built in and written on first launch.

**Fixed**

* The plugin no longer reads the game's code through ReadProcessMemory, which made an antivirus flag the DLL.
* The DLL now names wild-healer as its author.
```

## GitHub release

- Tag: `visual-clutter-v1.0.1`
- Title: `Visual Clutter 1.0.1`
- Asset: `visual-clutter.zip`

Body:

```markdown
**Visual Clutter** 1.0.1

### New
- Every setting can be changed in game with I Hate TOMLs and applies at once, enabled included.
- The aura, shout, skill and charge orb choices show in I Hate TOMLs as tables of check boxes, one row per kind of unit and one column per caster (needs I Hate TOMLs 1.0.1).
- Auras and shouts already on units follow a change at once: newly hidden ones lose their look, newly shown ones get it back.
- The terror zone tint clears or comes back at once when you change `hide_terror_zone_tint` inside a terror zone.
- Charge orbs already drawn go away when you hide them; orbs you show again come back with the next charge.
- The download is now named `visual-clutter.zip`, without a version. It holds the DLL alone: the default settings are built in and written on first launch.

### Fixed
- The plugin no longer reads the game's code through ReadProcessMemory, which made an antivirus flag the DLL.
- The DLL now names wild-healer as its author.

### Install
Load `visual-clutter.zip` in D2RMM, or copy `d2rl-visual-clutter.dll` to `<game>/d2rloader/plugins/`. Settings are unchanged.
```

## D2RLoader Hub

Name: `Visual Clutter`

Summary:

> Hides aura and shout visuals by who they are on and who cast them, plus Firestorm, Thunder Storm, charge orbs and the terror zone tint. The effects still work.

Description:

```markdown
**Visual Clutter** hides visual effects without changing what they do. Auras, shouts and skills keep working; only their look goes away, and you choose whose.

### Features
- **Auras by target and caster.** For each kind of unit an aura is drawn on (you, your merc, your summons, other players and theirs, monsters), choose whose auras to hide there. Default: auras from you, your merc and other players are hidden everywhere except on other players, and your own auras on you and your merc's auras on itself stay visible.
- **Shouts set separately.** Shout, Battle Orders and Battle Command have their own choices and are shown by default.
- **Hellfire Torch's Firestorm** and **Thunder Storm's bolts and their flash**, hidden by who cast them. Hiding Firestorm also hides the druid skill of the same name.
- **Martial arts charge orbs**, the ones Mosaic keeps up, hidden on the units you choose.
- **No red tint in terror zones.** The zone, its sound and its name on screen are unchanged.
- **I Hate TOMLs support.** Change the settings in game, ticking boxes in a who-on-whom table; they apply at once, to effects already on screen too.

### Requirements
- D2RLoader 1.3.1 or later.
- Steam 3.3.93787, 3.3.94995 or 3.4.95041. The plugin checks the game code when it loads; on another build it stays inactive and says why in its log and in the `visual-clutter` console command.
- Single player or a game you host: who cast an aura is read from the game server running on your PC. In a game hosted by someone else, every aura counts as cast by the unit it is on.

### Install
Load `visual-clutter.zip` in D2RMM, or copy `d2rl-visual-clutter.dll` to `<game>/d2rloader/plugins/`. Settings are in `d2rloader/config/visual-clutter.toml`, written on first launch, or in I Hate TOMLs.

### Known limits
- Firestorm and Thunder Storm are recognised by their missiles, so other skills using the same missiles are hidden with them. Missiles already flying when you change a setting finish as they were.
- Charge orbs shown again come back with the next charge.
```

# Visual Clutter 1.0.0

## GitHub release

- Tag: `visual-clutter-v1.0.0`
- Title: `Visual Clutter 1.0.0`
- Asset: `visual-clutter-1.0.0.zip`

Body:

```markdown
**Visual Clutter 1.0.0** is the first release: hide aura and shout effects by who they are on and who cast them, plus Hellfire Torch's Firestorm, Thunder Storm's bolts, martial arts charge orbs and the terror zone tint. Only the look goes away; every effect still works.

### Features
- **Auras by target and caster**: one `on_<kind>` line per kind of unit the aura is drawn on (`self`, `merc`, `minions`, `allies`, `monsters`), listing whose auras to hide there. Your merc's Infinity Conviction on monsters, your Holy Freeze on your summons, an ally's Fanaticism on you: any combination.
- **Shouts set separately**: Shout, Battle Orders and Battle Command have their own `shouts_on_<kind>` lines, all shown by default.
- **Hellfire Torch Firestorm** (`hide_firestorm`) and **Thunder Storm bolts and flashes** (`hide_thunder_storm`), by who cast them.
- **Martial arts charge orbs** (`hide_charges`), the orbs Mosaic keeps up.
- **Terror zone red tint** (`hide_terror_zone_tint`): the zone, its sound and its name on screen stay.
```

## Description (1.0.0)

```markdown
**Visual Clutter** is a D2RLoader plugin that hides visual effects without changing what they do. Auras, shouts and skills keep working; only their look goes away, and you choose whose.

### Features

* **Auras by target and caster.** For each kind of unit an aura is drawn on (you, your merc, your summons, other players and theirs, monsters), list whose auras to hide there. Default: auras from you, your merc and other players are hidden everywhere except on other players, and your own auras on you and your merc's auras on itself stay visible.
* **Shouts set separately.** Shout, Battle Orders and Battle Command follow their own lines and are shown by default.
* **Hellfire Torch's Firestorm** and **Thunder Storm's bolts and their flash**, hidden by who cast them. Hiding Firestorm also hides the druid skill of the same name.
* **Martial arts charge orbs**, the ones Mosaic keeps up, hidden on the units you choose.
* **No red tint in terror zones.** The zone, its sound and its name on screen are unchanged.

Everything above is off or shown by default except the aura matrix; turn on what you want in the config.

### Requirements

* D2RLoader 1.3.1 or later.
* Steam 3.3.93787 or 3.3.94995 (tested on 3.3.94995). The plugin hooks game code at fixed addresses and checks it when it loads: on any other build, Battle.net 3.3.93847 included, it stays inactive and says why in its log and in the `visual-clutter` console command.
* Single player or a game you host: who cast an aura is read from the game server running on your PC. In a game hosted by someone else, every aura counts as cast by the unit it is on.

### Install

Load `visual-clutter-1.0.0.zip` in D2RMM, or copy `d2rl-visual-clutter.dll` to `<game>/d2rloader/plugins/`. Settings are in `d2rloader/config/visual-clutter.toml`, written on first launch. Edit that copy, not the one in the zip, and restart the game after changing it.

### Console

`visual-clutter`: status, counts and current settings.

### Known limits

* Settings apply at the next game start. An aura already on a unit keeps its look until it ends and comes back.
* Firestorm and Thunder Storm are recognised by their missiles, so other skills using the same missiles are hidden with them.
```
