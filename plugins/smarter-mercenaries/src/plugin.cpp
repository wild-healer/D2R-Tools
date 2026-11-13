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
constexpr const char* PluginVersion = "0.0.30";

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
	std::atomic<int>  safeLife { 35 };  // safe mode below this life percent
	std::atomic<int>  safeBurst { 25 }; // ... or when it loses this percent of its life
	std::atomic<int>  safeWindow { 2 }; // ... within this many seconds
	std::atomic<int>  protect { 5 };    // melee mercenaries regroup on you when enemies get this close to you
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
constexpr Code SetEventCode     { 0x48B720, { 0x48, 0x83, 0xEC, 0x48, 0x8B, 0x84, 0x24, 0x80, 0x00, 0x00, 0x00, 0x89, 0x44, 0x24, 0x38, 0x8B }, 0 };
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
constexpr Code RemoveFootCode   { 0x342240, { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x20, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83 }, 0 };
// Path code (D2Common in 1.10): re-aim a run without a new mode.
constexpr Code FindPathCode     { 0x340E70, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89, 0x7C, 0x24, 0x18, 0x55 }, 5 }; // Edge Clicks hooks its entry
constexpr Code PathTargetCode   { 0x342A50, { 0x48, 0x85, 0xC9, 0x74, 0x11, 0x66, 0x89, 0x51, 0x10, 0x66, 0x44, 0x89, 0x41, 0x12, 0x48, 0xC7 }, 0 };
constexpr Code PathTypeCode     { 0x342A70, { 0x48, 0x63, 0xC2, 0x4C, 0x8D, 0x15, 0x86, 0xD5, 0xCB, 0xFF, 0x4C, 0x8D, 0x0C, 0x85, 0x00, 0x00 }, 0 };
constexpr Code RefreshCode      { 0x34DC80, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0xFA, 0x48, 0x8B, 0xD9, 0x85 }, 0 };
constexpr Code GridCode         { 0x364C70, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57 }, 0 }; // collision grid check
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
using SetEventFn     = void* (*)(void* game, void* unit, int type, int frame, int a, int b, int c) noexcept;
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
using GridFn         = int (*)(void* room, int x, int y, int pattern, int mask) noexcept; // nonzero: something with that mask there
using FindPathFn     = int (*)(void* path, void* unit, int allowTown, int server) noexcept; // number of points
using PathTargetFn   = void (*)(void* path, int x, int y) noexcept;
using PathTypeFn     = void (*)(void* path, int type) noexcept;
using RefreshFn      = void (*)(void* unit, int update) noexcept;
using RemoveFootFn   = int (*)(void* unit, int force) noexcept;
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
SetEventFn     SetEvent {};
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
FindPathFn     FindPath {};
GridFn         Grid {};
PathTargetFn   PathTarget {};
PathTypeFn     PathType {};
RefreshFn      Refresh {};
RemoveFootFn   RemoveFoot {};
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

// What you are doing, read from how you move (see ReadIntent): it changes
// what the mercenaries go for, where they stand and when they back off.
enum class Intent { Hold, Travel, Advance, Retreat };
const char* IntentName(Intent intent) noexcept {
	return intent == Intent::Advance ? "advance" : intent == Intent::Retreat ? "retreat" : intent == Intent::Travel ? "travel" : "hold";
}
struct View {
	Intent intent  = Intent::Hold;
	double hx = 0, hy = 0;   // your heading while you move
	bool   protect = false;  // enemies right next to you: melee regroups on you
	bool   safe    = false;  // this mercenary is in safe mode
};
thread_local View Now {};

// Set by ChooseTarget: a ranged mercenary saw targets, but only behind walls.
bool OnlyBlocked = false;

bool Position(void* unit, int& x, int& y) noexcept;

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
		// Protecting you: melee only looks at what is right around you.
		if (melee && Now.protect) {
			if (fromOwner > Config.protect.load() + 2 && !(selfDefense && fromMerc <= 2)) continue;
		} else if ((fromOwner > leash || (melee && fromMerc > (Now.intent == Intent::Retreat ? 3 : engage))) && !selfDefense) continue;
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
		if (melee && Now.protect) score = fromOwner * 6 + fromMerc;
		// Your own target: ranged mercenaries focus it, melee ones only lean
		// toward it (they deal with what is around you first).
		if (unit == wanted) score -= melee ? 15 : 60;
		// Where you're going: monsters ahead of you count more when you push
		// in, monsters left behind count less when you back off.
		if (Now.intent == Intent::Advance || Now.intent == Intent::Retreat) {
			int ox = 0, oy = 0, ux = 0, uy = 0;
			if (Position(owner, ox, oy) && Position(unit, ux, uy)) {
				const double dx = ux - ox, dy = uy - oy, length = std::sqrt(dx * dx + dy * dy);
				const double ahead = length > 0 ? (dx * Now.hx + dy * Now.hy) / length : 0;
				if (Now.intent == Intent::Advance && ahead > 0.5) score -= 20;
				if (Now.intent == Intent::Retreat && ahead < 0 && itsTarget != owner && itsTarget != merc) score += 40;
			}
		}
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
	int           sx, sy; // where you stood at the last think
	DWORD         movedAt; // last time you were seen moving
	bool          moving;  // moving, or stopped for less than a moment
	int           mirror;  // 1 or -1: which side of you each lane is on
	DWORD         stillSince; // first moment seen standing (0: moving)
	bool          stopped;    // standing for more than the frame between two moves
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
	entry = { id, -1, -1, 0.0, 1.0, -1, -1, 0.0, 0.0, -1, -1, 0, false, 1, 0, true };
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

