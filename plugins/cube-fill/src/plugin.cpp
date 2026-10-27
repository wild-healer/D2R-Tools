// Cube Fill: shows how full the Horadric Cube is on the cube itself in the
// inventory, as a small "7/12" badge in a corner of its icon and/or a mini
// map of the cube grid with each item's shape.
//
// D2RLoader 1.3.1 can add widgets to the stock inventory panel (a child
// layout) and show or hide them, but cannot move them or change their text
// (setText and setWidgetRect came later). So there is one child layout
// per cell the cube can sit on (9 x 3 in a 10 x 4 inventory), each with one
// text box per fill count (and the mini map tiles), and the plugin shows only
// the right ones:
//   - "visible": false is not applied to plugin layouts on my setup, so
//     everything is hidden at runtime, again after each game start;
//   - widget handles are paths that stay valid when the panel is rebuilt, and
//     the loader hands out about 250, so groups' texts are looked up only for
//     the cube positions actually used.
// Cells used = the cube's items with their sizes. Sizes come from the game's
// item table when it can be read (game thread, single player or a TCP/IP game
// you host): the byte offsets of invwidth/invheight in its compiled rows are
// found once from items of known size. Otherwise the stock sizes built into
// the plugin are used.

#include <D2RLPlugin/api.h>
#include <windows.h>

#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

constexpr const char* PluginId      = "cube-fill";
constexpr const char* PluginVersion = "1.0.0";

const D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = PluginId,
	.name        = "Cube Fill",
	.version     = PluginVersion,
	.author      = "wild-healer",
	.description = "Shows how full the Horadric Cube is on its inventory icon.",
	.flags       = D2RL::PluginFlags::Client | D2RL::PluginFlags::NativeHooks,
};

namespace Items   = D2RL::Items;
namespace Widgets = D2RL::Widgets;
namespace Events  = D2RL::SharedEvents;
namespace Tables  = D2RL::DataTables;

constexpr std::uint32_t Code(const char* text) noexcept {
	std::uint32_t code = 0;
	for (int i = 0; i < 4; ++i) code |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(text[i] ? text[i] : ' ')) << (i * 8);
	return code;
}
constexpr std::uint32_t CubeCode = Code("box ");

// Cube grid (inventory.txt "Transmogrify Box") and player inventory grid.
constexpr int CubeWidth = 3, CubeHeight = 4, CubeCells = CubeWidth * CubeHeight;
constexpr int InvWidth = 10, InvHeight = 4;
constexpr int CubeSize = 2; // the cube is 2 x 2
constexpr int SpotsX = InvWidth - CubeSize + 1, SpotsY = InvHeight - CubeSize + 1, Spots = SpotsX * SpotsY;
// Stock playerinventory*layouthd.json: grid at 93,897, 98-unit cells.
constexpr int GridX = 93, GridY = 897, Cell = 98;

// One child layout per cube spot: D2RLoader skips a child layout file that is
// too large (one file with the map for all 27 spots, 194 KB, was refused; the
// badges alone, 96 KB, loaded).
std::string SpotLocalId(int spot) {
	return "S" + std::to_string(spot);
}
std::string SpotName(int spot) {
	return "cube-fill/" + SpotLocalId(spot);
}
constexpr const char* StockPanels[] { "PlayerInventoryExpansionLayout", "PlayerInventoryOriginalLayout" };

struct Settings {
	bool        enabled   = true;
	bool        showFree  = false; // false: "used/12", true: free cells
	bool        showEmpty = false;
	bool        showFull  = true;
	int         fontSize  = 30;
	std::string corner    = "bottom-right";
	bool        badge     = false; // "7/12" text
	bool        grid      = false; // mini cube map with each item's shape
	bool        sprites   = true;  // mini cube map drawn with the items' own pictures (game hook)
	int         spriteBackground = 1; // FillRect mode of the map's dark back: 0 = 25 %, 1 = 50 %, 2 = 75 %
} Config;

// Mini cube map pieces, each one widget shown or hidden: cell tiles, the
// joints between two cells of one item (across, down) and the middle of four.
constexpr int TilePieces = CubeCells, AcrossPieces = (CubeWidth - 1) * CubeHeight, DownPieces = CubeWidth * (CubeHeight - 1),
              MiddlePieces = (CubeWidth - 1) * (CubeHeight - 1);
constexpr int Pieces = TilePieces + AcrossPieces + DownPieces + MiddlePieces;
// Handles the plugin allows itself (the loader hands out about 250).
constexpr int HandleBudget = 230;

const D2RL::PluginContext*      Context {};
const D2RL::InventoryService*   Inventory {};
const D2RL::ThreadService*      Threads {};
const D2RL::WidgetService*      WidgetApi {};
const D2RL::DataTableService*  TableApi {};

void Log(bool warn, const char* format, ...) noexcept {
	if (!Context) return;
	char    line[512] {};
	va_list args;
	va_start(args, format);
	std::vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	warn ? Context->LogWarn(line) : Context->LogInfo(line);
}

// ---- item sizes ----

struct Size {
	std::uint8_t w, h;
};

struct StockSize {
	const char* code;
	int         w, h;
};
constexpr StockSize StockSizes[] {
#include "sizes.inc"
};

std::unordered_map<std::uint32_t, Size> StockByCode;

// Filled on the game thread, read on the UI thread.
std::mutex                              SizeLock;
std::unordered_map<std::uint32_t, Size> GameByClass; // Items row index -> size
bool                                    GameTableBroken {}; // offsets not found: stock sizes only
std::atomic<bool>                       LookupQueued {};

