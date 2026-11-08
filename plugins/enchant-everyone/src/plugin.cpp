// Enchant Everyone: when Enchant is cast, every friendly unit around the caster
// gets it too: the caster, party members, mercenaries, summons and revives.
//
// Game code used (Steam 3.3.94995 RVAs), all on the server thread:
//   - EnchantDo 0x553650 (Game*, Unit* caster, int skill, int level) -> int:
//     srvdofunc 25 (table 0x238EA00 + 25*8), Enchant's server step. It reads
//     the target from the caster's path, keeps it if it's an ally (else the
//     caster), and gives it the Enchant state with the skill's stats.
//   - SkillTarget 0x48FE20 (Game*, Unit*) -> Unit*: the path's target unit,
//     after checking it still exists (path +0x70 unit, +0x78 type, +0x7C id).
//   - IsAlly 0x48E4D0 (Game*, Unit*, Unit*) -> int: owners of pets resolved,
//     players allied when in the same party, monsters by alignment.
//   - UnitRoom 0x34B440 (Unit*) -> Room*; room +0x00 rooms rooms (itself
//     included), +0x40 their count, +0xA8 first unit; unit +0x160 next unit.
// The hook calls the game's own Enchant once more per friendly unit in range,
// with the caster's path target pointed at that unit for the call and put
// back right after. Mana is spent once, by the game, for the cast itself.
// It runs where the game's server runs: single player and games you host.

#include <D2RLPlugin/api.h>
#include <windows.h>

#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "i_hate_tomls_client.h"

