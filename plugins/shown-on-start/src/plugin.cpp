// Shown on Start: item labels on the ground (the Show Items key) and the
// automap (the AutoMap key, Tab) start turned on, so you don't have to press
// their keys after launching D2R.
//
// How the game keeps it (Steam 3.4.95041 / 3.3.94995 RVAs):
// The Show Items state is a static array of two bytes, one per key: 0 = Show
// Items, 1 = Show Unfiltered Items. 0x1FAE30 `bool (mode)` reads it and
// 0x1FAE90 `void (mode, bool)` writes it. The key handler (0xC66A0) flips or
// sets it, then 0xCE450 applies it: it opens or closes the labels HUD and sets
// the "labels shown" flag read by the rest of the game (0x8AFC0). Nothing
// clears it between games while the Item Name Display option is Toggle, so the
// plugin sets it once, when you are first in a game after launch, through the
// same two calls the key uses. With the Hold option the game clears it again
// whenever a key is released, as it does for the key itself.
//
// The automap is the game panel 10. Panel open flags are a byte array at
// D2R+0x2A2ADA0 (one per panel id), and 0xCD7C0 `bool (int panel, int)` opens
// a panel; the game's own Automap Auto Open option makes the same call while
// it sets up a game. The plugin opens it on the next UI frame after you are in
// each game, when it is not open already. The key still closes it.
// The code bytes of every function are checked at load; on a mismatch the
// plugin stays inactive.

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

constexpr const char* PluginId      = "shown-on-start";
constexpr const char* PluginVersion = "0.0.2";

const D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = PluginId,
	.name        = "Shown on Start",
	.version     = PluginVersion,
	.author      = "wild-healer",
	.description = "Item labels and the automap start turned on.",
	.flags       = D2RL::PluginFlags::Shared,
};

struct Settings {
	std::atomic<bool> items { true };
	std::atomic<bool> automap { true };
} Config;

// ---- native layout (Steam 3.4.95041 / 3.3.94995) ----

constexpr std::uint64_t ItemsGetRva = 0x1FAE30; // bool (uint8 mode)
constexpr std::array<std::uint8_t, 16> ItemsGetBytes { 0x48, 0x83, 0xEC, 0x38, 0x0F, 0xB6, 0xD1, 0x48, 0x3B, 0x15, 0x2A, 0x25, 0x16, 0x02, 0x48, 0x89 };
constexpr std::uint64_t ItemsSetRva = 0x1FAE90; // void (uint8 mode, bool on)
constexpr std::array<std::uint8_t, 16> ItemsSetBytes { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x0F, 0xB6, 0xDA, 0x0F, 0xB6, 0xD1, 0x48, 0x3B, 0x15, 0xC5 };
constexpr std::uint64_t ItemsApplyRva = 0xCE450; // void (): applies the state (labels HUD, shown flag)
constexpr std::array<std::uint8_t, 16> ItemsApplyBytes { 0x48, 0x83, 0xEC, 0x28, 0xE8, 0x27, 0xC9, 0x12, 0x00, 0x84, 0xC0, 0x74, 0x54, 0xE8, 0xAE, 0x99 };
constexpr std::uint8_t ModeShowItems = 0;
constexpr std::uint64_t OpenPanelRva = 0xCD7C0; // bool (int panel, int)
constexpr std::array<std::uint8_t, 16> OpenPanelBytes { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xAC };
constexpr std::uint64_t OpenPanelFlagsReadRva = 0xCD7FB; // movzx r15d, byte [r12 + rbx + 0x2A2ADA0] (r12 = image base)
constexpr std::array<std::uint8_t, 9> OpenPanelFlagsReadBytes { 0x45, 0x0F, 0xB6, 0xBC, 0x1C, 0xA0, 0xAD, 0xA2, 0x02 };
constexpr std::uint64_t PanelOpenFlagsRva = 0x2A2ADA0;
constexpr int           PanelAutomap      = 10;

using GetFn   = bool (*)(std::uint8_t mode) noexcept;
using SetFn   = void (*)(std::uint8_t mode, bool on) noexcept;
using VoidFn  = void (*)() noexcept;
using OpenFn  = bool (*)(int panel, int mode) noexcept;

std::uintptr_t Base {};
GetFn          ItemsGet {};
SetFn          ItemsSet {};
VoidFn         ItemsApply {};
OpenFn         OpenPanel {};

const D2RL::PluginContext* Context {};
const D2RL::ThreadService* Threads {};
std::atomic<bool>          ItemsDone { false }; // once per launch
std::atomic<bool>          Queued { false };

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

template <std::size_t N>
bool CodeMatches(std::uint64_t rva, const std::array<std::uint8_t, N>& bytes) noexcept {
	std::array<std::uint8_t, N> code {};
	return Read(Base + rva, code.data(), code.size()) && code == bytes;
}

bool LocateItems() noexcept {
	if (!CodeMatches(ItemsGetRva, ItemsGetBytes) || !CodeMatches(ItemsSetRva, ItemsSetBytes) || !CodeMatches(ItemsApplyRva, ItemsApplyBytes)) return false;
	ItemsGet   = reinterpret_cast<GetFn>(Base + ItemsGetRva);
	ItemsSet   = reinterpret_cast<SetFn>(Base + ItemsSetRva);
	ItemsApply = reinterpret_cast<VoidFn>(Base + ItemsApplyRva);
	return true;
}

