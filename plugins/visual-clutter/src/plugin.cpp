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
//   - Martial arts charge orbs (kept up by Mosaic): the legacy overlay creation
//     (0x150280) skips the charge overlays on the chosen units.
//   - Terror zone tint: QuestUpdateFX starts a screen tint when you enter a
//     terror zone; the call (0x170499) is NOPed.
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
#include <vector>

namespace {

constexpr const char* PluginId      = "visual-clutter";
constexpr const char* PluginVersion = "1.0.0";

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
// Legacy overlay creation: void (Unit*, int overlay, int type, ...7 args); no caller uses a result.
constexpr std::uint64_t CreateOverlayRva = 0x150280;
constexpr std::array<std::uint8_t, 24> CreateOverlayBytes { 0x40, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x57, 0x48, 0x81, 0xEC, 0x30, 0x01, 0x00, 0x00, 0x45, 0x8B, 0xE1, 0x45, 0x8B, 0xF8, 0x8B, 0xFA, 0x48 };
// QuestUpdateFX's "you entered a terror zone" screen tint: call ScreenTint(3, 2000, 1000, 1000).
// The tint lasts until the player leaves the zone; NOPing the call keeps the zone's sound.
constexpr std::uint64_t TerrorTintCallRva = 0x170499;
constexpr std::array<std::uint8_t, 5> TerrorTintCallBytes { 0xE8, 0xA2, 0x39, 0xF8, 0xFF };

// Called functions, checked the same way.
constexpr std::uint64_t OverlayRecordRva = 0x1501A0; // OverlayRecord* (uint8 version, int overlay), 0xE0-byte records
constexpr std::array<std::uint8_t, 24> OverlayRecordBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x63, 0xF2, 0xE8, 0xD9, 0x08, 0x1B, 0x00, 0x48 };
constexpr std::uint64_t StatListForStateRva = 0x2F5940; // StatList* (Unit*, int state)
constexpr std::array<std::uint8_t, 15> StatListForStateBytes { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0xDA, 0x48, 0x8B, 0xF9 };
constexpr std::uint64_t IsAuraStateRva = 0x335950; // int (uint8 version, int state)
constexpr std::array<std::uint8_t, 18> IsAuraStateBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x63, 0xDA };
constexpr std::uint64_t UnitVersionRva = 0x34A0E0; // uint8 (Unit*)
constexpr std::array<std::uint8_t, 9> UnitVersionBytes { 0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1A };

// Globals, read from the RIP-relative operand of known instructions.
constexpr std::uint64_t LocalIndexSiteRva = 0x8B2D0; // mov eax, [rip+x]; ret  -> local player index
constexpr std::array<int, 7> LocalIndexSite { 0x8B, 0x05, -1, -1, -1, -1, 0xC3 };
constexpr std::uint64_t PlayerIdsSiteRva = 0x9A4BE; // lea rdi, [rip+x]  -> uint32 ids[8], -1 = none
constexpr std::array<int, 7> PlayerIdsSite { 0x48, 0x8D, 0x3D, -1, -1, -1, -1 };
constexpr std::uint64_t PetListSiteRva = 0x13B270; // mov rax, [rip+x]; test rax, rax  -> first pet node
constexpr std::array<int, 10> PetListSite { 0x48, 0x8B, 0x05, -1, -1, -1, -1, 0x48, 0x85, 0xC0 };

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
using CreateOverlayFn    = void (*)(void* unit, int overlay, int type, int a4, int a5, int a6, int a7) noexcept;
using OverlayRecordFn    = const void* (*)(std::uint8_t version, int overlay) noexcept;
using AuraTargetFn       = std::uintptr_t (*)(void* context, void* target, void* r8, void* r9) noexcept;
using ApplyAuraFn        = int (*)(void* game, void* caster, void* target, int duration, int skill, int level) noexcept;
using StatListForStateFn = void* (*)(void* unit, int state) noexcept;
using IsAuraStateFn      = int (*)(std::uint8_t version, int state) noexcept;
using UnitVersionFn      = std::uint8_t (*)(void* unit) noexcept;

