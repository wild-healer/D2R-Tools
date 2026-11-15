// Quick Play Difficulty: on the character select screen, a left click on Play
// starts the game at the highest difficulty the selected character has
// unlocked, without the difficulty selector. A right click on Play opens the
// selector as before.
//
// How the game does it (Steam 3.3.94995 / 3.4.95041 RVAs):
//   - the Play button (mainmenupanelhd.json, QuickPlayButton) sends
//     MainMenuHD:Play. Offline, its handler gets the selected character
//     (0x5FF3D0, no arguments, null when none) and reads its progression
//     (word +0x122 & 0x1F00, i.e. the title byte +0x123; +0x124 == 1 for a
//     classic character). Below Nightmare (classic 4, expansion 5) it starts
//     the game at Normal; otherwise it posts
//     PanelManager:OpenPanel:CharacterDifficultyModal (code at 0x60F8F5).
//   - the selector's buttons send CharacterSelect:LoadCharacter:0/1/2.
// The plugin lets Play run, and when the game asks for the selector after a
// left click it consumes that message and sends LoadCharacter with the highest
// unlocked difficulty (classic: Nightmare from 4, Hell from 8; expansion:
// Nightmare from 5, Hell from 10). A right click on the Play button sends
// MainMenuHD:Play itself and lets the selector open.

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
#include <unordered_set>
#include <vector>

#include "i_hate_tomls_client.h"

namespace {

namespace Events = D2RL::SharedEvents;
namespace W      = D2RL::Widgets;

constexpr const char* PluginId      = "quick-play-difficulty";
constexpr const char* PluginVersion = "0.0.1";

const D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = PluginId,
	.name        = "Quick Play Difficulty",
	.version     = PluginVersion,
	.author      = "wild-healer",
	.description = "Left click on Play starts at the character's highest difficulty; right click on Play opens the difficulty selector.",
	.flags       = D2RL::PluginFlags::Client,
};

struct Settings {
	std::atomic<bool> enabled { true };
	std::atomic<bool> rightClick { true };
	std::atomic<bool> logUi { false };
} Config;

const D2RL::PluginContext*      Context {};
const D2RL::WidgetService*      WidgetApi {};
const D2RL::ThreadService*      Threads {};
const D2RL::SharedEventService* EventApi {};
std::uintptr_t                  Base {};
std::string                     LogDirectory;

void Log(bool warn, const char* format, ...) noexcept {
	if (!Context) return;
	char    line[1024] {};
	va_list args;
	va_start(args, format);
	std::vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	warn ? Context->LogWarn(line) : Context->LogInfo(line);
}

std::wstring Wide(const std::string& text) {
	const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
	std::wstring out(size > 0 ? size - 1 : 0, L'\0');
	if (size > 1) MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), size);
	return out;
}

std::string Utf8(const wchar_t* text) {
	if (!text) return {};
	const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
	std::string out(size > 0 ? size - 1 : 0, '\0');
	if (size > 1) WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
	return out;
}

// ---- the selected character (game code) ----

// Reads the game's memory after checking it is committed and readable.
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

template <std::size_t Size>
bool CodeMatches(std::uint64_t rva, const std::array<std::uint8_t, Size>& bytes) noexcept {
	std::array<std::uint8_t, Size> code {};
	return Read(Base + rva, code.data(), code.size()) && std::memcmp(code.data(), bytes.data(), code.size()) == 0;
}