namespace {

constexpr const char* PluginId      = "enchant-everyone";
constexpr const char* PluginVersion = "0.0.1";

const D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = PluginId,
	.name        = "Enchant Everyone",
	.version     = PluginVersion,
	.author      = "wild-healer",
	.description = "Casting Enchant enchants every friendly unit around you.",
	.flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

struct Settings {
	std::atomic<bool> enabled { true };
	std::atomic<int>  yards { 20 };
	std::atomic<bool> players { true };
	std::atomic<bool> minions { true };
} Config;

// ---- native layout (Steam 3.3.94995) ----

using Code = std::array<std::uint8_t, 16>;

constexpr std::uint64_t EnchantDoRva = 0x553650;
constexpr Code EnchantDoBytes { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x44, 0x89, 0x4C, 0x24, 0x20, 0x44, 0x89, 0x44, 0x24, 0x18, 0x55 };
constexpr std::uint64_t SkillTargetRva = 0x48FE20; // bytes 9..12 are a call displacement, checked as they are
constexpr Code SkillTargetBytes { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xDA, 0xE8, 0x22, 0x1D, 0x00, 0x00, 0x48, 0x8B };
constexpr std::uint64_t IsAllyRva = 0x48E4D0;
constexpr Code IsAllyBytes { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xCA, 0x49, 0x8B, 0xD8 };
constexpr std::uint64_t UnitRoomRva = 0x34B440;
constexpr Code UnitRoomBytes { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C };
// Room accessors the game uses for its own area searches (0x2EFDE0, 0x2EFD90).
constexpr std::uint64_t NearRoomsRva = 0x2EFDE0;
constexpr Code NearRoomsBytes { 0x8B, 0x41, 0x40, 0x41, 0x89, 0x00, 0x48, 0x8B, 0x01, 0x48, 0x89, 0x02, 0xC3, 0xCC, 0xCC, 0xCC };
constexpr std::uint64_t FirstUnitRva = 0x2EFD90;
constexpr Code FirstUnitBytes { 0x48, 0x8B, 0x81, 0xA8, 0x00, 0x00, 0x00, 0xC3, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC };

constexpr int            EnchantSkill      = 52;
constexpr std::uintptr_t UnitTypeOffset    = 0x00;  // u32: 0 player, 1 monster
constexpr std::uintptr_t UnitModeOffset    = 0x0C;  // u32
constexpr std::uintptr_t UnitPathOffset    = 0x38;  // Path*: +2 x, +6 y (u16)
constexpr std::uintptr_t UnitNextInRoom    = 0x160; // Unit*
constexpr std::uintptr_t RoomNearList      = 0x00;  // Room**
constexpr std::uintptr_t RoomNearCount     = 0x40;  // i32
constexpr std::uintptr_t RoomFirstUnit     = 0xA8;  // Unit*
constexpr std::uintptr_t PathTargetUnit    = 0x70;  // Unit*
constexpr std::uintptr_t PathTargetType    = 0x78;  // u32
constexpr std::uintptr_t PathTargetId      = 0x7C;  // u32
constexpr std::uintptr_t UnitIdOffset      = 0x08;  // u32
constexpr std::uint32_t  PlayerDeath = 0, PlayerDead = 17, MonsterDeath = 0, MonsterDead = 12;
constexpr std::size_t    MaxTargets  = 128;
constexpr int            MaxNearRooms = 64;
constexpr std::size_t    MaxUnitsPerRoom = 4096;

using EnchantDoFn   = int (*)(void* game, void* caster, int skill, int level) noexcept;
using SkillTargetFn = void* (*)(void* game, void* unit) noexcept;
using IsAllyFn      = int (*)(void* game, void* a, void* b) noexcept;
using UnitRoomFn    = void* (*)(void* unit) noexcept;

const D2RL::PluginContext* Context {};
std::uintptr_t             Base {};

EnchantDoFn   EnchantDo {}; // original, after the hook
SkillTargetFn SkillTarget {};
IsAllyFn      IsAlly {};
UnitRoomFn    UnitRoom {};

void Log(bool warn, const char* format, ...) noexcept {
	if (!Context) return;
	char    line[512] {};
	va_list args;
	va_start(args, format);
	std::vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	warn ? Context->LogWarn(line) : Context->LogInfo(line);
}

// Reads the game's memory after checking it is committed and readable.
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

template <typename T>
void WriteField(void* base, std::uintptr_t offset, const T& value) noexcept {
	std::memcpy(static_cast<std::uint8_t*>(base) + offset, &value, sizeof(T));
}

bool CodeMatches(std::uint64_t rva, const Code& bytes) noexcept {
	Code code {};
	return Read(Base + rva, code.data(), code.size()) && std::memcmp(code.data(), bytes.data(), code.size()) == 0;
}

bool Position(void* unit, int& x, int& y) noexcept {
	void*         path = nullptr;
	std::uint16_t px = 0, py = 0;
	if (!ReadField(unit, UnitPathOffset, path) || !path || !ReadField(path, 2, px) || !ReadField(path, 6, py)) return false;
	x = px, y = py;
	return true;
}

bool Alive(void* unit, std::uint32_t type) noexcept {
	std::uint32_t mode = 0;
	if (!ReadField(unit, UnitModeOffset, mode)) return false;
	return type == 0 ? mode != PlayerDeath && mode != PlayerDead : mode != MonsterDeath && mode != MonsterDead;
}

// Friendly, living players and monsters within the radius of the caster,
// the one the cast already enchanted left out.
void FindTargets(void* game, void* caster, void* done, std::vector<void*>& out) noexcept {
	int cx = 0, cy = 0;
	if (!Position(caster, cx, cy)) return;
	void* const room = UnitRoom(caster);
	void**      rooms = nullptr;
	int         count = 0;
	if (!room || !ReadField(room, RoomNearList, rooms) || !ReadField(room, RoomNearCount, count) || !rooms || count <= 0) return;
	count = count > MaxNearRooms ? MaxNearRooms : count;
	// 1 yard = 3/2 of a position unit (sub-tile).
	const int radius  = Config.yards.load(std::memory_order_relaxed) * 3 / 2;
	const int radius2 = radius * radius;
	const bool players = Config.players.load(std::memory_order_relaxed);
	const bool minions = Config.minions.load(std::memory_order_relaxed);
	for (int index = 0; index < count && out.size() < MaxTargets; ++index) {
		void* other = nullptr;
		if (!Read(reinterpret_cast<std::uintptr_t>(rooms + index), &other, sizeof(other)) || !other) continue;
		void*       unit = nullptr;
		std::size_t seen = 0;
		for (ReadField(other, RoomFirstUnit, unit); unit && seen < MaxUnitsPerRoom && out.size() < MaxTargets; ++seen) {
			void* const   current = unit;
			std::uint32_t type    = 0xFFFFFFFF;
			if (!ReadField(current, UnitNextInRoom, unit)) unit = nullptr;
			if (current == done || !ReadField(current, UnitTypeOffset, type) || type > 1) continue;
			if ((type == 0 && current != caster && !players) || (type == 1 && !minions)) continue;
			int x = 0, y = 0;
			if (!Position(current, x, y) || (x - cx) * (x - cx) + (y - cy) * (y - cy) > radius2) continue;
			if (!Alive(current, type) || !IsAlly(game, caster, current)) continue;
			bool duplicate = false;
			for (void* known : out) duplicate |= known == current;
			if (!duplicate) out.push_back(current);
		}
	}
}

thread_local bool Inside = false;

int HookEnchantDo(void* game, void* caster, int skill, int level) noexcept {
	if (Inside || skill != EnchantSkill || !caster || !game || !Config.enabled.load(std::memory_order_relaxed)) return EnchantDo(game, caster, skill, level);
	// Who the game enchants itself: its target when that's an ally, else the caster.
	void* done = SkillTarget(game, caster);
	if (!done || !IsAlly(game, caster, done)) done = caster;
	const int result = EnchantDo(game, caster, skill, level);
	if (!result) return result;
	void* path = nullptr;
	if (!ReadField(caster, UnitPathOffset, path) || !path) return result;
	std::vector<void*> targets;
	targets.reserve(16);
	FindTargets(game, caster, done, targets);
	if (targets.empty()) return result;
	void*         savedUnit = nullptr;
	std::uint32_t savedType = 0, savedId = 0;
	if (!ReadField(path, PathTargetUnit, savedUnit) || !ReadField(path, PathTargetType, savedType) || !ReadField(path, PathTargetId, savedId)) return result;
	Inside      = true;
	int applied = 0;
	for (void* target : targets) {
		std::uint32_t type = 0, id = 0;
		if (!ReadField(target, UnitTypeOffset, type) || !ReadField(target, UnitIdOffset, id)) continue;
		WriteField(path, PathTargetUnit, target);
		WriteField(path, PathTargetType, type);
		WriteField(path, PathTargetId, id);
		if (target == caster) {
			// The game takes "no target" as the caster itself.
			WriteField<void*>(path, PathTargetUnit, nullptr);
		}
		applied += EnchantDo(game, caster, skill, level) ? 1 : 0;
	}
	WriteField(path, PathTargetUnit, savedUnit);
	WriteField(path, PathTargetType, savedType);
	WriteField(path, PathTargetId, savedId);
	Inside = false;
	Log(false, "Enchant (level %d): %d more friendly unit%s enchanted", level, applied, applied == 1 ? "" : "s");
	return result;
}

// ---- version check ----

bool Locate() noexcept {
	struct Check {
		std::uint64_t rva;
		const Code*   bytes;
	};
	const std::array<Check, 6> checks { {
		{ EnchantDoRva, &EnchantDoBytes },
		{ SkillTargetRva, &SkillTargetBytes },
		{ IsAllyRva, &IsAllyBytes },
		{ UnitRoomRva, &UnitRoomBytes },
		{ NearRoomsRva, &NearRoomsBytes },
		{ FirstUnitRva, &FirstUnitBytes },
	} };
	for (const auto& check : checks) {
		if (!CodeMatches(check.rva, *check.bytes)) {
			Log(true, "game code differs at D2R+0x%llX", static_cast<unsigned long long>(check.rva));
			return false;
		}
	}
	SkillTarget = reinterpret_cast<SkillTargetFn>(Base + SkillTargetRva);
	IsAlly      = reinterpret_cast<IsAllyFn>(Base + IsAllyRva);
	UnitRoom    = reinterpret_cast<UnitRoomFn>(Base + UnitRoomRva);
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

constexpr int MinYards = 1, MaxYards = 25;

bool Apply(const std::string& key, const std::string& value, std::string& error) {
	const auto flag = [&](std::atomic<bool>& target) {
		if (value != "true" && value != "false") return error = "true or false", false;
		target = value == "true";
		return true;
	};
	if (key == "enabled") return flag(Config.enabled);
	if (key == "players") return flag(Config.players);
	if (key == "minions") return flag(Config.minions);
	if (key == "radius_yards") {
		char*      end   = nullptr;
		const long yards = std::strtol(value.c_str(), &end, 10);
		if (value.empty() || *end || yards < MinYards || yards > MaxYards) return error = "1 to 25", false;
		Config.yards = static_cast<int>(yards);
		return true;
	}
	return error = "unknown setting", false;
}

std::string ValueOf(const std::string& key) {
	const auto flag = [](bool on) { return std::string(on ? "true" : "false"); };
	if (key == "enabled") return flag(Config.enabled);
	if (key == "players") return flag(Config.players);
	if (key == "minions") return flag(Config.minions);
	if (key == "radius_yards") return std::to_string(Config.yards.load());
	return {};
}

std::string Render() {
	return R"(# Enchant Everyone
#
# Casting Enchant also enchants every friendly unit around you: yourself, your
# party, mercenaries, summons and revives. Works in single player and in games
# you host. Every setting can also be changed in game with I Hate TOMLs.

[enchant-everyone]
enabled = )" + ValueOf("enabled") + R"(

# How far around the caster Enchant reaches, in yards (1 to 25).
radius_yards = )" + ValueOf("radius_yards") + R"(

# Enchant the other players of your party.
players = )" + ValueOf("players") + R"(

# Enchant mercenaries, summons and revives (yours and your party's).
minions = )" + ValueOf("minions") + "\n";
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

constexpr const char* Schema = R"toml(title = "Enchant Everyone"
[[setting]]
key = "enabled"
type = "bool"
label = "Enchant everyone around"
note = "Enchant also lands on every friendly unit rooms the caster."
[[setting]]
key = "radius_yards"
type = "int"
label = "Radius (yards)"
note = "How far around the caster. 1 to 25."
min = 1
max = 25
step = 1
shift_step = 5
ctrl_step = 10
[[setting]]
key = "players"
type = "bool"
label = "Party members"
note = "Other players in your party."
[[setting]]
key = "minions"
type = "bool"
label = "Mercenaries and summons"
note = "Mercenaries, summons and revives, yours and your party's."
)toml";

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
	(void)Menu.Start(context, Schema, MenuGet, MenuSet, nullptr);
	Base = context->exeBase;
	if (!Base || !Locate()) {
		Log(true, "Enchant Everyone %s: inactive, this game build isn't supported (Steam 3.3.94995 is)", PluginVersion);
		return true;
	}
	if (!context->InstallInlineHook(EnchantDoRva, EnchantDoBytes.data(), static_cast<std::uint32_t>(EnchantDoBytes.size()), &HookEnchantDo, &EnchantDo) || !EnchantDo) {
		Log(true, "Enchant Everyone %s: inactive, could not hook D2R+0x%llX (another plugin may hook it)", PluginVersion, static_cast<unsigned long long>(EnchantDoRva));
		return true;
	}
	Log(false, "Enchant Everyone %s: %s, radius %d yards", PluginVersion, Config.enabled ? "active" : "loaded but turned off", Config.yards.load());
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	Menu.Stop();
}
