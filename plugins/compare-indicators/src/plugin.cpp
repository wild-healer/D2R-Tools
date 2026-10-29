// Compare Indicators: while the game compares a hovered unique, set item or
// runeword with another copy of the same item (the comparison tooltip beside
// it), each of its lines that can roll ends with an indicator against that
// copy's roll:
//
//   (perfect +x)       the best roll, x better than the copy's
//   (perfect)          the best roll, and the copy has it too
//   (better +x)        better than the copy's, not the best
//   (same)             the copy's roll
//   (worse -x)         worse than the copy's
//   (lost perfect -x)  worse than the copy's, which is the best roll (red)
//
// The copy is whatever item the comparison shows: the worn one the game picks,
// or the one a plugin puts there instead (Grail On!'s grail copy).
//
// How (Steam 3.3.93787 / 94995 RVAs, from the Grail On! roll markers):
//   - Rolls: the item's own rows (UniqueItems, SetItems, Runes) say which
//     properties roll and their best value, read from the game's compiled
//     tables, so modded tables count. A property rolls when its min and max
//     differ (fixed-range functions such as charges and procs don't count).
//     Every property on the same stat adds up (all resistances plus fire
//     resistance). The items' values come from their own magic stat lists.
//   - The comparison: InventoryGridWidget::ShowItemTooltip (0x2C3C30, called
//     only from 0x2C3AB0, a call redirected here, so a plugin hooking the
//     function itself still runs inside) builds the comparison texts for the
//     hovered item's body locations (0x36ED10) before its own text. Every item
//     text built meanwhile for another item is a compared item: the game's
//     (D2RCore BuildComparedItemTooltipWithStatRanges, thunk pointer
//     0x3E2AF60, also followed) or a plugin's own. It shows when the widget
//     compares (+0x631), the game's check at 0x2C3EE8 allows it, and Shift is
//     held (0x120A100(0x10)) or the widget always compares (+0x632), the
//     conditions the function checks at 0x2C3EC8.
//   - The text: the item tooltip text pointer (exe thunk pointer 0x3E2AF58,
//     D2RCore BuildItemTooltipWithStatRanges) is followed: a plugin that swapped
//     it first gets its own copy of D2RCore's pointer pointed here, so the
//     texts it builds are seen too. While the hovered item's text is built,
//     each stat line the stat line loop appends is recorded with its indicator
//     (D2RCore's loop: its strcat slot 0x708FC8; the game's own loop: the
//     strcat_s call at 0x2DACBE). The indicators are put at the end of their
//     lines in the finished text, so a plugin that rewrites the lines on the
//     way (Item Roll Ranges) keeps them. Enhanced Damage, Enhanced Defense and
//     Socketed lines are written outside the loop and found in the text by
//     their words; the defense line by its label.

#include <D2RLPlugin/api.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace {

namespace Items  = D2RL::Items;
namespace Tables = D2RL::DataTables;

constexpr const char* PluginId      = "compare-indicators";
constexpr const char* PluginVersion = "1.0.0";

const D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = PluginId,
	.name        = "Compare Indicators",
	.version     = PluginVersion,
	.author      = "wild-healer",
	.description = "Better, worse and perfect indicators on the rolled lines of uniques, set items and runewords.",
	.flags       = D2RL::PluginFlags::Client | D2RL::PluginFlags::NativeHooks,
};

struct Settings {
	bool enabled = true, uniques = true, sets = true, runewords = true;
	bool anyItems = true;  // indicators against any compared item, whatever its rarity
} Config;

const D2RL::PluginContext*       Context  = nullptr;
std::uintptr_t                   Base     = 0;
const D2RL::InventoryService*    InventoryApi = nullptr;
const D2RL::ThreadService*       Threads  = nullptr;
const D2RL::DataTableService*    TableApi = nullptr;
const D2RL::LocalizationService* TextApi  = nullptr;

void Say(bool warn, const char* format, ...) noexcept {
	if (!Context) return;
	char    line[4096] {};
	va_list args;
	va_start(args, format);
	std::vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	warn ? Context->LogWarn(line) : Context->LogInfo(line);
}

// Also on the game console (for the compare-indicators command).
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
constexpr std::uint64_t GetUnit = 0x9A5D0; // client unit (id, type)
constexpr std::uint8_t  GetUnitBytes[] { 0x4C, 0x63, 0xCA, 0x48, 0x8D, 0x05 };
constexpr std::uint64_t CompareSlot = 0x3E2AF60; // thunk pointer: a comparison text (GameString* out, int bodyLocation)
constexpr std::uint64_t TooltipSlot = 0x3E2AF58; // thunk pointer: an item's tooltip text (out, unit, unit, item, 0, 1, 0, 0, scratch)
constexpr std::uint64_t Player = 0x9A480, PlayerIndex = 0x8B2D0, Option = 0xCE500, Pet = 0x13ABC0, Inventory = 0x34A360, ItemAt = 0x3886D0;
constexpr std::uint8_t  PlayerBytes[] { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x83, 0xF9 };
constexpr std::uint8_t  PlayerIndexBytes[] { 0x8B, 0x05, 0x2E, 0x84, 0x99, 0x02, 0xC3 };
constexpr std::uint8_t  OptionBytes[] { 0x48, 0x63, 0xC1, 0x48, 0x8D, 0x0D, 0x96, 0xC8, 0x95, 0x02, 0x0F, 0xB6 };
constexpr std::uint8_t  PetBytes[] { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48 };
constexpr std::uint8_t  InventoryBytes[] { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B };
constexpr std::uint64_t FirstItem = 0x388C10, NextItem = 0x38ABA0; // an inventory's first item, the next one
constexpr std::uint8_t  FirstItemBytes[] { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x74, 0x2E, 0x81, 0x39 };
constexpr std::uint8_t  NextItemBytes[] { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x75, 0x10, 0x88, 0x4C };
constexpr std::uint8_t  ItemAtBytes[] { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x63 };
constexpr std::uint64_t BodyLocs = 0x36ED10; // an item's body locations (item, u8* first, u8* second)
constexpr std::uint8_t  BodyLocsBytes[] { 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x20 };
constexpr std::uint64_t ShowTooltip = 0x2C3C30, ShowTooltipCall = 0x2C3AB0; // InventoryGridWidget::ShowItemTooltip(widget, item), its one caller
constexpr std::uint8_t  ShowTooltipCallBytes[] { 0xE8, 0x7B, 0x01, 0x00, 0x00 };
// ShowItemTooltip: whether the comparison boxes show (widget+0x631, the check
// 0x8461C0()->0x11C860(hash), Shift 0x120A100(0x10) or widget+0x632).
constexpr std::uint64_t ShownCheck = 0x2C3EC8;
constexpr std::uint8_t  ShownCheckBytes[] { 0x80, 0xBF, 0x31, 0x06, 0x00, 0x00, 0x00, 0x0F, 0x84, 0xBA, 0x00, 0x00, 0x00, 0x48, 0x83, 0x7D, 0x80, 0x00, 0x77, 0x0C, 0x48, 0x83, 0x7C,
	                                    0x24, 0x58, 0x00, 0x0F, 0x86, 0xA7, 0x00, 0x00, 0x00, 0xE8, 0xD3, 0x22, 0x58, 0x00, 0x48, 0xBA, 0x53, 0x26, 0x06, 0x62, 0x5E, 0x05, 0x82,
	                                    0x01, 0x48, 0x8B, 0xC8, 0xE8, 0x61, 0x89, 0xE5, 0xFF, 0x84, 0xC0, 0x0F, 0x85, 0x88, 0x00, 0x00, 0x00, 0xB9, 0x10, 0x00, 0x00, 0x00, 0xE8,
	                                    0xEF, 0x61, 0xF4, 0x00, 0x85, 0xC0, 0x75, 0x08, 0x38, 0x87, 0x32, 0x06, 0x00, 0x00, 0x74, 0x72 };
constexpr std::uint64_t Overlay = 0x8461C0, HasFlag = 0x11C860, KeyDown = 0x120A100;
constexpr std::uint64_t NoCompareFlag = 0x182055E62062653;
constexpr std::ptrdiff_t Compares = 0x631, AlwaysCompares = 0x632;
constexpr std::uint64_t LineLoop = 0x2DA760; // stat line loop (D2RCore replaces it with a jump)
constexpr std::uint64_t LineCat  = 0x2DACBE; // stat line loop: strcat_s(text, size, line), r14d = stat, rbp = the loop's frame
constexpr std::uint8_t  LineCatBytes[] { 0xE8, 0xDD, 0xC9, 0xF4, 0x00 };
constexpr std::uint64_t Strcat = 0x12276A0, Reserve = 0x76210; // strcat_s; GameString reserve (string, size)
// The stat line loop's frame: the entry index, the entry count, and the
// entries of the current stat {u16 layer, u16, i32 value}.
constexpr std::ptrdiff_t LineIndex = -0x80, LineCount = -0x68, LineEntries = 0x140;

constexpr std::ptrdiff_t UnitId = 0x08, UnitMode = 0x0C, UnitData = 0x10, DataQuality = 0x00, DataFlags = 0x18, DataFile = 0x34;
constexpr std::uint32_t  Ethereal = 0x400000, Identified = 0x10, Runeword = 0x4000000;
constexpr int            ItemUnit = 4, SetQuality = 5, UniqueQuality = 7, Worn = 1; // unit mode 1: on a body location
} // namespace Game

using GetUnitFn  = void* (*)(std::uint32_t id, int type) noexcept;
using BodyLocsFn = void (*)(void* item, std::uint8_t* first, std::uint8_t* second) noexcept;
using StatFn     = int (*)(void* unit, int stat, int layer) noexcept; // D2RCore ReadWideUnitStat
using StrcatFn   = int (*)(char*, std::uint64_t, const char*) noexcept;