// Items of known size, to find where the compiled rows keep invwidth/height.
constexpr StockSize Probes[] {
	{ "hax", 1, 3 }, { "axe", 2, 3 }, { "2hs", 1, 4 }, { "cap", 2, 2 }, { "lrg", 2, 3 }, { "lbl", 2, 1 }, { "amu", 1, 1 },
	{ "aqv", 1, 3 }, { "box", 2, 2 }, { "cm2", 1, 2 }, { "cm3", 1, 3 }, { "r01", 1, 1 }, { "pk1", 1, 2 }, { "rin", 1, 1 },
};

int            WidthOffset = -1;
Tables::Bank   ItemBank {};

bool FindRow(Tables::Bank bank, const char* code, Tables::RowView* view) noexcept {
	for (const std::uint32_t packed : { Code(code), D2RL::DataTables::MakeFourCC(code[0], code[1], code[2], code[3]) }) {
		*view = Tables::RowView { .structSize = Tables::RowViewSize };
		if (TableApi->findRowByCode(Context, bank, Tables::TableId::Items, packed, view) == Tables::Result::Success && view->row && view->rowSize) return true;
	}
	return false;
}

// Game thread: invwidth at some offset o, invheight at o + 1, for every probe.
bool Calibrate() noexcept {
	for (const auto bank : { Tables::Bank::Rotw, Tables::Bank::Lod }) {
		std::vector<const std::uint8_t*> rows;
		std::uint32_t                    rowSize = 0;
		for (const auto& probe : Probes) {
			Tables::RowView view {};
			if (!FindRow(bank, probe.code, &view)) break;
			rows.push_back(static_cast<const std::uint8_t*>(view.row));
			rowSize = view.rowSize;
		}
		if (rows.size() != std::size(Probes)) continue;
		std::vector<int> found;
		for (std::uint32_t o = 0; o + 1 < rowSize; ++o) {
			bool all = true;
			for (std::size_t i = 0; all && i < rows.size(); ++i) all = rows[i][o] == Probes[i].w && rows[i][o + 1] == Probes[i].h;
			if (all) found.push_back(static_cast<int>(o));
		}
		if (found.size() == 1) {
			WidthOffset = found[0];
			ItemBank    = bank;
			Log(false, "item sizes: read from the game's item table (row size %u, invwidth at +0x%X)", rowSize, WidthOffset);
			return true;
		}
		Log(true, "item sizes: %zu places in the item table match the known sizes; using the built-in stock sizes", found.size());
		return false;
	}
	Log(true, "item sizes: the game's item table could not be read; using the built-in stock sizes");
	return false;
}

void __cdecl LookupSizes(const D2RL::PluginContext*, void* userData) noexcept {
	auto* classes = static_cast<std::vector<std::uint32_t>*>(userData);
	if (WidthOffset < 0 && !Calibrate()) {
		std::lock_guard lock(SizeLock);
		GameTableBroken = true;
	} else {
		std::lock_guard lock(SizeLock);
		for (const auto id : *classes) {
			Tables::RowView view { .structSize = Tables::RowViewSize };
			if (TableApi->getRow(Context, ItemBank, Tables::TableId::Items, id, &view) != Tables::Result::Success || !view.row ||
			    view.rowSize < static_cast<std::uint32_t>(WidthOffset + 2))
				continue;
			const auto* row = static_cast<const std::uint8_t*>(view.row);
			const Size  size { row[WidthOffset], row[WidthOffset + 1] };
			if (size.w >= 1 && size.w <= CubeWidth && size.h >= 1 && size.h <= CubeHeight) GameByClass[id] = size;
			else GameByClass[id] = { 1, 1 };
		}
	}
	delete classes;
	LookupQueued.store(false);
}

// UI thread. Missing game sizes are added to wanted.
Size SizeOf(const Items::ItemInfo& item, std::vector<std::uint32_t>& wanted) {
	{
		std::lock_guard lock(SizeLock);
		if (const auto it = GameByClass.find(item.classId); it != GameByClass.end()) return it->second;
		if (!GameTableBroken && TableApi) wanted.push_back(item.classId);
	}
	const auto it = StockByCode.find(item.code);
	return it != StockByCode.end() ? it->second : Size { 1, 1 };
}

// ---- layout ----

std::string Corner(int spot, int& x, int& y, int width, int height) {
	constexpr int Inset = 6;
	const int     left = GridX + (spot % SpotsX) * Cell, top = GridY + (spot / SpotsX) * Cell, span = CubeSize * Cell;
	const bool    right = Config.corner.find("right") != std::string::npos, bottom = Config.corner.find("bottom") != std::string::npos;
	x = right ? left + span - Inset - width : left + Inset;
	y = bottom ? top + span - Inset - height : top + Inset;
	char rect[96];
	std::snprintf(rect, sizeof(rect), R"({"x": %d, "y": %d, "width": %d, "height": %d})", x, y, width, height);
	return rect;
}

std::string StateText(int used) {
	return Config.showFree ? std::to_string(CubeCells - used) : std::to_string(used) + "/" + std::to_string(CubeCells);
}

std::string Rectangle(const std::string& name, int x, int y, int width, int height, const char* color) {
	char json[256];
	std::snprintf(json, sizeof(json), R"(, {"type": "RectangleWidget", "name": "%s", "fields": {"rect": {"x": %d, "y": %d, "width": %d, "height": %d}, "color": %s}})", name.c_str(), x, y,
	              width, height, color);
	return json;
}

// Mini map geometry: 3 x 4 tiles centered on the cube's 196 x 196 icon, with room around it.
constexpr int Pitch = 32, Tile = 28, Gap = Pitch - Tile;
constexpr int MapWidth = CubeWidth * Pitch - Gap, MapHeight = CubeHeight * Pitch - Gap;

