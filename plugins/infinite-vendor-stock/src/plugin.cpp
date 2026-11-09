// Infinite Vendor Stock: whatever you buy from a vendor stays in the shop, so
// it can be bought again and again, as the game already does for potions,
// scrolls, keys, arrows and bolts.
//
// Game code used (Steam 3.3.93787 / 94995 RVAs), all on the server:
//   Buy 0x53D850 (game, player, npc, u32 itemGuid, dest page, dest x|y<<16,
//     Dest* clientDest, u32 cost, u8 gamble, u8 fillStack, u8 gambleDone):
//     one purchase from an NPC (the packet 0x32 handler reaches it through a
//     virtual call).
//   IsPermanent 0x38C7D0 (u8 gameType, u8 difficulty, i32 npcClass, item) -> bool:
//     the game's "this store item never sells out" test (a list of codes, then
//     the item's PermStoreItem column). The purchase asks it at
//       0x53E465: permanent -> the player gets a copy (0x43D660, a full copy
//                 through the item's save format: affixes, sockets and socketed
//                 items included), otherwise the item itself leaves the store
//                 (0x389820);
//       0x53E578: permanent -> the store keeps showing it (no remove update);
//       0x53ED16: the purchase failed -> the copy is freed, otherwise the item
//                 goes back to the store;
//       0x53DA64 / 0x53DD3F: the same for a scroll bought into a tome and for
//                 shift-buying into a stack.
// Hooks:
//   - Buy: notes, for the server thread, that a purchase (not a gamble) is
//     running.
//   - IsPermanent: true when called from one of the sites above during such a
//     purchase. Everything else, the client's own calls included, is left alone.
// Gambling is not changed: its items are rolled when bought.
// The server code only runs on the PC that hosts the game, so it works in
// single player and in games you host (for everyone in them), not in games you
// join. Each function's first bytes and each call site are checked at load;
// on a mismatch the plugin stays inactive.

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
#include <vector>

#include "i_hate_tomls_client.h"

namespace {

constexpr const char* PluginId      = "infinite-vendor-stock";
constexpr const char* PluginVersion = "0.0.1";

const D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = PluginId,
	.name        = "Infinite Vendor Stock",
	.version     = PluginVersion,
	.author      = "wild-healer",
	.description = "Items bought from vendors stay in the shop and can be bought again.",
	.flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

// Read by the hooks on the server thread, changed live from I Hate TOMLs.
struct Settings {
	std::atomic<bool> enabled { true };
} Config;

// ---- native layout (Steam 3.3.93787 / 94995) ----

using Code = std::array<std::uint8_t, 16>;

constexpr std::uint64_t BuyRva = 0x53D850;
constexpr Code BuyBytes { 0x4C, 0x8B, 0xDC, 0x55, 0x53, 0x56, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0x88, 0x48, 0x81, 0xEC, 0x78 };
constexpr std::uint64_t BuyEndRva = 0x53EEE7; // past its last instruction
constexpr std::uint64_t IsPermanentRva = 0x38C7D0;
constexpr Code IsPermanentBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x49 };

constexpr std::array<std::uint64_t, 5> PermanentSites {
	0x53E465, // give a copy instead of the store's item
	0x53E578, // keep it shown in the store
	0x53ED16, // failed purchase: free the copy instead of putting the item back
	0x53DA64, // shift-buy into a stack is allowed
	0x53DD3F, // scroll bought into a tome stays in the store
};
constexpr std::size_t CallSize = 5;

// Every argument is passed through untouched as a full register / stack slot.
using BuyFn         = std::uintptr_t (*)(void* game, void* player, void* npc, std::uintptr_t itemGuid, std::uintptr_t page, std::uintptr_t xy,
                                          std::uintptr_t clientDest, std::uintptr_t cost, std::uintptr_t gamble, std::uintptr_t fillStack,
                                          std::uintptr_t gambleDone) noexcept;
using IsPermanentFn = bool (*)(std::uint8_t gameType, std::uint8_t difficulty, std::int32_t npcClass, void* item) noexcept;

const D2RL::PluginContext* Context {};
std::uintptr_t             Base {};
BuyFn                      Buy {};         // original, after the hook
IsPermanentFn              IsPermanent {}; // original, after the hook

// The purchase running on this thread, if any (not a gamble).
thread_local int InPurchase = 0;
std::atomic<std::uint32_t> Kept {};

void Log(bool warn, const char* format, ...) noexcept {
	if (!Context) return;
	char    line[512] {};
	va_list args;
	va_start(args, format);
	std::vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	warn ? Context->LogWarn(line) : Context->LogInfo(line);
}

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

bool CodeMatches(std::uint64_t rva, const Code& bytes) noexcept {
	Code code {};
	return Read(Base + rva, code.data(), code.size()) && code == bytes;
}

bool CallsTo(std::uint64_t site, std::uint64_t target) noexcept {
	std::uint8_t call[CallSize] {};
	if (!Read(Base + site, call, sizeof(call)) || call[0] != 0xE8) return false;
	std::int32_t offset = 0;
	std::memcpy(&offset, call + 1, sizeof(offset));
	return site + CallSize + static_cast<std::int64_t>(offset) == target;
}

