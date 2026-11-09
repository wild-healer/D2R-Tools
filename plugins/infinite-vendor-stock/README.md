# Infinite Vendor Stock

D2RLoader plugin: whatever you buy from a vendor stays in the shop, so you can
buy it again and again, the way the game already treats potions, scrolls, keys,
arrows and bolts. Weapons, armor, rings, amulets, charms, jewels and the items
you sold to the vendor all stay in stock. Gambling is not changed.

- Install: load `infinite-vendor-stock.zip` with D2RMM, or copy
  `d2rl-infinite-vendor-stock.dll` to `<game>/d2rloader/plugins/`.
- Config (`d2rloader/config/infinite-vendor-stock.toml`, written on first launch):
  `enabled`. It can be changed in game with I Hate TOMLs and applies at once.
- Works in single player and in games you host (for everyone in them). In games
  you join the host's shop rules apply.

How it works (Steam 3.3.93787 / 94995 RVAs, server code):
- A purchase goes through D2R+0x53D850 `(game, player, npc, item guid, dest page,
  dest x|y, client dest, cost, gamble, fill stack, ...)`.
- It asks D2R+0x38C7D0, the game's "this store item never sells out" test (a list
  of item codes, then the PermStoreItem column), at D2R+0x53E465 (permanent: the
  player gets a full copy of the item from D2R+0x43D660, otherwise the item leaves
  the store), D2R+0x53E578 (permanent: the store keeps showing it), D2R+0x53ED16
  (failed purchase: the copy is freed), D2R+0x53DA64 and D2R+0x53DD3F (shift-buy
  into a stack, scroll bought into a tome).
- During a purchase that isn't a gamble, the plugin answers yes at those five call
  sites. Every other caller, the client's included, gets the game's own answer.

Both functions' first 16 bytes and the five call sites are checked at load;
otherwise the plugin stays inactive and says so in its log.