std::string MapLayout(int spot) {
	const int   span = CubeSize * Cell;
	const int   left = GridX + (spot % SpotsX) * Cell + (span - MapWidth) / 2, top = GridY + (spot / SpotsX) * Cell + (span - MapHeight) / 2;
	const char* back = "[0.0, 0.0, 0.0, 0.35]", *empty = "[0.32, 0.30, 0.27, 0.25]", *filled = "[0.78, 0.70, 0.47, 0.65]";
	std::string json = Rectangle("MapBack", left - 4, top - 4, MapWidth + 8, MapHeight + 8, back);
	for (int i = 0; i < CubeCells; ++i) json += Rectangle("E" + std::to_string(i), left + (i % CubeWidth) * Pitch, top + (i / CubeWidth) * Pitch, Tile, Tile, empty);
	int piece = 0;
	for (int i = 0; i < TilePieces; ++i, ++piece)
		json += Rectangle("T" + std::to_string(piece), left + (i % CubeWidth) * Pitch, top + (i / CubeWidth) * Pitch, Tile, Tile, filled);
	for (int i = 0; i < AcrossPieces; ++i, ++piece)
		json += Rectangle("T" + std::to_string(piece), left + (i % (CubeWidth - 1)) * Pitch + Tile, top + (i / (CubeWidth - 1)) * Pitch, Gap, Tile, filled);
	for (int i = 0; i < DownPieces; ++i, ++piece)
		json += Rectangle("T" + std::to_string(piece), left + (i % CubeWidth) * Pitch, top + (i / CubeWidth) * Pitch + Tile, Tile, Gap, filled);
	for (int i = 0; i < MiddlePieces; ++i, ++piece)
		json += Rectangle("T" + std::to_string(piece), left + (i % (CubeWidth - 1)) * Pitch + Tile, top + (i / (CubeWidth - 1)) * Pitch + Tile, Gap, Gap, filled);
	return json;
}

std::string BuildLayout(int spot) {
	const int         width = Config.fontSize * 3, height = Config.fontSize + 10;
	int               x = 0, y = 0;
	const std::string rect = Corner(spot, x, y, width, height);
	std::string       children;
	if (Config.grid) children += MapLayout(spot);
	if (Config.badge) children += R"(, {"type": "RectangleWidget", "name": "Back", "fields": {"rect": )" + rect + R"(, "color": [0.0, 0.0, 0.0, 0.72]}})";
	for (int used = 0; Config.badge && used <= CubeCells; ++used) {
		const char* color = used == CubeCells ? R"({"r": 255, "g": 90, "b": 80, "a": 255})" : R"({"r": 240, "g": 240, "b": 240, "a": 255})";
		children += R"(, {"type": "TextBoxWidget", "name": "N)" + std::to_string(used) + R"(", "fields": {"rect": )" + rect + R"(, "text": ")" + StateText(used) +
		            R"(", "style": {"fontColor": )" + color + R"(, "pointSize": )" + std::to_string(Config.fontSize) + R"(, "alignment": {"h": "center", "v": "center"}}}})";
	}
	return R"({"type": "Widget", "name": ")" + SpotName(spot) + R"(", "fields": {"visible": false}, "children": [)" + children.substr(2) + "]}";
}

// ---- showing ----

// What should be shown: the badge group of a cube spot and its fill count.
struct View {
	int           spot = -1; // -1: nothing
	int           used = 0;
	std::uint64_t pieces {}; // mini map pieces shown, bit per piece

	bool operator==(const View&) const = default;
};

std::array<Widgets::WidgetHandle, Spots>                    Groups {};
std::array<std::array<Widgets::WidgetHandle, CubeCells + 1>, Spots> Texts {};
std::array<std::array<Widgets::WidgetHandle, Pieces>, Spots>        MapPieces {};
std::array<bool, Spots> SpotReady {};
int                     HandleCount {};
bool                    BudgetLogged {};
View          Applied {};
bool          AppliedOk {};
std::uint64_t LastFullApply {};
std::atomic<std::uint64_t> ForceUntil {};
bool          RootLogged {}, RootFailLogged {}, FoundAsPanel {};

bool Find(Widgets::WidgetHandle parent, const std::string& name, Widgets::WidgetHandle& handle) {
	if (handle) return true;
	if (WidgetApi->findWidget(Context, parent, name.c_str(), &handle) == Widgets::Result::Success) return ++HandleCount, true;
	handle = {};
	return false;
}

std::array<Widgets::WidgetHandle, std::size(StockPanels)> Stock {};

bool FindGroups() {
	for (int spot = 0; spot < Spots; ++spot) {
		// Handles found once are kept: the loader hands out a limited number.
		if (Groups[spot]) continue;
		const std::string     name = SpotName(spot);
		Widgets::WidgetHandle handle {};
		if (WidgetApi->findPanel(Context, name.c_str(), &handle) == Widgets::Result::Success) {
			Groups[spot] = handle;
			FoundAsPanel = true;
			++HandleCount;
		}
		for (std::size_t i = 0; !Groups[spot] && i < Stock.size(); ++i) {
			if (!Stock[i] && WidgetApi->findPanel(Context, StockPanels[i], &Stock[i]) == Widgets::Result::Success) ++HandleCount;
			else if (!Stock[i]) Stock[i] = {};
			if (Stock[i]) Find(Stock[i], name, Groups[spot]);
		}
		if (!Groups[spot]) {
			if (!RootFailLogged && spot > 0) Log(true, "only %d of %d cube spots found in the inventory panel", spot, Spots);
			RootFailLogged = spot > 0;
			return false;
		}
	}
	if (!RootLogged) Log(false, "cube spots found in the inventory panel%s", FoundAsPanel ? "" : " (through the stock panel)");
	RootLogged = true;
	return true;
}