constexpr std::uint64_t SelectedCharacterRva = 0x5FF3D0;
constexpr std::array<std::uint8_t, 15> SelectedCharacterBytes { 0x4C, 0x8B, 0xDC, 0x53, 0x48, 0x83, 0xEC, 0x50, 0x48, 0x8B, 0x05, 0xE9, 0xBE, 0x3C, 0x02 };
// The Play handler: call SelectedCharacter; mov ecx, 0x1F00; cmp byte [rax+0x124], 1;
// movzx eax, word [rax+0x122]; jne; and ax, cx; mov ecx, 0x400; cmp ax, cx; jae (selector).
constexpr std::uint64_t PlayCheckRva = 0x60F8F5;
constexpr std::array<std::uint8_t, 47> PlayCheckBytes { 0xE8, 0xD6, 0xFA, 0xFE, 0xFF, 0xB9, 0x00, 0x1F, 0x00, 0x00, 0x80, 0xB8, 0x24, 0x01, 0x00, 0x00,
	                                                    0x01, 0x0F, 0xB7, 0x80, 0x22, 0x01, 0x00, 0x00, 0x0F, 0x85, 0x91, 0x00, 0x00, 0x00, 0x66, 0x23,
	                                                    0xC1, 0xB9, 0x00, 0x04, 0x00, 0x00, 0x66, 0x3B, 0xC1, 0x0F, 0x83, 0x91, 0x00, 0x00, 0x00 };
constexpr std::uintptr_t ProgressionOffset = 0x123; // title byte, low 5 bits
constexpr std::uintptr_t ClassicOffset     = 0x124; // 1 = classic character

using SelectedCharacterFn = void* (*)() noexcept;
SelectedCharacterFn SelectedCharacter {};

const char* const DifficultyNames[] = { "Normal", "Nightmare", "Hell" };

// The highest difficulty the selected character can play, -1 if unknown.
int HighestDifficulty() noexcept {
	if (!SelectedCharacter) return -1;
	auto* const   character   = static_cast<std::uint8_t*>(SelectedCharacter());
	std::uint8_t  progression = 0, classic = 0;
	if (!character || !Read(reinterpret_cast<std::uintptr_t>(character) + ProgressionOffset, &progression, 1)
	    || !Read(reinterpret_cast<std::uintptr_t>(character) + ClassicOffset, &classic, 1))
		return -1;
	progression &= 0x1F;
	const int perDifficulty = classic == 1 ? 4 : 5; // acts per difficulty
	const int highest       = progression / perDifficulty;
	return highest > 2 ? 2 : highest;
}

// ---- Play and the selector (UI thread) ----

std::atomic<ULONGLONG> PlayAt {};       // the last MainMenuHD:Play
std::atomic<ULONGLONG> RightClickAt {}; // our own Play from a right click
std::atomic<bool>      SelectorWanted {}; // the last Play came from a right click
std::atomic<int>       PendingLoad { -1 };
std::atomic<bool>      InGame {};

void SendUi(const char* target, const char* command, const char* text) {
	if (!WidgetApi) return;
	const W::UiAction action { .structSize = W::UiActionSize, .target = target, .command = command, .text = text };
	const auto        result = WidgetApi->dispatchUiAction(Context, &action);
	if (result != W::Result::Success) Log(true, "could not send %s:%s%s%s (result %u)", target, command, text ? ":" : "", text ? text : "", static_cast<unsigned>(result));
}

void __cdecl SendLoad(const D2RL::PluginContext*, void*) noexcept {
	const int difficulty = PendingLoad.exchange(-1);
	if (difficulty < 0 || difficulty > 2) return;
	const char text[2] = { static_cast<char>('0' + difficulty), 0 };
	SendUi("CharacterSelect", "LoadCharacter", text);
}

void __cdecl SendPlay(const D2RL::PluginContext*, void*) noexcept {
	RightClickAt = GetTickCount64();
	SendUi("MainMenuHD", "Play", nullptr);
}

void LogUiMessage(const Events::UiMessageEvent* event) {
	static std::unordered_set<std::string> seen;
	static FILE*                           out {};
	if (seen.size() >= 2000 || LogDirectory.empty()) return;
	const std::string line = std::string(event->target ? event->target : "") + " | " + (event->command ? event->command : "") + " | " + (event->text ? event->text : "");
	if (!seen.insert(line).second) return;
	if (!out) out = _wfopen(Wide(LogDirectory + "quick-play-difficulty-ui.log").c_str(), L"wb");
	if (!out) return;
	SYSTEMTIME now {};
	GetLocalTime(&now);
	std::fprintf(out, "%02u:%02u:%02u.%03u %s\r\n", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, line.substr(0, 400).c_str());
	std::fflush(out);
}

