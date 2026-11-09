// Merc Gear Models: a merc's look follows its gear.
//
// A merc is a monster. Its look is 16 component bytes rolled when it is
// created (MonsterData +0x08, an index into each slot's choice list in
// monstats2), never its inventory. Players get their component codes from
// their equipped items instead. Both go through one function, so the plugin
// hooks it and, for mercs, picks the code from the merc's gear, limited to
// the choices that merc already has (monstats2 lists, which are also the
// variants in its HD json, so no piece can go missing).
//
// Game code used (Steam 3.3.93787 / 94995 RVAs, client and server):
//   - 0x195200 GetComponentCode(unit, unitType, classId, mode, slot) returns
//     a 4-char code ("lit ", "spr "...). Slots: 0 HD, 1 TR, 2 LG, 3 RA, 4 LA,
//     5 RH, 6 LH, 7 SH, 8 S1, 9 S2. Hooked.
//   - 0x34a360 GetInventory(unit, file, line).
//   - 0x3886d0 GetBodyLocItem(inventory, bodyloc): 1 head, 3 torso, 4 right arm.
//   - 0x370be0 ItemGfxCode(item): items record alternategfx, else code.
//   - 0x34a0e0 data bank of a unit, 0x314110 ItemRecord(bank, classId); the
//     record's +0x125..+0x12A bytes are armor.txt rArm, lArm, Torso, Legs,
//     rSPad, lSPad (0 lit, 1 med, 2 hvy). The player path reads them the same
//     way (0x194540, slot to byte map in 0x3b6d10).
//   - The HD layer builds each unit's part state from these codes (0x76b5b0)
//     and rebuilds the model when it changes (0x76e110, monsters included).
//   - Real item models: the merc HD definitions (data/hd/character/enemy/
//     <merc>.json) only have a few looks. The plugin registers its own copies
//     (the game's file and the tables tools/gen_defs.py writes
//     into merc_adds.inc; see merc_build.h) with one extra entity per
//     helm and per weapon the merc can use, each attaching that item's HD json
//     (items.json, uniques.json, sets.json), keyed by variant = item code,
//     "u<id>" for a unique or "s<id>" for a set item. 0x6631a0 builds a unit's
//     definition path as "data/hd/character/enemy/" + name + ".json", the name
//     coming from 0x662440 (monsters.json). Its call at 0x6633ea is sent to the
//     plugin, which answers "d2rloader/merc-gear-models/<merc>" for mercs.
//     Item: ItemData = unit +0x10, quality +0x00 (5 set, 7 unique), file +0x34;
//     items record +0x80 code.

#include <D2RLPlugin/api.h>
#include <D2RLPlugin/resources.h>
#include "i_hate_tomls_client.h"
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

#include "merc_adds.inc"
#include "merc_build.h"

constexpr const char* PluginId      = "merc-gear-models";
constexpr const char* PluginVersion = "1.0.0";

const D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = PluginId,
	.name        = "Merc Gear Models",
	.version     = PluginVersion,
	.author      = "wild-healer",
	.description = "Your merc's look follows its armor, helm and weapon.",
	.flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

struct Settings {
	bool enabled    = true;
	bool logChanges = true;
	// Per merc group (Rogue, Desert merc, Iron Wolf, Barbarian): weapon, helm, armor, enchant (item colors on the weapon).
	bool  parts[4][4] { { true, true, true, true }, { true, true, true, true }, { true, true, true, true }, { true, true, true, true } };
} Config;
enum Part { Weapon = 0, Helm = 1, Armor = 2, Enchant = 3 };
// Heavy armor look per merc group (Donors index): Rogue amazon (whole), Desert merc and Iron Wolf barbarian pieces,
// Barbarian barbarian (whole).
constexpr int Heavy[4] { 1, 3, 3, 3 };
constexpr const char* MercRows[4] { "rogue", "desert", "iron_wolf", "barbarian" };
constexpr const char* PartColumns[4] { "weapon", "helm", "armor", "enchant" };
// Desert merc: polearm/spear moved along its shaft (more = held closer to the end).
constexpr float PolearmGrip = 2.2f;

// ---- native layout (Steam 3.3.93787 / 94995) ----

constexpr std::uint64_t ComponentCodeRva = 0x195200;
constexpr std::array<std::uint8_t, 24> ComponentCodeBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89,
	                                                        0x74, 0x24, 0x20, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x30 };
constexpr std::uint64_t GetInventoryRva = 0x34A360;
constexpr std::array<std::uint8_t, 16> GetInventoryBytes { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF1, 0x48, 0x85, 0xC9 };
constexpr std::uint64_t BodyLocItemRva = 0x3886D0;
constexpr std::array<std::uint8_t, 16> BodyLocItemBytes { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x63, 0xFA, 0x48, 0x8B, 0xD9 };
constexpr std::uint64_t ItemGfxCodeRva = 0x370BE0;
constexpr std::array<std::uint8_t, 16> ItemGfxCodeBytes { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF9, 0x48, 0x85, 0xC9 };
constexpr std::uint64_t UnitBankRva = 0x34A0E0;
constexpr std::array<std::uint8_t, 16> UnitBankBytes { 0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1A, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C };
constexpr std::uint64_t ItemRecordRva = 0x314110;
constexpr std::array<std::uint8_t, 8> ItemRecordBytes { 0x40, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x8B, 0xFA };
constexpr std::uintptr_t ArmorPiecesOffset = 0x125;
constexpr std::uintptr_t ItemCodeOffset    = 0x80;
constexpr std::uintptr_t UnitDataOffset    = 0x10;
constexpr std::uintptr_t ItemQualityOffset = 0x00;
constexpr std::uintptr_t ItemFileOffset    = 0x34;
// In 0x6631a0: mov r9d,[rsp+30]; mov rdx,r15; mov r8d,[rsp+34]; mov rcx,r14; call 0x662440
constexpr std::uint64_t DefNameCallRva = 0x6633EA;
constexpr std::array<std::uint8_t, 21> DefNameSiteBytes { 0x44, 0x8B, 0x4C, 0x24, 0x30, 0x49, 0x8B, 0xD7, 0x44, 0x8B, 0x44,
	                                                      0x24, 0x34, 0x49, 0x8B, 0xCE, 0xE8, 0x51, 0xF0, 0xFF, 0xFF };
constexpr std::uint64_t DefNameRva = 0x662440; // rArm, lArm, Torso, Legs, rSPad, lSPad

constexpr std::uintptr_t UnitTypeOffset  = 0x00;
constexpr std::uintptr_t UnitClassOffset = 0x04;
constexpr std::uintptr_t UnitIdOffset    = 0x08;

constexpr int BodyLocHead = 1, BodyLocTorso = 3, BodyLocRightArm = 4, BodyLocLeftArm = 5;
enum Slot : unsigned { HD = 0, TR = 1, LG = 2, RA = 3, LA = 4, RH = 5, LH = 6, SH = 7, S1 = 8, S2 = 9 };
constexpr const char* SlotNames[16] { "head", "torso", "legs", "right arm", "left arm", "right hand", "left hand", "shield",
	                                  "special1", "special2", "s3", "s4", "s5", "s6", "s7", "s8" };

using ComponentCodeFn = std::uint32_t (*)(void* unit, int unitType, int classId, int mode, unsigned char slot) noexcept;
using GetInventoryFn  = void* (*)(void* unit, const char* file, int line) noexcept;
using BodyLocItemFn   = void* (*)(void* inventory, int bodyLoc) noexcept;
using ItemGfxCodeFn   = std::uint32_t (*)(void* item) noexcept;
using UnitBankFn      = std::uint8_t (*)(void* unit) noexcept;
using ItemRecordFn    = const std::uint8_t* (*)(std::uint8_t bank, int classId) noexcept;
using DefNameFn       = const char* (*)(void* mapper, void* unit, int unitType, int classId) noexcept;

const D2RL::PluginContext* Context {};
D2RL::Lifecycle::ListenerHandle TablesListener {};
std::uintptr_t             Base {};
ComponentCodeFn            OriginalComponentCode {};
GetInventoryFn             GetInventory {};
BodyLocItemFn              BodyLocItem {};
ItemGfxCodeFn              ItemGfxCode {};
UnitBankFn                 UnitBank {};
ItemRecordFn               ItemRecord {};
DefNameFn                  DefName {};
bool                       DefsActive = false;
std::array<std::string, std::size(MercNames)> DefNames;
std::array<std::string, std::size(MercNames)> DefTexts;

void Log(bool warn, const char* format, ...) noexcept {
	if (!Context) return;
	char    line[512] {};
	va_list args;
	va_start(args, format);
	std::vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	warn ? Context->LogWarn(line) : Context->LogInfo(line);
}

// Reads the game's memory after checking it is committed and readable (ReadProcessMemory made one antivirus
// heuristic call the DLL an injector). VirtualQuery is slow on the game's big heap regions and the draw hooks
// read every frame, so each thread remembers the last readable regions it checked for a quarter second.
struct ReadableRegion {
	std::uintptr_t begin = 0, end = 0;
	std::uint64_t  until = 0;
};
thread_local std::array<ReadableRegion, 16> ReadableRegions {};
thread_local std::size_t                    NextRegion = 0;

bool Read(std::uintptr_t address, void* output, std::size_t size) noexcept {
	if (address == 0 || address + size < address) return false;
	const std::uint64_t now = GetTickCount64();
	for (const auto& region : ReadableRegions)
		if (address >= region.begin && address + size <= region.end && now < region.until) {
			std::memcpy(output, reinterpret_cast<const void*>(address), size);
			return true;
		}
	MEMORY_BASIC_INFORMATION info {};
	if (VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)) != sizeof(info) || info.State != MEM_COMMIT) return false;
	constexpr DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
	if (!(info.Protect & readable) || (info.Protect & PAGE_GUARD)) return false;
	const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(info.BaseAddress), end = begin + info.RegionSize;
	if (address + size > end) return false;
	ReadableRegions[NextRegion] = { begin, end, now + 250 };
	NextRegion                  = (NextRegion + 1) % ReadableRegions.size();
	std::memcpy(output, reinterpret_cast<const void*>(address), size);
	return true;
}

template <typename T>
bool ReadField(const void* base, std::uintptr_t offset, T& value) noexcept {
	return base && Read(reinterpret_cast<std::uintptr_t>(base) + offset, &value, sizeof(T));
}

template <std::size_t Size>
bool CodeMatches(std::uint64_t rva, const std::array<std::uint8_t, Size>& bytes) noexcept {
	std::array<std::uint8_t, Size> code {};
	return Read(Base + rva, code.data(), code.size()) && std::memcmp(code.data(), bytes.data(), code.size()) == 0;
}

constexpr std::uint32_t Code(const char (&text)[4]) noexcept {
	return static_cast<std::uint8_t>(text[0]) | static_cast<std::uint8_t>(text[1]) << 8 | static_cast<std::uint8_t>(text[2]) << 16 | 0x20u << 24;
}

std::string CodeText(std::uint32_t code) {
	std::string text;
	for (int i = 0; i < 4; ++i) {
		const char c = static_cast<char>((code >> (8 * i)) & 0xFF);
		if (c > ' ' && c < 127) text += c;
	}
	return text.empty() ? "-" : text;
}