// The texts and map pieces of one cube spot, looked up the first time the
// cube sits there.
bool FindSpot(int spot) {
	if (SpotReady[spot]) return true;
	const int needed = (Config.badge ? CubeCells + 1 : 0) + (Config.grid ? Pieces : 0);
	if (HandleCount + needed > HandleBudget) {
		if (!BudgetLogged) Log(true, "cube moved to too many spots this session: no more widget handles for spot %d,%d (restart the game)", spot % SpotsX, spot / SpotsX);
		BudgetLogged = true;
		return false;
	}
	for (int used = 0; Config.badge && used <= CubeCells; ++used)
		if (!Find(Groups[spot], "N" + std::to_string(used), Texts[spot][used])) return false;
	for (int piece = 0; Config.grid && piece < Pieces; ++piece)
		if (!Find(Groups[spot], "T" + std::to_string(piece), MapPieces[spot][piece])) return false;
	return SpotReady[spot] = true;
}

bool Visible(Widgets::WidgetHandle handle, bool visible) {
	return WidgetApi->setWidgetVisible(Context, handle, visible) == Widgets::Result::Success;
}

// UI thread. Every group is set, not only the ones that changed: D2R rebuilds
// the panel with everything shown.
bool Apply(const View& view) {
	if (!FindGroups()) return false;
	bool ok = true;
	for (int spot = 0; spot < Spots; ++spot) ok = Visible(Groups[spot], spot == view.spot) && ok;
	if (view.spot >= 0) {
		if (!FindSpot(view.spot)) return false;
		for (int used = 0; Config.badge && used <= CubeCells; ++used) ok = Visible(Texts[view.spot][used], used == view.used) && ok;
		for (int piece = 0; Config.grid && piece < Pieces; ++piece) ok = Visible(MapPieces[view.spot][piece], (view.pieces >> piece) & 1) && ok;
	}
	return ok;
}

// ---- reading the cube ----

struct Scan {
	bool                         cubeFound {};
	Items::ItemInfo              cube {};
	std::vector<Items::ItemInfo> inside;
};

D2RL::Inventory::IterationAction __cdecl Collect(const D2RL::PluginContext*, const Items::ItemInfo* item, void* userData) noexcept {
	auto* scan = static_cast<Scan*>(userData);
	if (!item) return D2RL::Inventory::IterationAction::Continue;
	if (item->container == Items::ItemContainer::Cube) scan->inside.push_back(*item);
	else if (item->container == Items::ItemContainer::Inventory && item->code == CubeCode && !scan->cubeFound) {
		scan->cubeFound = true;
		scan->cube      = *item;
	}
	return D2RL::Inventory::IterationAction::Continue;
}

View ReadCube() {
	D2RL::PlayerHandle player {};
	if (Inventory->getLocalPlayer(Context, &player) != D2RL::Inventory::Result::Success) return {};
	Scan                              scan;
	const D2RL::Inventory::ItemFilter filter {
		.structSize    = D2RL::Inventory::ItemFilterSize,
		.containerMask = Items::ContainerBit(Items::ItemContainer::Inventory) | Items::ContainerBit(Items::ItemContainer::Cube),
	};
	if (Inventory->forEachInventoryItem(Context, player, &filter, Collect, &scan) != D2RL::Inventory::Result::Success || !scan.cubeFound) return {};
	if (scan.cube.x < 0 || scan.cube.x >= SpotsX || scan.cube.y < 0 || scan.cube.y >= SpotsY) return {};

	std::array<int, CubeCells> owner; // item index + 1 per cell, 0 = free
	owner.fill(0);
	std::vector<std::uint32_t> wanted;
	for (std::size_t i = 0; i < scan.inside.size(); ++i) {
		const auto& item = scan.inside[i];
		const Size  size = SizeOf(item, wanted);
		for (int y = item.y; y < item.y + size.h; ++y)
			for (int x = item.x; x < item.x + size.w; ++x)
				if (x >= 0 && x < CubeWidth && y >= 0 && y < CubeHeight) owner[y * CubeWidth + x] = static_cast<int>(i) + 1;
	}
	if (!wanted.empty() && !LookupQueued.exchange(true)) {
		auto* job = new std::vector<std::uint32_t>(std::move(wanted));
		if (Threads->runOnGameThread(Context, LookupSizes, job) != D2RL::Threads::Result::Success) {
			// A game you joined has no local game thread: stock sizes from now on.
			delete job;
			std::lock_guard lock(SizeLock);
			GameTableBroken = true;
			LookupQueued.store(false);
		}
	}
	View view { .spot = scan.cube.y * SpotsX + scan.cube.x };
	const auto at   = [&](int x, int y) { return owner[y * CubeWidth + x]; };
	int        bit  = 0;
	const auto mark = [&](bool on) { view.pieces |= static_cast<std::uint64_t>(on) << bit++; };
	for (int i = 0; i < CubeCells; ++i) mark(owner[i] != 0), view.used += owner[i] != 0;
	for (int y = 0; y < CubeHeight; ++y)
		for (int x = 0; x + 1 < CubeWidth; ++x) mark(at(x, y) && at(x, y) == at(x + 1, y));
	for (int y = 0; y + 1 < CubeHeight; ++y)
		for (int x = 0; x < CubeWidth; ++x) mark(at(x, y) && at(x, y) == at(x, y + 1));
	for (int y = 0; y + 1 < CubeHeight; ++y)
		for (int x = 0; x + 1 < CubeWidth; ++x) mark(at(x, y) && at(x, y) == at(x + 1, y) && at(x, y) == at(x, y + 1) && at(x, y) == at(x + 1, y + 1));
	if ((view.used == 0 && !Config.showEmpty) || (view.used == CubeCells && !Config.showFull)) view.spot = -1;
	return view;
}

