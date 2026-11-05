// Shrine Icons: a floating symbol over the shrines the game leaves bare, and
// a minimap icon per shrine type. Self-contained: everything the game needs for
// it is made or copied by the plugin and served as D2RLoader resources under
// .../d2rloader/shrine-icons/.
//
// Symbols. Booster shrines show a symbol over the shrine because of a client
// table with one entry per shrine code (shrines.txt "Code"): the function run
// when the shrine appears or changes mode, the one run when it is used, the
// symbol overlay, a shimmer overlay and a sound. Booster entries name a shared
// function that removes both overlays and adds them again while the shrine is
// idle (mode 0), so a used shrine loses its symbol and a recharged one gets it
// back. The plugin points the other shrine codes at a function of its own
// (Appear) that does the same with an overlay of their own when the settings
// say so, and runs it again on every shrine the client knows (unit hash
// 0x2A23910) when a setting changes.
//
// Those overlays are rows the plugin appends to the game's overlay table (a
// copy of shrine_skill each; the table is a vector at DataTables+0x1228, rows
// of 0xE0 bytes, the DataTables of each game version at 0x2A9A580). The HD
// client asks 0x662A90 for an overlay's file (data/hd/overlays/<folder>/<name>
// .json); the plugin answers its rows with its own OverlayDefinition, which
// floats the Skill Shrine effect with its mesh swapped for an item's. The
// effect, mesh and texture are the game's own files under new names (the effect
// stores paths as length-prefixed strings, so every new path has the length of
// the one it replaces).
//
// Native layout (Steam 3.3.93787 / 94995):
//   - Shrine table 0x1CDA280: 23 entries of 0x20 bytes
//       +0x00 void (*)(Unit*, ShrinesTxt*) on appear / mode change
//       +0x08 void (*)(Unit*, Unit* user, ShrinesTxt*) on use
//       +0x10 int symbol overlay (-1 none), +0x14 int shimmer overlay
//       +0x18 int sound
//   - 0x1CA3E0: the booster function above (entries 6-15): RemoveOverlay
//     0x152090(unit, 0, id), and when Mode 0x34AB60(unit) is 0, CreateOverlay
//     0x150280(unit, id, 3, 0, 0, 0, 0).
//   - AddAutomapCell 0xD52B0 (Unit*, int cell, void* ctx): its only caller
//     walks the units of a newly revealed room and passes objects.txt AutoMap,
//     310 for every shrine. Unit +0x00 type (2 = object), +0x10 ObjectData;
//     ObjectData +0x10 ShrinesTxt*, whose first byte is the shrine code.
// Every address is checked byte for byte before anything is written; a
// mismatch leaves that part of the game untouched.
//
// Models: the renderer takes a model's bounds from data/hd/model_lod_desc.bin,
// keyed by the FNV-1a hash of the lowercased path (lookup 0xF75AF0). A path the
// file does not list gets an empty box and is culled, so the plugin's copies of
// the item models are looked up under the item model they copy.
//
// Minimap: the automap tile sheets are sprites loaded by name through 0x858990
// (data/hd/global/ui/<name>); the tileset loader's call (0xD4724) goes through
// the plugin, which answers default07 and default13 with copies of the game's
// sheets that have one variant of the gold shrine cell per type painted into
// cells no data file uses (MinimapCell, BoosterCell; art built in from
// minimap_cells.bin). The hook on AddAutomapCell hands those cells out instead
// of 310. The number of automap tiles does not change, so saved maps stay
// valid. The automap draw loop reads each tile's cell at 0xD717E (movsx edi,
// word [rbx+0x22]); a jump there to a small stub maps the cell through a table
// (Remap), which shows a shrine's own cell or the gold ring as the settings
// say, for tiles already on the map too.

#include <D2RLPlugin/api.h>
#include <windows.h>

#include "i_hate_tomls_client.h"

#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>
#include <map>
#include <vector>

namespace {

constexpr const char* PluginId      = "shrine-icons";
constexpr const char* PluginVersion = "1.0.1-dev-2";

const D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = PluginId,
	.name        = "Shrine Icons",
	.version     = PluginVersion,
	.author      = "wild-healer",
	.description = "Floating symbols over the shrines that have none (refill, portal, gem, fire, monster, exploding, poison) and a minimap icon per shrine type.",
	.flags       = D2RL::PluginFlags::Client | D2RL::PluginFlags::NativeHooks,
};

// ---- native layout (Steam 3.3.93787 / 94995) ----

constexpr std::uint64_t ShrineTableRva   = 0x1CDA280;
constexpr std::uintptr_t EntrySize       = 0x20;
constexpr std::uint32_t  EntryCount      = 23;
constexpr std::uint64_t BoosterFnRva     = 0x1CA3E0;
// Start of the booster function: removes the symbol overlay via 0x152090.
constexpr std::array<std::uint8_t, 24> BoosterFnBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89,
	                                                    0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x0F, 0xB6, 0x3A, 0x48 };
constexpr std::int32_t   ArmorCode       = 6;  // first booster entry
constexpr std::int32_t   ArmorOverlay    = 59; // shrine_armor
constexpr std::int32_t   Shimmer00       = 57; // shrine_shimmer00

constexpr std::uint64_t FindModelDescRva = 0xF75AF0;
constexpr std::array<std::uint8_t, 24> FindModelDescBytes { 0x48, 0x83, 0xEC, 0x08, 0x4C, 0x63, 0x51, 0x04, 0x4C, 0x8D, 0x41, 0x04,
	                                                        0x8B, 0x01, 0x4D, 0x03, 0xC2, 0x4C, 0x6B, 0xC8, 0x2C, 0x44, 0x8B, 0xDA };

constexpr std::uint64_t AddCellRva = 0xD52B0;
constexpr std::array<std::uint8_t, 19> AddCellBytes { 0x48, 0x89, 0x5C, 0x24, 0x20, 0x55, 0x56, 0x57, 0x48, 0x83,
	                                                  0xEC, 0x50, 0x48, 0x8B, 0x05, 0x05, 0x60, 0x8F, 0x02 };
constexpr std::int32_t   ShrineCell    = 310;
// Automap draw: the tile's cell is read here; 7 bytes replaced by a jump.
constexpr std::uint64_t DrawCellRva = 0xD717E;
constexpr std::array<std::uint8_t, 7> DrawCellBytes { 0x0F, 0xBF, 0x7B, 0x22, 0x41, 0x2B, 0xD6 }; // movsx edi, word [rbx+0x22]; sub edx, r14d
constexpr std::uint32_t CellCount = 2048;
constexpr std::uint64_t RemoveOverlayRva = 0x152090;
constexpr std::uint64_t CreateOverlayRva = 0x150280;
constexpr std::uint64_t ModeRva          = 0x34AB60;
constexpr std::uint64_t UnitHashRva      = 0x2A23910; // client units: type * 0x400, 128 buckets
constexpr std::uintptr_t UnitNext        = 0x158;
constexpr std::uintptr_t ObjectsTxtSubClass = 0x127; // &1 = shrine
constexpr std::uint32_t  ObjectUnit    = 2;
constexpr std::uintptr_t UnitData      = 0x10;
constexpr std::uintptr_t ObjectShrine  = 0x10;

// Overlay table: DataTables per game version (4, 16 bytes apart); its overlay
// rows are a vector {rows, count, capacity} at +0x1228, rows of 0xE0 bytes
// (0x1501A0 reads them so).
constexpr std::uint64_t DataTablesRva = 0x2A9A580;
constexpr std::array<std::uint8_t, 8> DataTablesBytes { 0x48, 0x8D, 0x0D, 0xB9, 0x9A, 0x79, 0x02, 0x48 }; // at 0x300AC0: lea rcx, [DataTables]
constexpr std::uint64_t OverlayRowRva = 0x1501C1;
constexpr std::array<std::uint8_t, 7> OverlayRowBytes { 0x48, 0x3B, 0x98, 0x30, 0x12, 0x00, 0x00 }; // cmp rbx, [rax+0x1230]
constexpr std::uint64_t OverlayStrideRva = 0x15021B;
constexpr std::array<std::uint8_t, 4> OverlayStrideBytes { 0x48, 0x69, 0x44, 0x24 }; // imul rax, [rsp+0x50], 0xE0
constexpr std::uintptr_t OverlayVector = 0x1228;
constexpr std::size_t    OverlayRowSize = 0xE0;
constexpr std::uint64_t  NotOwned = 1ull << 63; // capacity flag: the game never frees the rows

// HD overlay file: 0x662A90(registry, string* out, overlay record*), the
// overlay id at record +4.
// HD overlay file list (no hook, so plugins that check 0x662A90 still load):
// the file of overlay id N is "data/hd/overlays" + folder + name + ".json" of
// entry N of a vector at +0x90 (count +0x98, capacity +0xA0) of the object
// 0x662610 returns (static at 0x3404430). Entries are 0x60 bytes: name
// char[0x30], folder char[0x30]; 0x6650F0 fills them from the overlay table.
constexpr std::uint64_t OverlayFilesRva   = 0x662610;
constexpr std::uint64_t OverlayFilesObj   = 0x3404430;
constexpr std::array<std::uint8_t, 13> OverlayFilesBytes { 0x48, 0x83, 0xEC, 0x28, 0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00 };
constexpr std::uint64_t OverlayFileCountRva = 0x662AF1; // cmp rax, [rcx+0x98]
constexpr std::array<std::uint8_t, 7> OverlayFileCountBytes { 0x48, 0x3B, 0x81, 0x98, 0x00, 0x00, 0x00 };
constexpr std::uint64_t OverlayFileListRva = 0x662B06; // lea rsi, [rcx+0x90]
constexpr std::array<std::uint8_t, 7> OverlayFileListBytes { 0x48, 0x8D, 0xB1, 0x90, 0x00, 0x00, 0x00 };
constexpr std::size_t   OverlayFileSize = 0x60;

