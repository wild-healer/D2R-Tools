// Quest Rewards: on the character select screen, a flag on each character's
// button marks characters with a quest reward still waiting, and its tooltip
// says which, per difficulty:
//   - Charsi's imbue (Act I, Tools of the Trade)
//   - Larzuk's socket (Act V, Siege on Harrogath)
//   - Anya's personalization (Act V, Betrayal of Harrogath)
//
// Data: the offline save files (<Saved Games>\Diablo II Resurrected\[mods\<mod>\]
// *.d2s), read only. The quest block starts with "Woo!" (offset 403 in v105
// saves, 335 before), then 4 bytes version and 2 bytes size, then 96 bytes per
// difficulty. Each quest is a 16-bit word of flags (D2MOO's QFLAG_*): bit 0
// REWARDGRANTED, bit 1 REWARDPENDING. Words used: +6 Tools of the Trade,
// +70 Siege on Harrogath, +76 Betrayal of Harrogath.
//
// Display: plugins cannot change a game layout, so the companion D2RMM mod
// adds QuestFlag0..9 to D2RLoader's CharacterSelectPanel, one per visible row of the
// character list, each with a tooltip "@quest-rewards:Flag<n>". This plugin
// writes those strings (registered again whenever the saves may have changed)
// and hides the flags of rows with nothing waiting. Rows follow the game's
// list order: most recently played first.

#include <D2RLPlugin/api.h>
#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace {

namespace Events = D2RL::SharedEvents;

constexpr const char* PluginId      = "quest-rewards";
constexpr const char* PluginVersion = "1.0.0";

const D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = PluginId,
	.name        = "Quest Rewards",
	.version     = PluginVersion,
	.author      = "wild-healer",
	.description = "Flags the characters that still have Charsi's imbue, Larzuk's socket or Anya's personalization waiting, on the character select screen.",
	.flags       = D2RL::PluginFlags::Client,
};

struct Settings {
	bool enabled = true;
	bool logUi   = false;
	bool quest[3] = { true, true, true }; // charsi, larzuk, anya
} Config;

const D2RL::PluginContext*      Context {};
const D2RL::WidgetService*      WidgetApi {};
const D2RL::ResourceService*    ResourceApi {};
const D2RL::ThreadService*      Threads {};
const D2RL::SharedEventService* EventApi {};

std::string LogDirectory;