// Every mercenary has its own lane beside your path, none on it: running
// alongside you they never bump into you or each other.
// Melee and ranged lanes interleave, so no two mercenaries share one
// (a shared lane made a front and a back mercenary run into each other).
double LaneSide(const MercState& merc) noexcept {
	static constexpr int FrontSide[] = { -2, 2, -6, 6, -10, 10, -14, 14 };
	static constexpr int BackSide[]  = { -4, 4, -8, 8, -12, 12, -16, 16 };
	return (merc.front ? FrontSide : BackSide)[merc.slot & 7];
}

// A new heading (you start moving, or turn sharply): lanes are set on the
// sides of your path where the mercenaries already are. Turning around
// otherwise sent every mercenary to the other side of you, all of them
// crossing in front of you and each other.
void ChooseSides(OwnerState& state, int ox, int oy, double nx, double ny) noexcept {
	const DWORD now = GetTickCount();
	double      keep = 0.0, flip = 0.0;
	int         count = 0;
	for (const auto& merc : Mercs) {
		if (!merc.id || merc.owner != state.id || now - merc.seen > 2000) continue;
		const double side = -ny * (merc.x - ox) + nx * (merc.y - oy); // its place across your new path
		const double lane = LaneSide(merc) * state.mirror;
		keep += std::abs(side - lane), flip += std::abs(side + lane), ++count;
	}
	if (count && flip < keep) {
		state.mirror = -state.mirror;
		Log(false, "mv %lu sides: new heading (%.2f,%.2f), lanes flip to keep %d mercenaries on their side (cost %.0f vs %.0f)",
		    static_cast<unsigned long>(now % 1000000), nx, ny, count, flip, keep);
	}
}

// Formation spot: `depth` toward where you're heading (negative: behind you),
// spread sideways by place in the line.
void Spot(OwnerState& state, void* owner, const MercState& merc, int& x, int& y) noexcept {
	int ox = 0, oy = 0;
	Position(owner, ox, oy);
	if (state.px < 0) state.px = ox, state.py = oy;
	// Moving: your mode says so, or you changed place less than half a
	// second ago. Holding the mouse to run makes you stand for a frame between
	// two moves; going by the mode alone, every such frame dropped the
	// mercenaries back to formation (and you "started moving" again a moment
	// later), a change of direction each time in a straight run.
	const std::uint32_t ownerMode = Field<std::uint32_t>(owner, UnitModeOffset);
	const DWORD         now       = GetTickCount();
	const bool going = ownerMode == PlayerRun || ownerMode == 2 || ownerMode == 6;
	if (going || ox != state.sx || oy != state.sy) state.movedAt = now, state.stillSince = 0;
	else if (!state.stillSince) state.stillSince = now;
	// Holding the mouse you stand for one frame (40 ms) between two moves;
	// standing longer than that is a stop.
	state.stopped = state.stillSince && now - state.stillSince >= 60;
	state.sx = ox, state.sy = oy;
	const bool moving = now - state.movedAt < 500;
	state.moving      = moving;
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
		if ((state.hx == 0 && state.hy == 0) || nx * state.hx + ny * state.hy < 0.5) {
			ChooseSides(state, ox, oy, nx, ny);
			state.hx = nx, state.hy = ny;
		}
		else {
			const double bx = state.hx + nx, by = state.hy + ny, blended = std::sqrt(bx * bx + by * by);
			state.hx = bx / blended, state.hy = by / blended;
		}
		state.px = ox, state.py = oy;
	}
	// The formation faces where you last headed.
	if (state.hx != 0 || state.hy != 0) state.fx = state.hx, state.fy = state.hy;
	// While you move the spot is ahead of you, so each run is long and
	// straight (it only thinks again once a run is over).
	// Only while you really move this frame: in the half second after you
	// stop the spot no longer leads, or every mercenary ran 3 past it and back.
	const double lead  = moving && !state.stopped && (state.hx != 0 || state.hy != 0) ? 3.0 : 0.0;
	const double depth = (merc.front ? 2.0 : -3.0) + lead;
	const double side  = LaneSide(merc) * state.mirror;
	x = ox + static_cast<int>(std::lround(state.fx * depth - state.fy * side));
	y = oy + static_cast<int>(std::lround(state.fy * depth + state.fx * side));
}

int Cheb(int ax, int ay, int bx, int by) noexcept {
	return std::max(std::abs(ax - bx), std::abs(ay - by));
}

