# Cube Empty

D2RLoader plugin: middle-click the Horadric Cube in your inventory and everything
inside moves to free spots in your inventory. Items that don't fit stay in the cube,
and your character says "impossible" so you know something is left.

- Install: get Cube Empty from the [D2RLoader Hub](https://d2rloader.net/hub/plugins/cube-empty) and add it through D2RMM like the other plugins.
- Config (`d2rloader/config/cube-empty.toml`): `button` (middle, back, forward),
  `require_hover` (false = any click in game empties the cube).
- Log: `mods/D2RMM/d2rloader/logs/cube-empty.log` lists each emptying and every
  item left behind.

How it works: the move uses the plugin SDK only, so game updates don't break it.
The "impossible" line uses the game's PlaySound (D2R+0x1A0780) and player sound
table (D2R+0x22A4110), checked at load; on another game build it just stays silent.
The hovered item comes from an item tooltip listener (the cube's tooltip must be
showing, or the mouse still within 40 px of where it appeared); the click from a
thread polling the mouse button while D2R is in front; each cube item is moved
with its own ExistingItemTransaction (Move to Inventory, automatic placement) on
the game thread. Works in your own games (single player or hosting), not as a
joined client (no authoritative game thread there).

Build: `D2RL_SDK=<PluginSDK checkout> sh build.sh` (MinGW-w64, SDK pinned to 03a5af9).

Fallback found during research (not used): D2R's own Ctrl+click transfer
`D2R+0x15F8B0 (item, destUnit, page, srcPage, flag, Pos*)` — with destUnit =
local player, page 0 and an invalid Pos it finds a free spot and sends packet
0x18. RuffnecKk's Materials Deposit calls it with page 4.
