# Stash Tab Dropdown 1.4.1: patch notes

## GitHub release

- Tag: `stash-tab-dropdown-v1.4.1`
- Title: `Stash Tab Dropdown 1.4.1`
- Asset: `stash-tab-dropdown-1.4.1.zip`

Body:

```markdown
### Fixed
- With a list that doesn't fill its last row, or without < or >, the empty spots blocked the mouse: items under them showed no details on hover. The list now only holds the pages your stash has, so nothing invisible sits over the stash. After changing the shared page count (Options slider), restart the game to update the list.
```

## Nexus Mods changelog (1.4.1)

```
**Stash Tab Dropdown 1.4.1**

**Fixed**

* Empty spots of the page list (after the last page, or where < and > are hidden) no longer block hovering the items under them. After changing the shared page count, restart the game to update the list.
```

# Stash Tab Dropdown 1.4.0: patch notes

## GitHub release

- Tag: `stash-tab-dropdown-v1.4.0`
- Title: `Stash Tab Dropdown 1.4.0`
- Asset: `stash-tab-dropdown-1.4.0.zip`

Body:

```markdown
### New
- **List button on every stash tab** (`stash_button_all_tabs = true`): the button also shows on the personal and materials tabs. Picking a page there switches to the shared tab and goes straight to that page.
- **Scale the page list** (`list_scale`, default 1.0): scales the squares, spacing and numbers from the list's top-left corner, e.g. 0.75 for a smaller list.
- **Escape closes the stash** (`escape_closes_list = false`): Escape closes the stash, and the list with it, instead of only the list. `keep_open` and `remember_open` work as before.

### Removed
- `button_width`, `button_height`, `button_scale` and `font_size`: `list_scale` replaces them. They are ignored if still in your config.
```

## Nexus Mods changelog (1.4.0)

```
**Stash Tab Dropdown 1.4.0**

**New**

* stash_button_all_tabs setting: show the list button on the personal and materials tabs too. Picking a page there switches to the shared tab and goes straight to that page.
* list_scale setting: scale the whole page list, e.g. 0.75 for a smaller list.
* escape_closes_list setting: set it to false so Escape closes the stash (and the list with it) instead of only the list. keep_open and remember_open work as before.

**Removed**

* button_width, button_height, button_scale and font_size settings: list_scale replaces them.
```

# Stash Tab Dropdown 1.4.0: description (GitHub README / Nexus)

```markdown
**Stash Tab Dropdown** is a D2RLoader plugin that jumps straight to any shared stash page instead of clicking the arrows one page at a time.

### Features

* **A list button next to the shared stash page arrows.** It opens a grid of every page over the stash, 10 per row and 12 rows per screen, with < and > to move between screens when you have more than 120 pages.
* **Click a page to switch to it instantly**, even across hundreds of pages.
* **A keybind for the list**: bind "Open/close the stash page list" in Options > Controls (unbound by default).
* **Keep the list open** if you like (`keep_open`), to hop between a few pages or move items from page to page, and have it **remembered** (`remember_open`), so it opens again with the shared tab, even after restarting the game.
* **On every stash tab** if you like (`stash_button_all_tabs`): picking a page from the personal or materials tab switches to the shared tab and goes straight to it.
* **Resize the list** with `list_scale`, and choose how many pages per row with `columns`.
* **Escape closes the stash** instead of only the list if you prefer (`escape_closes_list = false`).
* **Page names and colours** from Shared Stash Page Text: each number shows in its page's colour and the page name is its tooltip. You can also name pages in this plugin's own config.

### Requirements

* D2RLoader 1.3.1.

### Install

Load `stash-tab-dropdown-1.4.0.zip` in D2RMM, or copy `d2rl-stash-tab-dropdown.dll` to `<game>/d2rloader/plugins/`. Settings are in `d2rloader/config/stash-tab-dropdown.toml`, written on first launch. Edit that copy, not the one in the zip, and restart the game after changing it.

### Console

`stash-goto <n>`, `stash-page [n]`.

### Known limits

* Instant switching uses D2RLoader 1.3.1's internals, the same way Stash Search does. With another D2RLoader version the plugin falls back to clicking the page arrows for you, and the stash flips through the pages on the way (`instant_jump = false` does the same).
* The button and list positions were set with the Expanded Stash Plus (RoTW) stash layout. Another stash layout mod may need `stash_button_x`/`stash_button_y` and `list_x`/`list_y` adjusted in the config. If someone has an issue with this and finds settings that work, please send them to me so I can include config settings.
```

