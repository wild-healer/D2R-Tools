// Ethereal Ground Tag: adds a configurable tag to the ground label of ethereal items.
//
// The label text is produced by D2R's ground-label name writer, called by the
// label formatter right before the game measures the text. Changing the text
// there lets the native code size the label box around the new text.
//
// The tag is added right before the formatter measures the text: its two
// measure calls (one per UI mode) are redirected to a stub that tags the
// buffer, then jumps to the original measure function. No other known plugin
// touches those calls, so this works alongside Chronicle Ground Flag, UnHoarder
// and Controller QOL whatever the load order, and sees the text after all of
// them have written it.
//
// If the measure calls don't match, two older attach points are tried. Other
// plugins use them too, and D2RLoader allows only one owner for each:
//   - an inline hook on the writer's entry (Chronicle Ground Flag, UnHoarder);
//   - a patch of the formatter's call to the writer (Controller QOL, which
//     relays the call and still calls the writer entry).
// So we take whichever one is free. When the formatter still calls the writer
// directly, its call is redirected to us (through a jump placed in int3 padding
// inside D2R.exe) and we call the writer, hooked or not. When another plugin
// already patched that call, we hook the writer entry instead and call the
// original writer through D2RLoader's trampoline, which keeps the other plugin's
// relay in the path. When both are taken, the plugin stays inactive.
//
// Everything is found at run time instead of by a fixed build number:
//   0. the formatter at its known address when its prologue matches, else by
//      scan, with its two measure calls found by their argument setup;
//   1. the writer at its known address (same on Steam 3.3.93787 and
//      Battle.net 3.3.93847) when its prologue matches, or when D2RLoader
//      reports another plugin's hook there; else a scan for its prologue,
//      confirmed by the label formatter that calls it;
//   2. the formatter's call site at its known address when it calls the
//      writer, else the single formatter call to the writer found by scan;
//      if neither, an inline hook on the writer entry when nothing owns it.
// Anything else leaves the plugin loaded but inactive.

#include <D2RLPlugin/api.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Stubs for the formatter's measure calls and the hover label's copy (assembly
// below). They pass the unit and the text to EthTagFromMeasureA/B or
// EthTagFromHover, restore the original call's arguments, and jump to it.
extern "C" {
void  EthMeasureThunkA() noexcept;
void  EthMeasureThunkB() noexcept;
void  EthTagFromMeasureA(void* unit, void* output) noexcept;
void  EthTagFromMeasureB(void* unit, void* output) noexcept;
void  EthHoverThunk() noexcept;
void  EthTagFromHover(void* unit, void* text) noexcept;
void* EthHoverOriginal = nullptr;
void* EthMeasureOriginalA = nullptr;
void* EthMeasureOriginalB = nullptr;
}

// Measure A takes rcx, rdx, r8, xmm3 and a stack argument; measure B and the
// hover label's copy take rcx, rdx, r8. The thunks keep all four argument
// registers and xmm3. On entry rsp
// is 8 mod 16; four pushes and 0x38 bytes make it 16-aligned for the call.
#define ETH_THUNK(name, original, tag, unit, text)       \
	".globl " name "\n"                                  \
	name ":\n"                                           \
	"	push rcx\n"                                        \
	"	push rdx\n"                                        \
	"	push r8\n"                                         \
	"	push r9\n"                                         \
	"	sub rsp, 0x38\n"                                   \
	"	movdqu [rsp+0x20], xmm3\n"                         \
	"	mov rdx, " text "\n"                               \
	"	mov rcx, " unit "\n"                               \
	"	call " tag "\n"                                    \
	"	movdqu xmm3, [rsp+0x20]\n"                         \
	"	add rsp, 0x38\n"                                   \
	"	pop r9\n"                                          \
	"	pop r8\n"                                          \
	"	pop rdx\n"                                         \
	"	pop rcx\n"                                         \
	"	jmp [rip+" original "]\n"

asm(".intel_syntax noprefix\n"
    ".text\n"
    ETH_THUNK("EthMeasureThunkA", "EthMeasureOriginalA", "EthTagFromMeasureA", "r15", "rcx")
    ETH_THUNK("EthMeasureThunkB", "EthMeasureOriginalB", "EthTagFromMeasureB", "r15", "rcx")
    ETH_THUNK("EthHoverThunk", "EthHoverOriginal", "EthTagFromHover", "rsi", "r8")
    ".att_syntax prefix\n");

namespace {

constexpr const char* PluginId      = "ethereal-ground-tag";
constexpr const char* PluginVersion = "1.0.3";

const D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = PluginId,
	.name        = "Ethereal Ground Tag",
	.version     = PluginVersion,
	.author      = "wild-healer",
	.description = "Adds a tag such as (Eth) to the name of ethereal items on the ground.",
	.flags       = D2RL::PluginFlags::Client | D2RL::PluginFlags::NativeHooks,
};

// ---- native layout (verified on 3.3.93847 by UnHoarder, shared by 3.3.93787) ----

constexpr std::uint64_t KnownNameWriterRva = 0xCBEB0;
constexpr std::uint64_t KnownCallSiteRva   = 0x1FAA18; // formatter's call to the writer
constexpr std::uint32_t LabelBufferBytes   = 0x80; // bufferSize the formatter passes
constexpr std::uintptr_t TextOffset        = 4;    // text starts at output + 4
constexpr std::uintptr_t RecordFromOutput  = 0x24; // label record = output - 0x24
constexpr std::uintptr_t RecordUnitId      = 0x10; // record + 0x10 = unit id
constexpr std::uint32_t UnitTypeItem       = 4;
constexpr std::uintptr_t UnitItemData      = 0x10; // unit + 0x10 = item data
constexpr std::uintptr_t ItemDataFlags     = 0x18; // item data + 0x18 = item flags
constexpr std::uint32_t ItemFlagEthereal   = 0x00400000;

