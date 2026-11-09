// Quickcast over HUD: quick cast works while the cursor is over the bottom bar's art, as
// anywhere else in the game.
//
// Why the bar stops it (Steam 3.3.94995 / 93787 RVAs):
// A key goes to the UI first. With keyboard quick cast on, the click catchers of
// the HUD panel (HUDPanelHD, vtable D2R+0x1FDF990; ClickCatcherWidget, vtable
// D2R+0x1D80390, message handler 0x8E1920) mark a skill key handled (0x843FC0)
// when the cursor is inside them: the handler asks the catcher's "point inside"
// (vtable slot 8, calls returning to 0x8E1ADB / 0x8E1B02). The key binding
// (0x1203A0) then sees the key handled (0x8460D0) and never starts the cast.
// The plugin swaps that vtable slot and answers "outside" to those two calls for
// catchers inside the HUD panel. Buttons, belt, orbs and skill buttons are their
// own widgets and keep the key, as before. Clicks on the bar are left alone.
// It also hooks 0xF2670 `int (int* x, int* y)` (1 = cursor over the UI) for the
// hover search only (0xF1250, return 0xF1285): over the bar's art the monster
// under the cursor is found, so the quick cast aims at it.
// Widget: +0x08 name (char*), +0x30 parent. The code bytes are checked at load;
// on a mismatch the plugin stays inactive.

#include <D2RLPlugin/api.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "i_hate_tomls_client.h"

