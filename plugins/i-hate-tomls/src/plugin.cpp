// I Hate TOMLs (plugin id "i-hate-tomls"): one in-game window, drawn like the character screen, listing
// every running plugin with a button to its settings:
//
//   - plugins that joined the menu (include/i_hate_tomls_client.h) show their
//     settings in the window; they keep their own values and config, so
//     without this plugin they work from their TOML as before;
//   - plugins with a settings window of their own declare it, and the button
//     opens that window;
//   - other plugins with a TOML config show that file: its comments as text,
//     its values in check boxes and text boxes, saved by Apply (read by the
//     plugin at the next start).
//
// The window is the character screen's: its background (read from the game's
// files) with the stat boxes covered by plain stone made from the same art,
// its hinge, title and close button. Rows use the game's art: the loot filter
// list's rows and check box, the options arrows, the drop-down box frame for
// text boxes and buttons, the stash slot for the gear buttons (Grail On!'s
// Config button) and the scroll bar. Every view scrolls (wheel, bar); the
// list has a search bar; text boxes take keys through a keyboard hook.
//
// D2R keeps a panel's widgets from the first time it loads it, and plugins can
// only register panels while they load, so each view is a new layout in one of
// 60 panels registered at load: two take turns, each unloaded before it opens
// again (Run Counter's method). Buttons send PanelManager:ClosePanel:
// i-hate-tomls/Cmd-..., which the plugin catches.
//
// Opened from the Controls menu (I Hate TOMLs > Open I Hate TOMLs) or the
// console command "pluginmenu".

#include "../include/i_hate_tomls_client.h"
#include "gear.inc"
#include "toml_edit.h"
#include <D2RLPlugin/api.h>
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>
#include <cwctype>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr const char* PluginId      = "i-hate-tomls";
constexpr const char* PluginVersion = "1.0.0";
constexpr const char* CommandPrefix = "i-hate-tomls/Cmd-";

const D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = PluginId,
	.name        = "I Hate TOMLs",
	.version     = PluginVersion,
	.author      = "wild-healer",
	.description = "One in-game window listing the running plugins and their settings.",
	.flags       = D2RL::PluginFlags::Client,
};

const D2RL::PluginContext*      Context {};
const D2RL::ThreadService*      Threads {};
const D2RL::PanelService*       PanelApi {};
const D2RL::WidgetService*      WidgetApi {};
const D2RL::ResourceService*    ResourceApi {};
const D2RL::SharedEventService* EventApi {};

// ---- window geometry (the character screen's, HD layout units) ----

constexpr int PanelX = 236, PanelY = -651, PanelWidth = 1162, PanelHeight = 1507;
// Parts of the background covered with its own stone (measured on the
// 1162x1507 Character_Background): the area of the stat boxes, below the
// header box and above the bottom strip, and the experience bar in the header.
constexpr int CoverX = 70, CoverY = 306, CoverWidth = 1022, CoverHeight = 1040;
constexpr int PatchX = 80, PatchY = 465, PatchWidth = 420, PatchHeight = 165; // plain stone between two stat boxes
constexpr int BarX = 120, BarY = 208, BarWidth = 928, BarHeight = 44;         // the experience bar
constexpr int ContentLeft = 105, ContentRight = 1044;       // the scroll bar is right of the content
constexpr int HeaderY = 165;  // plugin name and its back arrow, in the header box
constexpr int RowsTop = 318;  // first row
constexpr int RowHeight = 96; // a joined plugin's settings rows
constexpr int ViewBottom = 1278;                       // rows end here
constexpr int ViewHeight = ViewBottom - RowsTop;
constexpr int NoteY   = ViewBottom + 4;                // inside the cover
constexpr int FooterY = 1364;                          // the bottom strip
constexpr int ControlLeft = 690;                       // where a joined plugin's control starts
constexpr int ListRowHeight = 80;                      // the plugin list
constexpr int ListRowWidth = ContentRight - ContentLeft, ListRowBack = 74;
constexpr int SearchY = RowsTop + 2;                   // the search bar, under the count
constexpr int TomlControl = 500;                       // where a TOML value's box starts
constexpr int BoxHeight = 56;
constexpr int ListTop = SearchY + BoxHeight + 18;      // the plugin list's first row
constexpr int TextBoxWidth = ContentRight - TomlControl;   // a value's text box
constexpr int ItemBoxWidth = TextBoxWidth - 70;            // a list item's, beside its remove button
constexpr int FieldHeight = 76, ItemHeight = 66;
constexpr int NoteLine = 34, NoteChars = 50;           // comment text: line height, characters per line (estimates: the game's
                                                       // small caps font is wide, about 17 px a character)
constexpr int LabelChar = 20, LabelLine = 50;          // a key's characters (estimate); a key too long for its column goes on
                                                       // its own line above the box
constexpr int WheelStep = 120;                         // pixels per wheel notch
constexpr int DropWidth = ContentRight - ControlLeft, TileHeight = 52; // a choice's drop-down box and its list's lines
constexpr int ArrowSize = 56;                          // the up/down arrows of a position's X and Y
constexpr int PositionLeft = ContentRight - ArrowSize - 20 - 2 * (32 + 2 * ArrowSize + 96 + 14); // a position's X
constexpr int ButtonWidth = 240;                       // Apply, Reload
constexpr int IconSize = 40;
constexpr int GearSize = 64;                           // the square button that opens a plugin's settings
constexpr int CharWidth = 17;                          // a text box's characters (estimate)
// The scroll bar, on the cover's right edge: a track cut into steps of
// StepHeight, each an invisible button scrolling to its share of the rows.
constexpr int BarLeft = 1054, ScrollWidth = 30, StepHeight = 30, MaxSteps = ViewHeight / StepHeight;
constexpr int PoolSize = 60;     // panels registered at load (64 allowed)

// ---- log ----

void Log(bool warn, const char* format, ...) noexcept {
	if (!Context) return;
	char    message[1024] {};
	va_list args;
	va_start(args, format);
	std::vsnprintf(message, sizeof(message), format, args);
	va_end(args);
	if (warn) Context->LogWarn(message);
	else Context->LogInfo(message);
}

void Say(const char* format, ...) noexcept {
	if (!Context) return;
	char    message[512] {};
	va_list args;
	va_start(args, format);
	std::vsnprintf(message, sizeof(message), format, args);
	va_end(args);
	Context->WriteConsoleMessage(message);
}

// ---- text helpers ----

std::string Json(std::string_view text) {
	std::string out = "\"";
	for (const char c : text) {
		if (c == '"' || c == '\\') out += '\\';
		if (c == '\n') out += "\\n";
		else if (c == '\t') out += ' ';
		else if (static_cast<unsigned char>(c) >= 0x20) out += c;
	}
	return out + '"';
}

std::string Trim(std::string_view text) {
	std::size_t start = 0, end = text.size();
	while (start < end && (text[start] == ' ' || text[start] == '\t' || text[start] == '\r' || text[start] == '\n')) ++start;
	while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r' || text[end - 1] == '\n')) --end;
	return std::string(text.substr(start, end - start));
}

// A TOML basic string's contents, or the text itself when it is not quoted.
std::string Unquote(std::string_view value) {
	std::string text = Trim(value);
	if (text.size() < 2 || text.front() != '"') return text;
	std::string out;
	for (std::size_t i = 1; i < text.size(); ++i) {
		const char c = text[i];
		if (c == '"') break;
		if (c == '\\' && i + 1 < text.size()) {
			const char next = text[++i];
			out += next == 'n' ? '\n' : next == 't' ? '\t' : next;
		} else out += c;
	}
	return out;
}

std::string Quote(std::string_view text) {
	std::string out = "\"";
	for (const char c : text) {
		if (c == '"' || c == '\\') out += '\\';
		out += c;
	}
	return out + '"';
}

// The items of a TOML array of strings or numbers.
std::vector<std::string> ArrayItems(std::string_view value) {
	std::vector<std::string> items;
	const std::string        text = Trim(value);
	if (text.empty() || text.front() != '[') return items;
	std::string item;
	bool        quoted = false, escaped = false;
	for (std::size_t i = 1; i < text.size(); ++i) {
		const char c = text[i];
		if (quoted) {
			item += c;
			if (escaped) escaped = false;
			else if (c == '\\') escaped = true;
			else if (c == '"') quoted = false;
			continue;
		}
		if (c == '"') quoted = true, item += c;
		else if (c == ',' || c == ']') {
			if (!Trim(item).empty()) items.push_back(Unquote(item));
			item.clear();
			if (c == ']') break;
		} else item += c;
	}
	return items;
}

// Written without printf's %f, which follows the game's locale.
std::string Decimal(double value, int decimals) {
	const bool negative = value < 0;
	double     scale    = 1;
	for (int i = 0; i < decimals; ++i) scale *= 10;
	long long  units = std::llround(std::fabs(value) * scale);
	std::string text = std::to_string(units / static_cast<long long>(scale));
	if (decimals > 0) {
		std::string fraction = std::to_string(units % static_cast<long long>(scale));
		while (static_cast<int>(fraction.size()) < decimals) fraction = "0" + fraction;
		text += "." + fraction;
	}
	return negative && units ? "-" + text : text;
}

double Number(std::string_view text, double fallback) {
	const std::string clean = Trim(text);
	if (clean.empty()) return fallback;
	char*        end   = nullptr;
	const double value = std::strtod(clean.c_str(), &end);
	// strtod follows the C locale ("."), which the plugin never changes.
	return end && end != clean.c_str() ? value : fallback;
}

std::string Narrow(const std::wstring& text) {
	if (text.empty()) return {};
	const int   size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
	std::string out(static_cast<std::size_t>(std::max(0, size)), '\0');
	if (size > 0) WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
	return out;
}

std::wstring Wide(const std::string& text) {
	if (text.empty()) return {};
	const int    size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
	std::wstring out(static_cast<std::size_t>(std::max(0, size)), L'\0');
	if (size > 0) MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size);
	return out;
}

std::wstring Folder(const std::wstring& path) {
	const auto slash = path.find_last_of(L"\\/");
	return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

std::wstring FileName(const std::wstring& path) {
	const auto slash = path.find_last_of(L"\\/");
	return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

bool FileExists(const std::wstring& path) {
	const DWORD attributes = GetFileAttributesW(path.c_str());
	return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool ReadWholeFile(const std::wstring& path, std::string& out) {
	FILE* in = _wfopen(path.c_str(), L"rb");
	if (!in) return false;
	out.clear();
	char buffer[16384];
	for (std::size_t n; (n = std::fread(buffer, 1, sizeof(buffer), in)) > 0;) out.append(buffer, n);
	std::fclose(in);
	return true;
}

// Written beside the file, then moved over it.
bool WriteWholeFile(const std::wstring& path, const std::string& bytes) {
	const std::wstring temp = path + L".i-hate-tomls.tmp";
	FILE*              out  = _wfopen(temp.c_str(), L"wb");
	if (!out) return false;
	const bool written = std::fwrite(bytes.data(), 1, bytes.size(), out) == bytes.size();
	const bool closed  = std::fclose(out) == 0;
	if (!written || !closed || !MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		DeleteFileW(temp.c_str());
		return false;
	}
	return true;
}

// ---- joined plugins ----

enum class Kind { Bool, Int, Float, Enum, String, Action, Header, Text, Position };

struct Setting {
	std::string              key, label, hint;
	Kind                     kind = Kind::Bool;
	std::vector<std::string> values, labels; // enum
	double                   min = 0, max = 100, step = 1;
	double                   shiftStep = 0, ctrlStep = 0; // Shift+click, Ctrl+click (0: 10 and 100 steps)
	int                      decimals = 0;
	bool                     restart {}, confirm {};
	std::string              note;        // text under the row, as a TOML comment
	std::string              defaultValue; // position: what its reset button sets
};

struct Client {
	uint64_t             handle {};
	std::string          plugin, title;
	std::vector<Setting> settings;
	PluginMenu::GetFn    get {};
	PluginMenu::SetFn    set {};
	void*                user {};
};

// A plugin with its own settings window.
struct OwnWindow {
	uint64_t           handle {};
	std::string        plugin;
	PluginMenu::OpenFn open {};
	void*              user {};
};

std::vector<Client>    Clients;
std::vector<OwnWindow> Windows;
uint64_t               NextHandle = 1;

Kind KindOf(const std::string& name) {
	if (name == "int") return Kind::Int;
	if (name == "float") return Kind::Float;
	if (name == "enum") return Kind::Enum;
	if (name == "string") return Kind::String;
	if (name == "action") return Kind::Action;
	if (name == "header") return Kind::Header;
	if (name == "text") return Kind::Text;
	if (name == "position") return Kind::Position;
	return Kind::Bool;
}

// Reads the schema: top-level keys, then one [[setting]] table per row.
void ReadSchema(const char* text, Client& client) {
	if (!text) return;
	Setting* current = nullptr;
	std::string_view rest(text);
	while (!rest.empty()) {
		const auto        end  = rest.find('\n');
		const std::string line = Trim(rest.substr(0, end));
		rest                   = end == std::string_view::npos ? std::string_view() : rest.substr(end + 1);
		if (line.empty() || line[0] == '#') continue;
		if (line == "[[setting]]") {
			client.settings.emplace_back();
			current = &client.settings.back();
			continue;
		}
		const auto equals = line.find('=');
		if (equals == std::string::npos) continue;
		const std::string key   = Trim(std::string_view(line).substr(0, equals));
		std::string       value = Trim(std::string_view(line).substr(equals + 1));
		// A comment after a value that is not a string.
		if (!value.empty() && value[0] != '"' && value[0] != '[')
			if (const auto hash = value.find('#'); hash != std::string::npos) value = Trim(std::string_view(value).substr(0, hash));
		if (!current) {
			if (key == "title") client.title = Unquote(value);
			continue;
		}
		if (key == "key") current->key = Unquote(value);
		else if (key == "type") current->kind = KindOf(Unquote(value));
		else if (key == "label") current->label = Unquote(value);
		else if (key == "hint") current->hint = Unquote(value);
		else if (key == "values") current->values = ArrayItems(value);
		else if (key == "labels") current->labels = ArrayItems(value);
		else if (key == "min") current->min = Number(value, current->min);
		else if (key == "max") current->max = Number(value, current->max);
		else if (key == "step") current->step = Number(value, current->step);
		else if (key == "decimals") current->decimals = static_cast<int>(Number(value, 0));
		else if (key == "restart") current->restart = value == "true";
		else if (key == "confirm") current->confirm = value == "true";
		else if (key == "shift_step") current->shiftStep = Number(value, 0);
		else if (key == "ctrl_step") current->ctrlStep = Number(value, 0);
		else if (key == "note") current->note = Unquote(value);
		else if (key == "default") current->defaultValue = value;
	}
	for (auto& s : client.settings) {
		if (s.label.empty()) s.label = s.key;
		if (s.step <= 0) s.step = 1;
		if (s.shiftStep <= 0) s.shiftStep = s.step * 10;
		if (s.ctrlStep <= 0) s.ctrlStep = s.step * 100;
		if (s.kind == Kind::Position && s.min == 0 && s.max == 100) s.min = -100000, s.max = 100000;
		if (s.kind == Kind::Int) s.decimals = 0;
	}
	// Rows a menu cannot show.
	std::erase_if(client.settings, [](const Setting& s) { return s.kind != Kind::Header && s.kind != Kind::Text && s.key.empty(); });
}

// ---- running plugins ----
// Every loaded DLL that exports D2RLoaderGetPluginInfo is a plugin; its config
// is <its plugins folder>\..\config\<id>.toml, as D2RLoader names it.

struct Plugin {
	std::string  id, name, version, author;
	std::wstring config; // empty when it has no TOML config
};

std::vector<Plugin> Running;

void FindRunningPlugins() {
	Running.clear();
	using EnumFn       = BOOL(WINAPI*)(HANDLE, HMODULE*, DWORD, LPDWORD);
	const auto enumerate = reinterpret_cast<EnumFn>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "K32EnumProcessModules")));
	if (!enumerate) {
		Log(true, "cannot list the loaded plugins: K32EnumProcessModules is missing");
		return;
	}
	std::vector<HMODULE> modules(1024);
	DWORD                needed = 0;
	if (!enumerate(GetCurrentProcess(), modules.data(), static_cast<DWORD>(modules.size() * sizeof(HMODULE)), &needed)) return;
	modules.resize(std::min<std::size_t>(modules.size(), needed / sizeof(HMODULE)));
	const std::wstring ownConfigFolder = Context->pluginConfigPath ? Folder(Context->pluginConfigPath) : std::wstring();
	using InfoFn                       = const D2RL::PluginInfo*(__cdecl*)() noexcept;
	for (HMODULE module : modules) {
		const auto get = reinterpret_cast<InfoFn>(reinterpret_cast<void*>(GetProcAddress(module, D2RL_PLUGIN_GET_INFO_EXPORT)));
		if (!get) continue;
		const D2RL::PluginInfo* info = get();
		if (!info || info->infoSize < D2RL::PluginInfoRequiredSize || !info->id || !info->id[0] || std::strcmp(info->id, PluginId) == 0) continue;
		if (std::any_of(Running.begin(), Running.end(), [&](const Plugin& p) { return p.id == info->id; })) continue;
		Plugin plugin { .id = info->id, .name = info->name && info->name[0] ? info->name : info->id, .version = info->version ? info->version : "", .author = info->author ? info->author : "" };
		wchar_t path[MAX_PATH * 2] {};
		if (GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)))) {
			const std::wstring folder = Folder(path), id = Wide(plugin.id);
			const std::wstring config = Folder(folder) + L"\\config\\" + id + L".toml";
			if (FileExists(config)) plugin.config = config;
			else if (!ownConfigFolder.empty() && FileExists(ownConfigFolder + L"\\" + id + L".toml")) plugin.config = ownConfigFolder + L"\\" + id + L".toml";
		}
		Running.push_back(std::move(plugin));
	}
	Log(false, "%zu other plugins running", Running.size());
}

// ---- config copies ----
// A D2RMM install rebuilds the mod's folder, configs included. Every plugin
// config is copied to <game>\d2rloader\I Hate TOMLs\<mod>\, outside D2RMM's
// reach. At load (this DLL sorts first, so before the other plugins read
// theirs) the stored values are written back onto each config key by key, so
// settings and comments added by plugin updates stay; a config missing from
// the folder comes back whole. While the game runs, any config that changes
// (in-game settings, TOML editor, a plugin's own writes) is copied again.
namespace Backup {
std::wstring ConfigDir, StoreDir;
std::map<std::wstring, std::pair<std::uint64_t, std::uint64_t>> Seen; // file -> (write time, size)

bool ReadAll(const std::wstring& path, std::string& out) {
	out.clear();
	HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) return false;
	LARGE_INTEGER size {};
	bool          ok = GetFileSizeEx(file, &size) && size.QuadPart < (16 << 20);
	if (ok) {
		out.resize(static_cast<std::size_t>(size.QuadPart));
		DWORD read = 0;
		ok = out.empty() || (ReadFile(file, out.data(), static_cast<DWORD>(out.size()), &read, nullptr) && read == out.size());
	}
	CloseHandle(file);
	return ok;
}

bool WriteAll(const std::wstring& path, const std::string& bytes) {
	const std::wstring temp = path + L".iht-tmp";
	HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) return false;
	DWORD written = 0;
	const bool ok = (bytes.empty() || WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr)) && written == bytes.size();
	CloseHandle(file);
	if (ok && MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
	DeleteFileW(temp.c_str());
	return false;
}

void MakeFolders(const std::wstring& path) {
	for (std::size_t at = path.find(L'\\', 3); ; at = path.find(L'\\', at + 1)) {
		(void)CreateDirectoryW(path.substr(0, at).c_str(), nullptr);
		if (at == std::wstring::npos) break;
	}
}