// -1 = any byte (RIP-relative displacements change between builds).
constexpr std::array<int, 19> NameWriterPattern {
	0x40, 0x53, 0x55, 0x56, 0x57,             // push rbx/rbp/rsi/rdi
	0x48, 0x81, 0xEC, 0x68, 0x04, 0x00, 0x00, // sub rsp, 0x468
	0x48, 0x8B, 0x05, -1, -1, -1, -1,         // mov rax, [rip+cookie]
};
constexpr std::array<int, 16> FormatterPattern {
	0x40, 0x55, 0x53, 0x56, 0x41, 0x54, 0x41, 0x57,
	0x48, 0x8B, 0xEC, 0x48, 0x83, 0xEC, 0x50, 0x45,
};
constexpr std::size_t FormatterCallWindow = 0x80; // the writer is called at +0x28 on 93847

// ---- state ----

using NameWriterFn = std::uint64_t (*)(void* unit, void* output, std::uint32_t bufferSize, void* metadata) noexcept;

const D2RL::PluginContext* Context {};
std::uintptr_t             Base {};
NameWriterFn               NameWriter {}; // the writer (or the hook on it), or the trampoline to the original writer
std::atomic_bool           Active {};

struct Settings {
	bool                     enabled = true;
	std::string              text    = "Eth";
	bool                     before  = true;
	std::string              open    = "[";      // brackets around the tag, or empty
	std::string              close   = "]";
	std::string              bracketColor = "0"; // color code character, or empty = same as item
	std::string              tagColor     = "4";
	std::vector<std::string> versions { "3.3" };
} Config;

std::string GameVersion;  // e.g. 3.3.93787
std::string LocateMethod; // how the writer was found
std::uint64_t HookedRva {};

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

