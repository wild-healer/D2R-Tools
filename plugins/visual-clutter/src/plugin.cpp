// Visual Clutter: hides visual effects without changing what they do.
//
// Features: aura effects and, separately, shout effects (Shout, Battle Orders,
// Battle Command, which the game draws as auras), each chosen per kind of unit
// they are drawn on and per kind of unit that cast them (you, your merc, your
// summons, other players and theirs, monsters).
//
// How D2R draws a state's look (RVAs of Steam 3.3.93787):
//   - The client's set-state handler (0x21A880, run for packets 0xA7/0xA8/0xAA)
//     toggles the state on the unit, then calls the state's overlay function
//     (0x21B640, unit + state). For aura states (0x335950) that function adds
//     overlay1/overlay2 of states.txt as "aura" overlay nodes (type 8) to the
//     unit's legacy overlay list, and the HD layer builds its effects from
//     those nodes. Not calling it for a state leaves the unit with no overlay
//     node for it, so nothing is drawn, nothing has to be removed later, and
//     the state itself (stats, buff icon) is untouched.
//   - The client knows nothing about who cast an aura: the set-state packet has
//     no caster. The in-process server does. Auras reach their targets through
//     0x574C90 (game, caster, target, ...), which gives the target a stat list
//     owned by the target itself and toggles the state on (D2Common 0x3354C0,
//     shared by server and client). The plugin hooks both: the caster passed to
//     0x574C90 is remembered for the target and state while the state lasts.
//     The newer aura code applies each target through a callback (0x5616F0,
//     context +0x08 = caster) whose stat list D2RCore builds and toggles from
//     its own code; that callback is hooked the same way.
//     A few older aura callbacks (e.g. 0x45E610) instead own the target's stat
//     list by the caster; for those the toggle hook reads the owner of the
//     unit's stat list for the state (0x2F5940). Client stat lists are owned by
//     their own unit, so client calls change nothing.
//   - Kinds of units come from the client: the local player's unit id, and the
//     client's pet list (filled from packets 0x7A / 0x81) that gives the owner
//     and pet type (7 = hireling) of every merc and summon.
// Other effects (client side only, the server still runs them):
//   - Missiles: the client's missile creation (0x1B7760) returns null for
//     Firestorm (Hellfire Torch, druid) and Thunder Storm bolts from the chosen
//     owners, so neither the bolt nor its light is ever drawn.
//   - Martial arts charge orbs (kept up by Mosaic): inside the legacy overlay
//     creation (0x150280), its overlay record lookup (call at 0x1502A4) gets
//     no record for the charge overlays on the chosen units, so the function
//     leaves at once. The function's start is left untouched: other plugins
//     (e.g. LTBL) call it and check its first bytes.
//   - Terror zone tint: QuestUpdateFX starts a screen tint when you enter a
//     terror zone (call at 0x170499); the screen tint function (0xF3E40) skips
//     that call.
// Settings apply live (I Hate TOMLs or the TOML): the client's aura and shout
// overlays are tracked, so a change removes the overlays of auras that are now
// hidden (as the game does when a state ends, 0x21B400) and adds those of auras
// that are now shown (0x21B640). Charge orbs already drawn are removed when
// they become hidden; the terror zone tint is cleared or put back at once.
// Everything is checked against the expected code at load: a mismatch leaves
// the plugin loaded but inactive.

#include <D2RLPlugin/api.h>
#include <windows.h>

#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "i_hate_tomls_client.h"

namespace {

constexpr const char* PluginId      = "visual-clutter";
constexpr const char* PluginVersion = "1.0.2-dev-1";

const D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = PluginId,
	.name        = "Visual Clutter",
	.version     = PluginVersion,
	.author      = "wild-healer",
	.description = "Hides aura visual effects by who they are on and who cast them; the auras still work.",
	.flags       = D2RL::PluginFlags::Client | D2RL::PluginFlags::NativeHooks,
};

// ---- native layout (Steam 3.3.93787) ----

// Hooked functions, with the bytes their entry must start with.
constexpr std::uint64_t StateOverlaysRva = 0x21B640; // void (Unit*, int state)
constexpr std::array<std::uint8_t, 15> StateOverlaysBytes { 0x40, 0x53, 0x57, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x50, 0x44, 0x8B, 0xFA, 0x48, 0x8B, 0xF9 };
constexpr std::uint64_t ToggleStateRva = 0x3354C0; // void (Unit*, int state, int on)
constexpr std::array<std::uint8_t, 20> ToggleStateBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x20 };
// Aura target callback (lambda) of the newer aura code: (context, target), context +0x08 = caster.
// Its stat list comes from 0x433D20, which D2RCore replaces and which toggles the state itself.
constexpr std::uint64_t AuraTargetRva = 0x5616F0; // uintptr (Context*, Unit* target, ...)
constexpr std::array<std::uint8_t, 27> AuraTargetBytes { 0x40, 0x55, 0x56, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xD1, 0x48, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00, 0x4C, 0x8B, 0x61, 0x18 };
constexpr std::uintptr_t AuraTargetCaster = 0x08;
constexpr std::uint64_t ApplyAuraRva = 0x574C90; // int (Game*, Unit* caster, Unit* target, int duration, int skill, int level)
constexpr std::array<std::uint8_t, 34> ApplyAuraBytes { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x41, 0x54, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x40, 0x4C, 0x8B, 0xE2, 0x48, 0x8B, 0xF1, 0x8B, 0x94, 0x24, 0x80 };

// Client missile creation: Unit* (MissileParams*, int), params +0x08 owner unit, +0x20 missile id.
// Returns null when the missile can't be made, which every caller handles.
constexpr std::uint64_t CreateMissileRva = 0x1B7760;
constexpr std::array<std::uint8_t, 25> CreateMissileBytes { 0x89, 0x54, 0x24, 0x10, 0x55, 0x57, 0x41, 0x55, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xC1, 0x48, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xF9 };
constexpr std::uintptr_t MissileParamsOwner = 0x08;
constexpr std::uintptr_t MissileParamsId    = 0x20;
// Legacy overlay creation 0x150280: void (Unit*, int overlay, int type, ...7 args). Its start
// (checked byte for byte by other plugins) is not patched. It keeps the unit in rsi
// (mov rsi, rcx at 0x150297), then looks up the overlay record:
//   0x1502A2 mov edx, edi; call OverlayRecord; mov rbp, rax; test rax, rax; je <return>
// A null record makes it return at once, nothing created. That call is pointed at a
// cave stub (mov r8, rsi; jmp ChargeRecordLookup) so the lookup also gets the unit.
constexpr std::uint64_t OverlayUnitRva = 0x150297;
constexpr std::array<std::uint8_t, 3> OverlayUnitBytes { 0x48, 0x8B, 0xF1 };
constexpr std::uint64_t OverlayLookupRva = 0x1502A2;
constexpr std::array<std::uint8_t, 19> OverlayLookupBytes { 0x8B, 0xD7, 0xE8, 0xF7, 0xFE, 0xFF, 0xFF, 0x48, 0x8B, 0xE8, 0x48, 0x85, 0xC0, 0x0F, 0x84, 0x49, 0x04, 0x00, 0x00 };
constexpr std::uint64_t OverlayLookupCallRva = 0x1502A4;
constexpr std::array<std::uint8_t, 5> OverlayLookupCallBytes { 0xE8, 0xF7, 0xFE, 0xFF, 0xFF };
// Screen tint: void (int kind, int fadeIn, int hold, int fadeOut), stores them in globals.
// QuestUpdateFX calls ScreenTint(3, 1000, 2000, 1000) at 0x170499 when you enter a terror
// zone; the tint lasts until you leave it. Skipping that call keeps the zone's sound.
constexpr std::uint64_t ScreenTintRva = 0xF3E40;
constexpr std::array<std::uint8_t, 15> ScreenTintBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18 };
constexpr std::uint64_t TerrorTintReturnRva = 0x17049E;
constexpr std::uint64_t TerrorTintCallRva   = 0x170480; // jne skip; mov edx, 1000; mov ecx, 3; mov r9d, edx; mov r8d, 2000; call ScreenTint
constexpr std::array<std::uint8_t, 30> TerrorTintCallBytes { 0x0F, 0x85, 0x92, 0x00, 0x00, 0x00, 0xBA, 0xE8, 0x03, 0x00, 0x00, 0xB9, 0x03, 0x00, 0x00, 0x00,
	                                                         0x44, 0x8B, 0xCA, 0x41, 0xB8, 0xD0, 0x07, 0x00, 0x00, 0xE8, 0xA2, 0x39, 0xF8, 0xFF };
