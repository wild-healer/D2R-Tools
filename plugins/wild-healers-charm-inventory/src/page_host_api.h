// Player Page Host: the service other plugins use to get an area of the
// custom player page. Copy this header into a client plugin.
//
// D2RLoader 1.3.1 has a single custom player page per character. The host
// (aaa-player-page-host) owns it at 16x16 and hands out non-overlapping
// rectangles ("areas"). Each area comes with its own panel, owned and bound by
// the host, whose page grid shows only that area: cells outside it are hidden
// and refuse drops. The client gives the panel's layout (or takes a plain
// default one), opens and closes it, decides what may be dropped in its area
// and moves items in and out of it through the host.
//
// The host decides where an area goes on the page: the client only gives its
// size, and works in area-relative cells everywhere (where the area is on the
// page doesn't change where its grid shows). Areas asked for while plugins
// load are packed together, largest first, when every plugin has loaded (the
// "ready" event): until then AreaInfo x, y are -1 and openArea answers Busy.
// An area that doesn't fit beside the others gets NoRoom then (getAreaInfo,
// openArea). An area keeps its place between sessions (the host saves the
// rectangles), so items saved in it come back where they were. Items stay on the page when
// their client is removed; `pagehost all` shows the whole page.
//
// Handshake: the host publishes the service when it loads, and its DLL name
// sorts first, so most clients acquire it at their own load. A client that
// loads before the host acquires it in its first DataTablesLoaded listener
// (every plugin has loaded by then), or when the host's "ready" event comes.
//
// Threads: call everything from the UI thread or a game-thread callback (both
// are the game's main thread in D2RLoader 1.3.1). moveItem has the same rule
// as executeLocalPlayerMove: a game-thread callback.
#pragma once

#include <D2RLPlugin/api.h>
#include <cstdint>

