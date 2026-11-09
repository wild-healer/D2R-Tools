// Smarter Mercenaries: replaces the game's one-size-fits-all mercenary AI with
// behaviour that fits each mercenary type.
//
// Vanilla, every hireling runs the same "Hireable" AI (monai index 62 in the
// AI table at D2R+0x2396E70): attack whatever is closest within 25, pick a
// skill by a dice roll from hireling.txt, no idea of immunities or of what the
// player is fighting. This plugin keeps the game's own following/teleport and
// missile dodging and changes two decisions:
//   - which monster the mercenary fights (target priority + leash);
//   - what it does once it fights (keep distance, skill choice per type).
//
// Game code used (Steam 3.3.94995 RVAs), all on the server thread:
//   Hireable      0x5BEAC0 (Game*, Unit*, AiTick*)  the Hireable think function
//   SelectTarget  0x595F80 (Game*, Unit*, int* dist, int* inMelee) -> Unit*
//   Combat        0x5BF6E0 (Game*, Unit*, u16 class, owner, target, seed, AiTick*, u16 params[4])
//   SelectAction  0x5BF070  hireling.txt skill roll (not called, kept for reference)
//   UseSkill      0x4A7BC0 (Game*, Unit*, mode, skill, target, x, y, u8)
//   DefaultAttack 0x4A78E0 (Game*, Unit*, mode, target)
//   Idle          0x4A6D10 (Game*, Unit*, frames)
//   Escape        0x4A7DF0 (Game*, Unit*, from, u8 steps, 1)
//   WalkToOwner   0x4A8090 (Game*, Unit*, owner, u8 steps, u16 0)
//   DeleteEvent   0x48B890 (Game*, Unit*, 2 = AI think, 0)
//   Distance      0x596720 (Unit*, Unit*)  full unit size distance the AI uses
//   InMelee       0x348650 (Unit*, Unit*, 0)
//   GetStat       0x2F5020 (Unit*, stat, layer)
//   CheckState    0x3351B0 (Unit*, state)
//   GetSkill      0x33DCD0 (Unit*, skill, -1); SkillLevel 0x33D1E0 (Unit*, Skill*, 1, 0)
//   SkillsTxt     0x097790 (bank, skill); MonStatsTxt 0x0976E0 (bank, class)
//   AllyForBuff   0x5BF5A0 (Game*, Unit*, SkillsTxt*) ally (owner first) missing the buff
//   FindTarget    0x596340 (Game*, Unit*, arg, callback, 1) calls back for units in adjacent rooms
//   CanTarget     0x597710 (Game*, Unit*, target) the AI's own "can attack this" check
//   Owner         0x4A53C0 (Unit*); UnitType 0x34B9D0 (Unit*); PetInfo 0x4FF2D0 (owner, guid)
//   SkillTarget   0x48FE20 (Game*, Unit*) the unit's path target (what it attacks)
// It runs where the game's server runs: single player and games you host.

#include <D2RLPlugin/api.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <climits>
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

constexpr const char* PluginId      = "smarter-mercenaries";
constexpr const char* PluginVersion = "0.0.1";

const D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = PluginId,
	.name        = "Smarter Mercenaries",
	.version     = PluginVersion,
	.author      = "wild-healer",
	.description = "Mercenaries fight the way their type is meant to: rogues and Iron Wolves keep their distance and pick the right skill, Desert mercenaries stay in aura range, Barbarians protect you.",
	.flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

struct Settings {
	std::atomic<bool> enabled { true };
	std::atomic<bool> rogue { true };
	std::atomic<bool> desert { true };
	std::atomic<bool> ironWolf { true };
	std::atomic<bool> barbarian { true };
	std::atomic<int>  leash { 14 };     // how far from you mercenaries pick fights
	std::atomic<int>  auraLeash { 8 };  // the same for the Desert mercenary
	std::atomic<bool> log { true };     // one line per decision change (test builds)
} Config;

// ---- native layout (Steam 3.3.94995) ----

struct Code {
	std::uint64_t                 rva;
	std::array<std::uint8_t, 16>  bytes;
	std::uint8_t                  from; // first byte checked (bytes before it may be another plugin's hook)
};