std::atomic<bool> TickQueued {};
std::uint64_t     FailSince {};
bool              FailLogged {};

void __cdecl Tick(const D2RL::PluginContext*, void*) noexcept {
	const View          view = ReadCube();
	const std::uint64_t now  = GetTickCount64();
	if (!AppliedOk || !(view == Applied) || now < ForceUntil.load() || now - LastFullApply >= 500) {
		AppliedOk = Apply(view);
		if (!AppliedOk && view.spot >= 0 && !FailSince) FailSince = now;
		if (AppliedOk) FailSince = 0;
		if (FailSince && now - FailSince > 20000 && !FailLogged) {
			FailLogged = true;
			Log(true, "badges: still not reachable 20 s after a cube was seen (spots %s); open the inventory to check", Groups[0] ? "found" : "not found");
		}
		Applied       = view;
		LastFullApply = now;
	}
	TickQueued.store(false);
}

std::atomic<bool> Running {};
HANDLE            Poller {};

DWORD WINAPI Poll(void*) noexcept {
	while (Running.load()) {
		if (!TickQueued.exchange(true) && Threads->runOnUiThread(Context, Tick, nullptr) != D2RL::Threads::Result::Success) TickQueued.store(false);
		Sleep(50);
	}
	return 0;
}

bool Contains(const char* text, const char* part) noexcept {
	return text && std::strstr(text, part);
}

// The inventory panel is built (again) at game start: hide its badges at once.
auto __cdecl OnUiMessage(const D2RL::PluginContext*, const Events::UiMessageEvent* event, void*) noexcept -> Events::UiMessageAction {
	if (event && Contains(event->target, "PanelManager") && Contains(event->text, "PlayerInventory")) ForceUntil.store(GetTickCount64() + 2000);
	return Events::UiMessageAction::Continue;
}

// ---- item pictures (game hook) ----
// The inventory grid's draw function (InventoryGridWidget vtable +0x18) is
// hooked: after the game has drawn the inventory, the cube's items are drawn
// again with the game's own item renderer, small, over the cube, on a
// darkened 3x4 map. RVAs of Steam 3.3.94995 (same code as 93787), each one
// checked against its first bytes when the plugin loads; on a mismatch the
// pictures are left off and the rest of the plugin works as usual.
//   GridDraw    0x2C3570 (grid): byte +0x630 = page shown (0 = inventory)
//   GridOwner   0x2A7810 (grid) -> unit whose items the grid shows
//   grid vtable +0xC0 (grid, int out[4], item) -> int* {cell x, cell y}
//   grid vtable +0xF8 (grid, int out[4], size, cell x | y << 32) -> screen x, y
//   GridScale   0x1E6750(widget): parent (+0x30) scale times float +0x80; cell width int +0x5B8
//   Inventory   0x34A360 (unit, file, line); FirstItem 0x388C10; NextItem 0x38ABA0
//   ItemStored  0x38A930 (item) == 1; ItemPage 0x36CFE0 (item) -> byte (3 = cube)
//   ItemClass   0x349860 (item, file, line); ItemBank 0x34A0E0 (item);
//   ItemsRow    0x314110 (bank, class) -> row, code at +0x80
//   ItemSize    0x371850 (item, u8* w, u8* h, file, line)
//   DrawItemAt  0x15BB80 (item, x | y << 32, float scale, {float, bool}*)
//   FillRect    0x657820 (x0, y0, x1, y1, r, g, b, mode, 1); mode 0 = 25 %
//               opaque, 1 = 50 %, 2 = 75 %
// Local player: 0x8B2D0 () -> index, 0x9A480 (index) -> unit.

struct NativeFn {
	std::uint64_t rva;
	std::uint8_t  bytes[16];
};

// First 16 bytes of each function in the 94995 image.
constexpr NativeFn Natives[] {
	{ 0x2C3570, { 0x40, 0x55, 0x56, 0x41, 0x56, 0x48, 0x8b, 0xec, 0x48, 0x81, 0xec, 0x80, 0x00, 0x00, 0x00, 0x48 } },
	{ 0x15BB80, { 0x40, 0x53, 0x55, 0x56, 0x48, 0x81, 0xec, 0x40, 0x01, 0x00, 0x00, 0x0f, 0x29, 0xb4, 0x24, 0x20 } },
	{ 0x657820, { 0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8d, 0x6c } },
	{ 0x34A360, { 0x48, 0x89, 0x5c, 0x24, 0x18, 0x56, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xf1, 0x48, 0x85, 0xc9 } },
	{ 0x388C10, { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0x48, 0x85, 0xc9, 0x74, 0x2e, 0x81, 0x39 } },
	{ 0x38ABA0, { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0x48, 0x85, 0xc9, 0x75, 0x10, 0x88, 0x4c } },
	{ 0x38A930, { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0x48, 0x85, 0xc9, 0x75, 0x10, 0x88, 0x4c } },
	{ 0x36CFE0, { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0x48, 0x85, 0xc9, 0x74, 0x0a, 0xe8, 0xdd } },
	{ 0x2A7810, { 0x48, 0x83, 0xec, 0x28, 0x44, 0x8b, 0x81, 0xc4, 0x05, 0x00, 0x00, 0x41, 0x83, 0xf8, 0xff, 0x75 } },
	{ 0x349860, { 0x48, 0x83, 0xec, 0x28, 0x48, 0x85, 0xc9, 0x75, 0x1d, 0x88, 0x4c, 0x24, 0x30, 0x48, 0x8d, 0x4c } },
	{ 0x34A0E0, { 0x48, 0x83, 0xec, 0x28, 0x48, 0x85, 0xc9, 0x75, 0x1a, 0x88, 0x4c, 0x24, 0x30, 0x48, 0x8d, 0x4c } },
	{ 0x314110, { 0x40, 0x57, 0x48, 0x83, 0xec, 0x30, 0x8b, 0xfa, 0xe8, 0x73, 0xc9, 0xfe, 0xff, 0x3b, 0xb8, 0xa8 } },
	{ 0x1E6750, { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0x48, 0x8b, 0x49, 0x30, 0x48, 0x85, 0xc9 } },
	{ 0x371850, { 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7c, 0x24, 0x20, 0x41, 0x56, 0x48, 0x83, 0xec, 0x20 } },
	{ 0x08B2D0, { 0x8b, 0x05, 0x2e, 0x84, 0x99, 0x02, 0xc3, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc } },
	{ 0x09A480, { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x83, 0xf9, 0x08, 0x0f, 0x83, 0x85 } },
};
constexpr std::uint32_t GridDrawPrologue = 15; // whole instructions: push rbp/rsi/r14, mov rbp,rsp, sub rsp,0x80