bool IsToml(const std::wstring& name) { return name.size() > 5 && _wcsicmp(name.c_str() + name.size() - 5, L".toml") == 0; }

std::vector<std::wstring> Tomls(const std::wstring& folder) {
	std::vector<std::wstring> names;
	WIN32_FIND_DATAW found {};
	HANDLE           find = FindFirstFileW((folder + L"\\*.toml").c_str(), &found);
	if (find == INVALID_HANDLE_VALUE) return names;
	do
		if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && IsToml(found.cFileName)) names.push_back(found.cFileName);
	while (FindNextFileW(find, &found));
	FindClose(find);
	return names;
}

// Each key's value as written, by "table\nkey" ([[array tables]] numbered).
std::map<std::string, const TomlDoc::Value*> Keys(const TomlDoc::Document& doc, std::vector<std::pair<std::string, int>>* order = nullptr) {
	std::map<std::string, const TomlDoc::Value*> keys;
	std::map<std::string, int>                   arrays;
	std::string                                  table;
	for (int k = 0; k < static_cast<int>(doc.items.size()); ++k) {
		const auto& item = doc.items[static_cast<std::size_t>(k)];
		if (item.kind == TomlDoc::Kind::Table) table = item.text;
		else if (item.kind == TomlDoc::Kind::ArrayTable) table = item.text + "[" + std::to_string(arrays[item.text]++) + "]";
		else if (item.kind == TomlDoc::Kind::Key) {
			const std::string key = table + "\n" + item.text;
			keys.emplace(key, &item.value);
			if (order) order->emplace_back(key, k);
		}
	}
	return keys;
}

// `actual` with the stored values written over it; false when nothing changed
// or the result would not read back.
bool Merge(const std::string& storedBytes, const std::string& actualBytes, std::string& out, int& changed) {
	const auto stored = TomlDoc::Parse(storedBytes), actual = TomlDoc::Parse(actualBytes);
	if (!stored.error.empty() || !actual.error.empty()) return false;
	const auto saved = Keys(stored);
	std::vector<std::pair<std::string, int>> order;
	(void)Keys(actual, &order);
	struct Edit {
		std::size_t start, end;
		std::string text;
	};
	std::vector<Edit> edits;
	for (const auto& [key, index] : order) {
		const auto it = saved.find(key);
		if (it == saved.end()) continue;
		const auto& now = actual.items[static_cast<std::size_t>(index)].value;
		const auto& old = *it->second;
		const bool  number = (now.type == TomlDoc::Type::Integer || now.type == TomlDoc::Type::Float) && (old.type == TomlDoc::Type::Integer || old.type == TomlDoc::Type::Float);
		if (now.type != old.type && !number) continue; // the setting changed kind: the plugin's new default stays
		std::string text = storedBytes.substr(old.start, old.end - old.start);
		if (text == actualBytes.substr(now.start, now.end - now.start)) continue;
		edits.push_back({ now.start, now.end, std::move(text) });
	}
	changed = static_cast<int>(edits.size());
	if (edits.empty()) return false;
	out = actualBytes;
	std::sort(edits.begin(), edits.end(), [](const Edit& a, const Edit& b) { return a.start > b.start; });
	for (const auto& e : edits) out.replace(e.start, e.end - e.start, e.text);
	return TomlDoc::Parse(out).error.empty();
}

std::pair<std::uint64_t, std::uint64_t> Stamp(const std::wstring& path) {
	WIN32_FILE_ATTRIBUTE_DATA data {};
	if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return { 0, 0 };
	return { (static_cast<std::uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime,
		     (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow };
}

// A config that reads as TOML is copied when it differs from its copy.
void Store(const std::wstring& name, bool quiet) {
	std::string now, copy;
	Seen[name] = Stamp(ConfigDir + L"\\" + name);
	if (!ReadAll(ConfigDir + L"\\" + name, now) || !TomlDoc::Parse(now).error.empty()) return;
	if (ReadAll(StoreDir + L"\\" + name, copy) && copy == now) return;
	if (WriteAll(StoreDir + L"\\" + name, now)) {
		if (!quiet) Log(false, "config copy updated: %s", Narrow(name).c_str());
	} else Log(true, "could not copy %s to %s", Narrow(name).c_str(), Narrow(StoreDir).c_str());
}

void Restore() {
	if (!Context->pluginConfigPath) return;
	ConfigDir = Folder(Context->pluginConfigPath);
	std::wstring lower = ConfigDir;
	for (auto& c : lower) c = static_cast<wchar_t>(std::towlower(c));
	std::wstring game, mod = L"base";
	if (const auto at = lower.rfind(L"\\mods\\"); at != std::wstring::npos) {
		game          = ConfigDir.substr(0, at);
		const auto to = ConfigDir.find(L'\\', at + 6);
		mod           = ConfigDir.substr(at + 6, to == std::wstring::npos ? std::wstring::npos : to - at - 6);
	} else game = Folder(Folder(ConfigDir)); // <game>\d2rloader\config
	if (game.empty() || mod.empty()) return;
	StoreDir = game + L"\\d2rloader\\I Hate TOMLs\\" + mod;
	MakeFolders(StoreDir);
	int restored = 0, merged = 0;
	for (const auto& name : Tomls(StoreDir)) {
		const std::wstring path = ConfigDir + L"\\" + name;
		std::string stored, actual, out;
		if (!ReadAll(StoreDir + L"\\" + name, stored) || !TomlDoc::Parse(stored).error.empty()) continue;
		if (!FileExists(path)) {
			if (WriteAll(path, stored)) ++restored, Log(false, "config restored from its copy: %s", Narrow(name).c_str());
			continue;
		}
		int changed = 0;
		if (!ReadAll(path, actual) || !Merge(stored, actual, out, changed)) continue;
		if (WriteAll(path, out)) ++merged, Log(false, "config %s: %d stored values written back", Narrow(name).c_str(), changed);
		else Log(true, "could not write %s", Narrow(path).c_str());
	}
	for (const auto& name : Tomls(ConfigDir)) Store(name, true);
	Log(false, "config copies in %s: %d restored, %d updated with stored values, %zu kept", Narrow(StoreDir).c_str(), restored, merged, Seen.size());
	// configs that change while the game runs are copied again
	std::thread([] {
		for (;;) {
			Sleep(2000);
			for (const auto& name : Tomls(ConfigDir)) {
				const auto stamp = Stamp(ConfigDir + L"\\" + name);
				const auto it    = Seen.find(name);
				if (it != Seen.end() && it->second == stamp) continue;
				Sleep(200); // let the writer finish
				Store(name, false);
			}
		}
	}).detach();
}
} // namespace Backup

enum class Mode { None, Toml, Menu, Window };

struct Entry {
	std::string  id, name, version, author;
	std::wstring config;
	Mode         mode = Mode::None;
};

int ClientIndex(const std::string& plugin) {
	for (std::size_t i = 0; i < Clients.size(); ++i)
		if (Clients[i].plugin == plugin) return static_cast<int>(i);
	return -1;
}

const OwnWindow* WindowOf(const std::string& plugin) {
	for (const auto& w : Windows)
		if (w.plugin == plugin) return &w;
	return nullptr;
}

// The list as shown: every running plugin, plus any that joined but was not found.
std::vector<Entry> Entries() {
	std::vector<Entry> list;
	for (const auto& p : Running) list.push_back({ p.id, p.name, p.version, p.author, p.config });
	const auto add = [&](const std::string& id, const std::string& name) {
		if (std::none_of(list.begin(), list.end(), [&](const Entry& e) { return e.id == id; })) list.push_back({ .id = id, .name = name });
	};
	for (const auto& c : Clients) add(c.plugin, c.title);
	for (const auto& w : Windows) add(w.plugin, w.plugin);
	for (auto& e : list) e.mode = WindowOf(e.id) ? Mode::Window : ClientIndex(e.id) >= 0 ? Mode::Menu : !e.config.empty() ? Mode::Toml : Mode::None;
	std::sort(list.begin(), list.end(), [](const Entry& a, const Entry& b) { return _stricmp(a.name.c_str(), b.name.c_str()) < 0; });
	return list;
}

// ---- the window's state ----

enum class View { List, Client, Toml };

View        Current  = View::List;
int         ListScroll   = 0; // first row shown of the plugin list
int         Shown        = 0; // index of the joined plugin shown
int         ClientScroll = 0; // first row shown of its settings
std::string SearchText;       // the list's search bar
int         ConfirmRow = -1;  // action row waiting for its second click
ULONGLONG   ConfirmUntil {};
std::string Notice;           // one line under the rows
bool        NoticeWarn {};

void SetNotice(std::string text, bool warn = false) {
	Notice     = std::move(text);
	NoticeWarn = warn;
}

void ShowView();
bool MenuOpen();
void CloseLeftPanels();

// Typing: the game's own text boxes don't get the keys in a plugin's panel
// (they go to the game's key bindings), so, as Grail On! does, a keyboard
// hook takes them while a box is active (see "typing" below). The box being
// typed in: "search", "f<field>" (a TOML value), "i<field>.<item>" (a TOML
// list item) or "s<setting>" (a joined plugin's text).
std::atomic<bool> Typing {};
std::string       TypingKey;    // UI thread
std::string       TypingBefore; // its text when typing began (Esc puts it back)
std::string       ClientDraft;  // a joined plugin's text being typed

// The scroll bar of the view shown, for the mouse (read on the window's thread).
std::atomic<int> BarTop {}, BarSteps {}, BarMax {};
std::atomic<bool> MenuVisible {}; // the menu is open, as last seen on the UI thread
void              WatchMouse();

void __cdecl RefreshTask(const D2RL::PluginContext*, void*) noexcept {
	if (MenuOpen()) ShowView();
}

void QueueRefresh() {
	if (Threads) (void)Threads->runOnUiThread(Context, RefreshTask, nullptr);
}

// ---- the service ----

uint64_t __cdecl RegisterClient(const D2RL::PluginContext* caller, const PluginMenu::ClientInfo* info) noexcept {
	if (!caller || !caller->pluginId || !info || info->structSize < sizeof(PluginMenu::ClientInfo) || !info->get || !info->set) {
		Log(true, "a plugin's registration was refused: incomplete");
		return 0;
	}
	const uint64_t handle = NextHandle++;
	Client         client { .handle = handle, .plugin = caller->pluginId, .get = info->get, .set = info->set, .user = info->user };
	ReadSchema(info->schemaToml, client);
	if (client.title.empty()) client.title = client.plugin;
	Log(false, "%s joined the menu as \"%s\" with %zu rows", client.plugin.c_str(), client.title.c_str(), client.settings.size());
	// One entry per plugin: a second registration replaces the first.
	std::erase_if(Clients, [&](const Client& c) { return c.plugin == client.plugin; });
	std::erase_if(Windows, [&](const OwnWindow& w) { return w.plugin == client.plugin; });
	Clients.push_back(std::move(client));
	QueueRefresh();
	return handle;
}

uint64_t __cdecl RegisterInterface(const D2RL::PluginContext* caller, const PluginMenu::InterfaceInfo* info) noexcept {
	if (!caller || !caller->pluginId || !info || info->structSize < sizeof(PluginMenu::InterfaceInfo) || !info->open) {
		Log(true, "a plugin's settings window was refused: incomplete");
		return 0;
	}
	const uint64_t handle = NextHandle++;
	std::erase_if(Clients, [&](const Client& c) { return c.plugin == caller->pluginId; });
	std::erase_if(Windows, [&](const OwnWindow& w) { return w.plugin == caller->pluginId; });
	Windows.push_back({ handle, caller->pluginId, info->open, info->user });
	Log(false, "%s declared its own settings window", caller->pluginId);
	QueueRefresh();
	return handle;
}

void __cdecl UnregisterClient(uint64_t handle) noexcept {
	std::erase_if(Clients, [&](const Client& c) { return c.handle == handle; });
	std::erase_if(Windows, [&](const OwnWindow& w) { return w.handle == handle; });
	Shown = std::clamp(Shown, 0, std::max(0, static_cast<int>(Clients.size()) - 1));
	if (Current == View::Client && Clients.empty()) Current = View::List;
}

void __cdecl ValueChanged(uint64_t, const char*) noexcept { QueueRefresh(); }

void __cdecl OpenFor(uint64_t handle) noexcept {
	for (std::size_t i = 0; i < Clients.size(); ++i)
		if (Clients[i].handle == handle) Shown = static_cast<int>(i), ClientScroll = 0, Current = View::Client;
	if (Threads)
		(void)Threads->runOnUiThread(
		    Context,
		    [](const D2RL::PluginContext*, void*) noexcept {
			    if (!MenuOpen()) CloseLeftPanels();
			    ShowView();
		    },
		    nullptr);
}

const PluginMenu::Service Service {
	.serviceSize       = sizeof(PluginMenu::Service),
	.serviceVersion    = 1,
	.registerClient    = RegisterClient,
	.unregisterClient  = UnregisterClient,
	.valueChanged      = ValueChanged,
	.open              = OpenFor,
	.registerInterface = RegisterInterface,
};

// ---- joined plugins' values ----

std::string GetValue(const Client& client, const Setting& setting) {
	char out[512] {};
	if (!client.get(setting.key.c_str(), out, sizeof(out), client.user)) return {};
	return Trim(out);
}

bool SetValue(Client& client, const Setting& setting, const std::string& value) {
	char error[256] {};
	const bool ok = client.set(setting.key.c_str(), value.c_str(), error, sizeof(error), client.user);
	Log(!ok, "%s: %s set to %s%s%s", client.plugin.c_str(), setting.key.c_str(), value.c_str(), ok ? "" : " REFUSED: ", ok ? "" : error);
	if (ok) SetNotice(setting.restart ? setting.label + " applies after a restart." : std::string());
	else SetNotice(setting.label + ": " + (error[0] ? error : "not changed"), true);
	return ok;
}

// The index of a value among an enum's values (TOML text compared unquoted).
int EnumIndex(const Setting& setting, const std::string& value) {
	const std::string bare = Unquote(value);
	for (std::size_t i = 0; i < setting.values.size(); ++i)
		if (setting.values[i] == bare) return static_cast<int>(i);
	return -1;
}

std::string EnumLabel(const Setting& setting, int index) {
	if (index < 0 || index >= static_cast<int>(setting.values.size())) return "?";
	return index < static_cast<int>(setting.labels.size()) ? setting.labels[static_cast<std::size_t>(index)] : setting.values[static_cast<std::size_t>(index)];
}

// What a row shows as its value.
std::string ShownValue(const Client& client, const Setting& setting) {
	const std::string value = GetValue(client, setting);
	switch (setting.kind) {
	case Kind::Enum: return EnumLabel(setting, EnumIndex(setting, value));
	case Kind::Int:
	case Kind::Float: return Decimal(Number(value, setting.min), setting.decimals);
	case Kind::String: return Unquote(value);
	default: return value;
	}
}

// The step of a click: Shift and Ctrl held make it bigger.
double StepSize(const Setting& setting) {
#ifdef _WIN32
	if (GetKeyState(VK_CONTROL) & 0x8000) return setting.ctrlStep;
	if (GetKeyState(VK_SHIFT) & 0x8000) return setting.shiftStep;
#endif
	return setting.step;
}

// Steps a choice or number one way and saves it.
void Step(Client& client, const Setting& setting, bool up) {
	const std::string value = GetValue(client, setting);
	if (setting.kind == Kind::Enum) {
		const int count = static_cast<int>(setting.values.size());
		if (!count) return;
		int index = EnumIndex(setting, value);
		index     = index < 0 ? 0 : (index + (up ? 1 : count - 1)) % count;
		(void)SetValue(client, setting, Quote(setting.values[static_cast<std::size_t>(index)]));
		return;
	}
	const double size   = StepSize(setting);
	double       number = Number(value, setting.min) + (up ? size : -size);
	number              = std::clamp(number, setting.min, setting.max);
	(void)SetValue(client, setting, Decimal(number, setting.decimals));
}

// Steps one axis of a position ("[x, y]") and saves it.
void StepPosition(Client& client, const Setting& setting, bool y, bool up) {
	auto [px, py]     = std::pair<double, double> {};
	const auto items  = ArrayItems(GetValue(client, setting));
	px                = items.size() > 0 ? Number(items[0], 0) : 0;
	py                = items.size() > 1 ? Number(items[1], 0) : 0;
	const double size = StepSize(setting);
	double&      v    = y ? py : px;
	v                 = std::clamp(v + (up ? size : -size), setting.min, setting.max);
	(void)SetValue(client, setting, "[" + Decimal(px, setting.decimals) + ", " + Decimal(py, setting.decimals) + "]");
}

// ---- a plugin's TOML config ----

struct TomlRow {
	enum class Type { Note, Section, Field, Item } type = Type::Note;
	int         field = -1, item = -1;
	int         height {};
	std::string text;
	bool        stacked {}; // a field whose key is above its box
	int         noteLines {}; // a field's own comment
};

struct TomlPage {
	std::string                  plugin, name;
	std::wstring                 path;
	TomlDoc::Document            doc;
	std::vector<TomlEdit::Field> fields;
	std::vector<TomlRow>         rows;
	int                          scroll {}; // first row shown
	bool                         leaving {}; // Back clicked once with changes not applied
};

TomlPage Toml;

int Lines(const std::string& text) {
	int         lines = 0;
	std::size_t start = 0;
	while (start <= text.size()) {
		const auto        end   = text.find('\n', start);
		const std::string line  = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
		int               chars = 0;
		for (const char c : line)
			if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++chars;
		lines += std::max(1, (chars + NoteChars - 1) / NoteChars);
		if (end == std::string::npos) break;
		start = end + 1;
	}
	return lines;
}

// A comment line that only continues the one before it: that one is long and
// does not end a sentence, and this one starts with plain text, not a list
// mark or indentation.
bool JoinsPrevious(const std::string& previous, const std::string& line) {
	if (previous.size() < 50 || line.empty()) return false;
	const char last = previous.back(), first = line.front();
	if (last == '.' || last == ':' || last == '!' || last == '?') return false;
	if (first == ' ' || first == '\t' || first == '-' || first == '*' || first == '#' || first == '|' || first == '=' || first == '[') return false;
	if (static_cast<unsigned char>(first) >= 0x80) return false; // box-drawing lines and the like
	return true;
}

void BuildTomlRows() {
	auto& rows = Toml.rows;
	rows.clear();
	std::vector<std::string> paragraph;
	const auto               flush = [&] {
        // a long comment is cut into rows that each fit the window
        const int   most = (ViewHeight - 12) / NoteLine;
        std::string text;
        int         lines = 0;
        const auto  emit  = [&] {
            if (!text.empty()) rows.push_back({ .type = TomlRow::Type::Note, .height = lines * NoteLine + 12, .text = text });
            text.clear();
            lines = 0;
        };
        for (const auto& line : paragraph) {
            const int count = Lines(line);
            if (lines && lines + count > most) emit();
            text += (text.empty() ? "" : "\n") + line;
            lines += count;
        }
        emit();
        paragraph.clear();
	};
	std::map<std::string, int> seen; // arrays of tables: their number
	int                        field = 0;
	for (const auto& item : Toml.doc.items) {
		switch (item.kind) {
		case TomlDoc::Kind::Comment: {
			std::string line = item.text;
			if (!line.empty() && line[0] == ' ') line.erase(0, 1);
			while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) line.pop_back();
			// a sentence the file wrapped by hand flows on in the window
			if (!paragraph.empty() && !line.empty() && JoinsPrevious(paragraph.back(), line)) paragraph.back() += " " + line;
			else paragraph.push_back(line);
			break;
		}
		case TomlDoc::Kind::Blank: flush(); break;
		case TomlDoc::Kind::Table:
		case TomlDoc::Kind::ArrayTable: {
			flush();
			std::string title = item.text;
			if (item.kind == TomlDoc::Kind::ArrayTable) title += "  #" + std::to_string(++seen[item.text]);
			rows.push_back({ .type = TomlRow::Type::Section, .height = 96, .text = title });
			if (!item.comment.empty()) rows.push_back({ .type = TomlRow::Type::Note, .height = Lines(item.comment) * NoteLine + 12, .text = item.comment });
			break;
		}
		case TomlDoc::Kind::Key: {
			flush();
			const auto& f = Toml.fields[static_cast<std::size_t>(field)];
			// a check box is at the row's end: its key has the room before it
			const int  room    = f.editor == TomlEdit::Editor::Check ? ContentRight - ContentLeft - 90 : TomlControl - ContentLeft - 16;
			const bool stacked = static_cast<int>(item.text.size()) * LabelChar > room;
			const int  notes   = item.comment.empty() ? 0 : Lines(item.comment);
			rows.push_back({ .type = TomlRow::Type::Field, .field = field, .height = FieldHeight + (stacked ? LabelLine : 0) + notes * NoteLine, .stacked = stacked, .noteLines = notes });
			if (f.editor == TomlEdit::Editor::List)
				for (int k = 0; k < static_cast<int>(f.list.size()); ++k) rows.push_back({ .type = TomlRow::Type::Item, .field = field, .item = k, .height = ItemHeight });
			++field;
			break;
		}
		}
	}
	flush();
}