bool Writable(std::uintptr_t address, std::size_t size) noexcept {
	MEMORY_BASIC_INFORMATION memory {};
	if (!VirtualQuery(reinterpret_cast<const void*>(address), &memory, sizeof(memory)) || memory.State != MEM_COMMIT || (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
	const DWORD protection = memory.Protect & 0xFF;
	if (protection != PAGE_READWRITE && protection != PAGE_WRITECOPY && protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY) return false;
	const auto start = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
	return address >= start && address + size <= start + memory.RegionSize;
}

template <std::size_t N>
bool Matches(const std::uint8_t* bytes, const std::array<int, N>& pattern) noexcept {
	for (std::size_t i = 0; i < N; ++i)
		if (pattern[i] >= 0 && bytes[i] != static_cast<std::uint8_t>(pattern[i])) return false;
	return true;
}

// ---- the hook ----

// In memory D2R marks a color change with U+E07E (UTF-8 EE 81 BE) and the color
// character; the "ÿc" of the string tables is converted to it on load. D2RLoader
// colors its own ground-label additions (socket counts) the same way.
constexpr std::string_view ColorMarker = "\xEE\x81\xBE";

// Length of a leading run of color codes (marker + one character).
std::size_t LeadingColorCodes(std::string_view text) noexcept {
	std::size_t at = 0;
	while (text.size() >= at + ColorMarker.size() + 1 && text.substr(at, ColorMarker.size()) == ColorMarker) at += ColorMarker.size() + 1;
	return at;
}

// D2R text color codes: name -> color character (the one after "ÿc" in string tables). "item" = same as the item name.
constexpr std::array<std::pair<std::string_view, char>, 39> Colors { {
	{ "white", '0' }, { "white2", '=' }, { "gray", '5' }, { "gray2", 'K' }, { "gray3", 'I' }, { "black", '6' }, { "black2", 'M' },
	{ "lightred", 'E' }, { "red", '1' }, { "red2", 'U' }, { "darkred", 'S' },
	{ "orange", '@' }, { "orange2", '8' }, { "orange3", 'J' }, { "orange4", 'L' },
	{ "lightgold", '7' }, { "lightgold2", 'H' }, { "gold", '4' }, { "gold2", 'D' },
	{ "yellow", '9' }, { "yellow2", 'R' },
	{ "green", '2' }, { "green2", 'Q' }, { "green3", 'C' }, { "green4", '<' }, { "darkgreen", 'A' }, { "darkgreen2", ':' },
	{ "turquoise", 'N' }, { "skyblue", 'T' }, { "lightblue", 'F' }, { "lightblue2", 'P' }, { "blue", '3' }, { "blue2", 'B' },
	{ "lightpink", 'G' }, { "pink", 'O' }, { "purple", ';' },
	{ "grey", '5' }, { "grey2", 'K' }, { "grey3", 'I' },
} };

std::string ColorCode(char c) {
	return std::string(ColorMarker) + c;
}

// The code that colors the item's name: its own leading code when it has one,
// otherwise the color D2R uses for its quality (ethereal white items are gray).
std::string ItemColor(std::string_view name, std::uintptr_t itemData) noexcept {
	const std::size_t leading = LeadingColorCodes(name);
	if (leading) return std::string(name.substr(leading - ColorMarker.size() - 1, ColorMarker.size() + 1)); // the last leading code
	std::uint32_t quality {};
	if (!Read(itemData, quality)) return {};
	switch (quality) {
	case 1: case 2: case 3: return ColorCode('5'); // low, normal, superior
	case 4: return ColorCode('3');                 // magic
	case 5: return ColorCode('2');                 // set
	case 6: return ColorCode('9');                 // rare
	case 7: return ColorCode('4');                 // unique
	case 8: return ColorCode('8');                 // crafted
	default: return {};
	}
}

// What happened to each label, counted for the eth-tag console command.
enum Outcome : std::size_t { NotItem, OtherRecord, NotEthereal, NoText, AlreadyTagged, TooLong, NotWritable, Tagged, OutcomeCount };
constexpr std::array<const char*, OutcomeCount> OutcomeNames { "not an item", "other record", "not ethereal", "no text", "already tagged", "too long", "not writable", "tagged" };
std::array<std::atomic<std::uint32_t>, OutcomeCount> Outcomes {};
std::array<std::atomic<std::uint32_t>, 4> Calls {}; // writer call/entry, measure A, measure B, hover label

Outcome TagIfEthereal(void* unit, void* output, std::uint32_t bufferSize) noexcept;
Outcome TagText(void* unit, std::uintptr_t text, std::size_t capacity) noexcept;

// path: 0 writer, 1 measure A, 2 measure B (ground-label records), 3 hover label (plain text).
void Count(std::size_t path, void* unit, void* output, std::uint32_t bufferSize) noexcept {
	Calls[path].fetch_add(1, std::memory_order_relaxed);
	const Outcome outcome = path == 3 ? TagText(unit, reinterpret_cast<std::uintptr_t>(output), bufferSize) : TagIfEthereal(unit, output, bufferSize);
	const std::uint32_t seen = Outcomes[outcome].fetch_add(1, std::memory_order_relaxed);
#ifdef ETH_TAG_DUMP
	// The first few labels of each kind (other than non-items), with their text after tagging.
	if (outcome != NotItem && seen < 4) {
		std::array<char, LabelBufferBytes> text {};
		const std::size_t skip = path == 3 ? 0 : TextOffset;
		Read(reinterpret_cast<std::uintptr_t>(output) + skip, text.data(), text.size() - skip - 1);
		Log(false, "label path %zu: %s: \"%s\"", path, OutcomeNames[outcome], text.data());
	}
#else
	(void)seen;
#endif
}

Outcome TagIfEthereal(void* unit, void* output, std::uint32_t bufferSize) noexcept {
	const auto unitAddress   = reinterpret_cast<std::uintptr_t>(unit);
	const auto outputAddress = reinterpret_cast<std::uintptr_t>(output);
	if (outputAddress < RecordFromOutput) return OtherRecord;

	std::uint32_t header[3] {}; // type, class id, unit id
	if (!Read(unitAddress, header, sizeof(header)) || header[0] != UnitTypeItem || header[2] == 0) return NotItem;
	// Only the formatter's ground-label record, which carries the same unit id.
	std::uint32_t recordId {};
	if (!Read(outputAddress - RecordFromOutput + RecordUnitId, recordId) || recordId != header[2]) return OtherRecord;
	return TagText(unit, outputAddress + TextOffset, bufferSize - TextOffset);
}

// Tags the NUL-terminated text at text (capacity bytes) when unit is an ethereal item.
Outcome TagText(void* unit, std::uintptr_t text, std::size_t capacity) noexcept {
	const auto unitAddress = reinterpret_cast<std::uintptr_t>(unit);
	std::uint32_t header[3] {}; // type, class id, unit id
	if (!Read(unitAddress, header, sizeof(header)) || header[0] != UnitTypeItem || header[2] == 0) return NotItem;

	std::uintptr_t itemData {};
	std::uint32_t  flags {};
	if (!Read(unitAddress + UnitItemData, itemData) || !Read(itemData + ItemDataFlags, flags) || !(flags & ItemFlagEthereal)) return NotEthereal;

	std::array<char, LabelBufferBytes> current {};
	capacity = std::min(capacity, current.size());
	if (!Read(text, current.data(), capacity)) return NoText;
	const auto* end = static_cast<const char*>(std::memchr(current.data(), '\0', capacity));
	if (!end || end == current.data()) return NoText;
	const std::string_view name(current.data(), static_cast<std::size_t>(end - current.data()));

	// [bracket color]open [tag color]text [bracket color]close, then the item's color again.
	// Without the item's color the name could not be colored back, so the tag stays plain.
	const std::string item    = ItemColor(name, itemData);
	const std::string bracket = item.empty() || Config.bracketColor.empty() ? item : ColorCode(Config.bracketColor[0]);
	std::string tag = item.empty() || Config.tagColor.empty() ? item : ColorCode(Config.tagColor[0]);
	tag.append(Config.text);
	if (!Config.open.empty()) tag = bracket + Config.open + tag + bracket + Config.close;
	if (name.find(tag) != std::string_view::npos) return AlreadyTagged;

	std::string tagged;
	if (Config.before) {
		const std::size_t at = LeadingColorCodes(name);
		tagged.append(name.substr(0, at)).append(tag).append(item).append(" ").append(name.substr(at));
	} else {
		std::size_t at = name.find('\n'); // keep second lines added by other plugins below
		if (at == std::string_view::npos) at = name.size();
		tagged.append(name.substr(0, at)).append(" ").append(tag);
		if (at < name.size()) tagged.append(item).append(name.substr(at));
	}
	if (tagged.size() + 1 > capacity) return TooLong;
	if (!Writable(text, tagged.size() + 1)) return NotWritable;
	std::memcpy(reinterpret_cast<void*>(text), tagged.c_str(), tagged.size() + 1);
	return Tagged;
}

// Called either from the formatter's call site (another plugin's hook on the
// writer still runs, inside our call) or as the inline hook on the writer entry
// (another plugin's relay at the call site still runs, before our call).
std::uint64_t HookNameWriterCall(void* unit, void* output, std::uint32_t bufferSize, void* metadata) noexcept {
	const NameWriterFn writer = NameWriter;
	if (!writer) return 0;
	const std::uint64_t result = writer(unit, output, bufferSize, metadata);
	if (Active.load(std::memory_order_acquire) && unit && output && bufferSize == LabelBufferBytes) Count(0, unit, output, bufferSize);
	return result;
}

// ---- finding the writer and its call site ----

struct Code {
	std::uintptr_t            start {}; // address of .text
	std::vector<std::uint8_t> bytes;
	std::uint64_t Rva(std::size_t offset) const noexcept { return start - Base + offset; }
	bool          Has(std::uint64_t rva, std::size_t size) const noexcept { return rva >= start - Base && rva - (start - Base) + size <= bytes.size(); }
	std::size_t   Offset(std::uint64_t rva) const noexcept { return static_cast<std::size_t>(rva - (start - Base)); }
};

bool ReadCode(Code& code) noexcept {
	IMAGE_DOS_HEADER   dos {};
	IMAGE_NT_HEADERS64 nt {};
	if (!Read(Base, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || !Read(Base + dos.e_lfanew, nt) || nt.Signature != IMAGE_NT_SIGNATURE) return false;
	const std::uintptr_t sections = Base + dos.e_lfanew + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + nt.FileHeader.SizeOfOptionalHeader;
	for (WORD i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
		IMAGE_SECTION_HEADER section {};
		if (!Read(sections + i * sizeof(section), section)) return false;
		if (std::memcmp(section.Name, ".text", 6) != 0) continue;
		code.start = Base + section.VirtualAddress;
		code.bytes.resize(section.Misc.VirtualSize);
		constexpr std::size_t chunk = 1 << 20;
		for (std::size_t at = 0; at < code.bytes.size(); at += chunk)
			if (!Read(code.start + at, code.bytes.data() + at, std::min(chunk, code.bytes.size() - at))) return false;
		return true;
	}
	return false;
}

// Target RVA of an E8 rel32 call at offset, or 0.
std::uint64_t CallTarget(const Code& code, std::size_t offset) noexcept {
	if (offset + 5 > code.bytes.size() || code.bytes[offset] != 0xE8) return 0;
	std::int32_t rel;
	std::memcpy(&rel, code.bytes.data() + offset + 1, sizeof(rel));
	return code.Rva(offset) + 5 + rel;
}

// Fixed bytes at the start of the writer (the pattern up to its first wildcard).
std::array<std::uint8_t, 12> WriterPrologue() noexcept {
	std::array<std::uint8_t, 12> bytes {};
	for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<std::uint8_t>(NameWriterPattern[i]);
	return bytes;
}

// Plugin ids of D2RLoader-tracked changes to the size bytes at rva, which
// should read expected. False when the bytes are unchanged or the service is missing.
bool ChangedBy(std::uint64_t rva, const std::uint8_t* expected, std::uint32_t size, std::string& owners) noexcept {
	const D2RL::DiagnosticsService* diagnostics {};
	if (Context->QueryService(&diagnostics) != D2RL::ServiceQueryResult::Success || !D2RL::HasDiagnosticsServiceField(diagnostics, D2RL::DiagnosticsServiceEnumerateModificationRangesFieldEnd)) return false;
	const D2RL::Diagnostics::HookQuery query { .structSize = D2RL::Diagnostics::HookQuerySize, .rva = rva, .expected = expected, .expectedSize = size };
	std::array<D2RL::Diagnostics::ModificationRange, 8> ranges {};
	for (auto& range : ranges) range.structSize = D2RL::Diagnostics::ModificationRangeSize;
	std::uint32_t count = 0;
	if (diagnostics->enumerateModificationRanges(Context, &query, ranges.data(), static_cast<std::uint32_t>(ranges.size()), &count) != D2RL::Diagnostics::Result::Success || count == 0) return false;
	owners.clear();
	for (std::uint32_t i = 0; i < count && i < ranges.size(); ++i) {
		if (!owners.empty()) owners += ", ";
		owners += ranges[i].state == D2RL::Diagnostics::ModificationState::Tracked && ranges[i].ownerPluginId[0] ? ranges[i].ownerPluginId : "unknown";
	}
	return true;
}

// Another plugin's D2RLoader-managed inline hook on the writer.
bool HookedByPlugin(std::uint64_t rva, std::string& owner) noexcept {
	const D2RL::DiagnosticsService* diagnostics {};
	if (Context->QueryService(&diagnostics) != D2RL::ServiceQueryResult::Success || !D2RL::HasDiagnosticsServiceField(diagnostics, D2RL::DiagnosticsServiceEnumerateModificationRangesFieldEnd)) return false;
	const auto expected = WriterPrologue();
	const D2RL::Diagnostics::HookQuery query { .structSize = D2RL::Diagnostics::HookQuerySize, .rva = rva, .expected = expected.data(), .expectedSize = static_cast<std::uint32_t>(expected.size()) };
	std::array<D2RL::Diagnostics::ModificationRange, 8> ranges {};
	for (auto& range : ranges) range.structSize = D2RL::Diagnostics::ModificationRangeSize;
	std::uint32_t count = 0;
	if (diagnostics->enumerateModificationRanges(Context, &query, ranges.data(), static_cast<std::uint32_t>(ranges.size()), &count) != D2RL::Diagnostics::Result::Success || count == 0) return false;
	for (std::uint32_t i = 0; i < count && i < ranges.size(); ++i) {
		const auto& range = ranges[i];
		if (range.state != D2RL::Diagnostics::ModificationState::Tracked || range.kind != D2RL::Diagnostics::ModificationKind::InlineHook) return false;
		owner = range.ownerPluginId[0] ? range.ownerPluginId : "another plugin";
	}
	return true;
}

bool WriterEntryFree = false; // the writer's prologue is untouched, so its entry can be hooked

std::uint64_t FindWriter(const Code& code) noexcept {
	if (code.Has(KnownNameWriterRva, NameWriterPattern.size())) {
		if (Matches(code.bytes.data() + code.Offset(KnownNameWriterRva), NameWriterPattern)) {
			WriterEntryFree = true;
			LocateMethod = "writer at known address";
			return KnownNameWriterRva;
		}
		std::string owner;
		if (HookedByPlugin(KnownNameWriterRva, owner)) {
			LocateMethod = "writer at known address, hooked by " + owner;
			return KnownNameWriterRva;
		}
	}
	std::vector<std::size_t> hits;
	for (std::size_t i = 0; i + NameWriterPattern.size() <= code.bytes.size(); ++i)
		if (Matches(code.bytes.data() + i, NameWriterPattern)) hits.push_back(i);
	// Keep the candidates called from the start of a label formatter.
	std::vector<std::uint64_t> confirmed;
	for (std::size_t f = 0; f + FormatterCallWindow + 5 <= code.bytes.size(); ++f) {
		if (!Matches(code.bytes.data() + f, FormatterPattern)) continue;
		for (std::size_t i = f; i < f + FormatterCallWindow; ++i)
			for (const std::size_t hit : hits)
				if (CallTarget(code, i) == code.Rva(hit)) confirmed.push_back(code.Rva(hit));
	}
	Log(false, "scan: %zu writer pattern hit(s), %zu confirmed by a formatter", hits.size(), confirmed.size());
	if (confirmed.size() != 1) return 0;
	WriterEntryFree = true;
	LocateMethod = "writer found by pattern scan";
	return confirmed[0];
}

// The single call to the writer from the start of the label formatter.
std::uint64_t FindCallSite(const Code& code, std::uint64_t writer) noexcept {
	if (code.Has(KnownCallSiteRva, 5) && CallTarget(code, code.Offset(KnownCallSiteRva)) == writer) return KnownCallSiteRva;
	std::vector<std::uint64_t> sites;
	for (std::size_t f = 0; f + FormatterCallWindow + 5 <= code.bytes.size(); ++f) {
		if (!Matches(code.bytes.data() + f, FormatterPattern)) continue;
		for (std::size_t i = f; i < f + FormatterCallWindow; ++i)
			if (CallTarget(code, i) == writer) sites.push_back(code.Rva(i));
	}
	Log(false, "scan: %zu formatter call site(s) to the writer", sites.size());
	return sites.size() == 1 ? sites[0] : 0;
}

// 16 bytes of int3 padding between functions, never executed, for the jump to our hook.
std::uint64_t UsedCave {}; // last cave taken, so the next one is never the same (the code copy is not updated)

std::uint64_t FindCave(const Code& code, std::uint64_t after = 0) noexcept {
	constexpr std::size_t need = 16, margin = 4;
	for (std::size_t i = margin; i + need + margin <= code.bytes.size(); i += 16) {
		if (after && code.Rva(i) <= after + need) continue;
		bool padding = true;
		for (std::size_t j = i - margin; j < i + need + margin && padding; ++j) padding = code.bytes[j] == 0xCC;
		if (padding) return code.Rva(i);
	}
	return 0;
}

std::uint64_t TakeCave(const Code& code) noexcept {
	const std::uint64_t found = FindCave(code, UsedCave);
	if (found) UsedCave = found;
	return found;
}

// ---- attach point 0: the formatter's measure calls ----

constexpr std::uint64_t KnownFormatterRva = 0x1FA9F0;
constexpr std::size_t   MeasureWindow     = 0xC0; // both measure calls sit within this on 93787
// The formatter's setup: mov r8d, 0x80 (buffer size) ... mov r15, rcx (unit kept in r15).
constexpr std::array<int, 12> FormatterSetupPattern { 0x41, 0xB8, 0x80, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xDA, 0x4C, 0x8B, 0xF9 };
// Measure A: lea r8, [rbp-0x18] ; mov rcx, rbx ; call
constexpr std::array<int, 8> MeasureAPattern { 0x4C, 0x8D, 0x45, 0xE8, 0x48, 0x8B, 0xCB, 0xE8 };
// Measure B: mov rcx, rbx ; lea rdx, [rbp-0x1C] ; call
constexpr std::array<int, 8> MeasureBPattern { 0x48, 0x8B, 0xCB, 0x48, 0x8D, 0x55, 0xE4, 0xE8 };

template <std::size_t N>
std::vector<std::size_t> FindIn(const Code& code, std::size_t from, std::size_t size, const std::array<int, N>& pattern) noexcept {
	std::vector<std::size_t> hits;
	for (std::size_t i = from; i < from + size && i + N <= code.bytes.size(); ++i)
		if (Matches(code.bytes.data() + i, pattern)) hits.push_back(i);
	return hits;
}

struct MeasureSites {
	std::uint64_t formatter {}, a {}, b {}; // RVAs of the formatter and of the two E8 calls
};

// A formatter whose setup keeps the unit in r15 and the buffer in rbx, with
// exactly one call of each measure shape.
bool MeasureSitesAt(const Code& code, std::size_t offset, MeasureSites& sites) noexcept {
	if (offset + MeasureWindow > code.bytes.size() || !Matches(code.bytes.data() + offset, FormatterPattern)) return false;
	if (FindIn(code, offset, 0x30, FormatterSetupPattern).size() != 1) return false;
	const auto a = FindIn(code, offset, MeasureWindow, MeasureAPattern);
	const auto b = FindIn(code, offset, MeasureWindow, MeasureBPattern);
	if (a.size() != 1 || b.size() != 1) return false;
	sites = { code.Rva(offset), code.Rva(a[0] + 7), code.Rva(b[0] + 7) };
	return true;
}

bool FindMeasureSites(const Code& code, MeasureSites& sites) noexcept {
	if (code.Has(KnownFormatterRva, MeasureWindow) && MeasureSitesAt(code, code.Offset(KnownFormatterRva), sites)) return true;
	std::vector<MeasureSites> found;
	for (std::size_t f = 0; f + MeasureWindow <= code.bytes.size(); ++f) {
		MeasureSites candidate;
		if (MeasureSitesAt(code, f, candidate)) found.push_back(candidate);
	}
	Log(false, "scan: %zu formatter(s) with both measure calls", found.size());
	if (found.size() != 1) return false;
	sites = found[0];
	return true;
}

// cave: jmp [rip+0] ; dq target, then the call at site goes to the cave.
bool RedirectCall(const Code& code, std::uint64_t site, std::uint64_t cave, void* target) noexcept {
	std::array<std::uint8_t, 14> jump { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
	const auto address = reinterpret_cast<std::uint64_t>(target);
	std::memcpy(jump.data() + 6, &address, sizeof(address));
	std::array<std::uint8_t, 14> padding {};
	padding.fill(0xCC);
	if (!Context->PatchBytes(cave, padding.data(), static_cast<std::uint32_t>(padding.size()), jump.data(), static_cast<std::uint32_t>(jump.size()))) return false;
	return D2RL::PatchCallRel32(Context, site, code.bytes.data() + code.Offset(site), 5, cave);
}

bool HookMeasureCalls(const Code& code) noexcept {
	MeasureSites sites;
	if (!FindMeasureSites(code, sites)) {
		LocateMethod = "measure calls not found";
		return false;
	}
	const std::uint64_t caveA = TakeCave(code);
	const std::uint64_t caveB = TakeCave(code);
	if (!caveA || !caveB) {
		LocateMethod = "no free padding for the measure jumps";
		return false;
	}
	// Set the originals before any call can reach the stubs.
	EthMeasureOriginalA = reinterpret_cast<void*>(Base + CallTarget(code, code.Offset(sites.a)));
	EthMeasureOriginalB = reinterpret_cast<void*>(Base + CallTarget(code, code.Offset(sites.b)));
	if (!RedirectCall(code, sites.a, caveA, reinterpret_cast<void*>(&EthMeasureThunkA))) {
		LocateMethod = "measure call A patch refused";
		return false;
	}
	if (!RedirectCall(code, sites.b, caveB, reinterpret_cast<void*>(&EthMeasureThunkB))) {
		// A is already live and tags the HD path; B only covers the other UI mode.
		Log(true, "measure call B patch refused; only measure call A is tagged");
	}
	LocateMethod = "formatter measure calls redirected";
	HookedRva    = sites.formatter;
	return true;
}

// ---- hover label (controller targeting, and mouse hover without Alt) ----
// D2R+0xC0420 builds the label of the hovered or targeted unit. For an item it
// writes the colored name to a 0x400 buffer, then copies it with
// strcpy_s(label, 0x80, name) into the label it draws. That copy is redirected
// to a stub that tags the name first (unit in rsi, name in r8). Another plugin
// may patch the 7 bytes before the copy (D2R+0xC08D9 on 93787), so they are
// not checked.

constexpr std::uint64_t KnownHoverCopyRva = 0xC08EC;
constexpr std::size_t   HoverCopyBytes    = 0x80;
// lea rcx,[rbp+0x1E0] ; call (color) ; 7 bytes ; mov edx,0x80 ; lea rcx,[rbp+0x5E0] ; call (copy)
constexpr std::array<int, 31> HoverCopyPattern {
	0x48, 0x8D, 0x8D, 0xE0, 0x01, 0x00, 0x00, 0xE8, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1,
	0xBA, 0x80, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x8D, 0xE0, 0x05, 0x00, 0x00,
};
constexpr std::size_t HoverCopyCallOffset = 31; // the E8 right after the pattern

bool HoverHooked = false;

std::uint64_t FindHoverCopy(const Code& code) noexcept {
	auto at = [&](std::size_t offset) {
		return offset + HoverCopyCallOffset + 5 <= code.bytes.size() && Matches(code.bytes.data() + offset, HoverCopyPattern) && code.bytes[offset + HoverCopyCallOffset] == 0xE8;
	};
	const std::uint64_t knownStart = KnownHoverCopyRva - HoverCopyCallOffset;
	if (code.Has(knownStart, HoverCopyCallOffset + 5) && at(code.Offset(knownStart))) return KnownHoverCopyRva;
	std::vector<std::uint64_t> hits;
	for (std::size_t i = 0; i + HoverCopyCallOffset + 5 <= code.bytes.size(); ++i)
		if (at(i)) hits.push_back(code.Rva(i + HoverCopyCallOffset));
	Log(false, "scan: %zu hover label copy call(s)", hits.size());
	return hits.size() == 1 ? hits[0] : 0;
}

bool HookHoverLabel(const Code& code) noexcept {
	const std::uint64_t site = FindHoverCopy(code);
	if (!site) {
		Log(true, "hover labels (controller) not tagged: label copy not found");
		return false;
	}
	const std::uint64_t cave = TakeCave(code);
	if (!cave) {
		Log(true, "hover labels (controller) not tagged: no free padding for the jump");
		return false;
	}
	EthHoverOriginal = reinterpret_cast<void*>(Base + CallTarget(code, code.Offset(site)));
	if (!RedirectCall(code, site, cave, reinterpret_cast<void*>(&EthHoverThunk))) {
		Log(true, "hover labels (controller) not tagged: patch at D2R+0x%llX refused", static_cast<unsigned long long>(site));
		return false;
	}
	Log(false, "hover labels (controller): label copy at D2R+0x%llX redirected", static_cast<unsigned long long>(site));
	return true;
}

// Inline hook on the writer entry, used when another plugin owns the formatter's call.
bool HookWriterEntry(std::uint64_t writer) noexcept {
	const auto expected = WriterPrologue();
	// The trampoline goes straight into NameWriter, so it is set as soon as the loader provides it.
	auto* target = reinterpret_cast<void*>(&HookNameWriterCall);
	if (!Context->InstallInlineHook(writer, expected.data(), static_cast<std::uint32_t>(expected.size()), target, reinterpret_cast<void**>(&NameWriter)) || !NameWriter) {
		LocateMethod += ", writer entry hook refused";
		return false;
	}
	HookedRva = writer;
	return true;
}

bool LocateAndHook() noexcept {
	Code code;
	if (!ReadCode(code)) {
		LocateMethod = "could not read D2R.exe code";
		return false;
	}
	HoverHooked = HookHoverLabel(code);
	if (HookMeasureCalls(code)) return true;
	Log(false, "%s; trying the label writer", LocateMethod.c_str());
	LocateMethod.clear();
	const std::uint64_t writer = FindWriter(code);
	if (!writer) {
		LocateMethod = "writer not found";
		return false;
	}
	const std::uint64_t site = FindCallSite(code, writer);
	if (!site) {
		// The formatter no longer calls the writer directly: another plugin patched that call.
		std::string owners;
		if (code.Has(KnownCallSiteRva, 5)) {
			std::array<std::uint8_t, 5> direct { 0xE8 };
			const std::int32_t rel = static_cast<std::int32_t>(static_cast<std::int64_t>(writer) - static_cast<std::int64_t>(KnownCallSiteRva + 5));
			std::memcpy(direct.data() + 1, &rel, sizeof(rel));
			if (ChangedBy(KnownCallSiteRva, direct.data(), 5, owners)) LocateMethod += ", formatter call patched by " + owners;
		}
		if (owners.empty()) LocateMethod += ", formatter call site not found";
		if (!WriterEntryFree) {
			LocateMethod += ", and the writer entry is hooked too";
			return false;
		}
		LocateMethod += ", hooking the writer entry";
		return HookWriterEntry(writer);
	}
	const std::uint64_t cave = TakeCave(code);
	if (!cave) {
		LocateMethod += ", no free padding for the jump";
		return false;
	}
	NameWriter = reinterpret_cast<NameWriterFn>(Base + writer);

	// cave: jmp [rip+0] ; dq HookNameWriterCall
	std::array<std::uint8_t, 14> jump { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
	const auto target = reinterpret_cast<std::uint64_t>(&HookNameWriterCall);
	std::memcpy(jump.data() + 6, &target, sizeof(target));
	std::array<std::uint8_t, 14> padding {};
	padding.fill(0xCC);
	if (!Context->PatchBytes(cave, padding.data(), static_cast<std::uint32_t>(padding.size()), jump.data(), static_cast<std::uint32_t>(jump.size()))) {
		LocateMethod += ", jump patch refused";
		return false;
	}
	// formatter: call writer -> call cave
	const std::uint8_t* original = code.bytes.data() + code.Offset(site);
	if (!D2RL::PatchCallRel32(Context, site, original, 5, cave)) {
		LocateMethod += ", call-site patch refused";
		return false;
	}
	LocateMethod += ", formatter call redirected";
	HookedRva = site;
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

void LoadConfig() {
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
		std::size_t at = 0;
		std::string parsed;
		if (key == "enabled") Config.enabled = value.rfind("false", 0) != 0;
		else if (key == "text" && ParseString(value, at, parsed) && !parsed.empty() && parsed.size() < 64) Config.text = parsed;
		else if (key == "position" && ParseString(value, at, parsed)) Config.before = parsed != "after" && parsed != "suffix";
		else if (key == "brackets" && ParseString(value, at, parsed)) {
			if (parsed == "parentheses" || parsed == "parenthesis") Config.open = "(", Config.close = ")";
			else if (parsed == "none") Config.open.clear(), Config.close.clear();
			else Config.open = "[", Config.close = "]";
		} else if ((key == "bracket_color" || key == "tag_color") && ParseString(value, at, parsed)) {
			std::string& color = key == "tag_color" ? Config.tagColor : Config.bracketColor;
			if (parsed == "item") color.clear();
			for (const auto& [colorName, code] : Colors)
				if (parsed == colorName) color = std::string(1, code);
		}
		else if (key == "versions" && !value.empty() && value[0] == '[') {
			std::vector<std::string> versions;
			for (at = 1; at < value.size() && value[at] != ']';) {
				if (value[at] == '"') {
					if (!ParseString(value, at, parsed)) break;
					versions.push_back(parsed);
				} else ++at;
			}
			if (!versions.empty()) Config.versions = versions;
		}
	}
}

#ifdef ETH_TAG_DUMP
// ---- diagnostic dump (test builds only) ----
// Writes the label code to the log so it can be disassembled offline: D2R.exe
// is encrypted on disk, so its code can only be read from memory.

void DumpBytes(const char* label, std::uintptr_t address, std::size_t size) noexcept {
	HMODULE module {};
	char    name[MAX_PATH] = "?";
	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(address), &module))
		GetModuleFileNameA(module, name, sizeof(name));
	const char* file = std::strrchr(name, '\\');
	Log(false, "dump %s: %p = %s+0x%llX, %zu bytes", label, reinterpret_cast<void*>(address), file ? file + 1 : name,
	    static_cast<unsigned long long>(address - reinterpret_cast<std::uintptr_t>(module)), size);
	for (std::size_t at = 0; at < size; at += 32) {
		std::array<std::uint8_t, 32> bytes {};
		const std::size_t count = std::min<std::size_t>(32, size - at);
		if (!Read(address + at, bytes.data(), count)) {
			Log(false, "dump %s +%04zX unreadable", label, at);
			return;
		}
		char line[80] {};
		for (std::size_t i = 0; i < count; ++i) std::snprintf(line + i * 2, 3, "%02X", bytes[i]);
		Log(false, "dump %s +%04zX %s", label, at, line);
	}
}

// Where a 5-byte E8 call or a 6-byte FF 25 jump at address leads, or 0.
std::uintptr_t Follow(std::uintptr_t address) noexcept {
	std::array<std::uint8_t, 6> op {};
	if (!Read(address, op.data(), op.size())) return 0;
	std::int32_t rel;
	std::memcpy(&rel, op.data() + 1, sizeof(rel));
	if (op[0] == 0xE8 || op[0] == 0xE9) return address + 5 + rel;
	if (op[0] == 0xFF && op[1] == 0x25) {
		std::memcpy(&rel, op.data() + 2, sizeof(rel));
		std::uintptr_t target {};
		return Read(address + 6 + rel, target) ? target : 0;
	}
	return 0;
}

void DumpLabelCode() noexcept {
	Log(false, "dump begin (Ethereal Ground Tag %s, game %s)", PluginVersion, GameVersion.c_str());
	DumpBytes("formatter", Base + 0x1FA9F0, 0x600);
	DumpBytes("writer-entry", Base + KnownNameWriterRva, 0x40);
	// Follow the formatter's call and the writer entry through other plugins' relays.
	std::uintptr_t at = Base + KnownCallSiteRva;
	for (int hop = 0; hop < 4 && (at = Follow(at)) != 0 && at != Base + KnownNameWriterRva; ++hop) {
		char label[32];
		std::snprintf(label, sizeof(label), "call-hop%d", hop);
		DumpBytes(label, at, 0x100);
	}
	at = Base + KnownNameWriterRva;
	for (int hop = 0; hop < 4 && (at = Follow(at)) != 0; ++hop) {
		char label[32];
		std::snprintf(label, sizeof(label), "entry-hop%d", hop);
		DumpBytes(label, at, 0x100);
	}
	// Calls to the formatter, in case the text is measured by its caller.
	Code code;
	if (ReadCode(code)) {
		int callers = 0;
		for (std::size_t i = 0; i + 5 <= code.bytes.size() && callers < 8; ++i)
			if (CallTarget(code, i) == 0x1FA9F0) {
				char label[32];
				std::snprintf(label, sizeof(label), "caller%d", callers++);
				DumpBytes(label, code.start + i - 0x100, 0x180);
			}
		Log(false, "dump: %d formatter caller(s)", callers);
	}
	Log(false, "dump end");
}

// The whole D2R.exe image as mapped in memory (unreadable pages left zero),
// written beside this DLL as D2R-<version>.image.bin for offline disassembly.
bool DumpImage(std::string& path) noexcept {
	IMAGE_DOS_HEADER   dos {};
	IMAGE_NT_HEADERS64 nt {};
	if (!Read(Base, dos) || !Read(Base + dos.e_lfanew, nt)) return false;
	HMODULE self {};
	char    dll[MAX_PATH] {};
	if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(&DumpImage), &self) || !GetModuleFileNameA(self, dll, sizeof(dll))) return false;
	path = dll;
	path = path.substr(0, path.find_last_of('\\') + 1) + "D2R-" + GameVersion + ".image.bin";
	FILE* file = std::fopen(path.c_str(), "wb");
	if (!file) return false;
	std::vector<std::uint8_t> page(0x1000);
	for (std::size_t at = 0; at < nt.OptionalHeader.SizeOfImage; at += page.size()) {
		if (!Read(Base + at, page.data(), page.size())) std::fill(page.begin(), page.end(), 0);
		std::fwrite(page.data(), 1, page.size(), file);
	}
	std::fclose(file);
	return true;
}

auto DumpCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
	if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
	DumpLabelCode();
	std::string path;
	if (DumpImage(path)) Log(false, "dump: image written to %s", path.c_str());
	else Log(true, "dump: could not write the image");
	command->plugin->WriteConsoleMessage("ethereal-ground-tag: label code and D2R image written; see d2rloader/logs/ethereal-ground-tag.log.");
	return D2RL::ConsoleCommandResult::Handled;
}
#endif

