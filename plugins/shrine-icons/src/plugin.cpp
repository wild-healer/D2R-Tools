// Shrine Icons: a floating symbol over the shrines the game leaves bare, and
// a minimap icon per shrine type.
//
// Booster shrines show a symbol over the shrine because of a client table with
// one entry per shrine code (shrines.txt "Code"). An entry holds the function
// run when the shrine appears or changes mode, the function run when it is
// used, the symbol overlay, a shimmer overlay and a sound. Booster entries name
// a shared function that removes both overlays and adds them again while the
// shrine is idle (mode 0), so a used shrine loses its symbol and a recharged
// one gets it back.
//
// The plugin gives the other shrine codes that same function and an overlay of
// their own. The overlays are rows the "Shrine Icons" D2RMM mod appends to
// overlay.txt; the plugin finds them by their Filename once the data tables
// are loaded.
//
// Native layout (Steam 3.3.93787 / 94995):
//   - Shrine table 0x1CDA280: 23 entries of 0x20 bytes
//       +0x00 void (*)(Unit*, ShrinesTxt*) on appear / mode change
//       +0x08 void (*)(Unit*, Unit* user, ShrinesTxt*) on use
//       +0x10 int symbol overlay (-1 none), +0x14 int shimmer overlay
//       +0x18 int sound
//   - 0x1CA3E0: the booster function above (entries 6-15).
//   - AddAutomapCell 0xD52B0 (Unit*, int cell, void* ctx): its only caller
//     walks the units of a newly revealed room and passes objects.txt AutoMap,
//     310 for every shrine. Unit +0x00 type (2 = object), +0x10 ObjectData;
//     ObjectData +0x10 ShrinesTxt*, whose first byte is the shrine code.
// The table and the functions are checked byte for byte before anything is
// written; a mismatch leaves the game untouched.
//
// Models: the renderer takes a model's bounds from data/hd/model_lod_desc.bin,
// keyed by the FNV-1a hash of the lowercased path (lookup 0xF75AF0). A path the
// file does not list gets an empty box and is culled, so the mod's copies of the
// item models (under new paths, since the effect files only take paths of the
// original length) are looked up under the item model they copy.
//
// Minimap: the D2RMM mod paints one variant of the gold shrine cell per type
// (booster shrines included, BoosterCell)
// into automap cells no data file uses (MinimapCell); the hook hands those
// cells out instead of 310. The number of automap tiles does not change, so
// saved maps stay valid.

#include <D2RLPlugin/api.h>
#include <windows.h>

#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr const char* PluginId      = "shrine-icons";
constexpr const char* PluginVersion = "1.0.0";

const D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = PluginId,
	.name        = "Shrine Icons",
	.version     = PluginVersion,
	.author      = "wild-healer",
	.description = "Floating symbols over the shrines that have none (refill, health, mana, portal, gem, fire, monster, exploding, poison).",
	.flags       = D2RL::PluginFlags::Client | D2RL::PluginFlags::NativeHooks,
};

namespace Tables = D2RL::DataTables;

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
constexpr std::uint32_t  ObjectUnit    = 2;
constexpr std::uintptr_t UnitData      = 0x10;
constexpr std::uintptr_t ObjectShrine  = 0x10;

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
	const char*   key;      // config key
	std::uint32_t code;     // shrines.txt Code
	const char*   filename; // overlay.txt Filename of the mod's row
	std::int32_t  cell;     // automap cell the mod paints for it
	const char*   model;    // model the mod copies for the symbol
	bool          symbol;   // false: minimap icon only
	bool          enabled = true;
	std::int32_t  overlay = -1;
	bool          patched = false;
};

std::array<Icon, 9> Icons { {
	// Cells: MINIMAP_CELLS in tools_make_mod_files.py.
	// Models: ICONS in tools_make_mod_files.py. Health and mana shrines get no
	// symbol: their coloured pool already tells them apart.
	{ "refill", 1, "ShrineIconRefill", 1332, "data/hd/items/misc/potion/full_rejuv_potion/full_rejuv_potion.model", true },
	{ "health", 2, "ShrineIconHealth", 1335, "data/hd/items/misc/potion/full_healing_potion/full_healing_potion.model", false },
	{ "mana", 3, "ShrineIconMana", 1358, "data/hd/items/misc/potion/full_mana_potion/full_mana_potion.model", false },
	{ "portal", 17, "ShrineIconPortal", 1359, "data/hd/items/misc/scroll/town_portal_scroll/town_portal_scroll.model", true },
	{ "gem", 18, "ShrineIconGem", 1369, "data/hd/items/misc/gem/perfect_amethyst/perfect_amethyst.model", true },
	{ "fire", 19, "ShrineIconFire", 1386, "data/hd/vfx/meshes/missiles/firehead/firehead.model", true },
	{ "monster", 20, "ShrineIconMonster", 1387, "data/hd/items/misc/gem/perfect_skull/perfect_skull.model", true },
	{ "exploding", 21, "ShrineIconExploding", 1388, "data/hd/vfx/meshes/missiles/firehead/firehead.model", true },
	{ "poison", 22, "ShrineIconPoison", 1389, "data/hd/vfx/meshes/missiles/firehead/firehead.model", true },
} };