// Reads the file again; false with the reason.
bool LoadToml(const std::wstring& path, std::string& why) {
	std::string bytes;
	if (!ReadWholeFile(path, bytes)) {
		Toml.doc = {};
		Toml.doc.error = "the file could not be read";
		Toml.fields.clear();
		BuildTomlRows();
		return why = Toml.doc.error, false;
	}
	Toml.doc    = TomlDoc::Parse(std::move(bytes));
	Toml.fields = Toml.doc.error.empty() ? TomlEdit::Fields(Toml.doc) : std::vector<TomlEdit::Field>();
	if (!Toml.doc.error.empty()) Toml.doc.items.clear();
	TomlEdit::Mark(Toml.doc, Toml.fields);
	Toml.leaving = false;
	BuildTomlRows();
	if (!Toml.doc.error.empty()) return why = "this file is not valid TOML (" + Toml.doc.error + ")", false;
	return true;
}

// True when the boxes hold something the file does not.
bool TomlChanged() {
	if (!Toml.doc.error.empty()) return false;
	std::string out, why;
	int         changed = 0;
	return !TomlEdit::Apply(Toml.doc, Toml.fields, out, why, changed) || changed > 0;
}

void ApplyToml() {
	std::string disk;
	if (!ReadWholeFile(Toml.path, disk)) return SetNotice("The file could not be read.", true);
	if (disk != Toml.doc.bytes) return SetNotice("The file changed since it was opened: click Reload first.", true);
	std::string out, why;
	int         changed = 0;
	if (!TomlEdit::Apply(Toml.doc, Toml.fields, out, why, changed)) return SetNotice(why, true);
	if (!changed) return SetNotice("Nothing to apply.");
	// the file as it was, beside it
	if (!WriteWholeFile(Toml.path + L".bak", disk)) return SetNotice("Could not write the backup (" + Narrow(FileName(Toml.path)) + ".bak), so nothing was saved.", true);
	if (!WriteWholeFile(Toml.path, out)) return SetNotice("Could not save the file.", true);
	Log(false, "%s: %d value(s) saved to %s", Toml.plugin.c_str(), changed, Narrow(Toml.path).c_str());
	const int   scroll = Toml.scroll;
	std::string reason;
	(void)LoadToml(Toml.path, reason);
	Toml.scroll = scroll; // clamped when shown
	SetNotice("Saved " + std::to_string(changed) + (changed == 1 ? " change" : " changes") + ". Restart the game to apply.");
}

// ---- game art (read on the game thread, made into sprites on the UI thread) ----
// The game's art comes from its files through D2RCore's archive open (export
// 160) and the game's file calls; the calls' first bytes are checked first, as
// Grail On! does.

namespace FileApi {
constexpr std::uint16_t OpenOrdinal = 160;
constexpr std::uint64_t Size = 0xA00050, Read = 0xA00EC0, Close = 0x9FFE50;
constexpr std::uint8_t  SizeBytes[] { 0x48, 0x8B, 0x01, 0x48, 0xFF, 0x60, 0x30 };
constexpr std::uint8_t  ReadBytes[] { 0x4C, 0x89, 0x44, 0x24, 0x18, 0x48, 0x89, 0x54, 0x24, 0x10 };
constexpr std::uint8_t  CloseBytes[] { 0xE9 };
} // namespace FileApi

using FileOpenFn  = void* (*)(const char* path) noexcept;
using FileSizeFn  = std::uint64_t (*)(void* file) noexcept;
using FileReadFn  = bool (*)(void* file, void* buffer, std::uint64_t size, std::uint64_t* read) noexcept;
using FileCloseFn = void (*)(void* file) noexcept;

std::string ArchiveFile(const char* path, std::string& trouble) {
	const auto base = static_cast<std::uintptr_t>(Context->exeBase);
	const auto same = [&](std::uint64_t rva, const std::uint8_t* bytes, std::size_t count) { return std::memcmp(reinterpret_cast<const void*>(base + rva), bytes, count) == 0; };
	HMODULE    core = GetModuleHandleW(L"D2RCore.dll");
	if (!base || !core || !same(FileApi::Size, FileApi::SizeBytes, sizeof(FileApi::SizeBytes)) || !same(FileApi::Read, FileApi::ReadBytes, sizeof(FileApi::ReadBytes))
	    || !same(FileApi::Close, FileApi::CloseBytes, sizeof(FileApi::CloseBytes))) {
		trouble = "the game's file calls are not where this build expects them";
		return {};
	}
	const auto open = reinterpret_cast<FileOpenFn>(reinterpret_cast<void*>(GetProcAddress(core, MAKEINTRESOURCEA(FileApi::OpenOrdinal))));
	if (!open) {
		trouble = "D2RCore's archive open is missing";
		return {};
	}
	void* file = open(path);
	if (!file) {
		trouble = "the game's files do not have it";
		return {};
	}
	std::string         bytes;
	const std::uint64_t size = reinterpret_cast<FileSizeFn>(base + FileApi::Size)(file);
	if (size > 0 && size < (64u << 20)) {
		bytes.resize(size);
		std::uint64_t read = 0;
		if (!reinterpret_cast<FileReadFn>(base + FileApi::Read)(file, bytes.data(), size, &read) || read != size) bytes.clear();
	}
	reinterpret_cast<FileCloseFn>(base + FileApi::Close)(file);
	if (bytes.empty()) trouble = "reading it failed";
	return bytes;
}

struct Picture {
	int                        width {}, height {};
	std::vector<unsigned char> rgba;
	unsigned char*             at(int x, int y) { return rgba.data() + (static_cast<std::size_t>(y) * width + x) * 4; }
	const unsigned char*       at(int x, int y) const { return rgba.data() + (static_cast<std::size_t>(y) * width + x) * 4; }
};

Picture Blank(int width, int height) { return { width, height, std::vector<unsigned char>(static_cast<std::size_t>(width) * height * 4) }; }

// The frames of a game sprite (SpA1 version 31, RGBA, frames side by side).
std::vector<Picture> Frames(const std::string& file, std::string& trouble) {
	if (file.size() < 0x28 || file.compare(0, 4, "SpA1")) {
		trouble = "not a sprite";
		return {};
	}
	const auto    u32 = [&](std::size_t at) { std::uint32_t v; std::memcpy(&v, file.data() + at, 4); return v; };
	std::uint16_t version = 0, frameWidth = 0;
	std::memcpy(&version, file.data() + 4, 2), std::memcpy(&frameWidth, file.data() + 6, 2);
	const std::uint32_t width = u32(8), height = u32(12), count = std::max<std::uint32_t>(1, u32(20));
	char what[160] {};
	std::snprintf(what, sizeof(what), "version %u, frame %ux%u, sheet %ux%u, %u frames, %zu bytes", version, frameWidth, height, width, height, count, file.size());
	trouble = what;
	if (version != 31 || !frameWidth || !height || width < frameWidth || width > 8192 || height > 4096 || file.size() < 0x28 + static_cast<std::size_t>(width) * height * 4) return {};
	std::vector<Picture> frames;
	for (std::uint32_t f = 0; f < count && (f + 1) * frameWidth <= width; ++f) {
		Picture frame = Blank(frameWidth, static_cast<int>(height));
		for (std::uint32_t y = 0; y < height; ++y)
			std::memcpy(frame.at(0, static_cast<int>(y)), file.data() + 0x28 + (static_cast<std::size_t>(y) * width + f * frameWidth) * 4, static_cast<std::size_t>(frameWidth) * 4);
		frames.push_back(std::move(frame));
	}
	return frames;
}

Picture FirstFrame(const std::string& file, std::string& trouble) {
	auto frames = Frames(file, trouble);
	return frames.empty() ? Picture {} : std::move(frames.front());
}

double Noise(std::uint32_t x, std::uint32_t y, std::uint32_t seed) {
	std::uint32_t h = x * 374761393u + y * 668265263u + seed * 2246822519u;
	h               = (h ^ (h >> 13)) * 1274126177u;
	return ((h ^ (h >> 16)) & 0xFF) / 255.0;
}

// The fine grain of the game's dark stone: single pixels and 3x3 blotches.
Picture Stone(int width, int height, const int color[3], int grain, std::uint32_t seed) {
	Picture out = Blank(width, height);
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x) {
			const double n = 0.6 * Noise(x, y, seed) + 0.4 * Noise(x / 3, y / 3, seed + 1) - 0.5;
			auto*        p = out.at(x, y);
			for (int c = 0; c < 3; ++c) p[c] = static_cast<unsigned char>(std::clamp(static_cast<int>(color[c] + n * grain), 0, 255));
			p[3] = 255;
		}
	return out;
}

// Bilinear scaling.
Picture Resize(const Picture& in, int width, int height) {
	Picture out = Blank(std::max(1, width), std::max(1, height));
	if (!in.width || !in.height) return out;
	for (int y = 0; y < out.height; ++y)
		for (int x = 0; x < out.width; ++x) {
			const double sx = std::clamp((x + 0.5) * in.width / out.width - 0.5, 0.0, in.width - 1.0), sy = std::clamp((y + 0.5) * in.height / out.height - 0.5, 0.0, in.height - 1.0);
			const int    x0 = static_cast<int>(sx), y0 = static_cast<int>(sy), x1 = std::min(x0 + 1, in.width - 1), y1 = std::min(y0 + 1, in.height - 1);
			const double fx = sx - x0, fy = sy - y0;
			for (int k = 0; k < 4; ++k) {
				const double top = in.at(x0, y0)[k] * (1 - fx) + in.at(x1, y0)[k] * fx, bottom = in.at(x0, y1)[k] * (1 - fx) + in.at(x1, y1)[k] * fx;
				out.at(x, y)[k] = static_cast<unsigned char>(std::lround(top * (1 - fy) + bottom * fy));
			}
		}
	return out;
}

Picture Half(const Picture& in) { return Resize(in, in.width / 2, in.height / 2); }

Picture Mirror(const Picture& in) {
	Picture out = Blank(in.width, in.height);
	for (int y = 0; y < in.height; ++y)
		for (int x = 0; x < in.width; ++x) std::memcpy(out.at(x, y), in.at(in.width - 1 - x, y), 4);
	return out;
}

void Paste(Picture& onto, const Picture& piece, int left, int top, int sourceLeft = 0, int width = -1) {
	if (width < 0) width = piece.width - sourceLeft;
	for (int y = 0; y < piece.height && top + y < onto.height; ++y)
		for (int x = 0; x < width && left + x < onto.width; ++x)
			if (left + x >= 0 && top + y >= 0) std::memcpy(onto.at(left + x, top + y), piece.at(sourceLeft + x, y), 4);
}

// A box of any width from the game's: its ends kept, its middle tiled. The
// right end is the left end's mirror when none is given.
Picture Join(const Picture& leftEnd, const Picture& middle, const Picture& rightEnd, int width, int height) {
	const auto scale = [&](const Picture& p) { return p.width ? Resize(p, std::max(1, p.width * height / std::max(1, p.height)), height) : Picture {}; };
	const Picture left = scale(leftEnd), right = rightEnd.width ? scale(rightEnd) : Mirror(left), mid = scale(middle);
	Picture       out = Blank(width, height);
	if (!left.width || !mid.width) return out;
	for (int x = left.width; x < width - right.width; x += mid.width) Paste(out, mid, x, 0, 0, std::min(mid.width, width - right.width - x));
	Paste(out, left, 0, 0, 0, std::min(left.width, width / 2));
	const int keep = std::min(right.width, width - width / 2);
	Paste(out, right, width - keep, 0, right.width - keep, keep);
	return out;
}

// A long piece of art fitted to another size: its ends kept, its middle stretched.
Picture Stretch(const Picture& in, int width, int height, int leftKeep, int rightKeep) {
	const Picture tall = Resize(in, in.width, height);
	Picture       out  = Blank(width, height);
	const int     l = std::min(leftKeep, width / 2), r = std::min(rightKeep, width - l);
	Paste(out, tall, 0, 0, 0, l);
	Paste(out, tall, width - r, 0, tall.width - r, r);
	Picture middle = Blank(std::max(1, tall.width - leftKeep - rightKeep), height);
	Paste(middle, tall, 0, 0, leftKeep, middle.width);
	Paste(out, Resize(middle, std::max(1, width - l - r), height), l, 0);
	return out;
}

std::string SpriteFile(const std::vector<Picture>& frames) {
	const int   width = frames.front().width, height = frames.front().height, count = static_cast<int>(frames.size());
	std::string out(40, '\0');
	const auto  put = [&](std::size_t at, std::uint32_t value, int bytes) {
        for (int i = 0; i < bytes; ++i) out[at + i] = static_cast<char>((value >> (8 * i)) & 0xFF);
	};
	std::memcpy(out.data(), "SpA1", 4);
	put(4, 31, 2);
	put(6, static_cast<std::uint32_t>(width), 2);
	put(8, static_cast<std::uint32_t>(width * count), 4);
	put(12, static_cast<std::uint32_t>(height), 4);
	put(20, static_cast<std::uint32_t>(count), 4);
	for (int y = 0; y < height; ++y)
		for (const auto& frame : frames) out.append(reinterpret_cast<const char*>(frame.at(0, y)), static_cast<std::size_t>(width) * 4);
	return out;
}

bool RegisterResource(const std::string& path, const std::string& bytes) {
	static std::vector<std::string> kept;
	kept.push_back(bytes);
	const D2RL::Resources::ResourceRegistration resource {
		.structSize = D2RL::Resources::ResourceRegistrationSize,
		.path       = path.c_str(),
		.bytes      = kept.back().data(),
		.byteCount  = kept.back().size(),
	};
	D2RL::Resources::RegistrationHandle handle = D2RL::Resources::InvalidHandle;
	return ResourceApi->registerResource(Context, &resource, &handle) == D2RL::Resources::Result::Success;
}

bool RegisterSprite(const std::string& name, const std::vector<Picture>& frames) {
	if (frames.empty() || !frames.front().width) return false;
	std::vector<Picture> half;
	for (const auto& f : frames) half.push_back(Half(f));
	const std::string path = "data/hd/global/ui/d2rloader/i-hate-tomls/" + name;
	return RegisterResource(path + ".sprite", SpriteFile(frames)) && RegisterResource(path + ".lowend.sprite", SpriteFile(half));
}

// The lobby's refresh button: its sprite file and frames.
struct NativeButton {
	std::string file;
	int         normal {}, hovered {}, pressed {};
};
NativeButton RefreshArt;

// Read on the game thread.
enum { ArtBackground, ArtRow, ArtBoxLeft, ArtBoxCenter, ArtClose, ArtBoxRight, ArtTileLeft, ArtTileCenter, ArtTileRight, ArtUp, ArtDown, ArtCount };
constexpr const char* ArtPaths[ArtCount] {
	"data/hd/global/ui/panel/character_sheet/character_background.sprite",
	"data/hd/global/ui/panel/loot_filter/slot_large_list.sprite",
	"data/hd/global/ui/panel/dropdownbutton_left.sprite",
	"data/hd/global/ui/panel/dropdownbutton_center.sprite",
	"data/hd/global/ui/panel/closebtn_4x.sprite",
	"data/hd/global/ui/panel/dropdownbutton_right.sprite",
	"data/hd/global/ui/panel/dropdowntile_left.sprite",
	"data/hd/global/ui/panel/dropdowntile_center.sprite",
	"data/hd/global/ui/panel/dropdowntile_right.sprite",
	"data/hd/global/ui/panel/loot_filter/slot_large_list_btn_1.sprite",
	"data/hd/global/ui/panel/loot_filter/slot_large_list_btn_2.sprite",
};
std::string ArtFiles[ArtCount];
bool        CoverReady {}, LineReady {}, BarReady {}, RowReady {}, BoxReady {}, ButtonReady {}, RemoveReady {}, IconsReady {}, GearReady {}, ScrollReady {}, BlankReady {}, DropReady {}, ArrowsReady {}, ResetReady {};

// The cover: the stone between two stat boxes, mirrored over the whole box
// area so no seam shows. Without the background, plain grained stone.
Picture MakeCover(const Picture& background) {
	if (background.width < CoverX + CoverWidth || background.height < CoverY + CoverHeight) {
		const int color[3] { 14, 15, 16 };
		return Stone(CoverWidth, CoverHeight, color, 12, 11);
	}
	Picture    out    = Blank(CoverWidth, CoverHeight);
	const auto mirror = [](int at, int size) {
		const int period = 2 * size, m = at % period;
		return m < size ? m : period - 1 - m;
	};
	for (int y = 0; y < CoverHeight; ++y)
		for (int x = 0; x < CoverWidth; ++x) {
			std::memcpy(out.at(x, y), background.at(PatchX + mirror(x, PatchWidth), PatchY + mirror(y, PatchHeight)), 4);
			out.at(x, y)[3] = 255;
		}
	return out;
}

// The header box's stone over the experience bar: the rows just above it,
// mirrored downwards.
Picture MakeBarCover(const Picture& background) {
	if (background.width < BarX + BarWidth || background.height < BarY + BarHeight) return {};
	Picture out = Blank(BarWidth, BarHeight);
	for (int y = 0; y < BarHeight; ++y)
		for (int x = 0; x < BarWidth; ++x) {
			std::memcpy(out.at(x, y), background.at(BarX + x, BarY - 1 - y), 4);
			out.at(x, y)[3] = 255;
		}
	return out;
}

// A thin gold rule under a section title.
Picture MakeLine() {
	const int width = ContentRight - ContentLeft, height = 4;
	Picture   out   = Blank(width, height);
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x) {
			const double fade = std::min(1.0, std::min(x, width - 1 - x) / 80.0);
			const int    tone[4][3] { { 60, 48, 24 }, { 150, 126, 70 }, { 120, 98, 47 }, { 40, 32, 16 } };
			auto*        p = out.at(x, y);
			for (int c = 0; c < 3; ++c) p[c] = static_cast<unsigned char>(tone[y][c]);
			p[3] = static_cast<unsigned char>(255 * fade);
		}
	return out;
}

// A dark box with a thin gold edge, when the game's art is missing.
Picture PlainBox(int width, int height, int edge) {
	const int color[3] { 20, 20, 22 };
	Picture   out = Stone(width, height, color, 10, 5);
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x)
			if (x < 2 || y < 2 || x >= width - 2 || y >= height - 2) {
				auto* p = out.at(x, y);
				p[0] = static_cast<unsigned char>(edge), p[1] = static_cast<unsigned char>(edge * 82 / 100), p[2] = static_cast<unsigned char>(edge * 45 / 100);
			}
	return out;
}

