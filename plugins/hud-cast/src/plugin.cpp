// HUD Cast: skills cast (and clicks walk) while the cursor is over the bottom
// bar's art, as anywhere else in the game.
//
// Why the bar stops them (Steam 3.3.94995 / 93787 RVAs):
// 1. A click goes to the UI first (0x840120 for a right click, 0x83FCC0 for a
//    left one). The HUD panel (HUDPanelHD, vtable D2R+0x1FDF990) holds click
//    catchers (ClickCatcherWidget, vtable D2R+0x1D80390): their message handler
//    0x8E1920 marks a mouse message handled (0x843FC0) when the catcher is the
//    hovered widget (0x876180, call at 0x8E1B34), and a handled click never
//    reaches the world handlers (0x83B00 right, 0x839E0 left).
// 2. Each frame the game asks 0xF2670 `int (int* x, int* y)` whether the cursor
//    is over the UI: it fills the cursor position and returns 1 when any widget
//    is hovered (UI manager 0x846020, +0x178) or the cursor is in the bottom 49
//    pixels. The unit under the cursor (0xF1250) and held clicks (0xF1DB2) stop
//    then, so holding a skill stops as soon as the cursor reaches the bar.
// The plugin points the catcher's hover call at a stub that answers "not
// hovered" for catchers inside the HUD panel, and hooks 0xF2670 so a cursor over
// such a catcher counts as over the world (the game's own tail then runs:
// 0x846210, 0x8F270). Buttons, belt, orbs and skill buttons are their own
// widgets: hovering them still stops the click, as before.
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
#include <vector>

#include "i_hate_tomls_client.h"

