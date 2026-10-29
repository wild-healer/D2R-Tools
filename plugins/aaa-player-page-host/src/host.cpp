// Player Page Host: owns D2RLoader 1.3.1's single custom player page (16x16)
// and leases rectangles of it to other plugins (see page_host_api.h).
//
// How (Steam 3.3.93787 / 94995 RVAs):
//   - The page is registered when the plugin loads. The DLL name sorts first
//     (aaa-...), so the host gets the page before any other page plugin.
//   - A pool of panels is registered at load, each with an InventoryGridWidget
//     bound to the page. A lease takes one panel; its layout is the client's
//     (or a default one) with the grid put in, re-registered before an open
//     (a panel loaded before is unloaded first, PanelManager:UnloadPanel).
//   - The grid widgets share the game's grid vtable (Base+0x1CF3800). On the
//     first DataTablesLoaded event (every plugin has loaded, so on top of
//     whatever Inventory Slots or others put there) these entries are wrapped
//     for the host's area grids only, marked by chronicleFlourishOffset
//     {AreaMarker + slot, AreaMarker} (+0x634):
//       vt3  Draw: page items outside the area are hidden for the call (item
//            page byte, ItemData+0x55, set to 0xFE then restored).
//       vt16 item rect, vt31 element rect: an area cell -> the widget's own
//            cell (area-relative), so the area shows at the grid's corner.
//       vt18 valid element: only area cells.
//       vt19 element under the mouse: the game's point-to-cell (0x2C6300,
//            which already accounts for the held item's size) moved into the
//            area; nothing when the item's cells would leave the area.
//   - The page's item policy goes to the client whose area grid is on screen
//     (last drawn); saved items loading back are always allowed.
//   - Charms: see "charms by area" (only areas with AreaCharms count).
//   - Ctrl+click into the page: placed by the host in the area shown (see
//     "Ctrl+click into the page").
//   - Area places are saved in aaa-player-page-host-areas.txt beside the config.
//   - Where items lie on the page is recorded per character (the host can't
//     look into a character that isn't loaded) in aaa-player-page-host-items.txt:
//     each item keeps the area it was put in. A new area prefers empty cells;
//     if it lands on items of another area (or of none), they go back to the
//     character's inventory (else personal stash) when the game loads.
//     `pagehost free` only frees a place empty on every character. The record
//     is refreshed when the character is ready, on every host grid draw and
//     after every host item call.

#include "page_host_api.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace Comm    = D2RL::PluginCommunication;
namespace Inv     = D2RL::Inventory;
namespace Panels  = D2RL::Panels;
namespace Widgets = D2RL::Widgets;
using PageHost::AreaHandle;
using PageHost::Result;

constexpr const char* PluginId      = "aaa-player-page-host";
constexpr const char* PluginVersion = "1.0.0";
constexpr const char* LogicalPageId = "page"; // keep: saved items are keyed by it

const D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = PluginId,
	.name        = "Player Page Host",
	.version     = PluginVersion,
	.author      = "wild-healer",
	.description = "Owns the custom player page and shares it: other plugins reserve areas of it.",
	.flags       = D2RL::PluginFlags::Client,
};

constexpr int   PageSize  = PageHost::PageSize;
constexpr int   SlotCount = 12; // panels a client can lease
constexpr int   OverlayFirst = 8; // slots 8-11: overlay panels (no left slot, no Escape) for AreaOverlay leases
constexpr int   AllSlot   = SlotCount; // the whole-page view
constexpr int   Cell      = 98;
constexpr float AreaMarker = 7300.0f;

// ---- native layout (Steam 3.3.93787 / 94995) ----

using Code16 = std::array<std::uint8_t, 16>;
struct Native {
	std::uint64_t rva;
	Code16        bytes;
};
constexpr std::uint64_t InventoryRva = 0x34A360, FirstItemRva = 0x388C10, NextItemRva = 0x38ABA0, NodeModeRva = 0x38A930, CursorItemRva = 0x15F100,
                        GridOwnerRva = 0x2A7810, PointToCellRva = 0x2C6300, ItemSizeRva = 0x371850;
// The local player's unit, for the item record (checked apart: LocalIndex reads a global).
constexpr std::uint64_t LocalIndexRva = 0x8B2D0, PlayerUnitRva = 0x9A480;
// The item's put-down sound and the sound player, as the game's own Ctrl+click
// transfer plays them (0x15F9F4); PlaySound is hooked by others, so not checked.
constexpr std::uint64_t ItemSoundRva = 0x1C3420, PlaySoundRva = 0x1A0780;
constexpr Code16        ItemSoundBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48 };
constexpr Code16        PlayerUnitBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x83, 0xF9, 0x08, 0x0F, 0x83, 0x85 };
constexpr std::array<Native, 8> Natives { {
	{ InventoryRva, { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF1, 0x48, 0x85, 0xC9 } },
	{ FirstItemRva, { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x74, 0x2E, 0x81, 0x39 } },
	{ NextItemRva, { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x75, 0x10, 0x88, 0x4C } },
	{ NodeModeRva, { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x75, 0x10, 0x88, 0x4C } },
	{ CursorItemRva, { 0x48, 0x83, 0xEC, 0x28, 0xE8, 0xC7, 0xC1, 0xF2, 0xFF, 0x8B, 0xC8, 0xE8, 0x70, 0xB3, 0xF3, 0xFF } },
	{ GridOwnerRva, { 0x48, 0x83, 0xEC, 0x28, 0x44, 0x8B, 0x81, 0xC4, 0x05, 0x00, 0x00, 0x41, 0x83, 0xF8, 0xFF, 0x75 } },
	{ PointToCellRva, { 0x48, 0x8B, 0xC4, 0x4C, 0x89, 0x40, 0x18, 0x53, 0x55, 0x56, 0x57, 0x41, 0x56, 0x48, 0x81, 0xEC } },
	{ ItemSizeRva, { 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x20 } }, // called, never hooked
} };
constexpr std::uint64_t GridVtableRva = 0x1CF3800;
constexpr int           VtDraw = 3, VtItemRect = 16, VtValidCell = 18, VtCellAtPoint = 19, VtCellRect = 31;
constexpr std::size_t   WidgetPage = 0x630, WidgetMarker = 0x634;
constexpr std::size_t   UnitId = 0x08, UnitData = 0x10, UnitPath = 0x38, PathX = 0x10, PathY = 0x14, DataPage = 0x55;
constexpr std::uint8_t  PageHidden = 0xFE;
constexpr int           NodeInGrid = 1;
constexpr const char*   SourceName = "aaa-player-page-host";

using InventoryFn   = void* (*)(void* unit, const char* file, int line) noexcept;
using ItemFn        = std::uint8_t* (*)(void* inventoryOrItem) noexcept;
using NodeModeFn    = int (*)(void* item) noexcept;
using CursorItemFn  = void* (*)() noexcept;
using GridOwnerFn   = void* (*)(void* widget) noexcept;
using PointToCellFn = void (*)(void* widget, std::uint64_t* out, std::uint64_t point) noexcept;
using ItemSizeFn    = void (*)(void* item, std::uint8_t* width, std::uint8_t* height, const char* file, int line) noexcept;
using LocalIndexFn  = int (*)() noexcept;
using PlayerUnitFn  = void* (*)(int index) noexcept;
using ItemSoundFn   = int (*)(void* item) noexcept;
using PlaySoundFn   = void (*)(int sound, void* unit, int a, int b, bool c) noexcept;
using DrawFn        = std::uint64_t (*)(void* widget, void* a, void* b, void* c) noexcept;
using ItemRectFn    = std::int32_t* (*)(void* widget, std::int32_t* out, std::uint64_t cell) noexcept;
using CellRectFn    = std::int32_t* (*)(void* widget, std::int32_t* out, std::uint64_t size, std::uint64_t cell) noexcept;
using ValidCellFn   = bool (*)(void* widget, std::uint64_t cell) noexcept;
using CellFn        = std::uint64_t* (*)(void* widget, std::uint64_t* out, std::uint64_t point) noexcept;

std::uintptr_t             Base {};
const D2RL::PluginContext* Context {};
const D2RL::InventoryService*           Inventory {};
const D2RL::PanelService*               PanelApi {};
const D2RL::ResourceService*            ResourceApi {};
const D2RL::WidgetService*              WidgetApi {};
const D2RL::ItemService*                ItemApi {};
const D2RL::PluginCommunicationService* CommApi {};
Inv::PageRegistrationHandle             Page = Inv::InvalidHandle;
bool                                    NativesOk {};

InventoryFn   InventoryOf {};
ItemFn        FirstItem {};
ItemFn        NextItem {};
NodeModeFn    NodeMode {};
CursorItemFn  CursorItem {};
GridOwnerFn   GridOwner {};
PointToCellFn PointToCell {};
ItemSizeFn    ItemSize {};
LocalIndexFn  LocalIndex {};
PlayerUnitFn  PlayerUnit {};
ItemSoundFn   ItemSound {};
PlaySoundFn   PlaySoundAt {};
// The entries found in the vtable (stock, or another plugin's wrappers).
DrawFn      NextDraw {};
ItemRectFn  NextItemRect {};
ValidCellFn NextValidCell {};
CellFn      NextCellAtPoint {};
CellRectFn  NextCellRect {};
void**      Vtable {};

void Log(bool warn, const char* format, ...) noexcept {
	if (!Context) return;
	char    line[768] {};
	va_list args;
	va_start(args, format);
	std::vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	warn ? Context->LogWarn(line) : Context->LogInfo(line);
}

bool Read(std::uintptr_t address, void* output, std::size_t size) noexcept {
	SIZE_T copied = 0;
	return address != 0 && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), output, size, &copied) && copied == size;
}

constexpr std::uint64_t Pack(int x, int y) noexcept {
	return static_cast<std::uint32_t>(x) | static_cast<std::uint64_t>(static_cast<std::uint32_t>(y)) << 32;
}
constexpr int CellX(std::uint64_t cell) noexcept { return static_cast<std::int32_t>(cell & 0xFFFFFFFF); }
constexpr int CellY(std::uint64_t cell) noexcept { return static_cast<std::int32_t>(cell >> 32); }
constexpr std::uint64_t NoCell = ~std::uint64_t { 0 };

// ---- areas ----

// A saved place: plugin id + "/" + area name -> rectangle.
struct Place {
	std::string key;
	int         x {}, y {}, width {}, height {};
};

struct Lease {
	AreaHandle             handle {};
	std::string            client, name, label, key;
	int                    slot = -1;
	std::uint32_t          flags {};
	PageHost::ItemPolicyFn policy {};
	void*                  user {};
	std::string            layout; // the client's, empty for the default one
	int                    gridX {}, gridY {};
	int                    width {}, height {};
	bool                   placed {}; // false while it waits for the load-time packing
};

struct Slot {
	std::string                         id; // "Area3"
	Panels::RegistrationHandle          panel = Panels::InvalidHandle;
	D2RL::Resources::RegistrationHandle layoutHandle = D2RL::Resources::InvalidHandle;
	std::string                         registered; // the layout D2RLoader has
	bool                                opened {};
};

std::recursive_mutex Lock;
std::vector<Place>   Places;
std::vector<Lease>   Leases;
std::array<Slot, SlotCount + 1> Slots;
AreaHandle           NextHandle = 1;
std::wstring         PlacesPath;

// What the grid hooks read without the lock: each slot's area (width 0 = none).
struct Area {
	int x, y, width, height;
};
std::array<std::atomic<std::uint64_t>, SlotCount> SlotArea {}; // x | y << 8 | w << 16 | h << 24
std::array<std::atomic<std::uint64_t>, SlotCount + 1> LastDrawn {}; // tick of each slot grid's last draw
thread_local bool HostMoving = false;
std::atomic<std::uint32_t> CharmSlots {}; // a bit per slot whose area asked for charms

