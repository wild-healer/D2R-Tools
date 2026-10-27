// Run Counter: counts runs and logs the items found during each one.
//
// Built only on documented D2RLoader services, with no native hooks, so the
// same DLL works on every build D2RLoader supports (Steam 3.3.93787 and
// Battle.net 3.3.93847 alike).
//
//   - A game becomes a run when the character first leaves town. Time is split
//     into farming (out of town) and town time; games spent only in town keep
//     their town time but are not runs.
//   - Items: D2RLoader has no "item dropped" event, so the plugin scans the
//     player's items (inventory, cursor, belt, Cube, stashes) twice a second and
//     records items it has never seen, keyed by item seed. Items already owned
//     when the game starts form the baseline. Items that first show up in the
//     Cube (cube outputs) or a stash (pages loaded late) are not finds.
//   - Everything is kept in runs.csv and drops.csv next to the plugin's log and
//     loaded again at launch, so the stats cover every game played.
//   - Character name, level and gold come from the character's save file,
//     which the game writes when the character leaves a game.
//   - What dropped an item is estimated from its item level: monsters drop at
//     their level, champions 2 above the area's level, uniques and their
//     minions 3 above, bosses at their own level.
//   - A small panel in the game's own UI shows the current run; the stats
//     panel (tabs, selectors, charts made of text) shows the rest.

#include <D2RLPlugin/api.h>
#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

namespace Items   = D2RL::Items;
namespace Overlay = D2RL::Overlay;
namespace Events  = D2RL::SharedEvents;

struct RecordName {
	std::uint32_t id;
	const char*   code;
	const char*   key;
};
struct BaseName {
	const char* code;
	const char* key;
	const char* english;
};
struct LevelName {
	std::int32_t id;
	const char*  key;
	const char*  english;
	bool         town;
	int          monsterLevel[3]; // per difficulty
};
struct MonsterName {
	std::uint32_t classId;
	const char*   key;
	int           level[3]; // per difficulty
};
// group: 0 equipment, 1 rune, 2 gem, 3 charm, 4 jewel, 5 key/essence/token,
// 6 other, 7 consumable (potions, scrolls, tomes, keys, ammunition...).
// slot (equipment): 1 helm, 2 armor, 3 weapon, 4 shield, 5 gloves, 6 belt,
// 7 boots, 8 ring, 9 amulet. tier: 1 normal, 2 exceptional, 3 elite, 0 none.
// gem: 1 amethyst, 2 diamond, 3 emerald, 4 ruby, 5 sapphire, 6 topaz, 7 skull;
// grade: 1 chipped ... 5 perfect.
struct ItemClass {
	const char*  code;
	std::uint8_t group, slot, tier, gem, grade;
};

#include "tables.inc"

constexpr const char* PluginId      = "run-counter";
constexpr const char* PluginVersion = "2.3.1";

const D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = PluginId,
	.name        = "Run Counter",
	.version     = PluginVersion,
	.author      = "wild-healer",
	.description = "Counts runs, times them and keeps stats of everything you pick up.",
	// NativeHooks: the real level of stackable items is read from the game's item data.
	.flags       = static_cast<D2RL::PluginFlags>(static_cast<std::uint32_t>(D2RL::PluginFlags::Client) | static_cast<std::uint32_t>(D2RL::PluginFlags::NativeHooks)),
};

// More new tracked items than this in one scan means the items were loaded,
// not found: the scan is treated as a new baseline.
constexpr std::size_t MaxDropsPerScan = 12;

// ---- services ----

const D2RL::PluginContext*       Context {};
const D2RL::LifecycleService*    Lifecycle {};
const D2RL::InventoryService*    Inventory {};
const D2RL::ItemService*         ItemApi {};
const D2RL::ThreadService*       Threads {};
const D2RL::OverlayService*      OverlayApi {};
const D2RL::LocalizationService* Strings {};
const D2RL::PanelService*        PanelApi {};
const D2RL::WidgetService*       WidgetApi {};
const D2RL::ResourceService*     ResourceApi {};
const D2RL::SharedEventService*  EventApi {};

// ---- config ----

struct Settings {
	bool                     enabled     = true;
	bool                     active      = true;  // false pauses recording (games and items are not saved)
	bool                     background  = false; // translucent background behind the game panel
	// Items tracked, by kind: TrackOff, TrackOn, or TrackHidden (counted but
	// not in the panel's drops list).
	int                      consumables = 0; // potions, scrolls, tomes, keys, arrows
	int                      uniques     = 1;
	int                      sets        = 1;
	int                      rares       = 1;
	int                      magic       = 1;
	int                      normal      = 1; // normal, superior and low quality items (not the kinds below)
	int                      gems        = 1;
	int                      essences    = 1; // essences, uber keys, the Ancients' statues
	int                      shards      = 1; // Worldstone shards
	int                      minRune     = 1;  // lowest rune tracked: 1 = El ... 33 = Zod, 0 = no runes
	int                      announceRune = 24; // lowest rune announced in the console; default Ist
	std::vector<std::string> codes;            // item codes that always count
	std::string              display     = "game"; // "game" (panel in the game's own UI), "overlay" or "off"
	bool                     colors      = true;
	float                    x           = 0.01f; // panel position, fraction of the screen
	float                    y           = 0.30f;
	float                    textScale   = 1.0f;
	int                      lastDrops   = 3;
	int                      pollMs      = 500;
	int                      barChars    = 40; // length of the stats panel's bars, in characters
	bool                     logUi       = false; // write the game's UI messages to run-counter-ui.log
	std::string              dataFolder;           // where runs.csv and drops.csv are kept; empty = <game>\d2rloader\run-counter
} Config;

// ---- records (guarded by Lock: monster deaths and the save reader use other threads) ----

enum Source : std::uint8_t { SourceOther, SourceTrash, SourceChampion, SourceElite, SourceBoss, SourceTown, SourceCount };
constexpr const char* SourceNames[] { "other", "trash", "champion", "elite", "boss", "town" };

struct RunRecord {
	std::uint32_t id {}; // 0: a game spent in town
	std::string   start, end;
	std::int64_t  stamp {}; // start, seconds since 1601
	std::uint32_t seconds {}, farm {}, town {};
	std::int32_t  difficulty = -1;
	std::string   area, levels, bosses;
	std::uint32_t items {};
	std::string   character;
	std::int32_t  level {};
	bool          terror {};
	bool          goldKnown {};
	std::int64_t  gold {};
};

struct ItemRecord {
	std::uint32_t    run {};
	std::string      time;
	std::string      area;    // where it was picked up
	std::string      runArea; // the run's name, empty for a game spent in town
	std::string      name;
	std::string      code;
	Items::Quality   quality {};
	std::uint32_t    itemLevel {};
	bool             ethereal {};
	std::uint32_t    sockets {};
	std::string      character;
	std::int32_t     difficulty = -1;
	bool             terror {};
	Source           source = SourceOther;
	const ItemClass* cls {};
	int              rune {};
	char             color {}; // color code a mod gave the name, 0 when none
};

// An item picked up in the current game, before the game is written out.
struct PendingItem {
	ItemRecord    record;
	std::uint32_t runtimeId {};
	std::int32_t levelId = -1;
	bool         inTown {};
	ULONGLONG    tick {};
};

struct BossKill {
	const MonsterName* boss;
	ULONGLONG          tick;
	std::int32_t       levelId = -1;
};

struct Run {
	std::uint32_t             number {};
	std::string               startTime;
	ULONGLONG                 start {};
	ULONGLONG                 end {};
	std::int32_t              difficulty = -1;
	std::vector<std::int32_t> levels;
	std::int32_t              level = -1;
	std::vector<BossKill>     bosses;
	std::vector<PendingItem>  items;
	// Time in town and out of town ("farming"). The game becomes a run, and
	// gets its number, when the character first leaves town.
	ULONGLONG                 townMs {};
	ULONGLONG                 farmMs {};
	ULONGLONG                 segmentStart {};
	bool                      paused {}; // the game's pause menu is open: time stands still
	bool                      inTown = true;
	std::int32_t              characterLevel {}; // from PlayerLevelChanged, 0 if unknown
};

std::mutex              Lock;
bool                    InGame {};
Run                     Current;
std::vector<RunRecord>  AllRuns;  // every finished game, oldest first
std::vector<ItemRecord> AllItems; // every recorded item, oldest first
std::int64_t            SessionStamp {}; // start of this session, seconds since 1601
std::uint32_t           NextRun = 1;
int                     PendingFinishes {}; // games ended but not written yet
std::uint32_t           Resets {};          // "delete everything" clicks; games ended before one are dropped
bool AddTownGold(const RunRecord& town);

// ---- UI-thread state (game events, scans and console run on the UI thread) ----

D2RL::PlayerHandle                Player = D2RL::InvalidPlayerHandle;
std::uint64_t                     Session {};
bool                              PlayerReady {};
ULONGLONG                         JoinedAt {};
bool                              Baselined {};
std::unordered_set<std::uint64_t> Known;
// Items owned at the last scan, by key, with their fingerprint; and the
// fingerprints of items that left them this game (dropped, sold), so picking
// one back up or buying it back does not count it again.
std::unordered_map<std::uint64_t, std::string> Owned;
// Items in the Cube at the last scan, by key, with their quantity: one that
// is gone or smaller means the Cube was used.
std::unordered_map<std::uint64_t, std::int32_t> InCube;
std::unordered_multiset<std::string>           Gone;
ULONGLONG                         LastScan {};
std::atomic_bool                  OverlayVisible { true };
std::atomic<std::uint64_t>        Frames {};
std::atomic_bool                  FrameLogged {};
std::atomic<ULONGLONG>            TestUntil {};
std::atomic_bool                  TestLogged {};
std::atomic_bool                  TickScheduled {};
std::atomic_bool                  Playing {};
std::atomic_bool                  TickFailLogged {};
bool                              TickLogged {};

void QueueTick();
void OpenGamePanel();
void CloseGamePanel();

std::string LogDirectory;  // UTF-8 path of the folder holding the plugin log, with trailing backslash
std::string DataDirectory; // where runs.csv and drops.csv live, with trailing backslash

void Log(bool warn, const char* format, ...) noexcept {
	if (!Context) return;
	char    message[1024] {};
	va_list args;
	va_start(args, format);
	std::vsnprintf(message, sizeof(message), format, args);
	va_end(args);
	if (warn) Context->LogWarn(message);
	else Context->LogInfo(message);
}

void Say(const char* format, ...) noexcept {
	if (!Context) return;
	char    message[1024] {};
	va_list args;
	va_start(args, format);
	std::vsnprintf(message, sizeof(message), format, args);
	va_end(args);
	Context->WriteConsoleMessage(message);
}

// ---- helpers ----