// ---- what each merc can show (its monstats2 lists, same as its HD json) ----

constexpr std::uint32_t Lit = Code("lit"), Med = Code("med"), Hvy = Code("hvy");

struct MercLooks {
	std::uint32_t classId;
	const char*   name;
	std::uint32_t weights[16]; // how many of lit/med/hvy each body slot has (0 or 1 = leave alone)
	bool          helmLitMed;  // head lit (no helm) / med (helm)
	bool          helmFullGreat; // head fhm / ghm
	bool          bowShortLong; // left hand sbw / lbw
	bool          spearPolearm; // right hand spr / glv
	int           group;        // settings group: 0 Rogue, 1 Desert merc, 2 Iron Wolf, 3 Barbarian
};

// Heavy armor donors: player class body parts, variant 'H' + 2 letters (tools/gen_defs.py DONORS).
struct Donor { const char* value; const char* label; const char* variant; };
// Classes whose body is modelled in another rest pose (T-pose) are re-posed at load (see Repose).
constexpr Donor Donors[] { { "own", "Own look", "" }, { "amazon", "Amazon", "Ham" }, { "assassin", "Assassin", "Has" },
	                       { "barbarian", "Barbarian", "Hba" }, { "druid", "Druid", "Hdr" }, { "necromancer", "Necromancer", "Hne" },
	                       { "paladin", "Paladin", "Hpa" }, { "sorceress", "Sorceress", "Hso" }, { "warlock", "Warlock", "Hwa" } };
constexpr const char* HeavyKeys[4] { "heavy_rogue", "heavy_desert", "heavy_iron_wolf", "heavy_barbarian" };

// Slot order: HD TR LG RA LA RH LH SH S1 S2 ...
constexpr std::array<MercLooks, 5> Mercs { {
	{ 271, "Rogue", { 0, 0, 0, 0, 0, 0, 0, 0, 2, 2 }, true, false, true, false, 0 },
	{ 338, "Desert merc", { 0, 2, 2, 2, 2, 0, 0, 0, 0, 0 }, true, false, false, true, 1 },
	{ 359, "Iron Wolf", {}, false, false, false, false, 2 },
	{ 560, "Barbarian", { 0, 3, 0, 0, 0, 0, 0, 0, 2, 2 }, false, true, false, false, 3 },
	{ 561, "Barbarian", { 0, 3, 0, 0, 0, 0, 0, 0, 2, 2 }, false, true, false, false, 3 },
} };

const MercLooks* FindMerc(int classId) noexcept {
	for (const auto& merc : Mercs)
		if (merc.classId == static_cast<std::uint32_t>(classId)) return &merc;
	return nullptr;
}

bool CodeIn(std::uint32_t code, std::initializer_list<std::uint32_t> codes) noexcept {
	for (const auto c : codes)
		if (c == code) return true;
	return false;
}

// Armor piece byte per slot, as the player path maps it (0x3b6d10).
int ArmorPieceIndex(unsigned slot) noexcept {
	switch (slot) {
	case TR: return 2;
	case LG: return 3;
	case RA: return 0;
	case LA: return 1;
	case S1: return 4;
	case S2: return 5;
	default: return -1;
	}
}

std::uint32_t VariantCode(const char* text) noexcept {
	std::uint32_t code = 0;
	bool          end  = false;
	for (int i = 0; i < 4; ++i) {
		if (!text[i]) end = true;
		code |= static_cast<std::uint32_t>(end ? 0x20u : static_cast<std::uint8_t>(text[i])) << (8 * i);
	}
	return code;
}

bool HasVariant(std::uint32_t classId, unsigned slot, std::uint32_t code) noexcept {
	for (const auto& v : Variants)
		if (v.classId == classId && v.slot == slot && VariantCode(v.variant) == code) return true;
	return false;
}

// The item-model variant for an equipped item: its unique or set entry if the
// merc definition has one, else its base item. 0 = none.
std::uint32_t ItemVariant(void* item, std::uint32_t classId, unsigned slot) noexcept {
	void*         data    = nullptr;
	std::uint32_t quality = 0, file = 0, itemClass = 0;
	if (ReadField(item, UnitDataOffset, data) && data && ReadField(data, ItemQualityOffset, quality) && ReadField(data, ItemFileOffset, file) &&
	    (quality == 5 || quality == 7) && file < 0x1000) {
		// Same naming as tools/gen_defs.py: '#' unique / '$' set + the id in two base 36 digits.
		constexpr char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
		const char     text[5] { quality == 7 ? '#' : '$', file < 1296 ? digits[file / 36] : '?', digits[file % 36], 0, 0 };
		if (const auto code = VariantCode(text); HasVariant(classId, slot, code)) return code;
	}
	if (!ReadField(item, UnitClassOffset, itemClass)) return 0;
	const std::uint8_t* record = ItemRecord(UnitBank(item), static_cast<int>(itemClass));
	std::uint32_t       code   = 0;
	if (!record || !ReadField(record, ItemCodeOffset, code)) return 0;
	return HasVariant(classId, slot, code) ? code : 0;
}

// The Iron Wolf's cap is part of his outfit mesh: hidden while he wears a helm (see the cap section).
std::atomic<bool> IronWolfHelm { false };
void SetIronWolfHelm(bool worn) noexcept { IronWolfHelm = worn; }

// 0 = leave the game's own code.
std::uint32_t GearCode(void* unit, const MercLooks& merc, unsigned slot) noexcept {
	const bool* parts = Config.parts[merc.group];
	if (slot == HD && merc.group == 2 && !parts[Helm]) SetIronWolfHelm(false);
	if (slot == HD ? !parts[Helm] : (slot == RH || slot == LH || slot == SH) ? !parts[Weapon] : !parts[Armor]) return 0;
	void* inventory = GetInventory(unit, "merc-gear-models", 0);
	if (!inventory) return 0;

	// Borrowed heavy look: a heavy torso piece swaps the whole body (torso, legs, arms) to the donor's,
	// so the parts always match (mixing donor and own parts leaves gaps, like bare wrists).
	if (const int donor = Heavy[merc.group]; DefsActive && donor > 0 && donor < static_cast<int>(std::size(Donors)) &&
	                                                (slot == TR || slot == LG || slot == RA || slot == LA)) {
		std::uint8_t torsoWeight = 0;
		if (void* armor = BodyLocItem(inventory, BodyLocTorso)) {
			std::uint32_t armorClass = 0;
			if (ReadField(armor, UnitClassOffset, armorClass))
				if (const std::uint8_t* record = ItemRecord(UnitBank(armor), static_cast<int>(armorClass)))
					(void)ReadField(record, ArmorPiecesOffset + ArmorPieceIndex(TR), torsoWeight);
		}
		if (torsoWeight >= 2)
			if (const auto code = VariantCode(Donors[donor].variant); HasVariant(merc.classId, slot, code)) return code;
	}

	const auto count = merc.weights[slot];
	if (count >= 2) {
		const int piece = ArmorPieceIndex(slot);
		if (piece < 0) return 0;
		int   weight = 0;
		void* armor  = BodyLocItem(inventory, BodyLocTorso);
		std::uint32_t armorClass = 0;
		if (armor && ReadField(armor, UnitClassOffset, armorClass)) {
			const std::uint8_t* record = ItemRecord(UnitBank(armor), static_cast<int>(armorClass));
			std::uint8_t        value  = 0;
			if (record && ReadField(record, ArmorPiecesOffset + piece, value)) weight = value;
		}
		if (weight >= static_cast<int>(count)) weight = static_cast<int>(count) - 1;
		return weight == 0 ? Lit : weight == 1 ? Med : Hvy;
	}

	if (DefsActive && slot == HD) {
		void* helm = BodyLocItem(inventory, BodyLocHead);
		if (merc.group == 2) SetIronWolfHelm(helm != nullptr);
		if (!helm) return HasVariant(merc.classId, HD, Lit) ? Lit : 0;
		if (const auto code = ItemVariant(helm, merc.classId, HD)) return code;
	}
	// Left hand of the Barbarian (second weapon) and the Iron Wolf's shield: the left arm item, nothing drawn when empty.
	if (DefsActive && ((slot == LH && merc.group == 3) || slot == SH) && HasVariant(merc.classId, slot, Code("nil"))) {
		void* item = BodyLocItem(inventory, BodyLocLeftArm);
		if (!item) return Code("nil");
		if (const auto code = ItemVariant(item, merc.classId, slot)) return code;
		return 0;
	}
	if (DefsActive && (slot == RH || slot == LH)) {
		if (void* weapon = BodyLocItem(inventory, BodyLocRightArm))
			if (const auto code = ItemVariant(weapon, merc.classId, slot)) return code;
	}

	if (slot == HD && (merc.helmLitMed || merc.helmFullGreat)) {
		void*               helm = BodyLocItem(inventory, BodyLocHead);
		const std::uint32_t gfx  = helm ? ItemGfxCode(helm) : 0;
		if (merc.helmLitMed) return helm ? Med : Lit;
		const bool great = CodeIn(gfx, { Code("ghm"), Code("crn"), Code("bhm"), Code("ba1"), Code("ba2"), Code("ba3"), Code("ba4"), Code("ba5") });
		return great ? Code("ghm") : Code("fhm");
	}

	if ((slot == LH && merc.bowShortLong) || (slot == RH && merc.spearPolearm)) {
		void* weapon = BodyLocItem(inventory, BodyLocRightArm);
		if (!weapon) return 0;
		const std::uint32_t gfx = ItemGfxCode(weapon);
		if (merc.bowShortLong) {
			if (CodeIn(gfx, { Code("sbw"), Code("sbb") })) return Code("sbw");
			if (CodeIn(gfx, { Code("lbw"), Code("lbb"), Code("am1"), Code("am2") })) return Code("lbw");
			return 0;
		}
		if (CodeIn(gfx, { Code("spr"), Code("tri"), Code("brn"), Code("pik"), Code("am3") })) return Code("spr");
		if (CodeIn(gfx, { Code("glv"), Code("hal"), Code("pax"), Code("scy"), Code("bar"), Code("vou"), Code("wsc") })) return Code("glv");
	}
	return 0;
}

// ---- change log ----

std::mutex                                       LogLock;
std::unordered_map<std::uint64_t, std::uint32_t> LastCodes;

void NoteCode(void* unit, const MercLooks& merc, unsigned slot, std::uint32_t code) noexcept {
	std::uint32_t id = 0;
	if (!ReadField(unit, UnitIdOffset, id)) return;
	const std::uint64_t         key = (static_cast<std::uint64_t>(id) << 8) | slot;
	std::lock_guard<std::mutex> lock(LogLock);
	auto [it, added] = LastCodes.try_emplace(key, code);
	if (!added && it->second == code) return;
	const std::uint32_t before = added ? 0 : it->second;
	it->second                 = code;
	if (LastCodes.size() > 512) LastCodes.clear();
	Log(false, "%s %u %s: %s -> %s", merc.name, id, SlotNames[slot], CodeText(before).c_str(), CodeText(code).c_str());
}

// ---- helm bone ----
// Granny FindBoneByName(skeleton, name, int* index) at 0x16183c0 (case-insensitive). Skeleton: +0 name, +8 count, +0xC bones (0xA4 each, +0 name).

