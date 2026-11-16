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

## The standard: declare the window, fill the data

A plugin describes its window and the library does everything else. This is
how every plugin integrates:

```cpp
#include <d2r_gui.h>
#include <d2r_gui/icons.h>
#include <d2r_gui/app.h>
namespace App = D2RGui::App;

bool ShowOdd = true; int Mode = 0, Count = 10; std::string Name;

App::Window window {
    .title = "My Plugin",
    .menus = {
        { .tab = {"scroll-unfurled", "Rules"}, .items = { App::Text("How it works...") } },
        { .tab = {"cog", "Settings"}, .pages = {
            { .tab = {"eyeball", "Display"}, .items = {
                App::Section("Display"),
                App::Check("Show odd rows", &ShowOdd).Tip("Shows or hides them"),
                App::Choice("Mode", &Mode, {"Fast", "Safe"}),
                App::Number("Count", &Count, 0, 99),
                App::Field("Name", &Name, "Type a name") } } } } },
    .console = "myplugin",
    .keybind = "Open My Plugin",
    .changed = [] { SaveSettings(); },
};

// D2RLoaderLoadPlugin:   App::Start(context, "my-plugin", std::move(window));
// D2RLoaderUnloadPlugin: App::Stop();
// data changed elsewhere: App::Refresh();
```

- **Frame**: `.frame = App::Frame::Stash` (default) or `App::Frame::Character`.
- **Menus and pages**: a row of icon tabs for the menus, a row for the current
  menu's pages. A menu without pages is a page. Each menu remembers its last page.
  A window without menus takes `.items` (or `.content`) directly.
- **Components**: `Section`, `Text`, `Check`, `Choice` (long lists over 12
  entries, or `.Filter()`, scroll and get a filter box; `Entries` adds section
  headers), `Number` (int or double, `.Decimals`, `.Step`, Shift and Ctrl
  steps, `.Reset`), `Position` (X and Y with arrows and a reset), `Table` (a
  check box per row and column, cells disabled with a reason), `Field`
  (`.Draft`: Enter saves, Esc cancels; `.Clearable`), `Value` (read-only),
  `Action` (`.Arrow`, `.Confirm`), `Adder`, `List` (rows with an icon, two
  lines, a value or a button, a gear; `.Empty`), `Cells` (the window's
  `.cells`), `HostGrid` (a Player Page Host area's grid), `Custom`. Each takes
  `.Tip()`, `.Note()`, `.Restart()`, `.Remove()`, `.OnChange()`, `.When()`.
- **Data**: components point at the plugin's variables, or take
  `App::Bound(get, set)` when the value lives elsewhere. A set can refuse a
  value: the reason shows in the window's notice line (`App::Notice` too).
  A page whose components depend on the data gives `.content`, a function
  called at every view.
- **Fixed parts**: `.header` and `.footer` on the window, a menu or a page
  stay outside the scrolled part. `App::Push(page)` opens a page with a back
  arrow, a title and a subtitle; `App::Back()`, `App::OpenOn(page)`. Each
  level keeps its scroll.
- **On a Player Page Host area**: `.host = {layout, open, close, isOpen}` makes
  the window the area's panel (one view, `@PANEL@`, `@GRID@`).
- **Handled by the library**: the views (panel pool), clicks, typing, open
  drop-downs, scrolling (bar, drag, wheel), the game's left-panel behaviour,
  `Kit::Read` and `Font::Measure` once the tables load, the console command,
  the key binding, an inventory button (`.inventory`), and a tick about 4 times
  a second (`.tick`).
- **Hooks for the rest**: `.command` (Custom and list row buttons), `.opened`,
  `.loaded`, `.inGame`, `.mouse` (`.mouseAlways`: from the first game on). `App::Go(menu, page)`, `App::SetMenus`,
  `App::Button()` (the inventory button).

The parts below stay available for windows App can't describe, but a plugin
asks this library for a missing component instead of building it.

`plugins/d2r-gui-demo` is the example: its whole window is one `App::Window`
(an About page, settings pages with each component, a long filtered list with
headers, a searchable clickable list). Console command `guidemo`.

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
| `tabs.h` (opt-in) | a row or column of icon tabs, as Grail On!'s menus | Grail On! |
| `menus.h` (opt-in) | a window's menus and pages as rows of icon tabs, each menu remembering its page | Grail On! |
| `app.h` (opt-in) | the declared window: frame, menus, pages and components bound to the plugin's data | new |
| `item_cells.h` (opt-in) | single item cells that each take one item code (an offering), on Player Page Host's page or the plugin's own | new |
| `page_host_api.h` | Player Page Host's client header (used by item_cells.h) | Player Page Host |

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
