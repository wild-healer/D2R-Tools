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

#include <map>
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
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <d2r_gui.h>
#include <d2r_gui/stash_window.h>

#include "i_hate_tomls_client.h"

namespace {

constexpr const char* PluginId      = "tz-plus";
constexpr const char* PluginVersion = "0.0.72";

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

enum class Kind : int { UMod, Aura, Tough, Vampiric, Regen, Poison };

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
	{ "poison_enchanted", "Poison Enchanted", Kind::Poison, 0, 10, 0, false },
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
              StatColdRes = 43, StatLifeSteal = 60, StatVelocity = 67, StatAttackRate = 68, StatRegen = 74, StatGoldFind = 79, StatMagicFind = 80, StatExperience = 85,
              StatPoisonMin = 57, StatPoisonMax = 58, StatPoisonLength = 59;
constexpr int PoisonPerLevel[3] { 2, 4, 6 }, PoisonFrames = 75;

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
constexpr int CurseLevelPct[3] { 50, 100, 150 }; // Amplify Damage level, % of the game's (monster level / 5 + 1)
constexpr int FastMove[3]     { 0, 15, 30 };  // % run/walk speed on top of the game's Extra Fast
constexpr int FastAttack[3]   { 0, 0, 10 };   // % attack speed

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
// Without the items and skills: what our rewards set.
std::int32_t GetBaseStat(void* unit, int stat) noexcept {
	return At<GetStatFn>(0x2F48C0)(unit, stat, 0);
}
void SetStat(void* unit, int stat, std::int32_t value) noexcept {
	At<SetStatFn>(SetStatRva)(unit, stat, value, 0);
}

// ---- game offsets ----

constexpr std::uintptr_t GameTerrorOn = 0x120, GameClients = 0x138, GameRefLevel = 0x102, GameDifficulty = 0x104, GameMode = 0x106, GameSeed = 0x1A8, GameUMod = 0x1A5C8,
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
	std::atomic<int>    zoneCount[4] {}; // monsters of the open TZ Plus zone: trash, champions, uniques, minions
	int                 zonesUsed = 0; // TZ Plus zones opened in this game
	void*               portal    = nullptr; // the open zone portal in Harrogath (checked by its id before use)
	int                 safeLevel = 0, safeX = 0, safeY = 0; // the zone's entry portal spot (log only)
	int                 zoneId = 0, level2Id = 0;            // the open zone and its "level 2" (one empty room)
	void*               exitPortal = nullptr;                // a single-level zone's portal to level 2 (its farthest room)
	bool                single = false;
	int                 exitX = 0, exitY = 0; // the zone's exit area (subtiles): the farthest room from the entry, 3 golden chests
	bool                exitDone = false;
	std::uint32_t       exitPortalId = 0;
	bool                roranDone = false; // Roran Stronghammer is in Harrogath
	int                 roranTries = 0;
	std::uint32_t       portalId  = 0;
	SRWLOCK             modLock = SRWLOCK_INIT;
	std::unordered_map<void*, std::uint32_t> modded; // monsters given our mods (unit -> unit id): not mercs or summons
	// The open zone's monsters, each counted once (under modLock): id -> kind (0 trash, 1 champion, 2 unique, 3 minion),
	// generated = made by the game, met = came within MeetRadius of a player, killed = met and then died.
	std::unordered_map<std::uint32_t, std::uint8_t> zoneMons;
	std::unordered_map<std::uint32_t, std::uint8_t> zoneMet; // id -> kind | 0x80 once killed
	int                 zoneGen[4] {}, zoneSeen[4] {}, zoneKilled[4] {};
	std::unordered_map<int, int> zoneClasses; // monster class -> generated
	std::uint64_t       meetTick = 0;
};

bool Modded(GameMods& g, void* unit) noexcept {
	AcquireSRWLockShared(&g.modLock);
	const auto it = g.modded.find(unit);
	const bool ok = it != g.modded.end() && it->second == Field<std::uint32_t>(unit, 8);
	ReleaseSRWLockShared(&g.modLock);
	return ok;
}

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
	for (const int id : { 9, 17, 18, 25 }) elem = std::max(elem, TierOf(g, id));
	for (const int row : { RowMinionElemMin + g.difficulty, RowMinionElemMax + g.difficulty }) {
		const std::int32_t base = Original(mode, row);
		SetRow(mode, row, elem ? base * ElemScale[elem - 1] / 100 : base);
	}
	SetRow(mode, RowCurse, Original(mode, RowCurse)); // the game's chance; the tier scales the curse level (HookCurse)
}