StatFn   ReadStat {};
StrcatFn Strcat {};

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
	bool              ed {};     // an Enhanced Defense property
	int               code = -1; // its base (Items row), uniques and sets
};

std::mutex                              Lock; // guards RuneByName
std::vector<RowProps>                   Uniques, Sets, Runewords; // by row; fixed once Ready
std::vector<std::string>                RuneKeys;   // Runes row -> its key ("Runeword1")
std::map<std::string, std::vector<int>> RuneByName; // a runeword's name -> its rows (built on first use)
std::map<std::uint32_t, int>            BaseByCode; // Items code -> row
std::vector<int>                        MaxDefense, MinDefense; // by Items row, 0 for none
std::vector<int>                        MaxSockets; // by Items row (gemsockets), -1 when not known
std::string DefenseLabel, EdLabel, EdefLabel, SocketLabel; // "Defense:", "Enhanced Damage", "Enhanced Defense", "Socketed"
std::atomic<bool> Ready {}, Running {};
std::thread       Ticker;

constexpr std::uint32_t ItemRowSize = 448, UniqueRowSize = 348, SetRowSize = 460;
constexpr int ItemCode = 128, ItemMinDefense = 212, ItemMaxDefense = 216;
constexpr int UniqueCode = 40, SetCode = 40, RowProps0 = 152, UniquePropCount = 12, SetPropCount = 9, RunePropCount = 7;
constexpr int DefenseStat = 31, EdefStat = 16, EdStat = 17, SocketsStat = 194;

// Where Properties keeps func1, stat1 and val1: found from rows every build
// has (ac, str, dmg%, res-all, ama, sor), not assumed.
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

// Where Runes keeps its 7 properties {i32 prop, par, min, max}: vanilla's
// first row (Ancient's Pledge: res-cold 30, res-all 13, ac% 50, dmg-to-mana
// 10) first, else the offset at which every row's 7 entries are a property or
// -1 (unused), with the most properties that have a value.
int FindRuneProps(const unsigned char* rows, std::uint32_t count, std::uint32_t size, std::uint32_t propCount) {
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
		if (ok && used > bestUsed && empty > 0) best = o, bestUsed = used;
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
		for (const int stat : entry.stats)
			if (stat == EdefStat && high > 0) out.ed = true;
		out.props.push_back(entry);
	}
	return out;
}

bool GetTable(Tables::TableId id, Tables::TableView& view) {
	view = { .structSize = Tables::TableViewSize };
	return TableApi->getTable(Context, Tables::Bank::Rotw, id, &view) == Tables::Result::Success && view.rows;
}

std::vector<std::string> LineWords(std::string_view line);

// A string's words without its format codes (%d, %+d, %%), from the first key
// the game has; the fallback when none.
std::string LabelOf(std::initializer_list<const char*> keys, const char* fallback, bool toColon = false) {
	for (const char* key : keys) {
		char label[128] {};
		if (!TextApi || TextApi->getStringByKey(Context, key, label, sizeof(label), nullptr) != D2RL::Localization::Result::Success) continue;
		std::string text;
		for (const char* c = label; *c; ++c) {
			if (toColon && *c == ':') {
				text += ':';
				break;
			}
			if (*c != '%') {
				text += *c;
				continue;
			}
			++c;
			while (*c == '+' || *c == '-' || std::isdigit(static_cast<unsigned char>(*c))) ++c;
			if (!*c) break;
		}
		if (!LineWords(text).empty()) return text;
	}
	return fallback;
}

void __cdecl BuildTables(const D2RL::PluginContext*, void*) noexcept {
	if (Ready) return;
	Tables::TableView props, items, uniques, sets, runes;
	if (!GetTable(Tables::TableId::Properties, props) || !GetTable(Tables::TableId::Items, items) || !GetTable(Tables::TableId::UniqueItems, uniques)
	    || !GetTable(Tables::TableId::SetItems, sets))
		return; // tables not loaded yet: the ticker tries again
	const bool  haveRunes = GetTable(Tables::TableId::Runes, runes);
	const auto* rows      = static_cast<const unsigned char*>(props.rows);
	const auto  layout    = FindPropLayout(rows, props.rowCount, props.rowSize);
	if (layout.func < 0 || layout.stat < 0 || layout.value < 0) {
		Say(true, "Compare Indicators: Properties rows (%u of %u bytes) are laid out differently; inactive", props.rowCount, props.rowSize);
		Running = false;
		return;
	}
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
		else Say(true, "Compare Indicators: the Items sockets column was not found; socket rolls are taken as listed");
	} else
		Say(true, "Compare Indicators: Items rows are %u bytes (expected %u); defense gets no indicator", items.rowSize, ItemRowSize);
	const auto readRows = [&](std::vector<RowProps>& out, const Tables::TableView& table, std::uint32_t expected, int count, int codeAt, const char* name) {
		out.clear();
		if (table.rowSize != expected) {
			Say(true, "Compare Indicators: %s rows are %u bytes (expected %u); no indicators on them", name, table.rowSize, expected);
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
		if (at < 0) Say(true, "Compare Indicators: the Runes table (%u rows of %u bytes) is laid out differently; no indicators on runewords", runes.rowCount, runes.rowSize);
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
				Say(true, "Compare Indicators: runeword names not found in the Runes rows; no indicators on runewords");
		}
	} else
		Say(true, "Compare Indicators: no Runes table; no indicators on runewords");
	// The lines found by their words, in the game's language.
	DefenseLabel = LabelOf({ "ItemStats1h", "strItemStats1h" }, "Defense:", true);
	EdLabel      = LabelOf({ "strModEnhancedDamage", "ModStr2a" }, "Enhanced Damage");
	EdefLabel    = LabelOf({ "Modstr2v", "ModStr2v" }, "Enhanced Defense");
	SocketLabel  = LabelOf({ "Socketable" }, "Socketed");
	Ready        = true;
	Say(false, "Compare Indicators: %zu uniques, %zu set items, %zu runewords read; labels \"%s\", \"%s\", \"%s\", \"%s\"", Uniques.size(), Sets.size(), Runewords.size(),
	    DefenseLabel.c_str(), EdLabel.c_str(), EdefLabel.c_str(), SocketLabel.c_str());
}

// ---- items (UI thread) ----

// Life, mana and stamina keep 8 fraction bits.
int Display(int stat, int value) { return stat >= 6 && stat <= 11 ? value >> 8 : value; }

// Layers only matter for skills and auras.
bool Layered(int stat) { return stat == 107 || stat == 97 || stat == 151 || stat == 188 || stat == 83; }

struct StatKey {
	int  stat, layer;
	auto operator<=>(const StatKey&) const = default;
};
StatKey KeyOf(int stat, int layer) { return { stat, Layered(stat) ? layer : 0 }; }

// A stat of a row: every property on it added up at its worst and its best,
// and whether any of them rolls.
struct Range {
	int  worst {}, best {};
	bool rolls {};
};
using Ranges = std::map<StatKey, Range>;

Ranges RangesOf(const RowProps& row) {
	Ranges ranges;
	for (const auto& prop : row.props)
		for (const int stat : prop.stats) {
			if (stat < 0) continue;
			auto& range = ranges[KeyOf(stat, prop.layer)];
			range.worst += prop.low;
			range.best += prop.high;
			range.rolls = range.rolls || prop.rolls;
		}
	return ranges;
}

// Whether a value is the best: past best, on the best's side of worst.
bool IsBest(const Range& range, int value) { return range.best >= range.worst ? value >= range.best : value <= range.best; }

int Better(const Range& range, int mine, int theirs) {
	if (mine == theirs) return 0;
	return (range.best >= range.worst) == (mine > theirs) ? 1 : -1;
}

// An item's values: its own magic stat lists, read like the game's stat
// lookup (0x2FA060) and list search (0x2F5A00): the unit's stat list at +0x88,
// its lists chained from +0x90 (and +0x98) by +0x68, each with its state at
// +0x20, flags at +0x1C (0x40 = magic properties) and stats at +0x30 {pointer,
// count}. D2RCore widens the stats to 16 bytes {u32 layer, u32 stat, i32
// value, u32}. Unlike the unit's totals they keep Enhanced Damage, and a worn
// item's hold no per-level bonuses of its wearer.
struct Values {
	void*                  unit {};
	std::map<StatKey, int> have;  // the item's own lists
	std::map<StatKey, int> total; // with what is socketed in it (runes, gems, jewels)
	bool                   lists {};
	int                    fillers {}; // socketed items read

	// A stat's value; false when the item doesn't have it.
	bool Get(int stat, int layer, int& value, bool listsOnly = false) const {
		if (const auto found = have.find(KeyOf(stat, layer)); found != have.end()) return value = Display(stat, found->second), true;
		if (listsOnly || !ReadStat || !unit) return false;
		const int read = ReadStat(unit, stat, Layered(stat) ? layer : 0);
		if (!read) return false;
		value = Display(stat, read);
		return true;
	}

	// A stat's value as the tooltip shows it: the item's lists and its
	// socketed items', else the item's totals.
	bool Shown(int stat, int layer, int& value) const {
		if (const auto found = total.find(KeyOf(stat, layer)); found != total.end()) return value = Display(stat, found->second), true;
		return Get(stat, layer, value);
	}
};

