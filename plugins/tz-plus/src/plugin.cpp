// TZ Plus: a bigger terror zone.
//   - Normal: the game's own terror zone, untouched.
//   - Nightmare: one whole act is terrorized, the act changing with the game's
//     30 minute rotation. 1 to 2 mods, tier 1 to 2.
//   - Hell: every non-town level is terrorized. 1 to 3 mods, tier 1 to 3. The
//     terror zone level goes up to 99 (the game stops at 96).
//   - The mods roll once per game and count for the whole game. How many, and
//     their tiers, grow with the terror zone level: a level 99 zone always has
//     3 tier 3 mods. Champions, uniques, minions and bosses get every mod one
//     tier higher (up to the difficulty's top tier).
//   - Rewards for the game: each mod gives its tier in points of gold find,
//     tier 2 and 3 mods also give (tier - 1) points of magic find, tier 3 mods
//     1 point of experience. A point is +25% gold find, +15% magic find, +5%
//     experience (on top of the game's +25%).
//   - No monster goes over level 99.
// Runs where the game's server runs: single player and games you host.
//
// Game code used (Steam 3.3.94995 / 3.4.95041, same RVAs):
//   Apply 0x4885F0 (game, zone, bool): applies a terror zone to one game (rolls
//     the game's monster mod, terrorizes each level of the zone). D2RCore calls
//     it through its own hook; ours sits on the function itself.
//   Desc 0x35B4F0 (zoneLevel, mode, difficulty) -> the level's settings for a
//     difficulty: +0 min level, +4 max level, +8 level boost, +0x10 experience %.
//   MonInit 0x49A7E0 (game, monster, room, flags), called once per monster at
//     the end of its creation (call at 0x543789): gives normal monsters the
//     game's terror zone mod. We call it, then add ours.
//   Monster mods: list 0x38E310 (9 bytes), record 0x4983C0 (mode, mod), check
//     0x4A1E50 (monster, record, any version), start table 0x2395FE0
//     (fn(monster, mod, unique)). Mod constants: record +0x1C.
//   Auras: add skill 0x433910 (unit, skill, level, 1), use it 0x438A70 (unit, 0, skill, -1).
//   Stats: get 0x2F5020 (unit, stat, layer), set 0x2F7D10 (unit, stat, value, layer).
//   Game: +0xF0 clients, +0x102 reference player level, +0x104 difficulty,
//     +0x106 mode (2 LoD, 3 RotW), +0x1A8 seed, +0x1A5C8 the game's mod,
//     +0x1A5CC rolled flag.

#include <D2RLPlugin/api.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
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

namespace {

constexpr const char* PluginId      = "tz-plus";
constexpr const char* PluginVersion = "0.0.4";

const D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = PluginId,
	.name        = "TZ Plus",
	.version     = PluginVersion,
	.author      = "wild-healer",
	.description = "Bigger terror zones: whole acts in Nightmare, all of Hell, up to 3 mods with tiers, more loot.",
	.flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

// ---- mods ----

enum class Kind : int { UMod, Aura, Tough, Vampiric, Regen };

struct Mod {
	const char* key;
	const char* name;
	Kind        kind;
	int         id;      // monster mod id, or aura skill id
	int         weight;
	int         divisor; // aura: level = monster level / divisor at tier 3
	bool        notOnBoss;
};

constexpr Mod Mods[] {
	{ "extra_strong", "Extra Strong", Kind::UMod, 5, 10, 0, false },
	{ "extra_fast", "Extra Fast", Kind::UMod, 6, 10, 0, false },
	{ "cursed", "Cursed", Kind::UMod, 7, 10, 0, false },
	{ "magic_resistant", "Magic Resistant", Kind::UMod, 8, 10, 0, false },
	{ "fire_enchanted", "Fire Enchanted", Kind::UMod, 9, 10, 0, false },
	{ "lightning_enchanted", "Lightning Enchanted", Kind::UMod, 17, 10, 0, false },
	{ "cold_enchanted", "Cold Enchanted", Kind::UMod, 18, 10, 0, false },
	{ "venomous", "Venomous", Kind::UMod, 23, 10, 0, false },
	{ "mana_burn", "Mana Burn", Kind::UMod, 25, 10, 0, false },
	{ "teleportation", "Teleportation", Kind::UMod, 26, 10, 0, true },
	{ "spectral_hit", "Spectral Hit", Kind::UMod, 27, 10, 0, false },
	{ "stone_skin", "Stone Skin", Kind::UMod, 28, 10, 0, false },
	{ "multiple_shots", "Multiple Shots", Kind::UMod, 29, 10, 0, true },
	{ "tough", "Tough", Kind::Tough, 0, 10, 0, false },
	{ "vampiric", "Vampiric", Kind::Vampiric, 0, 10, 0, false },
	{ "regenerating", "Regenerating", Kind::Regen, 0, 10, 0, false },
	{ "might", "Might", Kind::Aura, 98, 3, 6, false },
	{ "holy_fire", "Holy Fire", Kind::Aura, 368, 3, 6, false },
	{ "blessed_aim", "Blessed Aim", Kind::Aura, 108, 3, 5, false },
	{ "holy_freeze", "Holy Freeze", Kind::Aura, 365, 3, 7, false },
	{ "conviction", "Conviction", Kind::Aura, 123, 3, 8, false },
	{ "fanaticism", "Fanaticism", Kind::Aura, 122, 3, 8, false },
	{ "holy_shock", "Holy Shock", Kind::Aura, 369, 3, 8, false },
	{ "concentration", "Concentration", Kind::Aura, 113, 3, 6, false },
	{ "vigor", "Vigor", Kind::Aura, 115, 3, 6, false },
	{ "thorns", "Thorns", Kind::Aura, 428, 3, 10, false },
};
constexpr int ModCount = static_cast<int>(sizeof(Mods) / sizeof(Mods[0]));

// Stats
constexpr int StatHp = 6, StatMaxHp = 7, StatLevel = 12, StatDefense = 31, StatPhysRes = 36, StatMagicRes = 37, StatFireRes = 39, StatLightRes = 41,
              StatColdRes = 43, StatLifeSteal = 60, StatVelocity = 67, StatAttackRate = 68, StatRegen = 74, StatGoldFind = 79, StatMagicFind = 80;

// Per tier values (index = tier - 1).
constexpr int OwnResist[3]   { 50, 75, 100 }; // Fire/Lightning/Cold Enchanted
constexpr int ManaBurnRes[3] { 10, 20, 30 };
constexpr int SpectralRes[3] { 10, 20, 30 };
constexpr int MagicRes[3]    { 20, 40, 60 };
constexpr int StoneDefense[3] { 150, 200, 250 }; // % of defense
constexpr int StonePhys[3]   { 35, 50, 65 };
constexpr int ToughLife[3]   { 50, 100, 150 };
constexpr int LifeSteal[3]   { 5, 10, 15 };
constexpr int RegenPct[3]    { 2, 4, 6 };     // % of life per second
constexpr int AuraPct[3]     { 50, 75, 100 }; // of the game's elite aura level
// Mod constants (monumod.txt rows, the column the game reads: +0x1C).
constexpr int RowMinionAr = 12, RowMinionStrong = 14, RowMinionElemMin = 16, RowMinionElemMax = 19, RowCurse = 34;
constexpr int StrongDamage[3] { 75, 114, 152 }; // row 14: Hell +49% / +75% / +100% damage
constexpr int StrongAr[3]     { 50, 76, 114 };  // row 12: Hell +33% / +50% / +75% attack rating
constexpr int ElemScale[3]    { 100, 150, 200 }; // rows 16-21: 33-50% / 50-75% / 66-100% of base damage
constexpr int CursePct[3]     { 25, 50, 75 };
constexpr int FastMove[3]     { 0, 33, 66 };  // % run/walk speed on top of the game's Extra Fast
constexpr int FastAttack[3]   { 0, 0, 20 };   // % attack speed

constexpr int GoldPerPoint = 25, MagicPerPoint = 15, XpPerPoint = 5;

struct Range {
	int minLevel, maxLevel, maxMods, maxTier;
};
constexpr Range Ranges[3] { { 0, 0, 0, 0 }, { 40, 71, 2, 2 }, { 70, 99, 3, 3 } };

// ---- settings ----

struct Settings {
	std::atomic<bool> enabled { true };
	std::atomic<bool> nightmare { true };
	std::atomic<bool> hell { true };
	std::atomic<bool> allowed[ModCount] {};
	std::atomic<bool>  show { true };
	Settings() {
		for (auto& on : allowed) on = true;
	}
} Config;

// ---- native layout ----

using Code = std::array<std::uint8_t, 16>;

constexpr std::uint64_t ApplyRva = 0x4885F0;
constexpr Code ApplyBytes { 0x40, 0x55, 0x53, 0x41, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0x70, 0xF9, 0xFF, 0xFF, 0x48, 0x81, 0xEC };
constexpr std::uint64_t DescRva = 0x35B4F0;
constexpr Code DescBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x44, 0x89, 0x44, 0x24, 0x18, 0x55, 0x56, 0x57, 0x48, 0x8B, 0xEC };
constexpr std::uint64_t MonInitCallRva = 0x543789;
constexpr std::uint64_t MonInitRva     = 0x49A7E0;
constexpr std::uint64_t TerrorCheckCallRva = 0x54375B; // call 0x3AF200
constexpr std::uint64_t LevelUpCallRva     = 0x543775; // call 0x49AEB0
constexpr std::uint64_t ConfigAtRva    = 0x35DB10;
constexpr std::uint64_t UModTableRva   = 0x2395FE0;