// A round gem: what kind of settings a plugin has.
Picture Gem(const int color[3]) {
	Picture      out = Blank(IconSize, IconSize);
	const double r = IconSize / 2.0 - 3, c = IconSize / 2.0;
	for (int y = 0; y < IconSize; ++y)
		for (int x = 0; x < IconSize; ++x) {
			const double dx = x + 0.5 - c, dy = y + 0.5 - c, d = std::sqrt(dx * dx + dy * dy);
			auto*        p = out.at(x, y);
			if (d > r + 2.5) continue;
			if (d > r) { // gold setting
				const double a = std::clamp(r + 2.5 - d, 0.0, 1.0);
				p[0] = 150, p[1] = 122, p[2] = 64, p[3] = static_cast<unsigned char>(255 * a);
				continue;
			}
			// lit from the top left
			const double light = std::clamp(1.0 - (d / r) * 0.55 + (-dx - dy) / (2.2 * r), 0.25, 1.35);
			const double spot  = std::exp(-((dx + r * 0.35) * (dx + r * 0.35) + (dy + r * 0.35) * (dy + r * 0.35)) / (r * r * 0.05));
			for (int k = 0; k < 3; ++k) p[k] = static_cast<unsigned char>(std::clamp(color[k] * light + 255 * 0.7 * spot, 0.0, 255.0));
			p[3] = 255;
		}
	return out;
}

// An empty slot of any size: the stash slot's corners and edges stretched
// around grained stone of its inside colour (as Grail On!'s buttons).
Picture Slot(int width, int height) {
	const int  s = SlotSize, c = SlotCorner;
	const auto src = [&](int x, int y) { return SlotPixels + (static_cast<std::size_t>(y) * s + x) * 4; };
	long       sum[3] {}, count = 0;
	for (int y = c; y < s - c; ++y)
		for (int x = c; x < s - c; ++x, ++count)
			for (int k = 0; k < 3; ++k) sum[k] += src(x, y)[k];
	const int inside[3] { static_cast<int>(sum[0] / count), static_cast<int>(sum[1] / count), static_cast<int>(sum[2] / count) };
	Picture   out = Stone(width, height, inside, 10, 7);
	const int cw = std::min(c, width / 2), ch = std::min(c, height / 2);
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x) {
			const bool left = x < cw, right = x >= width - cw, top = y < ch, bottom = y >= height - ch;
			if (!left && !right && !top && !bottom) continue;
			const int sx = left ? x : right ? s - (width - x) : s / 2;
			const int sy = top ? y : bottom ? s - (height - y) : s / 2;
			std::memcpy(out.at(x, y), src(sx, sy), 4);
		}
	return out;
}

// The square gear button (Grail On!'s Config menu button): a slot, lighter
// when hovered, darker when pressed, the cog in the game's gold.
std::vector<Picture> GearButton(int size) {
	Picture cog = Blank(CogSize, CogSize);
	for (int p = 0; p < CogSize * CogSize; ++p) cog.rgba[static_cast<std::size_t>(p) * 4 + 3] = CogMask[p];
	const int icon = size * 66 / 100, at = (size - icon) / 2;
	cog            = Resize(cog, icon, icon);
	std::vector<Picture> frames;
	const int            shift[3] { 0, 22, -8 };
	const int            gold[3][3] { { 215, 200, 150 }, { 255, 240, 190 }, { 180, 165, 120 } };
	for (int f = 0; f < 3; ++f) {
		Picture picture = Slot(size, size);
		for (int y = 0; y < size; ++y)
			for (int x = 0; x < size; ++x) {
				auto* p = picture.at(x, y);
				for (int k = 0; k < 3; ++k) p[k] = static_cast<unsigned char>(std::clamp(p[k] + shift[f], 0, 255));
			}
		for (int y = 0; y < icon; ++y)
			for (int x = 0; x < icon; ++x) {
				const double a = cog.at(x, y)[3] / 255.0;
				auto*        p = picture.at(at + x, at + y);
				for (int k = 0; k < 3; ++k) p[k] = static_cast<unsigned char>(std::lround(p[k] * (1 - a) + gold[f][k] * a));
			}
		frames.push_back(std::move(picture));
	}
	return frames;
}

// A sprite's frames cut to what they draw, scaled to fit a square (the
// game's sheets pad their art differently).
std::vector<Picture> Fitted(const std::vector<Picture>& frames, int size) {
	int left = 1 << 30, top = 1 << 30, right = -1, bottom = -1;
	for (const auto& frame : frames)
		for (int y = 0; y < frame.height; ++y)
			for (int x = 0; x < frame.width; ++x)
				if (frame.at(x, y)[3] > 24) left = std::min(left, x), right = std::max(right, x), top = std::min(top, y), bottom = std::max(bottom, y);
	std::vector<Picture> out;
	for (const auto& frame : frames) {
		Picture cut = frame;
		if (right >= left && right < frame.width && bottom < frame.height) {
			cut = Blank(right - left + 1, bottom - top + 1);
			for (int y = 0; y < cut.height; ++y) std::memcpy(cut.at(0, y), frame.at(left, top + y), static_cast<std::size_t>(cut.width) * 4);
		}
		const double scale = std::min(static_cast<double>(size) / std::max(1, cut.width), static_cast<double>(size) / std::max(1, cut.height));
		const Picture fit  = Resize(cut, std::max(1, static_cast<int>(cut.width * scale)), std::max(1, static_cast<int>(cut.height * scale)));
		Picture       box  = Blank(size, size);
		Paste(box, fit, (size - fit.width) / 2, (size - fit.height) / 2);
		out.push_back(std::move(box));
	}
	return out;
}

// The reset button: a slot with a gold circular arrow (frames 0 normal, 1
// hovered, 2 pressed).
std::vector<Picture> ResetButton(int size) {
	std::vector<Picture> frames;
	const int            shift[3] { 0, 22, -8 };
	const int            gold[3][3] { { 215, 200, 150 }, { 255, 240, 190 }, { 180, 165, 120 } };
	const double         c = size / 2.0, r = size * 0.27, w = size * 0.06;
	constexpr double     pi = 3.14159265358979;
	for (int f = 0; f < 3; ++f) {
		Picture picture = Slot(size, size);
		for (int y = 0; y < size; ++y)
			for (int x = 0; x < size; ++x) {
				auto*        p     = picture.at(x, y);
				const double dx    = x + 0.5 - c, dy = y + 0.5 - c, d = std::sqrt(dx * dx + dy * dy);
				const double angle = std::atan2(dy, dx); // 0 right, pi/2 down
				double       a     = 0;
				// the ring, open at the top right
				if (!(angle < -pi / 4 && angle > -pi / 2 - 0.5)) a = std::clamp(w + 0.5 - std::abs(d - r), 0.0, 1.0);
				// the arrow head at the gap's end, pointing clockwise
				const double hx = c + r * std::cos(-pi / 4), hy = c + r * std::sin(-pi / 4);
				const double ux = x + 0.5 - hx, uy = y + 0.5 - hy;
				// a triangle: along the tangent (down-right) and across it
				const double along = (ux * 0.7071 + uy * 0.7071), across = (ux * 0.7071 - uy * 0.7071);
				if (along > -w * 2.6 && along < w * 2.2 && std::abs(across) < (w * 2.2 - along) * 0.9) a = std::max(a, 1.0);
				for (int k = 0; k < 3; ++k) {
					const int base = std::clamp(p[k] + shift[f], 0, 255);
					p[k]           = static_cast<unsigned char>(std::lround(base * (1 - a) + gold[f][k] * a));
				}
			}
		frames.push_back(std::move(picture));
	}
	return frames;
}

// The scroll bar's thumb: a bronze bar `steps` steps tall.
Picture ScrollThumb(int steps) {
	const int height = steps * StepHeight;
	Picture   out    = Blank(ScrollWidth, height);
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < ScrollWidth; ++x) {
			const int edge = std::min({ x, y, ScrollWidth - 1 - x, height - 1 - y });
			const int lit  = edge < 3 ? (std::min(x, y) == edge ? 50 : -50) : static_cast<int>(18 * Noise(x, y, 11)) - 9;
			const int rgba[4] { 128 + lit, 102 + lit, 62 + lit, edge < 1 ? 255 : 235 };
			for (int k = 0; k < 4; ++k) out.at(x, y)[k] = static_cast<unsigned char>(std::clamp(rgba[k], 0, 255));
		}
	return out;
}

void __cdecl MakeArt(const D2RL::PluginContext*, void*) noexcept {
	std::string trouble;
	Picture     background = FirstFrame(ArtFiles[ArtBackground], trouble);
	Log(background.width == 0, "character screen background: %s%s", trouble.c_str(), background.width ? "" : " (cover drawn without it)");
	CoverReady = RegisterSprite("cover", { MakeCover(background) });
	if (const Picture bar = MakeBarCover(background); bar.width) BarReady = RegisterSprite("bar", { bar });
	LineReady = RegisterSprite("line", { MakeLine() });

	// the loot filter list's row, for the plugin list
	const Picture row = FirstFrame(ArtFiles[ArtRow], trouble);
	RowReady          = RegisterSprite("row", { row.width ? Stretch(row, ListRowWidth, ListRowBack, 24, 16) : PlainBox(ListRowWidth, ListRowBack, 90) });

	// text boxes and buttons: the drop-down box's frame, its end and middle
	const auto left = Frames(ArtFiles[ArtBoxLeft], trouble), middle = Frames(ArtFiles[ArtBoxCenter], trouble);
	const bool box  = left.size() >= 4 && middle.size() >= 4;
	BoxReady        = RegisterSprite("box", { box ? Join(left[0], middle[0], {}, TextBoxWidth, BoxHeight) : PlainBox(TextBoxWidth, BoxHeight, 60) })
	        && RegisterSprite("itembox", { box ? Join(left[0], middle[0], {}, ItemBoxWidth, BoxHeight) : PlainBox(ItemBoxWidth, BoxHeight, 60) });
	std::vector<Picture> button;
	for (std::size_t f = 0; f < 4; ++f) button.push_back(box ? Join(left[f], middle[f], {}, ButtonWidth, BoxHeight) : PlainBox(ButtonWidth, BoxHeight, f == 1 || f == 3 ? 170 : 90));
	ButtonReady = RegisterSprite("button", button);

	// the close button, smaller, to remove a list item
	auto close = Frames(ArtFiles[ArtClose], trouble);
	if (close.size() >= 4) {
		for (auto& f : close) f = Resize(f, 52, 52);
		RemoveReady = RegisterSprite("remove", close);
	}

	// the search bar's box, a joined plugin's text box
	(void)RegisterSprite("searchbox", { box ? Join(left[0], middle[0], {}, ListRowWidth, BoxHeight) : PlainBox(ListRowWidth, BoxHeight, 60) });
	(void)RegisterSprite("stringbox", { box ? Join(left[0], middle[0], {}, ContentRight - ControlLeft, BoxHeight) : PlainBox(ContentRight - ControlLeft, BoxHeight, 60) });

	GearReady = RegisterSprite("gear", GearButton(GearSize));

	// the drop-down: its box (frames 0 normal, 1 pressed, 2 disabled, 3
	// hovered) and its list's lines (0 normal, 1 hovered), as Grail On!'s
	{
		const auto right = Frames(ArtFiles[ArtBoxRight], trouble);
		std::vector<Picture> drop;
		for (std::size_t f = 0; f < 4; ++f)
			drop.push_back(box ? Join(left[std::min(f, left.size() - 1)], middle[std::min(f, middle.size() - 1)], right.empty() ? Picture {} : right[std::min(f, right.size() - 1)], DropWidth, BoxHeight)
			                   : PlainBox(DropWidth, BoxHeight, f == 3 ? 170 : 90));
		const auto tl = Frames(ArtFiles[ArtTileLeft], trouble), tm = Frames(ArtFiles[ArtTileCenter], trouble), tr = Frames(ArtFiles[ArtTileRight], trouble);
		std::vector<Picture> tile;
		for (std::size_t f = 0; f < 2; ++f)
			tile.push_back(!tl.empty() && !tm.empty() && !tr.empty() ? Join(tl[std::min(f, tl.size() - 1)], tm[std::min(f, tm.size() - 1)], tr[std::min(f, tr.size() - 1)], DropWidth, TileHeight)
			                                                         : PlainBox(DropWidth, TileHeight, f ? 170 : 70));
		DropReady = RegisterSprite("dropbox", drop) && RegisterSprite("droptile", tile);
	}
	// the loot filter list's up and down arrows (0 normal, 1 hovered, 2 pressed), cut to their art
	{
		const auto up = Frames(ArtFiles[ArtUp], trouble), down = Frames(ArtFiles[ArtDown], trouble);
		ArrowsReady = up.size() >= 3 && down.size() >= 3 && RegisterSprite("uparrow", Fitted(up, ArrowSize)) && RegisterSprite("downarrow", Fitted(down, ArrowSize));
		// the reset: the lobby's refresh button (as Grail On!), else the plugin's own
		ResetReady = false;
		if (!RefreshArt.file.empty()) {
			auto frames = Frames(RefreshArt.file, trouble);
			const int count = static_cast<int>(frames.size());
			for (auto& frame : frames) {
				const double scale = std::min(static_cast<double>(ArrowSize) / std::max(1, frame.width), static_cast<double>(ArrowSize) / std::max(1, frame.height));
				const Picture fit = Resize(frame, std::max(1, static_cast<int>(frame.width * scale)), std::max(1, static_cast<int>(frame.height * scale)));
				Picture box = Blank(ArrowSize, ArrowSize);
				Paste(box, fit, (ArrowSize - fit.width) / 2, (ArrowSize - fit.height) / 2);
				frame = std::move(box);
			}
			if (count > 0) {
				const int pick[3] { std::clamp(RefreshArt.normal, 0, count - 1), std::clamp(RefreshArt.hovered, 0, count - 1), std::clamp(RefreshArt.pressed, 0, count - 1) };
				// registered as frames 0 normal, 1 hovered, 2 pressed (the plugin's own order)
				ResetReady = RegisterSprite("reset", { frames[pick[0]], frames[pick[1]], frames[pick[2]] });
			}
		}
		if (!ResetReady) ResetReady = RegisterSprite("reset", ResetButton(ArrowSize));
	}
	// the scroll bar: a track per length, a thumb per height, invisible steps
	ScrollReady = true;
	for (int steps = 1; steps <= MaxSteps; ++steps)
		ScrollReady = RegisterSprite("track" + std::to_string(steps), { Slot(ScrollWidth, steps * StepHeight) }) && RegisterSprite("thumb" + std::to_string(steps), { ScrollThumb(steps) }) && ScrollReady;
	{
		Picture clear = Blank(4, 4), lit = Blank(4, 4);
		for (int p = 0; p < 16; ++p) std::memcpy(lit.rgba.data() + p * 4, "\xFF\xF0\xC8\x28", 4);
		BlankReady = RegisterSprite("blank", { clear, lit });
	}

	const int menu[3] { 60, 200, 70 }, window[3] { 70, 120, 235 }, file[3] { 200, 200, 190 };
	IconsReady = RegisterSprite("iconmenu", { Gem(menu) }) && RegisterSprite("iconwindow", { Gem(window) }) && RegisterSprite("icontoml", { Gem(file) });
	if (!CoverReady || !LineReady || !RowReady || !BoxReady || !ButtonReady) Log(true, "could not register all of the window's sprites");
	for (auto& f : ArtFiles) f.clear(), f.shrink_to_fit();
	RefreshArt.file.clear(), RefreshArt.file.shrink_to_fit();
}

// ---- layout ----

std::string Rect(int x, int y, int width = -1, int height = -1) {
	char text[96] {};
	if (width < 0) std::snprintf(text, sizeof(text), R"("rect": {"x": %d, "y": %d})", x, y);
	else std::snprintf(text, sizeof(text), R"("rect": {"x": %d, "y": %d, "width": %d, "height": %d})", x, y, width, height);
	return text;
}

std::string Command(const std::string& command) { return Json(std::string("PanelManager:ClosePanel:") + CommandPrefix + command); }

std::string Tooltip(const std::string& tooltip) {
	return R"(, "children": [{"type": "FocusableWidget", "name": "Tooltip", "fields": {"fitToParent": true, "tooltipString": )" + Json(tooltip)
	     + R"(, "tooltipStyle": {"fontStyle": {"options": {"newlineHandling": "standard"}}}}}])";
}

std::string Text(const std::string& name, int x, int y, int width, int height, const std::string& text, const char* style, const std::string& tooltip = {}) {
	std::string widget = R"(, {"type": "TextBoxWidget", "name": )" + Json(name) + R"(, "fields": {)" + Rect(x, y, width, height) + R"(, "text": )" + Json(text) + R"(, "style": )" + style + "}";
	if (!tooltip.empty()) widget += Tooltip(tooltip);
	return widget + "}";
}

std::string Image(const std::string& name, int x, int y, const std::string& file) {
	return R"(, {"type": "ImageWidget", "name": )" + Json(name) + R"(, "fields": {)" + Rect(x, y) + R"(, "filename": "d2rloader\\i-hate-tomls\\)" + file + R"("}})";
}

// `file` is written into the JSON as is (backslashes doubled).
std::string Button(const std::string& name, int x, int y, const char* file, int normal, int pressed, int hovered, const std::string& command, const std::string& tooltip = {},
                   const char* sound = "cursor_button_click_hd") {
	char head[512] {};
	std::snprintf(head, sizeof(head), R"(, {"type": "ButtonWidget", "name": "%s", "fields": {"rect": {"x": %d, "y": %d}, "filename": "%s", "normalFrame": %d, "pressedFrame": %d, "hoveredFrame": %d, "sound": "%s")",
	              name.c_str(), x, y, file, normal, pressed, hovered, sound);
	std::string widget = head;
	if (!tooltip.empty()) widget += R"(, "tooltipString": )" + Json(tooltip);
	return widget + R"(, "onClickMessage": )" + Command(command) + "}}";
}

// A button with its text on it.
std::string TextButton(const std::string& name, int x, int y, const std::string& text, const std::string& command, const std::string& tooltip = {}) {
	std::string out = R"(, {"type": "ButtonWidget", "name": )" + Json(name) + R"(, "fields": {)" + Rect(x, y, ButtonWidth, BoxHeight)
	                + R"(, "filename": "d2rloader\\i-hate-tomls\\button", "normalFrame": 0, "hoveredFrame": 3, "pressedFrame": 1, "textString": )" + Json(text)
	                + R"(, "textColor": "$FontColorLightGold", "text/style": {"pointSize": "$MediumFontSize", "dropShadow": "$DefaultDropShadow", "spacing": "$ReducedSpacing", "alignment": {"h": "center", "v": "center"}})";
	if (!tooltip.empty()) out += R"(, "tooltipString": )" + Json(tooltip);
	return out + R"(, "sound": "cursor_button_click_hd", "onClickMessage": )" + Command(command) + "}}";
}

// An invisible button: `command` on a click, a faint light when hovered.
std::string HitButton(const std::string& name, int x, int y, int width, int height, const std::string& command, const std::string& tooltip = {}) {
	std::string out = R"(, {"type": "ButtonWidget", "name": )" + Json(name) + R"(, "fields": {)" + Rect(x, y, width, height)
	                + R"(, "filename": "d2rloader\\i-hate-tomls\\blank", "normalFrame": 0, "hoveredFrame": 1, "pressedFrame": 1)";
	if (!tooltip.empty()) out += R"(, "tooltipString": )" + Json(tooltip);
	return out + R"(, "sound": "cursor_menu_select_hd", "onClickMessage": )" + Command(command) + "}}";
}