// Adds a unit's magic stat lists (all its lists with anyFlags) to values;
// how many stats were read.
int ReadLists(const void* unit, std::map<StatKey, int>& values, bool anyFlags = false) {
	const auto* ex = unit ? Field<const unsigned char*>(unit, 0x88) : nullptr;
	if (!ex || Field<std::int32_t>(ex, 0x1C) >= 0) return 0;
	int read = 0;
	for (const std::ptrdiff_t chain : { 0x90, 0x98 }) {
		int guard = 0;
		for (const auto* list = Field<const unsigned char*>(ex, chain); list && guard < 32; list = Field<const unsigned char*>(list, 0x68), ++guard) {
			const auto  flags   = Field<std::uint32_t>(list, 0x1C), state = Field<std::uint32_t>(list, 0x20);
			const auto* entries = Field<const unsigned char*>(list, 0x30);
			const auto  count   = Field<std::uint64_t>(list, 0x38);
			if (state != 0 || (!anyFlags && !(flags & 0x40)) || (anyFlags && static_cast<std::int32_t>(flags) < 0) || !entries || count > 512) continue;
			for (std::uint64_t i = 0; i < count; ++i) {
				const auto at    = static_cast<std::ptrdiff_t>(i * 16);
				const auto layer = Field<std::uint32_t>(entries, at), stat = Field<std::uint32_t>(entries, at + 4);
				if (stat > 0xFFFF || layer > 0xFFFF) continue;
				values[KeyOf(static_cast<int>(stat), static_cast<int>(layer))] += Field<std::int32_t>(entries, at + 8);
				++read;
			}
		}
	}
	return read;
}

// What a unit's lists hold, for the log: each list's flags, state and stats.
std::string DescribeLists(const void* unit) {
	std::string text;
	const auto* ex = unit ? Field<const unsigned char*>(unit, 0x88) : nullptr;
	if (!ex) return " no lists";
	for (const std::ptrdiff_t chain : { 0x90, 0x98 }) {
		int guard = 0;
		for (const auto* list = Field<const unsigned char*>(ex, chain); list && guard < 32; list = Field<const unsigned char*>(list, 0x68), ++guard) {
			char head[64] {};
			std::snprintf(head, sizeof(head), " [+%X flags %X state %u:", static_cast<unsigned>(chain), Field<std::uint32_t>(list, 0x1C), Field<std::uint32_t>(list, 0x20));
			text += head;
			const auto* entries = Field<const unsigned char*>(list, 0x30);
			const auto  count   = Field<std::uint64_t>(list, 0x38);
			for (std::uint64_t i = 0; entries && count <= 64 && i < count; ++i)
				text += " " + std::to_string(Field<std::uint32_t>(entries, static_cast<std::ptrdiff_t>(i * 16) + 4)) + "=" + std::to_string(Field<std::int32_t>(entries, static_cast<std::ptrdiff_t>(i * 16) + 8));
			text += "]";
		}
	}
	return text;
}

using InventoryFn = void* (*)(void* unit, const char* file, int line) noexcept;
using ItemFn      = void* (*)(void* inventoryOrItem) noexcept;

// The items socketed in an item: its own inventory's.
std::vector<void*> Fillers(void* item) {
	std::vector<void*> fillers;
	void* inventory = item ? reinterpret_cast<InventoryFn>(Base + Game::Inventory)(item, "compare-indicators", 0) : nullptr;
	if (!inventory) return fillers;
	for (void* child = reinterpret_cast<ItemFn>(Base + Game::FirstItem)(inventory); child && fillers.size() < 8; child = reinterpret_cast<ItemFn>(Base + Game::NextItem)(child))
		if (child != item && Field<std::uint32_t>(child, 0) == Game::ItemUnit) fillers.push_back(child);
	return fillers;
}