constexpr std::uint64_t TintResetRva = 0xF3EB0; // void (), clears the screen tint (QuestUpdateFX calls it when you leave a terror zone)
constexpr std::array<std::uint8_t, 4> TintResetBytes { 0x33, 0xC0, 0x89, 0x05 };

// Called functions, checked the same way.
constexpr std::uint64_t OverlayRecordRva = 0x1501A0; // OverlayRecord* (uint8 version, int overlay), 0xE0-byte records
constexpr std::array<std::uint8_t, 24> OverlayRecordBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x63, 0xF2, 0xE8, 0xD9, 0x08, 0x1B, 0x00, 0x48 };
constexpr std::uint64_t StatListForStateRva = 0x2F5940; // StatList* (Unit*, int state)
constexpr std::array<std::uint8_t, 15> StatListForStateBytes { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0xDA, 0x48, 0x8B, 0xF9 };
constexpr std::uint64_t IsAuraStateRva = 0x335950; // int (uint8 version, int state)
constexpr std::array<std::uint8_t, 18> IsAuraStateBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x63, 0xDA };
constexpr std::uint64_t UnitVersionRva = 0x34A0E0; // uint8 (Unit*)
constexpr std::array<std::uint8_t, 9> UnitVersionBytes { 0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1A };

constexpr std::uint64_t FindClientUnitRva = 0x9A5D0; // Unit* (uint32 id, int type), the client's unit table
constexpr std::array<std::uint8_t, 6> FindClientUnitBytes { 0x4C, 0x63, 0xCA, 0x48, 0x8D, 0x05 };
constexpr std::uint64_t HasStateRva = 0x2F6220; // int (Unit*, int state): the state's bit in the unit's stat list
constexpr std::array<std::uint8_t, 16> HasStateBytes { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x63, 0xFA, 0x48, 0x85, 0xC9 };
constexpr std::uint64_t StateRecordRva = 0x21B0C0; // StatesRecord* (uint8 version, int state); +0x02 overlay1, +0x04 overlay2 (int16)
constexpr std::array<std::uint8_t, 18> StateRecordBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x63, 0xF2 };
constexpr std::uintptr_t StateOverlay1 = 0x02;
constexpr std::uintptr_t StateOverlay2 = 0x04;
// The client's state-off code (0x21AC50) removes a state's look with this: void (Unit*, int overlay), every row of the overlay.
constexpr std::uint64_t RemoveStateOverlayRva = 0x21B400;
constexpr std::array<std::uint8_t, 15> RemoveStateOverlayBytes { 0x83, 0xFA, 0xFF, 0x74, 0x63, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20 };
constexpr std::uint64_t RemoveOverlayRva = 0x152090; // void (Unit*, 0, int overlay): removes the unit's first node of that overlay
constexpr std::array<std::uint8_t, 9> RemoveOverlayBytes { 0x48, 0x85, 0xC9, 0x0F, 0x84, 0x98, 0x00, 0x00, 0x00 };

// Globals, read from the RIP-relative operand of known instructions.
constexpr std::uint64_t LocalIndexSiteRva = 0x8B2D0; // mov eax, [rip+x]; ret  -> local player index
constexpr std::array<int, 7> LocalIndexSite { 0x8B, 0x05, -1, -1, -1, -1, 0xC3 };
constexpr std::uint64_t PlayerIdsSiteRva = 0x9A4BE; // lea rdi, [rip+x]  -> uint32 ids[8], -1 = none
constexpr std::array<int, 7> PlayerIdsSite { 0x48, 0x8D, 0x3D, -1, -1, -1, -1 };
constexpr std::uint64_t PetListSiteRva = 0x13B270; // mov rax, [rip+x]; test rax, rax  -> first pet node
constexpr std::array<int, 10> PetListSite { 0x48, 0x8B, 0x05, -1, -1, -1, -1, 0x48, 0x85, 0xC0 };
constexpr std::uint64_t TerrorFlagSiteRva = 0x17046F; // mov dword [rip+x], 1  -> 1 while the terror zone tint is on
constexpr std::array<int, 10> TerrorFlagSite { 0xC7, 0x05, -1, -1, -1, -1, 0x01, 0x00, 0x00, 0x00 };

// Units: +0x00 uint32 type (0 player, 1 monster), +0x08 uint32 id.
constexpr std::uintptr_t UnitType = 0x00;
constexpr std::uintptr_t UnitId   = 0x08;
// Stat lists: +0x08 uint32 owner type, +0x0C uint32 owner id.
constexpr std::uintptr_t StatListOwnerType = 0x08;
constexpr std::uintptr_t StatListOwnerId   = 0x0C;
// Client pet nodes: +0x04 pet type (pettype.txt row), +0x08 pet id, +0x0C owner id, +0x30 next.
constexpr std::uintptr_t PetType  = 0x04;
constexpr std::uintptr_t PetId    = 0x08;
constexpr std::uintptr_t PetOwner = 0x0C;
constexpr std::uintptr_t PetNext  = 0x30;
constexpr std::uint32_t  PetTypeHireling = 7;

constexpr std::uint32_t TypePlayer  = 0;
constexpr std::uint32_t TypeMonster = 1;
constexpr std::uint32_t NoId        = 0xFFFFFFFF;

using StateOverlaysFn    = void (*)(void* unit, int state) noexcept;
using ToggleStateFn      = void (*)(void* unit, int state, int on) noexcept;
using CreateMissileFn    = void* (*)(void* params, int flags) noexcept;
using OverlayRecordFn    = const void* (*)(std::uint8_t version, int overlay) noexcept;
using AuraTargetFn       = std::uintptr_t (*)(void* context, void* target, void* r8, void* r9) noexcept;
using ApplyAuraFn        = int (*)(void* game, void* caster, void* target, int duration, int skill, int level) noexcept;
using StatListForStateFn = void* (*)(void* unit, int state) noexcept;
using IsAuraStateFn      = int (*)(std::uint8_t version, int state) noexcept;
using UnitVersionFn      = std::uint8_t (*)(void* unit) noexcept;
using ScreenTintFn       = void (*)(int kind, int fadeIn, int hold, int fadeOut) noexcept;
using TintResetFn        = void (*)() noexcept;
using FindClientUnitFn   = void* (*)(std::uint32_t id, int type) noexcept;
using HasStateFn         = int (*)(void* unit, int state) noexcept;
using StateRecordFn      = const void* (*)(std::uint8_t version, int state) noexcept;
using RemoveStateOverlayFn = void (*)(void* unit, int overlay) noexcept;
using RemoveOverlayFn    = void (*)(void* unit, int zero, int overlay) noexcept;

// ---- state ----

const D2RL::PluginContext* Context {};
std::uintptr_t             Base {};
std::atomic_bool           Active {};
std::string                GameVersion;
std::string                Problem; // why the plugin is inactive