Area AreaOfSlot(int slot) noexcept {
	const std::uint64_t v = SlotArea[slot].load();
	return { static_cast<int>(v & 0xFF), static_cast<int>(v >> 8 & 0xFF), static_cast<int>(v >> 16 & 0xFF), static_cast<int>(v >> 24 & 0xFF) };
}
void SetSlotArea(int slot, int x, int y, int w, int h) noexcept {
	SlotArea[slot].store(static_cast<std::uint64_t>(x) | static_cast<std::uint64_t>(y) << 8 | static_cast<std::uint64_t>(w) << 16 | static_cast<std::uint64_t>(h) << 24);
}

bool Inside(const Area& a, int x, int y, int w = 1, int h = 1) noexcept {
	return a.width > 0 && x >= a.x && y >= a.y && x + w <= a.x + a.width && y + h <= a.y + a.height;
}

bool Overlaps(int ax, int ay, int aw, int ah, const Place& b) noexcept {
	return ax < b.x + b.width && b.x < ax + aw && ay < b.y + b.height && b.y < ay + ah;
}

void SavePlaces() {
	if (PlacesPath.empty()) return;
	std::string text = "# Player Page Host: where each plugin's area is on the custom page (plugin/area x y width height).\n"
	                   "# Written by the plugin. Removing a line frees that place; items still there stay on the page and keep new areas off their cells.\n";
	for (const auto& place : Places) text += place.key + " " + std::to_string(place.x) + " " + std::to_string(place.y) + " " + std::to_string(place.width) + " " + std::to_string(place.height) + "\n";
	if (FILE* out = _wfopen(PlacesPath.c_str(), L"wb")) {
		std::fwrite(text.data(), 1, text.size(), out);
		std::fclose(out);
	} else Log(true, "could not write %ls", PlacesPath.c_str());
}

void LoadPlaces() {
	FILE* in = _wfopen(PlacesPath.c_str(), L"rb");
	if (!in) return;
	char line[512];
	while (std::fgets(line, sizeof(line), in)) {
		if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
		char key[300] {};
		Place place;
		if (std::sscanf(line, "%299s %d %d %d %d", key, &place.x, &place.y, &place.width, &place.height) != 5) continue;
		place.key = key;
		if (place.width < 1 || place.height < 1 || place.x < 0 || place.y < 0 || place.x + place.width > PageSize || place.y + place.height > PageSize) {
			Log(true, "areas file: %s is off the page, ignored", key);
			continue;
		}
		Places.push_back(place);
	}
	std::fclose(in);
	Log(false, "%zu saved area place(s)", Places.size());
}

// ---- items on the page, by character ----

struct Rect {
	int         x, y, width, height;
	std::string owner; // the area (plugin/area) the item was put in, empty for none
	bool        operator==(const Rect&) const = default;
	bool        operator<(const Rect& o) const { return x != o.x ? x < o.x : y < o.y; }
	bool        SameCells(const Rect& o) const { return x == o.x && y == o.y && width == o.width && height == o.height; }
};
std::map<std::string, std::vector<Rect>> PageItems; // character -> its page items
std::wstring                             ItemsPath;
std::atomic<std::uint8_t>                PageByte { 6 }; // the page's item byte (ItemData+0x55), read from the grids

bool Overlaps(const Rect& a, const Rect& b) noexcept {
	return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}
bool Within(const Rect& item, const Place& place) noexcept {
	return item.x >= place.x && item.y >= place.y && item.x + item.width <= place.x + place.width && item.y + item.height <= place.y + place.height;
}

void SaveItems() {
	if (ItemsPath.empty()) return;
	std::string text = "# Player Page Host: where items lie on the custom page, per character (name x y width height area, - for none).\n"
	                   "# Written by the plugin, refreshed whenever the character plays.\n";
	for (const auto& [name, items] : PageItems)
		for (const auto& r : items)
			text += name + " " + std::to_string(r.x) + " " + std::to_string(r.y) + " " + std::to_string(r.width) + " " + std::to_string(r.height) + " " + (r.owner.empty() ? "-" : r.owner) + "\n";
	if (FILE* out = _wfopen(ItemsPath.c_str(), L"wb")) {
		std::fwrite(text.data(), 1, text.size(), out);
		std::fclose(out);
	} else Log(true, "could not write %ls", ItemsPath.c_str());
}

void LoadItems() {
	FILE* in = _wfopen(ItemsPath.c_str(), L"rb");
	if (!in) return;
	char line[512];
	while (std::fgets(line, sizeof(line), in)) {
		if (line[0] == '#') continue;
		char name[64] {}, owner[300] {};
		Rect r {};
		if (std::sscanf(line, "%63s %d %d %d %d %299s", name, &r.x, &r.y, &r.width, &r.height, owner) != 6 || r.width < 1 || r.height < 1) continue;
		r.owner = std::strcmp(owner, "-") == 0 ? "" : owner;
		PageItems[name].push_back(r);
	}
	std::fclose(in);
}

// The cells of [x, y, w, h] hold an item put in another area (or in none), on any character?
bool HoldsOtherItems(int x, int y, int w, int h, const std::string& key) {
	const Rect wanted { x, y, w, h, {} };
	for (const auto& [name, items] : PageItems)
		for (const auto& r : items)
			if (Overlaps(wanted, r) && r.owner != key) return true;
	return false;
}

// The characters with items in a place.
std::string CharactersIn(const Place& place) {
	std::string names;
	for (const auto& [name, items] : PageItems)
		for (const auto& r : items)
			if (Overlaps(Rect { place.x, place.y, place.width, place.height, {} }, r)) {
				names += (names.empty() ? "" : ", ") + name;
				break;
			}
	return names;
}

bool InUse(const std::string& key);

// No other area there; with avoidItems, no items of another area (or of none)
// either; with takeIdle, the saved places of plugins not loaded don't count.
bool FreeAt(int x, int y, int w, int h, const std::string& except, bool avoidItems = false, bool takeIdle = false) {
	if (x < 0 || y < 0 || x + w > PageSize || y + h > PageSize) return false;
	for (const auto& place : Places)
		if (place.key != except && Overlaps(x, y, w, h, place) && (!takeIdle || InUse(place.key))) return false;
	return !avoidItems || !HoldsOtherItems(x, y, w, h, except);
}

// The area whose place holds an item, empty for none.
std::string PlaceKeyOf(const Rect& r) {
	for (const auto& place : Places)
		if (Within(r, place)) return place.key;
	return {};
}

bool InUse(const std::string& key) {
	return std::any_of(Leases.begin(), Leases.end(), [&](const Lease& lease) { return lease.key == key; });
}

Lease* LeaseOf(AreaHandle handle) {
	for (auto& lease : Leases)
		if (lease.handle == handle) return &lease;
	return nullptr;
}

Place* PlaceOf(const std::string& key) {
	for (auto& place : Places)
		if (place.key == key) return &place;
	return nullptr;
}

// ---- layouts ----

std::string Json(const std::string& text) {
	std::string out = "\"";
	for (const char c : text) {
		if (c == '"' || c == '\\') out += '\\';
		if (static_cast<unsigned char>(c) >= 0x20) out += c;
	}
	return out + '"';
}

std::string PanelName(int slot) { return std::string(PluginId) + "/" + Slots[slot].id; }

std::string GridWidget(int slot, int x, int y) {
	char grid[512] {};
	std::snprintf(grid, sizeof(grid),
	              R"({"type": "InventoryGridWidget", "name": "aaa-player-page-host/Grid", "fields": {"rect": {"x": %d, "y": %d}, "cellCount": {"x": %d, "y": %d}, "cellSize": "$ItemCellSize", "gemSocketFilename": "PANEL\\gemsocket", "chronicleFlourishOffset": {"x": %.0f, "y": %.0f}}})",
	              x, y, PageSize, PageSize, AreaMarker + static_cast<float>(slot), AreaMarker);
	return grid;
}

void Replace(std::string& text, const std::string& from, const std::string& to) {
	for (std::size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) text.replace(at, from.size(), to);
}

constexpr int Margin = 48, TitleHeight = 110;
std::string BackgroundName(int w, int h) { return "bg" + std::to_string(w) + "x" + std::to_string(h); }

// A plain panel: a title, the area's cells and the grid over them.
std::string DefaultLayout(int slot, const std::string& label, int w, int h, bool close = true) {
	const int width = w * Cell + 2 * Margin, height = h * Cell + TitleHeight + Margin;
	char      open[1400] {};
	std::snprintf(open, sizeof(open),
	              R"({"type": "Panel", "name": %s, "fields": {"priority": 10, "rect": {"x": 236, "y": -856, "width": %d, "height": %d}, "anchor": "$LeftPanelAnchor"}, "children": [)"
	              R"({"type": "ClickCatcherWidget", "name": "PanelClickCatcher", "fields": {"rect": {"x": 0, "y": 0, "width": %d, "height": %d}}})"
	              R"(, {"type": "ImageWidget", "name": "Background", "fields": {"rect": {"x": 0, "y": 0}, "filename": "d2rloader\\aaa-player-page-host\\%s"}})"
	              R"(, {"type": "TextBoxWidget", "name": "Title", "fields": {"rect": {"x": %d, "y": 20, "width": %d, "height": 70}, "text": %s, "style": {"fontColor": "$FontColorGoldYellow", "pointSize": 40, "dropShadow": "$DefaultDropShadow", "alignment": {"h": "center", "v": "center"}}}})",
	              Json(PanelName(slot)).c_str(), width, height, width, height, BackgroundName(w, h).c_str(), Margin, w * Cell, Json(label).c_str());
	std::string layout = open;
	layout += ", " + GridWidget(slot, Margin, TitleHeight);
	if (close)
		layout += R"(, {"type": "ButtonWidget", "name": "CloseButton", "fields": {"rect": {"x": )" + std::to_string(width - 90)
		        + R"(, "y": 10}, "filename": "PANEL\\closebtn_4x", "hoveredFrame": 3, "tooltipString": "@d2r:strClose", "sound": "cursor_close_window_hd", "onClickMessage": )"
		        + Json("PanelManager:ClosePanel:" + PanelName(slot)) + "}}";
	return layout + "]}";
}

// ---- sprites ----

struct Picture {
	int                        width {}, height {};
	std::vector<unsigned char> rgba;
	unsigned char*             at(int x, int y) { return rgba.data() + (static_cast<std::size_t>(y) * width + x) * 4; }
};

Picture Half(const Picture& in) {
	Picture out { std::max(1, in.width / 2), std::max(1, in.height / 2), {} };
	out.rgba.resize(static_cast<std::size_t>(out.width) * out.height * 4);
	for (int y = 0; y < out.height; ++y)
		for (int x = 0; x < out.width; ++x)
			for (int k = 0; k < 4; ++k) {
				int total = 0;
				for (int dy = 0; dy < 2; ++dy)
					for (int dx = 0; dx < 2; ++dx) total += in.rgba[(static_cast<std::size_t>(std::min(in.height - 1, 2 * y + dy)) * in.width + std::min(in.width - 1, 2 * x + dx)) * 4 + k];
				out.rgba[(static_cast<std::size_t>(y) * out.width + x) * 4 + k] = static_cast<unsigned char>(total / 4);
			}
	return out;
}

std::string SpriteFile(const Picture& picture) {
	std::string out(40, '\0');
	const auto  put = [&](std::size_t at, std::uint32_t value, int bytes) {
        for (int i = 0; i < bytes; ++i) out[at + i] = static_cast<char>((value >> (8 * i)) & 0xFF);
	};
	std::memcpy(out.data(), "SpA1", 4);
	put(4, 31, 2);
	put(6, static_cast<std::uint32_t>(picture.width), 2);
	put(8, static_cast<std::uint32_t>(picture.width), 4);
	put(12, static_cast<std::uint32_t>(picture.height), 4);
	put(20, 1, 4);
	out.append(reinterpret_cast<const char*>(picture.rgba.data()), picture.rgba.size());
	return out;
}