// What fits in a box `width` wide: the start of a long text, or its end
// while typing (where the caret is).
std::string Fit(const std::string& text, int width, bool end) {
	std::vector<std::size_t> starts; // each character's first byte
	for (std::size_t i = 0; i < text.size(); ++i)
		if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) starts.push_back(i);
	const std::size_t room = static_cast<std::size_t>(std::max(4, width / CharWidth));
	if (starts.size() <= room) return text;
	if (end) return "\u2026" + text.substr(starts[starts.size() - room + 1]);
	return text.substr(0, starts[room - 1]) + "\u2026";
}

// A text box: its frame, the text (a caret while typing, a grey hint while
// empty) and a button over it that starts typing.
std::string TypedBox(const std::string& name, const std::string& key, int x, int y, int width, const char* frame, const std::string& text, const std::string& hint,
                     const std::string& tooltip = {}) {
	std::string out;
	if (BoxReady) out += Image(name + "Frame", x, y, frame);
	else out += R"(, {"type": "RectangleWidget", "name": )" + Json(name + "Frame") + R"(, "fields": {)" + Rect(x, y, width, BoxHeight) + R"(, "color": [0.0, 0.0, 0.0, 0.55]}})";
	const bool        typing = Typing && TypingKey == key;
	const bool        empty  = text.empty() && !typing;
	const std::string shown  = empty ? hint : Fit(text, width - 40, typing) + (typing ? "|" : "");
	out += R"(, {"type": "TextBoxWidget", "name": )" + Json(name) + R"(, "fields": {)" + Rect(x + 20, y, width - 40, BoxHeight) + R"(, "text": )" + Json(shown)
	     + R"(, "style": {"fontColor": ")" + (empty ? "$FontColorLightGray" : "$FontColorWhite")
	     + R"(", "pointSize": "$MediumFontSize", "dropShadow": "$DefaultDropShadow", "spacing": "$ReducedSpacing", "alignment": {"h": "left", "v": "center"}}}})";
	return out + HitButton(name + "Hit", x, y, width, BoxHeight, "type" + key, tooltip);
}

// Game styles: the options screen's text, the panel title's colour.
constexpr const char* LabelStyle =
    R"({"fontColor": "$FontColorLightGold", "pointSize": "$MediumFontSize", "spacing": "$ReducedSpacing", "dropShadow": "$DefaultDropShadow", "alignment": {"h": "left", "v": "center"}, "options": {"lineWrap": true}})";
constexpr const char* SmallLabelStyle =
    R"({"fontColor": "$FontColorLightGold", "pointSize": "$SmallFontSize", "spacing": "$ReducedSpacing", "dropShadow": "$DefaultDropShadow", "alignment": {"h": "left", "v": "center"}, "options": {"lineWrap": true}})";
constexpr const char* ValueStyle =
    R"({"fontColor": "$FontColorWhite", "pointSize": "$MediumFontSize", "spacing": "$ReducedSpacing", "dropShadow": "$DefaultDropShadow", "alignment": {"h": "center", "v": "center"}})";
constexpr const char* SmallValueStyle =
    R"({"fontColor": "$FontColorWhite", "pointSize": "$SmallFontSize", "spacing": "$ReducedSpacing", "dropShadow": "$DefaultDropShadow", "alignment": {"h": "center", "v": "center"}, "options": {"lineWrap": true}})";
constexpr const char* AxisStyle =
    R"({"fontColor": "$FontColorGoldYellow", "pointSize": "$MediumFontSize", "dropShadow": "$DefaultDropShadow", "alignment": {"h": "center", "v": "center"}})";
constexpr const char* SectionStyle =
    R"({"fontColor": "$FontColorGoldYellow", "pointSize": "$MediumLargeFontSize", "dropShadow": "$DefaultDropShadow", "alignment": {"h": "left", "v": "center"}})";
constexpr const char* NameStyle =
    R"({"fontColor": "$FontColorWhite", "pointSize": "$LargeFontSize", "dropShadow": "$DefaultDropShadow", "alignment": {"h": "center", "v": "center"}})";
constexpr const char* NoteStyle =
    R"({"fontColor": "$FontColorLightGold", "pointSize": "$SmallFontSize", "dropShadow": "$DefaultDropShadow", "alignment": {"h": "center", "v": "center"}, "options": {"lineWrap": true}})";
constexpr const char* WarnStyle =
    R"({"fontColor": "$FontColorLightRed", "pointSize": "$SmallFontSize", "dropShadow": "$DefaultDropShadow", "alignment": {"h": "center", "v": "center"}, "options": {"lineWrap": true}})";
constexpr const char* CommentStyle =
    R"({"fontColor": "$FontColorWhite", "pointSize": "$SmallFontSize", "dropShadow": "$DefaultDropShadow", "alignment": {"h": "left", "v": "top"}, "options": {"lineWrap": true, "newlineHandling": "standard"}})";
constexpr const char* SmallLeftStyle =
    R"({"fontColor": "$FontColorLightGold", "pointSize": "$SmallFontSize", "dropShadow": "$DefaultDropShadow", "alignment": {"h": "left", "v": "center"}})";
constexpr const char* SmallRightStyle =
    R"({"fontColor": "$FontColorLightGold", "pointSize": "$SmallFontSize", "dropShadow": "$DefaultDropShadow", "alignment": {"h": "right", "v": "center"}})";
constexpr const char* PluginNameStyle =
    R"({"fontColor": "$FontColorLightGold", "pointSize": "$MediumFontSize", "spacing": "$ReducedSpacing", "dropShadow": "$DefaultDropShadow", "alignment": {"h": "left", "v": "center"}})";

constexpr const char* LeftArrow  = "PauseMenu\\\\LeftArrow";
constexpr const char* RightArrow = "PauseMenu\\\\RightArrow";
constexpr const char* CheckBox   = "PANEL\\\\loot_filter\\\\lootfilter_large_list_btn_checkbox";
constexpr const char* OpenArrow  = "PANEL\\\\Character_Sheet\\\\advancedstatsbutton";
constexpr const char* AddButton  = "PANEL\\\\loot_filter\\\\chronicle_btn_add";
constexpr const char* RemoveFile = "d2rloader\\\\i-hate-tomls\\\\remove";
constexpr const char* GearFile   = "d2rloader\\\\i-hate-tomls\\\\gear";

// Check box frames: 0-3 empty, 4-7 ticked (normal, pressed, disabled, hovered).
std::string Check(const std::string& name, int x, int y, bool on, const std::string& command) { return Button(name, x, y, CheckBox, on ? 4 : 0, on ? 5 : 1, on ? 7 : 3, command); }

// The scroll bar beside content `total` pixels tall shown in `room` pixels
// from `top`, while it doesn't all fit: a recessed track, the thumb (as tall
// as the share shown), and one invisible button per step. Also tells the
// mouse where it is.
std::string ScrollBar(int top, int room, int total, int scroll) {
	const int steps = std::clamp(room / StepHeight, 2, MaxSteps);
	const int max   = std::max(0, total - room);
	BarTop = top, BarSteps = steps, BarMax = max;
	if (max <= 0) return {};
	const int   thumb = std::clamp((steps * room + total / 2) / total, 1, steps - 1);
	const int   y     = top + (steps - thumb) * StepHeight * std::clamp(scroll, 0, max) / max;
	std::string out;
	if (ScrollReady) {
		out += Image("Track", BarLeft, top, "track" + std::to_string(steps));
		out += Image("Thumb", BarLeft, y, "thumb" + std::to_string(thumb));
	} else {
		out += R"(, {"type": "RectangleWidget", "name": "Track", "fields": {)" + Rect(BarLeft, top, ScrollWidth, steps * StepHeight) + R"(, "color": [0.0, 0.0, 0.0, 0.6]}})";
		out += R"(, {"type": "RectangleWidget", "name": "Thumb", "fields": {)" + Rect(BarLeft + 4, y, ScrollWidth - 8, thumb * StepHeight) + R"(, "color": [0.5, 0.4, 0.24, 1.0]}})";
	}
	for (int i = 0; i < steps; ++i) out += HitButton("Step" + std::to_string(i), BarLeft, top + i * StepHeight, ScrollWidth, StepHeight, "step" + std::to_string(i));
	return out;
}

// The rows' area: the game's ClipViewWidget cuts what it holds at its edges
// (drawing and clicks), so rows half scrolled out show their visible part.
// Its children's rects are relative to it: it starts at x 0, y `top`.
std::string Clip(int top, int room, const std::string& children) {
	if (children.empty()) return {};
	std::string body = children.substr(children[0] == ',' ? 1 : 0);
	return R"(, {"type": "ClipViewWidget", "name": "Rows", "fields": {)" + Rect(0, top, BarLeft - 6, room) + R"(, "clipMouseInput": true}, "children": [)" + body + "]}";
}

// The header box: a back arrow to the list, the plugin's name and a line under it.
std::string Header(const std::string& name, const std::string& under) {
	std::string out = Button("Back", ContentLeft, HeaderY + 22, LeftArrow, 0, 1, 3, "back", "All plugins");
	out += Text("PluginName", ContentLeft + 60, HeaderY, ContentRight - ContentLeft - 120, 90, name, NameStyle);
	if (!under.empty()) out += Text("PluginUnder", ContentLeft + 60, HeaderY + 78, ContentRight - ContentLeft - 120, 36, under, NoteStyle);
	return out;
}

// The open drop-down: its setting, and where its list goes (drawn last, over the rest).
int OpenDrop = -1;
struct DropList {
	int                      x {}, y {}, current {};
	std::vector<std::string> names;
} Drop;

// A row's height on a joined plugin's page: a text row as its lines, a
// setting with a note taller by the note's lines.
int RowHeightOf(const Setting& setting) {
	if (setting.kind == Kind::Text) return Lines(setting.label) * NoteLine + 12;
	return RowHeight + (setting.note.empty() ? 0 : Lines(setting.note) * NoteLine);
}

// The two numbers of a position ("[x, y]").
std::pair<double, double> PositionOf(const std::string& value) {
	const auto items = ArrayItems(value);
	return { items.size() > 0 ? Number(items[0], 0) : 0, items.size() > 1 ? Number(items[1], 0) : 0 };
}

std::string StepTip(const Setting& setting) {
	return "Click: " + Decimal(setting.step, setting.decimals) + ", Shift+click: " + Decimal(setting.shiftStep, setting.decimals) + ", Ctrl+click: " + Decimal(setting.ctrlStep, setting.decimals);
}

// A small up or down arrow button (the loot filter list's), or the options arrows.
std::string ArrowButton(const std::string& name, int x, int y, bool up, const std::string& command, const std::string& tooltip) {
	if (ArrowsReady) return Button(name, x, y, up ? "d2rloader\\\\i-hate-tomls\\\\uparrow" : "d2rloader\\\\i-hate-tomls\\\\downarrow", 0, 2, 1, command, tooltip);
	return Button(name, x, y + 4, up ? RightArrow : LeftArrow, 0, 1, 3, command, tooltip);
}

std::string RowWidgets(const Client& client, const Setting& setting, int row, int y) {
	const std::string id = std::to_string(row);
	std::string       out;
	if (setting.kind == Kind::Header) {
		out += Text("Section" + id, ContentLeft, y + 10, ContentRight - ContentLeft, 70, setting.label, SectionStyle, setting.hint);
		if (LineReady) out += Image("Line" + id, ContentLeft, y + 82, "line");
		return out;
	}
	if (setting.kind == Kind::Text) return Text("Text" + id, ContentLeft, y + 4, ContentRight - ContentLeft, RowHeightOf(setting) - 8, setting.label, CommentStyle, setting.hint);
	const std::string label = setting.label + (setting.restart ? " *" : "");
	// a position's controls start further left: its label has less room
	const int labelWidth = (setting.kind == Kind::Position ? PositionLeft : ControlLeft) - ContentLeft - 20;
	out += Text("Label" + id, ContentLeft, y, labelWidth, RowHeight - 6, label, static_cast<int>(label.size()) * LabelChar > labelWidth ? SmallLabelStyle : LabelStyle, setting.hint);
	if (!setting.note.empty()) out += Text("Note" + id, ContentLeft, y + RowHeight - 10, ContentRight - ContentLeft, Lines(setting.note) * NoteLine, setting.note, CommentStyle);
	switch (setting.kind) {
	case Kind::Bool: out += Check("Check" + id, ContentRight - 70, y + 14, Unquote(GetValue(client, setting)) == "true", "T" + id); break;
	case Kind::Int:
	case Kind::Float: {
		const std::string tip = StepTip(setting);
		out += Button("Left" + id, ControlLeft, y + 24, LeftArrow, 0, 1, 3, "L" + id, tip);
		const std::string value = ShownValue(client, setting);
		const int         width = ContentRight - ControlLeft - 120;
		out += Text("Value" + id, ControlLeft + 60, y, width, RowHeight - 6, value, static_cast<int>(value.size()) * LabelChar > width ? SmallValueStyle : ValueStyle, tip);
		out += Button("Right" + id, ContentRight - 50, y + 24, RightArrow, 0, 1, 3, "R" + id, tip);
		break;
	}
	case Kind::Enum: {
		// a drop-down (Grail On!'s): the choice shown, its list while open
		const int         current = EnumIndex(setting, GetValue(client, setting));
		const std::string shown   = EnumLabel(setting, current);
		const int         boxY    = y + (RowHeight - BoxHeight) / 2 - 3;
		if (DropReady)
			out += R"(, {"type": "ButtonWidget", "name": "Drop)" + id + R"(", "fields": {)" + Rect(ControlLeft, boxY, DropWidth, BoxHeight)
			     + R"(, "filename": "d2rloader\\i-hate-tomls\\dropbox", "normalFrame": 0, "pressedFrame": 1, "disabledFrame": 2, "hoveredFrame": 3, "textString": )" + Json(shown)
			     + R"(, "textColor": "$FontColorLightGold", "text/style": {"pointSize": ")" + (static_cast<int>(shown.size()) * LabelChar > DropWidth - 110 ? "$SmallFontSize" : "$MediumFontSize")
			     + R"(", "dropShadow": "$DefaultDropShadow", "spacing": "$ReducedSpacing", "alignment": {"h": "center", "v": "center"}}, "sound": "cursor_button_click_hd", "onClickMessage": )" + Command("D" + id) + "}}";
		else {
			out += Button("Left" + id, ControlLeft, y + 24, LeftArrow, 0, 1, 3, "D" + id);
			out += Text("Value" + id, ControlLeft + 60, y, DropWidth - 60, RowHeight - 6, shown, ValueStyle);
		}
		if (OpenDrop == row) {
			Drop.x = ControlLeft, Drop.y = RowsTop + boxY + BoxHeight, Drop.current = current;
			Drop.names.clear();
			for (int k = 0; k < static_cast<int>(setting.values.size()); ++k) Drop.names.push_back(EnumLabel(setting, k));
			// no room below: the list opens upwards
			if (Drop.y + static_cast<int>(Drop.names.size()) * TileHeight > FooterY + 60) Drop.y = RowsTop + boxY - static_cast<int>(Drop.names.size()) * TileHeight;
		}
		break;
	}
	case Kind::Position: {
		// X and Y, each with a step down and up, and a reset (Grail On!'s button position)
		const auto [px, py]   = PositionOf(GetValue(client, setting));
		const std::string tip = StepTip(setting);
		int               at  = PositionLeft;
		const int         top = y + (RowHeight - ArrowSize) / 2 - 3;
		for (const auto& [axis, value] : { std::pair { "X", px }, std::pair { "Y", py } }) {
			const std::string a = axis;
			out += Text("Axis" + a + id, at, y, 30, RowHeight - 6, a, AxisStyle);
			at += 32;
			out += ArrowButton("Down" + a + id, at, top, false, (a == "X" ? "H" : "K") + id, tip);
			at += ArrowSize;
			out += Text("Pos" + a + id, at, y, 96, RowHeight - 6, Decimal(value, setting.decimals), static_cast<int>(Decimal(value, setting.decimals).size()) > 5 ? SmallValueStyle : ValueStyle, tip);
			at += 96;
			out += ArrowButton("Up" + a + id, at, top, true, (a == "X" ? "J" : "M") + id, tip);
			at += ArrowSize + 14;
		}
		if (!setting.defaultValue.empty()) {
			if (ResetReady) out += Button("Reset" + id, ContentRight - ArrowSize, top, "d2rloader\\\\i-hate-tomls\\\\reset", 0, 2, 1, "Z" + id, "Back to " + setting.defaultValue);
			else out += TextButton("Reset" + id, ContentRight - ButtonWidth, top, "Reset", "Z" + id, "Back to " + setting.defaultValue);
		}
		break;
	}
	case Kind::String: {
		const std::string key  = "s" + id;
		const std::string text = Typing && TypingKey == key ? ClientDraft : ShownValue(client, setting);
		out += TypedBox("Input" + id, key, ControlLeft, y + 20, ContentRight - ControlLeft, "stringbox", text, "(empty)", "Click to type; Enter saves, Esc cancels");
		break;
	}
	case Kind::Action: {
		const bool armed = ConfirmRow == row && GetTickCount64() < ConfirmUntil;
		out += Text("Run" + id, ControlLeft, y, ContentRight - ControlLeft - 60, RowHeight - 6, armed ? "Click again to confirm" : "", armed ? WarnStyle : ValueStyle);
		out += Button("Do" + id, ContentRight - 50, y + 24, RightArrow, 0, 1, 3, "A" + id, setting.confirm ? "Click twice to run" : "Run");
		break;
	}
	default: break;
	}
	return out;
}

std::string ModeTip(Mode mode) {
	switch (mode) {
	case Mode::Menu: return "Settings in this menu";
	case Mode::Window: return "Has its own settings window";
	case Mode::Toml: return "Settings from its TOML file";
	default: return {};
	}
}

const char* ModeIcon(Mode mode) { return mode == Mode::Menu ? "iconmenu" : mode == Mode::Window ? "iconwindow" : "icontoml"; }

// Lower case, for the search.
std::wstring Lower(const std::string& text) {
	std::wstring wide = Wide(text);
	for (auto& c : wide) c = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(c)));
#ifdef _WIN32
	if (!wide.empty()) CharLowerBuffW(wide.data(), static_cast<DWORD>(wide.size()));
#endif
	return wide;
}

// Whether a plugin has the searched text in its name, id or author.
bool Matches(const Entry& e, const std::wstring& query) {
	if (query.empty()) return true;
	return Lower(e.name).find(query) != std::wstring::npos || Lower(e.id).find(query) != std::wstring::npos || Lower(e.author).find(query) != std::wstring::npos;
}

std::wstring SearchQuery() {
	std::wstring query = Lower(SearchText);
	while (!query.empty() && query.back() == L' ') query.pop_back();
	while (!query.empty() && query.front() == L' ') query.erase(query.begin());
	return query;
}

