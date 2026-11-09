# Enchant Everyone

D2RLoader plugin: when Enchant is cast, every friendly unit around the caster
gets it too: the caster, the other players of the party, mercenaries, summons
and revives (the caster's and the party's). The normal target still gets it as
usual. Each extra unit costs one more cast's mana, nearest first, until the
mana runs out, unless `single_mana_cost` is on (one cast pays for everyone).

- Install: add `enchant-everyone.zip` through D2RMM like the other plugins.
- Settings, in game with I Hate TOMLs or in `d2rloader/config/enchant-everyone.toml`,
  all applied at once: `enabled`, `radius_yards` (20, from 1 to 25), `players`
  (party members), `minions` (mercenaries, summons, revives), `single_mana_cost` (false).
- Log: `mods/D2RMM/d2rloader/logs/enchant-everyone.log` has one line per cast
  with how many more units it enchanted.
- Works in single player and in games you host, not in games you join (the
  host's server decides who gets the state).
- Players count as friendly only when in the same party, as for the game's own
  Enchant. Town NPCs and enemies are never enchanted.

How it works:
- D2R+0x553650 `(Game*, caster, skill, level)` is Enchant's server step
  (srvdofunc 25). It takes the target from the caster's path (+0x70 unit,
  +0x78 type, +0x7C id), keeps it when it is an ally (D2R+0x48E4D0), else
  enchants the caster.
- The hook lets the cast run, then looks through the rooms near the caster
  (D2R+0x34B440, room +0x00 / +0x40 near rooms, +0xA8 first unit, unit +0x160
  next) for living players and monsters within the radius that D2R+0x48E4D0
  calls allies, and calls the game's Enchant step once more for each, with the
  caster's path target pointed at it for that call and put back right after.
- Mana: D2R+0x33AA00 `(expansion, skill, level)` gives the skill's cost (as the
  game's own cast does), D2R+0x436A80 `(unit, cost)` takes it, 0 when short.

Every function's first 16 bytes are checked at load (Steam 3.3.94995);
otherwise the plugin stays inactive.

Build: `D2RL_SDK=<PluginSDK checkout> sh build.sh` (MinGW-w64, SDK pinned to 03a5af9).