// Automap tile sheets: the tileset loader's sprite load call.
constexpr std::uint64_t TileLoadCallRva = 0xD4724;
constexpr std::uint64_t SpriteLoadRva   = 0x858990;
constexpr std::uint64_t IsHdRva         = 0x846210; // bool (): HD graphics
struct Entry {
	std::uint64_t onAppear;
	std::uint64_t onUse;
	std::int32_t  symbol;
	std::int32_t  shimmer;
	std::int32_t  sound;
	std::int32_t  pad;
};
static_assert(sizeof(Entry) == EntrySize);

struct Icon {
	const char*   key;     // config key
	std::uint32_t code;    // shrines.txt Code
	std::int32_t  cell;    // automap cell painted for it
	const char*   model;   // the model whose bounds the copy takes (also the folder of the next two)
	const char*   lod0;    // mesh copied for the symbol
	const char*   albedo;  // its texture
	double        fit;     // scale that brings the model to the size of the booster symbols
	bool          upright; // lies flat on the ground: turned a quarter turn about X
	bool          symbol;  // false: minimap icon only
};

std::array<Icon, 9> Icons { {
	// Cells: MINIMAP_CELLS in tools_make_minimap.py. Health and mana shrines get
	// no symbol: their coloured pool already tells them apart. Fire, exploding
	// and poison float the burning skull of the fire skull missile.
	{ "refill", 1, 1332, "data/hd/items/misc/potion/full_rejuv_potion/full_rejuv_potion.model", "full_rejuv_potion_lod0.model", "misc_full_rejuv_potion_alb.texture", 1.1, false, true },
	{ "health", 2, 1335, "data/hd/items/misc/potion/full_healing_potion/full_healing_potion.model", "full_healing_potion_lod0.model", "misc_full_healing_potion_alb.texture", 1.1, false, false },
	{ "mana", 3, 1358, "data/hd/items/misc/potion/full_mana_potion/full_mana_potion.model", "full_mana_potion_lod0.model", "misc_full_mana_potion_alb.texture", 1.1, false, false },
	{ "portal", 17, 1359, "data/hd/items/misc/scroll/town_portal_scroll/town_portal_scroll.model", "town_portal_scroll_lod0.model", "misc_town_portal_scroll_alb.texture", 0.8, false, true },
	{ "gem", 18, 1369, "data/hd/items/misc/gem/perfect_amethyst/perfect_amethyst.model", "perfect_amethyst_lod0.model", "misc_perfect_amethyst_alb.texture", 1.85, true, true },
	{ "fire", 19, 1386, "data/hd/vfx/meshes/missiles/firehead/firehead.model", "firehead_lod0.model", "firehead_alb.texture", 1.0, false, true },
	{ "monster", 20, 1387, "data/hd/items/misc/gem/perfect_skull/perfect_skull.model", "perfect_skull_lod0.model", "misc_perfect_skull_alb.texture", 1.6, false, true },
	{ "exploding", 21, 1388, "data/hd/vfx/meshes/missiles/firehead/firehead.model", "firehead_lod0.model", "firehead_alb.texture", 1.0, false, true },
	{ "poison", 22, 1389, "data/hd/vfx/meshes/missiles/firehead/firehead.model", "firehead_lod0.model", "firehead_alb.texture", 1.0, false, true },
} };

struct Settings {
	std::atomic<bool> enabled { true };
	std::atomic<bool> shimmer { true };
	std::atomic<bool> symbols { true };
	std::atomic<bool> minimap { true };
	std::atomic<bool> boosters { true }; // minimap icons for booster shrines too
	std::atomic<double> size { 1.0 };    // symbol size, 1 = booster symbols
	std::atomic<double> height { 14.0 }; // how high the symbol floats
	// Gem tuning (dev builds only, not saved): extra turn about the world
	// axes (degrees, X then Y then Z, pivot = the symbol's origin) and a move.
	std::atomic<double> gemTurn[3] { 0.0, 0.0, 0.0 };
	std::atomic<double> gemMove[3] { 0.0, 0.0, 0.0 };
} Config;

std::array<std::atomic<bool>, Icons.size()> IconOn {};   // per-type setting
std::array<std::int32_t, Icons.size()>      Overlay {};  // overlay row the plugin added, -1 none
std::array<std::int32_t, EntryCount>         IconOf {};  // shrine code -> Icons index, -1 none

// Booster shrines (codes 6-15) keep the game's floating symbol but get a minimap
// icon of their own, in code order. Cells: BOOSTER_CELLS in
// tools_make_minimap.py.
constexpr std::uint32_t FirstBooster = 6;
constexpr std::array<std::int32_t, 10> BoosterCell { 700, 701, 702, 703, 704, 705, 706, 707, 715, 716 };

// Automap cell painted per shrine code, 0 = none. Written on the game thread
// when the sheets load, read by the hook on the same thread.
std::array<std::int32_t, EntryCount> MinimapCell {};
std::atomic<bool> SheetPainted[2] {}; // default13 (shrine icons), default07 (booster icons)
bool SymbolsReady = false;            // the table entries point at Appear
bool FilesReady   = false;            // the effects, meshes and textures are registered

// Cell the automap draws for each stored cell; read by the draw stub.
alignas(64) std::int16_t Remap[CellCount] {};
bool RemapActive = false;

const D2RL::PluginContext*    Context {};
const D2RL::ResourceService*  ResourceApi {};
const D2RL::LifecycleService* Lifecycle {};
const D2RL::ThreadService*    Threads {};
D2RL::Lifecycle::ListenerHandle TablesListener {};
std::uintptr_t Base {};

void Log(bool warn, const char* format, ...) noexcept {
	if (!Context) return;
	char    line[512] {};
	va_list args;
	va_start(args, format);
	std::vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	warn ? Context->LogWarn(line) : Context->LogInfo(line);
}

// Copies game memory after checking it is committed and readable.
bool Read(std::uintptr_t address, void* output, std::size_t size) noexcept {
	if (!address) return false;
	MEMORY_BASIC_INFORMATION info {};
	if (VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)) != sizeof(info) || info.State != MEM_COMMIT) return false;
	if (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
	const auto end = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
	if (address + size > end) return false;
	std::memcpy(output, reinterpret_cast<const void*>(address), size);
	return true;
}

bool Patch(std::uint64_t rva, const void* expected, const void* bytes, std::uint32_t size) noexcept {
	return Context->PatchBytes(rva, expected, size, bytes, size);
}

bool ReadEntry(std::uint32_t code, Entry& entry) noexcept {
	return Read(Base + ShrineTableRva + code * EntrySize, &entry, sizeof(entry));
}

// The table and the booster function look the way this plugin knows them.
bool GameMatches() noexcept {
	std::array<std::uint8_t, BoosterFnBytes.size()> code {};
	if (!Read(Base + BoosterFnRva, code.data(), code.size()) || code != BoosterFnBytes) return false;
	Entry armor {};
	if (!ReadEntry(ArmorCode, armor) || armor.onAppear != Base + BoosterFnRva || armor.symbol != ArmorOverlay || armor.shimmer != Shimmer00) return false;
	Entry none {};
	if (!ReadEntry(0, none) || none.onAppear != 0 || none.symbol != -1) return false;
	for (const auto& icon : Icons) {
		Entry entry {};
		if (!ReadEntry(icon.code, entry) || entry.onAppear != 0 || entry.symbol != -1 || entry.shimmer != -1) return false;
	}
	for (const auto& [rva, bytes] : { std::pair { RemoveOverlayRva, std::array<std::uint8_t, 4> { 0x48, 0x85, 0xC9, 0x0F } },
	                                  std::pair { ModeRva, std::array<std::uint8_t, 4> { 0x48, 0x83, 0xEC, 0x28 } } }) {
		std::array<std::uint8_t, 4> code {};
		if (!Read(Base + rva, code.data(), code.size()) || code != bytes) return false;
	}
	return true;
}

// ---- symbols ----

using RemoveOverlayFn = void (*)(void* unit, std::int32_t zero, std::int32_t overlay) noexcept;
using CreateOverlayFn = void (*)(void* unit, std::int32_t overlay, std::int32_t three, std::int32_t, std::int32_t, std::int32_t, std::int32_t) noexcept;
using ModeFn          = std::int32_t (*)(void* unit) noexcept;

bool SymbolShown(std::size_t i) noexcept {
	return Config.enabled && Config.symbols && Icons[i].symbol && IconOn[i] && Overlay[i] >= 0;
}

bool EnsureOverlayFiles() noexcept;

// Removes the shrine's symbol and shimmer, then adds them again while the
// shrine is idle and the settings want them: what the booster function does.
void ShowSymbol(void* unit, std::size_t i) noexcept {
	const auto overlay = Overlay[i];
	if (overlay < 0 || !unit) return;
	const auto remove = reinterpret_cast<RemoveOverlayFn>(Base + RemoveOverlayRva);
	const auto create = reinterpret_cast<CreateOverlayFn>(Base + CreateOverlayRva);
	const auto mode   = reinterpret_cast<ModeFn>(Base + ModeRva);
	remove(unit, 0, overlay);
	remove(unit, 0, Shimmer00);
	if (!SymbolShown(i) || mode(unit) != 0 || !EnsureOverlayFiles()) return;
	create(unit, overlay, 3, 0, 0, 0, 0);
	if (Config.shimmer) create(unit, Shimmer00, 3, 0, 0, 0, 0);
}

// Table entry function for the shrine codes with a new symbol.
void Appear(void* unit, const std::uint8_t* shrine) noexcept {
	if (!shrine || *shrine >= EntryCount || IconOf[*shrine] < 0) return;
	ShowSymbol(unit, static_cast<std::size_t>(IconOf[*shrine]));
}

// Shrine code of a client unit, -1 when it is no shrine.
std::int32_t ShrineCode(const std::uint8_t* unit) noexcept {
	if (*reinterpret_cast<const std::uint32_t*>(unit) != ObjectUnit) return -1;
	const auto* data = *reinterpret_cast<const std::uint8_t* const*>(unit + UnitData);
	if (!data) return -1;
	const auto* txt = *reinterpret_cast<const std::uint8_t* const*>(data);
	if (!txt || !(txt[ObjectsTxtSubClass] & 1)) return -1;
	const auto* shrine = *reinterpret_cast<const std::uint8_t* const*>(data + ObjectShrine);
	return shrine && *shrine < EntryCount ? *shrine : -1;
}

