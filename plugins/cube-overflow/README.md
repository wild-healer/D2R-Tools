# Cube Overflow

D2RLoader plugin: when you pick up, buy, gamble or get a quest reward and your
inventory has no room for it, it goes into the Horadric Cube instead. The cube must be in your inventory (not the
stash). If the cube is full too, your character says "no room" as usual.

- Install: get Cube Overflow from the [D2RLoader Hub](https://d2rloader.net/hub/creators/thewildhealer) and add it through D2RMM like the other plugins.
- Config (`d2rloader/config/cube-overflow.toml`): `enabled`, `cursor_pickups`
  (also send to the cube a pickup the game would put on the cursor because the
  inventory is full), `purchases_and_rewards` (store purchases, gambling and quest
  rewards overflow too).
- Log: `mods/D2RMM/d2rloader/logs/cube-overflow.log` lists every item sent to the cube.
- Works in single player and in games you host (for every player there), not in
  games you join: the pickup is decided by the host.

How it works (Steam 3.3.93787 / 94995 RVAs):
- Pickups: hooks the server pickup D2R+0x471950
  `(player, itemGuid, toInventory, maxDistance, checkCollision, toCube)`. With
  `toCube` the game itself puts the item on a free spot of page 3 (the cube); its own
  "pick up into the cube" packet 0x5F (D2R+0x4BB600) uses it. Before an inventory
  pickup the plugin asks D2R+0x46DED0 (the game's stack / belt / free spot search for
  an item given to the player) where it would go; when nowhere and the cube has a spot,
  it picks up with `toCube`, so nothing bounces. Scrolls are left to the game (tomes).
  Cursor pickups: no free inventory spot (D2R+0x3865B0 on page 0) but one in the cube.
- Purchases and gambling: the client picks the destination (D2R+0x1121A4 searches
  page 0, D2R+0x1121CC builds a page 0 destination) and sends its x/y without a page
  in packet 0x32. The server rebuilds it as page 0 (D2R+0x4C46E5) and refuses the
  purchase unless it equals what D2R+0x46DED0 finds. So: the client's failed search
  returns its cube spot and both destinations for that x/y get page 3, and
  D2R+0x46DED0, when it finds nothing, returns the cube spot (destination built with
  D2R+0x382E10, page 3). In games you host this works for your own purchases only.
- Quest rewards: D2R+0x517530 creates the reward, searches page 0 and builds a page 0
  destination, else drops it. While it runs, a failed page 0 search returns a cube spot
  and the next destination built on that thread gets page 3.
- The log names the game code (RVA) of each kind of failed inventory search once, to
  trace a "no room" the plugin doesn't handle.
- 1.0.0 retried a failed pickup into the cube; the failed first try bounced the item.

Every function's first 16 bytes are checked at load; otherwise the plugin stays
inactive. RVAs first found in RuffnecKk's Potion Auto Pickup 1.3.3.

Build: `D2RL_SDK=<PluginSDK checkout> sh build.sh` (MinGW-w64, SDK pinned to 03a5af9).
