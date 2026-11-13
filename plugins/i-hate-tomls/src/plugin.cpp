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
#include <d2r_gui.h>
#include "toml_edit.h"
#include <D2RLPlugin/api.h>
#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
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
constexpr const char* PluginVersion = "1.1.1-dev-1";
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

namespace Gui = D2RGui;
constexpr int PanelX = Gui::CharacterWindow::X, PanelY = Gui::CharacterWindow::Y, PanelWidth = Gui::CharacterWindow::Width, PanelHeight = Gui::CharacterWindow::Height;
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
// Text metrics, set from the game's font (RefreshText): comment line height
// and characters per line, a key's width per character and the height of a
// key stacked on its own line. The defaults fit the game's own font (a wide
// small caps font, about 17 px a character); a font mod with taller or wider
// letters raises them, never lowers them.
int NoteLine = 34, NoteChars = 50;
int LabelLine = 50; // (without the font, a character is taken as 20 units: D2R GUI Font::fallbackChar)
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
constexpr int BarLeft = 1054, ScrollWidth = 30, StepHeight = 30;
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

using Gui::Json;
using Gui::GameText;

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

enum class Kind { Bool, Int, Float, Enum, String, Action, Header, Text, Position, Table };

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
	// table: a check box per row and column, each one the bool "<row>.<column>"
	std::vector<std::string> rows, rowLabels, columns, columnLabels;
};

struct Client {
	uint64_t             handle {};
	std::string          plugin, title, hub; // hub: its slug on the D2RLoader Hub, when not built from its name
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
	if (name == "table") return Kind::Table;
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
			else if (key == "hub") client.hub = Unquote(value);
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
		else if (key == "rows") current->rows = ArrayItems(value);
		else if (key == "row_labels") current->rowLabels = ArrayItems(value);
		else if (key == "columns") current->columns = ArrayItems(value);
		else if (key == "column_labels") current->columnLabels = ArrayItems(value);
	}
	for (auto& s : client.settings) {
		if (s.label.empty() && s.kind != Kind::Table) s.label = s.key;
		if (s.kind == Kind::Table) {
			s.rowLabels.resize(s.rows.size()), s.columnLabels.resize(s.columns.size());
			for (std::size_t r = 0; r < s.rows.size(); ++r)
				if (s.rowLabels[r].empty()) s.rowLabels[r] = s.rows[r];
			for (std::size_t c = 0; c < s.columns.size(); ++c)
				if (s.columnLabels[c].empty()) s.columnLabels[c] = s.columns[c];
		}
		if (s.step <= 0) s.step = 1;
		if (s.shiftStep <= 0) s.shiftStep = s.step * 10;
		if (s.ctrlStep <= 0) s.ctrlStep = s.step * 100;
		if (s.kind == Kind::Position && s.min == 0 && s.max == 100) s.min = -100000, s.max = 100000;
		if (s.kind == Kind::Int) s.decimals = 0;
	}
	// Rows a menu cannot show.
	std::erase_if(client.settings, [](const Setting& s) {
		if (s.kind == Kind::Table) return s.rows.empty() || s.columns.empty();
		return s.kind != Kind::Header && s.kind != Kind::Text && s.key.empty();
	});
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
		if (!info || info->infoSize < D2RL::PluginInfoRequiredSize || !info->id || !info->id[0]) continue;
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

// ---- updates on the D2RLoader Hub ----
// Once per launch, on a thread of its own, the Hub's public catalog
// (d2rloader.net/api/v1/hub/plugins, no login) is read a page at a time; each
// running plugin is found by its slug, built from its name as the Hub builds
// it (lowercase, every run of other characters one "-", none at the ends). A
// joined plugin may give its own (`hub = "<slug>"` in its schema). Off with
// check_updates = false in i-hate-tomls.toml.
namespace Hub {
std::mutex                         Lock;
std::map<std::string, std::string> Latest; // slug -> latest version on the Hub
std::atomic<bool>                  Done {};
enum class State { Off, Checking, Failed, Done };
std::atomic<State>                 Now { State::Off };

std::string SlugOf(const std::string& name) {
	std::string slug;
	bool        dash = false;
	for (const unsigned char c : name) {
		const char lower = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
		if ((lower >= 'a' && lower <= 'z') || (lower >= '0' && lower <= '9')) {
			if (dash && !slug.empty()) slug += '-';
			slug += lower, dash = false;
		} else dash = true;
	}
	return slug;
}

// A small JSON reader: just enough to walk the catalog.
struct Json {
	std::string_view s;
	std::size_t      i = 0;
	void             Skip() { while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i; }
	bool             String(std::string& out) {
        Skip();
        if (i >= s.size() || s[i] != '"') return false;
        out.clear();
        for (++i; i < s.size(); ++i) {
            if (s[i] == '"') return ++i, true;
            if (s[i] == '\\' && i + 1 < s.size()) {
                const char e = s[++i];
                out += e == 'n' ? '\n' : e == 't' ? '\t' : e == 'u' ? (i += 4, '?') : e;
            } else out += s[i];
        }
        return false;
	}
	// Skips one value; with `fields`, keeps an object's string fields.
	bool Value(std::map<std::string, std::string>* fields = nullptr, const std::string& prefix = {}, int depth = 0) {
		Skip();
		if (i >= s.size() || depth > 32) return false;
		const char c = s[i];
		if (c == '"') {
			std::string text;
			return String(text);
		}
		if (c == '{' || c == '[') {
			const char close = c == '{' ? '}' : ']';
			++i;
			for (bool first = true;; first = false) {
				Skip();
				if (i < s.size() && s[i] == close) return ++i, true;
				if (!first) {
					if (i >= s.size() || s[i] != ',') return false;
					++i;
				}
				std::string key;
				if (c == '{') {
					if (!String(key)) return false;
					Skip();
					if (i >= s.size() || s[i] != ':') return false;
					++i;
					Skip();
					if (fields && i < s.size() && s[i] == '"') {
						std::string text;
						if (!String(text)) return false;
						(*fields)[prefix + key] = text;
						continue;
					}
				}
				if (!Value(c == '{' && fields ? fields : nullptr, prefix + key + ".", depth + 1)) return false;
			}
		}
		while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']') ++i; // number, true, false, null
		return true;
	}
};