// Runs the symbol logic again on every shrine the client knows. Game thread.
void RefreshSymbols() noexcept {
	if (!SymbolsReady) return;
	auto* const* bucket = reinterpret_cast<std::uint8_t* const*>(Base + UnitHashRva + ObjectUnit * 0x400);
	for (std::uint32_t b = 0; b < 128; ++b) {
		std::uint32_t guard = 0;
		for (auto* unit = bucket[b]; unit && guard < 100000; unit = *reinterpret_cast<std::uint8_t* const*>(unit + UnitNext), ++guard) {
			const auto code = ShrineCode(unit);
			if (code >= 0 && IconOf[code] >= 0 && Icons[IconOf[code]].symbol) ShowSymbol(unit, static_cast<std::size_t>(IconOf[code]));
		}
	}
}

// Points the new-symbol entries at Appear, once; their overlay fields stay -1.
bool PatchTable() noexcept {
	const std::uint64_t appear = reinterpret_cast<std::uint64_t>(&Appear), none = 0;
	for (const auto& icon : Icons) {
		if (!icon.symbol) continue;
		if (!Patch(ShrineTableRva + icon.code * EntrySize, &none, &appear, sizeof(appear))) return false;
	}
	return true;
}

// ---- minimap ----

using AddCellFn = void (*)(void* unit, std::int32_t cell, void* context) noexcept;
AddCellFn OriginalAddCell {};

// The settings show this code's own minimap icon.
bool MinimapShown(std::uint32_t code) noexcept {
	if (!Config.enabled || !Config.minimap || code >= EntryCount || !MinimapCell[code]) return false;
	if (IconOf[code] >= 0) return IconOn[IconOf[code]];
	return Config.boosters;
}

// With the draw stub, every shrine stores its own cell and the stub decides
// what is drawn; without it, the cell is chosen here once.
void HookAddCell(void* unit, std::int32_t cell, void* context) noexcept {
	if (cell == ShrineCell && unit && *static_cast<const std::uint32_t*>(unit) == ObjectUnit) {
		const auto* data   = *reinterpret_cast<const std::uint8_t* const*>(static_cast<const std::uint8_t*>(unit) + UnitData);
		const auto* shrine = data ? *reinterpret_cast<const std::uint8_t* const*>(data + ObjectShrine) : nullptr;
		if (shrine && *shrine < EntryCount && MinimapCell[*shrine] && (RemapActive || MinimapShown(*shrine))) cell = MinimapCell[*shrine];
	}
	OriginalAddCell(unit, cell, context);
}

// The plugin's cells drawn as themselves or as the gold ring; left alone while
// their sheet is the game's own.
void UpdateRemap() noexcept {
	const auto set = [](std::int32_t cell, bool shown) {
		if (cell > 0 && cell < static_cast<std::int32_t>(CellCount)) Remap[cell] = static_cast<std::int16_t>(shown ? cell : ShrineCell);
	};
	for (const auto& icon : Icons)
		set(icon.cell, !SheetPainted[0] || MinimapShown(icon.code));
	for (std::uint32_t i = 0; i < BoosterCell.size(); ++i)
		set(BoosterCell[i], !SheetPainted[1] || MinimapShown(FirstBooster + i));
}

// Memory within a rel32 jump of the game code.
void* AllocNear(std::uintptr_t target, std::size_t size) noexcept {
	SYSTEM_INFO system {};
	GetSystemInfo(&system);
	const std::uintptr_t step = system.dwAllocationGranularity ? system.dwAllocationGranularity : 0x10000;
	for (std::uintptr_t distance = step; distance < 0x70000000; distance += step) {
		for (const std::uintptr_t at : { target - distance, target + distance }) {
			if (distance > target && at == target - distance) continue;
			const std::uintptr_t aligned = at / step * step;
			MEMORY_BASIC_INFORMATION info {};
			if (VirtualQuery(reinterpret_cast<const void*>(aligned), &info, sizeof(info)) != sizeof(info) || info.State != MEM_FREE) continue;
			if (void* memory = VirtualAlloc(reinterpret_cast<void*>(aligned), size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) return memory;
		}
	}
	return nullptr;
}

// Executable stubs near the game code (one page, written before it is made
// read-only and executable, see SealStubs).
std::uint8_t* StubPage {};
std::size_t   StubUsed = 0;

std::uint8_t* NewStub(std::size_t size) noexcept {
	if (!StubPage) StubPage = static_cast<std::uint8_t*>(AllocNear(Base + DrawCellRva, 4096));
	if (!StubPage || StubUsed + size > 4096) return nullptr;
	auto* stub = StubPage + StubUsed;
	StubUsed += (size + 15) & ~std::size_t { 15 };
	return stub;
}

bool SealStubs() noexcept {
	DWORD old = 0;
	if (!StubPage || !VirtualProtect(StubPage, 4096, PAGE_EXECUTE_READ, &old)) return false;
	FlushInstructionCache(GetCurrentProcess(), StubPage, 4096);
	return true;
}

// rel32 from the end of a 5-byte jump/call at `site` to `target`; false when too far.
bool Rel32(std::uintptr_t site, const void* target, std::int32_t& rel) noexcept {
	const auto distance = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(target)) - static_cast<std::int64_t>(site + 5);
	if (distance < INT32_MIN || distance > INT32_MAX) return false;
	rel = static_cast<std::int32_t>(distance);
	return true;
}

// A stub that maps the automap draw's cell through Remap (the jump to it is
// written once the stub page is sealed, PatchDraw).
std::uint8_t* DrawStub {};

bool BuildDrawStub() noexcept {
	for (std::uint32_t i = 0; i < CellCount; ++i)
		Remap[i] = static_cast<std::int16_t>(i);
	UpdateRemap();
	const std::uintptr_t site = Base + DrawCellRva, back = site + DrawCellBytes.size();
	auto* stub = NewStub(64);
	if (!stub) return false;
	std::uint8_t* at   = stub;
	const auto    emit = [&](std::initializer_list<std::uint8_t> bytes) {
		for (const auto byte : bytes)
			*at++ = byte;
	};
	emit({ 0x0F, 0xBF, 0x7B, 0x22 });             // movsx edi, word [rbx+0x22]
	emit({ 0x41, 0x2B, 0xD6 });                   // sub edx, r14d
	emit({ 0x81, 0xFF, 0x00, 0x08, 0x00, 0x00 }); // cmp edi, CellCount
	emit({ 0x73, 0x10 });                         // jae done
	emit({ 0x50, 0x48, 0xB8 });                   // push rax; mov rax, Remap
	const auto table = reinterpret_cast<std::uint64_t>(&Remap[0]);
	std::memcpy(at, &table, 8), at += 8;
	emit({ 0x0F, 0xBF, 0x3C, 0x78 });             // movsx edi, word [rax+rdi*2]
	emit({ 0x58 });                               // pop rax
	emit({ 0xFF, 0x25, 0, 0, 0, 0 });             // done: jmp [rip]
	std::memcpy(at, &back, 8), at += 8;
	DrawStub = stub;
	return true;
}

bool PatchDraw() noexcept {
	std::int32_t rel = 0;
	if (!DrawStub || !Rel32(Base + DrawCellRva, DrawStub, rel)) return false;
	std::array<std::uint8_t, DrawCellBytes.size()> jump { 0xE9, 0, 0, 0, 0, 0x90, 0x90 };
	std::memcpy(jump.data() + 1, &rel, 4);
	return Patch(DrawCellRva, DrawCellBytes.data(), jump.data(), static_cast<std::uint32_t>(jump.size()));
}

// ---- model bounds ----

// The hash model_lod_desc.bin is keyed by (see 0x150140).
std::uint32_t PathHash(std::string_view path) noexcept {
	std::uint32_t hash = 0x811C9DC5;
	for (const char c : path) {
		const auto byte = static_cast<std::uint8_t>(c);
		hash            = (hash ^ (byte >= 'A' && byte <= 'Z' ? byte + 0x20 : byte)) * 0x1000193;
	}
	return hash;
}

// Hash of the plugin's copy of each icon's model, and of the item model it copies.
std::array<std::uint32_t, Icons.size()> CopyHash {}, ItemHash {};

using FindModelDescFn = const void* (*)(const void* table, std::uint32_t hash) noexcept;
FindModelDescFn OriginalFindModelDesc {};

const void* HookFindModelDesc(const void* table, std::uint32_t hash) noexcept {
	for (std::size_t i = 0; i < Icons.size(); ++i)
		if (hash == CopyHash[i]) hash = ItemHash[i];
	return OriginalFindModelDesc(table, hash);
}

void HashModels() noexcept {
	for (std::size_t i = 0; i < Icons.size(); ++i) {
		char path[96] {};
		std::snprintf(path, sizeof(path), "data/hd/vfx/meshes/lit_mesh/d2rloader/shrine-icons/%02u.model", Icons[i].code);
		CopyHash[i] = PathHash(path);
		ItemHash[i] = PathHash(Icons[i].model);
	}
}

// ---- game files ----
//
// The game's own files are read the way the game reads its json files: its
// resource manager (0x9F3540) loads the file (0x9F3DA0), so an active mod's
// copy wins over the game's. The result is a shared pointer {resource,
// control}; the resource holds {u32 state (3 = loaded), data at +8, size at
// +0x10}. If that fails, D2RCore's archive open (export ordinal 160) with the
// game's size, read and close.

namespace GameLoader {
constexpr std::uint64_t Manager = 0x9F3540, Load = 0x9F3DA0;
constexpr std::uint8_t  ManagerBytes[] { 0x48, 0x83, 0xEC, 0x38, 0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00 };
constexpr std::uint8_t  LoadBytes[] { 0x40, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B, 0xFA, 0x48, 0x8D, 0x54, 0x24, 0x20 };
constexpr std::uint16_t OpenOrdinal = 160;
constexpr std::uint64_t Size = 0xA00050, ReadFile = 0xA00EC0, Close = 0x9FFE50;
constexpr std::uint8_t  SizeBytes[] { 0x48, 0x8B, 0x01, 0x48, 0xFF, 0x60, 0x30 };
constexpr std::uint8_t  ReadBytes[] { 0x4C, 0x89, 0x44, 0x24, 0x18, 0x48, 0x89, 0x54, 0x24, 0x10 };
constexpr std::uint8_t  CloseBytes[] { 0xE9 };
} // namespace GameLoader

