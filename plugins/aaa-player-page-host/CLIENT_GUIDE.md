# Using Player Page Host from your plugin

D2RLoader 1.3.1 gives each character a single custom player page, and the first plugin that
registers it keeps it. Player Page Host (`aaa-player-page-host`) takes that page at 16x16 and
shares it. Your plugin reserves a rectangle (an "area") of the page and gets a panel that shows
only that rectangle. Several plugins can then store items on the page at the same time.

This guide covers what a client plugin does. The full API, with a comment on every field, is
[`src/page_host_api.h`](src/page_host_api.h). Copy that header into your plugin; it is the only file you need.
[`src/test_client.cpp`](src/test_client.cpp) is a working client that uses every call.

## What you get and what you give up

You get:
- A rectangle of the page of the size you ask for, that no other plugin can take. The host decides
  where it goes and keeps it there between sessions, so your saved items come back where they were.
- A panel, owned by the host, whose grid shows only your cells and accepts drops only inside them.
  You give its layout, or take a plain default one.
- Your own item policy (what may be dropped in your area).
- Optionally, active charms: charms in your area give their bonuses. Page charms outside a charm area never do.
- Item calls (create, move, destroy) that land items in your area.

You give up:
- Registering the page yourself. Don't call `registerPlayerPage`, `configurePlayerPage` or
  `bindPlayerPageGrid`: the host owns all of them.
- Using the page handle directly. D2RLoader refuses another plugin's page handle
  (`PolicyRejected`), so create, move and destroy items through the host.

## 1. Connect to the host

The host publishes the service `player-page` v1 when it loads. Its DLL name sorts first, so it
usually loads before you. Try at load, and if the host isn't there yet, try again in your first
`DataTablesLoaded` listener. Every plugin has loaded by then.

```cpp
#include "page_host_api.h"

const D2RL::PluginCommunicationService* CommApi {};
D2RL::PluginCommunication::ServiceLease<PageHost::Service> Host;

bool Join() {
	if (Host) return true;
	if (!CommApi || PageHost::Acquire(Context, CommApi, &Host) != D2RL::PluginCommunication::Result::Success) return false;
	// reserve your area here (step 2)
	return true;
}

// In D2RLoaderLoadPlugin:
//   Context->QueryService(&CommApi);
//   if (!Join()) register a DataTablesLoadedListener whose callback calls Join() once.
```

If `Acquire` still fails after every plugin has loaded, the host isn't installed. Fall back to
your own page if you have one, or switch your page features off. Don't fail to load.

## 2. Reserve an area

```cpp
PageHost::AreaHandle Area {};
PageHost::AreaInfo   Info { .structSize = sizeof(PageHost::AreaInfo) };

const PageHost::AreaRequest request {
	.structSize = sizeof(request),
	.flags      = 0,          // PageHost::AreaCharms, PageHost::AreaOverlay, or both
	.name       = "storage",  // stable: your saved place is keyed by your plugin id + this name
	.label      = "My Storage", // title of the default layout
	.width = 8, .height = 4,  // 1..16: you give the size, the host chooses the place
};
if (Host->reserveArea(Context, &request, &Info) == PageHost::Result::Success) Area = Info.handle;
```

- Reserve at load, or in your first `DataTablesLoaded` listener, so your area is packed with the
  others. Reserving later works, but it gets whatever room is left.
- Never rename `name` once players have items in it. A new name is a new area, and the items in the
  old place are no longer yours.
- You only give a size. The host packs every area on the page, and where yours lands doesn't matter
  to you: your grid shows wherever your layout puts it (step 3), and every cell you pass to the host is
  relative to your area's top-left cell. The only time you need the page position is when you read
  items yourself (step 6).
- Areas asked for while plugins load wait, and the host packs them together, largest first, when every
  plugin has loaded. That is when it publishes its `ready` event. Until then `Info.x, y` are -1 and
  `openArea` answers `Busy`. Call `getAreaInfo` again after `ready` (or in your first
  `DataTablesLoaded` listener, if you load after the host) to get your place.
- Each area keeps its place between sessions when it can. If the saved places no longer fit together
  with a new area, the host packs everything again. Items left in a place that moved go back to their
  character's inventory at the next game load.
- If your area can't fit beside the others, it is refused: `getAreaInfo` and `openArea` answer
  `NoRoom`. After loading, `reserveArea` answers `NoRoom` at once. Keep areas as small as you can.

