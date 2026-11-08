// Terror Zone Selector: choose which terror zones are active. All the chosen
// zones can be terrorized together, or the game's 30 minute rotation can be
// kept but limited to the chosen zones. Changes apply at once in game.
//
// Game code used (Steam 3.3.94995 RVAs):
//   - CurrentZone 0x35B380 (bool* changed) -> Zone*: DesecrateGetCurrentScheduledZone,
//     the zone of the current 30 minute slot (from the time and the config's seed).
//     Called when a game is created, when a player joins, and when the zone changes
//     (the broadcast to every game, D2R+0x405B30); the zone is then applied to
//     the game (levels terrorized, packet to the clients).
//   - ConfigAt 0x35DB10 (time_t, bool* changed) -> Config*: the desecrated zones
//     config in effect at that time. Config +0x00 start time (time_t), +0x10 zone
//     minutes, +0x14 break minutes, +0x250 zones (Zone, 0x3E8 bytes each), +0x258 count.
//   - Zone: +0x00..+0x84 id and difficulty data (plain bytes, copied as is by the
//     game), +0x88 levels (0x38 bytes each, +0x00 level id), +0x90 count, +0x98
//     capacity (top bit: not on the heap), +0xA0 room for 15 levels.
//   - Zone change timer in the main loop (D2R+0x8C044): armed byte 0x2A236D8 and
//     next change time 0x2A236D0 (time_t). Past that time the game broadcasts the
//     current zone to every game and sets the next time. Setting the time to 0
//     makes it broadcast at its next tick: that is how a change applies live.
// The hook returns a zone made of the chosen zones (a copy of the first chosen
// zone with all their levels), or one of them in rotation. Nothing chosen, or
// turned off: the game's own zone. It runs where the game's server runs:
// single player and games you host.

#include <D2RLPlugin/api.h>
#include <windows.h>

#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <string_view>
#include <vector>

#include "i_hate_tomls_client.h"
#include "zones.h"

namespace {

constexpr const char* PluginId      = "terror-zone-selector";
constexpr const char* PluginVersion = "0.0.1";

const D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = PluginId,
	.name        = "Terror Zone Selector",
	.version     = PluginVersion,
	.author      = "wild-healer",
	.description = "Choose which terror zones are active, changed live in game.",
	.flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

enum Mode : int { Together = 0, Rotate = 1 };

struct Settings {
	std::atomic<bool> enabled { true };
	std::atomic<int>  mode { Together };
	std::atomic<bool> chosen[ZoneCount] {};
} Config;

// ---- native layout (Steam 3.3.94995) ----

using Code = std::array<std::uint8_t, 16>;

constexpr std::uint64_t CurrentZoneRva = 0x35B380;
constexpr Code CurrentZoneBytes { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48 };
constexpr std::uint64_t ConfigAtRva = 0x35DB10;
constexpr Code ConfigAtBytes { 0x40, 0x53, 0x57, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x89, 0x6C, 0x24, 0x50, 0x48, 0x8B };
// cmp byte ptr [armed], 0 / je / cmp rbx, [next time]: the timer's globals are read from it.
constexpr std::uint64_t TimerCodeRva = 0x8C044;
constexpr Code TimerCodeBytes { 0x80, 0x3D, 0x8D, 0x76, 0x99, 0x02, 0x00, 0x74, 0x4B, 0x48, 0x3B, 0x1D, 0x7C, 0x76, 0x99, 0x02 };

constexpr std::uintptr_t ConfigStart     = 0x00;  // time_t
constexpr std::uintptr_t ConfigZoneMin   = 0x10;  // i32
constexpr std::uintptr_t ConfigBreakMin  = 0x14;  // i32
constexpr std::uintptr_t ConfigZones     = 0x250; // Zone*
constexpr std::uintptr_t ConfigZoneCount = 0x258; // u64
constexpr std::size_t    ZoneSize        = 0x3E8;
constexpr std::uintptr_t ZoneLevels      = 0x88;  // Level*
constexpr std::uintptr_t ZoneLevelCount  = 0x90;  // u64
constexpr std::uintptr_t ZoneLevelCap    = 0x98;  // u64, top bit: not on the heap
constexpr std::size_t    LevelSize       = 0x38;
constexpr std::uint64_t  NotOnHeap       = 0x8000000000000000ull;
constexpr std::uint64_t  MaxZones        = 256;
constexpr std::uint64_t  MaxLevels       = 64;

using CurrentZoneFn = void* (*)(bool* changed) noexcept;
using ConfigAtFn    = void* (*)(std::int64_t time, bool* changed) noexcept;

const D2RL::PluginContext* Context {};
std::uintptr_t             Base {};
CurrentZoneFn              CurrentZone {}; // original, after the hook
ConfigAtFn                 ConfigAt {};
std::uint8_t*              TimerArmed {};
std::int64_t*              TimerNext {};

void Log(bool warn, const char* format, ...) noexcept {
	if (!Context) return;
	char    line[1024] {};
	va_list args;
	va_start(args, format);
	std::vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	warn ? Context->LogWarn(line) : Context->LogInfo(line);
}

bool Read(std::uintptr_t address, void* output, std::size_t size) noexcept {
	if (address == 0) return false;
	MEMORY_BASIC_INFORMATION info {};
	if (VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)) != sizeof(info) || info.State != MEM_COMMIT) return false;
	constexpr DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
	if (!(info.Protect & readable) || (info.Protect & PAGE_GUARD)) return false;
	const std::uintptr_t end = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
	if (address + size > end) return false;
	std::memcpy(output, reinterpret_cast<const void*>(address), size);
	return true;
}

