# Permanent Cow Portal

D2RLoader plugin: a red portal to the Moo Moo Farm stands in the Rogue
Encampment, next to the waypoint, in every game once you have beaten Baal on
that difficulty (Diablo in a Classic game). No Wirt's Leg, no cube. It works
offline and in games you host, like the online Legacy of Terror event, and the
cow level opens even after the Cow King was killed.

- Install: add `permanent-cow-portal.zip` through D2RMM like the other plugins,
  or copy `d2rl-permanent-cow-portal.dll` to `<game>/d2rloader/plugins/`.
- Config (`d2rloader/config/permanent-cow-portal.toml`, or I Hate TOMLs in
  game): `enabled`, `require_baal` (false: the portal is always there),
  `offset_x` / `offset_y` (where it stands, in tiles from the waypoint; the game
  moves it to the nearest free spot). Changes apply to the next game.
- Console: `permanent-cow-portal` shows the status.
- Works in single player and in games you host.
- Steam 3.3.93787 / 3.3.94995 / 3.4.95041. The plugin checks the game code when
  it loads and stays inactive on other builds.
- How it works: it opens the portal with the same game calls as the cube's cow
  portal recipe, placed next to the town waypoint, and sets the game's own
  "cow portal opened" flag. Notes in src/plugin.cpp.
- Build: `sh build.sh` (MinGW-w64 win32 threads, D2RLoader PluginSDK 03a5af9).
