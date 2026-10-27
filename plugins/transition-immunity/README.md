# Transition Immunity

D2RLoader plugin: after anything with a loading screen (town portal, waypoint,
cave or dungeon entrance, stairs, joining a game), monsters can't hurt or curse
you until you move, or for 60 seconds at most. The loading screen itself is
covered too. While it lasts, the armor shrine's shield symbol floats over your head, above any skill effect.

- Install: get Transition Immunity from the [D2RLoader Hub](https://d2rloader.net/hub/plugins/transition-immunity) and add it through D2RMM like the other plugins.
- Config (`d2rloader/config/transition-immunity.toml`): `enabled`,
  `duration_seconds` (60), `indicator`, `indicator_overlay` (an overlay.txt
  Filename, `ShrineArmor` by default).
- Log: `mods/D2RMM/d2rloader/logs/transition-immunity.log` has one line per
  protection window: when it started, why it ended (moved / time up) and how many
  hits and curses it blocked, and how many of those during loading.
- Aura damage (Holy Fire, Holy Shock, Holy Freeze, Sanctuary) is blocked: those
  pulses go through the same hit step (D2R+0x561280 / 0x561480 -> 0x44CE80 -> 0x44A9B0).
  Their non-damage effects (Holy Freeze slow, Conviction) are not.
- Moving = walking, running or any position change (teleport included). Attacking
  or casting in place does not end it.
- Works in single player and in games you host (for you), not in games you join.

How it works:
- Hits: D2R+0x44A9B0 `(Game*, attacker, defender, Damage*)` is the step every
  landed hit goes through (melee, missiles, spells, traps): life/mana loss,
  poison, burn, freeze, chill, stun, knockback, hit recovery. It is skipped while
  you're protected, so the hit does nothing.
- Curses: D2R+0x55A7E0 is the per-target step of every curse (srvdofunc 30, the
  monsters' curses too); it returns 0 for you, as for a target the game rejects.
- Window: a 50 ms UI tick watches the local player (D2R+0x9A480 / 0x8B2D0); a new
  level (D2R+0x2EFC10 of the unit's room D2R+0x34B440) starts it, the player's
  mode (walk/run/town walk) or position (path +2/+6) ends it after a 0.6 s grace.
  While the server has you in another level than the client shows (loading),
  you're protected as well.
- Indicator: the game's own overlay calls, D2R+0x150280 `(unit, overlay, 3, 0, 0, 0, 0)`
  and D2R+0x152090 `(unit, 0, overlay)`, the overlay row found by Filename.

Every function's first 16 bytes are checked at load (Steam 3.3.93787 / 94995);
otherwise the plugin stays inactive.

Build: `D2RL_SDK=<PluginSDK checkout> sh build.sh` (MinGW-w64, SDK pinned to 03a5af9).