// Dark stone with a gold rim, and the area's cells.
Picture Background(int w, int h) {
	Picture p { w * Cell + 2 * Margin, h * Cell + TitleHeight + Margin, {} };
	p.rgba.resize(static_cast<std::size_t>(p.width) * p.height * 4);
	for (int y = 0; y < p.height; ++y)
		for (int x = 0; x < p.width; ++x) {
			std::uint32_t n = static_cast<std::uint32_t>(x) * 374761393u + static_cast<std::uint32_t>(y) * 668265263u;
			n               = (n ^ (n >> 13)) * 1274126177u;
			const int grain = static_cast<int>((n ^ (n >> 16)) & 0x0F);
			unsigned char* px = p.at(x, y);
			const int      edge = std::min({ x, y, p.width - 1 - x, p.height - 1 - y });
			if (edge < 4) px[0] = 150, px[1] = 120, px[2] = 70;
			else if (edge < 8) px[0] = 40, px[1] = 34, px[2] = 26;
			else px[0] = static_cast<unsigned char>(22 + grain), px[1] = static_cast<unsigned char>(20 + grain), px[2] = static_cast<unsigned char>(18 + grain);
			px[3] = 255;
			const int gx = x - Margin, gy = y - TitleHeight;
			if (gx >= 0 && gy >= 0 && gx < w * Cell && gy < h * Cell) {
				const int cx = gx % Cell, cy = gy % Cell;
				const bool line = cx < 2 || cy < 2 || cx >= Cell - 2 || cy >= Cell - 2;
				if (line) px[0] = 70, px[1] = 62, px[2] = 50;
				else px[0] = static_cast<unsigned char>(8 + grain / 2), px[1] = static_cast<unsigned char>(8 + grain / 2), px[2] = static_cast<unsigned char>(10 + grain / 2);
			}
		}
	return p;
}

bool RegisterResource(const std::string& path, const std::string& bytes, D2RL::Resources::RegistrationHandle* handle) {
	const D2RL::Resources::ResourceRegistration resource {
		.structSize = D2RL::Resources::ResourceRegistrationSize,
		.path       = path.c_str(),
		.bytes      = bytes.data(),
		.byteCount  = bytes.size(),
	};
	D2RL::Resources::RegistrationHandle ignored = D2RL::Resources::InvalidHandle;
	return ResourceApi->registerResource(Context, &resource, handle ? handle : &ignored) == D2RL::Resources::Result::Success;
}

std::vector<std::string> Backgrounds; // sizes registered
void EnsureBackground(int w, int h) {
	const std::string name = BackgroundName(w, h);
	if (std::find(Backgrounds.begin(), Backgrounds.end(), name) != Backgrounds.end()) return;
	const Picture     full = Background(w, h);
	const std::string path = "data/hd/global/ui/d2rloader/aaa-player-page-host/" + name;
	if (RegisterResource(path + ".sprite", SpriteFile(full), nullptr) && RegisterResource(path + ".lowend.sprite", SpriteFile(Half(full)), nullptr)) Backgrounds.push_back(name);
	else Log(true, "could not register the %dx%d background", w, h);
}

// The layout a slot should have now. Lock held.
std::string WantedLayout(int slot) {
	if (slot == AllSlot) return DefaultLayout(AllSlot, "Player page (all areas)", PageSize, PageSize);
	for (const auto& lease : Leases)
		if (lease.slot == slot) {
			const Area a = AreaOfSlot(slot);
			if (lease.layout.empty() && slot >= OverlayFirst) {
				char open[512] {};
				std::snprintf(open, sizeof(open), R"({"type": "Panel", "name": %s, "fields": {"priority": 11, "rect": {"x": 236, "y": -856, "width": %d, "height": %d}, "anchor": "$LeftPanelAnchor"}, "children": [)",
				              Json(PanelName(slot)).c_str(), a.width * Cell, a.height * Cell);
				return open + GridWidget(slot, lease.gridX, lease.gridY) + "]}";
			}
			if (lease.layout.empty()) {
				EnsureBackground(a.width, a.height);
				return DefaultLayout(slot, lease.label, a.width, a.height);
			}
			std::string layout = lease.layout;
			Replace(layout, "@PANEL@", PanelName(slot));
			Replace(layout, "@GRID@", GridWidget(slot, lease.gridX, lease.gridY));
			return layout;
		}
	return DefaultLayout(slot, "", 1, 1, false);
}

// Gives D2RLoader the slot's layout if it changed. Lock held.
bool SyncLayout(int slot) {
	auto&             s      = Slots[slot];
	const std::string layout = WantedLayout(slot);
	if (layout == s.registered && s.layoutHandle != D2RL::Resources::InvalidHandle) return true;
	if (s.opened && WidgetApi) {
		const std::string     name = PanelName(slot);
		const Widgets::UiAction unload { .structSize = Widgets::UiActionSize, .flags = 0, .target = "PanelManager", .command = "UnloadPanel", .text = name.c_str() };
		(void)WidgetApi->dispatchUiAction(Context, &unload);
		s.opened = false;
	}
	if (s.layoutHandle != D2RL::Resources::InvalidHandle) (void)ResourceApi->unregisterResource(Context, s.layoutHandle);
	s.layoutHandle = D2RL::Resources::InvalidHandle;
	if (!RegisterResource("data/global/ui/layouts/aaa-player-page-host/" + s.id + "hd.json", layout, &s.layoutHandle)) {
		Log(true, "could not register the layout of %s", s.id.c_str());
		s.registered.clear();
		return false;
	}
	s.registered = layout;
	return true;
}

bool PanelOpen(int slot) {
	if (Slots[slot].panel == Panels::InvalidHandle) return false;
	Panels::PanelInfo info { .structSize = Panels::PanelInfoSize };
	return PanelApi->getPanelInfo(Context, Slots[slot].panel, &info) == Panels::Result::Success && info.presentationState == Panels::PresentationState::Open;
}

bool LocalPlayer(D2RL::PlayerHandle& player);

Result OpenSlot(int slot) {
	if (Slots[slot].panel == Panels::InvalidHandle) return Result::NoPanel;
	if (!SyncLayout(slot)) return Result::Failed;
	D2RL::PlayerHandle player {};
	(void)LocalPlayer(player); // captured on the UI thread, for createItem / moveItems later
	const auto opened = PanelApi->openPanel(Context, Slots[slot].panel);
	if (opened != Panels::Result::Success) {
		Log(true, "could not open %s (panel result %u)", Slots[slot].id.c_str(), static_cast<unsigned>(opened));
		return Result::Failed;
	}
	Slots[slot].opened = true;
	return Result::Success;
}

// ---- grid hooks (UI thread) ----

// The slot of a host grid widget, AllSlot for the whole-page view, -1 for any other grid.
// Only area slots are limited (AreaSlotOf); the whole-page view is plain.
int SlotOf(const void* widget) noexcept {
	float marker[2] {};
	std::memcpy(marker, static_cast<const std::uint8_t*>(widget) + WidgetMarker, sizeof(marker));
	if (marker[1] != AreaMarker) return -1;
	const int slot = static_cast<int>(marker[0] - AreaMarker);
	return slot >= 0 && slot <= SlotCount && marker[0] == AreaMarker + static_cast<float>(slot) ? slot : -1;
}

int AreaSlotOf(const void* widget) noexcept {
	const int slot = SlotOf(widget);
	return slot == AllSlot ? -1 : slot;
}

void NoteWidget(void* widget, int slot) noexcept {
	static std::array<void*, 32> seen {};
	for (auto& known : seen) {
		if (known == widget) return;
		if (!known) {
			known = widget;
			const Area a = AreaOfSlot(slot);
			Log(false, "area grid %p (Area%d): page byte %u, owner %p, area %d,%d %dx%d", widget, slot, static_cast<const std::uint8_t*>(widget)[WidgetPage], GridOwner(widget), a.x, a.y,
			    a.width, a.height);
			return;
		}
	}
}

auto __cdecl FindItem(const D2RL::PluginContext*, const D2RL::Items::ItemInfo* item, void* user) noexcept -> Inv::IterationAction;
struct Find {
	std::uint32_t    runtimeId;
	D2RL::ItemHandle handle;
};

// Moves a page item to the inventory, else the personal stash (game thread).
bool GiveBack(std::uint32_t runtimeId) {
	D2RL::PlayerHandle player {};
	if (!ItemApi || !LocalPlayer(player)) return false;
	Find find { runtimeId, {} };
	const Inv::ItemFilter everything { .structSize = sizeof(Inv::ItemFilter), .containerMask = ~0U };
	(void)Inventory->forEachInventoryItem(Context, player, &everything, FindItem, &find);
	if (!find.handle) return false;
	for (const auto container : { D2RL::Items::ItemContainer::Inventory, D2RL::Items::ItemContainer::PersonalStash }) {
		D2RL::Items::ExistingItemOperation operation { .structSize = sizeof(D2RL::Items::ExistingItemOperation), .kind = D2RL::Items::ExistingItemOperationKind::Move, .item = find.handle };
		operation.move.destination = { .structSize = sizeof(D2RL::Items::ItemDestination), .container = container, .placement = D2RL::Items::Placement::Automatic };
		const D2RL::Items::ExistingItemTransaction transaction {
			.structSize = sizeof(D2RL::Items::ExistingItemTransaction), .player = player, .operationCount = 1, .operations = &operation };
		D2RL::Items::ExistingItemTransactionResult result { .structSize = sizeof(D2RL::Items::ExistingItemTransactionResult) };
		if (ItemApi->executeExistingItemTransaction(Context, &transaction, &result) == D2RL::Items::Result::Success) return true;
	}
	return false;
}

// Records where the playing character's page items are, each with the area it
// was put in (UI or game thread). With giveBack (game load), items lying in an
// area claimed after they were put there go back to the character.
void ScanPage(bool giveBack = false) noexcept {
	if (!NativesOk || !PlayerUnit) return;
	void* const unit = PlayerUnit(LocalIndex());
	void*       data = nullptr;
	char        text[17] {};
	if (!unit || !Read(reinterpret_cast<std::uintptr_t>(unit) + UnitData, &data, sizeof(data)) || !Read(reinterpret_cast<std::uintptr_t>(data), text, 16)) return;
	const std::string name = text;
	if (name.empty() || std::any_of(name.begin(), name.end(), [](char c) { return static_cast<unsigned char>(c) <= 32; })) return;
	void* const inventory = InventoryOf(unit, SourceName, __LINE__);
	if (!inventory) return;
	const std::uint8_t page = PageByte.load();
	struct Seen {
		Rect          rect;
		std::uint32_t runtimeId;
	};
	std::vector<Seen> found;
	int               total = 0;
	for (std::uint8_t* item = FirstItem(inventory); item && total < 4096; item = NextItem(item), ++total) {
		if (NodeMode(item) != NodeInGrid) continue;
		auto* const itemData = *reinterpret_cast<std::uint8_t**>(item + UnitData);
		auto* const path     = *reinterpret_cast<std::uint8_t**>(item + UnitPath);
		if (!itemData || !path || itemData[DataPage] != page) continue;
		std::uint8_t w = 1, h = 1;
		ItemSize(item, &w, &h, SourceName, __LINE__);
		found.push_back({ { *reinterpret_cast<std::int32_t*>(path + PathX), *reinterpret_cast<std::int32_t*>(path + PathY), w, h, {} }, *reinterpret_cast<std::uint32_t*>(item + UnitId) });
	}
	if (total == 0) return; // not loaded yet: keep what is known
	std::lock_guard guard(Lock);
	const auto              known = PageItems.find(name);
	const std::vector<Rect> before = known == PageItems.end() ? std::vector<Rect> {} : known->second;
	std::vector<Rect>       now;
	int                     returned = 0, stuck = 0;
	for (auto& seen : found) {
		// An item still where it was keeps its area; a new or moved one takes the area it is in.
		const auto old = std::find_if(before.begin(), before.end(), [&](const Rect& r) { return r.SameCells(seen.rect); });
		const std::string here = PlaceKeyOf(seen.rect);
		seen.rect.owner = old != before.end() ? old->owner : here;
		if (giveBack && !here.empty() && here != seen.rect.owner) {
			if (GiveBack(seen.runtimeId)) {
				++returned;
				Log(false, "%s: the item at %d,%d (%s) lies in %s, claimed after it was put there: given back", name.c_str(), seen.rect.x, seen.rect.y,
				    seen.rect.owner.empty() ? "no area" : seen.rect.owner.c_str(), here.c_str());
				continue;
			}
			++stuck;
			Log(true, "%s: the item at %d,%d lies in %s, claimed after it was put there, but the inventory and stash are full: it stays (next game load tries again)", name.c_str(),
			    seen.rect.x, seen.rect.y, here.c_str());
		}
		now.push_back(seen.rect);
	}
	if (returned || stuck) Log(!!stuck, "%s: %d item(s) given back from areas claimed over them, %d left", name.c_str(), returned, stuck);
	std::sort(now.begin(), now.end());
	if (now == before) return;
	if (now.empty()) PageItems.erase(name);
	else PageItems[name] = std::move(now);
	SaveItems();
}