constexpr Code HireableCode     { 0x5BEAC0, { 0x40, 0x55, 0x57, 0x41, 0x57, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xFA, 0x4D }, 0 };
constexpr Code SelectTargetCode { 0x595F80, { 0x4C, 0x8B, 0xDC, 0x53, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0x50 }, 0 };
constexpr Code CombatCode       { 0x5BF6E0, { 0x4C, 0x89, 0x4C, 0x24, 0x20, 0x66, 0x44, 0x89, 0x44, 0x24, 0x18, 0x53, 0x55, 0x56, 0x57, 0x41 }, 0 };
constexpr Code UseSkillCode     { 0x4A7BC0, { 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x50, 0x41 }, 0 };
constexpr Code DefaultAtkCode   { 0x4A78E0, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x50, 0x41, 0x8B, 0xC0, 0x4C, 0x8B, 0xD2 }, 0 };
constexpr Code IdleCode         { 0x4A6D10, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x70, 0x45 }, 0 };
constexpr Code EscapeCode       { 0x4A7DF0, { 0x48, 0x89, 0x5C, 0x24, 0x20, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x45, 0x0F }, 0 };
constexpr Code WalkToOwnerCode  { 0x4A8090, { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x41 }, 0 };
constexpr Code DeleteEventCode  { 0x48B890, { 0x40, 0x53, 0x56, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x45, 0x8B, 0xF1 }, 0 };
constexpr Code DistanceCode     { 0x596720, { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57 }, 0 };
constexpr Code InMeleeCode      { 0x348650, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57 }, 0 };
constexpr Code GetStatCode      { 0x2F5020, { 0xFF, 0x25, 0xF2, 0x51, 0xB3, 0x03, 0x90, 0x90, 0x90, 0x90, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57 }, 10 };
constexpr Code CheckStateCode   { 0x3351B0, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B }, 0 };
constexpr Code GetSkillCode     { 0x33DCD0, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41, 0x8B, 0xD8, 0x8B, 0xFA, 0xE8 }, 0 };
constexpr Code SkillLevelCode   { 0x33D1E0, { 0xE9, 0x21, 0x30, 0xCB, 0xFF, 0x03, 0x48, 0x8B, 0xF9, 0x48, 0x85, 0xC9, 0x75, 0x1B, 0x88, 0x4C }, 6 };
constexpr Code SkillsTxtCode    { 0x097790, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48 }, 0 };
constexpr Code MonStatsCode     { 0x0976E0, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48 }, 0 };
constexpr Code AllyForBuffCode  { 0x5BF5A0, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57 }, 0 };
constexpr Code FindTargetCode   { 0x596340, { 0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xE1 }, 0 };
constexpr Code CanTargetCode    { 0x597710, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48 }, 0 };
constexpr Code OwnerCode        { 0x4A53C0, { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0xE8, 0x02, 0x66, 0xEA, 0xFF, 0x83, 0xF8 }, 0 };
constexpr Code UnitTypeCode     { 0x34B9D0, { 0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1D, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C }, 0 };
constexpr Code PetInfoCode      { 0x4FF2D0, { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x55, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0xDA, 0x48 }, 0 };
constexpr Code SkillTargetCode  { 0x48FE20, { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xDA, 0xE8, 0x22, 0x1D, 0x00, 0x00, 0x48, 0x8B }, 0 };

constexpr std::uintptr_t UnitTypeOffset   = 0x00; // u32: 0 player, 1 monster
constexpr std::uintptr_t UnitClassOffset  = 0x04; // u32
constexpr std::uintptr_t UnitIdOffset     = 0x08; // u32
constexpr std::uintptr_t UnitDataOffset   = 0x10; // MonsterData*: +0x1A type flags
constexpr std::uintptr_t MonTypeFlags     = 0x1A; // u8: 2 super unique, 4 champion, 8 unique
constexpr std::uintptr_t GameBank         = 0x106; // u8, the data bank the game reads
constexpr std::uintptr_t AiControlParam0  = 0x44;  // i32, vanilla's attack chance accumulator
constexpr std::uintptr_t MonStatsSkill    = 0x1C2; // i16[8]
constexpr std::uintptr_t MonStatsSkillMode = 0x1D3; // u8[8]

enum : int { RogueClass = 271, DesertClass = 338, IronWolfClass = 359, FrenzyBarbClass = 560, BashBarbClass = 561 };
enum : int { StatLevel = 12, StatLife = 6, StatMaxLife = 7, StatFireRes = 39, StatLightRes = 41, StatColdRes = 43 };
enum : int { StateInnerSight = 17, StateStunned = 21, StateTaunt = 27, StateBattleCry = 89 };
enum : int {
	SkillFireArrow = 7, SkillInnerSight = 8, SkillColdArrow = 11, SkillExplodingArrow = 16, SkillFreezingArrow = 31,
	SkillFireBolt = 36, SkillChargedBolt = 38, SkillStatic = 42, SkillIceBlast = 45, SkillFireBall = 47, SkillLightning = 49,
	SkillEnchant = 52, SkillGlacialSpike = 55, SkillChillingArmor = 60,
	SkillBash = 126, SkillTaunt = 137, SkillStun = 139, SkillBattleCry = 146, SkillFrenzy = 147,
};
enum : int { ModeAttack1 = 4, ModeAttack2 = 5, ModeCast = 7 };

