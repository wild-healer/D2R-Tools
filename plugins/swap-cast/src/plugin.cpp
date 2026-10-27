// Swap Cast: each skill casts with weapon set I (M), set II (S), or one hand
// from each (MS = set I weapon + set II shield/off-hand, SM = set II weapon +
// set I shield/off-hand). Hover a skill in the skill picker (or the left/right
// skill button) and press the plugin key to cycle its mode; the letters show in
// the tile's corner. When the skill is cast its weapons go in your hands and
// set I comes back right after; a skill with no mode casts with set I. The items go back where
// the game thinks they are before a weapon swap (W) and when you leave.
//
// Game code used (Steam 3.3.93787 / 94995 RVAs):
//   Server (game thread)
//   - UseSkillOnLocation 0x4FDB40 (Game*, Unit* player, x, y, int, Skill*) and
//     UseSkillOnUnit 0x4F8DE0 (Game*, Unit* player, type, id, Skill*, int, int):
//     every left/right/hotkey cast of a player goes through one of them before
//     the skill starts. Skill +0 -> skills.txt row (int16 id first), +0x4C =
//     the item that grants it (-1 = the character's own skill).
//   - MoveItems 0x471E90 (Unit* player, Loc* from, Loc* to, u32* itemIds,
//     u64 count, u8 0) -> bool: the all-or-nothing rearrange the weapon swap
//     handler 0x4BEA50 uses; it checks every placement (two-handed weapons...).
//     Loc = { u32 1, u32 0xFF, u16 bodyLoc, u16 0, u32 bodyLoc }. Then, as the
//     swap handler does, per moved item: FindUnit 0x48FE80 (game, 4, id),
//     SetItemFlag 0x36D8F0 (item, 0x4000, 0), Notify 0x535F60 (game, player,
//     item, bodyLoc < 11 ? 0x41 : 0x81, 0x200000, 1, 0xFF, bodyLoc, bodyLoc),
//     and Refresh 0x34DBE0 (player, 1) once.
//   - body items: UnitInventory 0x34A360 (unit, file, line), BodyItem 0x3886D0
//     (inventory, bodyLoc): 4 weapon, 5 shield, 11/12 the other set.
//   - checks: IsDead 0x34C2C0, UsedSkill 0x34BA40 (non-null while a skill is
//     in use), HasState 0x3351B0 (unit, 0x36 = uninterruptable), UnitGuid 0x34A330.
//   - the weapon swap handler 0x4BEA50 (Game*, Unit*, packet, size) is hooked
//     to put the items back first and fix the packet's item ids, so a swap
//     right after a cast swaps your real sets.
//   - item skills: SetItemSkill 0x33FE60 (unit, item id, skill, level, charges,
//     remove) adds or removes a skill node an item grants (oskills, charges).
//     Swapping removes the nodes of the set you put away, so the skill picker
//     loses them (Call to Arms on swap). The plugin skips those removals for the
//     local player while the item stays in a weapon slot (4/5/11/12) and removes
//     the node itself once the item leaves them. A set never held this game had
//     no nodes yet: on your next packet (dispatcher 0x4F2FA0) its items are put in
//     your hands once and back, which adds them.
//   Client (UI thread)
//   - SkillSelectButtonWidget (vtable from its constructor 0x2371E0): +0xB88
//     side (0 = skill picker tile, 1/2 = HUD left/right button), +0xC08 skill
//     id, +0xC10 quantity (scrolls/tomes), +0xBF0 its QuantityText TextBoxWidget.
//     SetSkill 0x239110 (widget, skillId) and SetSkillObject 0x239380 (widget,
//     Skill*, u8) -> bool fill a tile (+0xC0C = the granting item, -1 own
//     skill; SetSkill leaves -1, so a HUD button looks at your left/right skill
//     0x34A540 / 0x34B400 for it). The label goes into
//     QuantityText when the tile shows no quantity: TextBox vtable +0x50 shows
//     it, Format 0xCE510 (text + 0x88, or + 0x200 when +0x1F6 and +0x1F7 are
//     set) writes it, the way SetSkill writes counts.
//   - hovered widget: UiRoot 0x846020 = [rip global] + 0xD0, hovered at +0x178
//     (what the game itself checks to bind F1-F8 to the hovered tile).
//   - local player: PlayerByIndex 0x9A480 (LocalIndex 0x8B2D0); unit +0x10 ->
//     PlayerData, name at +0.

#include <D2RLPlugin/api.h>
#include <shlobj.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr const char* PluginId      = "swap-cast";
constexpr const char* PluginVersion = "1.1.0";

const D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = PluginId,
	.name        = "Swap Cast",
	.version     = PluginVersion,
	.author      = "wild-healer",
	.description = "Cast each skill with your weapons, your swap weapons, or one hand from each.",
	.flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

struct Settings {
	bool enabled       = true;
	bool showMainLabel = false;
	bool allowCross    = true; // MS and SM
	int  restoreMs     = 150;
	bool logSwaps      = false;
	bool keepSwapSkills = true;
} Config;

// ---- modes ----

enum Mode : std::uint8_t { Main = 0, MainSwap = 1, SwapMain = 2, Swap = 3 };
constexpr const char* Labels[] = { "M", "MS", "SM", "S" };
constexpr int         MaxSkills = 1024;

// Next in the order of the request: M -> MS -> SM -> S -> M.
// Without cross casts, a mode is M or S, picked by where the weapon comes from.
Mode Allowed(Mode mode) noexcept { return Config.allowCross ? mode : mode == SwapMain ? Swap : mode == MainSwap ? Main : mode; }
Mode Next(Mode mode) noexcept {
	if (Config.allowCross) return static_cast<Mode>((mode + 1) & 3);
	return Allowed(mode) == Swap ? Main : Swap;
}
bool RightFromSwap(Mode mode) noexcept { return mode == SwapMain || mode == Swap; }
bool LeftFromSwap(Mode mode) noexcept { return mode == MainSwap || mode == Swap; }

// Written on the UI thread, read on the game thread.
std::array<std::atomic<std::uint8_t>, MaxSkills> Modes {};

// 0 = not chosen, else mode + 1.
std::uint8_t StoredMode(int skill) noexcept { return skill >= 0 && skill < MaxSkills ? Modes[skill].load(std::memory_order_relaxed) : 0; }

// Skills an item grants: 0 = not chosen, else mode + 1. The hand the item is in
// is fixed (the skill needs its item in hand); the mode picks the other hand.
std::array<std::atomic<std::uint8_t>, MaxSkills> ItemModes {};

// Slot = where the item is among R, L, other R, other L. Not chosen: S for an
// item of your other set (what you'd swap to for it), M for what you hold.
Mode ItemMode(int skill, int slot) noexcept {
	const std::uint8_t stored = skill >= 0 && skill < MaxSkills ? ItemModes[skill].load(std::memory_order_relaxed) : 0;
	if (slot < 0 || slot > 3) return Main;
	if (stored) return Allowed(static_cast<Mode>((stored - 1) & 3)); // any mode, even one that leaves the item out of your hands
	return slot >= 2 ? Swap : Main;
}

// The hand the item isn't in: the bit of the mode a key press flips.

// ---- native layout (Steam 3.3.93787 / 94995) ----

using Code = std::array<std::uint8_t, 16>;

