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
//   InMelee       0x348650 (Unit*, Unit*, 0, int* out or null)
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
#include <cmath>
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
constexpr const char* PluginVersion = "0.0.20";

const D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = PluginId,
	.name        = "Smarter Mercenaries",
	.version     = PluginVersion,
	.author      = "wild-healer",
	.description = "Mercenaries keep a formation around you and fight the way their type is meant to: melee in front, ranged behind hitting and running, skills picked by the target.",
	.flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

struct Settings {
	std::atomic<bool> enabled { true };
	std::atomic<bool> rogue { true };
	std::atomic<bool> desert { true };
	std::atomic<bool> ironWolf { true };
	std::atomic<bool> barbarian { true };
	std::atomic<int>  leash { 14 };     // how far from you mercenaries pick fights
	std::atomic<int>  engage { 10 };    // melee mercenaries leave the formation for enemies this close
	std::atomic<int>  danger { 10 };    // ranged mercenaries start positioning when you are this close to enemies
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
constexpr Code WalkToCode       { 0x4A8880, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57 }, 0 };
constexpr Code PetMoveCode      { 0x5C1460, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89, 0x7C, 0x24, 0x18, 0x55 }, 0 };
constexpr Code VelocityCode     { 0x4A7270, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57 }, 0 };
constexpr Code BlockedCode      { 0x3502A0, { 0x40, 0x53, 0x57, 0x41, 0x55, 0x48, 0x83, 0xEC, 0x50, 0x45, 0x8B, 0xE8, 0x48, 0x8B, 0xFA, 0x48 }, 0 };
constexpr Code MoveCode         { 0x4A8A10, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57 }, 0 };
constexpr Code GetPathCode      { 0x34AE80, { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C }, 0 };
constexpr Code SetStepsCode     { 0x342890, { 0x48, 0x85, 0xC9, 0x74, 0x13, 0xFE, 0xCA, 0x33, 0xC0, 0x0F, 0xB6, 0xD2, 0x80, 0xFA, 0x12, 0x0F }, 0 };
constexpr Code UnitRoomCode     { 0x34B440, { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C }, 0 };
constexpr Code InTownCode       { 0x2F0750, { 0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x07, 0x33, 0xC0, 0x48, 0x83, 0xC4, 0x28, 0xC3 }, 0 };
constexpr Code SkillTargetCode  { 0x48FE20, { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xDA, 0xE8, 0x22, 0x1D, 0x00, 0x00, 0x48, 0x8B }, 0 };

constexpr std::uintptr_t UnitTypeOffset   = 0x00; // u32: 0 player, 1 monster
constexpr std::uintptr_t UnitClassOffset  = 0x04; // u32
constexpr std::uintptr_t UnitIdOffset     = 0x08; // u32
constexpr std::uintptr_t UnitPathOffset   = 0x38; // Path*: +2 x, +6 y (u16)
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
	SkillJab = 10, SkillBash = 126, SkillTaunt = 137, SkillStun = 139, SkillBattleCry = 146, SkillFrenzy = 147,
};
enum : int { ModeAttack1 = 4, ModeAttack2 = 5, ModeCast = 7, ModeJab = 14 };

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
using InMeleeFn      = int (*)(void* a, void* b, int flag, int* out) noexcept; // out (may be null) gets the result too
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
using WalkToFn       = int (*)(void* game, void* unit, int x, int y) noexcept;
using PetMoveFn      = int (*)(void* game, void* owner, void* unit, int motion, int run, int speed, std::uint8_t steps) noexcept;
using MoveFn         = int (*)(void* game, void* unit, void* target, int mode, int x, int y, std::uint8_t flag, std::uint16_t extra) noexcept;
using GetPathFn      = void* (*)(void* unit) noexcept;
using BlockedFn      = int (*)(void* a, void* b, int mask) noexcept; // nonzero: a wall between them
using VelocityFn     = void (*)(void* unit, int mode, int speed, std::uint8_t percent) noexcept;
using SetStepsFn     = void (*)(void* path, std::uint8_t steps) noexcept;
using UnitRoomFn     = void* (*)(void* unit) noexcept;
using InTownFn       = int (*)(void* room) noexcept;

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
WalkToFn       WalkTo {};
PetMoveFn      PetMove {};
MoveFn         Move {};
GetPathFn      GetPath {};
BlockedFn      Blocked {};
VelocityFn     Velocity {};
SetStepsFn     SetSteps {};
UnitRoomFn     UnitRoom {};
InTownFn       InTown {};

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

// Set by ChooseTarget: a ranged mercenary saw targets, but only behind walls.
bool OnlyBlocked = false;

void* ChooseTarget(void* game, void* merc, void* owner, Role role) noexcept {
	OnlyBlocked = false;
	Scan scan;
	Gather(game, merc, scan);
	if (!scan.count) return nullptr;
	const bool     melee    = role == Role::Desert || role == Role::BashBarb || role == Role::FrenzyBarb;
	const int      leash    = Config.leash.load();
	const int      engage   = Config.engage.load();
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
		if ((fromOwner > leash || (melee && fromMerc > engage)) && !selfDefense) continue;
		// Ranged: only targets it can shoot from where it stands (missile line,
		// as the game's own ranged target choice checks), so it never runs
		// around looking for a shot.
		if (!melee && Blocked(unit, merc, 4)) {
			OnlyBlocked = true;
			continue;
		}
		// Barbarians intercept: the monster closest to you counts most, so they
		// meet threats before they reach you. Everyone else: closest to itself.
		const bool tank = role == Role::BashBarb || role == Role::FrenzyBarb;
		int        score = tank ? fromOwner * 3 + fromMerc : fromMerc * 4;
		if (unit == wanted) score -= 60;
		if (itsTarget == owner) score -= melee ? 60 : 40;
		if (itsTarget == merc) score -= 20;
		if (ImmuneToAll(unit, elements)) score += role == Role::IronWolf ? 200 : 30;
		if (score < bestScore) bestScore = score, best = unit;
	}
	if (best) OnlyBlocked = false;
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
	if (distance > 4 || Random(100) >= 70) return false;
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