std::string Now() {
	SYSTEMTIME t {};
	GetLocalTime(&t);
	char text[32] {};
	std::snprintf(text, sizeof(text), "%04u-%02u-%02u %02u:%02u:%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
	return text;
}

// "YYYY-MM-DD HH:MM:SS" -> seconds since 1601, 0 if malformed.
std::int64_t Stamp(const std::string& text) {
	SYSTEMTIME t {};
	unsigned   y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
	if (std::sscanf(text.c_str(), "%u-%u-%u %u:%u:%u", &y, &mo, &d, &h, &mi, &s) != 6) return 0;
	t.wYear = static_cast<WORD>(y), t.wMonth = static_cast<WORD>(mo), t.wDay = static_cast<WORD>(d);
	t.wHour = static_cast<WORD>(h), t.wMinute = static_cast<WORD>(mi), t.wSecond = static_cast<WORD>(s);
	FILETIME file {};
	if (!SystemTimeToFileTime(&t, &file)) return 0;
	return static_cast<std::int64_t>((static_cast<ULONGLONG>(file.dwHighDateTime) << 32 | file.dwLowDateTime) / 10000000ULL);
}

std::string Duration(ULONGLONG ms) {
	const auto seconds = ms / 1000;
	char text[32] {};
	if (seconds >= 3600) std::snprintf(text, sizeof(text), "%llu:%02llu:%02llu", seconds / 3600, seconds / 60 % 60, seconds % 60);
	else std::snprintf(text, sizeof(text), "%llu:%02llu", seconds / 60, seconds % 60);
	return text;
}

std::string Hours(ULONGLONG ms) {
	const auto seconds = ms / 1000;
	char       text[32] {};
	std::snprintf(text, sizeof(text), "%llu:%02llu:%02llu", seconds / 3600, seconds / 60 % 60, seconds % 60);
	return text;
}

// 1234567 -> "1 234 567"
std::string Thousands(std::int64_t value) {
	std::string digits = std::to_string(value < 0 ? -value : value), out;
	for (std::size_t i = 0; i < digits.size(); ++i) {
		if (i && (digits.size() - i) % 3 == 0) out += ' ';
		out += digits[i];
	}
	return (value < 0 ? "-" : "") + out;
}

std::string Fixed(double value, int decimals) {
	// Written by hand: printf's %f follows the game's locale.
	const double scale = decimals == 2 ? 100.0 : decimals == 1 ? 10.0 : 1.0;
	const auto   whole = static_cast<long long>(value * scale + (value < 0 ? -0.5 : 0.5));
	std::string  out   = std::to_string(whole / static_cast<long long>(scale));
	if (decimals) {
		std::string fraction = std::to_string((whole < 0 ? -whole : whole) % static_cast<long long>(scale));
		while (static_cast<int>(fraction.size()) < decimals) fraction.insert(0, "0");
		out += "." + fraction;
	}
	return out;
}

std::string CodeText(std::uint32_t code) {
	std::string text;
	for (int i = 0; i < 4; ++i) {
		const char c = static_cast<char>(code >> (i * 8));
		if (c && c != ' ') text += c;
	}
	return text;
}

// Removes color codes that mods put in item names: "\xFFc" or "ÿc" (UTF-8)
// or U+E07E, each followed by the color character. The panel's text boxes
// would show them as "*"; the first color goes to `color` instead.
std::string StripColors(std::string text, char* color = nullptr) {
	for (std::size_t at = 0; at < text.size();) {
		std::size_t marker = 0;
		if (text.compare(at, 3, "\xC3\xBF" "c") == 0) marker = 3;
		else if (text.compare(at, 2, "\xFF" "c") == 0) marker = 2;
		else if (text.compare(at, 3, "\xEE\x81\xBE") == 0) marker = text.compare(at + 3, 1, "c") == 0 ? 4 : 3;
		if (marker && color && !*color && at + marker < text.size()) *color = text[at + marker];
		if (marker) text.erase(at, std::min(marker + 1, text.size() - at));
		else ++at;
	}
	return text;
}

// Game text for a string key, e.g. "The Gnasher" -> localized name; the fallback when unknown.
// `color` gets the color code a mod put in the text, if any.
std::string Text(const char* key, const char* fallback, char* color = nullptr) {
	static std::mutex                                                     cacheLock;
	static std::unordered_map<std::string, std::pair<std::string, char>> cache;
	if (!key || !*key) return fallback ? fallback : "";
	std::lock_guard guard(cacheLock);
	if (const auto found = cache.find(key); found != cache.end()) {
		if (color) *color = found->second.second;
		return found->second.first;
	}
	std::string result = fallback ? fallback : key;
	if (Strings) {
		const std::string full = std::string("d2r:") + key;
		char          buffer[256] {};
		std::uint32_t required = 0;
		if (Strings->getStringByKey(Context, full.c_str(), buffer, sizeof(buffer), &required) == D2RL::Localization::Result::Success && buffer[0]) result = buffer;
	}
	char code = 0;
	result    = StripColors(std::move(result), &code);
	// Some languages prefix names with grammar markers such as "[ms]".
	while (result.size() > 4 && result[0] == '[' && result[3] == ']') result.erase(0, result[4] == ' ' ? 5 : 4);
	if (const auto newline = result.find('\n'); newline != std::string::npos) result.erase(newline);
	cache.emplace(key, std::pair { result, code });
	if (color) *color = code;
	return result;
}

const LevelName* FindLevel(std::int32_t id) {
	for (const auto& level : Levels)
		if (level.id == id) return &level;
	return nullptr;
}

std::string LevelText(std::int32_t id) {
	const auto* level = FindLevel(id);
	return level ? Text(level->key, level->english) : std::string();
}

std::string BaseText(const std::string& code, char* color = nullptr) {
	for (const auto& base : Bases)
		if (code == base.code) return Text(base.key, base.english, color);
	return code;
}

const ItemClass* FindClass(const std::string& code) {
	static const auto classes = [] {
		std::unordered_map<std::string, const ItemClass*> map;
		for (const auto& item : ItemClasses) map.emplace(item.code, &item);
		return map;
	}();
	const auto found = classes.find(code);
	return found == classes.end() ? nullptr : found->second;
}

int RuneNumber(const std::string& code) {
	if (code.size() != 3 || code[0] != 'r' || code[1] < '0' || code[1] > '9' || code[2] < '0' || code[2] > '9') return 0;
	const int number = (code[1] - '0') * 10 + (code[2] - '0');
	return number >= 1 && number <= 33 ? number : 0;
}

const char* QualityName(Items::Quality quality) {
	switch (quality) {
		case Items::Quality::Inferior: return "inferior";
		case Items::Quality::Normal:   return "normal";
		case Items::Quality::Superior: return "superior";
		case Items::Quality::Magic:    return "magic";
		case Items::Quality::Set:      return "set";
		case Items::Quality::Rare:     return "rare";
		case Items::Quality::Unique:   return "unique";
		case Items::Quality::Crafted:  return "crafted";
		default:                       return "unknown";
	}
}

Items::Quality ParseQuality(const std::string& text) {
	for (int q = 1; q <= 8; ++q)
		if (text == QualityName(static_cast<Items::Quality>(q))) return static_cast<Items::Quality>(q);
	return Items::Quality::Unknown;
}

const char* DifficultyName(std::int32_t difficulty) {
	switch (difficulty) {
		case 0:  return "Normal";
		case 1:  return "Nightmare";
		case 2:  return "Hell";
		default: return "";
	}
}

std::int32_t ParseDifficulty(const std::string& text) {
	for (int d = 0; d < 3; ++d)
		if (text == DifficultyName(d)) return d;
	return -1;
}

// Notable finds are announced in the console and listed in the stats.
bool Notable(const ItemRecord& item) {
	if (item.rune) return Config.announceRune && item.rune >= Config.announceRune;
	return item.quality == Items::Quality::Unique || item.quality == Items::Quality::Set;
}

// The run's name: the last boss killed, else the last area outside town, else the town.
std::string RunArea(const Run& run) {
	if (!run.bosses.empty()) return Text(run.bosses.back().boss->key, run.bosses.back().boss->key);
	for (auto level = run.levels.rbegin(); level != run.levels.rend(); ++level) {
		const auto* found = FindLevel(*level);
		if (found && !found->town) return Text(found->key, found->english);
	}
	return run.levels.empty() ? std::string() : LevelText(run.levels.back());
}

std::wstring Wide(const std::string& text) {
	const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
	std::wstring out(size > 0 ? size - 1 : 0, L'\0');
	if (size > 1) MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), size);
	return out;
}

std::string Utf8(const wchar_t* text) {
	if (!text) return {};
	const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
	std::string out(size > 0 ? size - 1 : 0, '\0');
	if (size > 1) WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
	return out;
}

// ---- CSV files ----

constexpr const char* RunsFile    = "runs.csv";
constexpr const char* ItemsFile   = "drops.csv";
constexpr const char* RunsHeader  = "run,start,end,seconds,farm_seconds,town_seconds,difficulty,area,levels,bosses,items,character,level,terrorized,gold";
constexpr const char* ItemsHeader = "run,time,area,name,quality,code,ilvl,ethereal,sockets,character,difficulty,terrorized,source,run_area";

std::string Csv(std::string_view text) {
	std::string out = "\"";
	for (const char c : text) {
		if (c == '"') out += '"';
		out += c;
	}
	return out + '"';
}

std::vector<std::string> SplitCsv(const std::string& line) {
	std::vector<std::string> fields(1);
	bool                     quoted = false;
	for (std::size_t i = 0; i < line.size(); ++i) {
		const char c = line[i];
		if (quoted) {
			if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') fields.back() += '"', ++i;
			else if (c == '"') quoted = false;
			else fields.back() += c;
		} else if (c == '"') quoted = true;
		else if (c == ',') fields.emplace_back();
		else if (c != '\r') fields.back() += c;
	}
	return fields;
}

std::string RunCsv(const RunRecord& run) {
	return std::to_string(run.id) + "," + Csv(run.start) + "," + Csv(run.end) + "," + std::to_string(run.seconds) + "," + std::to_string(run.farm) + "," + std::to_string(run.town) + ","
	     + DifficultyName(run.difficulty) + "," + Csv(run.area) + "," + Csv(run.levels) + "," + Csv(run.bosses) + "," + std::to_string(run.items) + "," + Csv(run.character) + ","
	     + (run.level ? std::to_string(run.level) : "") + "," + (run.terror ? "yes" : "no") + "," + (run.goldKnown ? std::to_string(run.gold) : "");
}

std::string ItemCsv(const ItemRecord& item) {
	return (item.run ? std::to_string(item.run) : std::string()) + "," + Csv(item.time) + "," + Csv(item.area) + "," + Csv(item.name) + "," + QualityName(item.quality) + "," + item.code
	     + "," + std::to_string(item.itemLevel) + "," + (item.ethereal ? "yes" : "no") + "," + std::to_string(item.sockets) + "," + Csv(item.character) + "," + DifficultyName(item.difficulty)
	     + "," + (item.terror ? "yes" : "no") + "," + SourceNames[item.source] + "," + Csv(item.runArea);
}

void AppendLines(const char* file, const char* header, const std::vector<std::string>& lines) {
	if (DataDirectory.empty() || lines.empty()) return;
	const std::wstring path  = Wide(DataDirectory + file);
	const bool         fresh = GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES;
	FILE*              out   = _wfopen(path.c_str(), L"ab");
	if (!out) {
		Log(true, "could not write %s", file);
		return;
	}
	if (fresh) std::fprintf(out, "%s\r\n", header);
	for (const auto& line : lines) std::fprintf(out, "%s\r\n", line.c_str());
	std::fclose(out);
}

// Rewrites both files from the records, after copying the old ones to
// <name>-<suffix>.csv.
bool RewriteFiles(const char* backupSuffix) {
	if (DataDirectory.empty()) return false;
	bool ok = true;
	for (int f = 0; f < 2; ++f) {
		const std::string name = f ? ItemsFile : RunsFile;
		const std::wstring path = Wide(DataDirectory + name);
		std::string backup = name;
		backup.insert(backup.rfind('.'), std::string("-") + backupSuffix);
		if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) (void)CopyFileW(path.c_str(), Wide(DataDirectory + backup).c_str(), FALSE);
		const std::wstring temp = path + L".tmp";
		FILE*              out  = _wfopen(temp.c_str(), L"wb");
		if (!out) {
			ok = false;
			continue;
		}
		std::fprintf(out, "%s\r\n", f ? ItemsHeader : RunsHeader);
		if (f)
			for (const auto& item : AllItems) std::fprintf(out, "%s\r\n", ItemCsv(item).c_str());
		else
			for (const auto& run : AllRuns) std::fprintf(out, "%s\r\n", RunCsv(run).c_str());
		std::fclose(out);
		ok = MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) && ok;
	}
	if (!ok) Log(true, "could not rewrite the CSV files");
	return ok;
}

// Reads a CSV file into rows of named fields. Returns false if it does not exist.
bool ReadCsv(const char* file, std::vector<std::string>& header, std::vector<std::vector<std::string>>& rows) {
	FILE* in = _wfopen(Wide(DataDirectory + file).c_str(), L"rb");
	if (!in) return false;
	std::string text;
	char        buffer[65536];
	for (std::size_t read; (read = std::fread(buffer, 1, sizeof(buffer), in)) > 0;) text.append(buffer, read);
	std::fclose(in);
	if (text.rfind("\xEF\xBB\xBF", 0) == 0) text.erase(0, 3);
	std::size_t at = 0;
	bool        first = true;
	while (at < text.size()) {
		auto end = text.find('\n', at);
		if (end == std::string::npos) end = text.size();
		const std::string line = text.substr(at, end - at);
		at                     = end + 1;
		if (line.empty() || line == "\r") continue;
		if (first) header = SplitCsv(line);
		else rows.push_back(SplitCsv(line));
		first = false;
	}
	return true;
}

std::uint32_t Number(const std::string& text) { return static_cast<std::uint32_t>(std::strtoul(text.c_str(), nullptr, 10)); }

// The stats files live outside the mod folder, which D2RMM rebuilds (and
// empties) each time it installs mods: <game>\d2rloader\run-counter\ by default.
// Files left beside the log by earlier versions are copied there once.
std::string FindDataDirectory() {
	std::string folder = Config.dataFolder;
	if (folder.empty()) {
		wchar_t exe[MAX_PATH] {};
		const DWORD length = GetModuleFileNameW(nullptr, exe, MAX_PATH);
		if (!length || length >= MAX_PATH) return LogDirectory;
		folder = Utf8(exe);
		folder.erase(folder.find_last_of("\\/") + 1);
		folder += "d2rloader\\run-counter";
	}
	std::replace(folder.begin(), folder.end(), '/', '\\');
	while (!folder.empty() && (folder.back() == '\\' || folder.back() == '/')) folder.pop_back();
	const int made = SHCreateDirectoryExW(nullptr, Wide(folder).c_str(), nullptr);
	if (made != ERROR_SUCCESS && made != ERROR_ALREADY_EXISTS && made != ERROR_FILE_EXISTS) {
		Log(true, "cannot create the data folder %s (error %d); keeping the stats beside the log", folder.c_str(), made);
		return LogDirectory;
	}
	folder += '\\';
	const auto exists = [](const std::string& path) { return GetFileAttributesW(Wide(path).c_str()) != INVALID_FILE_ATTRIBUTES; };
	if (!LogDirectory.empty() && !exists(folder + RunsFile) && exists(LogDirectory + RunsFile)) {
		for (const char* file : { RunsFile, ItemsFile })
			if (exists(LogDirectory + file)) (void)CopyFileW(Wide(LogDirectory + file).c_str(), Wide(folder + file).c_str(), TRUE);
		Log(false, "copied the stats files from %s to %s", LogDirectory.c_str(), folder.c_str());
	}
	return folder;
}

void LoadRecords() {
	std::vector<std::string>              runHeader, itemHeader;
	std::vector<std::vector<std::string>> runRows, itemRows;
	const bool                            haveRuns = ReadCsv(RunsFile, runHeader, runRows), haveItems = ReadCsv(ItemsFile, itemHeader, itemRows);
	const auto column = [](const std::vector<std::string>& header, const char* name) {
		const auto found = std::find(header.begin(), header.end(), name);
		return found == header.end() ? -1 : static_cast<int>(found - header.begin());
	};
	const auto field = [](const std::vector<std::string>& row, int index) -> std::string { return index >= 0 && index < static_cast<int>(row.size()) ? row[index] : std::string(); };

	// Files from 1.x number runs again in every session and have no character:
	// runs get new numbers, and items join the run whose time span holds them.
	const bool old = haveRuns && column(runHeader, "character") < 0;
	{
		const int id = column(runHeader, "run"), start = column(runHeader, "start"), end = column(runHeader, "end"), seconds = column(runHeader, "seconds"),
		          farm = column(runHeader, "farm_seconds"), town = column(runHeader, "town_seconds"), difficulty = column(runHeader, "difficulty"), area = column(runHeader, "area"),
		          levels = column(runHeader, "levels"), bosses = column(runHeader, "bosses"), items = std::max(column(runHeader, "items"), column(runHeader, "drops")),
		          character = column(runHeader, "character"), level = column(runHeader, "level"), terror = column(runHeader, "terrorized"), gold = column(runHeader, "gold");
		std::uint32_t renumber = 1;
		for (const auto& row : runRows) {
			RunRecord run;
			run.id = Number(field(row, id));
			if (old) run.id = run.id ? renumber++ : 0;
			run.start = field(row, start), run.end = field(row, end), run.stamp = Stamp(run.start);
			run.seconds = Number(field(row, seconds));
			run.farm    = farm >= 0 ? Number(field(row, farm)) : run.seconds;
			run.town    = Number(field(row, town));
			run.difficulty = ParseDifficulty(field(row, difficulty));
			run.area = field(row, area), run.levels = field(row, levels), run.bosses = field(row, bosses);
			run.items     = Number(field(row, items));
			run.character = field(row, character);
			run.level     = static_cast<std::int32_t>(Number(field(row, level)));
			run.terror    = field(row, terror) == "yes";
			const auto goldText = field(row, gold);
			run.goldKnown = !goldText.empty();
			run.gold      = std::strtoll(goldText.c_str(), nullptr, 10);
			// Games spent in town are not runs: their gold goes to the previous run.
			if (run.id) AllRuns.push_back(run);
			else if (run.goldKnown && run.gold) (void)AddTownGold(run);
			NextRun = std::max(NextRun, run.id + 1);
		}
	}
	if (haveItems) {
		const int run = column(itemHeader, "run"), time = column(itemHeader, "time"), area = column(itemHeader, "area"), name = column(itemHeader, "name"),
		          quality = column(itemHeader, "quality"), code = column(itemHeader, "code"), ilvl = column(itemHeader, "ilvl"), ethereal = column(itemHeader, "ethereal"),
		          sockets = column(itemHeader, "sockets"), character = column(itemHeader, "character"), difficulty = column(itemHeader, "difficulty"),
		          terror = column(itemHeader, "terrorized"), source = column(itemHeader, "source"), runArea = column(itemHeader, "run_area");
		for (const auto& row : itemRows) {
			ItemRecord item;
			item.run = Number(field(row, run));
			item.time = field(row, time), item.area = field(row, area), item.name = StripColors(field(row, name), &item.color);
			item.quality   = ParseQuality(field(row, quality));
			item.code      = field(row, code);
			item.itemLevel = Number(field(row, ilvl));
			item.ethereal  = field(row, ethereal) == "yes";
			item.sockets   = Number(field(row, sockets));
			item.character = field(row, character);
			item.difficulty = ParseDifficulty(field(row, difficulty));
			item.terror    = field(row, terror) == "yes";
			item.runArea   = field(row, runArea);
			const auto sourceText = field(row, source);
			for (int s = 0; s < SourceCount; ++s)
				if (sourceText == SourceNames[s]) item.source = static_cast<Source>(s);
			if (old) {
				// Join the run whose time span holds the item.
				const auto when = Stamp(item.time);
				item.run        = 0;
				for (const auto& r : AllRuns)
					if (r.id && when >= r.stamp && when <= Stamp(r.end) + 2) {
						item.run        = r.id;
						item.runArea    = r.area;
						item.difficulty = r.difficulty;
					}
			}
			item.cls  = FindClass(item.code);
			item.rune = RuneNumber(item.code);
			if (item.run) AllItems.push_back(item);
		}
	}
	if (old) {
		Log(false, "converting runs.csv and drops.csv from Run Counter 1.x (copies kept as runs-1.x.csv and drops-1.x.csv)");
		RewriteFiles("1.x");
	} else if (haveItems && column(itemHeader, "source") < 0) RewriteFiles("1.x");
	SessionStamp = Stamp(Now());
	Log(false, "loaded %zu games and %zu items; next run is %u", AllRuns.size(), AllItems.size(), NextRun);
}