// Back to the game's values (a game without TZ Plus).
void RestoreConstants() noexcept {
	for (const Saved& saved : Originals)
		if (std::int32_t* at = RowValue(saved.mode, saved.row)) *at = saved.value;
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

// TZ Plus zones (see "TZ Plus zones" below): hidden act 5 levels added after the game's own, always terrorized.
std::atomic<int> ZoneBase { 0 }; // the first one's level id (the game's level count), 0 = none
constexpr int    ZoneSlots = 30; // ZoneParts levels a zone
// A zone's levels: its outdoor area, its dungeon, the dungeon's down stairs stand-in (sends you back outdoors), and two small
// outdoor areas joined to the outdoor one by the game's own passages: the entry (where the portal arrives) and the exit (chests).
constexpr int    ZoneParts = 5, KindMain = 0, KindDungeon = 1, KindStandIn = 2, KindEntry = 3, KindExit = 4;
constexpr int    SmallArea = 24; // tiles, the entry and exit areas' size (levels come in 8-tile cells; border pieces that don't fit are skipped, HookBorderPlace)
thread_local int  BuildingKind = -1;   // the kind of zone level being built (outdoor build steps skipped for the small areas)
thread_local bool OwnObject    = false; // our own objects (golden chests), let through the small areas' object block
using GetLevelFn  = void* (*)(std::uint8_t mode, void* drlg, int level) noexcept; // D2R+0x3267C0: the act's level (made if new)
using BuildLevelFn = std::uint64_t (*)(std::uint8_t mode, void* level) noexcept;  // D2R+0x3271C0: builds a level's rooms
std::atomic<std::uint32_t> ZoneSeed[ZoneSlots] {}; // each zone's layout seed, rolled when its portal opens (server and client build the same map)
std::atomic<int> ZoneTemplate[ZoneSlots] {}; // the template of each zone (0 = none, level 2s too): its stairs rooms (HookMazeBuild)
std::atomic<std::uint32_t> ZoneEntryBit[ZoneSlots] {}; // Room2 +0x50 flag of each zone's entry stairs room (0x10 << the template's Vis index)
std::atomic<bool> ZoneSingle[ZoneSlots] {};
std::atomic<bool> ZoneStandIn[ZoneSlots] {}; // arriving there sends you to Harrogath (the dungeon's down stairs)
// A zone's monsters, picked once for every bank (each bank's own pool differs, so picking per bank gave each its own monsters).
std::int16_t ForcedPicks[32] {};
int          ForcedCount = 0;
struct ZoneLink {
	std::int32_t from, to; // a template Vis target and what our copy's stairs lead to instead
}; // the template has one stairs room only: level 2 is reached by a portal in the farthest room
// Monsters made too close to a zone's entry portal, moved away on the next game frame (HookFrameCall).
struct SafeMove {
	std::uint8_t* game;
	void*         unit;
	std::uint32_t id;
};
SRWLOCK               SafeLock = SRWLOCK_INIT;
std::vector<SafeMove> SafeQueue;

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
	const int zoneBase = ZoneBase.load();
	if (zoneBase > 0) total += ZoneSlots;
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
	for (int i = 0; zoneBase > 0 && i < ZoneSlots; ++i, ++at) {
		std::memcpy(levels + at * LevelSize, realLevels, LevelSize);
		SetField<std::int32_t>(levels + at * LevelSize, 0, zoneBase + i);
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
int RewardLogs = 0; // reward diagnostics, per game

int GiveRewards(std::uint8_t* game, const GameMods& g, bool on, int level = -1) noexcept {
	int players = 0, given = 0;
	for (auto* client = Field<std::uint8_t*>(game, GameClients); client && players < 64; client = Field<std::uint8_t*>(client, ClientNext), ++players) {
		void* unit = ClientUnitOf(client);
		if (RewardLogs < 2)
			++RewardLogs, Log(false, "rewards: client %p (state %u, flags 0x%X) -> player %p", client, Field<std::uint32_t>(client, 4),
			                  Field<std::uint32_t>(client, ClientFlags), unit);
		if (!unit) continue;
		if (level >= 0) {
			void* room = At<UnitRoomFn>(0x34B440)(unit);
			if (!room || At<RoomLevelFn>(0x2EFC10)(room) != level) continue;
		}
		const std::int32_t gold = on ? g.gold : 0, magic = on ? g.magic : 0;
		const std::int32_t gold0 = GetBaseStat(unit, StatGoldFind), magic0 = GetBaseStat(unit, StatMagicFind);
		const std::int32_t total0 = GetStat(unit, StatGoldFind), totalMagic0 = GetStat(unit, StatMagicFind);
		if (gold0 != gold) SetStat(unit, StatGoldFind, gold);
		if (magic0 != magic) SetStat(unit, StatMagicFind, magic);
		if (GetBaseStat(unit, StatExperience) != (on ? g.xp : 0)) SetStat(unit, StatExperience, on ? g.xp : 0);
		if ((gold0 != gold || magic0 != magic) && RewardLogs < 24)
			++RewardLogs, Log(false, "rewards on server player %p: gold find %d -> %d (bonus %d -> %d), magic find %d -> %d (bonus %d -> %d)", unit, total0,
			                  GetStat(unit, StatGoldFind), gold0, gold, totalMagic0, GetStat(unit, StatMagicFind), magic0, magic);
		++given;
	}
	return given;
}

// ---- applying a zone to a game ----

std::uint64_t HookApply(std::uint8_t* game, std::uint8_t* zone, std::uint8_t flag) noexcept {
	if (!game || !zone || !flag || !Config.enabled.load(std::memory_order_relaxed)) return Apply(game, zone, flag);
	const int          difficulty = Field<std::uint8_t>(game, GameDifficulty);
	const std::uint8_t mode       = Field<std::uint8_t>(game, GameMode);
	if (mode < 2 || difficulty < 1 || difficulty > 2 || (difficulty == 1 && !Config.nightmare) || (difficulty == 2 && !Config.hell)) return Apply(game, zone, flag);
	// Terror zones turned off for this game (the game's own setting, checked first thing in Apply).
	if (Field<std::int32_t>(game, GameTerrorOn) == 0) return Apply(game, zone, flag);

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

	Applying.maxLevel = Ranges[difficulty].maxLevel;
	Applying.xp       = 0; // the experience bonus is a player stat (see GiveRewards)
	Applying.thread.store(GetCurrentThreadId());
	const std::uint64_t result = Apply(game, static_cast<std::uint8_t*>(ours ? ours : zone), flag);
	Applying.thread.store(0);
	// The game may still refuse terror zones (its other checks: quests, game type). Then no TZ Plus either.
	const auto* used = static_cast<const std::uint8_t*>(ours ? ours : zone);
	const auto* levels = Field<const std::uint8_t*>(used, 0x88);
	const int   first  = levels && Field<std::uint64_t>(used, 0x90) ? Field<std::int32_t>(levels, 0) : 0;
	if (first > 0 && !At<LevelTzFn>(0x489B70)(game, first)) {
		GiveRewards(game, *g, false);
		RestoreConstants();
		AcquireSRWLockExclusive(&Lock);
		for (auto it = Games.begin(); it != Games.end(); ++it)
			if (*it == g) {
				Games.erase(it);
				break;
			}
		if (Last == g) Last = nullptr;
		ReleaseSRWLockExclusive(&Lock);
		Log(false, "terror zones are off in this game: no TZ Plus mods or rewards");
		return result;
	}
	SetConstants(*g, mode);
	// Hell: every level counts. Nightmare: set as the players reach a level (see AddMods).
	// At game creation the player may not be in yet: AddMods gives them again.
	const int players = GiveRewards(game, *g, difficulty == 2);
	if (RewardLogs < 24) ++RewardLogs, Log(false, "rewards at zone change: %d player(s) found", players);
	return result;
}

// The 30 minute rotation (the broadcast threads D2R+0x405B30 and 0x4073C0, for each game: 0x488DF0 (game, 1) clears its terror
// zone, 0x488C70 (game) checks terror zones are on, then the new zone is applied and 0x4F22C0 tells the players). In a TZ Plus
// Hell game every level stays terrorized: the rotation is skipped there (no "terror has spread" message every half hour).
constexpr std::uint64_t RotationClearCalls[2] { 0x405CCD, 0x4074DD }, RotationCheckCalls[2] { 0x405CD5, 0x4074E5 };
constexpr std::uint64_t RotationClearRva = 0x488DF0, RotationCheckRva = 0x488C70;
thread_local const void* SkipRotation = nullptr;
std::atomic<int>         RotationLogs { 0 };
bool HellTzPlus(const std::uint8_t* game) noexcept {
	return game && Config.enabled.load(std::memory_order_relaxed) && Config.hell && Field<std::uint8_t>(game, GameDifficulty) == 2 && Field<std::uint8_t>(game, GameMode) >= 2 &&
	       ModsOf(game);
}
void HookRotationClear(std::uint8_t* game, std::uint8_t flag) noexcept {
	if (HellTzPlus(game)) {
		SkipRotation = game;
		if (RotationLogs.fetch_add(1, std::memory_order_relaxed) < 10) Log(false, "rotation skipped in a Hell game: every level stays terrorized");
		return;
	}
	At<void (*)(std::uint8_t*, std::uint8_t) noexcept>(RotationClearRva)(game, flag);
}
std::uint8_t HookRotationCheck(std::uint8_t* game) noexcept {
	if (game && SkipRotation == game) {
		SkipRotation = nullptr;
		return 0;
	}
	return At<std::uint8_t (*)(std::uint8_t*) noexcept>(RotationCheckRva)(game);
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
	// Multiple Shots: the game's check keeps normal monsters out; ours may have it (it acts on their missiles only).
	if (!record || (id != 29 && !At<UModAllowedFn>(0x4A1E50)(unit, record, 1))) return;

	int before[6] {};
	constexpr int watched[6] { StatFireRes, StatLightRes, StatColdRes, StatMagicRes, StatPhysRes, StatDefense };
	for (int i = 0; i < 6; ++i) before[i] = GetStat(unit, watched[i]);

	list[slot] = static_cast<std::uint8_t>(id);
	const auto start = *reinterpret_cast<UModStartFn*>(Base + UModTableRva + static_cast<std::uint64_t>(id) * 8);
	// Teleportation only gives the teleport skill to "unique" monsters (its start skips the others).
	if (start) start(unit, id, elite || id == 26 ? 1 : 0);

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
// Zone statistics, written when the zone portal closes: monster type flags (MonsterData +0x1A: 0x2 superunique,
// 0x4 champion, 0x8 unique, 0x10 minion) and the rooms the monsters spawned in. Room1 +0x18 = Room2, Room2 +0x90 = level.
struct ZoneStats {
	int                                  level = 0;
	std::map<std::uint16_t, int>         types;
	std::vector<std::uint8_t*>           rooms; // Room2s
	std::uint8_t*                        drlgLevel = nullptr;
};
SRWLOCK   StatsLock = SRWLOCK_INIT;
ZoneStats Stats;

std::string Hex(const std::uint8_t* p, std::size_t n) {
	std::string out;
	char        b[12];
	for (std::size_t i = 0; i < n; i += 4) {
		std::snprintf(b, sizeof(b), "%s%08X", i % 32 ? " " : (i ? " | " : ""), Field<std::uint32_t>(p, i));
		out += b;
	}
	return out;
}

void NoteZoneMonster(int level, std::uint16_t type, void* room) noexcept {
	AcquireSRWLockExclusive(&StatsLock);
	try {
		if (Stats.level != level) Stats = {}, Stats.level = level;
		++Stats.types[type];
		auto* room2 = room ? Field<std::uint8_t*>(room, 0x18) : nullptr;
		if (room2 && std::find(Stats.rooms.begin(), Stats.rooms.end(), room2) == Stats.rooms.end()) {
			Stats.rooms.push_back(room2);
			if (!Stats.drlgLevel) Stats.drlgLevel = Field<std::uint8_t*>(room2, 0x90);
			if (Stats.rooms.size() <= 4) Log(false, "zone level %d room2 %p: %s", level, room2, Hex(room2, 0xC0).c_str());
			if (Stats.rooms.size() == 1 && Stats.drlgLevel) Log(false, "zone level %d drlg level %p: %s", level, Stats.drlgLevel, Hex(Stats.drlgLevel, 0x240).c_str());
		}
	} catch (...) {}
	ReleaseSRWLockExclusive(&StatsLock);
}

constexpr int MeetRadius = 30; // subtiles (2/3 yard each): about the screen around you

void WriteZoneStats(GameMods& g, bool floor) noexcept {
	AcquireSRWLockExclusive(&StatsLock);
	try {
		if (Stats.level) {
			std::string types;
			for (const auto& [t, n] : Stats.types) {
				char item[32];
				std::snprintf(item, sizeof(item), "%s0x%X x%d", types.empty() ? "" : ", ", t, n);
				types += item;
			}
			AcquireSRWLockShared(&g.modLock);
			const auto line = [](const char* what, const int (&n)[4]) {
				Log(false, "  %s: %d monsters: %d trash, %d champions, %d uniques, %d minions", what, n[0] + n[1] + n[2] + n[3], n[0], n[1], n[2], n[3]);
			};
			Log(false, "zone level %d final stats (each monster once; met = came within %d yards of you):", Stats.level, MeetRadius * 2 / 3);
			line("generated", g.zoneGen);
			line("met", g.zoneSeen);
			line("killed", g.zoneKilled);
			{
				std::vector<std::pair<int, int>> classes(g.zoneClasses.begin(), g.zoneClasses.end());
				std::sort(classes.begin(), classes.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
				std::string text;
				for (const auto& [c, n] : classes) text += (text.empty() ? "" : ", ") + std::to_string(c) + " x" + std::to_string(n);
				Log(false, "  generated by monster class: %s", text.c_str());
			}
			ReleaseSRWLockShared(&g.modLock);
			Log(false, "zone level %d: type flags [%s]; %zu rooms with monsters", Stats.level, types.c_str(), Stats.rooms.size());
			if (std::uint8_t* level = floor ? Stats.drlgLevel : nullptr) {
				int rooms = 0;
				long long tiles = 0;
				std::string sizes;
				for (auto* r = Field<std::uint8_t*>(level, 0x10); r && rooms < 4096; r = Field<std::uint8_t*>(r, 0x48), ++rooms) {
					const int w = Field<std::int32_t>(r, 0x68), h = Field<std::int32_t>(r, 0x6C);
					tiles += static_cast<long long>(w) * h;
					if (rooms < 64) {
						char item[48];
						if (const std::uint32_t vis = Field<std::uint32_t>(r, 0x50) & 0xFF0)
							std::snprintf(item, sizeof(item), "%s%dx%d@%d,%d(stairs 0x%X)", sizes.empty() ? "" : " ", w, h, Field<std::int32_t>(r, 0x60), Field<std::int32_t>(r, 0x64), vis);
						else std::snprintf(item, sizeof(item), "%s%dx%d", sizes.empty() ? "" : " ", w, h);
						sizes += item;
					}
				}
				Log(false, "zone level %d floor: %d rooms (level says %d), %lld tiles = %lld subtiles; level box %dx%d tiles at %d,%d; rooms [%s]", Stats.level, rooms,
				    Field<std::int32_t>(level, 8), tiles, tiles * 25, Field<std::int32_t>(level, 0x2C), Field<std::int32_t>(level, 0x30), Field<std::int32_t>(level, 0x24),
				    Field<std::int32_t>(level, 0x28), sizes.c_str());
			}
		}
		Stats = {};
	} catch (...) {}
	ReleaseSRWLockExclusive(&StatsLock);
}

int  LevelListHas(int mode, int level, int monster, std::string& list) noexcept; // 1 yes, 0 no, -1 unknown (zones section)
std::atomic<int> VanillaSpawnLogs { 0 };

void ApplyMods(std::uint8_t* game, void* unit, void* room) noexcept {
	if (!game || !unit || !Config.enabled.load(std::memory_order_relaxed)) return;
	static int entries = 0; // first calls logged, to see where it stops
	const bool first = entries < 3;
	if (first) ++entries;
	GameMods* g = ModsOf(game);
	if (!g) {
		// Not the game pointer we saw in Apply: one game at a time here, so take the last one of that difficulty.
		AcquireSRWLockExclusive(&Lock);
		if (Last && Last->difficulty == Field<std::uint8_t>(game, GameDifficulty)) {
			if (first) Log(false, "monster init: game %p not seen in Apply (that was %p), using its mods", game, Last->game);
			Last->game = game;
			g          = Last;
		}
		ReleaseSRWLockExclusive(&Lock);
	}
	if (!g) {
		if (first) Log(false, "monster init: no TZ Plus game for %p (difficulty %d)", game, Field<std::uint8_t>(game, GameDifficulty));
		return;
	}
	if (!room) room = At<UnitRoomFn>(0x34B440)(unit);
	const int level = room ? At<RoomLevelFn>(0x2EFC10)(room) : 0;
	if (level <= 0) {
		if (first) Log(false, "monster init: no level (room %p)", room);
		return;
	}
	const bool terrorized = At<LevelTzFn>(0x489B70)(game, level);
	// Hell: game-wide. Nightmare: only one act is terrorized, so the rewards follow the level the players are in
	// (monsters, town people included, are created as a player reaches a level).
	const int players = GiveRewards(game, *g, g->difficulty == 2 || terrorized, g->difficulty == 2 ? -1 : level);
	if (first) Log(false, "monster init: level %d, terrorized %d, rewards on %d player(s)", level, terrorized ? 1 : 0, players);
	static int quiet = 0; // monsters outside terror zones logged so far
	static int tracedElites = 0;
	if (TracedGame != game) TracedGame = game, Traced = 0, quiet = 0, tracedElites = 0;
	bool trace = Traced < 25;
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
	if (elite && tracedElites < 15) ++tracedElites, trace = true;
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
		case Kind::Poison: {
			// Poison on the monster's attacks: mlvl x 2 / 4 / 6 damage over 3 seconds.
			const std::int64_t total = static_cast<std::int64_t>(std::max(1, GetStat(unit, StatLevel))) * PoisonPerLevel[tier - 1];
			const std::int32_t perFrame = static_cast<std::int32_t>(total * 256 / PoisonFrames); // 1/256 per frame
			SetStat(unit, StatPoisonMin, GetStat(unit, StatPoisonMin) + perFrame * 4 / 5);
			SetStat(unit, StatPoisonMax, GetStat(unit, StatPoisonMax) + perFrame * 6 / 5);
			SetStat(unit, StatPoisonLength, std::max(GetStat(unit, StatPoisonLength), PoisonFrames));
			break;
		}
		case Kind::Regen: {
			const std::int64_t life = GetStat(unit, StatMaxHp); // 1/256 points
			SetStat(unit, StatRegen, GetStat(unit, StatRegen) + static_cast<std::int32_t>(life * RegenPct[tier - 1] / 100 / 25));
			break;
		}
		}
	}
	CapLevel(unit);
	g->monsters.fetch_add(1, std::memory_order_relaxed);
	if (const int zb = ZoneBase.load(); zb > 0 && level >= zb && level < zb + ZoneSlots) {
		const int kind = (type & 0x10) ? 3 : (type & 0x04) ? 1 : (type & 0x0A) ? 2 : 0; // minion, champion (spawned 0x4|0x8), unique/superunique, trash
		AcquireSRWLockExclusive(&g->modLock);
		bool fresh = false;
		try {
			fresh = g->zoneMons.emplace(Field<std::uint32_t>(unit, 8), static_cast<std::uint8_t>(kind)).second;
		} catch (...) {}
		int total = 0;
		if (fresh) {
			++g->zoneGen[kind];
			try {
				++g->zoneClasses[static_cast<int>(Field<std::uint32_t>(unit, 4))];
			} catch (...) {}
			total = g->zoneGen[0] + g->zoneGen[1] + g->zoneGen[2] + g->zoneGen[3];
		}
		ReleaseSRWLockExclusive(&g->modLock);
		if (fresh) {
			g->zoneCount[kind].fetch_add(1, std::memory_order_relaxed);
			if (total % 50 == 0) Log(false, "zone level %d: %d monsters generated so far", level, total);
			NoteZoneMonster(level, type, room);
			{ // checked against the entry and exit areas on the next frame (the entry is known only once the portal is made)
				AcquireSRWLockExclusive(&SafeLock);
				try {
					SafeQueue.push_back({ game, unit, Field<std::uint32_t>(unit, 8) });
				} catch (...) {}
				ReleaseSRWLockExclusive(&SafeLock);
			}
		}
	} else if (g->zoneId && level > 0 && VanillaSpawnLogs.load(std::memory_order_relaxed) < 40) {
		// Diagnosis: monsters of vanilla levels after a zone portal opened, against the level's own lists.
		std::string list;
		const int   cls = static_cast<int>(Field<std::uint32_t>(unit, 4));
		const int   has = LevelListHas(Field<std::uint8_t>(game, GameMode), level, cls, list);
		VanillaSpawnLogs.fetch_add(1);
		Log(false, "vanilla spawn: level %d monster class %d type 0x%X: %s the level's lists [%s]", level, cls, type, has > 0 ? "IN" : has == 0 ? "NOT in" : "?", list.c_str());
	}
	AcquireSRWLockExclusive(&g->modLock);
	try {
		g->modded[unit] = Field<std::uint32_t>(unit, 8);
	} catch (...) {}
	ReleaseSRWLockExclusive(&g->modLock);
	if (trace) {
		++Traced;
		std::string mods;
		if (const std::uint8_t* list = At<UModListFn>(0x38E310)(unit))
			for (int i = 0; i < 9; ++i)
				if (list[i]) mods += (mods.empty() ? "" : ",") + std::to_string(list[i]);
		Trace("monster in level %d (mlvl %d, %s, type 0x%X): mods [%s], cold res %d -> %d, fire res %d -> %d, poison %d-%d/%d", level, GetStat(unit, StatLevel),
		      boss ? "boss" : elite ? "elite" : "normal", type, mods.c_str(), cold0, GetStat(unit, StatColdRes), fire0, GetStat(unit, StatFireRes),
		      GetStat(unit, StatPoisonMin), GetStat(unit, StatPoisonMax), GetStat(unit, StatPoisonLength));
	}
}

using TerrorCheckFn = bool (*)(void* unit) noexcept;
using LevelUpFn     = std::uint64_t (*)(void* unit, void* info, std::uint16_t flags) noexcept;

// 0x54375B: call 0x3AF200 (monster has the terror state?), reached by every monster.
// The stub passes the game (r14) and the room (the init's 2nd argument, saved at [rsp+0x30]).
// ---- champions, uniques, super uniques and minions ----
// Their spawn functions create a normal monster first (our hook above), then copy in their own mods
// (over ours) and set their type. So inside those functions we wait and add our mods when the outermost one returns.

struct Pending {
	std::uint8_t* game;
	void*         unit;
};
struct SpawnSlot {
	std::atomic<DWORD>   thread { 0 };
	int                  depth = 0;
	std::vector<Pending> pending;
};
SpawnSlot SpawnSlots[16];

SpawnSlot* SlotFor(DWORD thread, bool claim) noexcept {
	for (SpawnSlot& slot : SpawnSlots)
		if (slot.thread.load(std::memory_order_relaxed) == thread) return &slot;
	if (!claim) return nullptr;
	for (SpawnSlot& slot : SpawnSlots) {
		DWORD free = 0;
		if (slot.thread.compare_exchange_strong(free, thread)) return &slot;
	}
	return nullptr;
}

void AddMods(std::uint8_t* game, void* unit, void* room) noexcept {
	if (SpawnSlot* slot = SlotFor(GetCurrentThreadId(), false); slot && slot->depth > 0) {
		if (slot->pending.size() < 256) slot->pending.push_back({ game, unit });
		return;
	}
	ApplyMods(game, unit, room);
}

std::uint64_t SpawnOrig[9] {};
static int    GroupLogs = 0;

template <int I>
std::uint64_t HookSpawn(std::uint64_t a, std::uint64_t b, std::uint64_t c, std::uint64_t d, std::uint64_t e, std::uint64_t f, std::uint64_t g,
                        std::uint64_t h, std::uint64_t i, std::uint64_t j, std::uint64_t k, std::uint64_t l, std::uint64_t m, std::uint64_t n) noexcept {
	using Fn            = std::uint64_t (*)(std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t,
                                 std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t) noexcept;
	SpawnSlot* slot     = SlotFor(GetCurrentThreadId(), true);
	if (slot) ++slot->depth;
	const std::uint64_t result = reinterpret_cast<Fn>(SpawnOrig[I])(a, b, c, d, e, f, g, h, i, j, k, l, m, n);
	if (slot && --slot->depth == 0 && !slot->pending.empty()) {
		std::vector<Pending> units;
		units.swap(slot->pending);
		static int PackLogs = 0;
		if (I == 8 ? PackLogs++ < 20 : GroupLogs < 10) {
			if (I != 8) ++GroupLogs;
			Log(false, I == 8 ? "champion pack: %zu more champion(s) with the leader" : "group spawn %d: %zu monster(s), mods added now", I == 8 ? units.size() : static_cast<std::size_t>(I), units.size());
		}
		for (const Pending& p : units)
			if (Field<std::uint32_t>(p.unit, 0) == 1) ApplyMods(p.game, p.unit, nullptr); // still a monster
	}
	return result;
}

struct SpawnHook {
	std::uint64_t rva;
	Code          bytes;
	void*         hook;
};

// ---- Teleportation (TZ Plus mod only): a chance to blink away each time the monster is hit ----
// The game runs per-mod event handlers from a table (D2R+0x2396210, 6 per monster mod); event 4 is "was hit"
// (Lightning Enchanted's charged bolts). Teleportation has none: we add ours. The move is the game's own
// warp for the monster teleport skill (D2R+0x5A7510, skill 0xB8 to x, y; it checks the destination).
constexpr std::uint64_t TeleportHitSlotRva = 0x2396210 + (26 * 6 + 4) * 8, WarpRva = 0x5A7510;
constexpr int           TeleportChance[3] { 2, 5, 15 };
using WarpFn      = int (*)(void* game, void* unit, int skill, void* target, int x, int y) noexcept;
using CastFn      = int (*)(void* game, void* unit, int skill, void* target, int x, int y) noexcept;
constexpr std::uint64_t CastRva = 0x4A7CF0;
using PathCoordFn = int (*)(void* path) noexcept;
std::atomic<std::uint64_t> TeleportRandom { 0x9E3779B97F4A7C15ULL };

struct PendingTeleport {
	void*         unit;
	std::uint32_t id;
	std::uint8_t* game;
	std::uint64_t at;
};
SRWLOCK          TeleportLock = SRWLOCK_INIT;
PendingTeleport  Teleports[64] {};
std::atomic<int> PendingCount { 0 };

void TeleportWhenHit(std::uint8_t* game, void* unit, int, int hit) noexcept {
	if (!hit || !game || !unit || !Config.enabled.load(std::memory_order_relaxed)) return;
	GameMods* g = ModsOf(game);
	if (!g) return;
	int tier = TierOf(*g, 26);
	if (!tier) return; // a monster's own Teleportation: the game's behaviour only
	void* room = At<UnitRoomFn>(0x34B440)(unit);
	const int level = room ? At<RoomLevelFn>(0x2EFC10)(room) : 0;
	if (level <= 0 || !At<LevelTzFn>(0x489B70)(game, level) || !Modded(*g, unit)) return;
	if (At<IsSpecialFn>(0x38E0E0)(unit, true)) tier = std::min(tier + 1, Ranges[g->difficulty].maxTier);
	std::uint64_t state = TeleportRandom.fetch_add(0x9E3779B97F4A7C15ULL) ^ reinterpret_cast<std::uintptr_t>(unit);
	static int logs = 0;
	if (logs < 20) ++logs, Log(false, "Teleportation: monster %p was hit (tier %d)", unit, tier);
	if (static_cast<int>(Mix(state) % 100) >= TeleportChance[tier - 1]) return;
	// Being hit is no time to start a cast (the hit recovery takes over and the clients lose track), so
	// the teleport waits for the monster's next AI think, where its own AI would cast it (HookThink).
	AcquireSRWLockExclusive(&TeleportLock);
	PendingTeleport* slot = nullptr;
	for (PendingTeleport& p : Teleports)
		if (p.unit == unit || !p.unit || GetTickCount64() - p.at > 3000) {
			slot = &p;
			if (p.unit == unit) break;
		}
	if (slot) *slot = { unit, Field<std::uint32_t>(unit, 8), game, GetTickCount64() }, PendingCount.store(1, std::memory_order_relaxed);
	ReleaseSRWLockExclusive(&TeleportLock);
}

bool InTzPlusZone(std::uint8_t* game, void* unit, GameMods** mods) noexcept;

// D2R+0x4346F0 (game, 3, monster, level, duration, callback): Cursed's Amplify Damage around the monster,
// only called by the Cursed handler (0x4A0970). The tier scales the level the game picked.
constexpr std::uint64_t CurseRva = 0x4346F0;
constexpr Code          CurseBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89, 0x7C, 0x24, 0x18, 0x55 };
std::uint64_t           CurseOrig {};

std::uint64_t HookCurse(std::uint64_t a, std::uint64_t b, std::uint64_t c, std::uint64_t d, std::uint64_t e, std::uint64_t f, std::uint64_t g7,
                        std::uint64_t h, std::uint64_t i, std::uint64_t j, std::uint64_t k, std::uint64_t l, std::uint64_t m, std::uint64_t n) noexcept {
	using Fn = std::uint64_t (*)(std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t,
                                 std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t) noexcept;
	auto*     game = reinterpret_cast<std::uint8_t*>(a);
	void*     unit = reinterpret_cast<void*>(c);
	GameMods* mods = nullptr;
	if (InTzPlusZone(game, unit, &mods)) {
		if (int tier = TierOf(*mods, 7)) {
			if (At<IsSpecialFn>(0x38E0E0)(unit, true)) tier = std::min(tier + 1, Ranges[mods->difficulty].maxTier);
			const int before = static_cast<int>(d), after = std::clamp(before * CurseLevelPct[tier - 1] / 100, 1, 99);
			d = static_cast<std::uint64_t>(after);
			static int logs = 0;
			if (logs < 10) ++logs, Log(false, "Cursed: monster %p, tier %d, Amplify Damage level %d -> %d", unit, tier, before, after);
		}
	}
	return reinterpret_cast<Fn>(CurseOrig)(a, b, c, d, e, f, g7, h, i, j, k, l, m, n);
}

// D2R+0x4A2A00 (game, monster, ...): a monster's AI think.
constexpr std::uint64_t ThinkRva = 0x4A2A00;
constexpr Code          ThinkBytes { 0x48, 0x89, 0x54, 0x24, 0x10, 0x48, 0x89, 0x4C, 0x24, 0x08, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48 };
std::uint64_t           ThinkOrig {};

bool CastTeleport(std::uint8_t* game, void* unit) noexcept {
	void* path = Field<void*>(unit, 0x38);
	if (!path) return false;
	const std::uint32_t mode = Field<std::uint32_t>(unit, 0x0C);
	if (mode != 1 && mode != 2 && mode != 15) return false; // neutral, walking, running
	static int logs = 0;
	const int x = At<PathCoordFn>(0x341A20)(path), y = At<PathCoordFn>(0x341A30)(path);
	std::uint64_t state = TeleportRandom.fetch_add(0x9E3779B97F4A7C15ULL) ^ reinterpret_cast<std::uintptr_t>(unit);
	for (int attempt = 0; attempt < 6; ++attempt) {
		const double angle    = static_cast<double>(Mix(state) % 6283) / 1000.0;
		const int    distance = 6 + static_cast<int>(Mix(state) % 7);
		const int    tx = x + static_cast<int>(std::lround(std::cos(angle) * distance)), ty = y + static_cast<int>(std::lround(std::sin(angle) * distance));
		// 0x5A7510 checks the skill can land there; 0x4A7CF0 casts it (sequence mode, the monster's cast speed,
		// the skill's sound), what the monster AIs do. The skill (0x57D6D0) then moves it.
		if (!At<WarpFn>(WarpRva)(game, unit, 0xB8, nullptr, tx, ty)) continue;
		const int cast = At<CastFn>(CastRva)(game, unit, 0xB8, nullptr, tx, ty);
		if (logs < 40) ++logs, Log(false, "Teleportation: monster %p at %d,%d casts to %d,%d (%d)", unit, x, y, tx, ty, cast);
		return cast != 0;
	}
	if (logs < 40) ++logs, Log(false, "Teleportation: monster %p found no spot around %d,%d", unit, x, y);
	return false;
}

std::uint64_t HookThink(std::uint64_t a, std::uint64_t b, std::uint64_t c, std::uint64_t d, std::uint64_t e, std::uint64_t f, std::uint64_t g7,
                        std::uint64_t h, std::uint64_t i, std::uint64_t j, std::uint64_t k, std::uint64_t l, std::uint64_t m, std::uint64_t n) noexcept {
	using Fn  = std::uint64_t (*)(std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t,
                                 std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t) noexcept;
	void* unit = reinterpret_cast<void*>(b);
	bool  due  = false;
	if (unit && PendingCount.load(std::memory_order_relaxed)) {
		AcquireSRWLockExclusive(&TeleportLock);
		const std::uint32_t mode  = Field<std::uint32_t>(unit, 0x0C);
		const bool          ready = mode == 1 || mode == 2 || mode == 15; // a think during hit recovery keeps it waiting
		for (PendingTeleport& p : Teleports)
			if (p.unit == unit) {
				const bool fresh = p.game == reinterpret_cast<std::uint8_t*>(a) && p.id == Field<std::uint32_t>(unit, 8) && GetTickCount64() - p.at < 2000;
				if (!fresh || ready) due = fresh, p.unit = nullptr;
			}
		ReleaseSRWLockExclusive(&TeleportLock);
	}
	if (due && CastTeleport(reinterpret_cast<std::uint8_t*>(a), unit)) return 0;
	return reinterpret_cast<Fn>(ThinkOrig)(a, b, c, d, e, f, g7, h, i, j, k, l, m, n);
}

// ---- monster mod events (D2R+0x49D240: game, monster, other unit, event, ...) ----
// The game gives the mods' event handlers "is unique" (MonsterData flag 8) and most of them do nothing
// without it: Multiple Shots, Lightning Enchanted's bolts, our Teleportation... So for the monsters of a
// TZ Plus terror zone we raise that flag for the duration of the call.
constexpr std::uint64_t ModEventRva = 0x49D240;
constexpr Code          ModEventBytes { 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x48, 0x89, 0x54, 0x24, 0x10, 0x57 };
constexpr int           RangedPoisonDivisor = 3;
std::uint64_t           ModEventOrig {};
int                     MissileLogs = 0;

bool InTzPlusZone(std::uint8_t* game, void* unit, GameMods** mods = nullptr) noexcept {
	if (!game || !unit || Field<std::uint32_t>(unit, 0) != 1 || !Config.enabled.load(std::memory_order_relaxed)) return false;
	GameMods* g = ModsOf(game);
	if (!g) return false;
	void* room = At<UnitRoomFn>(0x34B440)(unit);
	const int level = room ? At<RoomLevelFn>(0x2EFC10)(room) : 0;
	if (level <= 0 || !At<LevelTzFn>(0x489B70)(game, level) || !Modded(*g, unit)) return false;
	if (mods) *mods = g;
	return true;
}

std::uint64_t HookModEvent(std::uint64_t a, std::uint64_t b, std::uint64_t c, std::uint64_t d, std::uint64_t e, std::uint64_t f, std::uint64_t g7,
                           std::uint64_t h, std::uint64_t i, std::uint64_t j, std::uint64_t k, std::uint64_t l, std::uint64_t m, std::uint64_t n) noexcept {
	using Fn      = std::uint64_t (*)(std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t,
                                 std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t) noexcept;
	auto* game    = reinterpret_cast<std::uint8_t*>(a);
	void* unit    = reinterpret_cast<void*>(b);
	GameMods* mods = nullptr;
	std::uint8_t* data = nullptr;
	bool          raised = false;
	if (InTzPlusZone(game, unit, &mods)) {
		static int eventLogs = 0;
		if (eventLogs < 40) {
			++eventLogs;
			std::string list;
			if (const std::uint8_t* umods = At<UModListFn>(0x38E310)(unit))
				for (int x = 0; x < 9; ++x)
					if (umods[x]) list += (list.empty() ? "" : ",") + std::to_string(umods[x]);
			const void* other = reinterpret_cast<void*>(c);
			Log(false, "mod event %u: monster %p [%s], other %p (type %d)", static_cast<std::uint32_t>(d), unit, list.c_str(), other,
			    other ? static_cast<int>(Field<std::uint32_t>(other, 0)) : -1);
		}
		data = Field<std::uint8_t*>(unit, 0x10); // MonsterData, +0x1A type flags
		if (data && !(Field<std::uint16_t>(data, 0x1A) & 8)) SetField<std::uint16_t>(data, 0x1A, Field<std::uint16_t>(data, 0x1A) | 8), raised = true;
	}
	const std::uint64_t result = reinterpret_cast<Fn>(ModEventOrig)(a, b, c, d, e, f, g7, h, i, j, k, l, m, n);
	if (raised) SetField<std::uint16_t>(data, 0x1A, Field<std::uint16_t>(data, 0x1A) & ~std::uint16_t { 8 });
	return result;
}

// Poison: 0x4518C0(attacker, defender, damage, length, ...) puts the poison state on the defender (called at 0x44D21A,
// 0x44D262, 0x44D380 from the hit code). A hit weaker than the poison already running is dropped by the game, so it
// doesn't even refresh it. In TZ Plus games every hit refreshes the duration and the strongest damage stays: never more
// time than one hit's length, never more damage than one hit. A Poison Enchanted monster's hits last its poison length.
constexpr std::uint64_t PoisonRva = 0x4518C0;
constexpr std::uint64_t PoisonCallRvas[] { 0x44D21A, 0x44D262, 0x44D380 };
using PoisonFn     = void (*)(void* attacker, void* defender, int damage, int length, int a5, int a6, int a7) noexcept;
using GameOfUnitFn = std::uint8_t* (*)(void* unit) noexcept;
using StateListFn  = void* (*)(void* unit, int state) noexcept;
using ListStatFn   = std::int32_t (*)(std::uint8_t mode, void* list, int stat, int layer) noexcept;
int PoisonLogs = 0;

void HookPoison(void* attacker, void* defender, int damage, int length, int a5, int a6, int a7) noexcept {
	if (attacker && defender && damage > 0 && length > 0 && Config.enabled.load(std::memory_order_relaxed)) {
		std::uint8_t* game = At<GameOfUnitFn>(0x48FF00)(attacker);
		if (game && ModsOf(game)) {
			const int asked = length, rolled = damage;
			GameMods* mods  = nullptr;
			if (InTzPlusZone(game, attacker, &mods) && TierOf(*mods, 0, Kind::Poison)) {
				const int own = GetStat(attacker, StatPoisonLength);
				if (own > 0 && length > own) length = own;
			}
			int old = 0;
			if (void* list = At<StateListFn>(0x2F5940)(defender, 2)) {
				old = -At<ListStatFn>(0x2F5DF0)(Field<std::uint8_t>(game, GameMode), list, 0x4A, 0);
				if (old > damage) damage = old; // a weaker hit refreshes the running poison
			}
			if (PoisonLogs < 30) {
				++PoisonLogs;
				Log(false, "poison on %s (attacker type %d): damage %d (rolled %d, running %d), length %d (asked %d)",
				    Field<std::uint32_t>(defender, 0) == 0 ? "player" : "monster", static_cast<int>(Field<std::uint32_t>(attacker, 0)), damage, rolled, old, length, asked);
			}
		}
	}
	At<PoisonFn>(PoisonRva)(attacker, defender, damage, length, a5, a6, a7);
}

// Missiles take another way: 0x499160(game, monster, missile) runs the mods' event 5 (Multiple Shots)
// from missile creation (call at 0x537B96), with the same "is unique" rule. We raise the flag there too,
// and give the missiles of a Poison Enchanted monster a third of its poison (missiles hit with their own stats).
constexpr std::uint64_t MissileEventCallRva = 0x537B96;
constexpr std::uint64_t MissileEventRva     = 0x499160;
using MissileEventFn = void (*)(void* game, void* monster, void* missile) noexcept;

void HookMissileEvent(std::uint8_t* game, void* monster, void* missile) noexcept {
	GameMods*     mods   = nullptr;
	std::uint8_t* data   = nullptr;
	bool          raised = false;
	const bool    ours   = InTzPlusZone(game, monster, &mods);
	if (ours) {
		data = Field<std::uint8_t*>(monster, 0x10);
		if (data && !(Field<std::uint16_t>(data, 0x1A) & 8)) SetField<std::uint16_t>(data, 0x1A, Field<std::uint16_t>(data, 0x1A) | 8), raised = true;
	}
	static thread_local int depth = 0;
	const bool extra = depth > 0; // created by a mod's handler (Multiple Shots) while the first missile is set up
	++depth;
	At<MissileEventFn>(MissileEventRva)(game, monster, missile);
	--depth;
	if (raised) SetField<std::uint16_t>(data, 0x1A, Field<std::uint16_t>(data, 0x1A) & ~std::uint16_t { 8 });
	if (!ours || !missile || Field<std::uint32_t>(missile, 0) != 3) return;
	const bool poison = TierOf(*mods, 0, Kind::Poison) != 0;
	if (poison) {
		const std::int32_t low = GetStat(monster, StatPoisonMin), high = GetStat(monster, StatPoisonMax), length = GetStat(monster, StatPoisonLength);
		if (high > 0) {
			SetStat(missile, StatPoisonMin, std::max(1, low / RangedPoisonDivisor));
			SetStat(missile, StatPoisonMax, std::max(1, high / RangedPoisonDivisor));
			SetStat(missile, StatPoisonLength, length);
		}
	}
	if (MissileLogs < 30) {
		++MissileLogs;
		std::string list;
		if (const std::uint8_t* umods = At<UModListFn>(0x38E310)(monster))
			for (int x = 0; x < 9; ++x)
				if (umods[x]) list += (list.empty() ? "" : ",") + std::to_string(umods[x]);
		Log(false, "missile %p of monster %p [%s]%s%s: poison %d-%d/%d", missile, monster, list.c_str(), raised ? " (unique flag raised)" : "", extra ? " (extra shot)" : "",
		    GetStat(missile, StatPoisonMin), GetStat(missile, StatPoisonMax), GetStat(missile, StatPoisonLength));
	}
}

// The game client runs its own copy of the mods' missile handlers: 0x1E0410(monster, other, event) with
// event 4 = "fired a missile", passing the same "is unique" flag. Its Multiple Shots (0x1E1630) draws the two
// extra missiles, so for the monsters of a TZ Plus zone that have it we raise the flag there as well.
constexpr std::uint64_t ClientModEventRva = 0x1E0410;
constexpr Code          ClientModEventBytes { 0x48, 0x89, 0x6C, 0x24, 0x18, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x49, 0x63 };
std::uint64_t           ClientModEventOrig {};
bool                    ClientZone(void* unit) noexcept;

std::uint64_t HookClientModEvent(std::uint64_t a, std::uint64_t b, std::uint64_t c, std::uint64_t d, std::uint64_t e, std::uint64_t f, std::uint64_t g7,
                                 std::uint64_t h, std::uint64_t i, std::uint64_t j, std::uint64_t k, std::uint64_t l, std::uint64_t m, std::uint64_t n) noexcept {
	using Fn = std::uint64_t (*)(std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t,
                                 std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t) noexcept;
	void*         unit   = reinterpret_cast<void*>(a);
	std::uint8_t* data   = nullptr;
	bool          raised = false;
	if (static_cast<std::uint32_t>(c) == 4 && unit && Field<std::uint32_t>(unit, 0) == 1 && Config.enabled.load(std::memory_order_relaxed)) {
		bool shots = false;
		if (const std::uint8_t* umods = At<UModListFn>(0x38E310)(unit))
			for (int x = 0; x < 9; ++x) shots = shots || umods[x] == 29;
		data = shots ? Field<std::uint8_t*>(unit, 0x10) : nullptr;
		if (data && !(Field<std::uint16_t>(data, 0x1A) & 8) && ClientZone(unit)) {
			SetField<std::uint16_t>(data, 0x1A, Field<std::uint16_t>(data, 0x1A) | 8), raised = true;
			static int logs = 0;
			if (logs < 10) ++logs, Log(false, "client: Multiple Shots for monster %p's missile %p", unit, reinterpret_cast<void*>(b));
		}
	}
	const std::uint64_t result = reinterpret_cast<Fn>(ClientModEventOrig)(a, b, c, d, e, f, g7, h, i, j, k, l, m, n);
	if (raised) SetField<std::uint16_t>(data, 0x1A, Field<std::uint16_t>(data, 0x1A) & ~std::uint16_t { 8 });
	return result;
}

bool HookTerrorCheck(void* unit, std::uint8_t* game, void* room) noexcept {
	AddMods(game, unit, room);
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
// prefix: instructions run first (to pass more arguments), at most 16 bytes.
bool PatchCall(std::uint64_t siteRva, std::uint64_t targetRva, const void* hook, std::initializer_list<std::uint8_t> prefix = {}) noexcept {
	const std::uintptr_t site = Base + siteRva;
	if (!StubPage) StubPage = static_cast<std::uint8_t*>(AllocNear(site, 4096));
	if (prefix.size() > 16 || !StubPage || StubUsed + 32 > 4096) return false;
	std::uint8_t* stub = StubPage + StubUsed;
	StubUsed += 32;
	const auto         target = reinterpret_cast<std::uint64_t>(hook);
	const std::uint8_t jump[6] { 0xFF, 0x25, 0, 0, 0, 0 }; // jmp [rip]
	DWORD old = 0;
	if (!VirtualProtect(StubPage, 4096, PAGE_READWRITE, &old)) return false;
	std::size_t at = 0;
	for (const std::uint8_t byte : prefix) stub[at++] = byte;
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

// Same as PatchCall for a tail "jmp target" (the hook returns straight to the caller's caller).
bool PatchJump(std::uint64_t siteRva, std::uint64_t targetRva, const void* hook) noexcept {
	const std::uintptr_t site = Base + siteRva;
	if (!StubPage) StubPage = static_cast<std::uint8_t*>(AllocNear(site, 4096));
	if (!StubPage || StubUsed + 32 > 4096) return false;
	std::uint8_t* stub = StubPage + StubUsed;
	StubUsed += 32;
	const auto         target = reinterpret_cast<std::uint64_t>(hook);
	const std::uint8_t jump[6] { 0xFF, 0x25, 0, 0, 0, 0 };
	DWORD old = 0;
	if (!VirtualProtect(StubPage, 4096, PAGE_READWRITE, &old)) return false;
	std::memcpy(stub, jump, 6);
	std::memcpy(stub + 6, &target, 8);
	if (!VirtualProtect(StubPage, 4096, PAGE_EXECUTE_READ, &old)) return false;
	FlushInstructionCache(GetCurrentProcess(), StubPage, 4096);
	const auto from = static_cast<std::int64_t>(targetRva) - static_cast<std::int64_t>(siteRva + 5);
	const auto to   = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(stub)) - static_cast<std::int64_t>(site + 5);
	if (to < INT32_MIN || to > INT32_MAX) return false;
	std::uint8_t expected[5] { 0xE9 }, jmp[5] { 0xE9 };
	const auto   from32 = static_cast<std::int32_t>(from), to32 = static_cast<std::int32_t>(to);
	std::memcpy(expected + 1, &from32, 4);
	std::memcpy(jmp + 1, &to32, 4);
	return Context->PatchBytes(siteRva, expected, 5, jmp, 5);
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
void ShowRewards(int level) noexcept;
void StartWindowHook() noexcept;
std::atomic<bool> ZonePortalOpen { false }; // this game has an open TZ Plus zone portal (the barbarian offers to close it)

void __cdecl OnGameplayEvent(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent* event, void*) noexcept {
	using EventKind = D2RL::Lifecycle::GameplayEventKind;
	if (!D2RL::Lifecycle::HasGameplayEventField(event, D2RL::Lifecycle::GameplayEventRequiredSize)) return;
	switch (event->kind) {
	case EventKind::GameJoined: InGame = true, CurrentLevel = -1; break;
	case EventKind::LocalPlayerReady: D2RGui::LeftPanel::InGame(true), StartWindowHook(); break;
	case EventKind::GameLeft: {
		InGame = false, CurrentLevel = -1, ZonePortalOpen = false;
		D2RGui::LeftPanel::InGame(false);
		AcquireSRWLockExclusive(&Lock); // the game is gone: a new one in the same memory rolls again
		for (GameMods* g : Games)
			if (g->zoneId) WriteZoneStats(*g, false); // a zone left open: its counts (its floor is gone with the game)
		Games.clear();
		Last       = nullptr;
		RewardLogs = 0;
		RestoreConstants();
		ReleaseSRWLockExclusive(&Lock);
		break;
	}
	case EventKind::LevelChanged: CurrentLevel = event->currentValue, ShowRewards(CurrentLevel); break;
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

// Our lines: one per mod, then the rewards.
std::vector<std::string> ModLines() noexcept {
	std::vector<std::string> lines;
	if (!Config.show || !Config.enabled || !InGame) return lines;
	AcquireSRWLockShared(&Lock);
	if (const GameMods* g = Last; g && g->count > 0) {
		for (int i = 0; i < g->count; ++i) lines.push_back(std::string(Mods[g->mods[i].mod].name) + " " + Roman(g->mods[i].tier));
		char line[64];
		std::snprintf(line, sizeof(line), "+%d%% Gold Find", g->gold), lines.push_back(line);
		std::snprintf(line, sizeof(line), "+%d%% Magic Find", g->magic), lines.push_back(line);
		std::snprintf(line, sizeof(line), "+%d%% Experience", g->xp), lines.push_back(line);
	}
	ReleaseSRWLockShared(&Lock);
	return lines;
}

// The game sets the rewards on the server's player; the client's copy of the player (what Advanced Stats reads)
// does not get these stats, so we set them there too.
using LocalIndexFn    = int (*)() noexcept;
using PlayerByIndexFn = void* (*)(int index) noexcept;
using InZoneListFn    = bool (*)(int level) noexcept; // the client's terror zone list
constexpr std::uint64_t InZoneListRva = 0x90D20;

void ShowRewards(int level) noexcept {
	if (!Config.enabled || !InGame) return;
	const int index = At<LocalIndexFn>(0x8B2D0)();
	if (index < 0 || index >= 8) return;
	void* player = At<PlayerByIndexFn>(0x9A480)(index);
	if (!player) return;
	int difficulty = 0, gold = 0, magic = 0, xp = 0;
	AcquireSRWLockShared(&Lock);
	if (const GameMods* g = Last) difficulty = g->difficulty, gold = g->gold, magic = g->magic, xp = g->xp;
	ReleaseSRWLockShared(&Lock);
	if (!difficulty) return;
	const bool on = difficulty == 2 || (level > 0 && At<InZoneListFn>(InZoneListRva)(level));
	const std::int32_t gold0 = GetBaseStat(player, StatGoldFind), magic0 = GetBaseStat(player, StatMagicFind);
	const std::int32_t total0 = GetStat(player, StatGoldFind), totalMagic0 = GetStat(player, StatMagicFind);
	if (gold0 != (on ? gold : 0)) SetStat(player, StatGoldFind, on ? gold : 0);
	if (magic0 != (on ? magic : 0)) SetStat(player, StatMagicFind, on ? magic : 0);
	if (GetBaseStat(player, StatExperience) != (on ? xp : 0)) SetStat(player, StatExperience, on ? xp : 0);
	static int logs = 0; // a change after the first one means something reset it
	if ((gold0 != (on ? gold : 0) || magic0 != (on ? magic : 0)) && logs < 30)
		++logs, Log(false, "rewards on client player %p (index %d, level %d): gold find %d -> %d (bonus %d), magic find %d -> %d (bonus %d)", player, index,
		            level, total0, GetStat(player, StatGoldFind), gold0, totalMagic0, GetStat(player, StatMagicFind), magic0);
}

void* HookMapLine(void* text, char c) noexcept {
	void* const result = At<AppendCharFn>(AppendCharRva)(text, c);
	ShowRewards(CurrentLevel);
	if (!text) return result;
	for (const std::string& line : ModLines()) AppendText(text, line);
	return result;
}

// Outside terror zones the map text goes to D2R+0x147542D: call InZoneList(level), the stub passes the text (r12).
// Towns get our lines there.
constexpr std::uint64_t TownCheckCallRva = 0x147542D;

bool HookTownCheck(int level, void* text) noexcept {
	const bool listed = At<InZoneListFn>(InZoneListRva)(level);
	ShowRewards(level);
	const bool town = level == 1 || level == 40 || level == 75 || level == 103 || level == 109;
	if (!listed && town && text)
		for (const std::string& line : ModLines()) AppendText(text, line);
	// Hell: every level is a terror zone, no list of them under the map (true = "this level is listed", the game adds none).
	AcquireSRWLockShared(&Lock);
	const bool hell = Last && Last->difficulty == 2 && Config.enabled.load() && Config.hell;
	ReleaseSRWLockShared(&Lock);
	return listed || hell;
}

// ---- monster names (the line of mods under the life bar) ----
// D2R+0x1DFAE0 writes the names of a 9-byte mod list in one color into a game string. Called at
// D2R+0xCF606 for normal monsters with the terror state (purple, color 0x1D) and at D2R+0x1DF7A5 for
// champions and uniques (gold, color 4). We let it write the mods that aren't ours, then add ours in
// purple with their tier, the stat mods and auras too.
struct GameString {
	char*         text;
	std::uint64_t size;
	std::uint64_t capacity; // top bit: the text is inside this object
	char          inside[0x118];
};
using UModNamesFn = GameString* (*)(GameString* out, std::uint8_t flag, const std::uint8_t* list, std::uint32_t count, std::int32_t color,
                                    const char* width) noexcept;
using AllocatorFn = void** (*)() noexcept;
constexpr std::uint64_t UModNamesRva = 0x1DFAE0, NormalNamesCallRva = 0xCF606, EliteNamesCallRva = 0x1DF7A5;
constexpr std::int32_t  Purple = 0x1D;

std::string Take(GameString& text) noexcept {
	std::string result(text.text ? text.text : "", text.text ? static_cast<std::size_t>(text.size) : 0);
	if (text.text && !(text.capacity & 0x8000000000000000ULL)) {
		void** allocator = At<AllocatorFn>(0x12063C0)();
		using FreeFn     = void (*)(void* self, void* memory) noexcept;
		reinterpret_cast<FreeFn>((*reinterpret_cast<void***>(allocator))[4])(allocator, text.text);
	}
	return result;
}

std::string NamesOf(std::uint8_t flag, std::initializer_list<std::uint8_t> ids, std::int32_t color) noexcept {
	std::uint8_t list[9] {};
	std::uint32_t n = 0;
	for (const std::uint8_t id : ids) list[n++] = id;
	GameString text {};
	At<UModNamesFn>(UModNamesRva)(&text, flag, list, n, color, nullptr);
	return Take(text);
}

struct NameParts {
	bool        ready = false;
	std::string color;     // the purple color code
	std::string separator; // between two mods
	std::string umod[45];  // purple names of the monster mods
} Names;

void LearnNames(std::uint8_t flag) noexcept {
	if (Names.ready) return;
	Names.ready         = true;
	const std::string a = NamesOf(flag, { 5 }, Purple), b = NamesOf(flag, { 8 }, Purple), ab = NamesOf(flag, { 5, 8 }, Purple);
	std::size_t common  = 0;
	while (common < a.size() && common < b.size() && a[common] == b[common]) ++common;
	Names.color             = a.substr(0, common);
	const std::string nameB = b.substr(common);
	Names.separator         = " ";
	if (ab.compare(0, a.size(), a) == 0) {
		const std::string rest = ab.substr(a.size());
		if (rest.size() >= b.size() && rest.compare(rest.size() - b.size(), b.size(), b) == 0) Names.separator = rest.substr(0, rest.size() - b.size());
		else if (rest.size() >= nameB.size() && rest.compare(rest.size() - nameB.size(), nameB.size(), nameB) == 0)
			Names.separator = rest.substr(0, rest.size() - nameB.size());
	}
	for (const Mod& mod : Mods)
		if (mod.kind == Kind::UMod && mod.id > 0 && mod.id < 45) Names.umod[mod.id] = NamesOf(flag, { static_cast<std::uint8_t>(mod.id) }, Purple);
	Log(false, "monster names: color code %zu byte(s), separator \"%s\"", Names.color.size(), Names.separator.c_str());
}

GameString* WriteNames(GameString* out, std::uint8_t flag, const std::uint8_t* list, std::uint32_t count, std::int32_t color, const char* width,
                       bool elite) noexcept {
	const auto original = At<UModNamesFn>(UModNamesRva);
	if (!out || !list || !Config.enabled || !InGame) return original(out, flag, list, count, color, width);
	struct Entry {
		int  mod;
		int  tier;
	};
	Entry entries[ModCount] {};
	int   n = 0, difficulty = 0;
	AcquireSRWLockShared(&Lock);
	if (const GameMods* g = Last) {
		difficulty = g->difficulty;
		for (int i = 0; i < g->count; ++i) entries[n++] = { g->mods[i].mod, g->mods[i].tier };
	}
	ReleaseSRWLockShared(&Lock);
	// Elites: only in terror zones (normal monsters only come here with the terror state).
	const int level = CurrentLevel;
	if (!n || (elite && !(level > 0 && At<InZoneListFn>(InZoneListRva)(level))))
		return original(out, flag, list, count, color, width);
	LearnNames(flag);
	std::uint8_t theirs[9] {};
	for (std::uint32_t i = 0; i < count && i < 9; ++i) {
		bool ours = false;
		for (int e = 0; e < n; ++e) ours |= Mods[entries[e].mod].kind == Kind::UMod && Mods[entries[e].mod].id == list[i];
		if (!ours) theirs[i] = list[i];
	}
	GameString* result = original(out, flag, theirs, count < 9 ? count : 9, color, width);
	const int   top    = Ranges[difficulty].maxTier;
	std::string more;
	for (int e = 0; e < n; ++e) {
		const Mod& mod  = Mods[entries[e].mod];
		const int  tier = elite ? std::min(entries[e].tier + 1, top) : entries[e].tier;
		std::string name = mod.kind == Kind::UMod && mod.id > 0 && mod.id < 45 && !Names.umod[mod.id].empty() ? Names.umod[mod.id] : Names.color + mod.name;
		if (!more.empty() || (result && result->size)) more += Names.separator;
		more += name + " " + Roman(tier);
	}
	if (result)
		for (const char c : more) At<AppendCharFn>(AppendCharRva)(result, c);
	return result;
}

// D2R+0xCF5AD: IsSpecial(unit, with minions) keeps champions, uniques and minions off the purple line.
// Minions have no line of their own: in a TZ Plus game they get the purple one (tiers as elites).
using IsSpecialCheckFn = bool (*)(void* unit, bool minions) noexcept;
constexpr std::uint64_t NameSpecialCallRva = 0xCF5AD, NameTerrorCallRva = 0xCF59B;

// D2R+0xCF59B: the purple line needs the terror state, which the game gives normal monsters only.
// In a TZ Plus terror zone, every monster passes (champions and uniques are still sent to their own line).
void NoteHover(void* unit) noexcept;

bool InClientZone(void* unit) noexcept {
	if (At<TerrorCheckFn>(0x3AF200)(unit)) return true;
	if (!Config.enabled || !InGame) return false;
	AcquireSRWLockShared(&Lock);
	const bool game = Last != nullptr;
	ReleaseSRWLockShared(&Lock);
	const int level = CurrentLevel;
	return game && level > 0 && At<InZoneListFn>(InZoneListRva)(level);
}
// The name bar of the monster under the mouse asks this (D2R+0xCF59B): that monster is the one hovered.
bool HookNameTerror(void* unit) noexcept {
	NoteHover(unit);
	return InClientZone(unit);
}
bool ClientZone(void* unit) noexcept { return InClientZone(unit); }
bool                    NameIsMinion       = false;

bool HookNameSpecial(void* unit, bool minions) noexcept {
	const auto special = At<IsSpecialCheckFn>(0x38E0E0);
	NameIsMinion       = false;
	const bool result  = special(unit, minions);
	if (!result || !minions || !Config.enabled || !InGame || special(unit, false)) return result;
	AcquireSRWLockShared(&Lock);
	const bool game = Last != nullptr;
	ReleaseSRWLockShared(&Lock);
	if (!game) return result;
	NameIsMinion = true;
	return false;
}

GameString* HookNormalNames(GameString* out, std::uint8_t flag, const std::uint8_t* list, std::uint32_t count, std::int32_t color, const char* width) noexcept {
	if (NameIsMinion) return NameIsMinion = false, WriteNames(out, flag, list, count, 4, width, true);
	return WriteNames(out, flag, list, count, color, width, false);
}
GameString* HookEliteNames(GameString* out, std::uint8_t flag, const std::uint8_t* list, std::uint32_t count, std::int32_t color, const char* width) noexcept {
	return WriteNames(out, flag, list, count, color, width, true);
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
	for (const EventKind kind : { EventKind::GameJoined, EventKind::GameLeft, EventKind::LevelChanged, EventKind::LocalPlayerReady }) {
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

// ---- the TZ Plus window (D2R GUI): this game's mods and rewards, and the settings ----
// Console "tzplus" or the Controls menu (TZ Plus > Open TZ Plus). Built like the game's left panels.

namespace Gui = D2RGui;

constexpr int PanelX = 236, PanelY = -651, PanelWidth = 1162, PanelHeight = 1507;
constexpr int Left = 105, Right = 1044, BarLeft = 1054, LineHeight = 56, RowHeight = 70, WheelStep = 120;

Gui::PanelPool    Window;
Gui::ScrollBar    Bar("mods", BarLeft, 30, 30, PanelX, PanelY);
std::atomic<bool> WindowOpen {};
int               Scroll = 0;

// This game, as lines (UI thread).
std::vector<std::string> GameLines() {
	std::vector<std::string> lines;
	AcquireSRWLockShared(&Lock);
	if (const GameMods* g = Last; g && InGame) {
		char head[96];
		if (g->difficulty == 2) std::snprintf(head, sizeof(head), "Hell, terror zone level %d", g->zoneLevel);
		else std::snprintf(head, sizeof(head), "Nightmare act %d, terror zone level %d", g->act.load(), g->zoneLevel);
		lines.push_back(head);
		for (int i = 0; i < g->count; ++i) lines.push_back(std::string(Mods[g->mods[i].mod].name) + " " + Roman(g->mods[i].tier));
		char line[64];
		std::snprintf(line, sizeof(line), "+%d%% Gold Find", g->gold), lines.push_back(line);
		std::snprintf(line, sizeof(line), "+%d%% Magic Find", g->magic), lines.push_back(line);
		std::snprintf(line, sizeof(line), "+%d%% Experience", g->xp), lines.push_back(line);
	} else lines.push_back(InGame ? "No TZ Plus mods in this game" : "Not in a game");
	ReleaseSRWLockShared(&Lock);
	return lines;
}

std::atomic<bool> TalkMode { false };
extern std::atomic<bool> PortalRequested, CloseRequested; // the window is the fire barbarian's menu

std::string BuildTalk(const std::string& panelName, std::string c) {
	int y = Gui::StashWindow::InnerY + 40;
	c += Gui::Text("TalkLine0", Left, y, Right - Left, LineHeight, "The fire shows me places full of terror.", Gui::Style::Label);
	y += LineHeight;
	c += Gui::Text("TalkLine1", Left, y, Right - Left, LineHeight, "I can open the way to one of them.", Gui::Style::Label);
	y += LineHeight + 40;
	const int width = Right - Left - 200;
	if (ZonePortalOpen.load())
		c += Gui::TextButton("TalkClose", Left + 100, y, width, 80, "Close the portal", "zone-close", "Closes the portal to this game's terror zone");
	else
		c += Gui::TextButton("TalkOpen", Left + 100, y, width, 80, "Open a portal to a terror zone", "zone-open",
		                     "A random dungeon with monsters from every act. The game's mods roll again");
	y += 110;
	c += Gui::TextButton("TalkLeave", Left + 100, y, width, 80, "Leave", "close");
	return Gui::StashWindow::Layout(panelName, "Barbarian", "close", c);
}

std::string BuildWindow(const std::string& panelName, const std::string& probe) {
	std::string c = Gui::Probe(probe);
	if (TalkMode.load()) return BuildTalk(panelName, c);

	int y = Gui::StashWindow::InnerY + 12;
	c += Gui::Text("GameTitle", Left, y, Right - Left, LineHeight, "This game", Gui::Style::Section);
	y += LineHeight;
	const auto lines = GameLines();
	for (std::size_t i = 0; i < lines.size(); ++i, y += LineHeight)
		c += Gui::Text("Game" + std::to_string(i), Left + 20, y, Right - Left - 20, LineHeight, lines[i],
		               i == 0 || i + 1 == lines.size() ? Gui::Style::Label : Gui::Style::SmallLabel);
	y += 20;

	c += Gui::Text("SettingsTitle", Left, y, Right - Left, LineHeight, "Settings (from the next game)", Gui::Style::Section);
	y += LineHeight;
	const struct {
		const char* key;
		const char* label;
		const char* tip;
	} options[] {
		{ "enabled", "TZ Plus", "Turns everything off when unchecked" },
		{ "nightmare", "Nightmare: a whole act", "One act at a time, changing every 30 minutes. 1 to 2 mods, tier 1 to 2" },
		{ "hell", "Hell: every level", "Up to level 99. 1 to 3 mods, tier 1 to 3" },
		{ "show_mods", "Show the mods on the map", "Under \"Terrorized\" in the map's text" },
	};
	int o = 0;
	for (const auto& option : options) {
		const std::string n = "Option" + std::to_string(o++);
		c += Gui::Text(n + "Label", Left, y, Right - Left - 100, RowHeight, option.label, Gui::Style::Label, option.tip);
		c += Gui::Check(n, Right - 60, y + 7, ValueOf(option.key) == "true", std::string("set:") + option.key, option.tip);
		y += RowHeight;
	}
	y += 20;

	c += Gui::Text("ModsTitle", Left, y, Right - Left, LineHeight, "Mods that can roll", Gui::Style::Section);
	y += LineHeight;
	const int listTop = y, listRoom = PanelHeight - 150 - listTop;
	const int total   = ModCount * RowHeight;
	Scroll            = std::clamp(Scroll, 0, std::max(0, total - listRoom));
	std::string inside;
	const int   first = Scroll / RowHeight, last = std::min(ModCount, (Scroll + listRoom) / RowHeight + 1);
	for (int m = first; m < last; ++m) {
		const int         top = m * RowHeight - Scroll;
		const std::string n   = "Row" + std::to_string(m - first);
		const Mod&        mod = Mods[m];
		inside += Gui::Image(n + "Back", Left, top + 3, Gui::Kit::Row(Right - Left, RowHeight - 6));
		inside += Gui::Text(n + "Name", Left + 30, top, Right - Left - 140, RowHeight, std::string(mod.name) + (mod.kind == Kind::Aura ? " aura" : ""), Gui::Style::Label);
		inside += Gui::Check(n + "Check", Right - 74, top + 7, Config.allowed[m].load(), std::string("set:") + mod.key);
	}
	c += Gui::Clip("List", 0, listTop, BarLeft - 6, listRoom, inside);
	c += Bar.Layout(listTop, listRoom, total, Scroll);
	return Gui::StashWindow::Layout(panelName, "TZ Plus", "close", c);
}

void CloseWindow() {
	Window.Close();
	WindowOpen = false;
	Gui::LeftPanel::Closed();
}

void WatchWindow() {
	const bool open = Window.IsOpen();
	WindowOpen      = open;
	if (Gui::LeftPanel::Watch(open, Window.Busy())) CloseWindow();
}

// ---- the fire barbarian (an Act 5 Combatant in Harrogath): a click on him opens his menu ----
using HoveredUnitFn = void* (*)(int index) noexcept;     // the unit under the mouse of a local player (D2R+0xF1900)
int TalkLogs = 0;

// The label of the unit under the mouse (D2R+0xC0420 (unit), called at 0x880AA5/0x880AE3/0x880B0C): the last one seen.
using LabelFn = std::uint64_t (*)(void* unit, std::uint64_t, std::uint64_t, std::uint64_t) noexcept;
constexpr std::uint64_t LabelRva = 0xC0420, LabelCalls[] { 0x880AA5, 0x880AE3, 0x880B0C };
std::atomic<void*>         LastLabel { nullptr };
std::atomic<std::uint64_t> LastLabelTick { 0 };
int                        LabelLogs = 0;
std::uint32_t              LabelLogged[32] {};

void NoteHover(void* unit) noexcept {
	if (unit) {
		LastLabel = unit, LastLabelTick = GetTickCount64();
		const std::uint32_t key = (Field<std::uint32_t>(unit, 0) << 16) | (Field<std::uint32_t>(unit, 4) & 0xFFFF);
		if (Field<std::uint32_t>(unit, 0) == 1 && LabelLogs < 32 && std::find(LabelLogged, LabelLogged + LabelLogs, key) == LabelLogged + LabelLogs)
			LabelLogged[LabelLogs++] = key, Log(false, "hover: monster class %u (unit %u) shows its name", Field<std::uint32_t>(unit, 4), Field<std::uint32_t>(unit, 8));
	}
}

std::uint64_t HookLabel(void* unit, std::uint64_t b, std::uint64_t c, std::uint64_t d) noexcept {
	NoteHover(unit);
	return At<LabelFn>(LabelRva)(unit, b, c, d);
}

// Diagnostics: every fire barbarian the client knows, with each check of the game's "can be selected" test
// (0x9B840: 0x34F8D0 must be 0, 0x971E0 player-vs-unit, unit flags +0x124 bit 1 targetable, flagsEx +0x128 0x80).
int BarbDumps = 0;
void DumpBarbarians() noexcept {
	using UnitFlagFn   = int (*)(void* unit) noexcept;
	using UnitFlagExFn = int (*)(void* unit, std::uint32_t mask) noexcept;
	using CanFn        = int (*)(void* player, void* unit, int skill) noexcept;
	using SelectFn     = int (*)(void* unit, int x, int y, int mode, int a5) noexcept;
	using ClientPlayer = void* (*)(int index) noexcept;
	using ModeFn       = std::uint8_t (*)(void* unit) noexcept;
	using Ms2Fn        = std::uint8_t* (*)(std::uint8_t mode, int monster) noexcept;
	using CoordsFn = void (*)(void* unit, void* coord) noexcept;
	static std::uint32_t seen[512] {};
	static int           seenCount = 0;
	if (BarbDumps >= 200) return;
	++BarbDumps;
	const int index  = At<LocalIndexFn>(0x8B2D0)();
	void*     player = At<ClientPlayer>(0x9A480)(index);
	std::int32_t at[2] {};
	if (player) At<CoordsFn>(0x34A040)(player, at);
	Log(false, "barb dump %d: player at %d,%d", BarbDumps, at[0], at[1]);
	try {
		for (const std::uint32_t type : { 1u, 2u, 5u }) { // monsters, objects, tiles
			auto* table = reinterpret_cast<std::uint8_t*>(Base + 0x2A23910 + type * 0x400); // client units: 128 lists (next +0x158)
			for (int bucket = 0; bucket < 128; ++bucket) {
				int guard = 0;
				for (auto* u = Field<std::uint8_t*>(table, bucket * 8); u && guard < 256; u = Field<std::uint8_t*>(u, 0x158), ++guard) {
					if (Field<std::uint32_t>(u, 0) != type) continue;
					const std::uint32_t cls = Field<std::uint32_t>(u, 4);
					const std::uint32_t id  = Field<std::uint32_t>(u, 8);
					const std::uint32_t key = type << 28 | (id & 0x0FFFFFFF);
					if (std::find(seen, seen + seenCount, key) != seen + seenCount || seenCount >= 512) continue;
					std::int32_t xy[2] {};
					At<CoordsFn>(0x34A040)(u, xy);
					if (player && (std::abs(xy[0] - at[0]) > 40 || std::abs(xy[1] - at[1]) > 40)) continue; // log it once it's near
					seen[seenCount++] = key;
					if (type != 1) {
						Log(false, "barb dump %d: %s %u class %u at %d,%d anim %u flags 0x%08X ex 0x%08X", BarbDumps, type == 2 ? "object" : "tile", id, cls, xy[0], xy[1],
						    Field<std::uint32_t>(u, 0xC), Field<std::uint32_t>(u, 0x124), Field<std::uint32_t>(u, 0x128));
						continue;
					}
					const std::uint8_t  mode = At<ModeFn>(0x34A0E0)(u);
					const std::uint8_t* ms2  = At<Ms2Fn>(0x975B0)(mode, static_cast<int>(cls));
					Log(false, "barb dump %d: unit %u class %u at %d,%d anim %u flags 0x%08X ex 0x%08X | bank %u monstats2 flags 0x%X | 34F8D0 %d targetable %d ex80 %d canInteract %d selectable %d",
					    BarbDumps, id, cls, xy[0], xy[1], Field<std::uint32_t>(u, 0xC), Field<std::uint32_t>(u, 0x124), Field<std::uint32_t>(u, 0x128), mode,
					    ms2 ? Field<std::uint32_t>(ms2, 4) : 0xFFFFFFFFu, At<UnitFlagFn>(0x34F8D0)(u), At<UnitFlagFn>(0x3500E0)(u), At<UnitFlagExFn>(0x34FB00)(u, 0x80),
					    player ? At<CanFn>(0x971E0)(player, u, -1) : -1, At<SelectFn>(0x9B840)(u, 0, 0, -1, -1));
				}
			}
		}
	} catch (...) {
		Log(true, "barb dump: failed while reading the client units");
	}
}

bool OnGameMessage(UINT message, WPARAM, LPARAM) {
	if (message != WM_LBUTTONDOWN || !InGame.load()) return false;
	const int index = At<LocalIndexFn>(0x8B2D0)();
	void*     unit  = index >= 0 && index < 8 ? At<HoveredUnitFn>(0xF1900)(index) : nullptr;
	if (!unit && GetTickCount64() - LastLabelTick.load() < 400) unit = LastLabel.load(); // named a moment ago
	if (TalkLogs < 20 && CurrentLevel.load() == 109)
		++TalkLogs, Log(false, "click: level %d, player %d, unit under the mouse %p (type %d, class %d, id %u)", CurrentLevel.load(), index, unit,
		                unit ? static_cast<int>(Field<std::uint32_t>(unit, 0)) : -1, unit ? static_cast<int>(Field<std::uint32_t>(unit, 4)) : -1,
		                unit ? Field<std::uint32_t>(unit, 8) : 0u);
	if (CurrentLevel.load() == 109) DumpBarbarians();
	if (CurrentLevel.load() != 109 || !unit || Field<std::uint32_t>(unit, 0) != 1) return false;
	const std::uint32_t cls = Field<std::uint32_t>(unit, 4);
	if (cls != 534) return false; // act5pow: Roran Stronghammer
	Gui::Commands::Post("talk");
	return false; // the click goes on: you walk to him
}

void StartWindowHook() noexcept {
	static std::atomic<bool> once { false };
	if (!once.exchange(true)) Log(false, "window hook started (clicks on the fire barbarians watched)");
	Gui::WindowHook::Start({
	    .active = [] { return WindowOpen.load(); },
	    .wheel  = [](int notches) {
            Scroll = std::clamp(Scroll + notches * WheelStep, 0, std::max(0, Bar.Max()));
            Window.Show();
	    },
	    .extra = OnGameMessage,
	    .watch = WatchWindow,
	});
}

void OpenWindow(bool talk) {
	TalkMode = talk;
	Gui::Font::Refresh();
	StartWindowHook();
	static bool attached = false;
	if (!attached) Bar.Attach(), attached = true;
	if (!Window.IsOpen()) Gui::LeftPanel::Opening();
	Window.Show();
	WindowOpen = true;
}

void ToggleWindow() {
	if (Window.IsOpen() && !TalkMode.load()) return CloseWindow();
	OpenWindow(false);
}

void OnWindowCommand(const std::string& command) {
	if (command == "toggle") return ToggleWindow();
	if (command == "talk") return OpenWindow(true);
	if (command == "zone-open" || command == "zone-close") {
		if (command == "zone-open") PortalRequested = true;
		else CloseRequested = true;
		return CloseWindow();
	}
	if (!Window.IsOpen() && command != "close") return;
	if (Bar.Handle(command)) return;
	if (command == "close") return CloseWindow();
	if (command.rfind("set:", 0) == 0) {
		const std::string key = command.substr(4);
		std::string       error;
		if (ApplySetting(key, ValueOf(key) == "true" ? "false" : "true", error)) SaveConfig();
	}
	Window.Show();
}

void __cdecl WindowDataLoaded(const D2RL::PluginContext*, const D2RL::Lifecycle::DataTablesLoadedEvent*, void*) noexcept {
	static bool done = false;
	if (done) return;
	done = true;
	Gui::Kit::Read();
	Gui::Font::Measure();
}

auto OnOpenAction(const D2RL::PluginContext*, const D2RL::Input::ActionEvent* event, void*) noexcept -> D2RL::Input::ActionResult {
	if (!D2RL::Input::HasActionEventField(event, D2RL::Input::ActionEventRequiredSize) || event->kind != D2RL::Input::ActionEventKind::Pressed)
		return D2RL::Input::ActionResult::Ignored;
	Gui::Commands::Post("toggle");
	return D2RL::Input::ActionResult::Handled;
}

void StartWindow(const D2RL::PluginContext* context) noexcept {
	if (!Gui::Init(context, PluginId)) return Log(true, "the TZ Plus window is off: a service it needs is missing");
	Window.Register(BuildWindow, "TZPlus", 16);
	Bar.OnScroll([](int scroll) { Scroll = scroll, Window.Show(); });
	Gui::Commands::Listen(OnWindowCommand);
	if (Lifecycle) {
		D2RL::Lifecycle::ListenerHandle                 handle {};
		const D2RL::Lifecycle::DataTablesLoadedListener loaded { .structSize = sizeof(D2RL::Lifecycle::DataTablesLoadedListener), .callback = WindowDataLoaded };
		(void)Lifecycle->registerDataTablesLoadedListener(context, &loaded, &handle);
	}
	if (const auto* input = Gui::Detail::Query<D2RL::InputService>(context, "input"); input && D2RL::HasInputServiceField(input, D2RL::InputServiceRequiredSize)) {
		const D2RL::Input::ActionRegistration action {
			.structSize       = D2RL::Input::ActionRegistrationSize,
			.logicalId        = "open-tz-plus",
			.displayName      = "Open TZ Plus",
			.category         = "TZ Plus",
			.defaultPrimary   = { .key = D2RL::Input::Key::None },
			.defaultSecondary = { .key = D2RL::Input::Key::None },
			.callback         = OnOpenAction,
		};
		D2RL::Input::ActionHandle handle = D2RL::Input::InvalidHandle;
		(void)input->registerAction(context, &action, &handle);
	}
}

// ---- TZ Plus zones: hidden act 5 levels added to the game's level tables ----
// When the tables are loaded, ZoneSlots levels are added after the game's own (ids 138+), in our own copies of the
// Levels, LevelDefs and LvlMaze rows (the game reads them by id: Levels/LevelDefs by index, LvlMaze by its Level field).
// Each portal takes the next unused one in the game and turns it into a copy of a random vanilla dungeon with random
// monsters from every act. No stairs (Vis/Warp cleared), no quest, no waypoint, act 5 (Pandemonium levels are made this way).

constexpr std::uint64_t  TablesRva = 0x2A9A580; // the data tables of each game mode (16 bytes each), see D2R+0x300A90
constexpr std::uintptr_t LevelsVec = 0x1460, DefsVec = 0x14A0, MazeVec = 0x18C0; // {rows, count, capacity}
constexpr std::size_t    LevelRow = 0x18C, DefRow = 0x9C, MazeRow = 0x1C;
constexpr std::uintptr_t LvId = 4, LvPal = 12, LvAct = 13, LvQuest = 56, LvNumMon = 58, LvMonCount = 59, LvMon = 62, LvNMon = 112, LvUMon = 162,
                         LvWaypoint = 236, LvName = 253, LvWarpName = 293, LvEntryName = 333, LvDen = 36;
constexpr int            ZoneDensity = 3000; // fallback density (a template without its own)
// A zone's density is its dungeon's own (Levels MonDen: Pit 600, Tower Cellar 1024, Stony Tomb 2200: tombs have more walls, so the
// game gives them more spawn tries for the same monsters per room) x DensityScale %, then scaled by the picks' group sizes.
constexpr int            DensityScale = 125;
constexpr int            DungeonDensity = 150; // a zone's dungeon vs its outdoor area
constexpr int            OutdoorElites = 150, DungeonElites = 200; // elite packs vs the template level's own (%)
constexpr int            GroupRef = 25;      // x10: the average group (with minions) ZoneDensity is tuned for; zones of bigger groups get fewer of them
constexpr std::uintptr_t MonStatsVec = 0xF58; // MonStats {rows, count}: row 0x1FC, minion1 word +0x5A, PartyMin/Max +0x62/+0x63, MinGrp/MaxGrp +0x65/+0x66
constexpr std::size_t    MonStatsRow = 0x1FC;
constexpr int            ZoneArea    = 2304; // map size of every zone: rooms x room width x room height (Pit, Crypt, Cave in Hell)
constexpr int            SafeRadius  = 30;   // no monster spawns this close to the zone's entry portal
constexpr std::uintptr_t DefSizeX = 12, DefSizeY = 24;
constexpr std::uintptr_t DefQuest = 0, DefQuestEx = 4, DefLayer = 8, DefDrlg = 48, DefType = 52, DefDepend = 44, DefVis = 72, DefWarp = 104;
constexpr int            MonSlots = 25, ZoneMonsters = 8;
// Automap layers of our levels: past the game's 0-99 (the character's .map file has a 100-entry table, HookLayerSave/Load keep
// ours out of it), a new number for each zone opened so an old zone's map never shows up again.
std::atomic<int> NextLayer { 1000 };
std::atomic<bool> LayersSafe { false }; // our layer numbers only once the .map file hooks are in
std::atomic<int> ZoneLayer[30] {};
// Generic dungeons (no level-specific generation code, no quest): cave, crypt, jail, catacombs, tombs, lair, sewers,
// spider caves, swamp/flayer dungeons, Abaddon.
constexpr int ZoneTemplates[] { 9, 11, 12, 18, 21, 29, 34, 55, 56, 58, 62, 65, 84, 86, 88, 125 };
struct ZonePair {
	int outdoor, dungeon;
};
constexpr ZonePair ZonePairs[] { { 3, 9 } }; // Cold Plains + Cave (first test pair)
constexpr int Harrogath = 109;

struct ZoneBank {
	std::uint8_t* tables  = nullptr;
	std::uint8_t* levels  = nullptr; // our copies: the game's rows, then ours
	std::uint8_t* defs    = nullptr;
	std::uint8_t* maze    = nullptr;
	int           count   = 0; // the game's levels
	std::uint64_t mazes   = 0; // the game's maze rows
	std::vector<std::int16_t> pool; // every monster of the game's level lists (Nightmare/Hell lists)
};
ZoneBank Banks[4];

using GetCoordsFn    = void (*)(void* unit, void* coord) noexcept;
using FindFreeSpotFn = std::uint64_t (*)(void* game, void* room, void* coord, int size, int mask, void** outRoom, int a7, int a8) noexcept;
using CreatePortalFn = int (*)(void* game, void* player, void* room, int x, int y, int destLevel, void** outPortal, int objectClass, int a9, int a10) noexcept;
using ClientPlayerFn = void* (*)(void* client, int flag) noexcept;
using FrameCallFn    = std::uint64_t (*)(void* game) noexcept;
constexpr std::uint64_t FrameCallRva = 0x40660B, FrameCallTarget = 0x404330; // UpdateGameFrame 0x406550: call 0x404330 (game), once a frame

struct Coord {
	std::int32_t x, y;
};

// A table vector replaced by our copy with `extra` more rows (the game keeps reading it in place; capacity marked not owned).
std::uint8_t* GrowVector(std::uint8_t* tables, std::uintptr_t at, std::size_t rowSize, std::uint64_t extra, std::uint64_t& count) noexcept {
	auto* rows = Field<std::uint8_t*>(tables, at);
	count      = Field<std::uint64_t>(tables, at + 8);
	if (!rows || count == 0 || count > 0x10000) return nullptr;
	auto* copy = static_cast<std::uint8_t*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (count + extra) * rowSize));
	if (!copy) return nullptr;
	std::memcpy(copy, rows, count * rowSize);
	for (std::uint64_t e = 0; e < extra; ++e) std::memcpy(copy + (count + e) * rowSize, rows + (count - 1) * rowSize, rowSize);
	SetField<std::uint8_t*>(tables, at, copy);
	SetField<std::uint64_t>(tables, at + 16, (count + extra) | NotOnHeap);
	SetField<std::uint64_t>(tables, at + 8, count + extra);
	return copy;
}

std::uint8_t* MazeOf(const ZoneBank& b, int level) noexcept {
	for (std::uint64_t i = 0; i < b.mazes; ++i)
		if (Field<std::int32_t>(b.maze + i * MazeRow, 0) == level) return b.maze + i * MazeRow;
	return nullptr;
}

std::string LevelName(const ZoneBank& b, int level) {
	char name[41] {};
	std::memcpy(name, b.levels + level * LevelRow + LvName, 40);
	return name;
}

int LevelListHas(int mode, int level, int monster, std::string& list) noexcept {
	if (mode < 1 || mode > 3 || !Banks[mode].levels || level <= 0 || level >= Banks[mode].count + ZoneSlots) return -1;
	const std::uint8_t* row = Banks[mode].levels + level * LevelRow;
	int                 has = 0;
	try {
		for (const std::uintptr_t at : { LvMon, LvNMon, LvUMon }) {
			list += at == LvMon ? "mon:" : at == LvNMon ? " nmon:" : " umon:";
			for (int i = 0; i < MonSlots; ++i) {
				const auto m = Field<std::int16_t>(row, at + i * 2);
				if (m < 0) break;
				list += std::to_string(m) + ",";
				if (m == monster) has = 1;
			}
		}
	} catch (...) {}
	return has;
}

// Level `id` (a TZ Plus zone) becomes a copy of `tmpl` with random monsters.
// vis0/vis1: the levels the entry and exit rooms lead to (Warp -1: rooms without stairs; a portal arrives in the
// entry room, D2R+0x3DA650). empty: no monsters, one room (the zone's "level 2").
void SetupZone(ZoneBank& b, int id, int tmpl, std::uint64_t& state, int vis0 = 0, int vis1 = 0, bool empty = false, const ZoneLink* links = nullptr,
               int linkCount = 0, std::int32_t entryFrom = 0) noexcept {
	if (!b.levels || id < b.count || id >= b.count + ZoneSlots || tmpl <= 0 || tmpl >= b.count) return;
	const int zoneBase = ZoneBase.load(), slot = zoneBase > 0 ? id - zoneBase : id - b.count; // the banks' level counts can differ
	if (slot < 0 || slot >= ZoneSlots) return;
	std::uint8_t* row = b.levels + id * LevelRow;
	std::memcpy(row, b.levels + tmpl * LevelRow, LevelRow);
	SetField<std::uint16_t>(row, LvId, static_cast<std::uint16_t>(id));
	SetField<std::uint8_t>(row, LvAct, 4);
	SetField<std::uint8_t>(row, LvQuest, 0);
	SetField<std::uint8_t>(row, LvWaypoint, 0xFF);
	std::int32_t baseDen[3] {};
	for (int i = 0; i < 3; ++i) {
		const std::int32_t own = Field<std::int32_t>(row, LvDen + i * 4);
		baseDen[i] = (own > 0 ? own : ZoneDensity) * DensityScale / 100;
		SetField<std::int32_t>(row, LvDen + i * 4, baseDen[i]);
	}
	// Our names: keys answered by HookStringByKey.
	const int n = slot + 1;
	char      key[40] {};
	std::snprintf(key, sizeof(key), "TZPlusZone%d", n);
	std::memcpy(row + LvName, key, 40);
	std::snprintf(key, sizeof(key), "TZPlusWarp%d", n);
	std::memcpy(row + LvWarpName, key, 40);
	std::snprintf(key, sizeof(key), "TZPlusEntry%d", n);
	std::memcpy(row + LvEntryName, key, 40);
	if (!b.pool.empty()) {
		std::int16_t picks[MonSlots];
		int          count = 0;
		if (ForcedCount > 0)
			for (int i = 0; i < ForcedCount && count < MonSlots; ++i) picks[count++] = ForcedPicks[i];
		else
			for (int tries = 0; count < ZoneMonsters && count < static_cast<int>(b.pool.size()) && tries < 200; ++tries) {
				const std::int16_t m = b.pool[Mix(state) % b.pool.size()];
				if (std::find(picks, picks + count, m) == picks + count) picks[count++] = m;
			}
		for (const std::uintptr_t list : { LvMon, LvNMon, LvUMon })
			for (int i = 0; i < MonSlots; ++i) SetField<std::int16_t>(row, list + i * 2, i < count ? picks[i] : static_cast<std::int16_t>(-1));
		for (int i = 0; i < 3; ++i) SetField<std::uint8_t>(row, LvMonCount + i, static_cast<std::uint8_t>(count));
		// The same number of monsters in every zone: density scaled by the average group size of the picks.
		const auto* stats = Field<std::uint8_t*>(b.tables, MonStatsVec);
		const auto  total = Field<std::uint64_t>(b.tables, MonStatsVec + 8);
		int         sum = 0, counted = 0;
		for (int i = 0; stats && i < count; ++i) {
			if (picks[i] < 0 || static_cast<std::uint64_t>(picks[i]) >= total) continue;
			const std::uint8_t* m     = stats + picks[i] * MonStatsRow;
			const int           lo    = Field<std::uint8_t>(m, 0x65), hi = std::max<int>(lo, Field<std::uint8_t>(m, 0x66));
			const std::uint16_t minion = Field<std::uint16_t>(m, 0x5A);
			int                 group = std::max(10, (lo + hi) * 5);
			if (minion != 0xFFFF && static_cast<std::uint64_t>(minion) < total) group += (Field<std::uint8_t>(m, 0x62) + Field<std::uint8_t>(m, 0x63)) * 5;
			sum += group, ++counted;
		}
		if (counted) {
			for (int i = 0; i < 3; ++i)
				SetField<std::int32_t>(row, LvDen + i * 4, std::clamp(baseDen[i] * GroupRef * counted / sum, baseDen[i] / 2, baseDen[i] * 2));
		}
	}
	std::uint8_t* def = b.defs + id * DefRow;
	std::memcpy(def, b.defs + tmpl * DefRow, DefRow);
	SetField<std::int32_t>(def, DefQuest, 0);
	SetField<std::int32_t>(def, DefQuestEx, 0);
	SetField<std::int32_t>(def, DefDepend, 0);
	// Keep the template's stair tiles at their own Vis index (the maze code places each stairs room by index: Stony Tomb's exit
	// is Vis 4, Room2 flag 0x100) so the maze builds real entry and exit rooms. Our empty levels (built under their own id) use index 0.
	int          index[2] { -1, -1 };
	std::int32_t warps[2] { -1, -1 };
	for (int i = 0, k = 0; i < 8 && k < 2; ++i)
		if (const std::int32_t w = Field<std::int32_t>(def, DefWarp + i * 4); w >= 0 && Field<std::int32_t>(def, DefVis + i * 4) > 0) index[k] = i, warps[k++] = w;
	if (empty && index[0] >= 0) index[0] = 0, index[1] = -1;
	for (int i = 0; i < 8; ++i) SetField<std::int32_t>(def, DefVis + i * 4, 0), SetField<std::int32_t>(def, DefWarp + i * 4, -1);
	if (vis0 && index[0] >= 0) SetField<std::int32_t>(def, DefVis + index[0] * 4, vis0), SetField<std::int32_t>(def, DefWarp + index[0] * 4, warps[0]);
	if (vis1 && index[1] >= 0) SetField<std::int32_t>(def, DefVis + index[1] * 4, vis1), SetField<std::int32_t>(def, DefWarp + index[1] * 4, warps[1]);
	// Links by where the template's stairs lead (an outdoor area's cave entrances, a dungeon's up and down stairs).
	int entryIndex = -1;
	if (links) {
		const std::uint8_t* from = b.defs + tmpl * DefRow;
		std::string         text;
		for (int i = 0; i < 8; ++i) {
			const std::int32_t target = Field<std::int32_t>(from, DefVis + i * 4), warp = Field<std::int32_t>(from, DefWarp + i * 4);
			std::int32_t       to     = 0;
			for (int k = 0; k < linkCount; ++k)
				if (links[k].from == target) to = links[k].to;
			if (!to || warp < 0) continue;
			SetField<std::int32_t>(def, DefVis + i * 4, to), SetField<std::int32_t>(def, DefWarp + i * 4, warp);
			if (entryIndex < 0 && entryFrom && target == entryFrom) entryIndex = i;
			text += (text.empty() ? "" : ", ") + std::string("Vis") + std::to_string(i) + " " + std::to_string(target) + "->" + std::to_string(to) + " (warp " + std::to_string(warp) + ")";
		}
		Log(false, "zones: level %d from template %d links: %s; entry stairs Vis%d", id, tmpl, text.c_str(), entryIndex);
	}
	// Each level its own automap layer (the template's is shared with the real dungeon and with our other levels).
	const int layer = !LayersSafe ? Field<std::int32_t>(b.defs + tmpl * DefRow, DefLayer) : ZoneLayer[slot].load() ? ZoneLayer[slot].load() : 900 + slot;
	SetField<std::int32_t>(def, DefLayer, layer);
	if (!empty) {
		ZoneEntryBit[slot] = links ? (entryIndex >= 0 ? 0x10u << entryIndex : 0) : index[0] >= 0 ? 0x10u << index[0] : 0;
		ZoneSingle[slot]   = false;
	}
	Log(false, "zones: level %d from template %d: vis %d (index %d, warp %d) / %d (index %d, warp %d), layer %d%s", id, tmpl, vis0, index[0], warps[0], vis1, index[1],
	    warps[1], layer, !empty && index[1] < 0 ? ", one stairs room: level 2 by portal" : "");
	if (empty) {
		for (const std::uintptr_t den : { LvDen, LvDen + 4, LvDen + 8 }) SetField<std::int32_t>(row, den, 0);
		for (int i = 0; i < 6; ++i) SetField<std::uint8_t>(row, 48 + i, 0); // MonUMin/MonUMax
		for (int i = 0; i < 3; ++i) SetField<std::uint8_t>(row, LvMonCount + i, 0);
		for (const std::uintptr_t list : { LvMon, LvNMon, LvUMon })
			for (int i = 0; i < MonSlots; ++i) SetField<std::int16_t>(row, list + i * 2, -1);
	}
	std::uint8_t* maze = b.maze + (b.mazes + (id - b.count)) * MazeRow;
	if (const std::uint8_t* from = MazeOf(b, tmpl)) std::memcpy(maze, from, MazeRow);
	SetField<std::int32_t>(maze, 0, id);
	const int roomArea = Field<std::int32_t>(maze, 16) * Field<std::int32_t>(maze, 20);
	if (roomArea > 0) {
		const int rooms = empty ? 1 : std::clamp((ZoneArea + roomArea / 2) / roomArea, 3, 40);
		for (const std::uintptr_t at : { 4, 8, 12 }) SetField<std::int32_t>(maze, at, rooms);
	}
}

// The Act 5 Townguards (act5barb3/4, the barbarians standing at Harrogath's fires) can't be selected (monstats2 noSel):
// made selectable like the Combatants, so they show their name and can be clicked. monstats2 flags: dword +4, isSel bit 3, noSel bit 5.
constexpr std::uint32_t Townguards[] { 534 }, Combatant = 522; // monstats ids: act5pow (Roran Stronghammer), act5barb1
constexpr std::uint32_t SelIs = 1u << 3, SelNo = 1u << 5;

// The monstats2 row of a monster: MonStats word +0x4C (the table skips the "Expansion" line, so ids and rows differ).
const void* MonStats2Row(const D2RL::PluginContext* context, const D2RL::DataTableService* service, int bank, std::uint32_t monster, std::uint32_t* index) noexcept {
	using namespace D2RL::DataTables;
	RowView stats { .structSize = RowViewSize };
	if (service->getRow(context, static_cast<Bank>(bank), TableId::MonStats, monster, &stats) != Result::Success || !stats.row || stats.rowSize < 0x4E) return nullptr;
	const std::int16_t row = Field<std::int16_t>(stats.row, 0x4C);
	RowView            view { .structSize = RowViewSize };
	if (row < 0 || service->getRow(context, static_cast<Bank>(bank), TableId::MonStats2, static_cast<std::uint32_t>(row), &view) != Result::Success || !view.row || view.rowSize < 8) return nullptr;
	*index = static_cast<std::uint32_t>(row);
	return view.row;
}

void MakeTownguardsSelectable(const D2RL::PluginContext* context, const D2RL::DataTableService* service, int bank) noexcept {
	std::uint32_t index = 0;
	const void*   combatant = MonStats2Row(context, service, bank, Combatant, &index);
	if (!combatant) return;
	const std::uint32_t known = Field<std::uint32_t>(combatant, 4);
	for (const std::uint32_t monster : Townguards) {
		const void* guard = MonStats2Row(context, service, bank, monster, &index);
		if (!guard) {
			Log(true, "zones: bank %d monster %u has no monstats2 row, the townguard stays unselectable", bank, monster);
			continue;
		}
		auto*               row   = const_cast<void*>(guard);
		const std::uint32_t flags = Field<std::uint32_t>(row, 4);
		if ((flags & SelIs) && !(flags & SelNo)) continue; // done already (banks can share tables)
		if (!(known & SelIs) || (known & SelNo)) {
			Log(true, "zones: bank %d monstats2 row %u flags 0x%X (combatant 0x%X) not as expected, the townguard stays unselectable", bank, index, flags, known);
			continue;
		}
		SetField<std::uint32_t>(row, 4, (flags | SelIs) & ~SelNo);
		Log(false, "zones: bank %d townguard %u (monstats2 %u) made selectable, flags 0x%X -> 0x%X", bank, monster, index, flags, Field<std::uint32_t>(row, 4));
	}
}

// Roran (act5barb3) like the injured barbarians: Idle AI (stands still) and not killable (no life bar).
// Found by comparing rows: the flags dword has npc/interact (bits 8, 9) on Larzuk, killable (bit 15) on the Townguard and none of
// them on the injured barbarian, inTown (bit 10) on all three; the AI word (+0x52) is the same for Larzuk and the Townguard (Npc).
void MakeRoranStill(const D2RL::PluginContext* context, const D2RL::DataTableService* service, int bank) noexcept {
	using namespace D2RL::DataTables;
	RowView larzuk { .structSize = RowViewSize }, guard { .structSize = RowViewSize }, injured { .structSize = RowViewSize };
	if (service->getRow(context, static_cast<Bank>(bank), TableId::MonStats, 511, &larzuk) != Result::Success || !larzuk.row ||
	    service->getRow(context, static_cast<Bank>(bank), TableId::MonStats, 535, &guard) != Result::Success || !guard.row ||
	    service->getRow(context, static_cast<Bank>(bank), TableId::MonStats, 567, &injured) != Result::Success || !injured.row || guard.rowSize < 0x60)
		return Log(true, "Roran: bank %d monstats rows missing, he keeps the Townguard's behaviour", bank);
	RowView pow { .structSize = RowViewSize };
	if (service->getRow(context, static_cast<Bank>(bank), TableId::MonStats, 534, &pow) != Result::Success || !pow.row || pow.rowSize < 0x60)
		return Log(true, "Roran: bank %d has no act5pow row", bank);
	const auto*              townguard = static_cast<const std::uint8_t*>(guard.row);
	auto*                    row  = static_cast<std::uint8_t*>(const_cast<void*>(pow.row));
	constexpr std::uint32_t  Npc = 1u << 8, Interact = 1u << 9, InTown = 1u << 10, Killable = 1u << 15;
	int                      flagsAt = -1, matches = 0;
	for (int at = 0; at < 0x4C; at += 4) {
		const std::uint32_t l = Field<std::uint32_t>(larzuk.row, at), g = Field<std::uint32_t>(townguard, at), i = Field<std::uint32_t>(injured.row, at);
		if ((l & (Npc | Interact)) == (Npc | Interact) && !(l & Killable) && (g & Killable) && !(g & (Npc | Interact)) && !(i & (Npc | Interact | Killable)) &&
		    (l & g & i & InTown))
			flagsAt = at, ++matches;
	}
	if (matches == 1) {
		const std::uint32_t before = Field<std::uint32_t>(row, flagsAt);
		SetField<std::uint32_t>(row, flagsAt, before & ~Killable);
		Log(false, "Roran: bank %d monstats flags at +0x%X: 0x%X -> 0x%X (not killable)", bank, flagsAt, before, Field<std::uint32_t>(row, flagsAt));
	} else if (matches == 0 && !(Field<std::uint32_t>(row, 0x3C) & Killable)) {
		Log(true, "Roran: bank %d monstats flags not found (%d matches), he keeps his life bar", bank, matches);
	}
	const std::uint16_t ai = Field<std::uint16_t>(row, 0x52), aiLarzuk = Field<std::uint16_t>(larzuk.row, 0x52), aiIdle = Field<std::uint16_t>(injured.row, 0x52);
	if (Field<std::uint16_t>(townguard, 0x52) == aiLarzuk && aiLarzuk != aiIdle) { // the AI word checked on the Townguard (Npc, like Larzuk)
		if (ai != aiIdle) SetField<std::uint16_t>(row, 0x52, aiIdle), Log(false, "Roran: bank %d AI %u -> %u (Idle, stands still)", bank, ai, aiIdle);
	} else Log(true, "Roran: bank %d AI word not as expected (Townguard %u, Larzuk %u, injured %u), he keeps walking", bank, Field<std::uint16_t>(townguard, 0x52), aiLarzuk, aiIdle);
}

void __cdecl OnTablesLoaded(const D2RL::PluginContext* context, const D2RL::Lifecycle::DataTablesLoadedEvent*, void*) noexcept {
	const auto* service = Query<D2RL::DataTableService>("data table");
	if (service && D2RL::HasDataTableServiceField(service, D2RL::DataTableServiceRequiredSize))
		for (int bank = 1; bank <= 3; ++bank) MakeTownguardsSelectable(context, service, bank), MakeRoranStill(context, service, bank);
	int         base    = 0;
	for (int bank = 1; bank <= 3; ++bank) {
		auto* tables = Field<std::uint8_t*>(reinterpret_cast<void*>(Base + TablesRva), static_cast<std::uintptr_t>(bank) * 16);
		if (!tables) continue;
		ZoneBank& b = Banks[bank];
		bool      shared = false;
		for (int other = 1; other < bank; ++other)
			if (Banks[other].tables == tables) shared = true;
		if (shared || Field<std::uint8_t*>(tables, LevelsVec) == b.levels) continue; // already ours
		const std::uint64_t levels = Field<std::uint64_t>(tables, LevelsVec + 8), defs = Field<std::uint64_t>(tables, DefsVec + 8);
		if (service && D2RL::HasDataTableServiceField(service, D2RL::DataTableServiceRequiredSize)) {
			D2RL::DataTables::TableView view { .structSize = D2RL::DataTables::TableViewSize };
			if (service->getTable(context, static_cast<D2RL::DataTables::Bank>(bank), D2RL::DataTables::TableId::Levels, &view) == D2RL::DataTables::Result::Success &&
			    (view.rows != Field<void*>(tables, LevelsVec) || view.rowSize != LevelRow || view.rowCount != levels)) {
				Log(true, "zones: bank %d levels table not where expected (%p/%u/%u vs %p/%llu), no TZ Plus zones", bank, view.rows, view.rowCount, view.rowSize,
				    Field<void*>(tables, LevelsVec), static_cast<unsigned long long>(levels));
				continue;
			}
		}
		if (levels < 100 || levels != defs) {
			Log(true, "zones: bank %d has %llu levels and %llu level defs, no TZ Plus zones", bank, static_cast<unsigned long long>(levels),
			    static_cast<unsigned long long>(defs));
			continue;
		}
		std::uint64_t n = 0, nd = 0, nm = 0;
		b.levels = GrowVector(tables, LevelsVec, LevelRow, ZoneSlots, n);
		b.defs   = b.levels ? GrowVector(tables, DefsVec, DefRow, ZoneSlots, nd) : nullptr;
		b.maze   = b.defs ? GrowVector(tables, MazeVec, MazeRow, ZoneSlots, nm) : nullptr;
		if (!b.maze) {
			Log(true, "zones: bank %d tables could not be extended", bank);
			b = ZoneBank {};
			continue;
		}
		b.tables = tables;
		b.count  = static_cast<int>(n);
		b.mazes  = nm;
		b.pool.clear();
		for (int level = 1; level < b.count; ++level)
			for (int i = 0; i < MonSlots; ++i) {
				const auto m = Field<std::int16_t>(b.levels + level * LevelRow, LvNMon + i * 2);
				if (m < 0) break;
				if (std::find(b.pool.begin(), b.pool.end(), m) == b.pool.end()) b.pool.push_back(m);
			}
		std::uint64_t state = 0x5A17C0DEull;
		for (int i = 0; i < ZoneSlots; ++i) SetupZone(b, b.count + i, 125, state);
		base = b.count;
		Log(false, "zones: bank %d: %d levels, levels %d-%d added (%llu maze rows), %zu monster types to pick from", bank, b.count, b.count, b.count + ZoneSlots - 1,
		    static_cast<unsigned long long>(nm), b.pool.size());
	}
	if (base > 0) ZoneBase.store(base);
}

// ---- opening a zone portal (console "tzplus portal" for now), on the game's thread ----

std::atomic<bool> PortalRequested { false }, CloseRequested { false };
SRWLOCK           PortalLock = SRWLOCK_INIT;
std::string       PortalResult;
struct ZonePortal {
	std::uint32_t unit;
	int           level;
};
ZonePortal ZonePortals[64] {}; // our portals (unit id -> zone), for their name on the client (same ids in single player)
int        PortalCursor = 0;

// ---- names: the zones' LevelName/LevelWarp/LevelEntry keys (TZPlusZone<n>...), answered here ----
// D2R+0x3E2B736 is the game's string-by-key call (jmp [slot]); the slot is pointed at us, everything else goes on.

struct KeyView {
	const char* text;
	std::size_t length;
};
using StringByKeyFn = const char* (*)(const KeyView* key) noexcept;
constexpr std::uint64_t StringByKeyThunkRva = 0x3E2B736;
StringByKeyFn           StringByKeyOrig {};
char                    ZoneTexts[3][ZoneSlots][48] {};

std::atomic<int> RoranNameLogs { 0 };

const char* HookStringByKey(const KeyView* key) noexcept {
	if (key && key->text && (key->length == 9 || key->length == 15)) { // Roran's name: the Townguard's NameStr key
		const std::string_view k(key->text, key->length);
		if (k == "act5barb3" || k == "act5barb4" || k == "Act 5 Townguard") {
			if (RoranNameLogs.fetch_add(1) < 3) Log(false, "Roran Stronghammer: name asked by key \"%.*s\"", static_cast<int>(k.size()), k.data());
			return "Roran Stronghammer";
		}
	}
	if (key && key->text && key->length >= 11 && key->length <= 13 && std::memcmp(key->text, "TZPlus", 6) == 0) {
		const std::string_view k(key->text, key->length);
		for (int kind = 0; kind < 3; ++kind) {
			const std::string_view prefix = kind == 0 ? "TZPlusZone" : kind == 1 ? "TZPlusWarp" : "TZPlusEntry";
			if (k.substr(0, prefix.size()) != prefix) continue;
			const int n = std::atoi(std::string(k.substr(prefix.size())).c_str());
			if (n >= 1 && n <= ZoneSlots) {
				// 3 levels a zone: the zone, its stand-in (the entry stairs lead to Harrogath), its level 2.
				char*       text  = ZoneTexts[kind][n - 1];
				const int   slot  = n - 1, zone = slot / ZoneParts + 1;
				const char* verb  = kind == 0 ? "" : kind == 1 ? "To " : "Entering ";
				if (!text[0]) {
					std::snprintf(text, 48, slot % ZoneParts == KindDungeon ? "%sTerror Zone %d Dungeon" : "%sTerror Zone %d", verb, zone);
				}
				return text;
			}
		}
	}
	return StringByKeyOrig(key);
}

// The portal's level for its name (D2R+0xC1882: call 0x34AD40 (unit) in the client's object name code).
using ObjectLevelFn = std::uint64_t (*)(void* unit) noexcept;
constexpr std::uint64_t PortalNameCallRva = 0xC1882, ObjectLevelRva = 0x34AD40;
int                     PortalNameLogs = 0;

std::uint64_t HookPortalLevel(void* unit) noexcept {
	std::uint64_t level = At<ObjectLevelFn>(ObjectLevelRva)(unit);
	if (!unit || Field<std::uint32_t>(unit, 0) != 2) return level;
	const std::uint32_t id = Field<std::uint32_t>(unit, 8);
	AcquireSRWLockShared(&PortalLock);
	for (const ZonePortal& p : ZonePortals)
		if (p.level && p.unit == id) level = (level & ~0xFFFFull) | static_cast<std::uint16_t>(p.level);
	ReleaseSRWLockShared(&PortalLock);
	if (PortalNameLogs < 6 && Field<std::uint32_t>(unit, 4) == 60)
		++PortalNameLogs, Log(false, "client: red portal %u leads to level %u", id, static_cast<unsigned>(level & 0xFFFF));
	return level;
}

void SetPortalResult(const char* format, ...) noexcept {
	char    line[300] {};
	va_list args;
	va_start(args, format);
	std::vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	Log(false, "%s", line);
	AcquireSRWLockExclusive(&PortalLock);
	try {
		PortalResult = line;
	} catch (...) {
	}
	ReleaseSRWLockExclusive(&PortalLock);
}

// The zone side of a portal being made (D2R+0x435FDF: call 0x2F0680 (room, x, y), with the spot it is placed at).
thread_local GameMods* Creating   = nullptr;
thread_local int       CreatingId = 0;
thread_local std::uint8_t* CreatingGame = nullptr;
using RoomHasFn = std::uint64_t (*)(void* room, std::int32_t x, std::int32_t y) noexcept;
constexpr std::uint64_t PortalSpotCallRva = 0x435FDF, RoomHasRva = 0x2F0680;

// The stub puts the caller's rbp in r9: the spot the caller goes on with is room [rbp-0x28], x [rbp-0x2C], y [rbp-0x30].
// A zone's portal is moved to its entry stairs room (the level's Vis0 room, Room2 flag 0x10).
using ActivateRoomFn = void (*)(std::uint8_t mode, void* room2) noexcept; // D2R+0x3289A0: loads a Room2's Room1
std::uint64_t HookPortalSpot(void* room, std::int32_t x, std::int32_t y, std::uint8_t* frame) noexcept {
	if (Creating && CreatingGame && room && frame && At<RoomLevelFn>(0x2EFC10)(room) == CreatingId) {
		const int fromX = x, fromY = y, slot = CreatingId - ZoneBase.load();
		const std::uint32_t entryBit = slot >= 0 && slot < ZoneSlots ? ZoneEntryBit[slot].load() : 0x10u;
		auto*     room2 = Field<std::uint8_t*>(room, 0x18);
		auto*     level = room2 ? Field<std::uint8_t*>(room2, 0x90) : nullptr;
		int       guard = 0;
		for (auto* r = level ? Field<std::uint8_t*>(level, 0x10) : nullptr; r && guard < 4096; r = Field<std::uint8_t*>(r, 0x48), ++guard) {
			if (!(Field<std::uint32_t>(r, 0x50) & entryBit)) continue;
			if (!Field<void*>(r, 0x58)) At<ActivateRoomFn>(0x3289A0)(Field<std::uint8_t>(CreatingGame, GameMode), r);
			void* entry = Field<void*>(r, 0x58);
			if (!entry) break;
			Coord spot { (Field<std::int32_t>(r, 0x60) + Field<std::int32_t>(r, 0x68) / 2) * 5, (Field<std::int32_t>(r, 0x64) + Field<std::int32_t>(r, 0x6C) / 2) * 5 };
			void* freeRoom = nullptr;
			At<FindFreeSpotFn>(0x517F90)(CreatingGame, entry, &spot, 3, 0x400, &freeRoom, 4, 100);
			if (!freeRoom) break;
			room = freeRoom, x = spot.x, y = spot.y;
			SetField<void*>(frame, static_cast<std::uintptr_t>(-0x28), room);
			SetField<std::int32_t>(frame, static_cast<std::uintptr_t>(-0x2C), x);
			SetField<std::int32_t>(frame, static_cast<std::uintptr_t>(-0x30), y);
			break;
		}
		Creating->safeLevel = CreatingId, Creating->safeX = x, Creating->safeY = y;
		Log(false, "zones: portal into level %d at %d,%d (game's pick %d,%d)%s", CreatingId, x, y, fromX, fromY,
		    x == fromX && y == fromY ? ", no entry stairs room found" : ", moved to the entry stairs");
	}
	return At<RoomHasFn>(RoomHasRva)(room, x, y);
}

// Monster spawns (the game's spawn core D2R+0x493CC0 (params: +0 game, +8 room, +0x18 monster)): none in a zone's level 2 or its
// town stand-in (our levels without a template), whatever spawns them (room population, presets, groups).
constexpr std::uint64_t SpawnCoreRva = 0x493CC0;
constexpr std::array<std::uint8_t, 15> SpawnCoreBytes { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20 };
using SpawnCoreFn = void* (*)(void* params) noexcept;
SpawnCoreFn      SpawnCoreOrig {};
std::atomic<int> EmptyBlocked { 0 };

bool EmptyLevel(int level) noexcept {
	const int base = ZoneBase.load(std::memory_order_relaxed);
	return base > 0 && level >= base && level < base + ZoneSlots && ZoneTemplate[level - base].load() == 0;
}

// Monster type diagnosis (0.0.69): the zone's picks didn't spawn (0.0.68: picks 96,630,662... but 46,17,134... spawned). For the
// first spawns in each zone level: the monster asked for, the level row's lists in every bank and in the game's current table
// (datatables +0x1460), and the call stack (D2R RVAs) that asked for it.
std::atomic<int> SpawnDiagLogs { 0 };
void LogZoneSpawn(void* params, void* room) noexcept {
	const int level = At<RoomLevelFn>(0x2EFC10)(room), base = ZoneBase.load(std::memory_order_relaxed);
	if (level < base || level >= base + ZoneSlots) return;
	const int kind = (level - base) % ZoneParts;
	if (kind != KindMain && kind != KindDungeon) return;
	static std::atomic<int> perLevel[ZoneSlots] {};
	if (perLevel[level - base].fetch_add(1, std::memory_order_relaxed) >= 12 || SpawnDiagLogs.fetch_add(1, std::memory_order_relaxed) >= 120) return;
	const int mon = Field<std::int32_t>(params, 0x18);
	void* frames[12] {};
	const USHORT n = RtlCaptureStackBackTrace(1, 12, frames, nullptr);
	std::string stack;
	for (USHORT i = 0; i < n; ++i) {
		const auto a = reinterpret_cast<std::uintptr_t>(frames[i]);
		char buf[32];
		if (a >= Base && a < Base + 0x4000000) std::snprintf(buf, sizeof(buf), " D2R+%llX", static_cast<unsigned long long>(a - Base));
		else std::snprintf(buf, sizeof(buf), " %llX", static_cast<unsigned long long>(a));
		stack += buf;
	}
	Log(false, "spawn diag: level %d (kind %d) monster %d, params +0x10 %d +0x1C %d +0x20 %d; stack%s", level, kind, mon, Field<std::int32_t>(params, 0x10),
	    Field<std::int32_t>(params, 0x1C), Field<std::int32_t>(params, 0x20), stack.c_str());
	if (perLevel[level - base].load() != 1) return;
	for (int m = 0; m < 4; ++m) {
		const ZoneBank& b = Banks[m];
		if (!b.levels || !b.tables) continue;
		auto list = [&](const std::uint8_t* row, std::uintptr_t off) {
			std::string t;
			for (int i = 0; i < MonSlots; ++i) {
				const auto v = Field<std::int16_t>(row, off + i * 2);
				if (v < 0) break;
				t += (t.empty() ? "" : ",") + std::to_string(v);
			}
			return t;
		};
		const std::uint8_t* ours = b.levels + level * LevelRow;
		const auto*         live = Field<std::uint8_t*>(b.tables, LevelsVec);
		const auto          liveCount = Field<std::uint64_t>(b.tables, LevelsVec + 8);
		Log(false, "spawn diag: bank %d tables %p levels ours %p live %p (count %llu): row %d id %d mon [%s] nmon [%s] umon [%s] count %d/%d/%d",
		    m, b.tables, b.levels, live, static_cast<unsigned long long>(liveCount), level, Field<std::uint16_t>(ours, LvId), list(ours, LvMon).c_str(),
		    list(ours, LvNMon).c_str(), list(ours, LvUMon).c_str(), Field<std::uint8_t>(ours, LvMonCount), Field<std::uint8_t>(ours, LvMonCount + 1),
		    Field<std::uint8_t>(ours, LvMonCount + 2));
		if (live && live != b.levels && static_cast<std::uint64_t>(level) < liveCount) {
			const std::uint8_t* row = live + level * LevelRow;
			Log(false, "spawn diag: bank %d LIVE row %d id %d nmon [%s] umon [%s]", m, level, Field<std::uint16_t>(row, LvId), list(row, LvNMon).c_str(), list(row, LvUMon).c_str());
		}
	}
}

std::atomic<int> EntryBlocked { 0 };
void* HookSpawnCore(void* params) noexcept {
	void* room = params ? Field<void*>(params, 8) : nullptr;
	if (room && ZoneBase.load(std::memory_order_relaxed) > 0) {
		const int level = At<RoomLevelFn>(0x2EFC10)(room);
		if (EmptyLevel(level)) {
			if (EmptyBlocked.fetch_add(1) < 5) Log(false, "zones: no monster %d in level %d (empty)", Field<std::int32_t>(params, 0x18), level);
			return nullptr;
		}
		const int base = ZoneBase.load(std::memory_order_relaxed);
		// No monster in the entry and exit areas.
		if (level >= base && level < base + ZoneSlots && ((level - base) % ZoneParts == KindEntry || (level - base) % ZoneParts == KindExit)) {
			if (EntryBlocked.fetch_add(1) < 5) Log(false, "zones: no monster %d in level %d (entry or exit area)", Field<std::int32_t>(params, 0x18), level);
			return nullptr;
		}
		// No monster in a zone's entry stairs room, where you arrive.
		if (level >= base && level < base + ZoneSlots && ZoneTemplate[level - base].load()) {
			const auto* room2 = Field<std::uint8_t*>(room, 0x18);
			const std::uint32_t bit = ZoneEntryBit[level - base].load();
			if (room2 && bit && (Field<std::uint32_t>(room2, 0x50) & bit)) {
				if (EntryBlocked.fetch_add(1) < 5) Log(false, "zones: no monster %d in the entry stairs room of level %d", Field<std::int32_t>(params, 0x18), level);
				return nullptr;
			}
		}
	}
	if (room && ZoneBase.load(std::memory_order_relaxed) > 0) LogZoneSpawn(params, room);
	return SpawnCoreOrig(params);
}

// Preset monsters (D2R+0x50A050 (game, room, preset, x, y, ...): the map's own monsters, camps, superuniques): none in our zones.
constexpr std::uint64_t PresetSpawnRva = 0x50A050, PresetSpawnCalls[6] { 0x452AEA, 0x45CFE5, 0x48DECD, 0x51835F, 0x54905B, 0x5950DA };
using PresetSpawnFn = void* (*)(void* game, void* room, std::uint64_t preset, std::uint64_t x, std::uint64_t y, std::uint64_t a6, std::uint64_t a7) noexcept;
std::atomic<int> PresetBlocked { 0 };
void* HookPresetSpawn(void* game, void* room, std::uint64_t preset, std::uint64_t x, std::uint64_t y, std::uint64_t a6, std::uint64_t a7) noexcept {
	if (room && ZoneBase.load(std::memory_order_relaxed) > 0) {
		const int level = At<RoomLevelFn>(0x2EFC10)(room), base = ZoneBase.load(std::memory_order_relaxed);
		if (level >= base && level < base + ZoneSlots) {
			if (PresetBlocked.fetch_add(1, std::memory_order_relaxed) < 30)
				Log(false, "zones: no preset monster %d in level %d (at %d,%d)", static_cast<int>(preset), level, static_cast<int>(x), static_cast<int>(y));
			return nullptr;
		}
	}
	return At<PresetSpawnFn>(PresetSpawnRva)(game, room, preset, x, y, a6, a7);
}

// A player arriving in a zone's town stand-in (behind the entry stairs) is sent on to Harrogath's waypoint (D2R+0x488180, reason 0xD).
using WarpUnitFn = std::uint64_t (*)(void* game, void* unit, int level, int reason) noexcept;
void SendStandInsToTown(std::uint8_t* game) noexcept {
	const int base = ZoneBase.load(std::memory_order_relaxed);
	if (base <= 0) return;
	auto* client = Field<std::uint8_t*>(game, GameClients);
	for (int guard = 0; client && guard < 64; ++guard, client = Field<std::uint8_t*>(client, ClientNext)) {
		void* player = Field<std::uint32_t>(client, 4) == 4 ? At<ClientPlayerFn>(0x484C90)(client, 0) : nullptr;
		void* room   = player ? At<UnitRoomFn>(0x34B440)(player) : nullptr;
		if (!room) continue;
		const int level = At<RoomLevelFn>(0x2EFC10)(room);
		if (level < base || level >= base + ZoneSlots || !ZoneStandIn[level - base].load()) continue;
		static std::uint64_t last = 0; // one warp at a time: the move may finish on a later frame
		if (GetTickCount64() - last < 1500) continue;
		last = GetTickCount64();
		// The dungeon's down stairs (the zone's third level): back to the outdoor area, at the dungeon's entrance (WarpUnit's 4th
		// argument is the level to arrive next to: the game looks for the room of the outdoor area that leads to the dungeon).
		if (const GameMods* g = ModsOf(game); g && g->zoneId && level == g->zoneId + KindStandIn) {
			Log(false, "zones: player took the dungeon's down stairs (level %d): sent back to level %d, at the entrance of level %d", level, g->zoneId, g->zoneId + 1);
			At<WarpUnitFn>(0x488180)(game, player, g->zoneId, g->zoneId + 1);
			continue;
		}
		Log(false, "zones: player took the down stairs to level %d: sent to Harrogath", level);
		At<WarpUnitFn>(0x488180)(game, player, Harrogath, 0xD);
	}
}

// The game's monster regions (game +0x1D0: one per level id, built once at game creation by D2R+0x508750 from the level rows):
// the room population picks from the region (+0x10 count, +0x11 rarity sum, +0x14 entries of 0x34: word monster, byte rarity),
// not from the row. Our rows change when a zone opens, after the regions were built, so the zone got the previous picks
// (0.0.69 log). Rebuilt here from the new row like the game does: 0x509940 (mode, row, region, seed, hell lists) appends the
// entries, 0x4195E0 (mode, seed, entry, 3) sets each one up.
using RegionFillFn  = void (*)(std::uint8_t mode, const void* row, void* region, void* seed, int nightmareLists) noexcept;
using RegionEntryFn = void (*)(std::uint8_t mode, void* seed, void* entry, int flags) noexcept;
void RebuildRegion(std::uint8_t* game, std::uint8_t mode, int id, std::uint64_t& state) noexcept {
	auto** regions = Field<std::uint8_t**>(game, 0x1D0);
	if (!regions || mode > 3 || !Banks[mode].levels) return;
	std::uint8_t* region = regions[id];
	if (!region) return Log(true, "zones: level %d has no monster region", id);
	std::string before;
	for (int i = 0; i < region[0x10] && i < 13; ++i) before += (before.empty() ? "" : ",") + std::to_string(Field<std::int16_t>(region, 0x14 + i * 0x34));
	std::uint32_t seed[2] {};
	At<void (*)(void*, std::uint32_t) noexcept>(0x367110)(seed, static_cast<std::uint32_t>(Mix(state)) | 1);
	region[0x10] = 0, region[0x11] = 0;
	const std::uint8_t* row = Banks[mode].levels + id * LevelRow;
	At<RegionFillFn>(0x509940)(mode, row, region, seed, Field<std::uint8_t>(game, GameDifficulty) > 0 ? 1 : 0);
	std::string after;
	for (int i = 0; i < region[0x10] && i < 13; ++i) {
		At<RegionEntryFn>(0x4195E0)(mode, seed, region + 0x14 + i * 0x34, 3);
		after += (after.empty() ? "" : ",") + std::to_string(Field<std::int16_t>(region, 0x14 + i * 0x34));
	}
	Log(false, "zones: level %d monster region rebuilt: [%s] -> [%s] (row picks %d of %d)", id, before.c_str(), after.c_str(), row[0x3A], row[LvMonCount + 1]);
}

void OpenZonePortal(std::uint8_t* game) noexcept {
	const int base = ZoneBase.load();
	if (base <= 0) return SetPortalResult("TZ Plus zones: not available (the level tables could not be extended, see the log)");
	const int  difficulty = Field<std::uint8_t>(game, GameDifficulty);
	const auto mode       = Field<std::uint8_t>(game, GameMode);
	GameMods*  g          = ModsOf(game);
	if (!g || mode > 3 || !Banks[mode].levels) return SetPortalResult("TZ Plus zones: only in Nightmare or Hell games with TZ Plus on");
	void* player = nullptr;
	auto* client = Field<std::uint8_t*>(game, GameClients);
	for (int guard = 0; client && guard < 64 && !player; ++guard, client = Field<std::uint8_t*>(client, ClientNext))
		if (Field<std::uint32_t>(client, 4) == 4) player = At<ClientPlayerFn>(0x484C90)(client, 0);
	void* room = player ? At<UnitRoomFn>(0x34B440)(player) : nullptr;
	if (!room || At<RoomLevelFn>(0x2EFC10)(room) != Harrogath) return SetPortalResult("TZ Plus zones: stand in Harrogath to open a zone portal");
	if (g->portal) return SetPortalResult("TZ Plus zones: a zone portal is already open in this game, close it first");
	if (g->zonesUsed + ZoneParts > ZoneSlots) return SetPortalResult("TZ Plus zones: all %d zones of this game are used, make a new game for more", ZoneSlots / ZoneParts);
	const int     id    = base + g->zonesUsed;
	std::uint64_t state = Field<std::uint64_t>(game, GameSeed) ^ (static_cast<std::uint64_t>(std::time(nullptr)) << 16) ^ static_cast<std::uint64_t>(id);
	// A zone = an outdoor area (where the portal arrives) + its dungeon, one level; the dungeon's down stairs lead to Harrogath
	// (through an empty stand-in level, the third slot).
	const ZonePair pair = ZonePairs[Mix(state) % (sizeof(ZonePairs) / sizeof(ZonePairs[0]))];
	const int      tmpl = pair.outdoor, dungeon = pair.dungeon;
	if (tmpl >= Banks[mode].count || dungeon >= Banks[mode].count || !MazeOf(Banks[mode], dungeon))
		return SetPortalResult("TZ Plus zones: the level tables have no %d / %d to copy", tmpl, dungeon);
	{
		const int layer = NextLayer.fetch_add(ZoneParts);
		for (int i = 0; i < ZoneParts; ++i) ZoneLayer[id + i - base] = layer + i;
		// The entry and exit areas share the outdoor area's automap layer (walking between them keeps the map, like the act's areas).
		ZoneLayer[id + KindEntry - base] = layer, ZoneLayer[id + KindExit - base] = layer;
	}
	// The zone's monsters, once for all banks.
	ForcedCount = 0;
	if (const auto& pool = Banks[mode].pool; !pool.empty())
		for (int tries = 0; ForcedCount < ZoneMonsters && ForcedCount < static_cast<int>(pool.size()) && tries < 200; ++tries) {
			const std::int16_t m = pool[Mix(state) % pool.size()];
			if (std::find(ForcedPicks, ForcedPicks + ForcedCount, m) == ForcedPicks + ForcedCount) ForcedPicks[ForcedCount++] = m;
		}
	{
		// The dungeon's template stairs: toward the outdoor area (its entry), and deeper (its down stairs).
		std::int32_t deeper = 0;
		const std::uint8_t* ddef = Banks[mode].defs + dungeon * DefRow;
		for (int i = 0; i < 8; ++i)
			if (const std::int32_t v = Field<std::int32_t>(ddef, DefVis + i * 4); v > 0 && v != tmpl && Field<std::int32_t>(ddef, DefWarp + i * 4) >= 0) deeper = v;
		const ZoneLink outdoorLinks[] { { dungeon, id + 1 } };
		// The dungeon's down stairs lead to an empty stand-in level (a stairs tile must lead to a level that links back, else the
		// client crashes reaching it, 0.0.55); a player arriving there is sent back to the outdoor area (SendStandInsToTown).
		const ZoneLink dungeonLinks[] { { tmpl, id }, { deeper, id + 2 } };
		for (int bank = 1; bank <= 3; ++bank) {
			std::uint64_t copy = state, copy1 = state, copy2 = state;
			SetupZone(Banks[bank], id, tmpl, copy, 0, 0, false, outdoorLinks, 1, 0);
			SetupZone(Banks[bank], id + 1, dungeon, copy1, 0, 0, false, dungeonLinks, deeper ? 2 : 1, tmpl);
			SetupZone(Banks[bank], id + 2, dungeon, copy2, id + 1, 0, true);
			// The entry and exit areas: empty copies of the outdoor area; the outdoor area and they list each other as neighbours
			// (LevelDefs Vis with Warp -1, like the act's own outdoor areas), which the game turns into passages (BuildZoneLevel).
			std::uint64_t copy3 = state, copy4 = state;
			SetupZone(Banks[bank], id + KindEntry, tmpl, copy3, 0, 0, true);
			SetupZone(Banks[bank], id + KindExit, tmpl, copy4, 0, 0, true);
			if (std::uint8_t* defs = Banks[bank].defs; defs && id + KindExit < Banks[bank].count + ZoneSlots) {
				auto neighbour = [&](int level, int index, int other) {
					SetField<std::int32_t>(defs + level * DefRow, DefVis + index * 4, other);
					SetField<std::int32_t>(defs + level * DefRow, DefWarp + index * 4, -1);
				};
				for (const int small : { id + KindEntry, id + KindExit })
					for (int i = 0; i < 8; ++i) neighbour(small, i, 0);
				neighbour(id, 0, id + KindEntry), neighbour(id, 1, id + KindExit);
				neighbour(id + KindEntry, 0, id), neighbour(id + KindExit, 0, id);
			}
			// Elite packs (Levels MonUMin/MonUMax, a byte per difficulty at +48/+51): x1.5 outdoors, x2 in the dungeon.
			for (const auto [lvl, pct] : { std::pair { id, OutdoorElites }, std::pair { id + 1, DungeonElites } })
				if (Banks[bank].levels && lvl < Banks[bank].count + ZoneSlots)
					for (int i = 0; i < 6; ++i) {
						std::uint8_t* row = Banks[bank].levels + lvl * LevelRow;
						SetField<std::uint8_t>(row, 48 + i, static_cast<std::uint8_t>(std::min(60, (Field<std::uint8_t>(row, 48 + i) * pct + 50) / 100)));
					}
			// The dungeon: DungeonDensity % of the outdoor area's density.
			if (Banks[bank].levels && id + 1 < Banks[bank].count + ZoneSlots)
				for (int i = 0; i < 3; ++i)
					SetField<std::int32_t>(Banks[bank].levels + (id + 1) * LevelRow, LvDen + i * 4,
					                       Field<std::int32_t>(Banks[bank].levels + id * LevelRow, LvDen + i * 4) * DungeonDensity / 100);
		}
	}
	ForcedCount = 0;
	(void)Mix(state);
	std::string monsters;
	for (int i = 0; i < MonSlots; ++i) {
		const auto m = Field<std::int16_t>(Banks[mode].levels + id * LevelRow, LvNMon + i * 2);
		if (m < 0) break;
		monsters += (monsters.empty() ? "" : ",") + std::to_string(m);
	}
	// New mods for the game when a zone opens.
	AcquireSRWLockExclusive(&Lock);
	g->seed ^= Mix(state);
	WriteZoneStats(*g, true);
	for (auto& c : g->zoneCount) c = 0;
	AcquireSRWLockExclusive(&g->modLock);
	g->zoneMons.clear(), g->zoneMet.clear(), g->zoneClasses.clear();
	VanillaSpawnLogs = 0;
	for (int i = 0; i < 4; ++i) g->zoneGen[i] = g->zoneSeen[i] = g->zoneKilled[i] = 0;
	ReleaseSRWLockExclusive(&g->modLock);
	Roll(*g);
	const std::string mods = Describe(*g);
	ReleaseSRWLockExclusive(&Lock);
	SetConstants(*g, mode);
	GiveRewards(game, *g, difficulty == 2);

	Coord at {};
	At<GetCoordsFn>(0x34A040)(player, &at);
	Coord spot { at.x + 3, at.y + 3 };
	void* freeRoom = nullptr;
	At<FindFreeSpotFn>(0x517F90)(game, room, &spot, 3, 0x400, &freeRoom, 4, 100);
	// a10 = 1: past the game's town check (only the cow level and Pandemonium from towns); the act must still match.
	void* portal = nullptr;
	g->safeLevel = 0;
	ZoneTemplate[id - base] = tmpl, ZoneTemplate[id + 1 - base] = dungeon, ZoneTemplate[id + 2 - base] = 0; // the stand-in keeps its id: one entry room
	ZoneTemplate[id + KindEntry - base] = tmpl, ZoneTemplate[id + KindExit - base] = tmpl;
	ZoneEntryBit[id + KindEntry - base] = 0, ZoneEntryBit[id + KindExit - base] = 0;
	ZoneStandIn[id - base] = false, ZoneStandIn[id + 1 - base] = false, ZoneStandIn[id + 2 - base] = true;
	ZoneStandIn[id + KindEntry - base] = false, ZoneStandIn[id + KindExit - base] = false;
	{
		std::uint64_t layout = Field<std::uint64_t>(game, GameSeed) ^ GetTickCount64() ^ (static_cast<std::uint64_t>(id) << 40);
		for (int i = 0; i < ZoneParts; ++i) ZoneSeed[id + i - base] = static_cast<std::uint32_t>(Mix(layout)) | 1;
	}
	for (const int level : { id, id + KindDungeon }) RebuildRegion(game, mode, level, state);
	Creating     = g;
	CreatingId   = id + KindEntry; // the portal arrives in the entry area
	CreatingGame = game;
	const int created = freeRoom ? At<CreatePortalFn>(0x432CE0)(game, player, freeRoom, spot.x, spot.y, id + KindEntry, &portal, 60, 0, 1) : 0;
	Creating = nullptr, CreatingGame = nullptr;
	if (!created)
		return SetPortalResult("TZ Plus zones: the game refused the portal to level %d (%s) at %d,%d%s", id, LevelName(Banks[mode], tmpl).c_str(), spot.x, spot.y,
		                       freeRoom ? "" : " (no free spot)");
	g->zonesUsed += ZoneParts;
	g->zoneId = id, g->level2Id = id + 2, g->exitPortal = nullptr, g->exitPortalId = 0, g->single = ZoneSingle[id - base].load();
	g->exitX = g->exitY = 0, g->exitDone = false;
	g->portal   = portal;
	g->portalId = portal ? Field<std::uint32_t>(portal, 8) : 0;
	ZonePortalOpen = portal != nullptr;
	if (portal) {
		AcquireSRWLockExclusive(&PortalLock);
		ZonePortals[PortalCursor++ % 64] = { Field<std::uint32_t>(portal, 8), id + KindEntry };
		ReleaseSRWLockExclusive(&PortalLock);
	}
	SetPortalResult("TZ Plus zone %d/%d opened: level %d, a copy of %s (level %d) with its %s (level %d, our %d), monsters [%s], density %d/%d, Hell elite packs %d-%d/%d-%d; new mods: %s",
	                g->zonesUsed / ZoneParts, ZoneSlots / ZoneParts, id, LevelName(Banks[mode], tmpl).c_str(), tmpl, LevelName(Banks[mode], dungeon).c_str(), dungeon, id + 1,
	                monsters.c_str(), Field<std::int32_t>(Banks[mode].levels + id * LevelRow, LvDen + 8),
	                Field<std::int32_t>(Banks[mode].levels + (id + 1) * LevelRow, LvDen + 8), Field<std::uint8_t>(Banks[mode].levels + id * LevelRow, 50), Field<std::uint8_t>(Banks[mode].levels + id * LevelRow, 53),
	                Field<std::uint8_t>(Banks[mode].levels + (id + 1) * LevelRow, 50), Field<std::uint8_t>(Banks[mode].levels + (id + 1) * LevelRow, 53), mods.c_str());
}

using RemoveUnitFn   = void (*)(void* game, void* unit) noexcept;
using RoomUnitGoneFn = void (*)(void* room, int type, std::uint32_t id) noexcept; // D2R+0x48FAA0 (the game removes a portal it could not link this way)

void CloseZonePortal(std::uint8_t* game) noexcept {
	GameMods* g = ModsOf(game);
	if (!g || !g->portal) return ZonePortalOpen = false, SetPortalResult("TZ Plus zones: no zone portal is open in this game");
	void* portal = At<FindUnitFn>(0x48FE80)(game, 2, g->portalId);
	if (portal == g->portal) {
		// Like the game does for a portal it drops (D2R+0x435EB0): tell the room's clients the unit is gone, then remove it.
		if (void* room = At<UnitRoomFn>(0x34B440)(portal)) At<RoomUnitGoneFn>(0x2EF4D0)(room, 2, g->portalId);
		At<RemoveUnitFn>(0x48FAA0)(game, portal);
	}
	WriteZoneStats(*g, true);
	g->portal = nullptr, g->portalId = 0, ZonePortalOpen = false;
	SetPortalResult(portal ? "TZ Plus zones: the zone portal is closed" : "TZ Plus zones: the zone portal was already gone");
}

// Monsters of a zone that ended up within SafeRadius of its entry portal: moved out to the edge of that area.
using MoveUnitFn = void (*)(void* game, void* unit, void* room, int x, int y, int a6, int a7) noexcept;
using MoveDoneFn = void (*)(void* unit, int flag) noexcept;
using RoomAtFn   = void* (*)(void* room, int x, int y) noexcept;
int SafeLogs = 0;

void MoveOutOfSafeArea(std::uint8_t* game) noexcept {
	std::vector<SafeMove> moves;
	AcquireSRWLockExclusive(&SafeLock);
	for (auto it = SafeQueue.begin(); it != SafeQueue.end();)
		if (it->game == game) moves.push_back(*it), it = SafeQueue.erase(it);
		else ++it;
	ReleaseSRWLockExclusive(&SafeLock);
	if (moves.empty()) return;
	GameMods* g = ModsOf(game);
	if (!g || !g->safeLevel) return;
	for (const SafeMove& m : moves) {
		if (At<FindUnitFn>(0x48FE80)(game, 1, m.id) != m.unit) continue;
		void* room = At<UnitRoomFn>(0x34B440)(m.unit);
		if (!room || At<RoomLevelFn>(0x2EFC10)(room) != g->safeLevel) continue;
		Coord at {};
		At<GetCoordsFn>(0x34A040)(m.unit, &at);
		// The entry area (the portal) or the exit area (the chests).
		int cx = g->safeX, cy = g->safeY;
		if (g->exitX && std::abs(at.x - g->exitX) <= SafeRadius && std::abs(at.y - g->exitY) <= SafeRadius) cx = g->exitX, cy = g->exitY;
		const int dx = at.x - cx, dy = at.y - cy;
		if (std::abs(dx) > SafeRadius || std::abs(dy) > SafeRadius) continue;
		// Out along the line from the portal through the monster (straight out on x if it stands on the portal).
		const double length = std::sqrt(static_cast<double>(dx * dx + dy * dy));
		// Each monster its own direction (a little turned by its id) so the moved ones don't end up in one heap.
		const double base = length > 0.5 ? std::atan2(static_cast<double>(dy), static_cast<double>(dx)) : 0.0;
		const double angle = base + ((static_cast<int>(m.id % 7) - 3) * 0.35);
		const double ux = std::cos(angle), uy = std::sin(angle);
		bool moved = false;
		for (int extra = 4 + static_cast<int>(m.id % 5) * 3; extra <= 40 && !moved; extra += 6) {
			Coord spot { cx + static_cast<int>(ux * (SafeRadius * 1.42 + extra)), cy + static_cast<int>(uy * (SafeRadius * 1.42 + extra)) };
			void* nearRoom = At<RoomAtFn>(0x38DFB0)(room, spot.x, spot.y);
			if (!nearRoom) continue;
			void* free = nullptr;
			At<FindFreeSpotFn>(0x517F90)(game, nearRoom, &spot, 3, 0x400, &free, 4, 100);
			if (!free || (std::abs(spot.x - g->safeX) <= SafeRadius && std::abs(spot.y - g->safeY) <= SafeRadius) ||
			    (g->exitX && std::abs(spot.x - g->exitX) <= SafeRadius && std::abs(spot.y - g->exitY) <= SafeRadius)) continue;
			At<MoveUnitFn>(0x491CA0)(game, m.unit, free, spot.x, spot.y, 0, 0);
			At<MoveDoneFn>(0x34F490)(m.unit, 1);
			moved = true;
			if (SafeLogs < 15) ++SafeLogs, Log(false, "zones: monster %u moved from %d,%d to %d,%d (out of the %s area)", m.id, at.x, at.y, spot.x, spot.y, cx == g->safeX && cy == g->safeY ? "entry" : "exit");
		}
		if (!moved && SafeLogs < 15) ++SafeLogs, Log(false, "zones: monster %u at %d,%d could not be moved out of the entry or exit area", m.id, at.x, at.y);
	}
}

// A zone copied from a one-stairs dungeon (Spider Cave, Crypt, Ancient Tunnels...) has no exit stairs: its farthest room from the
// entry gets a red portal to level 2 once a player is in the zone. Rooms: level +0x10, next +0x48, flags +0x50, Room1 +0x58,
// x, y, w, h +0x60 (tiles), level +0x90.
int ExitLogs = 0;
// A zone's exit area (its own small level, east of the outdoor area): 3 golden chests (Objects 397, sparkly chest) in its middle.
// Set up once, the first frame a player is in one of the zone's levels.
using SpawnUnitFn = void* (*)(int type, int cls, int x, int y, void* game, void* room, std::uint64_t flag, int mode, int a9, void* a10) noexcept;
constexpr int GoldenChest = 397, ExitChests = 3;
void ZoneExitArea(std::uint8_t* game) noexcept {
	GameMods* g = ModsOf(game);
	if (!g || !g->zoneId || g->exitDone) return;
	auto* client = Field<std::uint8_t*>(game, GameClients);
	for (int guard = 0; client && guard < 64; ++guard, client = Field<std::uint8_t*>(client, ClientNext)) {
		void* player = Field<std::uint32_t>(client, 4) == 4 ? At<ClientPlayerFn>(0x484C90)(client, 0) : nullptr;
		void* room   = player ? At<UnitRoomFn>(0x34B440)(player) : nullptr;
		const int at = room ? At<RoomLevelFn>(0x2EFC10)(room) : 0;
		if (at < g->zoneId || at >= g->zoneId + ZoneParts) continue;
		g->exitDone = true;
		auto* room2 = Field<std::uint8_t*>(room, 0x18);
		auto* here  = room2 ? Field<std::uint8_t*>(room2, 0x90) : nullptr;
		void* drlg  = here ? Field<void*>(here, 0x1C8) : nullptr;
		const auto mode = Field<std::uint8_t>(game, GameMode);
		auto* level = drlg ? static_cast<std::uint8_t*>(At<GetLevelFn>(0x3267C0)(mode, drlg, g->zoneId + KindExit)) : nullptr;
		if (!level) return Log(true, "zones: the exit area (level %d) could not be found", g->zoneId + KindExit);
		if (!Field<void*>(level, 0x10)) At<BuildLevelFn>(0x3271C0)(mode, level); // not built yet: build it (as the game does, D2R+0x3DA6C3)
		// Its middle room.
		const long long cx = Field<std::int32_t>(level, 0x24) + Field<std::int32_t>(level, 0x2C) / 2, cy = Field<std::int32_t>(level, 0x28) + Field<std::int32_t>(level, 0x30) / 2;
		std::uint8_t*   mid = nullptr;
		long long       best = -1;
		int             rooms = 0;
		for (auto* r = Field<std::uint8_t*>(level, 0x10); r && rooms < 4096; r = Field<std::uint8_t*>(r, 0x48), ++rooms) {
			const long long dx = Field<std::int32_t>(r, 0x60) + Field<std::int32_t>(r, 0x68) / 2 - cx, dy = Field<std::int32_t>(r, 0x64) + Field<std::int32_t>(r, 0x6C) / 2 - cy;
			if (best < 0 || dx * dx + dy * dy < best) best = dx * dx + dy * dy, mid = r;
		}
		if (!mid) return Log(true, "zones: the exit area (level %d) has no rooms", g->zoneId + KindExit);
		g->exitX = static_cast<int>(cx) * 5, g->exitY = static_cast<int>(cy) * 5;
		if (!Field<void*>(mid, 0x58)) At<ActivateRoomFn>(0x3289A0)(mode, mid);
		void* exitRoom = Field<void*>(mid, 0x58);
		if (!exitRoom) return Log(true, "zones: the exit area's middle room (level %d) could not be loaded", g->zoneId + KindExit);
		int made = 0;
		for (int i = 0; i < ExitChests; ++i) {
			Coord spot { g->exitX + 12, g->exitY + (i - 1) * 8 }; // a row across the area, toward the back (east), facing the passage (west)
			void* free = nullptr;
			At<FindFreeSpotFn>(0x517F90)(game, exitRoom, &spot, 2, 0x400, &free, 4, 100);
			OwnObject   = true;
			void* chest = free ? At<SpawnUnitFn>(0x490410)(2, GoldenChest, spot.x, spot.y, game, free, 1, 0, 0, nullptr) : nullptr;
			OwnObject   = false;
			if (chest) ++made;
			Log(false, "zones: exit area (level %d): golden chest %d at %d,%d %s (unit id %u)", g->zoneId + KindExit, i + 1, spot.x, spot.y,
			    chest ? "made" : free ? "refused" : "(no free spot)", chest ? Field<std::uint32_t>(chest, 8) : 0);
		}
		Log(false, "zones: exit area (level %d, %d rooms) at %d,%d: %d of %d chests", g->zoneId + KindExit, rooms, g->exitX, g->exitY, made, ExitChests);
		return;
	}
}


void OpenExitPortal(std::uint8_t* game) noexcept {
	GameMods* g = ModsOf(game);
	if (!g || !g->zoneId || !g->single || g->exitPortal) return;
	const int slot = g->zoneId - ZoneBase.load();
	if (slot < 0 || slot >= ZoneSlots) return;
	auto* client = Field<std::uint8_t*>(game, GameClients);
	for (int guard = 0; client && guard < 64; ++guard, client = Field<std::uint8_t*>(client, ClientNext)) {
		void* player = Field<std::uint32_t>(client, 4) == 4 ? At<ClientPlayerFn>(0x484C90)(client, 0) : nullptr;
		void* room   = player ? At<UnitRoomFn>(0x34B440)(player) : nullptr;
		if (!room || At<RoomLevelFn>(0x2EFC10)(room) != g->zoneId) continue;
		auto* room2 = Field<std::uint8_t*>(room, 0x18);
		auto* level = room2 ? Field<std::uint8_t*>(room2, 0x90) : nullptr;
		if (!level) return;
		const std::uint32_t entryBit = ZoneEntryBit[slot].load();
		// From the entry stairs room, else from the zone portal's spot.
		long long fromX = g->safeX / 5, fromY = g->safeY / 5;
		int       guardRooms = 0;
		for (auto* r = Field<std::uint8_t*>(level, 0x10); r && guardRooms < 4096; r = Field<std::uint8_t*>(r, 0x48), ++guardRooms)
			if (entryBit && (Field<std::uint32_t>(r, 0x50) & entryBit)) {
				fromX = Field<std::int32_t>(r, 0x60) + Field<std::int32_t>(r, 0x68) / 2, fromY = Field<std::int32_t>(r, 0x64) + Field<std::int32_t>(r, 0x6C) / 2;
				break;
			}
		std::uint8_t* farRoom = nullptr;
		long long     best = -1;
		guardRooms         = 0;
		for (auto* r = Field<std::uint8_t*>(level, 0x10); r && guardRooms < 4096; r = Field<std::uint8_t*>(r, 0x48), ++guardRooms) {
			const long long dx = Field<std::int32_t>(r, 0x60) + Field<std::int32_t>(r, 0x68) / 2 - fromX, dy = Field<std::int32_t>(r, 0x64) + Field<std::int32_t>(r, 0x6C) / 2 - fromY;
			if (dx * dx + dy * dy > best) best = dx * dx + dy * dy, farRoom = r;
		}
		if (!farRoom) {
			if (ExitLogs < 5) ++ExitLogs, Log(true, "zones: level %d has no rooms for its exit portal", g->zoneId);
			g->exitPortal = reinterpret_cast<void*>(1);
			return;
		}
		if (!Field<void*>(farRoom, 0x58)) At<ActivateRoomFn>(0x3289A0)(Field<std::uint8_t>(game, GameMode), farRoom);
		void* exitRoom = Field<void*>(farRoom, 0x58);
		if (!exitRoom) {
			if (ExitLogs < 5) ++ExitLogs, Log(true, "zones: the farthest room of level %d could not be loaded", g->zoneId);
			g->exitPortal = reinterpret_cast<void*>(1);
			return;
		}
		Coord spot { (Field<std::int32_t>(farRoom, 0x60) + Field<std::int32_t>(farRoom, 0x68) / 2) * 5, (Field<std::int32_t>(farRoom, 0x64) + Field<std::int32_t>(farRoom, 0x6C) / 2) * 5 };
		void* freeRoom = nullptr;
		At<FindFreeSpotFn>(0x517F90)(game, exitRoom, &spot, 3, 0x400, &freeRoom, 4, 100);
		void*     portal  = nullptr;
		const int created = freeRoom ? At<CreatePortalFn>(0x432CE0)(game, player, freeRoom, spot.x, spot.y, g->level2Id, &portal, 60, 0, 1) : 0;
		if (created && portal) {
			g->exitPortal = portal, g->exitPortalId = Field<std::uint32_t>(portal, 8);
			AcquireSRWLockExclusive(&PortalLock);
			ZonePortals[PortalCursor++ % 64] = { g->exitPortalId, g->level2Id };
			ReleaseSRWLockExclusive(&PortalLock);
			Log(false, "zones: level %d has one stairs room: exit portal at %d,%d (%lld tiles from the entry), leads to level 2 (level %d)", g->zoneId, spot.x,
			    spot.y, static_cast<long long>(std::sqrt(static_cast<double>(best))), g->level2Id);
		} else {
			if (ExitLogs < 5) ++ExitLogs, Log(true, "zones: the game refused the exit portal of level %d at %d,%d%s", g->zoneId, spot.x, spot.y, freeRoom ? "" : " (no free spot)");
			g->exitPortal = reinterpret_cast<void*>(1); // don't retry every frame
		}
		return;
	}
}

// Warps (D2R+0x488180 (game, unit, level, reason)): a zone's entry stairs lead to Harrogath; sent as a waypoint arrival
// (reason 0xD: the game puts you at Harrogath's waypoint) since Harrogath has no stairs back to the zone.
constexpr std::uint64_t LevelWarpRva = 0x488180;
constexpr std::array<std::uint8_t, 15> LevelWarpBytes { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x48, 0x89, 0x68, 0x18, 0x56, 0x57, 0x41, 0x54 };
using LevelWarpFn = std::uint64_t (*)(void* game, void* unit, int level, int reason) noexcept;
LevelWarpFn           WarpOrig {};
std::atomic<int> WarpLogs { 0 };

std::uint64_t HookWarp(void* game, void* unit, int level, int reason) noexcept {
	const int base = ZoneBase.load(std::memory_order_relaxed);
	if (base > 0 && unit) {
		void*     room = At<UnitRoomFn>(0x34B440)(unit);
		const int from = room ? At<RoomLevelFn>(0x2EFC10)(room) : 0;
		// The entry stairs: toward a zone's stand-in level (the level after a zone, which has a template).
		const bool toTown = level >= base && level < base + ZoneSlots && ZoneStandIn[level - base].load();
		if (toTown || (from >= base && from < base + ZoneSlots) || Field<std::uint32_t>(unit, 0) == 0) {
			// A dungeon's down stairs: back to the zone's outdoor area, next to the dungeon's entrance (the 4th argument is the level
			// to arrive next to); other stand-ins: Harrogath's waypoint.
			const bool back = toTown && (level - base) % ZoneParts == KindStandIn;
			const int  main = level - KindStandIn;
			if (WarpLogs.fetch_add(1) < 40)
				Log(false, "warp: unit type %u from level %d to level %d (reason 0x%X)%s", Field<std::uint32_t>(unit, 0), from, level, reason,
				    back ? ", the dungeon's down stairs: back to the outdoor area" : toTown ? ", the entry stairs: sent to Harrogath's waypoint" : "");
			if (back) level = main, reason = main + KindDungeon;
			else if (toTown) level = Harrogath, reason = 0xD;
		}
	}
	return WarpOrig(game, unit, level, reason);
}

// Zone layouts: the level's random seed (D2R+0x326939: call 0x367110 (&level->seed, act seed + level id)) made random for our
// zones, so single player gets a new layout every time (the act seed is the character's map seed).
using SeedFn = std::uint64_t (*)(void* seed, std::uint32_t value) noexcept;
constexpr std::uint64_t LevelSeedCallRva = 0x326939, SeedInitRva = 0x367110;
std::atomic<std::uint64_t> SeedState { 0 };

std::uint64_t HookLevelSeed(void* seed, std::uint32_t value) noexcept {
	const int base = ZoneBase.load(std::memory_order_relaxed);
	const int id   = seed ? Field<std::int32_t>(static_cast<std::uint8_t*>(seed) - 0x1E4, 0x1F8) : 0;
	if (base > 0 && id >= base && id < base + ZoneSlots) {
		std::uint32_t random = ZoneSeed[id - base].load();
		if (!random) {
			std::uint64_t state = SeedState.fetch_add(0x9E3779B97F4A7C15ull) ^ GetTickCount64() ^ (static_cast<std::uint64_t>(id) << 32);
			random = static_cast<std::uint32_t>(Mix(state)) | 1;
			ZoneSeed[id - base] = random;
		}
		Log(false, "zones: level %d layout seed %u (the map seed would give %u)", id, random, value);
		value = random;
	}
	return At<SeedFn>(SeedInitRva)(seed, value);
}

// Maze stair rooms (D2R+0x3E69E0 (mode, level)): the game places the exit stairs room only for the level ids it knows
// (a switch on the level id per level type). Our zone is built under its template's id, then gets its own back.
constexpr std::uint64_t MazeBuildRva = 0x3E69E0;
constexpr std::array<std::uint8_t, 16> MazeBuildBytes { 0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x55, 0x41, 0x56, 0x48, 0x8B, 0xEC, 0x48, 0x83, 0xEC, 0x28 };
using MazeBuildFn = std::uint64_t (*)(std::uint8_t mode, void* level) noexcept;
MazeBuildFn      MazeBuildOrig {};

constexpr std::uint64_t OutdoorBuildCall = 0x32720E, OutdoorBuildRva = 0x3E2B5CE;
std::uint64_t OutdoorBuild(std::uint8_t mode, void* level) noexcept { return At<MazeBuildFn>(OutdoorBuildRva)(mode, level); }
std::atomic<int> OutdoorTrace { 0 };

// Border piece placement (D2R+0x3F4F53 calls slot D2R+0x3E2B5EC (mode, ctx, preset, file), which jumps into the loader's
// secondary-border pass). On a small level the pass counts a negative area for a piece and stops the game ("the
// secondary-border area is negative"); same formula here: such a piece is skipped (not placed) instead.
constexpr std::uint64_t BorderPlaceCall = 0x3F4F53, BorderPlaceRva = 0x3E2B5EC;
using BorderPlaceFn = std::uint8_t (*)(std::uint8_t mode, void* ctx, void* preset, void* file) noexcept;
std::atomic<int> BorderLogs { 0 };
bool IsZoneId(int id) noexcept { const int b = ZoneBase.load(std::memory_order_relaxed); return b > 0 && id >= b && id < b + ZoneSlots; }
std::uint8_t HookBorderPlace(std::uint8_t mode, void* ctx, void* preset, void* file) noexcept {
	void* level = ctx ? Field<void*>(ctx, 0) : nullptr;
	void* grid  = ctx ? Field<void*>(ctx, 8) : nullptr;
	if (level && grid && preset && file) {
		const std::int64_t count = Field<std::int32_t>(preset, 0x4C), fw = Field<std::int32_t>(file, 8), fh = Field<std::int32_t>(file, 0xC);
		const std::int64_t gw = Field<std::int32_t>(grid, 8), gh = Field<std::int32_t>(grid, 0xC);
		const int          side = Field<std::int32_t>(ctx, 0x28);
		std::int64_t       adj  = 1;
		if (side == 1) {
			const auto* outdoor = Field<std::uint8_t*>(level, 0x18);
			adj = outdoor && (outdoor[0] & 0xC) ? -1 : 1;
		}
		const std::int64_t across = gw - fw * count + adj, down = gh - fh * count;
		const bool skip = count > 0 && fw > 0 && fh > 0 && across < 0 && down < -1;
		if (skip || (IsZoneId(Field<std::int32_t>(level, 0x1F8)) && BorderLogs.load(std::memory_order_relaxed) < 40)) {
			if (BorderLogs.fetch_add(1, std::memory_order_relaxed) < 40)
				Log(false, "zones: border piece level %d side %d grid %lldx%lld piece %lldx%lld x%lld -> %lld,%lld%s", Field<std::int32_t>(level, 0x1F8), side,
				    static_cast<long long>(gw), static_cast<long long>(gh), static_cast<long long>(fw), static_cast<long long>(fh), static_cast<long long>(count),
				    static_cast<long long>(across), static_cast<long long>(down), skip ? " (does not fit, skipped)" : "");
		}
		if (skip) return 0;
	}
	return At<BorderPlaceFn>(BorderPlaceRva)(mode, ctx, preset, file);
}

// Preset room files (D2R+0x3DE0E0 (mode, room2) loads the room's DS1 through 0x3DCF10 (mode, &cache, name) at 0x3DE138; the
// name is the preset row's File[index] (row +0x40, 60 bytes each), index at [room2+0x40]+8 -> +4). An empty or missing name gives
// an invalid file handle (Preset.cpp:395) and a crash reading it (0.0.63/0.0.64 at D2R+0x3E15xx on the 16x16 areas). Logged in
// zone levels; an empty name falls back to the row's first file.
constexpr std::uint64_t Ds1LoadCall = 0x3DE138, Ds1LoadRva = 0x3DCF10;
using Ds1LoadFn = std::uint64_t (*)(std::uint8_t mode, void* cache, const char* name, void* room2) noexcept;
std::atomic<int> Ds1Logs { 0 };
std::uint64_t HookDs1Load(std::uint8_t mode, void* cache, const char* name, void* room2) noexcept {
	void* level = room2 ? Field<void*>(room2, 0x90) : nullptr;
	const int id = level ? Field<std::int32_t>(level, 0x1F8) : -1;
	if (IsZoneId(id) && room2) {
		void*                data  = Field<void*>(room2, 0x40) ? Field<void*>(Field<void*>(room2, 0x40), 8) : nullptr;
		const std::uint8_t*  row   = data ? Field<const std::uint8_t*>(data, 8) : nullptr;
		const int            index = data ? Field<std::int32_t>(data, 4) : -1;
		// A real file name ends in .ds1; past the row's last file the column holds "0" (0.0.65: Wild Border 7, 1 file, asked for file 3).
		const std::size_t    len   = name ? std::strlen(name) : 0;
		const bool           empty = len < 4 || _stricmp(name + len - 4, ".ds1") != 0;
		if (Ds1Logs.fetch_add(1, std::memory_order_relaxed) < 200 || empty)
			Log(empty, "zones: level %d room at %d,%d (%dx%d): preset %d file %d '%s'%s", id, Field<std::int32_t>(room2, 0x60), Field<std::int32_t>(room2, 0x64),
			    Field<std::int32_t>(room2, 0x68), Field<std::int32_t>(room2, 0x6C), row ? Field<std::int32_t>(row, 0) : -1, index, name ? name : "(null)",
			    empty ? (row && row[0x40] ? ", not a file: the first file is used" : ", not a file") : "");
		if (empty && row && row[0x40]) name = reinterpret_cast<const char*>(row + 0x40), SetField<std::int32_t>(data, 4, 0);
	}
	return At<Ds1LoadFn>(Ds1LoadRva)(mode, cache, name, room2);
}

// Act 1 fill pieces (D2R+0x3FB1A0 (mode, level), the act 1 builder's last step, a tail jump at 0x3FAD8A): trees, ruins, camps,
// cottages... in the free blocks. None in the entry and exit areas (plain ground).
constexpr std::uint64_t FillJump = 0x3FAD8A, FillRva = 0x3FB1A0;
using FillFn = std::uint64_t (*)(std::uint8_t mode, void* level) noexcept;
std::uint64_t HookFill(std::uint8_t mode, void* level) noexcept {
	if (BuildingKind == KindEntry || BuildingKind == KindExit) {
		Log(false, "zones: no fill pieces in level %d (small area)", level ? Field<std::int32_t>(level, 0x1F8) : -1);
		return 0;
	}
	return At<FillFn>(FillRva)(mode, level);
}

// Objects (D2R+0x490410 SpawnUnit, type 2): none in the entry and exit areas (barrels, caches, chests from the tiles) except
// portals and our golden chests.
constexpr std::uint64_t SpawnUnitRva = 0x490410;
constexpr std::array<std::uint8_t, 19> SpawnUnitBytes { 0x40, 0x55, 0x53, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0x70, 0xFC, 0xFF, 0xFF, 0x48, 0x81, 0xEC, 0x90, 0x04, 0x00, 0x00 };
using SpawnUnitHookFn = void* (*)(int type, int cls, int x, int y, void* game, void* room, std::uint64_t flag, std::uint64_t mode, std::uint64_t a9, void* a10) noexcept;
SpawnUnitHookFn  SpawnUnitOrig {};
std::atomic<int> ObjectBlocked { 0 };
void* HookSpawnUnit(int type, int cls, int x, int y, void* game, void* room, std::uint64_t flag, std::uint64_t mode, std::uint64_t a9, void* a10) noexcept {
	if (type == 2 && room && !OwnObject && cls != 59 && cls != 60 && ZoneBase.load(std::memory_order_relaxed) > 0) {
		const int level = At<RoomLevelFn>(0x2EFC10)(room), base = ZoneBase.load(std::memory_order_relaxed);
		if (level >= base && level < base + ZoneSlots && ((level - base) % ZoneParts == KindEntry || (level - base) % ZoneParts == KindExit)) {
			if (ObjectBlocked.fetch_add(1, std::memory_order_relaxed) < 20) Log(false, "zones: no object %d in level %d (small area)", cls, level);
			return nullptr;
		}
	}
	return SpawnUnitOrig(type, cls, x, y, game, room, flag, mode, a9, a10);
}

// Where each of a zone's levels goes (tiles). The dungeon and the stand-in: their own slot's place. The outdoor area: its slot's
// place, the template's size; the entry area to its west and the exit area to its east, touching it (the game links levels
// whose boxes touch, D2R+0x326A10), SmallArea square, centred on its height.
void ZoneBox(std::uint8_t mode, int id, int& x, int& y, int& w, int& h) noexcept {
	const int base = ZoneBase.load(std::memory_order_relaxed), slot = id - base, kind = slot % ZoneParts, first = slot - kind;
	w = h = 0;
	if (kind == KindDungeon || kind == KindStandIn) {
		x = 3300 + (slot % 10) * 220, y = 3200 + (slot / 10) * 230;
		return;
	}
	const int mx = 3300 + (first % 10) * 220, my = 3200 + (first / 10) * 230;
	int mw = 80, mh = 80;
	if (const int tmpl = ZoneTemplate[first].load(); mode <= 3 && Banks[mode].defs && tmpl > 0) {
		const std::uint8_t* def = Banks[mode].defs + tmpl * DefRow;
		for (int d = 0; d < 3; ++d) mw = std::max(mw, Field<std::int32_t>(def, DefSizeX + d * 4)), mh = std::max(mh, Field<std::int32_t>(def, DefSizeY + d * 4));
		mw = std::min((mw + 7) & ~7, 200), mh = std::min((mh + 7) & ~7, 200);
	}
	// One block taller than wide: a 2x2-block area is 4 corner pieces and has no edge piece to open for the passage (0.0.66 pen),
	// a 3rd block along the side facing the outdoor area gives it one.
	const int sh = SmallArea, sy = my + ((mh - sh) / 2 & ~7);
	if (kind == KindEntry) x = mx - SmallArea, y = sy, w = SmallArea, h = sh;
	else if (kind == KindExit) x = mx + mw, y = sy, w = SmallArea, h = sh;
	else x = mx, y = my, w = mw, h = mh;
}

using LinkLevelsFn = void (*)(std::uint8_t mode, void* drlg, int first, int last) noexcept; // D2R+0x3F1B40

void LinkNeighbours(std::uint8_t mode, void* level, int id) noexcept {
	auto* outdoor = Field<std::uint8_t*>(level, 0x18);
	void* drlg    = Field<void*>(level, 0x1C8);
	if (!outdoor || !drlg) return;
	if (Field<void*>(outdoor, 0x338)) return; // linked already (an earlier build of this level)
	const int base = ZoneBase.load(std::memory_order_relaxed), kind = (id - base) % ZoneParts, first = id - kind;
	int       others[2] { 0, 0 };
	if (kind == KindMain) others[0] = first + KindEntry, others[1] = first + KindExit;
	else others[0] = first;
	for (const int other : others) {
		if (!other) continue;
		void* n = At<GetLevelFn>(0x3267C0)(mode, drlg, other);
		if (!n) continue;
		int x = 0, y = 0, w = 0, h = 0;
		ZoneBox(mode, other, x, y, w, h);
		SetField<std::int32_t>(n, 0x24, x), SetField<std::int32_t>(n, 0x28, y);
		if (w > 0) SetField<std::int32_t>(n, 0x2C, w), SetField<std::int32_t>(n, 0x30, h);
	}
	At<LinkLevelsFn>(0x3F1B40)(mode, drlg, id, id);
	std::string text;
	int         count = 0;
	for (auto* o = Field<std::uint8_t*>(outdoor, 0x338); o && count < 16; o = Field<std::uint8_t*>(o, 0x20), ++count) {
		const auto* other = Field<std::uint8_t*>(o, 0);
		text += (text.empty() ? "" : ", ") + std::string("level ") + std::to_string(other ? Field<std::int32_t>(other, 0x1F8) : 0) + " side " +
		        std::to_string(Field<std::int32_t>(o, 8));
	}
	Log(false, "zones: level %d (mode %d) passages: %d [%s]", id, mode, count, text.c_str());
}

// Every build of one of our levels (the server's and the client's, and the client's again when you come back) starts from the
// zone's seed, so they all make the same map; each level gets its own place on the act's map (else their automaps overlap).
// While a zone is built under its template's id, the template's links (LevelDefs Vis) are ours, so its stairs lead to our levels.
std::uint64_t BuildZoneLevel(std::uint8_t mode, void* level, MazeBuildFn build, const char* what) noexcept {
	const int base = ZoneBase.load(std::memory_order_relaxed);
	const int id   = level ? Field<std::int32_t>(level, 0x1F8) : 0;
	if (base <= 0 || id < base || id >= base + ZoneSlots) return build(mode, level);
	const int slot = id - base, tmpl = ZoneTemplate[slot].load();
	if (const std::uint32_t seed = ZoneSeed[slot].load()) At<SeedFn>(SeedInitRva)(static_cast<std::uint8_t*>(level) + 0x1E4, seed);
	// Tiles, past act 5's own levels (y up to ~3060); subtiles (x5) must stay under 32768: x 3300-5480, y 3200-3890.
	{
		int x = 0, y = 0, w = 0, h = 0;
		ZoneBox(mode, id, x, y, w, h);
		SetField<std::int32_t>(level, 0x24, x), SetField<std::int32_t>(level, 0x28, y);
		if (w > 0) SetField<std::int32_t>(level, 0x2C, w), SetField<std::int32_t>(level, 0x30, h); // outdoor levels: not sized by the game
	}
	if (!tmpl) {
		const std::uint64_t result = build(mode, level);
		Log(false, "zones: level %d built (%s) at %d,%d", id, what, Field<std::int32_t>(level, 0x24), Field<std::int32_t>(level, 0x28));
		return result;
	}
	std::uint8_t* defs = mode <= 3 ? Banks[mode].defs : nullptr;
	std::int32_t  saved[16] {};
	if (defs) {
		std::memcpy(saved, defs + tmpl * DefRow + DefVis, sizeof(saved)); // Vis[8] then Warp[8]
		std::memcpy(defs + tmpl * DefRow + DefVis, defs + id * DefRow + DefVis, sizeof(saved));
	}
	const bool outdoor = build == &OutdoorBuild; // linked under our own id (its Vis), built under the template's
	// The game's passages to the neighbouring areas: its list of them (outdoor data +0x338, made by D2R+0x3F1B40 from the level's
	// Vis with Warp -1 when the act is laid out, which never happens for ours), made here with the neighbours placed next to us.
	if (outdoor) LinkNeighbours(mode, level, id);
	BuildingKind = slot % ZoneParts;
	SetField<std::int32_t>(level, 0x1F8, tmpl);
	if (outdoor) {
		Log(false, "zones: level %d (outdoor, as level %d, mode %d) building at %d,%d, box %dx%d, type %d, outdoor data %p", id, tmpl, mode,
		    Field<std::int32_t>(level, 0x24), Field<std::int32_t>(level, 0x28), Field<std::int32_t>(level, 0x2C), Field<std::int32_t>(level, 0x30),
		    Field<std::int32_t>(level, 0), Field<void*>(level, 0x18));
		OutdoorTrace.fetch_add(1);
	}
	const std::uint64_t result = build(mode, level);
	if (outdoor) OutdoorTrace.fetch_sub(1);
	BuildingKind = -1;
	SetField<std::int32_t>(level, 0x1F8, id);
	if (defs) std::memcpy(defs + tmpl * DefRow + DefVis, saved, sizeof(saved));
	int rooms = 0, stairs = 0;
	for (auto* r = Field<std::uint8_t*>(level, 0x10); r && rooms < 4096; r = Field<std::uint8_t*>(r, 0x48), ++rooms)
		if (Field<std::uint32_t>(r, 0x50) & 0xFF0) ++stairs;
	Log(false, "zones: level %d built (%s) as level %d at %d,%d, box %dx%d, %d rooms, %d with stairs", id, what, tmpl, Field<std::int32_t>(level, 0x24),
	    Field<std::int32_t>(level, 0x28), Field<std::int32_t>(level, 0x2C), Field<std::int32_t>(level, 0x30), rooms, stairs);
	return result;
}

std::uint64_t HookMazeBuild(std::uint8_t mode, void* level) noexcept { return BuildZoneLevel(mode, level, MazeBuildOrig, "maze"); }

// Outdoor areas (D2R+0x32720E: call 0x3E2B5CE (mode, level), a slot the loader's core fills): our outdoor copies the same way.
std::uint64_t HookOutdoorBuild(std::uint8_t mode, void* level) noexcept { return BuildZoneLevel(mode, level, &OutdoorBuild, "outdoor"); }

// Diagnosis (0.0.50): each step of the outdoor build logged while one of our outdoor levels builds, to find the one that crashes.
struct TraceSite {
	std::uint64_t site, target;
	const char*   name;
};
constexpr TraceSite TraceSites[] {
	{ 0x3EE3B7, 0x3E2B5C8, "act of the level" },       { 0x3EE3D4, 0x3F9B80, "level box list" },
	{ 0x3EE4DB, 0x3FAC10, "act 1 outdoor builder" },  { 0x3EE4FA, 0x3277E0, "after the act builder" },
	{ 0x3FAC36, 0x3FAF60, "act 1 step A" },           { 0x3FAC42, 0x3EFA20, "act 1 step B" },
	{ 0x3FAC4E, 0x3EFAF0, "act 1 step C" },           { 0x3FAC79, 0x3EE010, "border side 0" },
	{ 0x3FAC85, 0x3FB710, "act 1 step D" },           { 0x3FAC9B, 0x3EE010, "border side 1" },
	{ 0x3FACB1, 0x3EE010, "border side 2" },          { 0x3FACBD, 0x3FB910, "act 1 step E" },
	{ 0x3FACD3, 0x3EE010, "border side 3" },          { 0x3FACDB, 0x3FF5C0, "act 1 step F" },
	{ 0x3FAD59, 0x3E2B5E6, "act 1 step G (core)" },   { 0x3FAD74, 0x3E2B5D4, "act 1 step H (core)" },
	{ 0x3EE52C, 0x32C360, "level lookup" },           { 0x3EE663, 0x3EDDA0, "room step" },
};
using Trace4Fn = std::uint64_t (*)(std::uint64_t a, std::uint64_t b, std::uint64_t c, std::uint64_t d) noexcept;
template <int N>
std::uint64_t HookTrace(std::uint64_t a, std::uint64_t b, std::uint64_t c, std::uint64_t d) noexcept {
	const bool on = OutdoorTrace.load(std::memory_order_relaxed) > 0;
	// Act 1 step G runs for Cold Plains, Stony Field, Dark Wood and Black Marsh only: the levels with a waypoint. Not in our copies.
	// Act 1 step H places the area's dungeon entrance (Vis with stairs): the entry and exit areas have none.
	// Step E (D2R+0x3FB910) places the act's features (cliffs/river at fixed cells from the far edges: grid width - 7, height - 6)
	// and the cave entrance: on a 2x2-cell area they land outside the grid (OutWild.cpp:595 'fAdded', then a bad preset crashed).
	if (on && (TraceSites[N].site == 0x3FAD74 || TraceSites[N].site == 0x3FACBD) && (BuildingKind == KindEntry || BuildingKind == KindExit)) {
		Log(false, "outdoor trace %d (%s): skipped (small area)", N, TraceSites[N].name);
		return 1;
	}
	if (on && TraceSites[N].site == 0x3FAD59) {
		Log(false, "outdoor trace %d (%s): skipped (the waypoint tile)", N, TraceSites[N].name);
		return 1;
	}
	if (on) Log(false, "outdoor trace %d (%s): start, args %llX %llX %llX %llX", N, TraceSites[N].name, static_cast<unsigned long long>(a),
	            static_cast<unsigned long long>(b), static_cast<unsigned long long>(c), static_cast<unsigned long long>(d));
	const std::uint64_t result = At<Trace4Fn>(TraceSites[N].target)(a, b, c, d);
	if (on) Log(false, "outdoor trace %d (%s): done, result %llX", N, TraceSites[N].name, static_cast<unsigned long long>(result));
	return result;
}
template <int... N>
void InstallTraces(std::integer_sequence<int, N...>) noexcept {
	const void* hooks[] { reinterpret_cast<const void*>(&HookTrace<N>)... };
	int         done = 0;
	for (int i = 0; i < static_cast<int>(sizeof...(N)); ++i)
		if (PatchCall(TraceSites[i].site, TraceSites[i].target, hooks[i])) ++done;
		else Log(true, "zones: outdoor trace %d: could not patch D2R+0x%llX", i, static_cast<unsigned long long>(TraceSites[i].site));
	Log(false, "zones: outdoor trace on (%d of %d steps)", done, static_cast<int>(sizeof...(N)));
}

// Client tile pass around the player (D2R+0x21D090 (self, tile, flag, x, ...)): reads the tile's graphics (+0x20) with no null
// check. In outdoor zones some tiles have none (0.0.51 crashed there a few seconds in): those are skipped.
constexpr std::uint64_t TilePassRva = 0x21D090, TilePassCalls[2] { 0x21CD9F, 0x21CFCE };
using TilePassFn = std::uint64_t (*)(void* self, void* tile, std::uint64_t a3, std::uint64_t a4, std::uint64_t a5, std::uint64_t a6, std::uint64_t a7) noexcept;
std::atomic<int> TileSkipLogs { 0 };
std::uint64_t HookTilePass(void* self, void* tile, std::uint64_t a3, std::uint64_t a4, std::uint64_t a5, std::uint64_t a6, std::uint64_t a7) noexcept {
	if (tile && !(Field<std::uint32_t>(tile, 0x18) & 0x408) && !Field<void*>(tile, 0x20)) {
		if (TileSkipLogs.fetch_add(1, std::memory_order_relaxed) < 20) {
			char raw[200] {};
			int  at = 0;
			for (int i = 0; i < 0x48 / 4 && at < 190; ++i) at += std::snprintf(raw + at, sizeof(raw) - at, "%08X ", Field<std::uint32_t>(tile, i * 4));
			Log(false, "zones: tile without graphics skipped at %d,%d (args %d %d %d), tile %p: %s", static_cast<int>(a4), static_cast<int>(a5),
			    static_cast<int>(a3), static_cast<int>(a6), static_cast<int>(a7), tile, raw);
		}
		return 0;
	}
	return At<TilePassFn>(TilePassRva)(self, tile, a3, a4, a5, a6, a7);
}

// Automap layers (client, D2R+0xD1710 switches the current layer, D2R+0x2A2CF68): the old layer is saved to the character's .map
// file (0xD6230) and cleared (0xD4D60 on its 4 lists), then the new one loaded from it (0xD5710). The file has a table of 100 layers;
// ours (>= 100) stay in memory instead: not saved, not cleared, not loaded.
constexpr std::uint64_t CurLayerRva = 0x2A2CF68, LayerSaveRva = 0xD6230, LayerLoadRva = 0xD5710, LayerClearRva = 0xD4D60;
constexpr std::uint64_t LayerSaveCall = 0xD1869, LayerLoadCall = 0xD18C8, LayerClearCalls[4] { 0xD187E, 0xD188E, 0xD189E, 0xD18AE };
using LayerTwoFn = void (*)(void* a, void* b) noexcept;
using LayerOneFn = void (*)(void* list) noexcept;
std::atomic<int> LayerLogs { 0 };

int CurrentLayerNo() noexcept {
	const auto* layer = *reinterpret_cast<const std::int32_t* const*>(Base + CurLayerRva);
	return layer ? *layer : -1;
}
void HookLayerSave(void* a, void* b) noexcept {
	if (const int no = CurrentLayerNo(); no >= 100) {
		if (LayerLogs.fetch_add(1) < 20) Log(false, "automap: layer %d kept in memory (not saved to the .map file)", no);
		return;
	}
	At<LayerTwoFn>(LayerSaveRva)(a, b);
}
void HookLayerClear(void* list) noexcept {
	if (CurrentLayerNo() >= 100) return;
	At<LayerOneFn>(LayerClearRva)(list);
}
void HookLayerLoad(void* a, void* b) noexcept {
	if (const int no = CurrentLayerNo(); no >= 100) {
		if (LayerLogs.fetch_add(1) < 20) Log(false, "automap: layer %d from memory (not loaded from the .map file)", no);
		return;
	}
	At<LayerTwoFn>(LayerLoadRva)(a, b);
}

// Roran's name (the client's unit name, D2R+0x9A1B0 (unit)).
constexpr std::uint64_t UnitNameRva = 0x9A1B0;
constexpr std::array<std::uint8_t, 12> UnitNameBytes { 0x40, 0x53, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x40, 0x4C, 0x8B, 0xF1 };
using UnitNameFn = const char* (*)(void* unit) noexcept;
UnitNameFn UnitNameOrig {};

const char* HookUnitName(void* unit) noexcept {
	if (unit && Field<std::uint32_t>(unit, 0) == 1 && Field<std::uint32_t>(unit, 4) == 534 && CurrentLevel.load(std::memory_order_relaxed) == 109) return "Roran Stronghammer";
	return UnitNameOrig(unit);
}

// Roran Stronghammer: an Act 5 Townguard (act5barb3) put at the fire south of Harrogath's waypoint once a player is near.
// Spawn: D2R+0x492CF0 (game, room, x, y, monster, mode, a7, a8), as the game's own calls do (a7 -1 then 4, a8 0x42).
// Room at a spot: D2R+0x38DFB0 (room near it, x, y).
constexpr std::uint32_t RoranClass = 534; // act5pow: a barbarian without weapons or helm
constexpr int           RoranX = 5145, RoranY = 5107;
using SpawnFn  = void* (*)(void* game, void* room, int x, int y, int monster, int mode, int a7, std::uint16_t a8) noexcept;
using RoomAtFn = void* (*)(void* room, int x, int y) noexcept;

void SpawnRoran(std::uint8_t* game) noexcept {
	GameMods* g = ModsOf(game);
	if (!g || g->roranDone) return;
	auto* client = Field<std::uint8_t*>(game, GameClients);
	for (int guard = 0; client && guard < 64; ++guard, client = Field<std::uint8_t*>(client, ClientNext)) {
		void* player = Field<std::uint32_t>(client, 4) == 4 ? At<ClientPlayerFn>(0x484C90)(client, 0) : nullptr;
		void* room   = player ? At<UnitRoomFn>(0x34B440)(player) : nullptr;
		if (!room || At<RoomLevelFn>(0x2EFC10)(room) != 109) continue;
		void* spot = At<RoomAtFn>(0x38DFB0)(room, RoranX, RoranY);
		if (!spot) continue; // not loaded near this player yet
		void* roran = At<SpawnFn>(0x492CF0)(game, spot, RoranX, RoranY, RoranClass, 1, -1, 0x42);
		if (!roran) roran = At<SpawnFn>(0x492CF0)(game, spot, RoranX, RoranY, RoranClass, 1, 4, 0x42);
		if (roran) {
			g->roranDone = true;
			Coord at {};
			At<GetCoordsFn>(0x34A040)(roran, &at);
			Log(false, "Roran Stronghammer: unit %u in Harrogath at %d,%d (asked %d,%d)", Field<std::uint32_t>(roran, 8), at.x, at.y, RoranX, RoranY);
		} else if (++g->roranTries >= 5) {
			g->roranDone = true;
			Log(true, "Roran Stronghammer: the game refused to spawn him at %d,%d", RoranX, RoranY);
		}
		return;
	}
}

// Monsters of the open zone that come near a player (met) and die after that (killed), checked 5 times a second.
const char* const KindNames[4] { "monster", "champion", "unique", "minion" };
void TrackMeetings(std::uint8_t* game) noexcept {
	GameMods* g = ModsOf(game);
	if (!g || !g->zoneId || GetTickCount64() - g->meetTick < 200) return;
	g->meetTick = GetTickCount64();
	Coord players[8] {};
	int   count  = 0;
	auto* client = Field<std::uint8_t*>(game, GameClients);
	for (int guard = 0; client && guard < 64 && count < 8; ++guard, client = Field<std::uint8_t*>(client, ClientNext)) {
		void* player = Field<std::uint32_t>(client, 4) == 4 ? At<ClientPlayerFn>(0x484C90)(client, 0) : nullptr;
		void* room   = player ? At<UnitRoomFn>(0x34B440)(player) : nullptr;
		if (!room) continue;
		if (const int lv = At<RoomLevelFn>(0x2EFC10)(room); lv != g->zoneId && lv != g->zoneId + 1) continue;
		At<GetCoordsFn>(0x34A040)(player, &players[count++]);
	}
	if (!count) return;
	std::vector<std::pair<std::uint32_t, std::uint8_t>> mons;
	AcquireSRWLockShared(&g->modLock);
	try {
		mons.reserve(g->zoneMons.size());
		for (const auto& [id, kind] : g->zoneMons) {
			const auto met = g->zoneMet.find(id);
			if (met == g->zoneMet.end()) mons.emplace_back(id, kind);
			else if (!(met->second & 0x80)) mons.emplace_back(id, kind | 0x40); // met, alive so far
		}
	} catch (...) {}
	ReleaseSRWLockShared(&g->modLock);
	for (const auto& [id, flags] : mons) {
		void* unit = At<FindUnitFn>(0x48FE80)(game, 1, id);
		if (!unit) continue;
		const int  kind = flags & 3;
		const auto mode = Field<std::uint32_t>(unit, 0x0C);
		const bool dead = mode == 0 || mode == 12;
		if (flags & 0x40) {
			if (!dead) continue;
			AcquireSRWLockExclusive(&g->modLock);
			g->zoneMet[id] |= 0x80, ++g->zoneKilled[kind];
			ReleaseSRWLockExclusive(&g->modLock);
			if (kind == 1 || kind == 2) Log(false, "zone level %d: killed %s %u (class %u)", g->zoneId, KindNames[kind], id, Field<std::uint32_t>(unit, 4));
			continue;
		}
		if (dead) continue;
		Coord at {};
		At<GetCoordsFn>(0x34A040)(unit, &at);
		bool close = false;
		for (int i = 0; i < count && !close; ++i) close = std::abs(at.x - players[i].x) <= MeetRadius && std::abs(at.y - players[i].y) <= MeetRadius;
		if (!close) continue;
		AcquireSRWLockExclusive(&g->modLock);
		try {
			g->zoneMet[id] = static_cast<std::uint8_t>(kind), ++g->zoneSeen[kind];
		} catch (...) {}
		ReleaseSRWLockExclusive(&g->modLock);
		if (kind == 1 || kind == 2) Log(false, "zone level %d: met %s %u (class %u) at %d,%d", g->zoneId, KindNames[kind], id, Field<std::uint32_t>(unit, 4), at.x, at.y);
	}
}

std::uint64_t HookFrameCall(void* game) noexcept {
	if (game) SpawnRoran(static_cast<std::uint8_t*>(game)), SendStandInsToTown(static_cast<std::uint8_t*>(game)), TrackMeetings(static_cast<std::uint8_t*>(game)),
		          ZoneExitArea(static_cast<std::uint8_t*>(game)), MoveOutOfSafeArea(static_cast<std::uint8_t*>(game));
	if (game && PortalRequested.exchange(false)) OpenZonePortal(static_cast<std::uint8_t*>(game));
	if (game && CloseRequested.exchange(false)) CloseZonePortal(static_cast<std::uint8_t*>(game));
	return At<FrameCallFn>(FrameCallTarget)(game);
}

void StartZones(const D2RL::PluginContext* context) noexcept {
	if (!Lifecycle) return Log(true, "zones: no lifecycle service, no TZ Plus zones");
	const D2RL::Lifecycle::DataTablesLoadedListener listener {
		.structSize = D2RL::Lifecycle::DataTablesLoadedListenerSize,
		.callback   = OnTablesLoaded,
	};
	D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
	if (Lifecycle->registerDataTablesLoadedListener(context, &listener, &handle) != D2RL::Lifecycle::Result::Success)
		return Log(true, "zones: could not listen to table loads, no TZ Plus zones");
	if (!PatchCall(FrameCallRva, FrameCallTarget, reinterpret_cast<const void*>(&HookFrameCall)))
		Log(true, "zones: could not patch the game update at D2R+0x%llX, no zone portals", static_cast<unsigned long long>(FrameCallRva));
	for (const std::uint64_t site : LabelCalls)
		if (!PatchCall(site, LabelRva, reinterpret_cast<const void*>(&HookLabel)))
			Log(true, "zones: could not patch D2R+0x%llX (hover)", static_cast<unsigned long long>(site));
	for (int i = 0; i < 2; ++i)
		if (!PatchCall(RotationClearCalls[i], RotationClearRva, reinterpret_cast<const void*>(&HookRotationClear)) ||
		    !PatchCall(RotationCheckCalls[i], RotationCheckRva, reinterpret_cast<const void*>(&HookRotationCheck)))
			Log(true, "could not patch the rotation at D2R+0x%llX: Hell games still get the half-hour terror zone message", static_cast<unsigned long long>(RotationClearCalls[i]));
	if (!PatchJump(FillJump, FillRva, reinterpret_cast<const void*>(&HookFill)))
		Log(true, "zones: could not patch D2R+0x%llX (fill pieces)", static_cast<unsigned long long>(FillJump));
	if (!PatchCall(Ds1LoadCall, Ds1LoadRva, reinterpret_cast<const void*>(&HookDs1Load), { 0x4D, 0x89, 0xF1 })) // mov r9, r14 (room2)
		Log(true, "zones: could not patch D2R+0x%llX (preset files)", static_cast<unsigned long long>(Ds1LoadCall));
	if (!PatchCall(BorderPlaceCall, BorderPlaceRva, reinterpret_cast<const void*>(&HookBorderPlace)))
		Log(true, "zones: could not patch D2R+0x%llX (border pieces)", static_cast<unsigned long long>(BorderPlaceCall));
	if (!PatchCall(OutdoorBuildCall, OutdoorBuildRva, reinterpret_cast<const void*>(&HookOutdoorBuild)))
		Log(true, "zones: outdoor zones can't be built, could not patch D2R+0x%llX", static_cast<unsigned long long>(OutdoorBuildCall));
	for (const std::uint64_t site : PresetSpawnCalls)
		if (!PatchCall(site, PresetSpawnRva, reinterpret_cast<const void*>(&HookPresetSpawn)))
			Log(true, "zones: could not patch D2R+0x%llX (preset monsters)", static_cast<unsigned long long>(site));
	for (const std::uint64_t site : TilePassCalls)
		if (!PatchCall(site, TilePassRva, reinterpret_cast<const void*>(&HookTilePass)))
			Log(true, "zones: could not patch D2R+0x%llX (tile pass)", static_cast<unsigned long long>(site));
	InstallTraces(std::make_integer_sequence<int, static_cast<int>(sizeof(TraceSites) / sizeof(TraceSites[0]))>());
	{
		bool cleared = true;
		for (const std::uint64_t site : LayerClearCalls) cleared = PatchCall(site, LayerClearRva, reinterpret_cast<const void*>(&HookLayerClear)) && cleared;
		const bool saved  = PatchCall(LayerSaveCall, LayerSaveRva, reinterpret_cast<const void*>(&HookLayerSave));
		const bool loaded = PatchCall(LayerLoadCall, LayerLoadRva, reinterpret_cast<const void*>(&HookLayerLoad));
		LayersSafe        = saved && loaded;
		if (!LayersSafe || !cleared)
			Log(true, "zones: automap layer hooks failed (save %d, load %d, clear %d): zones share their dungeon's map", saved, loaded, cleared);
	}
	if (!PatchCall(PortalSpotCallRva, RoomHasRva, reinterpret_cast<const void*>(&HookPortalSpot), { 0x49, 0x89, 0xE9 })) // mov r9, rbp
		Log(true, "zones: portal spot not logged, could not patch D2R+0x%llX", static_cast<unsigned long long>(PortalSpotCallRva));
	{
		void* original = nullptr;
		if (!context->InstallInlineHook(LevelWarpRva, LevelWarpBytes.data(), static_cast<std::uint32_t>(LevelWarpBytes.size()), reinterpret_cast<void*>(&HookWarp), &original) || !original)
			Log(true, "zones: the entry stairs lead nowhere safe, could not hook D2R+0x%llX", static_cast<unsigned long long>(LevelWarpRva));
		else WarpOrig = reinterpret_cast<LevelWarpFn>(original);
		original = nullptr;
		if (!context->InstallInlineHook(UnitNameRva, UnitNameBytes.data(), static_cast<std::uint32_t>(UnitNameBytes.size()), reinterpret_cast<void*>(&HookUnitName), &original) || !original)
			Log(true, "zones: Roran keeps the Townguard's name, could not hook D2R+0x%llX", static_cast<unsigned long long>(UnitNameRva));
		else UnitNameOrig = reinterpret_cast<UnitNameFn>(original);
		original = nullptr;
		if (!context->InstallInlineHook(SpawnUnitRva, SpawnUnitBytes.data(), static_cast<std::uint32_t>(SpawnUnitBytes.size()), reinterpret_cast<void*>(&HookSpawnUnit), &original) || !original)
			Log(true, "zones: objects not blocked in small areas, could not hook D2R+0x%llX", static_cast<unsigned long long>(SpawnUnitRva));
		else SpawnUnitOrig = reinterpret_cast<SpawnUnitHookFn>(original);
		original = nullptr;
		if (!context->InstallInlineHook(SpawnCoreRva, SpawnCoreBytes.data(), static_cast<std::uint32_t>(SpawnCoreBytes.size()), reinterpret_cast<void*>(&HookSpawnCore), &original) || !original)
			Log(true, "zones: level 2 gets monsters, could not hook D2R+0x%llX", static_cast<unsigned long long>(SpawnCoreRva));
		else SpawnCoreOrig = reinterpret_cast<SpawnCoreFn>(original);
		original = nullptr;
		if (!context->InstallInlineHook(MazeBuildRva, MazeBuildBytes.data(), static_cast<std::uint32_t>(MazeBuildBytes.size()), reinterpret_cast<void*>(&HookMazeBuild), &original) || !original)
			Log(true, "zones: no exit stairs, could not hook D2R+0x%llX", static_cast<unsigned long long>(MazeBuildRva));
		else MazeBuildOrig = reinterpret_cast<MazeBuildFn>(original);
	}
	if (!PatchCall(LevelSeedCallRva, SeedInitRva, reinterpret_cast<const void*>(&HookLevelSeed)))
		Log(true, "zones: layouts follow the map seed, could not patch D2R+0x%llX", static_cast<unsigned long long>(LevelSeedCallRva));
	if (!PatchCall(PortalNameCallRva, ObjectLevelRva, reinterpret_cast<const void*>(&HookPortalLevel)))
		Log(true, "zones: portals keep the game's name, could not patch D2R+0x%llX", static_cast<unsigned long long>(PortalNameCallRva));
	std::uint8_t thunk[6] {};
	if (Read(Base + StringByKeyThunkRva, thunk, 6) && thunk[0] == 0xFF && thunk[1] == 0x25) {
		std::int32_t rel = 0;
		std::memcpy(&rel, thunk + 2, 4);
		const std::uint64_t slot = StringByKeyThunkRva + 6 + rel;
		std::uint8_t        current[8] {}, ours[8] {};
		const auto          hook = reinterpret_cast<std::uint64_t>(&HookStringByKey);
		std::memcpy(ours, &hook, 8);
		if (Read(Base + slot, current, 8)) {
			std::memcpy(&StringByKeyOrig, current, 8);
			if (!StringByKeyOrig || !Context->PatchBytes(slot, current, 8, ours, 8)) StringByKeyOrig = nullptr, Log(true, "zones: zone names off, could not patch D2R+0x%llX", static_cast<unsigned long long>(slot));
		}
	} else Log(true, "zones: zone names off, D2R+0x%llX differs", static_cast<unsigned long long>(StringByKeyThunkRva));
}

// ---- console: "tzplus" shows this game's mods ----

D2RL::ConsoleCommandResult __cdecl Command(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept {
	const std::string_view args = command && command->args ? std::string_view(command->args, command->argsLength) : std::string_view();
	if (args.find("close") != std::string_view::npos) {
		CloseRequested.store(true);
		Context->WriteConsoleMessage("TZ Plus: closing the zone portal");
		return D2RL::ConsoleCommandResult::Handled;
	}
	if (args.find("portal") != std::string_view::npos) {
		AcquireSRWLockShared(&PortalLock);
		const std::string last = PortalResult;
		ReleaseSRWLockShared(&PortalLock);
		if (!last.empty()) Context->WriteConsoleMessage(("last time: " + last).c_str());
		PortalRequested.store(true);
		Context->WriteConsoleMessage("TZ Plus: opening a zone portal next to you (the result is in tz-plus.log and shown on the next \"tzplus portal\")");
		return D2RL::ConsoleCommandResult::Handled;
	}
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
	Gui::Commands::Post("toggle");
	const int index = At<LocalIndexFn>(0x8B2D0)();
	if (void* player = index >= 0 && index < 8 ? At<PlayerByIndexFn>(0x9A480)(index) : nullptr) {
		char line[160];
		std::snprintf(line, sizeof(line), "You: gold find %d (TZ Plus %d), magic find %d (TZ Plus %d)", GetStat(player, StatGoldFind),
		              GetBaseStat(player, StatGoldFind), GetStat(player, StatMagicFind), GetBaseStat(player, StatMagicFind));
		Context->WriteConsoleMessage(line);
		Log(false, "%s", line);
	}
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
	(void)context->RegisterConsoleCommand("tzplus", &Command, "Opens the TZ Plus window and shows this game's mods and rewards.");
	StartDisplay(context);
	StartWindow(context);
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
	// mov rdx, r14 (game); mov r8, [rsp+0x38] (room: the init's [rsp+0x30], past our return address)
	if (!PatchCall(TerrorCheckCallRva, 0x3AF200, reinterpret_cast<const void*>(&HookTerrorCheck), { 0x4C, 0x89, 0xF2, 0x4C, 0x8B, 0x44, 0x24, 0x38 }))
		Log(true, "TZ Plus %s: monster mods off, could not patch D2R+0x%llX", PluginVersion, static_cast<unsigned long long>(TerrorCheckCallRva));
	if (!PatchCall(MonInitCallRva, MonInitRva, reinterpret_cast<const void*>(&HookMonInit)) ||
	    !PatchCall(LevelUpCallRva, 0x49AEB0, reinterpret_cast<const void*>(&HookLevelUp)))
		Log(true, "TZ Plus %s: level 99 cap partly off, could not patch the monster level calls", PluginVersion);
	if (!PatchCall(MapLineCallRva, AppendCharRva, reinterpret_cast<const void*>(&HookMapLine)))
		Log(true, "TZ Plus %s: the mods are not shown on the map, could not patch D2R+0x%llX", PluginVersion, static_cast<unsigned long long>(MapLineCallRva));
	{
		void* original = nullptr;
		if (!context->InstallInlineHook(ModEventRva, ModEventBytes.data(), static_cast<std::uint32_t>(ModEventBytes.size()), reinterpret_cast<void*>(&HookModEvent), &original) || !original)
			Log(true, "TZ Plus %s: mod events keep the game's rules (Multiple Shots, Teleportation on normal monsters), could not hook D2R+0x%llX", PluginVersion,
			    static_cast<unsigned long long>(ModEventRva));
		else ModEventOrig = reinterpret_cast<std::uint64_t>(original);
	}
	{
		const std::uint8_t empty[8] {};
		const auto         handler = reinterpret_cast<std::uint64_t>(&TeleportWhenHit);
		std::uint8_t       bytes[8];
		std::memcpy(bytes, &handler, 8);
		if (!Context->PatchBytes(TeleportHitSlotRva, empty, 8, bytes, 8))
			Log(true, "TZ Plus %s: Teleportation keeps the game's behaviour, could not patch D2R+0x%llX", PluginVersion,
			    static_cast<unsigned long long>(TeleportHitSlotRva));
	}
	{
		void* original = nullptr;
		if (!context->InstallInlineHook(ThinkRva, ThinkBytes.data(), static_cast<std::uint32_t>(ThinkBytes.size()), reinterpret_cast<void*>(&HookThink), &original) || !original)
			Log(true, "TZ Plus %s: Teleportation off, could not hook D2R+0x%llX", PluginVersion, static_cast<unsigned long long>(ThinkRva));
		else ThinkOrig = reinterpret_cast<std::uint64_t>(original);
		original = nullptr;
		if (!context->InstallInlineHook(CurseRva, CurseBytes.data(), static_cast<std::uint32_t>(CurseBytes.size()), reinterpret_cast<void*>(&HookCurse), &original) || !original)
			Log(true, "TZ Plus %s: Cursed keeps the game's curse level, could not hook D2R+0x%llX", PluginVersion, static_cast<unsigned long long>(CurseRva));
		else CurseOrig = reinterpret_cast<std::uint64_t>(original);
		original = nullptr;
		if (!context->InstallInlineHook(ClientModEventRva, ClientModEventBytes.data(), static_cast<std::uint32_t>(ClientModEventBytes.size()), reinterpret_cast<void*>(&HookClientModEvent), &original) || !original)
			Log(true, "TZ Plus %s: Multiple Shots not drawn on normal monsters, could not hook D2R+0x%llX", PluginVersion, static_cast<unsigned long long>(ClientModEventRva));
		else ClientModEventOrig = reinterpret_cast<std::uint64_t>(original);
	}
	for (const std::uint64_t site : PoisonCallRvas)
		if (!PatchCall(site, PoisonRva, reinterpret_cast<const void*>(&HookPoison)))
			Log(true, "TZ Plus %s: poison hits may stack, could not patch D2R+0x%llX", PluginVersion, static_cast<unsigned long long>(site));
	if (!PatchCall(MissileEventCallRva, MissileEventRva, reinterpret_cast<const void*>(&HookMissileEvent)))
		Log(true, "TZ Plus %s: Multiple Shots and ranged poison off, could not patch D2R+0x%llX", PluginVersion, static_cast<unsigned long long>(MissileEventCallRva));
	if (!PatchCall(NameTerrorCallRva, 0x3AF200, reinterpret_cast<const void*>(&HookNameTerror)))
		Log(true, "TZ Plus %s: minions show no mods line, could not patch D2R+0x%llX", PluginVersion, static_cast<unsigned long long>(NameTerrorCallRva));
	if (!PatchCall(NameSpecialCallRva, 0x38E0E0, reinterpret_cast<const void*>(&HookNameSpecial)))
		Log(true, "TZ Plus %s: minions show no mods line, could not patch D2R+0x%llX", PluginVersion, static_cast<unsigned long long>(NameSpecialCallRva));
	if (!PatchCall(NormalNamesCallRva, UModNamesRva, reinterpret_cast<const void*>(&HookNormalNames)) ||
	    !PatchCall(EliteNamesCallRva, UModNamesRva, reinterpret_cast<const void*>(&HookEliteNames)))
		Log(true, "TZ Plus %s: monster names show the game's mods only, could not patch their calls", PluginVersion);
	if (!PatchCall(TownCheckCallRva, InZoneListRva, reinterpret_cast<const void*>(&HookTownCheck), { 0x4C, 0x89, 0xE2 })) // mov rdx, r12
		Log(true, "TZ Plus %s: the mods are not shown on the town map, could not patch D2R+0x%llX", PluginVersion, static_cast<unsigned long long>(TownCheckCallRva));
	{
		const SpawnHook spawns[] {
			{ 0x499210, { 0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C }, reinterpret_cast<void*>(&HookSpawn<0>) },
			{ 0x499880, { 0x48, 0x89, 0x54, 0x24, 0x10, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24 }, reinterpret_cast<void*>(&HookSpawn<1>) },
			{ 0x499BA0, { 0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0x48, 0xFF, 0xFF }, reinterpret_cast<void*>(&HookSpawn<2>) },
			{ 0x49AB40, { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x6C, 0x24, 0x20, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x41 }, reinterpret_cast<void*>(&HookSpawn<3>) },
			{ 0x49AC30, { 0x40, 0x55, 0x41, 0x54, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xB9, 0x48, 0x81, 0xEC, 0xA0, 0x00 }, reinterpret_cast<void*>(&HookSpawn<4>) },
			{ 0x4996B0, { 0x40, 0x55, 0x57, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x4D, 0x8B, 0xF8, 0x48, 0x8B, 0xEA, 0x48 }, reinterpret_cast<void*>(&HookSpawn<5>) },
			{ 0x498810, { 0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C }, reinterpret_cast<void*>(&HookSpawn<6>) },
			{ 0x49AF30, { 0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x5B, 0x08, 0x49, 0x89, 0x6B, 0x10, 0x56, 0x57, 0x41, 0x54, 0x41 }, reinterpret_cast<void*>(&HookSpawn<7>) },
			// A champion's pack (D2R+0x50B1B0 (game, leader, room, monster)): 1-3 more made as normal monsters (0x492BF0), then
			// turned into champions (0x49AB40): their mods and count wait until they are.
			{ 0x50B1B0, { 0x48, 0x89, 0x6C, 0x24, 0x18, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B }, reinterpret_cast<void*>(&HookSpawn<8>) },
		};
		int failed = 0;
		for (int i = 0; i < 9; ++i) {
			void* original = nullptr;
			if (!context->InstallInlineHook(spawns[i].rva, spawns[i].bytes.data(), static_cast<std::uint32_t>(spawns[i].bytes.size()), spawns[i].hook, &original) ||
			    !original)
				++failed, Log(true, "elite mods: could not hook D2R+0x%llX", static_cast<unsigned long long>(spawns[i].rva));
			else SpawnOrig[i] = reinterpret_cast<std::uint64_t>(original);
		}
		(void)failed;
	}
	StartZones(context);
	Log(false, "TZ Plus %s: %s (Nightmare %s, Hell %s)", PluginVersion, Config.enabled ? "active" : "turned off", Config.nightmare ? "on" : "off",
	    Config.hell ? "on" : "off");
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	Menu.Stop();
	D2RGui::WindowHook::Stop();
}
