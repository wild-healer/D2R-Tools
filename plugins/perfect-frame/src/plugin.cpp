// Perfect Frame: a golden frame around the tooltip of a perfect unique, set
// item or runeword (every stat that can roll is at its best), and a red one
// when every roll is at its worst.
//
// How (Steam 3.3.93787 / 94995 RVAs):
//   - Rolls: the item's own rows (UniqueItems, SetItems, Runes) say which
//     properties roll and their range, read from the game's compiled tables,
//     so modded tables count. A property rolls when its min and max differ
//     (fixed-range functions such as charges and procs don't count). The
//     item's values come from its own magic stat lists (unit +0x88, lists
//     chained from +0x90/+0x98, D2RCore's 16-byte entries {layer, stat,
//     value}), summed per stat; every property on the same stat is summed at
//     its best (or worst) too. Uniques and set items with defense also need
//     the best base defense roll, read from the unit's own list (+0x30, so a
//     worn item's per-level defense doesn't count): maxac (+1 with Enhanced
//     Defense, as the game rolls it), x1.5 when ethereal. Sockets are capped
//     at the base's gemsockets. An item with nothing that varies is always
//     perfect, never the worst. Unidentified items get no frame.
//   - Which tooltip: D2RLoader's item tooltip event names the hovered item
//     (box 0). Comparison texts are built through the thunk pointers
//     0x3E2AF60 (BuildCompared(out, bodyLocation)) and 0x3E2AF58 (an item's
//     tooltip text): both are hooked, so the compared item is known even when
//     another plugin builds the comparison itself; its box is matched by its
//     text, else by the order the comparisons were asked for.
//   - The frame: TooltipsPanel::Draw (0x880550) draws its boxes, a vector of
//     0x2E8-byte entries at +0x168 (count +0x170), then empties it. Each box's
//     rect {x, y, w, h} is at +0x60 (the header box, when there is one, at
//     +0x70, its text size at +0x30). After the panel draws, the frame is
//     drawn around the box with the game's filled rectangle 0x657B90(x1, y1,
//     x2, y2, const float rgba[4], 1), the call the box backgrounds use.

#include <D2RLPlugin/api.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

namespace Items  = D2RL::Items;
namespace Tables = D2RL::DataTables;
namespace Events = D2RL::SharedEvents;

constexpr const char* PluginId      = "perfect-frame";
constexpr const char* PluginVersion = "1.0.0";

const D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = PluginId,
	.name        = "Perfect Frame",
	.version     = PluginVersion,
	.author      = "wild-healer",
	.description = "A golden frame around the tooltip of perfect uniques, set items and runewords, a red one when every roll is at its worst.",
	.flags       = D2RL::PluginFlags::Client | D2RL::PluginFlags::NativeHooks,
};

struct Settings {
	bool  enabled = true, uniques = true, sets = true, runewords = true;
	bool  antiperfect = true;
	float color[4] { 226 / 255.f, 182 / 255.f, 76 / 255.f, 1.f };
	float antiColor[4] { 180 / 255.f, 20 / 255.f, 20 / 255.f, 1.f };
	int   thickness = 0;
} Config;

const D2RL::PluginContext*     Context  = nullptr;
std::uintptr_t                 Base     = 0;
const D2RL::ItemService*       ItemApi  = nullptr;
const D2RL::InventoryService*  InventoryApi = nullptr;
const D2RL::ThreadService*     Threads  = nullptr;
const D2RL::SharedEventService* EventApi = nullptr;
const D2RL::DataTableService*  TableApi = nullptr;

void Say(bool warn, const char* format, ...) noexcept {
	if (!Context) return;
	char    line[4096] {};
	va_list args;
	va_start(args, format);
	std::vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	warn ? Context->LogWarn(line) : Context->LogInfo(line);
}

// Also on the game console (for the perfect-frame command).
void Tell(const char* format, ...) noexcept {
	if (!Context) return;
	char    line[4096] {};
	va_list args;
	va_start(args, format);
	std::vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	Context->LogInfo(line);
	if (auto* api = Context->GetApi(); api && api->writeConsoleMessage) api->writeConsoleMessage(Context, line, D2RL::ConsoleMessageKind::Output);
}

template <typename Value>
Value Field(const void* row, std::ptrdiff_t offset) {
	Value value {};
	if (row) std::memcpy(&value, static_cast<const unsigned char*>(row) + offset, sizeof(value));
	return value;
}

// ---- native layout (Steam 3.3.93787 / 94995) ----

namespace Game {
constexpr std::uint64_t PanelDraw = 0x880550; // TooltipsPanel::Draw(panel)
constexpr std::uint8_t  PanelDrawBytes[] { 0x4C, 0x8B, 0xDC, 0x55, 0x53, 0x56, 0x57, 0x41, 0x55, 0x49, 0x8D, 0xAB, 0x78, 0xFD, 0xFF, 0xFF };
constexpr std::uint64_t FillRect  = 0x657B90; // (x1, y1, x2, y2, const float rgba[4], mode)
constexpr std::uint8_t  FillRectBytes[] { 0x44, 0x3B, 0xC1, 0x0F, 0x8C, 0xD3, 0x02, 0x00, 0x00, 0x4C, 0x8B, 0xDC, 0x55, 0x41, 0x54, 0x41 };
constexpr std::uint64_t GetUnit   = 0x9A5D0; // client unit (id, type)
constexpr std::uint8_t  GetUnitBytes[] { 0x4C, 0x63, 0xCA, 0x48, 0x8D, 0x05 };
constexpr std::uint64_t CompareSlot = 0x3E2AF60; // thunk pointer: a comparison text (GameString* out, int bodyLocation)
constexpr std::uint64_t TooltipSlot = 0x3E2AF58; // thunk pointer: an item's tooltip text (out, unit, unit, item, 0, 1, 0, 0, scratch)
constexpr std::uint64_t Player = 0x9A480, PlayerIndex = 0x8B2D0, Option = 0xCE500, Pet = 0x13ABC0, Inventory = 0x34A360, ItemAt = 0x3886D0;
constexpr std::uint8_t  PlayerBytes[] { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x83, 0xF9 };
constexpr std::uint8_t  PlayerIndexBytes[] { 0x8B, 0x05, 0x2E, 0x84, 0x99, 0x02, 0xC3 };
constexpr std::uint8_t  OptionBytes[] { 0x48, 0x63, 0xC1, 0x48, 0x8D, 0x0D, 0x96, 0xC8, 0x95, 0x02, 0x0F, 0xB6 };
constexpr std::uint8_t  PetBytes[] { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48 };
constexpr std::uint8_t  InventoryBytes[] { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B };
constexpr std::uint8_t  ItemAtBytes[] { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x63 };
constexpr std::uint64_t BodyLocs  = 0x36ED10; // an item's body locations (item, u8* first, u8* second)
constexpr std::uint8_t  BodyLocsBytes[] { 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x20 };

// Code the panel and box layout are read from (checked, not hooked).
struct Check {
	std::uint64_t             rva;
	std::vector<std::uint8_t> bytes;
};
const std::array<Check, 5> Checks { {
	// ShowItemTooltip: BodyLocs(item, &first, &second), then the comparison texts
	// for the first location and the second (added in that order, the second
	// only when it differs).
	{ 0x2C3DCF, { 0x4C, 0x8D, 0x44, 0x24, 0x20, 0x88, 0x85, 0x14, 0x01, 0x00, 0x00, 0x48, 0x8D, 0x54, 0x24, 0x21, 0x48, 0x8B, 0xCB, 0xE8, 0x29, 0xAF, 0x0A, 0x00, 0x0F,
	              0xB6, 0x54, 0x24, 0x21, 0x48, 0x8D, 0x4C, 0x24, 0x78, 0xE8, 0x36, 0x7A, 0xB6, 0x03, 0x0F, 0xB6, 0x54, 0x24, 0x20, 0x48, 0x8D, 0x4C, 0x24, 0x50 } },
	// Draw: cmp [r15+0x170], r14 (box count) / ... / cmp byte [r15+0xA39], 0 (one comparison at a time)
	{ 0x880B14, { 0x4D, 0x39, 0xB7, 0x70, 0x01, 0x00, 0x00, 0x0F, 0x86, 0xBE, 0x00, 0x00, 0x00, 0x33, 0xF6, 0x41, 0x80, 0xBF, 0x39, 0x0A, 0x00, 0x00, 0x00 } },
	// Draw: lea rdi, [r15+0x168] (boxes)
	{ 0x880B4A, { 0x49, 0x8D, 0xBF, 0x68, 0x01, 0x00, 0x00 } },
	// Draw: imul rcx, [rsp+0x40], 0x2E8 / add rcx, [rdi] / call draw box
	{ 0x880BBB, { 0x48, 0x69, 0x4C, 0x24, 0x40, 0xE8, 0x02, 0x00, 0x00, 0x48, 0x03, 0x0F, 0xE8, 0x14, 0x9C, 0x05, 0x00 } },
	// Box draw: its rect, x+w and y+h, background 0x657B90(.., [rsi+0x168], 1)
	{ 0x8DA9D2, { 0x44, 0x8B, 0x4E, 0x6C, 0x44, 0x8B, 0x46, 0x68, 0x8B, 0x56, 0x64, 0x44, 0x03, 0xCA, 0x8B, 0x4E, 0x60, 0x44, 0x03, 0xC1, 0xC7, 0x44, 0x24, 0x28, 0x01,
	              0x00, 0x00, 0x00, 0x4C, 0x89, 0x74, 0x24, 0x20, 0xE8, 0x98, 0xD1, 0xD7, 0xFF } },
} };

constexpr std::ptrdiff_t Boxes = 0x168, BoxCount = 0x170, OneCompare = 0xA39, CompareSide = 0xA38;
constexpr std::size_t    BoxSize = 0x2E8;
constexpr std::ptrdiff_t BoxText = 0x00, BoxHeaderSize = 0x30, BoxRect = 0x60, HeaderRect = 0x70;
constexpr std::ptrdiff_t UnitId = 0x08, UnitData = 0x10, DataFlags = 0x18, DataFile = 0x34;
constexpr std::uint32_t  Ethereal = 0x400000, Identified = 0x10;
constexpr int            ItemUnit = 4;
} // namespace Game

