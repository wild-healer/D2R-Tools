# Visual Clutter

Hides visual effects without changing what they do. So far: auras (the glow, the rings at the feet and the small icons that auras draw on units) and, set separately, the barbarian's shouts (Shout, Battle Orders, Battle Command). You choose, for each kind of unit the effect is drawn on, whose auras or shouts to hide there. They keep working. It can also hide Hellfire Torch's Firestorm, Thunder Storm's bolts and flashes, the martial arts charge orbs (Mosaic), and the red tint inside terror zones.

It is one D2RLoader plugin (`d2rl-visual-clutter.dll`) with native hooks.

## Install

1. Plugin: get Visual Clutter from the [D2RLoader Hub](https://d2rloader.net/hub/plugins/visual-clutter), put its zip in D2RMM's `d2rloader` folder and refresh plugins.
2. Apply mods and start the game.

## Config

`d2rloader/config/visual-clutter.toml` (created on first launch; edit that copy, not the one in the zip). Changes apply at the next game start.

Each `on_<kind>` line is a kind of unit the effect is drawn on and lists the kinds of caster whose auras are hidden there. Both sides use the same five kinds:

| Kind | Who |
| --- | --- |
| `self` | you |
| `merc` | your mercenary |
| `minions` | your summons (golems, druid spirits, Valkyrie, ...) |
| `allies` | other players, their mercenaries and summons |
| `monsters` | monsters (aura-enchanted bosses, monster auras) |

Default:

```toml
on_self     = ["merc", "allies"]
on_merc     = ["self", "allies"]
on_minions  = ["self", "merc", "allies"]
on_allies   = []
on_monsters = ["self", "merc", "allies"]
```

Examples: your own Might on you is `on_self` + `"self"`, your Might on your merc is `on_merc` + `"self"`, your Conviction on monsters is `on_monsters` + `"self"`, and an aura-enchanted boss's aura on itself and its minions is `on_monsters` + `"monsters"` (shown by default). `[]` shows every aura on that kind of unit.

| Key | Default | |
| --- | --- | --- |
| `enabled` | true | |
| `on_self`, `on_merc`, `on_minions`, `on_allies`, `on_monsters` | see above | Casters whose auras are hidden on that kind of unit |
| `shouts_on_self`, `shouts_on_merc`, `shouts_on_minions`, `shouts_on_allies`, `shouts_on_monsters` | `[]` (all shown) | Casters whose shouts (Shout, Battle Orders, Battle Command) are hidden on that kind of unit; the `on_` lines don't cover them |
| `hide_firestorm` | `[]` | Casters whose Firestorm (Hellfire Torch, druid skill) is not drawn |
| `hide_thunder_storm` | `[]` | Casters whose Thunder Storm bolts (and their flash) are not drawn |
| `hide_charges` | `[]` | Kinds of unit whose martial arts charge orbs are not drawn |
| `hide_terror_zone_tint` | false | No red screen tint in terror zones (the zone, its sound and its name stay) |
| `versions` | `["3.3"]` | Game versions the plugin may hook |

## Who cast an aura

The game client is never told who cast an aura on a unit, only that the unit has the aura's state. The game server knows: it is told the caster when it puts an aura or shout on a unit. The plugin catches that moment, so the caster is known in single player and in games you host. In a game hosted by someone else, every aura counts as cast by the unit it is on (an aura on you counts as yours).

Your merc and summons are told apart from monsters with the game's own pet list, the one behind the party portraits.

## How it works

The client turns a state's look on by adding the overlays listed in `states.txt` to the unit; the HD renderer draws those overlays. For a hidden aura the plugin skips that step, so the unit never gets the overlay: nothing is drawn and the state itself (stats, buff icons) is untouched. Addresses are those of Steam 3.3.93787 (unchanged in 3.3.94995) and are checked against the game's code at load; on a build where they don't match, the plugin loads but does nothing and says why in its log and in the `visual-clutter` console command.

A decision is made when the aura reaches the unit. Changing the config needs a game restart, and an aura that is already on a unit keeps its look until it leaves and comes back.

## Other effects

These change only what your client draws; the server still runs the skill and deals its damage. Firestorm and Thunder Storm missiles are not created on the client when their owner's kind is listed, so neither the missile nor its light shows. Charge orbs are legacy overlays the client skips. The terror zone tint is a screen effect the client starts when you enter a terror zone; with `hide_terror_zone_tint = true` the plugin removes that one call at load.

## Console

`visual-clutter` prints whether the plugin is active, how many aura effects it hid and showed, how many missiles and charge orbs it hid, and the current settings. The first 40 aura decisions and the first 40 Firestorm / Thunder Storm missiles are written to `d2rloader/logs/visual-clutter.log` with the unit's kind and the caster's kind.

## Build

`./build.sh` cross-builds the DLL with MinGW-w64 on Linux against the pinned PluginSDK commit and writes `visual-clutter-<version>.zip`.