bool Is(const char* value, const char* expected) { return value && std::strcmp(value, expected) == 0; }

auto OnUiMessage(const D2RL::PluginContext*, const Events::UiMessageEvent* event, void*) noexcept -> Events::UiMessageAction {
	if (!event) return Events::UiMessageAction::Continue;
	if (Config.logUi) LogUiMessage(event);
	if (Is(event->target, "MainMenuHD") && Is(event->command, "Play")) {
		const ULONGLONG now = GetTickCount64();
		SelectorWanted      = now - RightClickAt.load() < 500;
		PlayAt              = now;
		return Events::UiMessageAction::Continue;
	}
	if (!Is(event->target, "PanelManager") || !Is(event->command, "OpenPanel") || !Is(event->text, "CharacterDifficultyModal")) return Events::UiMessageAction::Continue;
	// The game asks for the selector: Play was pressed for a character with
	// Nightmare or Hell open.
	const bool fromPlay = GetTickCount64() - PlayAt.load() < 2000;
	if (!Config.enabled || !fromPlay || SelectorWanted.load()) {
		Log(false, "difficulty selector opens (%s)", !Config.enabled ? "turned off" : !fromPlay ? "not from Play" : "right click");
		return Events::UiMessageAction::Continue;
	}
	PlayAt = 0;
	const int highest = HighestDifficulty();
	if (highest < 0) {
		Log(true, "difficulty selector opens: the selected character's difficulty could not be read");
		return Events::UiMessageAction::Continue;
	}
	Log(false, "Play: starting at %s, the highest difficulty unlocked", DifficultyNames[highest]);
	PendingLoad = highest;
	if (Threads->runOnUiThread(Context, SendLoad, nullptr) != D2RL::Threads::Result::Success) {
		PendingLoad = -1;
		Log(true, "difficulty selector opens: could not queue the game start");
		return Events::UiMessageAction::Continue;
	}
	return Events::UiMessageAction::Consume;
}

void __cdecl OnGameplayEvent(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent* event, void*) noexcept {
	if (!event) return;
	if (event->kind == D2RL::Lifecycle::GameplayEventKind::GameJoined) InGame = true;
	else if (event->kind == D2RL::Lifecycle::GameplayEventKind::GameLeft) InGame = false;
}

// ---- right click on Play (window thread + UI thread) ----

// The Play button in client pixels, updated from the UI thread.
std::atomic<int>  PlayLeft {}, PlayTop {}, PlayRight {}, PlayBottom {};
std::atomic<bool> PlayKnown {};
HWND              GameWindow {};
WNDPROC           OldProc {};
bool              SwallowRightUp {};

bool OverPlay(LPARAM lParam) {
	if (!PlayKnown.load() || InGame.load()) return false;
	const int x = static_cast<short>(LOWORD(lParam)), y = static_cast<short>(HIWORD(lParam));
	return x >= PlayLeft.load() && x < PlayRight.load() && y >= PlayTop.load() && y < PlayBottom.load();
}

LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
	if (message == WM_RBUTTONDOWN && Config.enabled && Config.rightClick && OverPlay(lParam)) {
		Log(false, "right click on Play: difficulty selector");
		(void)Threads->runOnUiThread(Context, SendPlay, nullptr);
		SwallowRightUp = true;
		return 0;
	}
	if (message == WM_RBUTTONUP && SwallowRightUp) {
		SwallowRightUp = false;
		return 0;
	}
	return CallWindowProcW(OldProc, window, message, wParam, lParam);
}