void Log(bool warn, const char* format, ...) noexcept {
	if (!Context) return;
	char    line[8192] {};
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

std::string Now() {
	SYSTEMTIME t {};
	GetLocalTime(&t);
	char text[32] {};
	std::snprintf(text, sizeof(text), "%04u-%02u-%02u %02u:%02u:%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
	return text;
}

// ---- config ----

std::string Trim(std::string_view text) {
	const auto begin = text.find_first_not_of(" \t\r");
	if (begin == std::string_view::npos) return {};
	const auto end = text.find_last_not_of(" \t\r");
	return std::string(text.substr(begin, end - begin + 1));
}

bool ParseBool(const std::string& value, bool fallback) {
	if (value.rfind("true", 0) == 0) return true;
	if (value.rfind("false", 0) == 0) return false;
	return fallback;
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
		const std::string value = Trim(std::string_view(line).substr(equals + 1));
		if (key == "enabled") Config.enabled = ParseBool(value, Config.enabled);
		else if (key == "log_ui") Config.logUi = ParseBool(value, Config.logUi);
		else if (key == "charsi") Config.quest[0] = ParseBool(value, Config.quest[0]);
		else if (key == "larzuk") Config.quest[1] = ParseBool(value, Config.quest[1]);
		else if (key == "anya") Config.quest[2] = ParseBool(value, Config.quest[2]);
	}
}

// ---- save files ----

enum class Reward : std::uint8_t { NotYet, Free, Used };

constexpr int         Difficulties = 3, Quests = 3;
constexpr const char* QuestNames[Quests]            = { "Charsi", "Larzuk", "Anya" };
constexpr const char* QuestRewards[Quests]          = { "imbue", "socket", "personalize" };
constexpr const char* DifficultyNames[Difficulties] = { "Normal", "Nightmare", "Hell" };
constexpr const char* ClassNames[]                  = { "Amazon", "Sorceress", "Necromancer", "Paladin", "Barbarian", "Druid", "Assassin", "Warlock" };

// Word offsets in a difficulty's 96 bytes of quest data.
constexpr std::size_t QuestWords[Quests] = { 6, 70, 76 }; // Tools of the Trade, Siege on Harrogath, Betrayal of Harrogath

struct Character {
	std::string   name;
	int           level {};
	int           classId = -1;
	ULONGLONG     written {};
	Reward        rewards[Quests][Difficulties] {};
	std::uint16_t raw[Quests][Difficulties] {};
};

std::vector<std::wstring> SaveDirectories() {
	std::vector<std::wstring> out;
	PWSTR                     folder = nullptr;
	if (SHGetKnownFolderPath(FOLDERID_SavedGames, 0, nullptr, &folder) == S_OK && folder) {
		const std::wstring base = std::wstring(folder) + L"\\Diablo II Resurrected\\";
		if (Context->activeMod && *Context->activeMod) out.push_back(base + L"mods\\" + Wide(Context->activeMod) + L"\\");
		else out.push_back(base);
	}
	if (folder) CoTaskMemFree(folder);
	return out;
}

std::string ReadFileBytes(const std::wstring& path) {
	std::string bytes;
	if (FILE* in = _wfopen(path.c_str(), L"rb")) {
		char buffer[16384];
		for (std::size_t read; (read = std::fread(buffer, 1, sizeof(buffer), in)) > 0;) bytes.append(buffer, read);
		std::fclose(in);
	}
	return bytes;
}

Reward Classify(std::uint16_t flags) {
	if (flags & 0x01) return Reward::Used;
	if (flags & 0x02) return Reward::Free;
	return Reward::NotYet;
}

bool ReadCharacter(const std::wstring& path, Character& character) {
	const std::string bytes = ReadFileBytes(path);
	if (bytes.size() < 0x200 || std::memcmp(bytes.data(), "\x55\xAA\x55\xAA", 4) != 0) return false;
	const auto woo = bytes.find("Woo!", 0x100);
	if (woo == std::string::npos || woo > 0x400) return false;
	const std::size_t quests = woo + 10;
	if (quests + Difficulties * 96 > bytes.size()) return false;
	const auto slash = path.find_last_of(L"\\/");
	character.name   = Utf8(path.substr(slash + 1, path.size() - slash - 5).c_str());
	// Class and level: bytes 24 and 27 in v97+ saves (40 and 43 before).
	std::uint32_t version = 0;
	std::memcpy(&version, bytes.data() + 4, 4);
	const std::size_t header = version >= 97 ? 24 : 40;
	character.classId        = static_cast<unsigned char>(bytes[header]);
	character.level          = static_cast<unsigned char>(bytes[header + 3]);
	for (int d = 0; d < Difficulties; ++d)
		for (int q = 0; q < Quests; ++q) {
			std::uint16_t flags = 0;
			std::memcpy(&flags, bytes.data() + quests + d * 96 + QuestWords[q], 2);
			character.raw[q][d]     = flags;
			character.rewards[q][d] = Classify(flags);
		}
	return true;
}

std::vector<Character> ReadCharacters() {
	std::vector<Character> out;
	for (const auto& folder : SaveDirectories()) {
		WIN32_FIND_DATAW data {};
		HANDLE           find = FindFirstFileW((folder + L"*.d2s").c_str(), &data);
		if (find == INVALID_HANDLE_VALUE) {
			Log(true, "no save files in %s", Utf8(folder.c_str()).c_str());
			continue;
		}
		do {
			Character character;
			if (!ReadCharacter(folder + data.cFileName, character)) {
				Log(true, "%s: not a save file this plugin can read", Utf8(data.cFileName).c_str());
				continue;
			}
			character.written = static_cast<ULONGLONG>(data.ftLastWriteTime.dwHighDateTime) << 32 | data.ftLastWriteTime.dwLowDateTime;
			out.push_back(std::move(character));
		} while (FindNextFileW(find, &data));
		FindClose(find);
	}
	// Most recently played first, like the game's list.
	std::sort(out.begin(), out.end(), [](const Character& a, const Character& b) { return a.written > b.written; });
	return out;
}

bool AnyFree(const Character& character) {
	for (int q = 0; q < Quests; ++q)
		if (Config.quest[q])
			for (const auto reward : character.rewards[q])
				if (reward == Reward::Free) return true;
	return false;
}

// The tooltip: one line per quest with a reward waiting.
std::string Tooltip(const Character& character) {
	std::string text;
	for (int q = 0; q < Quests; ++q) {
		if (!Config.quest[q]) continue;
		std::string where;
		for (int d = 0; d < Difficulties; ++d)
			if (character.rewards[q][d] == Reward::Free) where += std::string(where.empty() ? "" : ", ") + DifficultyNames[d];
		if (!where.empty()) text += std::string("\n") + QuestNames[q] + " (" + QuestRewards[q] + "): " + where;
	}
	return text.empty() ? "No quest reward waiting" : "Quest rewards waiting" + text;
}

std::string Summary(const std::vector<Character>& characters) {
	std::string summary;
	for (std::size_t r = 0; r < characters.size(); ++r) {
		const auto& character = characters[r];
		char        head[160] {};
		std::snprintf(head, sizeof(head), "\n  row %zu: %s %d %s:", r, character.name.c_str(), character.level,
		              character.classId >= 0 && character.classId < static_cast<int>(std::size(ClassNames)) ? ClassNames[character.classId] : "?");
		summary += head;
		for (int q = 0; q < Quests; ++q) {
			summary += std::string(" ") + QuestNames[q];
			for (int d = 0; d < Difficulties; ++d) {
				const auto reward = character.rewards[q][d];
				char       cell[32] {};
				std::snprintf(cell, sizeof(cell), " %c=%s(%04x)", DifficultyNames[d][0], reward == Reward::Free ? "free" : reward == Reward::Used ? "used" : "-", character.raw[q][d]);
				summary += cell;
			}
		}
	}
	return summary;
}

// ---- flags (UI thread) ----

constexpr int         RowPixels   = 184; // height of a row of the list (layout units)
constexpr int         FlagRows    = 10; // rows the companion mod adds (D2RLoader's list shows 9.3)
constexpr const char* StringsPath = "data/local/lng/strings/d2rloader/quest-rewards/flags.json";
constexpr const char* Languages[] = { "enUS", "deDE", "esES", "esMX", "frFR", "itIT", "jaJP", "koKR", "plPL", "ptBR", "ruRU", "zhCN", "zhTW" };

D2RL::Resources::RegistrationHandle StringsHandle = D2RL::Resources::InvalidHandle;
std::string                         StringsKey; // the text last registered

std::string Json(std::string_view text) {
	std::string out = "\"";
	for (const char c : text) {
		if (c == '"' || c == '\\') out += '\\';
		if (c == '\n') out += "\\n";
		else if (static_cast<unsigned char>(c) >= 0x20) out += c;
	}
	return out + '"';
}

// Writes Flag0..8 from the saves; returns the characters read.
std::vector<Character> RegisterStrings() {
	auto        characters = ReadCharacters();
	std::string json       = "[";
	for (int r = 0; r < FlagRows; ++r) {
		// An empty translation shows nothing; every language gets the same text.
		const std::string text = r < static_cast<int>(characters.size()) ? Tooltip(characters[static_cast<std::size_t>(r)]) : " ";
		json += std::string(r ? ",\n" : "\n") + R"({"Key": "Flag)" + std::to_string(r) + "\"";
		for (const char* language : Languages) json += std::string(", \"") + language + "\": " + Json(text);
		json += "}";
	}
	json += "\n]";
	if (json == StringsKey) return characters;
	if (StringsHandle != D2RL::Resources::InvalidHandle) {
		(void)ResourceApi->unregisterResource(Context, StringsHandle);
		StringsHandle = D2RL::Resources::InvalidHandle;
	}
	const D2RL::Resources::ResourceRegistration strings {
		.structSize = D2RL::Resources::ResourceRegistrationSize,
		.path       = StringsPath,
		.bytes      = json.data(),
		.byteCount  = json.size(),
	};
	const auto result = ResourceApi->registerResource(Context, &strings, &StringsHandle);
	if (result != D2RL::Resources::Result::Success) {
		StringsHandle = D2RL::Resources::InvalidHandle;
		Log(true, "could not register the tooltips (result %u)", static_cast<unsigned>(result));
	} else {
		StringsKey = json;
		Log(false, "tooltips written for %zu character(s)%s%s", characters.size(), characters.size() > FlagRows ? " (only the first 10 rows have a flag)" : "", Summary(characters).c_str());
	}
	return characters;
}