using PanelDrawFn = void (*)(void* panel) noexcept;
using FillRectFn  = void (*)(int x1, int y1, int x2, int y2, const float* rgba, int mode) noexcept;
using GetUnitFn   = void* (*)(std::uint32_t id, int type) noexcept;
using BodyLocsFn  = void (*)(void* item, std::uint8_t* first, std::uint8_t* second) noexcept;
using StatFn      = int (*)(void* unit, int stat, int layer) noexcept; // D2RCore ReadWideUnitStat

PanelDrawFn PanelDraw {};
StatFn      ReadStat {};

// ---- rolls (built on the game thread) ----

// A property of an item's row: the stats it sets, their layer, its min and
// max, and whether it rolls.
struct Prop {
	int  stats[4] { -1, -1, -1, -1 };
	int  layer {}, low {}, high {};
	bool rolls {};
};
struct RowProps {
	std::vector<Prop> props;
	bool              ed {};   // an Enhanced Defense property
	int               flat {}; // flat defense at its best
	int               code = -1; // its base (Items row), uniques and sets
};

std::mutex                 Lock; // guards the tables below
std::vector<RowProps>      Uniques, Sets, Runewords; // by row
std::vector<std::string>   RuneKeys;  // Runes row -> its key ("Runeword1")
std::map<std::string, std::vector<int>> RuneByName; // a runeword's name -> its rows (UI thread, built on first use)
const D2RL::LocalizationService* TextApi = nullptr;
std::map<std::uint32_t, int> BaseByCode; // Items code -> row
std::vector<int>           MaxDefense, MinDefense; // by Items row, 0 for none
std::vector<int>           MaxSockets; // by Items row (gemsockets), -1 when not known
std::atomic<bool>          Ready {}, Running {};
std::thread                Ticker;

constexpr std::uint32_t ItemRowSize = 448, UniqueRowSize = 348, SetRowSize = 460;
constexpr int ItemCode = 128, ItemMinDefense = 212, ItemMaxDefense = 216;
constexpr int UniqueCode = 40, SetCode = 40, RowProps0 = 152, UniquePropCount = 12, SetPropCount = 9, RunePropCount = 7;
constexpr int DefenseStat = 31, EdStat = 16, SocketsStat = 194;

// Where Properties keeps func1, stat1 and val1: found from rows every build
// has (ac, str, dmg%, res-all, ama, sor), not assumed (from the grail view).
struct PropLayout {
	int func = -1, stat = -1, value = -1;
};

PropLayout FindPropLayout(const unsigned char* rows, std::uint32_t count, std::uint32_t size) {
	PropLayout layout;
	if (count < 71 || size < 8) return layout;
	const auto row = [&](int r) { return rows + static_cast<std::size_t>(r) * size; };
	const auto u16 = [&](int r, int at) { return Field<std::uint16_t>(row(r), at); };
	for (int f = 0; f + 2 < static_cast<int>(size) && layout.func < 0; ++f)
		if (row(0)[f] == 1 && row(7)[f] == 1 && row(29)[f] == 7 && row(41)[f] == 1 && row(41)[f + 1] == 3 && row(67)[f] == 21 && row(70)[f] == 21) layout.func = f;
	for (int s = 0; s + 4 <= static_cast<int>(size) && layout.stat < 0; s += 2)
		if (u16(0, s) == 31 && u16(7, s) == 0 && u16(41, s) == 39 && u16(41, s + 2) == 41 && u16(67, s) == 83 && u16(70, s) == 83) layout.stat = s;
	for (int v = 0; v + 2 <= static_cast<int>(size) && layout.value < 0; v += 2)
		if (v != layout.stat && u16(67, v) == 0 && u16(70, v) == 1 && u16(0, v) == 0 && u16(7, v) == 0) layout.value = v;
	return layout;
}

// Where Runes keeps its 7 properties {i32 prop, par, min, max}: the offset at
// which every row's 7 entries are a property or -1 (unused), with the most
// properties that have a value.
// Vanilla's first row (Ancient's Pledge: 30, 13, 50 and 10) decides a tie.
int FindRuneProps(const unsigned char* rows, std::uint32_t count, std::uint32_t size, std::uint32_t propCount) {
	// Vanilla's first row first (the grail view's anchor): res-cold 30, res-all 13, ac% 50, dmg-to-mana 10.
	for (int o = 0; count > 0 && o + RunePropCount * 16 <= static_cast<int>(size); o += 4)
		if (Field<std::int32_t>(rows, o + 8) == 30 && Field<std::int32_t>(rows, o + 12) == 30 && Field<std::int32_t>(rows, o + 24) == 13 && Field<std::int32_t>(rows, o + 28) == 13
		    && Field<std::int32_t>(rows, o + 40) == 50 && Field<std::int32_t>(rows, o + 44) == 50 && Field<std::int32_t>(rows, o + 56) == 10 && Field<std::int32_t>(rows, o + 60) == 10)
			return o;
	int best = -1, bestUsed = 0;
	for (int o = 0; o + RunePropCount * 16 <= static_cast<int>(size); o += 4) {
		int  used = 0, empty = 0;
		bool ok   = true;
		for (std::uint32_t r = 0; r < count && ok; ++r) {
			const unsigned char* row = rows + static_cast<std::size_t>(r) * size;
			for (int p = 0; p < RunePropCount && ok; ++p) {
				const int prop = Field<std::int32_t>(row, o + p * 16), low = Field<std::int32_t>(row, o + p * 16 + 8), high = Field<std::int32_t>(row, o + p * 16 + 12);
				if (prop < -1 || prop >= static_cast<int>(propCount)) ok = false;
				else if (prop < 0) ++empty;
				else if (low != 0 || high != 0) { // zeroed memory reads as property 0 with no value: not counted
					++used;
					if (low > high && high >= 0) ok = false; // a rolled range is low to high (negative rolls are allowed)
				}
			}
		}
		const bool vanilla = count > 0 && Field<std::int32_t>(rows, o + 8) == 30 && Field<std::int32_t>(rows, o + 24) == 13 && Field<std::int32_t>(rows, o + 40) == 50
		                  && Field<std::int32_t>(rows, o + 56) == 10;
		if (ok && used > 0 && empty > 0 && (used > bestUsed || vanilla)) {
			best     = o;
			bestUsed = vanilla ? 1 << 30 : used;
		}
	}
	return best;
}