// An item's values: its own magic stat lists, read like the game's stat
// lookup (0x2FA060) and list search (0x2F5A00): the unit's stat list at +0x88,
// its lists chained from +0x90 (and +0x98) by +0x68, each with its state at
// +0x20, flags at +0x1C (0x40 = magic properties) and stats at +0x30 {pointer,
// count}. D2RCore widens the stats to 16 bytes {u32 layer, u32 stat, i32
// value, u32}. Unlike the unit's totals they keep Enhanced Damage, and a worn
// item's hold no per-level bonuses of its wearer. The totals add the lists of
// the items socketed in it (the item's inventory).
Values ValuesOf(void* unit) {
	Values values { .unit = unit };
	values.lists = ReadLists(unit, values.have) > 0;
	values.total = values.have;
	for (void* filler : Fillers(unit)) {
		++values.fillers;
		if (!ReadLists(filler, values.total)) (void)ReadLists(filler, values.total, true);
	}
	return values;
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

const void* DataOf(const void* item) { return item && Field<std::uint32_t>(item, 0) == Game::ItemUnit ? Field<const void*>(item, Game::UnitData) : nullptr; }
bool        Identified(const void* item) { const void* data = DataOf(item); return data && (Field<std::uint32_t>(data, Game::DataFlags) & Game::Identified); }
bool        Ethereal(const void* item) { const void* data = DataOf(item); return data && (Field<std::uint32_t>(data, Game::DataFlags) & Game::Ethereal); }
bool        Worn(const void* item) { return item && Field<std::uint32_t>(item, Game::UnitMode) == Game::Worn; }

std::string StringById(std::uint32_t id) {
	char text[256] {};
	if (!TextApi || TextApi->getStringById(Context, id, text, sizeof(text), nullptr) != D2RL::Localization::Result::Success) return {};
	return text;
}

// The Runes rows of a runeword's name: two when two runewords share a name.
std::vector<int> RunewordRows(std::uint32_t nameId) {
	std::lock_guard guard(Lock);
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

struct FindById {
	std::uint32_t   id;
	Items::ItemInfo info;
	bool            found;
};

// A runeword's name string: from D2RLoader's info on the player's item with
// the unit's id (its first prefix), 0 when not the player's.
std::uint32_t RunewordName(void* item) {
	FindById           find { .id = Field<std::uint32_t>(item, Game::UnitId) };
	D2RL::PlayerHandle player {};
	if (!InventoryApi || InventoryApi->getLocalPlayer(Context, &player) != D2RL::Inventory::Result::Success) return 0;
	const D2RL::Inventory::ItemFilter filter { .structSize = D2RL::Inventory::ItemFilterSize, .containerMask = 0xFFFFFFFFu };
	(void)InventoryApi->forEachInventoryItem(Context, player, &filter, [](const D2RL::PluginContext*, const Items::ItemInfo* info, void* user) noexcept {
		auto* find = static_cast<FindById*>(user);
		if (info->runtimeId != find->id) return D2RL::Inventory::IterationAction::Continue;
		find->info  = *info;
		find->found = true;
		return D2RL::Inventory::IterationAction::Stop;
	}, &find);
	return find.found ? find.info.prefixIds[0] : 0;
}

// What an item is: its kind (unique, set item, runeword), its row (a
// runeword: its name string), and the row whose rolls it has.
enum Kind { NoKind, UniqueKind, SetKind, RunewordKind };
constexpr const char* KindNames[] { "item", "unique", "set item", "runeword" };
struct Ident {
	Kind            kind = NoKind;
	std::uint32_t   key {}; // row, or a runeword's name string
	int             row = -1;
	const RowProps* props {};
	bool operator==(const Ident& other) const { return kind == other.kind && key == other.key; }
};

Ident IdentOf(void* item, const Values& values) {
	Ident       ident;
	const void* data = DataOf(item);
	if (!data) return ident;
	const auto quality = Field<std::uint32_t>(data, Game::DataQuality);
	const auto file    = Field<std::int32_t>(data, Game::DataFile);
	if (quality == Game::UniqueQuality && Config.uniques && file >= 0 && file < static_cast<int>(Uniques.size()))
		return { UniqueKind, static_cast<std::uint32_t>(file), file, &Uniques[file] };
	if (quality == Game::SetQuality && Config.sets && file >= 0 && file < static_cast<int>(Sets.size())) return { SetKind, static_cast<std::uint32_t>(file), file, &Sets[file] };
	if (!Config.runewords || !(Field<std::uint32_t>(data, Game::DataFlags) & Game::Runeword) || Runewords.empty()) return ident;
	const std::uint32_t name = RunewordName(item);
	const auto          rows = name ? RunewordRows(name) : std::vector<int> {};
	// A shared name: the row with the most of the item's stats.
	int best = -1, bestCount = -1;
	for (const int row : rows) {
		if (row < 0 || row >= static_cast<int>(Runewords.size())) continue;
		int count = 0;
		for (const auto& [key, range] : RangesOf(Runewords[row])) count += values.have.count(key) ? 1 : 0;
		if (count > bestCount) best = row, bestCount = count;
	}
	if (best < 0) return ident;
	return { RunewordKind, name, best, &Runewords[best] };
}

void* LocalPlayer() {
	using IndexFn  = std::uint32_t (*)() noexcept;
	using PlayerFn = void* (*)(std::uint32_t) noexcept;
	return reinterpret_cast<PlayerFn>(Base + Game::Player)(reinterpret_cast<IndexFn>(Base + Game::PlayerIndex)());
}

// The item the game compares with at a body location: the player's, or the
// mercenary's for the head, body and hands when the game's option 0x1E says
// so (as 0x2BCE14..0x2BCEA1 decide).
void* ComparedItem(int location) {
	using OptionFn    = std::uint8_t (*)(int) noexcept;
	using PetFn       = int (*)(void* unit, int type, int index) noexcept;
	using InventoryFn = void* (*)(void* unit, const char* file, int line) noexcept;
	using AtFn        = void* (*)(void* inventory, int location) noexcept;
	void* player      = LocalPlayer();
	if (!player || location < 1 || location > 10) return nullptr;
	void* owner = player;
	if (reinterpret_cast<OptionFn>(Base + Game::Option)(0x1E) && (location == 1 || (location >= 3 && location <= 5))) {
		const int merc = reinterpret_cast<PetFn>(Base + Game::Pet)(player, 7, 0);
		owner          = merc == -1 ? nullptr : reinterpret_cast<GetUnitFn>(Base + Game::GetUnit)(static_cast<std::uint32_t>(merc), 1);
	}
	void* inventory = owner ? reinterpret_cast<InventoryFn>(Base + Game::Inventory)(owner, "compare-indicators", 0) : nullptr;
	return inventory ? reinterpret_cast<AtFn>(Base + Game::ItemAt)(inventory, location) : nullptr;
}

// ---- which compared items count (weapons and shields) ----

using HandednessFn = int (*)(void* unit, void* item, std::uint8_t policy) noexcept;                            // D2RCore GetItemHandednessForUnit
using PairFn       = bool (*)(void* unit, void* first, void* second, int bodyLocation, std::uint32_t flags) noexcept; // D2RCore CanEquipItemPair
HandednessFn Handedness {};
PairFn       CanPair {};

// An item's first body location (4 or 5 for anything held in the hands), 0
// when none.
int FirstLocation(void* item) {
	std::uint8_t first = 0, second = 0;
	if (item) reinterpret_cast<BodyLocsFn>(Base + Game::BodyLocs)(item, &first, &second);
	return first;
}

// What a hand item is: a weapon (4) or a shield or off-hand with a defense (5);
// other items, their first body location.
int KindOf(void* item) {
	const int location = FirstLocation(item);
	if (location != 4 && location != 5) return location;
	return BaseDefense(item) >= 0 ? 5 : 4;
}

bool InHands(int location) { return location == 4 || location == 5; }
bool TwoHanded(void* item) { return Handedness && Handedness(LocalPlayer(), item, 0) == 2; }

// Whether the player could hold two of the hovered weapon (dual wield): the
// game's pair rule with the character's off-hand restriction, else a barbarian.
bool DualWields(void* weapon) {
	void* player = LocalPlayer();
	if (!player) return false;
	// Barbarians dual wield any one-handed weapon, assassins two claws (the
	// game's pair rule says which).
	const auto playerClass = Field<std::uint32_t>(player, 4);
	if (playerClass == 4) return true;
	return playerClass == 6 && CanPair && CanPair(player, weapon, weapon, 5, 1);
}

// Whether a compared item is what the hovered one would replace. Weapons and
// shields: the item of the same kind; both hands for a two-handed item (either
// side), or for a one-handed weapon the character can dual wield. Everything
// else: every compared item.
bool Replaces(void* hovered, void* compared) {
	const int mine = KindOf(hovered), theirs = KindOf(compared);
	if (!InHands(mine) || !InHands(theirs) || mine == theirs) return true;
	const bool twoHands = TwoHanded(hovered) || TwoHanded(compared), dual = !twoHands && mine == 4 && DualWields(hovered);
	return twoHands || dual;
}

// ---- indicators ----

// Tooltip text in memory takes U+E07E and a colour character (what D2RCore
// writes for its socket counts), not the strings' "ÿc", which shows as is.
#define COLOR "\xEE\x81\xBE"
constexpr const char *ColorBetter = COLOR "2", *ColorWorse = COLOR "1", *ColorSame = COLOR "5", *ColorPerfect = COLOR "4", *ColorStat = COLOR "3";

// Against the compared copy's roll: a perfect roll says by how much it beats
// the copy; losing the copy's perfect roll says so (the game's font draws no
// strikethrough).
std::string Indicator(int better, bool perfect, bool theirsPerfect, int difference) {
	char text[96] {};
	if (perfect && better > 0) std::snprintf(text, sizeof(text), " %s(perfect +%d)%s", ColorPerfect, std::abs(difference), ColorStat);
	else if (perfect) std::snprintf(text, sizeof(text), " %s(perfect)%s", ColorPerfect, ColorStat);
	else if (better > 0) std::snprintf(text, sizeof(text), " %s(better +%d)%s", ColorBetter, std::abs(difference), ColorStat);
	else if (better < 0 && theirsPerfect) std::snprintf(text, sizeof(text), " %s(lost perfect -%d)%s", ColorWorse, std::abs(difference), ColorStat);
	else if (better < 0) std::snprintf(text, sizeof(text), " %s(worse -%d)%s", ColorWorse, std::abs(difference), ColorStat);
	else std::snprintf(text, sizeof(text), " %s(same)%s", ColorSame, ColorStat);
	return text;
}

// The tooltip being built for a hovered item (UI thread).
// What a text is marked against: another copy of the same item (its rolls),
// another item (its stats), or, in the compared item's own text, the hovered
// item (the stats it doesn't have).
enum MarkMode { SameItem, OtherItem, LostStats };

struct Marking {
	MarkMode mode = SameItem;
	void*  item {};
	void*  copy {}; // the compared copy
	Ident  ident;
	Ranges ranges;
	Values mine, theirs;
	// The marked lines: each line as the stat line loop wrote it and its
	// indicator. They go into the finished text afterwards, so that a plugin
	// rewriting the lines on the way (Item Roll Ranges) keeps them.
	std::vector<std::pair<std::string, std::string>> lines;
	mutable std::string notes; // values that differ from the rolls (socketed items), for the log
};
thread_local Marking* Marks {};
thread_local int      CompareDepth {}; // inside a comparison text: the worn item's own box

// The indicator for a value of a stat that rolls, or "".
// mine/theirs: the values shown (socketed items included), which the
// difference is about; the rolls: the items' own, which perfect is about.
std::string IndicatorFor(const Marking& mark, int stat, int layer, int mine, int theirs, int rollMine, int rollTheirs) {
	const auto found = mark.ranges.find(KeyOf(stat, layer));
	if (found == mark.ranges.end() || !found->second.rolls) return {};
	const Range& range = found->second;
	return Indicator(Better(range, mine, theirs), IsBest(range, rollMine), IsBest(range, rollTheirs), mine - theirs);
}

// Stats where less is better (Requirements -x%).
bool LowerIsBetter(int stat) { return stat == 91; }

// Against another item's stat: better, worse or the same; new when it has
// none; in the compared item's text, lost when the hovered item has none.
std::string StatIndicator(MarkMode mode, int stat, bool haveMine, int mine, bool haveTheirs, int theirs) {
	char text[96] {};
	if (mode == LostStats) {
		if (haveMine && !haveTheirs) std::snprintf(text, sizeof(text), " %s(lost)%s", ColorWorse, ColorStat);
		return text;
	}
	if (!haveMine) return {};
	if (!haveTheirs) {
		std::snprintf(text, sizeof(text), " %s(new)%s", ColorBetter, ColorStat);
		return text;
	}
	const int difference = mine - theirs, better = difference == 0 ? 0 : (difference > 0) != LowerIsBetter(stat) ? 1 : -1;
	if (better > 0) std::snprintf(text, sizeof(text), " %s(better +%d)%s", ColorBetter, std::abs(difference), ColorStat);
	else if (better < 0) std::snprintf(text, sizeof(text), " %s(worse -%d)%s", ColorWorse, std::abs(difference), ColorStat);
	else std::snprintf(text, sizeof(text), " %s(same)%s", ColorSame, ColorStat);
	return text;
}

// Against another copy of the same item: the roll indicator for a stat that
// rolls (the difference with what is socketed, perfect by the item's own
// roll); else, when the values shown differ (socketed items), better, worse
// or new.
std::string SameItemIndicator(const Marking& mark, int stat, int layer, bool haveMine, int mine, bool haveTheirs, int theirs) {
	if (!haveMine) return {};
	const auto found = mark.ranges.find(KeyOf(stat, layer));
	if (haveTheirs && found != mark.ranges.end() && found->second.rolls) {
		int rollMine = mine, rollTheirs = theirs;
		(void)mark.mine.Get(stat, layer, rollMine, true);
		(void)mark.theirs.Get(stat, layer, rollTheirs, true);
		if ((rollMine != mine || rollTheirs != theirs) && mark.notes.size() < 200)
			mark.notes += " " + std::to_string(stat) + "=" + std::to_string(mine) + "(roll " + std::to_string(rollMine) + ")/" + std::to_string(theirs) + "(roll " + std::to_string(rollTheirs) + ")";
		return IndicatorFor(mark, stat, layer, mine, theirs, rollMine, rollTheirs);
	}
	if (haveTheirs && mine == theirs) return {};
	return StatIndicator(OtherItem, stat, haveMine, mine, haveTheirs, theirs);
}

std::string LineIndicator(const Marking& mark, int stat, int layer) {
	int        mine = 0, theirs = 0;
	const bool haveMine = mark.mine.Shown(stat, layer, mine), haveTheirs = mark.theirs.Shown(stat, layer, theirs);
	if (mark.mode != SameItem) return StatIndicator(mark.mode, stat, haveMine, mine, haveTheirs, theirs);
	return SameItemIndicator(mark, stat, layer, haveMine, mine, haveTheirs, theirs);
}

// Stats whose line is written outside the stat loop (Enhanced Damage,
// Enhanced Defense, sockets): found in the finished text by their words.
constexpr bool TextStat(int stat) { return stat == 16 || stat == 17 || stat == 18 || stat == 194; }

void Record(int stat, int layer, const char* line) {
	Marking* mark = Marks;
	if (!mark || !line || stat < 0 || stat == DefenseStat || TextStat(stat)) return;
	if (auto indicator = LineIndicator(*mark, stat, layer); !indicator.empty()) mark->lines.emplace_back(line, std::move(indicator));
}

// strcat_s from the game's stat line loop, through the jump: the stat is in
// LineStat, frame is the loop's rbp.
thread_local int LineStat = -1;
void             SetLineStat(int stat) noexcept { LineStat = stat; }

int __cdecl MarkLine(char* text, std::uint64_t size, const char* line, const unsigned char* frame) noexcept {
	if (Marks && frame) {
		const auto index = Field<std::int64_t>(frame, Game::LineIndex), count = Field<std::int64_t>(frame, Game::LineCount);
		if (index >= 0 && index < count && count <= 0x1FF) Record(LineStat, Field<std::uint16_t>(frame, Game::LineEntries + index * 8), line);
	}
	return Strcat(text, size, line);
}

void* MakeStub(const std::vector<std::uint8_t>& code) {
	void* stub = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!stub) return nullptr;
	std::memcpy(stub, code.data(), code.size());
	DWORD old = 0;
	return VirtualProtect(stub, 4096, PAGE_EXECUTE_READ, &old) ? stub : nullptr;
}

void Put(std::vector<std::uint8_t>& code, std::uint64_t value) {
	for (int i = 0; i < 8; ++i) code.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}

// The game's loop: its strcat_s call goes to a stub that passes rbp as the
// fourth argument and r14d (the stat) in LineStat, then goes on to MarkLine.
// The call needs a target within 2 GB: a 14-byte absolute jump to the stub in
// int3 padding inside D2R.exe.
bool RedirectLineCat() {
	if (std::memcmp(reinterpret_cast<const void*>(Base + Game::LineCat), Game::LineCatBytes, sizeof(Game::LineCatBytes)) != 0) return false;
	//   mov r9, rbp; push rcx; push rdx; push r8; push r9; mov ecx, r14d; sub rsp, 0x28
	//   mov rax, SetLineStat; call rax; add rsp, 0x28; pop r9; pop r8; pop rdx; pop rcx
	//   mov rax, MarkLine; jmp rax
	// Four pushes keep the stack as aligned as it was at the call.
	std::vector<std::uint8_t> code { 0x49, 0x89, 0xE9, 0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x44, 0x89, 0xF1, 0x48, 0x83, 0xEC, 0x28, 0x48, 0xB8 };
	Put(code, reinterpret_cast<std::uint64_t>(&SetLineStat));
	code.insert(code.end(), { 0xFF, 0xD0, 0x48, 0x83, 0xC4, 0x28, 0x41, 0x59, 0x41, 0x58, 0x5A, 0x59, 0x48, 0xB8 });
	Put(code, reinterpret_cast<std::uint64_t>(&MarkLine));
	code.insert(code.end(), { 0xFF, 0xE0 });
	void* stub = MakeStub(code);
	if (!stub) return false;
	std::array<std::uint8_t, 14> jump { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
	const auto                   target = reinterpret_cast<std::uint64_t>(stub);
	std::memcpy(jump.data() + 6, &target, sizeof(target));
	std::array<std::uint8_t, 16> padding {};
	padding.fill(0xCC);
	int tries = 0;
	for (std::uint64_t start = 0x2D0000; start < 0x1400000 && tries < 16; start += 16) {
		if (std::memcmp(reinterpret_cast<const void*>(Base + start), padding.data(), padding.size()) != 0) continue;
		++tries;
		if (!Context->PatchBytes(start + 1, padding.data(), 14, jump.data(), 14)) continue; // another plugin took it
		return D2RL::PatchCallRel32(Context, Game::LineCat, Game::LineCatBytes, 5, start + 1);
	}
	return false;
}

// D2RCore replaces the game's stat line loop (0x2DA760 jumps to it), so with
// D2RCore the lines are marked in its loop instead: its strcat pointer (a
// D2RCore slot) goes through a stub that passes the caller's r14d (the stat)
// and rdi (the stat entry {u32 layer, u32 stat, i32 value}) when the call is
// the loop's line append, -1 otherwise (D2RCore build of Sep 23, checked).
namespace CoreLine {
constexpr std::uint64_t Slot = 0x708FC8, Site = 0x3E7E45, Return = 0x3E7E63;
constexpr std::uint8_t  SiteBytes[] { 0x48, 0x8B, 0x8C, 0x24, 0xF8, 0x01, 0x00, 0x00, 0x48, 0x8B, 0x94, 0x24, 0x00, 0x02, 0x00, 0x00,
	                                  0x4C, 0x8D, 0x84, 0x24, 0xE0, 0x00, 0x00, 0x00, 0xFF, 0x15, 0x65, 0x11, 0x32, 0x00 };
} // namespace CoreLine

thread_local int                  CoreStat = -1;
thread_local const unsigned char* CoreEntry {};

void SetCoreLine(int stat, const unsigned char* entry) noexcept {
	CoreStat  = stat;
	CoreEntry = entry;
}

int __cdecl MarkCoreLine(char* text, std::uint64_t size, const char* line) noexcept {
	if (Marks && CoreStat >= 0 && CoreEntry && Field<std::uint16_t>(CoreEntry, 4) == CoreStat) Record(CoreStat, Field<std::uint16_t>(CoreEntry, 0), line);
	return Strcat(text, size, line);
}

bool HookCoreLines() {
	HMODULE core = GetModuleHandleW(L"D2RCore.dll");
	if (!core) return false;
	const auto coreBase = reinterpret_cast<std::uintptr_t>(core);
	auto*      slot     = reinterpret_cast<std::uint64_t*>(coreBase + CoreLine::Slot);
	if (std::memcmp(reinterpret_cast<const void*>(coreBase + CoreLine::Site), CoreLine::SiteBytes, sizeof(CoreLine::SiteBytes)) != 0 || *slot != Base + Game::Strcat) return false;
	std::vector<std::uint8_t> code { 0x49, 0xBA }; // mov r10, the loop's return address
	Put(code, coreBase + CoreLine::Return);
	code.insert(code.end(), { 0x4C, 0x39, 0x14, 0x24 });             // cmp [rsp], r10
	code.insert(code.end(), { 0xB8, 0xFF, 0xFF, 0xFF, 0xFF });       // mov eax, -1
	code.insert(code.end(), { 0x41, 0x0F, 0x44, 0xC6 });             // cmove eax, r14d
	code.insert(code.end(), { 0x51, 0x52, 0x41, 0x50, 0x41, 0x51 }); // push rcx, rdx, r8, r9
	code.insert(code.end(), { 0x89, 0xC1, 0x48, 0x89, 0xFA });       // mov ecx, eax; mov rdx, rdi
	code.insert(code.end(), { 0x48, 0x83, 0xEC, 0x28, 0x48, 0xB8 }); // sub rsp, 0x28; mov rax, SetCoreLine
	Put(code, reinterpret_cast<std::uint64_t>(&SetCoreLine));
	code.insert(code.end(), { 0xFF, 0xD0, 0x48, 0x83, 0xC4, 0x28 });             // call rax; add rsp, 0x28
	code.insert(code.end(), { 0x41, 0x59, 0x41, 0x58, 0x5A, 0x59, 0x48, 0xB8 }); // pop r9, r8, rdx, rcx; mov rax, MarkCoreLine
	Put(code, reinterpret_cast<std::uint64_t>(&MarkCoreLine));
	code.insert(code.end(), { 0xFF, 0xE0 }); // jmp rax
	void* stub = MakeStub(code);
	DWORD old  = 0;
	if (!stub || !VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old)) return false;
	const auto original = static_cast<LONG64>(Base + Game::Strcat);
	const bool swapped  = InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(slot), reinterpret_cast<LONG64>(stub), original) == original;
	VirtualProtect(slot, sizeof(*slot), old, &old);
	return swapped;
}