StateOverlaysFn    OriginalStateOverlays {};
ToggleStateFn      OriginalToggleState {};
CreateMissileFn    OriginalCreateMissile {};
OverlayRecordFn    OverlayRecord {};
ApplyAuraFn        OriginalApplyAura {};
AuraTargetFn       OriginalAuraTarget {};
StatListForStateFn StatListForState {};
IsAuraStateFn      IsAuraState {};
UnitVersionFn      UnitVersion {};
ScreenTintFn       OriginalScreenTint {};
TintResetFn        TintReset {};
FindClientUnitFn   FindClientUnit {};
HasStateFn         HasState {};
StateRecordFn      StateRecord {};
RemoveStateOverlayFn RemoveStateOverlay {};
RemoveOverlayFn    RemoveOverlay {};
std::uintptr_t     TerrorFlagAddress {};
std::uintptr_t     LocalIndexAddress {};
std::uintptr_t     PlayerIdsAddress {};
std::uintptr_t     PetListAddress {};

// Who a unit is, seen from the local player.
enum Kind : int { Self, Merc, Minions, Allies, Monsters, KindCount, Other = KindCount };
constexpr std::array<const char*, KindCount> KindNames { "self", "merc", "minions", "allies", "monsters" };

// A set of kinds, one bit per Kind.
using Kinds = std::atomic<std::uint8_t>;

bool Has(const Kinds& kinds, int kind) noexcept {
	return kind >= 0 && kind < KindCount && (kinds.load(std::memory_order_relaxed) >> kind & 1);
}

// Read by the hooks on the game's threads, changed live from I Hate TOMLs.
struct Settings {
	std::atomic<bool> enabled { true };
	// hide[on]: kinds of caster whose auras are hidden on a unit of kind `on`.
	std::array<Kinds, KindCount> hide {};
	// The same for the barbarian's shouts (Shout, Battle Orders, Battle Command),
	// which the game also draws as auras.
	std::array<Kinds, KindCount> hideShouts {};
	// Kinds of unit whose effects are hidden: missiles by owner, charge orbs by the unit wearing them.
	Kinds             hideFirestorm {}, hideThunderStorm {}, hideCharges {};
	std::atomic<bool> hideTerrorTint { false };
} Config;

// missiles.txt rows (RotW, same in 93787 and 94995).
// Hellfire Torch's "Firestorm" is skill 197 DiabWall (diabwallmaker 179, diabwall 180);
// the druid's Firestorm is firestorm 457, firestormmaker 458.
constexpr std::array<int, 4> FirestormMissiles { 179, 180, 457, 458 };
constexpr std::array<int, 5> ThunderStormMissiles { 166, 167, 168, 169, 741 }; // thunderstorm1-4, colossal thunderstorm
// Overlay files of the martial arts charge orbs (progressive_* 1-3, tiger and cobra strike), matched by file
// name because overlay ids shift around the "Expansion" row of overlay.txt.
constexpr std::array<const char*, 24> ChargeOverlayFiles {
	"missile_a_a", "missile_a_b", "missile_a_c", "missile_b_a", "missile_b_b", "missile_b_c",
	"missile_c_a", "missile_c_b", "missile_c_c", "missile_red_a", "missile_red_b", "missile_red_c",
	"missile_blue_a", "missile_blue_b", "missile_blue_c", "missile_white_a", "missile_white_b", "missile_white_c",
	"tiger_strike_a", "tiger_strike_b", "tiger_strike_c", "cobra_strike_a", "cobra_strike_b", "cobra_strike_c" };
constexpr int MaxOverlays = 1024;
std::array<std::atomic<std::uint8_t>, MaxOverlays> ChargeOverlay {}; // 0 unknown, 1 charge orb, 2 other

// Casters of aura states, by (unit type, unit id, state), as the server attached them.
SRWLOCK                                        CasterLock = SRWLOCK_INIT;
std::unordered_map<std::uint64_t, std::uint64_t> Casters;

// The aura being applied on this (server) thread, while 0x574C90 runs.
thread_local void*         PendingTarget {};
thread_local std::uint64_t PendingCaster {};

// Aura and shout states the client has been asked to draw, by Key: Drawn or Skipped.
// Kept so a settings change can add or remove their overlays.
enum : std::uint8_t { Drawn = 1, Skipped = 2 };
SRWLOCK                                         TrackLock = SRWLOCK_INIT;
std::unordered_map<std::uint64_t, std::uint8_t> Tracked;
// Units (Caster keys) that got a charge orb overlay drawn.
std::unordered_set<std::uint64_t>               ChargeUnits;
// Set while the game's own state overlay code runs: its off/on toggle of the state is not a state change.
thread_local bool InStateOverlays {};

std::atomic<std::uint32_t> HiddenCount {}, ShownCount {}, RecordedCount {};
std::atomic<std::uint32_t> LoggedDecisions {};
constexpr std::uint32_t    MaxLoggedDecisions = 40;

void Log(bool warn, const char* format, ...) noexcept {
	if (!Context) return;
	char    line[512] {};
	va_list args;
	va_start(args, format);
	std::vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	warn ? Context->LogWarn(line) : Context->LogInfo(line);
}

// ---- guarded memory access ----

// Reads the game's own memory after checking it is committed and readable
// (ReadProcessMemory made one antivirus heuristic call a DLL an injector).
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
bool Read(std::uintptr_t address, T& value) noexcept {
	return Read(address, &value, sizeof(T));
}

template <std::size_t N>
bool CodeMatches(std::uint64_t rva, const std::array<std::uint8_t, N>& bytes) noexcept {
	std::array<std::uint8_t, N> code {};
	return Read(Base + rva, code.data(), N) && code == bytes;
}

// Where the RIP-relative operand (at site+operand) of the instruction at rva points, if the code matches.
template <std::size_t N>
std::uintptr_t RipTarget(std::uint64_t rva, const std::array<int, N>& pattern, std::size_t operand, std::size_t instructionSize) noexcept {
	std::array<std::uint8_t, N> code {};
	if (!Read(Base + rva, code.data(), N)) return 0;
	for (std::size_t i = 0; i < N; ++i)
		if (pattern[i] >= 0 && code[i] != static_cast<std::uint8_t>(pattern[i])) return 0;
	std::int32_t displacement;
	std::memcpy(&displacement, code.data() + operand, sizeof(displacement));
	return Base + rva + instructionSize + displacement;
}

std::uint64_t Key(std::uint32_t type, std::uint32_t id, int state) noexcept {
	return (static_cast<std::uint64_t>(type & 0xFF) << 56) | (static_cast<std::uint64_t>(state & 0xFFFF) << 32) | id;
}

std::uint64_t Caster(std::uint32_t type, std::uint32_t id) noexcept {
	return (static_cast<std::uint64_t>(type) << 32) | id;
}

// ---- who is who ----

std::uint32_t LocalPlayerId() noexcept {
	std::uint32_t index {}, id = NoId;
	if (!Read(LocalIndexAddress, index) || index >= 8) return NoId;
	(void)Read(PlayerIdsAddress + index * sizeof(std::uint32_t), id);
	return id;
}

Kind KindOf(std::uint32_t type, std::uint32_t id, std::uint32_t localId) noexcept {
	if (type == TypePlayer) return id == localId ? Self : Allies;
	if (type != TypeMonster) return Other;
	std::uintptr_t node {};
	if (!Read(PetListAddress, node)) return Monsters;
	for (int guard = 0; node && guard < 1024; ++guard) {
		std::uint32_t petId {};
		if (!Read(node + PetId, petId)) break;
		if (petId == id) {
			std::uint32_t owner = NoId, petType {};
			(void)Read(node + PetOwner, owner);
			(void)Read(node + PetType, petType);
			if (owner != localId) return Allies;
			return petType == PetTypeHireling ? Merc : Minions;
		}
		if (!Read(node + PetNext, node)) break;
	}
	return Monsters;
}

bool UnitIdentity(void* unit, std::uint32_t& type, std::uint32_t& id) noexcept {
	const auto address = reinterpret_cast<std::uintptr_t>(unit);
	return Read(address + UnitType, type) && Read(address + UnitId, id);
}