struct Settings {
	bool enabled = true;
	bool shimmer = true;
	bool symbols = true;
	bool minimap = true;
	bool boosters = true; // minimap icons for booster shrines too
} Config;

// Booster shrines (codes 6-15) keep the game's floating symbol but get a minimap
// icon of their own, in code order. Cells: BOOSTER_CELLS in
// tools_make_mod_files.py.
constexpr std::uint32_t FirstBooster = 6;
constexpr std::array<std::int32_t, 10> BoosterCell { 700, 701, 702, 703, 704, 705, 706, 707, 715, 716 };

// Automap cell per shrine code, 0 = keep the game's. Written on the game
// thread when the tables load, read by the hook on the same thread.
std::array<std::int32_t, EntryCount> MinimapCell {};

const D2RL::PluginContext*    Context {};
const D2RL::DataTableService* TablesApi {};
const D2RL::LifecycleService* Lifecycle {};
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

bool Read(std::uintptr_t address, void* output, std::size_t size) noexcept {
	SIZE_T copied = 0;
	return address != 0 && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), output, size, &copied) && copied == size;
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
	return true;
}

// ---- minimap ----

using AddCellFn = void (*)(void* unit, std::int32_t cell, void* context) noexcept;
AddCellFn OriginalAddCell {};

void HookAddCell(void* unit, std::int32_t cell, void* context) noexcept {
	if (cell == ShrineCell && unit && *static_cast<const std::uint32_t*>(unit) == ObjectUnit) {
		const auto* data   = *reinterpret_cast<const std::uint8_t* const*>(static_cast<const std::uint8_t*>(unit) + UnitData);
		const auto* shrine = data ? *reinterpret_cast<const std::uint8_t* const*>(data + ObjectShrine) : nullptr;
		if (shrine && *shrine < EntryCount && MinimapCell[*shrine]) cell = MinimapCell[*shrine];
	}
	OriginalAddCell(unit, cell, context);
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

// Hash of the mod's copy of each icon's model, and of the item model it copies.
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
		std::snprintf(path, sizeof(path), "data/hd/vfx/meshes/lit_mesh/shrine_ic_%02u/shrine_ic_%02u.model", Icons[i].code, Icons[i].code);
		CopyHash[i] = PathHash(path);
		ItemHash[i] = PathHash(Icons[i].model);
	}
}

// ---- overlay rows ----

// Row index of the overlay.txt row whose Filename is `filename`, -1 if none.
// The row layout is not documented, so the name is searched for as a whole
// NUL-terminated string anywhere in the row.
std::int32_t FindOverlay(const Tables::TableView& view, const char* filename) noexcept {
	const auto  length = std::strlen(filename);
	const auto* rows   = static_cast<const std::uint8_t*>(view.rows);
	for (std::uint32_t row = 0; row < view.rowCount; ++row) {
		const auto* begin = rows + static_cast<std::size_t>(row) * view.rowSize;
		for (std::uint32_t at = 0; at + length < view.rowSize; ++at) {
			if ((at == 0 || begin[at - 1] == 0) && begin[at + length] == 0 && std::memcmp(begin + at, filename, length) == 0) return static_cast<std::int32_t>(row);
		}
	}
	return -1;
}

bool OverlayTable(Tables::TableView& view) noexcept {
	for (const auto bank : { Tables::Bank::Rotw, Tables::Bank::Lod, Tables::Bank::Classic }) {
		view = Tables::TableView { .structSize = Tables::TableViewSize };
		if (TablesApi->getTable(Context, bank, Tables::TableId::Overlay, &view) == Tables::Result::Success && view.rows && view.rowSize && view.rowCount) return true;
	}
	return false;
}

// ---- patching ----

bool Patch(std::uint64_t rva, const void* expected, const void* bytes, std::uint32_t size) noexcept {
	return Context->PatchBytes(rva, expected, size, bytes, size);
}

// Gives one shrine code the booster function and its overlay. Only the fields
// this plugin owns are written; the "on use" function stays as it is.
bool PatchEntry(Icon& icon) noexcept {
	Entry current {};
	if (!ReadEntry(icon.code, current)) return false;
	const std::uint64_t rva      = ShrineTableRva + icon.code * EntrySize;
	const std::uint64_t onAppear = Base + BoosterFnRva;
	const std::int32_t  overlays[2] { icon.overlay, Config.shimmer ? Shimmer00 : -1 };
	const std::int32_t  before[2] { current.symbol, current.shimmer };
	if (current.onAppear != onAppear && !Patch(rva, &current.onAppear, &onAppear, sizeof(onAppear))) return false;
	if (std::memcmp(before, overlays, sizeof(overlays)) != 0 && !Patch(rva + offsetof(Entry, symbol), before, overlays, sizeof(overlays))) return false;
	return true;
}