// ---- the finished text ----

struct GameString {
	char*         text;
	std::uint64_t size;
	std::uint64_t capacity; // top bit: the text is in the inline buffer below
	char          buffer[16];
};
using TooltipFn = GameString* (*)(GameString*, void*, void*, void*, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, void*) noexcept;
using CompareFn = GameString* (*)(GameString*, int) noexcept;
using ReserveFn = void (*)(GameString*, std::uint64_t) noexcept;
TooltipFn Tooltip {};
CompareFn Compared {};

void SetText(GameString* text, const std::string& all) {
	reinterpret_cast<ReserveFn>(Base + Game::Reserve)(text, all.size());
	if (!text->text) return;
	std::memcpy(text->text, all.data(), all.size());
	text->text[all.size()] = 0;
	text->size             = all.size();
}

// A line's words (letters only: numbers, ranges and colours left out), to
// find it again in the finished text.
std::vector<std::string> LineWords(std::string_view line) {
	std::vector<std::string> words;
	std::string              word;
	for (std::size_t i = 0; i < line.size(); ++i) {
		const unsigned char c = line[i];
		if (c == 0xEE && i + 3 < line.size()) { // a colour: U+E07E and its character
			i += 3;
			continue;
		}
		if (std::isalpha(c) || c >= 0x80) word += static_cast<char>(std::tolower(c));
		else if (!word.empty()) words.push_back(std::move(word)), word.clear();
	}
	if (!word.empty()) words.push_back(std::move(word));
	return words;
}

// Whether a line's words start with `want` (with the roll ranges shown, Item
// Roll Ranges adds words after the line's own).
bool SameStart(const std::vector<std::string>& words, const std::vector<std::string>& want) {
	return words.size() >= want.size() && std::equal(want.begin(), want.end(), words.begin());
}

// Whether `want` is a run of a line's words.
bool HasRun(const std::vector<std::string>& words, const std::vector<std::string>& want) {
	return !want.empty() && std::search(words.begin(), words.end(), want.begin(), want.end()) != words.end();
}

// A line's numbers, in order.
std::vector<int> LineNumbers(std::string_view line) {
	std::vector<int> numbers;
	for (std::size_t i = 0; i < line.size(); ++i) {
		if (static_cast<unsigned char>(line[i]) == 0xEE && i + 3 < line.size()) {
			i += 3;
			continue;
		}
		if (!std::isdigit(static_cast<unsigned char>(line[i]))) continue;
		int value = 0;
		while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) value = value * 10 + (line[i++] - '0');
		numbers.push_back(value);
	}
	return numbers;
}

// Whether a line goes on past `want` with the per-level note's words (another
// stat's line, not a roll range added after it).
bool Annotated(const std::vector<std::string>& words, const std::vector<std::string>& want) {
	for (std::size_t k = want.size(); k < words.size(); ++k)
		if (words[k] == "based" || words[k] == "level" || words[k] == "per") return true;
	return false;
}