constexpr std::uint64_t UseSkillLocRva = 0x4FDB40;
constexpr Code UseSkillLocBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57 };
constexpr std::uint64_t UseSkillUnitRva = 0x4F8DE0;
constexpr Code UseSkillUnitBytes { 0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xAC };
constexpr std::uint64_t WeaponSwapRva = 0x4BEA50;
constexpr Code WeaponSwapBytes { 0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x56, 0x48, 0x8D, 0xAC, 0x24, 0x70, 0xFF, 0xFF, 0xFF, 0x48 };
constexpr std::uint64_t SetSkillRva = 0x239110;
constexpr Code SetSkillBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57 };
constexpr std::uint64_t SetSkillObjectRva = 0x239380;
constexpr Code SetSkillObjectBytes { 0x48, 0x89, 0x5C, 0x24, 0x20, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57 };
constexpr std::uint64_t SetItemSkillRva = 0x33FE60;
constexpr Code SetItemSkillBytes { 0x48, 0x85, 0xC9, 0x0F, 0x84, 0x31, 0x02, 0x00, 0x00, 0x48, 0x89, 0x5C, 0x24, 0x20, 0x56, 0x41 };
constexpr std::uint64_t DispatchRva = 0x4F2FA0;
constexpr Code DispatchBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57 };
constexpr std::uint64_t LeftSkillRva  = 0x34A540;
constexpr std::uint64_t RightSkillRva = 0x34B400;
constexpr Code SideSkillBytes { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C }; // both
constexpr std::uint64_t RemoveSkillRva = 0x33EA00;
constexpr Code RemoveSkillBytes { 0x48, 0x83, 0xEC, 0x38, 0x44, 0x89, 0x4C, 0x24, 0x20, 0x4D, 0x8B, 0xC8, 0x41, 0xB8, 0x01, 0x00 };
constexpr std::uint64_t AddSkillRva = 0x33EFB0; // (unit, skill, 0, 0, file, line): what the client's "+skill" stat callback (D2R+0xFD1C0) uses
constexpr Code AddSkillBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57 };
constexpr std::uint64_t SkillTxtRva = 0x97790; // (u8 data, skill) -> skills.txt row, class at +0x2C
constexpr Code SkillTxtBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48 };
constexpr std::uint64_t DataTablesRva = 0x300A90; // (u8 data) -> tables, skills.txt row count at +0x11B8
constexpr Code DataTablesBytes { 0x48, 0x83, 0xEC, 0x28, 0x0F, 0xB6, 0xC1, 0x48, 0x89, 0x44, 0x24, 0x38, 0x48, 0x83, 0xF8, 0x04 };
constexpr std::uintptr_t UnitDataIndexOffset = 0x1BD, SkillCountOffset = 0x11B8, SkillClassOffset = 0x2C;
constexpr std::uintptr_t ChargeShiftOffset = 0x14D0, ChargeMaskOffset = 0x14D4; // in the data tables: how the charged skill layer splits
constexpr std::uint64_t SkillTipRva = 0x28B950; // (GameString* out, unit, skill, level, a5, a6) -> out: a skill button's tooltip text (from 0x237CE0)
constexpr Code SkillTipBytes { 0x40, 0x55, 0x53, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0x60, 0xF0, 0xFF };
constexpr std::uint64_t StringReserveRva = 0x76210; // (GameString*, size): room for size chars (+1)
constexpr Code StringReserveBytes { 0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x5B, 0x08, 0x49, 0x89, 0x6B, 0x18, 0x49, 0x89, 0x73, 0x20, 0x49 };
constexpr std::uint64_t FindSkillRva = 0x33DCD0;
constexpr Code FindSkillBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41, 0x8B, 0xD8, 0x8B, 0xFA, 0xE8 };
constexpr std::uint64_t SkillLevelRva = 0x33D1E0; // entries reached through a jmp [rip]: not checked
constexpr std::uint64_t BonusLevelRva = 0x3400A0; // (unit, skill, flag): +skills on top of the base level (node +0x40)
constexpr std::array<std::uint8_t, 6> BonusLevelBytes { 0xFF, 0x25, 0xD2, 0xA2, 0xAE, 0x03 }; // jmp [rip+0x3AEA2D2]
constexpr std::array<std::uint8_t, 6> SkillLevelBytes { 0xFF, 0x25, 0x8A, 0xD1, 0xAE, 0x03 }; // jmp [rip+0x3AED18A], hooked (MinHook relocates it)
constexpr std::uint64_t CanUseSkillRva = 0x33F360; // (unit, skill) -> 0 usable, 4 wrong weapon ("I can't"), 7 level 0 (red, silent)
constexpr Code CanUseSkillBytes { 0x40, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0xFA, 0x48, 0x8B, 0xF1, 0x48, 0x85, 0xD2 };
constexpr int NoLevelResult = 7, WrongWeaponResult = 4; // 2-4: the items in your hands don't fit the skill
constexpr std::uint64_t SideSkillOkRva  = 0x438F90; // (player, side 1 left / 0 right, skill) -> nonzero if it can go there
constexpr Code SideSkillOkBytes { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41 };
constexpr std::uint64_t SetSideSkillRva = 0x438A70; // (player, side, skill, owner): what the weapon swap uses to put a set's skills back
constexpr Code SetSideSkillBytes { 0x41, 0x83, 0xF8, 0x05, 0x0F, 0x84, 0x28, 0x02, 0x00, 0x00, 0x44, 0x89, 0x4C, 0x24, 0x20, 0x55 };
constexpr std::uint64_t SendSideSkillRva = 0x5393C0; // (player, side, u16 skill, owner): tells the client
constexpr Code SendSideSkillBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48 };
constexpr std::uint64_t TryCastRva = 0x21B170; // client (unit, skill) -> CanUseSkill result
constexpr Code TryCastBytes { 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xFA, 0x48, 0x8B, 0xF1 };
constexpr std::uint64_t LoadPlayerRva = 0x41DF40; // (game, client, name, Unit** out): reads your save into the game as it starts
constexpr Code LoadPlayerBytes { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x68, 0x10, 0x56, 0x57, 0x41, 0x56, 0x48 };
constexpr std::uint64_t LeaveSaveRva = 0x41E0D0; // (game, player): saves a player (leaving the game, and the autosave every 30 s)
constexpr Code LeaveSaveBytes { 0x48, 0x8B, 0xC4, 0x53, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x68, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9 };
// "Impossible", the way Cube Empty says it: PlaySound(sounds.txt row, unit, 0,
// 0, false) with the row from the player sound table (0x30 per character in
// sounds.txt order, "impossible" at +0x08; amazon_impossible_1 = 2953).
constexpr std::uint64_t PlaySoundRva = 0x1A0780;
constexpr std::array<std::uint8_t, 15> PlaySoundTail { 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xD1, 0x48, 0x81, 0xEC, 0x00, 0x01, 0x00, 0x00 }; // after 5 bytes another plugin may patch
constexpr std::uint64_t ImpossibleRva    = 0x22A4118;
constexpr std::uintptr_t ImpossibleStride = 0x30;
constexpr std::int32_t   AmazonImpossible = 2953;
constexpr std::array<std::uint32_t, 8> SoundRow { 0, 4, 2, 3, 1, 6, 5, 7 }; // playerclass.txt order -> sounds.txt order
constexpr std::uintptr_t UnitClassOffset = 0x04;
constexpr std::uint64_t GetStatRva    = 0x2F5020;
constexpr std::uint64_t PlayerClientRva = 0x48FDE0;
constexpr Code PlayerClientBytes { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x74, 0x1E, 0xE8, 0xDD };
constexpr std::uint64_t SwitchStateRva = 0x485200;
constexpr Code SwitchStateBytes { 0x0F, 0xB6, 0x81, 0x70, 0x05, 0x00, 0x00, 0xC3, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC };
constexpr std::uint64_t ButtonLevelCallRva = 0x239BF4; // call SkillLevel in the skill button update 0x2399F0
constexpr std::array<std::uint8_t, 9> ButtonLevelCallBytes { 0xE8, 0xE7, 0x35, 0x10, 0x00, 0x85, 0xC0, 0x7F, 0x67 };
constexpr std::uint64_t MoveItemsRva = 0x471E90;
constexpr Code MoveItemsBytes { 0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x48, 0x8D, 0xAC, 0x24, 0x58, 0x56, 0xFF };
constexpr std::uint64_t NotifyRva = 0x535F60;
constexpr Code NotifyBytes { 0x48, 0x89, 0x5C, 0x24, 0x20, 0x55, 0x56, 0x57, 0x41, 0x55, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x60 };
constexpr std::uint64_t SetItemFlagRva = 0x36D8F0;
constexpr Code SetItemFlagBytes { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41 };
constexpr std::uint64_t RefreshRva = 0x34DBE0;
constexpr Code RefreshBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0xFA, 0x48, 0x8B, 0xD9, 0x85 };
constexpr std::uint64_t FindUnitRva = 0x48FE80;
constexpr Code FindUnitBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41 };
constexpr std::uint64_t UnitInventoryRva = 0x34A360;
constexpr Code UnitInventoryBytes { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF1, 0x48, 0x85, 0xC9 };
constexpr std::uint64_t BodyItemRva = 0x3886D0;
constexpr Code BodyItemBytes { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x63, 0xFA, 0x48, 0x8B, 0xD9 };
constexpr std::uint64_t UnitGuidRva = 0x34A330;
constexpr std::uint64_t IsDeadRva   = 0x34C2C0;
constexpr Code GuardBytes { 0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1D, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C }; // both start like this
constexpr std::uint64_t UsedSkillRva = 0x34BA40;
constexpr Code UsedSkillBytes { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C };
constexpr std::uint64_t HasStateRva = 0x3351B0;
constexpr Code HasStateBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B };
constexpr std::uint64_t FormatRva = 0xCE510;
constexpr Code FormatBytes { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x50, 0x10, 0x4C, 0x89, 0x40, 0x18, 0x4C, 0x89, 0x48, 0x20, 0x53 };
constexpr std::uint64_t UiRootRva = 0x846020; // sub rsp,28; mov rax,[rip+x] at +4
constexpr std::uint64_t SsbwCtorRva = 0x2371E0; // lea rax,[rip+vtable] at +0x1B
constexpr Code SsbwCtorBytes { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0xE8, 0x32, 0x77, 0x63, 0x00, 0x45, 0x33 };
constexpr std::uint64_t PlayerByIndexRva = 0x9A480;
constexpr Code PlayerByIndexBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x83, 0xF9, 0x08, 0x0F, 0x83, 0x85 };
constexpr std::uint64_t LocalIndexRva = 0x8B2D0; // mov eax, [rip+x]; ret

constexpr std::uintptr_t UnitTypeOffset = 0x00;  // u32, 0 = player
constexpr std::uintptr_t UnitIdOffset   = 0x08;  // u32
constexpr std::uintptr_t UnitDataOffset = 0x10;  // PlayerData*: name at +0
constexpr std::uintptr_t SkillTxtOffset = 0x00;  // skills.txt row*, int16 id at +0
constexpr std::uintptr_t SkillOwnerOffset = 0x4C; // u32 item id, -1 = own skill
constexpr std::uintptr_t SkillNextOffset  = 0x08;
constexpr std::uintptr_t SkillBaseOffset  = 0x40; // int, hard points
constexpr std::uintptr_t UnitSkillsOffset = 0x100; // skill list, first node at +0
constexpr std::uintptr_t WidgetSide     = 0xB88;
constexpr std::uintptr_t WidgetSkill    = 0xC08;
constexpr std::uintptr_t WidgetOwner    = 0xC0C;
constexpr std::uintptr_t WidgetQuantity = 0xC10;
constexpr std::uintptr_t WidgetQuantityText = 0xBF0;
constexpr std::uintptr_t UiHovered      = 0x178;
constexpr std::uint32_t  NoId           = 0xFFFFFFFF;
constexpr int            PlayerType     = 0;
constexpr int            ItemType       = 4;
constexpr int            NonClassSkillStat = 0x61; // item_nonclassskill, layer = skill
constexpr int            SingleSkillStat   = 0x6B; // item_singleskill
constexpr int            ChargedSkillStat  = 0xCC; // item_charged_skill, layer = skill << shift | level, value = charges (low byte)
constexpr int            UninterruptableState = 0x36;

// Slots in the order R, L, other R, other L.
constexpr std::array<std::uint16_t, 4> Slots { 4, 5, 11, 12 };

struct Loc {
	std::uint32_t kind = 1;
	std::uint32_t page = 0xFF;
	std::uint16_t bodyLoc = 0;
	std::uint16_t zero = 0;
	std::uint32_t bodyLoc32 = 0;
};
static_assert(sizeof(Loc) == 16);

using UseSkillLocFn  = int (*)(void* game, void* player, std::uint32_t x, std::uint32_t y, int flag, void* skill) noexcept;
using UseSkillUnitFn = int (*)(void* game, void* player, std::uint32_t type, std::uint32_t id, void* skill, int a6, int a7) noexcept;
using WeaponSwapFn   = int (*)(void* game, void* player, std::uint8_t* packet, int size) noexcept;
using SetSkillFn     = void (*)(void* widget, int skill) noexcept;
using SetSkillObjectFn = bool (*)(void* widget, void* skill, std::uint8_t flag) noexcept;
using SetItemSkillFn = void (*)(void* unit, std::uint32_t owner, int skill, int level, int charges, int remove) noexcept;
using DispatchFn     = std::uint64_t (*)(void* game, void* player, std::uint8_t* packet, int size) noexcept;
using SideSkillFn    = void* (*)(void* unit) noexcept;
using RemoveSkillFn  = std::uint64_t (*)(void* unit, int skill, const char* file, int line) noexcept;
using AddSkillFn     = void* (*)(void* unit, int skill, int a3, int a4, const char* file, int line) noexcept;
using SkillTxtFn     = void* (*)(std::uint8_t data, int skill) noexcept;
using DataTablesFn   = void* (*)(std::uint8_t data) noexcept;
struct GameString {
	char*         text;
	std::uint64_t size;
	std::uint64_t capacity; // top bit: the inline buffer
};
using SkillTipFn     = GameString* (*)(GameString* out, void* unit, int skill, int level, int a5, int a6) noexcept;
using StringReserveFn = void (*)(GameString* string, std::uint64_t size) noexcept;
using FindSkillFn    = void* (*)(void* unit, int skill, std::uint32_t owner) noexcept;
using SkillLevelFn   = int (*)(void* unit, void* skill, int bonus, int flag) noexcept;
using BonusLevelFn   = int (*)(void* unit, void* skill, int flag) noexcept;
using CanUseSkillFn  = int (*)(void* unit, void* skill) noexcept;
using PlaySoundFn    = void (*)(int sound, void* unit, int, int, bool) noexcept;
using SideSkillOkFn  = int (*)(void* player, int side, int skill) noexcept;
using TryCastFn      = int (*)(void* unit, void* skill) noexcept;
using SetSideSkillFn = std::uint64_t (*)(void* player, int side, int skill, std::uint32_t owner) noexcept;
using SendSideSkillFn = void (*)(void* player, int side, std::uint16_t skill, std::uint32_t owner) noexcept;
using LoadPlayerFn   = std::uint64_t (*)(void* game, void* client, void* name, void** out) noexcept;
using LeaveSaveFn    = std::uint64_t (*)(void* game, void* player) noexcept;
using GetStatFn      = int (*)(void* unit, int stat, int layer) noexcept;
using PlayerClientFn = void* (*)(void* player) noexcept;
using SwitchStateFn  = std::uint8_t (*)(void* client) noexcept;
using MoveItemsFn    = bool (*)(void* player, Loc* from, Loc* to, std::uint32_t* ids, std::uint64_t count, std::uint8_t flag) noexcept;
using NotifyFn       = void (*)(void* game, void* player, void* item, int command, int flags, std::uint32_t kind, std::uint32_t page, std::uint32_t bodyLoc, std::uint32_t loc) noexcept;
using SetItemFlagFn  = void (*)(void* item, std::uint32_t flag, int value) noexcept;
using RefreshFn      = void (*)(void* player, int value) noexcept;
using FindUnitFn     = void* (*)(void* game, int type, std::uint32_t id) noexcept;
using UnitInventoryFn = void* (*)(void* unit, const char* file, int line) noexcept;
using BodyItemFn     = void* (*)(void* inventory, int bodyLoc) noexcept;
using UnitGuidFn     = std::uint32_t (*)(void* unit) noexcept;
using IsDeadFn       = int (*)(void* unit) noexcept;
using UsedSkillFn    = void* (*)(void* unit) noexcept;
using HasStateFn     = int (*)(void* unit, int state) noexcept;
using FormatFn       = void (*)(void* text, const char* format, ...) noexcept;
using SetVisibleFn   = void (*)(void* widget, bool visible) noexcept;
using PlayerByIndexFn = void* (*)(std::uint32_t index) noexcept;
using LocalIndexFn   = std::uint32_t (*)() noexcept;