// ---- console ----

auto StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
	if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
	char line[256] {};
	if (Active.load()) std::snprintf(line, sizeof(line), "ethereal-ground-tag: active on %s at D2R+0x%llX (%s).", GameVersion.c_str(), static_cast<unsigned long long>(HookedRva), LocateMethod.c_str());
	else std::snprintf(line, sizeof(line), "ethereal-ground-tag: inactive on %s; see d2rloader/logs/ethereal-ground-tag.log.", GameVersion.c_str());
	command->plugin->WriteConsoleMessage(line);
	if (Active.load()) {
		std::snprintf(line, sizeof(line), "calls: writer %u, measure A %u, measure B %u, hover %u", Calls[0].load(), Calls[1].load(), Calls[2].load(), Calls[3].load());
		command->plugin->WriteConsoleMessage(line);
		Log(false, "%s", line);
		std::string counts = "labels:";
		for (std::size_t i = 0; i < OutcomeCount; ++i) counts += std::string(i ? ", " : " ") + OutcomeNames[i] + " " + std::to_string(Outcomes[i].load());
		command->plugin->WriteConsoleMessage(counts.c_str());
		Log(false, "%s", counts.c_str());
	}
	return D2RL::ConsoleCommandResult::Handled;
}

}

extern "C" void EthTagFromMeasureA(void* unit, void* output) noexcept {
	if (Active.load(std::memory_order_acquire) && unit && output) Count(1, unit, output, LabelBufferBytes);
}