// Puts the recorded indicators at the end of their lines in the finished
// text: the first line not marked yet that has the recorded line's words in
// order (`exact`: as a run). How many were placed.
int PlaceIndicators(GameString* text, const std::vector<std::pair<std::string, std::string>>& marks, bool exact = false) {
	if (!text || !text->text || marks.empty()) return 0;
	const std::string all(text->text, text->size);
	struct Line {
		std::size_t              start, end;
		std::vector<std::string> words;
		std::string              indicator;
	};
	std::vector<Line> lines;
	for (std::size_t start = 0; start <= all.size();) {
		std::size_t end = all.find('\n', start);
		if (end == std::string::npos) end = all.size();
		lines.push_back({ start, end, LineWords(std::string_view(all).substr(start, end - start)), {} });
		start = end + 1;
	}
	int placed = 0;
	for (const auto& [line, indicator] : marks) {
		const auto want = LineWords(line);
		if (want.empty()) continue;
		const auto numbers = LineNumbers(line);
		// Best first: the same words and numbers, the same words, then the
		// words in order with others between (the stat with "(Based on
		// Character Level)" after its words is another line).
		Line* best      = nullptr;
		int   bestScore = 0;
		for (auto& candidate : lines) {
			if (!candidate.indicator.empty() || (exact && !HasRun(candidate.words, want))) continue;
			std::size_t at = 0;
			for (const auto& word : candidate.words)
				if (at < want.size() && word == want[at]) ++at;
			if (at < want.size()) continue;
			const auto have  = LineNumbers(std::string_view(all).substr(candidate.start, candidate.end - candidate.start));
			const bool same  = have.size() >= numbers.size() && std::equal(numbers.begin(), numbers.end(), have.begin());
			const int  score = 1 + (candidate.words == want || (SameStart(candidate.words, want) && !Annotated(candidate.words, want)) ? 2 : 0) + (same ? 1 : 0);
			if (score > bestScore) best = &candidate, bestScore = score;
		}
		if (!best) continue;
		best->indicator = indicator;
		++placed;
	}
	if (!placed) return 0;
	std::string out;
	for (std::size_t k = 0; k < lines.size(); ++k) {
		std::size_t end = lines[k].end;
		while (end > lines[k].start && all[end - 1] == '\r') --end;
		out.append(all, lines[k].start, end - lines[k].start);
		out += lines[k].indicator;
		out.append(all, end, lines[k].end - end);
		if (lines[k].end < all.size()) out += '\n';
	}
	SetText(text, out);
	return placed;
}

// The number on a tooltip's line for `label` (its words starting the line,
// else anywhere in it), or -1: the first number before a '%' (the roll, not a
// range shown after it), else the first number.
int NumberInText(const GameString* text, const std::string& label) {
	if (!text || !text->text) return -1;
	const std::string_view all(text->text, text->size);
	const auto             want = LineWords(label);
	for (const bool loose : { false, true })
		for (std::size_t start = 0; start <= all.size();) {
			std::size_t end = all.find('\n', start);
			if (end == std::string_view::npos) end = all.size();
			const auto line  = all.substr(start, end - start);
			const auto words = LineWords(line);
			start            = end + 1;
			if (loose ? !HasRun(words, want) : !SameStart(words, want)) continue;
			int first = -1;
			for (std::size_t i = 0; i < line.size(); ++i) {
				if (static_cast<unsigned char>(line[i]) == 0xEE) { // a colour code
					i += 3;
					continue;
				}
				if (!std::isdigit(static_cast<unsigned char>(line[i]))) continue;
				int value = 0;
				while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) value = value * 10 + (line[i++] - '0');
				if (i < line.size() && line[i] == '%') return value;
				if (first < 0) first = value;
			}
			if (first >= 0) return first;
		}
	return -1;
}

// Appends an indicator to the end of the defense line. Whether it found it.
bool MarkDefense(GameString* text, const std::string& indicator) {
	if (!text || !text->text || DefenseLabel.empty()) return false;
	std::string       all(text->text, text->size);
	const std::size_t at = all.find(DefenseLabel);
	if (at == std::string::npos) return false;
	std::size_t end = all.find('\n', at);
	if (end == std::string::npos) end = all.size();
	while (end > at && all[end - 1] == '\r') --end;
	all.insert(end, indicator);
	SetText(text, all);
	return true;
}

// An item's defense: its base roll raised by its Enhanced Defense, plus flat
// defense (both from its magic lists), so a worn item's per-level bonuses
// don't count. -1 when the base roll is not readable.
int DefenseOf(void* item, const Values& values) {
	const int base = BaseDefense(item);
	if (base < 0) return -1;
	int ed = 0, flat = 0;
	(void)values.Get(EdefStat, 0, ed, true);
	(void)values.Get(DefenseStat, 0, flat, true);
	return base * (100 + ed) / 100 + flat;
}

// The defense indicator, when the defense of this unique or set item varies:
// the base roll (without Enhanced Defense, which sets it to the base's best +
// 1), Enhanced Defense or flat defense.
std::string DefenseIndicator(const Marking& mark, std::string& why) {
	if (mark.mode == LostStats) return {};
	if (mark.mode == OtherItem) {
		// Both with a defense: the larger is better.
		const int mine = DefenseOf(mark.item, mark.mine), theirs = DefenseOf(mark.copy, mark.theirs);
		if (mine < 0 || theirs < 0) return {};
		why += " defense " + std::to_string(mine) + "/" + std::to_string(theirs);
		return StatIndicator(OtherItem, DefenseStat, true, mine, true, theirs);
	}
	if (mark.ident.kind == RunewordKind || !mark.ident.props) return {};
	const RowProps& row  = *mark.ident.props;
	const int       code = row.code;
	if (code < 0 || code >= static_cast<int>(MaxDefense.size()) || MaxDefense[code] <= 0) return {};
	const auto ed   = mark.ranges.find({ EdefStat, 0 });
	const auto flat = mark.ranges.find({ DefenseStat, 0 });
	const bool baseRolls = !row.ed && MinDefense[code] < MaxDefense[code];
	if (!baseRolls && !(ed != mark.ranges.end() && ed->second.rolls) && !(flat != mark.ranges.end() && flat->second.rolls)) return {};
	const int mine = DefenseOf(mark.item, mark.mine);
	if (mine < 0) return {};
	// The best: the base's best (x1.5 when ethereal, if its roll is kept
	// raised), every Enhanced Defense and flat defense at its best.
	const int  base = BaseDefense(mark.item), plain = MaxDefense[code] + (row.ed ? 1 : 0);
	const int  top  = Ethereal(mark.item) && base > plain ? plain * 3 / 2 : plain;
	const int  best = top * (100 + (ed != mark.ranges.end() ? ed->second.best : 0)) / 100 + (flat != mark.ranges.end() ? flat->second.best : 0);
	const Range range { .worst = 0, .best = best, .rolls = true };
	why += " defense " + std::to_string(mine) + "/" + std::to_string(best);
	// An ethereal copy's defense is not comparable with a plain one's.
	if (Ethereal(mark.copy) != Ethereal(mark.item)) return {};
	const int theirs = DefenseOf(mark.copy, mark.theirs);
	if (theirs < 0) return {};
	why += " (copy " + std::to_string(theirs) + ")";
	return Indicator(Better(range, mine, theirs), mine >= best, theirs >= best, mine - theirs);
}

bool SwapSlot(std::uint64_t rva, void* hook, std::uint64_t* original);

std::string LastReport = "no item hovered yet";

// One ShowItemTooltip call: its widget, the hovered item, and the items whose
// texts were built for its comparison, with whether the comparison shows.
struct Round {
	void*                               widget {};
	void*                               item {};
	std::vector<std::pair<void*, bool>> compared;
	std::uint64_t                       at {}; // GetTickCount64 when it ended
};
thread_local Round Current, Last; // the one running (item set while it runs), the last one
thread_local void* BuiltInCompare {}; // the item whose text the comparison being built has

using ShowTooltipFn = void (*)(void* widget, void* item) noexcept;

// "unique row 157 (eth)", for the log and the console.
std::string DescribeItem(void* item) {
	if (!DataOf(item)) return "a non-item";
	const Ident ident = IdentOf(item, ValuesOf(item));
	char        text[96] {};
	if (ident.kind == NoKind) std::snprintf(text, sizeof(text), "another item");
	else std::snprintf(text, sizeof(text), "%s row %d%s%s", KindNames[ident.kind], ident.row, Ethereal(item) ? " (eth)" : "", Identified(item) ? "" : " (unidentified)");
	return text;
}

// Whether the widget's comparison boxes show, as ShowItemTooltip decides
// (0x2C3EC8..0x2C3F1B), apart from the texts being empty.
bool CompareShows(const unsigned char* widget) {
	using OverlayFn = void* (*)() noexcept;
	using HasFlagFn = bool (*)(void*, std::uint64_t) noexcept;
	using KeyDownFn = int (*)(int) noexcept;
	if (!widget || !widget[Game::Compares]) return false;
	if (void* overlay = reinterpret_cast<OverlayFn>(Base + Game::Overlay)(); reinterpret_cast<HasFlagFn>(Base + Game::HasFlag)(overlay, Game::NoCompareFlag)) return false;
	return reinterpret_cast<KeyDownFn>(Base + Game::KeyDown)(0x10) || widget[Game::AlwaysCompares];
}

void AddCompared(void* item) {
	if (!Current.item || !item || item == Current.item) return;
	const bool shows = CompareShows(static_cast<const unsigned char*>(Current.widget));
	for (auto& [known, shown] : Current.compared)
		if (known == item) return void(shown = shown || shows);
	if (Current.compared.size() < 8) Current.compared.emplace_back(item, shows);
}

