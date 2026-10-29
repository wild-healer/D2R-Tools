# Player Page Host 1.0.0: release texts

## Patch notes

```
**Player Page Host 1.0.0**

**New**

* Takes the custom player page (16x16) and shares it: other plugins reserve an area of it by size, and the host packs every area on the page.
* Each area gets its own panel whose grid shows and takes items only in that area's cells; each plugin decides what may go in its area.
* Ctrl+click puts the item in the first free cell of the area on screen.
* Charms on the page give their bonuses only inside areas that ask for it.
* Removing a plugin keeps its items on the page and its place reserved; pagehost all opens the whole page to take them out.
* Items left in a place another area takes go back to their character's inventory, or personal stash, at the next game load.
* pagehost console command: lists the areas; pagehost all opens the whole page; pagehost free frees an empty saved place.
```

## GitHub release

- Tag: `aaa-player-page-host-v1.0.0`
- Title: `Player Page Host 1.0.0`
- Asset: `aaa-player-page-host-1.0.0.zip`

Body:

```markdown
**Player Page Host** 1.0.0: takes D2RLoader's single custom player page and shares it, so several plugins can store items on it at the same time.

### Features
- Plugins reserve an area of the 16x16 page by size; the host packs every area, largest first, once all plugins have loaded, and keeps each one in place between sessions.
- Each area has its own panel whose grid shows and takes items only in that area's cells. Each plugin decides what may go in its area.
- Ctrl+click puts the item in the first free cell of the area on screen.
- Charms on the page give their bonuses only inside areas that ask for it.
- Uninstall safe: a removed plugin's items stay on the page and its place stays reserved; `pagehost all` opens the whole page to take them out.
- Items left in a place another area takes go back to their character's inventory (or personal stash) at the next game load.
- `pagehost` console command: lists the areas; `pagehost all`; `pagehost free <plugin>/<area>`.

### Requirements
- D2RLoader 1.3.1 or later.
- Steam 3.3.93787 or 3.3.94995. The plugin checks the game code when it loads; on another build its area limits stay off and it says so in its log.
- On its own it does nothing visible: it is used by other plugins. Plugins that register their own custom page (such as Charm Inventory) can't share it.

### Install
Load `aaa-player-page-host-1.0.0.zip` in D2RMM, or copy `aaa-player-page-host.dll` to `<game>/d2rloader/plugins/`. Keep the DLL name: it must sort before the plugins that use the page.

### For plugin developers
To use the page from your own plugin, read the [client guide](https://github.com/wild-healer/D2R-Tools/blob/main/plugins/aaa-player-page-host/CLIENT_GUIDE.md). The API is the single header [`page_host_api.h`](https://github.com/wild-healer/D2R-Tools/blob/main/plugins/aaa-player-page-host/src/page_host_api.h).
```

## D2RLoader Hub

Name: `Player Page Host`

Summary:

> Shares the custom player page between plugins: each one reserves an area of it, and the host packs them.

Description: the GitHub release body above.