struct ScreenPos {
	std::int32_t x, y, w, h;
};
struct OptFloat {
	float value;
	bool  valid;
};

using GridDrawFn   = void (*)(void* grid, void*, void*, void*) noexcept;
using GridOwnerFn  = void* (*)(void* grid) noexcept;
using CellOfFn     = std::int32_t* (*)(void* grid, ScreenPos* out, void* item) noexcept;
using CellToScreen = ScreenPos* (*)(void* grid, ScreenPos* out, std::uint64_t size, std::uint64_t cell) noexcept;
using ScaleFn      = float (*)(void* widget) noexcept;
using InventoryFn  = void* (*)(void* unit, const char* file, int line) noexcept;
using ItemFn       = void* (*)(void* item) noexcept;
using IntOfItemFn  = int (*)(void* item) noexcept;
using ByteOfItemFn = std::uint8_t (*)(void* item) noexcept;
using ClassFn      = int (*)(void* item, const char* file, int line) noexcept;
using RowFn        = std::uint8_t* (*)(std::uint8_t bank, int classId) noexcept;
using SizeFn       = void (*)(void* item, std::uint8_t* w, std::uint8_t* h, const char* file, int line) noexcept;
using DrawItemAtFn = void (*)(void* item, std::uint64_t pos, float scale, OptFloat* opt) noexcept;
using FillRectFn   = void (*)(int x0, int y0, int x1, int y1, std::uint8_t r, std::uint8_t g, std::uint8_t b, int mode, int hd) noexcept;
using LocalIndexFn = int (*)() noexcept;
using PlayerUnitFn = void* (*)(int index) noexcept;

std::uintptr_t Base {};
GridDrawFn     OriginalGridDraw {};
template <typename Fn>
Fn At(std::uint64_t rva) {
	return reinterpret_cast<Fn>(Base + rva);
}
constexpr const char* SourceName = "cube-fill";

bool NativesMatch() noexcept {
	for (const auto& fn : Natives) {
		std::uint8_t code[16] {};
		SIZE_T       copied = 0;
		if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(Base + fn.rva), code, sizeof(code), &copied) || copied != sizeof(code) ||
		    std::memcmp(code, fn.bytes, sizeof(code)) != 0) {
			Log(true, "item pictures: game code at D2R+0x%llX differs from 3.3.94995 (another plugin hooks it, or another game build); pictures off",
			    static_cast<unsigned long long>(fn.rva));
			return false;
		}
	}
	return true;
}

std::uint32_t CodeOf(void* item) noexcept {
	const int     classId = At<ClassFn>(0x349860)(item, SourceName, 0);
	std::uint8_t* row     = At<RowFn>(0x314110)(At<ByteOfItemFn>(0x34A0E0)(item), classId);
	return row ? *reinterpret_cast<std::uint32_t*>(row + 0x80) : 0;
}