# Stash Tab Dropdown 1.3.0: patch notes

## GitHub release

- Tag: `stash-tab-dropdown-v1.3.0`
- Title: `Stash Tab Dropdown 1.3.0`
- Asset: `stash-tab-dropdown-1.3.0.zip`

Body:

```markdown
### New
- **Keybind for the page list**: bind "Open/close the stash page list" in Options > Controls (category Stash Tab Dropdown, unbound by default). It opens the list while the stash shows the shared tab, and closes it.
```

## Nexus Mods changelog (1.3.0)

```
**Stash Tab Dropdown 1.3.0**

**New**

* Keybind to open and close the page list: bind "Open/close the stash page list" in Options > Controls (category Stash Tab Dropdown, unbound by default).
```

# Stash Tab Dropdown 1.2.0: patch notes

## GitHub release

- Tag: `stash-tab-dropdown-v1.2.0`
- Title: `Stash Tab Dropdown 1.2.0`
- Asset: `stash-tab-dropdown-1.2.0.zip`

Body:

```markdown
### New
- **Instant jumps**: picking a page switches the stash straight to it, the way Stash Search does, instead of clicking the page arrows. No more flipping through the pages on the way. Needs D2RLoader 1.3.1 on Steam 3.3.93787 or Battle.net 3.3.93847; anywhere else, or with `instant_jump = false`, the plugin clicks the arrows as before.
```

## Nexus Mods changelog (1.2.0)

```
**Stash Tab Dropdown 1.2.0**

**New**

* Instant jumps: picking a page switches the stash straight to it instead of clicking the page arrows (instant_jump, on by default). Needs D2RLoader 1.3.1 on Steam 3.3.93787 or Battle.net 3.3.93847; elsewhere the arrows are clicked as before.
```

# Stash Tab Dropdown 1.1.0: patch notes

## GitHub release

- Tag: `stash-tab-dropdown-v1.1.0`
- Title: `Stash Tab Dropdown 1.1.0`
- Asset: `stash-tab-dropdown-1.1.0.zip`

Body:

```markdown
### Added
- **Keep the list open** (`keep_open = true` in `stash-tab-dropdown.toml`): the page list stays open after you pick a page, so you can hop between a few pages or move items from page to page. Escape, switching tabs or the list button still close it.
- **Remember the list** (`remember_open = true`, needs `keep_open`): if you leave the list open, it opens again whenever the shared tab shows, also after restarting the game.

### Fixed
- Settings written below a `[pages.N]` section in the config are no longer ignored.
```

## Nexus Mods changelog (1.1.0)

```
**Stash Tab Dropdown 1.1.0**

**New**

* Keep the page list open after picking a page (keep_open), to hop between a few pages or move items from page to page.
* Remember the open list (remember_open, needs keep_open): it opens again with the shared tab, also after restarting the game.

**Fixed**

* Settings written below a [pages.N] section in the config were ignored.
```

# Stash Tab Dropdown 1.0.1: patch notes

## GitHub release

- Tag: `stash-tab-dropdown-v1.0.1`
- Title: `Stash Tab Dropdown 1.0.1`
- Asset: `stash-tab-dropdown-1.0.1.zip`

Body:

```markdown
### Fixed
- The page list sometimes showed a full screen of 120 pages when the stash has fewer. Pages past your page count now stay hidden.
- After starting a new game, the list no longer reuses what it had hidden in the previous game.
```

## Nexus Mods changelog (1.0.1)

```
Fixed: the page list sometimes showed a full screen of 120 pages when the stash has fewer.
Fixed: after starting a new game, the list no longer reuses what it had hidden in the previous game.
```

# Stash Tab Dropdown 1.0.0: release texts

## GitHub release

- Tag: `stash-tab-dropdown-v1.0.0`
- Title: `Stash Tab Dropdown 1.0.0`
- Asset: `stash-tab-dropdown-1.0.0.zip`

Body:

