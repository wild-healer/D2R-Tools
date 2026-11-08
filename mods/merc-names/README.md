# Merc Names

A D2RMM mod that gives every mercenary of an act the name you pick.

## Settings

One dropdown per act, each listing that mercenary's names from the game:

| Setting | Mercenary | Names |
| --- | --- | --- |
| Act 1 Rogue | Rogue Scout | 41 |
| Act 2 Desert Mercenary | Desert Mercenary | 21 |
| Act 3 Iron Wolf | Eastern Sorceror | 20 |
| Act 5 Barbarian | Barbarian | 67 |

"Random (vanilla)" keeps the game's names for that act.

## How it works

The mod rewrites `local/lng/strings/mercenaries.json`: every name in the act's range (NameFirst to NameLast in `hireling.txt`) gets the chosen name's text, in all languages. Every new hire shows the chosen name, and so does the mercenary you already have. Saves are not changed, so turning the mod off brings the old names back.

## Install

Extract the `Merc Names` folder into D2RMM's `mods` folder, enable it, pick the names and apply.