// ---- hooks ----

// D2Common's state toggle, run by the server and the client. On the server the
// unit's stat list for an aura state is owned by the caster: remember it.
void HookToggleState(void* unit, int state, int on) noexcept {
	OriginalToggleState(unit, state, on);
	if (!Active.load(std::memory_order_acquire) || !unit || state <= 0) return;
	if (!IsAuraState(UnitVersion(unit), state)) return;
	std::uint32_t type {}, id {};
	if (!UnitIdentity(unit, type, id) || (type != TypePlayer && type != TypeMonster)) return;
	const std::uint64_t key = Key(type, id, state);
	if (!on) {
		if (InStateOverlays) return; // the overlay code's off/on toggle, the state stays
		AcquireSRWLockExclusive(&CasterLock);
		Casters.erase(key);
		ReleaseSRWLockExclusive(&CasterLock);
		AcquireSRWLockExclusive(&TrackLock);
		Tracked.erase(key);
		ReleaseSRWLockExclusive(&TrackLock);
		return;
	}
	std::uint32_t ownerType = NoId, ownerId = NoId;
	if (unit == PendingTarget) {
		ownerType = static_cast<std::uint32_t>(PendingCaster >> 32);
		ownerId   = static_cast<std::uint32_t>(PendingCaster);
	} else {
		const auto list = reinterpret_cast<std::uintptr_t>(StatListForState(unit, state));
		if (!list || !Read(list + StatListOwnerType, ownerType) || !Read(list + StatListOwnerId, ownerId)) return;
	}
	if (ownerType == type && ownerId == id) return; // own state, or a client stat list
	AcquireSRWLockExclusive(&CasterLock);
	try {
		Casters[key] = Caster(ownerType, ownerId);
	} catch (...) {
	}
	ReleaseSRWLockExclusive(&CasterLock);
	if (RecordedCount.fetch_add(1, std::memory_order_relaxed) < MaxLoggedDecisions)
		Log(false, "server: aura state %d on type %u id %u cast by type %u id %u", state, type, id, ownerType, ownerId);
}

// The server's "put this aura's target state on a unit": remember who cast it
// for the toggle it makes.
int HookApplyAura(void* game, void* caster, void* target, int duration, int skill, int level) noexcept {
	std::uint32_t type = NoId, id = NoId;
	const bool known = caster && target && UnitIdentity(caster, type, id);
	void* const         previousTarget = PendingTarget;
	const std::uint64_t previousCaster = PendingCaster;
	if (known) PendingTarget = target, PendingCaster = Caster(type, id);
	const int result = OriginalApplyAura(game, caster, target, duration, skill, level);
	PendingTarget = previousTarget, PendingCaster = previousCaster;
	return result;
}

// states.txt rows of the shout states: shout, battleorders, battlecommand.
bool IsShoutState(int state) noexcept {
	return state == 26 || state == 32 || state == 51;
}

// The newer aura code's "apply this aura to one target": same, caster from its context.
std::uintptr_t HookAuraTarget(void* context, void* target, void* r8, void* r9) noexcept {
	std::uintptr_t caster {};
	std::uint32_t  type = NoId, id = NoId;
	const bool known = context && target && Read(reinterpret_cast<std::uintptr_t>(context) + AuraTargetCaster, caster) && caster &&
	                   UnitIdentity(reinterpret_cast<void*>(caster), type, id);
	void* const         previousTarget = PendingTarget;
	const std::uint64_t previousCaster = PendingCaster;
	if (known) PendingTarget = target, PendingCaster = Caster(type, id);
	const auto result = OriginalAuraTarget(context, target, r8, r9);
	PendingTarget = previousTarget, PendingCaster = previousCaster;
	return result;
}

// ---- missiles and overlays ----

template <std::size_t N>
bool Contains(const std::array<int, N>& values, int value) noexcept {
	for (const int candidate : values)
		if (candidate == value) return true;
	return false;
}

std::atomic<std::uint32_t> HiddenMissiles {}, HiddenOverlays {};
std::atomic<std::uint32_t> LoggedMissiles {};

void* HookCreateMissile(void* params, int flags) noexcept {
	if (Active.load(std::memory_order_acquire) && params) {
		const auto base = reinterpret_cast<std::uintptr_t>(params);
		int        id   = -1;
		std::uintptr_t owner {};
		if (Read(base + MissileParamsId, id) && Read(base + MissileParamsOwner, owner)) {
			const bool firestorm = Contains(FirestormMissiles, id), storm = !firestorm && Contains(ThunderStormMissiles, id);
			if (firestorm || storm) {
				std::uint32_t type = NoId, unitId = NoId;
				const Kind    kind = owner && UnitIdentity(reinterpret_cast<void*>(owner), type, unitId) ? KindOf(type, unitId, LocalPlayerId()) : Other;
				const bool    hide = Config.enabled.load(std::memory_order_relaxed) && Has(firestorm ? Config.hideFirestorm : Config.hideThunderStorm, kind);
				if (LoggedMissiles.fetch_add(1, std::memory_order_relaxed) < MaxLoggedDecisions)
					Log(false, "missile %d (%s) from %s: %s", id, firestorm ? "firestorm" : "thunder storm", kind == Other ? "other" : KindNames[kind], hide ? "hidden" : "shown");
				if (hide) {
					HiddenMissiles.fetch_add(1, std::memory_order_relaxed);
					return nullptr;
				}
			}
		}
	}
	return OriginalCreateMissile(params, flags);
}

// Whether an overlay id is one of the charge orbs, from its file name in the game's overlay table.
bool IsChargeOverlay(void* unit, int overlay) noexcept {
	if (overlay < 0 || overlay >= MaxOverlays) return false;
	if (const auto known = ChargeOverlay[overlay].load(std::memory_order_relaxed)) return known == 1;
	const auto record = reinterpret_cast<std::uintptr_t>(OverlayRecord(UnitVersion(unit), overlay));
	std::array<char, 0x60> bytes {};
	if (!record || !Read(record, bytes.data(), bytes.size())) return false;
	// The file name is the first printable run of the record, e.g. "expansion\MISSILE_A_A".
	std::string file;
	for (std::size_t i = 0; i < bytes.size(); ++i) {
		const char c = bytes[i];
		if (c >= 0x20 && c < 0x7F) file += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
		else if (file.size() >= 4) break;
		else file.clear();
	}
	if (const auto slash = file.find_last_of("\\/"); slash != std::string::npos) file.erase(0, slash + 1);
	bool charge = false;
	for (const char* name : ChargeOverlayFiles) charge |= file == name;
	ChargeOverlay[overlay].store(charge ? 1 : 2, std::memory_order_relaxed);
	if (charge) Log(false, "overlay %d (%s) is a charge orb", overlay, file.c_str());
	return charge;
}

// Reached from the overlay creation's record lookup (see OverlayLookupRva); null = create nothing.
const void* ChargeRecordLookup(std::uint8_t version, int overlay, void* unit) noexcept {
	std::uint32_t unitType = NoId, id = NoId;
	if (Active.load(std::memory_order_acquire) && unit && IsChargeOverlay(unit, overlay) && UnitIdentity(unit, unitType, id)) {
		const Kind kind = KindOf(unitType, id, LocalPlayerId());
		if (Config.enabled.load(std::memory_order_relaxed) && Has(Config.hideCharges, kind)) {
			HiddenOverlays.fetch_add(1, std::memory_order_relaxed);
			return nullptr;
		}
		// Remembered so these orbs can be removed if they become hidden.
		AcquireSRWLockExclusive(&TrackLock);
		try {
			ChargeUnits.insert(Caster(unitType, id));
		} catch (...) {
		}
		ReleaseSRWLockExclusive(&TrackLock);
	}
	return OverlayRecord(version, overlay);
}