std::uint64_t HookDraw(void* widget, void* a, void* b, void* c) noexcept {
	const int slot = widget ? SlotOf(widget) : -1;
	if (slot >= 0) {
		LastDrawn[slot].store(GetTickCount64());
		PageByte.store(static_cast<const std::uint8_t*>(widget)[WidgetPage]);
		ScanPage(); // drops and pick-ups only happen while a host grid shows
	}
	if (slot < 0 || slot == AllSlot) return NextDraw(widget, a, b, c);
	NoteWidget(widget, slot);
	const Area area = AreaOfSlot(slot);
	void* const owner = GridOwner(widget);
	void* const inventory = owner && area.width > 0 ? InventoryOf(owner, SourceName, __LINE__) : nullptr;
	if (!inventory) return 0; // nothing rather than the whole page in the wrong place
	const std::uint8_t page = static_cast<const std::uint8_t*>(widget)[WidgetPage];
	std::array<std::uint8_t*, 512> hidden {};
	std::size_t count = 0;
	for (std::uint8_t* item = FirstItem(inventory); item && count < hidden.size(); item = NextItem(item)) {
		if (NodeMode(item) != NodeInGrid) continue;
		auto* const data = *reinterpret_cast<std::uint8_t**>(item + UnitData);
		auto* const path = *reinterpret_cast<std::uint8_t**>(item + UnitPath);
		if (!data || !path || data[DataPage] != page) continue;
		const int x = *reinterpret_cast<std::int32_t*>(path + PathX), y = *reinterpret_cast<std::int32_t*>(path + PathY);
		if (Inside(area, x, y)) continue;
		data[DataPage]  = PageHidden;
		hidden[count++] = data;
	}
	const auto result = NextDraw(widget, a, b, c);
	for (std::size_t i = 0; i < count; ++i) hidden[i][DataPage] = page;
	return result;
}

// An area cell -> the widget's own cell; anything else as is.
std::uint64_t ToWidget(int slot, std::uint64_t cell) noexcept {
	const Area a = AreaOfSlot(slot);
	if (cell == NoCell || !Inside(a, CellX(cell), CellY(cell))) return cell;
	return Pack(CellX(cell) - a.x, CellY(cell) - a.y);
}

std::int32_t* HookItemRect(void* widget, std::int32_t* out, std::uint64_t cell) noexcept {
	const int slot = widget ? AreaSlotOf(widget) : -1;
	return NextItemRect(widget, out, slot < 0 ? cell : ToWidget(slot, cell));
}

std::int32_t* HookCellRect(void* widget, std::int32_t* out, std::uint64_t size, std::uint64_t cell) noexcept {
	const int slot = widget ? AreaSlotOf(widget) : -1;
	return NextCellRect(widget, out, size, slot < 0 ? cell : ToWidget(slot, cell));
}

bool HookValidCell(void* widget, std::uint64_t cell) noexcept {
	const int slot = widget ? AreaSlotOf(widget) : -1;
	if (slot < 0) return NextValidCell(widget, cell);
	return cell != NoCell && Inside(AreaOfSlot(slot), CellX(cell), CellY(cell)) && NextValidCell(widget, cell);
}

std::uint64_t* HookCellAtPoint(void* widget, std::uint64_t* out, std::uint64_t point) noexcept {
	const int slot = widget ? AreaSlotOf(widget) : -1;
	if (slot < 0) return NextCellAtPoint(widget, out, point);
	*out = NoCell;
	const Area a = AreaOfSlot(slot);
	if (a.width <= 0) return out;
	std::uint64_t cell = NoCell;
	PointToCell(widget, &cell, point); // widget cell; with an item held, its top-left cell
	const int x = CellX(cell) + a.x, y = CellY(cell) + a.y;
	int       w = 1, h = 1;
	if (void* const held = CursorItem()) {
		std::uint8_t iw = 1, ih = 1;
		ItemSize(held, &iw, &ih, SourceName, __LINE__);
		w = iw, h = ih;
	}
	if (!Inside(a, x, y, w, h)) return out;
	if (!NextValidCell(widget, Pack(x, y))) return out;
	*out = Pack(x, y);
	return out;
}

bool PatchVtable() noexcept {
	Vtable = reinterpret_cast<void**>(Base + GridVtableRva);
	void* entries[VtCellRect + 1] {};
	if (!Read(reinterpret_cast<std::uintptr_t>(Vtable), entries, sizeof(entries))) return false;
	for (const int index : { VtDraw, VtItemRect, VtValidCell, VtCellAtPoint, VtCellRect })
		if (!entries[index]) return false;
	NextDraw        = reinterpret_cast<DrawFn>(entries[VtDraw]);
	NextItemRect    = reinterpret_cast<ItemRectFn>(entries[VtItemRect]);
	NextValidCell   = reinterpret_cast<ValidCellFn>(entries[VtValidCell]);
	NextCellAtPoint = reinterpret_cast<CellFn>(entries[VtCellAtPoint]);
	NextCellRect    = reinterpret_cast<CellRectFn>(entries[VtCellRect]);
	DWORD protect = 0;
	if (!VirtualProtect(Vtable, (VtCellRect + 1) * sizeof(void*), PAGE_READWRITE, &protect)) return false;
	Vtable[VtDraw]        = reinterpret_cast<void*>(&HookDraw);
	Vtable[VtItemRect]    = reinterpret_cast<void*>(&HookItemRect);
	Vtable[VtValidCell]   = reinterpret_cast<void*>(&HookValidCell);
	Vtable[VtCellAtPoint] = reinterpret_cast<void*>(&HookCellAtPoint);
	Vtable[VtCellRect]    = reinterpret_cast<void*>(&HookCellRect);
	VirtualProtect(Vtable, (VtCellRect + 1) * sizeof(void*), protect, &protect);
	for (const int index : { VtDraw, VtItemRect, VtValidCell, VtCellAtPoint, VtCellRect }) {
		const auto rva = reinterpret_cast<std::uintptr_t>(entries[index]) - Base;
		Log(false, "grid vtable entry %d wrapped (was %s+0x%llX)", index, rva < 0x4000000 ? "D2R" : "outside D2R, 0x", static_cast<unsigned long long>(rva < 0x4000000 ? rva : reinterpret_cast<std::uintptr_t>(entries[index])));
	}
	return true;
}

void RestoreVtable() noexcept {
	if (!Vtable || !NextDraw) return;
	DWORD protect = 0;
	if (!VirtualProtect(Vtable, (VtCellRect + 1) * sizeof(void*), PAGE_READWRITE, &protect)) return;
	// Only entries still ours: a plugin that wrapped on top keeps its own.
	const auto put = [](int index, void* ours, void* next) {
		if (Vtable[index] == ours) Vtable[index] = next;
	};
	put(VtDraw, reinterpret_cast<void*>(&HookDraw), reinterpret_cast<void*>(NextDraw));
	put(VtItemRect, reinterpret_cast<void*>(&HookItemRect), reinterpret_cast<void*>(NextItemRect));
	put(VtValidCell, reinterpret_cast<void*>(&HookValidCell), reinterpret_cast<void*>(NextValidCell));
	put(VtCellAtPoint, reinterpret_cast<void*>(&HookCellAtPoint), reinterpret_cast<void*>(NextCellAtPoint));
	put(VtCellRect, reinterpret_cast<void*>(&HookCellRect), reinterpret_cast<void*>(NextCellRect));
	VirtualProtect(Vtable, (VtCellRect + 1) * sizeof(void*), protect, &protect);
}

// ---- charms by area ----
// The game asks whether a charm gives its bonuses through a D2RCore jump table
// thunk (exe+0x3E2B61C: jmp [exe+0x3E2ACA0], 12 call sites) that D2RCore
// points at its IsCharmUsable export (charm type, the page rule of the charm
// policy, then the requirements). The page's charm policy lets page charms
// count, and the slot is wrapped on the first DataTablesLoaded event: a charm
// on the page counts only inside an area that asked for charms (D2RCore's
// check then, for the requirements); anything else goes to the slot's previous
// target (D2RCore, or another plugin's wrapper such as Charm Zone's).
constexpr std::uint64_t CharmThunkRva = 0x3E2B61C, CharmSlotRva = 0x3E2ACA0;
constexpr std::uint8_t  CustomPage = 6; // the custom page's item page byte (D2RCore "custom page-6")
using CharmFn = std::int32_t (*)(void* item, void* unit) noexcept;
CharmFn NextCharm {};
CharmFn CoreCharm {};
bool    CharmSlotOk {};

bool CheckCharmSlot() noexcept {
	std::uint8_t thunk[6] {};
	std::int32_t rel = 0;
	if (!Read(Base + CharmThunkRva, thunk, sizeof(thunk)) || thunk[0] != 0xFF || thunk[1] != 0x25) return false;
	std::memcpy(&rel, thunk + 2, sizeof(rel));
	if (CharmThunkRva + 6 + rel != CharmSlotRva) return false;
	std::uint64_t target = 0;
	if (!Read(Base + CharmSlotRva, &target, sizeof(target)) || !target) return false;
	if (HMODULE core = GetModuleHandleW(L"D2RCore.dll")) CoreCharm = reinterpret_cast<CharmFn>(GetProcAddress(core, "IsCharmUsable"));
	return true;
}

std::int32_t HookCharm(void* item, void* unit) noexcept {
	auto* const bytes = static_cast<std::uint8_t*>(item);
	auto* const data  = bytes ? *reinterpret_cast<std::uint8_t**>(bytes + UnitData) : nullptr;
	if (!data || data[DataPage] != CustomPage) return NextCharm(item, unit);
	auto* const path = *reinterpret_cast<std::uint8_t**>(bytes + UnitPath);
	if (!path) return 0;
	const int           x = *reinterpret_cast<std::int32_t*>(path + PathX), y = *reinterpret_cast<std::int32_t*>(path + PathY);
	const std::uint32_t charms = CharmSlots.load();
	for (int slot = 0; slot < SlotCount; ++slot)
		if ((charms >> slot & 1) && Inside(AreaOfSlot(slot), x, y)) return CoreCharm ? CoreCharm(item, unit) : NextCharm(item, unit);
	return 0;
}

bool SwapSlot(std::uint64_t* slot, std::uint64_t from, std::uint64_t to) noexcept {
	DWORD keep = 0;
	if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &keep)) return false;
	const bool swapped = InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(slot), static_cast<LONG64>(to), static_cast<LONG64>(from)) == static_cast<LONG64>(from);
	VirtualProtect(slot, sizeof(*slot), keep, &keep);
	return swapped;
}

bool WrapCharmSlot() noexcept {
	auto* const   slot    = reinterpret_cast<std::uint64_t*>(Base + CharmSlotRva);
	const auto    current = *slot;
	if (!current) return false;
	NextCharm = reinterpret_cast<CharmFn>(current);
	if (!SwapSlot(slot, current, reinterpret_cast<std::uint64_t>(&HookCharm))) {
		NextCharm = nullptr;
		return false;
	}
	HMODULE owner {};
	char    name[MAX_PATH] = "?";
	if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(current), &owner)) GetModuleFileNameA(owner, name, sizeof(name));
	const char* file = std::strrchr(name, '\\');
	Log(false, "charm check wrapped (was %s; D2RCore's own check %s)", file ? file + 1 : name, CoreCharm ? "found" : "NOT found");
	return true;
}