using FindBoneFn = bool (*)(const std::uint8_t* skeleton, const char* name, int* index) noexcept;
constexpr std::uint64_t FindBoneRva = 0x16183C0;
constexpr std::array<std::uint8_t, 15> FindBoneBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20 };
FindBoneFn                                  OriginalFindBone {};
std::mutex                                  BoneLock;
std::unordered_map<const void*, int>        SkeletonsDumped;

void DumpSkeleton(const std::uint8_t* skeleton, const char* skelName) noexcept {
	int count = 0;
	std::memcpy(&count, skeleton + 8, sizeof(count));
	const std::uint8_t* bones = nullptr;
	std::memcpy(&bones, skeleton + 0xC, sizeof(bones));
	if (count <= 0 || count > 512 || !bones) return;
	std::string line;
	int         part = 1;
	for (int i = 0; i < count; ++i) {
		const char* bone = nullptr;
		std::memcpy(&bone, bones + static_cast<std::size_t>(i) * 0xA4, sizeof(bone));
		if (!line.empty()) line += ", ";
		line += bone ? bone : "?";
		if (line.size() > 380 || i + 1 == count) {
			Log(false, "bones of %s (%d, part %d): %s", skelName, count, part++, line.c_str());
			line.clear();
		}
	}
}

// Helms on mercs attach to the sentinel bone "mgl_helm": each merc skeleton names its head bone differently
// (Desert merc head_attach, act 5 helmet, act 3 head_bind_jnt, Rogue head_main_00_M_JNT), so take the first one it has.
constexpr const char* HelmBones[] { "head_attach", "helmet", "head_bind_jnt", "head_main_00_M_JNT", "head0" };

bool FindBone(const std::uint8_t* skeleton, const char* name, int* index) noexcept {
	if (!skeleton || !name || std::strcmp(name, "mgl_helm") != 0) return OriginalFindBone(skeleton, name, index);
	for (const char* bone : HelmBones)
		if (OriginalFindBone(skeleton, bone, index)) return true;
	const char* skelName = nullptr;
	std::memcpy(&skelName, skeleton, sizeof(skelName));
	std::lock_guard<std::mutex> lock(BoneLock);
	if (SkeletonsDumped.size() < 16 && SkeletonsDumped.try_emplace(skeleton, 0).second) {
		Log(true, "helm: no head bone found in this merc skeleton, its bones are:");
		DumpSkeleton(skeleton, skelName ? skelName : "?");
	}
	if (index) *index = -1;
	return false;
}

// ---- mesh binding: bones a borrowed body part can't find on the merc skeleton ----
// Granny mesh binding looks each mesh bone up by exact name at 0x1618340 (call 0x1638a4a).
constexpr std::uint64_t BindBoneRva = 0x1618340;
FindBoneFn                            OriginalBindBone {};
std::unordered_map<std::string, int>  BindMisses;

// A donor bone the merc skeleton lacks (a Paladin's hip plates, belt...) is bound to the nearest merc bone
// guessed from its name. Only on humanoid skeletons (they have a pelvis bone), so other meshes are left alone.
constexpr const char* PelvisBones[] { "pelvis_bind_jnt", "root_main_00_M_JNT", "pelvis0" };
constexpr const char* SpineBones[] { "spine_03_bind_jnt", "spine_main_02_M_JNT", "C_Spine_spine3", "spine3_bind_jnt" };
constexpr const char* NeckBones[] { "neck_01_bind_jnt", "neck_bind_jnt", "neck_main_00_M_JNT", "C_Neck_neck1" };
constexpr const char* LegBones[2][4] { { "L_leg_01_bind_jnt", "leg_hip_00_L_JNT", "L_leg1_bind_jnt", "L_Thigh" },
	                                   { "R_leg_01_bind_jnt", "leg_hip_00_R_JNT", "R_leg1_bind_jnt", "R_Thigh" } };
constexpr const char* KneeBones[2][4] { { "L_leg_02_bind_jnt", "leg_knee_00_L_JNT", "L_leg2_bind_jnt", "L_Calf" },
	                                    { "R_leg_02_bind_jnt", "leg_knee_00_R_JNT", "R_leg2_bind_jnt", "R_Calf" } };
constexpr const char* UpperArmBones[2][4] { { "L_upr_arm_bind_jnt", "arm_shoulder_00_L_JNT", "L_shoulder_bind_jnt", "L_Arm_UpperArm" },
	                                        { "R_upr_arm_bind_jnt", "arm_shoulder_00_R_JNT", "R_shoulder_bind_jnt", "R_Arm_UpperArm" } };
constexpr const char* LowerArmBones[2][4] { { "L_lwr_arm_bind_jnt", "arm_elbow_00_L_JNT", "L_elbow_bind_jnt", "L_Arm_ForeArm" },
	                                        { "R_lwr_arm_bind_jnt", "arm_elbow_00_R_JNT", "R_elbow_bind_jnt", "R_Arm_ForeArm" } };
constexpr const char* ShoulderBones[2][3] { { "L_shoulder_bind_jnt", "clavicle_main_00_L_JNT", "L_upr_arm_bind_jnt" },
	                                       { "R_shoulder_bind_jnt", "clavicle_main_00_R_JNT", "R_upr_arm_bind_jnt" } };
constexpr const char* FootBones[2][4] { { "L_leg_03_bind_jnt", "leg_ankle_00_L_JNT", "L_leg3_bind_jnt", "L_leg_02_bind_jnt" },
	                                    { "R_leg_03_bind_jnt", "leg_ankle_00_R_JNT", "R_leg3_bind_jnt", "R_leg_02_bind_jnt" } };
constexpr const char* ToeBones[2][4] { { "L_leg_04_bind_jnt", "leg_ball_00_L_JNT", "L_leg4_bind_jnt", "L_leg_03_bind_jnt" },
	                                   { "R_leg_04_bind_jnt", "leg_ball_00_R_JNT", "R_leg4_bind_jnt", "R_leg_03_bind_jnt" } };
constexpr const char* HeadBones[] { "head_bind_jnt", "head_main_00_M_JNT", "head0", "neck_bind_jnt" };
constexpr const char* HandBones[2][3] { { "L_hand_bind_jnt", "hand_main_00_L_JNT", "L_Arm_Wrist" },
	                                    { "R_hand_bind_jnt", "hand_main_00_R_JNT", "R_Arm_Wrist" } };

template <std::size_t N>
bool FirstBone(const std::uint8_t* skeleton, const char* const (&names)[N], int* index, const char*& used) noexcept {
	for (const char* bone : names)
		if (OriginalBindBone(skeleton, bone, index)) return used = bone, true;
	return false;
}

bool Has(const std::string& text, const char* part) noexcept { return text.find(part) != std::string::npos; }

bool AliasBone(const std::uint8_t* skeleton, const char* name, int* index, const char*& used) noexcept {
	int probe = 0;
	if (!FirstBone(skeleton, PelvisBones, &probe, used)) return false; // not a humanoid skeleton
	std::string n;
	for (const char* c = name; *c && n.size() < 128; ++c) n += static_cast<char>(std::tolower(static_cast<unsigned char>(*c)));
	// Same bone in the merc's naming: "spine_01" -> "spine_01_bind_jnt".
	{
		const std::string bound = std::string(name) + "_bind_jnt";
		if (OriginalBindBone(skeleton, bound.c_str(), index)) {
			static thread_local char last[160];
			std::snprintf(last, sizeof(last), "%s", bound.c_str());
			return used = last, true;
		}
	}
	const auto ends = [&](const char* tail) { const std::size_t len = std::strlen(tail); return n.size() > len && n.compare(n.size() - len, len, tail) == 0; };
	int side = -1;
	if (n.rfind("l_", 0) == 0 || Has(n, "_l_") || ends("_l") || ends("_l_jnt") || Has(n, "left")) side = 0;
	else if (n.rfind("r_", 0) == 0 || Has(n, "_r_") || ends("_r") || ends("_r_jnt") || Has(n, "right")) side = 1;
	if (side >= 0) {
		// numbered leg chains ("L_leg_02_bind_jnt", twists too): 01 hip, 02 knee, 03 ankle, 04 ball
		if (Has(n, "leg_02")) return FirstBone(skeleton, KneeBones[side], index, used);
		if (Has(n, "leg_03")) return FirstBone(skeleton, FootBones[side], index, used);
		if (Has(n, "leg_04")) return FirstBone(skeleton, ToeBones[side], index, used);
		if (Has(n, "fauld") || Has(n, "skirt") || Has(n, "tasset") || Has(n, "thigh") || Has(n, "hip") || Has(n, "cloth"))
			return FirstBone(skeleton, LegBones[side], index, used);
		if (Has(n, "ball") || Has(n, "toe")) return FirstBone(skeleton, ToeBones[side], index, used);
		if (Has(n, "foot") || Has(n, "ankle") || Has(n, "heel") || Has(n, "boot")) return FirstBone(skeleton, FootBones[side], index, used);
		if (Has(n, "clav")) return FirstBone(skeleton, ShoulderBones[side], index, used);
		// elbow cover ("HVY_couter_L_JNT", child of the Assassin's shoulder): rides the upper arm
		if (Has(n, "couter")) return FirstBone(skeleton, UpperArmBones[side], index, used);
		// forearm armor ("MED_wristArmorPlate"): it rides the forearm, not the hand
		if (Has(n, "wristarmor") || Has(n, "armorplate")) return FirstBone(skeleton, LowerArmBones[side], index, used);
		if (Has(n, "hand") || Has(n, "wrist") || Has(n, "finger") || Has(n, "thumb") || Has(n, "glove") || Has(n, "cuff"))
			return FirstBone(skeleton, HandBones[side], index, used);
		if (Has(n, "lwr_arm") || Has(n, "forearm") || Has(n, "elbow") || Has(n, "bracer") || Has(n, "sleeve"))
			return FirstBone(skeleton, LowerArmBones[side], index, used);
		// before "pad": knee pads and their straps go on the knee, not the shoulder
		if (Has(n, "knee") || Has(n, "calf") || Has(n, "shin") || Has(n, "greave"))
			return FirstBone(skeleton, KneeBones[side], index, used);
		if (Has(n, "shoulder") || Has(n, "pad") || Has(n, "pauldron") || Has(n, "arm") || Has(n, "bicep") || Has(n, "tricep"))
			return FirstBone(skeleton, UpperArmBones[side], index, used);
		if (Has(n, "leg")) return FirstBone(skeleton, LegBones[side], index, used);
	}
	if (n == "head" || Has(n, "skull") || Has(n, "jaw")) return FirstBone(skeleton, HeadBones, index, used);
	if (Has(n, "neck") || Has(n, "head") || Has(n, "hair") || Has(n, "hood") || Has(n, "collar")) return FirstBone(skeleton, NeckBones, index, used);
	if (Has(n, "spine") || Has(n, "chest") || Has(n, "torso") || Has(n, "cape") || Has(n, "cloak") || Has(n, "back") || Has(n, "strap") ||
	    Has(n, "scarf") || Has(n, "breast"))
		return FirstBone(skeleton, SpineBones, index, used);
	return FirstBone(skeleton, PelvisBones, index, used); // belt, pouch, loincloth, anything else
}