// ---- config ----

std::string Trim(std::string_view text) {
	const auto begin = text.find_first_not_of(" \t\r");
	if (begin == std::string_view::npos) return {};
	const auto end = text.find_last_not_of(" \t\r");
	return std::string(text.substr(begin, end - begin + 1));
}

// Reads "quoted" TOML basic strings starting at text[at]; returns false if malformed.
bool ParseString(std::string_view text, std::size_t& at, std::string& output) {
	if (at >= text.size() || text[at] != '"') return false;
	output.clear();
	for (++at; at < text.size(); ++at) {
		const char c = text[at];
		if (c == '"') {
			++at;
			return true;
		}
		if (c == '\\' && at + 1 < text.size()) {
			const char e = text[++at];
			output += e == 'n' ? '\n' : e == 't' ? '\t' : e;
		} else output += c;
	}
	return false;
}

bool ParseBool(const std::string& value, bool fallback) {
	if (value.rfind("true", 0) == 0) return true;
	if (value.rfind("false", 0) == 0) return false;
	return fallback;
}

constexpr int TrackOff = 0, TrackOn = 1, TrackHidden = 2;

// true, false or "no_overlay".
int ParseTrack(const std::string& value, int fallback) {
	if (value.rfind("\"no_overlay\"", 0) == 0) return TrackHidden;
	if (value.rfind("true", 0) == 0) return TrackOn;
	if (value.rfind("false", 0) == 0) return TrackOff;
	return fallback;
}

float ParseFloat(const std::string& value, float fallback, float low, float high) {
	// Parsed by hand: strtof follows the game's locale, which may use a decimal comma.
	std::size_t at       = 0;
	const bool  negative = at < value.size() && value[at] == '-';
	if (negative) ++at;
	float whole = 0, scale = 1;
	bool  digits = false, decimals = false;
	for (; at < value.size(); ++at) {
		const char c = value[at];
		if (c >= '0' && c <= '9') {
			digits = true;
			if (decimals) whole += (c - '0') * (scale /= 10);
			else whole = whole * 10 + (c - '0');
		} else if (c == '.' && !decimals) decimals = true;
		else break;
	}
	const float parsed = negative ? -whole : whole;
	return digits && parsed >= low && parsed <= high ? parsed : fallback;
}

std::string ConfigText; // the TOML D2RLoader gave us, kept to write changes back

void ApplyConfig(std::string_view text);

void LoadConfig() {
	(void)Context->EnsureConfig();
	std::uint32_t     required = 0;
	std::vector<char> buffer(8192);
	if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) {
		if (required <= buffer.size()) {
			Log(true, "config: could not read the config file, using defaults");
			return;
		}
		buffer.resize(required);
		if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) return;
	}
	ConfigText.assign(buffer.data(), std::strlen(buffer.data()));
	ApplyConfig(ConfigText);
}

bool LegacyConfig {}; // a 2.2 config (track_all) was read: save it in the new form

int ParseRune(const std::string& value, int fallback) {
	std::size_t at = 0;
	std::string parsed;
	if (!ParseString(value, at, parsed)) return fallback;
	if (parsed.empty() || parsed == "none") return 0;
	for (int i = 0; i < 33; ++i)
		if (_stricmp(parsed.c_str(), RuneNames[i]) == 0) return i + 1;
	return fallback;
}

void ApplyConfig(std::string_view text) {
	int  trackAll = -1; // 2.2 and before: track_all, with min_rune as the announced rune
	bool announce = false;
	std::size_t lineStart = 0;
	while (lineStart < text.size()) {
		auto lineEnd = text.find('\n', lineStart);
		if (lineEnd == std::string_view::npos) lineEnd = text.size();
		const std::string line = Trim(text.substr(lineStart, lineEnd - lineStart));
		lineStart              = lineEnd + 1;
		const auto equals      = line.find('=');
		if (line.empty() || line[0] == '#' || line[0] == '[' || equals == std::string::npos) continue;
		const std::string key   = Trim(std::string_view(line).substr(0, equals));
		const std::string value = Trim(std::string_view(line).substr(equals + 1));
		std::size_t       at    = 0;
		std::string       parsed;
		if (key == "enabled") Config.enabled = ParseBool(value, Config.enabled);
		else if (key == "tracking_active") Config.active = ParseBool(value, Config.active);
		else if (key == "panel_background") Config.background = ParseBool(value, Config.background);
		else if (key == "track_all") trackAll = ParseBool(value, true) ? 1 : 0;
		else if (key == "consumables") Config.consumables = ParseTrack(value, Config.consumables);
		else if (key == "uniques") Config.uniques = ParseTrack(value, Config.uniques);
		else if (key == "sets") Config.sets = ParseTrack(value, Config.sets);
		else if (key == "rares") Config.rares = ParseTrack(value, Config.rares);
		else if (key == "magic") Config.magic = ParseTrack(value, Config.magic);
		else if (key == "normal") Config.normal = ParseTrack(value, Config.normal);
		else if (key == "gems") Config.gems = ParseTrack(value, Config.gems);
		else if (key == "essences") Config.essences = ParseTrack(value, Config.essences);
		else if (key == "shards") Config.shards = ParseTrack(value, Config.shards);
		else if (key == "min_rune") Config.minRune = ParseRune(value, Config.minRune);
		else if (key == "announce_rune") Config.announceRune = ParseRune(value, Config.announceRune), announce = true;
		else if (key == "codes" && !value.empty() && value[0] == '[') {
			Config.codes.clear();
			for (at = 1; at < value.size() && value[at] != ']';) {
				if (value[at] == '"') {
					if (!ParseString(value, at, parsed)) break;
					if (!parsed.empty()) Config.codes.push_back(parsed);
				} else ++at;
			}
		} else if (key == "panel" && ParseString(value, at, parsed) && (parsed == "game" || parsed == "overlay" || parsed == "off")) Config.display = parsed;
		else if (key == "overlay" && !ParseBool(value, true)) Config.display = "off"; // 1.0.x setting
		else if (key == "colors") Config.colors = ParseBool(value, Config.colors);
		else if (key == "overlay_x") Config.x = ParseFloat(value, Config.x, 0.0f, 1.0f);
		else if (key == "overlay_y") Config.y = ParseFloat(value, Config.y, 0.0f, 1.0f);
		else if (key == "text_scale") Config.textScale = ParseFloat(value, Config.textScale, 0.5f, 3.0f);
		else if (key == "last_drops") Config.lastDrops = static_cast<int>(ParseFloat(value, static_cast<float>(Config.lastDrops), 0.0f, 20.0f));
		else if (key == "scan_interval_ms") Config.pollMs = static_cast<int>(ParseFloat(value, static_cast<float>(Config.pollMs), 100.0f, 10000.0f));
		else if (key == "bar_length") Config.barChars = static_cast<int>(ParseFloat(value, static_cast<float>(Config.barChars), 10.0f, 120.0f));
		else if (key == "log_ui_messages") Config.logUi = ParseBool(value, Config.logUi);
		else if (key == "data_folder" && ParseString(value, at, parsed)) Config.dataFolder = parsed;
	}
	if (trackAll < 0) return;
	// Before 2.3, min_rune was the announced rune; track_all = true tracked
	// every quality and every rune.
	LegacyConfig = true;
	if (!announce) Config.announceRune = Config.minRune;
	if (trackAll == 1) {
		Config.uniques = Config.sets = Config.rares = Config.magic = Config.normal = Config.gems = Config.essences = Config.shards = true;
		Config.minRune = 1;
	} else Config.normal = Config.gems = Config.essences = Config.shards = false;
}

// ---- settings changed in the stats panel ----
// D2RMM rewrites the mod's config folder each time it installs mods, so the
// stats panel keeps its changes in settings.toml beside runs.csv, read after
// the config at launch, and also writes them into the config file.

constexpr const char* SettingsFile = "settings.toml";

std::string Decimal(float value, int decimals) {
	char text[32] {};
	std::snprintf(text, sizeof(text), "%.*f", decimals, static_cast<double>(value));
	return text;
}

// Every setting the stats panel changes, as TOML values.
std::vector<std::pair<std::string, std::string>> PanelSettings() {
	const auto flag  = [](bool value) { return std::string(value ? "true" : "false"); };
	const auto track = [](int value) { return std::string(value == TrackHidden ? "\"no_overlay\"" : value ? "true" : "false"); };
	return {
		{ "tracking_active", flag(Config.active) },
		{ "consumables", track(Config.consumables) },
		{ "uniques", track(Config.uniques) },
		{ "sets", track(Config.sets) },
		{ "rares", track(Config.rares) },
		{ "magic", track(Config.magic) },
		{ "normal", track(Config.normal) },
		{ "gems", track(Config.gems) },
		{ "essences", track(Config.essences) },
		{ "shards", track(Config.shards) },
		{ "min_rune", "\"" + std::string(Config.minRune ? RuneNames[Config.minRune - 1] : "none") + "\"" },
		{ "announce_rune", "\"" + std::string(Config.announceRune ? RuneNames[Config.announceRune - 1] : "none") + "\"" },
		{ "panel", "\"" + Config.display + "\"" },
		{ "panel_background", flag(Config.background) },
		{ "colors", flag(Config.colors) },
		{ "overlay_x", Decimal(Config.x, 2) },
		{ "overlay_y", Decimal(Config.y, 2) },
		{ "text_scale", Decimal(Config.textScale, 1) },
		{ "last_drops", std::to_string(Config.lastDrops) },
		{ "scan_interval_ms", std::to_string(Config.pollMs) },
		{ "bar_length", std::to_string(Config.barChars) },
		{ "log_ui_messages", flag(Config.logUi) },
	};
}

void LoadSettings() {
	if (DataDirectory.empty()) return;
	FILE* in = _wfopen(Wide(DataDirectory + SettingsFile).c_str(), L"rb");
	if (!in) return;
	std::string text;
	char        chunk[4096];
	for (std::size_t got; (got = std::fread(chunk, 1, sizeof(chunk), in)) > 0;) text.append(chunk, got);
	std::fclose(in);
	const auto folder = Config.dataFolder; // only the config file sets it
	ApplyConfig(text);
	Config.dataFolder = folder;
	Log(false, "settings: loaded %s (changes made in the stats panel)", SettingsFile);
}

// Writes settings.toml and the same values into the config file.
void SaveSettings() {
	const auto settings = PanelSettings();
	if (!DataDirectory.empty()) {
		std::string text = "# Run Counter settings changed in the stats panel. They override the\r\n# config file; delete this file to use the config file alone.\r\n[run-counter]\r\n";
		for (const auto& [key, value] : settings) text += key + " = " + value + "\r\n";
		if (FILE* out = _wfopen(Wide(DataDirectory + SettingsFile).c_str(), L"wb")) {
			std::fwrite(text.data(), 1, text.size(), out);
			std::fclose(out);
		} else Log(true, "settings: could not write %s", SettingsFile);
	}
	// Replace each key's line in the config text, keeping its comments; add
	// the keys it lacks at the end of the [run-counter] table.
	std::string out, missing;
	std::vector<bool> found(settings.size());
	for (std::size_t start = 0; start < ConfigText.size();) {
		auto end = ConfigText.find('\n', start);
		end      = end == std::string::npos ? ConfigText.size() : end + 1;
		std::string line(ConfigText, start, end - start);
		start             = end;
		const auto equals = line.find('=');
		const auto key    = equals == std::string::npos ? std::string() : Trim(std::string_view(line).substr(0, equals));
		if (key == "track_all" || key == "crafted" || key == "superior") continue; // gone in 2.3 (crafted items never drop)
		for (std::size_t i = 0; i < settings.size(); ++i)
			if (!key.empty() && key[0] != '#' && key == settings[i].first) {
				const bool crlf = line.size() > 1 && line[line.size() - 2] == '\r';
				line            = key + " = " + settings[i].second + (crlf ? "\r\n" : "\n");
				found[i]        = true;
			}
		out += line;
	}
	for (std::size_t i = 0; i < settings.size(); ++i)
		if (!found[i]) missing += settings[i].first + " = " + settings[i].second + "\n";
	if (!missing.empty()) out += (out.empty() || out.back() == '\n' ? "" : "\n") + std::string(out.find("[run-counter]") == std::string::npos ? "[run-counter]\n" : "") + missing;
	if (Context->WriteConfig(out.c_str())) ConfigText = std::move(out);
	else Log(true, "settings: could not write the config file");
}