using HireableFn     = void (*)(void* game, void* unit, void* tick) noexcept;
using SelectTargetFn = void* (*)(void* game, void* unit, int* distance, int* inMelee) noexcept;
using CombatFn       = void (*)(void* game, void* unit, std::uint16_t classId, void* owner, void* target, void* seed, void* tick, std::uint16_t* params) noexcept;
using UseSkillFn     = int (*)(void* game, void* unit, int mode, int skill, void* target, int x, int y, std::uint8_t flag) noexcept;
using DefaultAtkFn   = int (*)(void* game, void* unit, int mode, void* target) noexcept;
using IdleFn         = void (*)(void* game, void* unit, int frames) noexcept;
using EscapeFn       = int (*)(void* game, void* unit, void* from, std::uint8_t steps, int flag) noexcept;
using WalkToOwnerFn  = int (*)(void* game, void* unit, void* owner, std::uint8_t steps, std::uint16_t flag) noexcept;
using DeleteEventFn  = void (*)(void* game, void* unit, int type, int arg) noexcept;
using DistanceFn     = int (*)(void* a, void* b) noexcept;
using InMeleeFn      = int (*)(void* a, void* b, int flag) noexcept;
using GetStatFn      = int (*)(void* unit, int stat, int layer) noexcept;
using CheckStateFn   = int (*)(void* unit, int state) noexcept;
using GetSkillFn     = void* (*)(void* unit, int skill, int owner) noexcept;
using SkillLevelFn   = int (*)(void* unit, void* skill, int bonus, int flag) noexcept;
using TxtFn          = void* (*)(std::uint8_t bank, int id) noexcept;
using AllyForBuffFn  = void* (*)(void* game, void* unit, void* skillsTxt) noexcept;
using TargetCallback = void* (*)(void* game, void* unit, void* other, void* arg) noexcept;
using FindTargetFn   = void* (*)(void* game, void* unit, void* arg, TargetCallback callback, int id) noexcept;
using CanTargetFn    = int (*)(void* game, void* unit, void* target) noexcept;
using OwnerFn        = void* (*)(void* unit) noexcept;
using UnitTypeFn     = int (*)(void* unit) noexcept;
using PetInfoFn      = void* (*)(void* owner, std::uint32_t guid) noexcept;
using SkillTargetFn  = void* (*)(void* game, void* unit) noexcept;

const D2RL::PluginContext* Context {};
std::uintptr_t             Base {};

HireableFn     Hireable {};     // originals, after the hooks
SelectTargetFn SelectTarget {};
CombatFn       Combat {};
UseSkillFn     UseSkill {};
DefaultAtkFn   DefaultAttackFn {};
IdleFn         Idle {};
EscapeFn       Escape {};
WalkToOwnerFn  WalkToOwner {};
DeleteEventFn  DeleteEvent {};
DistanceFn     Distance {};
InMeleeFn      InMelee {};
GetStatFn      GetStat {};
CheckStateFn   CheckState {};
GetSkillFn     GetSkill {};
SkillLevelFn   SkillLevel {};
TxtFn          SkillsTxt {};
TxtFn          MonStatsTxt {};
AllyForBuffFn  AllyForBuff {};
FindTargetFn   FindTarget {};
CanTargetFn    CanTarget {};
OwnerFn        Owner {};
UnitTypeFn     UnitType {};
PetInfoFn      PetInfo {};
SkillTargetFn  SkillTarget {};

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

bool CodeMatches(const Code& code) noexcept {
	std::array<std::uint8_t, 16> bytes {};
	if (!Read(Base + code.rva, bytes.data(), bytes.size())) return false;
	return std::memcmp(bytes.data() + code.from, code.bytes.data() + code.from, bytes.size() - code.from) == 0;
}

// Per think, cheap random numbers (the game's seed is left alone).
std::uint32_t Random(std::uint32_t range) noexcept {
	thread_local std::uint32_t state = 0x9E3779B9u ^ GetCurrentThreadId();
	state ^= state << 13;
	state ^= state >> 17;
	state ^= state << 5;
	return range ? state % range : 0;
}

// ---- mercenary model ----

enum class Role { None, Rogue, Desert, IronWolf, BashBarb, FrenzyBarb };
enum Element : unsigned { NoElement = 0, Fire = 1, Cold = 2, Lightning = 4 };

const char* RoleName(Role role) noexcept {
	switch (role) {
	case Role::Rogue: return "Rogue";
	case Role::Desert: return "Desert mercenary";
	case Role::IronWolf: return "Iron Wolf";
	case Role::BashBarb: return "Barbarian";
	case Role::FrenzyBarb: return "Frenzy Barbarian";
	default: return "?";
	}
}

Role RoleOf(void* unit) noexcept {
	switch (Field<std::uint32_t>(unit, UnitClassOffset)) {
	case RogueClass: return Config.rogue ? Role::Rogue : Role::None;
	case DesertClass: return Config.desert ? Role::Desert : Role::None;
	case IronWolfClass: return Config.ironWolf ? Role::IronWolf : Role::None;
	case BashBarbClass: return Config.barbarian ? Role::BashBarb : Role::None;
	case FrenzyBarbClass: return Config.barbarian ? Role::FrenzyBarb : Role::None;
	default: return Role::None;
	}
}

bool Has(void* unit, int skill) noexcept {
	void* const entry = GetSkill(unit, skill, -1);
	return entry && SkillLevel(unit, entry, 1, 0) > 0;
}

unsigned ElementsOf(void* unit, Role role) noexcept {
	unsigned elements = NoElement;
	if (role == Role::Rogue) {
		if (Has(unit, SkillFireArrow) || Has(unit, SkillExplodingArrow)) elements |= Fire;
		if (Has(unit, SkillColdArrow) || Has(unit, SkillFreezingArrow)) elements |= Cold;
	} else if (role == Role::IronWolf) {
		if (Has(unit, SkillFireBall) || Has(unit, SkillFireBolt)) elements |= Fire;
		if (Has(unit, SkillGlacialSpike) || Has(unit, SkillIceBlast)) elements |= Cold;
		if (Has(unit, SkillLightning) || Has(unit, SkillChargedBolt)) elements |= Lightning;
	}
	return elements;
}