const D2RL::PluginContext*    Context {};
const D2RL::ThreadService*    Threads {};
std::uintptr_t                Base {};

UseSkillLocFn   UseSkillLoc {};  // originals, after the hooks
UseSkillUnitFn  UseSkillUnit {};
WeaponSwapFn    WeaponSwap {};
SetSkillFn      SetSkill {};
SetSkillObjectFn SetSkillObject {};
SetItemSkillFn  SetItemSkill {};
DispatchFn      Dispatch {};
SideSkillFn     LeftSkill {};
SideSkillFn     RightSkill {};
RemoveSkillFn   RemoveSkill {};
FindSkillFn     FindSkill {};
SkillTipFn      SkillTip {};
StringReserveFn StringReserve {};
AddSkillFn      AddSkill {};
SkillTxtFn      SkillTxt {};
DataTablesFn    DataTables {};
SkillLevelFn    SkillLevel {};
BonusLevelFn    BonusLevel {};
CanUseSkillFn   CanUseSkill {};
PlaySoundFn     Speak {};
SideSkillOkFn   SideSkillOk {};
TryCastFn       TryCast {};
SetSideSkillFn  SetSideSkill {};
SendSideSkillFn SendSideSkill {};
LeaveSaveFn     LeaveSave {};
LoadPlayerFn    LoadPlayer {};
GetStatFn       GetStat {};
PlayerClientFn  PlayerClient {};
SwitchStateFn   SwitchState {};
MoveItemsFn     MoveItems {};
NotifyFn        Notify {};
SetItemFlagFn   SetItemFlag {};
RefreshFn       Refresh {};
FindUnitFn      FindUnit {};
UnitInventoryFn UnitInventory {};
BodyItemFn      BodyItem {};
UnitGuidFn      UnitGuid {};
IsDeadFn        IsDead {};
UsedSkillFn     UsedSkill {};
HasStateFn      HasState {};
FormatFn        Format {};
PlayerByIndexFn PlayerByIndex {};
LocalIndexFn    LocalIndex {};
std::uintptr_t  UiRootGlobal {}; // address of the UI root pointer
std::uintptr_t  SsbwVtable {};

std::atomic<std::uint32_t> PlayerId { NoId }; // local player, from the UI thread

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
	SIZE_T copied = 0;
	return address != 0 && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), output, size, &copied) && copied == size;
}

template <typename T>
bool ReadField(const void* base, std::uintptr_t offset, T& value) noexcept {
	return base && Read(reinterpret_cast<std::uintptr_t>(base) + offset, &value, sizeof(T));
}

bool CodeMatches(std::uint64_t rva, const Code& bytes) noexcept {
	Code code {};
	return Read(Base + rva, code.data(), code.size()) && std::memcmp(code.data(), bytes.data(), code.size()) == 0;
}

// Target of a rip-relative operand whose 4-byte displacement is at `at` and
// whose instruction ends at `end`.
std::uintptr_t RipTarget(std::uintptr_t at, std::uintptr_t end) noexcept {
	std::int32_t displacement = 0;
	return Read(at, &displacement, sizeof(displacement)) ? end + displacement : 0;
}

// ---- server: putting the items in place ----

// Server state, game thread only. Natural = the items in R, L, other R, other
// L as the game sees them (the set it thinks you hold first); Tracking while
// the plugin holds them some other way. The sets are absolute: set 1 is the
// game's weapon set I, set 2 its set II, whatever you hold.
struct ServerState {
	void*                        game     = nullptr;
	void*                        player   = nullptr;
	bool                         tracking = false;
	std::array<std::uint32_t, 4> natural {};
	bool                         away     = false; // not on set I: ServerTick brings it back
	ULONGLONG                    lastBusy = 0;
} Server;

std::atomic<bool> TickQueued {};

// Set 1 weapon, set 1 shield, set 2 weapon, set 2 shield (for the labels on
// the UI thread; written on the game thread).
std::array<std::atomic<std::uint32_t>, 4> Sets { NoId, NoId, NoId, NoId };

void* LocalPlayer() noexcept;

bool IsLocalPlayer(void* unit) noexcept {
	std::uint32_t type = NoId, id = NoId;
	if (!ReadField(unit, UnitTypeOffset, type) || type != 0 || !ReadField(unit, UnitIdOffset, id)) return false;
	if (id == NoId) return false;
	std::uint32_t local = PlayerId.load(std::memory_order_acquire);
	// Loading into a game: the UI tick hasn't seen your character yet, but the
	// client has it as soon as it's in (so the first packets, sent while the
	// game loads, count).
	if (local == NoId)
		if (void* const client = LocalPlayer(); client && ReadField(client, UnitIdOffset, local)) PlayerId.store(local, std::memory_order_release);
	return id == local;
}

std::array<std::uint32_t, 4> CurrentItems(void* player) noexcept {
	std::array<std::uint32_t, 4> ids { NoId, NoId, NoId, NoId };
	void* const inventory = UnitInventory(player, __FILE__, __LINE__);
	if (!inventory) return ids;
	for (std::size_t slot = 0; slot < Slots.size(); ++slot)
		if (void* const item = BodyItem(inventory, Slots[slot])) ids[slot] = UnitGuid(item);
	return ids;
}

int SlotOf(const std::array<std::uint32_t, 4>& items, std::uint32_t item) noexcept {
	if (item == NoId) return -1;
	const auto found = std::find(items.begin(), items.end(), item);
	return found == items.end() ? -1 : static_cast<int>(found - items.begin());
}

std::array<std::uint32_t, 4> SetsNow() noexcept {
	return { Sets[0].load(), Sets[1].load(), Sets[2].load(), Sets[3].load() };
}

// ---- skills of the set you don't hold ----
//
// Two kinds: skills an item grants as its own node (charges; SetItemSkill with
// the item id) and "+skill" stats (Call to Arms' Battle Orders): a normal skill
// node the client deletes once nothing gives it a level (RemoveSkill). Both
// removals are skipped for you and done later by Sweep, once no item in your
// weapon slots gives the skill any more.
struct KeptSkill {
	void*         unit;
	std::uint32_t owner; // NoId for a "+skill" node
	int           skill;
	int           level;
};
std::mutex             KeptLock;
std::vector<KeptSkill> Kept;

void Keep(const KeptSkill& skill) noexcept {
	std::lock_guard lock(KeptLock);
	const auto      same = std::find_if(Kept.begin(), Kept.end(), [&](const KeptSkill& kept) { return kept.unit == skill.unit && kept.owner == skill.owner && kept.skill == skill.skill; });
	if (same != Kept.end()) return;
	if (Kept.size() >= 256) Kept.erase(Kept.begin());
	Kept.push_back(skill);
}

std::array<std::uint32_t, 4> CurrentItems(void* player) noexcept;
bool                         GrantedByWeapon(void* unit, int skill) noexcept;
extern std::atomic<void*>    ClientPlayer;
std::atomic<ULONGLONG>       MovingUntil {}; // your weapons are being moved (swap cast, W): the client drops their skills on the way

void Moving() noexcept { MovingUntil.store(GetTickCount64() + 1500, std::memory_order_relaxed); }

// Your client may let a removal through: not while your weapons move, nor
// for an item still in a weapon slot.
bool ClientMayRemove(void* unit) noexcept { return unit == ClientPlayer.load(std::memory_order_relaxed) && GetTickCount64() >= MovingUntil.load(std::memory_order_relaxed); }

// Hovering an item in your inventory adds its skills to compare and removes
// them right away: those removals go through.
void HookSetItemSkill(void* unit, std::uint32_t owner, int skill, int level, int charges, int remove) noexcept {
	if (Config.keepSwapSkills && owner != NoId && level > 0 && IsLocalPlayer(unit)) {
		if (remove && (!ClientMayRemove(unit) || SlotOf(CurrentItems(unit), owner) >= 0)) {
			Keep({ unit, owner, skill, level });
			return;
		}
		std::lock_guard lock(KeptLock);
		const auto      same = std::find_if(Kept.begin(), Kept.end(), [&](const KeptSkill& kept) { return kept.unit == unit && kept.owner == owner && kept.skill == skill; });
		if (same != Kept.end()) Kept.erase(same); // back in use: the game tracks it again
	}
	SetItemSkill(unit, owner, skill, level, charges, remove);
}

std::uint64_t HookRemoveSkill(void* unit, int skill, const char* file, int line) noexcept {
	if (Config.keepSwapSkills && skill >= 0 && IsLocalPlayer(unit) && (!ClientMayRemove(unit) || GrantedByWeapon(unit, skill))) {
		Keep({ unit, NoId, skill, 0 });
		return 0;
	}
	return RemoveSkill(unit, skill, file, line);
}

// Does an item in a weapon slot give this "+skill"?
bool GrantedByWeapon(void* unit, int skill) noexcept;

// A skill of your own: its chosen mode, else S for a "+skill" only your set II
// gives (Call to Arms' Battle Orders when you haven't learned it), else M.
Mode OwnMode(void* unit, int skill, const std::array<std::uint32_t, 4>& sets) noexcept {
	if (const std::uint8_t stored = StoredMode(skill)) return Allowed(static_cast<Mode>((stored - 1) & 3));
	void* const inventory = unit ? UnitInventory(unit, __FILE__, __LINE__) : nullptr;
	if (!inventory || skill < 0) return Main;
	bool setOne = false, setTwo = false;
	for (const auto slot : Slots) {
		void* const item = BodyItem(inventory, slot);
		if (!item || (GetStat(item, NonClassSkillStat, skill) <= 0 && GetStat(item, SingleSkillStat, skill) <= 0)) continue;
		(SlotOf(sets, UnitGuid(item)) >= 2 ? setTwo : setOne) = true;
	}
	if (!setTwo || setOne) return Main;
	void* const node = FindSkill(unit, skill, NoId);
	return node && SkillLevel(unit, node, 0, 0) > 0 ? Main : Swap;
}

bool GrantedByWeapon(void* unit, int skill) noexcept {
	void* const inventory = UnitInventory(unit, __FILE__, __LINE__);
	if (!inventory) return false;
	for (const auto slot : Slots) {
		void* const item = BodyItem(inventory, slot);
		if (item && (GetStat(item, NonClassSkillStat, skill) > 0 || GetStat(item, SingleSkillStat, skill) > 0)) return true;
	}
	return false;
}

int SkillId(void* skill) noexcept;

// A "+skill" node nothing gives any more: no hard points, no item in your
// weapon slots and no other gear (the game's own test, D2R+0xFD2E7). The game
// would have removed it, but taking an item off set II tells it nothing.
bool Orphan(void* unit, void* node, int skill) noexcept {
	std::uint32_t owner = 0;
	return node && skill >= 0 && ReadField(node, SkillOwnerOffset, owner) && owner == NoId && SkillLevel(unit, node, 0, 0) <= 0 && !GrantedByWeapon(unit, skill) &&
	       GetStat(unit, NonClassSkillStat, skill) <= 0 && GetStat(unit, SingleSkillStat, skill) <= 0;
}