BOOL CALLBACK FindGameWindow(HWND window, LPARAM found) {
	DWORD process = 0;
	GetWindowThreadProcessId(window, &process);
	if (process != GetCurrentProcessId() || !IsWindowVisible(window) || GetWindow(window, GW_OWNER)) return TRUE;
	RECT  rect {}, best {};
	auto* result = reinterpret_cast<HWND*>(found);
	GetWindowRect(window, &rect);
	if (*result) GetWindowRect(*result, &best);
	if (!*result || (rect.right - rect.left) * (rect.bottom - rect.top) > (best.right - best.left) * (best.bottom - best.top)) *result = window;
	return TRUE;
}

void HookWindow() {
	if (GameWindow) return;
	HWND window {};
	EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&window));
	if (!window) return;
	OldProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(Proc)));
	if (!OldProc) {
		Log(true, "could not watch the game window: right click on Play does nothing");
		GameWindow = reinterpret_cast<HWND>(-1);
		return;
	}
	GameWindow = window;
}

// UI thread, every 100 ms: where the Play button is. Its layout rect is in
// layout units (the UI is drawn at the window's height / 2160), anchored to
// the middle of the screen.
void __cdecl UpdatePlay(const D2RL::PluginContext*, void*) noexcept {
	HookWindow();
	W::WidgetHandle panel {}, button {};
	W::Rect         rect {};
	RECT            client {};
	const bool      found = !InGame.load() && WidgetApi->findPanel(Context, "MainMenuPanel", &panel) == W::Result::Success
	                   && WidgetApi->findWidget(Context, panel, "QuickPlayButton", &button) == W::Result::Success
	                   && WidgetApi->getWidgetRect(Context, button, &rect) == W::Result::Success && GameWindow && GameWindow != reinterpret_cast<HWND>(-1)
	                   && GetClientRect(GameWindow, &client) && client.bottom > 0;
	if (!found) {
		PlayKnown = false;
		return;
	}
	const double scale  = client.bottom / 2160.0;
	const int    width  = rect.width > 0 ? rect.width : 520;
	const int    height = rect.height > 0 ? rect.height : 140;
	const int    left   = static_cast<int>(client.right / 2.0 + rect.x * scale);
	const int    top    = static_cast<int>(rect.y * scale);
	PlayLeft = left, PlayTop = top, PlayRight = left + static_cast<int>(width * scale), PlayBottom = top + static_cast<int>(height * scale);
	PlayKnown = true;
	static std::string told;
	char               line[200];
	std::snprintf(line, sizeof(line), "Play button at %d,%d %dx%d (layout), %d,%d-%d,%d in the %ldx%ld window", rect.x, rect.y, rect.width, rect.height, PlayLeft.load(), PlayTop.load(),
	              PlayRight.load(), PlayBottom.load(), client.right, client.bottom);
	if (told != line) Log(false, "%s", line);
	told = line;
}

DWORD WINAPI Poller(void*) {
	for (;;) {
		Sleep(100);
		(void)Threads->runOnUiThread(Context, UpdatePlay, nullptr);
	}
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
	if (key == "right_click_selector") return flag(Config.rightClick);
	if (key == "log_ui") return flag(Config.logUi);
	return error = "unknown setting", false;
}

std::string ValueOf(const std::string& key) {
	const auto flag = [](bool on) { return std::string(on ? "true" : "false"); };
	if (key == "enabled") return flag(Config.enabled);
	if (key == "right_click_selector") return flag(Config.rightClick);
	if (key == "log_ui") return flag(Config.logUi);
	return {};
}

std::string Render() {
	return R"(# Quick Play Difficulty
#
# On the character select screen, a left click on Play starts the game at the
# highest difficulty the selected character has unlocked, without the
# difficulty selector. A right click on Play opens the selector.
# Every setting can also be changed in game with I Hate TOMLs, and applies at once.

[quick-play-difficulty]
enabled = )" + ValueOf("enabled") + R"(

# Right click on Play opens the difficulty selector.
right_click_selector = )" + ValueOf("right_click_selector") + R"(

# Write the game's UI messages to logs/quick-play-difficulty-ui.log (for bug reports).
log_ui = )" + ValueOf("log_ui") + "\n";
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