// The rel32 call has to land in the game image: the stub goes in 16 bytes of int3 padding.
bool RedirectOverlayLookup() noexcept {
	if (!CodeMatches(OverlayUnitRva, OverlayUnitBytes) || !CodeMatches(OverlayLookupRva, OverlayLookupBytes)) return false;
	std::array<std::uint8_t, 15> stub { 0x49, 0x89, 0xF0, 0x48, 0xB8 }; // mov r8, rsi; mov rax, imm64; jmp rax
	const auto target = reinterpret_cast<std::uint64_t>(&ChargeRecordLookup);
	std::memcpy(stub.data() + 5, &target, sizeof(target));
	stub[13] = 0xFF, stub[14] = 0xE0;
	std::array<std::uint8_t, 16> padding {};
	padding.fill(0xCC);
	std::array<std::uint8_t, 0x10000> chunk {};
	int tries = 0;
	for (std::uint64_t start = 0x1000; start < 0x1400000 && tries < 16; start += chunk.size()) {
		if (!Read(Base + start, chunk.data(), chunk.size())) continue;
		for (std::size_t at = 0; at + padding.size() <= chunk.size() && tries < 16; at += 16) {
			if (std::memcmp(chunk.data() + at, padding.data(), padding.size()) != 0) continue;
			++tries;
			const std::uint64_t cave = start + at;
			if (!Context->PatchBytes(cave + 1, padding.data(), static_cast<std::uint32_t>(stub.size()), stub.data(), static_cast<std::uint32_t>(stub.size()))) continue; // taken
			return D2RL::PatchCallRel32(Context, OverlayLookupCallRva, OverlayLookupCallBytes.data(), 5, cave + 1);
		}
	}
	return false;
}

// Whether to leave out the look of an aura or shout state on a client unit. `log` = count it in the log.
bool ShouldHide(void* unit, std::uint32_t type, std::uint32_t id, int state, bool log) noexcept {
	if (!Config.enabled.load(std::memory_order_relaxed)) return false;
	const std::uint32_t localId = LocalPlayerId();
	const Kind on = KindOf(type, id, localId);
	if (on == Other) return false;
	Kind from = on; // no caster on record: the unit's own aura
	std::uint64_t caster = 0;
	bool known = false;
	AcquireSRWLockShared(&CasterLock);
	if (const auto found = Casters.find(Key(type, id, state)); found != Casters.end()) caster = found->second, known = true;
	ReleaseSRWLockShared(&CasterLock);
	if (known) from = KindOf(static_cast<std::uint32_t>(caster >> 32), static_cast<std::uint32_t>(caster), localId);
	const bool shout = IsShoutState(state);
	const bool hide  = Has((shout ? Config.hideShouts : Config.hide)[on], from);
	if (log && LoggedDecisions.fetch_add(1, std::memory_order_relaxed) < MaxLoggedDecisions)
		Log(false, "%s state %d on %s (type %u id %u), cast by %s%s: %s", shout ? "shout" : "aura", state, KindNames[on], type, id,
		    from == Other ? "other" : KindNames[from], known ? "" : " (own)", hide ? "hidden" : "shown");
	return hide;
}

void Track(std::uint64_t key, std::uint8_t how) noexcept {
	AcquireSRWLockExclusive(&TrackLock);
	try {
		Tracked[key] = how;
	} catch (...) {
	}
	ReleaseSRWLockExclusive(&TrackLock);
}

// Units can leave without their states ending (killed, out of sight): every
// 512 tracked states past 2048, forget the units the client no longer has.
void PruneTracked() noexcept {
	static std::uint32_t calls = 0;
	if (++calls % 512 != 0) return;
	std::vector<std::uint64_t> keys, units;
	AcquireSRWLockShared(&TrackLock);
	const bool many = Tracked.size() > 2048 || ChargeUnits.size() > 512;
	try {
		if (many) {
			for (const auto& entry : Tracked) keys.push_back(entry.first);
			units.assign(ChargeUnits.begin(), ChargeUnits.end());
		}
	} catch (...) {
	}
	ReleaseSRWLockShared(&TrackLock);
	if (!many) return;
	std::vector<std::uint64_t> goneKeys, goneUnits;
	try {
		for (const std::uint64_t key : keys)
			if (!FindClientUnit(static_cast<std::uint32_t>(key), static_cast<int>(key >> 56))) goneKeys.push_back(key);
		for (const std::uint64_t unit : units)
			if (!FindClientUnit(static_cast<std::uint32_t>(unit), static_cast<int>(unit >> 32))) goneUnits.push_back(unit);
	} catch (...) {
	}
	AcquireSRWLockExclusive(&TrackLock);
	for (const std::uint64_t key : goneKeys) Tracked.erase(key);
	for (const std::uint64_t unit : goneUnits) ChargeUnits.erase(unit);
	ReleaseSRWLockExclusive(&TrackLock);
}

void DrawStateOverlays(void* unit, int state) noexcept {
	InStateOverlays = true;
	OriginalStateOverlays(unit, state);
	InStateOverlays = false;
}

