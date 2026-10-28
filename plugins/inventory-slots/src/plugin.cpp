// Inventory Slots: four slots outside the inventory grid for the Horadric
// Cube, the two tomes and keys.
//
// The slots are real inventory cells (page 0), so everything the game does
// with inventory items keeps working: tomes are used from there, keys open
// chests, the cube opens and Cube Overflow / Cube Fill see it. D2RLoader 1.3.1
// has a single custom player page and Charm Inventory owns it, so the plugin
// does not use one.
//
// How (Steam 3.3.93787 / 94995 RVAs):
//   - Two rows are added under each class's inventory grid (inventory.txt
//     rows 0-4, 12-14 and their "2" twins, gridY + 2) when the data tables
//     load: the bank array Base+0x2A9A580 (16 bytes per bank), its inventory
//     rows at bank+0x1588 {rows, count}, 0xF0 bytes per row, gridX/gridY at
//     +0x10/+0x11. Saved items there load back like any inventory item.
//   - The slots are cells of those rows: cube (0, H) 2x2, Town Portal (2, H)
//     1x2, Identify (3, H) 1x2, keys (4, H) 1x1, H = the class's own height.
//     The other cells of the rows are never used.
//   - The game's free-spot searches (0x38D8F0, 0x38DBB0, 0x38DCC0, called only
//     by FindSpot 0x3865B0) see the grid without those rows: loot, purchases
//     and moves never land there, except a cube, tome or key whose slot is
//     empty, which gets its slot. The grid is recognised by its size, the
//     item by FindSpot's size query just before (call 0x371850 at 0x3866CA,
//     sent through a relay in the int3 gap at 0x380FC1).
//   - Four InventoryGridWidgets (a child layout of the inventory panel, page 0)
//     show one slot each. They share the stock grid widget's vtable
//     (Base+0x1CF3800), whose entries are wrapped:
//       vt3  Draw: items that are not this widget's are hidden for the call
//            (item page byte, ItemData+0x55, set to 0xFE then restored). The
//            stock grid hides the two rows the same way.
//       vt16 item rect, vt31 element rect: a slot's cell -> the widget's own
//            cell (0, 0).
//       vt18 valid element, vt19 element under the mouse: a slot widget
//            answers its slot cell, and only takes its own item.
//     Everything else (pick up, drop, use, tooltips) gets real inventory
//     coordinates and runs unchanged. Slot widgets are recognised by a
//     marker in their chronicleFlourishOffset field (+0x634/+0x638 floats).
//   - Behind each slot: the paperdoll slot sprite of its size and the item's
//     art in gray, hidden through the widget service while the slot is filled.

#include <D2RLPlugin/api.h>
#include <windows.h>

#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace Widgets = D2RL::Widgets;

constexpr const char* PluginId      = "inventory-slots";
constexpr const char* PluginVersion = "1.0.1";

const D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = PluginId,
	.name        = "Inventory Slots",
	.version     = PluginVersion,
	.author      = "wild-healer",
	.description = "Slots outside the inventory grid for the Horadric Cube, the tomes and keys.",
	.flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

constexpr std::uint32_t Code(const char* text) noexcept {
	std::uint32_t code = 0;
	for (int i = 0; i < 4; ++i) code |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(text[i] ? text[i] : ' ')) << (i * 8);
	return code;
}

struct SlotDef {
	const char*   key;        // config prefix
	std::uint32_t code;       // the one item it takes
	int           column;     // first cell, in the added rows
	int           width, height;
	const char*   art;        // item art, shown gray while the slot is empty
	const char*   background; // the paperdoll slot of that size
	float         scale;      // of the background
};
// No paperdoll slot is 1x2: the tomes use the 2x4 weapon slot at half size.
constexpr std::array<SlotDef, 4> Slots { {
	{ "cube", Code("box "), 0, 2, 2, "items/misc/quest/horadric_cube", "PANEL\\\\Inventory\\\\Inventory_Paperdoll_Glove", 1.0f },
	{ "tp_tome", Code("tbk "), 2, 1, 2, "items/misc/book/town_portal_book", "PANEL\\\\Inventory\\\\Inventory_Paperdoll_Weapon", 0.5f },
	{ "id_tome", Code("ibk "), 3, 1, 2, "items/misc/book/identify_book", "PANEL\\\\Inventory\\\\Inventory_Paperdoll_Weapon", 0.5f },
	{ "keys", Code("key "), 4, 1, 1, "items/misc/key/skeleton_key", "PANEL\\\\Inventory\\\\Inventory_Paperdoll_Ring", 1.0f },
} };
constexpr const char* RootName      = "inventory-slots/Slots";
constexpr const char* StockPanels[] { "PlayerInventoryExpansionLayout", "PlayerInventoryOriginalLayout" };
constexpr int AddedRows = 2;
constexpr int MinWidth  = 5; // the slots use columns 0-4
constexpr int Cell      = 98;
constexpr float Marker  = 7000.0f; // chronicleFlourishOffset {Marker + slot, Marker}

struct Settings {
	bool                           enabled    = true;
	bool                           showFrames = true;
	bool                           showArt    = true;
	std::array<std::array<int, 2>, 4> position { { { 1400, 955 }, { 1400, 1161 }, { 1498, 1161 }, { 1449, 1367 } } };
} Config;

// ---- native layout (Steam 3.3.93787 / 94995) ----

using Code16 = std::array<std::uint8_t, 16>;
struct Native {
	std::uint64_t rva;
	Code16        bytes;
};
// Functions the plugin calls or hooks, checked at load.
constexpr std::uint64_t ClientPlayerIdRva = 0x8B2D0, ClientPlayerRva = 0x9A480, ClientFlagRva = 0x8D3B0, PageGridRva = 0x34A410, InventoryRva = 0x34A360,
                        FirstItemRva = 0x388C10, NextItemRva = 0x38ABA0, NodeModeRva = 0x38A930, ItemCodeRva = 0x36EF50, CursorItemRva = 0x15F100,
                        GridOwnerRva = 0x2A7810, PointToCellRva = 0x2C6300, SearchARva = 0x38D8F0, SearchBRva = 0x38DBB0, SearchCRva = 0x38DCC0,
                        BankOfRva = 0x300A90, GridInfoRva = 0x3C6D80, ItemSizeRva = 0x371850;