template <typename T>
bool ReadField(const void* base, std::uintptr_t offset, T& value) noexcept {
	return base && Read(reinterpret_cast<std::uintptr_t>(base) + offset, &value, sizeof(T));
}

bool CodeMatches(std::uint64_t rva, const Code& bytes) noexcept {
	Code code {};
	return Read(Base + rva, code.data(), code.size()) && std::memcmp(code.data(), bytes.data(), code.size()) == 0;
}

// ---- the chosen zone ----

struct Zone {
	const void* zone;
	int         index; // in Zones, -1 when not one of ours
	std::uint64_t levels;
	const void*   levelData;
};

// Built zones are never freed: a game may still read one after a change.
// Changes are rare, so this stays small.
struct Built {
	const void*   config = nullptr;
	const void*   zones  = nullptr;
	std::uint64_t mask   = ~0ull;
	void*         zone   = nullptr;
};

SRWLOCK       Lock = SRWLOCK_INIT;
Built         Cache;
std::uint64_t LastLogged = ~0ull; // what was last logged (mask, or 1<<63 | rotated index)

std::uint64_t ChosenMask() noexcept {
	std::uint64_t mask = 0;
	for (int index = 0; index < ZoneCount; ++index)
		if (Config.chosen[index].load(std::memory_order_relaxed)) mask |= 1ull << index;
	return mask;
}

int ZoneIndexOf(int firstLevel) noexcept {
	for (int index = 0; index < ZoneCount; ++index)
		if (Zones[index].firstLevel == firstLevel) return index;
	return -1;
}

// The config's zones that are chosen, in the config's order.
bool ChosenZones(const void* config, std::uint64_t mask, std::vector<Zone>& out) noexcept {
	const std::uint8_t* zones = nullptr;
	std::uint64_t       count = 0;
	if (!ReadField(config, ConfigZones, zones) || !ReadField(config, ConfigZoneCount, count) || !zones || count == 0 || count > MaxZones) return false;
	for (std::uint64_t slot = 0; slot < count; ++slot) {
		const std::uint8_t* zone      = zones + slot * ZoneSize;
		const void*         levelData = nullptr;
		std::uint64_t       levels    = 0;
		std::int32_t        first     = -1;
		if (!ReadField(zone, ZoneLevels, levelData) || !ReadField(zone, ZoneLevelCount, levels) || !levelData || levels == 0 || levels > MaxLevels) continue;
		if (!Read(reinterpret_cast<std::uintptr_t>(levelData), &first, sizeof(first))) continue;
		const int index = ZoneIndexOf(first);
		if (index < 0 || !(mask & (1ull << index))) continue;
		out.push_back({ zone, index, levels, levelData });
	}
	return !out.empty();
}

std::string Names(const std::vector<Zone>& zones) {
	std::string text;
	for (const Zone& zone : zones) {
		if (!text.empty()) text += ", ";
		text += Zones[zone.index].label;
	}
	return text;
}