std::string ListLayout() {
	const auto         list  = Entries();
	const std::wstring query = SearchQuery();
	std::vector<int>   shown; // indexes in `list` of the plugins the search keeps
	for (int i = 0; i < static_cast<int>(list.size()); ++i)
		if (Matches(list[static_cast<std::size_t>(i)], query)) shown.push_back(i);
	std::string out = Text("Count", ContentLeft, HeaderY, ContentRight - ContentLeft, 90, std::to_string(list.size()) + (list.size() == 1 ? " plugin running" : " plugins running"), NameStyle);

	// the search bar: a clear button and the count found while something is typed
	const int barWidth = ContentRight - ContentLeft;
	out += TypedBox("Search", "search", ContentLeft, SearchY, barWidth, "searchbox", SearchText, "Search", "Type part of a plugin's name");
	if (!SearchText.empty()) {
		out += Text("Found", ContentRight - 330, SearchY, 240, BoxHeight, std::to_string(shown.size()) + " found", SmallRightStyle);
		if (RemoveReady) out += Button("SearchClear", ContentRight - 60, SearchY + 2, RemoveFile, 0, 1, 3, "searchclear", "Clear the search");
		else out += TextButton("SearchClear", ContentRight - 60, SearchY, "x", "searchclear", "Clear the search");
	}

	const int room  = ViewBottom - ListTop;
	const int total = static_cast<int>(shown.size()) * ListRowHeight;
	ListScroll      = std::clamp(ListScroll, 0, std::max(0, total - room));
	std::string rows;
	if (list.empty()) out += Text("Empty", ContentLeft, 520, ContentRight - ContentLeft, 300, "No other plugin was found.", NoteStyle);
	else if (shown.empty()) out += Text("Empty", ContentLeft, 520, ContentRight - ContentLeft, 200, "No plugin matches the search.", NoteStyle);
	for (int at = ListScroll / ListRowHeight; at < static_cast<int>(shown.size()); ++at) {
		const int y = at * ListRowHeight - ListScroll; // in the clip
		if (y >= room) break;
		const int         index = shown[static_cast<std::size_t>(at)];
		const auto&       e     = list[static_cast<std::size_t>(index)];
		const std::string id    = std::to_string(at);
		std::string&      out   = rows;
		if (RowReady) out += Image("RowBack" + id, ContentLeft, y, "row");
		if (e.mode != Mode::None && IconsReady) {
			out += Image("Icon" + id, ContentLeft + 34, y + (ListRowBack - IconSize) / 2, ModeIcon(e.mode));
			out += Text("IconTip" + id, ContentLeft + 34, y + (ListRowBack - IconSize) / 2, IconSize, IconSize, "", NoteStyle, ModeTip(e.mode));
		}
		std::string under = e.version;
		if (!e.author.empty()) under += (under.empty() ? "by " : "  by ") + e.author;
		out += Text("Name" + id, ContentLeft + 92, y + 4, 700, 40, e.name, PluginNameStyle, e.id);
		out += Text("Under" + id, ContentLeft + 92, y + 40, 700, 30, under, SmallLeftStyle);
		const std::string open = "O" + std::to_string(index), tip = e.mode == Mode::Window ? "Open its settings window" : "Open its settings";
		if (e.mode == Mode::None) out += Text("NoSettings" + id, ContentRight - 260, y, 230, ListRowBack, "no settings", NoteStyle);
		else if (GearReady) out += Button("Open" + id, ContentRight - GearSize - 10, y + (ListRowBack - GearSize) / 2, GearFile, 0, 2, 1, open, tip);
		else out += Button("Open" + id, ContentRight - 76, y + (ListRowBack - 68) / 2, OpenArrow, 0, 1, 3, open, tip);
	}
	out += Clip(ListTop, room, rows);
	out += ScrollBar(ListTop, room, total, ListScroll);
	// the icons' meaning
	if (IconsReady) {
		const Mode  modes[] { Mode::Menu, Mode::Window, Mode::Toml };
		const char* names[] { "this menu", "own window", "TOML file" };
		for (int k = 0; k < 3; ++k) {
			const int x = ContentLeft + 120 + k * 260;
			out += Image("Legend" + std::to_string(k), x, NoteY + 8, ModeIcon(modes[k]));
			out += Text("LegendText" + std::to_string(k), x + IconSize + 10, NoteY + 4, 200, 48, names[k], SmallLeftStyle);
		}
	}
	return out;
}

std::string ClientLayout() {
	Shown                = std::clamp(Shown, 0, static_cast<int>(Clients.size()) - 1);
	const Client& client = Clients[static_cast<std::size_t>(Shown)];
	const int     count  = static_cast<int>(client.settings.size());
	int           total  = 0;
	for (const auto& setting : client.settings) total += RowHeightOf(setting);
	ClientScroll = std::clamp(ClientScroll, 0, std::max(0, total - ViewHeight));
	Drop         = {};
	std::string out      = Header(client.title, {});
	bool        restart  = false;
	for (const auto& setting : client.settings) restart = restart || setting.restart;
	std::string rows;
	for (int index = 0, at = -ClientScroll; index < count && at < ViewHeight; at += RowHeightOf(client.settings[static_cast<std::size_t>(index)]), ++index)
		if (at + RowHeightOf(client.settings[static_cast<std::size_t>(index)]) > 0) rows += RowWidgets(client, client.settings[static_cast<std::size_t>(index)], index, at);
	out += Clip(RowsTop, ViewHeight, rows);
	if (client.settings.empty()) out += Text("NoRows", ContentLeft, 520, ContentRight - ContentLeft, 200, "This plugin has no settings to show.", NoteStyle);
	out += ScrollBar(RowsTop, ViewHeight, total, ClientScroll);
	std::string note = Notice;
	if (note.empty() && restart) note = "* applies after a restart";
	if (!note.empty()) out += Text("Note", ContentLeft, NoteY, ContentRight - ContentLeft, 56, note, NoticeWarn ? WarnStyle : NoteStyle);
	// the open drop-down's list, over everything: a click anywhere else closes it
	if (!Drop.names.empty()) {
		out += HitButton("DropCatch", 0, 0, PanelWidth, PanelHeight, "dropx");
		for (int k = 0; k < static_cast<int>(Drop.names.size()); ++k) {
			const std::string& name = Drop.names[static_cast<std::size_t>(k)];
			out += R"(, {"type": "ButtonWidget", "name": "DropItem)" + std::to_string(k) + R"(", "fields": {)" + Rect(Drop.x, Drop.y + k * TileHeight, DropWidth, TileHeight)
			     + R"(, "filename": "d2rloader\\i-hate-tomls\\droptile", "normalFrame": 0, "pressedFrame": 1, "hoveredFrame": 1, "textString": )" + Json(name) + R"(, "textColor": ")"
			     + (k == Drop.current ? "$FontColorGoldYellow" : "$FontColorWhite") + R"(", "text/style": {"pointSize": ")" + (static_cast<int>(name.size()) * LabelChar > DropWidth - 40 ? "$SmallFontSize" : "$MediumFontSize")
			     + R"(", "dropShadow": "$DefaultDropShadow", "spacing": "$ReducedSpacing", "alignment": {"h": "center", "v": "center"}}, "sound": "cursor_button_click_hd", "onClickMessage": )"
			     + Command("P" + std::to_string(OpenDrop) + ":" + std::to_string(k)) + "}}";
		}
	}
	return out;
}

int TomlTotal() {
	int total = 0;
	for (const auto& row : Toml.rows) total += row.height;
	return total;
}

std::string TomlLayout() {
	std::string out = Header(Toml.name, Narrow(FileName(Toml.path)));
	if (!Toml.doc.error.empty() || Toml.rows.empty()) {
		const std::string why = Toml.doc.error.empty() ? "This file is empty." : "This file could not be read as TOML:\n" + Toml.doc.error + "\nEdit it in a text editor.";
		out += Text("Error", ContentLeft, 480, ContentRight - ContentLeft, 300, why, Toml.doc.error.empty() ? NoteStyle : WarnStyle);
		out += TextButton("Reload", ContentLeft + 20, FooterY, "Reload", "reload", "Read the file again");
		BarMax = 0;
		return out;
	}
	const int total = TomlTotal();
	Toml.scroll     = std::clamp(Toml.scroll, 0, std::max(0, total - ViewHeight));
	std::string  rows;
	std::string& head = out;
	int          at   = -Toml.scroll; // the row's top in the clip
	for (int r = 0; r < static_cast<int>(Toml.rows.size()); at += Toml.rows[static_cast<std::size_t>(r)].height, ++r) {
		const auto&       row = Toml.rows[static_cast<std::size_t>(r)];
		const std::string id  = std::to_string(r);
		if (at + row.height <= 0) continue;
		if (at >= ViewHeight) break;
		std::string& out = rows;
		int          y   = at;
		switch (row.type) {
		case TomlRow::Type::Note: out += Text("Comment" + id, ContentLeft, y + 4, ContentRight - ContentLeft, row.height - 8, row.text, CommentStyle); break;
		case TomlRow::Type::Section:
			out += Text("Section" + id, ContentLeft, y + 10, ContentRight - ContentLeft, 70, row.text, SectionStyle);
			if (LineReady) out += Image("Line" + id, ContentLeft, y + 82, "line");
			break;
		case TomlRow::Type::Field: {
			const auto& f    = Toml.fields[static_cast<std::size_t>(row.field)];
			const auto& item = Toml.doc.items[static_cast<std::size_t>(f.item)];
			std::string tip  = f.key + "\nType: " + TomlEdit::TypeName(f.type);
			if (f.editor == TomlEdit::Editor::List && f.itemTypeKnown) tip += std::string(" of ") + TomlEdit::TypeName(f.itemType) + " items";
			if (f.escaped) tip += "\nWrite a line break as \\n.";
			// a long key on its own line, the value under it
			const int labelWidth = row.stacked || f.editor == TomlEdit::Editor::Check ? ContentRight - ContentLeft - (row.stacked ? 0 : 90) : TomlControl - ContentLeft - 16;
			const int fit        = static_cast<int>(f.key.size()) * LabelChar <= labelWidth;
			out += Text("Label" + id, ContentLeft, y + 10, labelWidth, BoxHeight, f.key, fit ? LabelStyle : SmallLabelStyle, tip);
			if (row.stacked) y += LabelLine;
			if (!item.comment.empty()) out += Text("KeyNote" + id, ContentLeft, y + 10 + BoxHeight + 4, ContentRight - ContentLeft, row.noteLines * NoteLine, item.comment, CommentStyle);
			switch (f.editor) {
			case TomlEdit::Editor::Check: out += Check("Check" + id, ContentRight - 70, y + 6, f.text == "true", "C" + id); break;
			case TomlEdit::Editor::Text:
			case TomlEdit::Editor::Raw: out += TypedBox("In" + id, "f" + std::to_string(row.field), TomlControl, y + 10, TextBoxWidth, "box", f.text, "(empty)", "Click to type; Esc puts the value back"); break;
			case TomlEdit::Editor::ReadOnly:
				out += Text("Fixed" + id, TomlControl, y + 10, TextBoxWidth, BoxHeight, "(on several lines: edit it in the file)", SmallLeftStyle, f.text);
				break;
			case TomlEdit::Editor::List:
				out += Text("Count" + id, TomlControl, y + 10, TextBoxWidth - 90, BoxHeight,
				            f.list.empty() ? "empty list" : std::to_string(f.list.size()) + (f.list.size() == 1 ? " item" : " items"), SmallLeftStyle);
				out += Button("Add" + id, ContentRight - 76, y + 7, AddButton, 0, 1, 3, "+" + id, "Add an item");
				break;
			}
			break;
		}
		case TomlRow::Type::Item: {
			const auto& f   = Toml.fields[static_cast<std::size_t>(row.field)];
			const auto  key = "i" + std::to_string(row.field) + "." + std::to_string(row.item);
			out += Text("ItemNumber" + id, TomlControl - 80, y + 5, 64, BoxHeight, std::to_string(row.item + 1), SmallLeftStyle);
			out += TypedBox("In" + id, key, TomlControl, y + 5, ItemBoxWidth, "itembox", f.list[static_cast<std::size_t>(row.item)], "(empty)", "Click to type; Esc puts the item back");
			if (RemoveReady) out += Button("Remove" + id, ContentRight - 58, y + 7, RemoveFile, 0, 1, 3, "-" + id, "Remove this item");
			else out += Button("Remove" + id, ContentRight - 50, y + 16, LeftArrow, 0, 1, 3, "-" + id, "Remove this item");
			break;
		}
		}
	}
	(void)head;
	out += Clip(RowsTop, ViewHeight, rows);
	out += ScrollBar(RowsTop, ViewHeight, total, Toml.scroll);
	out += TextButton("Reload", ContentLeft + 20, FooterY, "Reload", "reload", "Read the file again, dropping the changes not applied");
	out += TextButton("Apply", ContentRight - 20 - ButtonWidth, FooterY, "Apply", "apply", "Save the changes to the file");
	const std::string note = Notice.empty() ? "Applied changes take effect after a restart." : Notice;
	out += Text("Note", ContentLeft, NoteY, ContentRight - ContentLeft, 56, note, NoticeWarn ? WarnStyle : NoteStyle);
	return out;
}

// The whole window for the view shown.
std::string ViewLayout(const std::string& panelId, unsigned serial) {
	char open[2048] {};
	std::snprintf(open, sizeof(open),
	              R"({"type": "Panel", "name": "i-hate-tomls/%s", "fields": {"priority": 10, "rect": {"x": %d, "y": %d, "width": %d, "height": %d}, "anchor": "$LeftPanelAnchor"}, "children": [)"
	              R"({"type": "ClickCatcherWidget", "name": "PanelClickCatcher", "fields": {"rect": {"x": 0, "y": 0, "width": %d, "height": %d}}})"
	              // the game's side pillar on the screen's left edge, where its own left
	              // panels (imbue, hire menu) put it, and clicks on it kept from the game
	              R"(, {"type": "ClickCatcherWidget", "name": "LeftSideCatcher", "fields": {"rect": {"x": -236, "y": -205, "width": 236, "height": 2160}}})"
	              R"(, {"type": "ImageWidget", "name": "LeftSide", "fields": {"rect": {"x": -236, "y": -205}, "filename": "$LeftSideSprite"}})"
	              R"(, {"type": "ImageWidget", "name": "Background", "fields": {"rect": {"x": 0, "y": 0}, "filename": "PANEL\\Character_Sheet\\Character_Background", "frame": 0}})",
	              panelId.c_str(), PanelX, PanelY, PanelWidth, PanelHeight, PanelWidth, PanelHeight);
	std::string layout = open;
	if (CoverReady) layout += Image("Cover", CoverX, CoverY, "cover");
	if (BarReady) layout += Image("BarCover", BarX, BarY, "bar");
	layout += R"(, {"type": "ImageWidget", "name": "LeftHinge", "fields": {"rect": "$LeftHingeRect", "filename": "$LeftHingeSprite"}})";
	layout += R"(, {"type": "TextBoxWidget", "name": "Title", "fields": {"rect": "$InGamePanelTitleRect", "text": "Plugins", "style": "$StyleTitleBlock"}})";
	layout += R"(, {"type": "ButtonWidget", "name": "CloseButton", "fields": {"rect": {"x": 1075, "y": 9}, "filename": "PANEL\\closebtn_4x", "hoveredFrame": 3, "tooltipString": "@d2r:strClose", "sound": "cursor_close_window_hd", "onClickMessage": )"
	        + Command("close") + "}}";
	layout += R"(, {"type": "RectangleWidget", "name": "Probe)" + std::to_string(serial) + R"(", "fields": {"rect": {"x": 0, "y": 0, "width": 1, "height": 1}, "color": [0.0, 0.0, 0.0, 0.0]}})";

	if (Current == View::Client && Clients.empty()) Current = View::List;
	if (Current == View::List) layout += ListLayout();
	else if (Current == View::Client) layout += ClientLayout();
	else layout += TomlLayout();
	return layout + "]}";
}

// ---- panels (UI thread) ----

struct ViewPanel {
	std::string                         id;
	D2RL::Panels::RegistrationHandle    panel  = D2RL::Panels::InvalidHandle;
	D2RL::Resources::RegistrationHandle layout = D2RL::Resources::InvalidHandle;
	bool                                opened {};
};
std::vector<ViewPanel> Panels;
int                    ShownPanel = -1;
bool                   ReuseWorks = true;
unsigned               Serial {};
std::string            ProbeName, ProbePanel;
ULONGLONG              ProbeUntil {};

bool IsOpen(D2RL::Panels::RegistrationHandle handle) {
	D2RL::Panels::PanelInfo info { .structSize = D2RL::Panels::PanelInfoSize };
	return handle != D2RL::Panels::InvalidHandle && PanelApi->getPanelInfo(Context, handle, &info) == D2RL::Panels::Result::Success
	    && info.presentationState == D2RL::Panels::PresentationState::Open;
}

bool MenuOpen() { return ShownPanel >= 0 && IsOpen(Panels[static_cast<std::size_t>(ShownPanel)].panel); }

void ClosePanels() {
	for (const auto& view : Panels)
		if (IsOpen(view.panel)) (void)PanelApi->closePanel(Context, view.panel);
	ConfirmRow = -1;
}

void StopTyping(bool keep);
void CloseLeftPanels();

void CloseMenu() {
	StopTyping(true);
	ClosePanels();
	SetNotice({});
}

int NextPanel() {
	const int count = static_cast<int>(Panels.size());
	if (ReuseWorks || count < 3) return count < 2 ? 0 : ShownPanel == 0 ? 1 : 0;
	for (int i = 2; i < count; ++i)
		if (!Panels[static_cast<std::size_t>(i)].opened) return i;
	static bool told = false;
	if (!told) Say("I Hate TOMLs: out of fresh panels; restart the game to see changes");
	told = true;
	return ShownPanel == 0 ? 1 : 0;
}

void __cdecl ProbeTask(const D2RL::PluginContext*, void*) noexcept;

void ShowView() {
	if (Panels.empty() || !PanelApi || !ResourceApi) {
		Say("I Hate TOMLs is not available (see i-hate-tomls.log)");
		return;
	}
	const int index = NextPanel();
	auto&     next  = Panels[static_cast<std::size_t>(index)];
	++Serial;
	static std::vector<std::string> kept; // the loader copies the bytes, but keep them anyway
	if (kept.size() > 8) kept.erase(kept.begin());
	kept.push_back(ViewLayout(next.id, Serial));
	const std::string& layout = kept.back();
	ClosePanels();
	if (next.opened && WidgetApi) {
		const std::string             name = std::string(PluginId) + "/" + next.id;
		const D2RL::Widgets::UiAction unload { .structSize = D2RL::Widgets::UiActionSize, .flags = 0, .target = "PanelManager", .command = "UnloadPanel", .text = name.c_str() };
		(void)WidgetApi->dispatchUiAction(Context, &unload);
	}
	if (next.layout != D2RL::Resources::InvalidHandle) (void)ResourceApi->unregisterResource(Context, next.layout);
	next.layout            = D2RL::Resources::InvalidHandle;
	const std::string path = "data/global/ui/layouts/i-hate-tomls/" + next.id + "hd.json";
	const D2RL::Resources::ResourceRegistration resource {
		.structSize = D2RL::Resources::ResourceRegistrationSize,
		.path       = path.c_str(),
		.bytes      = layout.data(),
		.byteCount  = layout.size(),
	};
	const auto registered = ResourceApi->registerResource(Context, &resource, &next.layout);
	const auto opened     = PanelApi->openPanel(Context, next.panel);
	if (registered != D2RL::Resources::Result::Success || opened != D2RL::Panels::Result::Success)
		Log(true, "view on %s: layout result %u, open result %u (%zu bytes)", next.id.c_str(), static_cast<unsigned>(registered), static_cast<unsigned>(opened), layout.size());
	if (ReuseWorks && next.opened) {
		ProbeName  = "Probe" + std::to_string(Serial);
		ProbePanel = std::string(PluginId) + "/" + next.id;
		ProbeUntil = GetTickCount64() + 3000;
	}
	next.opened = true;
	ShownPanel  = index;
	MenuVisible = true;
	WatchMouse();
	if (Threads) (void)Threads->runOnUiThread(Context, ProbeTask, nullptr);
}

// Each frame until done: once a reused panel is open, checks that it shows
// the new layout. True to run again next frame.
bool CheckProbe() {
	if (ProbeName.empty() || !WidgetApi) return false;
	D2RL::Widgets::WidgetHandle root = D2RL::Widgets::InvalidHandle, probe = D2RL::Widgets::InvalidHandle;
	if (WidgetApi->findPanel(Context, ProbePanel.c_str(), &root) == D2RL::Widgets::Result::Success
	    && WidgetApi->findWidget(Context, root, ProbeName.c_str(), &probe) == D2RL::Widgets::Result::Success) {
		ProbeName.clear();
		return false;
	}
	if (GetTickCount64() < ProbeUntil) return true;
	ProbeName.clear();
	if (!MenuOpen()) return false;
	ReuseWorks = false;
	Log(true, "a reopened panel kept its old layout; each view now takes a new panel");
	ShowView();
	return false;
}