void UnwrapCharmSlot() noexcept {
	if (NextCharm) (void)SwapSlot(reinterpret_cast<std::uint64_t*>(Base + CharmSlotRva), reinterpret_cast<std::uint64_t>(&HookCharm), reinterpret_cast<std::uint64_t>(NextCharm));
}

// ---- the page's item policy ----

// The slot whose grid is on screen (drawn in the last 300 ms), or -1.
int ShownSlot() noexcept {
	const std::uint64_t now = GetTickCount64();
	int                 best = -1;
	std::uint64_t       latest = 0;
	for (int slot = 0; slot <= SlotCount; ++slot) {
		const std::uint64_t at = LastDrawn[slot].load();
		if (at && now - at < 300 && at > latest) best = slot, latest = at;
	}
	return best;
}

// ---- Ctrl+click into the page ----
// D2RLoader's own quick transfer would pick the first free cell of the whole
// page, maybe in another area. So a Ctrl+click on an item outside the page,
// while a host grid shows, is taken by the host's item click listener (lowest
// priority: a client's own Ctrl+click handling goes first): it picks the first
// free cell of the area shown (or, in the whole-page view, outside every
// area) and moves the item there itself, asking the area's policy as for a
// drop. No room or a refusal: nothing moves.

namespace Clicks = D2RL::ItemInteractions;
const D2RL::ThreadService* ThreadApi {};
bool                       ClicksOn {};
thread_local int           CtrlSlot = -1; // the slot a host Ctrl+click move is for

// The local player's page inventory (UI or game thread).
void* PlayerInventory() noexcept {
	void* const unit = NativesOk && PlayerUnit ? PlayerUnit(LocalIndex()) : nullptr;
	return unit ? InventoryOf(unit, SourceName, __LINE__) : nullptr;
}

// The first free top-left cell for a w x h item in the slot's area, column by
// column like the game.
bool FreeCell(int slot, int w, int h, std::uint32_t skipId, int& outX, int& outY) noexcept {
	void* const inventory = PlayerInventory();
	if (!inventory) return false;
	const std::uint8_t page = PageByte.load();
	std::array<std::array<bool, PageSize>, PageSize> taken {}; // [y][x]
	for (std::uint8_t* other = FirstItem(inventory); other; other = NextItem(other)) {
		if (NodeMode(other) != NodeInGrid || *reinterpret_cast<std::uint32_t*>(other + UnitId) == skipId) continue;
		auto* const data = *reinterpret_cast<std::uint8_t**>(other + UnitData);
		auto* const path = *reinterpret_cast<std::uint8_t**>(other + UnitPath);
		if (!data || !path || data[DataPage] != page) continue;
		std::uint8_t ow = 1, oh = 1;
		ItemSize(other, &ow, &oh, SourceName, __LINE__);
		const int ox = *reinterpret_cast<std::int32_t*>(path + PathX), oy = *reinterpret_cast<std::int32_t*>(path + PathY);
		for (int cy = std::max(oy, 0); cy < std::min(oy + oh, PageSize); ++cy)
			for (int cx = std::max(ox, 0); cx < std::min(ox + ow, PageSize); ++cx) taken[cy][cx] = true;
	}
	Area               area { 0, 0, PageSize, PageSize };
	std::vector<Place> others;
	{
		std::lock_guard guard(Lock);
		if (slot != AllSlot) area = AreaOfSlot(slot);
		else others = Places; // the whole-page view: outside every area
	}
	const auto fits = [&](int tx, int ty) {
		for (int cy = ty; cy < ty + h; ++cy)
			for (int cx = tx; cx < tx + w; ++cx)
				if (taken[cy][cx]) return false;
		return std::none_of(others.begin(), others.end(), [&](const Place& p) { return Overlaps(tx, ty, w, h, p); });
	};
	for (int tx = area.x; tx + w <= area.x + area.width; ++tx)
		for (int ty = area.y; ty + h <= area.y + area.height; ++ty)
			if (fits(tx, ty)) {
				outX = tx, outY = ty;
				return true;
			}
	return false;
}

// The local player's item unit with that id.
std::uint8_t* ItemUnit(std::uint32_t id) noexcept {
	void* const inventory = PlayerInventory();
	for (std::uint8_t* item = inventory ? FirstItem(inventory) : nullptr; item; item = NextItem(item))
		if (*reinterpret_cast<std::uint32_t*>(item + UnitId) == id) return item;
	return nullptr;
}

struct CtrlMove {
	int           slot;
	std::uint32_t runtimeId;
	int           x, y;
};

void __cdecl RunCtrlMove(const D2RL::PluginContext*, void* user) noexcept {
	const CtrlMove move = *static_cast<CtrlMove*>(user);
	delete static_cast<CtrlMove*>(user);
	D2RL::PlayerHandle player {};
	if (!ItemApi || !LocalPlayer(player)) return;
	Find find { move.runtimeId, {} };
	const Inv::ItemFilter everything { .structSize = sizeof(Inv::ItemFilter), .containerMask = ~0U };
	(void)Inventory->forEachInventoryItem(Context, player, &everything, FindItem, &find);
	if (!find.handle) return;
	D2RL::Items::ExistingItemOperation operation { .structSize = sizeof(D2RL::Items::ExistingItemOperation), .kind = D2RL::Items::ExistingItemOperationKind::Move, .item = find.handle };
	operation.move.destination = { .structSize = sizeof(D2RL::Items::ItemDestination), .container = D2RL::Items::ItemContainer::CustomPage, .placement = D2RL::Items::Placement::Exact,
		                           .customPageHandle = Page, .x = static_cast<std::uint32_t>(move.x), .y = static_cast<std::uint32_t>(move.y) };
	const D2RL::Items::ExistingItemTransaction transaction { .structSize = sizeof(D2RL::Items::ExistingItemTransaction), .player = player, .operationCount = 1, .operations = &operation };
	D2RL::Items::ExistingItemTransactionResult result { .structSize = sizeof(D2RL::Items::ExistingItemTransactionResult) };
	CtrlSlot         = move.slot;
	const auto moved = ItemApi->executeExistingItemTransaction(Context, &transaction, &result);
	CtrlSlot         = -1;
	if (moved == D2RL::Items::Result::Success && ItemSound)
		if (std::uint8_t* const unit = ItemUnit(move.runtimeId)) PlaySoundAt(ItemSound(unit), nullptr, 0, 0, false);
	static std::atomic<int> told {};
	if (told.fetch_add(1) < 8 || moved != D2RL::Items::Result::Success)
		Log(false, "Ctrl+click: item %u to page cell %d,%d (%s): item result %u", move.runtimeId, move.x, move.y, move.slot == AllSlot ? "whole page" : Slots[move.slot].id.c_str(),
		    static_cast<unsigned>(moved));
	ScanPage();
}

auto __cdecl OnItemClick(const D2RL::PluginContext*, const Clicks::ItemInteractionEvent* event, void*) noexcept -> Clicks::Decision {
	if (!event || event->action != Clicks::Action::Activate || event->container == D2RL::Items::ItemContainer::CustomPage) return Clicks::Decision::Continue;
	const bool ctrl = (event->modifiers & Clicks::ModifierBit(Clicks::Modifier::Control)) || (GetAsyncKeyState(VK_CONTROL) & 0x8000);
	if (!ctrl || Page == Inv::InvalidHandle) return Clicks::Decision::Continue;
	const int slot = ShownSlot();
	if (slot < 0) return Clicks::Decision::Continue; // no host grid on screen: not for the page
	D2RL::Items::ItemInfo info { .structSize = D2RL::Items::ItemInfoSize };
	if (ItemApi->getItemInfo(Context, event->item, &info) != D2RL::Items::Result::Success) return Clicks::Decision::Continue;
	std::uint8_t* const unit = ItemUnit(info.runtimeId);
	std::uint8_t        w = 1, h = 1;
	if (unit) ItemSize(unit, &w, &h, SourceName, __LINE__);
	int x = -1, y = -1;
	if (!unit || !FreeCell(slot, w, h, info.runtimeId, x, y)) {
		Log(false, "Ctrl+click: no room for a %ux%u item in %s%s", w, h, slot == AllSlot ? "the page outside the areas" : Slots[slot].id.c_str(), unit ? "" : " (item not found)");
		return Clicks::Decision::Consume;
	}
	auto* move = new (std::nothrow) CtrlMove { slot, info.runtimeId, x, y };
	if (!move || ThreadApi->runOnGameThread(Context, RunCtrlMove, move) != D2RL::Threads::Result::Success) delete move;
	return Clicks::Decision::Consume;
}

auto __cdecl OnPagePolicy(const D2RL::PluginContext*, const Inv::ItemPolicyRequest* request, void*) noexcept -> Inv::ItemPolicyDecision {
	using Decision = Inv::ItemPolicyDecision;
	if (!request || request->reason == Inv::ItemPlacementReason::SavedItemRestore || HostMoving) return Decision::Allow;
	const int slot = CtrlSlot >= 0 ? CtrlSlot : ShownSlot();
	if (slot == AllSlot) return Decision::Allow;
	if (slot < 0) return Decision::Reject;
	// D2RLoader's own Ctrl+click transfer (no cursor item) would choose any cell
	// of the page: refused. The host's own Ctrl+click move comes with CtrlSlot.
	if (CtrlSlot < 0 && NativesOk && !CursorItem()) {
		static std::atomic<int> told {};
		if (told.fetch_add(1) < 5) Log(false, "a Ctrl+click transfer the host didn't place was refused (item click listener %s)", ClicksOn ? "on" : "off");
		return Decision::Reject;
	}
	PageHost::ItemPolicyFn policy {};
	void*                  user {};
	AreaHandle             handle {};
	{
		std::lock_guard guard(Lock);
		for (const auto& lease : Leases)
			if (lease.slot == slot) policy = lease.policy, user = lease.user, handle = lease.handle;
	}
	if (!handle) return Decision::Reject;
	if (!policy) return Decision::Allow;
	// A client whose DLL is gone (removed or unloaded): its area takes nothing new.
	HMODULE module {};
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(policy), &module)) return Decision::Reject;
	return policy(handle, request, user);
}

// ---- the service ----

void FillInfo(const Lease& lease, PageHost::AreaInfo* info) {
	const Area a = AreaOfSlot(lease.slot);
	const auto size = info->structSize;
	std::memset(info, 0, std::min<std::size_t>(size, sizeof(PageHost::AreaInfo)));
	info->structSize   = size;
	info->flags        = lease.flags;
	info->handle       = lease.handle;
	info->x            = lease.placed ? a.x : -1;
	info->y            = lease.placed ? a.y : -1;
	info->width        = static_cast<std::uint32_t>(lease.width);
	info->height       = static_cast<std::uint32_t>(lease.height);
	info->charmsActive = (lease.flags & PageHost::AreaCharms) && CharmSlotOk ? 1 : 0;
	std::snprintf(info->panelName, sizeof(info->panelName), "%s", PanelName(lease.slot).c_str());
	info->pageHandle    = Page;
	info->gridMarker[0] = AreaMarker + static_cast<float>(lease.slot);
	info->gridMarker[1] = AreaMarker;
}

// Areas are placed by the host. A saved place of the same size comes back at
// once; new (or resized) areas asked for before every plugin has loaded wait
// and are packed together, largest first, at the first DataTablesLoaded; later
// ones are placed at once. Cells without other items first, then cells with
// items (they go back at game load), last the saved places of plugins not
// loaded now.
bool                    Packed {};       // the load-time areas are placed
std::vector<AreaHandle> Refused;         // areas that found no room when packed

void ApplyPlace(Lease& lease, int x, int y);

// After the load-time packing: a new area goes where it fits now.
bool PlaceLease(Lease& lease) {
	const int w = lease.width, h = lease.height;
	const std::string& key = lease.key;
	int    x = -1, y = -1;
	Place* saved = PlaceOf(key);
	if (saved && saved->width == w && saved->height == h) x = saved->x, y = saved->y;
	else
		for (const auto& [avoidItems, takeIdle] : { std::pair { true, false }, std::pair { false, false }, std::pair { false, true } }) {
			if (x < 0 && saved && FreeAt(saved->x, saved->y, w, h, key, avoidItems, takeIdle)) x = saved->x, y = saved->y; // resized in place
			for (int ty = 0; ty + h <= PageSize && x < 0; ++ty)
				for (int tx = 0; tx + w <= PageSize && x < 0; ++tx)
					if (FreeAt(tx, ty, w, h, key, avoidItems, takeIdle)) x = tx, y = ty;
		}
	if (x < 0) {
		Log(true, "%s: no room for %dx%d on the page beside the other areas", key.c_str(), w, h);
		return false;
	}
	ApplyPlace(lease, x, y);
	return true;
}