// ---- state ----

const D2RL::PluginContext* Context {};
std::uintptr_t             Base {};
std::atomic_bool           Active {};
std::string                GameVersion;
std::string                Problem; // why the plugin is inactive

StateOverlaysFn    OriginalStateOverlays {};
ToggleStateFn      OriginalToggleState {};
CreateMissileFn    OriginalCreateMissile {};
CreateOverlayFn    OriginalCreateOverlay {};
OverlayRecordFn    OverlayRecord {};
ApplyAuraFn        OriginalApplyAura {};
AuraTargetFn       OriginalAuraTarget {};
StatListForStateFn StatListForState {};
IsAuraStateFn      IsAuraState {};
UnitVersionFn      UnitVersion {};
std::uintptr_t     LocalIndexAddress {};
std::uintptr_t     PlayerIdsAddress {};
std::uintptr_t     PetListAddress {};

// Who a unit is, seen from the local player.
enum Kind : int { Self, Merc, Minions, Allies, Monsters, KindCount, Other = KindCount };
constexpr std::array<const char*, KindCount> KindNames { "self", "merc", "minions", "allies", "monsters" };

struct Settings {
	bool                     enabled = true;
	// hide[on][from]: hide auras drawn on a unit of kind `on` that a unit of kind `from` cast.
	std::array<std::array<bool, KindCount>, KindCount> hide {};
	// The same for the barbarian's shouts (Shout, Battle Orders, Battle Command),
	// which the game also draws as auras.
	std::array<std::array<bool, KindCount>, KindCount> hideShouts {};
	// Kinds of unit whose effects are hidden: missiles by owner, charge orbs by the unit wearing them.
	std::array<bool, KindCount> hideFirestorm {}, hideThunderStorm {}, hideCharges {};
	bool                     hideTerrorTint = false;
	std::vector<std::string> versions { "3.3" };
} Config;

// missiles.txt rows (RotW, same in 93787 and 94995).
constexpr std::array<int, 2> FirestormMissiles { 457, 458 };              // firestorm, firestormmaker
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

bool Read(std::uintptr_t address, void* output, std::size_t size) noexcept {
	SIZE_T copied = 0;
	return address != 0 && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), output, size, &copied) && copied == size;
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
		AcquireSRWLockExclusive(&CasterLock);
		Casters.erase(key);
		ReleaseSRWLockExclusive(&CasterLock);
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
				const bool    hide = kind != Other && (firestorm ? Config.hideFirestorm : Config.hideThunderStorm)[kind];
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

void HookCreateOverlay(void* unit, int overlay, int type, int a4, int a5, int a6, int a7) noexcept {
	if (Active.load(std::memory_order_acquire) && unit) {
		bool any = false;
		for (const bool kind : Config.hideCharges) any |= kind;
		std::uint32_t unitType = NoId, id = NoId;
		if (any && IsChargeOverlay(unit, overlay) && UnitIdentity(unit, unitType, id)) {
			const Kind kind = KindOf(unitType, id, LocalPlayerId());
			if (kind != Other && Config.hideCharges[kind]) {
				HiddenOverlays.fetch_add(1, std::memory_order_relaxed);
				return;
			}
		}
	}
	OriginalCreateOverlay(unit, overlay, type, a4, a5, a6, a7);
}

bool ShouldHide(void* unit, int state) noexcept {
	if (state <= 0 || !IsAuraState(UnitVersion(unit), state)) return false;
	std::uint32_t type {}, id {};
	if (!UnitIdentity(unit, type, id)) return false;
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
	const bool hide  = from != Other && (shout ? Config.hideShouts : Config.hide)[on][from];
	if (LoggedDecisions.fetch_add(1, std::memory_order_relaxed) < MaxLoggedDecisions)
		Log(false, "%s state %d on %s (type %u id %u), cast by %s%s: %s", shout ? "shout" : "aura", state, KindNames[on], type, id,
		    from == Other ? "other" : KindNames[from], known ? "" : " (own)", hide ? "hidden" : "shown");
	return hide;
}