// Drops what the game wanted to remove, once no weapon-slot item gives it.
// A "+skill" still selected on a skill button is left to the game, which
// removes it itself.
void Sweep(void* unit) noexcept {
	const auto             items = CurrentItems(unit);
	std::vector<KeptSkill> gone;
	{
		std::lock_guard lock(KeptLock);
		for (auto kept = Kept.begin(); kept != Kept.end();) {
			bool drop = false;
			if (kept->unit == unit) {
				if (kept->owner != NoId) drop = SlotOf(items, kept->owner) < 0;
				else if (!GrantedByWeapon(unit, kept->skill)) {
					void* const node = FindSkill(unit, kept->skill, NoId);
					if (!node || !Orphan(unit, node, kept->skill)) {
						drop        = true; // gone, or learned / given by other gear
						kept->level = -1;
					} else drop = node != LeftSkill(unit) && node != RightSkill(unit);
				}
			}
			if (drop) {
				gone.push_back(*kept);
				kept = Kept.erase(kept);
			} else ++kept;
		}
	}
	for (const auto& kept : gone) {
		if (kept.owner != NoId) SetItemSkill(kept.unit, kept.owner, kept.skill, kept.level, 0, 1);
		else if (kept.level == 0) RemoveSkill(kept.unit, kept.skill, __FILE__, __LINE__);
	}
}

// The skill button's level check (D2R+0x239BF4): a "+skill" of your other set
// counts as level 1, so the button keeps it instead of falling back to Attack.
int HookButtonLevel(void* unit, void* node, int bonus, int flag) noexcept {
	const int     level = SkillLevel(unit, node, bonus, flag);
	std::uint32_t owner = 0;
	if (level > 0 && Config.keepSwapSkills && IsLocalPlayer(unit) && Orphan(unit, node, SkillId(node))) return 0; // the button lets it go, then Sweep removes it
	if (level > 0 || !Config.keepSwapSkills || !node || !ReadField(node, SkillOwnerOffset, owner) || owner != NoId || !IsLocalPlayer(unit)) return level;
	void*        txt = nullptr;
	std::int16_t id  = -1;
	return ReadField(node, SkillTxtOffset, txt) && ReadField(txt, 0, id) && GrantedByWeapon(unit, id) ? 1 : level;
}

std::atomic<void*> ClientPlayer {}; // the client's unit for you, from the UI tick

std::array<std::uint32_t, 4> Wanted(const std::array<std::uint32_t, 4>& sets, Mode mode) noexcept;

int GrantLevel(void* item, int skill) noexcept { return std::max(0, GetStat(item, NonClassSkillStat, skill)) + std::max(0, GetStat(item, SingleSkillStat, skill)); }

// A "+skill" only your other set gives: what that set gives in all, what the
// skill's mode puts in your hands, and whether it is a class skill (+X to a
// skill of your class) rather than an oskill. All 0 when your hands give it.
struct OtherSetGrant {
	int  total  = 0;
	int  inHand = 0;
	bool single = false;
};

OtherSetGrant OtherSetLevel(void* unit, void* node) noexcept {
	std::uint32_t owner = 0;
	void*         txt   = nullptr;
	std::int16_t  id    = -1;
	if (!ReadField(node, SkillOwnerOffset, owner) || owner != NoId || !ReadField(node, SkillTxtOffset, txt) || !ReadField(txt, 0, id) || id < 0) return {};
	void* const inventory = UnitInventory(unit, __FILE__, __LINE__);
	if (!inventory) return {};
	for (const int slot : { 4, 5 })
		if (void* const item = BodyItem(inventory, slot); item && GrantLevel(item, id) > 0) return {};
	const auto    sets = SetsNow();
	const auto    want = Wanted(sets, OwnMode(unit, id, sets));
	OtherSetGrant grant;
	for (const int slot : { 11, 12 }) {
		void* const item  = BodyItem(inventory, slot);
		const int   level = item ? GrantLevel(item, id) : 0;
		if (level <= 0) continue;
		grant.total += level;
		grant.single = grant.single || GetStat(item, SingleSkillStat, id) > 0;
		if (const std::uint32_t guid = UnitGuid(item); guid == want[0] || guid == want[1]) grant.inHand += level;
	}
	return grant;
}

bool ForClient(void* unit, void* node) noexcept { return node && Config.keepSwapSkills && (unit == ClientPlayer.load(std::memory_order_relaxed) || IsLocalPlayer(unit)); }

// SkillLevel for you (skill buttons, picker colour, the "can cast" checks):
// a skill at level 0 that your set II gives counts at the level set II gives
// it when its mode puts that item in your hands, so it isn't red and the
// cast goes through (the server puts set II in your hands first). Both the
// client's unit and the server's, which refuses the skill on a button
// otherwise. Other units are left alone.
int HookSkillLevel(void* unit, void* node, int bonus, int flag) noexcept {
	const int level = SkillLevel(unit, node, bonus, flag);
	if (level > 0 || !bonus || !ForClient(unit, node)) return level;
	const int other = OtherSetLevel(unit, node).inHand;
	return other > 0 ? other : level;
}

// The "+skills" part (D2R+0x3400A0), which the "can use this skill" check
// (D2R+0x33F360: red tile, cast refused) adds to the base level itself.
int HookBonusLevel(void* unit, void* node, int flag) noexcept {
	const int bonus = BonusLevel(unit, node, flag);
	int       base  = 0;
	if (!ForClient(unit, node) || !ReadField(node, SkillBaseOffset, base) || base + bonus > 0) return bonus;
	const int other = OtherSetLevel(unit, node).inHand;
	return other > 0 ? other - base : bonus;
}

// What a skill node casts with: its mode, then what that puts in your hands.
std::array<std::uint32_t, 4> WantedFor(void* unit, void* node) noexcept {
	const auto    sets  = SetsNow();
	std::uint32_t owner = NoId;
	void*         txt   = nullptr;
	std::int16_t  id    = -1;
	if (!ReadField(node, SkillOwnerOffset, owner) || !ReadField(node, SkillTxtOffset, txt) || !ReadField(txt, 0, id) || id < 0) return Wanted(sets, Main);
	return Wanted(sets, owner == NoId ? OwnMode(unit, id, sets) : ItemMode(id, SlotOf(sets, owner)));
}

thread_local bool Refused = false; // HookCanUseSkill refused a skill for its mode

// A skill only your set II gives, with a mode that leaves its item out of
// your hands, can't be cast: red, and "I can't" when you try (the game's
// wrong weapon answer). Your other gear can still give it a level.
// A skill that needs an item type (Holy Shield: a shield) is checked by the
// client against the items in your hands now; when its mode puts set II items
// in your hands, the server checks it once they are there.
int HookCanUseSkill(void* unit, void* node) noexcept {
	const int result = CanUseSkill(unit, node);
	if (result >= 2 && result <= WrongWeaponResult && node && Config.keepSwapSkills && unit == ClientPlayer.load(std::memory_order_relaxed)) {
		const auto want = WantedFor(unit, node);
		const auto now  = CurrentItems(unit);
		return want[0] != now[0] || want[1] != now[1] ? 0 : result;
	}
	if (result == 0 && ForClient(unit, node) && Orphan(unit, node, SkillId(node))) return NoLevelResult;
	std::uint32_t owner = NoId;
	if (result == 0 && ForClient(unit, node) && ReadField(node, SkillOwnerOffset, owner) && owner != NoId) {
		// An item's own skill (charges): its mode must put the item in your hands.
		const auto want = WantedFor(unit, node);
		if (SlotOf(SetsNow(), owner) < 0 || want[0] == owner || want[1] == owner) return result;
		Refused = true;
		return WrongWeaponResult;
	}
	int base = 0;
	if ((result != 0 && result != NoLevelResult) || !ForClient(unit, node) || !ReadField(node, SkillBaseOffset, base) || base > 0) return result;
	const auto grant = OtherSetLevel(unit, node);
	if (grant.total <= 0 || grant.inHand > 0) return result;
	if (grant.single && BonusLevel(unit, node, 0) > 0) return result; // +all skills etc. from your armor
	void*        txt = nullptr;
	std::int16_t id  = -1;
	if (!ReadField(node, SkillTxtOffset, txt) || !ReadField(txt, 0, id)) return result;
	if (void* const inventory = UnitInventory(unit, __FILE__, __LINE__))
		for (int slot = 1; slot <= 10; ++slot)
			if (slot != 4 && slot != 5)
				if (void* const item = BodyItem(inventory, slot); item && GrantLevel(item, id) > 0) return result;
	Refused = true;
	return WrongWeaponResult;
}

// Does the unit have a skill node granted by this item?
bool HasItemSkill(void* unit, std::uint32_t item) noexcept {
	void* list = nullptr;
	void* node = nullptr;
	if (!ReadField(unit, UnitSkillsOffset, list) || !list || !ReadField(list, 0, node)) return false;
	for (int guard = 0; node && guard < 1024; ++guard) {
		std::uint32_t owner = NoId;
		if (!ReadField(node, SkillOwnerOffset, owner)) return false;
		if (owner == item) return true;
		if (!ReadField(node, SkillNextOffset, node)) return false;
	}
	return false;
}

// The client adds a "+skill" node once the item's stat reaches your unit,
// which for set II only happens when the set is held after loading has ended
// (the hold made while loading isn't seen). So as soon as your set II items
// are on you, their "+skills" get their node here, the way the game adds it;
// Sweep removes them like the ones the game wanted gone.
struct Looked {
	void*                      player {};
	std::vector<std::uint32_t> items; // set II items already looked at
};
Looked ClientLooked, ServerLooked; // UI thread, game thread

int AddSetTwoSkills(void* player, Looked& looked) noexcept {
	if (!Config.keepSwapSkills || !player) return 0;
	if (player != looked.player) {
		looked.items.clear();
		looked.player = player;
	}
	void* const inventory = UnitInventory(player, __FILE__, __LINE__);
	if (!inventory) return 0;
	std::uint8_t  data   = 0;
	std::uint32_t cls    = 0;
	std::uint64_t skills = 0;
	void*         tables = nullptr;
	if (!ReadField(player, UnitDataIndexOffset, data) || !ReadField(player, UnitClassOffset, cls) || !(tables = DataTables(data)) || !ReadField(tables, SkillCountOffset, skills)) return 0;
	skills = std::min<std::uint64_t>(skills, 4096);
	std::uint32_t shift = 0, mask = 0;
	const bool    charges = ReadField(tables, ChargeShiftOffset, shift) && ReadField(tables, ChargeMaskOffset, mask) && shift > 0 && shift < 16 && mask > 0 && mask < (1u << shift);
	int           added = 0;
	for (const int slot : { 11, 12 }) {
		void* const item = BodyItem(inventory, slot);
		if (!item) continue;
		const std::uint32_t guid = UnitGuid(item);
		if (std::find(looked.items.begin(), looked.items.end(), guid) != looked.items.end()) continue;
		looked.items.push_back(guid);
		for (int skill = 0; skill < static_cast<int>(skills); ++skill) {
			const bool other = GetStat(item, NonClassSkillStat, skill) > 0;
			if (!other && GetStat(item, SingleSkillStat, skill) <= 0) continue;
			if (FindSkill(player, skill, NoId)) continue;
			if (!other) { // +X to a skill of a class: only your class's
				void* const  txt   = SkillTxt(data, skill);
				std::int8_t  owner = -1;
				if (!txt || !ReadField(txt, SkillClassOffset, owner) || owner != static_cast<std::int8_t>(cls)) continue;
			}
			AddSkill(player, skill, 0, 0, __FILE__, __LINE__);
			if (!FindSkill(player, skill, NoId)) continue;
			Keep({ player, NoId, skill, 0 });
			++added;
		}
		// Skills on charges: a node of the item's own, the way the game makes
		// it when the item's charges reach you (D2R+0xFD45B).
		if (!charges) continue;
		for (int skill = 0; skill < static_cast<int>(skills); ++skill)
			for (int level = 1; level <= static_cast<int>(mask); ++level) {
				const int value = GetStat(item, ChargedSkillStat, (skill << shift) | level);
				if (value <= 0 || FindSkill(player, skill, guid)) continue;
				SetItemSkill(player, guid, skill, level, value & 0xFF, 0);
				if (!FindSkill(player, skill, guid)) continue;
				Keep({ player, guid, skill, level });
				++added;
			}
	}
	return added;
}