// The client's "add this state's overlays to the unit".
void HookStateOverlays(void* unit, int state) noexcept {
	std::uint32_t type {}, id {};
	if (!Active.load(std::memory_order_acquire) || !unit || state <= 0 || !IsAuraState(UnitVersion(unit), state) || !UnitIdentity(unit, type, id)) {
		DrawStateOverlays(unit, state);
		return;
	}
	const bool hide = ShouldHide(unit, type, id, state, true);
	Track(Key(type, id, state), hide ? Skipped : Drawn);
	PruneTracked();
	if (hide) {
		HiddenCount.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	ShownCount.fetch_add(1, std::memory_order_relaxed);
	DrawStateOverlays(unit, state);
}

// QuestUpdateFX's terror zone tint is skipped while it is hidden; other tints pass.
void HookScreenTint(int kind, int fadeIn, int hold, int fadeOut) noexcept {
	if (reinterpret_cast<std::uintptr_t>(__builtin_return_address(0)) == Base + TerrorTintReturnRva && Config.enabled.load(std::memory_order_relaxed) &&
	    Config.hideTerrorTint.load(std::memory_order_relaxed))
		return;
	OriginalScreenTint(kind, fadeIn, hold, fadeOut);
}

// ---- live changes (game's main thread) ----

bool TerrorTintHidden() noexcept {
	return Config.enabled.load(std::memory_order_relaxed) && Config.hideTerrorTint.load(std::memory_order_relaxed);
}

// Adds or removes the overlays of every tracked aura and shout whose decision changed.
void RefreshAuras() noexcept {
	std::vector<std::pair<std::uint64_t, std::uint8_t>> states;
	AcquireSRWLockShared(&TrackLock);
	try {
		states.assign(Tracked.begin(), Tracked.end());
	} catch (...) {
	}
	ReleaseSRWLockShared(&TrackLock);
	int added = 0, removed = 0, gone = 0;
	for (const auto& [key, how] : states) {
		const auto type  = static_cast<std::uint32_t>(key >> 56);
		const auto state = static_cast<int>(key >> 32 & 0xFFFF);
		const auto id    = static_cast<std::uint32_t>(key);
		void* const unit = FindClientUnit(id, static_cast<int>(type));
		if (!unit || !HasState(unit, state)) {
			AcquireSRWLockExclusive(&TrackLock);
			Tracked.erase(key);
			ReleaseSRWLockExclusive(&TrackLock);
			++gone;
			continue;
		}
		const bool hide = ShouldHide(unit, type, id, state, false);
		if (hide && how == Drawn) {
			// What the client does when the state ends.
			if (const auto record = reinterpret_cast<std::uintptr_t>(StateRecord(UnitVersion(unit), state))) {
				std::int16_t first = -1, second = -1;
				if (Read(record + StateOverlay1, first) && first >= 0) RemoveStateOverlay(unit, first);
				if (Read(record + StateOverlay2, second) && second >= 0) RemoveStateOverlay(unit, second);
			}
			Track(key, Skipped);
			++removed;
		} else if (!hide && how == Skipped) {
			DrawStateOverlays(unit, state);
			Track(key, Drawn);
			++added;
		}
	}
	if (added || removed) Log(false, "settings changed: aura effects removed %d, added %d (%d tracked, %d gone)", removed, added, static_cast<int>(states.size()), gone);
}

// Removes the charge orbs drawn on units whose orbs are now hidden. Orbs shown
// again appear with the next charge.
void RefreshCharges() noexcept {
	if (!Config.enabled.load(std::memory_order_relaxed) || !Config.hideCharges.load(std::memory_order_relaxed)) return;
	std::vector<std::uint64_t> units;
	AcquireSRWLockShared(&TrackLock);
	try {
		units.assign(ChargeUnits.begin(), ChargeUnits.end());
	} catch (...) {
	}
	ReleaseSRWLockShared(&TrackLock);
	const std::uint32_t localId = LocalPlayerId();
	for (const std::uint64_t key : units) {
		const auto type = static_cast<std::uint32_t>(key >> 32), id = static_cast<std::uint32_t>(key);
		void* const unit = FindClientUnit(id, static_cast<int>(type));
		if (unit && !Has(Config.hideCharges, KindOf(type, id, localId))) continue;
		if (unit)
			for (int overlay = 0; overlay < MaxOverlays; ++overlay)
				if (ChargeOverlay[overlay].load(std::memory_order_relaxed) == 1)
					for (int node = 0; node < 3; ++node) RemoveOverlay(unit, 0, overlay);
		AcquireSRWLockExclusive(&TrackLock);
		ChargeUnits.erase(key);
		ReleaseSRWLockExclusive(&TrackLock);
	}
}

// Clears the terror zone tint or puts it back, when you are in a terror zone.
void RefreshTerrorTint(bool wasHidden) noexcept {
	const bool hidden = TerrorTintHidden();
	std::uint32_t inZone = 0;
	if (hidden == wasHidden || !TerrorFlagAddress || !Read(TerrorFlagAddress, inZone) || inZone != 1) return;
	if (hidden) TintReset();
	else OriginalScreenTint(3, 1000, 2000, 1000);
	Log(false, "terror zone tint %s", hidden ? "cleared" : "back on");
}

// ---- setup ----

bool Locate() noexcept {
	if (!CodeMatches(StatListForStateRva, StatListForStateBytes)) return Problem = "stat list lookup not found", false;
	if (!CodeMatches(IsAuraStateRva, IsAuraStateBytes)) return Problem = "aura state check not found", false;
	if (!CodeMatches(UnitVersionRva, UnitVersionBytes)) return Problem = "unit version getter not found", false;
	LocalIndexAddress = RipTarget(LocalIndexSiteRva, LocalIndexSite, 2, 6);
	PlayerIdsAddress  = RipTarget(PlayerIdsSiteRva, PlayerIdsSite, 3, 7);
	PetListAddress    = RipTarget(PetListSiteRva, PetListSite, 3, 7);
	if (!LocalIndexAddress || !PlayerIdsAddress || !PetListAddress) return Problem = "player or pet list not found", false;
	StatListForState = reinterpret_cast<StatListForStateFn>(Base + StatListForStateRva);
	IsAuraState      = reinterpret_cast<IsAuraStateFn>(Base + IsAuraStateRva);
	UnitVersion      = reinterpret_cast<UnitVersionFn>(Base + UnitVersionRva);
	if (!CodeMatches(OverlayRecordRva, OverlayRecordBytes)) return Problem = "overlay table lookup not found", false;
	OverlayRecord = reinterpret_cast<OverlayRecordFn>(Base + OverlayRecordRva);
	if (!CodeMatches(FindClientUnitRva, FindClientUnitBytes) || !CodeMatches(HasStateRva, HasStateBytes) || !CodeMatches(StateRecordRva, StateRecordBytes) ||
	    !CodeMatches(RemoveStateOverlayRva, RemoveStateOverlayBytes) || !CodeMatches(RemoveOverlayRva, RemoveOverlayBytes))
		return Problem = "overlay add/remove code not found", false;
	FindClientUnit     = reinterpret_cast<FindClientUnitFn>(Base + FindClientUnitRva);
	HasState           = reinterpret_cast<HasStateFn>(Base + HasStateRva);
	StateRecord        = reinterpret_cast<StateRecordFn>(Base + StateRecordRva);
	RemoveStateOverlay = reinterpret_cast<RemoveStateOverlayFn>(Base + RemoveStateOverlayRva);
	RemoveOverlay      = reinterpret_cast<RemoveOverlayFn>(Base + RemoveOverlayRva);
	return true;
}

bool InstallHooks() noexcept {
	if (!Context->InstallInlineHook(ToggleStateRva, ToggleStateBytes.data(), static_cast<std::uint32_t>(ToggleStateBytes.size()), HookToggleState, &OriginalToggleState) || !OriginalToggleState)
		return Problem = "state toggle hook refused (code differs or another plugin hooks it)", false;
	if (!Context->InstallInlineHook(ApplyAuraRva, ApplyAuraBytes.data(), static_cast<std::uint32_t>(ApplyAuraBytes.size()), HookApplyAura, &OriginalApplyAura) || !OriginalApplyAura)
		return Problem = "aura apply hook refused (code differs or another plugin hooks it)", false;
	if (!Context->InstallInlineHook(AuraTargetRva, AuraTargetBytes.data(), static_cast<std::uint32_t>(AuraTargetBytes.size()), HookAuraTarget, &OriginalAuraTarget) || !OriginalAuraTarget)
		return Problem = "aura target hook refused (code differs or another plugin hooks it)", false;
	if (!Context->InstallInlineHook(StateOverlaysRva, StateOverlaysBytes.data(), static_cast<std::uint32_t>(StateOverlaysBytes.size()), HookStateOverlays, &OriginalStateOverlays) || !OriginalStateOverlays)
		return Problem = "state overlay hook refused (code differs or another plugin hooks it)", false;
	if (!Context->InstallInlineHook(CreateMissileRva, CreateMissileBytes.data(), static_cast<std::uint32_t>(CreateMissileBytes.size()), HookCreateMissile, &OriginalCreateMissile) || !OriginalCreateMissile)
		return Problem = "missile hook refused (code differs or another plugin hooks it)", false;
	if (!RedirectOverlayLookup())
		return Problem = "overlay record lookup could not be redirected (code differs or another plugin patches it)", false;
	// The terror zone tint is optional: without it everything else still works.
	if (CodeMatches(TerrorTintCallRva, TerrorTintCallBytes) && CodeMatches(TintResetRva, TintResetBytes) &&
	    Context->InstallInlineHook(ScreenTintRva, ScreenTintBytes.data(), static_cast<std::uint32_t>(ScreenTintBytes.size()), HookScreenTint, &OriginalScreenTint) && OriginalScreenTint) {
		TintReset         = reinterpret_cast<TintResetFn>(Base + TintResetRva);
		TerrorFlagAddress = RipTarget(TerrorFlagSiteRva, TerrorFlagSite, 2, 10);
	} else Log(true, "terror zone tint: code differs or another plugin hooks it, the tint stays");
	return true;
}

// ---- version check ----

std::string ReadGameVersion() noexcept {
	if (const char* build = D2RL::GetBuildVersion(Context); build && *build) return build;
	wchar_t path[MAX_PATH] {};
	if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return {};
	DWORD handle = 0;
	const DWORD size = GetFileVersionInfoSizeW(path, &handle);
	if (!size) return {};
	std::vector<std::uint8_t> data(size);
	VS_FIXEDFILEINFO* info {};
	UINT length = 0;
	if (!GetFileVersionInfoW(path, 0, size, data.data()) || !VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &length) || !info) return {};
	char version[32] {};
	std::snprintf(version, sizeof(version), "%u.%u", static_cast<unsigned>(HIWORD(info->dwFileVersionMS)), static_cast<unsigned>(LOWORD(info->dwFileVersionMS)));
	return version;
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

// ["a", "b"] -> {"a", "b"}; the array must be on one line.
std::vector<std::string> ParseStrings(std::string_view value) {
	std::vector<std::string> strings;
	std::string parsed;
	for (std::size_t at = 1; at < value.size() && value[at] != ']';) {
		if (value[at] == '"') {
			if (!ParseString(value, at, parsed)) break;
			strings.push_back(parsed);
		} else if (value[at] == '#') break;
		else ++at;
	}
	return strings;
}

int KindIndex(std::string_view name) noexcept {
	for (int kind = 0; kind < KindCount; ++kind)
		if (name == KindNames[kind]) return kind;
	if (name == "minion" || name == "summons") return Minions;
	if (name == "ally") return Allies;
	if (name == "monster") return Monsters;
	if (name == "mercenary") return Merc;
	return -1;
}

std::uint8_t Bit(int kind) noexcept {
	return static_cast<std::uint8_t>(1u << kind);
}

void DefaultMatrix() noexcept {
	// Hide what you, your merc and other players cast, on everyone but other
	// players, except a unit's own auras on you and your merc.
	const std::uint8_t casters = Bit(Self) | Bit(Merc) | Bit(Allies);
	Config.hide[Self]     = casters & ~Bit(Self);
	Config.hide[Merc]     = casters & ~Bit(Merc);
	Config.hide[Minions]  = casters;
	Config.hide[Allies]   = 0;
	Config.hide[Monsters] = casters;
}

// ["self", "merc"] -> bits; false + reason when a name is unknown.
bool ParseKinds(const std::string& value, std::uint8_t& kinds, std::string& error) {
	if (value.empty() || value[0] != '[') return error = "a list of kinds, like [\"self\", \"merc\"]", false;
	kinds = 0;
	for (const auto& name : ParseStrings(value)) {
		const int kind = KindIndex(name);
		if (kind < 0) return error = "unknown kind \"" + name + "\" (self, merc, minions, allies, monsters)", false;
		kinds |= Bit(kind);
	}
	return true;
}

// The kinds setting a key names: on_<kind>, shouts_on_<kind>, hide_firestorm, hide_thunder_storm, hide_charges.
Kinds* KindsSetting(std::string_view key) noexcept {
	if (key == "hide_firestorm") return &Config.hideFirestorm;
	if (key == "hide_thunder_storm") return &Config.hideThunderStorm;
	if (key == "hide_charges") return &Config.hideCharges;
	const bool shouts = key.rfind("shouts_on_", 0) == 0;
	if (!shouts && key.rfind("on_", 0) != 0) return nullptr;
	const int on = KindIndex(key.substr(shouts ? 10 : 3));
	if (on < 0) return nullptr;
	return &(shouts ? Config.hideShouts : Config.hide)[on];
}

// I Hate TOMLs shows each kinds setting as one switch per kind: "<setting>.<kind>".
bool SplitSwitch(std::string_view key, Kinds*& setting, int& kind) noexcept {
	const auto dot = key.rfind('.');
	if (dot == std::string_view::npos) return false;
	setting = KindsSetting(key.substr(0, dot));
	kind    = KindIndex(key.substr(dot + 1));
	return setting && kind >= 0;
}

// Applies one setting from its TOML text (strings unquoted); false + reason when refused.
bool Apply(const std::string& key, const std::string& value, std::string& error) {
	const auto flag = [&](std::atomic<bool>& target) {
		if (value != "true" && value != "false") return error = "true or false", false;
		target = value == "true";
		return true;
	};
	if (key == "enabled") return flag(Config.enabled);
	if (key == "hide_terror_zone_tint") return flag(Config.hideTerrorTint);
	if (Kinds* setting = KindsSetting(key)) {
		std::uint8_t kinds = 0;
		if (!ParseKinds(value, kinds, error)) return false;
		setting->store(kinds);
		return true;
	}
	Kinds* setting {};
	int    kind {};
	if (SplitSwitch(key, setting, kind)) {
		if (value != "true" && value != "false") return error = "true or false", false;
		if (value == "true") setting->fetch_or(Bit(kind));
		else setting->fetch_and(static_cast<std::uint8_t>(~Bit(kind)));
		return true;
	}
	if (key == "versions") return true; // removed in 1.0.2-dev-1 (code bytes are checked instead), ignored in older config files
	return error = "unknown setting", false;
}

std::string KindsList(const Kinds& kinds) {
	std::string text;
	for (int kind = 0; kind < KindCount; ++kind)
		if (Has(kinds, kind)) text += std::string(text.empty() ? "" : ", ") + "\"" + KindNames[kind] + "\"";
	return "[" + text + "]";
}

std::string ValueOf(const std::string& key) {
	const auto flag = [](bool on) { return std::string(on ? "true" : "false"); };
	if (key == "enabled") return flag(Config.enabled);
	if (key == "hide_terror_zone_tint") return flag(Config.hideTerrorTint);
	if (const Kinds* setting = KindsSetting(key)) return KindsList(*setting);
	Kinds* setting {};
	int    kind {};
	if (SplitSwitch(key, setting, kind)) return flag(Has(*setting, kind));
	return {};
}

std::string Render() {
	const auto row = [](const char* prefix) {
		std::string text;
		for (int kind = 0; kind < KindCount; ++kind) {
			const std::string key = std::string(prefix) + KindNames[kind];
			text += key + std::string(std::strlen(prefix) + std::strlen("monsters") - key.size() + 1, ' ') + "= " + ValueOf(key) + "\n";
		}
		return text;
	};
	return R"(# Visual Clutter
#
# Hides visual effects without changing what they do. D2RLoader writes this
# file to d2rloader/config/visual-clutter.toml on first launch. Every setting
# can also be changed in game with I Hate TOMLs, and applies at once.

[visual-clutter]
enabled = )" + ValueOf("enabled") + R"(

# ---- aura effects ----
#
# Only the look of an aura is hidden (the glow, the rings at the feet, the
# small icons over affected units); the aura still works.
#
# Each "on_..." line is one kind of unit the effect is drawn on, and lists who
# cast the auras to hide there. Both sides use the same five kinds:
#   "self"      you
#   "merc"      your mercenary
#   "minions"   your summons (golems, spirits, Valkyrie, ...)
#   "allies"    other players, their mercenaries and their summons
#   "monsters"  monsters (aura-enchanted bosses, monster auras)
# An empty list [] shows every aura on that kind of unit.
#
# Who cast an aura is read from the game you play in, so it needs a single
# player game or a game you host. Joined to someone else's game, every aura
# counts as cast by the unit it is on.
)" + row("on_") + R"(
# ---- shout effects ----
#
# The same, for the barbarian's Shout, Battle Orders and Battle Command, which
# the game draws like auras. These lines do not affect the aura lines above.
# By default every shout is shown.
)" + row("shouts_on_") + R"(
# ---- other effects ----
#
# Lists of kinds as above. Only the look is removed; the damage still happens.
#
# Hellfire Torch's Firestorm (also the druid skill Firestorm), by who cast it.
hide_firestorm = )" + ValueOf("hide_firestorm") + R"(
# Thunder Storm's lightning bolts and their flash, by who cast it.
hide_thunder_storm = )" + ValueOf("hide_thunder_storm") + R"(
# Martial arts charge orbs (Tiger Strike, Fists of Fire, ... kept up by
# Mosaic), by who carries them.
hide_charges = )" + ValueOf("hide_charges") + R"(
# The red screen tint inside terror zones. The zone itself, its sound and its
# name on screen are unchanged.
hide_terror_zone_tint = )" + ValueOf("hide_terror_zone_tint") + "\n";
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
		const std::string value = Trim(std::string_view(line).substr(equals + 1));
		std::string       error;
		if (!Apply(key, value, error)) Log(true, "config: %s = %s ignored (%s)", key.c_str(), value.c_str(), error.c_str());
	}
}