// ---- save files: character name, level and gold ----
// The game writes <character>.d2s when the character leaves a game, a moment
// before GameLeft. Gold is the character's (stat 14) plus its stash (15), and
// the shared stash's (each .d2i page header holds its gold at offset 12).

std::vector<std::wstring> SaveDirectories() {
	std::vector<std::wstring> out;
	PWSTR                     folder = nullptr;
	if (SHGetKnownFolderPath(FOLDERID_SavedGames, 0, nullptr, &folder) == S_OK && folder) {
		const std::wstring base = std::wstring(folder) + L"\\Diablo II Resurrected\\";
		if (Context->activeMod && *Context->activeMod) out.push_back(base + L"mods\\" + Wide(Context->activeMod) + L"\\");
		out.push_back(base);
	}
	if (folder) CoTaskMemFree(folder);
	return out;
}

struct SaveInfo {
	std::wstring  path;
	std::string   name;
	std::int32_t  level {};
	std::int64_t  gold {};
	bool          valid {};
	ULONGLONG     written {}; // FILETIME of the last write
};

std::string ReadFileBytes(const std::wstring& path) {
	std::string bytes;
	if (FILE* in = _wfopen(path.c_str(), L"rb")) {
		char buffer[16384];
		for (std::size_t read; (read = std::fread(buffer, 1, sizeof(buffer), in)) > 0;) bytes.append(buffer, read);
		std::fclose(in);
	}
	return bytes;
}

SaveInfo ReadSave(const std::wstring& path) {
	SaveInfo          info { .path = path };
	const std::string bytes = ReadFileBytes(path);
	if (bytes.size() < 0x200 || std::memcmp(bytes.data(), "\x55\xAA\x55\xAA", 4) != 0) return info;
	const auto slash = path.find_last_of(L"\\/");
	info.name        = Utf8(path.substr(slash + 1, path.size() - slash - 5).c_str());
	const auto gf    = bytes.find("gf", 0x100);
	if (gf == std::string::npos) return info;
	// Stats: 9-bit id, then a value whose width depends on the stat, until 0x1FF.
	static constexpr int widths[16] { 10, 10, 10, 10, 10, 8, 21, 21, 21, 21, 21, 21, 7, 32, 25, 25 };
	std::size_t          bit  = 0;
	const auto           read = [&](int count) {
        std::uint64_t value = 0;
        for (int i = 0; i < count; ++i, ++bit) {
            const std::size_t at = gf + 2 + bit / 8;
            if (at >= bytes.size()) return std::uint64_t(~0ULL);
            value |= static_cast<std::uint64_t>((static_cast<unsigned char>(bytes[at]) >> (bit % 8)) & 1) << i;
        }
        return value;
	};
	for (int guard = 0; guard < 16; ++guard) {
		const auto id = read(9);
		if (id >= 16) break;
		const auto value = read(widths[id]);
		if (id == 12) info.level = static_cast<std::int32_t>(value);
		if (id == 14 || id == 15) info.gold += static_cast<std::int64_t>(value);
	}
	info.valid = true;
	return info;
}

std::vector<SaveInfo> ListSaves(bool read) {
	std::vector<SaveInfo> out;
	for (const auto& folder : SaveDirectories()) {
		WIN32_FIND_DATAW data {};
		HANDLE           find = FindFirstFileW((folder + L"*.d2s").c_str(), &data);
		if (find == INVALID_HANDLE_VALUE) continue;
		do {
			const std::wstring path = folder + data.cFileName;
			SaveInfo           info = read ? ReadSave(path) : SaveInfo { .path = path };
			info.written            = static_cast<ULONGLONG>(data.ftLastWriteTime.dwHighDateTime) << 32 | data.ftLastWriteTime.dwLowDateTime;
			out.push_back(info);
		} while (FindNextFileW(find, &data));
		FindClose(find);
	}
	return out;
}

std::int64_t SharedStashGold() {
	std::int64_t gold = 0;
	for (const auto& folder : SaveDirectories()) {
		WIN32_FIND_DATAW data {};
		HANDLE           find = FindFirstFileW((folder + L"*.d2i").c_str(), &data);
		if (find == INVALID_HANDLE_VALUE) continue;
		do {
			const std::string bytes = ReadFileBytes(folder + data.cFileName);
			for (std::size_t at = bytes.find("\x55\xAA\x55\xAA"); at != std::string::npos && at + 16 <= bytes.size(); at = bytes.find("\x55\xAA\x55\xAA", at + 4)) {
				std::uint32_t value = 0;
				std::memcpy(&value, bytes.data() + at + 12, 4);
				if (value < 100000000) gold += value; // a page holds at most 2.5 million
			}
		} while (FindNextFileW(find, &data));
		FindClose(find);
		break; // the folder in use only
	}
	return gold;
}

// Gold in every save at the start of the game, by path, and in the shared stash.
std::map<std::wstring, std::int64_t> StartGold;
std::int64_t                         StartSharedGold {};
bool                                 StartGoldRead {};

ULONGLONG FileTimeNow() {
	FILETIME now {};
	GetSystemTimeAsFileTime(&now);
	return static_cast<ULONGLONG>(now.dwHighDateTime) << 32 | now.dwLowDateTime;
}

// ---- runs ----

// Caller holds Lock. Adds the time since the last area change to town or farming time.
void CloseSegment(ULONGLONG now) {
	if (!Current.paused) (Current.inTown ? Current.townMs : Current.farmMs) += now - Current.segmentStart;
	Current.segmentStart = now;
}

// Pauses or resumes the farming and town clocks (the pause menu, offline).
void SetPaused(bool paused) {
	std::lock_guard guard(Lock);
	if (!InGame || Current.paused == paused) return;
	const ULONGLONG now = GetTickCount64();
	CloseSegment(now);
	Current.paused = paused;
	Log(false, "game %s", paused ? "paused: clocks stopped" : "resumed");
}

// Caller holds Lock. Current farming and town time, including the open segment.
ULONGLONG CurrentFarm(ULONGLONG now) { return Current.farmMs + (Current.inTown || Current.paused ? 0 : now - Current.segmentStart); }
ULONGLONG CurrentTown(ULONGLONG now) { return Current.townMs + (Current.inTown && !Current.paused ? now - Current.segmentStart : 0); }

// What dropped an item, from its item level against the area's monster level.
// Also tells whether the area was terrorized: its monsters were above the
// area's own level.
Source EstimateSource(const Run& run, const PendingItem& item, std::int32_t characterLevel, bool& terror) {
	terror = false;
	if (item.inTown) return SourceTown;
	const auto* level = FindLevel(item.levelId);
	if (!level || run.difficulty < 0 || run.difficulty > 2) return SourceOther;
	const int ilvl = static_cast<int>(item.record.itemLevel), base = level->monsterLevel[run.difficulty];
	// A boss killed in the same area before the pickup dropped the item when
	// its level is at least the boss's. In a terror zone the boss is raised
	// above its own level (Mephisto drops level 99 items there).
	for (const auto& kill : run.bosses)
		if (kill.tick <= item.tick + 1000 && (kill.levelId < 0 || kill.levelId == item.levelId) && ilvl >= kill.boss->level[run.difficulty]) {
			terror = ilvl > kill.boss->level[run.difficulty];
			return SourceBoss;
		}
	if (base <= 0) return SourceOther;
	int floor = base;
	if (ilvl > base + 3) {
		// Terror zones raise monsters to 2 levels above the character, up to
		// 45 / 71 / 96 by difficulty.
		terror = true;
		static constexpr int caps[3] { 45, 71, 96 };
		if (!characterLevel) return SourceOther;
		floor = std::max(base, std::min(characterLevel + 2, caps[run.difficulty]));
	}
	switch (ilvl - floor) {
		case 0:  return SourceTrash;
		case 2:  return SourceChampion;
		case 3:  return SourceElite;
		default: return SourceOther;
	}
}

// Writes out a finished game once its save file is known. Runs on its own
// thread, a moment after GameLeft, so the save written on leaving is read.
struct FinishedGame {
	Run                                  run;
	std::map<std::wstring, std::int64_t> startGold;
	std::int64_t                         startSharedGold {};
	bool                                 goldRead {};
	ULONGLONG                            leftAt {}; // FILETIME
	std::uint32_t                        resets {}; // Resets when the game ended
};

// Caller holds Lock. The game in progress as a finished one would be written,
// so the stats panel shows it before it ends. The character is the last one
// saved (the save is only read when the game ends); gold is unknown.
bool LiveGame(RunRecord& record, std::vector<ItemRecord>& items) {
	if (!InGame || !Current.number) return false;
	const ULONGLONG now = GetTickCount64();
	record.id           = Current.number;
	record.start        = Current.startTime;
	record.end          = Now();
	record.stamp        = Stamp(Current.startTime);
	record.farm         = static_cast<std::uint32_t>(CurrentFarm(now) / 1000);
	record.town         = static_cast<std::uint32_t>(CurrentTown(now) / 1000);
	record.seconds      = record.farm + record.town;
	record.difficulty   = Current.difficulty;
	record.area         = Current.number ? RunArea(Current) : std::string();
	record.items        = static_cast<std::uint32_t>(Current.items.size());
	record.level        = Current.characterLevel;
	for (auto run = AllRuns.rbegin(); run != AllRuns.rend() && record.character.empty(); ++run) record.character = run->character;
	for (const auto& pending : Current.items) {
		auto item       = pending.record;
		bool terror     = false;
		item.source     = EstimateSource(Current, pending, Current.characterLevel, terror);
		item.run        = Current.number;
		item.runArea    = record.area;
		item.character  = record.character;
		item.difficulty = Current.difficulty;
		if (terror) record.terror = true;
		items.push_back(item);
	}
	for (auto& item : items) item.terror = record.terror;
	return true;
}

// Caller holds Lock. Adds a town game's gold to the same character's last
// run (any character's when it is unknown); false when there is none.
bool AddTownGold(const RunRecord& town) {
	for (auto run = AllRuns.rbegin(); run != AllRuns.rend(); ++run)
		if (run->id && (town.character.empty() || run->character == town.character)) {
			run->gold += town.gold;
			run->goldKnown = true;
			return true;
		}
	return false;
}

void WriteGame(FinishedGame& game, bool wait) {
	// The save of the character that just left: the newest one, written in the
	// last few seconds.
	SaveInfo save;
	for (int attempt = 0; attempt < (wait ? 12 : 1); ++attempt) {
		if (attempt) Sleep(500);
		const auto saves = ListSaves(false);
		const auto newest = std::max_element(saves.begin(), saves.end(), [](const SaveInfo& a, const SaveInfo& b) { return a.written < b.written; });
		if (newest != saves.end() && newest->written + 30ULL * 10000000ULL >= game.leftAt) {
			save = ReadSave(newest->path);
			break;
		}
	}
	if (wait) Sleep(1000); // the shared stash is written right after
	auto& run = game.run;
	const std::int32_t characterLevel = save.valid ? save.level : run.characterLevel;

	RunRecord record;
	record.id         = run.number;
	record.start      = run.startTime;
	record.end        = Now();
	record.stamp      = Stamp(run.startTime);
	record.seconds    = static_cast<std::uint32_t>((run.end - run.start) / 1000);
	record.farm       = static_cast<std::uint32_t>(run.farmMs / 1000);
	record.town       = static_cast<std::uint32_t>(run.townMs / 1000);
	record.difficulty = run.difficulty;
	record.area       = run.number ? RunArea(run) : std::string();
	for (const auto level : run.levels) {
		if (!record.levels.empty()) record.levels += " > ";
		record.levels += LevelText(level);
	}
	for (const auto& kill : run.bosses) {
		if (!record.bosses.empty()) record.bosses += ", ";
		record.bosses += Text(kill.boss->key, kill.boss->key);
	}
	record.items     = static_cast<std::uint32_t>(run.items.size());
	record.character = save.valid ? save.name : std::string();
	record.level     = characterLevel;
	if (save.valid && game.goldRead) {
		if (const auto found = game.startGold.find(save.path); found != game.startGold.end()) {
			record.goldKnown = true;
			record.gold      = save.gold - found->second + (SharedStashGold() - game.startSharedGold);
		}
	}

	std::vector<ItemRecord> items;
	for (auto& pending : run.items) {
		auto& item     = pending.record;
		bool  terror   = false;
		item.source    = EstimateSource(run, pending, characterLevel, terror);
		item.terror    = terror;
		item.run       = run.number;
		item.runArea   = record.area;
		item.character = record.character;
		item.difficulty = run.difficulty;
		// The run was in a terror zone if anything picked up out of town shows it.
		if (terror) record.terror = true;
		items.push_back(item);
	}
	// Items share their run's terror zone flag, so the stats filter both alike.
	for (auto& item : items) item.terror = record.terror;

	if (!run.number) {
		std::lock_guard guard(Lock);
		--PendingFinishes;
		if (game.resets != Resets || !record.goldKnown || !record.gold || !AddTownGold(record)) {
			Log(false, "game ended without leaving town: not saved (%s in town, gold %s)", Duration(run.townMs).c_str(), record.goldKnown ? Thousands(record.gold).c_str() : "unknown");
			return;
		}
		// Kept in runs.csv as a town row, folded into the previous run on load.
		AppendLines(RunsFile, RunsHeader, { RunCsv(record) });
		Log(false, "game spent in town: gold %s added to the previous run of %s", Thousands(record.gold).c_str(), record.character.empty() ? "(character unknown)" : record.character.c_str());
		return;
	}

	std::vector<std::string> itemLines;
	for (const auto& item : items) itemLines.push_back(ItemCsv(item));
	{
		std::lock_guard guard(Lock);
		--PendingFinishes;
		if (game.resets != Resets) {
			Log(false, "game ended before the stats were deleted: not written");
			return;
		}
		AllRuns.push_back(record);
		AllItems.insert(AllItems.end(), items.begin(), items.end());
		AppendLines(RunsFile, RunsHeader, { RunCsv(record) });
		AppendLines(ItemsFile, ItemsHeader, itemLines);
	}
	Log(false, "run %u written: %s, level %d, farming %s, town %s, %s%s, %zu item(s), gold %s", run.number, record.character.empty() ? "(character unknown)" : record.character.c_str(),
	    record.level, Duration(run.farmMs).c_str(), Duration(run.townMs).c_str(), record.area.c_str(), record.terror ? " (terrorized)" : "", items.size(),
	    record.goldKnown ? Thousands(record.gold).c_str() : "unknown");
}