// A zone holding the levels of every chosen zone: the first one's data, all their levels.
void* Merge(const std::vector<Zone>& zones) noexcept {
	std::uint64_t total = 0;
	for (const Zone& zone : zones) total += zone.levels;
	auto* zone   = static_cast<std::uint8_t*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, ZoneSize));
	auto* levels = static_cast<std::uint8_t*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, total * LevelSize));
	if (!zone || !levels) return nullptr;
	if (!Read(reinterpret_cast<std::uintptr_t>(zones.front().zone), zone, ZoneSize)) return nullptr;
	std::uint64_t at = 0;
	for (const Zone& part : zones) {
		if (!Read(reinterpret_cast<std::uintptr_t>(part.levelData), levels + at * LevelSize, part.levels * LevelSize)) return nullptr;
		at += part.levels;
	}
	void* const         pointer  = levels;
	const std::uint64_t capacity = total | NotOnHeap;
	std::memcpy(zone + ZoneLevels, &pointer, sizeof(pointer));
	std::memcpy(zone + ZoneLevelCount, &total, sizeof(total));
	std::memcpy(zone + ZoneLevelCap, &capacity, sizeof(capacity));
	return zone;
}

// The slot number the game uses for its 30 minute rotation.
std::int64_t SlotOf(const void* config, std::int64_t now) noexcept {
	std::int64_t start = 0;
	std::int32_t zoneMinutes = 0, breakMinutes = 0;
	if (!ReadField(config, ConfigStart, start) || !ReadField(config, ConfigZoneMin, zoneMinutes) || !ReadField(config, ConfigBreakMin, breakMinutes)) return now / 1800;
	const std::int64_t period = (static_cast<std::int64_t>(zoneMinutes) + breakMinutes) * 60;
	if (period <= 0 || now < start) return now / 1800;
	return (now - start) / period;
}

void* HookCurrentZone(bool* changed) noexcept {
	void* const real = CurrentZone(changed);
	if (!Config.enabled.load(std::memory_order_relaxed)) return real;
	const std::uint64_t mask = ChosenMask();
	if (!mask) return real;
	const std::int64_t now     = static_cast<std::int64_t>(std::time(nullptr));
	bool               ignored = false;
	const void* const  config  = ConfigAt(now, &ignored);
	if (!config) return real;
	std::vector<Zone> zones;
	zones.reserve(ZoneCount);
	if (!ChosenZones(config, mask, zones)) return real;

	AcquireSRWLockExclusive(&Lock);
	void* result = nullptr;
	if (Config.mode.load(std::memory_order_relaxed) == Rotate || zones.size() == 1) {
		const std::int64_t slot = SlotOf(config, now);
		const Zone&        pick = zones[static_cast<std::size_t>(slot % static_cast<std::int64_t>(zones.size()))];
		result                  = const_cast<void*>(pick.zone);
		const std::uint64_t logged = NotOnHeap | static_cast<std::uint64_t>(pick.index);
		if (logged != LastLogged) {
			LastLogged = logged;
			Log(false, "terror zone: %s (%s)", Zones[pick.index].label, Zones[pick.index].levels);
		}
	} else {
		const void* zonesPointer = nullptr;
		(void)ReadField(config, ConfigZones, zonesPointer);
		if (Cache.zone && Cache.config == config && Cache.zones == zonesPointer && Cache.mask == mask) result = Cache.zone;
		else if (void* const merged = Merge(zones)) {
			Cache  = { config, zonesPointer, mask, merged };
			result = merged;
		}
		if (result && mask != LastLogged) {
			LastLogged = mask;
			Log(false, "terror zones (%zu together): %s", zones.size(), Names(zones).c_str());
		}
	}
	ReleaseSRWLockExclusive(&Lock);
	return result ? result : real;
}

// Makes the game broadcast the current zone to every game at its next tick.
void ApplyNow() noexcept {
	if (!TimerArmed || !TimerNext) return;
	*reinterpret_cast<volatile std::int64_t*>(TimerNext)  = 0;
	*reinterpret_cast<volatile std::uint8_t*>(TimerArmed) = 1;
}

// ---- version check ----