```markdown
First release of **Stash Tab Dropdown**, a D2RLoader plugin that jumps straight to any shared stash page instead of clicking the arrows one page at a time.

### Features
- **A list button next to the shared stash page arrows.** It opens a grid of every page over the stash, 10 per row and 12 rows per screen, with < and > to move between screens when you have more than 120 pages.
- **Click a page to jump to it**, even across hundreds of pages, in well under a second.
- **Page names and colours** from Shared Stash Page Text: each number shows in its page's colour and the page name is its tooltip. You can also name pages in this plugin's own config.
- **The current page is highlighted** and follows you live, whether you change pages with the arrows, your keybinds or the list.
- **Follows your page count**: RotW's 5 shared pages plus the extra pages from D2RLoader's shared stash slider, re-read each time the list opens.
- No D2RMM mod needed: the button is drawn by the plugin itself.

### Requirements
- D2RLoader 1.3.1 or later.
- Steam 3.3.93787 or Battle.net 3.3.93847 (both tested). The plugin reads the stash's tab and page from the game and checks that code when it loads: on any other build it stays out of the way, shows no button, and only the `stash-goto` console command works.
- Single player or TCP/IP.

### Install
Load `stash-tab-dropdown-1.0.0.zip` in D2RMM, or copy `d2rl-stash-tab-dropdown.dll` to `<game>/d2rloader/plugins/`. Settings are in `d2rloader/config/stash-tab-dropdown.toml`, written on first launch.

### Console
`stash-goto <n>`, `stash-page [n]`.

### Known limits
- There is no "go to page" in the game, so a jump clicks the page arrows for you; the stash flips through the pages on the way.
- The button and list positions were set with the Expanded Stash Plus (RoTW) stash layout. Another stash layout mod may need `stash_button_x`/`stash_button_y` and `list_x`/`list_y` adjusted in the config.
```

## Nexus Mods

Name: `Stash Tab Dropdown (D2RLoader plugin)`

Summary (short description):

> Adds a button to the shared stash that lists every page with its name and colour, and jumps straight to the one you click.

Description (BBCode):

```bbcode
[size=5][b]Stash Tab Dropdown[/b][/size]
A D2RLoader plugin that jumps straight to any shared stash page instead of clicking the arrows one page at a time.

[size=4][b]Features[/b][/size]
[list]
[*][b]List button[/b] next to the shared stash page arrows. It opens a grid of every page over the stash: 10 per row, 12 rows per screen, with < and > between screens past 120 pages.
[*][b]One click to any page[/b], even hundreds of pages away, in well under a second.
[*][b]Page names and colours[/b] from Shared Stash Page Text: numbers in each page's colour, the page name as the tooltip. Pages can also be named in this plugin's config.
[*][b]Current page highlighted[/b], following you live whether you use the arrows, your keybinds or the list.
[*][b]Any page count:[/b] RotW's 5 shared pages plus D2RLoader's extra pages, up to 1000.
[*]No D2RMM mod needed.
[/list]

[size=4][b]Requirements[/b][/size]
[list]
[*]D2RLoader 1.3.1 or later
[*]Steam 3.3.93787 or Battle.net 3.3.93847 (both tested). On other builds the plugin shows no button and leaves the game alone.
[*]Single player or TCP/IP
[/list]

[size=4][b]Installation[/b][/size]
Load the zip with D2RMM, or copy [i]d2rl-stash-tab-dropdown.dll[/i] to [i]<game>/d2rloader/plugins/[/i].
Settings are in [i]d2rloader/config/stash-tab-dropdown.toml[/i], written on first launch. Page names are read from Shared Stash Page Text's config when it is installed.

[size=4][b]Usage[/b][/size]
Open the stash, go to the shared tab and click the button right of the page arrows. Click a page to go there. Escape, switching tabs or the button again closes the list.

[size=4][b]Console commands[/b][/size]
[list]
[*][b]stash-goto <n>[/b]: go to page n
[*][b]stash-page [n][/b]: show the current page, or correct it
[/list]

[size=4][b]Known limits[/b][/size]
[list]
[*]The game has no "go to page", so a jump clicks the page arrows for you and the stash flips through the pages on the way.
[*]Positions were set with the Expanded Stash Plus (RoTW) stash layout. Another stash layout mod may need the button and list positions adjusted in the config.
[/list]
```