// ---- automap ----

bool LocateAutomap() noexcept {
	if (!CodeMatches(OpenPanelRva, OpenPanelBytes) || !CodeMatches(OpenPanelFlagsReadRva, OpenPanelFlagsReadBytes)) return false;
	OpenPanel = reinterpret_cast<OpenFn>(Base + OpenPanelRva);
	return true;
}

bool AutomapOpen() noexcept {
	return *reinterpret_cast<const std::uint8_t volatile*>(Base + PanelOpenFlagsRva + PanelAutomap) != 0;
}

// ---- UI thread, in a game ----

void __cdecl TurnOn(const D2RL::PluginContext*, void*) noexcept {
	Queued.store(false);
	if (OpenPanel && Config.automap.load()) {
		if (AutomapOpen()) {
			Log(false, "automap was already open");
		} else {
			const bool opened = OpenPanel(PanelAutomap, 0) && AutomapOpen();
			Log(!opened, "automap %s", opened ? "opened" : "could not be opened");
		}
	}
	if (ItemsGet && !ItemsDone.exchange(true) && Config.items.load()) {
		if (ItemsGet(ModeShowItems)) {
			Log(false, "item labels were already shown");
		} else {
			ItemsSet(ModeShowItems, true);
			ItemsApply();
			Log(false, "item labels turned on");
		}
	}
}

void __cdecl OnGameplayEvent(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent* event, void*) noexcept {
	if (!event || event->kind != D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady || !Threads) return;
	// Run on the next UI frame, after the game has finished setting up its HUD.
	if (!Queued.exchange(true) && Threads->runOnUiThread(Context, TurnOn, nullptr) != D2RL::Threads::Result::Success) Queued.store(false);
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
	const auto flag = [&](std::atomic<bool>& target) {
		if (value != "true" && value != "false") return error = "true or false", false;
		target = value == "true";
		return true;
	};
	if (key == "items") return flag(Config.items);
	if (key == "automap") return flag(Config.automap);
	return error = "unknown setting", false;
}

std::string ValueOf(const std::string& key) {
	const auto flag = [](bool on) { return std::string(on ? "true" : "false"); };
	if (key == "items") return flag(Config.items);
	if (key == "automap") return flag(Config.automap);
	return {};
}

std::string Render() {
	return R"(# Shown on Start
#
# Turns on, so you don't have to press their keys after launching D2R:
# - items: item labels on the ground (the Show Items key), in your first game
#   after launch. They stay as you leave them after that. Needs the game's
#   Item Name Display option on Toggle.
# - automap: the automap (the AutoMap key, Tab), opened in every game.
# Their keys still turn them off. Every setting can also be changed in game
# with I Hate TOMLs.

[shown-on-start]
items = )" + ValueOf("items") + R"(
automap = )" + ValueOf("automap") + "\n";
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

constexpr const char* Schema = R"toml(title = "Shown on Start"
[[setting]]
key = "items"
type = "bool"
label = "Item labels shown on start"
note = "Turned on in your first game after launch. Needs Item Name Display on Toggle."
[[setting]]
key = "automap"
type = "bool"
label = "Automap open on start"
note = "Opened in every game, as if you pressed the AutoMap key."
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

template <typename Service>
const Service* Query(const char* name) {
	const Service* service = nullptr;
	if (Context->QueryService(&service) != D2RL::ServiceQueryResult::Success) {
		Log(true, "%s service unavailable", name);
		return nullptr;
	}
	return service;
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
	if (!Base) return true;
	if (!LocateAutomap()) Log(true, "automap: inactive, this game build isn't supported (Steam 3.4.95041 / 3.3.94995 are)");
	if (!LocateItems()) Log(true, "item labels: inactive, this game build isn't supported (Steam 3.4.95041 / 3.3.94995 are)");
	if (!ItemsGet && !OpenPanel) return true;
	Threads = Query<D2RL::ThreadService>("thread");
	if (Threads && !D2RL::HasThreadServiceField(Threads, D2RL::ThreadServiceRequiredSize)) Threads = nullptr;
	const auto* lifecycle = Query<D2RL::LifecycleService>("lifecycle");
	if (!Threads || !lifecycle || !D2RL::HasLifecycleServiceField(lifecycle, D2RL::LifecycleServiceRequiredSize)) {
		Log(true, "Shown on Start %s: inactive, missing loader services", PluginVersion);
		return true;
	}
	const D2RL::Lifecycle::GameplayEventListener listener {
		.structSize = D2RL::Lifecycle::GameplayEventListenerSize,
		.kind       = D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady,
		.callback   = OnGameplayEvent,
	};
	D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
	if (lifecycle->registerGameplayEventListener(context, &listener, &handle) != D2RL::Lifecycle::Result::Success) {
		Log(true, "Shown on Start %s: inactive, could not listen to game joins", PluginVersion);
		return true;
	}
	Log(false, "Shown on Start %s: active (item labels %s, automap %s)", PluginVersion, !ItemsGet ? "inactive" : Config.items ? "on at start" : "left alone",
	    !OpenPanel ? "inactive" : Config.automap ? "opened in every game" : "left alone");
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	Menu.Stop();
}
