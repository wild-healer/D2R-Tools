// Quickcast Save Fix: with Quick Cast on, a skill hotkey puts its skill on your
// left or right click for the cast and remembers the skill that was there. The
// game puts that skill back only on your next click, so a save before it
// (Save and Exit mid-cast or right after a cast) wrote the hotkey skill as your
// left/right skill. The plugin gives the save your own skills.
//
// Game code used (Steam 3.3.93787 / 94995 RVAs):
//   Client (UI thread)
//   - quick cast state: a static object (address from the lea at 0x144669 in
//     its getter 0x144640), 0xB8 bytes per player index (PlayerByIndex 0x9A480,
//     LocalIndex 0x8B2D0). Left skill to put back: +0x197C skill id, +0x1980
//     byte pending, +0x198C granting item; right: +0x1984, +0x1988, +0x1990.
//     Set by the hotkey cast 0x14A5A0, put back by 0x14A230 (from the mouse
//     click handlers 0x839E0 / 0x83FC0) and 0x14A1B0, which select it again
//     with packet 0x3C.
//   Server (game thread)
//   - the save writer (Game*, Unit* player, buffer, size*, capacity, flag) is
//     reached through the thunk 0x3E2B69A from the leave save 0x41E0D0 (call at
//     0x41E207, Save and Exit) and 0x4135D0 (call at 0x413631). Both calls are
//     sent to the plugin through a jump in int3 padding.
//   - skills: unit +0x100 (getter 0x34B6E0) holds the left skill node at +8
//     and the right one at +0x10. SetLeftSkill 0x33EC70 / SetRightSkill
//     0x33EF10 (unit, skill id, granting item) pick the node the way the game's
//     own skill select does, with no packet and no other effect. The nodes the
//     save saw are written back after it.

#include <D2RLPlugin/api.h>
#include <windows.h>

#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr const char* PluginId      = "quickcast-save-fix";
constexpr const char* PluginVersion = "1.0.0";

const D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = PluginId,
	.name        = "Quickcast Save Fix",
	.version     = PluginVersion,
	.author      = "wild-healer",
	.description = "Saving during or right after a quick cast keeps your own left and right skills.",
	.flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

struct Settings {
	bool enabled = true;
	bool logFixes = true;
} Config;

// ---- native layout (Steam 3.3.93787 / 94995) ----

using Code = std::array<std::uint8_t, 16>;

constexpr std::array<std::uint64_t, 2> WriterCallRvas { 0x41E207, 0x413631 }; // call thunk 0x3E2B69A
constexpr std::array<std::array<std::uint8_t, 5>, 2> WriterCallBytes { { { 0xE8, 0x8E, 0xD4, 0xA0, 0x03 }, { 0xE8, 0x64, 0x80, 0xA1, 0x03 } } };
constexpr std::uint64_t WriterThunkRva = 0x3E2B69A;
constexpr std::array<std::uint8_t, 6> WriterThunkBytes { 0xFF, 0x25, 0xA8, 0xF6, 0xFF, 0xFF }; // jmp [rip-0x958]
constexpr std::uint64_t SetLeftSkillRva  = 0x33EC70;
constexpr std::uint64_t SetRightSkillRva = 0x33EF10;
constexpr Code SetSideSkillBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57 }; // both
constexpr std::uint64_t SkillsRva = 0x34B6E0;
constexpr Code SkillsBytes { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x75, 0x20, 0x88, 0x4C };
constexpr std::uint64_t PlayerByIndexRva = 0x9A480;
constexpr Code PlayerByIndexBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x83, 0xF9, 0x08, 0x0F, 0x83, 0x85 };
constexpr std::uint64_t LocalIndexRva = 0x8B2D0;
constexpr std::array<std::uint8_t, 2> LocalIndexBytes { 0x8B, 0x05 }; // mov eax, [rip+x]; ret
constexpr std::uint64_t QuickCastLeaRva = 0x144669; // lea rax, [rip+x]: the quick cast state
constexpr std::array<std::uint8_t, 3> QuickCastLeaBytes { 0x48, 0x8D, 0x05 };
// The game's own put-back 0x14A230 reading the fields below: proves the layout.
constexpr std::uint64_t PutBackRva = 0x14A271;
constexpr std::array<std::uint8_t, 27> PutBackBytes { 0x80, 0xBC, 0x1F, 0x80, 0x19, 0x00, 0x00, 0x00, 0x74, 0x4C, 0x8B, 0xB4, 0x1F, 0x7C,
	                                                  0x19, 0x00, 0x00, 0xB1, 0x01, 0x44, 0x8B, 0x84, 0x1F, 0x8C, 0x19, 0x00, 0x00 };