// Path types (D2Common): 13 is the game's own walk and run path, a straight
// line toward the spot that steps around what is in the way; 15 slides along
// walls. Up to 0.0.27 every order asked for 15 (the first argument of
// Velocity is the path type, not the mode): a sliding path is a line of grid
// steps, and it has no path at all to a spot 2 or fewer steps away (every
// such order failed, a stop and a retry each time).
constexpr int PathToward = 13, PathSlide = 15;
constexpr std::uintptr_t PathIndexOffset = 0x30, PathCountOffset = 0x34, PathTargetUnitOffset = 0x70, PathDistanceOffset = 0xC0;

// Re-aims a run already going, without a new mode. A new Move is a new mode:
// the game restarts the run animation and sends the run again, so re-aiming
// every few frames that way showed as a twitch each time. The game itself
// re-aims a run this way when the unit it follows moves on.
bool Steer(void* merc, int x, int y, std::uint32_t mode) noexcept {
	if (Field<std::uint32_t>(merc, UnitModeOffset) != mode) return false;
	void* const path = GetPath(merc);
	if (!path || Field<void*>(path, PathTargetUnitOffset)) return false;
	if (Field<std::int32_t>(path, PathIndexOffset) >= Field<std::int32_t>(path, PathCountOffset)) return false; // the run is ending
	PathTarget(path, x, y);
	static_cast<std::uint8_t*>(path)[PathDistanceOffset] = 20; // steps it may still re-plan, as a new move sets
	PathType(path, PathToward);
	int points = FindPath(path, merc, 0, 1);
	if (!points) {
		PathType(path, PathSlide);
		points = FindPath(path, merc, 0, 1);
	}
	if (points) Refresh(merc, 1); // sends the new path to the clients
	return points != 0;
}

// Moves to a point or a unit. Running needs the run speed set first, as the
// game's own follow move does (speed 60 percent, path up to 100 steps).
void* GoingOwner = nullptr; // the owner of the mercenary thinking now

// Ends a run where the mercenary is: a path to where it stands has no points,
// so the game ends the run at its next step (no new mode, no turn).
void Halt(void* merc) noexcept {
	void* const path = GetPath(merc);
	int         x = 0, y = 0;
	if (!path || !Position(merc, x, y)) return;
	PathTarget(path, x, y);
	FindPath(path, merc, 0, 1);
	Mv(merc, "  Halt: run ends here");
}

bool Go(void* game, void* merc, void* target, int x, int y, bool run) noexcept {
	const int mode = run ? ModeRun : ModeWalk;
	if (!target && Steer(merc, x, y, mode)) {
		Mv(merc, "  Steer to (%d,%d) run %d -> ok, path type %d, %d points", x, y, run, Field<std::int32_t>(GetPath(merc), 0x50), Field<std::int32_t>(GetPath(merc), PathCountOffset));
		return true;
	}
	Velocity(merc, 0, run ? 60 : 0, 100); // 0: the mode's own path type (13)
	const bool ok = Move(game, merc, target, mode, x, y, 1, 0) != 0;
	void* const path = GetPath(merc);
	const int   type = path ? Field<std::int32_t>(path, 0x50) : -1, points = path ? Field<std::int32_t>(path, PathCountOffset) : 0;
	if (target) Mv(merc, "  Move to unit %u (%d,%d) run %d -> %s, mode now %u, path type %d, %d points", Field<std::uint32_t>(target, UnitIdOffset), x, y, run, ok ? "ok" : "FAIL", Field<std::uint32_t>(merc, 0x0C), type, points);
	else Mv(merc, "  Move to (%d,%d) run %d -> %s, mode now %u, path type %d, %d points", x, y, run, ok ? "ok" : "FAIL", Field<std::uint32_t>(merc, 0x0C), type, points);
	return ok;
}


// ---- awareness: enemy speed, mercenary health, your intent ----

// How fast each monster moves, from where it was seen before (tiles a
// second). The top speed seen fades over a second and a half, so a monster
// that charged a moment ago still counts as fast.
struct Track {
	std::uint32_t id;
	int           x, y;
	DWORD         at;
	double        speed;
};
std::array<Track, 256> Tracks {};

double SpeedOf(void* unit) noexcept {
	const std::uint32_t id = Field<std::uint32_t>(unit, UnitIdOffset);
	Track&              track = Tracks[id & 255];
	int                 x = 0, y = 0;
	if (!Position(unit, x, y)) return 0;
	const DWORD now = GetTickCount();
	if (track.id != id || !track.at) {
		track = { id, x, y, now, 0.0 };
		return 0;
	}
	const DWORD elapsed = now - track.at;
	if (elapsed >= 150) {
		const double moved   = std::sqrt(double(x - track.x) * (x - track.x) + double(y - track.y) * (y - track.y));
		const double current = std::min(moved * 1000.0 / elapsed, 30.0); // a teleport is not a speed
		track.speed = elapsed > 3000 ? current : std::max(current, track.speed * std::pow(0.5, elapsed / 1500.0));
		track.x = x, track.y = y, track.at = now;
	}
	return track.speed;
}

// Room a ranged mercenary keeps from a monster: more for a fast one, since it
// covers the gap before the mercenary can step away again.
double Exclusion(void* unit, double base) noexcept {
	return std::clamp(base + SpeedOf(unit) * 0.4, base, base + 6.0);
}