extern "C" void EthTagFromMeasureB(void* unit, void* output) noexcept {
	if (Active.load(std::memory_order_acquire) && unit && output) Count(2, unit, output, LabelBufferBytes);
}

extern "C" void EthTagFromHover(void* unit, void* text) noexcept {
	if (Active.load(std::memory_order_acquire) && unit && text) Count(3, unit, text, HoverCopyBytes);
}

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (!D2RL::HasContext(context) || !context->GetApi()) return false;
	Context = context;
	Base    = context->exeBase;
	(void)context->RegisterConsoleCommand("eth-tag", StatusCommand, "Show Ethereal Ground Tag status.");
#ifdef ETH_TAG_DUMP
	(void)context->RegisterConsoleCommand("eth-tag-dump", DumpCommand, "Write the ground-label code to the Ethereal Ground Tag log.");
#endif

	LoadConfig();
	GameVersion = ReadGameVersion();
	const std::string majorMinor = MajorMinor(GameVersion);
	Log(false, "Ethereal Ground Tag %s: game %s, text \"%s\" %s", PluginVersion, GameVersion.c_str(), Config.text.c_str(), Config.before ? "before the name" : "after the name");
	if (!Config.enabled) {
		Log(false, "disabled in config");
		return true;
	}
	bool supported = false;
	for (const auto& version : Config.versions) supported |= version == majorMinor;
	if (!supported) {
		Log(true, "inactive: game version %s is not in versions (edit the config to allow it)", majorMinor.c_str());
		return true;
	}
	const bool ground = Base && LocateAndHook();
	if (!ground && !HoverHooked) {
		Log(true, "inactive: ground-label name writer not found or hook refused (%s)", LocateMethod.empty() ? "no match" : LocateMethod.c_str());
		return true;
	}
	Active.store(true, std::memory_order_release);
	if (ground) Log(false, "active: name writer hooked at D2R+0x%llX (%s)", static_cast<unsigned long long>(HookedRva), LocateMethod.c_str());
	else Log(true, "active for hover labels only: ground labels not tagged (%s)", LocateMethod.empty() ? "no match" : LocateMethod.c_str());
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	Active.store(false, std::memory_order_release);
}