struct Check {
	std::uint64_t rva;
	std::uint8_t  bytes[8];
};
// First bytes of each game function we call (the stat functions are jumps, checked by opcode only).
constexpr Check Calls[] {
	{ 0x49A7E0, { 0x4D, 0x85, 0xC0, 0x0F, 0x84, 0xEC, 0x02, 0x00 } }, // MonInit
	{ 0x38E310, { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B } }, // UModList
	{ 0x38E0E0, { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83 } }, // IsSpecial
	{ 0x4983C0, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74 } }, // UModRecord
	{ 0x4A1E50, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x54 } }, // UModAllowed
	{ 0x433910, { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x41 } }, // AddSkill
	{ 0x438A70, { 0x41, 0x83, 0xF8, 0x05, 0x0F, 0x84, 0x28, 0x02 } }, // UseSkill
	{ 0x2EFC10, { 0x48, 0x85, 0xC9, 0x75, 0x03, 0x33, 0xC0, 0xC3 } }, // RoomLevel
	{ 0x489B70, { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83 } }, // LevelTerrorized
	{ 0x48FE80, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74 } }, // FindUnit
	{ 0x542EA0, { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83 } }, // GetLevelInfo
	{ 0x543A40, { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83 } }, // SetLevelInfo
	{ 0x3AEFF0, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83 } }, // BossKind
	{ 0x35DB10, { 0x40, 0x53, 0x57, 0x41, 0x57, 0x48, 0x83, 0xEC } }, // ConfigAt
	{ 0x34B440, { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B } }, // UnitRoom
	{ 0x3AF200, { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B } }, // TerrorCheck
	{ 0x49AEB0, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74 } }, // LevelUp
	{ 0x1475050, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74 } }, // AppendChar (map text)
};
constexpr std::uint64_t GetStatRva = 0x2F5020, SetStatRva = 0x2F7D10; // jmp qword ptr [rip+x]

using ApplyFn         = std::uint64_t (*)(std::uint8_t* game, std::uint8_t* zone, std::uint8_t flag) noexcept;
using DescFn          = std::uint8_t* (*)(const void* zoneLevel, std::uint8_t mode, std::int32_t difficulty) noexcept;
using MonInitFn       = std::uint64_t (*)(std::uint8_t* game, std::uint8_t* unit, void* room, std::uint16_t flags) noexcept;
using UModListFn      = std::uint8_t* (*)(void* unit) noexcept;
using IsSpecialFn     = bool (*)(void* unit, bool any) noexcept;
using UModRecordFn    = void* (*)(std::uint8_t mode, std::int32_t mod) noexcept;
using UModAllowedFn   = std::int32_t (*)(void* unit, void* record, std::int32_t anyVersion) noexcept;
using UModStartFn     = void (*)(void* unit, std::int32_t mod, std::int32_t unique) noexcept;
using AddSkillFn      = void* (*)(void* unit, std::int32_t skill, std::int32_t level, std::int32_t one) noexcept;
using UseSkillFn      = void (*)(void* unit, std::int32_t hand, std::int32_t skill, std::int32_t minusOne) noexcept;
using UnitRoomFn      = void* (*)(void* unit) noexcept;
using RoomLevelFn     = std::int32_t (*)(void* room) noexcept;
using LevelTzFn       = bool (*)(void* game, std::int32_t level) noexcept;
using FindUnitFn      = void* (*)(void* game, std::int32_t type, std::uint32_t id) noexcept;
using LevelInfoFn     = bool (*)(void* unit, void* info) noexcept;
using BossKindFn      = std::int32_t (*)(void* unused, void* unit) noexcept;
using ConfigAtFn      = void* (*)(std::int64_t time, bool* changed) noexcept;
using GetStatFn       = std::int32_t (*)(void* unit, std::int32_t stat, std::uint16_t layer) noexcept;
using SetStatFn       = void (*)(void* unit, std::int32_t stat, std::int32_t value, std::uint16_t layer) noexcept;