DWORD WINAPI WriteGameThread(LPVOID parameter) {
	auto* game = static_cast<FinishedGame*>(parameter);
	WriteGame(*game, true);
	delete game;
	return 0;
}

// Caller holds Lock. wait = false when the plugin unloads: write right away.
void FinishRun(bool wait = true) {
	if (!InGame) return;
	InGame      = false;
	Current.end = GetTickCount64();
	CloseSegment(Current.end);
	if (!Config.active) {
		Log(false, "game ended while tracking is off: not saved");
		if (Current.number && Current.number + 1 == NextRun) --NextRun;
		return;
	}
	// A game that never left town is not a run: only gold it gained (sales)
	// is kept, added to the character's previous run once the save is read.
	if (!Current.number && !StartGoldRead) {
		Log(false, "game ended without leaving town: not saved (%s in town)", Duration(Current.townMs).c_str());
		return;
	}
	auto* game = new FinishedGame { .run = Current, .startGold = StartGold, .startSharedGold = StartSharedGold, .goldRead = StartGoldRead, .leftAt = FileTimeNow(), .resets = Resets };
	++PendingFinishes;
	Log(false, "run %u ended: farming %s, town %s, %s, %zu item(s)", Current.number, Duration(Current.farmMs).c_str(), Duration(Current.townMs).c_str(), RunArea(Current).c_str(),
	    Current.items.size());
	if (!wait) {
		Lock.unlock();
		WriteGame(*game, false);
		Lock.lock();
		delete game;
		return;
	}
	if (HANDLE thread = CreateThread(nullptr, 0, WriteGameThread, game, 0, nullptr)) CloseHandle(thread);
	else {
		Lock.unlock();
		WriteGame(*game, false);
		Lock.lock();
		delete game;
	}
}

// Caller holds Lock.
void StartRun() {
	if (InGame) FinishRun();
	Current              = Run {};
	Current.start        = GetTickCount64();
	Current.segmentStart = Current.start;
	Current.startTime    = Now();
	InGame               = true;
}

void ResetSession() {
	std::lock_guard guard(Lock);
	SessionStamp = Stamp(Now());
	Log(false, "session reset");
}

// ---- items ----

std::uint64_t ItemKey(const Items::ItemInfo& item) {
	if (item.itemSeed) return static_cast<std::uint64_t>(item.itemSeed) | static_cast<std::uint64_t>(item.classId) << 32;
	return 1ULL << 63 | (Session & 0xFFFFFF) << 32 | item.runtimeId;
}

// Every id the game gives an item: a dropped item can come back with a new
// one, so an item is new only when none of them is known.
std::vector<std::uint64_t> ItemKeys(const Items::ItemInfo& item) {
	std::vector<std::uint64_t> keys { ItemKey(item), 1ULL << 63 | (Session & 0xFFFFFF) << 32 | item.runtimeId };
	if (item.generationSeed) keys.push_back(1ULL << 62 | static_cast<std::uint64_t>(item.classId) << 32 | item.generationSeed);
	return keys;
}

// What stays the same when an item is dropped and picked up again.
std::string Fingerprint(const Items::ItemInfo& item) {
	std::string text = std::to_string(item.code) + "," + std::to_string(static_cast<int>(item.quality)) + "," + std::to_string(item.itemLevel) + "," +
	                   std::to_string(item.qualityRecordId) + "," + std::to_string(item.socketCount);
	for (const auto id : item.prefixIds) text += "," + std::to_string(id);
	for (const auto id : item.suffixIds) text += "," + std::to_string(id);
	return text;
}

std::string ItemName(const Items::ItemInfo& item, const std::string& code, char* color = nullptr) {
	const auto base = BaseText(code, color);
	const bool unique = item.quality == Items::Quality::Unique, set = item.quality == Items::Quality::Set;
	if ((unique || set) && item.qualityRecordId >= 0) {
		const auto id = static_cast<std::uint32_t>(item.qualityRecordId);
		for (const auto& record : unique ? std::span<const RecordName>(UniqueRows) : std::span<const RecordName>(SetRows)) {
			if (record.id != id) continue;
			if (code == record.code) return Text(record.key, record.key, color);
			Log(true, "%s row %u is %s but the item is %s; check tables.inc", unique ? "UniqueItems" : "SetItems", id, record.code, code.c_str());
			break;
		}
		return base + (unique ? " (unique)" : " (set)");
	}
	return base;
}

// The setting of an item's kind (not runes): TrackOff, TrackOn or TrackHidden.
int Tracking(const ItemClass* cls, Items::Quality quality, const std::string& code) {
	if (cls && cls->group == 7 && quality <= Items::Quality::Superior) return Config.consumables;
	if (cls && cls->group == 2) return Config.gems;
	if (cls && cls->group == 5) return code.rfind("xa", 0) == 0 ? Config.shards : Config.essences;
	switch (quality) {
		case Items::Quality::Unique:   return Config.uniques;
		case Items::Quality::Set:      return Config.sets;
		case Items::Quality::Rare:     return Config.rares;
		case Items::Quality::Magic:    return Config.magic;
		case Items::Quality::Crafted:  return TrackOff; // made in the Cube, never dropped
		default:                       return Config.normal; // normal, superior, low
	}
}

// Runewords are made, never dropped, so they are not counted.
bool Counts(const Items::ItemInfo& item, const std::string& code) {
	if (std::find(Config.codes.begin(), Config.codes.end(), code) != Config.codes.end()) return true;
	if (const int rune = RuneNumber(code)) return Config.minRune && rune >= Config.minRune;
	return Tracking(FindClass(code), item.quality, code) != TrackOff;
}

// ---- native item reads (game thread) ----
// Stackable items (gems, runes, essences and the like) report item level 1,
// but the game keeps the level they dropped at in its own item data
// (ItemData + 0x38, ItemData at unit + 0x10). It is read on the game thread
// with ReadProcessMemory, so a wrong pointer fails instead of crashing, and
// only kept when the unit and quality fields match the item.
constexpr std::size_t UnitDataOffset = 0x10, DataLevelOffset = 0x38;

bool ReadMemory(const void* address, void* out, std::size_t size) {
	SIZE_T read = 0;
	return address && ReadProcessMemory(GetCurrentProcess(), address, out, size, &read) && read == size;
}

struct NativeRequest {
	D2RL::ItemHandle handle;
	std::uint32_t    runtimeId;
	std::uint32_t    quality;
	std::string      code;
	std::uint64_t    session;
	std::uint32_t    level {}; // read from the native item, 0 if unknown
};

#ifdef RUNS_DROPPER_DUMP
// Test build: also logs the native bytes of each item picked up, to find
// where the game keeps the monster that dropped it.
std::string HexDump(const void* address, std::size_t size) {
	std::vector<unsigned char> bytes(size);
	if (!ReadMemory(address, bytes.data(), size)) return "??";
	static const char digits[] = "0123456789abcdef";
	std::string       out;
	for (const auto byte : bytes) out += digits[byte >> 4], out += digits[byte & 15];
	return out;
}
#endif

void __cdecl ReadNativeItem(const D2RL::PluginContext*, void* native, void* userData) noexcept {
	auto*         request = static_cast<NativeRequest*>(userData);
	std::uint32_t head[3] {}; // unit type, class, unit id
	const char*   data = nullptr;
	std::uint32_t quality = 0, level = 0;
	if (ReadMemory(native, head, sizeof(head)) && head[0] == 4 && head[2] == request->runtimeId
	    && ReadMemory(static_cast<const char*>(native) + UnitDataOffset, &data, sizeof(data)) && ReadMemory(data, &quality, sizeof(quality))
	    && quality == request->quality && ReadMemory(data + DataLevelOffset, &level, sizeof(level)) && level >= 1 && level <= 120)
		request->level = level;
#ifdef RUNS_DROPPER_DUMP
	for (std::size_t at = 0; at < 0x400; at += 0x100) {
		Log(false, "dump item %u %s unit+%03zx %s", request->runtimeId, request->code.c_str(), at, HexDump(static_cast<const char*>(native) + at, 0x100).c_str());
		Log(false, "dump item %u %s data+%03zx %s", request->runtimeId, request->code.c_str(), at, HexDump(data ? data + at : nullptr, 0x100).c_str());
	}
#endif
}

// The handle from a UI-thread scan is stale on the game thread: the item is
// looked up again there by its unit id.
auto __cdecl FindNativeItem(const D2RL::PluginContext*, const Items::ItemInfo* item, void* userData) noexcept -> D2RL::Inventory::IterationAction {
	auto* request = static_cast<NativeRequest*>(userData);
	if (!item || item->runtimeId != request->runtimeId) return D2RL::Inventory::IterationAction::Continue;
	request->handle = item->handle;
	return D2RL::Inventory::IterationAction::Stop;
}

void __cdecl ReadOnGameThread(const D2RL::PluginContext* context, void* userData) noexcept {
	std::unique_ptr<NativeRequest> request(static_cast<NativeRequest*>(userData));
	D2RL::PlayerHandle             player = D2RL::InvalidPlayerHandle;
	if (!Inventory || Inventory->getLocalPlayer(context, &player) != D2RL::Inventory::Result::Success) return;
	const D2RL::Inventory::ItemFilter filter { .structSize = D2RL::Inventory::ItemFilterSize, .containerMask = Items::AllItemContainers };
	request->handle = D2RL::InvalidItemHandle;
	(void)Inventory->forEachInventoryItem(context, player, &filter, FindNativeItem, request.get());
	const auto result = request->handle == D2RL::InvalidItemHandle ? Items::Result::NotFound : ItemApi->editNativeItem(context, request->handle, ReadNativeItem, request.get());
	if (result != Items::Result::Success || !request->level) {
		static bool told = false;
		if (!told) Log(true, "item level of %s: not readable from the game (result %u); stackable items keep level 1", request->code.c_str(), static_cast<unsigned>(result));
		told = true;
		return;
	}
	std::lock_guard guard(Lock);
	if (request->session != Session) return;
	for (auto& pending : Current.items)
		if (pending.runtimeId == request->runtimeId && pending.record.itemLevel <= 1) {
			pending.record.itemLevel = request->level;
			Log(false, "item level of %s read from the game: %u", pending.record.name.c_str(), request->level);
		}
}

// UI thread: reads the real level of a stackable item (or, in the test
// build, of every item) on the game thread.
void ReadItemLevel(const Items::ItemInfo& info, const std::string& code) {
#ifndef RUNS_DROPPER_DUMP
	if (info.itemLevel > 1) return;
#endif
	if (!ItemApi || !Threads || !Inventory) return;
	auto* request = new NativeRequest { .handle = info.handle, .runtimeId = info.runtimeId, .quality = static_cast<std::uint32_t>(info.quality), .code = code, .session = Session };
	if (Threads->runOnGameThread(Context, ReadOnGameThread, request) != D2RL::Threads::Result::Success) delete request;
}

// Caller holds Lock. Why an item picked up now cannot be a drop, or null.
// Nothing found in town counts (stash, vendors, gambling). Monsters drop
// items at their own level, never below the area's lowest (normal difficulty
// when it is not known yet). Stackable items (gems, runes and the like) are
// level 1 even when dropped, so they are left out of that check.
const char* NotDropped(std::uint32_t itemLevel) {
	if (Current.inTown) return "in town";
	const auto* level = FindLevel(Current.level);
	if (!level) return nullptr;
	const int difficulty = Current.difficulty >= 0 && Current.difficulty <= 2 ? Current.difficulty : 0;
	const int lowest     = level->monsterLevel[difficulty];
	return lowest > 1 && itemLevel > 1 && static_cast<int>(itemLevel) < lowest ? "below the area level" : nullptr;
}

void RecordDrop(const Items::ItemInfo& info, const std::string& code) {
	PendingItem pending;
	auto&       item = pending.record;
	item.time        = Now();
	item.name        = ItemName(info, code, &item.color);
	item.code        = code;
	item.quality     = info.quality;
	item.itemLevel   = info.itemLevel;
	item.ethereal    = (info.stateFlags & Items::ItemStateEthereal) != 0;
	item.sockets     = info.socketCount;
	item.cls         = FindClass(code);
	item.rune        = RuneNumber(code);
	pending.tick     = GetTickCount64();
	pending.runtimeId = info.runtimeId;
	std::uint32_t run = 0;
	{
		std::lock_guard guard(Lock);
		run             = Current.number;
		pending.levelId = Current.level;
		pending.inTown  = Current.inTown;
		item.area       = LevelText(Current.level);
		// Not a drop: found in town, or too low to have dropped here.
		if (const char* why = NotDropped(info.itemLevel)) {
			Log(false, "item not counted (%s): %s (ilvl %u)", why, item.name.c_str(), info.itemLevel);
			return;
		}
		Current.items.push_back(pending);
	}
	ReadItemLevel(info, code);
	Log(false, "%s%s item: %s (%s, ilvl %u)%s", run ? "run " : "town", run ? std::to_string(run).c_str() : "", item.name.c_str(), QualityName(item.quality), item.itemLevel, item.ethereal ? " eth" : "");
	if (Notable(item)) {
		if (run) Say("Run %u: %s", run, item.name.c_str());
		else Say("Town: %s", item.name.c_str());
	}
}

struct ScanResult {
	std::vector<Items::ItemInfo> fresh;
	std::unordered_map<std::uint64_t, std::string> owned;
	std::unordered_map<std::uint64_t, std::int32_t> cube;
	std::uint32_t                count {};
	std::uint32_t                perContainer[11] {};
};

auto __cdecl CollectItem(const D2RL::PluginContext*, const Items::ItemInfo* item, void* userData) noexcept -> D2RL::Inventory::IterationAction {
	auto* scan = static_cast<ScanResult*>(userData);
	if (!item || item->structSize < Items::ItemInfoRequiredSize) return D2RL::Inventory::IterationAction::Continue;
	++scan->count;
	const auto container = static_cast<std::uint32_t>(item->container);
	if (container < 11) ++scan->perContainer[container];
	scan->owned[ItemKey(*item)] = Fingerprint(*item);
	if (item->container == Items::ItemContainer::Cube) scan->cube[ItemKey(*item)] = item->quantity;
	const auto keys = ItemKeys(*item);
	if (std::none_of(keys.begin(), keys.end(), [](std::uint64_t key) { return Known.contains(key); })) scan->fresh.push_back(*item);
	return D2RL::Inventory::IterationAction::Continue;
}