// Desert mercenary: Jab bosses and champions, mix Jab and normal attacks otherwise.
void DesertAttack(void* game, void* merc, void* target, const std::uint16_t* params) noexcept {
	if ((IsBoss(target) || Random(100) < 60) && Cast(game, merc, ModeJab, SkillJab, target))
		return Note(merc, Role::Desert, target, SkillJab, IsBoss(target) ? "Jabs a boss or champion" : "Jab");
	Note(merc, Role::Desert, target, 0, "normal attack");
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

// Diagnostics: what each mercenary's think did, one line per mercenary and
// reason every few seconds (only with log on).
struct TraceSlot {
	std::uint32_t id;
	const char*   reason;
	DWORD         tick;
};
std::array<TraceSlot, 32> TraceSlots {};

void Trace(void* merc, const char* reason, int value = -1) noexcept {
	if (!Config.log.load(std::memory_order_relaxed)) return;
	const std::uint32_t id  = Field<std::uint32_t>(merc, UnitIdOffset);
	const DWORD         now = GetTickCount();
	TraceSlot*          slot = nullptr;
	for (auto& entry : TraceSlots)
		if (entry.id == id && entry.reason == reason) slot = &entry;
	if (slot && now - slot->tick < 5000) return;
	if (!slot) {
		slot = &TraceSlots[0];
		for (auto& entry : TraceSlots)
			if (entry.tick < slot->tick) slot = &entry;
	}
	*slot = { id, reason, now };
	if (value >= 0) Log(false, "merc class %u id %u: %s %d", Field<std::uint32_t>(merc, UnitClassOffset), id, reason, value);
	else Log(false, "merc class %u id %u: %s", Field<std::uint32_t>(merc, UnitClassOffset), id, reason);
}

bool Position(void* unit, int& x, int& y) noexcept {
	void* const path = Field<void*>(unit, UnitPathOffset);
	if (!path) return false;
	x = Field<std::uint16_t>(path, 2);
	y = Field<std::uint16_t>(path, 6);
	return true;
}

// Barbarians body-block: a monster coming at you meets the barbarian standing
// between the two of you, a few steps out from you.
bool BodyBlock(void* game, void* merc, void* owner, void* target, Role role) noexcept {
	if (SkillTarget(game, target) != owner) return false;
	const int fromOwner = Distance(owner, target);
	if (fromOwner < 4 || fromOwner > 14 || Distance(merc, target) <= 3) return false;
	int ox = 0, oy = 0, tx = 0, ty = 0, mx = 0, my = 0;
	if (!Position(owner, ox, oy) || !Position(target, tx, ty) || !Position(merc, mx, my)) return false;
	const int dx = tx - ox, dy = ty - oy;
	const int length = std::max(std::abs(dx), std::abs(dy));
	if (length <= 0) return false;
	const int step = 3; // how far out from you it stands
	const int px = ox + dx * step / length, py = oy + dy * step / length;
	if (std::max(std::abs(px - mx), std::abs(py - my)) <= 1) return false; // already there: let it fight
	DeleteEvent(game, merc, 2, 0);
	if (!WalkTo(game, merc, px, py)) return false;
	Note(merc, role, target, -3, "steps between you and a monster coming at you");
	return true;
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

// ---- formation think ----
//
// Replaces the game's Hireable think for a player's mercenary outside town:
// no idle wandering, a formation around you (melee slightly in front, ranged
// slightly behind, spread sideways), re-planned every few frames.

constexpr std::uintptr_t UnitModeOffset = 0x0C;

// Movement log (debug build): one line per step of every think, with where
// the mercenary stands and what it does, so its path can be followed.
void Mv(void* merc, const char* format, ...) noexcept {
	if (!Config.log.load(std::memory_order_relaxed) || !merc) return;
	char    text[400] {};
	va_list args;
	va_start(args, format);
	std::vsnprintf(text, sizeof(text), format, args);
	va_end(args);
	int x = 0, y = 0;
	Position(merc, x, y);
	Log(false, "mv %lu c%u id%u (%d,%d) m%u: %s", static_cast<unsigned long>(GetTickCount() % 1000000), Field<std::uint32_t>(merc, UnitClassOffset),
	    Field<std::uint32_t>(merc, UnitIdOffset), x, y, Field<std::uint32_t>(merc, 0x0C), text);
} // u32: monster mode 2 walk, 15 run
constexpr int            ModeWalk = 2, ModeRun = 15;
constexpr std::uint32_t  PlayerRun = 3; // player mode
constexpr std::uintptr_t TickMonStats2 = 0x30; // monstats2 record: +0x0E melee range

struct OwnerState {
	std::uint32_t id;
	int           lastX, lastY;
	double        fx, fy; // facing (where you last moved)
	int           px, py; // last sample for the heading
	double        hx, hy; // heading right now (last 3 steps)
};
struct MercState {
	std::uint32_t id, owner;
	int           slot;
	bool          front;
	int           spotX, spotY;
	bool          moving;
	DWORD         seen;
	int           x, y;    // where it stood at its last think
	std::uint32_t chase;   // monster it is running to
	DWORD         chaseAt; // when that run was ordered
};
std::array<OwnerState, 8> Owners {};
std::array<MercState, 24> Mercs {};

bool IsMelee(Role role) noexcept {
	return role == Role::Desert || role == Role::BashBarb || role == Role::FrenzyBarb;
}

OwnerState& OwnerOf(void* owner) noexcept {
	const std::uint32_t id = Field<std::uint32_t>(owner, UnitIdOffset);
	for (auto& entry : Owners)
		if (entry.id == id && (entry.fx != 0 || entry.fy != 0)) return entry;
	OwnerState& entry = Owners[id % Owners.size()];
	entry = { id, -1, -1, 0.0, 1.0, -1, -1, 0.0, 0.0 };
	return entry;
}

MercState& MercOf(void* merc, void* owner, bool front) noexcept {
	const std::uint32_t id = Field<std::uint32_t>(merc, UnitIdOffset), ownerId = Field<std::uint32_t>(owner, UnitIdOffset);
	const DWORD         now = GetTickCount();
	for (auto& entry : Mercs)
		if (entry.id == id && entry.owner == ownerId && entry.front == front) {
			Position(merc, entry.x, entry.y);
			return entry.seen = now, entry;
		}
	MercState* slot = &Mercs[0];
	for (auto& entry : Mercs)
		if (now - entry.seen > now - slot->seen) slot = &entry;
	// Next free place in this line of the formation.
	int place = 0;
	for (bool taken = true; taken && place < 8; ) {
		taken = false;
		for (const auto& entry : Mercs)
			if (&entry != slot && entry.owner == ownerId && entry.front == front && entry.slot == place && now - entry.seen < 10000) taken = true, ++place;
	}
	*slot = { id, ownerId, place, front, 0, 0, false, now, 0, 0, 0, 0 };
	Position(merc, slot->x, slot->y);
	return *slot;
}

// Formation spot: `depth` toward where you're heading (negative: behind you),
// spread sideways by place in the line.
void Spot(OwnerState& state, void* owner, const MercState& merc, int& x, int& y) noexcept {
	int ox = 0, oy = 0;
	Position(owner, ox, oy);
	if (state.px < 0) state.px = ox, state.py = oy;
	const std::uint32_t ownerMode = Field<std::uint32_t>(owner, UnitModeOffset);
	const bool          moving    = ownerMode == PlayerRun || ownerMode == 2 || ownerMode == 6;
	// Heading: where you're going right now. Standing still forgets it, so a
	// new move doesn't start in the direction of the previous one; the first
	// 2 steps give the new one. Small changes are blended (the grid makes a
	// straight run look like a zig-zag of steps), a real turn (over 60
	// degrees) is taken at once.
	if (!moving)
		state.hx = state.hy = 0.0, state.px = ox, state.py = oy;
	else if (const int dx = ox - state.px, dy = oy - state.py; std::max(std::abs(dx), std::abs(dy)) >= ((state.hx == 0 && state.hy == 0) ? 2 : 3)) {
		const double length = std::sqrt(double(dx) * dx + double(dy) * dy);
		const double nx = dx / length, ny = dy / length;
		if ((state.hx == 0 && state.hy == 0) || nx * state.hx + ny * state.hy < 0.5) state.hx = nx, state.hy = ny;
		else {
			const double bx = state.hx + nx, by = state.hy + ny, blended = std::sqrt(bx * bx + by * by);
			state.hx = bx / blended, state.hy = by / blended;
		}
		state.px = ox, state.py = oy;
	}
	// The formation faces where you last headed.
	if (state.hx != 0 || state.hy != 0) state.fx = state.hx, state.fy = state.hy;
	// Every mercenary has its own lane beside your path, none on it: running
	// alongside you they never bump into you or each other.
	static constexpr int Side[] = { -3, 3, -6, 6, -9, 9, -12, 12 };
	// While you move the spot is ahead of you, so each run is long and
	// straight (it only thinks again once a run is over).
	const double lead  = moving && (state.hx != 0 || state.hy != 0) ? 5.0 : 0.0;
	const double depth = (merc.front ? 2.0 : -3.0) + lead;
	const double side  = Side[merc.slot & 7];
	x = ox + static_cast<int>(std::lround(state.fx * depth - state.fy * side));
	y = oy + static_cast<int>(std::lround(state.fy * depth + state.fx * side));
}

int Cheb(int ax, int ay, int bx, int by) noexcept {
	return std::max(std::abs(ax - bx), std::abs(ay - by));
}

// Moves to a point or a unit. Running needs the run velocity set first, as the
// game's own follow move does (mode 15, speed 60, 100%).
void* GoingOwner = nullptr; // the owner of the mercenary thinking now

bool Go(void* game, void* merc, void* target, int x, int y, bool run) noexcept {
	// Never through you: you block mercenaries, and a run that bumps into you
	// ends there (then a new run, a new direction: the zig-zag). When the
	// straight line passes within 2 of you, it goes to a point 3 beside you,
	// on the far side of the line, first; its next think goes on from there.
	if (int mx = 0, my = 0, ox = 0, oy = 0; !target && GoingOwner && Position(merc, mx, my) && Position(GoingOwner, ox, oy)) {
		const double dx = x - mx, dy = y - my, length = std::sqrt(dx * dx + dy * dy);
		if (length >= 1.0 && Cheb(x, y, ox, oy) > 2) {
			const double ux = dx / length, uy = dy / length;
			const double along = (ox - mx) * ux + (oy - my) * uy;
			const double cross = ux * (oy - my) - uy * (ox - mx); // > 0: you are left of the line
			if (along > 0.0 && along < length && std::abs(cross) < 2.0) {
				const double side = cross > 0 ? -3.0 : 3.0; // left normal is (-uy, ux)
				const int    wx = ox + static_cast<int>(std::lround(-uy * side + ux)), wy = oy + static_cast<int>(std::lround(ux * side + uy));
				Mv(merc, "  goes around you: (%d,%d) first, on the way to (%d,%d)", wx, wy, x, y);
				x = wx, y = wy;
			}
		}
	}
	Velocity(merc, run ? ModeRun : 0, run ? 60 : 0, 100);
	// Steps 8: the whole path, corners included. With 1 (the game's usual
	// value) a run ended at the first corner, so going around you or another
	// mercenary became a stop, a new think and a new direction each time.
	const bool ok = Move(game, merc, target, run ? ModeRun : ModeWalk, x, y, 8, 0) != 0;
	// The planned path (points at path +0x204, count at +0x200) and where the
	// other mercenaries stand, to see detours around them mid-run.
	if (void* const path = ok ? GetPath(merc) : nullptr) {
		char         text[300] {};
		int          used  = 0;
		const auto   count = std::min<std::uint32_t>(Field<std::uint32_t>(path, 0x200), 12);
		for (std::uint32_t index = 0; index < count && used < 250; ++index)
			used += std::snprintf(text + used, sizeof(text) - used, " (%u,%u)", Field<std::uint16_t>(path, 0x204 + index * 4), Field<std::uint16_t>(path, 0x206 + index * 4));
		char       others[200] {};
		int        written = 0;
		const auto mercId  = Field<std::uint32_t>(merc, UnitIdOffset);
		const DWORD now    = GetTickCount();
		for (const auto& other : Mercs)
			if (other.id != mercId && other.id && now - other.seen < 1000 && written < 170)
				written += std::snprintf(others + written, sizeof(others) - written, " id%u(%d,%d)", other.id, other.x, other.y);
		Mv(merc, "  path %u points:%s | mask 0x%X | others:%s", Field<std::uint32_t>(path, 0x200), text, Field<std::uint32_t>(path, 0x64), others);
	}
	if (target) Mv(merc, "  Move to unit %u (%d,%d) run %d -> %s, mode now %u", Field<std::uint32_t>(target, UnitIdOffset), x, y, run, ok ? "ok" : "FAIL", Field<std::uint32_t>(merc, 0x0C));
	else Mv(merc, "  Move to (%d,%d) run %d -> %s, mode now %u", x, y, run, ok ? "ok" : "FAIL", Field<std::uint32_t>(merc, 0x0C));
	return ok;
}

// Goes to (or keeps going to) its formation spot. Returns false when already there.
// Standing on you (they all start the game on top of you): not a place to stay.
bool Crowded(const MercState& state, int ox, int oy) noexcept {
	// Other mercenaries don't count: they walk through each other.
	return Cheb(state.x, state.y, ox, oy) < 2;
}

// `loose` (town): stays wherever it is while within that range of you.
bool KeepFormation(void* game, void* merc, void* owner, MercState& state, int x, int y, bool walking, int loose = 0) noexcept {
	int mx = 0, my = 0, ox = 0, oy = 0;
	Position(merc, mx, my);
	Position(owner, ox, oy);
	const int  away    = Cheb(mx, my, x, y);
	const std::uint32_t ownerMode = Field<std::uint32_t>(owner, UnitModeOffset);
	const bool          running   = ownerMode == PlayerRun || ownerMode == 2 || ownerMode == 6; // run, walk, town walk
	if (loose > 0) {
		// Loose: no need for the spot, only to stay close to you.
		if (Cheb(mx, my, ox, oy) <= loose && !Crowded(state, ox, oy)) {
			Mv(merc, " town: within %d of you, stays", loose);
			state.moving = false;
			return false;
		}
		if (walking && state.moving) return Mv(merc, " town: still running back"), true; // let it finish the run back
		if (Cheb(mx, my, x, y) <= 1) return Mv(merc, " town: on its spot"), false; // it is on its spot already
	}
	// You're moving: run along its lane, to a spot ahead of you. The game
	// only lets it think again once a run is over, so short orders made it
	// stop and turn every few steps.
	if (loose <= 0 && running) {
		const OwnerState& heading = OwnerOf(owner);
		if (heading.hx == 0 && heading.hy == 0) return Mv(merc, " follow: you just started moving, waits for your heading"), false;
		Mv(merc, " follow: you head (%.2f,%.2f), runs to its lane spot (%d,%d)", heading.hx, heading.hy, x, y);
		DeleteEvent(game, merc, 2, 0);
		if (Go(game, merc, nullptr, x, y, true)) {
			state.spotX = x, state.spotY = y, state.moving = true;
			return true;
		}
	}
	// Ranged mercenaries accept a place slightly off their spot, as long as
	// it is still close to you, instead of hunting for the exact spot.
	const int tolerance = state.front ? 1 : 3;
	if (loose <= 0 && away <= tolerance && (state.front || Cheb(mx, my, ox, oy) <= 7) && (away == 0 || !Crowded(state, ox, oy))) {
		if (running && walking) return Mv(merc, " form: at spot (away %d), keeps running with you", away), true; // keep the run going, the spot moves on
		state.moving = false;
		if (!running) return Mv(merc, " form: in place (away %d, crowded %d)", away, Crowded(state, ox, oy)), false;
		Mv(merc, " form: in place but you run, moves on");
	}
	// On its way: keep the run going in a straight line instead of
	// re-planning every time the spot shifts a little.
	if (walking && state.moving && Cheb(state.spotX, state.spotY, x, y) <= (running ? 5 : 3))
		return Mv(merc, " form: on its way to (%d,%d), spot now (%d,%d), away %d", state.spotX, state.spotY, x, y, away), true;
	Mv(merc, " form: new order, spot (%d,%d) away %d, walking %d moving %d old (%d,%d) you run %d crowded %d", x, y, away, walking, state.moving, state.spotX, state.spotY, running, Crowded(state, ox, oy));
	DeleteEvent(game, merc, 2, 0);
	// Runs like you: whenever you run, or when it is more than a couple of steps off.
	const bool run = running || away > 3;
	if (Go(game, merc, nullptr, x, y, run)) {
		state.spotX = x, state.spotY = y, state.moving = true;
		return true;
	}
	// Spot taken (another mercenary, a wall): try around it.
	static constexpr int Around[8][2] = { { 2, 0 }, { -2, 0 }, { 0, 2 }, { 0, -2 }, { 2, 2 }, { -2, -2 }, { 2, -2 }, { -2, 2 } };
	for (const auto& offset : Around)
		if (Go(game, merc, nullptr, x + offset[0], y + offset[1], run)) {
			state.spotX = x, state.spotY = y, state.moving = true;
			return true;
		}
	// Spot blocked (wall, cliff). Ranged: stay put if already near you.
	state.moving = false;
	if (!state.front && Cheb(mx, my, ox, oy) <= 7) return Mv(merc, " form: spot blocked, stays"), false;
	Mv(merc, " form: spot blocked, game follow move");
	PetMove(game, owner, merc, 0, run ? 1 : 0, run ? 60 : 0, 0);
	return true;
}

// Self positioning: the place closest to you (then to where it stands) that
// keeps at least `exclusion` from every monster. False when where it stands
// already does.
bool Reposition(void* game, void* merc, void* owner, const Scan& scan, Role role) noexcept {
	constexpr double exclusion = 5.0;
	int ox = 0, oy = 0, mx = 0, my = 0;
	if (!Position(owner, ox, oy) || !Position(merc, mx, my)) return false;
	int ex[MaxCandidates], ey[MaxCandidates], count = 0;
	for (std::size_t index = 0; index < scan.count; ++index)
		if (Position(scan.units[index], ex[count], ey[count])) ++count;
	const auto clear = [&](double x, double y) {
		for (int index = 0; index < count; ++index)
			if ((ex[index] - x) * (ex[index] - x) + (ey[index] - y) * (ey[index] - y) < exclusion * exclusion) return false;
		return true;
	};
	const auto fromOwner = [&](double x, double y) { return std::sqrt((x - ox) * (x - ox) + (y - oy) * (y - oy)); };
	// Where it stands is good enough while a little looser than the target
	// distances: no hopping between two nearly equal places.
	const auto loose = [&](double x, double y) {
		for (int index = 0; index < count; ++index)
			if ((ex[index] - x) * (ex[index] - x) + (ey[index] - y) * (ey[index] - y) < (exclusion - 1) * (exclusion - 1)) return false;
		return true;
	};
	if (loose(mx, my) && fromOwner(mx, my) <= 9.0) return Mv(merc, " safe: current place ok (%d monsters)", count), false;
	double bestScore = 1e9;
	int    bx = 0, by = 0;
	for (const double radius : { 2.0, 3.5, 5.0, 6.5, 8.0, 10.0 }) {
		for (int step = 0; step < 16; ++step) {
			const double angle = step * 3.14159265358979 / 8.0;
			const double x = ox + radius * std::cos(angle), y = oy + radius * std::sin(angle);
			if (!clear(x, y)) continue;
			const double score = radius * 3.0 + std::sqrt((x - mx) * (x - mx) + (y - my) * (y - my));
			if (score < bestScore) bestScore = score, bx = static_cast<int>(std::lround(x)), by = static_cast<int>(std::lround(y));
		}
	}
	if (bestScore >= 1e9) return Mv(merc, " safe: no safe place found (%d monsters)", count), false;
	if (Cheb(bx, by, mx, my) <= 1) return Mv(merc, " safe: best place is here"), false;
	Mv(merc, " safe: goes to (%d,%d) score %.1f, you at (%d,%d), %d monsters", bx, by, bestScore, ox, oy, count);
	DeleteEvent(game, merc, 2, 0);
	if (!Go(game, merc, nullptr, bx, by, true)) return false;
	Note(merc, role, nullptr, -6, "moves to a safe place close to you");
	return true;
}

// Every target is behind a wall from where it stands: step right next to
// you, where you can see what you fight, rather than look for a shot.
bool StepToOwner(void* game, void* merc, void* owner, Role role) noexcept {
	int ox = 0, oy = 0, mx = 0, my = 0;
	if (!Position(owner, ox, oy) || !Position(merc, mx, my) || Cheb(mx, my, ox, oy) <= 3) return false;
	DeleteEvent(game, merc, 2, 0);
	if (!Go(game, merc, nullptr, ox + (mx > ox ? 1 : mx < ox ? -1 : 0), oy + (my > oy ? 1 : my < oy ? -1 : 0), true)) return false;
	Note(merc, role, nullptr, -7, "no clear shot, steps next to you");
	return true;
}

void AttackInner(void* game, void* merc, void* owner, void* target, Role role, const Scan& scan, void* tick, const std::uint16_t* params) noexcept {
	if (!AttackRoll(merc, tick, IsMelee(role))) return Idle(game, merc, 5);
	switch (role) {
	case Role::Rogue: return RogueAttack(game, merc, target, scan, params);
	case Role::IronWolf: return IronWolfAttack(game, merc, owner, target, scan, params);
	case Role::Desert: return DesertAttack(game, merc, target, params);
	case Role::BashBarb: return BashBarbAttack(game, merc, target, scan, params);
	case Role::FrenzyBarb: return FrenzyBarbAttack(game, merc, owner, target, params);
	default: return Idle(game, merc, 5);
	}
}

// Logs what an attack order did to the mercenary (a skill can make it walk).
void Attack(void* game, void* merc, void* owner, void* target, Role role, const Scan& scan, void* tick, const std::uint16_t* params) noexcept {
	const int distance = Distance(merc, target);
	AttackInner(game, merc, owner, target, role, scan, tick, params);
	Mv(merc, "  attack order on %u at dist %d, mode now %u", Field<std::uint32_t>(target, UnitIdOffset), distance, Field<std::uint32_t>(merc, UnitModeOffset));
}

// The hireling's ai params for this difficulty, as the game's Hireable builds them.
void Params(void* tick, std::uint16_t (&params)[4]) noexcept {
	void* const record = Field<void*>(tick, 0x28);
	const int   level  = Field<std::uint8_t>(tick, 0x38);
	for (int index = 0; index < 4; ++index) params[index] = record ? Field<std::uint16_t>(record, 0x90 + index * 6 + level * 2) : 0;
}

void Think(void* game, void* merc, void* owner, void* tick, Role role) noexcept {
	const std::uint32_t mode    = Field<std::uint32_t>(merc, UnitModeOffset);
	const bool          walking = mode == ModeWalk || mode == ModeRun;
	const int           fromOwner = Distance(merc, owner);
	if (fromOwner > 100) {
		PetMove(game, owner, merc, 3, 0, 0, 0); // teleport next to you, as the game does
		return;
	}
	const bool  melee = IsMelee(role);
	OwnerState& ownerState = OwnerOf(owner);
	MercState&  mercState  = MercOf(merc, owner, melee);
	int         sx = 0, sy = 0;
	Spot(ownerState, owner, mercState, sx, sy);
	{
		int ox = 0, oy = 0;
		Position(owner, ox, oy);
		Mv(merc, "THINK you (%d,%d) m%u dist %d face (%.2f,%.2f) slot %d%s spot (%d,%d)", ox, oy, Field<std::uint32_t>(owner, UnitModeOffset), fromOwner,
		   ownerState.fx, ownerState.fy, mercState.slot, melee ? " front" : " back", sx, sy);
	}
	if (fromOwner > 40) {
		Mv(merc, " far (>40): game trail run %s", walking ? "(already moving)" : "");
		if (!walking) PetMove(game, owner, merc, 1, 1, 60, 0); // catch up along your trail
		mercState.moving = false;
		return;
	}
	std::uint16_t params[4] {};
	Params(tick, params);
	Scan scan;
	Gather(game, merc, scan);
	void* const target  = ChooseTarget(game, merc, owner, role);
	const bool  blocked = OnlyBlocked;
	if (melee) {
		if (target) {
			mercState.moving = false;
			if (Distance(merc, target) >= 3 || !InMelee(merc, target, 0, nullptr)) {
				// Already running to it: let the run go on instead of starting
				// it over every few frames (that made them stutter).
				const std::uint32_t targetId = Field<std::uint32_t>(target, UnitIdOffset);
				if (walking && mercState.chase == targetId && GetTickCount() - mercState.chaseAt < 500) return Mv(merc, " melee: still running to %u (dist %d)", targetId, Distance(merc, target));
				Mv(merc, " melee: run to %u (dist %d, walking %d, previous %u)", targetId, Distance(merc, target), walking, mercState.chase);
				mercState.chase = targetId, mercState.chaseAt = GetTickCount();
				void* const stats2 = Field<void*>(tick, TickMonStats2);
				if (void* const path = GetPath(merc); path && stats2) SetSteps(path, Field<std::uint8_t>(stats2, 0x0E));
				DeleteEvent(game, merc, 2, 0);
				// To where it stands, not to the unit: a run to a unit only lasts
				// a step or two before it has to think again.
				int tx = 0, ty = 0;
				const bool placed = Position(target, tx, ty);
				if (int ux = 0, uy = 0; placed && Position(merc, ux, uy)) tx += (ux > tx) - (ux < tx), ty += (uy > ty) - (uy < ty); // next to it
				if (placed ? Go(game, merc, nullptr, tx, ty, true) : Go(game, merc, target, 0, 0, true)) return Note(merc, role, target, -4, "leaves the formation for an enemy in range");
			}
			if (walking) return Mv(merc, " melee: in reach but still moving");
			Mv(merc, " melee: attacks %u", Field<std::uint32_t>(target, UnitIdOffset));
			return Attack(game, merc, owner, target, role, scan, tick, params);
		}
		Mv(merc, " melee: no target");
		if (KeepFormation(game, merc, owner, mercState, sx, sy, walking)) return;
		return Mv(merc, " idle 3"), Idle(game, merc, 3);
	}
	// Ranged: positioning mode once you're close to enemies.
	int   nearOwner = INT_MAX, nearMerc = INT_MAX;
	void* closest   = nullptr;
	for (std::size_t index = 0; index < scan.count; ++index) {
		nearOwner = std::min(nearOwner, Distance(owner, scan.units[index]));
		if (scan.distance[index] < nearMerc) nearMerc = scan.distance[index], closest = scan.units[index];
	}
	const bool danger = nearOwner <= Config.danger.load(std::memory_order_relaxed);
	Mv(merc, " ranged: %zu monsters, nearest to you %d, to it %d, danger %d, target %u%s", scan.count, nearOwner == INT_MAX ? -1 : nearOwner,
	   nearMerc == INT_MAX ? -1 : nearMerc, danger, target ? Field<std::uint32_t>(target, UnitIdOffset) : 0, blocked ? " (all blocked)" : "");
	if (danger) {
		mercState.moving = false;
		if (walking) return Mv(merc, " ranged: still moving");
		if (Reposition(game, merc, owner, scan, role)) return;
		if (closest && nearMerc <= 4) {
			// Nowhere safe near you: the game's own step away from the monster.
			DeleteEvent(game, merc, 2, 0);
			if (Escape(game, merc, closest, 5, 1)) return Note(merc, role, closest, -5, "runs from a monster (hit and run)");
		}
		if (role == Role::IronWolf && Buff(game, merc, role)) return;
		if (target) return Mv(merc, " ranged: shoots"), Attack(game, merc, owner, target, role, scan, tick, params);
		if (blocked && StepToOwner(game, merc, owner, role)) return;
		if (KeepFormation(game, merc, owner, mercState, sx, sy, walking)) return;
		return Mv(merc, " idle 3"), Idle(game, merc, 3);
	}
	// Formation first; it shoots from its spot.
	if (KeepFormation(game, merc, owner, mercState, sx, sy, walking)) return;
	if (walking) return Mv(merc, " ranged: in place but still moving");
	if (role == Role::IronWolf && Buff(game, merc, role)) return;
	if (target) return Mv(merc, " ranged: shoots from formation"), Attack(game, merc, owner, target, role, scan, tick, params);
	if (blocked && StepToOwner(game, merc, owner, role)) return;
	Mv(merc, " idle 3");
	Idle(game, merc, 3);
}

// Mercenaries walk through your other mercenaries and summons. With allies
// on the game's pet footprint (Walk Through Allies) a pet only stands in
// the PET bit (0x2000) of the collision grid, and monsters move with that
// bit in their mask (0x3C01), so your mercenaries kept bumping into each
// other: each bump ends a run, and the next run starts in a new direction.
// Enemies and you still block them (NO_PATH 0x1000 stays in the mask).
void PassAllies(void* merc) noexcept {
	void* const path = GetPath(merc);
	if (!path) return;
	auto& mask = *reinterpret_cast<std::uint32_t*>(static_cast<char*>(path) + 0x64);
	if (mask & 0x2000) {
		Mv(merc, "collision mask 0x%X -> 0x%X (walks through your other pets), footprint 0x%X", mask, mask & ~0x2000u, Field<std::uint32_t>(path, 0x60));
		mask &= ~0x2000u;
	}
}

// Town: a loose group around you, never further than a warcry reaches.
constexpr int TownRange = 7;

void TownThink(void* game, void* merc, void* owner, Role role) noexcept {
	const std::uint32_t mode    = Field<std::uint32_t>(merc, UnitModeOffset);
	const bool          walking = mode == ModeWalk || mode == ModeRun;
	if (Distance(merc, owner) > 100) {
		PetMove(game, owner, merc, 3, 0, 0, 0);
		return;
	}
	OwnerState& ownerState = OwnerOf(owner);
	MercState&  mercState  = MercOf(merc, owner, IsMelee(role));
	int         sx = 0, sy = 0;
	Spot(ownerState, owner, mercState, sx, sy);
	// Two thirds of its field spot: well inside the range once it gets there.
	int ox = 0, oy = 0;
	Position(owner, ox, oy);
	sx = ox + (sx - ox) * 2 / 3, sy = oy + (sy - oy) * 2 / 3;
	Mv(merc, "TOWN you (%d,%d) m%u spot (%d,%d)", ox, oy, Field<std::uint32_t>(owner, UnitModeOffset), sx, sy);
	if (KeepFormation(game, merc, owner, mercState, sx, sy, walking, TownRange)) return;
	if (walking) return;
	Idle(game, merc, 5);
}

void HookHireable(void* game, void* unit, void* tick) noexcept {
	if (unit && tick && Enabled()) {
		const Role role = RoleOf(unit);
		if (role != Role::None) {
			void* const owner = PlayerOwner(unit);
			void* const room  = owner ? UnitRoom(unit) : nullptr;
			if (owner && room) {
				PassAllies(unit);
				GoingOwner = owner;
				struct Clear { ~Clear() { GoingOwner = nullptr; } } clear;
				if (InTown(room)) {
					Trace(unit, "town think");
					return TownThink(game, unit, owner, role);
				}
				Trace(unit, "formation think");
				return Think(game, unit, owner, tick, role);
			}
		}
	}
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
	Trace(unit, target ? "picked a target" : "no target in leash");
	if (!target) {
		if (distance) *distance = INT_MAX;
		if (inMelee) *inMelee = 0;
		return nullptr;
	}
	if (distance) *distance = Distance(unit, target);
	if (inMelee) *inMelee = InMelee(unit, target, 0, nullptr);
	return target;
}

void HookCombat(void* game, void* unit, std::uint16_t classId, void* owner, void* target, void* seed, void* tick, std::uint16_t* params) noexcept {
	const Role role = Enabled() && unit && owner && target && params ? RoleOf(unit) : Role::None;
	if (role == Role::None || UnitType(owner) != 0 || !PetInfo(owner, Field<std::uint32_t>(unit, UnitIdOffset))) {
		if (unit && Enabled()) Trace(unit, role == Role::None ? "combat: type not handled, game AI" : "combat: no pet info, game AI");
		return Combat(game, unit, classId, owner, target, seed, tick, params);
	}
	Trace(unit, "combat: new AI");
	Mv(unit, "COMBAT hook (game's AI asked for an attack) target %u dist %d", Field<std::uint32_t>(target, UnitIdOffset), Distance(unit, target));
	const bool melee = params[0] == 2;
	if ((role == Role::BashBarb || role == Role::FrenzyBarb) && BodyBlock(game, unit, owner, target, role)) return;
	// Melee types out of reach: the game runs to the target as usual.
	if (melee && (Distance(unit, target) >= 3 || !InMelee(unit, target, 0, nullptr)))
		return Combat(game, unit, classId, owner, target, seed, tick, params);
	Scan scan;
	Gather(game, unit, scan);
	if (role == Role::IronWolf && Buff(game, unit, role)) return;
	if (!melee && KeepDistance(game, unit, owner, target, role)) return;
	if (!AttackRoll(unit, tick, melee)) return Idle(game, unit, 10);
	switch (role) {
	case Role::Rogue: return RogueAttack(game, unit, target, scan, params);
	case Role::IronWolf: return IronWolfAttack(game, unit, owner, target, scan, params);
	case Role::Desert: return DesertAttack(game, unit, target, params);
	case Role::BashBarb: return BashBarbAttack(game, unit, target, scan, params);
	case Role::FrenzyBarb: return FrenzyBarbAttack(game, unit, owner, target, params);
	default: return Combat(game, unit, classId, owner, target, seed, tick, params);
	}
}

// ---- version check ----

bool Locate() noexcept {
	const std::array<const Code*, 33> checks { &BlockedCode, &VelocityCode, &WalkToCode, &PetMoveCode, &MoveCode, &GetPathCode, &SetStepsCode, &UnitRoomCode, &InTownCode, &HireableCode, &SelectTargetCode, &CombatCode, &UseSkillCode, &DefaultAtkCode, &IdleCode,
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
	WalkTo          = reinterpret_cast<WalkToFn>(at(WalkToCode));
	PetMove         = reinterpret_cast<PetMoveFn>(at(PetMoveCode));
	Move            = reinterpret_cast<MoveFn>(at(MoveCode));
	GetPath         = reinterpret_cast<GetPathFn>(at(GetPathCode));
	Blocked         = reinterpret_cast<BlockedFn>(at(BlockedCode));
	Velocity        = reinterpret_cast<VelocityFn>(at(VelocityCode));
	SetSteps        = reinterpret_cast<SetStepsFn>(at(SetStepsCode));
	UnitRoom        = reinterpret_cast<UnitRoomFn>(at(UnitRoomCode));
	InTown          = reinterpret_cast<InTownFn>(at(InTownCode));
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
	if (key == "engage_range") return range(Config.engage);
	if (key == "danger_range") return range(Config.danger);
	if (key == "aura_leash") return true; // 0.0.1-0.0.4 setting, no longer used
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
	if (key == "engage_range") return std::to_string(Config.engage.load());
	if (key == "danger_range") return std::to_string(Config.danger.load());
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

# Mercenaries keep a formation around you: melee ones slightly in front,
# ranged ones slightly behind. All distances are 4 to 24.

# How far from you a mercenary picks fights. Monsters attacking the mercenary
# itself are always fought.
leash = )" + ValueOf("leash") + R"(

# Melee mercenaries (Desert mercenary, Barbarians) leave the formation for an
# enemy this close to them.
engage_range = )" + ValueOf("engage_range") + R"(

# Ranged mercenaries (Rogue, Iron Wolf) stay in formation and shoot from there
# until you get this close to enemies; then they position themselves away from
# monsters, hitting and running.
danger_range = )" + ValueOf("danger_range") + R"(

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
note = "Behind you, hit and run, area arrows on groups, normal arrows on immunes."
[[setting]]
key = "desert_mercenary"
type = "bool"
label = "Desert mercenary"
note = "In front of you like the Barbarians; Jab on bosses."
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
key = "engage_range"
type = "int"
label = "Melee engage range"
note = "Melee mercenaries leave the formation for an enemy this close. 4 to 24."
min = 4
max = 24
step = 1
shift_step = 2
ctrl_step = 4
[[setting]]
key = "danger_range"
type = "int"
label = "Ranged danger range"
note = "Ranged mercenaries start keeping away from monsters when you get this close to them. 4 to 24."
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
	Log(false, "Smarter Mercenaries %s: %s, leash %d, engage %d, danger %d", PluginVersion, Config.enabled ? "active" : "loaded but turned off",
	    Config.leash.load(), Config.engage.load(), Config.danger.load());
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	Menu.Stop();
}
