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
