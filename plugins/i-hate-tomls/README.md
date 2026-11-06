# I Hate TOMLs

Plugin id `i-hate-tomls`: `aaa-i-hate-tomls.dll` in `i-hate-tomls.zip`, downloaded from its [D2RLoader Hub page](https://d2rloader.net/hub/plugins/i-hate-tomls).

Plugin authors: see [INTEGRATION.md](INTEGRATION.md) to add a settings page to a plugin.

One in-game window, drawn like the character screen, listing every running plugin (I Hate TOMLs itself first).
It opens on the left like the game's left panels: it closes them, a left panel that opens closes it, and the
view moves to the right while it is open, as the game does for its own panels.

- Open: Controls menu, *I Hate TOMLs > Open I Hate TOMLs* (no default key), or the console command `pluginmenu`.
- A search bar under the plugin count keeps the plugins whose name, id or author has the typed text.
- Every view scrolls: mouse wheel over the window, the scroll bar on its right (click or drag).
- Text boxes: click one and type (Enter keeps the text, Esc puts it back, a click elsewhere keeps it). The game's
  own text boxes don't get the keys in a plugin window, so a keyboard hook takes them while a box is active.
- Each plugin has a status gem and a gear button that opens its settings:
  - green, *this menu*: the plugin joined with `include/i_hate_tomls_client.h` (`Connection::Start`): a TOML list
    of its settings and get/set callbacks. Its page has check boxes, drop-down lists, numbers with `<` `>`
    (Shift and Ctrl steps), X/Y positions with a reset, tables of check boxes (one bool per row and column), text boxes, action buttons, section titles and plain text; the plugin keeps its own values and config, so without I Hate TOMLs it works from its TOML.
  - blue, *own window*: the plugin has its own settings window and declared it (`Connection::StartInterface`);
    the gear closes the menu and calls the plugin.
  - white, *TOML file*: any other plugin with `config/<id>.toml`. The page shows the file: comments as text,
    `[tables]` as titles, each value in its editor. Characters the game's font lacks (box lines, arrows, curly
    quotes) are shown as plain ones. Apply writes the changed values only (comments, spacing
    and order stay), after copying the file to `<id>.toml.bak`; plugins read it at the next start.
  - no gem and no gear: no config file.

TOML values and their editors:

| TOML | Editor |
| --- | --- |
| boolean | check box |
| string (basic, literal, multi-line) | text box; a string with line breaks shows them as `\n` |
| integer (decimal, hex, octal, binary), float (incl. inf, nan) | text box, checked on Apply |
| offset date-time, local date-time, local date, local time | text box, checked on Apply |
| array of simple values | one text box per item, `+` to add, `x` to remove |
| array of arrays or tables, inline table | one text box with the TOML text |
| any value written over several lines (except simple arrays) | shown, edited in the file |

`i-hate-tomls-demo` (separate zip) has one setting of each kind and a second plugin that declares its own window.

The window's art is the game's: the character screen background (stat boxes covered with its own stone), the loot
filter list's rows and check box, the drop-down box frame for text boxes, buttons and lists, the loot filter list's arrows, the lobby's refresh button (position reset), the stash slot (gear buttons, scroll bar).
The gear is the "cog" icon of [game-icons.net](https://game-icons.net) (CC BY 3.0, by its contributors).

Build: `sh build.sh` (MinGW-w64, D2RLoader PluginSDK 03a5af9).

## Updates

Once per launch, in the background, I Hate TOMLs reads the D2RLoader Hub's public plugin list
(`d2rloader.net/api/v1/hub/plugins`, a few requests, no login). A plugin with a newer version there
gets an "x.y.z out" button in the list that opens its Hub page. Each plugin is found by its Hub URL, built
from its name the way the Hub builds it: lowercase, every run of other characters becomes one `-`.
A joined plugin whose Hub URL differs gives it in its schema (`hub = "<slug>"`). Turn the check off with
`check_updates = false` in `i-hate-tomls.toml`, or on I Hate TOMLs' own page.
When updates are out, the chat says "N plugin(s) can be updated" once per launch, at the first game
(`chat_updates = false` turns it off).

## Config copies

A D2RMM install rebuilds the mod's folder, which resets every plugin config.
I Hate TOMLs keeps a copy of each config (`mods\<mod>\d2rloader\config\*.toml`)
in `<game>\d2rloader\I Hate TOMLs\<mod>\`, outside D2RMM's reach.

- At load, the stored values are written back onto each config, key by key.
  New settings and comments from a plugin update stay; a setting that no
  longer exists is dropped. A config missing from the folder is restored whole.
- The DLL is named `aaa-i-hate-tomls.dll` so it loads before the other plugins
  read their configs.
- While the game runs, any config that changes (in-game settings, the TOML
  editor, a plugin's own writes) is copied again within a few seconds.
- A config edited by hand while the game is closed is overwritten by the copy
  at the next start; edit it with the game running, or in the menu.
