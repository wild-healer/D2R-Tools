# I Hate TOMLs 1.1.0: release texts

## Patch notes

```
**I Hate TOMLs 1.1.0**

**New**

* Update check: plugins with a newer version on the D2RLoader Hub get a button in the list that opens their Hub page.
* chat_updates setting: the chat says how many plugins can be updated, once per launch, at the first game.
* check_updates setting: turns the Hub check off.
* I Hate TOMLs has its own entry, first in the list.
* The view moves to the right while the window is open, as with the game's left panels.

**Fixed**

* Opening the character screen, a waypoint or another left panel now closes the window.
* Scrolling a large config became slow after a while.
* Comments drawn with box lines, as in the Item Sorting config, overlapped the rows below.

**Removed**

* The "no settings" label in the list.
```

## GitHub release

- Tag: `i-hate-tomls-v1.1.0`
- Title: `I Hate TOMLs 1.1.0`
- Asset: `i-hate-tomls.zip`

Body:

```markdown
**I Hate TOMLs** 1.1.0: Hub update check, camera like the game's left panels, scrolling fix.

### New
- Update check at each launch, in the background, from the D2RLoader Hub's public list. A plugin with a newer version gets an "x.y.z out" button that opens its Hub page. Plugins are found by their Hub URL, built from their name; a joined plugin can give its own with `hub = "<slug>"` in its schema.
- `chat_updates` (default on): "N plugin(s) can be updated" in the chat once per launch. `check_updates = false` turns the check off.
- I Hate TOMLs has its own entry, first in the list, with these settings.
- The game view moves to the right while the window is open, as the game does for its left panels.

### Fixed
- Left panels (character, waypoints, quests...) now close the window; Alt no longer does.
- Long scrolling in a large config made every view slow: each view took a widget handle for good and the loader's limit was reached. Views now reuse the same few names.
- Box line characters, arrows and curly quotes in TOML comments are drawn as plain characters (the game switched to a bigger fallback font and rows overlapped).

### Removed
- The "no settings" label.
```

# I Hate TOMLs 1.0.1: release texts

## Patch notes

```
**I Hate TOMLs 1.0.1**

**New**

* Table setting type for plugins: a grid of check boxes, one per row and column.

**Fixed**

* Scrolling or clicking fast in the window could crash the game after a while.
* The position reset button now uses the game's own refresh button, as in Grail On!.
```

## GitHub release

- Tag: `i-hate-tomls-v1.0.1`
- Title: `I Hate TOMLs 1.0.1`
- Asset: `i-hate-tomls.zip`

Body:

```markdown
**I Hate TOMLs** 1.0.1: table settings, and a crash fix.

### New
- `type = "table"` settings: a grid of check boxes. `rows` and `columns` (with optional `row_labels` and `column_labels`) name the boxes; each one is the bool `<row>.<column>` for get and set. See `include/i_hate_tomls_client.h`.

### Fixed
- Fast scrolling or clicking could pile up windows until the game crashed. Refreshes are now combined, wait for the previous one to load, and windows left behind are unloaded.
- The X/Y position reset button is the game's lobby refresh button.
```

# I Hate TOMLs 1.0.0: release texts

## Patch notes

```
**I Hate TOMLs 1.0.0**

**New**

* One in-game window, drawn like the character screen, listing every running plugin with a search bar.
* Plugins that support it get a settings page with check boxes, drop-down lists, numbers with Shift and Ctrl steps, X/Y positions with a reset, text boxes and plain text lines.
* Plugins with their own settings window open it from the list.
* Every other plugin's TOML config can be edited in the window: comments, spacing and order are kept, and a .bak copy is written before Apply.
* Every plugin config is copied to <game>\d2rloader\I Hate TOMLs\<mod>\ and written back at each start, so a D2RMM install no longer resets your settings.
* Open it from the Controls menu (no default key) or the console command pluginmenu.
```

## GitHub release

- Tag: `i-hate-tomls-v1.0.0`
- Title: `I Hate TOMLs 1.0.0`
- Asset: `i-hate-tomls.zip`

Body:

```markdown
**I Hate TOMLs** 1.0.0: every plugin's settings in one in-game window, and configs that survive D2RMM installs.

### Features
- A list of every running plugin, with a search bar and a gear button per plugin.
- Joined plugins (green) get a real settings page: check boxes, drop-down lists, numbers with Shift/Ctrl steps, X/Y positions, text boxes, actions and plain text.
- Plugins with their own window (blue) open it from the list.
- Any other plugin's TOML (white) is edited in place: only changed values are rewritten, a `.bak` copy is kept, changes apply at the next start.
- Config copies in `<game>\d2rloader\I Hate TOMLs\<mod>\`: written back at each start, refreshed while you play.

### Install
Add the zip through D2RMM, or copy `aaa-i-hate-tomls.dll` to `<game>/d2rloader/plugins/`. Needs D2RLoader 1.3.1.

### For plugin authors
Copy `include/i_hate_tomls_client.h` into your plugin and call `Connection::Start` with a TOML schema of your settings. Without I Hate TOMLs your plugin keeps working from its TOML.
```