// The properties of a row, as the grail tracker's perfect rolls count them:
// they roll when min != max, and not with a fixed-range func.
RowProps ReadProps(const unsigned char* row, int first, int count, const unsigned char* props, std::uint32_t propCount, std::uint32_t propSize, const PropLayout& layout) {
	RowProps out;
	for (int p = 0; p < count; ++p) {
		const int at = first + p * 16;
		const int prop = Field<std::int32_t>(row, at), par = Field<std::int32_t>(row, at + 4), low = Field<std::int32_t>(row, at + 8), high = Field<std::int32_t>(row, at + 12);
		if (prop < 0 || static_cast<std::uint32_t>(prop) >= propCount) continue;
		const unsigned char* text = props + static_cast<std::size_t>(prop) * propSize;
		const int            func = text[layout.func];
		if (func == 11 || func == 12 || func == 15 || func == 16 || func == 19 || func == 36 || (func == 17 && par)) continue;
		Prop entry { .low = low, .high = high, .rolls = low != high };
		// Min/max damage: the one- or two-handed or the throw stat, whichever the base has.
		if (func == 5) entry.stats[0] = 21, entry.stats[1] = 23, entry.stats[2] = 159;
		else if (func == 6) entry.stats[0] = 22, entry.stats[1] = 24, entry.stats[2] = 160;
		else if (func == 7) entry.stats[0] = 17, entry.stats[1] = 18; // item_maxdamage_percent, item_mindamage_percent
		else
			for (int s = 0; s < 4; ++s) {
				const int stat = Field<std::uint16_t>(text, layout.stat + 2 * s);
				if (stat != 0xFFFF && (s == 0 || text[layout.func + s])) entry.stats[s] = stat;
			}
		if (entry.stats[0] < 0) continue;
		entry.layer = func == 21 ? Field<std::uint16_t>(text, layout.value) : func == 22 || func == 10 ? par : 0;
		for (const int stat : entry.stats) {
			if (stat == EdStat && high > 0) out.ed = true;
			if (stat == DefenseStat) out.flat += high;
		}
		out.props.push_back(entry);
	}
	return out;
}

bool GetTable(Tables::TableId id, Tables::TableView& view) {
	view = { .structSize = Tables::TableViewSize };
	return TableApi->getTable(Context, Tables::Bank::Rotw, id, &view) == Tables::Result::Success && view.rows;
}

void __cdecl BuildTables(const D2RL::PluginContext*, void*) noexcept {
	if (Ready) return;
	Tables::TableView props, items, uniques, sets, runes;
	if (!GetTable(Tables::TableId::Properties, props) || !GetTable(Tables::TableId::Items, items) || !GetTable(Tables::TableId::UniqueItems, uniques)
	    || !GetTable(Tables::TableId::SetItems, sets))
		return; // tables not loaded yet: the ticker tries again
	const bool haveRunes = GetTable(Tables::TableId::Runes, runes);
	const auto* rows   = static_cast<const unsigned char*>(props.rows);
	const auto  layout = FindPropLayout(rows, props.rowCount, props.rowSize);
	if (layout.func < 0 || layout.stat < 0 || layout.value < 0) {
		Say(true, "Perfect Frame: Properties rows (%u of %u bytes) are laid out differently; inactive", props.rowCount, props.rowSize);
		Running = false;
		return;
	}
	std::lock_guard guard(Lock);
	if (items.rowSize == ItemRowSize) {
		MaxDefense.assign(items.rowCount, 0);
		MinDefense.assign(items.rowCount, 0);
		for (std::uint32_t i = 0; i < items.rowCount; ++i) {
			const auto* row = static_cast<const unsigned char*>(items.rows) + static_cast<std::size_t>(i) * items.rowSize;
			MaxDefense[i]   = static_cast<int>(Field<std::uint32_t>(row, ItemMaxDefense));
			MinDefense[i]   = static_cast<int>(Field<std::uint32_t>(row, ItemMinDefense));
			BaseByCode.emplace(Field<std::uint32_t>(row, ItemCode), static_cast<int>(i));
		}
		// The gemsockets column: the byte where these bases (vanilla values)
		// all have theirs.
		const std::pair<const char*, int> known[] { { "lgl", 0 }, { "wnd", 1 }, { "hax", 2 }, { "gsc", 3 }, { "axe", 4 }, { "2ax", 5 }, { "mpi", 6 }, { "9mt", 3 } };
		const auto rowOf = [&](const char* code) -> const unsigned char* {
			std::uint32_t key = 0;
			std::memcpy(&key, "    ", 4);
			std::memcpy(&key, code, std::strlen(code));
			const auto found = BaseByCode.find(key);
			return found == BaseByCode.end() ? nullptr : static_cast<const unsigned char*>(items.rows) + static_cast<std::size_t>(found->second) * items.rowSize;
		};
		int socketsAt = -1;
		for (int at = ItemCode + 4; at < static_cast<int>(ItemRowSize) && socketsAt < 0; ++at) {
			bool all = true;
			for (const auto& [code, sockets] : known)
				if (const auto* row = rowOf(code); !row || row[at] != sockets) all = false;
			if (all) socketsAt = at;
		}
		MaxSockets.assign(items.rowCount, -1);
		if (socketsAt >= 0)
			for (std::uint32_t i = 0; i < items.rowCount; ++i) MaxSockets[i] = static_cast<const unsigned char*>(items.rows)[static_cast<std::size_t>(i) * items.rowSize + socketsAt];
		else Say(true, "Perfect Frame: the Items sockets column was not found; socket rolls are taken as listed");
	} else
		Say(true, "Perfect Frame: Items rows are %u bytes (expected %u); defense is not checked", items.rowSize, ItemRowSize);
	const auto readRows = [&](std::vector<RowProps>& out, const Tables::TableView& table, std::uint32_t expected, int count, int codeAt, const char* name) {
		out.clear();
		if (table.rowSize != expected) {
			Say(true, "Perfect Frame: %s rows are %u bytes (expected %u); not framed", name, table.rowSize, expected);
			return;
		}
		out.resize(table.rowCount);
		for (std::uint32_t r = 0; r < table.rowCount; ++r) {
			const auto* row = static_cast<const unsigned char*>(table.rows) + static_cast<std::size_t>(r) * table.rowSize;
			out[r]          = ReadProps(row, RowProps0, count, rows, props.rowCount, props.rowSize, layout);
			if (const auto base = BaseByCode.find(Field<std::uint32_t>(row, codeAt)); base != BaseByCode.end()) out[r].code = base->second;
			// Sockets: never more than the base can have (Aldur's Rhythm rolls
			// 2-5 on a Jagged Star, which takes 3).
			if (const int code = out[r].code; code >= 0 && code < static_cast<int>(MaxSockets.size()) && MaxSockets[code] > 0)
				for (auto& prop : out[r].props)
					if (prop.stats[0] == SocketsStat) prop.high = std::min(prop.high, MaxSockets[code]), prop.low = std::min(prop.low, prop.high), prop.rolls = prop.low != prop.high;
		}
	};
	readRows(Uniques, uniques, UniqueRowSize, UniquePropCount, UniqueCode, "UniqueItems");
	readRows(Sets, sets, SetRowSize, SetPropCount, SetCode, "SetItems");
	Runewords.clear();
	if (haveRunes) {
		const auto* runeRows = static_cast<const unsigned char*>(runes.rows);
		const int   at       = FindRuneProps(runeRows, runes.rowCount, runes.rowSize, props.rowCount);
		if (at < 0) Say(true, "Perfect Frame: the Runes table (%u rows of %u bytes) is laid out differently; runewords are not framed", runes.rowCount, runes.rowSize);
		else {
			Runewords.resize(runes.rowCount);
			for (std::uint32_t r = 0; r < runes.rowCount; ++r)
				Runewords[r] = ReadProps(runeRows + static_cast<std::size_t>(r) * runes.rowSize, at, RunePropCount, rows, props.rowCount, props.rowSize, layout);
			// Its key ("Runeword1"), whose text is the runeword's name: a
			// runeword item only carries its name string.
			int name = -1;
			for (int o = 0; runes.rowCount > 3 && o + 10 <= static_cast<int>(runes.rowSize) && name < 0; ++o)
				if (!std::memcmp(runeRows + o, "Runeword1", 10) && !std::memcmp(runeRows + 3 * runes.rowSize + o, "Runeword4", 10)) name = o;
			RuneKeys.assign(runes.rowCount, {});
			if (name >= 0)
				for (std::uint32_t r = 0; r < runes.rowCount; ++r) {
					const char* key = reinterpret_cast<const char*>(runeRows + static_cast<std::size_t>(r) * runes.rowSize + name);
					RuneKeys[r].assign(key, strnlen(key, 64));
				}
			else
				Say(true, "Perfect Frame: runeword names not found in the Runes rows; runewords are only known through D2RLoader's item details");
			Say(false, "Perfect Frame: runeword properties at +%d of %u-byte Runes rows", at, runes.rowSize);
		}
	} else
		Say(true, "Perfect Frame: no Runes table; runewords are not framed");
	Ready = true;
	Say(false, "Perfect Frame: %zu uniques, %zu set items, %zu runewords read", Uniques.size(), Sets.size(), Runewords.size());
}

// ---- perfect check (UI thread) ----

// Life, mana and stamina keep 8 fraction bits.
int Display(int stat, int value) { return stat >= 6 && stat <= 11 ? value >> 8 : value; }