constexpr std::array<Native, 18> Natives { {
	{ ClientPlayerIdRva, { 0x8B, 0x05, 0x2E, 0x84, 0x99, 0x02, 0xC3, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC } },
	{ ClientPlayerRva, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x83, 0xF9, 0x08, 0x0F, 0x83, 0x85 } },
	{ ClientFlagRva, { 0x48, 0x8B, 0x05, 0x29, 0x63, 0x99, 0x02, 0x48, 0x85, 0xC0, 0x74, 0x08, 0x80, 0x78, 0x5C, 0x01 } },
	{ PageGridRva, { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57 } },
	{ InventoryRva, { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF1, 0x48, 0x85, 0xC9 } },
	{ FirstItemRva, { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x74, 0x2E, 0x81, 0x39 } },
	{ NextItemRva, { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x75, 0x10, 0x88, 0x4C } },
	{ NodeModeRva, { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x75, 0x10, 0x88, 0x4C } },
	{ ItemCodeRva, { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF9, 0x48, 0x85, 0xC9 } },
	{ CursorItemRva, { 0x48, 0x83, 0xEC, 0x28, 0xE8, 0xC7, 0xC1, 0xF2, 0xFF, 0x8B, 0xC8, 0xE8, 0x70, 0xB3, 0xF3, 0xFF } },
	{ GridOwnerRva, { 0x48, 0x83, 0xEC, 0x28, 0x44, 0x8B, 0x81, 0xC4, 0x05, 0x00, 0x00, 0x41, 0x83, 0xF8, 0xFF, 0x75 } },
	{ PointToCellRva, { 0x48, 0x8B, 0xC4, 0x4C, 0x89, 0x40, 0x18, 0x53, 0x55, 0x56, 0x57, 0x41, 0x56, 0x48, 0x81, 0xEC } },
	{ SearchARva, { 0x48, 0x8B, 0xC4, 0x4C, 0x89, 0x48, 0x20, 0x4C, 0x89, 0x40, 0x18, 0x48, 0x89, 0x50, 0x10, 0x41 } },
	{ SearchBRva, { 0x48, 0x89, 0x5C, 0x24, 0x20, 0x4C, 0x89, 0x44, 0x24, 0x18, 0x48, 0x89, 0x54, 0x24, 0x10, 0x48 } },
	{ SearchCRva, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x4C, 0x89, 0x44, 0x24, 0x18, 0x48, 0x89, 0x54, 0x24, 0x10, 0x55 } },
	{ BankOfRva, { 0x48, 0x83, 0xEC, 0x28, 0x0F, 0xB6, 0xC1, 0x48, 0x89, 0x44, 0x24, 0x38, 0x48, 0x83, 0xF8, 0x04 } },
	{ GridInfoRva, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57 } },
	{ ItemSizeRva, { 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x20 } }, // called, never hooked
} };
// FindSpot's item size query, redirected through an int3 gap in the game
// (jmp [rip] to the plugin) so the plugin learns the item. Item size itself
// is not hooked: Item Quality Backgrounds checks its bytes.
constexpr std::uint64_t SizeCallRva = 0x3866CA; // call 0x371850
constexpr std::array<std::uint8_t, 5> SizeCall { 0xE8, 0x81, 0xB1, 0xFE, 0xFF };
constexpr std::uint64_t RelayRva  = 0x380FC1; // 15 bytes of int3 between two functions
constexpr std::size_t   RelaySize = 14;
// Data locations, checked through the instructions that use them.
constexpr std::uint64_t BankArrayLeaRva = 0x300AC0; // lea rcx, [rip + banks]
constexpr std::uint64_t BankArrayRva    = 0x2A9A580;
constexpr std::uint64_t InventoryRowsLeaRva = 0x3C6DA4; // lea rsi, [rax + 0x1588]
constexpr std::array<std::uint8_t, 7> InventoryRowsLea { 0x48, 0x8D, 0xB0, 0x88, 0x15, 0x00, 0x00 };
constexpr std::size_t   BankCount = 4, BankStride = 16, InventoryRows = 0x1588, RowSize = 0xF0, RowGridX = 0x10, RowGridY = 0x11;
constexpr int           RowsPerSet = 15; // the "2" twin rows follow 15 rows later
constexpr std::array<int, 8> PlayerRows { 0, 1, 2, 3, 4, 12, 13, 14 };
constexpr int           RecordCount = 30;
// Grid widget vtable and the entries the plugin wraps.
constexpr std::uint64_t GridVtableRva = 0x1CF3800;
constexpr int           VtDraw = 3, VtItemRect = 16, VtValidCell = 18, VtCellAtPoint = 19, VtItemAtCell = 25, VtCellRect = 31, VtUseCell = 33;
constexpr std::array<std::pair<int, std::uint64_t>, 7> VtableEntries { {
	{ VtDraw, 0x2C3570 },
	{ VtItemRect, 0x2C47B0 },
	{ VtValidCell, 0x2C6290 },
	{ VtCellAtPoint, 0x2C3FF0 },
	{ VtItemAtCell, 0x2C49F0 }, // not wrapped, checked
	{ VtUseCell, 0x2C3310 },    // not wrapped, checked
	{ VtCellRect, 0x2C4B80 },
} };
// Object layouts.
constexpr std::size_t WidgetPage = 0x630, WidgetCellCount = 0x628, WidgetMarker = 0x634;
// The unit a grid shows (id, unit type, and a unit pointer next to them). The
// inventory panel sets them on its own "grid" child only (0x2431D0 calls
// SetUnit 0x2AAB40), so the slots copy them from it.
// What a right click on an item may do there (0 by default; the inventory
// panel sets it on its grid): checked by the right click use 0x161F20, reached
// through vt33 0x2C3310 on the mouse up message.
constexpr std::size_t WidgetUseFlags = 0x5C0;
constexpr std::size_t WidgetUnitId = 0x5C4, WidgetUnitType = 0x5C8, WidgetUnitPointer = 0x5D0;
constexpr std::size_t UnitData = 0x10, UnitPath = 0x38, PathX = 0x10, PathY = 0x14, DataPage = 0x55;
constexpr std::size_t GridWidth = 0x10, GridHeight = 0x11, GridCells = 0x18; // cells: item pointers, y * width + x
constexpr std::uint8_t PageInventory = 0, PageHidden = 0xFE;
constexpr std::uint32_t UnitItem = 4; // unit type, the first field of a unit
constexpr int          NodeInGrid = 1;
constexpr const char*  SourceName = "inventory-slots";

using ClientPlayerIdFn = std::uint32_t (*)() noexcept;
using ClientPlayerFn   = void* (*)(std::uint32_t id) noexcept;
using ClientFlagFn     = bool (*)() noexcept;
using PageGridFn       = int (*)(void* player, std::uint8_t page, int flag) noexcept;
using InventoryFn      = void* (*)(void* unit, const char* file, int line) noexcept;
using ItemFn           = std::uint8_t* (*)(void* inventoryOrItem) noexcept;
using NodeModeFn       = int (*)(void* item) noexcept;
using ItemCodeFn       = std::uint32_t (*)(void* item) noexcept;
using CursorItemFn     = void* (*)() noexcept;
using GridOwnerFn      = void* (*)(void* widget) noexcept;
using CellFn           = std::uint64_t* (*)(void* widget, std::uint64_t* out, std::uint64_t point) noexcept;
using ItemSizeFn       = void (*)(void* item, std::uint8_t* width, std::uint8_t* height, const char* file, int line) noexcept;
using SearchAFn        = int (*)(void* inventory, std::uint8_t* grid, void* x, void* y, std::uint8_t w, std::uint8_t h) noexcept;
using SearchBFn        = int (*)(std::uint8_t* grid, void* x, void* y, std::uint8_t w, std::uint8_t h) noexcept;
using DrawFn           = void (*)(void* widget, void* a, void* b, void* c) noexcept;
using ItemRectFn       = std::int32_t* (*)(void* widget, std::int32_t* out, std::uint64_t cell) noexcept;
using CellRectFn       = std::int32_t* (*)(void* widget, std::int32_t* out, std::uint64_t size, std::uint64_t cell) noexcept;
using ValidCellFn      = bool (*)(void* widget, std::uint64_t cell) noexcept;