bool CanMove(void* player) noexcept { return !IsDead(player) && !UsedSkill(player) && !HasState(player, UninterruptableState); }

// Puts the items so that slot i holds want[i]. Returns false if the game
// refused the placement (nothing moved then).
bool Arrange(void* game, void* player, const std::array<std::uint32_t, 4>& current, const std::array<std::uint32_t, 4>& want) noexcept {
	std::array<Loc, 4>           from {}, to {};
	std::array<std::uint32_t, 4> ids {};
	std::size_t                  count = 0;
	for (std::size_t target = 0; target < want.size(); ++target) {
		const std::uint32_t id = want[target];
		if (id == NoId || current[target] == id) continue;
		const auto source = std::find(current.begin(), current.end(), id);
		if (source == current.end()) return false; // not in a weapon slot any more
		from[count].bodyLoc = from[count].bodyLoc32 = Slots[source - current.begin()];
		to[count].bodyLoc = to[count].bodyLoc32 = Slots[target];
		ids[count++] = id;
	}
	if (!count) return true;
	Moving();
	if (!MoveItems(player, from.data(), to.data(), ids.data(), count, 0)) return false;
	for (std::size_t i = 0; i < count; ++i) {
		void* const item = FindUnit(game, ItemType, ids[i]);
		if (!item) continue;
		SetItemFlag(item, 0x4000, 0);
		const std::uint32_t bodyLoc = to[i].bodyLoc32;
		Notify(game, player, item, bodyLoc < 11 ? 0x41 : 0x81, 0x200000, to[i].kind, to[i].page, bodyLoc, bodyLoc);
	}
	Refresh(player, 1);
	return true;
}

// Where the items go for a cast or back to set I.
bool Place(void* game, void* player, const std::array<std::uint32_t, 4>& current, const std::array<std::uint32_t, 4>& want) noexcept {
	return Arrange(game, player, current, want);
}

// sets = set 1 R, L, set 2 R, L. What goes in R, L, other R, other L.
std::array<std::uint32_t, 4> Wanted(const std::array<std::uint32_t, 4>& sets, Mode mode) noexcept {
	const bool right = RightFromSwap(mode), left = LeftFromSwap(mode);
	return { sets[right ? 2 : 0], sets[left ? 3 : 1], sets[right ? 0 : 2], sets[left ? 1 : 3] };
}

bool OnSetTwo(void* player) noexcept {
	void* const client = PlayerClient(player);
	return client && SwitchState(client) != 0;
}

// Puts the items back the way the game sees them. False if they couldn't be
// (the plugin then stops tracking them, the items stay where they are).
bool Restore(const char* why) noexcept {
	if (!Server.tracking) return true;
	const auto current = CurrentItems(Server.player);
	const bool done    = Arrange(Server.game, Server.player, current, Server.natural);
	if (!done) Log(true, "could not put your weapons back (%s); they stay where they are", why);
	else if (Config.logSwaps) Log(false, "weapons put back (%s)", why);
	Server.tracking = false;
	Server.away     = false;
	return done;
}

std::array<std::uint32_t, 4> Follow(void* player, const std::array<std::uint32_t, 4>& current) noexcept;

void __cdecl ServerTick(const D2RL::PluginContext*, void*) noexcept;

void QueueTick() noexcept {
	if (Threads && !TickQueued.exchange(true) && Threads->runOnGameThread(Context, ServerTick, nullptr) != D2RL::Threads::Result::Success) {
		TickQueued.store(false);
		if (Config.logSwaps) Log(false, "could not queue the set I check");
	}
}

// Every game update while the items are off set I: puts set I back once no
// skill has been in use for restore_delay_ms.
void __cdecl ServerTick(const D2RL::PluginContext*, void*) noexcept {
	TickQueued.store(false);
	if (!Server.away || !Server.player) return;
	void* const player = Server.player;
	if (IsDead(player)) {
		Server.away = Server.tracking = false; // items go to the corpse
		Log(true, "you died with moved weapons; your sets are as they were at that moment");
		return;
	}
	const ULONGLONG now = GetTickCount64();
	static ULONGLONG waitLogged = 0;
	if (Config.logSwaps && now > Server.lastBusy + 2000 && now > waitLogged + 2000) {
		waitLogged = now;
		Log(false, "set I still not back after 2 s: skill in use %d, uninterruptible %d", UsedSkill(player) != nullptr, HasState(player, UninterruptableState) != 0);
	}
	if (UsedSkill(player) || HasState(player, UninterruptableState) || now < Server.lastBusy) Server.lastBusy = std::max(Server.lastBusy, now);
	else if (now - Server.lastBusy >= static_cast<ULONGLONG>(Config.restoreMs)) {
		const auto current = CurrentItems(player);
		const auto home    = Wanted(Follow(player, current), Main);
		if (!Place(Server.game, player, current, home)) Log(true, "could not put weapon set I back; the items stay where they are");
		else if (Config.logSwaps) Log(false, "set I back");
		Server.tracking = CurrentItems(player) != Server.natural;
		Server.away     = false;
		return;
	}
	QueueTick();
}

std::vector<std::uint32_t> Primed; // items whose skills the game added this game

void Bind(void* game, void* player) noexcept {
	if (Server.player == player && Server.game == game) return;
	Server        = ServerState {};
	Server.game   = game;
	Server.player = player;
	Primed.clear();
	TickQueued.store(false); // a check queued as the last game ended never ran
}

// Follows the items: natural (the game's view) and the absolute sets.
std::array<std::uint32_t, 4> Follow(void* player, const std::array<std::uint32_t, 4>& current) noexcept {
	if (Server.tracking && std::any_of(Server.natural.begin(), Server.natural.end(), [&](std::uint32_t item) { return item != NoId && SlotOf(current, item) < 0; }))
		Server.tracking = false; // the items changed some other way
	if (!Server.tracking) Server.natural = current;
	const auto&                  natural = Server.natural;
	const bool                   two     = OnSetTwo(player);
	std::array<std::uint32_t, 4> sets    = two ? std::array { natural[2], natural[3], natural[0], natural[1] } : natural;
	for (std::size_t i = 0; i < sets.size(); ++i) Sets[i].store(sets[i]);
	return sets;
}

// Before a cast: puts the skill's weapons in your hands, and leaves them there.
// A skill with no mode casts with set 1.
void BeforeCast(void* game, void* player, void* skill) noexcept {
	if (!game || !player || !skill || !IsLocalPlayer(player)) return;
	void*         txt   = nullptr;
	std::int16_t  id    = -1;
	std::uint32_t owner = 0;
	if (!ReadField(skill, SkillTxtOffset, txt) || !ReadField(txt, 0, id) || !ReadField(skill, SkillOwnerOffset, owner)) return;
	Bind(game, player);
	const auto current = CurrentItems(player);
	const auto sets    = Follow(player, current);
	// A skill an item grants needs the item in hand: its hand is fixed by where
	// the item is, the mode chooses the other one.
	const Mode mode = owner == NoId ? OwnMode(player, id, sets) : ItemMode(id, SlotOf(sets, owner));
	const auto want = Wanted(sets, mode);
	if (want == current) {
		if (Config.logSwaps) Log(false, "skill %d cast with %s (already in hand)", id, Labels[mode]);
		if (Server.away) Server.lastBusy = GetTickCount64();
		return;
	}
	if (!CanMove(player)) return; // mid-cast: this one keeps the current items
	if (!Place(game, player, current, want)) {
		if (Config.logSwaps) Log(false, "skill %d (%s): the game refused this combination, cast with the current weapons", id, Labels[mode]);
		return;
	}
	Server.tracking = want != Server.natural;
	Server.away     = want != Wanted(sets, Main);
	Server.lastBusy = GetTickCount64();
	if (Config.logSwaps) Log(false, "skill %d cast with %s", id, Labels[mode]);
	if (Server.away) QueueTick();
}

int HookUseSkillLoc(void* game, void* player, std::uint32_t x, std::uint32_t y, int flag, void* skill) noexcept {
	BeforeCast(game, player, skill);
	return UseSkillLoc(game, player, x, y, flag, skill);
}

int HookUseSkillUnit(void* game, void* player, std::uint32_t type, std::uint32_t id, void* skill, int a6, int a7) noexcept {
	BeforeCast(game, player, skill);
	return UseSkillUnit(game, player, type, id, skill, a6, a7);
}

// Weapon swap packet: +1 shield, +5 weapon, +9 other shield, +0xD other
// weapon item ids, checked against what you hold. Put the items back the way
// the game sees them first and send the ids of where they are now.
int HookWeaponSwap(void* game, void* player, std::uint8_t* packet, int size) noexcept {
	if (IsLocalPlayer(player)) Moving();
	if (!Server.tracking || player != Server.player || game != Server.game || !packet || size < 0x11) return WeaponSwap(game, player, packet, size);
	if (!CanMove(player)) return WeaponSwap(game, player, packet, size);
	// The set in your weapon hand now vs the one the game thinks you hold: when
	// they differ, putting the game's back already is the swap you see.
	const auto sets    = Follow(player, CurrentItems(player));
	const auto visible = CurrentItems(player);
	const int  hand    = visible[0] != NoId ? SlotOf(sets, visible[0]) : visible[1] != NoId ? SlotOf(sets, visible[1]) - 1 : 0;
	const bool shown   = hand >= 2, held = OnSetTwo(player);
	if (!Restore("weapon swap")) return WeaponSwap(game, player, packet, size);
	if (shown != held) return 0;
	std::vector<std::uint8_t> copy(packet, packet + size);
	const auto                current = CurrentItems(player);
	std::memcpy(&copy[1], &current[1], 4);
	std::memcpy(&copy[5], &current[0], 4);
	std::memcpy(&copy[9], &current[3], 4);
	std::memcpy(&copy[0xD], &current[2], 4);
	return WeaponSwap(game, player, copy.data(), size);
}

// Client packets 0x05-0x0A cast the left skill, 0x0C-0x11 the right one
// (on a location / unit, start or hold), 0x69 leaves the game.
constexpr std::uint8_t LeaveGamePacket = 0x69;

void Settle(void* game, void* player) noexcept;

// Every packet of the local player, before the game handles it.
void ServerPacket(void* game, void* player, std::uint8_t id) noexcept {
	Bind(game, player);
	if (id == LeaveGamePacket) {
		Restore("leaving the game"); // saved the way the game sees them
		return;
	}
	if (id >= 0x05 && id <= 0x11 && id != 0x0B) {
		// Before the game checks the skill: a "+skill" of the other set has
		// no level until its item is in hand.
		BeforeCast(game, player, id < 0x0B ? LeftSkill(player) : RightSkill(player));
		return;
	}
	Settle(game, player);
}

// Reads your sets, and holds a set never held this game once.
void Settle(void* game, void* player) noexcept {
	const auto current = CurrentItems(player);
	(void)Follow(player, current);
	Sweep(player);
	// A set never held this game has no skill nodes yet: hold it once.
	if (!Config.keepSwapSkills || Server.tracking || Server.away) return;
	auto primed = [](std::uint32_t item) { return item == NoId || std::find(Primed.begin(), Primed.end(), item) != Primed.end(); };
	auto mark   = [&](std::uint32_t item) {
		if (!primed(item)) Primed.push_back(item);
	};
	mark(current[0]);
	mark(current[1]);
	bool needed = false;
	for (int slot = 2; slot < 4; ++slot) {
		if (primed(current[slot])) continue;
		if (HasItemSkill(player, current[slot])) mark(current[slot]);
		else needed = true;
	}
	if (!needed || !CanMove(player)) return;
	// Held for a moment: the client only adds the skills once it gets the items'
	// stats, which it doesn't if they come back in the same update.
	if (Place(game, player, current, Wanted(current, Swap))) {
		Server.tracking = CurrentItems(player) != Server.natural;
		Server.away     = true;
		Server.lastBusy = GetTickCount64() + 300;
		QueueTick();
		if (Config.logSwaps) Log(false, "holding your other set a moment to add its skills");
	}
	mark(current[2]);
	mark(current[3]);
}

