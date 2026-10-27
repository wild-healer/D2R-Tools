# Visual Clutter 1.0.0: release texts

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

## Nexus Mods changelog (1.0.0)

```
**Visual Clutter 1.0.0**

**New**

* First release.
* Hide aura effects by who they are on and who cast them (you, your merc, your summons, other players, monsters).
* Shouts (Shout, Battle Orders, Battle Command) set separately, shown by default.
* Hide Hellfire Torch's Firestorm and Thunder Storm's bolts and flashes, by caster.
* Hide martial arts charge orbs (Mosaic).
* Turn off the red tint inside terror zones.
```

# Visual Clutter 1.0.0: description (GitHub README / Nexus)

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