void __cdecl ProbeTask(const D2RL::PluginContext*, void*) noexcept {
	if (CheckProbe() && Threads) (void)Threads->runOnUiThread(Context, ProbeTask, nullptr);
}

// ---- typing ----
// While a box is active, a low-level keyboard hook on a thread of the
// plugin's own sends the keys to a hidden Windows EDIT control there (so
// keyboard layouts, Shift, AltGr and paste work) and keeps them from the
// game; the window shows the box's text with a caret. Enter keeps the text
// (a joined plugin's text is saved then), Esc puts it back, a click anywhere
// else keeps it.

std::atomic<bool>          TypedChanged {}, EnterHit {}, EscHit {};
std::atomic<std::uint64_t> TypeClickAt {}, TypeHitAt {}; // the last click, the last click on the box
std::mutex                 TypedLock;
std::wstring               Typed; // the box's text, as the EDIT control has it

void __cdecl TypedTask(const D2RL::PluginContext*, void*) noexcept;
void TypedNow() {
	if (Threads) (void)Threads->runOnUiThread(Context, TypedTask, nullptr);
}

// The joined plugin's setting a "s<index>" box edits.
Setting* DraftSetting(const std::string& key) {
	if (key.empty() || key[0] != 's' || Current != View::Client || Clients.empty()) return nullptr;
	auto&     client = Clients[static_cast<std::size_t>(std::clamp(Shown, 0, static_cast<int>(Clients.size()) - 1))];
	const int index  = std::atoi(key.c_str() + 1);
	return index >= 0 && index < static_cast<int>(client.settings.size()) ? &client.settings[static_cast<std::size_t>(index)] : nullptr;
}

// The TOML text a "f<field>" or "i<field>.<item>" box edits.
std::string* TomlText(const std::string& key) {
	if (key.size() < 2 || (key[0] != 'f' && key[0] != 'i') || Current != View::Toml) return nullptr;
	const int field = std::atoi(key.c_str() + 1);
	if (field < 0 || field >= static_cast<int>(Toml.fields.size())) return nullptr;
	auto& f = Toml.fields[static_cast<std::size_t>(field)];
	if (key[0] == 'f') return &f.text;
	const auto dot  = key.find('.');
	const int  item = dot == std::string::npos ? -1 : std::atoi(key.c_str() + dot + 1);
	return item >= 0 && item < static_cast<int>(f.list.size()) ? &f.list[static_cast<std::size_t>(item)] : nullptr;
}

std::string BoxText(const std::string& key) {
	if (key == "search") return SearchText;
	if (const Setting* setting = DraftSetting(key)) return ShownValue(Clients[static_cast<std::size_t>(Shown)], *setting);
	if (const std::string* text = TomlText(key)) return *text;
	return {};
}

void SetBoxText(const std::string& key, const std::string& text) {
	if (key == "search") {
		if (SearchText != text) ListScroll = 0;
		SearchText = text;
	} else if (DraftSetting(key)) ClientDraft = text;
	else if (std::string* box = TomlText(key)) *box = text;
}

#ifdef _WIN32
HWND               Edit {};
HHOOK              Hook {};
HANDLE             InputThread {};
std::atomic<DWORD> InputThreadId {};
std::uint8_t       Forwarded[256] {};
constexpr UINT     MsgLoad = WM_APP + 43;

bool GameInFront() {
	const HWND front = GetForegroundWindow();
	DWORD      pid   = 0;
	return front && GetWindowThreadProcessId(front, &pid) && pid == GetCurrentProcessId();
}

LRESULT CALLBACK KeyHook(int code, WPARAM wParam, LPARAM lParam) {
	const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
	if (code < 0 || !key || key->vkCode > 0xFF || (wParam != WM_KEYDOWN && wParam != WM_KEYUP)) return CallNextHookEx(Hook, code, wParam, lParam);
	const DWORD vk = key->vkCode;
	if (wParam == WM_KEYUP) {
		if (!Forwarded[vk]) return CallNextHookEx(Hook, code, wParam, lParam);
		Forwarded[vk] = 0;
	} else if (!Typing || !GameInFront()) return CallNextHookEx(Hook, code, wParam, lParam);
	switch (vk) {
	case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: case VK_MENU: case VK_LMENU: case VK_RMENU:
	case VK_LWIN: case VK_RWIN: case VK_CAPITAL: case VK_NUMLOCK: case VK_SCROLL: return CallNextHookEx(Hook, code, wParam, lParam);
	default: break;
	}
	const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0, alt = (key->flags & LLKHF_ALTDOWN) != 0;
	if (alt && !ctrl) return CallNextHookEx(Hook, code, wParam, lParam); // Alt+Tab, Alt+F4, Alt+Enter
	if (wParam == WM_KEYDOWN && (vk == VK_RETURN || vk == VK_ESCAPE)) {
		Forwarded[vk] = 1;
		if (vk == VK_ESCAPE) Typing = false, EscHit = true;
		else EnterHit = true;
		TypedNow();
		return 1;
	}
	if (!Edit) return CallNextHookEx(Hook, code, wParam, lParam);
	LPARAM message = static_cast<LPARAM>(1 | (static_cast<std::uint64_t>(static_cast<std::uint8_t>(key->scanCode)) << 16) | ((key->flags & LLKHF_EXTENDED) ? 0x01000000ULL : 0));
	if (wParam == WM_KEYUP) message |= static_cast<LPARAM>(0xC0000000ULL);
	UINT   id    = static_cast<UINT>(wParam);
	WPARAM value = vk;
	if (wParam == WM_KEYDOWN && ctrl && !alt && (vk == 'A' || vk == 'C' || vk == 'V' || vk == 'X' || vk == 'Z')) id = WM_CHAR, value = vk - 'A' + 1;
	if (!PostMessageW(Edit, id, value, message)) return CallNextHookEx(Hook, code, wParam, lParam);
	if (wParam == WM_KEYDOWN) Forwarded[vk] = 1;
	return 1;
}

// The game window's keyboard layout and key states, so the EDIT control
// makes the characters the game would.
void SyncKeyboard() {
	HWND game = GetForegroundWindow();
	if (DWORD pid = 0; game && GetWindowThreadProcessId(game, &pid) && pid == GetCurrentProcessId())
		if (const HKL layout = GetKeyboardLayout(GetWindowThreadProcessId(game, nullptr)); layout && layout != GetKeyboardLayout(0)) ActivateKeyboardLayout(layout, 0);
	BYTE keys[256] {};
	if (!GetKeyboardState(keys)) return;
	for (const int vk : { VK_SHIFT, VK_LSHIFT, VK_RSHIFT, VK_CONTROL, VK_LCONTROL, VK_RCONTROL, VK_MENU, VK_LMENU, VK_RMENU })
		keys[vk] = (GetAsyncKeyState(vk) & 0x8000) ? 0x80 : 0;
	for (const int vk : { VK_CAPITAL, VK_NUMLOCK })
		keys[vk] = static_cast<BYTE>((GetKeyState(vk) & 1) | ((GetAsyncKeyState(vk) & 0x8000) ? 0x80 : 0));
	SetKeyboardState(keys);
}

DWORD WINAPI InputMain(void*) {
	HMODULE self = nullptr;
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&KeyHook), &self);
	Edit = CreateWindowExW(0, L"EDIT", L"", ES_AUTOHSCROLL, 0, 0, 0, 0, HWND_MESSAGE, nullptr, self, nullptr);
	if (Edit) SendMessageW(Edit, EM_LIMITTEXT, 2000, 0), SetFocus(Edit);
	Hook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyHook, self, 0);
	if (!Edit || !Hook) Log(true, "typing could not start (edit %d, hook %d)", Edit != nullptr, Hook != nullptr);
	MSG message {};
	PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE); // the queue exists before the thread id is known
	InputThreadId = GetCurrentThreadId();
	while (GetMessageW(&message, nullptr, 0, 0) > 0) {
		if (message.message == MsgLoad) { // another box: its text, the caret at its end
			std::wstring text;
			{
				std::lock_guard guard(TypedLock);
				text = Typed;
			}
			if (Edit) SetWindowTextW(Edit, text.c_str()), SendMessageW(Edit, EM_SETSEL, text.size(), text.size());
			continue;
		}
		const bool key = message.message == WM_KEYDOWN || message.message == WM_KEYUP || message.message == WM_CHAR;
		if (message.message == WM_KEYDOWN || message.message == WM_KEYUP) SyncKeyboard();
		TranslateMessage(&message);
		DispatchMessageW(&message);
		if (key && Edit) {
			std::wstring buffer(static_cast<std::size_t>(GetWindowTextLengthW(Edit)) + 1, L'\0');
			buffer.resize(static_cast<std::size_t>(GetWindowTextW(Edit, buffer.data(), static_cast<int>(buffer.size()))));
			std::lock_guard guard(TypedLock);
			if (Typed != buffer) Typed = buffer, TypedChanged = true, TypedNow();
		}
	}
	return 0;
}
#endif

// UI thread: typing goes to box `key`.
void StartTyping(const std::string& key) {
	TypeHitAt = GetTickCount64();
	if (Typing && TypingKey == key) return;
	StopTyping(true);
	const std::string text = BoxText(key);
	TypingKey              = key;
	TypingBefore           = text;
	if (key[0] == 's') ClientDraft = text;
	{
		std::lock_guard guard(TypedLock);
		Typed = Wide(text);
	}
	TypedChanged = false, EnterHit = false, EscHit = false;
#ifdef _WIN32
	if (!InputThread) InputThread = CreateThread(nullptr, 0, InputMain, nullptr, 0, nullptr);
	std::memset(Forwarded, 0, sizeof(Forwarded));
	for (int k = 0; k < 50 && !InputThreadId; ++k) Sleep(2);
	if (const DWORD id = InputThreadId.load()) PostThreadMessageW(id, MsgLoad, 0, 0);
#endif
	Typing = true;
}

// UI thread: the box lets the keys go; `keep` false puts its text back.
void StopTyping(bool keep) {
	Typing                = false;
	const std::string key = std::move(TypingKey);
	TypingKey.clear();
	if (key.empty()) return;
	if (Setting* setting = DraftSetting(key)) {
		if (keep && ClientDraft != TypingBefore) (void)SetValue(Clients[static_cast<std::size_t>(Shown)], *setting, Quote(ClientDraft));
	} else if (!keep) SetBoxText(key, TypingBefore);
	ClientDraft.clear();
}

void __cdecl TypedTask(const D2RL::PluginContext*, void*) noexcept {
	if (EscHit.exchange(false)) {
		StopTyping(false);
		if (MenuOpen()) ShowView();
		return;
	}
	if (TypingKey.empty()) return;
	if (!MenuOpen()) return StopTyping(true);
	const bool changed = TypedChanged.exchange(false);
	if (changed) {
		std::wstring typed;
		{
			std::lock_guard guard(TypedLock);
			typed = Typed;
		}
		SetBoxText(TypingKey, Narrow(typed));
	}
	if (EnterHit.exchange(false)) StopTyping(true);
	else if (!changed) return;
	ShowView();
}

// UI thread, a moment after a click while typing: a click anywhere but on
// the box (its command came first) lets the keys go.
void __cdecl ClickTask(const D2RL::PluginContext*, void*) noexcept {
	const auto click = TypeClickAt.exchange(0);
	if (!click || TypingKey.empty() || TypeHitAt.load() >= click) return;
	StopTyping(true);
	if (MenuOpen()) ShowView();
}

// ---- scrolling ----
// No plugin service reports the wheel or the mouse moving, so the game
// window is subclassed (as Grail On! does): while the menu is open, the wheel
// over the left of the screen scrolls it, and the left button held on the
// scroll bar drags it.

int& ScrollOf() { return Current == View::List ? ListScroll : Current == View::Client ? ClientScroll : Toml.scroll; }

// The furthest the shown view scrolls; its row from scroll bar step `step`.
int StepRow(int step) {
	const int steps = std::max(2, BarSteps.load());
	return (BarMax * std::clamp(step, 0, steps - 1) + (steps - 1) / 2) / (steps - 1);
}

std::atomic<int> WheelSum {}, DragRow { -1 };

void __cdecl WheelTask(const D2RL::PluginContext*, void*) noexcept {
	const int rows = WheelSum.exchange(0);
	if (!rows || !MenuOpen()) return;
	int&      scroll = ScrollOf();
	const int before = scroll;
	scroll           = std::clamp(scroll + rows * WheelStep, 0, std::max(0, BarMax.load()));
	if (scroll != before) ShowView();
}

void __cdecl DragTask(const D2RL::PluginContext*, void*) noexcept {
	const int row = DragRow.load();
	if (row < 0 || !MenuOpen() || ScrollOf() == row) return;
	ScrollOf() = row;
	ShowView();
}

// UI thread, now and then: whether the menu is open, for the window's thread.
// The game's panels on the left side: the menu closes them when it opens,
// and closes when one of them opens.
constexpr const char* LeftPanels[] { "CharacterStatsPanel", "QuestLogPanel",      "PartyPanel",        "WaypointsPanel", "HirelingInventoryPanel", "BankExpansionLayout", "BankOriginalLayout",
	                                 "VendorPanelLayout",   "HoradricCubeLayout", "TradePanel",        "MessageLogPanel", "ImbueItemsPanel",        "HireMenuPanel" };
bool LeftShown[std::size(LeftPanels)] {};

bool PanelVisible(const char* name) {
	if (!WidgetApi || !D2RL::HasWidgetServiceField(WidgetApi, D2RL::WidgetServiceGetWidgetStateFieldEnd) || !WidgetApi->getWidgetState) return false;
	D2RL::Widgets::WidgetHandle panel = D2RL::Widgets::InvalidHandle;
	D2RL::Widgets::State        state {};
	return WidgetApi->findPanel(Context, name, &panel) == D2RL::Widgets::Result::Success && WidgetApi->getWidgetState(Context, panel, &state) == D2RL::Widgets::Result::Success && state.visible;
}

// UI thread: the game's left panels closed, before the menu opens.
void CloseLeftPanels() {
	for (std::size_t k = 0; k < std::size(LeftPanels); ++k) {
		// as seen now: only a panel opening from here on closes the menu
		LeftShown[k] = PanelVisible(LeftPanels[k]);
		if (!WidgetApi || !LeftShown[k]) continue;
		const D2RL::Widgets::UiAction close { .structSize = D2RL::Widgets::UiActionSize, .flags = 0, .target = "PanelManager", .command = "ClosePanel", .text = LeftPanels[k] };
		(void)WidgetApi->dispatchUiAction(Context, &close);
	}
}

void __cdecl WatchTask(const D2RL::PluginContext*, void*) noexcept {
	MenuVisible = MenuOpen();
	if (!MenuVisible && !TypingKey.empty()) StopTyping(true);
	// a left panel that opens while the menu is open closes the menu
	bool opened = false;
	for (std::size_t k = 0; k < std::size(LeftPanels); ++k) {
		const bool shown = MenuVisible && PanelVisible(LeftPanels[k]);
		opened           = opened || (shown && !LeftShown[k]);
		LeftShown[k]     = shown;
	}
	if (opened) {
		CloseMenu();
		MenuVisible = false;
	}
}

#ifdef _WIN32
HWND                GameWindow {};
WNDPROC             GameProc {};
std::atomic<bool>   BarLearned {};
std::atomic<double> BarOffsetX {}, BarOffsetY {};
bool                Dragging {}; // window thread

double UiScale() {
	RECT client {};
	return GameWindow && GetClientRect(GameWindow, &client) && client.bottom > 0 ? client.bottom / 2160.0 : 0;
}

// Where the panel is in the window: window = panel * scale + offset. The
// game draws its UI at the window's height / 2160, the panel hanging from
// $LeftPanelAnchor (x 0, y 39.7% of the height). A click on a scroll step
// puts the offset where the click was.
std::pair<double, double> PanelOffset() {
	if (BarLearned) return { BarOffsetX, BarOffsetY };
	RECT client {};
	if (!GameWindow || !GetClientRect(GameWindow, &client)) return {};
	const double scale = client.bottom / 2160.0;
	return { PanelX * scale, client.bottom * 0.397 + PanelY * scale };
}

void LearnBar(int step) {
	POINT        cursor {};
	const double scale = UiScale();
	if (!scale || !GetCursorPos(&cursor) || !ScreenToClient(GameWindow, &cursor)) return;
	BarOffsetX = cursor.x - (BarLeft + ScrollWidth / 2.0) * scale;
	BarOffsetY = cursor.y - (BarTop + (step + 0.5) * StepHeight) * scale;
	BarLearned = true;
}

// The step under a window point, or -1 off the bar (with `loose`, any
// height counts: a drag goes on above and below the bar).
int StepAt(POINT point, bool loose) {
	const double scale = UiScale();
	if (!scale) return -1;
	const auto [ox, oy] = PanelOffset();
	const int    steps  = BarSteps;
	const double x = (point.x - ox) / scale, y = (point.y - oy) / scale - BarTop;
	if (!loose && (x < BarLeft - 12 || x > BarLeft + ScrollWidth + 12 || y < 0 || y >= steps * StepHeight)) return -1;
	return std::clamp(static_cast<int>(std::floor(y / StepHeight)), 0, steps - 1);
}

LRESULT CALLBACK MenuProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
	if (message == WM_MOUSEWHEEL && MenuVisible) {
		POINT point { static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
		RECT  client {};
		if (ScreenToClient(window, &point) && GetClientRect(window, &client) && point.x < client.right * 45 / 100) {
			if (WheelSum.fetch_add(static_cast<short>(HIWORD(wParam)) > 0 ? -1 : 1) == 0 && Threads) (void)Threads->runOnUiThread(Context, WheelTask, nullptr);
			return 0;
		}
	}
	const POINT point { static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
	if (message == WM_LBUTTONDOWN && MenuVisible && BarMax > 0 && StepAt(point, false) >= 0) Dragging = true, DragRow = -1;
	else if (message == WM_MOUSEMOVE && Dragging) {
		if ((wParam & MK_LBUTTON) && MenuVisible && BarMax > 0) {
			const int step = StepAt(point, true);
			if (step >= 0 && DragRow.exchange(StepRow(step)) != StepRow(step) && Threads) (void)Threads->runOnUiThread(Context, DragTask, nullptr);
		} else Dragging = false;
	} else if (message == WM_LBUTTONUP) Dragging = false;
	if ((message == WM_LBUTTONUP || message == WM_RBUTTONUP) && Typing) {
		TypeClickAt = GetTickCount64();
		std::thread([] {
			Sleep(150);
			if (Threads) (void)Threads->runOnUiThread(Context, ClickTask, nullptr);
		}).detach();
	}
	return CallWindowProcW(GameProc, window, message, wParam, lParam);
}

BOOL CALLBACK FindGameWindow(HWND window, LPARAM found) {
	DWORD process = 0;
	GetWindowThreadProcessId(window, &process);
	if (process != GetCurrentProcessId() || !IsWindowVisible(window) || GetWindow(window, GW_OWNER)) return TRUE;
	RECT rect {}, best {};
	auto* result = reinterpret_cast<HWND*>(found);
	GetWindowRect(window, &rect);
	if (*result) GetWindowRect(*result, &best);
	if (!*result || (rect.right - rect.left) * (rect.bottom - rect.top) > (best.right - best.left) * (best.bottom - best.top)) *result = window;
	return TRUE;
}

// Once, when the menu first opens: the mouse watched, and a thread that
// asks the UI thread now and then whether the menu is still open.
void WatchMouse() {
	static bool started = false;
	if (started) return;
	started = true;
	HWND window {};
	EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&window));
	if (window) GameProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(MenuProc)));
	if (GameProc) GameWindow = window;
	else Log(true, "could not watch the mouse wheel (error %lu): use the scroll bar", GetLastError());
	std::thread([] {
		for (;;) {
			Sleep(250);
			if (Threads) (void)Threads->runOnUiThread(Context, WatchTask, nullptr);
		}
	}).detach();
}
#else
void LearnBar(int) {}
void WatchMouse() {}
#endif