void ApplyPlace(Lease& lease, int x, int y) {
	const int w = lease.width, h = lease.height;
	const std::string& key = lease.key;
	// Saved places of plugins not loaded that the new place covers are dropped;
	// their items go back to their characters at game load.
	for (auto place = Places.begin(); place != Places.end();)
		if (place->key != key && Overlaps(x, y, w, h, *place) && !InUse(place->key)) {
			Log(true, "%s takes over the place of %s (not loaded): its items go back to their characters at their next game load", key.c_str(), place->key.c_str());
			place = Places.erase(place);
		} else ++place;
	Place* saved = PlaceOf(key);
	if (HoldsOtherItems(x, y, w, h, key)) Log(false, "%s: %d,%d %dx%d holds items of another area or of none: they go back to their character at its next game load", key.c_str(), x, y, w, h);
	if (saved && (saved->x != x || saved->y != y || saved->width != w || saved->height != h))
		Log(true, "%s moves from %d,%d %dx%d to %d,%d %dx%d; items left in the old place go back to their characters if another area takes it", key.c_str(), saved->x, saved->y, saved->width,
		    saved->height, x, y, w, h);
	if (saved) *saved = { key, x, y, w, h };
	else Places.push_back({ key, x, y, w, h });
	SavePlaces();
	lease.placed = true;
	SetSlotArea(lease.slot, x, y, w, h);
	if (lease.flags & PageHost::AreaCharms) CharmSlots.fetch_or(1U << lease.slot);
	Log(false, "%s placed at %d,%d %dx%d (Area%d)", key.c_str(), x, y, w, h, lease.slot);
}

// At the first DataTablesLoaded every area asked for while plugins loaded is
// placed together on an empty page (places of plugins not loaded don't count:
// they are taken over if needed). First try: each area keeps its saved place
// (when its size is the same and it doesn't clash with another kept one), the
// others go first-fit, largest first. If that leaves one out: everything is
// packed again from scratch, largest first; items left in moved places go back
// to their characters at game load. What still doesn't fit gets NoRoom.
struct Spot {
	int x = -1, y = -1;
};

std::vector<Spot> TryPack(const std::vector<Lease*>& areas, const std::vector<std::size_t>& order, bool keepSaved, std::size_t& placed) {
	std::array<std::array<bool, PageSize>, PageSize> taken {};
	const auto free = [&](int x, int y, int w, int h) {
		if (x < 0 || y < 0 || x + w > PageSize || y + h > PageSize) return false;
		for (int cy = y; cy < y + h; ++cy)
			for (int cx = x; cx < x + w; ++cx)
				if (taken[cy][cx]) return false;
		return true;
	};
	const auto take = [&](int x, int y, int w, int h) {
		for (int cy = y; cy < y + h; ++cy)
			for (int cx = x; cx < x + w; ++cx) taken[cy][cx] = true;
	};
	std::vector<Spot> spots(areas.size());
	placed = 0;
	if (keepSaved)
		for (const std::size_t i : order) {
			const Place* saved = PlaceOf(areas[i]->key);
			if (saved && saved->width == areas[i]->width && saved->height == areas[i]->height && free(saved->x, saved->y, saved->width, saved->height)) {
				take(saved->x, saved->y, saved->width, saved->height);
				spots[i] = { saved->x, saved->y };
				++placed;
			}
		}
	for (const std::size_t i : order) {
		if (spots[i].x >= 0) continue;
		const int w = areas[i]->width, h = areas[i]->height;
		for (int ty = 0; ty + h <= PageSize && spots[i].x < 0; ++ty)
			for (int tx = 0; tx + w <= PageSize && spots[i].x < 0; ++tx)
				if (free(tx, ty, w, h)) {
					take(tx, ty, w, h);
					spots[i] = { tx, ty };
					++placed;
				}
	}
	return spots;
}

void PackWaiting() {
	std::vector<Lease*> areas;
	for (auto& lease : Leases)
		if (!lease.placed) areas.push_back(&lease);
	std::vector<std::size_t> order(areas.size());
	for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
	std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
		const int sa = areas[a]->width * areas[a]->height, sb = areas[b]->width * areas[b]->height;
		return sa != sb ? sa > sb : areas[a]->height > areas[b]->height;
	});
	std::size_t       kept = 0, fresh = 0;
	std::vector<Spot> spots = TryPack(areas, order, true, kept);
	if (kept < areas.size()) {
		std::vector<Spot> again = TryPack(areas, order, false, fresh);
		if (fresh > kept) {
			Log(true, "the saved places don't fit together with the new areas: every area is packed again (items left in old places go back to their characters at game load)");
			spots = std::move(again);
		}
	}
	std::vector<AreaHandle> refused;
	for (std::size_t i = 0; i < areas.size(); ++i)
		if (spots[i].x >= 0) ApplyPlace(*areas[i], spots[i].x, spots[i].y);
		else {
			Log(true, "%s: %dx%d doesn't fit on the page beside the other areas: refused (NoRoom)", areas[i]->key.c_str(), areas[i]->width, areas[i]->height);
			refused.push_back(areas[i]->handle);
		}
	for (const AreaHandle handle : refused) {
		Lease* lease = LeaseOf(handle);
		Refused.push_back(handle);
		Leases.erase(Leases.begin() + (lease - Leases.data()));
	}
	Packed = true;
}

auto __cdecl ReserveArea(const D2RL::PluginContext* caller, const PageHost::AreaRequest* request, PageHost::AreaInfo* info) noexcept -> Result {
	if (!caller || !caller->pluginId || !request || request->structSize < sizeof(PageHost::AreaRequest) || !request->name || !request->name[0]
	    || (info && info->structSize < sizeof(PageHost::AreaInfo)))
		return Result::InvalidArgument;
	const int w = static_cast<int>(request->width), h = static_cast<int>(request->height);
	if (w < 1 || h < 1 || w > PageSize || h > PageSize) return Result::InvalidArgument;
	if (Page == Inv::InvalidHandle) return Result::NoPage;
	std::lock_guard guard(Lock);
	const std::string client = caller->pluginId, key = client + "/" + request->name;
	if (key.find_first_of(" \t\r\n") != std::string::npos) return Result::InvalidArgument;
	if (InUse(key)) return Result::Busy;
	int slot = -1;
	const bool overlay = (request->flags & PageHost::AreaOverlay) != 0;
	for (int s = overlay ? OverlayFirst : 0; s < (overlay ? SlotCount : OverlayFirst) && slot < 0; ++s)
		if (Slots[s].panel != Panels::InvalidHandle && std::none_of(Leases.begin(), Leases.end(), [&](const Lease& l) { return l.slot == s; })) slot = s;
	if (slot < 0) return Result::NoPanel;
	Lease lease;
	lease.handle = NextHandle++;
	lease.client = client;
	lease.name   = request->name;
	lease.label  = request->label ? request->label : request->name;
	lease.key    = key;
	lease.slot   = slot;
	lease.flags  = request->flags;
	lease.width  = w;
	lease.height = h;
	const bool now = Packed;
	if (now && !PlaceLease(lease)) return Result::NoRoom;
	Leases.push_back(lease);
	EnsureBackground(w, h); // clients may use it in their own layout
	if ((request->flags & PageHost::AreaCharms) && !CharmSlotOk) Log(true, "%s asked for active charms, but the charm check could not be wrapped: charms there stay inactive", key.c_str());
	Log(false, "%s reserved %dx%d (Area%d, handle %llu)%s", key.c_str(), w, h, slot, static_cast<unsigned long long>(lease.handle), now ? "" : ": placed when every plugin has loaded");
	if (info) FillInfo(Leases.back(), info);
	return Result::Success;
}

auto __cdecl ReleaseArea(const D2RL::PluginContext* caller, AreaHandle handle) noexcept -> Result {
	std::lock_guard guard(Lock);
	Lease* lease = LeaseOf(handle);
	if (!lease) return Result::NotFound;
	if (!caller || !caller->pluginId || lease->client != caller->pluginId) return Result::InvalidArgument;
	const int slot = lease->slot;
	if (PanelOpen(slot)) (void)PanelApi->closePanel(Context, Slots[slot].panel);
	Log(false, "%s released (its place stays saved)", lease->key.c_str());
	Leases.erase(Leases.begin() + (lease - Leases.data()));
	SlotArea[slot].store(0);
	CharmSlots.fetch_and(~(1U << slot));
	return Result::Success;
}

auto __cdecl GetAreaInfo(AreaHandle handle, PageHost::AreaInfo* info) noexcept -> Result {
	if (!info || info->structSize < sizeof(PageHost::AreaInfo)) return Result::InvalidArgument;
	std::lock_guard guard(Lock);
	const Lease* lease = LeaseOf(handle);
	if (!lease) return std::find(Refused.begin(), Refused.end(), handle) != Refused.end() ? Result::NoRoom : Result::NotFound;
	FillInfo(*lease, info);
	return Result::Success;
}

auto __cdecl SetAreaLayout(AreaHandle handle, const PageHost::LayoutRequest* request) noexcept -> Result {
	if (!request || request->structSize < sizeof(PageHost::LayoutRequest)) return Result::InvalidArgument;
	std::lock_guard guard(Lock);
	Lease* lease = LeaseOf(handle);
	if (!lease) return Result::NotFound;
	if (request->layout) {
		const std::string_view layout(request->layout);
		if (layout.find("@GRID@") == std::string_view::npos || layout.find("@PANEL@") == std::string_view::npos) {
			Log(true, "%s: layout refused, it needs @PANEL@ and @GRID@", lease->key.c_str());
			return Result::InvalidArgument;
		}
	}
	lease->layout = request->layout ? request->layout : "";
	lease->gridX  = request->gridX;
	lease->gridY  = request->gridY;
	if ((request->flags & PageHost::LayoutApplyNow) && PanelOpen(lease->slot)) {
		if (PanelApi->closePanel(Context, Slots[lease->slot].panel) != Panels::Result::Success) return Result::Failed;
		return OpenSlot(lease->slot);
	}
	return Result::Success;
}

auto __cdecl OpenArea(AreaHandle handle) noexcept -> Result {
	std::lock_guard guard(Lock);
	const Lease* lease = LeaseOf(handle);
	if (!lease) return std::find(Refused.begin(), Refused.end(), handle) != Refused.end() ? Result::NoRoom : Result::NotFound;
	if (!lease->placed) return Result::Busy; // waits for every plugin to load
	return OpenSlot(lease->slot);
}

auto __cdecl CloseArea(AreaHandle handle) noexcept -> Result {
	std::lock_guard guard(Lock);
	const Lease* lease = LeaseOf(handle);
	if (!lease) return Result::NotFound;
	if (PanelOpen(lease->slot) && PanelApi->closePanel(Context, Slots[lease->slot].panel) != Panels::Result::Success) return Result::Failed;
	return Result::Success;
}

bool __cdecl IsAreaOpen(AreaHandle handle) noexcept {
	std::lock_guard guard(Lock);
	const Lease* lease = LeaseOf(handle);
	return lease && PanelOpen(lease->slot);
}

auto __cdecl SetItemPolicy(AreaHandle handle, PageHost::ItemPolicyFn policy, void* user) noexcept -> Result {
	std::lock_guard guard(Lock);
	Lease* lease = LeaseOf(handle);
	if (!lease) return Result::NotFound;
	lease->policy = policy;
	lease->user   = user;
	return Result::Success;
}