bool BindBone(const std::uint8_t* skeleton, const char* name, int* index) noexcept {
	if (OriginalBindBone(skeleton, name, index)) return true;
	if (!skeleton || !name) return false;
	int count = 0;
	std::memcpy(&count, skeleton + 8, sizeof(count));
	const char* used  = nullptr;
	const bool  found = count >= 60 && AliasBone(skeleton, name, index, used);
	if (count >= 60 && Config.logChanges) {
		std::lock_guard<std::mutex> lock(BoneLock);
		if (BindMisses.size() < 300 && BindMisses.try_emplace(std::to_string(count) + "|" + name, 0).second) {
			if (found) Log(false, "binding: skeleton with %d bones has no bone %s, using %s", count, name, used);
			else Log(false, "binding: skeleton with %d bones has no bone %s", count, name);
		}
	}
	return found;
}

// ---- rest pose matching for borrowed bodies ----
// A borrowed body part's mesh is bound straight onto the merc skeleton: 0xf92700 makes a Granny mesh binding
// (wrapper 0x1616680 -> 0x1635580, mesh, fromSkeleton = toSkeleton = merc). Each frame the game builds the skinning
// matrices with BuildIndexedCompositeBufferTransposed (thunk 0x1614da0 -> 0x163a6c0: skeleton, world pose,
// binding's bone indices, count, 3x4 buffer) as merc World x merc InverseWorld. So the mesh keeps the pose it was
// modelled in: a body modelled in a T-pose stays in a T-pose. The plugin loads each class skeleton
// (data/hd/character/player/<class>/skeleton/<class>.skeleton) and, for a binding whose mesh comes from one of them,
// swings each class bone so it points like its merc bone at rest; every skinning matrix of that binding is then
// multiplied by the swing of its bone, so the mesh sits in the merc's rest pose before the merc animation moves it.
// Granny layouts: skeleton +0 name, +8 count, +0xC bones (0xA4: +0 name, +8 parent, +0x50 InverseWorld4x4 row-major,
// row vectors); mesh +0x30 bone binding count, +0x34 bindings (0x2C, +0 name); binding +0 mesh, +0x1C to skeleton,
// +0x24 to bone indices; file_info +0x30 skeleton count, +0x34 skeletons, +0x60 model count, +0x64 models (+8 skeleton).

struct V3 { float x, y, z; };
struct Q4 { float x, y, z, w; };