// Where a new item can be a find. Items that first show up in a stash were
// already owned (pages the game loads late). New Cube items are checked in
// Scan: they are finds unless the Cube was just used.
bool FoundIn(Items::ItemContainer container) {
	using enum Items::ItemContainer;
	return container != Cube && container != PersonalStash && container != SharedStash && container != CustomPage && container != Trade;
}

// UI thread. With baseline, every item found becomes known without being recorded.
void Scan(bool baseline) {
	{
		std::lock_guard guard(Lock);
		if (!InGame) return;
	}
	// Wait for LocalPlayerReady so the baseline holds the character's items; if
	// that event never comes, start anyway a few seconds into the game.
	if (!Inventory || (!PlayerReady && GetTickCount64() - JoinedAt < 5000)) return;
	if (Player == D2RL::InvalidPlayerHandle && Inventory->getLocalPlayer(Context, &Player) != D2RL::Inventory::Result::Success) {
		Player = D2RL::InvalidPlayerHandle;
		return;
	}
	ScanResult                        scan;
	const D2RL::Inventory::ItemFilter filter { .structSize = D2RL::Inventory::ItemFilterSize, .containerMask = Items::AllItemContainers };
	const auto                        result = Inventory->forEachInventoryItem(Context, Player, &filter, CollectItem, &scan);
	if (result == D2RL::Inventory::Result::StaleHandle) {
		Player = D2RL::InvalidPlayerHandle;
		return;
	}
	if (result != D2RL::Inventory::Result::Success) return;

	for (const auto& [key, fingerprint] : Owned)
		if (!scan.owned.contains(key)) Gone.insert(fingerprint);
	Owned = std::move(scan.owned);
	// A Cube item used up (gone from your items, or a smaller stack) means a
	// transmute: what is new in the Cube then is its output, not a find.
	bool transmuted = false;
	for (const auto& [key, quantity] : InCube) {
		const auto now = scan.cube.find(key);
		if (!Owned.contains(key) || (now != scan.cube.end() && now->second < quantity)) transmuted = true;
	}
	InCube = std::move(scan.cube);

	std::vector<Items::ItemInfo> drops;
	for (const auto& item : scan.fresh) {
		for (const auto key : ItemKeys(item)) Known.insert(key);
		// Socketed items (runes in a runeword, jewels...) come with their host.
		if (item.container == Items::ItemContainer::Unknown) continue;
		// An item that left your items this game and came back.
		if (const auto back = Gone.find(Fingerprint(item)); back != Gone.end()) {
			Gone.erase(back);
			if (!baseline && Baselined) Log(false, "item picked up again, not counted: %s", ItemName(item, CodeText(item.code)).c_str());
			continue;
		}
		// Items put straight into the Cube count, unless the Cube was just used.
		const bool found = FoundIn(item.container) || (item.container == Items::ItemContainer::Cube && !transmuted);
		if (!baseline && Baselined && Config.active && found && Counts(item, CodeText(item.code))) drops.push_back(item);
	}
	if (!Baselined || baseline || drops.size() > MaxDropsPerScan) {
		const auto& c = scan.perContainer;
		Log(false, "baseline: %u items known (inventory %u, belt %u, cube %u, equipped %u, stash %u, shared stash %u, cursor %u, other pages %u)%s", scan.count,
		    c[static_cast<int>(Items::ItemContainer::Inventory)], c[static_cast<int>(Items::ItemContainer::Belt)], c[static_cast<int>(Items::ItemContainer::Cube)],
		    c[static_cast<int>(Items::ItemContainer::Equipment)], c[static_cast<int>(Items::ItemContainer::PersonalStash)], c[static_cast<int>(Items::ItemContainer::SharedStash)],
		    c[static_cast<int>(Items::ItemContainer::Cursor)], c[static_cast<int>(Items::ItemContainer::CustomPage)],
		    Baselined && drops.size() > MaxDropsPerScan ? " (too many new items at once, taken as already owned)" : "");
		Baselined = true;
		return;
	}
	for (const auto& item : drops) RecordDrop(item, CodeText(item.code));
}

// ---- events ----

// Reads the gold in every save while the game starts, off the UI thread.
DWORD WINAPI ReadStartGold(LPVOID) {
	std::map<std::wstring, std::int64_t> gold;
	for (const auto& save : ListSaves(true))
		if (save.valid) gold[save.path] = save.gold;
	const auto shared = SharedStashGold();
	std::lock_guard guard(Lock);
	StartGold       = std::move(gold);
	StartSharedGold = shared;
	StartGoldRead   = true;
	return 0;
}

void __cdecl OnGameplayEvent(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent* event, void*) noexcept {
	using Kind = D2RL::Lifecycle::GameplayEventKind;
	if (!D2RL::Lifecycle::HasGameplayEventField(event, D2RL::Lifecycle::GameplayEventRequiredSize)) return;
	switch (event->kind) {
		case Kind::GameJoined: {
			Session   = event->sessionGeneration;
			Player    = D2RL::InvalidPlayerHandle;
			Baselined = false;
			JoinedAt  = GetTickCount64();
			Owned.clear();
			InCube.clear();
			Gone.clear();
			FrameLogged.store(false);
			Playing.store(true);
			{
				std::lock_guard guard(Lock);
				StartRun();
				StartGoldRead = false;
				Log(false, "game joined; it becomes run %u on leaving town", NextRun);
			}
			if (HANDLE thread = CreateThread(nullptr, 0, ReadStartGold, nullptr, 0, nullptr)) CloseHandle(thread);
			QueueTick();
			break;
		}
		case Kind::LocalPlayerReady:
			PlayerReady = true;
			Player      = D2RL::InvalidPlayerHandle;
			Scan(true);
			// As d2rl-buff-hud does: open the panel right here, on the UI
			// thread, then let the tick keep it filled.
			OpenGamePanel();
			QueueTick();
			break;
		case Kind::PlayerLevelChanged: {
			std::lock_guard guard(Lock);
			if (InGame && event->currentValue > 0) Current.characterLevel = event->currentValue;
			break;
		}
		case Kind::LevelChanged: {
			Scan(false);
			QueueTick();
			std::lock_guard guard(Lock);
			if (!InGame) break;
			Current.level = event->currentValue;
			if (Current.levels.empty() || Current.levels.back() != event->currentValue) Current.levels.push_back(event->currentValue);
			CloseSegment(GetTickCount64());
			const auto* level = FindLevel(event->currentValue);
			Current.inTown    = level && level->town;
			if (!Current.inTown && !Current.number) {
				Current.number = NextRun++;
				Log(false, "run %u started (left town after %s)", Current.number, Duration(Current.townMs).c_str());
			}
			break;
		}
		case Kind::GameLeft: {
			Scan(false);
			Player      = D2RL::InvalidPlayerHandle;
			PlayerReady = false;
			Playing.store(false);
			TickScheduled.store(false);
			CloseGamePanel();
			std::lock_guard guard(Lock);
			FinishRun();
			break;
		}
		default: break;
	}
}

// Server thread, after the native drop. Records boss kills and the difficulty.
void __cdecl OnMonsterDeath(const D2RL::PluginContext*, const D2RL::Lifecycle::MonsterDeathEvent* event, void*) noexcept {
	if (!D2RL::Lifecycle::HasMonsterDeathEventField(event, D2RL::Lifecycle::MonsterDeathEventRequiredSize)) return;
	const MonsterName* boss = nullptr;
	for (const auto& candidate : Bosses)
		if (candidate.classId == event->monster.classId) boss = &candidate;
#ifdef RUNS_DROPPER_DUMP
	Log(false, "death monster %u class %u level %d killer type %u id %u class %u", event->monster.id, event->monster.classId, event->levelId,
	    static_cast<unsigned>(event->killer.type), event->killer.id, event->killer.classId);
#endif
	std::lock_guard guard(Lock);
	if (!InGame) return;
	if (event->difficulty <= 2) Current.difficulty = static_cast<std::int32_t>(event->difficulty);
	if (boss) Current.bosses.push_back({ boss, GetTickCount64(), event->levelId });
}

// ---- lifetime numbers for the small panel (caller holds Lock) ----

std::uint32_t LifetimeRuns() {
	std::uint32_t runs = 0;
	for (const auto& run : AllRuns) runs += run.id != 0;
	return runs;
}

ULONGLONG AverageFarmMs() {
	ULONGLONG     total = 0;
	std::uint32_t runs  = 0;
	for (const auto& run : AllRuns)
		if (run.id) total += run.farm * 1000ULL, ++runs;
	return runs ? total / runs : 0;
}

// Items per finished run, in tenths.
ULONGLONG AverageItemsTenths() {
	ULONGLONG     items = 0;
	std::uint32_t runs  = 0;
	for (const auto& run : AllRuns)
		if (run.id) items += run.items, ++runs;
	return runs ? (items * 10 + runs / 2) / runs : 0;
}

// ---- overlay panel contents ----

struct Line {
	std::string    text;
	Overlay::Color color;
};

Overlay::Color QualityColor(const ItemRecord& item) {
	if (item.rune) return { 1.0f, 0.66f, 0.0f, 1.0f };
	switch (item.quality) {
		case Items::Quality::Unique:  return { 0.78f, 0.70f, 0.47f, 1.0f };
		case Items::Quality::Set:     return { 0.0f, 0.93f, 0.0f, 1.0f };
		case Items::Quality::Rare:    return { 1.0f, 1.0f, 0.39f, 1.0f };
		case Items::Quality::Magic:   return { 0.41f, 0.41f, 1.0f, 1.0f };
		case Items::Quality::Crafted: return { 1.0f, 0.66f, 0.0f, 1.0f };
		default:                      return { 1.0f, 1.0f, 1.0f, 1.0f };
	}
}

// Caller holds Lock.
std::vector<Line> BuildLines(ULONGLONG now) {
	const Overlay::Color white { 1.0f, 1.0f, 1.0f, 1.0f }, gray { 0.7f, 0.7f, 0.7f, 1.0f };
	std::vector<Line>    lines;
	lines.push_back({ "Run " + std::to_string(Current.number ? Current.number : NextRun) + "   farming " + Duration(CurrentFarm(now)) + "   town " + Duration(CurrentTown(now)), white });
	lines.push_back({ "Avg farming " + Duration(AverageFarmMs()), gray });
	const auto average = AverageItemsTenths();
	lines.push_back({ "Items " + std::to_string(Current.items.size()) + " this run, " + std::to_string(average / 10) + "." + std::to_string(average % 10) + " per run", gray });
	const auto shown = std::min<std::size_t>(Current.items.size(), static_cast<std::size_t>(Config.lastDrops));
	for (std::size_t i = Current.items.size() - shown; i < Current.items.size(); ++i)
		lines.push_back({ Current.items[i].record.name + (Current.items[i].record.ethereal ? " (eth)" : ""), QualityColor(Current.items[i].record) });
	return lines;
}

// ---- panel in the game's own UI ----
// D2RLoader 1.3.1 can open plugin panels and show or hide their widgets, but it
// cannot change a widget's text (WidgetService::setText came later). So the
// layout holds one text box per character each number can show, and the
// plugin hides all of them but the right ones.
// What the game showed on my setup decides the shape of this:
//   - "visible": false in the layout is not applied, so every box is hidden by
//     the plugin after the panel opens;
//   - the loader hands out about 250 widget handles, so the panel stays under
//     220 boxes: numbers only, no item names (notable finds go to the console);
//   - the panel stays open for the whole game and the show/hide control hides
//     its root, so handles are not used up again by reopening it.
//   - its priority is below the stock panels (stash, vendor, inventory...:
//     0 to 10), so a panel opened on the left covers it.

constexpr const char* PanelName  = "run-counter/RunPanel";
constexpr const char* Digits     = "0123456789";

// One character position: the characters it can show.
struct Slot {
	const char* chars;
	int         offset; // x, in digit widths from the field start (times 2)
};

// Values are right-aligned on one edge (10 half digits from the value
// column), so times line up on their colon whatever their length.
// "m:ss" fields: two minute digits, a fixed colon, two second digits.
constexpr Slot TimeSlots[] { { "123456789", 1 }, { Digits, 3 }, { "012345", 6 }, { Digits, 8 } };
constexpr Slot Number4[] { { Digits, 2 }, { Digits, 4 }, { Digits, 6 }, { Digits, 8 } };
constexpr Slot Number2[] { { Digits, 6 }, { Digits, 8 } };
// "12.3" fields: two digits, a fixed point, the tenths.
constexpr Slot TenthSlots[] { { "123456789", 3 }, { Digits, 5 }, { Digits, 8 } };

struct Field {
	int                   row, column;
	const char*           label;
	std::span<const Slot> slots;
	bool                  time;
	bool                  tenths {}; // the value is in tenths, shown with a point
};
const Field Fields[] {
	{ 0, 0, "Run", Number4, false },  { 1, 0, "Farm", TimeSlots, true }, { 2, 0, "Town", TimeSlots, true },
	{ 3, 0, "Avg", TimeSlots, true }, { 4, 0, "Items", Number2, false }, { 5, 0, "Per run", TenthSlots, false, true },
};
constexpr int FieldCount = 6;
constexpr int MaxSlots   = 5;

std::string                      PanelLayout;
D2RL::Panels::RegistrationHandle PanelHandle = D2RL::Panels::InvalidHandle;
ULONGLONG                        PanelOpenTry {};
D2RL::Panels::Result             PanelLastResult = D2RL::Panels::Result::Success;
bool                             PanelLogged {};
// Character index shown in each slot, -1 for none, -2 when unknown (just opened).
int                              SlotShown[FieldCount][MaxSlots];
int                              RootShown = -1; // root visibility: 1, 0, or -1 unknown
int                              BackShown = -1; // background visibility, the same way
D2RL::Widgets::WidgetHandle      PanelRoot = D2RL::Widgets::InvalidHandle;
std::unordered_map<std::string, D2RL::Widgets::WidgetHandle> WidgetHandles;

std::string Json(std::string_view text) {
	std::string out = "\"";
	for (const char c : text) {
		if (c == '"' || c == '\\') out += '\\';
		if (static_cast<unsigned char>(c) >= 0x20) out += c;
	}
	return out + '"';
}

std::string TextBox(const std::string& name, int x, int y, int width, int height, const std::string& text, const char* color, const char* size = "$MediumFontSize",
                    const char* align = "left") {
	char rect[160] {};
	std::snprintf(rect, sizeof(rect), R"("rect": {"x": %d, "y": %d, "width": %d, "height": %d})", x, y, width, height);
	return R"({"type": "TextBoxWidget", "name": )" + Json(name) + R"(, "fields": {)" + rect + R"(, "text": )" + Json(text) + R"(, "style": {"fontColor": ")" + color
	     + R"(", "pointSize": ")" + size + R"(", "alignment": {"h": ")" + align + R"(", "v": "center"}}}})";
}