const D2RL::PluginContext* Context {};
std::uintptr_t             Base {};
ApplyFn                    Apply {};
DescFn                     Desc {};
MonInitFn                  MonInit {};

template <typename T>
T At(std::uint64_t rva) noexcept {
	return reinterpret_cast<T>(Base + rva);
}

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
T Field(const void* base, std::uintptr_t offset) noexcept {
	T value {};
	std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(T));
	return value;
}

template <typename T>
void SetField(void* base, std::uintptr_t offset, T value) noexcept {
	std::memcpy(static_cast<std::uint8_t*>(base) + offset, &value, sizeof(T));
}

std::int32_t GetStat(void* unit, int stat) noexcept {
	return At<GetStatFn>(GetStatRva)(unit, stat, 0);
}
void SetStat(void* unit, int stat, std::int32_t value) noexcept {
	At<SetStatFn>(SetStatRva)(unit, stat, value, 0);
}

// ---- game offsets ----

constexpr std::uintptr_t GameClients = 0xF0, GameRefLevel = 0x102, GameDifficulty = 0x104, GameMode = 0x106, GameSeed = 0x1A8, GameUMod = 0x1A5C8,
                         GameUModRolled = 0x1A5CC;
constexpr std::uintptr_t ClientNext = 0x628, ClientFlags = 0x4E0, ClientUnit = 0x278, ClientGame = 0x2B0, ClientUnitType = 0x26C, ClientUnitId = 0x270;
constexpr std::uintptr_t ConfigStart = 0x00, ConfigZoneMin = 0x10, ConfigBreakMin = 0x14, ConfigZones = 0x250, ConfigZoneCount = 0x258;
constexpr std::size_t    ZoneSize = 0x3E8, LevelSize = 0x38, DescSize = 0x6F8;
constexpr std::uintptr_t ZoneLevels = 0x88, ZoneLevelCount = 0x90, ZoneLevelCap = 0x98;
constexpr std::uint64_t  NotOnHeap = 0x8000000000000000ull;
constexpr std::uintptr_t DescMax = 0x04, DescXp = 0x10;

// ---- the rolled mods of a game ----

struct Rolled {
	int mod;
	int tier;
};

struct GameMods {
	std::uint8_t*       game = nullptr;
	std::uint64_t       seed = 0;
	int                 difficulty = 0;
	int                 zoneLevel = 0;
	std::atomic<int>    act { 0 }; // Nightmare: the terrorized act (1-5); Hell: 0 (all)
	int                 count = 0;
	Rolled              mods[3] {};
	int                 gold = 0, magic = 0, xp = 0; // % bonuses
	std::atomic<int>    monsters { 0 };
};

SRWLOCK               Lock = SRWLOCK_INIT;
std::vector<GameMods*> Games; // never freed (a few per session)
GameMods*             Last {};

std::uint64_t Mix(std::uint64_t& state) noexcept {
	std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
	z               = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
	z               = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
	return z ^ (z >> 31);
}
double Unit(std::uint64_t& state) noexcept {
	return static_cast<double>(Mix(state) >> 11) / 9007199254740992.0;
}
int Grow(double value, std::uint64_t& state) noexcept {
	const int whole = static_cast<int>(std::floor(value));
	return whole + (Unit(state) < value - whole ? 1 : 0);
}

std::string Roman(int tier) {
	return tier == 1 ? "I" : tier == 2 ? "II" : "III";
}

std::string Describe(const GameMods& g) {
	std::string text;
	for (int i = 0; i < g.count; ++i) {
		if (!text.empty()) text += ", ";
		text += std::string(Mods[g.mods[i].mod].name) + " " + Roman(g.mods[i].tier);
	}
	char rewards[160];
	std::snprintf(rewards, sizeof(rewards), "; +%d%% gold find, +%d%% magic find, +%d%% experience", g.gold, g.magic, g.xp);
	return (text.empty() ? std::string("no mods") : text) + rewards;
}

void Roll(GameMods& g) noexcept {
	const Range& range = Ranges[g.difficulty];
	const int    ref   = Field<std::uint8_t>(g.game, GameRefLevel);
	g.zoneLevel        = std::clamp(ref == 0xFF ? range.minLevel : ref + 2, range.minLevel, range.maxLevel);
	const double p     = static_cast<double>(g.zoneLevel - range.minLevel) / (range.maxLevel - range.minLevel);
	std::uint64_t state = g.seed ^ (static_cast<std::uint64_t>(std::time(nullptr)) << 20) ^ reinterpret_cast<std::uintptr_t>(g.game);
	const int     want  = std::clamp(Grow(1.0 + (range.maxMods - 1) * p, state), 1, range.maxMods);
	bool          taken[ModCount] {};
	bool          aura = false;
	g.count            = 0;
	while (g.count < want) {
		int total = 0;
		for (int m = 0; m < ModCount; ++m)
			if (!taken[m] && Config.allowed[m] && !(aura && Mods[m].kind == Kind::Aura)) total += Mods[m].weight;
		if (total <= 0) break;
		int pick = static_cast<int>(Mix(state) % static_cast<std::uint64_t>(total));
		int chosen = -1;
		for (int m = 0; m < ModCount; ++m) {
			if (taken[m] || !Config.allowed[m] || (aura && Mods[m].kind == Kind::Aura)) continue;
			if (pick < Mods[m].weight) {
				chosen = m;
				break;
			}
			pick -= Mods[m].weight;
		}
		if (chosen < 0) break;
		taken[chosen] = true;
		aura |= Mods[chosen].kind == Kind::Aura;
		const int tier          = std::clamp(Grow(1.0 + (range.maxTier - 1) * p, state), 1, range.maxTier);
		g.mods[g.count++]       = { chosen, tier };
	}
	int gold = 0, magic = 0, xp = 0;
	for (int i = 0; i < g.count; ++i) {
		gold += g.mods[i].tier;
		magic += std::max(0, g.mods[i].tier - 1);
		xp += std::max(0, g.mods[i].tier - 2);
	}
	g.gold  = gold * GoldPerPoint;
	g.magic = magic * MagicPerPoint;
	g.xp    = xp * XpPerPoint;
}

GameMods* Find(const std::uint8_t* game) noexcept {
	// By the game's memory and difficulty (its seed at +0x1A8 is the game's random state, it changes).
	// Forgotten when the local player leaves a game (OnGameplayEvent).
	for (GameMods* g : Games)
		if (g->game == game && g->difficulty == Field<std::uint8_t>(game, GameDifficulty)) return g;
	return nullptr;
}