void DrawCubePictures(void* grid) noexcept {
	if (*(static_cast<std::uint8_t*>(grid) + 0x630) != 0) return; // not the inventory page
	void* owner = At<GridOwnerFn>(0x2A7810)(grid);
	void* local = At<PlayerUnitFn>(0x09A480)(At<LocalIndexFn>(0x08B2D0)());
	if (!owner || owner != local) return;
	void* inventory = At<InventoryFn>(0x34A360)(owner, SourceName, 0);
	if (!inventory) return;

	void*              cube = nullptr;
	std::vector<void*> inside;
	for (void* item = At<ItemFn>(0x388C10)(inventory); item; item = At<ItemFn>(0x38ABA0)(item)) {
		if (At<IntOfItemFn>(0x38A930)(item) != 1) continue;
		const std::uint8_t page = At<ByteOfItemFn>(0x36CFE0)(item);
		if (page == 3) inside.push_back(item);
		else if (page == 0 && !cube && CodeOf(item) == CubeCode) cube = item;
	}
	if (!cube || (inside.empty() && !Config.showEmpty)) return;

	auto**          vtable = *reinterpret_cast<void***>(grid);
	const auto      cellOf = reinterpret_cast<CellOfFn>(vtable[0xC0 / 8]);
	const auto      toScreen = reinterpret_cast<CellToScreen>(vtable[0xF8 / 8]);
	ScreenPos       scratch {}, screen {};
	const auto*     cell = cellOf(grid, &scratch, cube);
	if (!cell) return;
	const std::uint64_t packed = static_cast<std::uint32_t>(cell[0]) | static_cast<std::uint64_t>(static_cast<std::uint32_t>(cell[1])) << 32;
	const auto*         cubeAt = toScreen(grid, &screen, 0, packed);
	if (!cubeAt) return;
	const float  scale  = At<ScaleFn>(0x1E6750)(grid); // walks the parents (+0x30), times each +0x80
	const int    cellW  = *reinterpret_cast<std::int32_t*>(static_cast<std::uint8_t*>(grid) + 0x5B8);
	if (!(scale > 0.0f) || cellW <= 0) return;
	const float unit = scale * cellW / Cell; // screen pixels per layout unit
	const auto  px   = [&](int units) { return static_cast<int>(units * unit + 0.5f); };
	const int   left = cubeAt->x + px((CubeSize * Cell - MapWidth) / 2), top = cubeAt->y + px((CubeSize * Cell - MapHeight) / 2);

	const auto fill = At<FillRectFn>(0x657820);
	fill(left - px(4), top - px(4), left + px(MapWidth + 4), top + px(MapHeight + 4), 0, 0, 0, Config.spriteBackground, 1);
	std::array<bool, CubeCells> taken {};
	for (void* item : inside) {
		ScreenPos     tmp {};
		const auto*   at = cellOf(grid, &tmp, item);
		std::uint8_t  w = 1, h = 1;
		At<SizeFn>(0x371850)(item, &w, &h, SourceName, 0);
		if (!at) continue;
		for (int y = at[1]; y < at[1] + h; ++y)
			for (int x = at[0]; x < at[0] + w; ++x)
				if (x >= 0 && x < CubeWidth && y >= 0 && y < CubeHeight) taken[y * CubeWidth + x] = true;
	}
	for (int i = 0; i < CubeCells; ++i) {
		if (taken[i]) continue;
		const int x = left + px((i % CubeWidth) * Pitch), y = top + px((i / CubeWidth) * Pitch);
		fill(x, y, x + px(Tile), y + px(Tile), 82, 76, 68, 0, 1);
	}
	const auto draw = At<DrawItemAtFn>(0x15BB80);
	for (void* item : inside) {
		ScreenPos   tmp {};
		const auto* at = cellOf(grid, &tmp, item);
		if (!at || at[0] < 0 || at[0] >= CubeWidth || at[1] < 0 || at[1] >= CubeHeight) continue;
		const std::uint32_t x = left + px(at[0] * Pitch), y = top + px(at[1] * Pitch);
		OptFloat            none {};
		draw(item, x | static_cast<std::uint64_t>(y) << 32, scale * Pitch / Cell, &none);
	}
}

void GridDrawDetour(void* grid, void* a, void* b, void* c) noexcept {
	OriginalGridDraw(grid, a, b, c);
	if (grid) DrawCubePictures(grid);
}

bool HookGridDraw() noexcept {
	if (!NativesMatch()) return false;
	if (!Context->InstallInlineHook(0x2C3570, Natives[0].bytes, GridDrawPrologue, GridDrawDetour, &OriginalGridDraw)) {
		Log(true, "item pictures: the inventory grid draw could not be hooked (another plugin may own it); pictures off");
		return false;
	}
	return true;
}

// ---- config ----

std::string Trim(std::string_view text) {
	const auto first = text.find_first_not_of(" \t\r\n");
	if (first == std::string_view::npos) return {};
	const auto last = text.find_last_not_of(" \t\r\n");
	return std::string(text.substr(first, last - first + 1));
}

std::string Unquote(std::string value) {
	if (const auto hash = value.find('#'); hash != std::string::npos) value = Trim(value.substr(0, hash));
	if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
	return value;
}

void LoadConfig() {
	(void)Context->EnsureConfig();
	std::uint32_t     required = 0;
	std::vector<char> buffer(4096);
	if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) {
		if (required <= buffer.size()) {
			Log(true, "config: could not read the config file, using defaults");
			return;
		}
		buffer.resize(required);
		if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) return;
	}
	const std::string_view text(buffer.data(), std::strlen(buffer.data()));
	std::size_t            lineStart = 0;
	while (lineStart < text.size()) {
		auto lineEnd = text.find('\n', lineStart);
		if (lineEnd == std::string_view::npos) lineEnd = text.size();
		const std::string line = Trim(text.substr(lineStart, lineEnd - lineStart));
		lineStart              = lineEnd + 1;
		const auto equals      = line.find('=');
		if (line.empty() || line[0] == '#' || line[0] == '[' || equals == std::string::npos) continue;
		const std::string key   = Trim(std::string_view(line).substr(0, equals));
		const std::string value = Unquote(Trim(std::string_view(line).substr(equals + 1)));
		if (key == "enabled") Config.enabled = value != "false";
		else if (key == "show") {
			if (value == "used" || value == "free") Config.showFree = value == "free";
			else Log(true, "config: unknown show \"%s\", using used", value.c_str());
		} else if (key == "show_empty") Config.showEmpty = value == "true";
		else if (key == "show_full") Config.showFull = value != "false";
		else if (key == "font_size") {
			const int size = std::atoi(value.c_str());
			if (size >= 12 && size <= 60) Config.fontSize = size;
			else Log(true, "config: font_size %s out of range (12 to 60), using %d", value.c_str(), Config.fontSize);
		} else if (key == "corner") {
			if (value == "bottom-right" || value == "bottom-left" || value == "top-right" || value == "top-left") Config.corner = value;
			else Log(true, "config: unknown corner \"%s\", using %s", value.c_str(), Config.corner.c_str());
		} else if (key == "picture_background") {
			if (value == "25") Config.spriteBackground = 0;
			else if (value == "50") Config.spriteBackground = 1;
			else if (value == "75") Config.spriteBackground = 2;
			else Log(true, "config: picture_background must be 25, 50 or 75, using 50");
		} else if (key == "display") {
			if (value == "badge" || value == "grid" || value == "both" || value == "sprites" || value == "sprites-badge") {
				Config.badge   = value == "badge" || value == "both" || value == "sprites-badge";
				Config.grid    = value == "grid" || value == "both";
				Config.sprites = value == "sprites" || value == "sprites-badge";
			} else Log(true, "config: unknown display \"%s\", using sprites", value.c_str());
		} else Log(true, "config: unknown key %s", key.c_str());
	}
}