auto __cdecl MoveItem(AreaHandle handle, PageHost::MoveKind kind, std::int32_t x, std::int32_t y) noexcept -> Result {
	Area a {};
	{
		std::lock_guard guard(Lock);
		const Lease* lease = LeaseOf(handle);
		if (!lease) return Result::NotFound;
		a = AreaOfSlot(lease->slot);
	}
	if (kind != PageHost::MoveKind::CursorToArea && kind != PageHost::MoveKind::AreaToCursor) return Result::InvalidArgument;
	int w = 1, h = 1;
	if (kind == PageHost::MoveKind::CursorToArea && NativesOk)
		if (void* const held = CursorItem()) {
			std::uint8_t iw = 1, ih = 1;
			ItemSize(held, &iw, &ih, SourceName, __LINE__);
			w = iw, h = ih;
		}
	if (!Inside(a, a.x + x, a.y + y, w, h)) return Result::OutsideArea;
	const Inv::LocalPlayerPageMoveRequest move {
		.structSize = Inv::LocalPlayerPageMoveRequestSize,
		.kind       = kind == PageHost::MoveKind::CursorToArea ? Inv::LocalPlayerPageMoveKind::CursorToPage : Inv::LocalPlayerPageMoveKind::PageToCursor,
		.x          = static_cast<std::uint32_t>(a.x + x),
		.y          = static_cast<std::uint32_t>(a.y + y),
	};
	HostMoving        = true;
	const auto result = Inventory->executeLocalPlayerMove(Context, Page, &move);
	HostMoving        = false;
	ScanPage();
	if (result != Inv::Result::Success) {
		Log(true, "move %s %d,%d refused (inventory result %u)", kind == PageHost::MoveKind::CursorToArea ? "to" : "from", a.x + x, a.y + y, static_cast<unsigned>(result));
		return Result::Failed;
	}
	return Result::Success;
}

// A client's CustomPage destination -> the host's page cell; false when not
// Exact or off the area.
bool ToPage(const Area& a, D2RL::Items::ItemDestination& destination) {
	if (destination.container != D2RL::Items::ItemContainer::CustomPage) return true;
	if (destination.placement != D2RL::Items::Placement::Exact || !Inside(a, a.x + static_cast<int>(destination.x), a.y + static_cast<int>(destination.y))) return false;
	destination.customPageHandle = Page;
	destination.x += static_cast<std::uint32_t>(a.x);
	destination.y += static_cast<std::uint32_t>(a.y);
	return true;
}

bool AreaFor(AreaHandle handle, Area& a) {
	std::lock_guard guard(Lock);
	const Lease* lease = LeaseOf(handle);
	if (!lease) return false;
	a = AreaOfSlot(lease->slot);
	return true;
}

// Item and player handles belong to the plugin that got them, so none cross
// between client and host: the host uses its own local player and finds items
// by runtime id.
std::atomic<D2RL::PlayerHandle> HostPlayer {};

bool LocalPlayer(D2RL::PlayerHandle& player) {
	D2RL::PlayerHandle now {};
	if (Inventory->getLocalPlayer(Context, &now) == Inv::Result::Success && now) HostPlayer.store(now);
	player = HostPlayer.load();
	return player != D2RL::PlayerHandle {};
}

auto __cdecl FindItem(const D2RL::PluginContext*, const D2RL::Items::ItemInfo* item, void* user) noexcept -> Inv::IterationAction {
	auto* find = static_cast<Find*>(user);
	if (item && item->runtimeId == find->runtimeId) {
		find->handle = item->handle;
		return Inv::IterationAction::Stop;
	}
	return Inv::IterationAction::Continue;
}

auto __cdecl CreateItem(AreaHandle handle, const D2RL::Items::ItemCreateSpec* spec, std::uint32_t* runtimeId, std::uint32_t* itemResult) noexcept -> Result {
	if (itemResult) *itemResult = 0;
	if (runtimeId) *runtimeId = 0;
	if (!spec || spec->structSize < sizeof(D2RL::Items::ItemCreateSpec) || !ItemApi) return Result::InvalidArgument;
	Area a {};
	if (!AreaFor(handle, a)) return Result::NotFound;
	D2RL::Items::ItemCreateSpec copy = *spec;
	if (!ToPage(a, copy.destination)) return Result::OutsideArea;
	D2RL::PlayerHandle player {};
	if (!LocalPlayer(player)) return Result::Failed;
	D2RL::ItemHandle item {};
	HostMoving        = true;
	const auto result = ItemApi->createItem(Context, player, &copy, &item);
	HostMoving        = false;
	ScanPage();
	if (itemResult) *itemResult = static_cast<std::uint32_t>(result);
	if (result != D2RL::Items::Result::Success) return Result::Failed;
	D2RL::Items::ItemInfo info { .structSize = D2RL::Items::ItemInfoSize };
	if (runtimeId && ItemApi->getItemInfo(Context, item, &info) == D2RL::Items::Result::Success) *runtimeId = info.runtimeId;
	return Result::Success;
}

auto __cdecl MoveItems(AreaHandle handle, const D2RL::Items::ExistingItemTransaction* transaction, D2RL::Items::ExistingItemTransactionResult* out, std::uint32_t* itemResult) noexcept -> Result {
	if (itemResult) *itemResult = 0;
	if (!transaction || transaction->structSize < sizeof(D2RL::Items::ExistingItemTransaction) || (transaction->operationCount && !transaction->operations) || !ItemApi
	    || transaction->operationCount > 4096)
		return Result::InvalidArgument;
	Area a {};
	if (!AreaFor(handle, a)) return Result::NotFound;
	D2RL::PlayerHandle player {};
	if (!LocalPlayer(player)) return Result::Failed;
	std::vector<D2RL::Items::ExistingItemOperation> operations(transaction->operations, transaction->operations + transaction->operationCount);
	const D2RL::Inventory::ItemFilter everything { .structSize = sizeof(D2RL::Inventory::ItemFilter), .containerMask = ~0U };
	for (auto& operation : operations) {
		if (operation.kind == D2RL::Items::ExistingItemOperationKind::Move && !ToPage(a, operation.move.destination)) return Result::OutsideArea;
		Find find { static_cast<std::uint32_t>(operation.item), {} };
		(void)Inventory->forEachInventoryItem(Context, player, &everything, FindItem, &find);
		if (!find.handle) return Result::NotFound;
		operation.item = find.handle;
	}
	D2RL::Items::ExistingItemTransaction copy = *transaction;
	copy.player       = player;
	copy.operations   = operations.data();
	HostMoving        = true;
	const auto result = ItemApi->executeExistingItemTransaction(Context, &copy, out);
	HostMoving        = false;
	ScanPage();
	if (itemResult) *itemResult = static_cast<std::uint32_t>(result);
	return result == D2RL::Items::Result::Success ? Result::Success : Result::Failed;
}

auto __cdecl DestroyItem(AreaHandle handle, std::uint32_t runtimeId, D2RL::Items::SocketedItemPolicy socketed, std::uint32_t* itemResult) noexcept -> Result {
	if (itemResult) *itemResult = 0;
	if (!ItemApi) return Result::InvalidArgument;
	Area a {};
	if (!AreaFor(handle, a)) return Result::NotFound;
	D2RL::PlayerHandle player {};
	if (!LocalPlayer(player)) return Result::Failed;
	Find find { runtimeId, {} };
	const D2RL::Inventory::ItemFilter everything { .structSize = sizeof(D2RL::Inventory::ItemFilter), .containerMask = ~0U };
	(void)Inventory->forEachInventoryItem(Context, player, &everything, FindItem, &find);
	if (!find.handle) return Result::NotFound;
	// Only an item of this area.
	D2RL::Items::ItemInfo info { .structSize = D2RL::Items::ItemInfoSize };
	if (ItemApi->getItemInfo(Context, find.handle, &info) != D2RL::Items::Result::Success) return Result::Failed;
	if (info.container != D2RL::Items::ItemContainer::CustomPage || !Inside(a, info.x, info.y)) return Result::OutsideArea;
	const auto result = ItemApi->destroyItem(Context, player, find.handle, socketed);
	ScanPage();
	if (itemResult) *itemResult = static_cast<std::uint32_t>(result);
	return result == D2RL::Items::Result::Success ? Result::Success : Result::Failed;
}

const PageHost::Service Service {
	.serviceSize    = sizeof(PageHost::Service),
	.serviceVersion = PageHost::ServiceVersion,
	.reserveArea    = ReserveArea,
	.releaseArea    = ReleaseArea,
	.getAreaInfo    = GetAreaInfo,
	.setAreaLayout  = SetAreaLayout,
	.openArea       = OpenArea,
	.closeArea      = CloseArea,
	.isAreaOpen     = IsAreaOpen,
	.setItemPolicy  = SetItemPolicy,
	.moveItem       = MoveItem,
	.createItem     = CreateItem,
	.moveItems      = MoveItems,
	.destroyItem    = DestroyItem,
};

// ---- console ----

void Say(const D2RL::PluginContext* plugin, const std::string& text) {
	Log(false, "%s", text.c_str());
	if (plugin) plugin->WriteConsoleMessage(text.c_str());
}

auto Command(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
	const std::string args = command && command->args ? command->args : "";
	const auto*       plugin = command ? command->plugin : Context;
	std::lock_guard   guard(Lock);
	if (args == "all") {
		const auto opened = OpenSlot(AllSlot);
		Say(plugin, opened == Result::Success ? "pagehost: the whole page is open (no area limits there)" : "pagehost: could not open the whole page view, see the log");
		return D2RL::ConsoleCommandResult::Handled;
	}
	if (args.rfind("free ", 0) == 0) {
		const std::string key = args.substr(5);
		for (const auto& lease : Leases)
			if (lease.key == key) {
				Say(plugin, "pagehost: " + key + " is in use; remove its plugin first");
				return D2RL::ConsoleCommandResult::Handled;
			}
		const Place* place = PlaceOf(key);
		if (!place) {
			Say(plugin, "pagehost: no area " + key);
			return D2RL::ConsoleCommandResult::Handled;
		}
		ScanPage();
		if (const std::string holders = CharactersIn(*place); !holders.empty()) {
			Say(plugin, "pagehost: " + key + " still holds items on " + holders + "; take them out with 'pagehost all' first");
			return D2RL::ConsoleCommandResult::Handled;
		}
		Places.erase(Places.begin() + (place - Places.data()));
		SavePlaces();
		Say(plugin, "pagehost: " + key + " freed");
		return D2RL::ConsoleCommandResult::Handled;
	}
	Say(plugin, std::string("pagehost: page ") + (Page == Inv::InvalidHandle ? "NOT owned" : "owned, 16x16") + ", grid hooks " + (NextDraw ? "on" : "off") + ", " + std::to_string(Places.size())
	                + " area(s):");
	for (const auto& place : Places) {
		std::string state = "saved, plugin not loaded";
		for (const auto& lease : Leases)
			if (lease.key == place.key) state = "Area" + std::to_string(lease.slot) + (PanelOpen(lease.slot) ? ", open" : "") + (lease.layout.empty() ? ", default layout" : ", own layout");
		Say(plugin, "  " + place.key + " at " + std::to_string(place.x) + "," + std::to_string(place.y) + " " + std::to_string(place.width) + "x" + std::to_string(place.height) + " (" + state + ")");
	}
	for (const auto& [name, items] : PageItems) {
		int loose = 0;
		for (const auto& r : items)
			if (std::none_of(Places.begin(), Places.end(), [&](const Place& p) { return Within(r, p); })) ++loose;
		if (loose) Say(plugin, "  " + name + ": " + std::to_string(loose) + " item(s) outside every area (a new area avoids them, or gives them back at game load)");
	}
	Say(plugin, "pagehost all: open the whole page.  pagehost free <plugin>/<area>: free a saved place (it must be empty on every character).");
	return D2RL::ConsoleCommandResult::Handled;
}

// ---- load ----

template <typename Service>
const Service* Query(const char* name) {
	const Service* service = nullptr;
	if (Context->QueryService(&service) != D2RL::ServiceQueryResult::Success || !service) {
		Log(true, "%s service unavailable", name);
		return nullptr;
	}
	return service;
}

