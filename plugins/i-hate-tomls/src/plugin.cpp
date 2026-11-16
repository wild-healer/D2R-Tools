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
// The window is declared with the D2R GUI library's App (lib/d2r-gui app.h):
// the list as the base page, a joined plugin's settings and a TOML file as
// pages pushed over it (Back returns), each value bound to the plugin's get
// and set callbacks or to the TOML text. The library draws it in the
// character screen's frame and does the clicks, typing, scrolling and the
// game's left-panel behaviour.
//
// Opened from the Controls menu (I Hate TOMLs > Open I Hate TOMLs) or the
// console command "pluginmenu".

#include "../include/i_hate_tomls_client.h"
#include <d2r_gui.h>
#include <d2r_gui/app.h>
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
constexpr const char* PluginVersion = "1.1.1-dev-4";
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

// ---- the window: declared with D2R GUI's App (lib/d2r-gui app.h) ----

namespace Gui = D2RGui;
namespace App = D2RGui::App;

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
	std::vector<int>         needs; // per column: the column of the same row that must be ticked for it to be used, or -1
	std::vector<std::pair<std::string, std::string>> needsWritten; // `requires` as written: column, the column it needs
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
		else if (key == "requires" && value.size() >= 2 && value.front() == '{') { // { enchant = "weapon", ... }
			const auto       close = value.rfind('}');
			std::string_view inner = std::string_view(value).substr(1, close == std::string::npos ? std::string::npos : close - 1);
			while (!inner.empty()) {
				const auto        comma = inner.find(',');
				const std::string pair  = Trim(inner.substr(0, comma));
				inner                   = comma == std::string_view::npos ? std::string_view() : inner.substr(comma + 1);
				const auto        eq    = pair.find('=');
				if (eq != std::string::npos) current->needsWritten.emplace_back(Unquote(Trim(std::string_view(pair).substr(0, eq))), Unquote(Trim(std::string_view(pair).substr(eq + 1))));
			}
		}
	}
	for (auto& s : client.settings) {
		if (s.label.empty() && s.kind != Kind::Table) s.label = s.key;
		if (s.kind == Kind::Table) {
			s.rowLabels.resize(s.rows.size()), s.columnLabels.resize(s.columns.size());
			for (std::size_t r = 0; r < s.rows.size(); ++r)
				if (s.rowLabels[r].empty()) s.rowLabels[r] = s.rows[r];
			for (std::size_t c = 0; c < s.columns.size(); ++c)
				if (s.columnLabels[c].empty()) s.columnLabels[c] = s.columns[c];
			s.needs.assign(s.columns.size(), -1);
			const auto column = [&](const std::string& name) { return static_cast<int>(std::find(s.columns.begin(), s.columns.end(), name) - s.columns.begin()); };
			for (const auto& [col, need] : s.needsWritten) {
				const int c = column(col), n = column(need);
				if (c < static_cast<int>(s.columns.size()) && n < static_cast<int>(s.columns.size()) && c != n) s.needs[static_cast<std::size_t>(c)] = n;
			}
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

// A game joined (the player ready) or left.
void InGameChanged(bool in) {
	InGame = in;
	if (!in) return;
	PlayerReady = true;
	Try();
}
} // namespace Chat

int Chat::UpdateCount() {
	int count = 0;
	for (const auto& e : Entries())
		if (!Hub::UpdateFor(e.slug, e.version).empty()) ++count;
	return count;
}

// ---- the window's state ----

std::string SearchText; // the list's search bar

// Any thread: the window again with the data as it is now.
void QueueRefresh() {
	(void)Gui::RunOnUi([] { App::Refresh(); });
}

App::Page ClientPage(const std::string& plugin);

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
	QueueRefresh();
}

void __cdecl ValueChanged(uint64_t, const char*) noexcept { QueueRefresh(); }

