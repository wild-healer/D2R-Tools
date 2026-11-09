# Merc Names

A D2RMM mod that sets which names each act's mercenaries can spawn with.

## Settings

One collapsible section per act, with a checkbox for each of that mercenary's names:

| Setting | Mercenary | Names |
| --- | --- | --- |
| Act 1 Rogue | Rogue Scout | 41 |
| Act 2 Desert Mercenary | Desert Mercenary | 21 |
| Act 3 Iron Wolf | Eastern Sorceror | 20 |
| Act 5 Barbarian | Barbarian | 67 |

Every name is ticked by default. All ticked, or none, keeps the game's names for that act.

## How it works

The mod rewrites `local/lng/strings/mercenaries.json`: the act's name slots (NameFirst to NameLast in `hireling.txt`) are shared out between the ticked names, each name's text in all languages. The game picks a slot at random, so every ticked name is about equally likely (one slot more or less when they don't divide evenly, e.g. 14/14/13 for three Rogue names). The mercenary you already have also shows one of the ticked names. Saves are not changed, so turning the mod off brings the old names back.

## Install

Extract the `Merc Names` folder into D2RMM's `mods` folder, enable it, pick the names and apply.
