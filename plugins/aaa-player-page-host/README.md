# Player Page Host

D2RLoader plugin that takes the custom player page and shares it between plugins, so several plugins can store items on it at the same time.

D2RLoader 1.3.1 gives each character a single custom player page, and the first plugin that registers it keeps it. Player Page Host registers it first (its DLL name sorts first) at 16x16, and other plugins reserve a rectangle of it, an "area", by asking for a size. The host packs every area on the page and gives each one its own panel, whose grid shows only that area's cells.

- **Areas**: each plugin gives only a size. Areas asked for while plugins load are packed together, largest first, once every plugin has loaded. An area keeps its place between sessions, so saved items come back where they were. An area that can't fit beside the others is refused.
- **Drops and Ctrl+click**: an area's grid takes items only into its own cells, and each plugin decides what may go in its area. Ctrl+click puts the item in the area's first free cell.
- **Charms**: a charm on the page gives its bonuses only inside an area that asked for active charms.
- **Removing a plugin**: its items stay on the page and its place stays reserved. `pagehost all` opens the whole page to take them out.
- **Items left behind**: the host records where each character's page items lie and which area each was put in. If an area is placed over items that belong to another area or to none, those items go back to that character's inventory (or personal stash) at the next game load.

On its own the host does nothing visible: it is used by other plugins. Plugins that register their own custom page (such as Charm Inventory) can't share it.

Hooks the game's inventory grid widget and its charm check; it checks that code when it loads and leaves those features off on other builds. Tested on Steam 3.3.94995 (3.3.93787 has the same code). Requires D2RLoader 1.3.1 or later.

## Install

Download it from the [D2RLoader Hub](https://d2rloader.net/hub/plugins/aaa-player-page-host). Load `aaa-player-page-host-<version>.zip` (the DLL alone; its default settings are built in) in D2RMM, or copy `aaa-player-page-host.dll` to `<game>/d2rloader/plugins/`. Keep the DLL name: it must sort before every plugin that uses the page.

## Settings

`d2rloader/config/aaa-player-page-host.toml`, written on first launch:

| Key | Default | Meaning |
| --- | --- | --- |
| `enabled` | `true` | Turns the plugin off without removing it. |

The host also writes `aaa-player-page-host-areas.txt` (where each area is) and `aaa-player-page-host-items.txt` (where each character's page items lie) beside it.

## Console

| Command | Does |
| --- | --- |
| `pagehost` | Lists the areas (in use or saved), and items outside every area per character. |
| `pagehost all` | Opens the whole page with no area limits, to take items out. |
| `pagehost free <plugin>/<area>` | Frees a saved place, only when it is empty on every character. |

Log: `d2rloader/logs/aaa-player-page-host.log`.

## For plugin developers

[CLIENT_GUIDE.md](CLIENT_GUIDE.md) explains how a plugin reserves and uses an area. The API is [`src/page_host_api.h`](src/page_host_api.h), the one header a client copies, and [`src/test_client.cpp`](src/test_client.cpp) is a working client that uses every call (built as two test plugins by `build.sh`).

## Building

`sh build.sh` cross-builds `aaa-player-page-host.dll` and the two test clients with MinGW-w64, against the D2RLoader PluginSDK at commit 03a5af9.