namespace PageHost {

inline constexpr const char* ProviderId     = "aaa-player-page-host";
inline constexpr const char* ServiceName    = "player-page";
inline constexpr const char* ReadyEvent     = "ready";
inline constexpr uint32_t    ServiceVersion = 1;
inline constexpr int         PageSize       = 16;

using AreaHandle = uint64_t;
inline constexpr AreaHandle InvalidArea = 0;

enum class Result : uint32_t {
	Success         = 0,
	InvalidArgument = 1,
	NoPage          = 2, // the host could not get the custom page (another plugin has it)
	Overlap         = 3, // not returned (areas are placed by the host)
	NoRoom          = 4, // no room for the area beside the others (impossible combination)
	NoPanel         = 5, // every panel of the host (of that kind: normal or overlay) is leased
	NotFound        = 6, // unknown or released area
	Busy            = 7, // this plugin already holds an area of that name; openArea: not placed yet
	Failed          = 8, // D2RLoader refused the call; the host log says why
	OutsideArea     = 9, // a cell (or an item's cells) outside the area
};

// Area flags.
// AreaOverlay: the area's panel takes no left panel slot and ignores Escape
// (4 such panels): the client opens and closes it beside its own panel. Its
// default layout is the grid alone at gridX/gridY, panel priority 11.
inline constexpr uint32_t AreaOverlay = 1U << 1U;
inline constexpr uint32_t AreaCharms = 1U << 0U; // charms here give their bonuses (other page charms never do); AreaInfo::charmsActive says if it worked

struct AreaRequest {
	uint32_t    structSize; // sizeof(AreaRequest)
	uint32_t    flags;      // Area* flags
	const char* name;       // stable id within the plugin, e.g. "grail"; the saved place is keyed by plugin id + name
	const char* label;      // title of the default layout
	int32_t     reserved[2]; // 0; ignored (the host chooses where the area goes)
	uint32_t    width, height; // 1..16
};

struct AreaInfo {
	uint32_t   structSize; // sizeof(AreaInfo)
	uint32_t   flags;      // the request's flags
	AreaHandle handle;
	int32_t    x, y;       // top-left page cell, -1 until placed
	uint32_t   width, height;
	uint32_t   charmsActive; // 1 when charms in the area give bonuses
	uint32_t   reserved;
	char       panelName[64]; // "aaa-player-page-host/AreaN", for WidgetService::findPanel
	uint64_t   pageHandle;    // the host's page handle (D2RLoader may refuse it from another plugin: use createItem / moveItems)
	float      gridMarker[2]; // the area grid's chronicleFlourishOffset (+0x634 of the widget), to recognise it in a grid vtable wrapper
	uint32_t   reserved2;
};
// Grid vtable wrappers put on top of the host's (after the first
// DataTablesLoaded event) see PAGE cells in and out of vt16, vt18, vt19 and
// vt31 for an area grid: the host turns them into widget cells below.

// A client layout: a whole panel layout (D2R layout JSON) in which "@PANEL@"
// stands for the panel's name (inside the quotes of the root "name") and
// "@GRID@" for the page grid widget object (put it in a children list: the
// bound grid must be a direct child of the panel). gridX, gridY: where the
// area's top-left cell goes, in the panel's units (one cell is 98).
// Widgets listed after @GRID@ get clicks before the grid. The host's plain
// background for the area, "d2rloader\\aaa-player-page-host\\bg<W>x<H>" (W x H
// cells, the cells 48 units from its left and 110 from its top), can be used.
inline constexpr uint32_t LayoutApplyNow = 1U << 0U; // when the area is open: close, apply and open again now

struct LayoutRequest {
	uint32_t    structSize; // sizeof(LayoutRequest)
	uint32_t    flags;      // LayoutApplyNow or 0
	const char* layout;     // null: back to the default layout
	int32_t     gridX, gridY;
};

// Asked before an item lands in the area by a player's drop (not for saved
// items loading back, and not for the client's own moveItem). The request is
// D2RLoader's, unchanged (item types only, no cell: the host has already
// checked the cell). A Ctrl+click into the page lands in the first free cell
// of the area shown, then asks the policy like a drop.
using ItemPolicyFn = D2RL::Inventory::ItemPolicyDecision(__cdecl*)(AreaHandle area, const D2RL::Inventory::ItemPolicyRequest* request, void* userData) noexcept;

enum class MoveKind : uint32_t {
	CursorToArea = 1, // x, y: the item's new top-left cell, area-relative
	AreaToCursor = 2, // x, y: any cell of the item, area-relative
};

struct Service {
	uint32_t serviceSize;
	uint32_t serviceVersion;
	Result (__cdecl* reserveArea)(const D2RL::PluginContext* caller, const AreaRequest* request, AreaInfo* info) noexcept;
	Result (__cdecl* releaseArea)(const D2RL::PluginContext* caller, AreaHandle area) noexcept; // the saved place stays
	Result (__cdecl* getAreaInfo)(AreaHandle area, AreaInfo* info) noexcept;
	Result (__cdecl* setAreaLayout)(AreaHandle area, const LayoutRequest* layout) noexcept; // applies on the next open
	Result (__cdecl* openArea)(AreaHandle area) noexcept;
	Result (__cdecl* closeArea)(AreaHandle area) noexcept;
	bool (__cdecl* isAreaOpen)(AreaHandle area) noexcept;
	Result (__cdecl* setItemPolicy)(AreaHandle area, ItemPolicyFn policy, void* userData) noexcept; // null: allow everything
	Result (__cdecl* moveItem)(AreaHandle area, MoveKind kind, int32_t x, int32_t y) noexcept;
	// ItemService calls made by the host for the client (game-thread callback).
	// Every CustomPage destination must be Exact with area-relative x, y (its
	// customPageHandle is ignored, the host puts its own). The page's item
	// policy is not asked. Handles belong to the plugin that got them, so none
	// cross: the host uses its own local player (transaction->player is
	// ignored), createItem gives the new item's runtime id, and in moveItems
	// each operation's `item` holds the item's RUNTIME ID (the host finds it).
	// itemResult gets D2RLoader's Items::Result when the call reached it.
	Result (__cdecl* createItem)(AreaHandle area, const D2RL::Items::ItemCreateSpec* spec, uint32_t* runtimeId, uint32_t* itemResult) noexcept;
	Result (__cdecl* moveItems)(AreaHandle area, const D2RL::Items::ExistingItemTransaction* transaction, D2RL::Items::ExistingItemTransactionResult* result, uint32_t* itemResult) noexcept;
	// Destroys an item of the area (its top-left cell inside), found by runtime id.
	Result (__cdecl* destroyItem)(AreaHandle area, uint32_t runtimeId, D2RL::Items::SocketedItemPolicy socketed, uint32_t* itemResult) noexcept;
};

// Acquires the host's service. Returns D2RL's result (NotFound: host not
// loaded yet, or not installed).
inline auto Acquire(const D2RL::PluginContext* context, const D2RL::PluginCommunicationService* comm, D2RL::PluginCommunication::ServiceLease<Service>* lease) noexcept
    -> D2RL::PluginCommunication::Result {
	return D2RL::PluginCommunication::Acquire(context, comm, ProviderId, ServiceName, ServiceVersion, sizeof(Service), lease);
}

} // namespace PageHost