// Layers only matter for skills and auras.
bool Layered(int stat) { return stat == 107 || stat == 97 || stat == 151 || stat == 188 || stat == 83; }

struct Verdict {
	bool        perfect {};
	bool        anti {}; // every roll at its worst
	std::string why;     // for the perfect-frame command
	// The frame: 1 gold (perfect), 2 brown (every roll at its worst), 0 none.
	std::uint8_t Mark() const;
};
std::string LastVerdict = "no item hovered yet";

struct StatKey {
	int  stat, layer;
	auto operator<=>(const StatKey&) const = default;
};

// The item's own magic stat lists, read like the game's stat lookup
// (0x2FA060) and list search (0x2F5A00): the unit's stat list at +0x88, its
// lists chained from +0x90 (and +0x98) by +0x68, each with its state at +0x20,
// flags at +0x1C (0x40 = magic properties) and stats at +0x30 {pointer,
// count}. D2RCore widens the stats to 16 bytes {u32 layer, u32 stat, i32
// value, u32}. Unlike the unit's totals they keep the damage-related stats
// (Enhanced Damage), which the totals leave out.
std::vector<Items::ItemStatValue> NativeStats(void* unit, std::string* lists) {
	std::vector<Items::ItemStatValue> stats;
	const auto* ex = unit ? Field<const unsigned char*>(unit, 0x88) : nullptr;
	if (!ex || Field<std::int32_t>(ex, 0x1C) >= 0) return stats;
	for (const std::ptrdiff_t chain : { 0x90, 0x98 }) {
		int guard = 0;
		for (const auto* list = Field<const unsigned char*>(ex, chain); list && guard < 32; list = Field<const unsigned char*>(list, 0x68), ++guard) {
			const auto  flags   = Field<std::uint32_t>(list, 0x1C), state = Field<std::uint32_t>(list, 0x20);
			const auto* entries = Field<const unsigned char*>(list, 0x30);
			const auto  count   = Field<std::uint64_t>(list, 0x38);
			if (state != 0 || !(flags & 0x40) || !entries || count > 512) continue;
			for (std::uint64_t i = 0; i < count; ++i) {
				const auto at    = static_cast<std::ptrdiff_t>(i * 16);
				const auto layer = Field<std::uint32_t>(entries, at), stat = Field<std::uint32_t>(entries, at + 4);
				const auto value = Field<std::int32_t>(entries, at + 8);
				if (stat > 0xFFFF || layer > 0xFFFF) continue;
				stats.push_back({ stat, layer, value, flags });
				if (lists) *lists += " " + std::to_string(stat) + (layer ? ":" + std::to_string(layer) : "") + "=" + std::to_string(value);
			}
		}
	}
	return stats;
}

// The item's base defense roll: stat 31 in the unit's own list (+0x30), which
// holds the base's stats (defense, durability) without the magic ones or the
// wearer's per-level bonuses. -1 when it has none.
int BaseDefense(void* unit) {
	const auto* ex = unit ? Field<const unsigned char*>(unit, 0x88) : nullptr;
	if (!ex || Field<std::int32_t>(ex, 0x1C) >= 0) return -1;
	const auto* entries = Field<const unsigned char*>(ex, 0x30);
	const auto  count   = Field<std::uint64_t>(ex, 0x38);
	for (std::uint64_t i = 0; entries && count <= 64 && i < count; ++i) {
		const auto at = static_cast<std::ptrdiff_t>(i * 16);
		if (Field<std::uint32_t>(entries, at + 4) == 31 && Field<std::uint32_t>(entries, at) == 0) return Field<std::int32_t>(entries, at + 8);
	}
	return -1;
}

std::string StringById(std::uint32_t id) {
	char text[256] {};
	if (!TextApi || TextApi->getStringById(Context, id, text, sizeof(text), nullptr) != D2RL::Localization::Result::Success) return {};
	return text;
}

// The Runes rows of a runeword item, from its name (prefixIds[0] is its name
// string): two when two runewords share a name (Hustle).
std::vector<int> RunewordRows(std::uint32_t nameId) {
	if (RuneByName.empty() && TextApi)
		for (std::size_t r = 0; r < RuneKeys.size(); ++r) {
			char text[256] {};
			if (RuneKeys[r].empty() || TextApi->getStringByKey(Context, RuneKeys[r].c_str(), text, sizeof(text), nullptr) != D2RL::Localization::Result::Success || !text[0])
				continue;
			RuneByName[text].push_back(static_cast<int>(r));
		}
	const auto found = RuneByName.find(StringById(nameId));
	return found == RuneByName.end() ? std::vector<int> {} : found->second;
}

// One row's verdict for an item. Lock held.
Verdict JudgeRow(const RowProps& entry, bool isRune, const char* kind, int row, void* unit, bool eth, const std::vector<Items::ItemStatValue>& stats, bool fromLists) {
	Verdict verdict;
	char    line[160];
	std::snprintf(line, sizeof(line), "%s row %d%s:", kind, row, eth ? " (eth)" : "");
	verdict.why = line;
	// The item's values per stat and layer (and per stat over all layers).
	std::map<StatKey, int> have;
	std::map<int, int>     haveAny;
	for (const auto& stat : stats) {
		have[{ static_cast<int>(stat.statId), static_cast<int>(stat.layer) }] += stat.value;
		haveAny[static_cast<int>(stat.statId)] += stat.value;
	}
	const auto value = [&](int stat, int layer, bool& present) {
		if (fromLists) {
			if (const auto found = have.find({ stat, Layered(stat) ? layer : 0 }); found != have.end()) return present = true, Display(stat, found->second);
			if (const auto found = haveAny.find(stat); found != haveAny.end()) return present = true, Display(stat, found->second);
			return present = false, 0;
		}
		const int read = ReadStat && unit ? ReadStat(unit, stat, Layered(stat) ? layer : 0) : 0;
		present        = read != 0;
		return Display(stat, read);
	};
	// The best total of each stat: every property on it at its best.
	std::map<StatKey, int> best, worst;
	for (const auto& prop : entry.props)
		for (const int stat : prop.stats)
			if (stat >= 0) best[{ stat, Layered(stat) ? prop.layer : 0 }] += prop.high, worst[{ stat, Layered(stat) ? prop.layer : 0 }] += prop.low;

	bool varies = false, perfect = true, anti = true;
	// Defense of uniques and set items: the base's roll (runewords keep the
	// base's own). With Enhanced Defense the game sets it to the base's best + 1.
	const int  base    = BaseDefense(unit);
	const bool hasBase = !isRune && entry.code >= 0 && entry.code < static_cast<int>(MaxDefense.size()) && MaxDefense[entry.code] > 0;
	if (hasBase && base >= 0) {
		// Ethereal: the stored roll may be the base's or already x1.5.
		const int top = MaxDefense[entry.code] + (entry.ed ? 1 : 0), low = entry.ed ? top : MinDefense[entry.code];
		const bool best1 = base >= top && (!eth || base >= top * 3 / 2 || top * 2 < low * 3);
		const bool worst1 = base == low || (eth && base == low * 3 / 2);
		if (!entry.ed) varies = true;
		std::snprintf(line, sizeof(line), " base defense %d/%d%s", base, eth ? top * 3 / 2 : top, best1 ? "" : "!");
		verdict.why += line;
		if (!best1) perfect = false;
		if (!entry.ed && !worst1) anti = false;
	} else if (hasBase) {
		const int top  = MaxDefense[entry.code] + (entry.ed ? 1 : 0);
		const int want = (eth ? top * 3 / 2 : top) + entry.flat;
		varies         = true;
		anti           = false;
		if (ReadStat && unit) {
			const int got = ReadStat(unit, DefenseStat, 0);
			std::snprintf(line, sizeof(line), " defense %d/%d", got, want);
			verdict.why += line;
			if (got < want) perfect = false;
		} else
			verdict.why += " defense not readable";
	}
	for (const auto& prop : entry.props) {
		// Flat defense: in the totals above when the base roll isn't readable.
		if (!prop.rolls || (!isRune && prop.stats[0] == DefenseStat && !(hasBase && base >= 0 && fromLists))) continue;
		varies = true;
		bool any = false;
		for (const int stat : prop.stats) {
			if (stat < 0) continue;
			bool      present = false;
			const int got     = value(stat, prop.layer, present);
			if (!present) continue; // the damage stat this base doesn't use
			any            = true;
			const int want = best[{ stat, Layered(stat) ? prop.layer : 0 }], bad = worst[{ stat, Layered(stat) ? prop.layer : 0 }];
			const bool ok  = prop.high >= prop.low ? got >= want : got <= want;
			if (!(prop.high >= prop.low ? got <= bad : got >= bad)) anti = false;
			if (Layered(stat)) std::snprintf(line, sizeof(line), " %d:%d=%d/%d%s", stat, prop.layer, got, want, ok ? "" : "!");
			else std::snprintf(line, sizeof(line), " %d=%d/%d%s", stat, got, want, ok ? "" : "!");
			verdict.why += line;
			if (!ok) perfect = false;
		}
		if (!any) {
			std::snprintf(line, sizeof(line), " %d missing!", prop.stats[0]);
			verdict.why += line;
			perfect = false;
		}
	}
	if (!varies) verdict.why += " nothing rolls"; // every copy is perfect
	verdict.perfect = perfect;
	verdict.anti    = varies && anti && !perfect;
	return verdict;
}