namespace {

constexpr const char* PluginId      = "hud-cast";
constexpr const char* PluginVersion = "0.0.3";

const D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = PluginId,
	.name        = "HUD Cast",
	.version     = PluginVersion,
	.author      = "wild-healer",
	.description = "Skills cast while the cursor is over the bottom bar.",
	.flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

struct Settings {
	std::atomic<bool> enabled { true };
	std::atomic<bool> logParts { true };
} Config;

// ---- native layout (Steam 3.3.93787 / 94995) ----

constexpr std::uint64_t OverUiRva = 0xF2670; // int (int* x, int* y): 1 = cursor over the UI
constexpr std::array<std::uint8_t, 16> OverUiBytes { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xDA, 0x48, 0x8B, 0xF9 };
// In the click catcher's handler: mov rcx, rsi; call Hovered; test al, al; je +5; call SetHandled
constexpr std::uint64_t CatcherHoverRva = 0x8E1B31;
constexpr std::array<std::uint8_t, 17> CatcherHoverBytes { 0x48, 0x8B, 0xCE, 0xE8, 0x47, 0x46, 0xF9, 0xFF, 0x84, 0xC0, 0x74, 0x05, 0xE8, 0x7E, 0x24, 0xF6, 0xFF };
constexpr std::uint64_t CatcherHoverCallRva = 0x8E1B34;
constexpr std::array<std::uint8_t, 5> CatcherHoverCallBytes { 0xE8, 0x47, 0x46, 0xF9, 0xFF };
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
bool           CatcherPatched {};

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

std::string NameOf(const void* widget) {
	const char* name = nullptr;
	if (!Read(reinterpret_cast<std::uintptr_t>(widget) + WidgetName, &name, sizeof(name)) || !name) return "?";
	char text[64] {};
	for (std::size_t i = 0; i + 1 < sizeof(text); ++i) {
		if (!Read(reinterpret_cast<std::uintptr_t>(name) + i, &text[i], 1)) break;
		if (!text[i]) break;
	}
	return text[0] ? text : "?";
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

// Logs each HUD part the cursor stopped on once (name and vtable), to find
// which parts the plugin lets through and which it leaves alone.
SRWLOCK                     SeenLock = SRWLOCK_INIT;
std::vector<std::uintptr_t> Seen;

void LogPart(const void* widget, bool through) noexcept {
	if (!Config.logParts.load(std::memory_order_relaxed) || !widget) return;
	AcquireSRWLockExclusive(&SeenLock);
	bool fresh = false;
	try {
		const auto key = reinterpret_cast<std::uintptr_t>(widget);
		fresh = std::find(Seen.begin(), Seen.end(), key) == Seen.end() && Seen.size() < 256;
		if (fresh) Seen.push_back(key);
	} catch (...) {
	}
	ReleaseSRWLockExclusive(&SeenLock);
	if (!fresh) return;
	try {
		std::string path = NameOf(widget);
		const void* at   = ParentOf(widget);
		for (int depth = 0; at && depth < 6; ++depth, at = ParentOf(at)) path = NameOf(at) + "/" + path;
		const std::uintptr_t vtable = VtableOf(widget);
		Log(false, "cursor over %s (vtable D2R+0x%llX): %s", path.c_str(), static_cast<unsigned long long>(vtable ? vtable - Base : 0),
		    through ? "casts go through" : "left alone (part of the UI)");
	} catch (...) {
	}
}

// ---- game threads ----

// Called by the stub instead of Hovered from the click catcher's message handler.
bool CatcherHovered(void* widget) noexcept {
	const bool hovered = Hovered(widget);
	if (!hovered || !Config.enabled.load(std::memory_order_relaxed) || !IsHudBackground(widget)) return hovered;
	return false;
}

void WrapHandlerList() noexcept;
int HookOverUi(int* x, int* y) noexcept {
	if (Config.logParts.load(std::memory_order_relaxed)) WrapHandlerList();
	const int over = OverUi(x, y);
	if (!over || !x || !y || !Config.enabled.load(std::memory_order_relaxed)) return over;
	if (Controller() || !MouseAllowed()) return over;
	void* const hovered = HoveredWidget();
	if (!hovered || !InHud(hovered)) return over;
	const bool through = IsHudBackground(hovered);
	LogPart(hovered, through);
	if (!through) return over;
	if (UiReady()) CursorWorld(x, y);
	return 0;
}

// ---- diagnostics (0.0.2): who stops a click over the bar ----

// SetHandled 0x843FC0: void (), sets UI manager (global D2R+0x3440170) +0xB8 = 1.
constexpr std::uint64_t SetHandledRva = 0x843FC0;
constexpr std::array<std::uint8_t, 20> SetHandledBytes { 0x48, 0x8B, 0x05, 0xA9, 0xC1, 0xBF, 0x02, 0x48, 0x85, 0xC0, 0x74, 0x07, 0xC6, 0x80, 0xB8, 0x00, 0x00, 0x00, 0x01, 0xC3 };
constexpr std::uint64_t UiManagerGlobalRva = 0x3440170;
// UI mouse-down handlers: bool (MouseMessage*), true = the UI took the click.
constexpr std::uint64_t UiLeftDownRva  = 0x83FCC0;
constexpr std::uint64_t UiRightDownRva = 0x840120;
constexpr std::array<std::uint8_t, 14> UiDownBytes { 0x48, 0x89, 0x74, 0x24, 0x20, 0x55, 0x48, 0x8D, 0xAC, 0x24, 0xE0, 0xFD, 0xFF, 0xFF };
using UiDownFn = bool (*)(void* message) noexcept;
UiDownFn UiLeftDown {}, UiRightDown {};
thread_local std::uintptr_t HandledBy {};
std::atomic<int> ClickLogs {};

void SetHandledHook() noexcept {
	HandledBy = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
	std::uint8_t* manager = *reinterpret_cast<std::uint8_t**>(Base + UiManagerGlobalRva);
	if (manager) manager[0xB8] = 1;
}

bool LogClick(const char* side, UiDownFn original, void* message) noexcept {
	HandledBy = 0;
	const bool taken = original(message);
	if (!Config.logParts.load(std::memory_order_relaxed) || ClickLogs.load() >= 40) return taken;
	void* const hovered = HoveredWidget();
	++ClickLogs;
	try {
		const std::string name = hovered ? NameOf(hovered) : std::string("nothing");
		Log(false, "%s click over %s: %s%s", side, name.c_str(), taken ? "taken by the UI" : "passed to the game",
		    taken && HandledBy ? (" (set at D2R+0x" + [] (std::uintptr_t a) { char t[32]; std::snprintf(t, sizeof(t), "%llX", static_cast<unsigned long long>(a)); return std::string(t); }(HandledBy - Base) + ")").c_str() : "");
	} catch (...) {
	}
	return taken;
}
bool HookUiLeftDown(void* message) noexcept { return LogClick("left", UiLeftDown, message); }
bool HookUiRightDown(void* message) noexcept { return LogClick("right", UiRightDown, message); }

// Mouse message handlers (runtime list in .data: {u32 message, u32 flags, fn}, game
// handlers after the UI ones). Down messages 0x10005 (left) and 0x10009 (right) are
// wrapped to log what each handler answers; the first returning true stops the rest.
constexpr std::uint64_t HandlerListRva = 0x229BD00;
constexpr std::size_t   HandlerListSize = 0x400;
constexpr int           MaxWrapped = 12;
using HandlerFn = bool (*)(void* message) noexcept;
struct Wrapped { std::uint32_t message; std::uint64_t rva; HandlerFn original; std::atomic<int> logs; };
Wrapped Wraps[MaxWrapped] {};
int WrapCount {};
std::atomic<bool> ListWrapped {};

bool CallWrapped(int i, void* message) noexcept {
	Wrapped& w = Wraps[i];
	const bool taken = w.original(message);
	if (Config.logParts.load(std::memory_order_relaxed) && w.logs.fetch_add(1) < 12) {
		void* const hovered = HoveredWidget();
		try {
			const std::string name = hovered ? NameOf(hovered) : std::string("nothing");
			const char* where = w.rva < 0x3000000 ? "D2R" : "outside D2R";
			Log(false, "message 0x%X handler %d (%s+0x%llX): %s, hovered %s", w.message, i, where, static_cast<unsigned long long>(w.rva), taken ? "TAKEN" : "passed", name.c_str());
		} catch (...) {
		}
	}
	return taken;
}
template <int I> bool WrapFn(void* message) noexcept { return CallWrapped(I, message); }
constexpr HandlerFn WrapFns[MaxWrapped] { &WrapFn<0>, &WrapFn<1>, &WrapFn<2>, &WrapFn<3>, &WrapFn<4>, &WrapFn<5>, &WrapFn<6>, &WrapFn<7>, &WrapFn<8>, &WrapFn<9>, &WrapFn<10>, &WrapFn<11> };

void WrapHandlerList() noexcept {
	if (ListWrapped.exchange(true)) return;
	std::uint8_t* const list = reinterpret_cast<std::uint8_t*>(Base + HandlerListRva);
	std::array<std::uint8_t, HandlerListSize> copy {};
	if (!Read(Base + HandlerListRva, copy.data(), copy.size())) { Log(true, "diag: handler list unreadable"); return; }
	for (std::size_t at = 0; at + 16 <= copy.size() && WrapCount < MaxWrapped; at += 16) {
		std::uint32_t message = 0;
		std::uint64_t fn = 0;
		std::memcpy(&message, copy.data() + at, 4);
		std::memcpy(&fn, copy.data() + at + 8, 8);
		if ((message != 0x10005 && message != 0x10009) || fn < Base) continue;
		Wrapped& w = Wraps[WrapCount];
		w.message  = message;
		w.rva      = fn - Base;
		w.original = reinterpret_cast<HandlerFn>(fn);
		DWORD old = 0;
		if (!VirtualProtect(list + at + 8, 8, PAGE_READWRITE, &old)) continue;
		*reinterpret_cast<HandlerFn volatile*>(list + at + 8) = WrapFns[WrapCount];
		DWORD ignored = 0;
		VirtualProtect(list + at + 8, 8, old, &ignored);
		Log(false, "diag: wrapped handler %d for message 0x%X at list +0x%llX: D2R+0x%llX", WrapCount, message, static_cast<unsigned long long>(at), static_cast<unsigned long long>(w.rva));
		++WrapCount;
	}
	if (!WrapCount) Log(true, "diag: no down handlers found in the list");
}

void InstallDiagnostics() noexcept {
	if (CodeMatches(SetHandledRva, SetHandledBytes)) {
		std::array<std::uint8_t, 12> jump { 0x48, 0xB8 };
		const auto target = reinterpret_cast<std::uint64_t>(&SetHandledHook);
		std::memcpy(jump.data() + 2, &target, sizeof(target));
		jump[10] = 0xFF, jump[11] = 0xE0;
		if (!Context->PatchBytes(SetHandledRva, SetHandledBytes.data(), 12, jump.data(), 12)) Log(true, "diag: could not patch SetHandled");
	}
	if (!Context->InstallInlineHook(UiLeftDownRva, UiDownBytes.data(), 14, &HookUiLeftDown, &UiLeftDown) || !UiLeftDown) Log(true, "diag: no left click hook");
	if (!Context->InstallInlineHook(UiRightDownRva, UiDownBytes.data(), 14, &HookUiRightDown, &UiRightDown) || !UiRightDown) Log(true, "diag: no right click hook");
}

// The rel32 call has to land in the game image: the stub goes in 16 bytes of int3 padding.
bool RedirectCatcherHover() noexcept {
	if (!CodeMatches(CatcherHoverRva, CatcherHoverBytes)) return false;
	std::array<std::uint8_t, 12> stub { 0x48, 0xB8 }; // mov rax, imm64; jmp rax (rcx = the widget)
	const auto target = reinterpret_cast<std::uint64_t>(&CatcherHovered);
	std::memcpy(stub.data() + 2, &target, sizeof(target));
	stub[10] = 0xFF, stub[11] = 0xE0;
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
			if (!Context->PatchBytes(cave + 2, padding.data(), static_cast<std::uint32_t>(stub.size()), stub.data(), static_cast<std::uint32_t>(stub.size()))) continue; // taken
			return Context->PatchCallRel32(CatcherHoverCallRva, CatcherHoverCallBytes.data(), 5, cave + 2);
		}
	}
	return false;
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
	if (key == "log_parts") return flag(Config.logParts);
	return error = "unknown setting", false;
}