// ShowItemTooltip, from its caller: the round's items are collected while it
// runs (a plugin hooking ShowItemTooltip itself runs inside, so its own
// comparison counts).
void HookShowTooltip(void* widget, void* item) noexcept {
	const Round outer = Current;
	Current           = { .widget = widget, .item = item };
	reinterpret_cast<ShowTooltipFn>(Base + Game::ShowTooltip)(widget, item);
	Current.at = GetTickCount64();
	// The game calls it every frame, mostly without an item (the item's text
	// is then rebuilt on its own every frame): only a call with one counts.
	if (item) Last = std::move(Current);
	Current = outer;
}

// The game's comparison text (or another plugin's, under this pointer): the
// item it built the text of; when no item text came through, the item the
// game compares with there.
int  FinishMarks(GameString* built, const Marking& mark, std::string& why);
bool SameItemPair(void* a, void* b);

std::string SocketReport(const Marking& mark);

GameString* HookCompared(GameString* out, int bodyLocation) noexcept {
	++CompareDepth;
	void* outer    = BuiltInCompare;
	BuiltInCompare = nullptr;
	// The compared item's lines the hovered item has no stat for are marked
	// lost: against another copy of the same item (what is socketed in it),
	// and against any item with the option.
	Marking lost { .mode = LostStats };
	void*   hovered = Current.item;
	if (Ready && !Marks && hovered && Identified(hovered) && !Worn(hovered))
		if (void* worn = ComparedItem(bodyLocation); worn && worn != hovered && Identified(worn) && Replaces(hovered, worn) && (Config.anyItems || SameItemPair(hovered, worn))) {
			lost.item = lost.copy = worn;
			lost.mine             = ValuesOf(worn);
			lost.theirs           = ValuesOf(hovered);
			Marks                 = &lost;
		}
	GameString* built = Compared(out, bodyLocation);
	if (Marks == &lost) Marks = nullptr;
	void* item     = BuiltInCompare;
	BuiltInCompare = outer;
	--CompareDepth;
	// Placed only when the text is the item the lines were recorded for.
	if (lost.item && built && built->text && built->size > 0 && (!item || item == lost.item)) {
		std::string why;
		(void)FinishMarks(built, lost, why);
	}
	// (While its lines were being marked, the item text hook let it through unseen.)
	if (Ready && Current.item && built && built->size > 0) AddCompared(item ? item : ComparedItem(bodyLocation));
	return built;
}

// For the log: the items socketed in both items and what their lists hold.
std::string SocketReport(const Marking& mark) {
	std::string text;
	for (const auto* values : { &mark.mine, &mark.theirs }) {
		if (!values->fillers) continue;
		text += values == &mark.mine ? "; socketed in it:" : "; socketed in the other:";
		for (void* filler : Fillers(values->unit)) text += " {" + DescribeLists(filler) + " }";
	}
	return text;
}

// Puts a marked text's indicators in: the recorded lines, Enhanced Damage,
// Enhanced Defense and sockets (their lines are written outside the stat
// loop; the numbers from the stat lists, the text's own item's from its text
// when not there) and defense. How many were placed.
int FinishMarks(GameString* built, const Marking& mark, std::string& why) {
	int placed = PlaceIndicators(built, mark.lines);
	for (const auto& [stat, label] : { std::pair { EdStat, &EdLabel }, std::pair { EdefStat, &EdefLabel }, std::pair { SocketsStat, &SocketLabel } }) {
		int  mine = 0, theirs = 0;
		bool haveMine = mark.mine.Shown(stat, 0, mine);
		if (!haveMine) mine = NumberInText(built, *label), haveMine = mine >= 0;
		if (!haveMine) continue;
		bool haveTheirs = mark.theirs.Shown(stat, 0, theirs);
		if (!haveTheirs && stat == SocketsStat && mark.mode == SameItem) theirs = 0, haveTheirs = true; // no Socketed line: none
		const std::string indicator = mark.mode == SameItem ? SameItemIndicator(mark, stat, 0, haveMine, mine, haveTheirs, theirs)
		                                                    : StatIndicator(mark.mode, stat, haveMine, mine, haveTheirs, theirs);
		if (indicator.empty()) continue;
		const int count = PlaceIndicators(built, { { *label, indicator } }, true);
		placed += count;
		why += " " + std::to_string(stat) + "=" + std::to_string(mine) + "/" + (haveTheirs ? std::to_string(theirs) : "none") + (count ? "" : " (line not found)");
	}
	if (const auto indicator = DefenseIndicator(mark, why); !indicator.empty()) {
		if (MarkDefense(built, indicator)) ++placed;
		else why += " (defense line not found)";
	}
	return placed;
}

// Whether two items are copies of the same unique, set item or runeword.
bool SameItemPair(void* a, void* b) {
	const Ident first = IdentOf(a, ValuesOf(a));
	return first.props && IdentOf(b, ValuesOf(b)) == first;
}

// Any item the hovered one is compared with (the option): shown, identified.
// The one of the same kind first (the weapon for a weapon), else any it replaces.
void* OtherFor(const Round& round, void* item) {
	const bool showsNow = CompareShows(static_cast<const unsigned char*>(round.widget));
	void*      fallback {};
	for (const auto& [copy, shown] : round.compared)
		if ((shown || showsNow) && copy != item && Identified(copy) && Replaces(item, copy)) {
			if (KindOf(copy) == KindOf(item)) return copy;
			if (!fallback) fallback = copy;
		}
	return fallback;
}

// The comparison copy of the hovered item: identified, the same item, and
// shown: when its text was built (a plugin showing its own comparison), or
// now (the game's, with Shift; the hovered item's text is rebuilt every frame).
void* CopyFor(const Round& round, void* item, const Ident& ident) {
	const bool showsNow = CompareShows(static_cast<const unsigned char*>(round.widget));
	for (const auto& [copy, shown] : round.compared)
		if ((shown || showsNow) && copy != item && Identified(copy) && IdentOf(copy, ValuesOf(copy)) == ident) return copy;
	return nullptr;
}

GameString* HookTooltip(GameString* out, void* unit, void* other, void* item, std::uint64_t a5, std::uint64_t a6, std::uint64_t a7, std::uint64_t a8, void* a9) noexcept {
	if (Ready && item && CompareDepth > 0 && !BuiltInCompare) BuiltInCompare = item;
	if (!Ready || Marks || !item) return Tooltip(out, unit, other, item, a5, a6, a7, a8, a9);
	// Another item's text while a tooltip is shown: what it is compared with.
	if (CompareDepth > 0 || (Current.item && item != Current.item)) {
		if (CompareDepth > 0 && !BuiltInCompare) BuiltInCompare = item;
		AddCompared(item);
		return Tooltip(out, unit, other, item, a5, a6, a7, a8, a9);
	}
	// The hovered item: its round, running or just ended (its text may be
	// built after ShowItemTooltip returns, or reused from the last frame).
	const Round* round = Current.item == item ? &Current : Last.item == item ? &Last : nullptr;
	if (!round || round->compared.empty() || !Identified(item) || Worn(item)) return Tooltip(out, unit, other, item, a5, a6, a7, a8, a9);
	Marking mark { .item = item, .mine = ValuesOf(item) };
	mark.ident = IdentOf(item, mark.mine);
	if (mark.ident.props && (mark.copy = CopyFor(*round, item, mark.ident))) mark.ranges = RangesOf(*mark.ident.props);
	else if (Config.anyItems && (mark.copy = OtherFor(*round, item))) mark.mode = OtherItem;
	else {
		LastReport = DescribeItem(item) + ": no indicators, compared with";
		for (const auto& [copy, shown] : round->compared) LastReport += " " + DescribeItem(copy) + (shown ? " (shown)" : " (not shown)");
		LastReport += Config.anyItems ? " (none shown and identified)" : mark.ident.props ? " (no shown copy of the same item)" : " (not a unique, set item or runeword)";
		return Tooltip(out, unit, other, item, a5, a6, a7, a8, a9);
	}
	mark.theirs       = ValuesOf(mark.copy);
	Marks             = &mark;
	GameString* built = Tooltip(out, unit, other, item, a5, a6, a7, a8, a9);
	Marks             = nullptr;
	if (!built || !built->text) return built;
	std::string why;
	const int   placed = FinishMarks(built, mark, why);
	char        head[240] {};
	std::snprintf(head, sizeof(head), "%s, compared with %s (%s%s): %zu lines recorded, %d placed;", DescribeItem(item).c_str(), DescribeItem(mark.copy).c_str(),
	              mark.mode == SameItem ? "rolls" : "stats", Worn(mark.copy) ? ", worn" : ", not worn", mark.lines.size(), placed);
	LastReport = head + why + mark.notes + SocketReport(mark);
	return built;
}