// Immune to every element this mercenary deals (resistances as they are now,
// so Conviction or Lower Resist breaking an immunity counts).
bool ImmuneToAll(void* target, unsigned elements) noexcept {
	if (!elements) return false;
	if ((elements & Fire) && GetStat(target, StatFireRes, 0) < 100) return false;
	if ((elements & Cold) && GetStat(target, StatColdRes, 0) < 100) return false;
	if ((elements & Lightning) && GetStat(target, StatLightRes, 0) < 100) return false;
	return true;
}

bool Immune(void* target, Element element) noexcept {
	const int stat = element == Fire ? StatFireRes : element == Cold ? StatColdRes : StatLightRes;
	return GetStat(target, stat, 0) >= 100;
}

bool IsBoss(void* unit) noexcept {
	if (Field<std::uint32_t>(unit, UnitTypeOffset) != 1) return true; // hostile players count as dangerous
	void* const data = Field<void*>(unit, UnitDataOffset);
	return data && (Field<std::uint8_t>(data, MonTypeFlags) & 0x0E) != 0;
}

int LifePercent(void* unit) noexcept {
	const int max = GetStat(unit, StatMaxLife, 0);
	return max > 0 ? static_cast<int>(static_cast<long long>(GetStat(unit, StatLife, 0)) * 100 / max) : 100;
}

// ---- target choice ----

constexpr std::size_t MaxCandidates = 96;
constexpr int         MaxScanDistance = 30;

struct Scan {
	void*       merc;
	std::size_t count;
	void*       units[MaxCandidates];
	int         distance[MaxCandidates];
};

void* __cdecl Collect(void* game, void* unit, void* other, void* arg) noexcept {
	auto* const scan = static_cast<Scan*>(arg);
	if (!other || other == unit || scan->count >= MaxCandidates || !CanTarget(game, unit, other)) return nullptr;
	const int distance = Distance(unit, other);
	if (distance > MaxScanDistance) return nullptr;
	for (std::size_t index = 0; index < scan->count; ++index)
		if (scan->units[index] == other) return nullptr;
	scan->units[scan->count]    = other;
	scan->distance[scan->count] = distance;
	++scan->count;
	return nullptr; // keep scanning
}

void Gather(void* game, void* merc, Scan& scan) noexcept {
	scan.merc  = merc;
	scan.count = 0;
	(void)FindTarget(game, merc, &scan, &Collect, 1);
}

// Hostile units within `radius` of `center` (center not counted).
int CountNear(const Scan& scan, void* center, int radius) noexcept {
	int count = 0;
	for (std::size_t index = 0; index < scan.count; ++index)
		if (scan.units[index] != center && Distance(center, scan.units[index]) <= radius) ++count;
	return count;
}

// Rate-limited decision log, one line per mercenary when its choice changes.
struct LogSlot {
	std::uint32_t id;
	void*         target;
	int           skill;
	DWORD         tick;
};
std::array<LogSlot, 16> LogSlots {};

void Note(void* merc, Role role, void* target, int skill, const char* why) noexcept {
	if (!Config.log.load(std::memory_order_relaxed)) return;
	const std::uint32_t id  = Field<std::uint32_t>(merc, UnitIdOffset);
	const DWORD         now = GetTickCount();
	LogSlot*            slot = nullptr;
	for (auto& entry : LogSlots)
		if (entry.id == id) slot = &entry;
	if (!slot) {
		slot = &LogSlots[0];
		for (auto& entry : LogSlots)
			if (entry.tick < slot->tick) slot = &entry;
		*slot = { id, nullptr, -2, 0 };
	}
	if (slot->target == target && slot->skill == skill && now - slot->tick < 5000) return;
	if (now - slot->tick < 400 && slot->target == target) return;
	*slot = { id, target, skill, now };
	if (target)
		Log(false, "%s %u: %s (target monster %u id %u, life %d%%%s)", RoleName(role), id, why, Field<std::uint32_t>(target, UnitClassOffset),
		    Field<std::uint32_t>(target, UnitIdOffset), LifePercent(target), IsBoss(target) ? ", boss" : "");
	else
		Log(false, "%s %u: %s", RoleName(role), id, why);
}

void* ChooseTarget(void* game, void* merc, void* owner, Role role) noexcept {
	Scan scan;
	Gather(game, merc, scan);
	if (!scan.count) return nullptr;
	const bool     melee    = role == Role::Desert || role == Role::BashBarb || role == Role::FrenzyBarb;
	const int      leash    = role == Role::Desert ? Config.auraLeash.load() : Config.leash.load();
	const unsigned elements = ElementsOf(merc, role);
	void* const    wanted   = SkillTarget(game, owner); // what you're attacking
	void*          best     = nullptr;
	int            bestScore = INT_MAX;
	for (std::size_t index = 0; index < scan.count; ++index) {
		void* const unit        = scan.units[index];
		const int   fromMerc    = scan.distance[index];
		const int   fromOwner   = Distance(owner, unit);
		void* const itsTarget   = SkillTarget(game, unit);
		const bool  selfDefense = itsTarget == merc && fromMerc <= 6;
		if (fromOwner > leash && !selfDefense) continue;
		int score = fromMerc * 4;
		if (unit == wanted) score -= 60;
		if (itsTarget == owner) score -= melee ? 60 : 40;
		if (itsTarget == merc) score -= 20;
		if (ImmuneToAll(unit, elements)) score += role == Role::IronWolf ? 200 : 30;
		if (score < bestScore) bestScore = score, best = unit;
	}
	return best;
}