// The catalog page's items as their string fields ("slug", "latestRelease.version"...); false when unreadable.
bool Items(const std::string& body, std::vector<std::map<std::string, std::string>>& items, int& total) {
	Json json { body };
	json.Skip();
	if (json.i >= body.size() || body[json.i] != '{') return false;
	++json.i;
	for (bool first = true;; first = false) {
		json.Skip();
		if (json.i < body.size() && body[json.i] == '}') return true;
		if (!first) {
			if (json.i >= body.size() || body[json.i] != ',') return false;
			++json.i;
		}
		std::string key;
		if (!json.String(key)) return false;
		json.Skip();
		if (json.i >= body.size() || body[json.i] != ':') return false;
		++json.i;
		json.Skip();
		if (key == "items" && json.i < body.size() && body[json.i] == '[') {
			++json.i;
			for (bool firstItem = true;; firstItem = false) {
				json.Skip();
				if (json.i < body.size() && body[json.i] == ']') {
					++json.i;
					break;
				}
				if (!firstItem) {
					if (json.i >= body.size() || body[json.i] != ',') return false;
					++json.i;
				}
				items.emplace_back();
				if (!json.Value(&items.back())) return false;
			}
		} else if (key == "total") {
			total = std::atoi(body.c_str() + json.i);
			if (!json.Value()) return false;
		} else if (!json.Value()) return false;
	}
}