// Life over the last seconds, to see a mercenary in trouble.
struct Vitals {
	std::uint32_t id;
	int           life[32];
	DWORD         at[32];
	int           head;
	bool          safe;
	DWORD         troubleAt; // last time it was low or losing life fast
	DWORD         dodgeAt, breakAt;
};
std::array<Vitals, 24> VitalsTable {};

Vitals& VitalsOf(void* merc) noexcept {
	const std::uint32_t id = Field<std::uint32_t>(merc, UnitIdOffset);
	Vitals*             oldest = &VitalsTable[0];
	for (auto& entry : VitalsTable) {
		if (entry.id == id) return entry;
		const DWORD seen = entry.at[(entry.head + 31) & 31];
		if (seen < oldest->at[(oldest->head + 31) & 31]) oldest = &entry;
	}
	*oldest    = {};
	oldest->id = id;
	return *oldest;
}

// Safe mode: below the life setting, or losing the burst setting's worth of
// life within the window. It ends once its life is back up (25 over the
// setting) and nothing hurt it badly for 2 seconds.
bool UpdateSafe(void* merc, Role role) noexcept {
	Vitals&     vitals = VitalsOf(merc);
	const DWORD now    = GetTickCount();
	const int   life   = LifePercent(merc);
	if (now - vitals.at[(vitals.head + 31) & 31] >= 200) {
		vitals.life[vitals.head] = life, vitals.at[vitals.head] = now;
		vitals.head = (vitals.head + 1) & 31;
	}
	const DWORD window = static_cast<DWORD>(Config.safeWindow.load()) * 1000;
	int         peak   = life;
	for (int index = 0; index < 32; ++index)
		if (vitals.at[index] && now - vitals.at[index] <= window) peak = std::max(peak, vitals.life[index]);
	const int  lost    = peak - life;
	const int  low     = Config.safeLife.load();
	const bool trouble = life < low || lost >= Config.safeBurst.load();
	if (trouble) {
		if (!vitals.safe) Note(merc, role, nullptr, -10, life < low ? "safe mode: life is low" : "safe mode: losing life fast");
		vitals.safe = true, vitals.troubleAt = now;
	} else if (vitals.safe && life >= std::min(95, low + 25) && now - vitals.troubleAt >= 2000) {
		vitals.safe = false;
		Note(merc, role, nullptr, -11, "safe mode over, back to fighting");
	}
	if (vitals.safe) Mv(merc, " vitals: life %d%%, lost %d%% in %lus, safe mode", life, lost, static_cast<unsigned long>(window / 1000));
	return vitals.safe;
}

// Your intent, from how you move against the monsters around you: pushing
// in (advance), backing off (retreat), moving with nothing in the way
// (travel) or standing (hold).
Intent ReadIntent(void* owner, const OwnerState& state, const Scan& scan) noexcept {
	if (!state.moving || state.stopped || (state.hx == 0 && state.hy == 0)) return Intent::Hold;
	int ox = 0, oy = 0;
	if (!Position(owner, ox, oy)) return Intent::Hold;
	double cx = 0, cy = 0;
	int    count = 0;
	const int leash = Config.leash.load();
	for (std::size_t index = 0; index < scan.count; ++index) {
		int x = 0, y = 0;
		if (!Position(scan.units[index], x, y) || Cheb(x, y, ox, oy) > leash) continue;
		cx += x - ox, cy += y - oy, ++count;
	}
	if (!count) return Intent::Travel;
	const double length = std::sqrt(cx * cx + cy * cy);
	if (length < 1.0) return Intent::Advance; // you're in the middle of them
	const double toward = (cx * state.hx + cy * state.hy) / length;
	return toward > 0.4 ? Intent::Advance : toward < -0.2 ? Intent::Retreat : Intent::Travel;
}

// Missiles mark the collision grid (0x40), as the game's own mercenary
// dodge checks. A free spot: no missile, no wall.
constexpr int GridWall = 0x01, GridMissile = 0x40;

bool GridAt(void* merc, int x, int y, int mask) noexcept {
	void* const path = GetPath(merc);
	void* const room = path ? Field<void*>(path, 0x20) : nullptr;
	return room && Grid(room, x, y, static_cast<int>(Field<std::uint32_t>(path, 0x5C)), mask) != 0;
}

void* Nearest(void* merc, const Scan& scan, int& distance) noexcept {
	void* nearest = nullptr;
	distance      = INT_MAX;
	for (std::size_t index = 0; index < scan.count; ++index)
		if (scan.distance[index] < distance) distance = scan.distance[index], nearest = scan.units[index];
	return nearest;
}