struct SharedRef {
	unsigned char* object {};
	unsigned char* control {}; // vtable, strong count at +8, weak count at +0xC
};
// The load request, as Fog's JSON loader builds it on its stack: the path as
// the game's string (text, length, capacity with the "not owned" bit) with its
// inline buffer at +0x18, a byte 0x64 at +0xB8, then two empty callbacks
// (tagged 1) at +0xC0 and +0x108.
struct alignas(16) LoadRequest {
	unsigned char bytes[0x180] {};
};
using ManagerFn   = void* (*)() noexcept;
using LoadFn      = SharedRef* (*)(void* manager, SharedRef* out, LoadRequest* request, int flags) noexcept;
using ReleaseFn   = void (*)(void* control) noexcept;
using FileOpenFn  = void* (*)(const char* path) noexcept;
using FileSizeFn  = std::uint64_t (*)(void* file) noexcept;
using FileReadFn  = bool (*)(void* file, void* buffer, std::uint64_t size, std::uint64_t* read) noexcept;
using FileCloseFn = void (*)(void* file) noexcept;

bool CodeIs(std::uint64_t rva, const std::uint8_t* bytes, std::size_t count) noexcept {
	std::uint8_t code[32] {};
	return count <= sizeof(code) && Read(Base + rva, code, count) && std::memcmp(code, bytes, count) == 0;
}

std::string LoaderFile(const char* path) {
	using namespace GameLoader;
	if (!CodeIs(Manager, ManagerBytes, sizeof(ManagerBytes)) || !CodeIs(Load, LoadBytes, sizeof(LoadBytes))) return {};
	void* manager = reinterpret_cast<ManagerFn>(Base + Manager)();
	const std::size_t length = std::strlen(path);
	if (!manager || length >= 0x9A) return {};
	LoadRequest request;
	const auto  put = [&](std::size_t offset, std::uint64_t value) { std::memcpy(request.bytes + offset, &value, sizeof(value)); };
	std::memcpy(request.bytes + 0x18, path, length);
	put(0, reinterpret_cast<std::uint64_t>(request.bytes + 0x18));
	put(8, length);
	put(0x10, 0x800000000000009AULL);
	request.bytes[0xB8] = 0x64;
	put(0xC0, 1);
	put(0x108, 1);
	SharedRef ref;
	reinterpret_cast<LoadFn>(Base + Load)(manager, &ref, &request, 0);
	std::string bytes;
	if (ref.object) {
		std::uint32_t state = 0;
		std::memcpy(&state, ref.object, 4);
		const char*   data = nullptr;
		std::uint64_t size = 0;
		std::memcpy(&data, ref.object + 8, 8);
		std::memcpy(&size, ref.object + 0x10, 8);
		if (state == 3 && data && size > 0 && size < (64u << 20)) bytes.assign(data, size);
	}
	if (ref.control) {
		auto* strong = reinterpret_cast<volatile long*>(ref.control + 8);
		auto* weak   = reinterpret_cast<volatile long*>(ref.control + 0xC);
		auto* table  = *reinterpret_cast<ReleaseFn* const*>(ref.control);
		if (InterlockedDecrement(strong) == 0) {
			table[2](ref.control);
			if (InterlockedDecrement(weak) == 0) table[3](ref.control);
		}
	}
	return bytes;
}

std::string ArchiveFile(const char* path) {
	using namespace GameLoader;
	static const FileOpenFn open = [] {
		HMODULE core = GetModuleHandleW(L"D2RCore.dll");
		if (!core || !CodeIs(Size, SizeBytes, sizeof(SizeBytes)) || !CodeIs(ReadFile, ReadBytes, sizeof(ReadBytes)) || !CodeIs(Close, CloseBytes, sizeof(CloseBytes))) return FileOpenFn {};
		return reinterpret_cast<FileOpenFn>(reinterpret_cast<void*>(GetProcAddress(core, MAKEINTRESOURCEA(OpenOrdinal))));
	}();
	if (!open) return {};
	void* file = open(path);
	if (!file) return {};
	std::string         bytes;
	const std::uint64_t size = reinterpret_cast<FileSizeFn>(Base + Size)(file);
	if (size > 0 && size < (64u << 20)) {
		bytes.resize(size);
		std::uint64_t read = 0;
		if (!reinterpret_cast<FileReadFn>(Base + ReadFile)(file, bytes.data(), size, &read) || read != size) bytes.clear();
	}
	reinterpret_cast<FileCloseFn>(Base + Close)(file);
	return bytes;
}

// A game file, empty when it cannot be read. Game thread.
std::string GameFile(const std::string& path) {
	std::string bytes = LoaderFile(path.c_str());
	if (bytes.empty()) bytes = ArchiveFile(path.c_str());
	if (bytes.empty()) Log(true, "could not read the game's %s", path.c_str());
	return bytes;
}

// ---- resources ----

// Registrations made outside a UI-thread task stay staged (never served), so
// Register queues them and FlushResources registers them on the UI thread.
struct PendingResource {
	std::string                          path, bytes;
	D2RL::Resources::RegistrationHandle* handle;
};
SRWLOCK                      PendingLock = SRWLOCK_INIT;
struct Guard {
	Guard() noexcept { AcquireSRWLockExclusive(&PendingLock); }
	~Guard() { ReleaseSRWLockExclusive(&PendingLock); }
};
std::vector<PendingResource> Pending;
bool                         FlushQueued = false;
// Live registration per path. D2RLoader activates only what is registered
// while the plugin loads, so every path is registered then (with a stand-in
// when its bytes come from the game later) and replaced afterwards.
std::map<std::string, D2RL::Resources::RegistrationHandle> Live;

void __cdecl FlushResources(const D2RL::PluginContext*, void*) noexcept {
	std::vector<PendingResource> work;
	{
		Guard lock;
		work.swap(Pending);
		FlushQueued = false;
	}
	for (auto& item : work) {
		const D2RL::Resources::ResourceRegistration resource {
			.structSize = D2RL::Resources::ResourceRegistrationSize,
			.path       = item.path.c_str(),
			.bytes      = item.bytes.data(),
			.byteCount  = item.bytes.size(),
		};
		if (const auto old = Live.find(item.path); old != Live.end()) {
			(void)ResourceApi->unregisterResource(Context, old->second);
			Live.erase(old);
		}
		D2RL::Resources::RegistrationHandle handle = D2RL::Resources::InvalidHandle;
		const auto result = ResourceApi->registerResource(Context, &resource, &handle);
		if (result != D2RL::Resources::Result::Success) {
			Log(true, "could not register %s (result %u)", item.path.c_str(), static_cast<unsigned>(result));
			continue;
		}
		Live[item.path] = handle;
		if (item.handle) *item.handle = handle;
		else if (&item == &work.front()) {
			D2RL::Resources::RegistrationInfo info { .structSize = D2RL::Resources::RegistrationInfoSize };
			(void)ResourceApi->getRegistrationInfo(Context, handle, &info);
			Log(false, "%zu files registered on the UI thread (first one, %s: state %u)", work.size(), item.path.c_str(), static_cast<unsigned>(info.state));
		}
	}
}

// Queues the UI-thread task that registers what Register queued.
void QueueFlush() noexcept {
	Guard lock;
	if (Pending.empty() || FlushQueued) return;
	FlushQueued = Threads && Threads->runOnUiThread(Context, FlushResources, nullptr) == D2RL::Threads::Result::Success;
	if (!FlushQueued) Log(true, "could not queue the resource registrations");
}

// Serves `bytes` at `path` (under .../d2rloader/shrine-icons/) once the queue
// is flushed.
bool Loading = false; // inside the plugin's load: registrations become live

bool Register(const std::string& path, const std::string& bytes, D2RL::Resources::RegistrationHandle* out = nullptr) noexcept {
	if (!ResourceApi || bytes.empty()) return false;
	if (Loading) {
		const D2RL::Resources::ResourceRegistration resource {
			.structSize = D2RL::Resources::ResourceRegistrationSize,
			.path       = path.c_str(),
			.bytes      = bytes.data(),
			.byteCount  = bytes.size(),
		};
		D2RL::Resources::RegistrationHandle handle = D2RL::Resources::InvalidHandle;
		const auto result = ResourceApi->registerResource(Context, &resource, &handle);
		if (result != D2RL::Resources::Result::Success) {
			Log(true, "could not register %s (result %u)", path.c_str(), static_cast<unsigned>(result));
			return false;
		}
		Live[path] = handle;
		if (out) *out = handle;
		return true;
	}
	if (!Threads) return false;
	Guard lock;
	Pending.push_back({ path, bytes, out });
	return true;
}

// ---- symbol files ----

constexpr const char* NameSpace = "d2rloader/shrine-icons";

std::string Format(const char* format, ...) {
	char    text[256] {};
	va_list args;
	va_start(args, format);
	std::vsnprintf(text, sizeof(text), format, args);
	va_end(args);
	return text;
}

std::string Folder(const char* path) {
	const std::string text = path;
	return text.substr(0, text.find_last_of('/') + 1);
}

// The plugin's copy of an icon's mesh (and the path its bounds are asked by).
std::string MeshPath(const Icon& icon, const char* suffix) {
	return Format("data/hd/vfx/meshes/lit_mesh/%s/%02u%s", NameSpace, icon.code, suffix);
}

// The Skill Shrine effect stores paths as strings prefixed by their length.
bool ReplaceString(std::string& blob, const std::string& from, const std::string& to) {
	if (from.size() != to.size() || from.size() > 255) return false;
	const std::string needle = std::string(1, static_cast<char>(from.size())) + from;
	const auto        at     = blob.find(needle);
	if (at == std::string::npos || blob.find(needle, at + 1) != std::string::npos) return false;
	blob.replace(at + 1, to.size(), to);
	return true;
}

