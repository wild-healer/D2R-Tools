# Smarter Mercenaries

D2RLoader plugin: mercenaries fight the way their type is meant to, instead of
the game's single AI that attacks whatever is closest and picks skills by a
dice roll.

- Every type: fights what you're attacking first, then monsters attacking you,
  then monsters attacking the mercenary, then the closest. It doesn't pick
  fights farther than `leash` from you (monsters hitting the mercenary itself
  are always fought), and avoids targets immune to all its damage
  (resistances as they are now, so Conviction or Lower Resist count).
- Rogue: steps back toward you when a monster gets within 4, Exploding/Freezing
  Arrow on groups, Fire/Cold Arrow on single monsters, normal arrows on
  immunes, Inner Sight on bosses or groups that don't have it.
- Desert mercenary: only fights within `aura_leash` of you so its aura covers
  you, and walks back to you instead of chasing (unless it's being hit). Jab
  on bosses and champions, Jab or normal attacks otherwise.
- Iron Wolf: keeps its distance, keeps Enchant (on you first) and Chilling
  Armor up, picks its spell by the target's resistances; Static Field only on
  healthy bosses or crowds near it; stays with you when everything near is
  immune to it.
- Barbarian: body-blocks (steps between you and a monster coming at you), goes for the monsters closest to you and those attacking you first, Battle Cry on groups, Stun on
  bosses and champions, Bash otherwise. Frenzy Barbarian Taunts monsters that
  attack you, Frenzy otherwise.
- Attack rate is the game's own (same chance per think), only the choices change.
- Works with every mercenary Multi Mercenary adds (each one runs the same AI).
- Single player and games you host (it is server side); nothing changes in games you join.

Settings, in game with I Hate TOMLs or in `d2rloader/config/smarter-mercenaries.toml`:
`enabled`, `rogue`, `desert_mercenary`, `iron_wolf`, `barbarian`, `leash` (14,
4 to 24), `aura_leash` (8), `log` (decisions to
`mods/D2RMM/d2rloader/logs/smarter-mercenaries.log`, plus a line every few seconds per mercenary on whether the new AI ran).

How it works (Steam 3.3.94995): hooks the Hireable think function
D2R+0x5BEAC0 (AI table 0x2396E70 entry 62), the AI target choice D2R+0x595F80
(only while a player's mercenary thinks) and the hireling combat step
D2R+0x5BF6E0. Every game function's first 16 bytes are checked at load;
otherwise the plugin stays inactive. See the comment at the top of
`src/plugin.cpp` for every address used.

Build: `D2RL_SDK=<PluginSDK checkout> sh build.sh` (MinGW-w64, SDK pinned to 03a5af9).