// The game saves your character (Save and Exit, leaving, autosave): the items
// go back where the game expects them first, so the save has your sets as
// they are.
std::uint64_t HookLeaveSave(void* game, void* player) noexcept {
	if (Server.tracking && player == Server.player) Restore("leaving the game");
	return LeaveSave(game, player);
}

std::atomic<void*> LastGame {}; // the game of the last client packet or loaded character

std::uint64_t HookLoadPlayer(void* game, void* client, void* name, void** out) noexcept {
	const std::uint64_t result = LoadPlayer(game, client, name, out);
	if (game) LastGame.store(game, std::memory_order_relaxed);
	return result;
}

std::uint64_t HookDispatch(void* game, void* player, std::uint8_t* packet, int size) noexcept {
	if (game) LastGame.store(game, std::memory_order_relaxed);
	if (game && player && packet && size > 0 && IsLocalPlayer(player)) ServerPacket(game, player, packet[0]);
	return Dispatch(game, player, packet, size);
}

// Once loading has ended. The packets sent while loading may not be seen as
// yours (the first game after starting D2R), which left your sets unread and
// set II skills red until your first move: read them here.
std::atomic<bool> ResyncQueued {};

void __cdecl Resync(const D2RL::PluginContext*, void*) noexcept {
	ResyncQueued.store(false);
	void* const         game   = LastGame.load(std::memory_order_relaxed);
	const std::uint32_t id     = PlayerId.load(std::memory_order_acquire);
	void* const         player = game && id != NoId ? FindUnit(game, PlayerType, id) : nullptr;
	if (!player) return;
	const bool fresh = Server.player != player || Server.game != game;
	Bind(game, player);
	if (Server.tracking || Server.away) return;
	if (fresh) {
		// No hold now that you're in: the "+skills" of set II get their node
		// here, the way the client's got theirs.
		const int added = AddSetTwoSkills(player, ServerLooked);
		for (const std::uint32_t item : { CurrentItems(player)[2], CurrentItems(player)[3] })
			if (item != NoId && std::find(Primed.begin(), Primed.end(), item) == Primed.end()) Primed.push_back(item);
		Log(false, "weapon sets read after loading, %d skill%s of your other set added", added, added == 1 ? "" : "s");
	}
	Settle(game, player);
}

void QueueResync() noexcept {
	if (Threads && !ResyncQueued.exchange(true) && Threads->runOnGameThread(Context, Resync, nullptr) != D2RL::Threads::Result::Success) ResyncQueued.store(false);
}

// ---- client: labels and the key ----

std::vector<void*> HudButtons; // HUD left/right skill buttons seen, UI thread

bool IsSkillButton(void* widget) noexcept {
	std::uintptr_t vtable = 0;
	return widget && ReadField(widget, 0, vtable) && vtable == SsbwVtable;
}

void* LocalPlayer() noexcept {
	const std::uint32_t index = LocalIndex();
	return index < 8 ? PlayerByIndex(index) : nullptr;
}

int SkillId(void* skill) noexcept {
	void*        txt = nullptr;
	std::int16_t id  = -1;
	return skill && ReadField(skill, SkillTxtOffset, txt) && ReadField(txt, 0, id) ? id : -1;
}

// The item that grants the tile's skill, NoId for your own skill. A HUD button
// filled by skill id only: the item of your left/right skill if it's this one.
std::map<void*, std::pair<int, std::uint32_t>> TileItems; // tile -> skill, granting item (UI thread)

std::uint32_t TileOwner(void* widget, int skill, void* player) noexcept {
	std::uint32_t owner = NoId;
	int           side  = 0;
	if (const auto found = TileItems.find(widget); found != TileItems.end() && found->second.first == skill) return found->second.second;
	(void)ReadField(widget, WidgetOwner, owner);
	if (owner != NoId || !player || !ReadField(widget, WidgetSide, side) || (side != 1 && side != 2)) return owner;
	for (SideSkillFn get : side == 1 ? std::array { LeftSkill, RightSkill } : std::array { RightSkill, LeftSkill }) {
		void* const node = get(player);
		if (SkillId(node) != skill) continue;
		(void)ReadField(node, SkillOwnerOffset, owner);
		break;
	}
	return owner;
}

// What a tile's skill casts with now. Slot is set (>= 0) for an item's skill.
Mode TileMode(void* widget, int skill, int* slot = nullptr) noexcept {
	void* const         player = LocalPlayer();
	const std::uint32_t owner  = TileOwner(widget, skill, player);
	if (slot) *slot = -1;
	if (owner == NoId) return OwnMode(player, skill, SetsNow());
	const int where = SlotOf(SetsNow(), owner);
	if (slot) *slot = where;
	return ItemMode(skill, where);
}

void Label(void* widget) noexcept {
	int   skill = -1, quantity = 0;
	void* text  = nullptr;
	if (!ReadField(widget, WidgetSkill, skill) || skill < 0 || !ReadField(widget, WidgetQuantity, quantity) || quantity > 0) return;
	if (!ReadField(widget, WidgetQuantityText, text) || !text) return;
	std::uintptr_t vtable = 0;
	SetVisibleFn   setVisible {};
	if (!ReadField(text, 0, vtable) || !Read(vtable + 0x50, &setVisible, sizeof(setVisible)) || !setVisible) return;
	const Mode mode = TileMode(widget, skill);
	if (mode == Main && !Config.showMainLabel) {
		setVisible(text, false);
		return;
	}
	std::uint8_t a = 0, b = 0;
	(void)ReadField(text, 0x1F6, a);
	(void)ReadField(text, 0x1F7, b);
	setVisible(text, true);
	Format(reinterpret_cast<std::uint8_t*>(text) + (a && b ? 0x200 : 0x88), "%s", Labels[mode]);
}

void HookSetSkill(void* widget, int skill) noexcept {
	SetSkill(widget, skill);
	TileItems.erase(widget);
	int side = 0;
	if (!ReadField(widget, WidgetSide, side)) return;
	if ((side == 1 || side == 2) && std::find(HudButtons.begin(), HudButtons.end(), widget) == HudButtons.end()) HudButtons.push_back(widget);
	Label(widget);
}

// The skill picker fills its tiles from the skill node; this also hides the
// quantity text, so the label goes in after it.
bool HookSetSkillObject(void* widget, void* skill, std::uint8_t flag) noexcept {
	const bool    filled = SetSkillObject(widget, skill, flag);
	std::uint32_t owner  = NoId;
	if (filled && ReadField(skill, SkillOwnerOffset, owner) && owner != NoId) TileItems[widget] = { SkillId(skill), owner };
	else TileItems.erase(widget);
	Label(widget);
	return filled;
}

void* HoveredWidget() noexcept;

// The skill tooltip gets a line with the skill's weapons: a tile with
// charges shows the charges where the label goes.
constexpr const char* ModeNames[] = { "set I", "set I weapon, set II shield", "set II weapon, set I shield", "set II" };

GameString* HookSkillTip(GameString* out, void* unit, int skill, int level, int a5, int a6) noexcept {
	GameString* const result = SkillTip(out, unit, skill, level, a5, a6);
	void* const       widget = HoveredWidget();
	int               shown  = -1;
	if (!result || !result->text || !IsSkillButton(widget) || !ReadField(widget, WidgetSkill, shown) || shown != skill) return result;
	int        slot = -1;
	const Mode mode = TileMode(widget, skill, &slot);
	if (TileOwner(widget, skill, unit) != NoId && slot < 0) return result; // an item that isn't in a weapon slot
	char line[96];
	const int length = std::snprintf(line, sizeof(line), "Swap Cast: %s (%s)\n\n", Labels[mode], ModeNames[mode]); // the first line is drawn at the bottom; a blank line above it
	if (length <= 0 || static_cast<std::size_t>(length) >= sizeof(line)) return result;
	const std::string text(result->text, result->size);
	StringReserve(result, text.size() + length);
	if (!result->text) return result;
	std::memcpy(result->text, line, length);
	std::memcpy(result->text + length, text.data(), text.size());
	result->text[text.size() + length] = 0;
	result->size = text.size() + length;
	return result;
}

// ---- per-character modes file ----

std::string                                ModesPath;
std::map<std::string, std::map<int, Mode>> Saved;      // character -> skill -> mode
std::map<std::string, std::map<int, Mode>> SavedItems; // the same for skills items grant
std::string                                Character;

std::string Utf8(const wchar_t* text) {
	const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
	std::string out(size > 0 ? size - 1 : 0, '\0');
	if (size > 1) WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
	return out;
}

std::wstring Wide(const std::string& text) {
	const int    size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
	std::wstring out(size > 0 ? size - 1 : 0, L'\0');
	if (size > 1) MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), size);
	return out;
}

// <game>\d2rloader\swap-cast\modes.txt, outside the mod folder D2RMM empties.
void FindModesFile() {
	wchar_t     exe[MAX_PATH] {};
	const DWORD length = GetModuleFileNameW(nullptr, exe, MAX_PATH);
	if (!length || length >= MAX_PATH) return;
	std::string folder = Utf8(exe);
	folder.erase(folder.find_last_of("\\/") + 1);
	folder += "d2rloader\\swap-cast";
	const int made = SHCreateDirectoryExW(nullptr, Wide(folder).c_str(), nullptr);
	if (made != ERROR_SUCCESS && made != ERROR_ALREADY_EXISTS && made != ERROR_FILE_EXISTS) {
		Log(true, "cannot create %s (error %d): modes are not saved", folder.c_str(), made);
		return;
	}
	ModesPath = folder + "\\modes.txt";
}

Mode ParseMode(const std::string& text) noexcept {
	for (int mode = 0; mode < 4; ++mode)
		if (text == Labels[mode]) return static_cast<Mode>(mode);
	return Main;
}

// One line per skill: character<TAB>skill id<TAB>mode; "i" before the id for
// a skill an item grants.
void LoadModes() {
	if (ModesPath.empty()) return;
	FILE* file = _wfopen(Wide(ModesPath).c_str(), L"rb");
	if (!file) return;
	char line[256];
	while (std::fgets(line, sizeof(line), file)) {
		std::string text(line);
		while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
		const auto first = text.find('\t'), second = text.find('\t', first + 1);
		if (first == std::string::npos || second == std::string::npos) continue;
		const std::string id    = text.substr(first + 1, second - first - 1);
		const bool        item  = !id.empty() && id[0] == 'i';
		const int         skill = std::atoi(id.c_str() + (item ? 1 : 0));
		const Mode        mode  = ParseMode(text.substr(second + 1));
		if (skill < 0 || skill >= MaxSkills) continue;
		if (item) SavedItems[text.substr(0, first)][skill] = mode;
		else Saved[text.substr(0, first)][skill] = mode;
	}
	std::fclose(file);
}