GameMods* ModsFor(std::uint8_t* game, int difficulty) noexcept {
	AcquireSRWLockExclusive(&Lock);
	GameMods* g = Find(game);
	if (!g) {
		for (auto it = Games.begin(); it != Games.end();) // the same game memory reused by a new game
			if ((*it)->game == game) it = Games.erase(it);
			else ++it;
		g             = new GameMods();
		g->game       = game;
		g->seed       = Field<std::uint64_t>(game, GameSeed);
		g->difficulty = difficulty;
		Roll(*g);
		Games.push_back(g);
		if (Games.size() > 64) Games.erase(Games.begin());
		Log(false, "%s game, terror zone level %d: %s", difficulty == 2 ? "Hell" : "Nightmare", g->zoneLevel, Describe(*g).c_str());
	}
	Last = g;
	ReleaseSRWLockExclusive(&Lock);
	return g;
}

GameMods* ModsOf(const std::uint8_t* game) noexcept {
	AcquireSRWLockShared(&Lock);
	GameMods* g = Find(game);
	ReleaseSRWLockShared(&Lock);
	return g;
}

int TierOf(const GameMods& g, int id, Kind kind = Kind::UMod) noexcept {
	for (int i = 0; i < g.count; ++i)
		if (Mods[g.mods[i].mod].kind == kind && Mods[g.mods[i].mod].id == id) return g.mods[i].tier;
	return 0;
}

// ---- mod constants (shared game data, set for each game's difficulty when it starts) ----

struct Saved {
	std::uint8_t  mode;
	int           row;
	std::int32_t  value;
};
std::vector<Saved> Originals;

std::int32_t* RowValue(std::uint8_t mode, int row) noexcept {
	auto* record = static_cast<std::uint8_t*>(At<UModRecordFn>(0x4983C0)(mode, row));
	return record ? reinterpret_cast<std::int32_t*>(record + 0x1C) : nullptr;
}

std::int32_t Original(std::uint8_t mode, int row) noexcept {
	for (const Saved& s : Originals)
		if (s.mode == mode && s.row == row) return s.value;
	std::int32_t* value = RowValue(mode, row);
	if (!value) return -1;
	Originals.push_back({ mode, row, *value });
	return *value;
}

void SetRow(std::uint8_t mode, int row, std::int32_t value) noexcept {
	if (Original(mode, row) < 0) return;
	if (std::int32_t* at = RowValue(mode, row)) *at = value;
}

void SetConstants(const GameMods& g, std::uint8_t mode) noexcept {
	const int strong = TierOf(g, 5);
	SetRow(mode, RowMinionStrong, strong ? StrongDamage[strong - 1] : Original(mode, RowMinionStrong));
	SetRow(mode, RowMinionAr, strong ? StrongAr[strong - 1] : Original(mode, RowMinionAr));
	int elem = 0;
	for (const int id : { 9, 17, 18, 23, 25 }) elem = std::max(elem, TierOf(g, id));
	for (const int row : { RowMinionElemMin + g.difficulty, RowMinionElemMax + g.difficulty }) {
		const std::int32_t base = Original(mode, row);
		SetRow(mode, row, elem ? base * ElemScale[elem - 1] / 100 : base);
	}
	const int curse = TierOf(g, 7);
	SetRow(mode, RowCurse, curse ? CursePct[curse - 1] : Original(mode, RowCurse));
}

// ---- the terrorized levels ----

constexpr int Act1[] { 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39 };
constexpr int Act2[] { 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74 };
constexpr int Act3[] { 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102 };
constexpr int Act4[] { 104, 105, 106, 107, 108 };
constexpr int Act5[] { 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132 };

struct ActLevels {
	const int* ids;
	int        count;
};
constexpr ActLevels Acts[5] { { Act1, 38 }, { Act2, 34 }, { Act3, 27 }, { Act4, 5 }, { Act5, 22 } };

struct BuiltZone {
	const void* real;
	const void* config;
	int         act; // 0 = every act
	void*       zone;
};
std::vector<BuiltZone> Built; // never freed: a game may keep reading one

// The config's zone level entry for a level id (each one carries its own settings).
const std::uint8_t* EntryFor(const void* config, int level) noexcept {
	if (!config) return nullptr;
	const auto*         zones = Field<const std::uint8_t*>(config, ConfigZones);
	const std::uint64_t count = Field<std::uint64_t>(config, ConfigZoneCount);
	if (!zones || count > 512) return nullptr;
	for (std::uint64_t z = 0; z < count; ++z) {
		const std::uint8_t* zone   = zones + z * ZoneSize;
		const auto*         levels = Field<const std::uint8_t*>(zone, ZoneLevels);
		const std::uint64_t n      = Field<std::uint64_t>(zone, ZoneLevelCount);
		if (!levels || n > 64) continue;
		for (std::uint64_t l = 0; l < n; ++l)
			if (Field<std::int32_t>(levels + l * LevelSize, 0) == level) return levels + l * LevelSize;
	}
	return nullptr;
}

std::int64_t SlotOf(const void* config, std::int64_t now) noexcept {
	if (!config) return now / 1800;
	const std::int64_t start  = Field<std::int64_t>(config, ConfigStart);
	const std::int64_t period = (static_cast<std::int64_t>(Field<std::int32_t>(config, ConfigZoneMin)) + Field<std::int32_t>(config, ConfigBreakMin)) * 60;
	if (period <= 0 || now < start) return now / 1800;
	return (now - start) / period;
}

void* BuildZone(const std::uint8_t* real, const void* config, int act) noexcept {
	for (const BuiltZone& b : Built)
		if (b.real == real && b.config == config && b.act == act) return b.zone;
	const auto*         realLevels = Field<const std::uint8_t*>(real, ZoneLevels);
	const std::uint64_t realCount  = Field<std::uint64_t>(real, ZoneLevelCount);
	if (!realLevels || realCount == 0 || realCount > 64) return nullptr;
	std::uint64_t total = 0;
	for (int a = 0; a < 5; ++a)
		if (act == 0 || act == a + 1) total += static_cast<std::uint64_t>(Acts[a].count);
	auto* zone   = static_cast<std::uint8_t*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, ZoneSize));
	auto* levels = static_cast<std::uint8_t*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, total * LevelSize));
	if (!zone || !levels) return nullptr;
	std::memcpy(zone, real, ZoneSize);
	std::uint64_t at = 0;
	for (int a = 0; a < 5; ++a) {
		if (act != 0 && act != a + 1) continue;
		for (int i = 0; i < Acts[a].count; ++i) {
			const int           id    = Acts[a].ids[i];
			const std::uint8_t* entry = EntryFor(config, id);
			std::memcpy(levels + at * LevelSize, entry ? entry : realLevels, LevelSize);
			SetField<std::int32_t>(levels + at * LevelSize, 0, id);
			++at;
		}
	}
	SetField<void*>(zone, ZoneLevels, levels);
	SetField<std::uint64_t>(zone, ZoneLevelCount, total);
	SetField<std::uint64_t>(zone, ZoneLevelCap, total | NotOnHeap);
	Built.push_back({ real, config, act, zone });
	return zone;
}

