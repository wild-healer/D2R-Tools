// D2R GUI: item cells: a row or column of single cells on the window, each
// taking one item code only (an offering: runes r26..r33, one each). The
// items live on the player's custom page: an area of Player Page Host's page
// when the host is installed, else the plugin's own page (D2RLoader allows
// one custom page per character: without the host, two plugins with pages
// can't both have one). They can be picked up again at any time.
//
// The cells are a small panel of their own over the plugin's window (an
// overlay: the host's area panel, or the plugin's own panel bound to its
// page): open it with Show(true) while the window shows them, Show(false)
// when it closes or changes page. A drop is taken only when the item on the
// cursor has the code of the cell under the mouse (D2R GUI's grid wrap tells
// the cell: ItemCells wraps the grid, so a plugin using ItemCells doesn't
// call TileGrid::Wrap itself). Ctrl+click into the cells is refused.
//
//   ItemCells::Register({ .id = "offering", .label = "Offering",
//                         .codes = {"r26", "r27", ...}, .vertical = true,
//                         .changed = [](int cell, bool held) { ... } });   while loading
//   ItemCells::Place(x, y)   where the first cell goes (layout units from the
//                            left-panel anchor, as Panel's x, y; a StashWindow
//                            child at cx, cy: StashWindow::X + cx, StashWindow::Y + cy)
//   ItemCells::Show(on)      UI thread
//   ItemCells::Tick()        UI thread, every tick (placement, items checked
//                            ~4 times a second, `changed` called on a change)
//   ItemCells::Held(i)       whether cell i holds its item
//
// Opt-in: #include <d2r_gui/item_cells.h>. One set of cells per plugin.
#pragma once
#include "core.h"
#include "item_grid.h"
#include "layout.h"
#include "page_host_api.h"
#include "panels.h"
#include "resources.h"
#include "stash_kit.h"
#include <D2RLPlugin/api.h>
#include <functional>
#include <string>
#include <vector>