V3 Sub(V3 a, V3 b) noexcept { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
V3 Add(V3 a, V3 b) noexcept { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
float Dot(V3 a, V3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 Cross(V3 a, V3 b) noexcept { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
float Len(V3 a) noexcept { return std::sqrt(Dot(a, a)); }
Q4 Mul(Q4 a, Q4 b) noexcept {
	return { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
		     a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
}
Q4 Norm(Q4 q) noexcept {
	const float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
	return l > 1e-8f ? Q4 { q.x / l, q.y / l, q.z / l, q.w / l } : Q4 { 0, 0, 0, 1 };
}
V3 Rot(Q4 q, V3 v) noexcept {
	const V3 u { q.x, q.y, q.z };
	const V3 t = Cross(u, v);
	const V3 t2 { 2 * t.x, 2 * t.y, 2 * t.z };
	const V3 c = Cross(u, t2);
	return { v.x + q.w * t2.x + c.x, v.y + q.w * t2.y + c.y, v.z + q.w * t2.z + c.z };
}
// Shortest rotation taking direction a onto direction b.
Q4 FromTo(V3 a, V3 b) noexcept {
	const float la = Len(a), lb = Len(b);
	if (la < 1e-6f || lb < 1e-6f) return { 0, 0, 0, 1 };
	a = { a.x / la, a.y / la, a.z / la };
	b = { b.x / lb, b.y / lb, b.z / lb };
	const float d = Dot(a, b);
	if (d < -0.9999f) { // opposite: turn around any perpendicular axis
		V3 axis = Cross({ 1, 0, 0 }, a);
		if (Len(axis) < 1e-3f) axis = Cross({ 0, 1, 0 }, a);
		const float l = Len(axis);
		return { axis.x / l, axis.y / l, axis.z / l, 0 };
	}
	const V3 c = Cross(a, b);
	return Norm({ c.x, c.y, c.z, 1 + d });
}

struct Bones {
	const std::uint8_t* skeleton = nullptr;
	int                 count    = 0;
	const std::uint8_t* bones    = nullptr;
	const char* Name(int i) const noexcept { const char* n = nullptr; std::memcpy(&n, bones + i * 0xA4, sizeof(n)); return n ? n : ""; }
	int Parent(int i) const noexcept { int p = -1; std::memcpy(&p, bones + i * 0xA4 + 8, sizeof(p)); return p; }
	// Rest position in model space, from the bone's InverseWorld4x4 (row vectors: v * InverseWorld = bone local).
	V3 RestPos(int i) const noexcept {
		float m[16];
		std::memcpy(m, bones + i * 0xA4 + 0x50, sizeof(m));
		// InverseWorld = [A 0; t 1] -> World translation = -t * inverse(A)
		const float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], k = m[10];
		const float det = a * (e * k - f * h) - b * (d * k - f * g) + c * (d * h - e * g);
		if (std::fabs(det) < 1e-12f) return { 0, 0, 0 };
		const float inv[9] { (e * k - f * h) / det, (c * h - b * k) / det, (b * f - c * e) / det,
			                 (f * g - d * k) / det, (a * k - c * g) / det, (c * d - a * f) / det,
			                 (d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det };
		const float tx = -m[12], ty = -m[13], tz = -m[14];
		return { tx * inv[0] + ty * inv[3] + tz * inv[6], tx * inv[1] + ty * inv[4] + tz * inv[7], tx * inv[2] + ty * inv[5] + tz * inv[8] };
	}
};

bool ReadSkeleton(const std::uint8_t* skeleton, Bones& out) noexcept {
	if (!skeleton) return false;
	out.skeleton = skeleton;
	if (!Read(reinterpret_cast<std::uintptr_t>(skeleton) + 8, &out.count, sizeof(out.count)) ||
	    !Read(reinterpret_cast<std::uintptr_t>(skeleton) + 0xC, &out.bones, sizeof(out.bones)))
		return false;
	if (out.count <= 0 || out.count > 1024 || !out.bones) return false;
	std::uint8_t probe = 0;
	if (!Read(reinterpret_cast<std::uintptr_t>(out.bones) + static_cast<std::uintptr_t>(out.count) * 0xA4 - 1, &probe, 1)) return false;
	for (int i = 0; i < out.count; ++i)
		if (out.Parent(i) >= i) return false; // parents first, as Granny stores them
	return true;
}

// ---- class skeletons ----
// Read the way the game reads its json files: its resource manager (0x9F3540) loads the file (0x9F3DA0), so an
// active mod's copy wins. Then Granny ReadEntireFileFromMemory (0x16167d0) and GetFileInfo (0x1615b60).
namespace GameLoader {
constexpr std::uint64_t Manager = 0x9F3540, Load = 0x9F3DA0;
constexpr std::array<std::uint8_t, 13> ManagerBytes { 0x48, 0x83, 0xEC, 0x38, 0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00 };
constexpr std::array<std::uint8_t, 14> LoadBytes { 0x40, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B, 0xFA, 0x48, 0x8D, 0x54, 0x24, 0x20 };
constexpr std::uint64_t ReadGrannyRva = 0x16167D0, FileInfoRva = 0x1615B60;
constexpr std::array<std::uint8_t, 5> ReadGrannyBytes { 0xE9, 0xDB, 0xBE, 0x01, 0x00 };
constexpr std::array<std::uint8_t, 9> FileInfoBytes { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B, 0xD9 };
} // namespace GameLoader

struct SharedRef {
	unsigned char* object {};
	unsigned char* control {}; // vtable, strong count at +8, weak count at +0xC
};
struct alignas(16) LoadRequest {
	unsigned char bytes[0x180] {};
};
using ManagerFn    = void* (*)() noexcept;
using LoadFn       = SharedRef* (*)(void* manager, SharedRef* out, LoadRequest* request, int flags) noexcept;
using ReleaseFn    = void (*)(void* control) noexcept;
using ReadGrannyFn = void* (*)(int size, const void* memory) noexcept;
using FileInfoFn   = const std::uint8_t* (*)(void* file) noexcept;

std::string GameFile(const char* path) {
	using namespace GameLoader;
	if (!CodeMatches(Manager, ManagerBytes) || !CodeMatches(Load, LoadBytes)) return {};
	void* manager = reinterpret_cast<ManagerFn>(Base + Manager)();
	const std::size_t length = std::strlen(path);
	if (!manager || length >= 0x9A) return {};
	LoadRequest request;
	const auto  put = [&](std::size_t offset, std::uint64_t value) { std::memcpy(request.bytes + offset, &value, sizeof(value)); };
	std::memcpy(request.bytes + 0x18, path, length);
	put(0, reinterpret_cast<std::uint64_t>(request.bytes + 0x18));
	put(8, length);
	put(0x10, 0x800000000000009AULL);
	request.bytes[0xB8] = 0x64;
	put(0xC0, 1);
	put(0x108, 1);
	SharedRef ref;
	reinterpret_cast<LoadFn>(Base + Load)(manager, &ref, &request, 0);
	std::string bytes;
	if (ref.object) {
		std::uint32_t state = 0;
		std::memcpy(&state, ref.object, 4);
		const char*   data = nullptr;
		std::uint64_t size = 0;
		std::memcpy(&data, ref.object + 8, 8);
		std::memcpy(&size, ref.object + 0x10, 8);
		if (state == 3 && data && size > 0 && size < (64u << 20)) bytes.assign(data, size);
	}
	if (ref.control) {
		auto* strong = reinterpret_cast<volatile long*>(ref.control + 8);
		auto* weak   = reinterpret_cast<volatile long*>(ref.control + 0xC);
		auto* table  = *reinterpret_cast<ReleaseFn* const*>(ref.control);
		if (InterlockedDecrement(strong) == 0) {
			table[2](ref.control);
			if (InterlockedDecrement(weak) == 0) table[3](ref.control);
		}
	}
	return bytes;
}

struct ClassSkeleton {
	const char*  name;
	bool         tpose;          // rigged in a T-pose: arms and legs swung down to the merc's
	bool         snap;           // other proportions: joints moved onto the merc's joints
	bool         keepClavicle;   // clavicles not moved (the merc's "shoulder" joint sits at the arm, so moving
	                             // the Assassin's clavicle there pushes her shoulder pads out)
	bool         scale;          // smaller body: scaled up to the merc's height (neck to feet), so the helm sits on it
	std::string  bytes;          // kept alive for Granny
	Bones        bones;
	bool         ready = false;
};
ClassSkeleton             ClassSkeletons[] { { "amazon", false, false, false, true }, { "assassin", true, true, true, false },
	                                         { "barbarian", true, false, false, false }, { "druid", false, false, false, false },
	                                         { "necromancer", true, false, false, false }, { "paladin", false, false, false, false },
	                                         { "sorceress", true, true, false, false }, { "warlock", true, false, false, false } };
std::atomic<bool>         SkeletonsLoaded { false };

const std::uint8_t* SkeletonOf(const std::uint8_t* info) noexcept {
	int                 count = 0;
	const std::uint8_t* list  = nullptr;
	const std::uint8_t* first = nullptr;
	Bones               probe;
	if (Read(reinterpret_cast<std::uintptr_t>(info) + 0x30, &count, 4) && count > 0 && count < 16 &&
	    Read(reinterpret_cast<std::uintptr_t>(info) + 0x34, &list, 8) && list && Read(reinterpret_cast<std::uintptr_t>(list), &first, 8) &&
	    ReadSkeleton(first, probe))
		return first;
	const std::uint8_t* model = nullptr;
	if (Read(reinterpret_cast<std::uintptr_t>(info) + 0x60, &count, 4) && count > 0 && count < 16 &&
	    Read(reinterpret_cast<std::uintptr_t>(info) + 0x64, &list, 8) && list && Read(reinterpret_cast<std::uintptr_t>(list), &model, 8) && model &&
	    Read(reinterpret_cast<std::uintptr_t>(model) + 8, &first, 8) && ReadSkeleton(first, probe))
		return first;
	return nullptr;
}

void LoadClassSkeletons() noexcept {
	if (SkeletonsLoaded.load()) return;
	using namespace GameLoader;
	if (!CodeMatches(ReadGrannyRva, ReadGrannyBytes) || !CodeMatches(FileInfoRva, FileInfoBytes)) {
		Log(true, "borrowed bodies keep their own pose: the game's Granny file functions aren't the ones this version knows");
		SkeletonsLoaded = true;
		return;
	}
	int loaded = 0;
	try {
		for (auto& entry : ClassSkeletons) {
			const std::string path = std::string("data/hd/character/player/") + entry.name + "/skeleton/" + entry.name + ".skeleton";
			entry.bytes            = GameFile(path.c_str());
			if (entry.bytes.empty()) {
				Log(true, "could not read %s", path.c_str());
				continue;
			}
			void* file = reinterpret_cast<ReadGrannyFn>(Base + ReadGrannyRva)(static_cast<int>(entry.bytes.size()), entry.bytes.data());
			const std::uint8_t* info = file ? reinterpret_cast<FileInfoFn>(Base + FileInfoRva)(file) : nullptr;
			const std::uint8_t* skel = info ? SkeletonOf(info) : nullptr;
			if (!skel || !ReadSkeleton(skel, entry.bones)) {
				Log(true, "no skeleton in %s", path.c_str());
				continue;
			}
			entry.ready = true;
			++loaded;
		}
	} catch (...) {
	}
	SkeletonsLoaded = true;
	Log(false, "class skeletons for borrowed bodies: %d of %zu loaded", loaded, std::size(ClassSkeletons));
}

// ---- skinning correction ----
struct Correction {
	const std::uint8_t* binding  = nullptr;
	const std::uint8_t* mesh     = nullptr;
	const std::uint8_t* skeleton = nullptr;
	bool                hidden = false;
	std::vector<float>  matrices; // per bone binding, 3x4 rows (column vectors): rotation | translation
};
using NewBindingFn  = std::uint8_t* (*)(const std::uint8_t* mesh, const std::uint8_t* from, const std::uint8_t* to) noexcept;
using FreeBindingFn = void (*)(std::uint8_t* binding) noexcept;
using CompositeFn   = void (*)(const std::uint8_t* skeleton, const void* worldPose, const int* indices, int count, float* buffer) noexcept;
constexpr std::uint64_t NewBindingRva = 0x1616680, FreeBindingRva = 0x16155E0, CompositeRva = 0x1614DA0;
constexpr std::array<std::uint8_t, 15> NewBindingBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x40 };
constexpr std::array<std::uint8_t, 9>  FreeBindingBytes { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B, 0xD9 };
constexpr std::array<std::uint8_t, 16> CompositeBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57 };
NewBindingFn                                     OriginalNewBinding {};
FreeBindingFn                                    OriginalFreeBinding {};
CompositeFn                                      OriginalComposite {};
std::mutex                                       CorrectionLock;
std::unordered_map<const void*, int>             HiddenBindings; // bindings whose mesh is hidden
std::atomic<std::size_t>                         HiddenCount { 0 };
std::unordered_map<const int*, Correction>       Corrections; // by the binding's bone index array
std::atomic<std::size_t>                         CorrectionCount { 0 };
int                                              CorrectionsLogged = 0;

bool Chosen(const char* cls) noexcept {
	for (const int donor : Heavy)
		if (donor > 0 && donor < static_cast<int>(std::size(Donors)) && std::strcmp(Donors[donor].value, cls) == 0) return true;
	return false;
}

bool ArmorPiece(const char* meshName) {
	std::string n(meshName);
	for (char& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	for (const char* piece : { "tabard", "plate", "string", "belt", "buckle", "waistband", "skirt", "separated", "torso_armor" })
		if (n.find(piece) != std::string::npos) return true;
	return false;
}

int ExactBone(const Bones& skel, const char* name) noexcept {
	int index = -1;
	return OriginalBindBone(skel.skeleton, name, &index) ? index : -1;
}

int MercBone(const Bones& merc, const char* name) noexcept {
	int         index = -1;
	const char* used  = nullptr;
	if (OriginalBindBone(merc.skeleton, name, &index)) return index;
	return AliasBone(merc.skeleton, name, &index, used) ? index : -1;
}

// ---- the Iron Wolf's cap ----
// His cap is the part of his outfit mesh (outfitShape, on his 132 bone skeleton) skinned to his head bone. While he
// wears a helm, that bone's skinning matrix is collapsed onto the head's position, so the cap shrinks inside the helm.
struct CapMesh { const int* indices; int bone; V3 head; };
std::mutex                                     CapLock;
std::unordered_map<const void*, CapMesh>       CapMeshes; // outfit binding -> its head bone
std::atomic<std::size_t>                       CapCount { 0 };

void WatchIronWolfCap(const std::uint8_t* binding, const Bones& merc, const char* meshName, const std::vector<const char*>& names,
                      const int* indices) noexcept {
	if (merc.count != 132 || std::strcmp(meshName, "outfitShape") != 0) return;
	const int head = ExactBone(merc, "head_bind_jnt");
	if (head < 0) return;
	for (std::size_t k = 0; k < names.size(); ++k)
		if (std::strcmp(names[k], "head_bind_jnt") == 0) {
			std::lock_guard<std::mutex> lock(CapLock);
			if (CapMeshes.size() > 256) return;
			CapMeshes[binding] = { indices, static_cast<int>(k), merc.RestPos(head) };
			CapCount           = CapMeshes.size();
			static std::atomic<int> logged { 0 };
			if (logged.fetch_add(1) < 4) Log(false, "Iron Wolf cap found (outfit bone %zu), hidden while he wears a helm", k);
			return;
		}
}

// One 3x4 skinning matrix (rows: rotation | translation) made to put every vertex on one point.
void CollapseOnto(float* m, V3 at) noexcept {
	for (int r = 0; r < 3; ++r) {
		float*      row = m + r * 4;
		const float p   = row[0] * at.x + row[1] * at.y + row[2] * at.z + row[3];
		row[0] = row[1] = row[2] = 0.0f;
		row[3]                   = p;
	}
}

void Prepare(const std::uint8_t* binding, const std::uint8_t* mesh, const std::uint8_t* skeleton) {
	Bones merc;
	if (!ReadSkeleton(skeleton, merc) || merc.count < 60) return;
	{
		int         probe = 0;
		const char* used  = nullptr;
		if (!FirstBone(skeleton, PelvisBones, &probe, used)) return; // not a humanoid merc
	}
	int                 boneCount = 0;
	const std::uint8_t* entries   = nullptr;
	const int*          indices   = nullptr;
	std::memcpy(&boneCount, mesh + 0x30, 4);
	std::memcpy(&entries, mesh + 0x34, 8);
	std::memcpy(&indices, binding + 0x24, 8);
	if (boneCount <= 0 || boneCount > 1024 || !entries || !indices) return;
	std::vector<const char*> names(boneCount);
	int                      foreign = 0;
	for (int k = 0; k < boneCount; ++k) {
		std::memcpy(&names[k], entries + static_cast<std::size_t>(k) * 0x2C, 8);
		if (!names[k]) names[k] = "";
		if (ExactBone(merc, names[k]) < 0) ++foreign;
	}
	const char* meshName = nullptr;
	std::memcpy(&meshName, mesh, 8);
	if (!meshName) meshName = "?";
	if (!foreign) {
		WatchIronWolfCap(binding, merc, meshName, names, indices);
		return; // the merc's own mesh, or one rigged like it
	}

	// The class skeleton the mesh was rigged on: the one that has the most of its bones.
	const ClassSkeleton* donor = nullptr;
	int                  best  = 0;
	for (const auto& entry : ClassSkeletons) {
		if (!entry.ready) continue;
		int found = 0;
		for (const char* name : names)
			if (ExactBone(entry.bones, name) >= 0) ++found;
		// ties (Barbarian and Warlock name their bones alike): the class picked in the settings
		if (found > best || (found == best && found > 0 && Chosen(entry.name) && !(donor && Chosen(donor->name)))) best = found, donor = &entry;
	}
	if (!donor || best * 10 < boneCount * 9) {
		if (Config.logChanges) Log(false, "borrowed mesh with %d bones: no class skeleton matches it (best %d)", boneCount, best);
		return;
	}
	if (Config.logChanges) {
		static std::unordered_map<std::string, int> seen;
		std::lock_guard<std::mutex> lock(BoneLock);
		std::string key = std::string(donor->name) + "|" + (meshName ? meshName : "?") + "|" + std::to_string(boneCount);
		if (seen.size() < 400 && seen.try_emplace(key, 0).second) {
			std::string some;
			for (int k = 0; k < boneCount && some.size() < 300; ++k) some += std::string(k ? ", " : "") + names[k];
			Log(false, "mesh: %s %s (%d bones: %s)", donor->name, meshName, boneCount, some.c_str());
		}
	}
	// The merc keeps his own medium armor body: of a class's heavy armor only the separate pieces are shown
	// (tabards, plates, belts, skirts...); the class body meshes are collapsed (all skinning matrices zero).
	// The Barbarian merc (the only skeleton with a "helmet" bone) wears the class's whole heavy armor.
	// The Rogue also wears the whole class armor.
	// Skeletons are told apart by their head bone (helmet: Barbarian, head_main_00_M_JNT: Rogue).
	int        headBone   = -1;
	const bool wholeArmor = OriginalFindBone(skeleton, "helmet", &headBone) || OriginalFindBone(skeleton, "head_main_00_M_JNT", &headBone);
	{
		static std::atomic<int> named { 0 };
		if (Config.logChanges && named.fetch_add(1) < 8) Log(false, "borrowed armor on a %d bone skeleton: %s", merc.count, wholeArmor ? "whole armor" : "separate pieces only");
	}
	if (!ArmorPiece(meshName) && !wholeArmor) {
		Correction hide;
		hide.binding  = binding;
		hide.mesh     = mesh;
		hide.skeleton = skeleton;
		hide.hidden   = true;
		hide.matrices.assign(static_cast<std::size_t>(boneCount) * 12, 0.0f);
		std::lock_guard<std::mutex> lock(CorrectionLock);
		Corrections[indices] = std::move(hide);
		CorrectionCount      = Corrections.size();
		HiddenBindings[binding] = 1;
		HiddenCount             = HiddenBindings.size();
		return;
	}
	if (!donor->tpose && !donor->snap && !donor->scale) return; // modelled like the mercs already: left as the game draws it
	const Bones& cls = donor->bones;

	// Rest positions, and the merc bone each class bone follows.
	std::vector<V3>  clsPos(cls.count), mercPos(merc.count), posed(cls.count);
	std::vector<int> map(cls.count);
	std::vector<Q4>  applied(cls.count);
	for (int i = 0; i < merc.count; ++i) mercPos[i] = merc.RestPos(i);
	for (int i = 0; i < cls.count; ++i) clsPos[i] = cls.RestPos(i), map[i] = MercBone(merc, cls.Name(i));

	// Swing only the limbs (upper arm, forearm, thigh, calf), each with everything below it, so it points like
	// the merc's limb at rest. The torso keeps its own pose: bones with several children can't follow one direction.
	struct Link { int from, to; };
	std::vector<Link> links;
	for (int side = 0; side < 2; ++side) {
		int         upper = -1, lower = -1, hand = -1, leg = -1, knee = -1, foot = -1;
		const char* used  = nullptr;
		FirstBone(skeleton, UpperArmBones[side], &upper, used);
		FirstBone(skeleton, LowerArmBones[side], &lower, used);
		FirstBone(skeleton, HandBones[side], &hand, used);
		FirstBone(skeleton, LegBones[side], &leg, used);
		FirstBone(skeleton, KneeBones[side], &knee, used);
		FirstBone(skeleton, FootBones[side], &foot, used);
		for (const Link link : { Link { upper, lower }, Link { lower, hand }, Link { leg, knee }, Link { knee, foot } })
			if (link.from >= 0 && link.to >= 0 && link.from != link.to) links.push_back(link);
	}
	const auto below = [&](int c, int i) { for (int a = cls.Parent(c); a >= 0; a = cls.Parent(a)) if (a == i) return true; return false; };
	// Joints moved onto the merc's: per merc bone, the class bone following it that has the most bones below it
	// (the arm itself, not a shoulder pad hanging off the same merc bone).
	std::vector<int> under(cls.count, 0), anchor(merc.count, -1);
	for (int i = cls.count - 1; i >= 0; --i)
		if (const int p = cls.Parent(i); p >= 0) under[p] += under[i] + 1;
	// Uniform scale: merc neck height over the class neck height (both measured from the feet, the model origin).
	float scale = 1.0f;
	if (donor->scale) {
		int         neck = -1;
		const char* used = nullptr;
		if (FirstBone(skeleton, NeckBones, &neck, used)) {
			int best = -1;
			for (int i = 0; i < cls.count; ++i)
				if (map[i] == neck && (best < 0 || under[i] > under[best])) best = i;
			const float from = best >= 0 ? Len(clsPos[best]) : 0.0f, to = Len(mercPos[neck]);
			if (from > 1e-3f && to / from > 0.7f && to / from < 1.6f) scale = to / from;
		}
	}
	const auto scaled = [&](V3 v) { return V3 { v.x * scale, v.y * scale, v.z * scale }; };
	if (donor->snap)
		for (int i = 0; i < cls.count; ++i) {
			const int m = map[i], p = cls.Parent(i);
			if (m < 0 || (p >= 0 && map[p] == m)) continue;
			if (donor->keepClavicle) {
				std::string low(cls.Name(i));
				for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				if (low.find("clav") != std::string::npos) continue;
			}
			if (anchor[m] < 0 || under[i] > under[anchor[m]]) anchor[m] = i;
		}
	int swung = 0;
	for (int i = 0; i < cls.count; ++i) {
		const int p      = cls.Parent(i);
		const Q4  parent = p < 0 ? Q4 { 0, 0, 0, 1 } : applied[p];
		posed[i]         = p < 0 ? scaled(clsPos[i]) : Add(posed[p], Rot(parent, scaled(Sub(clsPos[i], clsPos[p]))));
		Q4 swing { 0, 0, 0, 1 };
		const int m = map[i];
		if (m >= 0 && anchor[m] == i) posed[i] = mercPos[m];
		// the limb bone itself (its parent follows another merc bone), not a twist bone below it
		const bool start = donor->tpose && m >= 0 && (p < 0 || map[p] != m);
		for (const Link link : links) {
			if (!start || link.from != m) continue;
			for (int c = i + 1; c < cls.count; ++c) {
				if (map[c] != link.to || !below(c, i)) continue;
				const V3 from = Rot(parent, Sub(clsPos[c], clsPos[i]));
				const V3 to   = Sub(mercPos[link.to], mercPos[m]);
				swing         = FromTo(from, to);
				if (std::fabs(swing.w) < 0.9999f) ++swung;
				break;
			}
			break;
		}
		applied[i] = Norm(Mul(swing, parent));
	}

	// Per bone binding: x -> applied * (x - rest) + posed, as a 3x4 (column vectors).
	Correction fix;
	fix.binding  = binding;
	fix.mesh     = mesh;
	fix.skeleton = skeleton;
	fix.matrices.assign(static_cast<std::size_t>(boneCount) * 12, 0.0f);
	for (int k = 0; k < boneCount; ++k) {
		float*    m = fix.matrices.data() + static_cast<std::size_t>(k) * 12;
		const int d = ExactBone(cls, names[k]);
		Q4        q { 0, 0, 0, 1 };
		V3        t { 0, 0, 0 };
		if (d >= 0) {
			q             = applied[d];
			const V3 r    = Rot(q, scaled(clsPos[d]));
			t             = Sub(posed[d], r);
		}
		const V3 cx = Rot(q, { scale, 0, 0 }), cy = Rot(q, { 0, scale, 0 }), cz = Rot(q, { 0, 0, scale });
		const float rows[12] { cx.x, cy.x, cz.x, t.x, cx.y, cy.y, cz.y, t.y, cx.z, cy.z, cz.z, t.z };
		std::memcpy(m, rows, sizeof(rows));
	}
	std::lock_guard<std::mutex> lock(CorrectionLock);
	Corrections[indices] = std::move(fix);
	CorrectionCount      = Corrections.size();
	if (Config.logChanges && CorrectionsLogged < 40) {
		++CorrectionsLogged;
		Log(false, "borrowed %s mesh (%d bones, %d not on the merc): %d bones swung to the merc's rest pose, scale %.2f", donor->name, boneCount, foreign, swung, scale);
	}
}

std::uint8_t* NewBinding(const std::uint8_t* mesh, const std::uint8_t* from, const std::uint8_t* to) noexcept {
	std::uint8_t* binding = OriginalNewBinding(mesh, from, to);
	if (!binding || !mesh || from != to || !SkeletonsLoaded.load()) return binding;
	try {
		Prepare(binding, mesh, to);
	} catch (...) {
	}
	return binding;
}

// Per-mesh skinning palette of a model instance (0xf3ec70: instance, mesh index, palette, previous palette, max bones).
// Some meshes take their palette from a cache here instead of the composite builder, so a hidden mesh's palette
// is also zeroed after it.
using PaletteFn = void (*)(std::uint8_t* instance, unsigned index, float* palette, float* previous, int maxBones) noexcept;
constexpr std::uint64_t PaletteRva = 0xF3EC70;
constexpr std::array<std::uint8_t, 13> PaletteBytes { 0x48, 0x8B, 0xC4, 0x4C, 0x89, 0x40, 0x18, 0x55, 0x57, 0x41, 0x54, 0x41, 0x55 };
PaletteFn                              OriginalPalette {};

void Palette(std::uint8_t* instance, unsigned index, float* palette, float* previous, int maxBones) noexcept {
	OriginalPalette(instance, index, palette, previous, maxBones);
	if (!instance) return;
	std::uint8_t* const* list = nullptr;
	std::uint64_t        size = 0;
	std::memcpy(&list, instance + 0x2D8, 8);
	std::memcpy(&size, instance + 0x2E0, 8);
	if (!list || index >= size) return;
	const std::uint8_t* binding = list[index];
	int                 bones   = 0;
	if (!binding) return;
	if (CapCount.load() && Config.enabled && IronWolfHelm.load()) {
		std::lock_guard<std::mutex> lock(CapLock);
		if (const auto it = CapMeshes.find(binding); it != CapMeshes.end() && it->second.bone < maxBones) {
			if (palette) CollapseOnto(palette + static_cast<std::size_t>(it->second.bone) * 12, it->second.head);
			if (previous) CollapseOnto(previous + static_cast<std::size_t>(it->second.bone) * 12, it->second.head);
			return;
		}
	}
	{
		std::lock_guard<std::mutex> lock(CorrectionLock);
		if (HiddenBindings.find(binding) == HiddenBindings.end()) return;
	}
	std::memcpy(&bones, binding + 8, 4);
	const int n = std::min(bones, maxBones);
	if (n <= 0) return;
	if (palette) std::memset(palette, 0, static_cast<std::size_t>(n) * 0x30);
	if (previous) std::memset(previous, 0, static_cast<std::size_t>(n) * 0x30);
}

void FreeBinding(std::uint8_t* binding) noexcept {
	if (binding && CapCount.load()) {
		std::lock_guard<std::mutex> lock(CapLock);
		if (CapMeshes.erase(binding)) CapCount = CapMeshes.size();
	}
	if (binding && HiddenCount.load()) {
		std::lock_guard<std::mutex> lock(CorrectionLock);
		HiddenBindings.erase(binding);
		HiddenCount = HiddenBindings.size();
	}
	if (binding && CorrectionCount.load()) {
		const int* indices = nullptr;
		std::memcpy(&indices, binding + 0x24, 8);
		std::lock_guard<std::mutex> lock(CorrectionLock);
		if (const auto it = Corrections.find(indices); it != Corrections.end() && it->second.binding == binding) {
			Corrections.erase(it);
			CorrectionCount = Corrections.size();
		}
	}
	OriginalFreeBinding(binding);
}

void Composite(const std::uint8_t* skeleton, const void* worldPose, const int* indices, int count, float* buffer) noexcept {
	OriginalComposite(skeleton, worldPose, indices, count, buffer);
	if (CapCount.load() && Config.enabled && IronWolfHelm.load() && buffer && count > 0) {
		std::lock_guard<std::mutex> lock(CapLock);
		for (const auto& [binding, cap] : CapMeshes)
			if (cap.indices == indices) {
				if (cap.bone < count) CollapseOnto(buffer + static_cast<std::size_t>(cap.bone) * 12, cap.head);
				break;
			}
	}
	if (!CorrectionCount.load() || !Config.enabled || !buffer || count <= 0) return;
	std::lock_guard<std::mutex> lock(CorrectionLock);
	const auto it = Corrections.find(indices);
	if (it == Corrections.end() || it->second.skeleton != skeleton) return;
	if (it->second.hidden) {
		std::memset(buffer, 0, static_cast<std::size_t>(count) * 12 * sizeof(float));
		return;
	}
	const std::vector<float>& fix = it->second.matrices;
	const int                 n   = std::min<int>(count, static_cast<int>(fix.size() / 12));
	for (int k = 0; k < n; ++k) {
		float*       t = buffer + static_cast<std::size_t>(k) * 12;
		const float* x = fix.data() + static_cast<std::size_t>(k) * 12;
		float        out[12];
		for (int r = 0; r < 3; ++r)
			for (int c = 0; c < 4; ++c)
				out[r * 4 + c] = t[r * 4 + 0] * x[0 * 4 + c] + t[r * 4 + 1] * x[1 * 4 + c] + t[r * 4 + 2] * x[2 * 4 + c] + (c == 3 ? t[r * 4 + 3] : 0.0f);
		std::memcpy(t, out, sizeof(out));
	}
}

// ---- item colors on the merc's attached item models ----
// Players' weapons are their own item units, so the game tints them itself (state colors like Enchant's red,
// colored uniques). The merc's helm and weapons are attachments of its own body parts instead, which the game
// never tints. 0x77d550(visuals, unit visual, unit, world, slot) is the game's per-slot item color pass (called
// for slots 6, 5, 7, 0, 1, 10 in that order): it reads the slot's item color with 0x376050(out, owner, item)
// (owner's state colors included), then writes class/color into each model's ModelMaterialComponent (+0x18,
// +0x19; skipped when +0x1a is negative). Mercs never reach it (see UnitVisualUpdate), so the plugin does the
// same for the merc's helm, weapon and shield parts, down into their attached item models:
//   - unit visual (shared_ptr) +0x10 entity ids, +0x18 count; world +0 registry.
//   - 0x71d100(world, entity) UnitPartComponent (+0 part: 0 head .. 5 rhand, 6 lhand, 7 shield).
//   - 0x71ced0(registry, entity) UnitAttachmentComponent (+0x278 weak_ptr to the attached unit visual).
//   - 0x71bcb0(world, entity) ModelMaterialComponent.
//   - 0x3885e0(inventory, slot) the item shown in a component slot.
using ItemColorFn     = std::uint8_t* (*)(std::uint8_t* out, void* owner, void* item) noexcept;
using SlotItemFn      = void* (*)(void* inventory, unsigned char slot) noexcept;
using ComponentFn     = std::uint8_t* (*)(void* world, std::uint32_t entity) noexcept;
constexpr std::uint64_t ItemColorRva = 0x376050, SlotItemRva = 0x3885E0;
constexpr std::uint64_t PartRva = 0x71D100, AttachmentRva = 0x71CED0, MaterialRva = 0x71BCB0;
constexpr std::array<std::uint8_t, 15> ItemColorBytes { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x49, 0x8B, 0xC0, 0x4C, 0x8B, 0xD2, 0x48, 0x8B, 0xD9 };
constexpr std::array<std::uint8_t, 14> SlotItemBytes { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x56, 0x57, 0x41, 0x56 };
constexpr std::array<std::uint8_t, 15> ComponentBytes { 0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x40 };
constexpr std::array<std::uint8_t, 15> AttachmentBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20 };
ItemColorFn     ItemColor {};
SlotItemFn      SlotItem {};
ComponentFn     PartOf {}, MaterialOf {};
ComponentFn     AttachmentOf {}; // takes the registry, not the world
std::atomic<bool> TintReached { false };

bool EntityList(const std::uint8_t* visual, const std::uint32_t*& ids, std::uint64_t& count) noexcept {
	return visual && ReadField(visual, 0x10, ids) && ReadField(visual, 0x18, count) && ids && count <= 4096;
}

bool Paint(void* world, std::uint32_t entity, std::uint8_t cls, std::uint8_t color) noexcept {
	std::uint8_t* material = MaterialOf(world, entity);
	if (!material || static_cast<std::int8_t>(material[0x1A]) < 0) return false;
	material[0x18] = cls;
	material[0x19] = color;
	return true;
}

void TintMerc(void** unitVisual, void* unit, void* world, const bool* parts) noexcept {
	const std::uint8_t* visual = static_cast<const std::uint8_t*>(*unitVisual);
	const std::uint32_t* ids    = nullptr;
	std::uint64_t        count  = 0;
	void*                registry = nullptr;
	if (!EntityList(visual, ids, count) || !ReadField(world, 0, registry) || !registry) return;
	void* inventory = GetInventory(unit, __FILE__, __LINE__);
	for (const unsigned char slot : { static_cast<unsigned char>(HD), static_cast<unsigned char>(RH), static_cast<unsigned char>(LH), static_cast<unsigned char>(SH) }) {
		// Helm colors come with the helm model; weapon colors (Enchant...) only with weapon and enchant both on.
		if (slot == HD ? !parts[Helm] : !(parts[Weapon] && parts[Enchant])) continue;
		std::uint8_t cls = 0, color = 0;
		if (void* item = inventory ? SlotItem(inventory, slot) : nullptr) {
			alignas(8) std::uint8_t out[16] {};
			ItemColor(out, unit, item);
			if (out[8]) {
				std::int32_t c = 0, k = 0;
				std::memcpy(&c, out, 4);
				std::memcpy(&k, out + 4, 4);
				cls = static_cast<std::uint8_t>(c), color = static_cast<std::uint8_t>(k);
			}
		}
		int painted = 0;
		for (std::uint64_t i = 0; i < count; ++i) {
			const std::uint32_t entity = ids[i];
			const std::uint8_t* part   = PartOf(world, entity);
			if (!part || part[0] != slot) continue;
			if (Paint(world, entity, cls, color)) ++painted; // the merc's own item models (Iron Wolf's sword...)
			const std::uint8_t* attachment = AttachmentOf(registry, entity);
			const std::uint8_t* child      = nullptr;
			const std::uint8_t* control    = nullptr;
			std::int32_t        uses       = 0;
			if (!attachment || !ReadField(attachment, 0x278, child) || !ReadField(attachment, 0x280, control) || !child || !control ||
			    !ReadField(control, 8, uses) || uses <= 0)
				continue;
			const std::uint32_t* childIds   = nullptr;
			std::uint64_t        childCount = 0;
			if (!EntityList(child, childIds, childCount)) continue;
			for (std::uint64_t j = 0; j < childCount; ++j)
				if (Paint(world, childIds[j], cls, color)) ++painted;
		}
		if (Config.logChanges) {
			static std::mutex                                      tintLock;
			static std::unordered_map<std::uint64_t, std::uint32_t> last;
			std::uint32_t id = 0;
			(void)ReadField(unit, UnitIdOffset, id);
			const std::uint64_t key  = (static_cast<std::uint64_t>(id) << 8) | slot;
			const std::uint32_t seen = (static_cast<std::uint32_t>(cls) << 24) | (static_cast<std::uint32_t>(color) << 16) | static_cast<std::uint32_t>(std::min(painted, 0xFFFF));
			std::lock_guard<std::mutex> lock(tintLock);
			if (last.size() < 4096) {
				auto [it, added] = last.try_emplace(key, seen);
				if ((added && (cls || painted)) || (!added && it->second != seen)) {
					it->second = seen;
					Log(false, "merc %u %s: item color %u/%u on %d models", id, SlotNames[slot], cls, color, painted);
				}
			}
		}
	}
}

// 0x754db0(visuals, entity, unit component (0xe0), unit holder): per unit visual update. Players go on to the item
// color pass; monsters (mercs) leave before it, so the plugin tints after it: unit = holder +8, unit visual =
// component +0xc8 (shared_ptr, control +0xd0), world = *visuals.
using UnitVisualUpdateFn = void (*)(void* visuals, std::uint32_t entity, std::uint8_t* component, std::uint8_t* holder) noexcept;
constexpr std::uint64_t UnitVisualUpdateRva = 0x754DB0;
constexpr std::array<std::uint8_t, 21> UnitVisualUpdateBytes { 0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56,
	                                                           0x41, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0xD8, 0xFD, 0xFF, 0xFF };
UnitVisualUpdateFn OriginalUnitVisualUpdate {};

void UnitVisualUpdate(void* visuals, std::uint32_t entity, std::uint8_t* component, std::uint8_t* holder) noexcept {
	OriginalUnitVisualUpdate(visuals, entity, component, holder);
	if (!Config.enabled || !DefsActive || !visuals || !component || !holder) return;
	void*         unit  = nullptr;
	void*         world = nullptr;
	std::uint32_t type = 0, classId = 0;
	if (!ReadField(holder, 8, unit) || !unit || !ReadField(unit, UnitTypeOffset, type) || type != 1 || !ReadField(unit, UnitClassOffset, classId) ||
	    !FindMerc(static_cast<int>(classId)) || !ReadField(visuals, 0, world) || !world)
		return;
	void*               visual  = nullptr;
	const std::uint8_t* control = nullptr;
	std::int32_t        uses    = 0;
	if (!ReadField(component, 0xC8, visual) || !visual || !ReadField(component, 0xD0, control) || !control || !ReadField(control, 8, uses) || uses <= 0) return;
	if (!TintReached.exchange(true)) Log(false, "item colors: reached the merc visual update");
	try {
		TintMerc(&visual, unit, world, Config.parts[FindMerc(static_cast<int>(classId))->group]);
	} catch (...) {
	}
}

int RegisterDefs() noexcept;

void __cdecl OnTablesLoaded(const D2RL::PluginContext*, const D2RL::Lifecycle::DataTablesLoadedEvent*, void*) noexcept {
	LoadClassSkeletons();
}

// ---- the hook ----

std::uint32_t ComponentCode(void* unit, int unitType, int classId, int mode, unsigned char slot) noexcept {
	const std::uint32_t code = OriginalComponentCode(unit, unitType, classId, mode, slot);
	// Off: the game's own codes, so each merc's look goes back to normal at its next update.
	if (!Config.enabled || unitType != 1 || !unit || slot >= 16) return code;
	const MercLooks* merc = FindMerc(classId);
	if (!merc) return code;
	std::uint32_t type = 0;
	if (!ReadField(unit, UnitTypeOffset, type) || type != 1) return code;
	const std::uint32_t gear = GearCode(unit, *merc, slot);
	if (!gear) return code;
	if (Config.logChanges) NoteCode(unit, *merc, slot, gear);
	return gear;
}

const char* MercDefName(void* mapper, void* unit, int unitType, int classId) noexcept {
	const char* name = DefName(mapper, unit, unitType, classId);
	if (!DefsActive || unitType != 1 || !name) return name;
	for (std::size_t i = 0; i < std::size(MercNames); ++i)
		if (std::strcmp(name, MercNames[i]) == 0) return DefNames[i].c_str();
	return name;
}

// ---- setup ----

// Each merc's definition: the game's own <merc>.json with the plugin's entities and dependencies added (all
// put together from the tables in merc_adds.inc), registered as data/hd/character/enemy/d2rloader/merc-gear-models/<merc>.json.
// Registered at load: a resource registered once the game runs is never seen by it. 0 = done, 2 = failed.
int RegisterDefs() noexcept {
	const D2RL::ResourceService* resources = nullptr;
	if (Context->QueryService(&resources) != D2RL::ServiceQueryResult::Success || !D2RL::HasResourceServiceField(resources, D2RL::ResourceServiceRequiredSize)) {
		Log(true, "item models off: the D2RLoader resource service is missing");
		return 2;
	}
	for (std::size_t i = 0; i < std::size(MercNames); ++i) {
		std::string       trouble, base;
		const std::string source = std::string("data/hd/character/enemy/") + MercNames[i] + ".json";
		try {
			base        = MercBuild::BaseText(i);
			DefTexts[i] = MercBuild::Build(i, base, PolearmGrip, trouble);
		} catch (...) {
			DefTexts[i].clear(), trouble = "out of memory";
		}
		if (DefTexts[i].empty()) {
			Log(true, "item models off: %s could not be extended (%s)", source.c_str(), trouble.c_str());
			return 2;
		}
		DefNames[i]            = std::string("d2rloader/") + PluginId + "/" + MercNames[i];
		const std::string path = "data/hd/character/enemy/" + DefNames[i] + ".json";
		const D2RL::Resources::ResourceRegistration resource {
			.structSize = D2RL::Resources::ResourceRegistrationSize,
			.path       = path.c_str(),
			.bytes      = DefTexts[i].data(),
			.byteCount  = DefTexts[i].size(),
		};
		D2RL::Resources::RegistrationHandle handle = D2RL::Resources::InvalidHandle;
		if (const auto result = resources->registerResource(Context, &resource, &handle); result != D2RL::Resources::Result::Success) {
			Log(true, "item models off: could not register %s (result %u)", path.c_str(), static_cast<unsigned>(result));
			return 2;
		}
		Log(false, "merc definitions: %s built (%zu -> %zu bytes)", MercNames[i], base.size(), DefTexts[i].size());
	}
	return 0;
}

// A rel32 call has to land in the game image: a jmp [rip] to MercDefName goes
// in int3 padding and the definition name call is pointed at it.
bool RedirectDefNameCall() noexcept {
	if (!CodeMatches(DefNameCallRva - 16, DefNameSiteBytes)) return false;
	std::array<std::uint8_t, 14> jump { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
	const auto                   target = reinterpret_cast<std::uint64_t>(&MercDefName);
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
			if (!Context->PatchBytes(cave + 1, padding.data(), 14, jump.data(), 14)) continue;
			return D2RL::PatchCallRel32(Context, DefNameCallRva, DefNameSiteBytes.data() + 16, 5, cave + 1);
		}
	}
	return false;
}

bool Locate() noexcept {
	if (!CodeMatches(GetInventoryRva, GetInventoryBytes) || !CodeMatches(BodyLocItemRva, BodyLocItemBytes) || !CodeMatches(ItemGfxCodeRva, ItemGfxCodeBytes) ||
	    !CodeMatches(UnitBankRva, UnitBankBytes) || !CodeMatches(ItemRecordRva, ItemRecordBytes))
		return false;
	GetInventory = reinterpret_cast<GetInventoryFn>(Base + GetInventoryRva);
	BodyLocItem  = reinterpret_cast<BodyLocItemFn>(Base + BodyLocItemRva);
	ItemGfxCode  = reinterpret_cast<ItemGfxCodeFn>(Base + ItemGfxCodeRva);
	UnitBank     = reinterpret_cast<UnitBankFn>(Base + UnitBankRva);
	ItemRecord   = reinterpret_cast<ItemRecordFn>(Base + ItemRecordRva);
	DefName      = reinterpret_cast<DefNameFn>(Base + DefNameRva);
	return true;
}

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

// ---- settings (TOML + I Hate TOMLs) ----

bool ParseBool(const std::string& value, bool& out) noexcept {
	if (value == "true") return out = true, true;
	if (value == "false") return out = false, true;
	return false;
}

// Applies one setting; false with a reason when the value is refused.
bool Apply(const std::string& key, const std::string& value, std::string& error) {
	bool on = false;
	if (key == "item_models") return true; // old key: item models are always on now
	if (key == "log_changes") return true; // old key: the plugin always logs look changes now
	if (key == "enabled") {
		if (!ParseBool(value, on)) return error = "true or false", false;
		Config.enabled = on;
		return true;
	}
	if (key == "polearm_grip" || key == "polearm_reach") return true; // old keys: the reach is fixed now
	for (int row = 0; row < 4; ++row)
		for (int column = 0; column < 4; ++column)
			if (key == std::string(MercRows[row]) + "." + PartColumns[column]) {
				if (!ParseBool(value, on)) return error = "true or false", false;
				Config.parts[row][column] = on;
				return true;
			}
	for (const char* old : HeavyKeys)
		if (key == old) return true; // old keys: the heavy armor looks are fixed now
	return error = "unknown setting", false;
}

std::string ValueOf(const std::string& key) {
	if (key == "enabled") return Config.enabled ? "true" : "false";
	for (int row = 0; row < 4; ++row)
		for (int column = 0; column < 4; ++column)
			if (key == std::string(MercRows[row]) + "." + PartColumns[column]) return Config.parts[row][column] ? "true" : "false";
	return {};
}

// Per merc: weapon (hand items and shield), helm, armor (body look), enchant (item colors on the weapon, needs weapon).
std::string PartsText() {
	std::string text = "\n# What each merc shows (true/false): weapon (weapon and shield models), helm,\n"
	                   "# armor (body armor look), enchant (item colors such as Enchant's red on the\n"
	                   "# weapon; needs weapon).\n";
	for (int row = 0; row < 4; ++row)
		for (int column = 0; column < 4; ++column) {
			const std::string key = std::string(MercRows[row]) + "." + PartColumns[column];
			text += key + " = " + ValueOf(key) + "\n";
		}
	return text;
}

std::string Render() {
	return R"(# Merc Gear Models
#
# Your merc's look follows its gear: the real models of its helm and weapon,
# and its body armor look (light, medium, heavy). Only changes what you see.
# Every setting can also be changed in game with I Hate TOMLs.

[merc-gear-models]
enabled = )" + ValueOf("enabled") + "\n" + PartsText();
}

void SaveConfig() {
	if (!Context->WriteConfig(Render().c_str())) Log(true, "config: could not write the config file");
}

constexpr const char* Schema = R"(title = "Merc Gear Models"
[[setting]]
key = "enabled"
type = "bool"
label = "Merc look follows its gear"
[[setting]]
type = "table"
label = "Shown on each merc"
note = "Enchant: item colors on the weapon, such as Enchant's red. Needs weapon."
rows = ["rogue", "desert", "iron_wolf", "barbarian"]
row_labels = ["Rogue", "Desert merc", "Iron Wolf", "Barbarian"]
columns = ["weapon", "helm", "armor", "enchant"]
column_labels = ["Weapon", "Helm", "Armor", "Enchant"]
requires = { enchant = "weapon" }
)";

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
		std::string       error;
		if (!Apply(key, value, error)) Log(true, "config: %s = %s ignored (%s)", key.c_str(), value.c_str(), error.c_str());
	}
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
	// The hooks go in even when disabled, so it can be turned on in game.
	Base = context->exeBase;
	if (!Base || !Locate()) {
		Log(true, "Merc Gear Models %s: inactive, this game build isn't supported (Steam 3.3.93787 / 3.3.94995 are)", PluginVersion);
		return true;
	}
	if (!context->InstallInlineHook(ComponentCodeRva, ComponentCodeBytes.data(), static_cast<std::uint32_t>(ComponentCodeBytes.size()), &ComponentCode,
	                                &OriginalComponentCode)) {
		Log(true, "Merc Gear Models %s: inactive, could not hook the component code function at D2R+0x%llX (another plugin may hook it)", PluginVersion,
		    static_cast<unsigned long long>(ComponentCodeRva));
		return true;
	}
	if (!context->InstallInlineHook(FindBoneRva, FindBoneBytes.data(), static_cast<std::uint32_t>(FindBoneBytes.size()), &FindBone, &OriginalFindBone))
		Log(true, "helms off: could not hook D2R+0x%llX", static_cast<unsigned long long>(FindBoneRva));
	if (!context->InstallInlineHook(BindBoneRva, FindBoneBytes.data(), static_cast<std::uint32_t>(FindBoneBytes.size()), &BindBone, &OriginalBindBone))
		Log(true, "binding probe off: could not hook D2R+0x%llX", static_cast<unsigned long long>(BindBoneRva));
	if (!context->InstallInlineHook(NewBindingRva, NewBindingBytes.data(), static_cast<std::uint32_t>(NewBindingBytes.size()), &NewBinding, &OriginalNewBinding) ||
	    !context->InstallInlineHook(FreeBindingRva, FreeBindingBytes.data(), static_cast<std::uint32_t>(FreeBindingBytes.size()), &FreeBinding, &OriginalFreeBinding) ||
	    !context->InstallInlineHook(CompositeRva, CompositeBytes.data(), static_cast<std::uint32_t>(CompositeBytes.size()), &Composite, &OriginalComposite))
		Log(true, "borrowed bodies keep their own pose: could not hook the game's mesh binding at D2R+0x%llX", static_cast<unsigned long long>(NewBindingRva));
	if (!context->InstallInlineHook(PaletteRva, PaletteBytes.data(), static_cast<std::uint32_t>(PaletteBytes.size()), &Palette, &OriginalPalette))
		Log(true, "hidden meshes may still show: could not hook D2R+0x%llX", static_cast<unsigned long long>(PaletteRva));
	if (CodeMatches(ItemColorRva, ItemColorBytes) && CodeMatches(SlotItemRva, SlotItemBytes) && CodeMatches(PartRva, ComponentBytes) &&
	    CodeMatches(MaterialRva, ComponentBytes) && CodeMatches(AttachmentRva, AttachmentBytes)) {
		ItemColor    = reinterpret_cast<ItemColorFn>(Base + ItemColorRva);
		SlotItem     = reinterpret_cast<SlotItemFn>(Base + SlotItemRva);
		PartOf       = reinterpret_cast<ComponentFn>(Base + PartRva);
		MaterialOf   = reinterpret_cast<ComponentFn>(Base + MaterialRva);
		AttachmentOf = reinterpret_cast<ComponentFn>(Base + AttachmentRva);
		if (!CodeMatches(UnitVisualUpdateRva, UnitVisualUpdateBytes) ||
		    !context->InstallInlineHook(UnitVisualUpdateRva, UnitVisualUpdateBytes.data(), 13, &UnitVisualUpdate, &OriginalUnitVisualUpdate))
			Log(true, "item colors off: could not hook D2R+0x%llX", static_cast<unsigned long long>(UnitVisualUpdateRva));
	} else
		Log(true, "item colors off: this game build's item color code differs");
	{
		const D2RL::LifecycleService* lifecycle = nullptr;
		const D2RL::Lifecycle::DataTablesLoadedListener listener {
			.structSize = D2RL::Lifecycle::DataTablesLoadedListenerSize,
			.callback   = OnTablesLoaded,
		};
		if (context->QueryService(&lifecycle) != D2RL::ServiceQueryResult::Success || !D2RL::HasLifecycleServiceField(lifecycle, D2RL::LifecycleServiceRequiredSize) ||
		    lifecycle->registerDataTablesLoadedListener(context, &listener, &TablesListener) != D2RL::Lifecycle::Result::Success)
			Log(true, "borrowed bodies keep their own pose: no data tables listener");
	}
	if (!RedirectDefNameCall())
		Log(true, "item models off: could not redirect the definition name call at D2R+0x%llX", static_cast<unsigned long long>(DefNameCallRva));
	else
		DefsActive = RegisterDefs() == 0;
	Log(false, "Merc Gear Models %s: active (item models %s)", PluginVersion, DefsActive ? "on" : "off");
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept { Menu.Stop(); }