std::atomic_bool Pending {}, Queued {}, Online {}, OnScreen {};
int              Selected = -1; // the row with the gold selection frame, -1 = unknown (UI thread)
ULONGLONG        FirstTry {}, LastPoll {};
bool             FindLogged {};
std::vector<Character> Rows; // the characters the flags show, by row (UI thread)

// A row has two flags at the same place: QuestFlag<n>, and QuestFlagSel<n> for
// the selected row (the same flag with the selection frame's edge painted on,
// as the flags draw over the frame), plus the tooltip area QuestFlagTip<n>
// over both. Shows the right flag and the tooltip, or nothing. Returns false
// if the widgets are not found.
bool ShowFlag(D2RL::Widgets::WidgetHandle root, int row, bool want, bool selected) {
	bool found = false;
	for (const char* prefix : { "QuestFlag", "QuestFlagSel", "QuestFlagTip" }) {
		const std::string           name   = prefix + std::to_string(row);
		D2RL::Widgets::WidgetHandle handle = D2RL::Widgets::InvalidHandle;
		if (WidgetApi->findWidget(Context, root, name.c_str(), &handle) != D2RL::Widgets::Result::Success) continue;
		const bool show = want && (prefix[9] == 'T' || selected == (prefix[9] == 'S'));
		found |= WidgetApi->setWidgetVisible(Context, handle, show) == D2RL::Widgets::Result::Success;
	}
	return found;
}