// Another plugin's Menu.Open(): the window straight on its page.
void __cdecl OpenFor(uint64_t handle) noexcept {
	std::string plugin;
	for (const auto& c : Clients)
		if (c.handle == handle) plugin = c.plugin;
	if (plugin.empty()) return;
	(void)Gui::RunOnUi([plugin] { App::OpenOn(ClientPage(plugin)); });
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
// A joined plugin's values live behind its get and set callbacks (TOML text);
// the window reads and writes them through App bindings. A set the plugin
// refuses returns why, which the window shows in red.

std::string Read(const std::string& plugin, const std::string& key) {
	const int i = ClientIndex(plugin);
	if (i < 0) return {};
	const Client& client = Clients[static_cast<std::size_t>(i)];
	char          out[512] {};
	if (!client.get(key.c_str(), out, sizeof(out), client.user)) return {};
	return Trim(out);
}

std::string Write(const std::string& plugin, const std::string& key, const std::string& label, const std::string& value) {
	const int i = ClientIndex(plugin);
	if (i < 0) return "This plugin left the menu.";
	const Client client = Clients[static_cast<std::size_t>(i)]; // its set may register again
	char         error[256] {};
	const bool   ok = client.set(key.c_str(), value.c_str(), error, sizeof(error), client.user);
	Log(!ok, "%s: %s set to %s%s%s", plugin.c_str(), key.c_str(), value.c_str(), ok ? "" : " REFUSED: ", ok ? "" : error);
	return ok ? std::string() : label + ": " + (error[0] ? error : "not changed");
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

// The two numbers of a position ("[x, y]").
std::pair<double, double> PositionOf(const std::string& value) {
	const auto items = ArrayItems(value);
	return { items.size() > 0 ? Number(items[0], 0) : 0, items.size() > 1 ? Number(items[1], 0) : 0 };
}

// One setting of a joined plugin as a component.
App::Item SettingItem(const std::string& plugin, const Setting& s) {
	const auto get = [plugin, key = s.key] { return Read(plugin, key); };
	const auto put = [plugin, key = s.key, label = s.label](const std::string& value) { return Write(plugin, key, label, value); };
	App::Item  item;
	switch (s.kind) {
	case Kind::Header: return App::Section(s.label).Tip(s.hint);
	case Kind::Text: return App::Text(s.label).Tip(s.hint);
	case Kind::Bool:
		item = App::Check(s.label, App::Bind<bool>([get] { return Unquote(get()) == "true"; }, [put](const bool& on) { return put(on ? "true" : "false"); }));
		break;
	case Kind::Int:
	case Kind::Float:
		item = App::Number(s.label, App::Bind<double>([get, s] { return Number(get(), s.min); }, [put, s](const double& v) { return put(Decimal(std::clamp(v, s.min, s.max), s.decimals)); }), s.min, s.max)
		           .Decimals(s.decimals)
		           .Step(s.step, s.shiftStep, s.ctrlStep);
		if (!s.defaultValue.empty()) item = item.Reset([put, d = s.defaultValue] { (void)put(d); });
		break;
	case Kind::Enum: {
		std::vector<std::string> names;
		for (int k = 0; k < static_cast<int>(s.values.size()); ++k) names.push_back(EnumLabel(s, k));
		item = App::Choice(s.label,
		                   App::Bind<int>([get, s] { return EnumIndex(s, get()); },
		                                  [put, s](const int& k) { return k >= 0 && k < static_cast<int>(s.values.size()) ? put(Quote(s.values[static_cast<std::size_t>(k)])) : std::string(); }),
		                   names);
		break;
	}
	case Kind::String:
		item = App::Field(s.label, App::Bind<std::string>([get] { return Unquote(get()); }, [put](const std::string& text) { return put(Quote(text)); }), "(empty)").Draft();
		break;
	case Kind::Position: {
		const auto axis = [get, put, s](bool y) {
			return App::Bind<int>([get, y] { const auto p = PositionOf(get()); return static_cast<int>(std::lround(y ? p.second : p.first)); },
			                      [get, put, s, y](const int& v) {
				                      auto p                    = PositionOf(get());
				                      (y ? p.second : p.first) = std::clamp(static_cast<double>(v), s.min, s.max);
				                      return put("[" + Decimal(p.first, s.decimals) + ", " + Decimal(p.second, s.decimals) + "]");
			                      });
		};
		item = App::Position(s.label, axis(false), axis(true)).Step(s.step, s.shiftStep, s.ctrlStep);
		if (!s.defaultValue.empty()) item = item.Reset([put, d = s.defaultValue] { (void)put(d); });
		break;
	}
	case Kind::Table: {
		// each cell the bool "<row>.<column>"; a cell whose column needs another one unticked is greyed
		const auto on = [plugin, s](int r, int c) {
			if (r < 0 || c < 0 || r >= static_cast<int>(s.rows.size()) || c >= static_cast<int>(s.columns.size())) return false;
			return Unquote(Read(plugin, s.rows[static_cast<std::size_t>(r)] + "." + s.columns[static_cast<std::size_t>(c)])) == "true";
		};
		item = App::Table(s.label, s.columnLabels, s.rowLabels, on,
		                  [plugin, s](int r, int c, bool tick) {
			                  if (r < 0 || c < 0 || r >= static_cast<int>(s.rows.size()) || c >= static_cast<int>(s.columns.size())) return std::string();
			                  const auto rr = static_cast<std::size_t>(r), cc = static_cast<std::size_t>(c);
			                  return Write(plugin, s.rows[rr] + "." + s.columns[cc], s.rowLabels[rr] + ", " + s.columnLabels[cc], tick ? "true" : "false");
		                  })
		           .Disabled([on, s](int r, int c) {
			           const int need = c >= 0 && c < static_cast<int>(s.needs.size()) ? s.needs[static_cast<std::size_t>(c)] : -1;
			           return need < 0 || on(r, need) ? std::string() : "Needs " + s.columnLabels[static_cast<std::size_t>(need)];
		           });
		break;
	}
	case Kind::Action:
		item = App::Action(s.label, "", [put, label = s.label] {
			       const auto why = put("\"run\"");
			       App::Notice(why.empty() ? label + ": done." : why, !why.empty());
		       })
		           .Confirm(s.confirm)
		           .Arrow();
		break;
	}
	return item.Tip(s.hint).Note(s.note).Restart(s.restart);
}

// A joined plugin's page: its settings, read again at each view.
App::Page ClientPage(const std::string& plugin) {
	const int i = ClientIndex(plugin);
	return {
		.title   = i >= 0 ? Clients[static_cast<std::size_t>(i)].title : plugin,
		.content = [plugin] {
			std::vector<App::Item> out;
			const int              at = ClientIndex(plugin);
			if (at < 0) return std::vector<App::Item> { App::Text("This plugin left the menu.") };
			const auto settings = Clients[static_cast<std::size_t>(at)].settings; // a set may change Clients
			if (settings.empty()) out.push_back(App::Text("This plugin has no settings to show."));
			for (const auto& s : settings) out.push_back(SettingItem(plugin, s));
			return out;
		},
		.backTip = "All plugins",
	};
}

// ---- a plugin's TOML config ----

struct TomlConfig {
	std::string                  plugin, name;
	std::wstring                 path;
	TomlDoc::Document            doc;
	std::vector<TomlEdit::Field> fields;
};

TomlConfig Toml;

bool JoinsPrevious(const std::string& previous, const std::string& line) {
	if (previous.size() < 50 || line.empty()) return false;
	const char last = previous.back(), first = line.front();
	if (last == '.' || last == ':' || last == '!' || last == '?') return false;
	if (first == ' ' || first == '\t' || first == '-' || first == '*' || first == '#' || first == '|' || first == '=' || first == '[') return false;
	if (static_cast<unsigned char>(first) >= 0x80) return false; // box-drawing lines and the like
	return true;
}

// Reads the file again; false with the reason.
bool LoadToml(std::string& why) {
	std::string bytes;
	if (!ReadWholeFile(Toml.path, bytes)) {
		Toml.doc       = {};
		Toml.doc.error = "the file could not be read";
		Toml.fields.clear();
		return why = Toml.doc.error, false;
	}
	Toml.doc    = TomlDoc::Parse(std::move(bytes));
	Toml.fields = Toml.doc.error.empty() ? TomlEdit::Fields(Toml.doc) : std::vector<TomlEdit::Field>();
	if (!Toml.doc.error.empty()) Toml.doc.items.clear();
	TomlEdit::Mark(Toml.doc, Toml.fields);
	if (!Toml.doc.error.empty()) return why = "this file is not valid TOML (" + Toml.doc.error + ")", false;
	return true;
}

void ApplyToml() {
	std::string disk;
	if (!ReadWholeFile(Toml.path, disk)) return App::Notice("The file could not be read.", true);
	if (disk != Toml.doc.bytes) return App::Notice("The file changed since it was opened: click Reload first.", true);
	std::string out, why;
	int         changed = 0;
	if (!TomlEdit::Apply(Toml.doc, Toml.fields, out, why, changed)) return App::Notice(why, true);
	if (!changed) return App::Notice("Nothing to apply.");
	// the file as it was, beside it
	if (!WriteWholeFile(Toml.path + L".bak", disk)) return App::Notice("Could not write the backup (" + Narrow(FileName(Toml.path)) + ".bak), so nothing was saved.", true);
	if (!WriteWholeFile(Toml.path, out)) return App::Notice("Could not save the file.", true);
	Log(false, "%s: %d value(s) saved to %s", Toml.plugin.c_str(), changed, Narrow(Toml.path).c_str());
	std::string reason;
	(void)LoadToml(reason);
	App::Notice("Saved " + std::to_string(changed) + (changed == 1 ? " change" : " changes") + ". Restart the game to apply.");
}

void ReloadToml() {
	std::string why;
	if (LoadToml(why)) App::Notice("Read again from the file.");
	else App::Notice(why, true);
}

TomlEdit::Field* FieldAt(int field) { return field >= 0 && field < static_cast<int>(Toml.fields.size()) ? &Toml.fields[static_cast<std::size_t>(field)] : nullptr; }

// A TOML text box's binding: written at every key, Esc puts the old text back.
App::Bind<std::string> TomlText(int field, int item) {
	const auto at = [field, item]() -> std::string* {
		auto* f = FieldAt(field);
		if (!f) return nullptr;
		if (item < 0) return &f->text;
		return item < static_cast<int>(f->list.size()) ? &f->list[static_cast<std::size_t>(item)] : nullptr;
	};
	return { [at] { const auto* t = at(); return t ? *t : std::string(); },
		     [at](const std::string& text) {
			     if (auto* t = at()) *t = text;
			     return std::string();
		     } };
}

// The file's rows: its comments as text, its tables as sections, its keys with their controls.
std::vector<App::Item> TomlItems() {
	std::vector<App::Item>   out;
	std::vector<std::string> paragraph;
	const auto               flush = [&] {
        // a long comment is cut into parts that each fit the window
        constexpr int most = 18;
        std::string   text;
        int           lines = 0;
        for (const auto& line : paragraph) {
            const int count = Gui::Font::WrappedLines(line, 939, 55, false);
            if (lines && lines + count > most) out.push_back(App::Text(text)), text.clear(), lines = 0;
            text += (text.empty() ? "" : "\n") + line;
            lines += count;
        }
        if (!text.empty()) out.push_back(App::Text(text));
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
			out.push_back(App::Section(title));
			if (!item.comment.empty()) out.push_back(App::Text(GameText(item.comment)));
			break;
		}
		case TomlDoc::Kind::Key: {
			flush();
			const int idx = field++;
			const auto* f = FieldAt(idx);
			if (!f) break;
			std::string tip = f->key + "\nType: " + TomlEdit::TypeName(f->type);
			if (f->editor == TomlEdit::Editor::List && f->itemTypeKnown) tip += std::string(" of ") + TomlEdit::TypeName(f->itemType) + " items";
			if (f->escaped) tip += "\nWrite a line break as \\n.";
			const std::string note = GameText(item.comment);
			switch (f->editor) {
			case TomlEdit::Editor::Check:
				out.push_back(App::Check(f->key, App::Bind<bool>([idx] { const auto* g = FieldAt(idx); return g && g->text == "true"; },
				                                                 [idx](const bool& on) {
					                                                 if (auto* g = FieldAt(idx)) g->text = on ? "true" : "false";
					                                                 return std::string();
				                                                 }))
				                  .Tip(tip)
				                  .Note(note));
				break;
			case TomlEdit::Editor::Text:
			case TomlEdit::Editor::Raw: out.push_back(App::Field(f->key, TomlText(idx, -1), "(empty)").Tip(tip + "\nClick to type; Esc puts the value back").Note(note)); break;
			case TomlEdit::Editor::ReadOnly: out.push_back(App::Value(f->key, "(on several lines: edit it in the file)").Tip(f->text).Note(note)); break;
			case TomlEdit::Editor::List: {
				const auto count = f->list.empty() ? std::string("empty list") : std::to_string(f->list.size()) + (f->list.size() == 1 ? " item" : " items");
				out.push_back(App::Adder(f->key, count, [idx] {
					              if (auto* g = FieldAt(idx)) g->list.emplace_back();
				              }).Note(note));
				for (int k = 0; k < static_cast<int>(f->list.size()); ++k)
					out.push_back(App::Field(std::to_string(k + 1), TomlText(idx, k), "(empty)").Tip("Click to type; Esc puts the item back").Remove([idx, k] {
						if (auto* g = FieldAt(idx); g && k < static_cast<int>(g->list.size())) g->list.erase(g->list.begin() + k);
					}));
				break;
			}
			}
			break;
		}
		}
	}
	flush();
	if (out.empty() && Toml.doc.error.empty()) out.push_back(App::Text("This file is empty."));
	if (!Toml.doc.error.empty()) out.push_back(App::Text("This file could not be read as TOML:\n" + Toml.doc.error + "\nEdit it in a text editor."));
	return out;
}