std::uint8_t Verdict::Mark() const {
	return perfect ? 1 : anti && Config.antiperfect ? 2 : 0;
}

// unit: the item's unit when known (a comparison's), else the client's unit
// with the item's id.
Verdict Judge(const Items::ItemInfo& info, void* unit = nullptr) {
	Verdict verdict;
	Items::ItemDetails details { .structSize = Items::ItemDetailsSize, .runewordRecordId = -1 };
	std::vector<Items::ItemStatValue> stats;
	const bool haveDetails = info.handle && D2RL::HasItemServiceField(ItemApi, D2RL::ItemServiceDetailsFieldEnd) && ItemApi->getItemDetails;
	if (haveDetails) {
		if (ItemApi->getItemDetails(Context, info.handle, &details, nullptr, 0) == Items::Result::Success || details.statCount > 0) {
			const std::uint32_t needed = details.statCount;
			stats.resize(needed);
			if (needed && ItemApi->getItemDetails(Context, info.handle, &details, stats.data(), needed) != Items::Result::Success) stats.clear();
		}
	}
	if (!unit && Base) unit = reinterpret_cast<GetUnitFn>(Base + Game::GetUnit)(info.runtimeId, Game::ItemUnit);
	const void* data = unit ? Field<const void*>(unit, Game::UnitData) : nullptr;
	const bool  eth  = data && (Field<std::uint32_t>(data, Game::DataFlags) & Game::Ethereal);
	std::string lists;
	const char* source = "item stat lists";
	if (stats.empty() && unit) {
		stats  = NativeStats(unit, &lists);
		source = stats.empty() ? "stat totals" : "game stat lists";
	}
	std::lock_guard guard(Lock);
	const char*                  kind  = nullptr;
	const std::vector<RowProps>* table = nullptr;
	std::vector<int>             rows;
	if (info.quality == Items::Quality::Unique && Config.uniques) kind = "unique", table = &Uniques, rows = { info.qualityRecordId };
	else if (info.quality == Items::Quality::Set && Config.sets) kind = "set", table = &Sets, rows = { info.qualityRecordId };
	else if (Config.runewords && haveDetails && (details.flags & Items::ItemDetailRuneword)) kind = "runeword", table = &Runewords, rows = { details.runewordRecordId };
	else if (Config.runewords && !haveDetails && (info.quality == Items::Quality::Normal || info.quality == Items::Quality::Superior) && info.socketedItemCount > 0
	         && info.prefixIds[0])
		kind = "runeword", table = &Runewords, rows = RunewordRows(info.prefixIds[0]);
	if (!kind) {
		verdict.why = "not a unique, set item or runeword (or turned off)";
		return verdict;
	}
	if (!data || !(Field<std::uint32_t>(data, Game::DataFlags) & Game::Identified)) {
		verdict.why = std::string(kind) + (data ? ": unidentified -> no frame" : ": item not found -> no frame");
		return verdict;
	}
	// Perfect by any of its rows (a shared runeword name: the right one fits).
	for (const int row : rows) {
		if (row < 0 || row >= static_cast<int>(table->size())) {
			verdict.why += std::string(verdict.why.empty() ? "" : "; ") + kind + " row " + std::to_string(row) + " unknown";
			continue;
		}
		Verdict one = JudgeRow((*table)[row], table == &Runewords, kind, row, unit, eth, stats, source[0] != 's');
		verdict.why += (verdict.why.empty() ? "" : "; ") + one.why;
		verdict.perfect = verdict.perfect || one.perfect;
		verdict.anti    = verdict.anti || one.anti;
	}
	if (verdict.perfect) verdict.anti = false;
	if (rows.empty()) verdict.why = std::string(kind) + ": name \"" + StringById(info.prefixIds[0]) + "\" not found in Runes";
	verdict.why += std::string(" (") + source + (lists.empty() ? "" : ", lists" + lists) + ")";
	verdict.why += verdict.perfect ? " -> perfect" : verdict.anti ? " -> every roll at its worst" : " -> not perfect";
	return verdict;
}

// ---- which boxes get the frame (UI thread) ----

// Each item tooltip event since the last panel draw, in order: whether that
// item is perfect. Box i of the panel belongs to event i.
std::mutex        FrameLock;
std::vector<std::uint8_t> Pending, Shown; // Shown: the last events, kept while the boxes stay the same
std::uint64_t     ShownHash {};   // the text of the first box they were matched with
void*             ShownPanel {};  // and its panel
bool              ToldRect {};
int               ToldBoxes {};

void __cdecl OnTooltip(const D2RL::PluginContext*, Events::ItemTooltipEvent* event, void*) noexcept {
	if (!event || !Ready || !ItemApi) return;
	{
		// One hovered item per frame: its comparison boxes are matched by their
		// text (HookCompared), not by these reports.
		std::lock_guard guard(FrameLock);
		if (!Pending.empty()) return;
	}
	Items::ItemInfo info { .structSize = Items::ItemInfoSize };
	if (ItemApi->getItemInfo(Context, event->item, &info) != Items::Result::Success) return;
	Verdict         verdict = Judge(info);
	std::lock_guard guard(FrameLock);
	LastVerdict = std::move(verdict.why);
	Pending.push_back(verdict.Mark());
}

std::uint64_t HashBytes(const unsigned char* text, std::uint64_t size) {
	if (!text || size == 0 || size > (1u << 16)) return 0;
	std::uint64_t hash = 1469598103934665603ull;
	for (std::uint64_t i = 0; i < size; ++i) hash = (hash ^ text[i]) * 1099511628211ull;
	return hash;
}

// ---- comparison boxes (UI thread) ----
// ShowItemTooltip builds each comparison text through the thunk pointer
// Base+0x3E2AF60 (D2RCore BuildComparedItemTooltipWithStatRanges, the game's
// 0x2BCDB0(GameString* out, int bodyLocation)) and adds the box with a copy of
// that text. The pointer is swapped (it chains with other plugins that swap it):
// the equipped item at that location is judged and its text remembered, and
// the panel frames the box with the same text.
struct GameString {
	char*         text;
	std::uint64_t size;
	std::uint64_t capacity;
};
using CompareFn = GameString* (*)(GameString* out, int bodyLocation) noexcept;
using TooltipFn = GameString* (*)(GameString* out, void* unit, void* other, void* item, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, void*) noexcept;
CompareFn Compared {};
TooltipFn Tooltip {};

// A game string's text (top bit of the capacity: held in the string itself).
std::string_view TextOf(const GameString* string) {
	if (!string) return {};
	const auto* text = string->capacity >> 63 ? reinterpret_cast<const char*>(string) + 24 : string->text;
	if (!text || string->size == 0 || string->size > (1u << 16)) return {};
	return { text, string->size };
}

// The texts built for comparisons, with their verdicts: a box showing one of
// them gets its verdict.
struct BuiltText {
	std::string   text;
	std::uint8_t  mark; // Verdict::Mark
	std::uint64_t at;   // GetTickCount64
};
std::vector<BuiltText> BuiltTexts; // guarded by FrameLock
std::string            LastCompared = "no comparison built yet";
// Each comparison the game asked for since the last panel draw, in order
// (ShowItemTooltip asks for its first body location, then its second; a
// non-empty text gets a box, the second only when the locations differ).
struct CompareCall {
	int          location;
	bool         shown;
	std::uint8_t mark;
};
std::vector<CompareCall>  PendingCalls; // guarded by FrameLock
std::vector<std::uint8_t> ShownCompare; // the comparison boxes' verdicts, in box order
// The item whose tooltip text was built inside a comparison (UI thread).
thread_local int   CompareDepth {};
thread_local void* BuiltItem {};