// A missile on it or coming from the nearest monster: a quick step sideways
// (or back), to the free side closest to you.
bool Dodge(void* game, void* merc, void* owner, const Scan& scan, Role role) noexcept {
	Vitals&     vitals = VitalsOf(merc);
	const DWORD now    = GetTickCount();
	if (now - vitals.dodgeAt < 600) return false;
	int distance = 0, mx = 0, my = 0, ex = 0, ey = 0, ox = 0, oy = 0;
	void* const enemy = Nearest(merc, scan, distance);
	if (!enemy || distance > 16 || !Position(merc, mx, my) || !Position(enemy, ex, ey) || !Position(owner, ox, oy)) return false;
	const double dx = ex - mx, dy = ey - my, length = std::max(1.0, std::sqrt(dx * dx + dy * dy));
	const double ux = dx / length, uy = dy / length;
	bool incoming = GridAt(merc, mx, my, GridMissile);
	for (const double step : { 2.0, 3.5 })
		incoming = incoming || GridAt(merc, mx + static_cast<int>(std::lround(ux * step)), my + static_cast<int>(std::lround(uy * step)), GridMissile);
	if (!incoming) return false;
	const double sides[3][2] = { { -uy * 3, ux * 3 }, { uy * 3, -ux * 3 }, { -ux * 3, -uy * 3 } };
	int  bx = 0, by = 0, best = INT_MAX;
	for (const auto& side : sides) {
		const int x = mx + static_cast<int>(std::lround(side[0])), y = my + static_cast<int>(std::lround(side[1]));
		if (GridAt(merc, x, y, GridWall | GridMissile)) continue;
		const int score = Cheb(x, y, ox, oy);
		if (score < best) best = score, bx = x, by = y;
	}
	if (best == INT_MAX) return false;
	vitals.dodgeAt = now;
	DeleteEvent(game, merc, 2, 0);
	if (!Go(game, merc, nullptr, bx, by, true)) return false;
	Note(merc, role, enemy, -12, "dodges a missile");
	return true;
}

// Crowd control on a monster that gets close: freezing or slowing arrows and
// spikes, Stun and Bash (knockback), Taunt. The Desert mercenary has none.
bool CrowdControl(void* game, void* merc, Role role, void* target) noexcept {
	const bool cold = !Immune(target, Cold);
	switch (role) {
	case Role::Rogue:
		if (cold && (Cast(game, merc, ModeAttack1, SkillFreezingArrow, target) || Cast(game, merc, ModeAttack1, SkillColdArrow, target)))
			return Note(merc, role, target, SkillFreezingArrow, "safe mode: slows a monster that got close"), true;
		return false;
	case Role::IronWolf:
		if (cold && Cast(game, merc, ModeCast, SkillGlacialSpike, target)) return Note(merc, role, target, SkillGlacialSpike, "safe mode: freezes a monster that got close"), true;
		return false;
	case Role::BashBarb:
		if (!CheckState(target, StateStunned) && Cast(game, merc, ModeAttack1, SkillStun, target)) return Note(merc, role, target, SkillStun, "safe mode: stuns a monster that got close"), true;
		if (Cast(game, merc, ModeAttack1, SkillBash, target)) return Note(merc, role, target, SkillBash, "safe mode: bashes a monster back"), true;
		return false;
	case Role::FrenzyBarb:
		if (!CheckState(target, StateTaunt) && Cast(game, merc, ModeAttack2, SkillTaunt, target)) return Note(merc, role, target, SkillTaunt, "safe mode: taunts a monster that got close"), true;
		return false;
	default: return false;
	}
}