// The effects, meshes and textures, read from the game once. Game thread.
bool RegisterSymbolFiles() {
	const std::string effect = GameFile("data/hd/vfx/particles/overlays/common/shrine_skill/vfx_shrine_skill_looping.particles");
	const std::string glow   = GameFile("data/hd/vfx/textures/mask/t_fx_radialgradient_mask_soft_02.texture");
	if (effect.empty() || glow.empty()) return false;
	const std::string glowPath = Format("data/hd/vfx/textures/graphic/%s/glow_mask.texture", NameSpace);
	if (!Register(glowPath, glow)) return false;
	bool all = true;
	for (const auto& icon : Icons) {
		if (!icon.symbol) continue;
		std::string blob = effect;
		if (!ReplaceString(blob, "data/hd/vfx/meshes/lit_mesh/shrine_skill/shrine_skill.model", MeshPath(icon, ".model"))
		    || !ReplaceString(blob, "data/hd/vfx/meshes/lit_mesh/shrine_skill/shrine_skill_ALB.texture", MeshPath(icon, "_alb.texture"))
		    || !ReplaceString(blob, "data/hd/vfx/textures/graphic/T_FX_Shrine_Skill_Glowmask_BlurB.texture", glowPath)) {
			Log(true, "the game's Skill Shrine effect is not the one this version knows");
			return false;
		}
		const std::string folder = Folder(icon.model);
		all = Register(Format("data/hd/vfx/particles/overlays/%s/%s.particles", NameSpace, icon.key), blob)
		      && Register(MeshPath(icon, "_lod0.model"), GameFile(folder + icon.lod0))
		      && Register(MeshPath(icon, "_alb.texture"), GameFile(folder + icon.albedo)) && all;
	}
	return all;
}

// OverlayDefinitions are cached by path, so each size / height gets paths of
// its own (generation), registered when it is first needed.
constexpr std::uint32_t    DefinitionSlots = 64; // paths registered at load, reused in turn
std::atomic<std::uint32_t> Generation { 0 };
std::uint32_t              Registered = ~0u;

std::string DefinitionPath(const Icon& icon, std::uint32_t generation) {
	return Format("data/hd/overlays/%s/%u/shrine_icon_%s.json", NameSpace, generation, icon.key);
}

std::string Definition(const Icon& icon, double size, double height) {
	const double scale = size * icon.fit;
	// Upright: a quarter turn about X stands it up, then a quarter turn about Y
	// turns its face to the camera (the X turn alone shows it edge-on).
	struct Quat {
		double x, y, z, w;
	};
	const auto mul = [](Quat a, Quat b) {
		return Quat { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
			          a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
	};
	const auto axis = [](int which, double degrees) {
		const double half = degrees * 3.14159265358979323846 / 360.0;
		Quat q { 0.0, 0.0, 0.0, std::cos(half) };
		(which == 0 ? q.x : which == 1 ? q.y : q.z) = std::sin(half);
		return q;
	};
	Quat   q { 0.0, 0.0, 0.0, 1.0 };
	double move[3] {};
	if (icon.upright) {
		q = Quat { 0.5, 0.5, -0.5, 0.5 };
		for (int i = 0; i < 3; ++i) {
			q       = mul(axis(i, Config.gemTurn[i]), q);
			move[i] = Config.gemMove[i];
		}
	}
	char turn[160] {};
	std::snprintf(turn, sizeof(turn), R"("x": %.6f, "y": %.6f, "z": %.6f, "w": %.6f)", q.x, q.y, q.z, q.w);
	const std::string particles = Format("data/hd/vfx/particles/overlays/%s/%s.particles", NameSpace, icon.key);
	char text[2048] {};
	std::snprintf(text, sizeof(text),
	              R"({"dependencies": {"particles": [{"path": "%s"}], "models": [{"path": "%s"}], "skeletons": [], "animations": [],)"
	              R"( "textures": [{"path": "data/hd/vfx/textures/gradient/T_FX_AlphaRemapCurve_blinkfix.texture"}], "physics": [], "json": [], "variantdata": [], "objecteffects": [], "other": []},)"
	              R"( "type": "OverlayDefinition", "name": "shrine_icon_%s", "entities": [{"type": "Entity", "name": "entity_root", "id": %u, "components": [)"
	              R"({"type": "VfxDefinitionComponent", "name": "entity_root_VfxDefinition", "filename": "%s", "hardKillOnDestroy": true},)"
	              R"( {"type": "TransformDefinitionComponent", "name": "entity_root_TransformDefinition", "position": {"x": %.3f, "y": %.3f, "z": %.3f}, "orientation": {%s},)"
	              R"( "scale": {"x": %.4f, "y": %.4f, "z": %.4f}, "inheritOnlyPosition": false}]}]})",
	              particles.c_str(), MeshPath(icon, ".model").c_str(), icon.key, 1907000000u + icon.code, particles.c_str(), move[0], height + move[1], move[2], turn, scale, scale, scale);
	return text;
}

// The definitions for the current size and height. Game thread.
void RegisterDefinitions() {
	const auto generation = Generation.load();
	if (!FilesReady || Registered == generation) return;
	for (const auto& icon : Icons)
		if (icon.symbol) (void)Register(DefinitionPath(icon, generation), Definition(icon, Config.size, Config.height));
	Registered = generation;
}

// ---- overlay rows ----

// Overlay id of the plugin's first row (rows follow in Icons order), -1 none.
std::int32_t FirstRow = -1;
std::array<std::uint8_t*, 4> OwnRows {}; // the row buffers the plugin made, per game version

// Appends a copy of the shrine_skill row per icon to every version's overlay
// table (again after the tables reload). Game thread.
void AddOverlayRows() noexcept {
	if (!CodeIs(0x300AC0, DataTablesBytes.data(), DataTablesBytes.size()) || !CodeIs(OverlayRowRva, OverlayRowBytes.data(), OverlayRowBytes.size())
	    || !CodeIs(OverlayStrideRva, OverlayStrideBytes.data(), OverlayStrideBytes.size())) {
		Log(true, "the game's overlay table is not the one this version knows, no floating symbols");
		return;
	}
	// The versions' tables differ in length (RotW has rows the others lack), so
	// every table is first padded to the longest one: the plugin's rows then
	// have the same ids whichever version the game plays.
	struct Table {
		std::uint8_t*       vector = nullptr;
		std::uint8_t*       rows   = nullptr;
		std::uint64_t       count  = 0;
		const std::uint8_t* skill  = nullptr;
		bool                own    = false;
	};
	std::array<Table, 4> tables {};
	std::uint64_t        longest = 0;
	for (std::uint32_t version = 0; version < 4; ++version) {
		auto* data = *reinterpret_cast<std::uint8_t* const*>(Base + DataTablesRva + version * 16);
		if (!data) continue;
		auto& table  = tables[version];
		table.vector = data + OverlayVector;
		table.rows   = *reinterpret_cast<std::uint8_t**>(table.vector);
		table.count  = *reinterpret_cast<std::uint64_t*>(table.vector + 8);
		if (table.rows && table.rows == OwnRows[version]) {
			table.own = true;
			longest   = std::max<std::uint64_t>(longest, table.count - Icons.size());
			continue;
		}
		if (!table.rows || table.count == 0 || table.count > 4096) {
			table.rows = nullptr;
			continue;
		}
		// shrine_skill: the row holding the NUL-terminated name ShrineSkill.
		for (std::uint64_t row = 0; row < table.count && !table.skill; ++row) {
			const auto* begin = table.rows + row * OverlayRowSize;
			for (std::size_t at = 0; at + 12 <= OverlayRowSize; ++at)
				if ((at == 0 || begin[at - 1] == 0) && std::memcmp(begin + at, "ShrineSkill", 12) == 0) {
					table.skill = begin;
					break;
				}
		}
		if (!table.skill) {
			Log(true, "no shrine_skill row in overlay table %u", version);
			table.rows = nullptr;
			continue;
		}
		longest = std::max(longest, table.count);
	}
	std::int32_t first = -1;
	for (std::uint32_t version = 0; version < 4; ++version) {
		auto& table = tables[version];
		if (table.own) {
			if (table.count - Icons.size() == longest) first = static_cast<std::int32_t>(longest);
			else Log(true, "overlay table %u was made for another length, left alone", version);
			continue;
		}
		if (!table.rows) continue;
		const std::uint64_t total = longest + Icons.size();
		auto* own = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, total * OverlayRowSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
		if (!own) continue;
		std::memcpy(own, table.rows, table.count * OverlayRowSize);
		// Padding (never named by this version's data) and the plugin's rows.
		for (std::uint64_t row = table.count; row < total; ++row)
			std::memcpy(own + row * OverlayRowSize, table.skill, OverlayRowSize);
		*reinterpret_cast<std::uint64_t*>(table.vector + 0x10) = total | NotOwned;
		*reinterpret_cast<std::uint8_t**>(table.vector)        = own;
		*reinterpret_cast<std::uint64_t*>(table.vector + 8)    = total;
		OwnRows[version]                                       = own;
		first                                                  = static_cast<std::int32_t>(longest);
	}
	FirstRow = first;
	for (std::size_t i = 0; i < Icons.size(); ++i)
		Overlay[i] = first >= 0 && Icons[i].symbol ? first + static_cast<std::int32_t>(i) : -1;
}

// The plugin's entries in the game's overlay file list, added again whenever
// the game rebuilds the list; the folder carries the definitions' generation.
std::uint8_t* OwnFiles     = nullptr;
std::uint64_t OwnFilesSize = 0;

bool EnsureOverlayFiles() noexcept {
	if (FirstRow < 0 || Registered == ~0u) return false;
	auto*               list  = reinterpret_cast<std::uint8_t*>(Base + OverlayFilesObj + 0x90);
	auto*               files = *reinterpret_cast<std::uint8_t**>(list);
	const std::uint64_t count = *reinterpret_cast<std::uint64_t*>(list + 8);
	const std::uint64_t total = static_cast<std::uint64_t>(FirstRow) + Icons.size();
	const std::string   folder = Format("/%s/%u/", NameSpace, Registered);
	const auto write = [&](std::uint8_t* entry, const Icon& icon) {
		std::memset(entry, 0, OverlayFileSize);
		std::snprintf(reinterpret_cast<char*>(entry), 0x30, "shrine_icon_%s", icon.key);
		std::snprintf(reinterpret_cast<char*>(entry + 0x30), 0x30, "%s", folder.c_str());
	};
	if (files && files == OwnFiles && count == total) {
		// Ours already: only a new generation changes the folders.
		for (std::size_t i = 0; i < Icons.size(); ++i) {
			auto* entry = files + (FirstRow + i) * OverlayFileSize;
			if (std::strncmp(reinterpret_cast<const char*>(entry + 0x30), folder.c_str(), 0x30) != 0) write(entry, Icons[i]);
		}
		return true;
	}
	if (!files || count == 0 || count > static_cast<std::uint64_t>(FirstRow)) return false; // not built yet, or not the table we know
	// shrine_skill's entry fills the rows this version's data never names.
	const std::uint8_t* skill = nullptr;
	for (std::uint64_t i = 0; i < count && !skill; ++i)
		if (std::strncmp(reinterpret_cast<const char*>(files + i * OverlayFileSize), "shrine_skill", 0x30) == 0) skill = files + i * OverlayFileSize;
	if (!skill) skill = files;
	auto* own = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, total * OverlayFileSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
	if (!own) return false;
	std::memcpy(own, files, count * OverlayFileSize);
	for (std::uint64_t i = count; i < static_cast<std::uint64_t>(FirstRow); ++i)
		std::memcpy(own + i * OverlayFileSize, skill, OverlayFileSize);
	for (std::size_t i = 0; i < Icons.size(); ++i)
		write(own + (FirstRow + i) * OverlayFileSize, Icons[i]);
	// The old buffer stays the game's (it frees it when it rebuilds the list);
	// the capacity flag keeps the game from freeing ours.
	*reinterpret_cast<std::uint64_t*>(list + 0x10) = total | NotOwned;
	*reinterpret_cast<std::uint8_t**>(list)        = own;
	*reinterpret_cast<std::uint64_t*>(list + 8)    = total;
	OwnFiles     = own;
	OwnFilesSize = total;
	Log(false, "overlay file list: %llu entries, the plugin's at %d-%d", static_cast<unsigned long long>(count), FirstRow, FirstRow + static_cast<int>(Icons.size()) - 1);
	return true;
}

// ---- minimap sheets ----

// Painted frames: "SIC1", u32 count, per frame u16 cell, u8 lowend, u8 0,
// u16 width, u16 height, RGBA (tools_make_minimap.py).
constexpr int CellsResource = 200;

std::string_view CellsData() noexcept {
	HMODULE module = nullptr;
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&CellsData), &module)) return {};
	HRSRC info = FindResourceW(module, MAKEINTRESOURCEW(CellsResource), MAKEINTRESOURCEW(10)); // RT_RCDATA
	if (!info) return {};
	HGLOBAL data = LoadResource(module, info);
	const auto* bytes = data ? static_cast<const char*>(LockResource(data)) : nullptr;
	return bytes ? std::string_view(bytes, SizeofResource(module, info)) : std::string_view {};
}