// A plugin's TOML file as a page over the list, Reload and Apply under it.
void OpenToml(const std::string& plugin, const std::string& name, const std::wstring& path) {
	Toml        = {};
	Toml.plugin = plugin, Toml.name = name, Toml.path = path;
	std::string why;
	const bool  ok = LoadToml(why);
	if (!ok) Log(true, "%s: %s", Narrow(path).c_str(), why.c_str());
	App::Push({
	    .title    = name,
	    .subtitle = Narrow(FileName(path)),
	    .content  = TomlItems,
	    .footer   = [] {
            std::vector<App::Item> out { App::Action("Read the file again", "Reload", ReloadToml).Tip("Drops the changes not applied") };
            if (Toml.doc.error.empty()) out.push_back(App::Action("Save the changes to the file", "Apply", ApplyToml));
            return out;
	    },
	    .backTip  = "All plugins",
	});
	App::Notice(ok ? "Applied changes take effect after a restart." : why, !ok);
}

// ---- the plugin list ----

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
	return mode == Mode::Menu ? Gui::Kit::Gem("menu", menu, 40) : mode == Mode::Window ? Gui::Kit::Gem("window", window, 40) : Gui::Kit::Gem("toml", file, 40);
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

// The rows the search keeps; their commands carry the plugin's index in Entries().
std::vector<App::Row> ListRows() {
	const auto            list  = Entries();
	const auto            query = SearchQuery();
	std::vector<App::Row> rows;
	for (int i = 0; i < static_cast<int>(list.size()); ++i) {
		const auto& e = list[static_cast<std::size_t>(i)];
		if (!Matches(e, query)) continue;
		App::Row row;
		row.text = e.name, row.tooltip = e.id;
		row.sub = e.version;
		if (!e.author.empty()) row.sub += (row.sub.empty() ? "by " : "  by ") + e.author;
		if (e.mode != Mode::None) {
			row.icon = ModeIcon(e.mode), row.iconTip = ModeTip(e.mode);
			row.gear = "O" + std::to_string(i), row.gearTip = e.mode == Mode::Window ? "Open its settings window" : "Open its settings";
		}
		if (const std::string update = Hub::UpdateFor(e.slug, e.version); !update.empty())
			row.button = update + " out", row.buttonCommand = "U" + std::to_string(i), row.buttonTip = "A newer version is on the D2RLoader Hub: click to open its page";
		else if (Hub::UpToDate(e.slug, e.version)) row.value = "up to date";
		rows.push_back(std::move(row));
	}
	return rows;
}