// Written without printf's %f, which follows the game's locale and gives
// "0,010" on a French system: invalid JSON.
std::string Fraction(float value) {
	const int thousandths = static_cast<int>(value * 1000.0f + 0.5f);
	char      text[16] {};
	std::snprintf(text, sizeof(text), "%d.%03d", thousandths / 1000, thousandths % 1000);
	return text;
}

std::string BoxName(int field, int slot, int index) { return "F" + std::to_string(field) + "S" + std::to_string(slot) + "C" + std::to_string(index); }

std::string BuildPanelLayout() {
	const float s      = Config.textScale;
	const int   height = static_cast<int>(36 * s), half = static_cast<int>(12 * s), label = static_cast<int>(130 * s), column = static_cast<int>(300 * s);
	std::string layout = R"({"type": "Panel", "name": ")" + std::string(PanelName) + R"(", "fields": {"anchor": {"x": )" + Fraction(Config.x) + R"(, "y": )" + Fraction(Config.y)
	                   + R"(}, "priority": -2}, "children": [)";
	bool first = true;
	auto add   = [&](const std::string& child) {
        if (!first) layout += ", ";
        layout += child;
        first = false;
	};
	// The background, hidden at run time when panel_background is off.
	char back[200] {};
	std::snprintf(back, sizeof(back), R"({"type": "RectangleWidget", "name": "Back", "fields": {"rect": {"x": %d, "y": %d, "width": %d, "height": %d}, "color": [0.0, 0.0, 0.0, 0.55]}})", -half,
	              -half / 2, label + 12 * half, 6 * height + half);
	add(back);
	// One indicator per row, values in one column. Each character is centered
	// in its own cell, so wide and narrow digits line up.
	for (int f = 0; f < FieldCount; ++f) {
		const auto& field = Fields[f];
		const int   x = field.column * column, y = field.row * height;
		const char* color = f < 2 ? "$FontColorWhite" : "$FontColorLightGray";
		add(TextBox("Label" + std::to_string(f), x, y, label, height, field.label, "$FontColorGray"));
		if (field.time) add(TextBox("Colon" + std::to_string(f), x + label + 5 * half, y, half, height, ":", color, "$MediumFontSize", "center"));
		if (field.tenths) add(TextBox("Point" + std::to_string(f), x + label + 7 * half, y, half, height, ".", color, "$MediumFontSize", "center"));
		for (std::size_t slot = 0; slot < field.slots.size(); ++slot)
			for (int c = 0; field.slots[slot].chars[c]; ++c)
				add(TextBox(BoxName(f, static_cast<int>(slot), c), x + label + field.slots[slot].offset * half, y, 2 * half, height, std::string(1, field.slots[slot].chars[c]), color,
				            "$MediumFontSize", "center"));
	}
	return layout + "]}";
}

bool FindLogged {}, ShowLogged {}, ShowFailLogged {};

D2RL::Panels::Result OpenPanelNow() {
	const auto result = PanelApi->openPanel(Context, PanelHandle);
	if (result != PanelLastResult || !PanelLogged) Log(result != D2RL::Panels::Result::Success, "game-UI panel: open result %u", static_cast<unsigned>(result));
	PanelLastResult = result;
	PanelLogged     = true;
	if (result == D2RL::Panels::Result::Success) {
		// The widgets are new: everything shows until hidden, and old handles
		// are tried again before asking for new ones.
		for (auto& field : SlotShown)
			for (auto& slot : field) slot = -2;
		RootShown = BackShown = -1;
		PanelRoot = D2RL::Widgets::InvalidHandle;
	}
	return result;
}

bool IsOpen(D2RL::Panels::RegistrationHandle handle) {
	D2RL::Panels::PanelInfo info { .structSize = D2RL::Panels::PanelInfoSize };
	return handle != D2RL::Panels::InvalidHandle && PanelApi->getPanelInfo(Context, handle, &info) == D2RL::Panels::Result::Success
	    && info.presentationState == D2RL::Panels::PresentationState::Open;
}

bool PanelIsOpen() { return IsOpen(PanelHandle); }

// Sets a widget's visibility, reusing its handle when it is still valid.
bool SetVisible(const std::string& name, bool visible) {
	auto handle = D2RL::Widgets::InvalidHandle;
	if (const auto found = WidgetHandles.find(name); found != WidgetHandles.end()) handle = found->second;
	auto result = handle == D2RL::Widgets::InvalidHandle ? D2RL::Widgets::Result::NotFound : WidgetApi->setWidgetVisible(Context, handle, visible);
	if (result != D2RL::Widgets::Result::Success) {
		result = WidgetApi->findWidget(Context, PanelRoot, name.c_str(), &handle);
		if (result == D2RL::Widgets::Result::Success) {
			WidgetHandles[name] = handle;
			result              = WidgetApi->setWidgetVisible(Context, handle, visible);
		}
	}
	if (result != D2RL::Widgets::Result::Success && !ShowFailLogged) {
		Log(true, "game-UI panel: could not %s %s (result %u, %zu handles in use)", visible ? "show" : "hide", name.c_str(), static_cast<unsigned>(result), WidgetHandles.size());
		ShowFailLogged = true;
	}
	return result == D2RL::Widgets::Result::Success;
}

// Shows character choice (-1 = none) in a slot, hiding the one shown before,
// or all the others when that is unknown.
void ShowSlot(int field, int slot, int choice) {
	int& shown = SlotShown[field][slot];
	if (shown == choice) return;
	const int count = static_cast<int>(std::strlen(Fields[field].slots[slot].chars));
	if (shown == -2) {
		for (int i = 0; i < count; ++i)
			if (i != choice) SetVisible(BoxName(field, slot, i), false);
	} else if (shown >= 0) SetVisible(BoxName(field, slot, shown), false);
	if (choice >= 0) SetVisible(BoxName(field, slot, choice), true);
	shown = choice;
}

// The characters of a field, one per slot ('\0' = empty slot).
std::string FieldChars(const Field& field, ULONGLONG value) {
	std::string chars(field.slots.size(), '\0');
	if (field.time) {
		const auto seconds = std::min<ULONGLONG>(value / 1000, 99 * 60 + 59);
		if (seconds / 600) chars[0] = static_cast<char>('0' + seconds / 600);
		chars[1] = static_cast<char>('0' + seconds / 60 % 10);
		chars[2] = static_cast<char>('0' + seconds % 60 / 10);
		chars[3] = static_cast<char>('0' + seconds % 10);
	} else if (field.tenths) {
		const auto tenths = std::min<ULONGLONG>(value, 999);
		if (tenths / 100) chars[0] = static_cast<char>('0' + tenths / 100);
		chars[1] = static_cast<char>('0' + tenths / 10 % 10);
		chars[2] = static_cast<char>('0' + tenths % 10);
	} else {
		ULONGLONG most = 9;
		for (std::size_t i = 1; i < field.slots.size(); ++i) most = most * 10 + 9;
		const auto text = std::to_string(std::min<ULONGLONG>(value, most));
		for (std::size_t i = 0; i < text.size(); ++i) chars[chars.size() - text.size() + i] = text[i];
	}
	return chars;
}

void RefreshDrops(bool want);

// UI thread.
void RefreshPanel(ULONGLONG now) {
	if (!PanelApi || !WidgetApi || PanelHandle == D2RL::Panels::InvalidHandle) return;
	ULONGLONG values[FieldCount] {};
	bool      inGame = false, want = false;
	{
		std::lock_guard guard(Lock);
		inGame = InGame;
		want   = inGame && OverlayVisible.load();
		if (inGame) {
			values[0] = Current.number ? Current.number : NextRun;
			values[1] = CurrentFarm(now);
			values[2] = CurrentTown(now);
			values[3] = AverageFarmMs();
			values[4] = Current.items.size();
			values[5] = AverageItemsTenths();
		}
	}
	if (!inGame) {
		RefreshDrops(false);
		return;
	}
	if (!PanelIsOpen()) {
		if (now - PanelOpenTry < 2000) return;
		PanelOpenTry = now;
		if (OpenPanelNow() != D2RL::Panels::Result::Success) return;
	}
	if (PanelRoot == D2RL::Widgets::InvalidHandle) {
		if (const auto found = WidgetApi->findPanel(Context, PanelName, &PanelRoot); found != D2RL::Widgets::Result::Success) {
			PanelRoot = D2RL::Widgets::InvalidHandle;
			if (!FindLogged) Log(true, "game-UI panel: open, but its widgets were not found (result %u)", static_cast<unsigned>(found));
			FindLogged = true;
			return;
		}
	}
	if (RootShown != (want ? 1 : 0)) {
		if (WidgetApi->setWidgetVisible(Context, PanelRoot, want) == D2RL::Widgets::Result::Success) RootShown = want ? 1 : 0;
	}
	RefreshDrops(want);
	if (!want) return;
	if (BackShown != (Config.background ? 1 : 0) && SetVisible("Back", Config.background)) BackShown = Config.background ? 1 : 0;
	for (int f = 0; f < FieldCount; ++f) {
		const auto chars = FieldChars(Fields[f], values[f]);
		for (std::size_t slot = 0; slot < chars.size(); ++slot) {
			int choice = -1;
			if (chars[slot])
				if (const char* at = std::strchr(Fields[f].slots[slot].chars, chars[slot])) choice = static_cast<int>(at - Fields[f].slots[slot].chars);
			ShowSlot(f, static_cast<int>(slot), choice);
		}
	}
	if (!ShowLogged && !ShowFailLogged) Log(false, "game-UI panel: showing its contents (%zu widget handles)", WidgetHandles.size());
	ShowLogged = true;
}

void OpenGamePanel() {
	// Log the panel steps again for each game.
	PanelLogged = FindLogged = ShowLogged = ShowFailLogged = TickLogged = false;
	if (Config.display != "game" || !PanelApi || PanelHandle == D2RL::Panels::InvalidHandle) return;
	PanelOpenTry = GetTickCount64();
	if (!PanelIsOpen()) (void)OpenPanelNow();
	RefreshPanel(PanelOpenTry);
}

// Hides the panel on leaving a game without closing it, so the next game
// reuses the same widgets and handles if the game keeps the panel.
void CloseDrops();

void CloseGamePanel() {
	if (PanelApi) CloseDrops();
	if (!PanelApi || !WidgetApi || PanelHandle == D2RL::Panels::InvalidHandle || PanelRoot == D2RL::Widgets::InvalidHandle) return;
	if (!PanelIsOpen() || WidgetApi->setWidgetVisible(Context, PanelRoot, false) != D2RL::Widgets::Result::Success) PanelRoot = D2RL::Widgets::InvalidHandle;
	RootShown = 0;
}

#include "stats.inc"

// ---- UI tick: item scans and the game-UI panel, re-queued every UI update ----

// Queued from game events and overlay frames, then re-queued by itself while
// in game, the way d2rl-buff-hud keeps its panel updated. The flag keeps one
// tick in the queue at a time.
void __cdecl UiTick(const D2RL::PluginContext*, void*) noexcept;

void QueueTick() {
	if (!Threads || !Playing.load()) return;
	bool expected = false;
	if (!TickScheduled.compare_exchange_strong(expected, true)) return;
	if (Threads->runOnUiThread(Context, UiTick, nullptr) != D2RL::Threads::Result::Success) {
		TickScheduled.store(false);
		if (!TickFailLogged.exchange(true)) Log(true, "could not queue the UI tick");
	}
}

void __cdecl UiTick(const D2RL::PluginContext*, void*) noexcept {
	TickScheduled.store(false);
	if (!Playing.load()) return;
	if (!TickLogged) Log(false, "UI tick running");
	TickLogged = true;
	const ULONGLONG now = GetTickCount64();
	if (now - LastScan >= static_cast<ULONGLONG>(Config.pollMs)) {
		LastScan = now;
		Scan(false);
		if (Config.display == "game") RefreshPanel(now);
	}
	StatsProbe();
	QueueTick();
}

// ---- overlay ----

// runs-test: a large pattern in the middle of the screen, to tell an overlay
// that draws nothing on this setup from a panel that is only misplaced.
void DrawTest(const D2RL::PluginContext* context, const Overlay::Frame* frame) {
	const float cx = frame->screenWidth / 2, cy = frame->screenHeight / 2;
	const Overlay::FilledRectangleRequest box {
		.structSize = Overlay::FilledRectangleRequestSize,
		.canvas     = frame->canvas,
		.rect       = { cx - 300, cy - 100, cx + 300, cy + 100 },
		.color      = { 0.8f, 0.0f, 0.0f, 0.9f },
	};
	const char*                text = "RUN COUNTER TEST";
	const Overlay::TextRequest label {
		.structSize = Overlay::TextRequestSize,
		.canvas     = frame->canvas,
		.position   = { cx - 280, cy - 90 },
		.color      = { 1.0f, 1.0f, 1.0f, 1.0f },
		.textSize   = 48.0f,
		.text       = text,
		.textLength = static_cast<std::uint32_t>(std::strlen(text)),
	};
	const auto boxResult  = OverlayApi->drawFilledRectangle(context, &box);
	const auto textResult = OverlayApi->drawText(context, &label);
	if (!TestLogged.exchange(true))
		Log(false, "test pattern: screen %.0fx%.0f, frame %llu, box result %u, text result %u", frame->screenWidth, frame->screenHeight,
		    static_cast<unsigned long long>(frame->frameNumber), static_cast<unsigned>(boxResult), static_cast<unsigned>(textResult));
}

