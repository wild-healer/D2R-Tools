# Terror Zone Selector

D2RLoader plugin: choose which terror zones are active. Tick zones in game with
I Hate TOMLs (or in `d2rloader/config/terror-zone-selector.toml`) and the
change applies at once, no restart and no new game needed.

- Install: add `terror-zone-selector.zip` through D2RMM like the other plugins.
- `mode = "together"` (default): every chosen zone is a terror zone at the same time.
- `mode = "rotate"`: one chosen zone at a time, changing every 30 minutes on
  the game's own schedule.
- Nothing chosen, or `enabled = false`: the game's own terror zone.
- Menu buttons: Clear all, and All / None for each act.
- Log: `mods/D2RMM/d2rloader/logs/terror-zone-selector.log` names the zones
  each time the active set changes.
- Works in single player and in games you host. On Battle.net the server picks
  the zone.

How it works:
- D2R+0x35B380 `(bool* changed) -> Zone*` is the game's "current scheduled
  terror zone" (DesecrateGetCurrentScheduledZone). The game calls it when a
  game is created, when a player joins and when the zone changes, then applies
  the zone to the game.
- The hook returns the chosen zone (rotate), or a zone made of all chosen zones
  (together: a copy of the first chosen zone holding every chosen zone's
  levels; zone +0x88 levels, +0x90 count, +0x98 capacity, levels 0x38 bytes,
  +0x00 level id). Zones come from the config of D2R+0x35DB10 `(time, bool*)`
  (+0x250 zones, 0x3E8 bytes each, +0x258 count), matched by first level id.
- Live changes: the main loop broadcasts the current zone to every game when
  its zone timer runs out (armed byte and next time read from the code at
  D2R+0x8C044). A change sets the next time to 0, so the game re-applies the
  zone at its next tick.

Every function's first 16 bytes are checked at load (Steam 3.3.94995);
otherwise the plugin stays inactive.

Build: `D2RL_SDK=<PluginSDK checkout> sh build.sh` (MinGW-w64, SDK pinned to 03a5af9).