bool FromPurchaseSite(void* returnAddress) noexcept {
	const auto address = reinterpret_cast<std::uintptr_t>(returnAddress);
	for (const std::uint64_t site : PermanentSites)
		if (address == Base + site + CallSize) return true;
	return false;
}

// ---- server thread ----

std::uintptr_t HookBuy(void* game, void* player, void* npc, std::uintptr_t itemGuid, std::uintptr_t page, std::uintptr_t xy, std::uintptr_t clientDest,
                       std::uintptr_t cost, std::uintptr_t gamble, std::uintptr_t fillStack, std::uintptr_t gambleDone) noexcept {
	// The flag is read once per purchase, so all its checks agree.
	const bool keep = Config.enabled.load(std::memory_order_relaxed) && (gamble & 0xFF) == 0;
	if (keep) ++InPurchase;
	const std::uintptr_t result = Buy(game, player, npc, itemGuid, page, xy, clientDest, cost, gamble, fillStack, gambleDone);
	if (keep) --InPurchase;
	return result;
}

bool HookIsPermanent(std::uint8_t gameType, std::uint8_t difficulty, std::int32_t npcClass, void* item) noexcept {
	if (InPurchase > 0 && item && FromPurchaseSite(__builtin_return_address(0))) {
		if (!IsPermanent(gameType, difficulty, npcClass, item)) Kept.fetch_add(1, std::memory_order_relaxed);
		return true;
	}
	return IsPermanent(gameType, difficulty, npcClass, item);
}

// ---- load ----

bool Locate() noexcept {
	if (!CodeMatches(BuyRva, BuyBytes)) {
		Log(true, "game code differs at D2R+0x%llX", static_cast<unsigned long long>(BuyRva));
		return false;
	}
	if (!CodeMatches(IsPermanentRva, IsPermanentBytes)) {
		Log(true, "game code differs at D2R+0x%llX", static_cast<unsigned long long>(IsPermanentRva));
		return false;
	}
	for (const std::uint64_t site : PermanentSites) {
		if (site < BuyRva || site >= BuyEndRva || !CallsTo(site, IsPermanentRva)) {
			Log(true, "game code differs at D2R+0x%llX", static_cast<unsigned long long>(site));
			return false;
		}
	}
	return true;
}

template <typename Function>
bool Hook(std::uint64_t rva, const Code& bytes, Function target, Function& original) noexcept {
	Function trampoline = nullptr;
	if (!Context->InstallInlineHook(rva, bytes.data(), static_cast<std::uint32_t>(bytes.size()), target, &trampoline) || !trampoline) {
		Log(true, "could not hook D2R+0x%llX (another plugin may hook it)", static_cast<unsigned long long>(rva));
		return false;
	}
	original = trampoline;
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

bool Apply(const std::string& key, const std::string& value, std::string& error) {
	if (key == "enabled") {
		if (value != "true" && value != "false") return error = "true or false", false;
		Config.enabled = value == "true";
		return true;
	}
	return error = "unknown setting", false;
}

std::string ValueOf(const std::string& key) {
	if (key == "enabled") return Config.enabled ? "true" : "false";
	return {};
}

std::string Render() {
	return R"(# Infinite Vendor Stock
#
# Whatever you buy from a vendor stays in the shop, so you can buy it again
# and again. Gambling is not changed. Single player and games you host only.
# Every setting can also be changed in game with I Hate TOMLs, and applies at once.

[infinite-vendor-stock]
enabled = )" + ValueOf("enabled") + "\n";
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

constexpr const char* Schema = R"toml(title = "Infinite Vendor Stock"
[[setting]]
key = "enabled"
type = "bool"
label = "Bought items stay in the shop"
note = "Vendors never run out of anything (single player and games you host). Gambling is not changed."
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

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (!D2RL::HasContext(context) || !context->GetApi()) return false;
	Context = context;
	LoadConfig();
	(void)Menu.Start(context, Schema, MenuGet, MenuSet, nullptr);
	// Hooked even when turned off: it can be turned on while playing.
	Base = context->exeBase;
	if (!Base || !Locate()) {
		Log(true, "Infinite Vendor Stock %s: inactive, this game build isn't supported (Steam 3.3.93787 / 3.3.94995 are)", PluginVersion);
		return true;
	}
	// The test first, so a purchase never runs half hooked.
	if (!Hook(IsPermanentRva, IsPermanentBytes, &HookIsPermanent, IsPermanent) || !Hook(BuyRva, BuyBytes, &HookBuy, Buy)) {
		Log(true, "Infinite Vendor Stock %s: inactive, a hook could not be installed", PluginVersion);
		return true;
	}
	Log(false, "Infinite Vendor Stock %s: %s, vendor items stay in stock (single player and hosted games)", PluginVersion,
	    Config.enabled ? "active" : "loaded but turned off");
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	Menu.Stop();
	if (Context && Kept.load()) Log(false, "Infinite Vendor Stock: %u purchases kept in stock this session", Kept.load());
}