void __cdecl OnTablesLoaded(const D2RL::PluginContext*, const D2RL::Lifecycle::DataTablesLoadedEvent* event, void*) noexcept {
	Tables::TableView view {};
	if (!OverlayTable(view)) {
		Log(true, "overlay table unavailable, no shrine symbols");
		return;
	}
	std::string missing, done;
	bool        installed = false;
	for (const auto& icon : Icons)
		installed = installed || FindOverlay(view, icon.filename) >= 0;
	// The mod is installed, so its automap cells are painted.
	for (std::uint32_t i = 0; i < BoosterCell.size(); ++i)
		MinimapCell[FirstBooster + i] = installed && OriginalAddCell && Config.boosters ? BoosterCell[i] : 0;
	for (auto& icon : Icons) {
		if (!icon.enabled) continue;
		const auto overlay = FindOverlay(view, icon.filename);
		if (overlay < 0) {
			missing += missing.empty() ? icon.key : std::string(", ") + icon.key;
			MinimapCell[icon.code] = 0;
			continue;
		}
		if (OriginalAddCell) MinimapCell[icon.code] = icon.cell;
		if (!Config.symbols || !icon.symbol || (icon.patched && icon.overlay == overlay)) continue;
		icon.overlay = overlay;
		icon.patched = PatchEntry(icon);
		if (icon.patched) done += (done.empty() ? "" : ", ") + std::string(icon.key) + "=" + std::to_string(overlay);
		else Log(true, "could not change the %s shrine entry", icon.key);
	}
	const unsigned long long revision = event ? event->revision : 0;
	if (!done.empty()) Log(false, "tables rev %llu: symbols set for %s", revision, done.c_str());
	if (!missing.empty()) Log(true, "tables rev %llu: no overlay row for %s; is the Shrine Icons D2RMM mod installed?", revision, missing.c_str());
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
		const bool        on    = value != "false";
		if (key == "enabled") Config.enabled = on;
		else if (key == "shimmer") Config.shimmer = on;
		else if (key == "symbols") Config.symbols = on;
		else if (key == "minimap") Config.minimap = on;
		else if (key == "boosters") Config.boosters = on;
		else {
			bool known = false;
			for (auto& icon : Icons)
				if (key == icon.key) icon.enabled = on, known = true;
			if (!known) Log(true, "config: unknown key %s", key.c_str());
		}
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
		Log(false, "Shrine Icons %s: disabled in config", PluginVersion);
		return true;
	}
	Base = context->exeBase;
	if (Config.symbols && !GameMatches()) {
		Log(true, "Shrine Icons %s: no floating symbols, the game's shrine table is not the one this version knows", PluginVersion);
		Config.symbols = false;
	}
	if (Config.symbols) {
		HashModels();
		if (!Context->InstallInlineHook(FindModelDescRva, FindModelDescBytes.data(), static_cast<std::uint32_t>(FindModelDescBytes.size()), HookFindModelDesc,
		                                &OriginalFindModelDesc) ||
		    !OriginalFindModelDesc) {
			Log(true, "Shrine Icons %s: no floating symbols, the game's model table code is not the one this version knows", PluginVersion);
			OriginalFindModelDesc = nullptr;
			Config.symbols        = false;
		}
	}
	if (Config.minimap &&
	    (!Context->InstallInlineHook(AddCellRva, AddCellBytes.data(), static_cast<std::uint32_t>(AddCellBytes.size()), HookAddCell, &OriginalAddCell) || !OriginalAddCell)) {
		Log(true, "Shrine Icons %s: no minimap icons, the game's automap code is not the one this version knows", PluginVersion);
		OriginalAddCell = nullptr;
	}
	if (!Config.symbols && !OriginalAddCell) {
		Log(true, "Shrine Icons %s: inactive", PluginVersion);
		return true;
	}
	TablesApi = Query<D2RL::DataTableService>("data table");
	Lifecycle = Query<D2RL::LifecycleService>("lifecycle");
	if (!D2RL::HasDataTableServiceField(TablesApi, D2RL::DataTableServiceRequiredSize) || !D2RL::HasLifecycleServiceField(Lifecycle, D2RL::LifecycleServiceRequiredSize)) {
		Log(true, "Shrine Icons %s: inactive, this D2RLoader lacks a needed service", PluginVersion);
		return true;
	}
	const D2RL::Lifecycle::DataTablesLoadedListener listener {
		.structSize = D2RL::Lifecycle::DataTablesLoadedListenerSize,
		.callback   = OnTablesLoaded,
	};
	const auto result = Lifecycle->registerDataTablesLoadedListener(context, &listener, &TablesListener);
	if (result != D2RL::Lifecycle::Result::Success) {
		Log(true, "Shrine Icons %s: inactive, data table listener refused (result %u)", PluginVersion, static_cast<unsigned>(result));
		return true;
	}
	Log(false, "Shrine Icons %s: active (symbols %s, minimap %s), waiting for the data tables", PluginVersion, Config.symbols ? "on" : "off", OriginalAddCell ? "on" : "off");
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {}
