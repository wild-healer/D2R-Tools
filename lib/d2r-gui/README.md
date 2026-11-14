# D2R GUI

Version 0.0.1 (not released, untested in game).

The in-game window parts shared by the D2RLoader plugins in this repo, taken
out of Grail On! and I Hate TOMLs so a new plugin can have a window that looks
and behaves like theirs without copying a thousand lines.

What it can do, with an example: [SHOWCASE.md](SHOWCASE.md).

## How plugins use it

D2R GUI is header-only. A plugin includes `d2r_gui.h` in its `.cpp`, and the
library compiles into the plugin's own DLL. Nothing else is installed and
nothing is shared between plugins at run time: each DLL has its own copy, so
a plugin still works alone and two plugins on different library versions do
not affect each other.

Add the include path in the plugin's `build.sh`:

```sh
gui=$here/../../lib/d2r-gui/include
x86_64-w64-mingw32-g++ -std=c++20 ... -I"$sdk/include" -I"$gui" src/plugin.cpp ...
```

The D2RLoader SDK is the same one the plugins use (PluginSDK 03a5af9).

`plugins/d2r-gui-demo` is a small plugin built only from the library: a window
with a search box, a check box, a drop-down, a number with arrows, a reset
button and a long scrolling list. Console command `guidemo`.

## Parts

| Header | What it gives | Taken from |
|---|---|---|
| `core.h` | `Init` (services, plugin id), `Log`, `RunOnUi`, `Readable` | both |
| `text.h` | `Json`, `GameText` (text the game's font can draw), `Fit`, UTF-8/16 | I Hate TOMLs |
| `picture.h` | RGBA `Picture`, sprite read/write (SpA1 v31), stone, crop, paste, blend, both scaling families | both |
| `game_files.h` | `ArchiveFile` (the game's files, mods included), `FindRefreshButton`, layout JSON readers | both |
| `resources.h` | `RegisterResource`, `RegisterSprite` (+ `.lowend`), `SpriteName` | both |
| `art.h` | slots, slot buttons, gear and reset buttons, check box, scroll thumb, gem, gold rule | both |
| `kit.h` | the game's own settings art (drop-down box and list, loot filter rows and arrows, close and refresh buttons) as sprites of any size, made on first use | both |
| `layout.h` | widget JSON: text, image, button, text button, invisible button, check box, clip view, panel, left-panel frame, styles | both |
| `commands.h` | button clicks to a handler on the UI thread (the game's double report dropped) | both |
| `panel_pool.h` | a window whose layout changes while open (panel pool, coalesced rebuilds, reuse probe and fallback) | I Hate TOMLs |
| `text_input.h` | typing into text boxes (keyboard hook + hidden EDIT control) | both |
| `window_hook.h` | wheel, scroll bar drag, clicks off a text box, a periodic watch | both |
| `scroll_bar.h` | a scroll bar with steps, thumb, drag | both |
| `font.h` | the game's font metrics and text sizes, Low Vision Mode | I Hate TOMLs |
| `widgets.h` | text box, drop-down, number with arrows, section title | both |
| `left_panel.h` | behave like a game left panel: close them, close for them, shift the view | I Hate TOMLs |
| `json_reader.h` | a small JSON reader for the game's data files | Grail On! |
| `png.h` | PNG decoding (art compiled into the DLL) | Grail On! |
| `strings.h` | a plugin's translated strings table, all 13 game languages | Grail On! |
| `hud.h` | hide the life number on the HUD; whether the stash is open | Grail On! |
| `inventory_button.h` | a button on the inventory panel, its mold, moved by settings | Grail On! |
| `item_art.h` | item colours, quality backgrounds, socket spots, rune stones | Grail On! |
| `stash_art.h`, `stash_kit.h` | the stash and settings art at any size (ticks, arrows, boxes, tiles, table rows, chart) | Grail On! |
| `item_grid.h` | the plugin's item grid drawn its way: half-cell tiles, peeking past the grid, no cannot-wear colour | Grail On! |
| `panels.h` | `SinglePanel` (one panel, a new layout per show) and `ViewCache` (many panels that each keep a view) | Grail On! |
| `icons.h` (opt-in) | 120 game-icons.net icons at any size and colour, as pictures or sprites ([preview](docs/icons.png), [credits](ICONS-CREDITS.txt)) | Grail On! |
| `stash_window.h` (opt-in) | a window in the stash's look (frame, gold title, close button), as Charm Inventory's | Charm Inventory |

## Rules the library follows

- Only fixed names are looked up with `findPanel`/`findWidget`: each lookup
  takes one of the loader's ~255 widget handles a plugin for good. The panel
  pool's probes are `Probe0` and `Probe1`. Plugins should name their own
  widgets the same way view to view (`Row0`, `Row1`, ... by position on
  screen, not by what they show) if they ever look them up.
- Panels can only be registered while the plugin loads: `PanelPool::Register`
  belongs in `D2RLoaderLoadPlugin`.
- No D2R version check. Calls into the game (`ArchiveFile`, the UI states,
  the view shift, Low Vision Mode) look at the code they call first and skip
  only that part when it is not there.
- Art falls back to the plugin's own drawing when the game's can't be read,
  so a window never needs a companion mod.

## Status

I Hate TOMLs (1.1.1-dev-1) and Grail On! (1.2.2-dev-1) draw everything
through the library; neither has window code of its own left.

## Versioning

0.x until its first release; the version is `D2R_GUI_VERSION` in `core.h`.