namespace {

constexpr const char* PluginId      = "quickcast-over-hud";
constexpr const char* PluginVersion = "1.0.0";

const D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = PluginId,
	.name        = "Quickcast over HUD",
	.version     = PluginVersion,
	.author      = "wild-healer",
	.description = "Quick cast works while the cursor is over the bottom bar.",
	.flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

struct Settings {
	std::atomic<bool> enabled { true };
} Config;

// ---- native layout (Steam 3.3.93787 / 94995) ----

constexpr std::uint64_t OverUiRva = 0xF2670; // int (int* x, int* y): 1 = cursor over the UI
constexpr std::array<std::uint8_t, 16> OverUiBytes { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xDA, 0x48, 0x8B, 0xF9 };
constexpr std::uint64_t HoveredRva = 0x876180; // bool (Widget*): the widget is the hovered one
constexpr std::array<std::uint8_t, 16> HoveredBytes { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0xE8, 0x92, 0xFE, 0xFC, 0xFF, 0x48, 0x39 };
constexpr std::uint64_t UiManagerRva = 0x846020; // UiManager* (), +0x178 hovered widget
constexpr std::array<std::uint8_t, 16> UiManagerBytes { 0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x05, 0x45, 0xA1, 0xBF, 0x02, 0x48, 0x85, 0xC0, 0x75, 0x1A };
constexpr std::uint64_t UiReadyRva = 0x846210; // bool ()
constexpr std::array<std::uint8_t, 12> UiReadyBytes { 0x48, 0x8B, 0x05, 0x59, 0x9F, 0xBF, 0x02, 0x48, 0x85, 0xC0, 0x74, 0x08 };
constexpr std::uint64_t CursorWorldRva = 0x8F270; // void (int* x, int* y), the tail of 0xF2670 over the world
constexpr std::array<std::uint8_t, 12> CursorWorldBytes { 0x40, 0x53, 0x57, 0x48, 0x83, 0xEC, 0x48, 0x0F, 0x29, 0x74, 0x24, 0x30 };
constexpr std::uint64_t MouseAllowedRva = 0x14E5C0; // bool (): the game takes mouse actions now (no menu, not loading...)
constexpr std::array<std::uint8_t, 12> MouseAllowedBytes { 0x48, 0x83, 0xEC, 0x28, 0xE8, 0x47, 0x98, 0xF2, 0xFF, 0x84, 0xC0, 0x74 };
constexpr std::uint64_t ControllerRva = 0x77E10; // bool (): controller mode
constexpr std::array<std::uint8_t, 12> ControllerBytes { 0x48, 0x83, 0xEC, 0x28, 0xE8, 0xF7, 0xE1, 0xFF, 0xFF, 0x80, 0xB8, 0xC8 };
constexpr std::uint64_t HudVtableRva     = 0x1FDF990;
constexpr std::uint64_t CatcherVtableRva = 0x1D80390;
constexpr std::size_t   UiHovered        = 0x178;
constexpr std::size_t   WidgetName       = 0x08;
constexpr std::size_t   WidgetParent     = 0x30;

using OverUiFn  = int (*)(int* x, int* y) noexcept;
using WidgetFn  = bool (*)(void* widget) noexcept;
using PtrFn     = std::uint8_t* (*)() noexcept;
using BoolFn    = bool (*)() noexcept;
using CursorFn  = void (*)(int* x, int* y) noexcept;

std::uintptr_t Base {};
OverUiFn       OverUi {}; // original, after the hook
WidgetFn       Hovered {};
PtrFn          UiManager {};
BoolFn         UiReady {};
CursorFn       CursorWorld {};
BoolFn         MouseAllowed {};
BoolFn         Controller {};

const D2RL::PluginContext* Context {};

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

std::uintptr_t VtableOf(const void* widget) noexcept {
	std::uintptr_t vtable = 0;
	return Read(reinterpret_cast<std::uintptr_t>(widget), &vtable, sizeof(vtable)) ? vtable : 0;
}

void* ParentOf(const void* widget) noexcept {
	void* parent = nullptr;
	return Read(reinterpret_cast<std::uintptr_t>(widget) + WidgetParent, &parent, sizeof(parent)) ? parent : nullptr;
}

bool InHud(const void* widget) noexcept {
	const void* at = widget;
	for (int depth = 0; at && depth < 24; ++depth, at = ParentOf(at))
		if (VtableOf(at) == Base + HudVtableRva) return true;
	return false;
}

// A click catcher of the HUD panel: the bar's empty art, nothing to click on.
bool IsHudBackground(const void* widget) noexcept {
	return widget && VtableOf(widget) == Base + CatcherVtableRva && InHud(widget);
}

void* HoveredWidget() noexcept {
	std::uint8_t* const manager = UiManager();
	void*               hovered = nullptr;
	return manager && Read(reinterpret_cast<std::uintptr_t>(manager) + UiHovered, &hovered, sizeof(hovered)) ? hovered : nullptr;
}

// ---- game threads ----

// A click catcher's own "is this point inside me" (vtable slot 8, +0x40), swapped in
// its vtable. Only the answers to its key branch are changed: there, with keyboard
// quick cast on, a skill key pressed while the cursor is inside a catcher is marked
// handled, and the key binding (0x1203A0) then never runs.
using InsideFn = bool (*)(void* widget, std::uint64_t point) noexcept;
constexpr std::size_t   CatcherInsideSlot = 8;
constexpr std::uint64_t CatcherKeyReturn1 = 0x8E1ADB; // after call [rax+0x40] for the key 0x3B
constexpr std::uint64_t CatcherKeyReturn2 = 0x8E1B02; // after call [rax+0x40] for skill keys
constexpr std::array<std::uint8_t, 0x32> CatcherKeyBytes { 0x48, 0x8B, 0x06, 0x48, 0x8B, 0x54, 0x24, 0x20, 0xFF, 0x50, 0x40, 0xEB, 0x5C, 0xE8, 0x0E, 0xD7, 0x86, 0xFF,
	0x89, 0x44, 0x24, 0x20, 0xE8, 0x15, 0xD7, 0x86, 0xFF, 0x89, 0x44, 0x24, 0x24, 0x40, 0x84, 0xED, 0x74, 0x4E, 0x48, 0x8B, 0x06, 0x48, 0x8B, 0xCE, 0x48, 0x8B, 0x54,
	0x24, 0x20, 0xFF, 0x50, 0x40 }; // at 0x8E1AD0
constexpr std::uint64_t CatcherKeyRva = 0x8E1AD0;
InsideFn CatcherInside {}; // original

bool HookCatcherInside(void* widget, std::uint64_t point) noexcept {
	const bool inside = CatcherInside(widget, point);
	if (!inside || !Config.enabled.load(std::memory_order_relaxed)) return inside;
	const auto caller = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
	if (caller != Base + CatcherKeyReturn1 && caller != Base + CatcherKeyReturn2) return inside;
	if (!InHud(widget)) return inside;
	return false;
}

bool SwapCatcherInside() noexcept {
	if (!CodeMatches(CatcherKeyRva, CatcherKeyBytes)) return false;
	auto* const slot = reinterpret_cast<std::uintptr_t*>(Base + CatcherVtableRva + CatcherInsideSlot * 8);
	std::uintptr_t original = 0;
	if (!Read(reinterpret_cast<std::uintptr_t>(slot), &original, sizeof(original)) || original < Base || original >= Base + 0x2000000) return false;
	CatcherInside = reinterpret_cast<InsideFn>(original);
	DWORD old = 0;
	if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old)) return false;
	*reinterpret_cast<std::uintptr_t volatile*>(slot) = reinterpret_cast<std::uintptr_t>(&HookCatcherInside);
	DWORD ignored = 0;
	VirtualProtect(slot, sizeof(*slot), old, &ignored);
	return true;
}