// ---- actions ----

bool Cast(void* game, void* merc, int mode, int skill, void* target) noexcept {
	if (!Has(merc, skill)) return false;
	return UseSkill(game, merc, mode, skill, target, 0, 0, 0) != 0;
}

// The type's normal attack, as the game's skill roll does when no skill wins.
void NormalAttack(void* game, void* merc, void* target, const std::uint16_t* params) noexcept {
	void* const record = MonStatsTxt(Field<std::uint8_t>(game, GameBank), static_cast<int>(Field<std::uint32_t>(merc, UnitClassOffset)));
	const int   index  = static_cast<std::int16_t>(params[1]);
	if (record && index > 0 && index < 8) {
		const int skill = Field<std::int16_t>(record, MonStatsSkill + index * 2);
		const int mode  = Field<std::uint8_t>(record, MonStatsSkillMode + index);
		UseSkill(game, merc, mode, skill, target, 0, 0, 0);
		return;
	}
	DefaultAttackFn(game, merc, static_cast<std::int16_t>(params[2]), target);
}

// Buffs the Iron Wolf keeps up: Enchant (on you first) and Chilling Armor.
bool Buff(void* game, void* merc, Role role) noexcept {
	for (const int skill : { SkillEnchant, SkillChillingArmor }) {
		if (!Has(merc, skill)) continue;
		void* const record = SkillsTxt(Field<std::uint8_t>(game, GameBank), skill);
		if (!record) continue;
		void* const ally = AllyForBuff(game, merc, record);
		if (ally && Cast(game, merc, ModeCast, skill, ally)) {
			Note(merc, role, nullptr, skill, skill == SkillEnchant ? "casts Enchant on an ally missing it" : "recasts Chilling Armor");
			return true;
		}
	}
	return false;
}

// Vanilla's attack-rate gate, kept so the mercenaries don't attack faster than before.
bool AttackRoll(void* merc, void* tick, bool melee) noexcept {
	void* const control = Field<void*>(tick, 0);
	if (!control) return true;
	int chance = 98;
	if (!melee) {
		const int param = Field<int>(control, AiControlParam0);
		chance          = std::min(param + 40 + 2 * GetStat(merc, StatLevel, 0), 95);
	}
	const bool hit = static_cast<int>(Random(100)) < chance;
	int        param = Field<int>(control, AiControlParam0);
	param            = hit ? 0 : param + 10;
	std::memcpy(static_cast<std::uint8_t*>(control) + AiControlParam0, &param, sizeof(param));
	return hit;
}

// Ranged mercenaries step back toward you when something gets close.
bool KeepDistance(void* game, void* merc, void* owner, void* target, Role role) noexcept {
	const int distance = Distance(merc, target);
	if (distance > 3 || Random(100) >= 70) return false;
	if (Distance(owner, target) > distance + 2 && WalkToOwner(game, merc, owner, 4, 0)) {
		Note(merc, role, target, -1, "steps back toward you");
		return true;
	}
	DeleteEvent(game, merc, 2, 0);
	if (Escape(game, merc, target, 4, 1)) {
		Note(merc, role, target, -1, "backs away from a monster in its face");
		return true;
	}
	return false;
}

void RogueAttack(void* game, void* merc, void* target, const Scan& scan, const std::uint16_t* params) noexcept {
	const unsigned elements = ElementsOf(merc, Role::Rogue);
	const Element  element  = (elements & Fire) ? Fire : Cold;
	const int      group    = CountNear(scan, target, 4);
	if (!CheckState(target, StateInnerSight) && (IsBoss(target) || group >= 2) && Random(100) < 35 && Cast(game, merc, ModeAttack1, SkillInnerSight, target)) {
		Note(merc, Role::Rogue, target, SkillInnerSight, "casts Inner Sight");
		return;
	}
	if (elements && !Immune(target, element)) {
		const int area   = element == Fire ? SkillExplodingArrow : SkillFreezingArrow;
		const int single = element == Fire ? SkillFireArrow : SkillColdArrow;
		if (group >= 1 && Cast(game, merc, ModeAttack1, area, target)) return Note(merc, Role::Rogue, target, area, "shoots an area arrow at a group");
		if (Cast(game, merc, ModeAttack1, single, target)) return Note(merc, Role::Rogue, target, single, "shoots an elemental arrow");
	}
	Note(merc, Role::Rogue, target, 0, elements && Immune(target, element) ? "normal arrows, target is immune" : "normal arrow");
	NormalAttack(game, merc, target, params);
}