std::uintptr_t Base {};
const D2RL::PluginContext* Context {};
const D2RL::ThreadService* Threads {};
const D2RL::WidgetService* WidgetApi {};

ClientPlayerIdFn ClientPlayerId {};
ClientPlayerFn   ClientPlayer {};
ClientFlagFn     ClientFlag {};
PageGridFn       PageGrid {};
InventoryFn      InventoryOf {};
ItemFn           FirstItem {};
ItemFn           NextItem {};
NodeModeFn       NodeMode {};
ItemCodeFn       ItemCode {};
CursorItemFn     CursorItem {};
GridOwnerFn      GridOwner {};
// Originals.
ItemSizeFn  ItemSize {};
SearchAFn   SearchA {};
SearchBFn   SearchB {};
SearchBFn   SearchC {};
DrawFn      Draw {};
ItemRectFn  ItemRect {};
CellRectFn  CellRect {};
ValidCellFn ValidCell {};
CellFn      CellAtPoint {};
void**      Vtable {};

// Each class's grid height before the two rows, by inventory.txt record
// (-1: unknown), and the grid sizes (width | height << 8) after.
std::array<std::atomic<int>, RecordCount> OriginalHeight {};
std::array<std::atomic<std::uint16_t>, 16> GrownSizes {};
// Rows already grown: their address and new height, so a second table event
// for the same tables does not add two more.
struct GrownRow {
	std::uint8_t* row;
	std::uint8_t  height;
};
std::vector<GrownRow> GrownRows;

