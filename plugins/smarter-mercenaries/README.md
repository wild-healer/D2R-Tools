# Smarter Mercenaries

D2RLoader plugin: mercenaries fight the way their type is meant to, instead of
the game's single AI that attacks whatever is closest and picks skills by a
dice roll.

- Formation: no more wandering around. Melee mercenaries (Desert mercenary,
  Barbarians) stand slightly in front of you, ranged ones (Rogue, Iron Wolf)
  slightly behind, spread sideways, and follow your moves every few frames.
- Melee mercenaries leave the formation when an enemy comes within
  `engage_range` (10) of them, and go back once it's dead.
- Ranged mercenaries stay in formation and shoot from there; a place up to 3
  off their spot is good enough. When you get within `danger_range` (10) of
  enemies they position themselves: the place closest to you that keeps 5
  from every monster (more from fast ones, up to 11), then shoot again (hit and run). They only shoot at monsters they have a
  clear line to from where they stand; when every target is behind a wall they
  step next to you instead of looking for a shot.
- Mercenaries run whenever you run or when more than 3 off their spot.
- Your intent, from how you move: pushing toward monsters, mercenaries favour
  what's ahead of you and ranged ones hold the formation longer; backing off,
  they back off with you and stop chasing what you leave behind; melee ones stop fighting and come with you.
- Protection: when a monster gets within `protect_range` (5) of you, melee
  mercenaries regroup on you and fight what is around you.
- Surrounded (monsters on every side within 4): the mercenary breaks out to
  the free spot with the most room, toward you.
- Safe mode: below `safe_life` (35%) life, or losing `safe_burst` (25%) of
  its life within `safe_window` (2) seconds, a mercenary keeps well away from
  monsters, dodges missiles, uses its crowd control on what gets close
  (Freezing/Cold Arrow, Glacial Spike, Stun, Bash, Taunt) and lets its life
  come back; it fights again once back over safe_life + 25.
- Targets: what you're attacking first (only a slight lean for melee), then monsters attacking you, then
  monsters attacking the mercenary, then the closest, never farther than
  `leash` (14) from you; targets immune to all its damage come last.
- Rogue: Exploding/Freezing Arrow on groups, Fire/Cold Arrow on single
  monsters, normal arrows on immunes, Inner Sight on bosses or groups.
- Desert mercenary: Jab on bosses and champions, Jab or normal attacks otherwise.
- Iron Wolf: keeps Enchant (on you first) and Chilling Armor up, picks its
  spell by the target's resistances, Static Field only on healthy bosses or
  crowds near it.
- Barbarian: Battle Cry on groups, Stun on bosses and champions, Bash
  otherwise. Frenzy Barbarian Taunts monsters attacking you, Frenzy otherwise.
- In town they stay in a loose group, never more than 7 steps from you (inside
  warcry range, so Battle Orders reaches them), and only move to catch up.
- Attack rate is the game's own (same chance per think), only the choices change.
- Works with every mercenary Multi Mercenary adds (each one runs the same AI).
- Single player and games you host (it is server side); nothing changes in games you join.

Settings, in game with I Hate TOMLs or in `d2rloader/config/smarter-mercenaries.toml`:
`enabled`, `rogue`, `desert_mercenary`, `iron_wolf`, `barbarian`, `leash` (14),
`engage_range` (10), `danger_range` (10), all 4 to 24, `protect_range` (5, 2 to
12), `safe_life` (35), `safe_burst` (25), both 10 to 90, `safe_window` (2, 1 to 5), `log` (decisions to
`mods/D2RMM/d2rloader/logs/smarter-mercenaries.log`, plus a line every few seconds per mercenary on whether the new AI ran).

How it works (Steam 3.3.94995): hooks the Hireable think function
D2R+0x5BEAC0 (AI table 0x2396E70 entry 62), the AI target choice D2R+0x595F80
(only while a player's mercenary thinks) and the hireling combat step
D2R+0x5BF6E0. Every game function's first 16 bytes are checked at load;
otherwise the plugin stays inactive. See the comment at the top of
`src/plugin.cpp` for every address used.

Build: `D2RL_SDK=<PluginSDK checkout> sh build.sh` (MinGW-w64, SDK pinned to 03a5af9).
