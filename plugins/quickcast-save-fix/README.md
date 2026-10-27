# Quickcast Save Fix

D2RLoader plugin: saving during or right after a quick cast keeps your own left
and right skills.

With Quick Cast on, a skill hotkey puts its skill on your left or right click
for the cast, and the game only puts your own skill back on your next mouse
click. Any save before that click (Save and Exit in the middle of a cast, or
right after one) wrote the hotkey skill as your left/right skill, so you came
back with it on your mouse button. With this plugin the save gets the skill the
game was going to put back. A save while you keep playing doesn't disturb the
cast.

- Install: get Quickcast Save Fix from the [D2RLoader Hub](https://d2rloader.net/hub/plugins/quickcast-save-fix-1-0-0) and add it through D2RMM like the other
  plugins, or copy `d2rl-quickcast-save-fix.dll` to `<game>/d2rloader/plugins/`.
- Config (`d2rloader/config/quickcast-save-fix.toml`): `enabled`, `log_fixes`
  (true: a line in the D2RLoader log each time a save gets your skills back).
- Works in single player and in games you host (for you), not in games you join.
- Steam 3.3.93787 / 3.3.94995. The plugin checks the game code when it loads
  and stays inactive on other builds.
- Works alongside Swap Cast.
- Build: `sh build.sh` (MinGW-w64, D2RLoader PluginSDK 03a5af9).