bool Wanted(int row) { return !Online.load() && row < static_cast<int>(Rows.size()) && AnyFree(Rows[static_cast<std::size_t>(row)]); }

// Shows the flags of rows with something waiting, hides the others. The
// panel's widgets can appear a few frames after it opens: tried every UI
// update for up to 3 seconds.
bool ApplyFlags() {
	if (!WidgetApi) return true;
	D2RL::Widgets::WidgetHandle root = D2RL::Widgets::InvalidHandle;
	const auto                  found = WidgetApi->findPanel(Context, "CharacterSelectPanel", &root);
	if (found != D2RL::Widgets::Result::Success) {
		if (GetTickCount64() - FirstTry < 3000) return false;
		if (!FindLogged) Log(true, "character select panel not found (result %u): flags left as they are", static_cast<unsigned>(found));
		FindLogged = true;
		return true;
	}
	Rows      = RegisterStrings();
	int done = 0, shown = 0;
	for (int r = 0; r < FlagRows; ++r) {
		if (!ShowFlag(root, r, Wanted(r), r == Selected)) continue;
		++done;
		shown += Wanted(r);
	}
	if (done == 0) {
		if (GetTickCount64() - FirstTry < 3000) return false;
		if (!FindLogged)
			Log(true, "no QuestFlag widget in the character select panel: is the Quest Rewards D2RMM mod installed and enabled?");
		FindLogged = true;
		return true;
	}
	Log(false, "flags: %d of %d rows shown, row %d selected%s", shown, done, Selected, Online.load() ? " (online tab: all hidden)" : "");
	return true;
}