struct Sheet {
	std::uint32_t width = 0, height = 0, frames = 0;
};

// Header of an uncompressed sprite ("SpA1", RGBA after 0x28 bytes).
bool ReadSheet(const std::string& sprite, Sheet& sheet) noexcept {
	if (sprite.size() < 0x28 || std::memcmp(sprite.data(), "SpA1", 4) != 0) return false;
	std::memcpy(&sheet.width, sprite.data() + 8, 4);
	std::memcpy(&sheet.height, sprite.data() + 12, 4);
	std::memcpy(&sheet.frames, sprite.data() + 0x14, 4);
	return sheet.frames && sheet.width % sheet.frames == 0 && sprite.size() == 0x28 + static_cast<std::size_t>(sheet.width) * sheet.height * 4;
}

// Copies the built-in frames of sheet `number` (07 / 13) into `sprite`.
std::uint32_t Paint(std::string& sprite, std::uint32_t number, bool lowend) noexcept {
	Sheet sheet;
	if (!ReadSheet(sprite, sheet)) return 0;
	const std::string_view cells = CellsData();
	if (cells.size() < 8 || cells.substr(0, 4) != "SIC1") return 0;
	std::uint32_t count = 0, painted = 0;
	std::memcpy(&count, cells.data() + 4, 4);
	const std::uint32_t step = sheet.width / sheet.frames;
	std::size_t         at   = 8;
	for (std::uint32_t i = 0; i < count && at + 8 <= cells.size(); ++i) {
		std::uint16_t cell = 0, width = 0, height = 0;
		std::memcpy(&cell, cells.data() + at, 2);
		const bool low = cells[at + 2] != 0;
		std::memcpy(&width, cells.data() + at + 4, 2);
		std::memcpy(&height, cells.data() + at + 6, 2);
		const std::size_t size = static_cast<std::size_t>(width) * height * 4;
		at += 8;
		if (at + size > cells.size()) break;
		const char* pixels = cells.data() + at;
		at += size;
		if (cell / 100 != number || low != lowend || width != step || height != sheet.height || cell % 100 >= sheet.frames) continue;
		for (std::uint32_t y = 0; y < height; ++y)
			std::memcpy(sprite.data() + 0x28 + (static_cast<std::size_t>(y) * sheet.width + (cell % 100) * step) * 4, pixels + static_cast<std::size_t>(y) * width * 4, static_cast<std::size_t>(width) * 4);
		++painted;
	}
	return painted;
}

// A lowend sheet from a full one: each frame at half size in the top left of
// a frame of (step / 2 + 1) pixels, as the game's lowend sheets are laid out.
std::string Halve(const std::string& full) {
	Sheet sheet;
	if (!ReadSheet(full, sheet)) return {};
	const std::uint32_t step = sheet.width / sheet.frames, small = step / 2 + 1, width = small * sheet.frames, height = sheet.height / 2;
	std::string         out(0x28 + static_cast<std::size_t>(width) * height * 4, '\0');
	std::memcpy(out.data(), full.data(), 0x28);
	const std::uint16_t frameWidth = static_cast<std::uint16_t>(*reinterpret_cast<const std::uint16_t*>(full.data() + 6) / 2);
	const std::uint32_t bytes      = width * height * 4;
	std::memcpy(out.data() + 6, &frameWidth, 2);
	std::memcpy(out.data() + 8, &width, 4);
	std::memcpy(out.data() + 12, &height, 4);
	std::memcpy(out.data() + 0x20, &bytes, 4);
	const auto* in = reinterpret_cast<const std::uint8_t*>(full.data() + 0x28);
	auto*       to = reinterpret_cast<std::uint8_t*>(out.data() + 0x28);
	for (std::uint32_t frame = 0; frame < sheet.frames; ++frame)
		for (std::uint32_t y = 0; y < height; ++y)
			for (std::uint32_t x = 0; x < step / 2; ++x)
				for (int c = 0; c < 4; ++c) {
					unsigned sum = 0;
					for (int dy = 0; dy < 2; ++dy)
						for (int dx = 0; dx < 2; ++dx)
							sum += in[((static_cast<std::size_t>(y) * 2 + dy) * sheet.width + frame * step + x * 2 + dx) * 4 + c];
					to[(static_cast<std::size_t>(y) * width + frame * small + x) * 4 + c] = static_cast<std::uint8_t>((sum + 2) / 4);
				}
	return out;
}

struct SheetState {
	std::string name;   // the name the plugin answers with, empty until made
	bool        failed = false;
	bool        warned = false;
	D2RL::Resources::RegistrationHandle handles[2] {};
};
SheetState Sheets[2]; // default13, default07
constexpr std::uint32_t SheetNumber[2] { 13, 7 };

// Index into Sheets of a tileset sprite name, -1 for others.
int SheetOf(const char* name) noexcept {
	std::string text = name;
	for (auto& c : text)
		c = c == '\\' ? '/' : static_cast<char>(c >= 'A' && c <= 'Z' ? c + 0x20 : c);
	if (text.find("automap") == std::string::npos || text.find(NameSpace) != std::string::npos) return -1;
	if (text.find("default13") != std::string::npos) return 0;
	if (text.find("default07") != std::string::npos) return 1;
	return -1;
}

// Paints the plugin's copies of a sheet and serves them. Game thread.
bool MakeSheet(int index, const char* name) {
	auto& state = Sheets[index];
	if (!state.name.empty() || state.failed) return !state.name.empty();
	state.failed = true;
	std::string base = name;
	for (auto& c : base)
		if (c == '\\') c = '/';
	if (base.size() > 7 && base.compare(base.size() - 7, 7, ".sprite") == 0) base.resize(base.size() - 7);
	std::string full = GameFile("data/hd/global/ui/" + base + ".sprite");
	std::string low  = GameFile("data/hd/global/ui/" + base + ".lowend.sprite");
	const auto  number = SheetNumber[index];
	const auto  done   = Paint(full, number, false);
	if (!done) {
		Log(true, "automap sheet %s: no minimap icons (the sheet is not the one this version knows)", base.c_str());
		return false;
	}
	if (!Paint(low, number, true)) low = Halve(full);
	// Resource paths are looked up lowercase.
	std::string lower = base;
	for (auto& c : lower)
		c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 0x20 : c);
	const std::string ours = std::string(NameSpace) + "/" + lower;
	if (low.empty() || !Register("data/hd/global/ui/" + ours + ".sprite", full, &state.handles[0]) || !Register("data/hd/global/ui/" + ours + ".lowend.sprite", low, &state.handles[1])) return false;
	state.name   = ours;
	state.failed = false;
	Log(false, "automap sheet %s: %u minimap icons painted", base.c_str(), done);
	return true;
}