bool Locate() noexcept {
	if (!CodeMatches(CurrentZoneRva, CurrentZoneBytes)) return Log(true, "game code differs at D2R+0x%llX", static_cast<unsigned long long>(CurrentZoneRva)), false;
	if (!CodeMatches(ConfigAtRva, ConfigAtBytes)) return Log(true, "game code differs at D2R+0x%llX", static_cast<unsigned long long>(ConfigAtRva)), false;
	ConfigAt = reinterpret_cast<ConfigAtFn>(Base + ConfigAtRva);
	// Bytes 2..5 and 12..15 are displacements: check the opcodes, then follow them.
	Code code {};
	if (Read(Base + TimerCodeRva, code.data(), code.size()) && code[0] == 0x80 && code[1] == 0x3D && code[6] == 0x00 && code[7] == 0x74 && code[9] == 0x48 &&
	    code[10] == 0x3B && code[11] == 0x1D) {
		std::int32_t armed = 0, next = 0;
		std::memcpy(&armed, code.data() + 2, 4);
		std::memcpy(&next, code.data() + 12, 4);
		TimerArmed = reinterpret_cast<std::uint8_t*>(Base + TimerCodeRva + 7 + armed);
		TimerNext  = reinterpret_cast<std::int64_t*>(Base + TimerCodeRva + 9 + 7 + next);
	} else
		Log(true, "zone timer not found at D2R+0x%llX: changes apply at the next game or zone change", static_cast<unsigned long long>(TimerCodeRva));
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

int ZoneKey(const std::string& key) {
	for (int index = 0; index < ZoneCount; ++index)
		if (key == Zones[index].key) return index;
	return -1;
}

void SetAct(int act, bool on) {
	for (int index = 0; index < ZoneCount; ++index)
		if (act == 0 || Zones[index].act == act) Config.chosen[index] = on;
}

bool Apply(const std::string& key, const std::string& value, std::string& error) {
	const auto flag = [&](std::atomic<bool>& target) {
		if (value != "true" && value != "false") return error = "true or false", false;
		target = value == "true";
		return true;
	};
	if (key == "enabled") return flag(Config.enabled);
	if (key == "mode") {
		if (value == "together") Config.mode = Together;
		else if (value == "rotate") Config.mode = Rotate;
		else return error = "together or rotate", false;
		return true;
	}
	if (key == "choose_none") return SetAct(0, false), true;
	if (key == "choose_all") return SetAct(0, true), true;
	const auto act = [&](std::size_t prefix, bool on) {
		const char digit = key.size() == prefix + 1 ? key[prefix] : '0';
		if (digit < '1' || digit > '5') return error = "unknown setting", false;
		SetAct(digit - '0', on);
		return true;
	};
	if (key.compare(0, 7, "act_all") == 0) return act(7, true);
	if (key.compare(0, 8, "act_none") == 0) return act(8, false);
	if (const int index = ZoneKey(key); index >= 0) return flag(Config.chosen[index]);
	return error = "unknown setting", false;
}

std::string ValueOf(const std::string& key) {
	const auto flag = [](bool on) { return std::string(on ? "true" : "false"); };
	if (key == "enabled") return flag(Config.enabled);
	if (key == "mode") return Config.mode == Rotate ? "\"rotate\"" : "\"together\"";
	if (const int index = ZoneKey(key); index >= 0) return flag(Config.chosen[index]);
	return {};
}

std::string Render() {
	std::string toml = R"(# Terror Zone Selector
#
# Choose which terror zones are active. Nothing chosen: the game's own terror
# zone. Works in single player and in games you host. Every setting can also be
# changed in game with I Hate TOMLs, and applies at once.

[terror-zone-selector]
enabled = )" + ValueOf("enabled") + R"(

# "together": every chosen zone is a terror zone at the same time.
# "rotate": one chosen zone at a time, changing every 30 minutes like the game's own.
mode = )" + ValueOf("mode") + "\n";
	int act = 0;
	for (int index = 0; index < ZoneCount; ++index) {
		if (Zones[index].act != act) {
			act = Zones[index].act;
			toml += "\n# Act " + std::to_string(act) + "\n";
		}
		toml += std::string("# ") + Zones[index].levels + "\n" + Zones[index].key + " = " + ValueOf(Zones[index].key) + "\n";
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
		if (key.compare(0, 6, "choose") == 0 || key.compare(0, 3, "act") == 0) continue; // actions, menu only
		std::string error;
		if (!Apply(key, value, error)) Log(true, "config: %s = %s ignored (%s)", key.c_str(), value.c_str(), error.c_str());
	}
}