void SaveModes() {
	if (ModesPath.empty()) return;
	const std::string temp = ModesPath + ".tmp";
	FILE*             file = _wfopen(Wide(temp).c_str(), L"wb");
	bool              ok   = file != nullptr;
	for (const auto& [name, skills] : Saved)
		for (const auto& [skill, mode] : skills) ok = ok && std::fprintf(file, "%s\t%d\t%s\n", name.c_str(), skill, Labels[mode]) > 0;
	for (const auto& [name, skills] : SavedItems)
		for (const auto& [skill, mode] : skills) ok = ok && std::fprintf(file, "%s\ti%d\t%s\n", name.c_str(), skill, Labels[mode]) > 0;
	if (file) ok = std::fclose(file) == 0 && ok;
	if (!ok) {
		Log(true, "could not write %s", temp.c_str());
		return;
	}
	if (!MoveFileExW(Wide(temp).c_str(), Wide(ModesPath).c_str(), MOVEFILE_REPLACE_EXISTING)) Log(true, "could not replace %s (error %lu)", ModesPath.c_str(), GetLastError());
}

void UseCharacter(const std::string& name) {
	Character = name;
	for (auto& mode : Modes) mode.store(0, std::memory_order_relaxed);
	if (const auto found = Saved.find(name); found != Saved.end())
		for (const auto& [skill, mode] : found->second) Modes[skill].store(mode + 1, std::memory_order_relaxed);
	for (auto& mode : ItemModes) mode.store(0, std::memory_order_relaxed);
	if (const auto found = SavedItems.find(name); found != SavedItems.end())
		for (const auto& [skill, mode] : found->second) ItemModes[skill].store(mode + 1, std::memory_order_relaxed);
}

std::string NameOf(void* player) noexcept {
	void* data = nullptr;
	char  name[32] {};
	if (!ReadField(player, UnitDataOffset, data) || !data || !Read(reinterpret_cast<std::uintptr_t>(data), name, sizeof(name) - 1)) return {};
	name[sizeof(name) - 1] = 0;
	return name;
}

// UI tick: follows the local player (id for the server side, name for modes).
std::atomic<bool> UiQueued {};

void __cdecl UiTick(const D2RL::PluginContext*, void*) noexcept {
	UiQueued.store(false);
	void* const   player = LocalPlayer();
	ClientPlayer.store(player, std::memory_order_relaxed);
	std::uint32_t id     = NoId;
	if (!player || !ReadField(player, UnitIdOffset, id)) {
		if (PlayerId.exchange(NoId) != NoId) HudButtons.clear();
		return;
	}
	if (id != PlayerId.load()) {
		PlayerId.store(id, std::memory_order_release);
		HudButtons.clear();
	}
	const std::string name = NameOf(player);
	if (!name.empty() && name != Character) {
		UseCharacter(name);
		int count = 0;
		if (const auto found = Saved.find(name); found != Saved.end()) count = static_cast<int>(found->second.size());
		if (const auto found = SavedItems.find(name); found != SavedItems.end()) count += static_cast<int>(found->second.size());
		Log(false, "%s: %d skill%s with a weapon mode", name.c_str(), count, count == 1 ? "" : "s");
	}
	Sweep(player);
	if (const int added = AddSetTwoSkills(player, ClientLooked)) Log(false, "added %d skill%s of your other set", added, added == 1 ? "" : "s");
	static void*     seen = nullptr;
	static ULONGLONG seenAt = 0, resynced = 0;
	const ULONGLONG  now = GetTickCount64();
	if (player != seen) {
		seen   = player;
		seenAt = now;
	}
	if (now < seenAt + 5000 && now >= resynced + 250) { // the first seconds in game
		resynced = now;
		QueueResync();
	}
}

std::atomic<bool> Running {};

// ---- "impossible" ----

void* HoveredWidget() noexcept;

bool LocateSpeech() noexcept {
	std::array<std::uint8_t, 15> tail {};
	std::int32_t                 amazon = 0;
	if (!Read(Base + PlaySoundRva + 5, tail.data(), tail.size()) || tail != PlaySoundTail || !Read(Base + ImpossibleRva, &amazon, sizeof(amazon)) || amazon != AmazonImpossible) return false;
	Speak = reinterpret_cast<PlaySoundFn>(Base + PlaySoundRva);
	return true;
}

void SayImpossible(void* player) noexcept {
	static ULONGLONG said = 0;
	const ULONGLONG  now  = GetTickCount64();
	std::uint32_t    cls  = 0;
	std::int32_t     sound = 0;
	if (!Speak || now < said + 1000 || !ReadField(player, UnitClassOffset, cls) || cls >= SoundRow.size() ||
	    !Read(Base + ImpossibleRva + SoundRow[cls] * ImpossibleStride, &sound, sizeof(sound)) || sound <= 0)
		return;
	said = now;
	Speak(sound, player, 0, 0, false);
}

// UI thread, after a click: the skill on that side can't be cast with its
// mode, so the character says "impossible" (the game sends nothing, so it
// stays silent itself).
void __cdecl CheckClick(const D2RL::PluginContext*, void* side) noexcept {
	void* const player = LocalPlayer();
	if (!player || !Speak) return;
	void* const node = side ? RightSkill(player) : LeftSkill(player);
	if (!node) return;
	Refused = false;
	(void)HookCanUseSkill(player, node);
	const bool refused = Refused;
	Refused            = false;
	if (refused) SayImpossible(player);
}

// The client's "can I use this skill" step (D2R+0x21B170, wraps CanUseSkill):
// called to draw tiles and to cast. A cast its mode refuses says "impossible"
// (quick cast too, which no click shows).
constexpr std::array<std::uint64_t, 8> CastCallers { 0xF17B4, 0x1007AC, 0x1007FB, 0x10089A, 0x1008E9, 0x1029C8, 0x102A16, 0x14B9D7 }; // return addresses (0x14B9D7: quick cast)
int HookTryCast(void* unit, void* node) noexcept {
	Refused         = false;
	const int result = TryCast(unit, node);
	const bool refused = Refused;
	Refused            = false;
	if (!refused) return result;
	const std::uint64_t from = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0)) - Base;
	const bool          cast = std::find(CastCallers.begin(), CastCallers.end(), from) != CastCallers.end();
	if (cast) SayImpossible(unit);
	return result;
}

// Mouse presses while the game window is in front: left = left skill, right =
// right skill.
DWORD WINAPI WatchClicks(void*) noexcept {
	bool down[2] {};
	while (Running.load()) {
		DWORD pid = 0;
		const HWND front = GetForegroundWindow();
		const bool game  = front && GetWindowThreadProcessId(front, &pid) && pid == GetCurrentProcessId();
		for (int side = 0; side < 2; ++side) {
			const bool pressed = game && (GetAsyncKeyState(side ? VK_RBUTTON : VK_LBUTTON) & 0x8000) != 0;
			if (pressed && !down[side]) (void)Threads->runOnUiThread(Context, CheckClick, reinterpret_cast<void*>(static_cast<std::uintptr_t>(side)));
			down[side] = pressed;
		}
		Sleep(15);
	}
	return 0;
}

DWORD WINAPI Poll(void*) noexcept {
	while (Running.load()) {
		if (!UiQueued.exchange(true) && Threads->runOnUiThread(Context, UiTick, nullptr) != D2RL::Threads::Result::Success) UiQueued.store(false);
		Sleep(250);
	}
	return 0;
}

void* HoveredWidget() noexcept {
	void* root = nullptr;
	void* ui   = nullptr;
	void* hovered = nullptr;
	if (!Read(UiRootGlobal, &root, sizeof(root)) || !root || !ReadField(root, 0xD0, ui) || !ui || !ReadField(ui, UiHovered, hovered)) return nullptr;
	return hovered;
}

void __cdecl CycleHovered(const D2RL::PluginContext*, void*) noexcept {
	void* const widget = HoveredWidget();
	int         skill  = -1;
	if (!IsSkillButton(widget) || !ReadField(widget, WidgetSkill, skill) || skill < 0 || skill >= MaxSkills || Character.empty()) return;
	int        slot    = -1;
	const Mode current = TileMode(widget, skill, &slot);
	const bool item    = TileOwner(widget, skill, LocalPlayer()) != NoId;
	if (item && slot < 0) return; // an item that isn't in a weapon slot
	Mode mode = Main;
	if (item) {
		mode = Next(current);
		ItemModes[skill].store(mode + 1, std::memory_order_relaxed);
		SavedItems[Character][skill] = mode;
	} else {
		mode = Next(current);
		Modes[skill].store(mode + 1, std::memory_order_relaxed);
		Saved[Character][skill] = mode;
	}
	SaveModes();
	Label(widget);
	for (void* hud : HudButtons)
		if (IsSkillButton(hud)) Label(hud);
	Log(false, "%s: %sskill %d -> %s", Character.c_str(), item ? "item " : "", skill, Labels[mode]);
}

auto OnCycleKey(const D2RL::PluginContext*, const D2RL::Input::ActionEvent* event, void*) noexcept -> D2RL::Input::ActionResult {
	if (!D2RL::Input::HasActionEventField(event, D2RL::Input::ActionEventRequiredSize) || event->kind != D2RL::Input::ActionEventKind::Pressed)
		return D2RL::Input::ActionResult::Ignored;
	void* const hovered = HoveredWidget();
	if (!IsSkillButton(hovered)) return D2RL::Input::ActionResult::Ignored;
	(void)Threads->runOnUiThread(Context, CycleHovered, nullptr);
	return D2RL::Input::ActionResult::Handled;
}

// ---- version check ----