// ---- level settings while a zone is applied (max level 99 in Hell, experience) ----

struct Override {
	std::atomic<DWORD> thread { 0 };
	int                maxLevel = 0;
	int                xp       = 0;
} Applying;

struct DescCopy {
	const std::uint8_t* real;
	int                 maxLevel, xp;
	std::uint8_t*       copy;
};
std::vector<DescCopy> Descs; // never freed

std::uint8_t* HookDesc(const void* zoneLevel, std::uint8_t mode, std::int32_t difficulty) noexcept {
	std::uint8_t* desc = Desc(zoneLevel, mode, difficulty);
	if (!desc || Applying.thread.load(std::memory_order_relaxed) != GetCurrentThreadId()) return desc;
	const int maxLevel = Applying.maxLevel ? Applying.maxLevel : Field<std::int32_t>(desc, DescMax);
	const int xp       = Field<std::int32_t>(desc, DescXp) + Applying.xp;
	for (const DescCopy& d : Descs)
		if (d.real == desc && d.maxLevel == maxLevel && d.xp == xp) return d.copy;
	auto* copy = static_cast<std::uint8_t*>(HeapAlloc(GetProcessHeap(), 0, DescSize));
	if (!copy) return desc;
	std::memcpy(copy, desc, DescSize);
	SetField<std::int32_t>(copy, DescMax, maxLevel);
	SetField<std::int32_t>(copy, DescXp, xp);
	Descs.push_back({ desc, maxLevel, xp, copy });
	return copy;
}

// ---- rewards (gold find and magic find on the players of the game; not saved with the character) ----

void* ClientUnitOf(const std::uint8_t* client) noexcept {
	if (Field<std::uint32_t>(client, ClientFlags) & 1) return Field<void*>(client, ClientUnit);
	if (!Field<void*>(client, ClientUnit)) return nullptr;
	void* game = Field<void*>(client, ClientGame);
	if (!game) return nullptr;
	return At<FindUnitFn>(0x48FE80)(game, Field<std::int32_t>(client, ClientUnitType), Field<std::uint32_t>(client, ClientUnitId));
}

// level < 0: every player. Else only the players in that level.
void GiveRewards(std::uint8_t* game, const GameMods& g, bool on, int level = -1) noexcept {
	int players = 0;
	for (auto* client = Field<std::uint8_t*>(game, GameClients); client && players < 64; client = Field<std::uint8_t*>(client, ClientNext), ++players) {
		void* unit = ClientUnitOf(client);
		if (!unit) continue;
		if (level >= 0) {
			void* room = At<UnitRoomFn>(0x34B440)(unit);
			if (!room || At<RoomLevelFn>(0x2EFC10)(room) != level) continue;
		}
		SetStat(unit, StatGoldFind, on ? g.gold : 0);
		SetStat(unit, StatMagicFind, on ? g.magic : 0);
	}
}

// ---- applying a zone to a game ----

std::uint64_t HookApply(std::uint8_t* game, std::uint8_t* zone, std::uint8_t flag) noexcept {
	if (!game || !zone || !flag || !Config.enabled.load(std::memory_order_relaxed)) return Apply(game, zone, flag);
	const int          difficulty = Field<std::uint8_t>(game, GameDifficulty);
	const std::uint8_t mode       = Field<std::uint8_t>(game, GameMode);
	if (mode < 2 || difficulty < 1 || difficulty > 2 || (difficulty == 1 && !Config.nightmare) || (difficulty == 2 && !Config.hell)) return Apply(game, zone, flag);

	const std::int64_t now     = static_cast<std::int64_t>(std::time(nullptr));
	bool               changed = false;
	const void*        config  = At<ConfigAtFn>(ConfigAtRva)(now, &changed);
	const int          act     = difficulty == 2 ? 0 : static_cast<int>(SlotOf(config, now) % 5) + 1;
	void*              ours    = BuildZone(zone, config, act);
	GameMods*          g       = ModsFor(game, difficulty);
	g->act.store(act);

	// No game mod: ours replace it, and it must not roll again on rotation.
	SetField<std::int32_t>(game, GameUMod, 0);
	SetField<std::uint8_t>(game, GameUModRolled, 1);
	SetConstants(*g, mode);

	Applying.maxLevel = Ranges[difficulty].maxLevel;
	Applying.xp       = g->xp;
	Applying.thread.store(GetCurrentThreadId());
	const std::uint64_t result = Apply(game, static_cast<std::uint8_t*>(ours ? ours : zone), flag);
	Applying.thread.store(0);
	// Hell: every level counts. Nightmare: set as the players reach a level (see HookMonInit).
	GiveRewards(game, *g, difficulty == 2);
	return result;
}

// ---- monsters ----

constexpr int Resists[] { StatFireRes, StatLightRes, StatColdRes };

void ApplyUMod(void* unit, std::uint8_t mode, int id, int tier, bool elite) noexcept {
	std::uint8_t* list = At<UModListFn>(0x38E310)(unit);
	if (!list) return;
	int slot = -1;
	for (int i = 0; i < 9; ++i) {
		if (list[i] == id) return; // already has it
		if (list[i] == 0 && slot < 0) slot = i;
	}
	if (slot < 0) return;
	void* record = At<UModRecordFn>(0x4983C0)(mode, id);
	if (!record || !At<UModAllowedFn>(0x4A1E50)(unit, record, 1)) return;

	int before[6] {};
	constexpr int watched[6] { StatFireRes, StatLightRes, StatColdRes, StatMagicRes, StatPhysRes, StatDefense };
	for (int i = 0; i < 6; ++i) before[i] = GetStat(unit, watched[i]);

	list[slot] = static_cast<std::uint8_t>(id);
	const auto start = *reinterpret_cast<UModStartFn*>(Base + UModTableRva + static_cast<std::uint64_t>(id) * 8);
	if (start) start(unit, id, elite ? 1 : 0);

	// Resist mods: our values replace the game's (no limit on new immunities).
	const int t = tier - 1;
	switch (id) {
	case 9: SetStat(unit, StatFireRes, before[0] + OwnResist[t]); break;
	case 17: SetStat(unit, StatLightRes, before[1] + OwnResist[t]); break;
	case 18: SetStat(unit, StatColdRes, before[2] + OwnResist[t]); break;
	case 25: SetStat(unit, StatMagicRes, before[3] + ManaBurnRes[t]); break;
	case 27:
		for (int i = 0; i < 3; ++i) SetStat(unit, Resists[i], before[i] + SpectralRes[t]);
		break;
	case 8:
		for (int i = 0; i < 3; ++i) SetStat(unit, Resists[i], before[i] + MagicRes[t]);
		break;
	case 6:
		if (FastMove[t]) SetStat(unit, StatVelocity, GetStat(unit, StatVelocity) + FastMove[t]);
		if (FastAttack[t]) SetStat(unit, StatAttackRate, GetStat(unit, StatAttackRate) + FastAttack[t]);
		break;
	case 28:
		SetStat(unit, StatDefense, before[5] * StoneDefense[t] / 100);
		SetStat(unit, StatPhysRes, before[4] + StonePhys[t]);
		break;
	default: break;
	}
}