// Both copies of a sheet are live in the resource registry; otherwise the game
// keeps its own sheet (a missing one blanks every cell it holds).
bool SheetActive(int index) noexcept {
	auto& state = Sheets[index];
	for (int i = 0; i < 2; ++i) {
		D2RL::Resources::RegistrationInfo info { .structSize = D2RL::Resources::RegistrationInfoSize };
		const auto result = ResourceApi && ResourceApi->getRegistrationInfo ? ResourceApi->getRegistrationInfo(Context, state.handles[i], &info) : D2RL::Resources::Result::Unsupported;
		if (result != D2RL::Resources::Result::Success || info.state != D2RL::Resources::RegistrationState::Active) {
			if (!state.warned) Log(true, "automap sheet %s: copy %d not live (result %u, state %u), the game's own sheet is kept", state.name.c_str(), i, static_cast<unsigned>(result), static_cast<unsigned>(info.state));
			state.warned = true;
			return false;
		}
	}
	return true;
}

// Paints and registers both sheets ahead of the automap's first load, so the
// registrations are live by then. Game thread.
void PrepareSheets() noexcept {
	static constexpr const char* Names[2] { "AUTOMAP/Default13/Tiles", "AUTOMAP/Default07/Tiles" };
	for (int index = 0; index < 2; ++index)
		MakeSheet(index, Names[index]);
}

using SpriteLoadFn = void (*)(void* sprite, const char* name, std::uint8_t flag) noexcept;
using IsHdFn       = bool (*)() noexcept;

// The tileset loader's sprite load: default13 and default07 come from the
// plugin. Game thread.
void TileLoad(void* sprite, const char* name, std::uint8_t flag) noexcept {
	const auto load = reinterpret_cast<SpriteLoadFn>(Base + SpriteLoadRva);
	const int  index = name ? SheetOf(name) : -1;
	if (index >= 0 && reinterpret_cast<IsHdFn>(Base + IsHdRva)() && !Sheets[index].name.empty() && SheetActive(index)) {
		if (index == 0)
			for (const auto& icon : Icons)
				MinimapCell[icon.code] = icon.cell;
		else
			for (std::uint32_t i = 0; i < BoosterCell.size(); ++i)
				MinimapCell[FirstBooster + i] = BoosterCell[i];
		SheetPainted[index] = true;
		UpdateRemap();
		Log(false, "automap sheet %s served as %s (flag %u)", name, Sheets[index].name.c_str(), static_cast<unsigned>(flag));
		load(sprite, Sheets[index].name.c_str(), flag);
		return;
	}
	load(sprite, name, flag);
}

// The tileset loader calls TileLoad through a stub (written before the page is sealed).
std::uint8_t* TileStub {};

bool BuildTileStub() noexcept {
	auto* stub = NewStub(16);
	if (!stub) return false;
	stub[0] = 0xFF, stub[1] = 0x25; // jmp [rip]
	std::memset(stub + 2, 0, 4);
	const auto target = reinterpret_cast<std::uint64_t>(&TileLoad);
	std::memcpy(stub + 6, &target, 8);
	TileStub = stub;
	return true;
}

bool PatchTileLoad() noexcept {
	std::int32_t original = 0, rel = 0;
	if (!TileStub || !Rel32(Base + TileLoadCallRva, reinterpret_cast<void*>(Base + SpriteLoadRva), original) || !Rel32(Base + TileLoadCallRva, TileStub, rel)) return false;
	std::array<std::uint8_t, 5> expected { 0xE8 }, call { 0xE8 };
	std::memcpy(expected.data() + 1, &original, 4);
	std::memcpy(call.data() + 1, &rel, 4);
	return Patch(TileLoadCallRva, expected.data(), call.data(), 5);
}

// ---- load-time registrations ----

// Every path the plugin serves, registered while it loads: the definitions
// with their real text (all slots), the rest with a stand-in that the real
// bytes replace once the game's files can be read (tables loaded).
void RegisterStandIns() noexcept {
	const std::string standIn(16, '\0');
	Loading = true;
	if (SymbolsReady) {
		(void)Register(Format("data/hd/vfx/textures/graphic/%s/glow_mask.texture", NameSpace), standIn);
		for (const auto& icon : Icons) {
			if (!icon.symbol) continue;
			(void)Register(Format("data/hd/vfx/particles/overlays/%s/%s.particles", NameSpace, icon.key), standIn);
			(void)Register(MeshPath(icon, "_lod0.model"), standIn);
			(void)Register(MeshPath(icon, "_alb.texture"), standIn);
			for (std::uint32_t slot = 0; slot < DefinitionSlots; ++slot)
				(void)Register(DefinitionPath(icon, slot), Definition(icon, Config.size, Config.height));
		}
		Registered = Generation.load(); // its definitions are the ones just registered
	}
	if (OriginalAddCell)
		for (const char* sheet : { "automap/default13/tiles", "automap/default07/tiles" }) {
			(void)Register(Format("data/hd/global/ui/%s/%s.sprite", NameSpace, sheet), standIn);
			(void)Register(Format("data/hd/global/ui/%s/%s.lowend.sprite", NameSpace, sheet), standIn);
		}
	Loading = false;
	Log(false, "%zu paths registered at load", Live.size());
}

// ---- tables loaded ----

void __cdecl OnTablesLoaded(const D2RL::PluginContext*, const D2RL::Lifecycle::DataTablesLoadedEvent* event, void*) noexcept {
	if (SymbolsReady) {
		if (!FilesReady) FilesReady = RegisterSymbolFiles();
		RegisterDefinitions();
		if (FilesReady) AddOverlayRows();
		EnsureOverlayFiles();
	}
	if (OriginalAddCell) PrepareSheets();
	QueueFlush();
	UpdateRemap();
	RefreshSymbols();
	const unsigned long long revision = event ? event->revision : 0;
	if (FirstRow >= 0) Log(false, "tables rev %llu: symbol overlays %d-%d", revision, FirstRow, FirstRow + static_cast<int>(Icons.size()) - 1);
	else if (SymbolsReady) Log(true, "tables rev %llu: no floating symbols (files %s)", revision, FilesReady ? "ready" : "missing");
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

// Applies one setting from its TOML text (strings unquoted); false + reason when refused.
bool Apply(const std::string& key, const std::string& value, std::string& error) {
	const auto flag = [&](std::atomic<bool>& target) {
		if (value != "true" && value != "false") return error = "true or false", false;
		target = value == "true";
		return true;
	};
	if (key == "enabled") return flag(Config.enabled);
	if (key == "symbols") return flag(Config.symbols);
	if (key == "minimap") return flag(Config.minimap);
	if (key == "boosters") return flag(Config.boosters);
	if (key == "shimmer") return flag(Config.shimmer);
	const auto number = [&](std::atomic<double>& target, double low, double high) {
		char*        end   = nullptr;
		const double parsed = std::strtod(value.c_str(), &end);
		if (value.empty() || !end || *end || !std::isfinite(parsed)) return error = "a number", false;
		if (parsed < low || parsed > high) return error = Format("from %g to %g", low, high), false;
		if (target != parsed) Generation = (Generation + 1) % DefinitionSlots;
		target = parsed;
		return true;
	};
	if (key == "size") return number(Config.size, 0.25, 4.0);
	if (key == "height") return number(Config.height, 0.0, 120.0);
	for (int i = 0; i < 3; ++i) {
		if (key == Format("gem_turn_%c", "xyz"[i])) return number(Config.gemTurn[i], -180.0, 180.0);
		if (key == Format("gem_move_%c", "xyz"[i])) return number(Config.gemMove[i], -60.0, 60.0);
	}
	for (std::size_t i = 0; i < Icons.size(); ++i)
		if (key == Icons[i].key) return flag(IconOn[i]);
	return error = "unknown setting", false;
}

std::string ValueOf(const std::string& key) {
	const auto flag = [](bool on) { return std::string(on ? "true" : "false"); };
	if (key == "enabled") return flag(Config.enabled);
	if (key == "symbols") return flag(Config.symbols);
	if (key == "minimap") return flag(Config.minimap);
	if (key == "boosters") return flag(Config.boosters);
	if (key == "shimmer") return flag(Config.shimmer);
	if (key == "size") return Format("%g", Config.size.load());
	if (key == "height") return Format("%g", Config.height.load());
	for (int i = 0; i < 3; ++i) {
		if (key == Format("gem_turn_%c", "xyz"[i])) return Format("%g", Config.gemTurn[i].load());
		if (key == Format("gem_move_%c", "xyz"[i])) return Format("%g", Config.gemMove[i].load());
	}
	for (std::size_t i = 0; i < Icons.size(); ++i)
		if (key == Icons[i].key) return flag(IconOn[i]);
	return {};
}

// The whole config file, comments included (shrine-icons.toml with the current values).
std::string Render() {
	std::string toml = R"(# Shrine Icons
#
# Shows a floating symbol over the shrines the game leaves bare, the way booster
# shrines (armor, combat, resists, skill, mana regen, stamina, experience)
# already show one, and gives every shrine type its own minimap icon. No mod
# needed: the plugin makes the art from the game's own files.
# Every setting can also be changed in game with I Hate TOMLs, and applies at once.

[shrine-icons]
enabled = )" + ValueOf("enabled") + R"(

# Floating symbol over the shrine.
symbols = )" + ValueOf("symbols") + R"(

# Minimap / automap icon of the shrine's type (an item picture with a gold
# outline) instead of the shared gold ring.
minimap = )" + ValueOf("minimap") + R"(

# Minimap icons for the booster shrines too (armor, combat, the four resists,
# skill, mana recharge, stamina, experience), which already float a symbol.
boosters = )" + ValueOf("boosters") + R"(

# Also give the shrines with a new symbol the faint shimmer booster shrines
# have.
shimmer = )" + ValueOf("shimmer") + R"(

# Size of the new symbols, 1 = the size of the booster shrine symbols
# (0.25 to 4).
size = )" + ValueOf("size") + R"(

# How high above the shrine the symbol floats, 14 = the height of the booster
# shrine symbols (0 to 120).
height = )" + ValueOf("height") + R"(

# Which shrines get a symbol and a minimap icon (minimap picture in brackets;
# fire, exploding and poison all float a burning skull).
)";
	const char* notes[Icons.size()] {
		"Refilling Shrine (rejuvenation potion)",
		"Health Shrine (healing potion; minimap only, the pool shows it)",
		"Mana Shrine (mana potion; minimap only, the pool shows it)",
		"Portal Shrine (town portal scroll)",
		"Gem Shrine (amethyst)",
		"Fire Shrine (torch)",
		"Monster Shrine (skull)",
		"Exploding Shrine (exploding potion)",
		"Poison Shrine (choking gas potion)",
	};
	for (std::size_t i = 0; i < Icons.size(); ++i) {
		char line[160] {};
		const std::string value = ValueOf(Icons[i].key);
		std::snprintf(line, sizeof(line), "%-9s = %-5s  # %s\n", Icons[i].key, value.c_str(), notes[i]);
		toml += line;
	}
	return toml;
}