void IronWolfAttack(void* game, void* merc, void* owner, void* target, const Scan& scan, const std::uint16_t* params) noexcept {
	const unsigned elements = ElementsOf(merc, Role::IronWolf);
	const int      distance = Distance(merc, target);
	const int      group    = CountNear(scan, target, 4);
	if ((elements & Lightning) && !Immune(target, Lightning)) {
		const int nearMerc = CountNear(scan, merc, 6);
		if (distance <= 6 && LifePercent(target) > 50 && (IsBoss(target) || nearMerc >= 3) && Cast(game, merc, ModeCast, SkillStatic, target))
			return Note(merc, Role::IronWolf, target, SkillStatic, "Static Field on healthy monsters around it");
		if ((distance <= 5 || group >= 2) && Random(100) < 40 && Cast(game, merc, ModeCast, SkillChargedBolt, target))
			return Note(merc, Role::IronWolf, target, SkillChargedBolt, "Charged Bolt at close range");
		if (Cast(game, merc, ModeCast, SkillLightning, target)) return Note(merc, Role::IronWolf, target, SkillLightning, "Lightning");
		if (Cast(game, merc, ModeCast, SkillChargedBolt, target)) return Note(merc, Role::IronWolf, target, SkillChargedBolt, "Charged Bolt");
	}
	if ((elements & Cold) && !Immune(target, Cold)) {
		if (group >= 2 && Cast(game, merc, ModeCast, SkillGlacialSpike, target)) return Note(merc, Role::IronWolf, target, SkillGlacialSpike, "Glacial Spike at a group");
		if (Cast(game, merc, ModeCast, SkillIceBlast, target)) return Note(merc, Role::IronWolf, target, SkillIceBlast, "Ice Blast");
		if (Cast(game, merc, ModeCast, SkillGlacialSpike, target)) return Note(merc, Role::IronWolf, target, SkillGlacialSpike, "Glacial Spike");
	}
	if ((elements & Fire) && !Immune(target, Fire)) {
		if (Cast(game, merc, ModeCast, SkillFireBall, target)) return Note(merc, Role::IronWolf, target, SkillFireBall, "Fire Ball");
		if (Cast(game, merc, ModeCast, SkillFireBolt, target)) return Note(merc, Role::IronWolf, target, SkillFireBolt, "Fire Bolt");
	}
	if (elements && ImmuneToAll(target, elements)) {
		// Nothing it can hurt here: stay with you instead of wasting casts.
		Note(merc, Role::IronWolf, target, -1, "target is immune to its spells, stays with you");
		if (!WalkToOwner(game, merc, owner, 4, 0)) Idle(game, merc, 10);
		return;
	}
	NormalAttack(game, merc, target, params);
}

void BashBarbAttack(void* game, void* merc, void* target, const Scan& scan, const std::uint16_t* params) noexcept {
	if (CountNear(scan, merc, 5) >= 2 && !CheckState(target, StateBattleCry) && Random(100) < 50 && Cast(game, merc, ModeAttack1, SkillBattleCry, target))
		return Note(merc, Role::BashBarb, target, SkillBattleCry, "Battle Cry on a group");
	if (IsBoss(target) && !CheckState(target, StateStunned) && Cast(game, merc, ModeAttack1, SkillStun, target))
		return Note(merc, Role::BashBarb, target, SkillStun, "Stun on a boss or champion");
	if (Cast(game, merc, ModeAttack1, SkillBash, target)) return Note(merc, Role::BashBarb, target, SkillBash, "Bash");
	NormalAttack(game, merc, target, params);
}

void FrenzyBarbAttack(void* game, void* merc, void* owner, void* target, const std::uint16_t* params) noexcept {
	if (SkillTarget(game, target) == owner && !CheckState(target, StateTaunt) && Cast(game, merc, ModeAttack2, SkillTaunt, target))
		return Note(merc, Role::FrenzyBarb, target, SkillTaunt, "Taunts a monster attacking you");
	if (Cast(game, merc, ModeAttack1, SkillFrenzy, target)) return Note(merc, Role::FrenzyBarb, target, SkillFrenzy, "Frenzy");
	NormalAttack(game, merc, target, params);
}

// ---- hooks ----

thread_local void* ThinkingMerc = nullptr;

bool Enabled() noexcept {
	return Config.enabled.load(std::memory_order_relaxed);
}

// The merc's owner when it is a player's hired mercenary of a handled type.
void* PlayerOwner(void* merc) noexcept {
	void* const owner = Owner(merc);
	return owner && UnitType(owner) == 0 ? owner : nullptr;
}

void HookHireable(void* game, void* unit, void* tick) noexcept {
	void* const previous = ThinkingMerc;
	ThinkingMerc         = unit;
	Hireable(game, unit, tick);
	ThinkingMerc = previous;
}

void* HookSelectTarget(void* game, void* unit, int* distance, int* inMelee) noexcept {
	if (!unit || unit != ThinkingMerc || !Enabled()) return SelectTarget(game, unit, distance, inMelee);
	const Role  role  = RoleOf(unit);
	void* const owner = role == Role::None ? nullptr : PlayerOwner(unit);
	if (!owner) return SelectTarget(game, unit, distance, inMelee);
	void* const target = ChooseTarget(game, unit, owner, role);
	if (!target) {
		if (distance) *distance = INT_MAX;
		if (inMelee) *inMelee = 0;
		return nullptr;
	}
	if (distance) *distance = Distance(unit, target);
	if (inMelee) *inMelee = InMelee(unit, target, 0);
	return target;
}