void ApplyAura(void* unit, const Mod& mod, int tier) noexcept {
	if (std::uint8_t* list = At<UModListFn>(0x38E310)(unit))
		for (int i = 0; i < 9; ++i)
			if (list[i] == 30) return; // has an aura of its own
	const int level = std::clamp(std::max(1, GetStat(unit, StatLevel)) / mod.divisor * AuraPct[tier - 1] / 100, 1, 99);
	At<AddSkillFn>(0x433910)(unit, mod.id, level, 1);
	At<UseSkillFn>(0x438A70)(unit, 0, mod.id, -1);
}

void CapLevel(void* unit) noexcept {
	const int level = GetStat(unit, StatLevel);
	if (level <= 99) return;
	alignas(16) std::uint8_t info[0x40] {};
	if (At<LevelInfoFn>(0x542EA0)(unit, info)) {
		SetField<std::int32_t>(info, 0x18, Field<std::int32_t>(info, 0x18) + (99 - level));
		At<LevelInfoFn>(0x543A40)(unit, info);
	}
	SetStat(unit, StatLevel, 99);
}

int  Traced = 0; // monsters logged so far (the first few of each game, to check the mods land)
const std::uint8_t* TracedGame {};

void Trace(const char* format, ...) noexcept {
	char    line[512] {};
	va_list args;
	va_start(args, format);
	std::vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	Log(false, "%s", line);
}

// Every monster, once, at the end of its creation (where the game checks for its terror zone mod).
void AddMods(std::uint8_t* game, void* unit) noexcept {
	if (!game || !unit || !Config.enabled.load(std::memory_order_relaxed)) return;
	GameMods* g = ModsOf(game);
	if (!g) return;
	void* room = At<UnitRoomFn>(0x34B440)(unit);
	if (!room) return;
	const int level = At<RoomLevelFn>(0x2EFC10)(room);
	if (level <= 0) return;
	const bool terrorized = At<LevelTzFn>(0x489B70)(game, level);
	// Nightmare: only one act is terrorized, so the rewards follow the level the players are in
	// (monsters, town people included, are created as a player reaches a level).
	if (g->difficulty == 1) GiveRewards(game, *g, terrorized, level);
	static int quiet = 0; // monsters outside terror zones logged so far
	if (TracedGame != game) TracedGame = game, Traced = 0, quiet = 0;
	const bool trace = Traced < 25;
	if (!terrorized) {
		if (quiet < 5) ++quiet, Trace("monster in level %d: not terrorized, no mods", level);
		return;
	}
	auto*              data  = Field<std::uint8_t*>(unit, 0x10); // MonsterData
	const std::uint16_t type = data ? Field<std::uint16_t>(data, 0x1A) : 0;
	const std::uint8_t mode  = Field<std::uint8_t>(game, GameMode);
	const bool         boss  = At<BossKindFn>(0x3AEFF0)(nullptr, unit) != 0;
	const bool         elite = boss || (type & 0x61E) != 0 || At<IsSpecialFn>(0x38E0E0)(unit, true);
	const int          top   = Ranges[g->difficulty].maxTier;
	const int          cold0 = trace ? GetStat(unit, StatColdRes) : 0, fire0 = trace ? GetStat(unit, StatFireRes) : 0;
	for (int i = 0; i < g->count; ++i) {
		const Mod& mod  = Mods[g->mods[i].mod];
		const int  tier = elite ? std::min(g->mods[i].tier + 1, top) : g->mods[i].tier;
		if (boss && mod.notOnBoss) continue;
		switch (mod.kind) {
		case Kind::UMod: ApplyUMod(unit, mode, mod.id, tier, elite); break;
		case Kind::Aura: ApplyAura(unit, mod, tier); break;
		case Kind::Tough: {
			const std::int32_t life = GetStat(unit, StatMaxHp);
			const std::int32_t more = static_cast<std::int32_t>(static_cast<std::int64_t>(life) * (100 + ToughLife[tier - 1]) / 100);
			SetStat(unit, StatMaxHp, more);
			SetStat(unit, StatHp, more);
			break;
		}
		case Kind::Vampiric: SetStat(unit, StatLifeSteal, GetStat(unit, StatLifeSteal) + LifeSteal[tier - 1]); break;
		case Kind::Regen: {
			const std::int64_t life = GetStat(unit, StatMaxHp); // 1/256 points
			SetStat(unit, StatRegen, GetStat(unit, StatRegen) + static_cast<std::int32_t>(life * RegenPct[tier - 1] / 100 / 25));
			break;
		}
		}
	}
	CapLevel(unit);
	g->monsters.fetch_add(1, std::memory_order_relaxed);
	if (trace) {
		++Traced;
		std::string mods;
		if (const std::uint8_t* list = At<UModListFn>(0x38E310)(unit))
			for (int i = 0; i < 9; ++i)
				if (list[i]) mods += (mods.empty() ? "" : ",") + std::to_string(list[i]);
		Trace("monster in level %d (mlvl %d, %s, type 0x%X): mods [%s], cold res %d -> %d, fire res %d -> %d", level, GetStat(unit, StatLevel),
		      boss ? "boss" : elite ? "elite" : "normal", type, mods.c_str(), cold0, GetStat(unit, StatColdRes), fire0, GetStat(unit, StatFireRes));
	}
}

using TerrorCheckFn = bool (*)(void* unit) noexcept;
using LevelUpFn     = std::uint64_t (*)(void* unit, void* info, std::uint16_t flags) noexcept;

// 0x54375B: call 0x3AF200 (monster has the terror state?), reached by every monster.
// The stub passes the game too (r14 there, see PatchCall).
bool HookTerrorCheck(void* unit, std::uint8_t* game) noexcept {
	AddMods(game, unit);
	return At<TerrorCheckFn>(0x3AF200)(unit);
}

// 0x543789: the game's terror zone mod (its +3 level for bosses). 0x543775: the other branch.
std::uint64_t HookMonInit(std::uint8_t* game, std::uint8_t* unit, void* room, std::uint16_t flags) noexcept {
	const std::uint64_t result = MonInit(game, unit, room, flags);
	if (unit && ModsOf(game)) CapLevel(unit);
	return result;
}
std::uint64_t HookLevelUp(void* unit, void* info, std::uint16_t flags) noexcept {
	const std::uint64_t result = At<LevelUpFn>(0x49AEB0)(unit, info, flags);
	if (unit) CapLevel(unit); // only reached through monster init; the cap is a no-op under 100
	return result;
}