bool Locate() noexcept {
	for (const auto& native : Natives) {
		Code16 code {};
		if (!Read(Base + native.rva, code.data(), code.size()) || code != native.bytes) {
			Log(true, "game code differs at D2R+0x%llX", static_cast<unsigned long long>(native.rva));
			return false;
		}
	}
	InventoryOf = reinterpret_cast<InventoryFn>(Base + InventoryRva);
	FirstItem   = reinterpret_cast<ItemFn>(Base + FirstItemRva);
	NextItem    = reinterpret_cast<ItemFn>(Base + NextItemRva);
	NodeMode    = reinterpret_cast<NodeModeFn>(Base + NodeModeRva);
	CursorItem  = reinterpret_cast<CursorItemFn>(Base + CursorItemRva);
	GridOwner   = reinterpret_cast<GridOwnerFn>(Base + GridOwnerRva);
	PointToCell = reinterpret_cast<PointToCellFn>(Base + PointToCellRva);
	ItemSize    = reinterpret_cast<ItemSizeFn>(Base + ItemSizeRva);
	Code16        unit {};
	std::uint8_t  index[7] {};
	if (Read(Base + PlayerUnitRva, unit.data(), unit.size()) && unit == PlayerUnitBytes && Read(Base + LocalIndexRva, index, sizeof(index)) && index[0] == 0x8B && index[1] == 0x05
	    && index[6] == 0xC3) {
		LocalIndex = reinterpret_cast<LocalIndexFn>(Base + LocalIndexRva);
		PlayerUnit = reinterpret_cast<PlayerUnitFn>(Base + PlayerUnitRva);
	} else Log(true, "the local player lookup differs: items left on the page aren't recorded, so a new area may land on them");
	if (Code16 sound {}; Read(Base + ItemSoundRva, sound.data(), sound.size()) && sound == ItemSoundBytes) {
		ItemSound   = reinterpret_cast<ItemSoundFn>(Base + ItemSoundRva);
		PlaySoundAt = reinterpret_cast<PlaySoundFn>(Base + PlaySoundRva);
	} else Log(true, "the item sound lookup differs: Ctrl+click into an area is silent");
	return true;
}

void __cdecl AfterAllLoaded(const D2RL::PluginContext*, const D2RL::Lifecycle::DataTablesLoadedEvent*, void*) noexcept {
	static bool done = false;
	if (done) return;
	done = true;
	// Every plugin has loaded: wrap the grid on top of whatever is there.
	if (NativesOk && !PatchVtable()) Log(true, "the grid widget could not be wrapped: area panels show the whole page and don't limit drops");
	if (CharmSlotOk && !WrapCharmSlot()) {
		CharmSlotOk = false;
		Log(true, "the charm check could not be wrapped: every charm on the page gives its bonuses");
	}
	{
		std::lock_guard guard(Lock);
		PackWaiting();
	}
	if (CommApi) {
		const std::uint32_t                 payload = 1;
		const Comm::PublishEventRequest ready {
			.structSize   = Comm::PublishEventRequestSize,
			.name         = PageHost::ReadyEvent,
			.eventVersion = 1,
			.dataSize     = sizeof(payload),
			.data         = &payload,
		};
		(void)CommApi->publishEvent(Context, &ready);
	}
	std::lock_guard guard(Lock);
	Log(false, "all plugins loaded: %zu area(s) leased", Leases.size());
}

void RegisterPanels() {
	for (int slot = 0; slot <= SlotCount; ++slot) {
		auto& s = Slots[slot];
		s.id    = slot == AllSlot ? "All" : "Area" + std::to_string(slot);
		if (!SyncLayout(slot)) continue;
		const Panels::PanelRegistration registration {
			.structSize = Panels::PanelRegistrationSize,
			.flags      = slot >= OverlayFirst && slot != AllSlot ? Panels::PanelFlags::None : Panels::PanelFlags::CloseOnEscape | Panels::PanelFlags::GameplayLeftSlot,
			.localId    = s.id.c_str(),
		};
		if (PanelApi->registerPanel(Context, &registration, &s.panel) != Panels::Result::Success) {
			Log(true, "could not register panel %s", s.id.c_str());
			s.panel = Panels::InvalidHandle;
			continue;
		}
		const Panels::PlayerPageGridBinding binding {
			.structSize = Panels::PlayerPageGridBindingSize,
			.panel      = s.panel,
			.page       = Page,
			.childName  = "aaa-player-page-host/Grid",
		};
		if (const auto bound = PanelApi->bindPlayerPageGrid(Context, &binding); bound != Panels::Result::Success) {
			Log(true, "could not bind the grid of %s (result %u)", s.id.c_str(), static_cast<unsigned>(bound));
			(void)PanelApi->unregisterPanel(Context, s.panel);
			s.panel = Panels::InvalidHandle;
		}
	}
}

bool Load() {
	Inventory   = Query<D2RL::InventoryService>("inventory");
	PanelApi    = Query<D2RL::PanelService>("panel");
	ResourceApi = Query<D2RL::ResourceService>("resource");
	WidgetApi   = Query<D2RL::WidgetService>("widget");
	if (!Inventory || !PanelApi || !ResourceApi || !D2RL::HasPanelServiceField(PanelApi, D2RL::PanelServiceRegisterChildLayoutFieldEnd)) {
		Log(true, "Player Page Host %s: inactive, this D2RLoader lacks a needed service", PluginVersion);
		return true;
	}
	if (!D2RL::HasWidgetServiceField(WidgetApi, D2RL::WidgetServiceRequiredSize)) WidgetApi = nullptr;
	if (Context->QueryService(&ItemApi) != D2RL::ServiceQueryResult::Success || !ItemApi
	    || ItemApi->serviceSize < offsetof(D2RL::ItemService, executeExistingItemTransaction) + sizeof(void*))
		ItemApi = nullptr;
	Base      = Context->exeBase;
	NativesOk = Base && Locate();
	if (!NativesOk) Log(true, "this game build isn't the one the area limits were made for (Steam 3.3.93787 / 94995): area panels will show the whole page");

	if (Context->pluginConfigPath) {
		PlacesPath       = Context->pluginConfigPath;
		const auto slash = PlacesPath.find_last_of(L"\\/");
		const std::wstring folder = slash == std::wstring::npos ? std::wstring() : PlacesPath.substr(0, slash + 1);
		PlacesPath       = folder + L"aaa-player-page-host-areas.txt";
		ItemsPath        = folder + L"aaa-player-page-host-items.txt";
		LoadPlaces();
		LoadItems();
	}

	// The page.
	const Inv::PlayerPageRegistration page { .structSize = Inv::PlayerPageRegistrationSize, .logicalId = LogicalPageId };
	auto result = Inventory->registerPlayerPage(Context, &page, &Page);
	if (result != Inv::Result::Success) {
		Page = Inv::InvalidHandle;
		Log(true, "Player Page Host %s: could not get the custom page (result %u); another plugin loaded first has it. Clients get NoPage.", PluginVersion, static_cast<unsigned>(result));
	} else {
		const Inv::PlayerPageConfiguration size { .structSize = Inv::PlayerPageConfigurationSize, .width = PageSize, .height = PageSize };
		if ((result = Inventory->configurePlayerPage(Context, Page, &size)) != Inv::Result::Success) Log(true, "page size 16x16 refused (result %u)", static_cast<unsigned>(result));
		// Charms on the page count; the wrapped charm check keeps that to charm areas.
		// Without the wrapper the default stays: no page charm counts.
		CharmSlotOk = NativesOk && CheckCharmSlot();
		if (CharmSlotOk) {
			const Inv::PlayerPageCharmPolicyConfiguration charms { .structSize = Inv::PlayerPageCharmPolicyConfigurationSize, .page = Page, .policy = Inv::PlayerPageCharmPolicy::StockInventoryAndPlayerPage };
			if ((result = Inventory->configurePlayerPageCharmPolicy(Context, &charms)) != Inv::Result::Success) {
				CharmSlotOk = false;
				Log(true, "the page's charm policy was refused (result %u): charm areas stay inactive", static_cast<unsigned>(result));
			}
		} else Log(true, "the game's charm check isn't where expected: charm areas stay inactive");
		const Inv::PlayerPageItemPolicy policy { .structSize = Inv::PlayerPageItemPolicySize, .page = Page, .callback = OnPagePolicy };
		if ((result = Inventory->configurePlayerPageItemPolicy(Context, &policy)) != Inv::Result::Success)
			Log(true, "could not check items before they land on the page (result %u)", static_cast<unsigned>(result));
		std::lock_guard guard(Lock);
		RegisterPanels();
	}

	// The service, then the post-load step.
	if (const D2RL::PluginCommunicationService* comm = nullptr; Context->QueryService(&comm) == D2RL::ServiceQueryResult::Success && comm
	                                                            && D2RL::HasPluginCommunicationServiceField(comm, D2RL::PluginCommunicationServiceRequiredSize)) {
		CommApi = comm;
		const Comm::PublishServiceRequest request {
			.structSize     = Comm::PublishServiceRequestSize,
			.name           = PageHost::ServiceName,
			.serviceVersion = PageHost::ServiceVersion,
			.tableSize      = sizeof(Service),
			.table          = &Service,
		};
		if (const auto published = CommApi->publishService(Context, &request); published != Comm::Result::Success)
			Log(true, "could not publish the %s service (result %u): no plugin can get an area", PageHost::ServiceName, static_cast<unsigned>(published));
	} else Log(true, "no plugin communication service: no plugin can get an area");

	if (const auto* lifecycle = Query<D2RL::LifecycleService>("lifecycle")) {
		const D2RL::Lifecycle::DataTablesLoadedListener listener { .structSize = D2RL::Lifecycle::DataTablesLoadedListenerSize, .callback = AfterAllLoaded };
		D2RL::Lifecycle::ListenerHandle handle {};
		if (lifecycle->registerDataTablesLoadedListener(Context, &listener, &handle) != D2RL::Lifecycle::Result::Success) Log(true, "could not follow the data table loads: area limits stay off");
		const D2RL::Lifecycle::GameplayEventListener ready {
			.structSize = D2RL::Lifecycle::GameplayEventListenerSize,
			.kind       = D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady,
			.callback   = [](const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept { ScanPage(true); },
		};
		if (lifecycle->registerGameplayEventListener(Context, &ready, &handle) != D2RL::Lifecycle::Result::Success) Log(true, "could not follow character loads: page items are recorded when an area shows");
	}
	// Ctrl+click into the page.
	if (const D2RL::ItemInteractionService* clicks = nullptr; Page != Inv::InvalidHandle && ItemApi && NativesOk && PlayerUnit
	                                                          && Context->QueryService(&clicks) == D2RL::ServiceQueryResult::Success && clicks
	                                                          && Context->QueryService(&ThreadApi) == D2RL::ServiceQueryResult::Success && ThreadApi) {
		const Clicks::ItemInteractionListener listener { .structSize = Clicks::ItemInteractionListenerSize, .priority = -1000, .callback = OnItemClick };
		Clicks::ListenerHandle handle {};
		ClicksOn = clicks->registerListener(Context, &listener, &handle) == Clicks::Result::Success;
	}
	if (!ClicksOn) Log(true, "no item click listener: Ctrl+click into an area is refused");
	(void)Context->RegisterConsoleCommand("pagehost", Command, "Player Page Host: list the areas. 'pagehost all' opens the whole page, 'pagehost free <plugin>/<area>' frees an empty saved place.");
	Log(false, "Player Page Host %s: %s, %d area panels + %d overlay panels", PluginVersion, Page == Inv::InvalidHandle ? "WITHOUT the page" : "page 16x16 owned", OverlayFirst,
	    SlotCount - OverlayFirst);
	return true;
}

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (!D2RL::HasContext(context) || !context->GetApi()) return false;
	Context = context;
	(void)Context->EnsureConfig();
	std::uint32_t required = 0;
	char          config[2048] {};
	if (Context->ReadConfig(config, sizeof(config), &required) && std::strstr(config, "enabled = false")) {
		Log(false, "Player Page Host %s: disabled in config", PluginVersion);
		return true;
	}
	return Load();
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	UnwrapCharmSlot();
	RestoreVtable();
}