// Getting surrounded: monsters within 4 on every side but one narrow gap
// (melee: 4 monsters and no gap of 120 degrees; ranged: 3 and 150). It moves
// out to the free spot with the most room, toward you.
bool BreakOut(void* game, void* merc, void* owner, const Scan& scan, Role role) noexcept {
	const bool  melee  = IsMelee(role);
	Vitals&     vitals = VitalsOf(merc);
	const DWORD now    = GetTickCount();
	if (now - vitals.breakAt < 1500) return false;
	int mx = 0, my = 0, ox = 0, oy = 0;
	if (!Position(merc, mx, my) || !Position(owner, ox, oy)) return false;
	double angles[MaxCandidates];
	int    count = 0;
	for (std::size_t index = 0; index < scan.count; ++index) {
		int x = 0, y = 0;
		if (scan.distance[index] > 4 || !Position(scan.units[index], x, y) || (x == mx && y == my)) continue;
		angles[count++] = std::atan2(double(y - my), double(x - mx));
	}
	if (count < (melee ? 4 : 3)) return false;
	std::sort(angles, angles + count);
	double gap = angles[0] + 2 * 3.14159265358979 - angles[count - 1];
	for (int index = 1; index < count; ++index) gap = std::max(gap, angles[index] - angles[index - 1]);
	if (gap >= (melee ? 2.09 : 2.62)) return false;
	double bestScore = -1e9;
	int    bx = 0, by = 0;
	for (const double radius : { 4.0, 6.0 })
		for (int step = 0; step < 16; ++step) {
			const double angle = step * 3.14159265358979 / 8.0;
			const int    x = mx + static_cast<int>(std::lround(radius * std::cos(angle))), y = my + static_cast<int>(std::lround(radius * std::sin(angle)));
			if (GridAt(merc, x, y, GridWall)) continue;
			double room = 1e9;
			for (std::size_t index = 0; index < scan.count; ++index) {
				int ux = 0, uy = 0;
				if (Position(scan.units[index], ux, uy)) room = std::min(room, std::sqrt(double(ux - x) * (ux - x) + double(uy - y) * (uy - y)));
			}
			if (room < 3.0) continue;
			const double score = room * 3.0 - std::sqrt(double(x - ox) * (x - ox) + double(y - oy) * (y - oy)) * 0.7;
			if (score > bestScore) bestScore = score, bx = x, by = y;
		}
	if (bestScore <= -1e9) return Mv(merc, " surrounded (%d monsters, gap %.0f deg), no way out", count, gap * 57.3), false;
	vitals.breakAt = now;
	Mv(merc, " surrounded (%d monsters, widest gap %.0f deg): breaks out to (%d,%d)", count, gap * 57.3, bx, by);
	DeleteEvent(game, merc, 2, 0);
	if (!Go(game, merc, nullptr, bx, by, true)) return false;
	Note(merc, role, nullptr, -13, "breaks out of a ring of monsters");
	return true;
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
	const bool running = OwnerOf(owner).moving; // moving, or stopped only for a moment
	// "Close to you" means no further than its own spot is (the outer lanes
	// are 8 to the side): with a fixed 7 a mercenary on its outer spot never
	// counted as in place and stepped off it and back again forever.
	const int closeEnough = std::max(7, Cheb(x, y, ox, oy) + 1);
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
		// You stopped (or stand for a frame between two moves): a mercenary
		// close to its spot stops where it is, instead of finishing a run
		// aimed ahead of you and coming back.
		const bool going = !heading.stopped;
		if (!going && away <= 3 && Cheb(mx, my, ox, oy) <= closeEnough && !Crowded(state, ox, oy)) {
			if (walking) Halt(merc);
			state.moving = false;
			return Mv(merc, " follow: you stopped, it stops close to its spot (away %d)", away), false;
		}
		// Already on its spot (it runs as fast as you): aim a little further
		// along your way, so it keeps running instead of stopping for a frame.
		if (away <= 1 && going) {
			x += static_cast<int>(std::lround(heading.hx * 3.0)), y += static_cast<int>(std::lround(heading.hy * 3.0));
			Mv(merc, " follow: on its lane spot, aims 3 further");
		}
		Mv(merc, " follow: you head (%.2f,%.2f), runs to its lane spot (%d,%d)", heading.hx, heading.hy, x, y);
		DeleteEvent(game, merc, 2, 0);
		if (Go(game, merc, nullptr, x, y, true)) {
			state.spotX = x, state.spotY = y, state.moving = true;
			// The game only lets it think again once a run is over, so a long
			// run went on to where you were heading when it started, even after
			// you turned. Its next think comes 6 frames into the run instead:
			// it re-aims mid-run, without stopping.
			SetEvent(game, merc, 2, Field<std::int32_t>(game, 0x170) + 6, 0, 0, 0);
			return true;
		}
		// Lane spot blocked (a wall, the edge of a path): halfway to you
		// instead. No more tries around it: ten orders in one think made it
		// stand and twitch, and the next think has a new spot anyway.
		const int hx = (x + ox) / 2, hy = (y + oy) / 2;
		if (Cheb(hx, hy, mx, my) > 1 && Go(game, merc, nullptr, hx, hy, true)) {
			state.spotX = x, state.spotY = y, state.moving = true;
			SetEvent(game, merc, 2, Field<std::int32_t>(game, 0x170) + 6, 0, 0, 0);
			return true;
		}
		state.moving = false;
		if (Cheb(mx, my, ox, oy) <= closeEnough) return Mv(merc, " follow: lane blocked, waits for the next think"), false;
		Mv(merc, " follow: lane blocked, game follow move");
		PetMove(game, owner, merc, 0, 1, 60, 0);
		return true;
	}
	// Ranged mercenaries accept a place slightly off their spot, as long as
	// it is still close to you, instead of hunting for the exact spot.
	// When you stop, a mercenary that ran a little ahead with you stays
	// where it is instead of running back to the exact spot (a run there and
	// a run back looked like a zig-zag).
	const int tolerance = 3;
	if (loose <= 0 && (away == 0 || (away <= tolerance && (state.front || Cheb(mx, my, ox, oy) <= closeEnough) && !Crowded(state, ox, oy)))) {
		if (running && walking) return Mv(merc, " form: at spot (away %d), keeps running with you", away), true; // keep the run going, the spot moves on
		state.moving = false;
		if (!running) {
			if (walking) Halt(merc); // in place: no run on past it
			return Mv(merc, " form: in place (away %d, crowded %d)", away, Crowded(state, ox, oy)), false;
		}
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
	if (!state.front && Cheb(mx, my, ox, oy) <= closeEnough) return Mv(merc, " form: spot blocked, stays"), false;
	Mv(merc, " form: spot blocked, game follow move");
	PetMove(game, owner, merc, 0, run ? 1 : 0, run ? 60 : 0, 0);
	return true;
}