void HookCombat(void* game, void* unit, std::uint16_t classId, void* owner, void* target, void* seed, void* tick, std::uint16_t* params) noexcept {
	const Role role = Enabled() && unit && owner && target && params ? RoleOf(unit) : Role::None;
	if (role == Role::None || UnitType(owner) != 0 || !PetInfo(owner, Field<std::uint32_t>(unit, UnitIdOffset)))
		return Combat(game, unit, classId, owner, target, seed, tick, params);
	const bool melee = params[0] == 2;
	// Melee types: the game runs to the target (and the Desert mercenary keeps
	// the game's own Jab/attack choice); our part is the target and leash.
	if (melee && (role == Role::Desert || Distance(unit, target) >= 3 || !InMelee(unit, target, 0)))
		return Combat(game, unit, classId, owner, target, seed, tick, params);
	Scan scan;
	Gather(game, unit, scan);
	if (role == Role::IronWolf && Buff(game, unit, role)) return;
	if (!melee && KeepDistance(game, unit, owner, target, role)) return;
	if (!AttackRoll(unit, tick, melee)) return Idle(game, unit, 10);
	switch (role) {
	case Role::Rogue: return RogueAttack(game, unit, target, scan, params);
	case Role::IronWolf: return IronWolfAttack(game, unit, owner, target, scan, params);
	case Role::BashBarb: return BashBarbAttack(game, unit, target, scan, params);
	case Role::FrenzyBarb: return FrenzyBarbAttack(game, unit, owner, target, params);
	default: return Combat(game, unit, classId, owner, target, seed, tick, params);
	}
}

// ---- version check ----