std::vector<App::Item> ListItems() {
	return { App::List(ListRows).Empty(Entries().empty() ? "No other plugin was found." : "No plugin matches the search.") };
}

// Over the list: the count, the Hub check, the search bar.
std::vector<App::Item> ListHeader() {
	if (App::Depth() > 0) return {};
	const auto list  = Entries();
	const auto query = SearchQuery();
	const auto found = std::count_if(list.begin(), list.end(), [&](const Entry& e) { return Matches(e, query); });
	std::vector<App::Item> out { App::Section(std::to_string(list.size()) + (list.size() == 1 ? " plugin running" : " plugins running")) };
	if (Hub::Now == Hub::State::Checking) out.push_back(App::Text(Hub::StateText()).Tip("Versions checked on the D2RLoader Hub at launch"));
	else if (Hub::Now == Hub::State::Failed)
		out.push_back(App::Custom(52, [](const App::Place& p) { return Gui::Text(p.name, p.x, p.y, p.width, 48, Hub::StateText(), Gui::Style::Warn, "The D2RLoader Hub could not be reached; see i-hate-tomls.log"); }));
	out.push_back(App::Field("", App::Bind<std::string>([] { return SearchText; }, [](const std::string& text) { SearchText = text; return std::string(); }), "Search")
	                  .Clearable()
	                  .Tip("Type part of a plugin's name"));
	if (!SearchText.empty()) out.push_back(App::Text(std::to_string(found) + " found"));
	return out;
}