// ---- call site stub (the monster init call must reach our code within a rel32) ----

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

std::uint8_t* StubPage {};
std::size_t   StubUsed = 0;

// Redirects "call target" at site to hook (through a jump near the game code).
bool PatchCall(std::uint64_t siteRva, std::uint64_t targetRva, const void* hook, bool gameInRdx = false) noexcept {
	const std::uintptr_t site = Base + siteRva;
	if (!StubPage) StubPage = static_cast<std::uint8_t*>(AllocNear(site, 4096));
	if (!StubPage || StubUsed + 32 > 4096) return false;
	std::uint8_t* stub = StubPage + StubUsed;
	StubUsed += 32;
	const auto         target = reinterpret_cast<std::uint64_t>(hook);
	const std::uint8_t jump[6] { 0xFF, 0x25, 0, 0, 0, 0 }; // jmp [rip]
	const std::uint8_t gameArg[3] { 0x4C, 0x89, 0xF2 };      // mov rdx, r14
	DWORD old = 0;
	if (!VirtualProtect(StubPage, 4096, PAGE_READWRITE, &old)) return false;
	std::size_t at = 0;
	if (gameInRdx) std::memcpy(stub, gameArg, 3), at = 3;
	std::memcpy(stub + at, jump, 6);
	std::memcpy(stub + at + 6, &target, 8);
	if (!VirtualProtect(StubPage, 4096, PAGE_EXECUTE_READ, &old)) return false;
	FlushInstructionCache(GetCurrentProcess(), StubPage, 4096);
	const auto from = static_cast<std::int64_t>(targetRva) - static_cast<std::int64_t>(siteRva + 5);
	const auto to   = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(stub)) - static_cast<std::int64_t>(site + 5);
	if (to < INT32_MIN || to > INT32_MAX) return false;
	std::uint8_t expected[5] { 0xE8 }, call[5] { 0xE8 };
	const auto   from32 = static_cast<std::int32_t>(from), to32 = static_cast<std::int32_t>(to);
	std::memcpy(expected + 1, &from32, 4);
	std::memcpy(call + 1, &to32, 4);
	return Context->PatchBytes(siteRva, expected, 5, call, 5);
}

bool Locate() noexcept {
	for (const Check& check : Calls) {
		std::uint8_t code[8] {};
		if (!Read(Base + check.rva, code, 8) || std::memcmp(code, check.bytes, 8) != 0)
			return Log(true, "game code differs at D2R+0x%llX", static_cast<unsigned long long>(check.rva)), false;
	}
	for (const std::uint64_t rva : { GetStatRva, SetStatRva }) {
		std::uint8_t code[2] {};
		if (!Read(Base + rva, code, 2) || code[0] != 0xFF || code[1] != 0x25)
			return Log(true, "game code differs at D2R+0x%llX", static_cast<unsigned long long>(rva)), false;
	}
	MonInit = At<MonInitFn>(MonInitRva);
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

int ModKey(const std::string& key) {
	for (int m = 0; m < ModCount; ++m)
		if (key == Mods[m].key) return m;
	return -1;
}

bool ApplySetting(const std::string& key, const std::string& value, std::string& error) {
	const auto flag = [&](std::atomic<bool>& target) {
		if (value != "true" && value != "false") return error = "true or false", false;
		target = value == "true";
		return true;
	};
	if (key == "enabled") return flag(Config.enabled);
	if (key == "nightmare") return flag(Config.nightmare);
	if (key == "hell") return flag(Config.hell);
	if (key == "show_mods") return flag(Config.show);
	if (const int m = ModKey(key); m >= 0) return flag(Config.allowed[m]);
	return error = "unknown setting", false;
}

std::string ValueOf(const std::string& key) {
	const auto flag = [](bool on) { return std::string(on ? "true" : "false"); };
	if (key == "enabled") return flag(Config.enabled);
	if (key == "nightmare") return flag(Config.nightmare);
	if (key == "hell") return flag(Config.hell);
	if (key == "show_mods") return flag(Config.show);
	if (const int m = ModKey(key); m >= 0) return flag(Config.allowed[m]);
	return {};
}

std::string Render() {
	std::string toml = R"(# TZ Plus
#
# Nightmare: one whole act is a terror zone, the act changing every 30 minutes.
# Hell: every level is a terror zone, up to level 99.
# The mods roll once per game. Changes apply from the next game.

[tz-plus]
enabled = )" + ValueOf("enabled") +
	                   "\nnightmare = " + ValueOf("nightmare") + "\nhell = " + ValueOf("hell") +
	                   "\n\n# The game's mods and rewards under \"Terrorized\" in the map's text.\nshow_mods = " + ValueOf("show_mods") +
	                   "\n\n# Mods that can roll\n";
	for (int m = 0; m < ModCount; ++m) toml += std::string(Mods[m].key) + " = " + ValueOf(Mods[m].key) + "\n";
	return toml;
}

void SaveConfig() {
	const std::string toml = Render();
	if (!Context->WriteConfig(toml.c_str())) Log(true, "config: could not write the config file");
}

void LoadConfig() {
	(void)Context->EnsureConfig();
	std::uint32_t     required = 0;
	std::vector<char> buffer(16384);
	if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) {
		if (required <= buffer.size()) return Log(true, "config: could not read the config file, using defaults");
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
		std::string error;
		const std::string key = Trim(std::string_view(line).substr(0, equals));
		if (!ApplySetting(key, Unquote(Trim(std::string_view(line).substr(equals + 1))), error))
			Log(true, "config: %s ignored (%s)", key.c_str(), error.c_str());
	}
}

// ---- I Hate TOMLs ----

std::string BuildSchema() {
	std::string schema = R"toml(title = "TZ Plus"
[[setting]]
key = "enabled"
type = "bool"
label = "TZ Plus"
note = "Applies from the next game."
[[setting]]
key = "nightmare"
type = "bool"
label = "Nightmare: a whole act"
note = "One act at a time, changing every 30 minutes. 1 to 2 mods, tier 1 to 2."
[[setting]]
key = "hell"
type = "bool"
label = "Hell: every level"
note = "Up to level 99. 1 to 3 mods, tier 1 to 3."
[[setting]]
key = "show_mods"
type = "bool"
label = "Show the mods on the map"
note = "The game's mods and rewards under \"Terrorized\" in the map's text."
[[setting]]
type = "header"
label = "Mods that can roll"
)toml";
	for (int m = 0; m < ModCount; ++m)
		schema += std::string("[[setting]]\nkey = \"") + Mods[m].key + "\"\ntype = \"bool\"\nlabel = \"" + Mods[m].name + (Mods[m].kind == Kind::Aura ? " aura" : "") +
		          "\"\n";
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
	if (!ApplySetting(key, Unquote(Trim(value)), reason)) {
		std::snprintf(error, size, "%s", reason.c_str());
		return false;
	}
	SaveConfig();
	return true;
}