// The item tooltip text pointer. A plugin that swapped it already calls
// D2RCore through its own copy of the pointer, so the hook goes under it:
// that copy, in the plugin's writable data, now points here and the hook
// calls D2RCore.
const char* HookTooltipSlot() {
	auto*      slot    = reinterpret_cast<std::uint64_t*>(Base + Game::TooltipSlot);
	const auto current = *slot;
	if (!current) return "no tooltip pointer";
	HMODULE core = GetModuleHandleW(L"D2RCore.dll"), owner = nullptr;
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(current), &owner) || !owner || owner == core
	    || !core) {
		std::uint64_t original = 0;
		if (!SwapSlot(Game::TooltipSlot, reinterpret_cast<void*>(&HookTooltip), &original)) return "could not swap the tooltip pointer";
		Tooltip = reinterpret_cast<TooltipFn>(original);
		return nullptr;
	}
	const auto target = reinterpret_cast<std::uint64_t>(reinterpret_cast<void*>(GetProcAddress(core, "BuildItemTooltipWithStatRanges")));
	if (!target) return "D2RCore has no BuildItemTooltipWithStatRanges";
	Tooltip             = reinterpret_cast<TooltipFn>(target);
	const auto* dos     = reinterpret_cast<const IMAGE_DOS_HEADER*>(owner);
	const auto* nt      = reinterpret_cast<const IMAGE_NT_HEADERS64*>(reinterpret_cast<const unsigned char*>(owner) + dos->e_lfanew);
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
		// Not found: on top of it then (the texts it builds itself are not seen).
		std::uint64_t original = 0;
		if (!SwapSlot(Game::TooltipSlot, reinterpret_cast<void*>(&HookTooltip), &original)) return "could not swap the tooltip pointer";
		Tooltip = reinterpret_cast<TooltipFn>(original);
		return "another plugin's tooltip pointer was not found; went on top of it";
	}
	Say(false, "Compare Indicators: item texts followed under another plugin's hook (%d pointer%s)", seated, seated == 1 ? "" : "s");
	return nullptr;
}

// ShowItemTooltip's one call goes through a 14-byte jump in int3 padding
// inside D2R.exe (the call needs a target within 2 GB).
bool RedirectCall(std::uint64_t site, const std::uint8_t* bytes, const void* target) {
	if (std::memcmp(reinterpret_cast<const void*>(Base + site), bytes, 5) != 0) return false;
	std::array<std::uint8_t, 14> jump { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
	const auto                   address = reinterpret_cast<std::uint64_t>(target);
	std::memcpy(jump.data() + 6, &address, sizeof(address));
	std::array<std::uint8_t, 16> padding {};
	padding.fill(0xCC);
	int tries = 0;
	for (std::uint64_t start = 0x2D0000; start < 0x1400000 && tries < 16; start += 16) {
		if (std::memcmp(reinterpret_cast<const void*>(Base + start), padding.data(), padding.size()) != 0) continue;
		++tries;
		if (!Context->PatchBytes(start + 1, padding.data(), 14, jump.data(), 14)) continue; // another plugin took it
		return D2RL::PatchCallRel32(Context, site, bytes, 5, start + 1);
	}
	return false;
}

// ---- console ----

D2RL::ConsoleCommandResult __cdecl OnCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext*, void*) noexcept {
	Tell("compare-indicators: last hovered item: %s", LastReport.c_str());
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
	const std::pair<const char*, bool Settings::*> keys[] { { "enabled", &Settings::enabled },   { "uniques", &Settings::uniques },
		                                                    { "sets", &Settings::sets },         { "runewords", &Settings::runewords }, { "any_items", &Settings::anyItems },
		                                                    };
	std::size_t lineStart = 0;
	while (lineStart < text.size()) {
		auto lineEnd = text.find('\n', lineStart);
		if (lineEnd == std::string_view::npos) lineEnd = text.size();
		const std::string line = Trim(text.substr(lineStart, lineEnd - lineStart));
		lineStart              = lineEnd + 1;
		const auto equals      = line.find('=');
		if (line.empty() || line[0] == '#' || line[0] == '[' || equals == std::string::npos) continue;
		const std::string key   = Trim(std::string_view(line).substr(0, equals));
		const std::string value = Unquote(Trim(std::string_view(line).substr(equals + 1)));
		bool              known = false;
		for (const auto& [name, field] : keys)
			if (key == name) Config.*field = value != "false", known = true;
		if (!known) Say(true, "config: unknown key %s", key.c_str());
	}
}

bool Matches(std::uint64_t rva, const std::uint8_t* bytes, std::size_t size) {
	SIZE_T                    copied = 0;
	std::vector<std::uint8_t> code(size);
	if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(Base + rva), code.data(), size, &copied) || copied != size) return false;
	return std::memcmp(code.data(), bytes, size) == 0;
}

bool Locate() {
	const std::pair<std::uint64_t, std::pair<const std::uint8_t*, std::size_t>> functions[] {
		{ Game::GetUnit, { Game::GetUnitBytes, sizeof(Game::GetUnitBytes) } },
		{ Game::BodyLocs, { Game::BodyLocsBytes, sizeof(Game::BodyLocsBytes) } },
		{ Game::Player, { Game::PlayerBytes, sizeof(Game::PlayerBytes) } },
		{ Game::PlayerIndex, { Game::PlayerIndexBytes, sizeof(Game::PlayerIndexBytes) } },
		{ Game::Option, { Game::OptionBytes, sizeof(Game::OptionBytes) } },
		{ Game::Pet, { Game::PetBytes, sizeof(Game::PetBytes) } },
		{ Game::Inventory, { Game::InventoryBytes, sizeof(Game::InventoryBytes) } },
		{ Game::ItemAt, { Game::ItemAtBytes, sizeof(Game::ItemAtBytes) } },
		{ Game::FirstItem, { Game::FirstItemBytes, sizeof(Game::FirstItemBytes) } },
		{ Game::NextItem, { Game::NextItemBytes, sizeof(Game::NextItemBytes) } },
		{ Game::ShownCheck, { Game::ShownCheckBytes, sizeof(Game::ShownCheckBytes) } },
	};
	for (const auto& [rva, code] : functions)
		if (!Matches(rva, code.first, code.second)) {
			Say(true, "game code differs at D2R+0x%llX", static_cast<unsigned long long>(rva));
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

template <typename Service>
const Service* Query() {
	const Service* service = nullptr;
	return Context->QueryService(&service) == D2RL::ServiceQueryResult::Success ? service : nullptr;
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
		Say(false, "Compare Indicators %s: disabled in config", PluginVersion);
		return true;
	}
	TextApi      = Query<D2RL::LocalizationService>();
	InventoryApi = Query<D2RL::InventoryService>();
	Threads      = Query<D2RL::ThreadService>();
	TableApi     = Query<D2RL::DataTableService>();
	if (!Threads || !TableApi) {
		Say(true, "Compare Indicators %s: inactive, D2RLoader lacks a service it needs", PluginVersion);
		return true;
	}
	if (!InventoryApi) Say(true, "Compare Indicators: no inventory service; runewords get no indicators");
	Base   = context->exeBase;
	Strcat = reinterpret_cast<StrcatFn>(Base + Game::Strcat);
	if (!Base || !Locate()) {
		Say(true, "Compare Indicators %s: inactive, this game build isn't supported (Steam 3.3.93787 / 3.3.94995 are)", PluginVersion);
		return true;
	}
	if (HMODULE core = GetModuleHandleW(L"D2RCore.dll")) {
		ReadStat   = reinterpret_cast<StatFn>(reinterpret_cast<void*>(GetProcAddress(core, "ReadWideUnitStat")));
		Handedness = reinterpret_cast<HandednessFn>(reinterpret_cast<void*>(GetProcAddress(core, "GetItemHandednessForUnit")));
		CanPair    = reinterpret_cast<PairFn>(reinterpret_cast<void*>(GetProcAddress(core, "CanEquipItemPair")));
		if (!Handedness) Handedness = reinterpret_cast<HandednessFn>(reinterpret_cast<void*>(GetProcAddress(core, MAKEINTRESOURCEA(2002))));
		if (!CanPair) CanPair = reinterpret_cast<PairFn>(reinterpret_cast<void*>(GetProcAddress(core, MAKEINTRESOURCEA(2003))));
		if (!Handedness || !CanPair) Say(false, "Compare Indicators: D2RCore has no %s%s (two-handed items %s)", Handedness ? "" : "GetItemHandednessForUnit ", CanPair ? "" : "CanEquipItemPair", Handedness ? "known" : "taken as one-handed");
	}
	// D2RCore replaces the stat line loop (it starts with a jump): its lines then.
	const bool coreLoop = *reinterpret_cast<const std::uint8_t*>(Base + Game::LineLoop) == 0xFF;
	if (!(coreLoop ? HookCoreLines() : RedirectLineCat())) {
		Say(true, "Compare Indicators %s: inactive, could not reach the item stat lines (%s; another plugin may use them)", PluginVersion, coreLoop ? "D2RCore's loop" : "the game's loop");
		return true;
	}
	if (const char* problem = HookTooltipSlot()) {
		Say(!Tooltip, "Compare Indicators: item texts: %s", problem);
		if (!Tooltip) return true;
	}
	std::uint64_t original = 0;
	if (SwapSlot(Game::CompareSlot, reinterpret_cast<void*>(&HookCompared), &original)) Compared = reinterpret_cast<CompareFn>(original);
	else Say(true, "Compare Indicators: the game's comparison texts could not be followed; only item texts built for them are seen");
	if (!RedirectCall(Game::ShowTooltipCall, Game::ShowTooltipCallBytes, reinterpret_cast<const void*>(&HookShowTooltip))) {
		Say(true, "Compare Indicators %s: inactive, the item tooltips could not be followed (another plugin may use the call at D2R+0x%llX)", PluginVersion,
		    static_cast<unsigned long long>(Game::ShowTooltipCall));
		return true;
	}
	auto command = D2RL::MakeConsoleCommand("compare-indicators", OnCommand, "Compare Indicators: what the last hovered item was compared with.");
	if (auto* api = context->GetApi(); !api->registerConsoleCommand || !api->registerConsoleCommand(context, &command))
		Say(true, "Compare Indicators: could not register the compare-indicators command");
	// The tables are read on the game thread once a game is loaded.
	Running = true;
	Ticker  = std::thread([] {
        while (Running && !Ready) {
            (void)Threads->runOnGameThread(Context, BuildTables, nullptr);
            for (int i = 0; i < 20 && Running && !Ready; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
	Say(false, "Compare Indicators %s: active (%s)", PluginVersion, coreLoop ? "D2RCore's stat lines" : "the game's stat lines");
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	Running = false;
	if (Ticker.joinable()) Ticker.join();
}