constexpr std::uintptr_t QuickCastStride = 0xB8;
struct SideFields {
	std::uintptr_t skill, pending, owner, node;
	const char*    name;
};
constexpr std::array<SideFields, 2> Sides { {
	{ 0x197C, 0x1980, 0x198C, 0x08, "left" },
	{ 0x1984, 0x1988, 0x1990, 0x10, "right" },
} };

constexpr std::uintptr_t UnitTypeOffset = 0x00; // u32, 0 = player
constexpr std::uintptr_t UnitIdOffset   = 0x08; // u32
constexpr std::uintptr_t SkillTxtOffset = 0x00; // skills.txt row*, int16 id at +0
constexpr std::uint32_t  NoId           = 0xFFFFFFFF;
constexpr int            MaxSkillId     = 0x2000;

using WriterFn        = std::uint64_t (*)(void* game, void* player, void* buffer, void* size, std::uint64_t capacity, std::uint64_t flag) noexcept;
using SetSideSkillFn  = void (*)(void* unit, int skill, std::uint32_t owner) noexcept;
using SkillsFn        = void* (*)(void* unit) noexcept;
using PlayerByIndexFn = void* (*)(std::uint32_t index) noexcept;
using LocalIndexFn    = std::uint32_t (*)() noexcept;

const D2RL::PluginContext* Context {};
std::uintptr_t             Base {};
std::uintptr_t             QuickCast {}; // the quick cast state object

WriterFn        Writer {};
SetSideSkillFn  SetLeftSkill {};
SetSideSkillFn  SetRightSkill {};
SkillsFn        Skills {};
PlayerByIndexFn PlayerByIndex {};
LocalIndexFn    LocalIndex {};

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

template <std::size_t Size>
bool CodeMatches(std::uint64_t rva, const std::array<std::uint8_t, Size>& bytes) noexcept {
	std::array<std::uint8_t, Size> code {};
	return Read(Base + rva, code.data(), code.size()) && std::memcmp(code.data(), bytes.data(), code.size()) == 0;
}

// Target of a rip-relative operand whose 4-byte displacement is at `at` and
// whose instruction ends at `end`.
std::uintptr_t RipTarget(std::uintptr_t at, std::uintptr_t end) noexcept {
	std::int32_t displacement = 0;
	return Read(at, &displacement, sizeof(displacement)) ? end + displacement : 0;
}

int SkillId(void* node) noexcept {
	void*        txt = nullptr;
	std::int16_t id  = -1;
	return node && ReadField(node, SkillTxtOffset, txt) && ReadField(txt, 0, id) ? id : -1;
}

// ---- the save ----

// The skill a quick cast will put back on one side of the local player, or -1.
struct PutBack {
	int           skill = -1;
	std::uint32_t owner = NoId;
};

std::array<PutBack, 2> PendingPutBacks(void* player) noexcept {
	std::array<PutBack, 2> result {};
	std::uint32_t          type = NoId, id = NoId, localId = NoId;
	if (!ReadField(player, UnitTypeOffset, type) || type != 0 || !ReadField(player, UnitIdOffset, id)) return result;
	const std::uint32_t index = LocalIndex();
	if (index >= 8) return result;
	// The save runs on the game thread: the player it gets is the server's,
	// the quick cast state is the client's. Same unit id = you.
	void* const local = PlayerByIndex(index);
	if (!ReadField(local, UnitIdOffset, localId) || localId != id) return result;
	const auto* slot = reinterpret_cast<const void*>(QuickCast + index * QuickCastStride);
	for (std::size_t side = 0; side < Sides.size(); ++side) {
		std::uint8_t pending = 0;
		PutBack      back;
		if (!ReadField(slot, Sides[side].pending, pending) || !pending || !ReadField(slot, Sides[side].skill, back.skill) ||
		    !ReadField(slot, Sides[side].owner, back.owner) || back.skill < 0 || back.skill >= MaxSkillId)
			continue;
		result[side] = back;
	}
	return result;
}