// ---- on screen ----


const D2RL::LifecycleService* Lifecycle {};
std::atomic<bool>             InGame { false };
std::atomic<int>              CurrentLevel { -1 };

void __cdecl OnGameplayEvent(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent* event, void*) noexcept {
	using EventKind = D2RL::Lifecycle::GameplayEventKind;
	if (!D2RL::Lifecycle::HasGameplayEventField(event, D2RL::Lifecycle::GameplayEventRequiredSize)) return;
	switch (event->kind) {
	case EventKind::GameJoined: InGame = true, CurrentLevel = -1; break;
	case EventKind::GameLeft: {
		InGame = false, CurrentLevel = -1;
		AcquireSRWLockExclusive(&Lock); // the game is gone: a new one in the same memory rolls again
		Games.clear();
		Last = nullptr;
		ReleaseSRWLockExclusive(&Lock);
		break;
	}
	case EventKind::LevelChanged: CurrentLevel = event->currentValue; break;
	default: break;
	}
}

// The map's text (top right, with the map open): after its "Terrorized: <level>" line,
// D2R+0x1475410 calls Append(text, '\n'). We add our lines there.
using AppendCharFn = void* (*)(void* text, char c) noexcept;
constexpr std::uint64_t MapLineCallRva = 0x1475410, AppendCharRva = 0x1475050;

void AppendText(void* text, const std::string& line) noexcept {
	for (const char c : line) At<AppendCharFn>(AppendCharRva)(text, c);
	At<AppendCharFn>(AppendCharRva)(text, '\n');
}

void* HookMapLine(void* text, char c) noexcept {
	void* const result = At<AppendCharFn>(AppendCharRva)(text, c);
	if (!text || !Config.show || !Config.enabled || !InGame) return result;
	std::vector<std::string> lines;
	AcquireSRWLockShared(&Lock);
	if (const GameMods* g = Last; g && g->count > 0) {
		for (int i = 0; i < g->count; ++i) lines.push_back(std::string(Mods[g->mods[i].mod].name) + " " + Roman(g->mods[i].tier));
		char rewards[128];
		std::snprintf(rewards, sizeof(rewards), "+%d%% Gold Find, +%d%% Magic Find, +%d%% Experience", g->gold, g->magic, g->xp);
		lines.push_back(rewards);
	}
	ReleaseSRWLockShared(&Lock);
	for (const std::string& line : lines) AppendText(text, line);
	return result;
}

template <typename Service>
const Service* Query(const char* name) {
	const Service* service = nullptr;
	if (Context->QueryService(&service) != D2RL::ServiceQueryResult::Success) {
		Log(true, "%s service unavailable: the mods are not shown on screen", name);
		return nullptr;
	}
	return service;
}

void StartDisplay(const D2RL::PluginContext* context) noexcept {
	Lifecycle  = Query<D2RL::LifecycleService>("lifecycle");
	if (!Lifecycle) return;
	using EventKind = D2RL::Lifecycle::GameplayEventKind;
	for (const EventKind kind : { EventKind::GameJoined, EventKind::GameLeft, EventKind::LevelChanged }) {
		const D2RL::Lifecycle::GameplayEventListener listener {
			.structSize = D2RL::Lifecycle::GameplayEventListenerSize,
			.kind       = kind,
			.callback   = OnGameplayEvent,
		};
		D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
		if (Lifecycle->registerGameplayEventListener(context, &listener, &handle) != D2RL::Lifecycle::Result::Success)
			return Log(true, "could not listen to game events: the mods are not shown on the map");
	}
}

// ---- console: "tzplus" shows this game's mods ----

D2RL::ConsoleCommandResult __cdecl Command(D2R::Game::Client*, const D2RL::ConsoleCommandContext*, void*) noexcept {
	AcquireSRWLockShared(&Lock);
	std::string text = "TZ Plus: no Nightmare or Hell game yet";
	if (Last) {
		char head[96];
		std::snprintf(head, sizeof(head), "TZ Plus (%s, zone level %d, %d monsters): ", Last->difficulty == 2 ? "Hell" : "Nightmare", Last->zoneLevel,
		              Last->monsters.load());
		text = head + Describe(*Last);
	}
	ReleaseSRWLockShared(&Lock);
	Context->WriteConsoleMessage(text.c_str());
	return D2RL::ConsoleCommandResult::Handled;
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
	(void)context->RegisterConsoleCommand("tzplus", &Command, "Shows this game's TZ Plus mods and rewards.");
	StartDisplay(context);
	Base = context->exeBase;
	if (!Base || !Locate()) {
		Log(true, "TZ Plus %s: inactive, this game build isn't supported (Steam 3.4.95041 is)", PluginVersion);
		return true;
	}
	if (!context->InstallInlineHook(DescRva, DescBytes.data(), static_cast<std::uint32_t>(DescBytes.size()), &HookDesc, &Desc) || !Desc) {
		Log(true, "TZ Plus %s: inactive, could not hook D2R+0x%llX (another plugin may hook it)", PluginVersion, static_cast<unsigned long long>(DescRva));
		return true;
	}
	if (!context->InstallInlineHook(ApplyRva, ApplyBytes.data(), static_cast<std::uint32_t>(ApplyBytes.size()), &HookApply, &Apply) || !Apply) {
		Log(true, "TZ Plus %s: inactive, could not hook D2R+0x%llX (another plugin may hook it)", PluginVersion, static_cast<unsigned long long>(ApplyRva));
		return true;
	}
	if (!PatchCall(TerrorCheckCallRva, 0x3AF200, reinterpret_cast<const void*>(&HookTerrorCheck), true))
		Log(true, "TZ Plus %s: monster mods off, could not patch D2R+0x%llX", PluginVersion, static_cast<unsigned long long>(TerrorCheckCallRva));
	if (!PatchCall(MonInitCallRva, MonInitRva, reinterpret_cast<const void*>(&HookMonInit)) ||
	    !PatchCall(LevelUpCallRva, 0x49AEB0, reinterpret_cast<const void*>(&HookLevelUp)))
		Log(true, "TZ Plus %s: level 99 cap partly off, could not patch the monster level calls", PluginVersion);
	if (!PatchCall(MapLineCallRva, AppendCharRva, reinterpret_cast<const void*>(&HookMapLine)))
		Log(true, "TZ Plus %s: the mods are not shown on the map, could not patch D2R+0x%llX", PluginVersion, static_cast<unsigned long long>(MapLineCallRva));
	Log(false, "TZ Plus %s: %s (Nightmare %s, Hell %s)", PluginVersion, Config.enabled ? "active" : "turned off", Config.nightmare ? "on" : "off",
	    Config.hell ? "on" : "off");
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	Menu.Stop();
}