// The item the game compares with at a body location: the player's, or the
// mercenary's for the head, body and hands when the game's option 0x1E says
// so (as 0x2BCE14..0x2BCEA1 decide).
void* ComparedItem(int location) {
	using IndexFn = std::uint32_t (*)() noexcept;
	using PlayerFn = void* (*)(std::uint32_t) noexcept;
	using OptionFn = std::uint8_t (*)(int) noexcept;
	using PetFn = int (*)(void* unit, int type, int index) noexcept;
	using InventoryFn = void* (*)(void* unit, const char* file, int line) noexcept;
	using AtFn = void* (*)(void* inventory, int location) noexcept;
	void* player = reinterpret_cast<PlayerFn>(Base + Game::Player)(reinterpret_cast<IndexFn>(Base + Game::PlayerIndex)());
	if (!player || location < 1 || location > 10) return nullptr;
	void* owner = player;
	if (reinterpret_cast<OptionFn>(Base + Game::Option)(0x1E) && (location == 1 || (location >= 3 && location <= 5))) {
		const int merc = reinterpret_cast<PetFn>(Base + Game::Pet)(player, 7, 0);
		owner          = merc == -1 ? nullptr : reinterpret_cast<GetUnitFn>(Base + Game::GetUnit)(static_cast<std::uint32_t>(merc), 1);
	}
	void* inventory = owner ? reinterpret_cast<InventoryFn>(Base + Game::Inventory)(owner, "perfect-frame", 0) : nullptr;
	return inventory ? reinterpret_cast<AtFn>(Base + Game::ItemAt)(inventory, location) : nullptr;
}

struct FindById {
	std::uint32_t   id;
	Items::ItemInfo info;
	bool            found;
};

// An item unit's info: the player's item with its id (with its runeword
// name), else what the unit itself says (a mercenary's item, a grail copy).
Items::ItemInfo InfoOf(void* item) {
	const void*     data = Field<const void*>(item, Game::UnitData);
	const auto      quality = static_cast<Items::Quality>(data ? Field<std::uint32_t>(data, 0) : 0);
	const auto      file    = data ? Field<std::int32_t>(data, Game::DataFile) : -1;
	FindById        find { .id = Field<std::uint32_t>(item, Game::UnitId) };
	D2RL::PlayerHandle player {};
	if (InventoryApi && InventoryApi->getLocalPlayer(Context, &player) == D2RL::Inventory::Result::Success) {
		const D2RL::Inventory::ItemFilter filter { .structSize = D2RL::Inventory::ItemFilterSize, .containerMask = 0xFFFFFFFEu };
		(void)InventoryApi->forEachInventoryItem(Context, player, &filter, [](const D2RL::PluginContext*, const Items::ItemInfo* info, void* user) noexcept {
			auto* find = static_cast<FindById*>(user);
			if (info->runtimeId != find->id) return D2RL::Inventory::IterationAction::Continue;
			find->info  = *info;
			find->found = true;
			return D2RL::Inventory::IterationAction::Stop;
		}, &find);
	}
	// Same id, other item (a copy made outside the client's units): its own.
	if (!find.found || find.info.quality != quality || find.info.qualityRecordId != file)
		find.info = { .structSize = Items::ItemInfoSize, .runtimeId = find.id, .quality = quality, .qualityRecordId = file };
	return find.info;
}

Verdict JudgeUnit(void* item) {
	if (!item) return { .why = "nothing equipped there" };
	return Judge(InfoOf(item), item);
}

// Remembers a built comparison text and its verdict.
void Remember(const GameString* built, std::uint8_t mark, std::uint64_t now) {
	std::string text(TextOf(built));
	std::erase_if(BuiltTexts, [&](const BuiltText& known) { return known.text == text || now - known.at > 3000; });
	if (BuiltTexts.size() >= 8) BuiltTexts.erase(BuiltTexts.begin());
	BuiltTexts.push_back({ std::move(text), mark, now });
}

// A text judged a moment ago: its verdict, else -1.
int Recent(const GameString* built, std::uint64_t now) {
	const std::string_view text = TextOf(built);
	for (auto& known : BuiltTexts)
		if (known.text == text && now - known.at < 500) {
			known.at = now;
			return known.mark;
		}
	return -1;
}

void TellCompared(const char* how, const std::string& why) {
	LastCompared = std::string(how) + ": " + why;
	static int told = 0;
	if (told < 8) {
		++told;
		Say(false, "Perfect Frame: compared %s", LastCompared.c_str());
	}
}

GameString* HookCompared(GameString* out, int bodyLocation) noexcept {
	++CompareDepth;
	BuiltItem         = nullptr;
	GameString* built = Compared(out, bodyLocation);
	--CompareDepth;
	void* item = BuiltItem;
	BuiltItem  = nullptr;
	if (!Ready) return built;
	const bool          shown = !TextOf(built).empty();
	const std::uint64_t now   = GetTickCount64();
	int                 known = -1;
	if (shown) {
		std::lock_guard guard(FrameLock);
		known = Recent(built, now);
	}
	if (shown && known < 0) {
		// The item whose text was built (another plugin may show its own),
		// else the one the game compares with.
		Verdict verdict = item ? JudgeUnit(item) : JudgeUnit(ComparedItem(bodyLocation));
		known           = verdict.Mark();
		std::lock_guard guard(FrameLock);
		Remember(built, verdict.Mark(), now);
		TellCompared(("location " + std::to_string(bodyLocation)).c_str(), verdict.why);
	}
	std::lock_guard guard(FrameLock);
	if (PendingCalls.size() < 8) PendingCalls.push_back({ bodyLocation, shown, static_cast<std::uint8_t>(known > 0 ? known : 0) });
	return built;
}

// An item's tooltip text: inside a comparison, the compared item; outside one
// (a plugin building its own comparison, like Grail On!), the text is judged
// and remembered so a box showing it is matched.
GameString* HookTooltip(GameString* out, void* unit, void* other, void* item, std::uint64_t a5, std::uint64_t a6, std::uint64_t a7, std::uint64_t a8, void* a9) noexcept {
	GameString* built = Tooltip(out, unit, other, item, a5, a6, a7, a8, a9);
	if (!Ready || !item) return built;
	if (CompareDepth > 0) {
		if (!BuiltItem) BuiltItem = item;
		return built;
	}
	if (TextOf(built).empty()) return built;
	const std::uint64_t now = GetTickCount64();
	{
		std::lock_guard guard(FrameLock);
		if (Recent(built, now) >= 0) return built;
	}
	Verdict         verdict = JudgeUnit(item);
	std::lock_guard guard(FrameLock);
	Remember(built, verdict.Mark(), now);
	TellCompared("item text", verdict.why);
	return built;
}

std::string_view BoxText(const unsigned char* box) {
	return TextOf(reinterpret_cast<const GameString*>(box + Game::BoxText));
}

// Whether a box shows a built comparison text (the same, or one holding the
// other), and its verdict, else -1.
int BuiltVerdict(std::string_view box) {
	if (box.empty()) return -1;
	for (auto it = BuiltTexts.rbegin(); it != BuiltTexts.rend(); ++it) {
		const std::string_view text = it->text;
		if (box == text || (text.size() >= 16 && box.find(text) != std::string_view::npos) || (box.size() >= 16 && text.find(box) != std::string_view::npos))
			return it->mark;
	}
	return -1;
}

std::string Printable(std::string_view text, std::size_t most) {
	std::string shown;
	for (std::size_t i = 0; i < text.size() && i < most; ++i) shown += text[i] >= 0x20 && text[i] < 0x7F ? text[i] : text[i] == '\n' ? '|' : '.';
	return shown;
}

std::uint64_t TextHash(const unsigned char* box) {
	const auto size     = Field<std::uint64_t>(box, Game::BoxText + 8);
	const auto capacity = Field<std::uint64_t>(box, Game::BoxText + 16);
	const auto* text    = capacity >> 63 ? box + Game::BoxText + 24 : Field<const unsigned char*>(box, Game::BoxText);
	return HashBytes(text, size);
}

// The game window's height, for the automatic thickness.
HWND GameWindow {};
BOOL CALLBACK FindWindowOfProcess(HWND window, LPARAM) {
	DWORD process = 0;
	GetWindowThreadProcessId(window, &process);
	if (process != GetCurrentProcessId() || !IsWindowVisible(window) || GetWindow(window, GW_OWNER)) return TRUE;
	GameWindow = window;
	return FALSE;
}

int Thickness() {
	if (Config.thickness > 0) return Config.thickness;
	if (!GameWindow || !IsWindow(GameWindow)) {
		GameWindow = nullptr;
		EnumWindows(FindWindowOfProcess, 0);
	}
	RECT client {};
	if (!GameWindow || !GetClientRect(GameWindow, &client) || client.bottom <= 0) return 3;
	return std::max(2, static_cast<int>((client.bottom + 180) / 360));
}