// Under the list: what the gems mean.
std::vector<App::Item> ListFooter() {
	if (App::Depth() > 0) return {};
	return { App::Custom(56, [](const App::Place& p) {
		const Mode  modes[] { Mode::Menu, Mode::Window, Mode::Toml };
		const char* names[] { "this menu", "own window", "TOML file" };
		std::string out;
		for (int k = 0; k < 3; ++k) {
			const int x = p.x + 120 + k * 260;
			out += Gui::Image(p.name + "G" + std::to_string(k), x, p.y + 8, ModeIcon(modes[k]));
			out += Gui::Text(p.name + "T" + std::to_string(k), x + 50, p.y + 4, 200, 48, names[k], Gui::Style::SmallLeft);
		}
		return out;
	}) };
}

// A row's gear ("O<index>") or update button ("U<index>").
bool RunCommand(const std::string& command) {
	if (command.size() < 2 || (command[0] != 'O' && command[0] != 'U')) return false;
	const auto list  = Entries();
	const int  index = std::atoi(command.c_str() + 1);
	if (index < 0 || index >= static_cast<int>(list.size())) return false;
	const auto& e = list[static_cast<std::size_t>(index)];
	if (command[0] == 'U') {
		Hub::Open(e.slug);
		return false;
	}
	switch (e.mode) {
	case Mode::Window:
		if (const OwnWindow* window = WindowOf(e.id)) {
			const auto open = window->open;
			void*      user = window->user;
			App::Close();
			open(user);
		}
		break;
	case Mode::Menu: App::Push(ClientPage(e.id)); break;
	case Mode::Toml: OpenToml(e.id, e.name, e.config); break;
	default: break;
	}
	return false;
}