void LoadConfig() {
	(void)Context->EnsureConfig();
	std::uint32_t     required = 0;
	std::vector<char> buffer(16384);
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

std::string Escape(const char* text) {
	std::string out;
	for (; *text; ++text) {
		if (*text == '"' || *text == '\\') out += '\\';
		out += *text;
	}
	return out;
}

std::string BuildSchema() {
	std::string schema = R"toml(title = "Terror Zone Selector"
[[setting]]
key = "enabled"
type = "bool"
label = "Choose terror zones"
note = "Off, or nothing chosen: the game's own terror zone."
[[setting]]
key = "mode"
type = "enum"
label = "Mode"
values = ["together", "rotate"]
labels = ["All together", "Rotate"]
note = "All together: every chosen zone at once. Rotate: one chosen zone at a time, every 30 minutes."
[[setting]]
key = "choose_none"
type = "action"
label = "Clear all"
)toml";
	int act = 0;
	for (int index = 0; index < ZoneCount; ++index) {
		if (Zones[index].act != act) {
			act = Zones[index].act;
			const std::string number = std::to_string(act);
			schema += "[[setting]]\ntype = \"header\"\nlabel = \"Act " + number + "\"\n";
			schema += "[[setting]]\nkey = \"act_all" + number + "\"\ntype = \"action\"\nlabel = \"All of act " + number + "\"\n";
			schema += "[[setting]]\nkey = \"act_none" + number + "\"\ntype = \"action\"\nlabel = \"None of act " + number + "\"\n";
		}
		schema += std::string("[[setting]]\nkey = \"") + Zones[index].key + "\"\ntype = \"bool\"\nlabel = \"" + Escape(Zones[index].label) + "\"\nnote = \"" +
		          Escape(Zones[index].levels) + "\"\n";
	}
	return schema;
}

std::string            Schema;
PluginMenu::Connection Menu;

bool __cdecl MenuGet(const char* key, char* out, std::uint32_t size, void*) noexcept {
	if (!key) return false;
	const std::string value = ValueOf(key);
	if (value.empty()) return false;
	std::snprintf(out, size, "%s", value.c_str());
	return true;
}

bool __cdecl MenuSet(const char* key, const char* value, char* error, std::uint32_t size, void*) noexcept {
	if (!key || !value) return false;
	std::string reason;
	if (!Apply(key, Unquote(Trim(value)), reason)) {
		std::snprintf(error, size, "%s", reason.c_str());
		return false;
	}
	SaveConfig();
	ApplyNow();
	return true;
}

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (!D2RL::HasContext(context) || !context->GetApi()) return false;
	Context = context;
	LoadConfig();
	Schema = BuildSchema();
	(void)Menu.Start(context, Schema.c_str(), MenuGet, MenuSet, nullptr);
	Base = context->exeBase;
	if (!Base || !Locate()) {
		Log(true, "Terror Zone Selector %s: inactive, this game build isn't supported (Steam 3.3.94995 is)", PluginVersion);
		return true;
	}
	if (!context->InstallInlineHook(CurrentZoneRva, CurrentZoneBytes.data(), static_cast<std::uint32_t>(CurrentZoneBytes.size()), &HookCurrentZone, &CurrentZone) ||
	    !CurrentZone) {
		Log(true, "Terror Zone Selector %s: inactive, could not hook D2R+0x%llX (another plugin may hook it)", PluginVersion,
		    static_cast<unsigned long long>(CurrentZoneRva));
		return true;
	}
	int chosen = 0;
	for (int index = 0; index < ZoneCount; ++index) chosen += Config.chosen[index] ? 1 : 0;
	Log(false, "Terror Zone Selector %s: %s, %d zone%s chosen, mode %s", PluginVersion, Config.enabled ? "active" : "loaded but turned off", chosen,
	    chosen == 1 ? "" : "s", Config.mode == Rotate ? "rotate" : "together");
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	Menu.Stop();
}