void DrawFrame(const std::int32_t rect[4], int thickness, const float* color) {
	const auto fill = reinterpret_cast<FillRectFn>(Base + Game::FillRect);
	const int  x1 = rect[0] - thickness, y1 = rect[1] - thickness, x2 = rect[0] + rect[2] + thickness, y2 = rect[1] + rect[3] + thickness;
	fill(x1, y1, x2, rect[1], color, 1);          // top
	fill(x1, rect[1] + rect[3], x2, y2, color, 1); // bottom
	fill(x1, rect[1], rect[0], rect[1] + rect[3], color, 1);                     // left
	fill(rect[0] + rect[2], rect[1], x2, rect[1] + rect[3], color, 1);          // right
}

void HookPanelDraw(void* panel) noexcept {
	// The boxes before the draw empties the list (their memory stays).
	auto* const   self  = static_cast<unsigned char*>(panel);
	auto* const   boxes = panel ? Field<unsigned char*>(self, Game::Boxes) : nullptr;
	std::uint64_t count = panel ? Field<std::uint64_t>(self, Game::BoxCount) : 0;
	const bool    one = panel && self[Game::OneCompare], side = panel && self[Game::CompareSide];
	PanelDraw(panel);
	if (!boxes || count == 0 || count > 16) {
		std::lock_guard guard(FrameLock);
		if (panel == ShownPanel) Shown.clear(), ShownCompare.clear(); // the tooltip is gone
		return;
	}
	std::vector<std::uint8_t> frames;
	{
		std::lock_guard guard(FrameLock);
		const std::uint64_t hash = TextHash(boxes);
		if (!Pending.empty()) {
			Shown.swap(Pending);
			Pending.clear();
			ShownHash  = hash;
			ShownPanel = panel;
		} else if (panel == ShownPanel && hash != ShownHash)
			Shown.clear(); // other boxes now (a skill, a button): the events were for earlier ones
		if (panel == ShownPanel) frames = Shown;
		frames.resize(count, 0);
		// The order the boxes were asked for: the last pair of calls.
		if (!PendingCalls.empty()) {
			ShownCompare.clear();
			const std::size_t n     = PendingCalls.size();
			const CompareCall first = PendingCalls[n >= 2 ? n - 2 : 0];
			if (first.shown) ShownCompare.push_back(first.mark);
			if (n >= 2 && PendingCalls[n - 1].shown && PendingCalls[n - 1].location != first.location) ShownCompare.push_back(PendingCalls[n - 1].mark);
			PendingCalls.clear();
		}
		// Comparison boxes: by their text, else by the order they were asked for.
		for (std::uint64_t i = 1; i < count; ++i) {
			const std::string_view text    = BoxText(boxes + i * Game::BoxSize);
			const int              verdict = BuiltVerdict(text);
			if (verdict >= 0) frames[i] = static_cast<std::uint8_t>(verdict);
			else if (i - 1 < ShownCompare.size()) frames[i] = ShownCompare[i - 1];
			if (ToldBoxes < 4) {
				++ToldBoxes;
				Say(false, "Perfect Frame: box %llu of %llu (%llu bytes \"%s\"): %s, frame %s", static_cast<unsigned long long>(i), static_cast<unsigned long long>(count),
				    static_cast<unsigned long long>(text.size()), Printable(text, 60).c_str(), verdict >= 0 ? "matches a built text" : "matched by order", frames[i] ? "yes" : "no");
			}
		}
	}
	const int thickness = Thickness();
	for (std::uint64_t i = 0; i < count && i < frames.size(); ++i) {
		if (!frames[i]) continue;
		// The panel shows one comparison at a time when cycling them.
		if (one && i != 0 && i != (side ? 1u : 2u)) continue;
		const unsigned char* box = boxes + i * Game::BoxSize;
		std::int32_t         rect[4] { Field<std::int32_t>(box, Game::BoxRect), Field<std::int32_t>(box, Game::BoxRect + 4), Field<std::int32_t>(box, Game::BoxRect + 8),
                                       Field<std::int32_t>(box, Game::BoxRect + 12) };
		if (rect[2] <= 0 || rect[3] <= 0) continue;
		// The header box ("Currently Equipped") sits on top: frame both.
		if (Field<std::uint64_t>(box, Game::BoxHeaderSize) > 0) {
			const std::int32_t hx = Field<std::int32_t>(box, Game::HeaderRect), hy = Field<std::int32_t>(box, Game::HeaderRect + 4), hw = Field<std::int32_t>(box, Game::HeaderRect + 8),
			                   hh = Field<std::int32_t>(box, Game::HeaderRect + 12);
			if (hw > 0 && hh > 0) {
				const int x1 = std::min(rect[0], hx), y1 = std::min(rect[1], hy), x2 = std::max(rect[0] + rect[2], hx + hw), y2 = std::max(rect[1] + rect[3], hy + hh);
				rect[0] = x1, rect[1] = y1, rect[2] = x2 - x1, rect[3] = y2 - y1;
			}
		}
		if (!ToldRect) {
			ToldRect = true;
			Say(false, "Perfect Frame: first frame around %d,%d %dx%d, %d px", rect[0], rect[1], rect[2], rect[3], thickness);
		}
		DrawFrame(rect, thickness, frames[i] == 2 ? Config.antiColor : Config.color);
	}
}

// ---- console ----

D2RL::ConsoleCommandResult __cdecl OnCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext*, void*) noexcept {
	std::string verdict;
	{
		std::lock_guard guard(FrameLock);
		verdict = LastVerdict;
	}
	std::string compared;
	{
		std::lock_guard guard(FrameLock);
		compared = LastCompared;
	}
	Tell("perfect-frame: last hovered item: %s", verdict.c_str());
	Tell("perfect-frame: last compared item: %s", compared.c_str());
	Tell("perfect-frame: values are stat=item/best, ! = below its best");
	return D2RL::ConsoleCommandResult::Handled;
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
			Say(true, "config: could not read the config file, using defaults");
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
		else if (key == "uniques") Config.uniques = value != "false";
		else if (key == "sets") Config.sets = value != "false";
		else if (key == "runewords") Config.runewords = value != "false";
		else if (key == "antiperfect") Config.antiperfect = value != "false";
		else if (key == "antiperfect_color") {
			int r = 0, g = 0, b = 0;
			if (std::sscanf(value.c_str(), "%d , %d , %d", &r, &g, &b) == 3) {
				Config.antiColor[0] = std::clamp(r, 0, 255) / 255.f;
				Config.antiColor[1] = std::clamp(g, 0, 255) / 255.f;
				Config.antiColor[2] = std::clamp(b, 0, 255) / 255.f;
			} else
				Say(true, "config: antiperfect_color \"%s\" is not r,g,b", value.c_str());
		} else if (key == "thickness") Config.thickness = std::clamp(std::atoi(value.c_str()), 0, 40);
		else if (key == "color") {
			int r = 0, g = 0, b = 0;
			if (std::sscanf(value.c_str(), "%d , %d , %d", &r, &g, &b) == 3) {
				Config.color[0] = std::clamp(r, 0, 255) / 255.f;
				Config.color[1] = std::clamp(g, 0, 255) / 255.f;
				Config.color[2] = std::clamp(b, 0, 255) / 255.f;
			} else
				Say(true, "config: color \"%s\" is not r,g,b", value.c_str());
		} else
			Say(true, "config: unknown key %s", key.c_str());
	}
}

bool Matches(std::uint64_t rva, const std::uint8_t* bytes, std::size_t size) {
	SIZE_T copied = 0;
	std::vector<std::uint8_t> code(size);
	if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(Base + rva), code.data(), size, &copied) || copied != size) return false;
	return std::memcmp(code.data(), bytes, size) == 0;
}

bool Locate() {
	const std::pair<std::uint64_t, std::pair<const std::uint8_t*, std::size_t>> functions[] {
		{ Game::PanelDraw, { Game::PanelDrawBytes, sizeof(Game::PanelDrawBytes) } },
		{ Game::FillRect, { Game::FillRectBytes, sizeof(Game::FillRectBytes) } },
		{ Game::GetUnit, { Game::GetUnitBytes, sizeof(Game::GetUnitBytes) } },
		{ Game::BodyLocs, { Game::BodyLocsBytes, sizeof(Game::BodyLocsBytes) } },
		{ Game::Player, { Game::PlayerBytes, sizeof(Game::PlayerBytes) } },
		{ Game::PlayerIndex, { Game::PlayerIndexBytes, sizeof(Game::PlayerIndexBytes) } },
		{ Game::Option, { Game::OptionBytes, sizeof(Game::OptionBytes) } },
		{ Game::Pet, { Game::PetBytes, sizeof(Game::PetBytes) } },
		{ Game::Inventory, { Game::InventoryBytes, sizeof(Game::InventoryBytes) } },
		{ Game::ItemAt, { Game::ItemAtBytes, sizeof(Game::ItemAtBytes) } },
	};
	for (const auto& [rva, code] : functions)
		if (!Matches(rva, code.first, code.second)) {
			Say(true, "game code differs at D2R+0x%llX", static_cast<unsigned long long>(rva));
			return false;
		}
	for (const auto& check : Game::Checks)
		if (!Matches(check.rva, check.bytes.data(), check.bytes.size())) {
			Say(true, "game code differs at D2R+0x%llX", static_cast<unsigned long long>(check.rva));
			return false;
		}
	return true;
}

