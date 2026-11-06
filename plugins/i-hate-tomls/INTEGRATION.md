# Adding I Hate TOMLs settings to a plugin

I Hate TOMLs (plugin id `i-hate-tomls`) shows a page of settings for every plugin that joins it.
A plugin joins it with one header file and two callbacks. If I Hate TOMLs is not installed, the
plugin works as before, from its own TOML.

Plugins that don't join are still listed. If they have a `config/<id>.toml`, the menu shows that
file as an editor. Joining gives a proper page instead: labels, drop-down lists, number arrows,
position pickers, check box tables, and changes that apply while the game runs.

## 1. Copy the header

Copy [`include/i_hate_tomls_client.h`](include/i_hate_tomls_client.h) into the plugin's sources.
It is header only and needs nothing else but the D2RLoader SDK. It talks to I Hate TOMLs through
D2RLoader's plugin communication service (`settings-menu`, version 1).

## 2. Start and stop the connection

```cpp
#include "i_hate_tomls_client.h"

PluginMenu::Connection Menu;

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	// ... the plugin's own setup, config read first ...
	Menu.Start(context, Schema, Get, Set, nullptr);
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept { Menu.Stop(); }
```

`Start` joins at once when I Hate TOMLs has already loaded. Otherwise it joins when every plugin
has loaded (the first DataTablesLoaded event). It returns true only when it joined at once, so a
false return is normal and needs no handling. `Menu.Registered()` tells whether it has joined.

## 3. Describe the settings

The schema is TOML text, copied by the menu. Each `[[setting]]` is one row, in page order.

```toml
title = "Ethereal Ground Tag"   # optional, else the plugin's name
hub = "ethereal-ground-tag"     # optional, see "Update check" below

[[setting]]
type = "header"
label = "Tag"

[[setting]]
key = "enabled"
type = "bool"
label = "Show the tag"
hint = "Shown when the mouse is over the label"
note = "Shown under the row, like a TOML comment"

[[setting]]
key = "position"
type = "enum"
label = "Tag position"
values = ["before", "after"]            # the TOML values, in order
labels = ["Before the name", "After the name"]

[[setting]]
key = "count"
type = "int"
label = "Items listed"
min = 0
max = 200
step = 1
shift_step = 5                          # Shift+click (default 10 steps)
ctrl_step = 50                          # Ctrl+click (default 100 steps)
```

### Setting types

| `type` | Row | Value given to get/set |
| --- | --- | --- |
| `bool` | check box | `true` / `false` |
| `int` | number with `<` `>` arrows (`min`, `max`, `step`, `shift_step`, `ctrl_step`) | `3` |
| `float` | same, plus `decimals` | `0.25` |
| `enum` | drop-down list (`values`, optional `labels`) | `"after"` (one of `values`) |
| `string` | text box (click, type, Enter) | `"Eth"` |
| `position` | X and Y with arrows and a reset button (`min`, `max`, `default = [x, y]`) | `[12, -40]` |
| `table` | grid of check boxes (`rows`, `columns`, optional `row_labels`, `column_labels`) | `true` / `false` for each box, key `"<row>.<column>"` |
| `action` | button (`confirm = true` asks for a second click) | `run` |
| `header` | section title (`label`) | no key |
| `text` | plain text (`label`, `\n` for a new line) | no key |

Keys every row can have: `key` (passed to get and set), `label`, `hint`, `note`. A row with
`restart = true` is marked "applies after a restart".

A table needs no `key`. Each box is its own bool:

```toml
[[setting]]
type = "table"
label = "Tag shown on"
rows = ["ground", "vendor"]
row_labels = ["On the ground", "At vendors"]
columns = ["normal", "magic", "rare", "unique"]
column_labels = ["Normal", "Magic", "Rare", "Unique"]
# -> get("ground.magic"), set("vendor.rare", "true")
```

## 4. Write get and set

```cpp
bool __cdecl Get(const char* key, char* out, uint32_t outSize, void* user) noexcept;
bool __cdecl Set(const char* key, const char* tomlValue, char* error, uint32_t errorSize, void* user) noexcept;
```

- Get writes the current value as a TOML value: `true`, `3`, `0.25`, `"Eth"`, `[0, 0]`. It returns
  false for a key it doesn't know.
- Set receives a TOML value in the same form. It checks it, applies it, saves it to the plugin's
  config, and returns true. To refuse, it writes a short reason into `error` (the menu shows it) and
  returns false.
- An `action` row calls set with the value `run`.
- Both run on the game's main thread, so set may change game state directly. Settings should apply
  at once when they can. For those that can't, use `restart = true`.
- Keep the plugin's TOML as the place its values live. The menu never stores them. I Hate TOMLs
  keeps a copy of every `config/*.toml` and writes the values back after a D2RMM install resets
  them, so a plugin that saves through its own TOML gets that for free.

The demo, [`demo/demo.cpp`](demo/demo.cpp), is a complete plugin with one setting of each kind.
Its get and set keep the values in a map and save them with `WriteConfig`.

## 5. When the plugin changes a value itself

If a setting changes outside the menu (a hotkey, a console command, the plugin's own button), call:

```cpp
Menu.ValueChanged("enabled");   // or Menu.ValueChanged() for every row
```

The menu then redraws the page if it is showing it. `Menu.Open()` opens the menu on the plugin's
own page, for example from a button in the plugin's UI.

## A plugin with its own settings window

A plugin that already has its own settings window declares it instead of a schema:

```cpp
void __cdecl OpenMyWindow(void* user) noexcept { /* open the plugin's window */ }

Menu.StartInterface(context, OpenMyWindow, nullptr);
```

The list shows it with a blue gem. Its gear button closes the menu and calls `OpenMyWindow` on the
game's main thread.

## Update check

At each launch, I Hate TOMLs compares each plugin's version with its page on the D2RLoader Hub.
It finds the page by building the Hub's address from the plugin's name: lowercase, with every run
of other characters turned into one `-` ("Wild Healer's Charm Inventory" becomes
`wild-healer-s-charm-inventory`). The Hub keeps the address from the plugin's first upload, so a
plugin that was renamed since, or whose address differs for another reason, gives its own with
`hub = "<slug>"` at the top of its schema.

## Checklist

- [ ] `i_hate_tomls_client.h` copied, `Menu.Start(...)` at load, `Menu.Stop()` at unload.
- [ ] Every `key` in the schema is answered by get and accepted by set.
- [ ] Set applies the value, saves the plugin's TOML, and refuses bad values with a reason.
- [ ] `Menu.ValueChanged(key)` wherever the plugin changes a setting itself.
- [ ] `hub = "..."` if the plugin's Hub address isn't built from its current name.
- [ ] The plugin still works with I Hate TOMLs removed.