void Toggle() {
	if (MenuOpen()) CloseMenu();
	else {
		SetNotice({});
		Current = View::List;
		WatchMouse();
		CloseLeftPanels();
		ShowView();
	}
	MenuVisible = MenuOpen();
}

void __cdecl ToggleTask(const D2RL::PluginContext*, void*) noexcept { Toggle(); }

// ---- clicks ----

std::string Pending;

void OpenEntry(int index) {
	const auto list = Entries();
	if (index < 0 || index >= static_cast<int>(list.size())) return;
	const auto& e = list[static_cast<std::size_t>(index)];
	SetNotice({});
	switch (e.mode) {
	case Mode::Window: {
		const OwnWindow* window = WindowOf(e.id);
		if (!window) return;
		const auto open = window->open;
		void*      user = window->user;
		CloseMenu();
		MenuVisible = false;
		open(user);
		return;
	}
	case Mode::Menu:
		Shown        = ClientIndex(e.id);
		ClientScroll = 0;
		OpenDrop     = -1;
		Current      = View::Client;
		break;
	case Mode::Toml: {
		Toml        = {};
		Toml.plugin = e.id;
		Toml.name   = e.name;
		Toml.path   = e.config;
		std::string why;
		if (!LoadToml(Toml.path, why)) Log(true, "%s: %s", Narrow(Toml.path).c_str(), why.c_str());
		Current = View::Toml;
		break;
	}
	default: return;
	}
	ShowView();
}

void RunListCommand(const std::string& command) {
	if (command == "searchclear") SearchText.clear(), ListScroll = 0;
	else if (command[0] == 'O') return OpenEntry(std::atoi(command.c_str() + 1));
	else return;
	ShowView();
}

void RunClientCommand(const std::string& command) {
	if (Clients.empty()) return;
	auto& client = Clients[static_cast<std::size_t>(std::clamp(Shown, 0, static_cast<int>(Clients.size()) - 1))];
	const int dropWas = OpenDrop;
	OpenDrop          = -1; // any click closes a drop-down list
	if (command == "back") {
		Current = View::List;
		SetNotice({});
	} else if (command == "dropx") {
	} else if (command.size() >= 2) {
		const int index = std::atoi(command.c_str() + 1);
		if (index < 0 || index >= static_cast<int>(client.settings.size())) return;
		const Setting setting = client.settings[static_cast<std::size_t>(index)];
		switch (command[0]) {
		case 'D': OpenDrop = dropWas == index ? -1 : index; break;
		case 'P': {
			const auto colon = command.find(':');
			const int  k     = colon == std::string::npos ? -1 : std::atoi(command.c_str() + colon + 1);
			if (k >= 0 && k < static_cast<int>(setting.values.size())) (void)SetValue(client, setting, Quote(setting.values[static_cast<std::size_t>(k)]));
			break;
		}
		case 'H': StepPosition(client, setting, false, false); break;
		case 'J': StepPosition(client, setting, false, true); break;
		case 'K': StepPosition(client, setting, true, false); break;
		case 'M': StepPosition(client, setting, true, true); break;
		case 'Z':
			if (!setting.defaultValue.empty()) (void)SetValue(client, setting, setting.defaultValue);
			break;
		case 'T': (void)SetValue(client, setting, Unquote(GetValue(client, setting)) == "true" ? "false" : "true"); break;
		case 'L': Step(client, setting, false); break;
		case 'R': Step(client, setting, true); break;
		case 'A':
			if (setting.confirm && !(ConfirmRow == index && GetTickCount64() < ConfirmUntil)) {
				ConfirmRow   = index;
				ConfirmUntil = GetTickCount64() + 10000;
				SetNotice({});
			} else {
				ConfirmRow = -1;
				(void)SetValue(client, setting, "\"run\"");
				if (Notice.empty()) SetNotice(setting.label + ": done.");
			}
			break;
		default: return;
		}
	} else return;
	ShowView();
}

// Scrolls the TOML view so row `r` shows whole.
void ShowTomlRow(int r) {
	int top = 0;
	for (int k = 0; k < r && k < static_cast<int>(Toml.rows.size()); ++k) top += Toml.rows[static_cast<std::size_t>(k)].height;
	const int height = r < static_cast<int>(Toml.rows.size()) ? Toml.rows[static_cast<std::size_t>(r)].height : 0;
	if (top < Toml.scroll) Toml.scroll = top;
	else if (top + height > Toml.scroll + ViewHeight) Toml.scroll = top + height - ViewHeight;
}

void RunTomlCommand(const std::string& command) {
	const bool leaving = Toml.leaving;
	Toml.leaving       = false;
	if (command == "back") {
		if (!leaving && TomlChanged()) {
			Toml.leaving = true;
			SetNotice("Changes not applied: Apply them, or click the back arrow again to drop them.", true);
		} else {
			Current = View::List;
			SetNotice({});
		}
	} else if (command == "apply") ApplyToml();
	else if (command == "reload") {
		const int   scroll = Toml.scroll;
		std::string why;
		if (LoadToml(Toml.path, why)) SetNotice("Read again from the file.");
		else SetNotice(why, true);
		Toml.scroll = scroll;
	} else if (command.size() >= 2 && (command[0] == 'C' || command[0] == '+' || command[0] == '-')) {
		const int r = std::atoi(command.c_str() + 1);
		if (r < 0 || r >= static_cast<int>(Toml.rows.size())) return;
		const TomlRow row = Toml.rows[static_cast<std::size_t>(r)];
		if (row.field < 0) return;
		auto& f = Toml.fields[static_cast<std::size_t>(row.field)];
		SetNotice({});
		if (command[0] == 'C') f.text = f.text == "true" ? "false" : "true";
		else if (command[0] == '-') {
			if (row.item >= 0 && row.item < static_cast<int>(f.list.size())) f.list.erase(f.list.begin() + row.item);
			BuildTomlRows();
		} else {
			// a new empty item, typed in at once
			f.list.emplace_back();
			BuildTomlRows();
			const int last = r + static_cast<int>(f.list.size());
			ShowTomlRow(last);
			StartTyping("i" + std::to_string(row.field) + "." + std::to_string(f.list.size() - 1));
		}
	} else return;
	ShowView();
}

void RunCommand(const std::string& command) {
	if (command.empty()) return;
	if (command.rfind("type", 0) == 0 && command.size() > 4) {
		StartTyping(command.substr(4));
		return ShowView();
	}
	if (command.rfind("step", 0) == 0) {
		const int step = std::atoi(command.c_str() + 4);
		LearnBar(step);
		ScrollOf() = StepRow(step);
		return ShowView();
	}
	StopTyping(true);
	if (command == "close") {
		CloseMenu();
		MenuVisible = false;
		return;
	}
	if (Current == View::List) RunListCommand(command);
	else if (Current == View::Client) RunClientCommand(command);
	else RunTomlCommand(command);
}

void __cdecl CommandTask(const D2RL::PluginContext*, void*) noexcept {
	const std::string command = std::move(Pending);
	Pending.clear();
	RunCommand(command);
}

auto OnUiMessage(const D2RL::PluginContext*, const D2RL::SharedEvents::UiMessageEvent* event, void*) noexcept -> D2RL::SharedEvents::UiMessageAction {
	if (!event || !event->text || !event->target || std::strcmp(event->target, "PanelManager") != 0) return D2RL::SharedEvents::UiMessageAction::Continue;
	const std::size_t prefix = std::strlen(CommandPrefix);
	if (std::strncmp(event->text, CommandPrefix, prefix) != 0) return D2RL::SharedEvents::UiMessageAction::Continue;
	// D2R reports a button's message twice, about a millisecond apart.
	static std::string last;
	static ULONGLONG   lastMs {};
	const ULONGLONG    now = GetTickCount64();
	if (last == event->text && now - lastMs < 150) return D2RL::SharedEvents::UiMessageAction::Consume;
	last   = event->text;
	lastMs = now;
	if (!Pending.empty()) return D2RL::SharedEvents::UiMessageAction::Consume; // one at a time
	Pending = event->text + prefix;
	if (!Threads || Threads->runOnUiThread(Context, CommandTask, nullptr) != D2RL::Threads::Result::Success) Pending.clear();
	return D2RL::SharedEvents::UiMessageAction::Consume;
}

// ---- opening ----

auto OnOpenAction(const D2RL::PluginContext*, const D2RL::Input::ActionEvent* event, void*) noexcept -> D2RL::Input::ActionResult {
	if (!D2RL::Input::HasActionEventField(event, D2RL::Input::ActionEventRequiredSize) || event->kind != D2RL::Input::ActionEventKind::Pressed) return D2RL::Input::ActionResult::Ignored;
	if (Threads) (void)Threads->runOnUiThread(Context, ToggleTask, nullptr);
	return D2RL::Input::ActionResult::Handled;
}

auto PluginsCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
	if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
	int counts[4] {};
	for (const auto& e : Entries()) ++counts[static_cast<int>(e.mode)];
	Say("I Hate TOMLs: %d in this menu, %d with their own window, %d with a TOML file, %d without settings", counts[static_cast<int>(Mode::Menu)], counts[static_cast<int>(Mode::Window)],
	    counts[static_cast<int>(Mode::Toml)], counts[static_cast<int>(Mode::None)]);
	if (Threads) (void)Threads->runOnUiThread(Context, ToggleTask, nullptr);
	return D2RL::ConsoleCommandResult::Handled;
}

// ---- after every plugin has loaded ----

// The lobby's refresh button (Grail On!'s reset): its sprite file and frames,
// found in the lobby panels' layouts. Read on the game thread.

void FindRefreshButton() {
	RefreshArt = {};
	std::string trouble;
	const std::string profile = ArchiveFile("data/global/ui/layouts/_profilehd.json", trouble);
	const auto stringAfter = [](const std::string& text, std::size_t from, const char* key, std::size_t limit) -> std::string {
		const auto at = text.find(std::string("\"") + key + "\"", from);
		if (at == std::string::npos || at > limit) return {};
		const auto open  = text.find('"', text.find(':', at) + 1);
		const auto close = open == std::string::npos ? open : text.find('"', open + 1);
		return close == std::string::npos ? std::string() : text.substr(open + 1, close - open - 1);
	};
	const auto numberAfter = [](const std::string& text, std::size_t from, const char* key, std::size_t limit, int fallback) {
		const auto at = text.find(std::string("\"") + key + "\"", from);
		if (at == std::string::npos || at > limit) return fallback;
		return std::atoi(text.c_str() + text.find(':', at) + 1);
	};
	for (const char* panel : { "joingamepanelhd", "gamelistpanelhd", "ladderpanelhd", "friendslistpanelhd", "consolelobbypanelhd", "createGamepanelhd" }) {
		const std::string path   = std::string("data/global/ui/layouts/") + panel + ".json";
		const std::string layout = ArchiveFile(path.c_str(), trouble);
		if (layout.empty()) continue;
		for (std::size_t at = 0; (at = layout.find("\"name\"", at)) != std::string::npos; ++at) {
			const auto  value = stringAfter(layout, at, "name", at + 8);
			std::string lower = value;
			for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			if (lower.find("refresh") == std::string::npos) continue;
			const auto  next     = layout.find("\"type\"", at + 8);
			const auto  limit    = next == std::string::npos ? layout.size() : next;
			std::string filename = stringAfter(layout, at, "filename", limit);
			if (!filename.empty() && filename[0] == '$') filename = stringAfter(profile, 0, filename.c_str() + 1, profile.size());
			if (filename.empty()) continue;
			std::string file = "data/hd/global/ui/";
			for (std::size_t k = 0; k < filename.size(); ++k) {
				if (filename[k] == '\\') {
					file += '/';
					if (k + 1 < filename.size() && filename[k + 1] == '\\') ++k;
				} else file += static_cast<char>(std::tolower(static_cast<unsigned char>(filename[k])));
			}
			file += ".sprite";
			RefreshArt.file    = ArchiveFile(file.c_str(), trouble);
			RefreshArt.normal  = numberAfter(layout, at, "normalFrame", limit, 0);
			RefreshArt.hovered = numberAfter(layout, at, "hoveredFrame", limit, RefreshArt.normal);
			RefreshArt.pressed = numberAfter(layout, at, "pressedFrame", limit, RefreshArt.normal);
			Log(false, "refresh button %s in %s: %s (%s), frames %d/%d/%d", value.c_str(), panel, file.c_str(), RefreshArt.file.empty() ? "not read" : "read", RefreshArt.normal, RefreshArt.hovered,
			    RefreshArt.pressed);
			if (!RefreshArt.file.empty()) return;
		}
	}
	Log(false, "the lobby's refresh button was not found; the reset button is the plugin's own");
}

void __cdecl AfterAllLoaded(const D2RL::PluginContext*, const D2RL::Lifecycle::DataTablesLoadedEvent*, void*) noexcept {
	static bool done = false;
	if (done) return;
	done = true;
	FindRunningPlugins();
	for (int k = 0; k < ArtCount; ++k) {
		std::string trouble;
		ArtFiles[k] = ArchiveFile(ArtPaths[k], trouble);
		if (ArtFiles[k].empty()) Log(true, "game art %s not read: %s", ArtPaths[k], trouble.c_str());
	}
	FindRefreshButton();
	if (Threads) (void)Threads->runOnUiThread(Context, MakeArt, nullptr);
	Log(false, "every plugin has loaded: %zu in the menu, %zu with their own window", Clients.size(), Windows.size());
}

template <typename T>
const T* Query(const char* name) {
	const T* service = nullptr;
	if (Context->QueryService(&service) != D2RL::ServiceQueryResult::Success || !service) {
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
	Backup::Restore(); // before the other plugins read their configs

	Threads     = Query<D2RL::ThreadService>("thread");
	PanelApi    = Query<D2RL::PanelService>("panel");
	WidgetApi   = Query<D2RL::WidgetService>("widget");
	ResourceApi = Query<D2RL::ResourceService>("resource");
	EventApi    = Query<D2RL::SharedEventService>("shared event");
	if (Threads && !D2RL::HasThreadServiceField(Threads, D2RL::ThreadServiceRequiredSize)) Threads = nullptr;
	if (WidgetApi && !D2RL::HasWidgetServiceField(WidgetApi, D2RL::WidgetServiceRequiredSize)) WidgetApi = nullptr;

	// The service first: plugins loading after this one register at once.
	if (const auto* comm = Query<D2RL::PluginCommunicationService>("plugin communication");
	    comm && D2RL::HasPluginCommunicationServiceField(comm, D2RL::PluginCommunicationServiceRequiredSize)) {
		const D2RL::PluginCommunication::PublishServiceRequest request {
			.structSize     = D2RL::PluginCommunication::PublishServiceRequestSize,
			.name           = PluginMenu::ServiceName,
			.serviceVersion = 1,
			.tableSize      = sizeof(Service),
			.table          = &Service,
		};
		const auto result = comm->publishService(context, &request);
		if (result != D2RL::PluginCommunication::Result::Success) Log(true, "could not offer the settings-menu service (result %u)", static_cast<unsigned>(result));
	}

	if (!PanelApi || !ResourceApi || !Threads || !EventApi) {
		Log(true, "I Hate TOMLs %s: inactive, a service it needs is missing", PluginVersion);
		return true;
	}
	// (Keys reach text boxes through the plugin's own keyboard hook.)
	// A window of its own: GameplayLeftSlot would open the inventory with it.
	const auto flags = D2RL::Panels::PanelFlags::CloseOnEscape;
	for (int i = 1; i <= PoolSize; ++i) {
		ViewPanel view { .id = "Menu" + std::to_string(i) };
		// The game only finds a panel's layout if one was registered at load
		// (as Grail On! does); this first one is replaced before each open.
		{
			static std::vector<std::string> kept;
			kept.push_back(R"({"type": "Panel", "name": ")" + std::string(PluginId) + "/" + view.id + R"(", "fields": {"priority": 10}, "children": []})");
			const std::string path = "data/global/ui/layouts/i-hate-tomls/" + view.id + "hd.json";
			const D2RL::Resources::ResourceRegistration resource {
				.structSize = D2RL::Resources::ResourceRegistrationSize,
				.path       = path.c_str(),
				.bytes      = kept.back().data(),
				.byteCount  = kept.back().size(),
			};
			if (const auto r = ResourceApi->registerResource(context, &resource, &view.layout); r != D2RL::Resources::Result::Success && i == 1)
				Log(true, "first layout for %s refused (result %u)", view.id.c_str(), static_cast<unsigned>(r));
		}
		const D2RL::Panels::PanelRegistration registration {
			.structSize = D2RL::Panels::PanelRegistrationSize,
			.flags      = flags,
			.localId    = view.id.c_str(),
		};
		const auto result = PanelApi->registerPanel(context, &registration, &view.panel);
		if (result != D2RL::Panels::Result::Success) {
			if (Panels.size() < 2) Log(true, "panel %s refused (result %u)", view.id.c_str(), static_cast<unsigned>(result));
			break;
		}
		Panels.push_back(std::move(view));
	}
	if (Panels.size() < 2) Log(true, "only %zu panels could be registered", Panels.size());

	const D2RL::SharedEvents::UiMessageListener listener {
		.structSize = D2RL::SharedEvents::UiMessageListenerSize,
		.priority   = 100,
		.callback   = OnUiMessage,
	};
	D2RL::SharedEvents::ListenerHandle listenerHandle = D2RL::SharedEvents::InvalidHandle;
	if (EventApi->registerUiMessageListener(context, &listener, &listenerHandle) != D2RL::SharedEvents::Result::Success) Log(true, "could not listen to button clicks");

	if (const auto* lifecycle = Query<D2RL::LifecycleService>("lifecycle")) {
		const D2RL::Lifecycle::DataTablesLoadedListener loaded { .structSize = sizeof(D2RL::Lifecycle::DataTablesLoadedListener), .callback = AfterAllLoaded };
		D2RL::Lifecycle::ListenerHandle                 handle {};
		(void)lifecycle->registerDataTablesLoadedListener(context, &loaded, &handle);
	}

	if (const auto* input = Query<D2RL::InputService>("input"); input && D2RL::HasInputServiceField(input, D2RL::InputServiceRequiredSize)) {
		const D2RL::Input::ActionRegistration action {
			.structSize       = D2RL::Input::ActionRegistrationSize,
			.logicalId        = "open-i-hate-tomls",
			.displayName      = "Open I Hate TOMLs",
			.category         = "I Hate TOMLs",
			.defaultPrimary   = { .key = D2RL::Input::Key::None },
			.defaultSecondary = { .key = D2RL::Input::Key::None },
			.callback         = OnOpenAction,
		};
		D2RL::Input::ActionHandle handle = D2RL::Input::InvalidHandle;
		if (input->registerAction(context, &action, &handle) != D2RL::Input::Result::Success) Log(true, "could not add Open I Hate TOMLs to the Controls menu");
	}
	(void)context->RegisterConsoleCommand("pluginmenu", PluginsCommand, "Opens or closes I Hate TOMLs (the plugin settings window).");
	Log(false, "I Hate TOMLs %s: ready on D2R %s, %zu panels", PluginVersion, context->buildVersion ? context->buildVersion : "?", Panels.size());
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {}