// Swaps a thunk pointer for a hook, keeping whatever it pointed to (D2RCore,
// or another plugin that swapped it first) as the original.
bool SwapSlot(std::uint64_t rva, void* hook, std::uint64_t* original) {
	auto*      slot    = reinterpret_cast<std::uint64_t*>(Base + rva);
	const auto current = *slot;
	if (!current) return false;
	DWORD old = 0;
	if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old)) return false;
	*original       = current;
	const bool done = InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(slot), reinterpret_cast<LONG64>(hook), static_cast<LONG64>(current))
	               == static_cast<LONG64>(current);
	VirtualProtect(slot, sizeof(*slot), old, &old);
	return done;
}

bool SwapCompareSlot() {
	std::uint64_t original = 0;
	const bool    done     = SwapSlot(Game::CompareSlot, reinterpret_cast<void*>(&HookCompared), &original);
	if (done) Compared = reinterpret_cast<CompareFn>(original);
	return done;
}

// The item tooltip text pointer. A plugin that swapped it already (Grail On!
// builds its comparison with what it found there) calls D2RCore through its
// own copy of the pointer, so the hook goes under it: that copy, in the
// plugin's writable data, now points here and the hook calls D2RCore.
const char* HookTooltipSlot() {
	auto*      slot    = reinterpret_cast<std::uint64_t*>(Base + Game::TooltipSlot);
	const auto current = *slot;
	if (!current) return "no tooltip pointer";
	HMODULE core = GetModuleHandleW(L"D2RCore.dll"), owner = nullptr;
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(current), &owner) || !owner
	    || owner == core || !core) {
		std::uint64_t original = 0;
		if (!SwapSlot(Game::TooltipSlot, reinterpret_cast<void*>(&HookTooltip), &original)) return "could not swap the tooltip pointer";
		Tooltip = reinterpret_cast<TooltipFn>(original);
		return nullptr;
	}
	const auto target = reinterpret_cast<std::uint64_t>(reinterpret_cast<void*>(GetProcAddress(core, "BuildItemTooltipWithStatRanges")));
	if (!target) return "D2RCore has no BuildItemTooltipWithStatRanges";
	Tooltip         = reinterpret_cast<TooltipFn>(target);
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(owner);
	const auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS64*>(reinterpret_cast<const unsigned char*>(owner) + dos->e_lfanew);
	const auto* section = IMAGE_FIRST_SECTION(nt);
	int         seated  = 0;
	for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
		if (!(section->Characteristics & IMAGE_SCN_MEM_WRITE) || (section->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
		auto*             begin = reinterpret_cast<unsigned char*>(owner) + section->VirtualAddress;
		const std::size_t words = section->Misc.VirtualSize / sizeof(std::uint64_t);
		for (std::size_t w = 0; w < words; ++w) {
			auto* word = reinterpret_cast<volatile LONG64*>(begin + w * sizeof(std::uint64_t));
			if (static_cast<std::uint64_t>(*word) == target
			    && InterlockedCompareExchange64(word, reinterpret_cast<LONG64>(&HookTooltip), static_cast<LONG64>(target)) == static_cast<LONG64>(target))
				++seated;
		}
	}
	if (!seated) {
		// Not found: on top of it then (its own comparison stays unframed).
		std::uint64_t original = 0;
		if (!SwapSlot(Game::TooltipSlot, reinterpret_cast<void*>(&HookTooltip), &original)) return "could not swap the tooltip pointer";
		Tooltip = reinterpret_cast<TooltipFn>(original);
		return "another plugin's tooltip pointer was not found; went on top of it";
	}
	Say(false, "Perfect Frame: item texts followed under another plugin's hook (%d pointer%s)", seated, seated == 1 ? "" : "s");
	return nullptr;
}

template <typename Service>
const Service* Query(const char* name) {
	const Service* service = nullptr;
	if (Context->QueryService(&service) != D2RL::ServiceQueryResult::Success) {
		Say(true, "Perfect Frame: %s service unavailable", name);
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
		Say(false, "Perfect Frame %s: disabled in config", PluginVersion);
		return true;
	}
	ItemApi  = Query<D2RL::ItemService>("item");
	if (Context->QueryService(&TextApi) != D2RL::ServiceQueryResult::Success) TextApi = nullptr;
	if (Context->QueryService(&InventoryApi) != D2RL::ServiceQueryResult::Success) InventoryApi = nullptr;
	if (!InventoryApi) Say(true, "Perfect Frame: no inventory service; compared items get no frame");
	Threads  = Query<D2RL::ThreadService>("thread");
	EventApi = Query<D2RL::SharedEventService>("shared event");
	TableApi = Query<D2RL::DataTableService>("data table");
	if (!ItemApi || !Threads || !EventApi || !TableApi) {
		Say(true, "Perfect Frame %s: inactive, D2RLoader lacks a service it needs", PluginVersion);
		return true;
	}
	if (!D2RL::HasItemServiceField(ItemApi, D2RL::ItemServiceDetailsFieldEnd) || !ItemApi->getItemDetails)
		Say(false, "Perfect Frame: this D2RLoader has no item details: stats are read from the game's stat lists and runewords are found by name");
	Base = context->exeBase;
	if (!Base || !Locate()) {
		Say(true, "Perfect Frame %s: inactive, this game build isn't supported (Steam 3.3.93787 / 3.3.94995 are)", PluginVersion);
		return true;
	}
	if (HMODULE core = GetModuleHandleW(L"D2RCore.dll")) ReadStat = reinterpret_cast<StatFn>(reinterpret_cast<void*>(GetProcAddress(core, "ReadWideUnitStat")));
	if (!ReadStat) Say(true, "Perfect Frame: D2RCore's ReadWideUnitStat not found; defense is not checked");

	if (!Context->InstallInlineHook(Game::PanelDraw, Game::PanelDrawBytes, sizeof(Game::PanelDrawBytes), reinterpret_cast<void*>(&HookPanelDraw), reinterpret_cast<void**>(&PanelDraw))
	    || !PanelDraw) {
		Say(true, "Perfect Frame %s: inactive, the tooltip panel could not be hooked", PluginVersion);
		return true;
	}
	const Events::ItemTooltipListener tooltip {
		.structSize = Events::ItemTooltipListenerSize,
		.region     = Events::ItemTooltipRegion::Description,
		.position   = Events::ItemTooltipPosition::Bottom,
		.anchor     = Events::ItemTooltipAnchor::None,
		.fallback   = Events::ItemTooltipFallback::Omit,
		.callback   = OnTooltip,
	};
	Events::ListenerHandle handle = Events::InvalidHandle;
	if (EventApi->registerItemTooltipListener(Context, &tooltip, &handle) != Events::Result::Success) {
		Say(true, "Perfect Frame %s: inactive, could not follow item tooltips", PluginVersion);
		return true;
	}
	if (!SwapCompareSlot()) Say(true, "Perfect Frame: the comparison texts could not be followed; compared items get no frame");
	if (const char* problem = HookTooltipSlot()) Say(!Tooltip, "Perfect Frame: item texts: %s", problem);
	auto command = D2RL::MakeConsoleCommand("perfect-frame", OnCommand, "Perfect Frame: why the last hovered item is perfect or not.");
	if (auto* api = context->GetApi(); !api->registerConsoleCommand || !api->registerConsoleCommand(context, &command)) Say(true, "Perfect Frame: could not register the perfect-frame command");
	// The tables are read on the game thread once a game is loaded.
	Running = true;
	Ticker  = std::thread([] {
        while (Running && !Ready) {
            (void)Threads->runOnGameThread(Context, BuildTables, nullptr);
            for (int i = 0; i < 20 && Running && !Ready; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
	Say(false, "Perfect Frame %s: active (uniques %s, sets %s, runewords %s)", PluginVersion, Config.uniques ? "on" : "off", Config.sets ? "on" : "off",
	    Config.runewords ? "on" : "off");
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	Running = false;
	if (Ticker.joinable()) Ticker.join();
}