bool Get(const std::wstring& path, std::string& body) {
	body.clear();
	HINTERNET session = WinHttpOpen(L"I Hate TOMLs", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session) session = WinHttpOpen(L"I Hate TOMLs", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session) return false;
	WinHttpSetTimeouts(session, 5000, 5000, 10000, 10000);
	bool      ok      = false;
	HINTERNET connect = WinHttpConnect(session, L"d2rloader.net", INTERNET_DEFAULT_HTTPS_PORT, 0);
	HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
	if (request && WinHttpSendRequest(request, L"Accept: application/json\r\n", static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(request, nullptr)) {
		DWORD status = 0, size = sizeof(status);
		WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
		for (DWORD available = 0; WinHttpQueryDataAvailable(request, &available) && available && body.size() < (4u << 20);) {
			std::string chunk(available, '\0');
			DWORD       read = 0;
			if (!WinHttpReadData(request, chunk.data(), available, &read)) break;
			body.append(chunk.data(), read);
		}
		ok = status == 200;
		if (!ok) Log(true, "hub: %ls answered %lu", path.c_str(), status);
	}
	if (request) WinHttpCloseHandle(request);
	if (connect) WinHttpCloseHandle(connect);
	WinHttpCloseHandle(session);
	return ok;
}

// Whether `hub` is newer than `running`: both as numbers when they read so
// ("v" and a "-dev-N" suffix ignored, a dev build counts as its version),
// else whenever they differ.
bool Newer(std::string hub, std::string running) {
	const auto clean = [](std::string v) {
		if (!v.empty() && (v[0] == 'v' || v[0] == 'V')) v.erase(0, 1);
		return v;
	};
	hub = clean(hub), running = clean(running);
	if (hub.empty() || hub == running) return false;
	const auto numbers = [](const std::string& v, std::vector<long>& out) {
		std::size_t at = 0;
		while (at < v.size()) {
			if (v[at] < '0' || v[at] > '9') return at > 0 && v[at] == '-' && !out.empty(); // a suffix such as -dev-3
			char* end = nullptr;
			out.push_back(std::strtol(v.c_str() + at, &end, 10));
			at = static_cast<std::size_t>(end - v.c_str());
			if (at < v.size() && v[at] == '.') ++at;
			else if (at < v.size() && v[at] != '-') return false;
			else if (at < v.size()) return true;
		}
		return !out.empty();
	};
	std::vector<long> a, b;
	if (!numbers(hub, a) || !numbers(running, b)) return true;
	while (a.size() < b.size()) a.push_back(0);
	while (b.size() < a.size()) b.push_back(0);
	return a > b;
}

// A switch of i-hate-tomls.toml (on unless "false").
bool Wanted(const char* key = "check_updates") {
	if (!Context->pluginConfigPath) return true;
	std::string text;
	HANDLE      file = CreateFileW(Context->pluginConfigPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) return true;
	char  buffer[4096] {};
	DWORD read = 0;
	(void)ReadFile(file, buffer, sizeof(buffer) - 1, &read, nullptr);
	CloseHandle(file);
	const auto doc = TomlDoc::Parse(std::string(buffer, read));
	for (const auto& item : doc.items)
		if (item.kind == TomlDoc::Kind::Key && item.text == key) return item.value.text != "false";
	return true;
}

void __cdecl Refresh(const D2RL::PluginContext*, void*) noexcept;

void Start() {
	if (!Wanted()) return Log(false, "hub: update check off (check_updates = false)");
	Now = State::Checking;
	std::thread([] {
		std::map<std::string, std::string> found;
		int                                pages = 0;
		for (int page = 1, total = 1; page <= 10 && (page - 1) * 50 < total; ++page) {
			if (page > 1) Sleep(1500); // the Hub limits quick requests
			std::string body;
			if (!Get(L"/api/v1/hub/plugins?sort=updated&type=plugin&pageSize=50&page=" + std::to_wstring(page), body)) break;
			std::vector<std::map<std::string, std::string>> items;
			total = 0;
			if (!Items(body, items, total)) {
				Log(true, "hub: page %d could not be read", page);
				break;
			}
			++pages;
			for (const auto& item : items) {
				const auto slug = item.find("slug"), version = item.find("latestRelease.version");
				if (slug != item.end() && version != item.end()) found[slug->second] = version->second;
			}
			if (items.empty()) break;
		}
		{
			std::lock_guard<std::mutex> hold(Lock);
			Latest = std::move(found);
		}
		Log(false, "hub: %zu plugins read from %d pages", Latest.size(), pages);
		Done = true;
		Now  = pages > 0 && !Latest.empty() ? State::Done : State::Failed;
		if (Threads) (void)Threads->runOnUiThread(Context, Refresh, nullptr);
	}).detach();
}

// The newer version on the Hub of a running plugin, or "".
std::string UpdateFor(const std::string& slug, const std::string& version) {
	if (!Done) return {};
	std::lock_guard<std::mutex> hold(Lock);
	const auto                  it = Latest.find(slug);
	return it != Latest.end() && Newer(it->second, version) ? it->second : std::string();
}

// Whether the Hub lists the plugin at this version or an older one.
bool UpToDate(const std::string& slug, const std::string& version) {
	if (Now != State::Done) return false;
	std::lock_guard<std::mutex> hold(Lock);
	const auto                  it = Latest.find(slug);
	return it != Latest.end() && !Newer(it->second, version);
}

// The check's state, for the list's header.
std::string StateText() {
	switch (Now.load()) {
	case State::Checking: return "checking updates...";
	case State::Failed: return "update check failed";
	case State::Done: return "up to date";
	default: return {};
	}
}

void Open(const std::string& slug) {
	const std::wstring url = L"https://d2rloader.net/hub/plugins/" + Wide(slug);
	ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
} // namespace Hub

enum class Mode { None, Toml, Menu, Window };

struct Entry {
	std::string  id, name, version, author;
	std::wstring config;
	Mode         mode = Mode::None;
	std::string  slug; // on the D2RLoader Hub
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
	for (auto& e : list) {
		e.mode = WindowOf(e.id) ? Mode::Window : ClientIndex(e.id) >= 0 ? Mode::Menu : !e.config.empty() ? Mode::Toml : Mode::None;
		const int client = ClientIndex(e.id);
		e.slug = client >= 0 && !Clients[static_cast<std::size_t>(client)].hub.empty() ? Clients[static_cast<std::size_t>(client)].hub : Hub::SlugOf(e.name);
	}
	// I Hate TOMLs first, then by name
	std::sort(list.begin(), list.end(), [](const Entry& a, const Entry& b) {
		const bool selfA = a.id == PluginId, selfB = b.id == PluginId;
		return selfA != selfB ? selfA : _stricmp(a.name.c_str(), b.name.c_str()) < 0;
	});
	return list;
}

// ---- "N plugins can be updated" in the chat ----
// Once per launch, when the first game's player is ready (or when the Hub
// answers after that), a local notice on the chat's current channel, as Loot
// Filter Plus prints its drop lines: the game's own ChatData push.
namespace Chat {
constexpr std::uint64_t InstanceRva = 0x85E5C0;  // ChatData* ChatData_Instance() (3.3.93787 = 94995)
constexpr std::uint8_t  InstanceStart[] { 0x48, 0x83, 0xEC, 0x28, 0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00, 0x8B, 0x0D, 0xF5, 0x5E, 0xD5, 0x02, 0xBA, 0x6C, 0x12, 0x00, 0x00 };
constexpr std::uint64_t PushRva = 0x3E2B7F0;     // thunk of ChatData_PushEntry
constexpr std::uintptr_t KeyOffset = 0xC3AC0;    // the current channel's key: type, then its name
struct BcString {
	const char*   data;
	std::size_t   size;
	std::uint64_t capacity; // bit 63: not owned by the game
	char          inline_[16];
};
struct Key {
	std::int32_t type, pad;
	BcString     name;
};
struct Option {
	std::uint64_t value;
	bool          has;
	std::uint8_t  pad[7];
};
using InstanceFn = void* (*)() noexcept;
using PushFn     = void (*)(void*, const BcString*, int, bool, const Key*, Option*, Option*, Option*, Option*, bool) noexcept;

bool Readable(std::uintptr_t at, std::size_t size) {
	MEMORY_BASIC_INFORMATION info {};
	if (!VirtualQuery(reinterpret_cast<const void*>(at), &info, sizeof(info)) || info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
	return at + size <= reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
}

std::atomic<bool> PlayerReady {}, Told {}, InGame {}; // InGame: from the player ready to leaving the game

void Print(const std::string& text) {
	const auto base = static_cast<std::uintptr_t>(Context->exeBase);
	if (!base || !Readable(base + InstanceRva, sizeof(InstanceStart)) || std::memcmp(reinterpret_cast<const void*>(base + InstanceRva), InstanceStart, sizeof(InstanceStart)) != 0
	    || !Readable(base + PushRva, 2) || reinterpret_cast<const std::uint8_t*>(base + PushRva)[0] != 0xFF || reinterpret_cast<const std::uint8_t*>(base + PushRva)[1] != 0x25) {
		Log(true, "chat: the game's chat is not where this build expects it; %s", text.c_str());
		Say("%s", text.c_str());
		return;
	}
	void* chat = reinterpret_cast<InstanceFn>(base + InstanceRva)();
	if (!chat) return;
	const BcString line { text.data(), text.size(), 0x8000000000000000ull, {} };
	Key            key {};
	const auto     at = reinterpret_cast<std::uintptr_t>(chat) + KeyOffset;
	if (Readable(at, 8 + sizeof(BcString))) std::memcpy(&key.type, reinterpret_cast<const void*>(at), 4), std::memcpy(&key.name, reinterpret_cast<const void*>(at + 8), 24);
	key.name.capacity |= 0x8000000000000000ull; // borrowed: never freed through this copy
	if (!key.name.data) key.name.size = 0, key.name.data = key.name.inline_;
	Option none[4] {};
	reinterpret_cast<PushFn>(base + PushRva)(chat, &line, 1, false, &key, &none[0], &none[1], &none[2], &none[3], false);
}

int UpdateCount();

// UI thread: once both the player and the Hub's answer are there.
void Try() {
	if (Told || !PlayerReady || Hub::Now == Hub::State::Checking) return;
	Told = true;
	const int count = UpdateCount();
	Log(false, "chat: %d plugins can be updated", count);
	if (count > 0 && Hub::Wanted("chat_updates")) Print(std::to_string(count) + (count == 1 ? " plugin can be updated" : " plugins can be updated"));
}

void __cdecl OnPlayerReady(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept {
	PlayerReady = true;
	InGame      = true;
	D2RGui::LeftPanel::InGame(true);
	Try();
}

void __cdecl OnGameLeft(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept {
	InGame = false;
	D2RGui::LeftPanel::InGame(false);
}
} // namespace Chat

int Chat::UpdateCount() {
	int count = 0;
	for (const auto& e : Entries())
		if (!Hub::UpdateFor(e.slug, e.version).empty()) ++count;
	return count;
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

// Typing: the game's own text boxes don't get the keys in a plugin's panel
// (they go to the game's key bindings), so, as Grail On! does, a keyboard
// hook takes them while a box is active (see "typing" below). The box being
// typed in: "search", "f<field>" (a TOML value), "i<field>.<item>" (a TOML
// list item) or "s<setting>" (a joined plugin's text).
std::string       TypingKey;    // UI thread
std::string       TypingBefore; // its text when typing began (Esc puts it back)
std::string       ClientDraft;  // a joined plugin's text being typed

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

// I Hate TOMLs' own page in its list: its settings, kept in i-hate-tomls.toml.
namespace Self {
constexpr const char* Schema = R"(title = "I Hate TOMLs"
[[setting]]
type = "header"
label = "Updates"
[[setting]]
key = "check_updates"
type = "bool"
label = "Check the D2RLoader Hub for updates"
note = "At each launch, in the background. Plugins with a newer version on the Hub get a button in the list."
[[setting]]
key = "chat_updates"
type = "bool"
label = "Say in the chat when updates are out"
note = "Once per launch, when the first game starts: how many plugins can be updated."
[[setting]]
type = "header"
label = "Config copies"
[[setting]]
type = "text"
label = "Every plugin config is copied to <game>\d2rloader\I Hate TOMLs\<mod>\ and written back at each start, so a D2RMM install does not reset your settings."
)";

bool __cdecl Get(const char* key, char* out, uint32_t size, void*) noexcept {
	if (!key || (std::strcmp(key, "check_updates") != 0 && std::strcmp(key, "chat_updates") != 0)) return false;
	std::snprintf(out, size, "%s", Hub::Wanted(key) ? "true" : "false");
	return true;
}

bool __cdecl Set(const char* key, const char* value, char* error, uint32_t size, void*) noexcept {
	if (!key || !value || (std::strcmp(key, "check_updates") != 0 && std::strcmp(key, "chat_updates") != 0) || !Context->pluginConfigPath) return false;
	const std::string wanted = Unquote(Trim(value)) == "true" ? "true" : "false";
	std::string       text;
	(void)Backup::ReadAll(Context->pluginConfigPath, text);
	const auto doc = TomlDoc::Parse(text);
	bool       put = false;
	if (doc.error.empty())
		for (const auto& item : doc.items)
			if (item.kind == TomlDoc::Kind::Key && item.text == key) {
				text.replace(item.value.start, item.value.end - item.value.start, wanted);
				put = true;
				break;
			}
	if (!put) {
		if (!doc.error.empty()) text.clear();
		if (!text.empty() && text.back() != '\n') text += '\n';
		text += std::strcmp(key, "check_updates") == 0 ? "# Check the D2RLoader Hub for plugin updates at each launch.\ncheck_updates = " + wanted + "\n"
		                                               : "# Say in the chat, once per launch, how many plugins can be updated.\nchat_updates = " + wanted + "\n";
	}
	if (!Backup::WriteAll(Context->pluginConfigPath, text)) {
		std::snprintf(error, size, "could not write i-hate-tomls.toml");
		return false;
	}
	if (std::strcmp(key, "check_updates") == 0 && wanted == "true" && Hub::Now == Hub::State::Off) Hub::Start(); // at once, no restart needed
	return true;
}

void Join() {
	const PluginMenu::ClientInfo info { .structSize = sizeof(PluginMenu::ClientInfo), .schemaToml = Schema, .get = Get, .set = Set };
	(void)RegisterClient(Context, &info);
}
} // namespace Self

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
			    if (!MenuOpen()) WatchMouse(), D2RGui::LeftPanel::Opening();
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

// A text's width in layout units, Medium (labels, values) or Small (the game's font, see D2R GUI).
int TextWidth(std::string_view text, bool medium = true) { return Gui::Font::TextWidth(text, medium); }

// Lines comment text takes in the comment box.
int Lines(const std::string& text) { return Gui::Font::WrappedLines(text, ContentRight - ContentLeft, NoteChars); }

// Sizes text for the profile in use (Low Vision Mode may have changed).
void RefreshText() {
	Gui::Font::Refresh();
	if (!Gui::Font::Measured()) return;
	NoteLine  = std::max(34, Gui::Font::LineHeight(false) + 1);
	LabelLine = std::max(50, Gui::Font::LineHeight(true) + 8);
}


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
			const bool  boxed = item.text.find("\xE2\x94") != std::string::npos || item.text.find("\xE2\x95") != std::string::npos;
			std::string line  = GameText(item.text);
			if (boxed) line = Trim(line);
			if (boxed && line.empty()) break; // a rule line only
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
			if (!item.comment.empty()) rows.push_back({ .type = TomlRow::Type::Note, .height = Lines(GameText(item.comment)) * NoteLine + 12, .text = GameText(item.comment) });
			break;
		}
		case TomlDoc::Kind::Key: {
			flush();
			const auto& f = Toml.fields[static_cast<std::size_t>(field)];
			// a check box is at the row's end: its key has the room before it
			const int  room    = f.editor == TomlEdit::Editor::Check ? ContentRight - ContentLeft - 90 : TomlControl - ContentLeft - 16;
			const bool stacked = TextWidth(item.text) > room;
			const std::string note  = GameText(item.comment);
			const int         notes = note.empty() ? 0 : Lines(note);
			rows.push_back({ .type = TomlRow::Type::Field, .field = field, .height = FieldHeight + (stacked ? LabelLine : 0) + notes * NoteLine, .text = note, .stacked = stacked, .noteLines = notes });
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

// ---- layout (D2R GUI pieces) ----

using Gui::Text;
using Gui::HitButton;
using Gui::Check;

// A button on a sprite (written into the JSON as is).
std::string Button(const std::string& name, int x, int y, const std::string& file, int normal, int pressed, int hovered, const std::string& command, const std::string& tooltip = {}) {
	return Gui::Button(name, x, y, file, { normal, pressed, hovered }, command, tooltip);
}

// A button with its text on it.
std::string TextButton(const std::string& name, int x, int y, const std::string& text, const std::string& command, const std::string& tooltip = {}) {
	return Gui::TextButton(name, x, y, ButtonWidth, BoxHeight, text, command, tooltip);
}

// A text box `width` wide.
std::string TypedBox(const std::string& name, const std::string& key, int x, int y, int width, const std::string& text, const std::string& hint, const std::string& tooltip = {}) {
	return Gui::TypedBox(name, key, x, y, width, BoxHeight, text, hint, tooltip, CharWidth);
}

namespace S = Gui::Style;
constexpr const char* LabelStyle      = S::Label;
constexpr const char* SmallLabelStyle = S::SmallLabel;
constexpr const char* ValueStyle      = S::Value;
constexpr const char* SmallValueStyle = S::SmallValue;
constexpr const char* AxisStyle       = S::Axis;
constexpr const char* SectionStyle    = S::Section;
constexpr const char* NameStyle       = S::Name;
constexpr const char* NoteStyle       = S::Note;
constexpr const char* WarnStyle       = S::Warn;
constexpr const char* CommentStyle    = S::Comment;
constexpr const char* SmallLeftStyle  = S::SmallLeft;
constexpr const char* SmallRightStyle = S::SmallRight;
constexpr const char* PluginNameStyle = S::LabelOneLine;

constexpr const char* LeftArrow  = Gui::Kit::Native::LeftArrow;
constexpr const char* RightArrow = Gui::Kit::Native::RightArrow;
constexpr const char* AddButton  = Gui::Kit::Native::AddButton;

// The scroll bar of the view shown (its steps send "step<i>").
Gui::ScrollBar Bar("", BarLeft, ScrollWidth, StepHeight, PanelX, PanelY);

std::string ScrollBar(int top, int room, int total, int scroll) { return Bar.Layout(top, room, total, scroll); }

// The rows' area: the game's ClipViewWidget cuts what it holds at its edges
// (drawing and clicks), so rows half scrolled out show their visible part.
// Its children's rects are relative to it: it starts at x 0, y `top`.
std::string Clip(int top, int room, const std::string& children) { return Gui::Clip("Rows", 0, top, BarLeft - 6, room, children); }

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
constexpr int TableTitle = 64, TableHead = 72, TableLine = 84, TableCheck = 79; // a table's title, column names, rows; the check box's size
int TableColumnWidth(const Setting& setting) { return std::min(150, (ContentRight - ContentLeft - 260) / std::max<int>(1, static_cast<int>(setting.columns.size()))); }

int RowHeightOf(const Setting& setting) {
	if (setting.kind == Kind::Text) return Lines(setting.label) * NoteLine + 12;
	if (setting.kind == Kind::Table)
		return (setting.label.empty() ? 0 : TableTitle) + TableHead + static_cast<int>(setting.rows.size()) * TableLine + (setting.note.empty() ? 0 : Lines(setting.note) * NoteLine) + 8;
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
	return Gui::ArrowButton(name, x, y, up, ArrowSize, command, tooltip);
}

std::string RowWidgets(const Client& client, const Setting& setting, int row, int y) {
	const std::string id = std::to_string(row);
	std::string       out;
	if (setting.kind == Kind::Header) return Gui::SectionTitle(id, ContentLeft, y, ContentRight - ContentLeft, setting.label, setting.hint);
	if (setting.kind == Kind::Text) return Text("Text" + id, ContentLeft, y + 4, ContentRight - ContentLeft, RowHeightOf(setting) - 8, setting.label, CommentStyle, setting.hint);
	if (setting.kind == Kind::Table) {
		// title, note, then the column names over a grid of check boxes
		int at = y;
		if (!setting.label.empty()) out += Text("Label" + id, ContentLeft, at, ContentRight - ContentLeft, TableTitle - 4, setting.label, LabelStyle, setting.hint), at += TableTitle;
		if (!setting.note.empty()) out += Text("Note" + id, ContentLeft, at - 6, ContentRight - ContentLeft, Lines(setting.note) * NoteLine, setting.note, CommentStyle), at += Lines(setting.note) * NoteLine;
		const int width = TableColumnWidth(setting), left = ContentRight - width * static_cast<int>(setting.columns.size());
		for (std::size_t c = 0; c < setting.columns.size(); ++c)
			out += Text("Col" + id + "_" + std::to_string(c), left + static_cast<int>(c) * width, at, width, TableHead - 6, setting.columnLabels[c], SmallValueStyle);
		at += TableHead;
		for (std::size_t r = 0; r < setting.rows.size(); ++r, at += TableLine) {
			const std::string cell = id + "_" + std::to_string(r);
			out += Text("RowName" + cell, ContentLeft, at, left - ContentLeft - 10, TableLine, setting.rowLabels[r], SmallLabelStyle);
			for (std::size_t c = 0; c < setting.columns.size(); ++c) {
				char value[64] {};
				const std::string key = setting.rows[r] + "." + setting.columns[c];
				const bool on = client.get(key.c_str(), value, sizeof(value), client.user) && Unquote(Trim(value)) == "true";
				out += Check("Cell" + cell + "_" + std::to_string(c), left + static_cast<int>(c) * width + (width - TableCheck) / 2, at + (TableLine - TableCheck) / 2, on,
				             "C" + id + ":" + std::to_string(r) + ":" + std::to_string(c));
			}
		}
		return out;
	}
	const std::string label = setting.label + (setting.restart ? " *" : "");
	// a position's controls start further left: its label has less room
	const int labelWidth = (setting.kind == Kind::Position ? PositionLeft : ControlLeft) - ContentLeft - 20;
	out += Text("Label" + id, ContentLeft, y, labelWidth, RowHeight - 6, label, TextWidth(label) > labelWidth ? SmallLabelStyle : LabelStyle, setting.hint);
	if (!setting.note.empty()) out += Text("Note" + id, ContentLeft, y + RowHeight - 10, ContentRight - ContentLeft, Lines(setting.note) * NoteLine, setting.note, CommentStyle);
	switch (setting.kind) {
	case Kind::Bool: out += Check("Check" + id, ContentRight - 70, y + 14, Unquote(GetValue(client, setting)) == "true", "T" + id); break;
	case Kind::Int:
	case Kind::Float: {
		out += Gui::Stepper(id, ControlLeft, y, ContentRight - ControlLeft, RowHeight - 6, ShownValue(client, setting), "L" + id, "R" + id, StepTip(setting));
		break;
	}
	case Kind::Enum: {
		// a drop-down (Grail On!'s): the choice shown, its list while open
		const int         current = EnumIndex(setting, GetValue(client, setting));
		const std::string shown   = EnumLabel(setting, current);
		const int         boxY    = y + (RowHeight - BoxHeight) / 2 - 3;
		out += Gui::DropDown::Box("Drop" + id, ControlLeft, boxY, DropWidth, BoxHeight, shown, "D" + id);
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
		if (!setting.defaultValue.empty()) out += Gui::ResetButton("Reset" + id, ContentRight - ArrowSize, top, ArrowSize, "Z" + id, "Back to " + setting.defaultValue);
		break;
	}
	case Kind::String: {
		const std::string key  = "s" + id;
		const std::string text = Gui::TextInput::Key() == key ? ClientDraft : ShownValue(client, setting);
		out += TypedBox("Input" + id, key, ControlLeft, y + 20, ContentRight - ControlLeft, text, "(empty)", "Click to type; Enter saves, Esc cancels");
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

// A round gem: what kind of settings a plugin has.
std::string ModeIcon(Mode mode) {
	static constexpr int menu[3] { 60, 200, 70 }, window[3] { 70, 120, 235 }, file[3] { 200, 200, 190 };
	return mode == Mode::Menu ? Gui::Kit::Gem("menu", menu, IconSize) : mode == Mode::Window ? Gui::Kit::Gem("window", window, IconSize) : Gui::Kit::Gem("toml", file, IconSize);
}

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
	if (Hub::Now == Hub::State::Checking || Hub::Now == Hub::State::Failed) {
		const bool failed = Hub::Now == Hub::State::Failed;
		out += Text("HubState", ContentLeft, HeaderY + 86, ContentRight - ContentLeft, 50, Hub::StateText(), failed ? WarnStyle : NoteStyle,
		            failed ? "The D2RLoader Hub could not be reached; see i-hate-tomls.log" : "Versions checked on the D2RLoader Hub at launch");
	}

	// the search bar: a clear button and the count found while something is typed
	const int barWidth = ContentRight - ContentLeft;
	out += TypedBox("Search", "search", ContentLeft, SearchY, barWidth, SearchText, "Search", "Type part of a plugin's name");
	if (!SearchText.empty()) {
		out += Text("Found", ContentRight - 330, SearchY, 240, BoxHeight, std::to_string(shown.size()) + " found", SmallRightStyle);
		out += Gui::RemoveButton("SearchClear", ContentRight - 60, SearchY + 2, 52, "searchclear", "Clear the search");
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
		out += Gui::Image("RowBack" + id, ContentLeft, y, Gui::Kit::Row(ListRowWidth, ListRowBack));
		if (e.mode != Mode::None) out += Gui::Icon("Icon" + id, ContentLeft + 34, y + (ListRowBack - IconSize) / 2, IconSize, ModeIcon(e.mode), ModeTip(e.mode));
		std::string under = e.version;
		if (!e.author.empty()) under += (under.empty() ? "by " : "  by ") + e.author;
		out += Text("Name" + id, ContentLeft + 92, y + 4, 700, 40, e.name, PluginNameStyle, e.id);
		out += Text("Under" + id, ContentLeft + 92, y + 40, 700, 30, under, SmallLeftStyle);
		if (const std::string update = Hub::UpdateFor(e.slug, e.version); !update.empty())
			out += TextButton("Update" + id, ContentRight - GearSize - 250, y + (ListRowBack - BoxHeight) / 2, update + " out", "U" + std::to_string(index), "A newer version is on the D2RLoader Hub: click to open its page");
		else if (Hub::UpToDate(e.slug, e.version))
			out += Text("UpToDate" + id, ContentRight - GearSize - 250, y, 230, ListRowBack, "up to date", SmallRightStyle, "Same version as on the D2RLoader Hub");
		const std::string open = "O" + std::to_string(index), tip = e.mode == Mode::Window ? "Open its settings window" : "Open its settings";
		if (e.mode != Mode::None) out += Gui::GearButton("Open" + id, ContentRight - GearSize - 10, y + (ListRowBack - GearSize) / 2, GearSize, open, tip);
	}
	out += Clip(ListTop, room, rows);
	out += ScrollBar(ListTop, room, total, ListScroll);
	// the icons' meaning
	{
		const Mode  modes[] { Mode::Menu, Mode::Window, Mode::Toml };
		const char* names[] { "this menu", "own window", "TOML file" };
		for (int k = 0; k < 3; ++k) {
			const int x = ContentLeft + 120 + k * 260;
			out += Gui::Image("Legend" + std::to_string(k), x, NoteY + 8, ModeIcon(modes[k]));
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
		std::string list = Gui::DropDown::List("Drop", Drop.x, Drop.y, DropWidth, TileHeight, Drop.names, Drop.current, "P" + std::to_string(OpenDrop) + ":", "dropx", PanelWidth, PanelHeight);
		// the catcher and items keep their names (DropCatch, DropItem<k>)
		out += list;
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
		Bar.Clear();
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
			out += Gui::SectionTitle(id, ContentLeft, y, ContentRight - ContentLeft, row.text);
			break;
		case TomlRow::Type::Field: {
			const auto& f    = Toml.fields[static_cast<std::size_t>(row.field)];
			std::string tip  = f.key + "\nType: " + TomlEdit::TypeName(f.type);
			if (f.editor == TomlEdit::Editor::List && f.itemTypeKnown) tip += std::string(" of ") + TomlEdit::TypeName(f.itemType) + " items";
			if (f.escaped) tip += "\nWrite a line break as \\n.";
			// a long key on its own line, the value under it
			const int labelWidth = row.stacked || f.editor == TomlEdit::Editor::Check ? ContentRight - ContentLeft - (row.stacked ? 0 : 90) : TomlControl - ContentLeft - 16;
			const int fit        = TextWidth(f.key) <= labelWidth;
			out += Text("Label" + id, ContentLeft, y + 10, labelWidth, BoxHeight, f.key, fit ? LabelStyle : SmallLabelStyle, tip);
			if (row.stacked) y += LabelLine;
			if (!row.text.empty()) out += Text("KeyNote" + id, ContentLeft, y + 10 + BoxHeight + 4, ContentRight - ContentLeft, row.noteLines * NoteLine, row.text, CommentStyle);
			switch (f.editor) {
			case TomlEdit::Editor::Check: out += Check("Check" + id, ContentRight - 70, y + 6, f.text == "true", "C" + id); break;
			case TomlEdit::Editor::Text:
			case TomlEdit::Editor::Raw: out += TypedBox("In" + id, "f" + std::to_string(row.field), TomlControl, y + 10, TextBoxWidth, f.text, "(empty)", "Click to type; Esc puts the value back"); break;
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
			out += TypedBox("In" + id, key, TomlControl, y + 5, ItemBoxWidth, f.list[static_cast<std::size_t>(row.item)], "(empty)", "Click to type; Esc puts the item back");
			out += Gui::RemoveButton("Remove" + id, ContentRight - 58, y + 7, 52, "-" + id, "Remove this item");
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
std::string ViewLayout(const std::string& panelName, const std::string& probe) {
	if (Current == View::Client && Clients.empty()) Current = View::List;
	std::string content;
	if (Current == View::List) content = ListLayout();
	else if (Current == View::Client) content = ClientLayout();
	else content = TomlLayout();
	return Gui::CharacterWindow::Layout(panelName, probe, "Plugins", "close", content);
}

// ---- the window (D2R GUI: panel pool, typing, mouse, left panel) ----

Gui::PanelPool Window;

bool MenuOpen() { return Window.IsOpen(); }

void ShowView() {
	if (!Window.Ready()) {
		Say("I Hate TOMLs is not available (see i-hate-tomls.log)");
		return;
	}
	Window.Show();
}

void StopTyping(bool keep);
void RetryReuse() { Window.RetryReuse(); }

void CloseMenu() {
	StopTyping(true);
	Window.Close();
	ConfirmRow = -1;
	SetNotice({});
	Gui::LeftPanel::Closed();
}

// The Hub's answer came: the list shows it if it is on screen.
void __cdecl Hub::Refresh(const D2RL::PluginContext*, void*) noexcept {
	Chat::Try();
	if (MenuOpen() && Current == View::List) ShowView();
}

// ---- typing ----
// The game's text boxes get no keys in a plugin's panel: D2R GUI's TextInput
// takes them while a box is active. Enter keeps the text (a joined plugin's
// text is saved then), Esc puts it back, a click anywhere else keeps it.

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

// UI thread: typing goes to box `key`.
void StartTyping(const std::string& key) {
	if (!(Gui::TextInput::Active() && TypingKey == key)) {
		StopTyping(true);
		const std::string text = BoxText(key);
		TypingKey              = key;
		TypingBefore           = text;
		if (key[0] == 's') ClientDraft = text;
	}
	Gui::TextInput::Start(key, TypingBefore, {
	    .changed = [](const std::string& text) {
		    if (TypingKey.empty()) return;
		    if (!MenuOpen()) return StopTyping(true);
		    SetBoxText(TypingKey, text);
		    ShowView();
	    },
	    .enter  = [](const std::string&) { StopTyping(true), ShowView(); },
	    .escape = [] {
		    StopTyping(false);
		    if (MenuOpen()) ShowView();
	    },
	    .away = [](const std::string&) {
		    StopTyping(true);
		    if (MenuOpen()) ShowView();
	    },
	});
}

// UI thread: the box lets the keys go; `keep` false puts its text back.
void StopTyping(bool keep) {
	Gui::TextInput::Stop();
	const std::string key = std::move(TypingKey);
	TypingKey.clear();
	if (key.empty()) return;
	if (Setting* setting = DraftSetting(key)) {
		if (keep && ClientDraft != TypingBefore) (void)SetValue(Clients[static_cast<std::size_t>(Shown)], *setting, Quote(ClientDraft));
	} else if (!keep) SetBoxText(key, TypingBefore);
	ClientDraft.clear();
}

// ---- scrolling, watching ----

int& ScrollOf() { return Current == View::List ? ListScroll : Current == View::Client ? ClientScroll : Toml.scroll; }

// UI thread, every 250 ms once the menu has opened: a game left panel opening
// closes the menu; the view moves for the menu while it is open.
void Watch() {
	MenuVisible = MenuOpen();
	if (!MenuVisible && !TypingKey.empty()) StopTyping(true);
	if (Gui::LeftPanel::Watch(MenuVisible, Window.Busy())) {
		CloseMenu();
		MenuVisible = false;
	}
}

// Once, when the menu first opens: the wheel over the left of the screen
// scrolls it, the scroll bar drags, the menu is watched.
void WatchMouse() {
	static bool started = false;
	if (started) return;
	started = true;
	Gui::WindowHook::Start({
	    .active = [] { return MenuVisible.load(); },
	    .wheel  = [](int notches) {
            if (!MenuOpen()) return;
            int&      scroll = ScrollOf();
            const int before = scroll;
            scroll           = std::clamp(scroll + notches * WheelStep, 0, std::max(0, Bar.Max()));
            if (scroll != before) ShowView();
	    },
	    .wheelRight = 0.45,
	    .watch      = Watch,
	});
	Bar.OnScroll([](int scroll) {
		if (!MenuOpen() || ScrollOf() == scroll) return;
		ScrollOf() = scroll;
		ShowView();
	});
	Bar.Attach();
}

void Toggle() {
	if (MenuOpen()) CloseMenu();
	else {
		SetNotice({});
		RefreshText();
		Current = View::List;
		WatchMouse();
		Gui::LeftPanel::Opening();
		ShowView();
	}
	MenuVisible = MenuOpen();
}

void __cdecl ToggleTask(const D2RL::PluginContext*, void*) noexcept { Toggle(); }


// ---- clicks ----


void OpenEntry(int index) {
	RefreshText();
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
	else if (command[0] == 'U') {
		const auto list  = Entries();
		const int  index = std::atoi(command.c_str() + 1);
		if (index >= 0 && index < static_cast<int>(list.size())) Hub::Open(list[static_cast<std::size_t>(index)].slug);
		return;
	}
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
		RetryReuse();
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
		case 'C': { // a table's check box: C<index>:<row>:<column>
			const auto first = command.find(':'), second = first == std::string::npos ? first : command.find(':', first + 1);
			if (second == std::string::npos) break;
			const std::size_t r = std::strtoul(command.c_str() + first + 1, nullptr, 10), c = std::strtoul(command.c_str() + second + 1, nullptr, 10);
			if (r >= setting.rows.size() || c >= setting.columns.size()) break;
			Setting cell = setting;
			cell.kind  = Kind::Bool;
			cell.key   = setting.rows[r] + "." + setting.columns[c];
			cell.label = setting.rowLabels[r] + ", " + setting.columnLabels[c];
			(void)SetValue(client, cell, Unquote(GetValue(client, cell)) == "true" ? "false" : "true");
			break;
		}
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
			RetryReuse();
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
	if (Bar.Handle(command)) return; // a scroll bar step
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

void __cdecl AfterAllLoaded(const D2RL::PluginContext*, const D2RL::Lifecycle::DataTablesLoadedEvent*, void*) noexcept {
	static bool done = false;
	if (done) return;
	done = true;
	FindRunningPlugins();
	Gui::Font::Measure();
	RefreshText();
	Hub::Start();
	Gui::Kit::Read(); // the game's art for the window
	Gui::RunOnUi([] { (void)Gui::CharacterWindow::Cover(), (void)Gui::CharacterWindow::BarCover(); }); // the covers made before the first open
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
		Self::Join(); // its own page, first in the list
	}

	if (!PanelApi || !ResourceApi || !Threads || !EventApi) {
		Log(true, "I Hate TOMLs %s: inactive, a service it needs is missing", PluginVersion);
		return true;
	}
	// The window: D2R GUI's panel pool (each view a new layout on one of
	// PoolSize panels registered now), its button clicks, its art.
	// A window of its own: GameplayLeftSlot would open the inventory with it.
	(void)Gui::Init(context, PluginId);
	Gui::Api().lowEndBilinear = true;
	Window.Register(ViewLayout, "Menu", PoolSize, D2RL::Panels::PanelFlags::CloseOnEscape);
	Window.OnShown([] { MenuVisible = true, WatchMouse(); });
	Gui::Commands::Listen(RunCommand);

	if (const auto* lifecycle = Query<D2RL::LifecycleService>("lifecycle")) {
		const D2RL::Lifecycle::DataTablesLoadedListener loaded { .structSize = sizeof(D2RL::Lifecycle::DataTablesLoadedListener), .callback = AfterAllLoaded };
		D2RL::Lifecycle::ListenerHandle                 handle {};
		(void)lifecycle->registerDataTablesLoadedListener(context, &loaded, &handle);
		const D2RL::Lifecycle::GameplayEventListener ready { .structSize = D2RL::Lifecycle::GameplayEventListenerSize, .kind = D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady, .callback = Chat::OnPlayerReady };
		if (lifecycle->registerGameplayEventListener(context, &ready, &handle) != D2RL::Lifecycle::Result::Success) Log(true, "could not listen to games starting: no chat notice of updates, the view does not move for the menu");
		const D2RL::Lifecycle::GameplayEventListener left { .structSize = D2RL::Lifecycle::GameplayEventListenerSize, .kind = D2RL::Lifecycle::GameplayEventKind::GameLeft, .callback = Chat::OnGameLeft };
		(void)lifecycle->registerGameplayEventListener(context, &left, &handle);
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
	Log(false, "I Hate TOMLs %s: ready on D2R %s, %s", PluginVersion, context->buildVersion ? context->buildVersion : "?", Window.Ready() ? "window ready" : "no window");
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	D2RGui::TextInput::Shutdown();
	D2RGui::WindowHook::Stop();
}
