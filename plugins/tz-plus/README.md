# TZ Plus

D2RLoader plugin. Bigger terror zones, with mods that have tiers and give more loot.

- **Normal:** the game's own terror zone, untouched.
- **Nightmare:** one whole act is a terror zone, the act changing with the game's 30 minute rotation. 1 to 2 mods, tier 1 to 2.
- **Hell:** every level is a terror zone, and the zone level goes up to 99. 1 to 3 mods, tier 1 to 3.
- The mods roll once per game and hold for the whole game. How many there are, and their tiers, grow with the terror zone level (your level + 2): a level 99 zone always has 3 tier 3 mods.
- Champions, uniques, minions and bosses get each mod one tier higher. No monster goes over level 99.
- Rewards for the game: each mod gives its tier in points of gold find (+25% each), tier 2 and 3 mods also give tier - 1 points of magic find (+15% each), and tier 3 mods give 1 point of experience (+5%, on top of the game's +25%).

Mods: Extra Strong, Extra Fast, Cursed, Magic Resistant, Fire / Lightning / Cold Enchanted, Venomous, Mana Burn, Teleportation, Spectral Hit, Stone Skin, Multiple Shots, Tough, Vampiric, Regenerating, and every monster aura (Might, Holy Fire, Blessed Aim, Holy Freeze, Conviction, Fanaticism, Holy Shock, Concentration, Vigor, Thorns; at most one aura per game).

The game's mods and rewards are listed in the map's text (top right, map open), under "Terrorized". The console command `tzplus` shows the game's mods and rewards. Settings: `d2rloader/config/tz-plus.toml`, or in game with I Hate TOMLs. Runs where the game's server runs: single player and games you host.

Build: `./build.sh` (MinGW-w64).