// The list moves its "Selection" image (the gold frame) to the selected row,
// and reports no message the plugins see: its position gives the row.
void PollSelection() {
	static bool                        logged = false;
	D2RL::Widgets::WidgetHandle        root = D2RL::Widgets::InvalidHandle, frame = D2RL::Widgets::InvalidHandle;
	D2RL::Widgets::Rect                rect {};
	if (WidgetApi->findPanel(Context, "CharacterSelectPanel", &root) != D2RL::Widgets::Result::Success
	    || WidgetApi->findWidget(Context, root, "Selection", &frame) != D2RL::Widgets::Result::Success || WidgetApi->getWidgetRect(Context, frame, &rect) != D2RL::Widgets::Result::Success) {
		if (!logged) Log(true, "the selection frame was not found: the selected row's flag draws over it");
		logged = true;
		return;
	}
	const int row = rect.y >= 0 ? (rect.y + RowPixels / 2) / RowPixels : -1;
	if (row == Selected) return;
	static int lastY = -100000;
	if (rect.y != lastY) Log(false, "selection frame at y %d: row %d", rect.y, row);
	lastY = rect.y;
	const int old = Selected;
	Selected      = row;
	for (const int r : { old, row })
		if (r >= 0 && r < FlagRows) (void)ShowFlag(root, r, Wanted(r), r == row);
}

// UI thread, every update while the character select screen is up.
void __cdecl Tick(const D2RL::PluginContext*, void*) noexcept {
	Queued.store(false);
	if (Pending.load() && ApplyFlags()) Pending.store(false);
	if (!Pending.load() && !OnScreen.load()) return;
	const ULONGLONG now = GetTickCount64();
	if (!Pending.load() && now - LastPoll >= 100) {
		LastPoll = now;
		PollSelection();
	}
	if (!Queued.exchange(true) && Threads->runOnUiThread(Context, Tick, nullptr) != D2RL::Threads::Result::Success) Queued.store(false);
}

void Refresh() {
	if (!Threads) return;
	Pending.store(true);
	OnScreen.store(true);
	if (Queued.exchange(true)) return;
	FirstTry   = GetTickCount64();
	FindLogged = false;
	if (Threads->runOnUiThread(Context, Tick, nullptr) != D2RL::Threads::Result::Success) Queued.store(false);
}

void __cdecl RewriteStrings(const D2RL::PluginContext*, void*) noexcept { (void)RegisterStrings(); }

// ---- UI messages ----

void LogUiMessage(const Events::UiMessageEvent* event) {
	static std::unordered_set<std::string> seen;
	static FILE*                           out {};
	if (seen.size() >= 1000 || LogDirectory.empty()) return;
	const std::string line = std::string(event->target ? event->target : "") + " | " + (event->command ? event->command : "") + " | " + (event->text ? event->text : "");
	if (!seen.insert(line).second) return;
	if (!out) out = _wfopen(Wide(LogDirectory + "quest-rewards-ui.log").c_str(), L"wb");
	if (!out) return;
	std::fprintf(out, "%s %s\r\n", Now().c_str(), line.substr(0, 400).c_str());
	std::fflush(out);
}

bool Is(const char* value, const char* expected) { return value && std::strcmp(value, expected) == 0; }

// The character select screen opens as CharacterSelectPanel; its tabs (Online,
// Offline) report CharacterSelect:SelectTab, also sent once the screen is ready.
auto OnUiMessage(const D2RL::PluginContext*, const Events::UiMessageEvent* event, void*) noexcept -> Events::UiMessageAction {
	if (!event) return Events::UiMessageAction::Continue;
	if (Config.logUi) LogUiMessage(event);
	if (Is(event->target, "PanelManager") && Is(event->text, "CharacterSelectPanel")) {
		if (Is(event->command, "OpenPanel") || Is(event->command, "OpenExclusivePanel")) Refresh();
		else if (Is(event->command, "ClosePanel") || Is(event->command, "UnloadPanel")) OnScreen.store(false);
	} else if (Is(event->target, "CharacterSelect") && Is(event->command, "SelectTab")) {
		static std::string lastTab = "?";
		const std::string  tab     = event->text ? event->text : "";
		if (tab != lastTab) Log(false, "character select tab: \"%s\"", tab.c_str());
		lastTab = tab;
		Online.store(tab == "0");
		Refresh();
	}
	return Events::UiMessageAction::Continue;
}