namespace D2RGui::ItemCells {

inline constexpr int Cell = 98;

struct Options {
	std::string                     id = "cells";   // the area's name (the host keeps its place under it) / the own page's logical id
	std::string                     label;          // the host's name for the area
	std::vector<std::string>        codes;          // one item code per cell ("r26"), in order
	bool                            vertical = false; // cells in a column (else a row)
	int                             priority = 11;  // over the window (10)
	std::function<void(int, bool)>  changed;        // UI thread: cell, whether it now holds its item
	std::function<void(const char*)> problem;       // what could not be set up (default: the log)
};

namespace Detail {
struct State {
	Options                                                    options;
	const D2RL::InventoryService*                              inventory {};
	const D2RL::ItemService*                                   items {};
	D2RL::PluginCommunication::ServiceLease<PageHost::Service> host;
	PageHost::AreaHandle                                       area = PageHost::InvalidArea;
	PageHost::AreaInfo                                         info { .structSize = sizeof(PageHost::AreaInfo) };
	D2RL::Inventory::PageRegistrationHandle                    page = D2RL::Inventory::InvalidHandle;
	SinglePanel                                                panel;
	int                                                        x = 0, y = 0;
	bool                                                       shown {}, placed {}, wrapped {};
	std::vector<bool>                                          held;
	ULONGLONG                                                  lastCheck {};
};
inline State& Get() {
	static State state;
	return state;
}

inline void Problem(const char* what) {
	if (Get().options.problem) Get().options.problem(what);
	else Log(true, "d2r-gui: %s", what);
}

inline int Columns() { return Get().options.vertical ? 1 : static_cast<int>(Get().options.codes.size()); }
inline int Rows() { return Get().options.vertical ? static_cast<int>(Get().options.codes.size()) : 1; }
inline bool Hosted() { return Get().area != PageHost::InvalidArea; }

// The cells' first page cell (the area's place on the host's page, 0, 0 on the own page).
inline std::pair<int, int> Origin() {
	const auto& s = Get();
	return Hosted() ? std::pair<int, int> { s.info.x, s.info.y } : std::pair<int, int> {};
}

inline std::string CodeText(std::uint32_t code) {
	std::string text;
	for (int i = 0; i < 4; ++i)
		if (const char c = static_cast<char>(code >> (i * 8)); c && c != ' ') text += c;
	return text;
}

inline std::string GridName() { return Api().pluginId + "/Cells"; }

// The cells panel: a slot under each cell, the page grid over them.
inline std::string Children(const std::string& grid) {
	std::string c;
	for (int i = 0; i < static_cast<int>(Get().options.codes.size()); ++i)
		c += Image("CellBack" + std::to_string(i), Get().options.vertical ? 0 : i * Cell, Get().options.vertical ? i * Cell : 0, StashKit::Slot(Cell, Cell));
	return c + grid;
}
inline std::string Layout(const std::string& panelName, const std::string& grid) {
	auto& s = Get();
	return Panel(panelName, s.x, s.y, Columns() * Cell, Rows() * Cell, Children(grid), s.options.priority, LeftAnchor);
}

// The cell index under the mouse (the last cell D2R GUI's grid wrap saw), -1 if none.
inline int HoveredCell() {
	const auto cell = TileGrid::HoverCell();
	if (cell == ~std::uint64_t { 0 }) return -1;
	const auto [ox, oy] = Origin();
	const int x = static_cast<std::int32_t>(cell) - ox, y = static_cast<std::int32_t>(cell >> 32) - oy;
	if (x < 0 || y < 0 || x >= Columns() || y >= Rows()) return -1;
	return Get().options.vertical ? y : x;
}

// A drop: the cursor item's code must be the hovered cell's.
inline D2RL::Inventory::ItemPolicyDecision Decide(const D2RL::Inventory::ItemPolicyRequest* request) {
	using Decision = D2RL::Inventory::ItemPolicyDecision;
	auto& s = Get();
	if (request && D2RL::Inventory::HasItemPolicyRequestField(request, D2RL::Inventory::ItemPolicyRequestRequiredSize) && request->reason == D2RL::Inventory::ItemPlacementReason::SavedItemRestore)
		return Decision::Allow;
	if (!s.inventory || !s.items) return Decision::Reject;
	D2RL::PlayerHandle player {};
	D2RL::ItemHandle   cursor {};
	if (s.inventory->getLocalPlayer(Api().context, &player) != D2RL::Inventory::Result::Success || !player) return Decision::Reject;
	if (s.inventory->getCursorItem(Api().context, player, &cursor) != D2RL::Inventory::Result::Success || !cursor) return Decision::Reject; // Ctrl+click
	D2RL::Items::ItemInfo info { .structSize = sizeof(D2RL::Items::ItemInfo) };
	if (s.items->getItemInfo(Api().context, cursor, &info) != D2RL::Items::Result::Success) return Decision::Reject;
	const int cell = HoveredCell();
	return cell >= 0 && CodeText(info.code) == s.options.codes[static_cast<std::size_t>(cell)] ? Decision::Allow : Decision::Reject;
}
inline auto __cdecl OnPagePolicy(const D2RL::PluginContext*, const D2RL::Inventory::ItemPolicyRequest* request, void*) noexcept -> D2RL::Inventory::ItemPolicyDecision { return Decide(request); }
inline auto __cdecl OnAreaPolicy(PageHost::AreaHandle, const D2RL::Inventory::ItemPolicyRequest* request, void*) noexcept -> D2RL::Inventory::ItemPolicyDecision { return Decide(request); }

struct Scan {
	std::vector<bool>* held;
	int                ox, oy;
};
inline auto __cdecl Collect(const D2RL::PluginContext*, const D2RL::Items::ItemInfo* item, void* data) noexcept -> D2RL::Inventory::IterationAction {
	auto*      scan = static_cast<Scan*>(data);
	auto&      s    = Get();
	const int  x = item->x - scan->ox, y = item->y - scan->oy;
	if (item->container == D2RL::Items::ItemContainer::CustomPage && x >= 0 && y >= 0 && x < Columns() && y < Rows()) {
		const int cell = s.options.vertical ? y : x;
		if (CodeText(item->code) == s.options.codes[static_cast<std::size_t>(cell)]) (*scan->held)[static_cast<std::size_t>(cell)] = true;
	}
	return D2RL::Inventory::IterationAction::Continue;
}

inline void Wrap() {
	auto& s = Get();
	if (s.wrapped) return;
	s.wrapped = true;
	TileGrid::Options options;
	options.mine = [](const void* widget) {
		const auto& st = Get();
		return Hosted() ? TileGrid::Marked(widget, st.info.gridMarker[0], st.info.gridMarker[1]) : TileGrid::Marked(widget, TileGrid::Marker, TileGrid::Marker);
	};
	options.origin            = [] { return Origin(); };
	options.clearRequirements = false;
	options.problem           = [](const char* what) { Problem(what); };
	if (!TileGrid::Wrap(std::move(options))) Problem("item cells: the grid wrap failed; drops into the cells are refused");
}
} // namespace Detail

// While loading (after Init): the cells' page, the host's area when Player
// Page Host is there, else the plugin's own page.
inline bool Register(Options options) {
	using namespace Detail;
	auto&       s       = Get();
	const auto* context = Api().context;
	s.options           = std::move(options);
	s.held.assign(s.options.codes.size(), false);
	if (s.options.codes.empty() || s.options.codes.size() > 16 || !context) return false;
	s.inventory = D2RGui::Detail::Query<D2RL::InventoryService>(context, "inventory");
	s.items     = D2RGui::Detail::Query<D2RL::ItemService>(context, "item");
	if (!s.inventory || !s.items) return false;
	const D2RL::PluginCommunicationService* comm = nullptr;
	if (context->QueryService(&comm) == D2RL::ServiceQueryResult::Success && comm && PageHost::Acquire(context, comm, &s.host) == D2RL::PluginCommunication::Result::Success) {
		const PageHost::AreaRequest request { .structSize = sizeof(request), .flags = PageHost::AreaOverlay, .name = s.options.id.c_str(), .label = s.options.label.c_str(),
			                                  .width = static_cast<std::uint32_t>(Columns()), .height = static_cast<std::uint32_t>(Rows()) };
		PageHost::AreaInfo info { .structSize = sizeof(info) };
		if (const auto result = s.host->reserveArea(context, &request, &info); result != PageHost::Result::Success) {
			char line[160];
			std::snprintf(line, sizeof(line), "item cells: Player Page Host has no room for a %dx%d area (result %u)", Columns(), Rows(), static_cast<unsigned>(result));
			Problem(line);
			return false; // the host has the page: no own page either
		}
		s.info = info, s.area = info.handle;
		if (s.host->setItemPolicy(s.area, OnAreaPolicy, nullptr) != PageHost::Result::Success) Problem("item cells: Player Page Host refused the cells' item check");
		Log(false, "d2r-gui: item cells on Player Page Host's page (%dx%d area)", Columns(), Rows());
		return true;
	}
	const D2RL::Inventory::PlayerPageRegistration page { .structSize = D2RL::Inventory::PlayerPageRegistrationSize, .logicalId = s.options.id.c_str() };
	if (s.inventory->registerPlayerPage(context, &page, &s.page) != D2RL::Inventory::Result::Success) {
		s.page = D2RL::Inventory::InvalidHandle;
		Problem("item cells: no custom page (D2RLoader allows one per character: another plugin has it; Player Page Host shares it)");
		return false;
	}
	const D2RL::Inventory::PlayerPageConfiguration size { .structSize = D2RL::Inventory::PlayerPageConfigurationSize, .width = static_cast<std::uint32_t>(Columns()), .height = static_cast<std::uint32_t>(Rows()) };
	if (s.inventory->configurePlayerPage(context, s.page, &size) != D2RL::Inventory::Result::Success) Problem("item cells: the page size was refused");
	const D2RL::Inventory::PlayerPageItemPolicy policy { .structSize = D2RL::Inventory::PlayerPageItemPolicySize, .page = s.page, .callback = OnPagePolicy };
	if (s.inventory->configurePlayerPageItemPolicy(context, &policy) != D2RL::Inventory::Result::Success) Problem("item cells: the cells' item check was refused");
	if (!s.panel.Register(s.options.id, EmptyPanel(Api().pluginId + "/" + s.options.id, s.options.priority), D2RL::Panels::PanelFlags::None, s.page, GridName().c_str())) {
		Problem("item cells: could not register the cells' panel");
		return false;
	}
	Log(false, "d2r-gui: item cells on the plugin's own page (%dx%d)", Columns(), Rows());
	return true;
}

// Where the first cell goes (layout units from the left-panel anchor). UI
// thread or while loading; open cells move at once.
inline void Place(int x, int y) {
	using namespace Detail;
	auto& s = Get();
	if (s.x == x && s.y == y && s.placed) return;
	s.x = x, s.y = y, s.placed = true;
	if (Hosted()) {
		const std::string             layout = Layout("@PANEL@", ", @GRID@"); // the token is a children-list entry
		const PageHost::LayoutRequest request { .structSize = sizeof(request), .flags = PageHost::LayoutApplyNow, .layout = layout.c_str(), .gridX = 0, .gridY = 0 };
		if (s.host->setAreaLayout(s.area, &request) != PageHost::Result::Success) Problem("item cells: Player Page Host refused the cells' layout");
	} else if (s.shown) (void)s.panel.Show(Layout(s.panel.Name(), ItemGrid(GridName(), 0, 0, Columns(), Rows(), TileGrid::MarkerFields)));
}

// UI thread: opens or closes the cells.
inline void Show(bool on) {
	using namespace Detail;
	auto& s = Get();
	s.shown = on;
	if (on) Wrap();
	if (Hosted()) {
		if (on == s.host->isAreaOpen(s.area)) return;
		if (!on) (void)s.host->closeArea(s.area);
		else if (const auto result = s.host->openArea(s.area); result != PageHost::Result::Success && result != PageHost::Result::Busy) Log(true, "d2r-gui: item cells: could not open the area (result %u)", static_cast<unsigned>(result));
		return;
	}
	if (!s.panel.Ready()) return;
	if (!on) s.panel.Close();
	else if (!s.panel.IsOpen()) (void)s.panel.Show(Layout(s.panel.Name(), ItemGrid(GridName(), 0, 0, Columns(), Rows(), TileGrid::MarkerFields)));
}

inline bool Shown() { return Detail::Get().shown; }
inline bool Held(int cell) { return cell >= 0 && cell < static_cast<int>(Detail::Get().held.size()) && Detail::Get().held[static_cast<std::size_t>(cell)]; }
inline int  Count() { return static_cast<int>(Detail::Get().options.codes.size()); }

// UI thread, every tick: the host's placement, the area opened once placed,
// and the items checked (about 4 times a second): `changed` for each cell
// that got or lost its item.
inline void Tick() {
	using namespace Detail;
	auto& s = Get();
	if (Hosted() && s.info.x < 0) {
		PageHost::AreaInfo info { .structSize = sizeof(info) };
		if (s.host->getAreaInfo(s.area, &info) == PageHost::Result::Success && info.x >= 0) s.info = info;
	}
	if (Hosted() && s.shown && s.info.x >= 0 && !s.host->isAreaOpen(s.area)) (void)s.host->openArea(s.area);
	const ULONGLONG now = GetTickCount64();
	if (now - s.lastCheck < 250 || (Hosted() && s.info.x < 0)) return;
	s.lastCheck = now;
	D2RL::PlayerHandle player {};
	if (!s.inventory || s.inventory->getLocalPlayer(Api().context, &player) != D2RL::Inventory::Result::Success || !player) return;
	std::vector<bool>                 held(s.options.codes.size(), false);
	const auto [ox, oy]             = Origin();
	Scan                              scan { &held, ox, oy };
	const D2RL::Inventory::ItemFilter filter { .structSize = sizeof(D2RL::Inventory::ItemFilter), .containerMask = D2RL::Items::ContainerBit(D2RL::Items::ItemContainer::CustomPage) };
	if (s.inventory->forEachInventoryItem(Api().context, player, &filter, Collect, &scan) != D2RL::Inventory::Result::Success) return;
	for (std::size_t i = 0; i < held.size(); ++i)
		if (held[i] != s.held[i]) {
			s.held[i] = held[i];
			if (s.options.changed) s.options.changed(static_cast<int>(i), held[i]);
		}
}

} // namespace D2RGui::ItemCells