void LoadConfig() {
	DefaultMatrix();
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
	ApplyText(std::string_view(buffer.data(), std::strlen(buffer.data())));
}

std::string KindsText(const Kinds& kinds) {
	std::string text;
	for (int kind = 0; kind < KindCount; ++kind)
		if (Has(kinds, kind)) text += std::string(text.empty() ? "" : ",") + KindNames[kind];
	return text.empty() ? "-" : text;
}

std::string EffectsText() {
	return "firestorm from " + KindsText(Config.hideFirestorm) + "; thunder storm from " + KindsText(Config.hideThunderStorm) + "; charges on " +
	       KindsText(Config.hideCharges) + "; terror zone tint " + (Config.hideTerrorTint ? "off" : "on");
}

std::string MatrixText(const std::array<Kinds, KindCount>& hide) {
	std::string text;
	for (int on = 0; on < KindCount; ++on) text += std::string(on ? "; " : "") + "on " + KindNames[on] + ": " + KindsText(hide[on]);
	return text;
}

// ---- I Hate TOMLs ----

// Who the kinds are, as the menu names them (column labels wrap to two short lines).
constexpr std::array<const char*, KindCount> RowLabels { "You", "Your merc", "Your summons", "Other players", "Monsters" };
constexpr std::array<const char*, KindCount> FromLabels { "From you", "From merc", "From summons", "From players", "From monsters" };
constexpr std::array<const char*, KindCount> OnLabels { "On you", "On merc", "On summons", "On players", "On monsters" };