void Log(bool warn, const char* format, ...) noexcept {
	if (!Context) return;
	char    line[512] {};
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

template <typename T>
T At(std::uint64_t rva) noexcept {
	return reinterpret_cast<T>(Base + rva);
}

constexpr std::uint64_t Pack(int x, int y) noexcept {
	return static_cast<std::uint32_t>(x) | static_cast<std::uint64_t>(static_cast<std::uint32_t>(y)) << 32;
}
constexpr int CellX(std::uint64_t cell) noexcept {
	return static_cast<std::int32_t>(cell & 0xFFFFFFFF);
}
constexpr int CellY(std::uint64_t cell) noexcept {
	return static_cast<std::int32_t>(cell >> 32);
}
constexpr std::uint64_t NoCell = ~std::uint64_t { 0 };

// ---- grid rows (game thread, when the tables load) ----

void NoteGrownSize(std::uint8_t width, std::uint8_t height) noexcept {
	const std::uint16_t size = static_cast<std::uint16_t>(width | height << 8);
	for (auto& slot : GrownSizes) {
		std::uint16_t value = slot.load();
		if (value == size) return;
		if (value == 0 && slot.compare_exchange_strong(value, size)) return;
		if (value == size) return;
	}
}

bool IsGrownSize(const std::uint8_t* grid) noexcept {
	const std::uint16_t size = static_cast<std::uint16_t>(grid[GridWidth] | grid[GridHeight] << 8);
	for (const auto& slot : GrownSizes) {
		const std::uint16_t value = slot.load(std::memory_order_relaxed);
		if (value == 0) return false;
		if (value == size) return true;
	}
	return false;
}

void GrowGrids() noexcept {
	int grown = 0, kept = 0, skipped = 0;
	for (std::size_t bank = 0; bank < BankCount; ++bank) {
		std::uint8_t* tables = *reinterpret_cast<std::uint8_t**>(Base + BankArrayRva + bank * BankStride);
		if (!tables) continue;
		std::uint8_t* const rows  = *reinterpret_cast<std::uint8_t**>(tables + InventoryRows);
		const std::uint64_t count = *reinterpret_cast<std::uint64_t*>(tables + InventoryRows + 8);
		if (!rows || count < static_cast<std::uint64_t>(RecordCount)) {
			if (rows) Log(true, "table bank %zu: %llu inventory rows, expected %d or more; left alone", bank, static_cast<unsigned long long>(count), RecordCount);
			continue;
		}
		for (int set = 0; set < 2; ++set)
			for (const int player : PlayerRows) {
				const int     record = player + set * RowsPerSet;
				std::uint8_t* row    = rows + record * RowSize;
				const std::uint8_t width = row[RowGridX], height = row[RowGridY];
				bool done = false;
				for (const auto& known : GrownRows)
					if (known.row == row && known.height == height) done = true;
				if (done) {
					++kept;
					continue;
				}
				if (width < MinWidth || height == 0 || height + AddedRows > 16) {
					++skipped;
					Log(true, "table bank %zu row %d: grid %ux%u can't take the slot rows", bank, record, width, height);
					continue;
				}
				DWORD protect = 0;
				const bool writable = VirtualProtect(row + RowGridY, 1, PAGE_READWRITE, &protect);
				row[RowGridY] = static_cast<std::uint8_t>(height + AddedRows);
				if (writable) VirtualProtect(row + RowGridY, 1, protect, &protect);
				GrownRows.push_back({ row, row[RowGridY] });
				OriginalHeight[record].store(height);
				NoteGrownSize(width, row[RowGridY]);
				++grown;
			}
	}
	Log(false, "inventory grids: %d class rows grown by %d rows, %d already done, %d skipped", grown, AddedRows, kept, skipped);
}

bool ReadPanelOffset() noexcept;
bool RegisterLayout() noexcept;

void __cdecl OnTablesLoaded(const D2RL::PluginContext*, const D2RL::Lifecycle::DataTablesLoadedEvent*, void*) noexcept {
	GrowGrids();
	if (ReadPanelOffset() && !RegisterLayout()) Log(true, "slot placement: the moved slot layout could not be registered; slots are invisible");
}

// ---- free-spot searches (server and client threads) ----

// While a search runs, a grown grid looks as tall as before.
struct HideAddedRows {
	std::uint8_t* grid {};
	std::uint8_t  height {};
	explicit HideAddedRows(std::uint8_t* target) noexcept {
		if (!target || !IsGrownSize(target)) return;
		grid   = target;
		height = grid[GridHeight];
		grid[GridHeight] = static_cast<std::uint8_t>(height - AddedRows);
		static std::atomic<bool> logged {};
		if (!logged.exchange(true)) Log(false, "free-spot search: added rows hidden (grid %ux%u)", grid[GridWidth], height);
	}
	~HideAddedRows() {
		if (grid) grid[GridHeight] = height;
	}
};

// The item a free-spot search is for. FindSpot asks the item's size
// (0x371850) just before it searches, on the same thread, and the searches
// themselves only get the size.
thread_local void* SizedItem {};

void FindSpotItemSize(void* item, std::uint8_t* width, std::uint8_t* height, const char* file, int line) noexcept {
	SizedItem = item;
	ItemSize(item, width, height, file, line);
}

bool RedirectSizeCall() noexcept {
	std::array<std::uint8_t, RelaySize> gap {}, relay { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
	gap.fill(0xCC);
	const auto target = reinterpret_cast<std::uint64_t>(&FindSpotItemSize);
	std::memcpy(relay.data() + 6, &target, sizeof(target));
	return Context->PatchBytes(RelayRva, gap.data(), gap.size(), relay.data(), relay.size()) &&
	       Context->PatchCallRel32(SizeCallRva, SizeCall.data(), SizeCall.size(), RelayRva);
}

// A cube, tome or key looking for room goes to its own slot when that slot is
// empty: purchases, pickups, quest rewards and Ctrl+click moves alike. The
// client and the server search the same way, so both agree on the spot.
bool SlotSpot(std::uint8_t* grid, void* x, void* y, std::uint8_t w, std::uint8_t h) noexcept {
	auto* const item = static_cast<std::uint8_t*>(SizedItem);
	SizedItem        = nullptr;
	if (!grid || !item || !IsGrownSize(grid) || *reinterpret_cast<const std::uint32_t*>(item) != UnitItem) return false;
	const std::uint32_t code = ItemCode(item);
	for (const auto& def : Slots) {
		if (def.code != code || def.width != w || def.height != h) continue;
		const int width = grid[GridWidth], top = grid[GridHeight] - AddedRows;
		auto* const* cells = *reinterpret_cast<void* const* const*>(grid + GridCells);
		if (!cells) return false;
		for (int row = top; row < top + def.height; ++row)
			for (int column = def.column; column < def.column + def.width; ++column)
				if (cells[row * width + column]) return false;
		*static_cast<std::int32_t*>(x) = def.column;
		*static_cast<std::int32_t*>(y) = top;
		return true;
	}
	return false;
}

int HookSearchA(void* inventory, std::uint8_t* grid, void* x, void* y, std::uint8_t w, std::uint8_t h) noexcept {
	if (SlotSpot(grid, x, y, w, h)) return 1;
	HideAddedRows hide(grid);
	return SearchA(inventory, grid, x, y, w, h);
}
int HookSearchB(std::uint8_t* grid, void* x, void* y, std::uint8_t w, std::uint8_t h) noexcept {
	if (SlotSpot(grid, x, y, w, h)) return 1;
	HideAddedRows hide(grid);
	return SearchB(grid, x, y, w, h);
}
int HookSearchC(std::uint8_t* grid, void* x, void* y, std::uint8_t w, std::uint8_t h) noexcept {
	if (SlotSpot(grid, x, y, w, h)) return 1;
	HideAddedRows hide(grid);
	return SearchC(grid, x, y, w, h);
}

// ---- grid widgets (UI thread) ----

// The slot a grid widget shows, or -1 for any other grid.
int SlotOf(const void* widget) noexcept {
	const auto* bytes = static_cast<const std::uint8_t*>(widget);
	float marker[2] {};
	std::memcpy(marker, bytes + WidgetMarker, sizeof(marker));
	if (marker[1] != Marker) return -1;
	for (int slot = 0; slot < static_cast<int>(Slots.size()); ++slot)
		if (marker[0] == Marker + static_cast<float>(slot)) return slot;
	return -1;
}

bool IsInventoryGrid(const void* widget) noexcept {
	return static_cast<const std::uint8_t*>(widget)[WidgetPage] == PageInventory;
}

// First added row for that player: their class's own grid height.
int AddedRowTop(void* player) noexcept {
	if (!player) return -1;
	const int record = PageGrid(player, PageInventory, !ClientFlag());
	return record >= 0 && record < RecordCount ? OriginalHeight[record].load() : -1;
}

// A slot's cell for the widget's owner (packed x | y << 32), or NoCell.
std::uint64_t SlotCell(void* widget, int slot) noexcept {
	const int top = AddedRowTop(GridOwner(widget));
	return top < 0 ? NoCell : Pack(Slots[slot].column, top);
}

// A slot's inventory cell -> the slot widget's own cell; anything else as is.
std::uint64_t ToWidgetCell(void* widget, int slot, std::uint64_t cell) noexcept {
	const std::uint64_t origin = SlotCell(widget, slot);
	if (origin == NoCell) return cell;
	const int x = CellX(cell) - CellX(origin), y = CellY(cell) - CellY(origin);
	if (x < 0 || y < 0 || x >= Slots[slot].width || y >= Slots[slot].height) return cell;
	return Pack(x, y);
}

void NoteWidget(void* widget, int slot, void* owner, int top) noexcept {
	static std::array<void*, 32> seen {};
	for (auto& known : seen) {
		if (known == widget) return;
		if (!known) {
			known = widget;
			const auto* bytes = static_cast<const std::uint8_t*>(widget);
			std::int32_t count[2] {};
			float        marker[2] {};
			std::memcpy(count, bytes + WidgetCellCount, sizeof(count));
			std::memcpy(marker, bytes + WidgetMarker, sizeof(marker));
			Log(false, "grid widget %p: %s, %dx%d cells, marker %.0f/%.0f, owner %p, added rows from y=%d", widget, slot < 0 ? "stock or other grid" : Slots[slot].key, count[0],
			    count[1], marker[0], marker[1], owner, top);
			return;
		}
	}
}

// The stock inventory grid's unit, last seen.
struct GridUnit {
	std::int32_t useFlags = 0;
	std::int32_t id       = -1;
	std::int32_t type     = 6;
	void*        pointer {};
} StockUnit;

// Slot widgets take the inventory grid's unit and right click rights: the
// panel only sets them on its own grid.
void ShareUnit(std::uint8_t* widget, int slot) noexcept {
	auto& useFlags = *reinterpret_cast<std::int32_t*>(widget + WidgetUseFlags);
	auto& id       = *reinterpret_cast<std::int32_t*>(widget + WidgetUnitId);
	auto& type     = *reinterpret_cast<std::int32_t*>(widget + WidgetUnitType);
	auto& pointer  = *reinterpret_cast<void**>(widget + WidgetUnitPointer);
	if (slot < 0) {
		if (id != -1 && widget[WidgetPage] == PageInventory) StockUnit = { useFlags, id, type, pointer };
		return;
	}
	useFlags = StockUnit.useFlags;
	id       = StockUnit.id;
	type     = StockUnit.type;
	pointer  = StockUnit.pointer;
}

// ---- gray art of empty slots ----
//
// Each slot's art is shown while the slot is empty. The slot widgets' draws
// say what they hold; the art is shown or hidden on the UI thread, again every
// half second because D2R rebuilds the panel with everything shown.

std::array<std::atomic<int>, Slots.size()> Filled {}; // 0 empty, 1 filled, by the last draw
std::array<int, Slots.size()>              Applied {};
std::atomic<std::uint64_t>                 LastApply {};
std::atomic<bool>                          ApplyQueued {};
Widgets::WidgetHandle                      Root {};
std::array<Widgets::WidgetHandle, Slots.size()> ArtHandles {};
bool                                       ArtFailLogged {};

bool FindRoot() {
	if (Root) return true;
	for (const char* name : StockPanels) {
		Widgets::WidgetHandle stock {};
		if (WidgetApi->findPanel(Context, name, &stock) == Widgets::Result::Success && WidgetApi->findWidget(Context, stock, RootName, &Root) == Widgets::Result::Success) return true;
		Root = {};
	}
	if (WidgetApi->findPanel(Context, RootName, &Root) == Widgets::Result::Success) return true;
	Root = {};
	return false;
}

bool FindArt() {
	for (std::size_t slot = 0; slot < Slots.size(); ++slot)
		if (!ArtHandles[slot] && WidgetApi->findWidget(Context, Root, ("Art" + std::to_string(slot)).c_str(), &ArtHandles[slot]) != Widgets::Result::Success) {
			ArtHandles[slot] = {};
			return false;
		}
	return true;
}

void __cdecl ApplySlots(const D2RL::PluginContext*, void*) noexcept {
	bool ok = FindRoot();
	if (Config.showArt) {
		ok = ok && FindArt();
		for (std::size_t slot = 0; ok && slot < Slots.size(); ++slot) {
			const int filled = Filled[slot].load();
			ok               = WidgetApi->setWidgetVisible(Context, ArtHandles[slot], filled == 0) == Widgets::Result::Success;
			Applied[slot]    = filled;
		}
		if (!ok && !ArtFailLogged) Log(true, "slot art: the widgets were not found, empty slots keep their art when filled");
		ArtFailLogged = ArtFailLogged || !ok;
	}
	LastApply.store(ok ? GetTickCount64() : GetTickCount64() + 4500); // a failure retries in 5 s
	ApplyQueued.store(false);
}

void NoteFilled(int slot, bool filled) noexcept {
	if (!Threads || !WidgetApi) return;
	Filled[slot].store(filled);
	const std::uint64_t now = GetTickCount64();
	if (((!Config.showArt || Applied[slot] == static_cast<int>(filled)) && now < LastApply.load() + 500) || ApplyQueued.exchange(true)) return;
	if (Threads->runOnUiThread(Context, ApplySlots, nullptr) != D2RL::Threads::Result::Success) ApplyQueued.store(false);
}

void HookDraw(void* widget, void* a, void* b, void* c) noexcept {
	if (!widget || !IsInventoryGrid(widget)) return Draw(widget, a, b, c);
	const int   slot  = SlotOf(widget);
	ShareUnit(static_cast<std::uint8_t*>(widget), slot);
	void* const owner = GridOwner(widget);
	const int   top   = AddedRowTop(owner);
	NoteWidget(widget, slot, owner, top);
	void* const inventory = owner && top >= 0 ? InventoryOf(owner, SourceName, __LINE__) : nullptr;
	// A slot that can't tell its cell shows nothing rather than the whole inventory.
	if (!inventory) {
		if (slot < 0) Draw(widget, a, b, c);
		return;
	}
	// Items this widget must not show: the added rows on any other grid, all
	// but the slot's own item on a slot.
	std::array<std::uint8_t*, 256> hidden {};
	std::size_t count = 0;
	bool        held  = false;
	for (std::uint8_t* item = FirstItem(inventory); item && count < hidden.size(); item = NextItem(item)) {
		if (NodeMode(item) != NodeInGrid) continue;
		auto* const data = *reinterpret_cast<std::uint8_t**>(item + UnitData);
		auto* const path = *reinterpret_cast<std::uint8_t**>(item + UnitPath);
		if (!data || !path || data[DataPage] != PageInventory) continue;
		const int x = *reinterpret_cast<std::int32_t*>(path + PathX), y = *reinterpret_cast<std::int32_t*>(path + PathY);
		const bool hide = slot < 0 ? y >= top : !(x == Slots[slot].column && y == top);
		held            = held || (slot >= 0 && !hide);
		if (!hide) continue;
		data[DataPage]   = PageHidden;
		hidden[count++] = data;
	}
	Draw(widget, a, b, c);
	for (std::size_t i = 0; i < count; ++i) hidden[i][DataPage] = PageInventory;
	if (slot >= 0) NoteFilled(slot, held);
}

std::int32_t* HookItemRect(void* widget, std::int32_t* out, std::uint64_t cell) noexcept {
	const int slot = widget && IsInventoryGrid(widget) ? SlotOf(widget) : -1;
	return ItemRect(widget, out, slot < 0 ? cell : ToWidgetCell(widget, slot, cell));
}

std::int32_t* HookCellRect(void* widget, std::int32_t* out, std::uint64_t size, std::uint64_t cell) noexcept {
	const int slot = widget && IsInventoryGrid(widget) ? SlotOf(widget) : -1;
	return CellRect(widget, out, size, slot < 0 ? cell : ToWidgetCell(widget, slot, cell));
}

bool HookValidCell(void* widget, std::uint64_t cell) noexcept {
	const int slot = widget && IsInventoryGrid(widget) ? SlotOf(widget) : -1;
	if (slot < 0) return ValidCell(widget, cell);
	const std::uint64_t origin = SlotCell(widget, slot);
	return origin != NoCell && cell == origin;
}

// The cell under the mouse. A slot widget answers its slot cell when the
// mouse is over it, the hand is empty or holds the slot's item.
// What the hand may hold over a slot: its own item, and once the slot is
// filled, anything for the cube (the game puts it inside) and scrolls for
// their tome (the game adds them).
bool Accepts(int slot, std::uint32_t held) noexcept {
	if (held == Slots[slot].code) return true;
	if (Filled[slot].load() != 1) return false;
	switch (slot) {
	case 0: return true;
	case 1: return held == Code("tsc ");
	case 2: return held == Code("isc ");
	default: return false;
	}
}

std::uint64_t* HookCellAtPoint(void* widget, std::uint64_t* out, std::uint64_t point) noexcept {
	const int slot = widget && IsInventoryGrid(widget) ? SlotOf(widget) : -1;
	if (slot < 0) return CellAtPoint(widget, out, point);
	*out = NoCell;
	const std::uint64_t origin = SlotCell(widget, slot);
	if (origin == NoCell) return out;
	// The widget's own cells on screen: (0,0) and (1,1) give the corner and the cell size.
	std::int32_t corner[4] {}, next[4] {};
	CellRect(widget, corner, Pack(1, 1), Pack(0, 0));
	CellRect(widget, next, Pack(1, 1), Pack(1, 1));
	const int unitX = next[0] - corner[0], unitY = next[1] - corner[1];
	const int x = CellX(point), y = CellY(point);
	if (unitX <= 0 || unitY <= 0 || x < corner[0] || y < corner[1] || x >= corner[0] + unitX * Slots[slot].width || y >= corner[1] + unitY * Slots[slot].height) return out;
	if (void* const held = CursorItem(); held && !Accepts(slot, ItemCode(held))) return out;
	*out = origin;
	return out;
}

bool PatchVtable() noexcept {
	Vtable = reinterpret_cast<void**>(Base + GridVtableRva);
	for (const auto& [index, rva] : VtableEntries) {
		void* entry = nullptr;
		if (!Read(reinterpret_cast<std::uintptr_t>(Vtable + index), &entry, sizeof(entry)) || entry != reinterpret_cast<void*>(Base + rva)) {
			Log(true, "grid widget table entry %d is not D2R+0x%llX (another plugin changed it, or another game build)", index, static_cast<unsigned long long>(rva));
			return false;
		}
	}
	Draw        = reinterpret_cast<DrawFn>(Vtable[VtDraw]);
	ItemRect    = reinterpret_cast<ItemRectFn>(Vtable[VtItemRect]);
	ValidCell   = reinterpret_cast<ValidCellFn>(Vtable[VtValidCell]);
	CellAtPoint = reinterpret_cast<CellFn>(Vtable[VtCellAtPoint]);
	CellRect    = reinterpret_cast<CellRectFn>(Vtable[VtCellRect]);
	DWORD protect = 0;
	if (!VirtualProtect(Vtable, (VtCellRect + 1) * sizeof(void*), PAGE_READWRITE, &protect)) return false;
	Vtable[VtDraw]        = reinterpret_cast<void*>(&HookDraw);
	Vtable[VtItemRect]    = reinterpret_cast<void*>(&HookItemRect);
	Vtable[VtValidCell]   = reinterpret_cast<void*>(&HookValidCell);
	Vtable[VtCellAtPoint] = reinterpret_cast<void*>(&HookCellAtPoint);
	Vtable[VtCellRect]    = reinterpret_cast<void*>(&HookCellRect);
	VirtualProtect(Vtable, (VtCellRect + 1) * sizeof(void*), protect, &protect);
	return true;
}

void RestoreVtable() noexcept {
	if (!Vtable || !Draw) return;
	DWORD protect = 0;
	if (!VirtualProtect(Vtable, (VtCellRect + 1) * sizeof(void*), PAGE_READWRITE, &protect)) return;
	Vtable[VtDraw]        = reinterpret_cast<void*>(Draw);
	Vtable[VtItemRect]    = reinterpret_cast<void*>(ItemRect);
	Vtable[VtValidCell]   = reinterpret_cast<void*>(ValidCell);
	Vtable[VtCellAtPoint] = reinterpret_cast<void*>(CellAtPoint);
	Vtable[VtCellRect]    = reinterpret_cast<void*>(CellRect);
	VirtualProtect(Vtable, (VtCellRect + 1) * sizeof(void*), protect, &protect);
}

// ---- slot placement ----
//
// The positions are on screen: inventory panel units from the top-left corner
// of Expanded Inventory's panel (RefRect, right-panel units from the panel
// anchor). The slots are a child of the inventory panel, and D2RLoader 1.3.1
// can't move widgets, so the layout itself starts at RefRect - the panel's
// rect, read from the active mod's layout files (stock 1162 x 1507 at
// -1394,-651; a 10x8 mod 1162 x 1737 at -1394,-851). The files are read when
// the data tables load (not at plugin load: the game's archives aren't open
// yet, and reading them then crashes the game), through D2RCore's archive open
// (export ordinal 160), so the mod's copy wins.

constexpr int RefRect[4] { -1614, -651, 1382, 1507 };

namespace FileApi {
constexpr std::uint16_t OpenOrdinal = 160;
constexpr std::uint64_t Size = 0xA00050, Read = 0xA00EC0, Close = 0x9FFE50;
constexpr std::uint8_t  SizeBytes[] { 0x48, 0x8B, 0x01, 0x48, 0xFF, 0x60, 0x30 };                    // mov rax, [rcx]; jmp [rax+0x30]
constexpr std::uint8_t  ReadBytes[] { 0x4C, 0x89, 0x44, 0x24, 0x18, 0x48, 0x89, 0x54, 0x24, 0x10 }; // mov [rsp+18h], r8; mov [rsp+10h], rdx
constexpr std::uint8_t  CloseBytes[] { 0xE9 };                                                     // jmp
} // namespace FileApi

using FileOpenFn  = void* (*)(const char* path) noexcept;
using FileSizeFn  = std::uint64_t (*)(void* file) noexcept;
using FileReadFn  = bool (*)(void* file, void* buffer, std::uint64_t size, std::uint64_t* read) noexcept;
using FileCloseFn = void (*)(void* file) noexcept;

// The whole file, or empty when it is not there or cannot be read.
std::string GameFile(const char* path) {
	const auto base = static_cast<std::uintptr_t>(Context->exeBase);
	static const FileOpenFn open = [base] {
		const auto same = [&](std::uint64_t rva, const std::uint8_t* bytes, std::size_t count) { return std::memcmp(reinterpret_cast<const void*>(base + rva), bytes, count) == 0; };
		HMODULE    core = GetModuleHandleW(L"D2RCore.dll");
		if (!base || !core || !same(FileApi::Size, FileApi::SizeBytes, sizeof(FileApi::SizeBytes)) || !same(FileApi::Read, FileApi::ReadBytes, sizeof(FileApi::ReadBytes)) ||
		    !same(FileApi::Close, FileApi::CloseBytes, sizeof(FileApi::CloseBytes)))
			return FileOpenFn {};
		return reinterpret_cast<FileOpenFn>(reinterpret_cast<void*>(GetProcAddress(core, MAKEINTRESOURCEA(FileApi::OpenOrdinal))));
	}();
	void* const file = open ? open(path) : nullptr;
	if (!file) return {};
	std::string         text;
	const std::uint64_t size = reinterpret_cast<FileSizeFn>(base + FileApi::Size)(file);
	if (size > 0 && size < (16u << 20)) {
		text.resize(size);
		std::uint64_t read = 0;
		if (!reinterpret_cast<FileReadFn>(base + FileApi::Read)(file, text.data(), size, &read) || read != size) text.clear();
	}
	reinterpret_cast<FileCloseFn>(base + FileApi::Close)(file);
	return text;
}

// Just enough JSON (with the comments D2R's files may have) to find a value
// by its key path among the root's members.
struct Json {
	std::string_view text;
	std::size_t      at {};

	void Space() {
		while (at < text.size()) {
			const char c = text[at];
			if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',' || c == ':' || static_cast<unsigned char>(c) >= 0x80) ++at;
			else if (c == '/' && at + 1 < text.size() && text[at + 1] == '/') at = text.find('\n', at) == std::string_view::npos ? text.size() : text.find('\n', at);
			else if (c == '/' && at + 1 < text.size() && text[at + 1] == '*') at = text.find("*/", at + 2) == std::string_view::npos ? text.size() : text.find("*/", at + 2) + 2;
			else break;
		}
	}
	std::string String() {
		std::string out;
		for (++at; at < text.size() && text[at] != '"'; ++at) {
			if (text[at] == '\\' && at + 1 < text.size()) ++at;
			out += text[at];
		}
		++at;
		return out;
	}
	void Skip() {
		Space();
		if (at >= text.size()) return;
		const char c = text[at];
		if (c == '"') {
			String();
		} else if (c == '{' || c == '[') {
			const char close = c == '{' ? '}' : ']';
			++at;
			for (Space(); at < text.size() && text[at] != close; Space()) Skip();
			++at;
		} else {
			while (at < text.size() && std::strchr(" \t\r\n,:}]/", text[at]) == nullptr) ++at;
		}
	}
	// Moves to the value of key inside the object starting here.
	bool Enter(std::string_view key) {
		Space();
		if (at >= text.size() || text[at] != '{') return false;
		for (++at, Space(); at < text.size() && text[at] != '}'; Space()) {
			if (text[at] != '"') return false;
			const std::string name = String();
			Space();
			if (name == key) return true;
			Skip();
		}
		return false;
	}
	double Number() {
		Space();
		std::size_t used = 0;
		const std::string rest(text.substr(at, 32));
		double      value = 0;
		try {
			value = std::stod(rest, &used);
		} catch (...) {
		}
		at += used;
		return value;
	}
};

// A rect object {x, y, width, height} here; false if it is anything else.
bool ReadRect(Json json, int (&rect)[4]) {
	json.Space();
	if (json.at >= json.text.size() || json.text[json.at] != '{') return false;
	bool any = false;
	for (int i = 0; i < 4; ++i) {
		static constexpr const char* Keys[] { "x", "y", "width", "height" };
		Json member = json;
		if (member.Enter(Keys[i])) {
			rect[i] = static_cast<int>(member.Number());
			any     = true;
		}
	}
	return any;
}

// The panel's rect from its layout's root fields; a "$Name" rect is looked up
// in _profilehd.json. 0 not found, 1 found.
int PanelRect(const std::string& layout, const std::string& profile, int (&rect)[4], std::string& from) {
	Json json { layout };
	if (!json.Enter("fields") || !json.Enter("rect")) return 0;
	json.Space();
	if (json.at < json.text.size() && json.text[json.at] == '"') {
		const std::string name = json.String();
		from                   = name;
		if (name.size() < 2 || name[0] != '$') return 0;
		Json values { profile };
		return values.Enter(name.substr(1)) && ReadRect(values, rect) ? 1 : 0;
	}
	from = "a literal rect";
	return ReadRect(json, rect) ? 1 : 0;
}

int  Offset[2] {};      // where the slots' layout starts, in panel units
bool OffsetKnown {};

// True when the offset changed.
bool ReadPanelOffset() noexcept {
	const std::string profile = GameFile("data/global/ui/layouts/_profilehd.json");
	int               rect[4] { 0, 0, 0, 0 };
	std::string       from;
	bool              found = false;
	// The expansion layout is based on the original one and may override its rect.
	for (const char* path : { "data/global/ui/layouts/playerinventoryexpansionlayouthd.json", "data/global/ui/layouts/playerinventoryoriginallayouthd.json" }) {
		const std::string layout = GameFile(path);
		if (!layout.empty() && PanelRect(layout, profile, rect, from)) {
			found = true;
			break;
		}
	}
	if (!found) {
		if (!OffsetKnown) Log(false, "slot placement: the inventory panel's rect was not found yet%s", profile.empty() ? " (the game files can't be read yet)" : "");
		return false;
	}
	const int dx = RefRect[0] - rect[0], dy = RefRect[1] - rect[1];
	const bool changed = !OffsetKnown || dx != Offset[0] || dy != Offset[1];
	if (changed) Log(false, "slot placement: inventory panel %d x %d at %d,%d (%s), slots offset by %d,%d", rect[2], rect[3], rect[0], rect[1], from.c_str(), dx, dy);
	Offset[0]   = dx;
	Offset[1]   = dy;
	OffsetKnown = true;
	return changed;
}

// ---- layout ----

std::string BuildLayout() {
	std::string children;
	const auto  add = [&](const std::string& widget) { children += (children.empty() ? "" : ", ") + widget; };
	for (int slot = 0; slot < static_cast<int>(Slots.size()); ++slot) {
		const auto& def = Slots[slot];
		const int   x = Config.position[slot][0], y = Config.position[slot][1];
		const std::string n = std::to_string(slot);
		// The paperdoll slot's own background and border, then the item's art
		// in gray (Item Dictionary's recolouring), then the grid over both.
		if (Config.showFrames) {
			char frame[256] {};
			std::snprintf(frame, sizeof(frame), R"({"type": "ImageWidget", "name": "Frame%d", "fields": {"rect": {"x": %d, "y": %d, "scale": %.2f}, "filename": "%s"}})", slot, x, y,
			              def.scale, def.background);
			add(frame);
		}
		if (Config.showArt)
			add(R"({"type": "ImageWidget", "name": "Art)" + n + R"(", "fields": {"rect": {"x": )" + std::to_string(x) + R"(, "y": )" + std::to_string(y) + R"(}, "filename": ")" +
			    def.art + R"(", "useAdvancedColoring": true, "recolorRange": {"hue": [0.5, 0.501], "saturation": [0.5, 0.501], "value": [0.5, 0.501]}, )"
			              R"("hsvTransform": [0.0, -1.0, -0.35, 0.0], "tintTransform": [0.0, 0.0, 0.0, 0.0]}})");
		char grid[512] {};
		std::snprintf(grid, sizeof(grid),
		              R"({"type": "InventoryGridWidget", "name": "Slot%d", "fields": {"rect": {"x": %d, "y": %d}, "cellCount": {"x": %d, "y": %d}, )"
		              R"("cellSize": "$ItemCellSize", "gemSocketFilename": "PANEL\\gemsocket", "chronicleFlourishOffset": {"x": %.0f, "y": %.0f}}})",
		              slot, x, y, def.width, def.height, Marker + static_cast<float>(slot), Marker);
		add(grid);
	}
	return std::string(R"({"type": "Widget", "name": ")") + RootName + R"(", "fields": {"rect": {"x": )" + std::to_string(Offset[0]) + R"(, "y": )" + std::to_string(Offset[1]) +
	       R"(}}, "children": [)" + children + "]}";
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
		if (key == "enabled") {
			Config.enabled = value != "false";
			continue;
		}
		if (key == "show_frames") {
			Config.showFrames = value != "false";
			continue;
		}
		if (key == "show_art") {
			Config.showArt = value != "false";
			continue;
		}
		bool known = false;
		for (int slot = 0; slot < static_cast<int>(Slots.size()); ++slot)
			for (int axis = 0; axis < 2; ++axis)
				if (key == std::string(Slots[slot].key) + (axis ? "_y" : "_x")) {
					Config.position[slot][axis] = std::atoi(value.c_str());
					known                       = true;
				}
		if (!known) Log(true, "config: unknown key %s", key.c_str());
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

bool Locate() noexcept {
	for (const auto& native : Natives) {
		Code16 code {};
		if (!Read(Base + native.rva, code.data(), code.size()) || code != native.bytes) {
			Log(true, "game code differs at D2R+0x%llX", static_cast<unsigned long long>(native.rva));
			return false;
		}
	}
	std::uint8_t lea[7] {};
	if (!Read(Base + BankArrayLeaRva, lea, sizeof(lea)) || lea[0] != 0x48 || lea[1] != 0x8D || lea[2] != 0x0D) return false;
	std::int32_t displacement = 0;
	std::memcpy(&displacement, lea + 3, sizeof(displacement));
	if (BankArrayLeaRva + sizeof(lea) + displacement != BankArrayRva) {
		Log(true, "data table banks moved");
		return false;
	}
	std::array<std::uint8_t, 7> rowsLea {};
	if (!Read(Base + InventoryRowsLeaRva, rowsLea.data(), rowsLea.size()) || rowsLea != InventoryRowsLea) {
		Log(true, "inventory table location differs");
		return false;
	}
	ClientPlayerId = At<ClientPlayerIdFn>(ClientPlayerIdRva);
	ClientPlayer   = At<ClientPlayerFn>(ClientPlayerRva);
	ClientFlag     = At<ClientFlagFn>(ClientFlagRva);
	PageGrid       = At<PageGridFn>(PageGridRva);
	InventoryOf    = At<InventoryFn>(InventoryRva);
	FirstItem      = At<ItemFn>(FirstItemRva);
	NextItem       = At<ItemFn>(NextItemRva);
	NodeMode       = At<NodeModeFn>(NodeModeRva);
	ItemCode       = At<ItemCodeFn>(ItemCodeRva);
	ItemSize       = At<ItemSizeFn>(ItemSizeRva);
	CursorItem     = At<CursorItemFn>(CursorItemRva);
	GridOwner      = At<GridOwnerFn>(GridOwnerRva);
	return true;
}

template <typename Function>
bool Hook(std::uint64_t rva, Function target, Function& original) noexcept {
	const Code16* bytes = nullptr;
	for (const auto& native : Natives)
		if (native.rva == rva) bytes = &native.bytes;
	Function trampoline = nullptr;
	if (!bytes || !Context->InstallInlineHook(rva, bytes->data(), static_cast<std::uint32_t>(bytes->size()), target, &trampoline) || !trampoline) {
		Log(true, "could not hook D2R+0x%llX", static_cast<unsigned long long>(rva));
		return false;
	}
	original = trampoline;
	return true;
}

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &Info;
}