void __cdecl OnFrame(const D2RL::PluginContext* context, const Overlay::Frame* frame, void*) noexcept {
	if (!frame || frame->structSize < Overlay::FrameRequiredSize) return;
	const ULONGLONG now = GetTickCount64();
	if (now < TestUntil.load()) DrawTest(context, frame);
	QueueTick(); // restarts the tick if the UI queue ever dropped it
	std::vector<Line> lines;
	{
		std::lock_guard guard(Lock);
		if (!InGame || Config.display != "overlay" || !OverlayVisible.load()) return;
		lines = BuildLines(now);
	}
	++Frames;
	const float base   = frame->defaultTextSize >= 6.0f ? frame->defaultTextSize : 18.0f;
	const float size   = base * Config.textScale;
	const float pad    = size * 0.4f;
	const float left   = frame->screenWidth * Config.x;
	const float top    = frame->screenHeight * Config.y;
	float       width  = 0.0f;
	const float height = size * 1.2f;
	for (const auto& line : lines) width = std::max(width, size * 0.55f * static_cast<float>(line.text.size()));
	const Overlay::FilledRectangleRequest box {
		.structSize = Overlay::FilledRectangleRequestSize,
		.canvas     = frame->canvas,
		.rect       = { left, top, left + width + pad * 2, top + height * static_cast<float>(lines.size()) + pad * 2 },
		.color      = { 0.0f, 0.0f, 0.0f, 0.6f },
		.rounding   = pad * 0.5f,
	};
	(void)OverlayApi->drawFilledRectangle(context, &box);
	float y = top + pad;
	for (const auto& line : lines) {
		const Overlay::TextRequest request {
			.structSize = Overlay::TextRequestSize,
			.canvas     = frame->canvas,
			.position   = { left + pad, y },
			.color      = Config.colors ? line.color : Overlay::Color { 1.0f, 1.0f, 1.0f, 1.0f },
			.textSize   = size,
			.text       = line.text.data(),
			.textLength = static_cast<std::uint32_t>(line.text.size()),
		};
		(void)OverlayApi->drawText(context, &request);
		y += height;
	}
	if (!FrameLogged.exchange(true)) Log(false, "overlay: drawing at %.0f,%.0f", left, top);
}

// ---- controls and console ----

auto __cdecl OnToggleOverlay(const D2RL::PluginContext*, const D2RL::Input::ActionEvent* event, void*) noexcept -> D2RL::Input::ActionResult {
	if (!D2RL::Input::HasActionEventField(event, D2RL::Input::ActionEventRequiredSize) || event->kind != D2RL::Input::ActionEventKind::Pressed) return D2RL::Input::ActionResult::Ignored;
	OverlayVisible.store(!OverlayVisible.load());
	Log(false, "panel %s (control)", OverlayVisible.load() ? "shown" : "hidden");
	Say("Run Counter panel %s", OverlayVisible.load() ? "shown" : "hidden");
	return D2RL::Input::ActionResult::Handled;
}

auto __cdecl OnResetAction(const D2RL::PluginContext*, const D2RL::Input::ActionEvent* event, void*) noexcept -> D2RL::Input::ActionResult {
	if (!D2RL::Input::HasActionEventField(event, D2RL::Input::ActionEventRequiredSize) || event->kind != D2RL::Input::ActionEventKind::Pressed) return D2RL::Input::ActionResult::Ignored;
	ResetSession();
	Say("Run Counter: new session (the saved runs are kept; use the Reset tab of the stats panel to delete runs)");
	return D2RL::Input::ActionResult::Handled;
}

auto RunsCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
	if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
	std::lock_guard guard(Lock);
	const auto now = GetTickCount64();
	const auto sessionRuns = std::count_if(AllRuns.begin(), AllRuns.end(), [](const RunRecord& run) { return run.id && run.stamp >= SessionStamp; });
	Say("Run Counter %s: %u run(s) in all, %d this session, %zu item(s) in all, average farming %s", PluginVersion, LifetimeRuns(), static_cast<int>(sessionRuns), AllItems.size(),
	    Duration(AverageFarmMs()).c_str());
	if (InGame) Say("Current: %s %u, farming %s, town %s, %s, %zu item(s)", Current.number ? "run" : "not left town yet, next run", Current.number ? Current.number : NextRun,
	                Duration(CurrentFarm(now)).c_str(), Duration(CurrentTown(now)).c_str(), RunArea(Current).c_str(), Current.items.size());
	int shown = 0;
	for (auto run = AllRuns.rbegin(); run != AllRuns.rend() && shown < 10; ++run) {
		if (!run->id) continue;
		++shown;
		Say("  run %u: %s, farming %s, town %s, %s %s, %u item(s)", run->id, run->character.c_str(), Duration(run->farm * 1000ULL).c_str(), Duration(run->town * 1000ULL).c_str(),
		    DifficultyName(run->difficulty), run->area.c_str(), run->items);
	}
	if (!DataDirectory.empty()) Say("Files: %s%s, %s", DataDirectory.c_str(), RunsFile, ItemsFile);
	return D2RL::ConsoleCommandResult::Handled;
}

// runs-panel: open the game panel now, outside the tick, and report each step.
auto PanelCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
	if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
	if (!PanelApi || PanelHandle == D2RL::Panels::InvalidHandle) {
		Say("Run Counter: the game panel is not registered (panel = \"%s\" in the config, see the log)", Config.display.c_str());
		return D2RL::ConsoleCommandResult::Handled;
	}
	OverlayVisible.store(true);
	OpenGamePanel();
	QueueTick();
	Say("Run Counter: panel %s, open result %u, contents %s", PanelIsOpen() ? "open" : "closed", static_cast<unsigned>(PanelLastResult), ShowLogged ? "shown" : "not shown");
	return D2RL::ConsoleCommandResult::Handled;
}

auto DropsCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
	if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
	std::lock_guard guard(Lock);
	int listed = 0;
	for (const auto& item : AllItems) {
		if (Stamp(item.time) < SessionStamp) continue;
		++listed;
		Say("  run %u, %s: %s (%s, ilvl %u, %s)%s", item.run, item.area.c_str(), item.name.c_str(), QualityName(item.quality), item.itemLevel, SourceNames[item.source], item.ethereal ? " eth" : "");
	}
	if (!listed && Current.items.empty()) Say("Run Counter: nothing picked up this session");
	for (const auto& pending : Current.items)
		Say("  this game, %s: %s (%s, ilvl %u)", pending.record.area.c_str(), pending.record.name.c_str(), QualityName(pending.record.quality), pending.record.itemLevel);
	return D2RL::ConsoleCommandResult::Handled;
}

auto TestCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
	if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
	TestLogged.store(false);
	TestUntil.store(GetTickCount64() + 15000);
	Say("Run Counter: drawing a red test box in the middle of the screen for 15 seconds (%llu panel frames so far)", static_cast<unsigned long long>(Frames.load()));
	return D2RL::ConsoleCommandResult::Handled;
}

auto ResetCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
	if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
	ResetSession();
	Say("Run Counter: new session (the saved runs are kept)");
	return D2RL::ConsoleCommandResult::Handled;
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
		Log(false, "Run Counter %s: disabled in config", PluginVersion);
		return true;
	}

	LogDirectory = Utf8(context->pluginLogPath);
	if (const auto slash = LogDirectory.find_last_of("\\/"); slash != std::string::npos) LogDirectory.erase(slash + 1);
	else LogDirectory.clear();

	Lifecycle  = Query<D2RL::LifecycleService>("lifecycle");
	Inventory  = Query<D2RL::InventoryService>("inventory");
	ItemApi    = Query<D2RL::ItemService>("item");
	Threads    = Query<D2RL::ThreadService>("thread");
	OverlayApi = Query<D2RL::OverlayService>("overlay");
	Strings    = Query<D2RL::LocalizationService>("localization");
	if (Lifecycle && !D2RL::HasLifecycleServiceField(Lifecycle, D2RL::LifecycleServiceRequiredSize)) Lifecycle = nullptr;
	if (Inventory && !D2RL::HasInventoryServiceField(Inventory, D2RL::InventoryServiceRequiredSize)) Inventory = nullptr;
	if (ItemApi && !D2RL::HasItemServiceField(ItemApi, D2RL::ItemServiceRequiredSize)) ItemApi = nullptr;
	if (Threads && !D2RL::HasThreadServiceField(Threads, D2RL::ThreadServiceRequiredSize)) Threads = nullptr;
	if (OverlayApi && !D2RL::HasOverlayServiceField(OverlayApi, D2RL::OverlayServiceRequiredSize)) OverlayApi = nullptr;
	if (Strings && !D2RL::HasLocalizationServiceField(Strings, D2RL::LocalizationServiceRequiredSize)) Strings = nullptr;
	if (!Lifecycle) {
		Log(true, "inactive: D2RLoader has no lifecycle events");
		return true;
	}
	DataDirectory = FindDataDirectory();
	LoadSettings();
	if (LegacyConfig) {
		SaveSettings();
		Log(false, "settings: track_all replaced by the quality settings");
	}
	LoadRecords();

	using Kind = D2RL::Lifecycle::GameplayEventKind;
	for (const auto kind : { Kind::GameJoined, Kind::LocalPlayerReady, Kind::LevelChanged, Kind::PlayerLevelChanged, Kind::GameLeft }) {
		const D2RL::Lifecycle::GameplayEventListener listener {
			.structSize = D2RL::Lifecycle::GameplayEventListenerSize,
			.kind       = kind,
			.callback   = OnGameplayEvent,
		};
		D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
		if (Lifecycle->registerGameplayEventListener(context, &listener, &handle) != D2RL::Lifecycle::Result::Success) {
			Log(true, "inactive: could not listen to game events");
			return true;
		}
	}
	const D2RL::Lifecycle::MonsterDeathListener deathListener {
		.structSize = D2RL::Lifecycle::MonsterDeathListenerSize,
		.callback   = OnMonsterDeath,
	};
	D2RL::Lifecycle::ListenerHandle deathHandle = D2RL::Lifecycle::InvalidHandle;
	const bool bossKills = Lifecycle->registerMonsterDeathListener(context, &deathListener, &deathHandle) == D2RL::Lifecycle::Result::Success;

	bool overlay = false;
	if (OverlayApi) {
		const Overlay::CallbackRegistration registration {
			.structSize = Overlay::CallbackRegistrationSize,
			.phase      = Overlay::Phase::AfterGameUi,
			.callback   = OnFrame,
		};
		Overlay::CallbackHandle handle {};
		overlay = OverlayApi->registerFrameCallback(context, &registration, &handle) == Overlay::Result::Success;
	}

	if (const auto* input = Query<D2RL::InputService>("input"); input && D2RL::HasInputServiceField(input, D2RL::InputServiceRequiredSize)) {
		const D2RL::Input::ActionRegistration actions[] {
			{ .structSize = D2RL::Input::ActionRegistrationSize, .logicalId = "toggle-overlay", .displayName = "Show/Hide Run Counter", .category = "Run Counter", .callback = OnToggleOverlay },
			{ .structSize = D2RL::Input::ActionRegistrationSize, .logicalId = "reset-session", .displayName = "New Run Counter Session", .category = "Run Counter", .callback = OnResetAction },
			{ .structSize = D2RL::Input::ActionRegistrationSize, .logicalId = "show-stats", .displayName = "Show Run Stats", .category = "Run Counter", .callback = OnStatsAction },
		};
		for (const auto& action : actions) {
			D2RL::Input::ActionHandle handle = D2RL::Input::InvalidHandle;
			if (input->registerAction(context, &action, &handle) != D2RL::Input::Result::Success) Log(true, "could not add the control \"%s\"", action.displayName);
		}
	}

	PanelApi    = Query<D2RL::PanelService>("panel");
	WidgetApi   = Query<D2RL::WidgetService>("widget");
	ResourceApi = Query<D2RL::ResourceService>("resource");
	EventApi    = Query<D2RL::SharedEventService>("shared event");
	if (PanelApi && !D2RL::HasPanelServiceField(PanelApi, D2RL::PanelServiceRequiredSize)) PanelApi = nullptr;
	if (WidgetApi && !D2RL::HasWidgetServiceField(WidgetApi, D2RL::WidgetServiceRequiredSize)) WidgetApi = nullptr;
	if (ResourceApi && !D2RL::HasResourceServiceField(ResourceApi, D2RL::ResourceServiceRequiredSize)) ResourceApi = nullptr;
	if (EventApi && !D2RL::HasSharedEventServiceField(EventApi, D2RL::SharedEventServiceRequiredSize)) EventApi = nullptr;

	if (Config.display == "game") {
		PanelLayout = BuildPanelLayout();
		const D2RL::Resources::ResourceRegistration resource {
			.structSize = D2RL::Resources::ResourceRegistrationSize,
			.path       = "data/global/ui/layouts/run-counter/RunPanelhd.json",
			.bytes      = PanelLayout.data(),
			.byteCount  = PanelLayout.size(),
		};
		D2RL::Resources::RegistrationHandle resourceHandle = D2RL::Resources::InvalidHandle;
		const D2RL::Panels::PanelRegistration registration {
			.structSize = D2RL::Panels::PanelRegistrationSize,
			.flags      = D2RL::Panels::PanelFlags::None,
			.localId    = "RunPanel",
		};
		if (!ResourceApi || !PanelApi || !WidgetApi)
			Log(true, "game-UI panel: missing services (resource %s, panel %s, widget %s), no panel", ResourceApi ? "ok" : "missing", PanelApi ? "ok" : "missing", WidgetApi ? "ok" : "missing");
		else if (ResourceApi->registerResource(context, &resource, &resourceHandle) != D2RL::Resources::Result::Success)
			Log(true, "game-UI panel: could not register its layout");
		else if (PanelApi->registerPanel(context, &registration, &PanelHandle) != D2RL::Panels::Result::Success) {
			PanelHandle = D2RL::Panels::InvalidHandle;
			Log(true, "game-UI panel: could not register it");
		} else {
			Log(false, "game-UI panel: registered (%zu KB of layout)", PanelLayout.size() / 1024);
			RegisterDropsPanels(context);
		}
	}

	// The stats panel works in every display mode: it only needs the game UI.
	RegisterStatsPanels(context);

	(void)context->RegisterConsoleCommand("runs", RunsCommand, "Show Run Counter totals and the last runs.");
	(void)context->RegisterConsoleCommand("runs-drops", DropsCommand, "List the items picked up this session.");
	(void)context->RegisterConsoleCommand("runs-reset", ResetCommand, "Start a new Run Counter session (saved runs are kept).");
	(void)context->RegisterConsoleCommand("runs-stats", StatsCommand, "Open or close the Run Counter stats panel.");
	(void)context->RegisterConsoleCommand("runs-panel", PanelCommand, "Open the Run Counter panel now and report the result.");
	(void)context->RegisterConsoleCommand("runs-test", TestCommand, "Draw a test box in the middle of the screen for 15 seconds.");

	Log(false, "Run Counter %s: %s; runes tracked from %s, announced from %s%s", PluginVersion, Config.active ? "tracking" : "tracking paused", Config.minRune ? RuneNames[Config.minRune - 1] : "none",
	    Config.announceRune ? RuneNames[Config.announceRune - 1] : "none", Config.consumables ? "; consumables tracked" : "");
	Log(false, "item scans: %s; panel: %s; overlay service: %s; boss kills: %s; files in %s", Threads && Inventory ? "on" : "off (missing services)", Config.display.c_str(), overlay ? "on" : "off",
	    bossKills ? "on" : "off", DataDirectory.empty() ? "(none)" : DataDirectory.c_str());
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	std::lock_guard guard(Lock);
	FinishRun(false);
}
