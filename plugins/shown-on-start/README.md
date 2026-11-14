# Shown on Start

D2RLoader plugin. Turns on, so you don't have to press their keys after launching D2R:

- **Item labels** on the ground (the Show Items key), turned on at the start of every game. Needs the game's Item Name Display option on Toggle.
- **Automap** (the AutoMap key, Tab), opened in every game.

Their keys still turn them off. Both options are in `shown-on-start.toml` and in game with I Hate TOMLs.

Supported: Steam D2R 3.4.95041 and 3.3.94995. On another build the plugin checks the game's code, finds it different and stays inactive.

Build: `./build.sh` (MinGW-w64, cross-built on Linux).