// The unit under the cursor (hover search 0xF1250, call returning to 0xF1285) is
// found over the bar's art too, so a quick cast there aims at the monster under it.
// Held clicks (the other caller) stay stopped by the bar, as in the game.
constexpr std::uint64_t HoverSearchReturn = 0xF1285;

int HookOverUi(int* x, int* y) noexcept {
	const int over = OverUi(x, y);
	if (!over || !x || !y || !Config.enabled.load(std::memory_order_relaxed)) return over;
	if (reinterpret_cast<std::uintptr_t>(__builtin_return_address(0)) != Base + HoverSearchReturn) return over;
	if (Controller() || !MouseAllowed()) return over;
	void* const hovered = HoveredWidget();
	if (!hovered || !IsHudBackground(hovered)) return over;
	if (UiReady()) CursorWorld(x, y);
	return 0;
}

bool Locate() noexcept {
	if (!CodeMatches(OverUiRva, OverUiBytes) || !CodeMatches(HoveredRva, HoveredBytes) || !CodeMatches(UiManagerRva, UiManagerBytes) ||
	    !CodeMatches(UiReadyRva, UiReadyBytes) || !CodeMatches(CursorWorldRva, CursorWorldBytes) || !CodeMatches(MouseAllowedRva, MouseAllowedBytes) ||
	    !CodeMatches(ControllerRva, ControllerBytes))
		return false;
	Hovered      = reinterpret_cast<WidgetFn>(Base + HoveredRva);
	UiManager    = reinterpret_cast<PtrFn>(Base + UiManagerRva);
	UiReady      = reinterpret_cast<BoolFn>(Base + UiReadyRva);
	CursorWorld  = reinterpret_cast<CursorFn>(Base + CursorWorldRva);
	MouseAllowed = reinterpret_cast<BoolFn>(Base + MouseAllowedRva);
	Controller   = reinterpret_cast<BoolFn>(Base + ControllerRva);
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
	const auto flag = [&](std::atomic<bool>& target) {
		if (value != "true" && value != "false") return error = "true or false", false;
		target = value == "true";
		return true;
	};
	if (key == "enabled") return flag(Config.enabled);
	return error = "unknown setting", false;
}

std::string ValueOf(const std::string& key) {
	const auto flag = [](bool on) { return std::string(on ? "true" : "false"); };
	if (key == "enabled") return flag(Config.enabled);
	return {};
}

std::string Render() {
	return R"(# Quickcast over HUD
#
# With keyboard quick cast on, a skill key does nothing while the cursor is over
# the bottom bar (the HUD with the orbs, belt and skill buttons), even over its
# empty art. With this plugin, a quick cast over the bar's art casts at the spot
# (or the monster) under the cursor, as anywhere else in the game. The bar's
# buttons, belt, orbs and skill buttons, and mouse clicks, work as usual.
# It can also be turned on or off in game with I Hate TOMLs, and applies at once.

[quickcast-over-hud]
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

constexpr const char* Schema = R"toml(title = "Quickcast over HUD"
[[setting]]
key = "enabled"
type = "bool"
label = "Quick cast over the bottom bar"
note = "Over the bar's art; its buttons keep their keys."
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
		Log(true, "Quickcast over HUD %s: inactive, this game build isn't supported (Steam 3.3.93787 / 3.3.94995 are)", PluginVersion);
		return true;
	}
	if (!SwapCatcherInside()) {
		Log(true, "Quickcast over HUD %s: inactive, the bar's key check differs on this build", PluginVersion);
		return true;
	}
	if (!context->InstallInlineHook(OverUiRva, OverUiBytes.data(), static_cast<std::uint32_t>(OverUiBytes.size()), &HookOverUi, &OverUi) || !OverUi) {
		Log(true, "could not hook D2R+0x%llX: monsters under the bar aren't targeted", static_cast<unsigned long long>(OverUiRva));
		OverUi = nullptr;
	}
	Log(false, "Quickcast over HUD %s: %s, quick cast works over the bottom bar's art", PluginVersion, Config.enabled ? "active" : "loaded but turned off");
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	Menu.Stop();
}
