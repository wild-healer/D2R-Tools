# Ethereal Ground Tag

D2RLoader plugin that adds a tag such as `[Eth]` to the name of ethereal items lying on the ground.

## Install

Get Ethereal Ground Tag from the [D2RLoader Hub](https://d2rloader.net/hub/creators/thewildhealer). Its zip holds the DLL and the default TOML at its root, ready to load in D2RMM.

By hand: copy `d2rl-ethereal-ground-tag.dll` to `<game>/d2rloader/plugins/` (works for every mod), or to `<game>/mods/<mod>/d2rloader/plugins/` for one mod only. Remove the DLL to uninstall.

The default settings are built into the DLL. On first launch D2RLoader writes them to `d2rloader/config/ethereal-ground-tag.toml` (same as [ethereal-ground-tag.toml](ethereal-ground-tag.toml)), which you then edit:

| Key | Default | Meaning |
| --- | --- | --- |
| `enabled` | `true` | Turns the plugin off without removing it. |
| `text` | `"Eth"` | Text of the tag. A space is put between the tag and the name. |
| `position` | `"before"` | `"before"` or `"after"` the name. After goes at the end of the first line, so second lines from other plugins stay below. |
| `brackets` | `"brackets"` | `"brackets"` for `[Eth]`, `"parentheses"` for `(Eth)`, or `"none"`. |
| `bracket_color` | `"white"` | Color of the brackets. |
| `tag_color` | `"gold"` | Color of the tag text. `gold` is the gold of unique item names, `gray` the gray of ethereal and socketed ones. |
| `versions` | `["3.3"]` | Game major.minor versions allowed to hook. The build number is ignored, so Steam (3.3.93787) and Battle.net (3.3.93847) both pass. |

Colors: `item` (same color as the item's name), `white`, `white2`, `gray`, `gray2`, `gray3`, `black`, `black2`, `lightred`, `red`, `red2`, `darkred`, `orange`, `orange2`, `orange3`, `orange4`, `lightgold`, `lightgold2`, `gold`, `gold2`, `yellow`, `yellow2`, `green`, `green2`, `green3`, `green4`, `darkgreen`, `darkgreen2`, `turquoise`, `skyblue`, `lightblue`, `lightblue2`, `blue`, `blue2`, `lightpink`, `pink`, `purple`. Colors are written with the marker D2R uses in memory for `ÿc` codes (U+E07E), the same one D2RLoader uses for its socket counts. After the tag, the name is colored back with the item's own color code, or the color of its quality (ethereal white items are gray); if neither is known, the tag is left uncolored.

## Checking it

- Log: `d2rloader/logs/ethereal-ground-tag.log` (under `mods/<mod>/` when loaded as a mod plugin) says `active: name writer hooked at D2R+0x... (<method>)` or why it stayed inactive.
- Console: `eth-tag` prints the same status in game.

## How it finds the label code

The label text comes from D2R's ground-label name writer, which fills a 128-byte buffer that the label formatter then measures, so the label box resizes around the tag.

First choice: the formatter's two measure calls (one per UI mode) are redirected, through int3 padding in D2R.exe, to a small stub that adds the tag to the finished text and then runs the original measure function. No other known plugin touches those calls, so this works with Chronicle Ground Flag, UnHoarder and Controller QOL loaded, in any order, and the tag goes on after they have written their text.

Hover labels: with a controller, or with the mouse when labels aren't shown with Alt, the game draws the label of the targeted item through a separate function (`D2R+0xC0420`). There the plugin redirects the call that copies the item's name into the drawn label (`D2R+0xC08EC`), so those labels get the tag too.

If those calls don't match, two older attach points are tried. Other plugins attach to the same text in one of two places, and D2RLoader allows one owner for each: an inline hook on the writer's entry (Chronicle Ground Flag, UnHoarder), or a patch of the label formatter's call to the writer (Controller QOL, whose relay still calls the writer). This plugin takes whichever is free:

- when the formatter still calls the writer directly, that call jumps (through 14 bytes of int3 padding in D2R.exe) to the plugin, which calls the writer, including any hook on it, then adds the tag;
- when another plugin already patched that call, the plugin hooks the writer entry instead and calls the original writer through D2RLoader's trampoline, so the other plugin's relay still runs;
- when both are taken, it stays inactive and the log names the plugins holding them.

Everything is located at load time, never by build number:

0. the formatter at `D2R+0x1FA9F0` when its prologue matches, otherwise by scan, with its measure calls found by their argument setup;
1. the writer at `D2R+0xCBEB0` when its prologue matches or D2RLoader reports another plugin's hook there, otherwise a scan for its prologue confirmed by the formatter that calls it;
2. the formatter's call at `D2R+0x1FAA18` when it calls the writer, otherwise the single formatter call to it found by scan; when neither exists, the writer entry.

Load order matters for the two places: if a call-site plugin such as Controller QOL loads after this one and expects the formatter to call the writer directly, it will find the call already redirected.

If anything doesn't match, the plugin loads but does nothing. A ground item is tagged only when its unit is an item, the label record carries the same unit id, and item flag `0x00400000` (ethereal) is set.

## Build

`sh build.sh` on Linux with MinGW-w64 (`x86_64-w64-mingw32-g++`, `windres`). It clones the D2RLoader PluginSDK at commit `03a5af9` into `.sdk/`, or uses `D2RL_SDK=<path>`.