bool Locate() noexcept {
	struct Check {
		std::uint64_t rva;
		const Code*   bytes;
	};
	const std::array<Check, 33> checks { {
		{ StringReserveRva, &StringReserveBytes },
		{ AddSkillRva, &AddSkillBytes },
		{ SkillTxtRva, &SkillTxtBytes },
		{ DataTablesRva, &DataTablesBytes },
		{ RemoveSkillRva, &RemoveSkillBytes },
		{ FindSkillRva, &FindSkillBytes },
		{ PlayerClientRva, &PlayerClientBytes },
		{ SwitchStateRva, &SwitchStateBytes },
		{ SetSkillObjectRva, &SetSkillObjectBytes },
		{ SetItemSkillRva, &SetItemSkillBytes },
		{ DispatchRva, &DispatchBytes },
		{ LeftSkillRva, &SideSkillBytes },
		{ RightSkillRva, &SideSkillBytes },
		{ UseSkillLocRva, &UseSkillLocBytes },
		{ UseSkillUnitRva, &UseSkillUnitBytes },
		{ WeaponSwapRva, &WeaponSwapBytes },
		{ SideSkillOkRva, &SideSkillOkBytes },
		{ SetSideSkillRva, &SetSideSkillBytes },
		{ SendSideSkillRva, &SendSideSkillBytes },
		{ SetSkillRva, &SetSkillBytes },
		{ MoveItemsRva, &MoveItemsBytes },
		{ NotifyRva, &NotifyBytes },
		{ SetItemFlagRva, &SetItemFlagBytes },
		{ RefreshRva, &RefreshBytes },
		{ FindUnitRva, &FindUnitBytes },
		{ UnitInventoryRva, &UnitInventoryBytes },
		{ BodyItemRva, &BodyItemBytes },
		{ UnitGuidRva, &GuardBytes },
		{ IsDeadRva, &GuardBytes },
		{ UsedSkillRva, &UsedSkillBytes },
		{ HasStateRva, &HasStateBytes },
		{ FormatRva, &FormatBytes },
		{ SsbwCtorRva, &SsbwCtorBytes },
	} };
	for (const auto& check : checks) {
		if (!CodeMatches(check.rva, *check.bytes)) {
			Log(true, "game code differs at D2R+0x%llX", static_cast<unsigned long long>(check.rva));
			return false;
		}
	}
	if (!CodeMatches(PlayerByIndexRva, PlayerByIndexBytes)) {
		Log(true, "game code differs at D2R+0x%llX", static_cast<unsigned long long>(PlayerByIndexRva));
		return false;
	}
	std::array<std::uint8_t, 7> local {}, root {}, ctor {};
	if (!Read(Base + LocalIndexRva, local.data(), local.size()) || local[0] != 0x8B || local[1] != 0x05 || local[6] != 0xC3 ||
	    !Read(Base + UiRootRva + 4, root.data(), root.size()) || root[0] != 0x48 || root[1] != 0x8B || root[2] != 0x05 ||
	    !Read(Base + SsbwCtorRva + 0x1B, ctor.data(), ctor.size()) || ctor[0] != 0x48 || ctor[1] != 0x8D || ctor[2] != 0x05) {
		Log(true, "game code differs around D2R+0x%llX / 0x%llX / 0x%llX", static_cast<unsigned long long>(LocalIndexRva), static_cast<unsigned long long>(UiRootRva),
		    static_cast<unsigned long long>(SsbwCtorRva));
		return false;
	}
	UiRootGlobal  = RipTarget(Base + UiRootRva + 7, Base + UiRootRva + 11);
	SsbwVtable    = RipTarget(Base + SsbwCtorRva + 0x1E, Base + SsbwCtorRva + 0x22);
	MoveItems     = reinterpret_cast<MoveItemsFn>(Base + MoveItemsRva);
	Notify        = reinterpret_cast<NotifyFn>(Base + NotifyRva);
	SetItemFlag   = reinterpret_cast<SetItemFlagFn>(Base + SetItemFlagRva);
	Refresh       = reinterpret_cast<RefreshFn>(Base + RefreshRva);
	FindUnit      = reinterpret_cast<FindUnitFn>(Base + FindUnitRva);
	UnitInventory = reinterpret_cast<UnitInventoryFn>(Base + UnitInventoryRva);
	BodyItem      = reinterpret_cast<BodyItemFn>(Base + BodyItemRva);
	UnitGuid      = reinterpret_cast<UnitGuidFn>(Base + UnitGuidRva);
	IsDead        = reinterpret_cast<IsDeadFn>(Base + IsDeadRva);
	UsedSkill     = reinterpret_cast<UsedSkillFn>(Base + UsedSkillRva);
	HasState      = reinterpret_cast<HasStateFn>(Base + HasStateRva);
	Format        = reinterpret_cast<FormatFn>(Base + FormatRva);
	PlayerByIndex = reinterpret_cast<PlayerByIndexFn>(Base + PlayerByIndexRva);
	LocalIndex    = reinterpret_cast<LocalIndexFn>(Base + LocalIndexRva);
	LeftSkill     = reinterpret_cast<SideSkillFn>(Base + LeftSkillRva);
	SideSkillOk   = reinterpret_cast<SideSkillOkFn>(Base + SideSkillOkRva);
	SetSideSkill  = reinterpret_cast<SetSideSkillFn>(Base + SetSideSkillRva);
	SendSideSkill = reinterpret_cast<SendSideSkillFn>(Base + SendSideSkillRva);
	RightSkill    = reinterpret_cast<SideSkillFn>(Base + RightSkillRva);
	FindSkill     = reinterpret_cast<FindSkillFn>(Base + FindSkillRva);
	AddSkill      = reinterpret_cast<AddSkillFn>(Base + AddSkillRva);
	StringReserve = reinterpret_cast<StringReserveFn>(Base + StringReserveRva);
	SkillTxt      = reinterpret_cast<SkillTxtFn>(Base + SkillTxtRva);
	DataTables    = reinterpret_cast<DataTablesFn>(Base + DataTablesRva);
	SkillLevel    = reinterpret_cast<SkillLevelFn>(Base + SkillLevelRva);
	BonusLevel    = reinterpret_cast<BonusLevelFn>(Base + BonusLevelRva);
	GetStat       = reinterpret_cast<GetStatFn>(Base + GetStatRva);
	PlayerClient  = reinterpret_cast<PlayerClientFn>(Base + PlayerClientRva);
	SwitchState   = reinterpret_cast<SwitchStateFn>(Base + SwitchStateRva);
	return UiRootGlobal && SsbwVtable;
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
	const std::string_view text(buffer.data(), std::strlen(buffer.data()));
	std::size_t            lineStart = 0;
	while (lineStart < text.size()) {
		auto lineEnd = text.find('\n', lineStart);
		if (lineEnd == std::string_view::npos) lineEnd = text.size();
		const std::string line = Trim(text.substr(lineStart, lineEnd - lineStart));
		lineStart              = lineEnd + 1;
		const auto equals      = line.find('=');
		if (line.empty() || line[0] == '#' || line[0] == '[' || equals == std::string::npos) continue;
		const std::string key   = Trim(std::string_view(line).substr(0, equals));
		const std::string value = Unquote(Trim(std::string_view(line).substr(equals + 1)));
		if (key == "enabled") Config.enabled = value != "false";
		else if (key == "show_m_label") Config.showMainLabel = value == "true";
		else if (key == "allow_cross_cast") Config.allowCross = value != "false";
		else if (key == "restore_delay_ms") {
			const int ms = std::atoi(value.c_str());
			if (ms >= 0 && ms <= 5000) Config.restoreMs = ms;
			else Log(true, "config: restore_delay_ms %s ignored (0-5000)", value.c_str());
		} else if (key == "log_swaps") Config.logSwaps = value == "true";
		else if (key == "show_swap_skills") Config.keepSwapSkills = value != "false";
		else Log(true, "config: unknown key %s", key.c_str());
	}
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

// The skill button's level check goes to HookButtonLevel through a jump put in
// int3 padding inside D2R.exe (a relative call can only reach the exe).
bool RedirectButtonLevel() noexcept {
	std::array<std::uint8_t, 14> jump { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
	const auto                   target = reinterpret_cast<std::uint64_t>(&HookButtonLevel);
	std::memcpy(jump.data() + 6, &target, sizeof(target));
	std::array<std::uint8_t, 16> padding {};
	padding.fill(0xCC);
	std::array<std::uint8_t, 0x10000> chunk {};
	int                               tries = 0;
	for (std::uint64_t start = 0x1000; start < 0x1400000 && tries < 16; start += chunk.size()) {
		if (!Read(Base + start, chunk.data(), chunk.size())) continue;
		for (std::size_t at = 0; at + padding.size() <= chunk.size() && tries < 16; at += 16) {
			if (std::memcmp(chunk.data() + at, padding.data(), padding.size()) != 0) continue;
			++tries;
			const std::uint64_t cave = start + at;
			if (!Context->PatchBytes(cave + 1, padding.data(), 14, jump.data(), 14)) continue; // another plugin took it
			if (D2RL::PatchCallRel32(Context, ButtonLevelCallRva, ButtonLevelCallBytes.data(), 5, cave + 1)) return true;
			Log(true, "could not redirect the skill button's level check at D2R+0x%llX", static_cast<unsigned long long>(ButtonLevelCallRva));
			return false;
		}
	}
	Log(true, "no free padding found for the skill button's level check");
	return false;
}

template <typename Fn, std::size_t Size>
bool Hook(std::uint64_t rva, const std::array<std::uint8_t, Size>& bytes, Fn detour, Fn* original) {
	if (Context->InstallInlineHook(rva, bytes.data(), static_cast<std::uint32_t>(bytes.size()), detour, original) && *original) return true;
	Log(true, "could not hook D2R+0x%llX (another plugin may hook it)", static_cast<unsigned long long>(rva));
	return false;
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
		Log(false, "Swap Cast %s: disabled in config", PluginVersion);
		return true;
	}
	Base = context->exeBase;
	if (!Base || !Locate()) {
		Log(true, "Swap Cast %s: inactive, this game build isn't supported (Steam 3.3.93787 / 3.3.94995 are)", PluginVersion);
		return true;
	}
	Threads           = Query<D2RL::ThreadService>("thread");
	const auto* input = Query<D2RL::InputService>("input");
	if (!D2RL::HasThreadServiceField(Threads, D2RL::ThreadServiceRequiredSize) || !D2RL::HasInputServiceField(input, D2RL::InputServiceRequiredSize)) {
		Log(true, "Swap Cast %s: inactive, this D2RLoader lacks the thread or input service", PluginVersion);
		return true;
	}
	const D2RL::Input::ActionRegistration action {
		.structSize       = D2RL::Input::ActionRegistrationSize,
		.flags            = 0,
		.logicalId        = "cycle-mode",
		.displayName      = "Swap Cast: change skill weapons",
		.category         = "",
		.defaultPrimary   = { .key = D2RL::Input::Key::F10 },
		.defaultSecondary = { .key = D2RL::Input::Key::None },
		.callback         = OnCycleKey,
		.userData         = nullptr,
	};
	D2RL::Input::ActionHandle handle = D2RL::Input::InvalidHandle;
	if (const auto result = input->registerAction(context, &action, &handle); result != D2RL::Input::Result::Success)
		Log(true, "could not register the key (result %u): modes can't be changed in game", static_cast<unsigned>(result));
	if (!Hook(UseSkillLocRva, UseSkillLocBytes, &HookUseSkillLoc, &UseSkillLoc) || !Hook(UseSkillUnitRva, UseSkillUnitBytes, &HookUseSkillUnit, &UseSkillUnit) ||
	    !Hook(WeaponSwapRva, WeaponSwapBytes, &HookWeaponSwap, &WeaponSwap) || !Hook(DispatchRva, DispatchBytes, &HookDispatch, &Dispatch)) {
		Log(true, "Swap Cast %s: inactive, the cast hooks failed", PluginVersion);
		return true;
	}
	if (!Hook(LoadPlayerRva, LoadPlayerBytes, &HookLoadPlayer, &LoadPlayer))
		Log(true, "on the first game after starting D2R, set II skills may stay red until you move (hook failed)");
	if (!Hook(LeaveSaveRva, LeaveSaveBytes, &HookLeaveSave, &LeaveSave))
		Log(true, "quitting right after a swap cast may save your weapons moved (hook failed)");
	if (!Hook(SkillTipRva, SkillTipBytes, &HookSkillTip, &SkillTip)) Log(true, "skill tooltips show no weapon line");
	if (!Hook(SetSkillRva, SetSkillBytes, &HookSetSkill, &SetSkill) | !Hook(SetSkillObjectRva, SetSkillObjectBytes, &HookSetSkillObject, &SetSkillObject))
		Log(true, "some skill tiles show no labels");
	if (Config.keepSwapSkills && (!Hook(SetItemSkillRva, SetItemSkillBytes, &HookSetItemSkill, &SetItemSkill) ||
	                              !Hook(RemoveSkillRva, RemoveSkillBytes, &HookRemoveSkill, &RemoveSkill) || !RedirectButtonLevel())) {
		Log(true, "your other set's item skills are not kept (hook failed)");
		Config.keepSwapSkills = false;
	}
	if (Config.keepSwapSkills && (!Hook(SkillLevelRva, SkillLevelBytes, &HookSkillLevel, &SkillLevel) | !Hook(BonusLevelRva, BonusLevelBytes, &HookBonusLevel, &BonusLevel)))
		Log(true, "class skills only your set II gives may show red on set I (hook failed)");
	if (Config.keepSwapSkills && !Hook(CanUseSkillRva, CanUseSkillBytes, &HookCanUseSkill, &CanUseSkill))
		Log(true, "skills only your set II gives are not red when their mode leaves the item out of your hands (hook failed)");
	else if (!LocateSpeech() || !Hook(TryCastRva, TryCastBytes, &HookTryCast, &TryCast))
		Log(true, "game code differs: no \"impossible\" when a skill's mode can't cast it");
	FindModesFile();
	LoadModes();
	Running.store(true);
	if (HANDLE poller = CreateThread(nullptr, 0, Poll, nullptr, 0, nullptr)) CloseHandle(poller);
	else Running.store(false);
	if (Speak) {
		if (HANDLE clicks = CreateThread(nullptr, 0, WatchClicks, nullptr, 0, nullptr)) CloseHandle(clicks);
	}
	Log(false, "Swap Cast %s: active, modes in %s", PluginVersion, ModesPath.empty() ? "(not saved)" : ModesPath.c_str());
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	Running.store(false);
}