constexpr const char* Schema = R"toml(title = "Quick Play Difficulty"
[[setting]]
key = "enabled"
type = "bool"
label = "Play starts at the highest difficulty"
note = "Left click on Play skips the difficulty selector."
[[setting]]
key = "right_click_selector"
type = "bool"
label = "Right click on Play opens the selector"
[[setting]]
key = "log_ui"
type = "bool"
label = "Log UI messages"
note = "Writes logs/quick-play-difficulty-ui.log, for bug reports."
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
	LogDirectory = Utf8(context->pluginLogPath);
	if (const auto slash = LogDirectory.find_last_of("\\/"); slash != std::string::npos) LogDirectory.erase(slash + 1);
	else LogDirectory.clear();

	Base = context->exeBase;
	if (Base && CodeMatches(SelectedCharacterRva, SelectedCharacterBytes) && CodeMatches(PlayCheckRva, PlayCheckBytes))
		SelectedCharacter = reinterpret_cast<SelectedCharacterFn>(Base + SelectedCharacterRva);
	WidgetApi = Query<D2RL::WidgetService>("widget");
	Threads   = Query<D2RL::ThreadService>("thread");
	EventApi  = Query<D2RL::SharedEventService>("shared event");
	if (WidgetApi && !D2RL::HasWidgetServiceField(WidgetApi, D2RL::WidgetServiceRequiredSize)) WidgetApi = nullptr;
	if (Threads && !D2RL::HasThreadServiceField(Threads, D2RL::ThreadServiceRequiredSize)) Threads = nullptr;
	if (EventApi && !D2RL::HasSharedEventServiceField(EventApi, D2RL::SharedEventServiceRequiredSize)) EventApi = nullptr;
	if (!SelectedCharacter) {
		Log(true, "Quick Play Difficulty %s: inactive, this game build isn't supported (Steam 3.3.94995 / 3.4.95041 are)", PluginVersion);
		return true;
	}
	if (!WidgetApi || !Threads || !EventApi) {
		Log(true, "Quick Play Difficulty %s: inactive, missing D2RLoader services (widget %s, thread %s, shared event %s)", PluginVersion, WidgetApi ? "ok" : "missing",
		    Threads ? "ok" : "missing", EventApi ? "ok" : "missing");
		return true;
	}
	const Events::UiMessageListener listener {
		.structSize = Events::UiMessageListenerSize,
		.priority   = 100, // before other plugins see the selector open
		.callback   = OnUiMessage,
	};
	Events::ListenerHandle uiHandle = Events::InvalidHandle;
	if (EventApi->registerUiMessageListener(context, &listener, &uiHandle) != Events::Result::Success) {
		Log(true, "Quick Play Difficulty %s: inactive, could not listen to the game's UI messages", PluginVersion);
		return true;
	}
	const D2RL::LifecycleService* lifecycle {};
	if (context->QueryService(&lifecycle) == D2RL::ServiceQueryResult::Success && lifecycle && D2RL::HasLifecycleServiceField(lifecycle, D2RL::LifecycleServiceRequiredSize)) {
		for (const auto kind : { D2RL::Lifecycle::GameplayEventKind::GameJoined, D2RL::Lifecycle::GameplayEventKind::GameLeft }) {
			const D2RL::Lifecycle::GameplayEventListener gameplay {
				.structSize = D2RL::Lifecycle::GameplayEventListenerSize,
				.kind       = kind,
				.callback   = OnGameplayEvent,
			};
			D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
			(void)lifecycle->registerGameplayEventListener(context, &gameplay, &handle);
		}
	}
	if (HANDLE thread = CreateThread(nullptr, 0, Poller, nullptr, 0, nullptr)) CloseHandle(thread);
	Log(false, "Quick Play Difficulty %s: %s", PluginVersion, Config.enabled ? "active" : "loaded but turned off");
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	Menu.Stop();
}
