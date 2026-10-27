# Stash Tab Dropdown

Adds a button next to the shared stash's page arrows. The button opens a grid of small buttons, one per shared stash page, showing the page number in the page's colour; hover one for the page's name (pages without a name have no tooltip), click it to jump straight to that page.

It is one D2RLoader plugin (`d2rl-stash-tab-dropdown.dll`). It reads from the game whether the stash shows the shared tab, and shows its list button just right of the page arrows while it does. It builds the page list and does the jump. The game has no "go to page n" message, so the plugin sends the stash's own left/right arrow messages, then checks where it landed.

Page names come from Shared Stash Page Text's `shared-stash-page-text.toml` when it is installed, in their colours, and from this plugin's own `[pages.N]` entries, which win.

No native hooks, and only D2RLoader services that exist in D2RLoader 1.3.1. Reading the stash's tab and page calls game functions at fixed addresses, checked at load against the code of Steam 3.3.93787 and Battle.net 3.3.93847 (identical there). On other builds the plugin loads without it, and `stash-goto <n>` in the console still jumps.

## Install

1. Plugin: get Stash Tab Dropdown from the [D2RLoader Hub](https://d2rloader.net/hub/creators/thewildhealer), put its zip in D2RMM's `d2rloader` folder and refresh plugins.
2. Apply mods and start the game.

## Use

Open the stash, go to the shared tab and click the button next to the right arrow. Pick a page (the list closes, unless `keep_open = true`). Escape (unless `escape_closes_list = false`, then Escape closes the stash), switching tabs, or clicking the button again closes the list. To open and close it from the keyboard, bind "Open/close the stash page list" in Options > Controls (under Stash Tab Dropdown; unbound by default). The highlighted square is the page you are on.

Console commands:

- `stash-goto <n>` goes to page n (the stash must be open on the shared tab).
- `stash-page` prints the page the plugin thinks you are on; `stash-page <n>` corrects it.

## Many pages

The page count is read from D2RLoader's own config: RotW's 5 shared pages plus `add_shared_tabs` (the Options slider), read when the game starts: the list only holds the pages the stash has, so after changing the slider, restart the game. With more pages than fit on one screen (120 by default), the list shows one screen at a time, opens on the screen holding your current page, and has < and > buttons beside the first row to move between screens. Jumps are instant where the plugin reads the page from the game and D2RLoader is 1.3.1: it sets the stash page directly and has the game reload the grid, the way Stash Search does. Elsewhere it clicks the arrows for you, several per frame, so even a jump across 400 pages takes well under a second.

## How the current page is known

On Steam 3.3.93787 and Battle.net 3.3.93847 the plugin reads the current page from the game itself, the same way Shared Stash Page Text does, about 20 times a second. That follows pages turned with the arrows, with keybinds, or by any other plugin, and the open list moves its highlight along. Jumps then send exactly the steps needed and check where they landed.

On other builds the plugin counts the arrow messages instead, starting at page 1 whenever the stash is loaded for a new game. Stash keybinds send no message, so pages turned with them are missed there; jumping to page 1 or the last page puts the count right (those jumps send a full page count of steps), and so does `stash-page <n>`. Set `read_game_page = false` to always count.

## Config

`d2rloader/config/stash-tab-dropdown.toml` (created on first launch). Layout settings take effect after a game restart.

| Key | Default | |
| --- | --- | --- |
| `use_page_text_names` | true | Read names from `shared-stash-page-text.toml` |
| `keep_open` | false | Keep the list open after picking a page |
| `remember_open` | false | With `keep_open`: if the list was left open, open it again with the shared tab, also after a restart |
| `columns` | 10 | Squares per row |
| `rows` | 12 | Rows per screen; more pages than `columns` x `rows` get < and > buttons to switch screens |
| `list_x`, `list_y` | 20, 196 | Top-left of the list (the < button), in stash layout units |
| `list_scale` | 1.0 | Scales the whole list (squares, spacing and numbers) from its top-left corner |
| `column_width`, `row_height` | 132, 104 | Button spacing |
| `button_sprite` | (empty) | A game sprite to use instead of the plugin's own image |
| `normal_frame`, `hovered_frame`, `pressed_frame` | 0, 1, 2 | Sprite frames |
| `stash_button` | true | Show the plugin's own list button on the shared tab |
| `escape_closes_list` | true | Escape closes the list; when false, Escape closes the stash (and the list with it) |
| `stash_button_all_tabs` | false | Show the button on the personal and materials tabs too; picking a page there switches to the shared tab first (needs `instant_jump`) |
| `stash_button_x`, `stash_button_y`, `stash_button_scale` | 1568, 110, 0.67 | Where that button sits, in stash panel units (right of the right page arrow) |
| `read_game_page` | true | Read the current page from the game (Steam 3.3.93787, Battle.net 3.3.93847) |
| `instant_jump` | true | Switch straight to the picked page instead of clicking the arrows (needs `read_game_page` and D2RLoader 1.3.1) |
| `steps_per_frame` | 0 | Arrow messages per frame while jumping; 0 sends more for long jumps so any jump takes about 40 frames |

Layout keys are only read when the file has `layout_version = 9`; a config from an older version keeps its other settings but uses the new layout defaults.

Name pages in the same format as Shared Stash Page Text:

```toml
[pages.1]
text = "Runes"
color = "#FFA500"
```

## Build

`./build.sh` cross-builds the DLL with MinGW-w64 on Linux against the pinned PluginSDK commit and writes `stash-tab-dropdown-<version>.zip`.