bool Locate() noexcept {
	const std::array<const Code*, 24> checks { &HireableCode, &SelectTargetCode, &CombatCode, &UseSkillCode, &DefaultAtkCode, &IdleCode,
		                                       &EscapeCode, &WalkToOwnerCode, &DeleteEventCode, &DistanceCode, &InMeleeCode, &GetStatCode,
		                                       &CheckStateCode, &GetSkillCode, &SkillLevelCode, &SkillsTxtCode, &MonStatsCode, &AllyForBuffCode,
		                                       &FindTargetCode, &CanTargetCode, &OwnerCode, &UnitTypeCode, &PetInfoCode, &SkillTargetCode };
	for (const Code* check : checks) {
		if (!CodeMatches(*check)) {
			Log(true, "game code differs at D2R+0x%llX", static_cast<unsigned long long>(check->rva));
			return false;
		}
	}
	const auto at = [](const Code& code) { return reinterpret_cast<void*>(Base + code.rva); };
	UseSkill        = reinterpret_cast<UseSkillFn>(at(UseSkillCode));
	DefaultAttackFn = reinterpret_cast<DefaultAtkFn>(at(DefaultAtkCode));
	Idle            = reinterpret_cast<IdleFn>(at(IdleCode));
	Escape          = reinterpret_cast<EscapeFn>(at(EscapeCode));
	WalkToOwner     = reinterpret_cast<WalkToOwnerFn>(at(WalkToOwnerCode));
	DeleteEvent     = reinterpret_cast<DeleteEventFn>(at(DeleteEventCode));
	Distance        = reinterpret_cast<DistanceFn>(at(DistanceCode));
	InMelee         = reinterpret_cast<InMeleeFn>(at(InMeleeCode));
	GetStat         = reinterpret_cast<GetStatFn>(at(GetStatCode));
	CheckState      = reinterpret_cast<CheckStateFn>(at(CheckStateCode));
	GetSkill        = reinterpret_cast<GetSkillFn>(at(GetSkillCode));
	SkillLevel      = reinterpret_cast<SkillLevelFn>(at(SkillLevelCode));
	SkillsTxt       = reinterpret_cast<TxtFn>(at(SkillsTxtCode));
	MonStatsTxt     = reinterpret_cast<TxtFn>(at(MonStatsCode));
	AllyForBuff     = reinterpret_cast<AllyForBuffFn>(at(AllyForBuffCode));
	FindTarget      = reinterpret_cast<FindTargetFn>(at(FindTargetCode));
	CanTarget       = reinterpret_cast<CanTargetFn>(at(CanTargetCode));
	Owner           = reinterpret_cast<OwnerFn>(at(OwnerCode));
	UnitType        = reinterpret_cast<UnitTypeFn>(at(UnitTypeCode));
	PetInfo         = reinterpret_cast<PetInfoFn>(at(PetInfoCode));
	SkillTarget     = reinterpret_cast<SkillTargetFn>(at(SkillTargetCode));
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

constexpr int MinLeash = 4, MaxLeash = 24;

bool Apply(const std::string& key, const std::string& value, std::string& error) {
	const auto flag = [&](std::atomic<bool>& target) {
		if (value != "true" && value != "false") return error = "true or false", false;
		target = value == "true";
		return true;
	};
	const auto range = [&](std::atomic<int>& target) {
		char*      end    = nullptr;
		const long number = std::strtol(value.c_str(), &end, 10);
		if (value.empty() || *end || number < MinLeash || number > MaxLeash) return error = "4 to 24", false;
		target = static_cast<int>(number);
		return true;
	};
	if (key == "enabled") return flag(Config.enabled);
	if (key == "rogue") return flag(Config.rogue);
	if (key == "desert_mercenary") return flag(Config.desert);
	if (key == "iron_wolf") return flag(Config.ironWolf);
	if (key == "barbarian") return flag(Config.barbarian);
	if (key == "leash") return range(Config.leash);
	if (key == "aura_leash") return range(Config.auraLeash);
	if (key == "log") return flag(Config.log);
	return error = "unknown setting", false;
}

std::string ValueOf(const std::string& key) {
	const auto flag = [](bool on) { return std::string(on ? "true" : "false"); };
	if (key == "enabled") return flag(Config.enabled);
	if (key == "rogue") return flag(Config.rogue);
	if (key == "desert_mercenary") return flag(Config.desert);
	if (key == "iron_wolf") return flag(Config.ironWolf);
	if (key == "barbarian") return flag(Config.barbarian);
	if (key == "leash") return std::to_string(Config.leash.load());
	if (key == "aura_leash") return std::to_string(Config.auraLeash.load());
	if (key == "log") return flag(Config.log);
	return {};
}

std::string Render() {
	return R"(# Smarter Mercenaries
#
# Mercenaries fight the way their type is meant to. Works in single player and
# in games you host. Every setting can also be changed in game with I Hate TOMLs.

[smarter-mercenaries]
enabled = )" + ValueOf("enabled") + R"(

# Which mercenary types use the new AI (false: the game's own AI).
rogue = )" + ValueOf("rogue") + R"(
desert_mercenary = )" + ValueOf("desert_mercenary") + R"(
iron_wolf = )" + ValueOf("iron_wolf") + R"(
barbarian = )" + ValueOf("barbarian") + R"(

# How far from you a mercenary picks fights (4 to 24, the game's follow range
# is 16 to 24). Monsters attacking the mercenary itself are always fought.
leash = )" + ValueOf("leash") + R"(

# The same for the Desert mercenary, kept short so its aura covers you.
aura_leash = )" + ValueOf("aura_leash") + R"(

# Write mercenary decisions to the plugin log.
log = )" + ValueOf("log") + "\n";
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

constexpr const char* Schema = R"toml(title = "Smarter Mercenaries"
[[setting]]
key = "enabled"
type = "bool"
label = "Smarter mercenaries"
note = "Mercenaries fight the way their type is meant to."
[[setting]]
key = "rogue"
type = "bool"
label = "Rogue"
note = "Keeps its distance, area arrows on groups, normal arrows on immunes."
[[setting]]
key = "desert_mercenary"
type = "bool"
label = "Desert mercenary"
note = "Stays close so its aura covers you, fights only near you."
[[setting]]
key = "iron_wolf"
type = "bool"
label = "Iron Wolf"
note = "Keeps its distance, picks spells by resistance, keeps Enchant and armor up."
[[setting]]
key = "barbarian"
type = "bool"
label = "Barbarian"
note = "Goes for monsters attacking you, Battle Cry on groups, Stun on bosses, Taunt."
[[setting]]
key = "leash"
type = "int"
label = "Leash"
note = "How far from you mercenaries pick fights. 4 to 24."
min = 4
max = 24
step = 1
shift_step = 2
ctrl_step = 4
[[setting]]
key = "aura_leash"
type = "int"
label = "Desert mercenary leash"
note = "The same for the Desert mercenary, short so its aura covers you."
min = 4
max = 24
step = 1
shift_step = 2
ctrl_step = 4
[[setting]]
key = "log"
type = "bool"
label = "Log decisions"
note = "Write what each mercenary decides to the plugin log."
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

template <typename Fn>
bool Hook(const Code& code, Fn detour, Fn& original) noexcept {
	if (!Context->InstallInlineHook(code.rva, code.bytes.data(), static_cast<std::uint32_t>(code.bytes.size()), detour, &original) || !original) {
		Log(true, "Smarter Mercenaries %s: could not hook D2R+0x%llX (another plugin may hook it)", PluginVersion, static_cast<unsigned long long>(code.rva));
		return false;
	}
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
		Log(true, "Smarter Mercenaries %s: inactive, this game build isn't supported (Steam 3.3.94995 is)", PluginVersion);
		return true;
	}
	// Combat first, then the target choice, then the think wrapper that turns them on.
	if (!Hook(CombatCode, &HookCombat, Combat)) return true;
	if (!Hook(SelectTargetCode, &HookSelectTarget, SelectTarget)) return true;
	if (!Hook(HireableCode, &HookHireable, Hireable)) return true;
	Log(false, "Smarter Mercenaries %s: %s, leash %d, Desert mercenary leash %d", PluginVersion, Config.enabled ? "active" : "loaded but turned off",
	    Config.leash.load(), Config.auraLeash.load());
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	Menu.Stop();
}