template <typename Service>
const Service* Query(const char* name) {
	const Service* service = nullptr;
	if (Context->QueryService(&service) != D2RL::ServiceQueryResult::Success) {
		Log(true, "%s service unavailable", name);
		return nullptr;
	}
	return service;
}

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (!D2RL::HasContext(context) || !context->GetApi()) return false;
	Context = context;
	LoadConfig();
	if (!Config.enabled) {
		Log(false, "Cube Fill %s: disabled in config", PluginVersion);
		return true;
	}
	Base = context->exeBase;
	if (Config.sprites) {
		if (HookGridDraw()) Log(false, "item pictures: drawn over the cube in the inventory");
		else {
			Log(true, "item pictures unavailable, showing the badge instead");
			Config.sprites = false;
			Config.badge   = true;
		}
		if (!Config.badge) {
			Log(false, "Cube Fill %s: active, item pictures only", PluginVersion);
			return true;
		}
	}
	for (const auto& entry : StockSizes) StockByCode[Code(entry.code)] = { static_cast<std::uint8_t>(entry.w), static_cast<std::uint8_t>(entry.h) };

	const auto* resources = Query<D2RL::ResourceService>("resource");
	const auto* panels    = Query<D2RL::PanelService>("panel");
	const auto* events    = Query<D2RL::SharedEventService>("shared event");
	Inventory             = Query<D2RL::InventoryService>("inventory");
	Threads               = Query<D2RL::ThreadService>("thread");
	WidgetApi             = Query<D2RL::WidgetService>("widget");
	TableApi              = Query<D2RL::DataTableService>("data tables");
	if (TableApi && !D2RL::HasDataTableServiceField(TableApi, D2RL::DataTableServiceRequiredSize)) TableApi = nullptr;
	if (!resources || !D2RL::HasResourceServiceField(resources, D2RL::ResourceServiceRequiredSize) || !D2RL::HasPanelServiceField(panels, D2RL::PanelServiceRegisterChildLayoutFieldEnd) ||
	    !events || !D2RL::HasInventoryServiceField(Inventory, D2RL::InventoryServiceRequiredSize) || !D2RL::HasThreadServiceField(Threads, D2RL::ThreadServiceRequiredSize) ||
	    !D2RL::HasWidgetServiceField(WidgetApi, D2RL::WidgetServiceRequiredSize)) {
		Log(true, "Cube Fill %s: inactive, this D2RLoader lacks a needed service", PluginVersion);
		return true;
	}

	std::size_t layoutBytes = 0;
	for (int spot = 0; spot < Spots; ++spot) {
		const std::string layout = BuildLayout(spot), localId = SpotLocalId(spot), path = "data/global/ui/layouts/cube-fill/" + localId + "hd.json";
		layoutBytes += layout.size();
		const D2RL::Resources::ResourceRegistration resource {
			.structSize = D2RL::Resources::ResourceRegistrationSize,
			.path       = path.c_str(),
			.bytes      = layout.data(),
			.byteCount  = layout.size(),
		};
		D2RL::Resources::RegistrationHandle resourceHandle {};
		const D2RL::Panels::ChildLayoutRegistration child {
			.structSize = D2RL::Panels::ChildLayoutRegistrationSize,
			.flags      = D2RL::Panels::ChildLayoutFlags::KeyboardMouseOnly,
			.stockPanel = D2RL::Panels::StockPanel::PlayerInventory,
			.localId    = localId.c_str(),
		};
		D2RL::Panels::ChildLayoutHandle childHandle {};
		if (resources->registerResource(context, &resource, &resourceHandle) != D2RL::Resources::Result::Success) {
			Log(true, "Cube Fill %s: inactive, could not register the layout of cube spot %d", PluginVersion, spot);
			return true;
		}
		if (const auto result = panels->registerChildLayout(context, &child, &childHandle); result != D2RL::Panels::Result::Success) {
			Log(true, "Cube Fill %s: inactive, the inventory panel refused cube spot %d (result %u)", PluginVersion, spot, static_cast<unsigned>(result));
			return true;
		}
	}

	const Events::UiMessageListener listener {
		.structSize = Events::UiMessageListenerSize,
		.priority   = 0,
		.callback   = OnUiMessage,
	};
	Events::ListenerHandle listenerHandle {};
	if (events->registerUiMessageListener(context, &listener, &listenerHandle) != Events::Result::Success) Log(true, "could not listen to UI messages: badges may show for a moment at game start");

	Running.store(true);
	Poller = CreateThread(nullptr, 0, Poll, nullptr, 0, nullptr);
	Log(false, "Cube Fill %s: active, %s%s%s (%zu layout bytes in 27 files)", PluginVersion, Config.badge ? (Config.showFree ? "free cells badge" : "used cells badge") : "",
	    Config.badge && (Config.grid || Config.sprites) ? " + " : "", Config.grid ? "mini cube map" : Config.sprites ? "item pictures" : "", layoutBytes);
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	Running.store(false);
	if (Poller) {
		WaitForSingleObject(Poller, 1000);
		CloseHandle(Poller);
		Poller = nullptr;
	}
}