// The save is written just before GameLeft: the tooltips are rewritten before
// the character select screen loads again.
void __cdecl OnGameplayEvent(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent* event, void*) noexcept {
	if (!event || !Threads) return;
	if (event->kind == D2RL::Lifecycle::GameplayEventKind::GameJoined) OnScreen.store(false);
	else if (event->kind == D2RL::Lifecycle::GameplayEventKind::GameLeft) (void)Threads->runOnUiThread(Context, RewriteStrings, nullptr);
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
	if (!Config.enabled) {
		Log(false, "Quest Rewards %s: disabled in config", PluginVersion);
		return true;
	}
	LogDirectory = Utf8(context->pluginLogPath);
	if (const auto slash = LogDirectory.find_last_of("\\/"); slash != std::string::npos) LogDirectory.erase(slash + 1);
	else LogDirectory.clear();

	WidgetApi   = Query<D2RL::WidgetService>("widget");
	ResourceApi = Query<D2RL::ResourceService>("resource");
	Threads     = Query<D2RL::ThreadService>("thread");
	EventApi    = Query<D2RL::SharedEventService>("shared event");
	if (WidgetApi && !D2RL::HasWidgetServiceField(WidgetApi, D2RL::WidgetServiceRequiredSize)) WidgetApi = nullptr;
	if (ResourceApi && !D2RL::HasResourceServiceField(ResourceApi, D2RL::ResourceServiceRequiredSize)) ResourceApi = nullptr;
	if (Threads && !D2RL::HasThreadServiceField(Threads, D2RL::ThreadServiceRequiredSize)) Threads = nullptr;
	if (EventApi && !D2RL::HasSharedEventServiceField(EventApi, D2RL::SharedEventServiceRequiredSize)) EventApi = nullptr;
	if (!ResourceApi) {
		Log(true, "inactive: no resource service, the tooltips cannot be written");
		return true;
	}
	// The tooltips exist before the character select screen first loads.
	const auto characters = RegisterStrings();
	Log(false, "Quest Rewards %s: %zu character(s) in %s", PluginVersion, characters.size(), SaveDirectories().empty() ? "(no Saved Games folder)" : Utf8(SaveDirectories()[0].c_str()).c_str());

	if (!WidgetApi || !Threads || !EventApi) {
		Log(true, "flags always shown: missing services (widget %s, thread %s, shared event %s)", WidgetApi ? "ok" : "missing", Threads ? "ok" : "missing", EventApi ? "ok" : "missing");
		return true;
	}
	const Events::UiMessageListener listener {
		.structSize = Events::UiMessageListenerSize,
		.priority   = 0,
		.callback   = OnUiMessage,
	};
	Events::ListenerHandle uiHandle = Events::InvalidHandle;
	if (EventApi->registerUiMessageListener(context, &listener, &uiHandle) != Events::Result::Success) Log(true, "flags always shown: could not listen to the game's UI messages");
	if (const auto* lifecycle = Query<D2RL::LifecycleService>("lifecycle"); lifecycle && D2RL::HasLifecycleServiceField(lifecycle, D2RL::LifecycleServiceRequiredSize)) {
		for (const auto kind : { D2RL::Lifecycle::GameplayEventKind::GameJoined, D2RL::Lifecycle::GameplayEventKind::GameLeft }) {
			const D2RL::Lifecycle::GameplayEventListener listener {
				.structSize = D2RL::Lifecycle::GameplayEventListenerSize,
				.kind       = kind,
				.callback   = OnGameplayEvent,
			};
			D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
			(void)lifecycle->registerGameplayEventListener(context, &listener, &handle);
		}
	}
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {}