// The client's "add this state's overlays to the unit".
void HookStateOverlays(void* unit, int state) noexcept {
	if (Active.load(std::memory_order_acquire) && unit && ShouldHide(unit, state)) {
		HiddenCount.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	if (Active.load(std::memory_order_relaxed)) ShownCount.fetch_add(1, std::memory_order_relaxed);
	OriginalStateOverlays(unit, state);
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
	if (!Context->InstallInlineHook(CreateOverlayRva, CreateOverlayBytes.data(), static_cast<std::uint32_t>(CreateOverlayBytes.size()), HookCreateOverlay, &OriginalCreateOverlay) || !OriginalCreateOverlay)
		return Problem = "overlay hook refused (code differs or another plugin hooks it)", false;
	if (Config.hideTerrorTint) {
		if (Context->PatchFill(TerrorTintCallRva, TerrorTintCallBytes.data(), static_cast<std::uint32_t>(TerrorTintCallBytes.size()), 0x90, static_cast<std::uint32_t>(TerrorTintCallBytes.size())))
			Log(false, "terror zone tint off (D2R+0x%llX)", static_cast<unsigned long long>(TerrorTintCallRva));
		else
			Log(true, "terror zone tint: code differs, left on");
	}
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

std::string MajorMinor(std::string_view version) noexcept {
	const auto first = version.find('.');
	if (first == std::string_view::npos) return std::string(version);
	const auto second = version.find('.', first + 1);
	return std::string(version.substr(0, second));
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

void DefaultMatrix() noexcept {
	// Hide what you, your merc and other players cast, on everyone but other
	// players, except a unit's own auras on you and your merc.
	for (int on : { Self, Merc, Minions, Monsters })
		for (int from : { Self, Merc, Allies }) Config.hide[on][from] = true;
	Config.hide[Self][Self] = false;
	Config.hide[Merc][Merc] = false;
}

void LoadConfig() {
	DefaultMatrix();
	(void)Context->EnsureConfig();
	std::uint32_t required = 0;
	std::vector<char> buffer(8192);
	if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) {
		if (required <= buffer.size()) {
			Log(true, "config: could not read the config file, using defaults");
			return;
		}
		buffer.resize(required);
		if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) return;
	}
	const std::string_view text(buffer.data(), std::strlen(buffer.data()));
	std::size_t lineStart = 0;
	while (lineStart < text.size()) {
		auto lineEnd = text.find('\n', lineStart);
		if (lineEnd == std::string_view::npos) lineEnd = text.size();
		const std::string line = Trim(text.substr(lineStart, lineEnd - lineStart));
		lineStart = lineEnd + 1;
		const auto equals = line.find('=');
		if (line.empty() || line[0] == '#' || line[0] == '[' || equals == std::string::npos) continue;
		const std::string key   = Trim(std::string_view(line).substr(0, equals));
		const std::string value = Trim(std::string_view(line).substr(equals + 1));
		if (key == "enabled") Config.enabled = value.rfind("false", 0) != 0;
		else if ((key.rfind("on_", 0) == 0 || key.rfind("shouts_on_", 0) == 0) && !value.empty() && value[0] == '[') {
			const bool shouts = key[0] == 's';
			const int  on     = KindIndex(std::string_view(key).substr(shouts ? 10 : 3));
			if (on < 0) {
				Log(true, "config: unknown key %s", key.c_str());
				continue;
			}
			auto& row = (shouts ? Config.hideShouts : Config.hide)[on];
			row.fill(false);
			for (const auto& name : ParseStrings(value)) {
				const int from = KindIndex(name);
				if (from < 0) Log(true, "config: %s: unknown kind \"%s\"", key.c_str(), name.c_str());
				else row[from] = true;
			}
		} else if ((key == "hide_firestorm" || key == "hide_thunder_storm" || key == "hide_charges") && !value.empty() && value[0] == '[') {
			auto& row = key == "hide_firestorm" ? Config.hideFirestorm : key == "hide_charges" ? Config.hideCharges : Config.hideThunderStorm;
			row.fill(false);
			for (const auto& name : ParseStrings(value)) {
				const int kind = KindIndex(name);
				if (kind < 0) Log(true, "config: %s: unknown kind \"%s\"", key.c_str(), name.c_str());
				else row[kind] = true;
			}
		} else if (key == "hide_terror_zone_tint") Config.hideTerrorTint = value.rfind("true", 0) == 0;
		else if (key == "versions" && !value.empty() && value[0] == '[') {
			auto versions = ParseStrings(value);
			if (!versions.empty()) Config.versions = std::move(versions);
		}
	}
}

std::string KindsText(const std::array<bool, KindCount>& kinds) {
	std::string text;
	for (int kind = 0; kind < KindCount; ++kind)
		if (kinds[kind]) text += std::string(text.empty() ? "" : ",") + KindNames[kind];
	return text.empty() ? "-" : text;
}

std::string EffectsText() {
	return "firestorm from " + KindsText(Config.hideFirestorm) + "; thunder storm from " + KindsText(Config.hideThunderStorm) + "; charges on " +
	       KindsText(Config.hideCharges) + "; terror zone tint " + (Config.hideTerrorTint ? "off" : "on");
}

std::string MatrixText(const std::array<std::array<bool, KindCount>, KindCount>& hide) {
	std::string text;
	for (int on = 0; on < KindCount; ++on) {
		text += std::string(on ? "; " : "") + "on " + KindNames[on] + ":";
		bool any = false;
		for (int from = 0; from < KindCount; ++from)
			if (hide[on][from]) text += std::string(any ? "," : " ") + KindNames[from], any = true;
		if (!any) text += " -";
	}
	return text;
}

// ---- console ----

auto StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
	if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
	char line[384] {};
	if (Active.load()) {
		std::size_t casters = 0;
		AcquireSRWLockShared(&CasterLock);
		casters = Casters.size();
		ReleaseSRWLockShared(&CasterLock);
		std::snprintf(line, sizeof(line), "visual-clutter %s: active on %s; aura effects hidden %u, shown %u; casters known %zu (recorded %u)", PluginVersion, GameVersion.c_str(),
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
	GameVersion = ReadGameVersion();
	const std::string majorMinor = MajorMinor(GameVersion);
	Log(false, "Visual Clutter %s: game %s, hide auras %s", PluginVersion, GameVersion.c_str(), MatrixText(Config.hide).c_str());
	Log(false, "hide shouts %s", MatrixText(Config.hideShouts).c_str());
	Log(false, "hide %s", EffectsText().c_str());
	if (!Config.enabled) {
		Problem = "disabled in config";
		Log(false, "disabled in config");
		return true;
	}
	bool supported = false;
	for (const auto& version : Config.versions) supported |= version == majorMinor;
	if (!supported) {
		Problem = "game version not in versions";
		Log(true, "inactive: game version %s is not in versions (edit the config to allow it)", majorMinor.c_str());
		return true;
	}
	if (!Base || !Locate() || !InstallHooks()) {
		Log(true, "inactive: %s", Problem.c_str());
		return true;
	}
	Active.store(true, std::memory_order_release);
	Log(false, "active: state overlays hooked at D2R+0x%llX, state toggle at D2R+0x%llX", static_cast<unsigned long long>(StateOverlaysRva), static_cast<unsigned long long>(ToggleStateRva));
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	Active.store(false, std::memory_order_release);
}