// Self positioning: the place closest to you (then to where it stands) that
// keeps at least `exclusion` from every monster. False when where it stands
// already does.
// Each monster's room grows with its speed (Exclusion); `scale` widens it
// all (safe mode keeps further away).
bool Reposition(void* game, void* merc, void* owner, const Scan& scan, Role role, double scale = 1.0) noexcept {
	int ox = 0, oy = 0, mx = 0, my = 0;
	if (!Position(owner, ox, oy) || !Position(merc, mx, my)) return false;
	int    ex[MaxCandidates], ey[MaxCandidates], count = 0;
	double room[MaxCandidates], widest = 0;
	for (std::size_t index = 0; index < scan.count; ++index)
		if (Position(scan.units[index], ex[count], ey[count])) {
			room[count] = Exclusion(scan.units[index], 5.0) * scale;
			widest      = std::max(widest, room[count]);
			++count;
		}
	const auto clear = [&](double x, double y) {
		for (int index = 0; index < count; ++index)
			if ((ex[index] - x) * (ex[index] - x) + (ey[index] - y) * (ey[index] - y) < room[index] * room[index]) return false;
		return true;
	};
	const auto fromOwner = [&](double x, double y) { return std::sqrt((x - ox) * (x - ox) + (y - oy) * (y - oy)); };
	// Where it stands is good enough while a little looser than the target
	// distances: no hopping between two nearly equal places.
	const auto loose = [&](double x, double y) {
		for (int index = 0; index < count; ++index)
			if ((ex[index] - x) * (ex[index] - x) + (ey[index] - y) * (ey[index] - y) < (room[index] - 1) * (room[index] - 1)) return false;
		return true;
	};
	if (loose(mx, my) && fromOwner(mx, my) <= 9.0) return Mv(merc, " safe: current place ok (%d monsters, widest room %.1f)", count, widest), false;
	double bestScore = 1e9;
	int    bx = 0, by = 0;
	for (const double radius : { 2.0, 3.5, 5.0, 6.5, 8.0, 10.0, 12.0 }) {
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

// Safe mode: keeps away from monsters (further than usual), dodges
// missiles, uses its crowd control on what gets close and lets its life come
// back. Ranged mercenaries still shoot when nothing is near them.
void SafeThink(void* game, void* merc, void* owner, MercState& state, int sx, int sy, bool walking, void* target, Role role, const Scan& scan, void* tick,
               const std::uint16_t* params) noexcept {
	if (walking) return Mv(merc, " safe mode: still moving");
	state.moving = false;
	if (Dodge(game, merc, owner, scan, role)) return;
	int         distance = 0;
	void* const nearest  = Nearest(merc, scan, distance);
	if (nearest && distance <= 4 && AttackRoll(merc, tick, IsMelee(role)) && CrowdControl(game, merc, role, nearest)) return;
	if (Reposition(game, merc, owner, scan, role, 1.6)) return;
	if (nearest && distance <= 3) {
		DeleteEvent(game, merc, 2, 0);
		if (Escape(game, merc, nearest, 5, 1)) return Note(merc, role, nearest, -14, "safe mode: backs away from a monster");
	}
	if (!IsMelee(role) && target && distance >= 8) return Mv(merc, " safe mode: shoots from a distance"), Attack(game, merc, owner, target, role, scan, tick, params);
	if (!nearest || distance > 10) {
		if (KeepFormation(game, merc, owner, state, sx, sy, walking)) return;
	}
	Mv(merc, " safe mode: waits, life coming back");
	Idle(game, merc, 5);
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
		void* const path = GetPath(merc);
		Mv(merc, "THINK you (%d,%d) m%u dist %d face (%.2f,%.2f) slot %d%s spot (%d,%d) | path type %d point %d/%d to (%d,%d)", ox, oy, Field<std::uint32_t>(owner, UnitModeOffset), fromOwner,
		   ownerState.fx, ownerState.fy, mercState.slot, melee ? " front" : " back", sx, sy, path ? Field<std::int32_t>(path, 0x50) : -1,
		   path ? Field<std::int32_t>(path, PathIndexOffset) : 0, path ? Field<std::int32_t>(path, PathCountOffset) : 0,
		   path ? Field<std::uint16_t>(path, 0x10) : 0, path ? Field<std::uint16_t>(path, 0x12) : 0);
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
	// What you're doing and how things stand, for the choices below.
	Now        = {};
	Now.intent = ReadIntent(owner, ownerState, scan);
	Now.hx = ownerState.hx, Now.hy = ownerState.hy;
	for (std::size_t index = 0; index < scan.count; ++index)
		if (Distance(owner, scan.units[index]) <= Config.protect.load()) Now.protect = true;
	Now.safe = UpdateSafe(merc, role);
	void* const target  = ChooseTarget(game, merc, owner, role);
	const bool  blocked = OnlyBlocked;
	Mv(merc, " view: you %s%s%s", IntentName(Now.intent), Now.protect ? ", enemies on you" : "", Now.safe ? ", it is in safe mode" : "");
	if (Now.safe) return SafeThink(game, merc, owner, mercState, sx, sy, walking, target, role, scan, tick, params);
	if (!walking && BreakOut(game, merc, owner, scan, role)) return;
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
				if (placed ? Go(game, merc, nullptr, tx, ty, true) : Go(game, merc, target, 0, 0, true))
					return Note(merc, role, target, Now.protect ? -15 : -4, Now.protect ? "regroups on you against a monster at you" : "leaves the formation for an enemy in range");
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
	// Pushing in, they hold the formation a little longer; backing off, they
	// back off with you (the formation moves away) instead of standing to
	// hit and run, unless something is in their face.
	const int  dangerRange = Config.danger.load(std::memory_order_relaxed) - (Now.intent == Intent::Advance ? 3 : 0);
	const bool danger      = Now.intent != Intent::Retreat && nearOwner <= dangerRange;
	if (Now.intent == Intent::Retreat && closest && nearMerc <= 3 && !walking) {
		DeleteEvent(game, merc, 2, 0);
		if (Escape(game, merc, closest, 5, 1)) return Note(merc, role, closest, -16, "backs off with you, away from a monster in its face");
	}
	Mv(merc, " ranged: %zu monsters, nearest to you %d, to it %d, danger %d, target %u%s", scan.count, nearOwner == INT_MAX ? -1 : nearOwner,
	   nearMerc == INT_MAX ? -1 : nearMerc, danger, target ? Field<std::uint32_t>(target, UnitIdOffset) : 0, blocked ? " (all blocked)" : "");
	if (danger) {
		mercState.moving = false;
		if (walking) return Mv(merc, " ranged: still moving");
		if (Reposition(game, merc, owner, scan, role)) return;
		if (closest && nearMerc <= (Now.intent == Intent::Advance ? 3 : 4)) {
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
	Now = {};
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
	const std::array<const Code*, 40> checks { &GridCode, &FindPathCode, &PathTargetCode, &PathTypeCode, &RefreshCode, &SetEventCode, &RemoveFootCode, &BlockedCode, &VelocityCode, &WalkToCode, &PetMoveCode, &MoveCode, &GetPathCode, &SetStepsCode, &UnitRoomCode, &InTownCode, &HireableCode, &SelectTargetCode, &CombatCode, &UseSkillCode, &DefaultAtkCode, &IdleCode,
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
	SetEvent        = reinterpret_cast<SetEventFn>(at(SetEventCode));
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
	FindPath        = reinterpret_cast<FindPathFn>(at(FindPathCode));
	Grid            = reinterpret_cast<GridFn>(at(GridCode));
	PathTarget      = reinterpret_cast<PathTargetFn>(at(PathTargetCode));
	PathType        = reinterpret_cast<PathTypeFn>(at(PathTypeCode));
	Refresh         = reinterpret_cast<RefreshFn>(at(RefreshCode));
	RemoveFoot      = reinterpret_cast<RemoveFootFn>(at(RemoveFootCode));
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
	const auto range = [&](std::atomic<int>& target, long low = MinLeash, long high = MaxLeash) {
		char*      end    = nullptr;
		const long number = std::strtol(value.c_str(), &end, 10);
		if (value.empty() || *end || number < low || number > high) return error = std::to_string(low) + " to " + std::to_string(high), false;
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
	if (key == "safe_life") return range(Config.safeLife, 10, 90);
	if (key == "safe_burst") return range(Config.safeBurst, 10, 90);
	if (key == "safe_window") return range(Config.safeWindow, 1, 5);
	if (key == "protect_range") return range(Config.protect, 2, 12);
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
	if (key == "safe_life") return std::to_string(Config.safeLife.load());
	if (key == "safe_burst") return std::to_string(Config.safeBurst.load());
	if (key == "safe_window") return std::to_string(Config.safeWindow.load());
	if (key == "protect_range") return std::to_string(Config.protect.load());
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

# Melee mercenaries drop what they're doing and regroup on you when a monster
# gets this close to you (2 to 12).
protect_range = )" + ValueOf("protect_range") + R"(

# Safe mode: a mercenary below safe_life percent of its life, or losing
# safe_burst percent within safe_window seconds, keeps away from monsters,
# dodges missiles, uses its crowd control on what gets close and lets its life
# come back (10 to 90, 10 to 90, 1 to 5).
safe_life = )" + ValueOf("safe_life") + R"(
safe_burst = )" + ValueOf("safe_burst") + R"(
safe_window = )" + ValueOf("safe_window") + R"(

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
key = "protect_range"
type = "int"
label = "Protect range"
note = "Melee mercenaries regroup on you when a monster gets this close to you. 2 to 12."
min = 2
max = 12
step = 1
shift_step = 2
ctrl_step = 4
[[setting]]
key = "safe_life"
type = "int"
label = "Safe mode below life %"
note = "A mercenary under this much life keeps away from monsters and lets its life come back. 10 to 90."
min = 10
max = 90
step = 5
shift_step = 10
ctrl_step = 20
[[setting]]
key = "safe_burst"
type = "int"
label = "Safe mode on life lost %"
note = "Also safe mode when a mercenary loses this much of its life within the time below. 10 to 90."
min = 10
max = 90
step = 5
shift_step = 10
ctrl_step = 20
[[setting]]
key = "safe_window"
type = "int"
label = "Life lost within (seconds)"
note = "The time for the setting above. 1 to 5."
min = 1
max = 5
step = 1
shift_step = 1
ctrl_step = 2
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