std::string ValueOf(const std::string& key) {
	const auto flag = [](bool on) { return std::string(on ? "true" : "false"); };
	if (key == "enabled") return flag(Config.enabled);
	if (key == "log_parts") return flag(Config.logParts);
	return {};
}

std::string Render() {
	return R"(# HUD Cast
#
# The bottom bar (the HUD with the orbs, belt and skill buttons) normally stops
# your clicks and casts when the cursor is over it, even over its empty art.
# With this plugin, clicking or holding a skill over the bar's art casts it, or
# walks, at the spot under the cursor, as anywhere else in the game. The bar's
# buttons, belt, orbs and skill buttons still work as usual.
# Every setting can also be changed in game with I Hate TOMLs, and applies at once.

[hud-cast]
enabled = )" + ValueOf("enabled") + R"(

# Write what the cursor is over each time the bar would have stopped a click
# (d2rloader/logs/hud-cast.log), once per part of the bar.
log_parts = )" + ValueOf("log_parts") + "\n";
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

constexpr const char* Schema = R"toml(title = "HUD Cast"
[[setting]]
key = "enabled"
type = "bool"
label = "Cast over the bottom bar"
note = "Clicks on the bar's art cast or walk; its buttons still work."
[[setting]]
key = "log_parts"
type = "bool"
label = "Log bar parts"
note = "In d2rloader/logs/hud-cast.log, once per part."
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
		Log(true, "HUD Cast %s: inactive, this game build isn't supported (Steam 3.3.93787 / 3.3.94995 are)", PluginVersion);
		return true;
	}
	CatcherPatched = RedirectCatcherHover();
	InstallDiagnostics();
	if (!CatcherPatched) Log(true, "could not patch D2R+0x%llX: clicks on the bar stay stopped", static_cast<unsigned long long>(CatcherHoverCallRva));
	if (!context->InstallInlineHook(OverUiRva, OverUiBytes.data(), static_cast<std::uint32_t>(OverUiBytes.size()), &HookOverUi, &OverUi) || !OverUi) {
		Log(true, "could not hook D2R+0x%llX: held casts still stop over the bar", static_cast<unsigned long long>(OverUiRva));
		OverUi = nullptr;
	}
	Log(false, "HUD Cast %s: %s, skills cast over the bottom bar's art", PluginVersion, Config.enabled ? "active" : "loaded but turned off");
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	Menu.Stop();
}