void SaveConfig() {
	const std::string toml = Render();
	if (!Context->WriteConfig(toml.c_str())) Log(true, "config: could not write the config file");
}

void ApplyText(std::string_view text) {
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
		std::string       error;
		if (!Apply(key, value, error)) Log(true, "config: %s = %s ignored (%s)", key.c_str(), value.c_str(), error.c_str());
	}
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
	ApplyText(std::string_view(buffer.data(), std::strlen(buffer.data())));
}

// ---- I Hate TOMLs ----

constexpr const char* Schema = R"toml(title = "Shrine Icons"
[[setting]]
key = "enabled"
type = "bool"
label = "Shrine Icons"
note = "Off: shrines look as in the game."
[[setting]]
key = "symbols"
type = "bool"
label = "Floating symbols"
note = "Over the shrines the game leaves bare."
[[setting]]
key = "shimmer"
type = "bool"
label = "Shimmer"
note = "The faint glow of booster shrines, on the new symbols."
[[setting]]
key = "minimap"
type = "bool"
label = "Minimap icons"
note = "One per shrine type instead of the gold ring."
[[setting]]
key = "boosters"
type = "bool"
label = "Booster minimap icons"
note = "Armor, combat, resists, skill, mana recharge, stamina, experience."
[[setting]]
key = "size"
type = "float"
label = "Symbol size"
note = "1 = the size of the booster shrine symbols."
min = 0.25
max = 4.0
step = 0.05
decimals = 2
shift_step = 0.25
[[setting]]
key = "height"
type = "float"
label = "Symbol height"
note = "How high it floats; 14 = booster shrine symbols."
min = 0
max = 120
step = 1
decimals = 0
shift_step = 10
[[setting]]
type = "header"
label = "Gem tuning (test build, not saved)"
[[setting]]
key = "gem_turn_x"
type = "float"
label = "Gem turn X"
note = "Degrees about the world X axis."
min = -180
max = 180
step = 15
decimals = 0
shift_step = 90
[[setting]]
key = "gem_turn_y"
type = "float"
label = "Gem turn Y (vertical)"
note = "Degrees about the vertical axis."
min = -180
max = 180
step = 15
decimals = 0
shift_step = 90
[[setting]]
key = "gem_turn_z"
type = "float"
label = "Gem turn Z"
note = "Degrees about the world Z axis."
min = -180
max = 180
step = 15
decimals = 0
shift_step = 90
[[setting]]
key = "gem_move_x"
type = "float"
label = "Gem move X"
min = -60
max = 60
step = 1
decimals = 0
shift_step = 5
[[setting]]
key = "gem_move_y"
type = "float"
label = "Gem move Y (up)"
min = -60
max = 60
step = 1
decimals = 0
shift_step = 5
[[setting]]
key = "gem_move_z"
type = "float"
label = "Gem move Z"
min = -60
max = 60
step = 1
decimals = 0
shift_step = 5
[[setting]]
type = "header"
label = "Shrines"
[[setting]]
key = "refill"
type = "bool"
label = "Refilling"
[[setting]]
key = "health"
type = "bool"
label = "Health"
note = "Minimap only, the pool shows it."
[[setting]]
key = "mana"
type = "bool"
label = "Mana"
note = "Minimap only, the pool shows it."
[[setting]]
key = "portal"
type = "bool"
label = "Portal"
[[setting]]
key = "gem"
type = "bool"
label = "Gem"
[[setting]]
key = "fire"
type = "bool"
label = "Fire"
[[setting]]
key = "monster"
type = "bool"
label = "Monster"
[[setting]]
key = "exploding"
type = "bool"
label = "Exploding"
[[setting]]
key = "poison"
type = "bool"
label = "Poison"
)toml";

PluginMenu::Connection Menu;

bool __cdecl MenuGet(const char* key, char* out, std::uint32_t size, void*) noexcept {
	if (!key) return false;
	const std::string value = ValueOf(key);
	if (value.empty()) return false;
	std::snprintf(out, size, "%s", value.c_str());
	return true;
}

// Runs on the game's main thread: the shrines on screen change at once.
bool __cdecl MenuSet(const char* key, const char* value, char* error, std::uint32_t size, void*) noexcept {
	if (!key || !value) return false;
	std::string reason;
	if (!Apply(key, Unquote(Trim(value)), reason)) {
		std::snprintf(error, size, "%s", reason.c_str());
		return false;
	}
	SaveConfig();
	RegisterDefinitions();
	QueueFlush();
	EnsureOverlayFiles();
	UpdateRemap();
	RefreshSymbols();
	return true;
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
	Base    = context->exeBase;
	for (auto& on : IconOn)
		on = true;
	Overlay.fill(-1);
	IconOf.fill(-1);
	for (std::size_t i = 0; i < Icons.size(); ++i)
		IconOf[Icons[i].code] = static_cast<std::int32_t>(i);
	LoadConfig();
	(void)Menu.Start(context, Schema, MenuGet, MenuSet, nullptr);
	// Everything is hooked even when turned off: it can be turned on while playing.
	if (!Base) {
		Log(true, "Shrine Icons %s: inactive, no game base address", PluginVersion);
		return true;
	}
	ResourceApi = Query<D2RL::ResourceService>("resource");
	Lifecycle   = Query<D2RL::LifecycleService>("lifecycle");
	Threads     = Query<D2RL::ThreadService>("thread");
	if (Threads && !D2RL::HasThreadServiceField(Threads, D2RL::ThreadServiceRequiredSize)) Threads = nullptr;
	if (!D2RL::HasResourceServiceField(ResourceApi, D2RL::ResourceServiceRequiredSize) || !D2RL::HasLifecycleServiceField(Lifecycle, D2RL::LifecycleServiceRequiredSize) || !Threads) {
		Log(true, "Shrine Icons %s: inactive, this D2RLoader lacks a needed service", PluginVersion);
		return true;
	}
	if (!GameMatches()) {
		Log(true, "Shrine Icons %s: no floating symbols, the game's shrine table is not the one this version knows", PluginVersion);
	} else {
		HashModels();
		if (!Context->InstallInlineHook(FindModelDescRva, FindModelDescBytes.data(), static_cast<std::uint32_t>(FindModelDescBytes.size()), HookFindModelDesc,
		                                &OriginalFindModelDesc) ||
		    !OriginalFindModelDesc) {
			Log(true, "Shrine Icons %s: no floating symbols, the game's model table code is not the one this version knows", PluginVersion);
			OriginalFindModelDesc = nullptr;
		} else if (!CodeIs(OverlayFilesRva, OverlayFilesBytes.data(), OverlayFilesBytes.size()) || !CodeIs(OverlayFileCountRva, OverlayFileCountBytes.data(), OverlayFileCountBytes.size())
		           || !CodeIs(OverlayFileListRva, OverlayFileListBytes.data(), OverlayFileListBytes.size())) {
			Log(true, "Shrine Icons %s: no floating symbols, the game's overlay file list is not the one this version knows", PluginVersion);
		} else if (!PatchTable()) {
			Log(true, "Shrine Icons %s: no floating symbols, could not change the shrine table", PluginVersion);
		} else {
			SymbolsReady = true;
		}
	}
	// Minimap: the room scan hook, then the stubs (tile sheets, draw), then their jumps.
	bool minimap = Context->InstallInlineHook(AddCellRva, AddCellBytes.data(), static_cast<std::uint32_t>(AddCellBytes.size()), HookAddCell, &OriginalAddCell) && OriginalAddCell;
	if (!minimap) OriginalAddCell = nullptr;
	const bool tileStub = minimap && BuildTileStub();
	const bool drawStub = minimap && BuildDrawStub();
	if (minimap && SealStubs()) {
		minimap     = tileStub && PatchTileLoad();
		RemapActive = minimap && drawStub && PatchDraw();
	} else {
		minimap = false;
	}
	if (!minimap) Log(true, "Shrine Icons %s: no minimap icons, the game's automap code is not the one this version knows", PluginVersion);
	else if (!RemapActive) Log(true, "Shrine Icons %s: minimap setting changes apply to newly revealed shrines only (automap draw code differs)", PluginVersion);
	if (!SymbolsReady && !minimap) {
		Log(true, "Shrine Icons %s: inactive", PluginVersion);
		return true;
	}
	const D2RL::Lifecycle::DataTablesLoadedListener listener {
		.structSize = D2RL::Lifecycle::DataTablesLoadedListenerSize,
		.callback   = OnTablesLoaded,
	};
	const auto result = Lifecycle->registerDataTablesLoadedListener(context, &listener, &TablesListener);
	if (result != D2RL::Lifecycle::Result::Success) Log(true, "Shrine Icons %s: no floating symbols, data table listener refused (result %u)", PluginVersion, static_cast<unsigned>(result));
	RegisterStandIns();
	Log(false, "Shrine Icons %s: %s (symbols %s, minimap %s%s)", PluginVersion, Config.enabled ? "active" : "loaded but turned off", SymbolsReady ? "ready" : "unavailable",
	    minimap ? "ready" : "unavailable", RemapActive ? ", live" : "");
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	Menu.Stop();
}