std::uint64_t SaveWriter(void* game, void* player, void* buffer, void* size, std::uint64_t capacity, std::uint64_t flag) noexcept {
	const auto  backs  = PendingPutBacks(player);
	auto* const skills = static_cast<std::uint8_t*>(backs[0].skill >= 0 || backs[1].skill >= 0 ? Skills(player) : nullptr);
	std::array<void*, 2> before {};
	bool                 changed = false;
	if (skills) {
		for (std::size_t side = 0; side < Sides.size(); ++side) {
			std::memcpy(&before[side], skills + Sides[side].node, sizeof(void*));
			if (backs[side].skill < 0) continue;
			(side == 0 ? SetLeftSkill : SetRightSkill)(player, backs[side].skill, backs[side].owner);
			void* now = nullptr;
			std::memcpy(&now, skills + Sides[side].node, sizeof(void*));
			if (now == before[side]) continue;
			changed = true;
			if (Config.logFixes) Log(false, "save: %s skill %d (quick cast) saved as %d, your own skill", Sides[side].name, SkillId(before[side]), backs[side].skill);
		}
	}
	const std::uint64_t result = Writer(game, player, buffer, size, capacity, flag);
	// A save while you play on (not leaving): the cast goes on as it was.
	if (changed)
		for (std::size_t side = 0; side < Sides.size(); ++side) std::memcpy(skills + Sides[side].node, &before[side], sizeof(void*));
	return result;
}

// ---- version check and patches ----

bool Locate() noexcept {
	for (std::size_t i = 0; i < WriterCallRvas.size(); ++i)
		if (!CodeMatches(WriterCallRvas[i], WriterCallBytes[i])) return false;
	if (!CodeMatches(WriterThunkRva, WriterThunkBytes) || !CodeMatches(SetLeftSkillRva, SetSideSkillBytes) || !CodeMatches(SetRightSkillRva, SetSideSkillBytes) ||
	    !CodeMatches(SkillsRva, SkillsBytes) || !CodeMatches(PlayerByIndexRva, PlayerByIndexBytes) || !CodeMatches(LocalIndexRva, LocalIndexBytes) ||
	    !CodeMatches(QuickCastLeaRva, QuickCastLeaBytes) || !CodeMatches(PutBackRva, PutBackBytes))
		return false;
	QuickCast = RipTarget(Base + QuickCastLeaRva + 3, Base + QuickCastLeaRva + 7);
	if (!QuickCast) return false;
	Writer        = reinterpret_cast<WriterFn>(Base + WriterThunkRva);
	SetLeftSkill  = reinterpret_cast<SetSideSkillFn>(Base + SetLeftSkillRva);
	SetRightSkill = reinterpret_cast<SetSideSkillFn>(Base + SetRightSkillRva);
	Skills        = reinterpret_cast<SkillsFn>(Base + SkillsRva);
	PlayerByIndex = reinterpret_cast<PlayerByIndexFn>(Base + PlayerByIndexRva);
	LocalIndex    = reinterpret_cast<LocalIndexFn>(Base + LocalIndexRva);
	return true;
}

// A rel32 call has to land in the game image: a jmp [rip] to SaveWriter goes
// in int3 padding and both save calls are pointed at it.
bool RedirectSaves() noexcept {
	std::array<std::uint8_t, 14> jump { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
	const auto                   target = reinterpret_cast<std::uint64_t>(&SaveWriter);
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
			int redirected = 0;
			for (std::size_t i = 0; i < WriterCallRvas.size(); ++i) {
				if (D2RL::PatchCallRel32(Context, WriterCallRvas[i], WriterCallBytes[i].data(), 5, cave + 1)) ++redirected;
				else Log(true, "could not redirect the save call at D2R+0x%llX (another plugin may patch it)", static_cast<unsigned long long>(WriterCallRvas[i]));
			}
			return redirected > 0;
		}
	}
	Log(true, "no free padding found for the save redirect");
	return false;
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
		else if (key == "log_fixes") Config.logFixes = value != "false";
		else Log(true, "config: unknown key %s", key.c_str());
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
	if (!Config.enabled) {
		Log(false, "Quickcast Save Fix %s: disabled in config", PluginVersion);
		return true;
	}
	Base = context->exeBase;
	if (!Base || !Locate()) {
		Log(true, "Quickcast Save Fix %s: inactive, this game build isn't supported (Steam 3.3.93787 / 3.3.94995 are)", PluginVersion);
		return true;
	}
	if (!RedirectSaves()) {
		Log(true, "Quickcast Save Fix %s: inactive, the save could not be redirected", PluginVersion);
		return true;
	}
	Log(false, "Quickcast Save Fix %s: active", PluginVersion);
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {}