// ---- layout registration ----

namespace {

const D2RL::ResourceService*        Resources {};
D2RL::Resources::RegistrationHandle LayoutHandle {};

// (Re)registers the slots' layout with the current offset.
bool RegisterLayout() noexcept {
	const std::string layout = BuildLayout();
	const std::string path   = "data/global/ui/layouts/inventory-slots/Slotshd.json";
	if (LayoutHandle) Resources->unregisterResource(Context, LayoutHandle);
	LayoutHandle = {};
	const D2RL::Resources::ResourceRegistration resource {
		.structSize = D2RL::Resources::ResourceRegistrationSize,
		.path       = path.c_str(),
		.bytes      = layout.data(),
		.byteCount  = layout.size(),
	};
	return Resources->registerResource(Context, &resource, &LayoutHandle) == D2RL::Resources::Result::Success;
}

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (!D2RL::HasContext(context) || !context->GetApi()) return false;
	Context = context;
	for (auto& height : OriginalHeight) height.store(-1);
	LoadConfig();
	if (!Config.enabled) {
		Log(false, "Inventory Slots %s: disabled in config", PluginVersion);
		return true;
	}
	Base = context->exeBase;
	if (!Base || !Locate()) {
		Log(true, "Inventory Slots %s: inactive, this game build isn't supported (Steam 3.3.93787 / 3.3.94995 are)", PluginVersion);
		return true;
	}
	const auto* lifecycle = Query<D2RL::LifecycleService>("lifecycle");
	const auto* resources = Query<D2RL::ResourceService>("resource");
	const auto* panels    = Query<D2RL::PanelService>("panel");
	if (!D2RL::HasLifecycleServiceField(lifecycle, D2RL::LifecycleServiceRequiredSize) || !resources || !D2RL::HasResourceServiceField(resources, D2RL::ResourceServiceRequiredSize) ||
	    !D2RL::HasPanelServiceField(panels, D2RL::PanelServiceRegisterChildLayoutFieldEnd)) {
		Log(true, "Inventory Slots %s: inactive, this D2RLoader lacks a needed service", PluginVersion);
		return true;
	}
	Threads   = Query<D2RL::ThreadService>("thread");
	WidgetApi = Query<D2RL::WidgetService>("widget");
	if (!D2RL::HasThreadServiceField(Threads, D2RL::ThreadServiceRequiredSize)) Threads = nullptr;
	if (!D2RL::HasWidgetServiceField(WidgetApi, D2RL::WidgetServiceRequiredSize)) WidgetApi = nullptr;
	// The searches first: once the rows exist, nothing may land there by itself.
	if (!Hook(SearchARva, &HookSearchA, SearchA) || !Hook(SearchBRva, &HookSearchB, SearchB) || !Hook(SearchCRva, &HookSearchC, SearchC)) {
		Log(true, "Inventory Slots %s: inactive, the free-spot search could not be hooked", PluginVersion);
		return true;
	}
	if (!RedirectSizeCall()) Log(true, "new cubes, tomes and keys won't go to their empty slot by themselves (the free-spot search could not be patched)");
	if (!PatchVtable()) {
		Log(true, "Inventory Slots %s: inactive, the inventory grid widget could not be wrapped", PluginVersion);
		return true;
	}
	const D2RL::Lifecycle::DataTablesLoadedListener listener {
		.structSize = D2RL::Lifecycle::DataTablesLoadedListenerSize,
		.callback   = OnTablesLoaded,
	};
	D2RL::Lifecycle::ListenerHandle listenerHandle {};
	if (lifecycle->registerDataTablesLoadedListener(context, &listener, &listenerHandle) != D2RL::Lifecycle::Result::Success) {
		RestoreVtable();
		Log(true, "Inventory Slots %s: inactive, could not follow the data table loads", PluginVersion);
		return true;
	}
	// Tables already loaded (plugin loaded late): grow them now.
	GrowGrids();

	Resources = resources;
	const D2RL::Panels::ChildLayoutRegistration child {
		.structSize = D2RL::Panels::ChildLayoutRegistrationSize,
		.flags      = D2RL::Panels::ChildLayoutFlags::KeyboardMouseOnly,
		.stockPanel = D2RL::Panels::StockPanel::PlayerInventory,
		.localId    = "Slots",
	};
	D2RL::Panels::ChildLayoutHandle childHandle {};
	if (!RegisterLayout()) {
		Log(true, "Inventory Slots %s: the slot layout could not be registered; slots are invisible", PluginVersion);
	} else if (const auto result = panels->registerChildLayout(context, &child, &childHandle); result != D2RL::Panels::Result::Success) {
		Log(true, "Inventory Slots %s: the inventory panel refused the slots (result %u); slots are invisible", PluginVersion, static_cast<unsigned>(result));
	}
	Log(false, "Inventory Slots %s: active, cube %d,%d, Town Portal %d,%d, Identify %d,%d, keys %d,%d", PluginVersion, Config.position[0][0], Config.position[0][1],
	    Config.position[1][0], Config.position[1][1], Config.position[2][0], Config.position[2][1], Config.position[3][0], Config.position[3][1]);
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	RestoreVtable();
}