// The Hub's answer came: the list shows it if it is on screen.
void __cdecl Hub::Refresh(const D2RL::PluginContext*, void*) noexcept {
	Chat::Try();
	App::Refresh();
}

// ---- after every plugin has loaded ----

void Loaded() {
	FindRunningPlugins();
	Hub::Start();
	int counts[4] {};
	for (const auto& e : Entries()) ++counts[static_cast<int>(e.mode)];
	Log(false, "every plugin has loaded: %d in this menu, %d with their own window, %d with a TOML file, %d without settings", counts[static_cast<int>(Mode::Menu)],
	    counts[static_cast<int>(Mode::Window)], counts[static_cast<int>(Mode::Toml)], counts[static_cast<int>(Mode::None)]);
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

	Threads = Query<D2RL::ThreadService>("thread");
	if (Threads && !D2RL::HasThreadServiceField(Threads, D2RL::ThreadServiceRequiredSize)) Threads = nullptr;

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

	// The window, declared: D2R GUI's App does its panels, clicks, typing,
	// scrolling, the left-panel behaviour, the key binding and the console command.
	Gui::Api().lowEndBilinear = true;
	const bool ready          = App::Start(context, PluginId,
	                                       {
	                                           .title       = "Plugins",
	                                           .frame       = App::Frame::Character,
	                                           .content     = ListItems,
	                                           .header      = ListHeader,
	                                           .footer      = ListFooter,
	                                           .console     = "pluginmenu",
	                                           .keybind     = "Open I Hate TOMLs",
	                                           .keyCategory = "I Hate TOMLs",
	                                           .views       = 60,
	                                           .command     = RunCommand,
	                                           .loaded      = Loaded,
	                                           .inGame      = Chat::InGameChanged,
	                                       });
	Log(!ready, "I Hate TOMLs %s: %s on D2R %s", PluginVersion, ready ? "ready" : "inactive, a service it needs is missing", context->buildVersion ? context->buildVersion : "?");
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	App::Stop();
}