Results you should handle: `NoPage` (another plugin took the page first), `NoRoom` (no room beside the
other areas), `NoPanel` (every host panel of that kind is leased), `Busy` (you already hold that name).

### Flags

| Flag | Effect |
| --- | --- |
| `AreaCharms` | Charms in your area give their bonuses. `Info.charmsActive` says whether it worked (the host could wrap the game's charm check). |
| `AreaOverlay` | Your area's panel takes no left panel slot and ignores Escape. Use it when your plugin has its own panel and shows the page grid inside or beside it: you open and close the area together with your panel. There are 4 overlay panels and 8 normal ones. |

## 3. Show it: the layout

Without a layout call, your area uses the host's default panel: a background sized to your area,
your `label` as the title, and Escape to close. That is enough for simple storage.

To use your own, pass a full D2R panel layout (JSON) with two tokens:
- `@PANEL@` stands for the panel's name. Put it inside the quotes of the root `"name"`.
- `@GRID@` stands for the page grid widget object. Put it in the root's `children` list: the
  bound grid must be a direct child of the panel. Widgets listed after it get clicks first.

```cpp
const std::string layout =
	R"({"type": "Panel", "name": "@PANEL@", "fields": {"priority": 10, "rect": {"x": 236, "y": -856, "width": 708, "height": 900}, "anchor": "$LeftPanelAnchor"}, "children": [)"
	R"({"type": "ClickCatcherWidget", "name": "PanelClickCatcher", "fields": {"rect": {"x": 0, "y": 0, "width": 708, "height": 900}}})"
	R"(, {"type": "ImageWidget", "name": "Background", "fields": {"rect": {"x": 12, "y": 30}, "filename": "d2rloader\\aaa-player-page-host\\bg8x4"}})"
	R"(, @GRID@]})";

const PageHost::LayoutRequest request { .structSize = sizeof(request), .flags = 0, .layout = layout.c_str(), .gridX = 60, .gridY = 140 };
Host->setAreaLayout(Area, &request);
```

- `gridX`, `gridY` say where your area's top-left cell goes, in panel units. One cell is 98 units.
- The host's plain background for any size is `d2rloader\aaa-player-page-host\bg<W>x<H>`. Its first cell is 48
  units from its left edge and 110 from its top, so place the image at `gridX - 48`, `gridY - 110`.
- A layout set while the area is open applies on its next open. Pass `LayoutApplyNow` to close,
  apply and reopen at once. Pass `layout = nullptr` to go back to the default.
- An `AreaOverlay` area's default layout is the grid alone at `gridX, gridY`, panel priority 11.

## 4. Open and close

```cpp
Host->openArea(Area);   // e.g. from your button, hotkey or console command
Host->closeArea(Area);
Host->isAreaOpen(Area);
```

Call these from the UI thread or a game-thread callback (they are the same thread in D2RLoader
1.3.1). Use `Info.panelName` with `WidgetService::findPanel` if you need the panel's widgets.

## 5. Decide what may be dropped

```cpp
auto __cdecl CharmsOnly(PageHost::AreaHandle, const D2RL::Inventory::ItemPolicyRequest* request, void*) noexcept
    -> D2RL::Inventory::ItemPolicyDecision {
	return D2RL::Inventory::ItemPolicyRequestMatchesType(request, D2RL::Inventory::MakeItemTypeFourCC("char"))
	           ? D2RL::Inventory::ItemPolicyDecision::Allow : D2RL::Inventory::ItemPolicyDecision::Reject;
}
Host->setItemPolicy(Area, CharmsOnly, nullptr); // nullptr policy: allow everything
```

- The host asks your policy only when a player puts an item into your area. The host has already checked
  the cell, so you only see D2RLoader's request (item types, no cell).
- Saved items loading back are always allowed, and so are your own host item calls.
- If your DLL is gone, the host refuses new drops into your area by itself.

## 6. Move items in and out

Every item call takes area-relative cells (0, 0 is your area's top-left cell). Cells outside your
area return `OutsideArea`.

**Cursor moves** (a game-thread callback, same rule as `executeLocalPlayerMove`):

```cpp
Host->moveItem(Area, PageHost::MoveKind::CursorToArea, x, y); // the held item's new top-left cell
Host->moveItem(Area, PageHost::MoveKind::AreaToCursor, x, y); // any cell of the item
```

**Item service calls** (a game-thread callback). Handles belong to the plugin that got them, so none
cross between plugins. Items are named by **runtime id**: the host finds them with its own handles
and its own local player (`transaction->player` is ignored).

```cpp
// Create: a CustomPage destination must be Exact, with area-relative x, y.
// customPageHandle is ignored (the host puts its own).
spec.destination = { .structSize = sizeof(D2RL::Items::ItemDestination), .container = D2RL::Items::ItemContainer::CustomPage,
                     .placement = D2RL::Items::Placement::Exact, .x = 1, .y = 0 };
std::uint32_t runtimeId = 0, itemResult = 0;
Host->createItem(Area, &spec, &runtimeId, &itemResult);

// Move: each operation's `item` holds the item's RUNTIME ID, not a handle.
D2RL::Items::ExistingItemOperation op { .structSize = sizeof(op), .kind = D2RL::Items::ExistingItemOperationKind::Move, .item = runtimeId };
op.move.destination = { .structSize = sizeof(D2RL::Items::ItemDestination), .container = D2RL::Items::ItemContainer::CustomPage,
                        .placement = D2RL::Items::Placement::Exact, .x = 2, .y = 0 };
const D2RL::Items::ExistingItemTransaction transaction { .structSize = sizeof(transaction), .operationCount = 1, .operations = &op };
D2RL::Items::ExistingItemTransactionResult outcome { .structSize = sizeof(outcome) };
Host->moveItems(Area, &transaction, &outcome, &itemResult);

// Destroy an item whose top-left cell is in your area.
Host->destroyItem(Area, runtimeId, D2RL::Items::SocketedItemPolicy::DestroyContents, &itemResult);
```

`itemResult` gets D2RLoader's `Items::Result` whenever the call reached D2RLoader. Moves to other
containers (inventory, stash) in the same transaction are passed through unchanged.

**Reading** your items needs no host call. Walk the player's items with `forEachInventoryItem`
and keep the ones with `container == CustomPage` inside your area. `ItemInfo::x, y` are **page**
cells, so subtract `Info.x, Info.y`, read with `getAreaInfo` after the host's `ready` event (never
the -1 you get while plugins load, and never a position you saved yourself: the host may move areas).

## 7. Grid vtable wrappers (advanced)

If your plugin wraps the game's grid widget vtable (as Inventory Slots does), install your wrappers
after the host's, which go on at the first `DataTablesLoaded`. Your wrappers then see **page** cells
in and out of vt16, vt18, vt19 and vt31 for an area grid, and the host turns them into widget cells
below. Recognise an area grid by `Info.gridMarker` (the two floats at widget+0x634).

## 8. Unload and uninstall

- In `D2RLoaderUnloadPlugin`, call `releaseArea(Context, Area)` and reset the lease. Releasing keeps
  your saved place: you get it back next session.
- When a player removes your plugin, the items stay on the page and your place stays reserved. Players
  reach them with `pagehost all`. If a new area needs that place, it takes it, and the items go back to
  their characters at the next game load.
- `pagehost free <your-plugin>/<name>` frees a place, but only when it is empty on every character.

## 9. Items another area left behind

The host records, for each character, where each page item lies and which area it was put in. If a
new area is placed over items that belong to another area or to none (for example, items left
behind when an area changed size), those items go back to that character's inventory, else the
personal stash, the next time that character joins a game. If both are full, the items stay and the
next game load tries again. You never find another plugin's items in your area after that load, so
don't remove "unknown" items yourself.

## Checklist

- [ ] `page_host_api.h` copied; `Acquire` at load, and again at the first `DataTablesLoaded`.
- [ ] A stable area `name`; your place read back from `AreaInfo` after the host's `ready` event.
- [ ] Works (or switches page features off cleanly) when the host isn't installed.
- [ ] No `registerPlayerPage` / `bindPlayerPageGrid` of your own while the host runs.
- [ ] Items created, moved and destroyed through the host, by runtime id, on the game thread.
- [ ] Item policy set if your area shouldn't take everything.
- [ ] `releaseArea` on unload.

## Console (for players and testing)

| Command | Does |
| --- | --- |
| `pagehost` | Lists the areas (in use or saved), and items outside every area per character. |
| `pagehost all` | Opens the whole page with no area limits, to take items out. |
| `pagehost free <plugin>/<area>` | Frees a saved place that is empty on every character. |

Host log: `d2rloader/logs/aaa-player-page-host.log`. Saved places:
`d2rloader/config/aaa-player-page-host-areas.txt`. Item record: `d2rloader/config/aaa-player-page-host-items.txt`.
