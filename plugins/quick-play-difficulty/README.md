# Quick Play Difficulty

D2RLoader plugin for the character select screen:

- **Left click on Play** starts the game at the highest difficulty the selected
  character has unlocked (Hell if Hell is open, else Nightmare, else Normal),
  without the difficulty selector.
- **Right click on Play** opens the difficulty selector as before.

Characters with only Normal open start right away, as in the base game. Keyboard
and controller Play work like the left click.

- Install: add `quick-play-difficulty.zip` through D2RMM like the other plugins,
  or copy `d2rl-quick-play-difficulty.dll` to `<game>/d2rloader/plugins/`.
- Config (`d2rloader/config/quick-play-difficulty.toml`): `enabled`,
  `right_click_selector`, `log_ui` (writes the game's UI messages to
  `logs/quick-play-difficulty-ui.log` for bug reports). All can be changed in
  game with I Hate TOMLs and apply at once.
- Offline characters. Steam 3.3.94995 / 3.4.95041: the plugin checks the game
  code when it loads and stays inactive on other builds.
- Build: `sh build.sh` (MinGW-w64, D2RLoader PluginSDK 03a5af9).