std::string Schema;

std::string List(const std::vector<std::string>& items) {
	std::string text;
	for (const auto& item : items) text += std::string(text.empty() ? "" : ", ") + "\"" + item + "\"";
	return "[" + text + "]";
}

std::vector<std::string> Strings(const std::array<const char*, KindCount>& items, const std::string& prefix = {}) {
	std::vector<std::string> strings;
	for (const char* item : items) strings.push_back(prefix + item);
	return strings;
}

// A grid of check boxes: each box is the switch "<row>.<kind>" served by MenuGet/MenuSet.
std::string Table(const std::string& label, const std::string& note, const std::vector<std::string>& rows, const std::vector<std::string>& rowLabels,
                  const std::array<const char*, KindCount>& columnLabels) {
	return "[[setting]]\ntype = \"table\"\nlabel = \"" + label + "\"\n" + (note.empty() ? "" : "note = \"" + note + "\"\n") + "rows = " + List(rows) +
	       "\nrow_labels = " + List(rowLabels) + "\ncolumns = " + List(Strings(KindNames)) + "\ncolumn_labels = " + List(Strings(columnLabels)) + "\n";
}

void BuildSchema() {
	Schema = R"(title = "Visual Clutter"
[[setting]]
key = "enabled"
type = "bool"
label = "Hide the chosen effects"
note = "Only the look goes away: auras, shouts and skills still work."
)";
	Schema += Table("Auras on", "Tick whose auras to hide on each kind of unit. Who cast an aura is known in single player and in games you host.",
	                Strings(KindNames, "on_"), Strings(RowLabels), FromLabels);
	Schema += Table("Shouts on", "Shout, Battle Orders and Battle Command, apart from the auras above.", Strings(KindNames, "shouts_on_"), Strings(RowLabels), FromLabels);
	Schema += Table("Skills", "Hellfire Torch's Firestorm (and the druid skill), Thunder Storm's bolts and flash, by who cast them.",
	                { "hide_firestorm", "hide_thunder_storm" }, { "Firestorm", "Thunder Storm" }, FromLabels);
	Schema += Table("Charge orbs", "Martial arts charges (Mosaic), by who carries them. Orbs shown again come back with the next charge.", { "hide_charges" },
	                { "Charge orbs" }, OnLabels);
	Schema += R"([[setting]]
type = "header"
label = "Terror zones"
[[setting]]
key = "hide_terror_zone_tint"
type = "bool"
label = "Hide the red tint"
note = "The zone, its sound and its name on screen stay."
)";
}

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
	const bool  tintWasHidden = TerrorTintHidden();
	std::string reason;
	try {
		if (!Apply(key, Trim(value), reason)) {
			std::snprintf(error, size, "%s", reason.c_str());
			return false;
		}
		SaveConfig();
	} catch (...) {
		std::snprintf(error, size, "%s", "out of memory");
		return false;
	}
	if (Active.load(std::memory_order_acquire)) {
		RefreshAuras();
		RefreshCharges();
		RefreshTerrorTint(tintWasHidden);
	}
	return true;
}

// ---- console ----

auto StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
	if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
	char line[400] {};
	if (Active.load()) {
		std::size_t casters = 0;
		AcquireSRWLockShared(&CasterLock);
		casters = Casters.size();
		ReleaseSRWLockShared(&CasterLock);
		std::snprintf(line, sizeof(line), "visual-clutter %s: %s on %s; aura effects hidden %u, shown %u; casters known %zu (recorded %u)", PluginVersion, Config.enabled ? "active" : "turned off", GameVersion.c_str(),
		              HiddenCount.load(), ShownCount.load(), casters, RecordedCount.load());
	} else std::snprintf(line, sizeof(line), "visual-clutter %s: inactive on %s (%s)", PluginVersion, GameVersion.c_str(), Problem.empty() ? "disabled" : Problem.c_str());
	command->plugin->WriteConsoleMessage(line);
	Log(false, "%s", line);
	const std::string matrix = "hide auras " + MatrixText(Config.hide);
	command->plugin->WriteConsoleMessage(matrix.c_str());
	const std::string shouts = "hide shouts " + MatrixText(Config.hideShouts);
	command->plugin->WriteConsoleMessage(shouts.c_str());
	char counts[160] {};
	std::snprintf(counts, sizeof(counts), "missiles hidden %u, charge orbs hidden %u", HiddenMissiles.load(), HiddenOverlays.load());
	const std::string effects = "hide " + EffectsText() + "; " + counts;
	command->plugin->WriteConsoleMessage(effects.c_str());
	return D2RL::ConsoleCommandResult::Handled;
}

}

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (!D2RL::HasContext(context) || !context->GetApi()) return false;
	Context = context;
	Base    = context->exeBase;
	(void)context->RegisterConsoleCommand("visual-clutter", StatusCommand, "Show Visual Clutter status.");

	LoadConfig();
	try {
		BuildSchema();
		(void)Menu.Start(context, Schema.c_str(), MenuGet, MenuSet, nullptr);
	} catch (...) {
	}
	GameVersion = ReadGameVersion();
	Log(false, "Visual Clutter %s: game %s, hide auras %s", PluginVersion, GameVersion.c_str(), MatrixText(Config.hide).c_str());
	Log(false, "hide shouts %s", MatrixText(Config.hideShouts).c_str());
	Log(false, "hide %s", EffectsText().c_str());
	// Hooked even when turned off: it can be turned on while playing.
	if (!Base || !Locate() || !InstallHooks()) {
		Log(true, "inactive: %s", Problem.c_str());
		return true;
	}
	Active.store(true, std::memory_order_release);
	Log(false, "%s: state overlays hooked at D2R+0x%llX, state toggle at D2R+0x%llX", Config.enabled ? "active" : "loaded but turned off", static_cast<unsigned long long>(StateOverlaysRva), static_cast<unsigned long long>(ToggleStateRva));
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	Active.store(false, std::memory_order_release);
	Menu.Stop();
}
