# Edge Clicks

D2RLoader plugin: clicking on the black void past the edge of the map, or past a
wall, normally does nothing. With this plugin your character walks as far toward
the click as the ground allows, sliding along the wall or going around it. Clicks
the game already handles are left alone.

- Install: get Edge Clicks from the [D2RLoader Hub](https://d2rloader.net/hub/plugins/edge-clicks) and load it with D2RMM, or copy `d2rl-edge-clicks.dll`
  to `<game>/d2rloader/plugins/`.
- Config (`d2rloader/config/edge-clicks.toml`): `enabled`, `log_moves`.
- Log: `d2rloader/logs/edge-clicks.log` lists each corrected click (at most one
  line every 250 ms).
- Single player, or a TCP/IP game you host.

## How it works

Two things stop those clicks (Steam 3.3.93787 / 3.3.94995):

1. **Clicks on the void are dropped.** The mouse handlers (D2R+0x839E0 for a left
   click) turn the cursor into a ground spot with D2R+0x8F3D0 `bool (int* x, int* y)`.
   In the 3D graphics this casts a ray onto the level's ground (D2R+0x7E7CE0); with
   no ground under the cursor it fails and the click never becomes a walk. The
   plugin learns how screen positions map to ground spots around you from the casts
   that hit (a least-squares fit over the last 64), and answers a missed cast from a
   mouse handler with that mapping, the way the classic graphics do.
2. **Walking stops at walls.** Walking (path type 7, D2R+0x3ABC40) heads straight
   for the click (D2R+0x3AACF0); that straight path can end past a wall, where you
   then stop. The real path search around obstacles (A*, D2R+0x3F6930) only runs
   within 18 steps, and a target outside every room (D2R+0x38DFB0) fails outright.
   The plugin hooks the path finder, D2R+0x340E70 `(Path*, Unit*, int, int)`, used
   by the client and the server. For a player walking to a spot (never to a unit),
   when the game's path doesn't reach the click, or one of its straight legs crosses
   a wall, it floods the free ground reachable within 17 steps (room present,
   collision check D2R+0x364C70 with the walker's pattern and mask; your own body
   blocks the squares next to you, so those count as free) and walks to the reached
   spot nearest the click, if that is at least 2 steps nearer.

Path fields: +0x02/+0x06 position, +0x10/+0x12 target, +0x20 room, +0x34 point
count, +0xC4 points, +0x48 flags, +0x50 type, +0x5C/+0x64 collision pattern/mask,
+0x70 target unit. Local player: D2R+0x9A480(D2R+0x8B2D0()).

Every function's first 16 bytes are checked at load; on another build the plugin
stays inactive and says so in its log.

Build: `D2RL_SDK=<PluginSDK checkout> sh build.sh` (MinGW-w64, SDK pinned to 03a5af9).
