// Stash Tab Dropdown: jump straight to a shared stash page from a list
// instead of paging with the arrows.
//
// Built on documented D2RLoader services, with no native hooks, and only on
// the parts of those services that D2RLoader 1.3.1 already has. It reads the
// stash's tab and current page from the game (as Shared Stash Page Text
// does), which needs the stash code of Steam 3.3.93787 / Battle.net
// 3.3.93847: elsewhere there is no list button and only the stash-goto
// console command.
//
//   - A small panel of the plugin's own ("StashButton") holds one button by
//     the stash's page arrows, opened while the game shows the shared tab. It
//     opens the "PageList" panel through the stock
//     "PanelManager:TogglePanel" message.
//   - PageList is a small panel under that button, built when the plugin loads
//     with one small square button per page showing the page number in the
//     page's colour; the page's name (from this plugin's config, or from Shared
//     Stash Page Text's config) is the button's tooltip.
//   - A page button sends "PanelManager:ClosePanel:stash-tab-dropdown/Page<n>".
//     The plugin consumes that message, closes the list, and walks to page n by
//     sending the stash's own SharedStashLeft / SharedStashRight messages. The
//     arrows stop at the first and last page (only keybinds cycle), so a jump
//     to the first or last page sends a full page count of steps: it lands
//     there whatever the plugin thought the current page was.
//   - The current page is read from the game where the build is known (see
//     ReadGamePage). Elsewhere it is tracked from those same arrow messages,
//     starting at page 1 whenever the stash panel is loaded for a new game;
//     stash keybinds send no UI message, so pages they turn are missed there.

#include <D2RLPlugin/api.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace Widgets = D2RL::Widgets;
namespace Panels  = D2RL::Panels;
namespace Events  = D2RL::SharedEvents;

constexpr const char* PluginId      = "stash-tab-dropdown";
constexpr const char* PluginVersion = "1.4.1";
constexpr const char* ListPanelName = "stash-tab-dropdown/PageList";
// Button messages. Neither may be a prefix of a panel name ("stash-tab-dropdown/
// PageList..."): the listener would take that panel's open message for a click.
constexpr const char* PagePrefix    = "stash-tab-dropdown/GoTo";
constexpr const char* GroupPrefix   = "stash-tab-dropdown/Screen";
constexpr int         MaxGroups     = 60; // D2RLoader allows 64 panels per plugin
constexpr int         MaxPages      = 1000; // D2RLoader's limit for the shared stash
constexpr int         VanillaPages  = 5;    // RotW's shared pages before D2RLoader adds more
constexpr const char* StashPanel    = "BankExpansionLayout";

const D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = PluginId,
	.name        = "Stash Tab Dropdown",
	.version     = PluginVersion,
	.author      = "wild-healer",
	.description = "Replaces the shared stash page arrows with a list that jumps straight to a page.",
	.flags       = D2RL::PluginFlags::Client,
};

// ---- services ----

const D2RL::PluginContext*      Context {};
const D2RL::ThreadService*      Threads {};
const D2RL::PanelService*       PanelApi {};
const D2RL::WidgetService*      WidgetApi {};
const D2RL::SharedEventService* EventApi {};

// One panel per screen of pages ("group"). The first is the one the stash
// button toggles ("PageList"); the others are "PageList2", "PageList3", ...
std::vector<Panels::RegistrationHandle> GroupHandles;

// ---- config ----

struct Page {
	std::string text;
	std::string color; // "#RRGGBB", empty = default button text colour
};

struct Settings {
	bool                  enabled       = true;
	bool                  readGamePage  = true; // read the current page from the game on known builds
	bool                  instantJump   = true; // set the page directly instead of clicking the arrows, where it is known to work
	int                   pageCount     = 40;   // read from D2RLoader's config, see ReadPageCount
	bool                  usePageText   = true; // read names from shared-stash-page-text.toml
	int                   columns       = 10;
	int                   rows          = 12;  // rows per screen; more pages get < and > buttons
	int                   listX         = 20; // stash panel coordinates: < button, then the pages, then >, right-aligned under the page arrows
	int                   listY         = 196;
	int                   columnWidth   = 132;
	int                   rowHeight     = 104;
	float                 buttonScale   = 1.0f;
	int                   buttonWidth   = 124; // size of the plugin's own button sprite
	int                   buttonHeight  = 96;
	int                   fontSize      = 32;
	std::string           buttonSprite;  // empty = the plugin's own sprite (OwnSprite), 124 x 96 scaled by list_scale
	int                   normalFrame   = 0;
	int                   hoveredFrame  = 1;
	int                   pressedFrame  = 2;
	int                   stepsPerFrame = 0; // 0 = enough for any jump to take about 40 frames
	bool                  keepOpen      = false; // the list stays open after picking a page
	bool                  rememberOpen  = false; // with keepOpen: the list reopens if it was left open, across restarts
	bool                  stashButton   = true; // the plugin's own list button, where the game page can be read
	bool                  escapeCloses  = true;  // Escape closes the list; false: Escape goes to the game, which closes the stash
	bool                  buttonAllTabs = false; // the list button on the personal and materials tabs too
	float                 listScale     = 1.0f;  // scales the page list's squares and numbers
	int                   stashButtonX  = 1568; // stash panel coordinates: right of the right page arrow
	int                   stashButtonY  = 110;
	float                 stashButtonScale = 0.67f;
	std::map<int, Page>   pages;         // from this plugin's own config
} Config;

std::map<int, Page> PageTextPages; // from Shared Stash Page Text's config

// ---- page state (UI thread only) ----

int  CurrentPage = 1;
int  Pending     = 0; // arrow steps still to send: > 0 right, < 0 left
int  Target      = 0;
bool Walking     = false; // our own arrow messages are being sent: the listener does not count them
// D2R reports some UI messages twice (about 1 ms apart). Arrow messages closer
// together than this are one click.
constexpr ULONGLONG DuplicateMs = 30;
ULONGLONG LastArrowMs = 0;
bool      LastArrowRight = false;

// ---- helpers ----

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
	char    message[1024] {};
	va_list args;
	va_start(args, format);
	std::vsnprintf(message, sizeof(message), format, args);
	va_end(args);
	Context->WriteConsoleMessage(message);
}

std::string Trim(std::string_view text) {
	const auto begin = text.find_first_not_of(" \t\r");
	if (begin == std::string_view::npos) return {};
	const auto end = text.find_last_not_of(" \t\r");
	return std::string(text.substr(begin, end - begin + 1));
}

bool ParseString(std::string_view text, std::size_t& at, std::string& output) {
	if (at >= text.size() || text[at] != '"') return false;
	output.clear();
	for (++at; at < text.size(); ++at) {
		const char c = text[at];
		if (c == '"') {
			++at;
			return true;
		}
		if (c == '\\' && at + 1 < text.size()) {
			const char e = text[++at];
			output += e == 'n' ? '\n' : e == 't' ? '\t' : e;
		} else output += c;
	}
	return false;
}

bool ParseBool(const std::string& value, bool fallback) {
	if (value.rfind("true", 0) == 0) return true;
	if (value.rfind("false", 0) == 0) return false;
	return fallback;
}

int ParseInt(const std::string& value, int fallback, int low, int high) {
	char*      end    = nullptr;
	const long parsed = std::strtol(value.c_str(), &end, 10);
	if (end == value.c_str()) return fallback;
	return static_cast<int>(std::clamp<long>(parsed, low, high));
}

float ParseFloat(const std::string& value, float fallback, float low, float high) {
	char*       end    = nullptr;
	const float parsed = std::strtof(value.c_str(), &end);
	if (end == value.c_str()) return fallback;
	return std::clamp(parsed, low, high);
}

// Reads the [pages.N] text/color tables shared by this plugin's config and
// Shared Stash Page Text's. Calls setting(key, value) for every other key.
template <typename Setting>
void ParseToml(std::string_view text, std::map<int, Page>& pages, Setting setting) {
	int         page      = 0;
	std::size_t lineStart = 0;
	while (lineStart < text.size()) {
		auto lineEnd = text.find('\n', lineStart);
		if (lineEnd == std::string_view::npos) lineEnd = text.size();
		const std::string line = Trim(text.substr(lineStart, lineEnd - lineStart));
		lineStart              = lineEnd + 1;
		if (line.empty() || line[0] == '#') continue;
		if (line[0] == '[') {
			page = line.rfind("[pages.", 0) == 0 ? ParseInt(line.substr(7), 0, 0, 1000) : 0;
			continue;
		}
		const auto equals = line.find('=');
		if (equals == std::string::npos) continue;
		const std::string key   = Trim(std::string_view(line).substr(0, equals));
		const std::string value = Trim(std::string_view(line).substr(equals + 1));
		std::size_t       at    = 0;
		std::string       parsed;
		if (page > 0) {
			if (key == "text" && ParseString(value, at, parsed)) pages[page].text = parsed;
			else if (key == "color" && ParseString(value, at, parsed)) pages[page].color = parsed;
			else setting(key, value); // a setting added below the page names still counts
		} else setting(key, value);
	}
}

void LoadConfig() {
	(void)Context->EnsureConfig();
	std::uint32_t     required = 0;
	std::vector<char> buffer(16384);
	if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) {
		if (required <= buffer.size()) {
			Log(true, "config: could not read the config file, using defaults");
			return;
		}
		buffer.resize(required);
		if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) return;
	}
	const std::string_view text(buffer.data(), std::strlen(buffer.data()));
	// 1.0.0 configs carry the old big-button layout; ignore their layout keys.
	int layoutVersion = 1;
	ParseToml(text, Config.pages, [&](const std::string& key, const std::string& value) {
		if (key == "layout_version") layoutVersion = ParseInt(value, 1, 1, 100);
	});
	Config.pages.clear();
	if (layoutVersion < 9) Log(true, "config: layout settings are from an older version and are ignored; delete stash-tab-dropdown.toml to get the new ones");
	ParseToml(text, Config.pages, [&](const std::string& key, const std::string& value) {
		std::size_t at = 0;
		std::string parsed;
		if (key == "enabled") Config.enabled = ParseBool(value, Config.enabled);
		else if (key == "read_game_page") Config.readGamePage = ParseBool(value, Config.readGamePage);
		else if (key == "instant_jump") Config.instantJump = ParseBool(value, Config.instantJump);
		else if (key == "keep_open") Config.keepOpen = ParseBool(value, Config.keepOpen);
		else if (key == "remember_open") Config.rememberOpen = ParseBool(value, Config.rememberOpen);
		else if (key == "escape_closes_list") Config.escapeCloses = ParseBool(value, Config.escapeCloses);
		else if (key == "stash_button_all_tabs") Config.buttonAllTabs = ParseBool(value, Config.buttonAllTabs);
		else if (key == "list_scale") Config.listScale = ParseFloat(value, Config.listScale, 0.2f, 4.0f);
		else if (key == "stash_button") Config.stashButton = ParseBool(value, Config.stashButton);
		else if (key == "stash_button_x") Config.stashButtonX = ParseInt(value, Config.stashButtonX, -2000, 4000);
		else if (key == "stash_button_y") Config.stashButtonY = ParseInt(value, Config.stashButtonY, -2000, 4000);
		else if (key == "stash_button_scale") Config.stashButtonScale = ParseFloat(value, Config.stashButtonScale, 0.2f, 4.0f);
		else if (key == "use_page_text_names") Config.usePageText = ParseBool(value, Config.usePageText);
		else if (layoutVersion < 9) return;
		else if (key == "columns") Config.columns = ParseInt(value, Config.columns, 1, 20);
		else if (key == "rows") Config.rows = ParseInt(value, Config.rows, 1, 20);
		else if (key == "list_x") Config.listX = ParseInt(value, Config.listX, -2000, 4000);
		else if (key == "list_y") Config.listY = ParseInt(value, Config.listY, -2000, 4000);
		else if (key == "column_width") Config.columnWidth = ParseInt(value, Config.columnWidth, 50, 2000);
		else if (key == "row_height") Config.rowHeight = ParseInt(value, Config.rowHeight, 20, 1000);
		else if (key == "button_sprite" && ParseString(value, at, parsed)) Config.buttonSprite = parsed;
		else if (key == "normal_frame") Config.normalFrame = ParseInt(value, Config.normalFrame, 0, 63);
		else if (key == "hovered_frame") Config.hoveredFrame = ParseInt(value, Config.hoveredFrame, 0, 63);
		else if (key == "pressed_frame") Config.pressedFrame = ParseInt(value, Config.pressedFrame, 0, 63);
		else if (key == "steps_per_frame") Config.stepsPerFrame = ParseInt(value, Config.stepsPerFrame, 0, 1000);
	});
}

std::string ReadTextFile(const std::wstring& path) {
	FILE* file = _wfopen(path.c_str(), L"rb");
	if (!file) return {};
	std::string text;
	char        chunk[4096];
	for (std::size_t got; (got = std::fread(chunk, 1, sizeof(chunk), file)) > 0;) text.append(chunk, got);
	std::fclose(file);
	return text;
}

// remember_open keeps whether the list was left open, across game restarts,
// in stash-tab-dropdown.state next to the config.
bool RememberedOpen = false;

std::wstring StatePath() {
	std::wstring path = Context->pluginConfigPath ? Context->pluginConfigPath : L"";
	const auto   dot  = path.find_last_of(L'.');
	if (path.empty() || dot == std::wstring::npos) return {};
	return path.substr(0, dot) + L".state";
}

void LoadOpenState() {
	const std::wstring path = StatePath();
	RememberedOpen          = !path.empty() && ReadTextFile(path).rfind("open", 0) == 0;
}

void SaveOpenState(bool open) {
	RememberedOpen          = open;
	const std::wstring path = StatePath();
	if (path.empty()) return;
	if (FILE* file = _wfopen(path.c_str(), L"wb")) {
		std::fputs(open ? "open\n" : "closed\n", file);
		std::fclose(file);
	}
}

// Shared Stash Page Text keeps its page names next to our config.
void LoadPageTextNames() {
	std::wstring path = Context->pluginConfigPath ? Context->pluginConfigPath : L"";
	const auto   cut  = path.find_last_of(L"\\/");
	if (cut == std::wstring::npos) return;
	path.erase(cut + 1);
	const std::string text = ReadTextFile(path + L"shared-stash-page-text.toml");
	if (text.empty()) return;
	ParseToml(text, PageTextPages, [](const std::string&, const std::string&) {});
	Log(false, "page names: read %zu from shared-stash-page-text.toml", PageTextPages.size());
}

// The shared stash has RotW's 5 pages plus D2RLoader's add_shared_tabs
// ([d2rcore.stash] in d2rloader/config/d2rloader.toml next to the game).
// The in-game Options slider writes that file too, so it is read again each
// time the list opens. Returns 0 when it cannot be read.
int ReadPageCount() {
	wchar_t     exe[MAX_PATH] {};
	const DWORD length = GetModuleFileNameW(nullptr, exe, MAX_PATH);
	if (length == 0 || length >= MAX_PATH) return 0;
	std::wstring path(exe, length);
	path.erase(path.find_last_of(L"\\/") + 1);
	const std::string text = ReadTextFile(path + L"d2rloader\\config\\d2rloader.toml");
	std::string       section;
	std::size_t       lineStart = 0;
	while (lineStart < text.size()) {
		auto lineEnd = text.find('\n', lineStart);
		if (lineEnd == std::string::npos) lineEnd = text.size();
		const std::string line = Trim(std::string_view(text).substr(lineStart, lineEnd - lineStart));
		lineStart              = lineEnd + 1;
		if (line.empty() || line[0] == '#') continue;
		if (line[0] == '[') section = line;
		else if (const auto equals = line.find('='); section == "[d2rcore.stash]" && equals != std::string::npos && Trim(std::string_view(line).substr(0, equals)) == "add_shared_tabs")
			return VanillaPages + ParseInt(Trim(std::string_view(line).substr(equals + 1)), 0, 0, MaxPages - VanillaPages);
	}
	return 0;
}

int BuiltPages = MaxPages; // pages the registered panels hold: the page count at load

void RefreshPageCount() {
	static bool warned = false;
	const int   count  = ReadPageCount();
	if (count == 0) {
		if (!warned) Log(true, "page count: could not read add_shared_tabs from d2rloader/config/d2rloader.toml, using %d pages", Config.pageCount);
		warned = true;
		return;
	}
	if (count == Config.pageCount) return;
	Log(false, "page count: %d (was %d)", count, Config.pageCount);
	if (count > BuiltPages && BuiltPages >= Config.pageCount) Log(true, "page list: restart the game to list the pages past %d", BuiltPages);
	Config.pageCount = count;
	CurrentPage      = std::min(CurrentPage, count);
}

Page PageFor(int page) {
	Page result;
	if (const auto it = PageTextPages.find(page); it != PageTextPages.end()) result = it->second;
	if (const auto it = Config.pages.find(page); it != Config.pages.end()) {
		if (!it->second.text.empty()) result.text = it->second.text;
		if (!it->second.color.empty()) result.color = it->second.color;
	}
	return result;
}


std::string Json(const std::string& text) {
	std::string out = "\"";
	for (const char c : text) {
		if (c == '"' || c == '\\') out += '\\';
		if (static_cast<unsigned char>(c) < 0x20) continue;
		out += c;
	}
	return out + "\"";
}

// The tooltip text takes D2R's colour codes, not hex: pick the nearest of
// them. In memory a code is U+E07E and the code's character ("ÿc" is only how
// string tables spell it, and shows as is here), as for ground labels.
std::string TooltipColor(const std::string& hex) {
	unsigned int r = 0, g = 0, b = 0;
	if (hex.size() != 7 || hex[0] != '#' || std::sscanf(hex.c_str() + 1, "%02x%02x%02x", &r, &g, &b) != 3) return "";
	static constexpr struct { char code; int r, g, b; } codes[] {
		{ '0', 255, 255, 255 }, { '1', 255, 77, 77 }, { '2', 0, 255, 0 },     { '3', 105, 105, 255 }, { '4', 199, 179, 119 },
		{ '5', 105, 105, 105 }, { '8', 255, 168, 0 }, { '9', 255, 255, 100 }, { ';', 174, 0, 255 },   { ':', 0, 128, 0 },
	};
	// Nearest hue first (plain RGB distance turned pure yellow into the
	// orange code), then nearest RGB; greys only match the grey codes.
	const auto hueSat = [](int r, int g, int b, double& hue) {
		const int hi = std::max({ r, g, b }), lo = std::min({ r, g, b });
		if (hi == lo) return hue = 0, 0.0;
		const double d = hi - lo;
		hue = hi == r ? 60 * std::fmod((g - b) / d + 6, 6) : hi == g ? 60 * ((b - r) / d + 2) : 60 * ((r - g) / d + 4);
		return d / hi;
	};
	double     hue = 0;
	const bool grey = hueSat(r, g, b, hue) < 0.2;
	char       best = '0';
	double     bestScore = 1e30;
	for (const auto& c : codes) {
		double     codeHue = 0;
		const bool codeGrey = hueSat(c.r, c.g, c.b, codeHue) < 0.2;
		if (grey != codeGrey) continue;
		const double turn = grey ? 0 : std::min(std::fabs(hue - codeHue), 360 - std::fabs(hue - codeHue));
		const int    dr = c.r - static_cast<int>(r), dg = c.g - static_cast<int>(g), db = c.b - static_cast<int>(b);
		if (const double score = turn * turn * 100 + dr * dr + dg * dg + db * db; score < bestScore) bestScore = score, best = c.code;
	}
	return std::string("\xEE\x81\xBE") + best;
}

std::string ColorJson(const std::string& hex) {
	unsigned int r = 0, g = 0, b = 0;
	if (hex.size() == 7 && hex[0] == '#' && std::sscanf(hex.c_str() + 1, "%02x%02x%02x", &r, &g, &b) == 3) {
		char out[64];
		std::snprintf(out, sizeof(out), R"({"r": %u, "g": %u, "b": %u, "a": 255})", r, g, b);
		return out;
	}
	return R"("$LightButtonTextColor")";
}


int PerGroup() {
	return Config.columns * Config.rows;
}

// Screens needed for the pages the stash has now.
int GroupCount() {
	return std::max(1, (std::min(Config.pageCount, BuiltPages) + PerGroup() - 1) / PerGroup());
}

int GroupOf(int page) {
	return std::clamp((page - 1) / PerGroup(), 0, GroupCount() - 1);
}

std::string GroupLocalId(int group) {
	return group == 0 ? "PageList" : "PageList" + std::to_string(group + 1);
}

std::string GroupPanelName(int group) {
	return std::string(PluginId) + "/" + GroupLocalId(group);
}

// The plugin's own button sprite, registered as a resource at load. Layouts
// cannot stretch a sprite one way (there is only "scale"), so the page buttons
// are drawn here at the size they should be.
constexpr const char* OwnSprite     = "d2rloader\\stash-tab-dropdown\\pagebutton";
constexpr const char* OwnSpritePath = "data/hd/global/ui/d2rloader/stash-tab-dropdown/pagebutton";

// A D2R HD sprite: "SpA1", version 31, frame width, total width, height, 0,
// frame count, then 32-bit RGBA rows across all frames side by side. Frames:
// 0 normal, 1 hovered, 2 pressed. A bevelled bronze frame with a see-through
// middle, so the page backdrop and the current page's highlight show inside.
std::string BuildButtonSprite(int width, int height) {
	constexpr int frames = 3;
	std::string   out(40 + static_cast<std::size_t>(width) * frames * height * 4, '\0');
	const auto    put = [&](std::size_t at, std::uint32_t value, int bytes) {
		for (int i = 0; i < bytes; ++i) out[at + i] = static_cast<char>((value >> (8 * i)) & 0xFF);
	};
	std::memcpy(out.data(), "SpA1", 4);
	put(4, 31, 2);
	put(6, width, 2);
	put(8, width * frames, 4);
	put(12, height, 4);
	put(20, frames, 4);
	const int border = std::max(2, height / 24);
	for (int frame = 0; frame < frames; ++frame)
		for (int y = 0; y < height; ++y)
			for (int x = 0; x < width; ++x) {
				const int edge = std::min({ x, y, width - 1 - x, height - 1 - y });
				int       rgba[4] {};
				if (edge < 1) rgba[0] = 20, rgba[1] = 16, rgba[2] = 12, rgba[3] = 255;
				else if (edge < 1 + border) {
					static constexpr int colors[frames][3] { { 128, 110, 76 }, { 222, 186, 100 }, { 92, 80, 56 } };
					const bool lit   = std::min(x, y) == edge; // top and left edges catch the light
					const int  shade = frame == 2 ? (lit ? -24 : 24) : (lit ? 24 : -24);
					for (int c = 0; c < 3; ++c) rgba[c] = std::clamp(colors[frame][c] + shade, 0, 255);
					rgba[3] = 255;
				} else if (edge < 2 + border) rgba[0] = 10, rgba[1] = 8, rgba[2] = 6, rgba[3] = 255;
				else if (frame == 1) rgba[0] = 255, rgba[1] = 230, rgba[2] = 160, rgba[3] = 28;
				else if (frame == 2) rgba[3] = 90;
				const std::size_t at = 40 + (static_cast<std::size_t>(y) * width * frames + frame * width + x) * 4;
				for (int c = 0; c < 4; ++c) out[at + c] = static_cast<char>(rgba[c]);
			}
	return out;
}

// A layout margin in list units, scaled with the list (list_scale).
int Scaled(int units) {
	return std::max(1, static_cast<int>(units * Config.listScale + 0.5f));
}

// A centred label over a button's area. Page numbers are drawn with these, not
// with the button's own text, so the current page's number can get an outline:
// four black copies of it, 2 units (about a screen pixel) off each way.
std::string Label(const std::string& name, int x, int y, const std::string& text, const std::string& colorJson) {
	char rect[128];
	std::snprintf(rect, sizeof(rect), R"({"x": %d, "y": %d, "width": %d, "height": %d})", x, y, Config.columnWidth - Scaled(8), Config.rowHeight - Scaled(8));
	return R"(, {"type": "TextBoxWidget", "name": ")" + name + R"(", "fields": {"rect": )" + rect + R"(, "text": )" + Json(text) +
	       R"(, "style": {"fontColor": )" + colorJson + R"(, "pointSize": )" + std::to_string(Config.fontSize) + R"(, "alignment": {"h": "center", "v": "center"}}}})";
}

std::string SquareButton(const std::string& name, int x, int y, const std::string& text, const std::string& color, const std::string& tooltip, const std::string& message) {
	char button[320];
	std::snprintf(button, sizeof(button), R"(, {"type": "ButtonWidget", "name": "%s", "fields": {"rect": {"x": %d, "y": %d, "scale": %.3f}, "normalFrame": %d, "hoveredFrame": %d, "pressedFrame": %d, "filename": )",
	              name.c_str(), x, y, Config.buttonScale, Config.normalFrame, Config.hoveredFrame, Config.pressedFrame);
	std::string json = button;
	json += Json(Config.buttonSprite.empty() ? OwnSprite : Config.buttonSprite);
	if (!text.empty()) {
		json += R"(, "textString": )" + Json(text);
		// The button's scale also scales its text, so divide it out: font_size
		// is the size the text shows at, whatever the button's size.
		json += R"(, "pointSize": )" + std::to_string(std::max(1, static_cast<int>(Config.fontSize / Config.buttonScale + 0.5f)));
		json += R"(, "textColor": )" + ColorJson(color);
	}
	if (!tooltip.empty()) json += R"(, "tooltipString": )" + Json(TooltipColor(color) + tooltip);
	return json + R"(, "sound": "select", "onClickMessage": )" + Json("PanelManager:ClosePanel:" + message) + "}}";
}

// A dark backdrop that also stops clicks reaching the stash under it.
std::string Backdrop(const std::string& name, int x, int y, int width, int height) {
	char area[160];
	std::snprintf(area, sizeof(area), R"({"x": %d, "y": %d, "width": %d, "height": %d})", x, y, width, height);
	return R"(, {"type": "ClickCatcherWidget", "name": "Catcher)" + name + R"(", "fields": {"rect": )" + area + "}}" +
	       R"(, {"type": "RectangleWidget", "name": "Shade)" + name + R"(", "fields": {"rect": )" + area + R"(, "color": [0.0, 0.0, 0.0, 0.85]}})";
}

// A plain container, so one visibility change shows or hides all of it.
std::string Box(const std::string& name, const std::string& children) {
	return R"(, {"type": "Widget", "name": ")" + name + R"(", "children": [)" + children.substr(2) + "]}";
}

// Each list panel uses the stash panel's own rect and anchor, so its children
// use the same coordinates as the stash layout (the page arrows sit at
// 1173,97, 420 x 93). Each page is one button "Page<n>", with a highlight
// "Mark<n>" behind it shown for the page you are on; a row's highlights are in
// a box "Marks<r>", so hiding all but one takes about 20 calls, not 120 (see
// ApplyMarks). No overlapping hidden
// buttons: a hidden button on top of a visible one swallows its tooltip, which
// is why each screen of pages is a panel of its own. The panels hold the pages
// the stash has at load and nothing more: hidden widgets still take the mouse.
// Each page has its own backdrop ("CatcherP<n>", "ShadeP<n>"), so a short last
// row is only as wide as its pages. Each row is a box "Row<r>", and the < and
// > buttons are boxes "PreviousBox" and "NextBox", so ConfigureGroup needs few
// widget calls to show only what the stash needs.
std::string BuildListLayout(int group) {
	const int   first = group * PerGroup() + 1;
	const int   last  = std::min(first + PerGroup() - 1, BuiltPages);
	const int   navW  = Config.columnWidth;
	const int   gridX = Config.listX + navW;
	const int   rows  = (last - first + Config.columns) / Config.columns;
	const int   nextX = gridX + Config.columns * Config.columnWidth;
	// Backdrops start 8 units (scaled) above and left of their squares and tile exactly.
	const int   gap   = Scaled(8);
	// Only the widgets a screen uses: hidden widgets still take the mouse, so
	// the stash's items under them would get no hover. No < on the first
	// screen, no > on the last.
	std::string json;
	if (group > 0)
		json += Box("PreviousBox", Backdrop("Previous", Config.listX - gap, Config.listY - gap, navW, Config.rowHeight) +
		                               SquareButton("Previous", Config.listX, Config.listY, "<", "", "", std::string(GroupPrefix) + "Previous"));
	if (last < BuiltPages)
		json += Box("NextBox", Backdrop("Next", nextX - gap, Config.listY - gap, navW, Config.rowHeight) +
		                           SquareButton("Next", nextX, Config.listY, ">", "", "", std::string(GroupPrefix) + "Next"));
	for (int row = 0; row < rows; ++row) {
		const int   y        = Config.listY + row * Config.rowHeight;
		std::string backdrops, marks, pages;
		for (int page = first + row * Config.columns; page <= std::min(first + (row + 1) * Config.columns - 1, last); ++page) {
			const int x = gridX + ((page - first) % Config.columns) * Config.columnWidth;
			backdrops += Backdrop("P" + std::to_string(page), x - gap, y - gap, Config.columnWidth, Config.rowHeight);
			// "Mark<n>", shown for the current page: the gold highlight and the
			// number's outline.
			char rect[256];
			std::snprintf(rect, sizeof(rect), R"(, {"type": "RectangleWidget", "name": "Highlight%d", "fields": {"rect": {"x": %d, "y": %d, "width": %d, "height": %d}, "color": [0.78, 0.70, 0.47, 0.55]}})",
			              page, x - Scaled(3), y - Scaled(3), Config.columnWidth - Scaled(2), Config.rowHeight - Scaled(2));
			std::string mark   = rect;
			const auto  number = std::to_string(page);
			for (const auto& [dx, dy] : { std::pair { -2, 0 }, { 2, 0 }, { 0, -2 }, { 0, 2 } })
				mark += Label("Outline" + number + "_" + std::to_string(dx) + "_" + std::to_string(dy), x + dx, y + dy, number, R"({"r": 0, "g": 0, "b": 0, "a": 255})");
			marks += Box("Mark" + number, mark);
			pages += Label("Number" + number, x, y, number, ColorJson(PageFor(page).color));
			pages += SquareButton("Page" + number, x, y, "", PageFor(page).color, PageFor(page).text, PagePrefix + number);
		}
		json += Box("Row" + std::to_string(row), backdrops + Box("Marks" + std::to_string(row), marks) + pages);
	}
	// Every child above starts with ", ".
	return R"({"type": "Panel", "name": ")" + GroupPanelName(group) + R"(", "fields": {"priority": 120, "rect": "$LeftPanelRect_ExpandedStash", "anchor": "$LeftPanelAnchor"}, "children": [)" + json.substr(2) + "]}";
}

// ---- widgets (UI thread) ----

// Reads the stash's own "Page n" label when the loader can read widget text
// (newer than 1.3.1). Returns 0 when it cannot.
int ReadNativePage() {
	if (!WidgetApi || !D2RL::HasWidgetServiceField(WidgetApi, D2RL::WidgetServiceGetTextFieldEnd)) return 0;
	Widgets::WidgetHandle panel = Widgets::InvalidHandle, label = Widgets::InvalidHandle;
	if (WidgetApi->findPanel(Context, StashPanel, &panel) != Widgets::Result::Success) return 0;
	if (WidgetApi->findWidget(Context, panel, "CurrentSharedStashText", &label) != Widgets::Result::Success) return 0;
	char          text[256] {};
	std::uint32_t required = 0;
	if (WidgetApi->getText(Context, label, text, sizeof(text), &required) != Widgets::Result::Success) return 0;
	for (const char* p = text; *p; ++p)
		if (*p >= '0' && *p <= '9') return std::atoi(p);
	return 0;
}

// The stash's current page as the game has it: the same three game functions
// and fields Shared Stash Page Text 0.1.24 reads. They sit at the same place
// with the same code on Steam 3.3.93787 and Battle.net 3.3.93847; the code is
// checked at load (GameCodeMatches), so another build just goes without.
// Returns 0 when not known, or when the stash is not open on the shared tab.
// UI thread.
constexpr std::uint64_t StashPanelRva    = 0x846190; // void* (uint32 panel id), stash = 0x18
constexpr std::uint64_t SharedTabRva     = 0x23af50; // bool (stash panel): shared tab selected
constexpr std::uint64_t PageFieldRva     = 0x23b910; // bool (stash panel): page index at +0x170, else +0x168
bool                    GamePageKnown    = false;    // set at load from the build

// Each function's first bytes on both builds (RIP-relative offsets included,
// so the data they reach is laid out the same too).
constexpr std::uint8_t StashPanelCode[] {
	0x48, 0x83, 0xEC, 0x28, 0x48, 0x83, 0x3D, 0xD4, 0x9F, 0xBF, 0x02, 0x00, 0x74, 0x18, 0xE8, 0xFD,
	0x18, 0x88, 0xFF, 0x48, 0x8B, 0x0D, 0xC6, 0x9F, 0xBF, 0x02, 0x48, 0x8B, 0xD0, 0x48, 0x83, 0xC4,
	0x28, 0xE9, 0xAA, 0x95, 0x05, 0x00, 0x33, 0xC0, 0x48, 0x83, 0xC4, 0x28, 0xC3,
};
constexpr std::uint8_t SharedTabCode[] {
	0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8D, 0x15, 0x3F, 0x9C, 0xAA,
	0x01, 0xE8, 0x2A, 0xAD, 0x61, 0x00, 0x48, 0x8B, 0xF8, 0x48, 0x85, 0xC0, 0x74, 0x24, 0xE8, 0x7D,
	0xCE, 0x63, 0x00, 0x48, 0x8B, 0x17, 0x48, 0x8B, 0xCF, 0x48, 0x8B, 0xD8, 0xFF, 0x52, 0x58,
};
constexpr std::uint8_t PageFieldCode[] {
	0x40, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8D, 0x15, 0x7B, 0x93, 0xAA, 0x01, 0xE8, 0x6E, 0xA3,
	0x61, 0x00, 0x48, 0x8B, 0xF8, 0x48, 0x85, 0xC0, 0x74, 0x48, 0x48, 0x89, 0x5C, 0x24, 0x30, 0xE8,
	0xAC, 0x72, 0x64, 0x00, 0x48, 0x8B, 0x17, 0x48, 0x8B, 0xCF, 0x48, 0x8B, 0xD8, 0xFF, 0x52, 0x58,
};

// Logs the first bytes of the three game functions, for a build where they
// differ.
void LogGameCode() {
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(Context->exeBase);
	const auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS64*>(Context->exeBase + dos->e_lfanew);
	for (const auto& [name, rva] : { std::pair { "stash panel", StashPanelRva }, { "shared tab", SharedTabRva }, { "page field", PageFieldRva } }) {
		if (rva + 64 > nt->OptionalHeader.SizeOfImage) continue;
		char hex[64 * 3 + 1] {};
		for (int i = 0; i < 64; ++i) std::snprintf(hex + i * 3, 4, "%02X ", reinterpret_cast<const std::uint8_t*>(Context->exeBase + rva)[i]);
		Log(false, "game code %s +0x%llX: %s", name, static_cast<unsigned long long>(rva), hex);
	}
}

bool GameCodeMatches() {
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(Context->exeBase);
	const auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS64*>(Context->exeBase + dos->e_lfanew);
	const auto  at  = [&](std::uint64_t rva, const std::uint8_t* code, std::size_t size) {
		return rva + size <= nt->OptionalHeader.SizeOfImage && std::memcmp(reinterpret_cast<const void*>(Context->exeBase + rva), code, size) == 0;
	};
	return at(StashPanelRva, StashPanelCode, sizeof(StashPanelCode)) && at(SharedTabRva, SharedTabCode, sizeof(SharedTabCode)) &&
	       at(PageFieldRva, PageFieldCode, sizeof(PageFieldCode));
}

int ReadGamePage() {
	if (!GamePageKnown) return 0;
	using PanelFn = void*(__cdecl*)(std::uint32_t);
	using TestFn  = std::uint8_t(__cdecl*)(void*);
	auto* const panel = static_cast<std::uint8_t*>(reinterpret_cast<PanelFn>(Context->exeBase + StashPanelRva)(0x18));
	if (!panel || !panel[0x51]) return 0;
	if (reinterpret_cast<TestFn>(Context->exeBase + SharedTabRva)(panel) != 1) return 0;
	std::uint64_t index = 0;
	std::memcpy(&index, panel + (reinterpret_cast<TestFn>(Context->exeBase + PageFieldRva)(panel) ? 0x170 : 0x168), sizeof(index));
	return index < static_cast<std::uint64_t>(MaxPages) ? static_cast<int>(index) + 1 : 0;
}

// Instant jumps, the way Stash Search 1.10.18 switches pages: write the page
// index into the stash panel, tell D2RLoader's D2RCore.dll which shared page is
// active, and have the game reload the grid for the player. D2RCore keeps the
// game functions it resolved in a table; the jump is only used when that table
// holds exactly the functions below, which pins both the game build and the
// D2RLoader build (D2RLoader 1.3.1).
constexpr std::uint64_t PageCountRva  = 0x23b060; // uint64 (stash panel): shared pages
constexpr std::uint64_t PageRecordRva = 0x23ad80; // uint32* (stash panel): the current page's id
constexpr std::uint64_t ReloadRva     = 0x23e630; // void (stash panel, player unit): reloads the grid
constexpr std::uint64_t PlayerIdRva   = 0x8b2d0;  // uint32 (): the player's unit id
constexpr std::uint64_t UnitByIdRva   = 0x9a480;  // void* (uint32 unit id)
constexpr std::uint64_t CoreActivePage = 0x700e38; // uint32 in D2RCore.dll: the active shared page's id
constexpr std::pair<std::uint64_t, std::uint64_t> CoreTable[] {
	{ 0x7004a8, PlayerIdRva },   { 0x7004d8, UnitByIdRva },   { 0x700dc0, SharedTabRva }, { 0x700dd8, PageFieldRva },
	{ 0x700df0, PageCountRva }, { 0x700e08, PageRecordRva }, { 0x700e20, ReloadRva },
};
std::uint8_t* CoreBase    = nullptr;
bool          InstantJump = false; // set at load

bool CoreMatches() {
	CoreBase = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(L"D2RCore.dll"));
	if (!CoreBase) return false;
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(CoreBase);
	const auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS64*>(CoreBase + dos->e_lfanew);
	if (nt->OptionalHeader.SizeOfImage < CoreActivePage + sizeof(std::uint32_t)) return false;
	for (const auto& [offset, rva] : CoreTable) {
		std::uint64_t entry = 0;
		std::memcpy(&entry, CoreBase + offset, sizeof(entry));
		if (entry != Context->exeBase + rva) return false;
	}
	return true;
}

// Switches the shared stash straight to page (1-based). False when it cannot,
// with nothing changed. UI thread.
bool JumpNow(int page) {
	if (!InstantJump || page < 1) return false;
	using PanelFn  = void*(__cdecl*)(std::uint32_t);
	using TestFn   = std::uint8_t(__cdecl*)(void*);
	using CountFn  = std::uint64_t(__cdecl*)(void*);
	using RecordFn = std::uint32_t*(__cdecl*)(void*);
	using ReloadFn = void(__cdecl*)(void*, void*);
	using IdFn     = std::uint32_t(__cdecl*)();
	using UnitFn   = void*(__cdecl*)(std::uint32_t);
	const auto at  = [](std::uint64_t rva) { return Context->exeBase + rva; };
	auto* const panel = static_cast<std::uint8_t*>(reinterpret_cast<PanelFn>(at(StashPanelRva))(0x18));
	if (!panel || !panel[0x51] || reinterpret_cast<TestFn>(at(SharedTabRva))(panel) != 1) return false;
	const std::uint64_t index = static_cast<std::uint64_t>(page - 1);
	if (index >= reinterpret_cast<CountFn>(at(PageCountRva))(panel)) return false;
	void* const player = reinterpret_cast<UnitFn>(at(UnitByIdRva))(reinterpret_cast<IdFn>(at(PlayerIdRva))());
	if (!player) return false;
	std::memcpy(panel + (reinterpret_cast<TestFn>(at(PageFieldRva))(panel) ? 0x170 : 0x168), &index, sizeof(index));
	const std::uint32_t* const record = reinterpret_cast<RecordFn>(at(PageRecordRva))(panel);
	const std::uint32_t        id     = record ? *record : 0xFFFFFFFE;
	std::memcpy(CoreBase + CoreActivePage, &id, sizeof(id));
	reinterpret_cast<ReloadFn>(at(ReloadRva))(panel, player);
	return true;
}

// The stash is open, on any tab.
bool StashShown() {
	if (!GamePageKnown) return false;
	using PanelFn     = void*(__cdecl*)(std::uint32_t);
	const auto* panel = static_cast<const std::uint8_t*>(reinterpret_cast<PanelFn>(Context->exeBase + StashPanelRva)(0x18));
	return panel && panel[0x51];
}

// Clicks the stash's shared tab, the way Stash Search does: the BankTabs
// widget's select function with tab 1. Only where the instant jump is known to
// work (the same game and D2RLoader builds). UI thread.
constexpr std::uint64_t FindWidgetRva = 0x855c90; // void* (stash panel, const char* name)
constexpr std::uint64_t SelectTabRva  = 0x878fb0; // void (tabs widget, int32 tab, bool)
bool SelectSharedTab() {
	if (!InstantJump) return false;
	using PanelFn  = void*(__cdecl*)(std::uint32_t);
	using FindFn   = void*(__cdecl*)(void*, const char*);
	using SelectFn = void(__cdecl*)(void*, std::int32_t, bool);
	auto* const panel = static_cast<std::uint8_t*>(reinterpret_cast<PanelFn>(Context->exeBase + StashPanelRva)(0x18));
	if (!panel || !panel[0x51]) return false;
	void* const tabs = reinterpret_cast<FindFn>(Context->exeBase + FindWidgetRva)(panel, "BankTabs");
	if (!tabs) return false;
	reinterpret_cast<SelectFn>(Context->exeBase + SelectTabRva)(tabs, 1, false);
	return true;
}

// Where the list button and list belong: the shared tab, or with
// stash_button_all_tabs any stash tab.
bool ListPlace() {
	return Config.buttonAllTabs ? StashShown() : ReadGamePage() != 0;
}

void Resync() {
	int page = ReadGamePage();
	if (page == 0) page = ReadNativePage();
	if (page >= 1 && page <= Config.pageCount && page != CurrentPage) {
		Log(false, "page: on %d by the game (tracked %d)", page, CurrentPage);
		CurrentPage = page;
	}
}

// Widget handles are looked up once and kept: they stay valid across
// reopening, and are dropped when a new game loads the stash.
std::map<std::string, Widgets::WidgetHandle> HandleCache;

// Last visibility set per widget; widgets not in it are as the layout made them.
// Only changes are sent: showing an already shown box made D2R draw its
// buttons in their hovered frame until the mouse passed over them.
std::map<std::string, bool> Shown;

// Returns false when the widget is not there (yet). force: send it even if
// unchanged.
bool SetVisible(int group, const std::string& name, bool visible, bool force = false) {
	static bool warned = false;
	const std::string key    = GroupPanelName(group) + "/" + name;
	const auto        known  = Shown.find(key);
	const bool        before = known == Shown.end() || known->second; // the layout's "visible": false is not honoured
	if (before == visible && !force) return true;
	auto              cached = HandleCache.find(key);
	if (cached == HandleCache.end()) {
		Widgets::WidgetHandle panel = Widgets::InvalidHandle, widget = Widgets::InvalidHandle;
		auto result = WidgetApi->findPanel(Context, GroupPanelName(group).c_str(), &panel);
		if (result == Widgets::Result::Success) result = WidgetApi->findWidget(Context, panel, name.c_str(), &widget);
		if (result != Widgets::Result::Success) {
			return false;
		}
		cached = HandleCache.emplace(key, widget).first;
	}
	const auto result = WidgetApi->setWidgetVisible(Context, cached->second, visible);
	if (result == Widgets::Result::Success) Shown[key] = visible;
	else if (!warned) {
		Log(true, "page list: could not show/hide %s (result %u)", key.c_str(), static_cast<unsigned>(result));
		warned = true;
	}
	return true;
}

std::map<int, int> HiddenFrom; // per screen: its pages from this one on are hidden (0 = none)

// Shows the current page's highlight on this screen and hides the others.
// D2R shows every highlight at first, whatever the layout says, and the
// visibility set the frame a screen opens does not always stick: these calls
// are sent every time, not only on changes, and repeated a few times after a
// screen opens (MarkPasses). Highlights hold no buttons, so showing a shown
// one again changes nothing.
void ApplyMarks(int group) {
	const int first = group * PerGroup() + 1;
	const int last  = std::min(first + PerGroup() - 1, BuiltPages);
	const int mark  = CurrentPage >= first && CurrentPage <= last ? CurrentPage : 0;
	const int row   = mark != 0 ? (mark - first) / Config.columns : -1;
	const auto set  = [&](const std::string& name, bool visible) { SetVisible(group, name, visible, true); };
	for (int r = 0; r < (last - first + Config.columns) / Config.columns; ++r) set("Marks" + std::to_string(r), r == row);
	if (row < 0) return;
	for (int page = first + row * Config.columns; page <= std::min(first + (row + 1) * Config.columns - 1, last); ++page) set("Mark" + std::to_string(page), page == mark);
}

// Shows the rows of pages the stash has on this screen, hides the pages past
// the last one in its last row, moves the current page's highlight, and shows
// < and > only when there is a screen before or after this one. Returns false
// when the screen's widgets are not there yet (it was opened this frame).
bool ConfigureGroup(int group) {
	if (!WidgetApi) return true;
	// The screen's widgets are there once its first row is.
	if (const std::string key = GroupPanelName(group) + "/Row0"; !HandleCache.contains(key)) {
		Widgets::WidgetHandle panel = Widgets::InvalidHandle, widget = Widgets::InvalidHandle;
		if (WidgetApi->findPanel(Context, GroupPanelName(group).c_str(), &panel) != Widgets::Result::Success ||
		    WidgetApi->findWidget(Context, panel, "Row0", &widget) != Widgets::Result::Success)
			return false;
		HandleCache.emplace(key, widget);
	}
	SetVisible(group, "PreviousBox", group > 0); // absent where not needed
	const int count = std::min(Config.pageCount, BuiltPages);
	const int first = group * PerGroup() + 1;
	const int last  = std::min(first + PerGroup() - 1, BuiltPages);
	const int shown = std::max(0, std::min(last, count) - first + 1); // pages on this screen
	const int used  = (shown + Config.columns - 1) / Config.columns;  // rows on this screen
	for (int row = 0; row < (last - first + Config.columns) / Config.columns; ++row) SetVisible(group, "Row" + std::to_string(row), row < used);
	// In the last row, hide the pages past the count (every time: cheap, and
	// right even if D2R rebuilt the panel); show again the ones an earlier
	// count hid.
	const auto rowEnd   = [&](int page) { return std::min(first + ((page - first) / Config.columns + 1) * Config.columns - 1, last); };
	const int  hideFrom = shown % Config.columns != 0 ? first + shown : 0;
	// Pages are hidden one widget at a time, not through a box (see Shown).
	const auto showPage = [&](int page, bool visible) {
		for (const char* prefix : { "Page", "Number", "CatcherP", "ShadeP" }) SetVisible(group, prefix + std::to_string(page), visible);
	};
	if (const int from = HiddenFrom[group]; from != 0 && from != hideFrom)
		for (int page = from; page <= rowEnd(from); ++page) showPage(page, true);
	if (hideFrom != 0)
		for (int page = hideFrom; page <= rowEnd(hideFrom); ++page) showPage(page, false);
	HiddenFrom[group] = hideFrom;
	ApplyMarks(group);
	SetVisible(group, "NextBox", group + 1 < GroupCount());
	return true;
}

// What ConfigureGroup hides can come back when it was hidden the frame its
// screen opened (rows past the page count showed the stash as 120 pages):
// hide it all again, a few times after a screen opens. Never re-shows
// anything, since a re-shown button looks hovered.
void HideAgain(int group) {
	const int count = std::min(Config.pageCount, BuiltPages);
	const int first = group * PerGroup() + 1;
	const int last  = std::min(first + PerGroup() - 1, BuiltPages);
	const int shown = std::max(0, std::min(last, count) - first + 1);
	const int used  = (shown + Config.columns - 1) / Config.columns;
	if (group == 0) SetVisible(group, "PreviousBox", false, true);
	if (group + 1 >= GroupCount()) SetVisible(group, "NextBox", false, true);
	for (int row = used; row < (last - first + Config.columns) / Config.columns; ++row) SetVisible(group, "Row" + std::to_string(row), false, true);
	if (shown % Config.columns != 0)
		for (int page = first + shown; page <= std::min(first + used * Config.columns - 1, last); ++page)
			for (const char* prefix : { "Page", "Number", "CatcherP", "ShadeP" }) SetVisible(group, prefix + std::to_string(page), false, true);
}

// A panel opened with openPanel has no widgets until D2R loads it, a frame or
// more later: try again each frame for a while.
int ConfigureTarget = -1;
int ConfigureTries  = 0;
void Queue(D2RL::Threads::Callback callback);
void OnConfigure(const D2RL::PluginContext*, void*) noexcept {
	if (ConfigureTarget < 0) return;
	if (ConfigureGroup(ConfigureTarget)) ConfigureTarget = -1;
	else if (++ConfigureTries < 120) Queue(OnConfigure);
	else {
		Log(true, "page list: %s never loaded its widgets", GroupPanelName(ConfigureTarget).c_str());
		ConfigureTarget = -1;
	}
}

void ConfigureLater(int group) {
	ConfigureTarget = group;
	ConfigureTries  = 0;
	OnConfigure(Context, nullptr);
}

bool IsOpen(int group) {
	Panels::PanelInfo info {};
	info.structSize = Panels::PanelInfoSize;
	return PanelApi->getPanelInfo(Context, GroupHandles[group], &info) == Panels::Result::Success && info.presentationState == Panels::PresentationState::Open;
}

void CloseList() {
	if (!PanelApi) return;
	for (const auto handle : GroupHandles) (void)PanelApi->closePanel(Context, handle);
}

int MarkPasses = 0; // polls left that re-apply the open screen's highlight

void ShowGroup(int group) {
	MarkPasses = 3;
	CloseList();
	if (group < 0 || group >= static_cast<int>(GroupHandles.size())) return;
	if (const auto result = PanelApi->openPanel(Context, GroupHandles[group]); result != Panels::Result::Success)
		Log(true, "page list: could not open %s (result %u)", GroupPanelName(group).c_str(), static_cast<unsigned>(result));
	ConfigureLater(group);
}

// keep_open: polls left in which a list closed by the game after a pick is reopened.
int ReopenPolls = 0;

// The stash button toggled the first screen of pages. Runs after the toggle.
void OnListToggled(const D2RL::PluginContext*, void*) noexcept {
	ReopenPolls = 0; // the list button closes it for good
	bool otherOpen = false;
	for (int group = 1; group < static_cast<int>(GroupHandles.size()); ++group) otherOpen = otherOpen || IsOpen(group);
	if (otherOpen || !IsOpen(0)) {
		CloseList(); // the button closes whichever screen is open
		return;
	}
	RefreshPageCount();
	Resync();
	if (const int group = GroupOf(CurrentPage); group != 0) ShowGroup(group); // open on the screen with the current page
	else {
		MarkPasses = 3;
		ConfigureLater(0);
	}
}

// Every 50 ms on the UI thread: follows the game's page (keybinds send no UI
// message) and moves the open list's highlight with it.
std::atomic<bool> Polling { false };
std::atomic<bool> PollQueued { false }; // one poll waits at a time, e.g. through a loading screen
HANDLE            PollThread {};

// The plugin's own list button: a panel of its own over the stash, opened
// while the stash shows the shared tab, where the game page can be read.
Panels::RegistrationHandle ButtonHandle = Panels::InvalidHandle;

void SyncButton() {
	if (ButtonHandle == Panels::InvalidHandle) return;
	const bool        want = ListPlace();
	Panels::PanelInfo info {};
	info.structSize   = Panels::PanelInfoSize;
	const bool open   = PanelApi->getPanelInfo(Context, ButtonHandle, &info) == Panels::Result::Success && info.presentationState == Panels::PresentationState::Open;
	if (want && !open) (void)PanelApi->openPanel(Context, ButtonHandle);
	else if (!want && open) (void)PanelApi->closePanel(Context, ButtonHandle);
}

// remember_open: follows the player opening and closing the list on the shared
// tab, and opens it again when the shared tab shows if it was left open. The
// game can close a list opened right as the stash loads (first stash of a
// session): a list of ours closed within half a second is opened again.
std::atomic<int> StashClosing { 0 }; // polls left after the stash closed: the list closing with it is not the player's choice

void TrackOpen() {
	static bool wasOpen     = false;
	static bool ours        = false; // the open list is one this opened
	static int  openPolls   = 0;
	static int  closedPolls = 0; // the stash closing closes the list first: wait to be sure
	static int  retryIn     = 0;
	static int  tries       = 0;
	if (StashClosing > 0 || !ListPlace()) {
		if (StashClosing > 0) --StashClosing;
		wasOpen = ours = false;
		openPolls = closedPolls = retryIn = tries = 0;
		return;
	}
	bool open = false;
	for (int group = 0; group < static_cast<int>(GroupHandles.size()); ++group) open = open || IsOpen(group);
	if (Pending != 0 || Walking || ReopenPolls > 0) {
		wasOpen = open;
		return;
	}
	if (open) {
		closedPolls = 0;
		if (++openPolls >= 10) ours = false; // it stuck
		if (!RememberedOpen) SaveOpenState(true);
		wasOpen = true;
		return;
	}
	if (wasOpen && !(ours && openPolls < 10)) {
		if (++closedPolls >= 4) {
			SaveOpenState(false);
			wasOpen     = false;
			openPolls   = 0;
			closedPolls = 0;
		}
		return;
	}
	wasOpen   = false;
	openPolls = 0;
	if (!RememberedOpen || tries >= 5) return;
	if (retryIn > 0) {
		--retryIn;
		return;
	}
	++tries;
	retryIn = 10;
	ours    = true;
	Log(false, "remember_open: opening the list (try %d)", tries);
	RefreshPageCount();
	ShowGroup(GroupOf(CurrentPage));
}

void GoTo(int page, bool retry);
int  TabGoTo   = 0; // a page picked from another stash tab, gone to once the shared tab shows
int  TabPolls  = 0;

void OnPoll(const D2RL::PluginContext*, void*) noexcept {
	PollQueued = false;
	SyncButton();
	if (TabGoTo != 0) {
		if (ReadGamePage() != 0) {
			const int page = std::exchange(TabGoTo, 0);
			GoTo(page, false);
		} else if (--TabPolls <= 0) {
			Log(true, "page: the shared tab did not show, %d not reached", TabGoTo);
			TabGoTo = 0;
		}
	}
	if (Config.rememberOpen && ConfigureTarget < 0) TrackOpen();
	if (ReopenPolls > 0 && Pending == 0 && !Walking) {
		--ReopenPolls;
		bool open = false;
		for (int group = 0; group < static_cast<int>(GroupHandles.size()); ++group) open = open || IsOpen(group);
		if (!open && ListPlace()) {
			Log(false, "keep_open: the list closed after a pick, reopening it");
			ReopenPolls = 0;
			ShowGroup(GroupOf(CurrentPage));
		}
	}
	if (Pending == 0 && !Walking)
		if (const int page = ReadGamePage(); page != 0 && page != CurrentPage) {
			if (page > Config.pageCount) RefreshPageCount();
			if (page <= Config.pageCount) {
				CurrentPage = page;
				MarkPasses  = std::max(MarkPasses, 1);
			}
		}
	if (MarkPasses > 0 && ConfigureTarget < 0) {
		--MarkPasses;
		for (int group = 0; group < static_cast<int>(GroupHandles.size()); ++group)
			if (IsOpen(group)) {
				ApplyMarks(group);
				HideAgain(group);
			}
	}
}

DWORD WINAPI PollLoop(void*) {
	while (Polling) {
		Sleep(50);
		if (Polling && Threads && !PollQueued.exchange(true) && Threads->runOnUiThread(Context, OnPoll, nullptr) != D2RL::Threads::Result::Success) PollQueued = false;
	}
	return 0;
}

int GroupStep = 0; // +1 for >, -1 for <; < and > are hidden at the ends
void OnGroupStep(const D2RL::PluginContext*, void*) noexcept {
	int open = 0;
	for (int group = 0; group < static_cast<int>(GroupHandles.size()); ++group)
		if (IsOpen(group)) open = group;
	ShowGroup(std::clamp(open + GroupStep, 0, GroupCount() - 1));
}

void OnCloseList(const D2RL::PluginContext*, void*) noexcept {
	CloseList();
}

void Queue(D2RL::Threads::Callback callback) {
	if (Threads) (void)Threads->runOnUiThread(Context, callback, nullptr);
}

void Step(const D2RL::PluginContext*, void*) noexcept;
void GoTo(int page, bool retry = false);
int  Retries = 0; // walks started again because the game ended up elsewhere

// The stash arrows stop at the first and last page.
int Stepped(int page, bool right) {
	return std::clamp(page + (right ? 1 : -1), 1, Config.pageCount);
}

void SendArrow(bool right) {
	const D2RL::Widgets::UiAction action {
		.structSize = D2RL::Widgets::UiActionSize,
		.target     = "BankPanelMessage",
		.command    = right ? "SharedStashRight" : "SharedStashLeft",
		.text       = "",
	};
	// The listener ignores this message (Walking): count the step here, once.
	LastArrowMs    = GetTickCount64(); // a late duplicate of this message is not a click either
	LastArrowRight = right;
	if (WidgetApi->dispatchUiAction(Context, &action) == Widgets::Result::Success) CurrentPage = Stepped(CurrentPage, right);
}

void Step(const D2RL::PluginContext*, void*) noexcept {
	Walking = true;
	const int perFrame = Config.stepsPerFrame > 0 ? Config.stepsPerFrame : std::max(1, (std::abs(Pending) + 39) / 40);
	for (int i = 0; i < perFrame && Pending != 0; ++i) {
		SendArrow(Pending > 0);
		Pending += Pending > 0 ? -1 : 1;
	}
	Walking = false;
	if (Pending != 0) Queue(Step);
	else {
		Resync();
		Log(false, "page: now on %d (asked for %d)", CurrentPage, Target);
		MarkPasses = std::max(MarkPasses, 1); // a list kept open moves its highlight
		if (CurrentPage != Target && ReadGamePage() != 0 && Retries < 2) GoTo(Target, true);
	}
}

void GoTo(int page, bool retry) {
	if (page < 1 || page > Config.pageCount) return;
	if (GamePageKnown && ReadGamePage() == 0 && StashShown()) {
		// Picked from the personal or materials tab: show the shared tab first.
		if (SelectSharedTab()) {
			TabGoTo  = page;
			TabPolls = 20;
			Log(false, "page: switching to the shared tab for %d", page);
		} else Log(true, "page: open the shared tab to go to %d", page);
		return;
	}
	Resync();
	Target  = page;
	Retries = retry ? Retries + 1 : 0;
	// Without the game's page, the first and last pages are reached with a full
	// page count of steps, which the arrows clamp: that corrects a wrong
	// CurrentPage too.
	const bool exact = ReadGamePage() != 0;
	Pending = exact ? page - CurrentPage : page == 1 ? 1 - Config.pageCount : page == Config.pageCount ? Config.pageCount - 1 : page - CurrentPage;
	if (!retry && JumpNow(page)) {
		if (const int now = ReadGamePage(); now == page) {
			Log(false, "page: %d -> %d (instant)", CurrentPage, page);
			Pending     = 0;
			CurrentPage = page;
			MarkPasses  = std::max(MarkPasses, 1); // a list kept open moves its highlight
			return;
		} else {
			Log(true, "page: an instant jump to %d ended on %d, using the arrows", page, now);
			if (now != 0) CurrentPage = now;
			Pending = page - CurrentPage;
		}
	}
	Log(false, "page: %d -> %d (%d step(s) %s)", CurrentPage, page, Pending < 0 ? -Pending : Pending, Pending < 0 ? "left" : "right");
	if (Pending != 0) Queue(Step);
}

// ---- UI messages ----

bool Is(const char* a, const char* b) {
	return a && b && std::strcmp(a, b) == 0;
}

auto OnUiMessage(const D2RL::PluginContext*, const Events::UiMessageEvent* event, void*) noexcept -> Events::UiMessageAction {
	if (!event) return Events::UiMessageAction::Continue;
	if (Is(event->target, "BankPanelMessage")) {
		if (Is(event->command, "SharedStashRight") || Is(event->command, "SharedStashLeft")) {
			if (Walking) return Events::UiMessageAction::Continue; // counted in SendArrow
			const bool      right = Is(event->command, "SharedStashRight");
			const ULONGLONG now   = GetTickCount64();
			if (right == LastArrowRight && now - LastArrowMs < DuplicateMs) return Events::UiMessageAction::Continue;
			LastArrowMs        = now;
			LastArrowRight     = right;
			CurrentPage        = Stepped(CurrentPage, right);
			Pending            = 0; // a click of the player's own cancels a walk
			MarkPasses         = std::max(MarkPasses, 2); // an open list follows (the game page is read on the next poll)
		} else if (Is(event->command, "Close")) {
			StashClosing = 10;
			Queue(OnCloseList);
		} else if (Is(event->command, "SelectTab") && !Config.buttonAllTabs) Queue(OnCloseList);
		return Events::UiMessageAction::Continue;
	}
	if (!Is(event->target, "PanelManager") || !event->text) return Events::UiMessageAction::Continue;
	if (Is(event->text, StashPanel)) {
		if (Is(event->command, "LoadPanel")) {
			// The game opens the shared stash on page 1 in every new game.
			CurrentPage = 1;
			Pending     = 0;
			// The list's panels may be rebuilt for the new game, back to every
			// widget shown: forget what was hidden and the widget handles.
			HandleCache.clear();
			Shown.clear();
			HiddenFrom.clear();
		} else if (Is(event->command, "ClosePanel") || Is(event->command, "UnloadPanel")) {
			StashClosing = 10;
			Queue(OnCloseList);
		}
		return Events::UiMessageAction::Continue;
	}
	if (Is(event->text, ListPanelName)) {
		static ULONGLONG lastToggleMs = 0; // D2R reports the message twice
		const ULONGLONG  now          = GetTickCount64();
		// Only the stash button's TogglePanel: OpenPanel also comes from the
		// plugin itself showing the first screen (< from the second), and
		// treating that as the button would jump back to the current page's screen.
		if (Is(event->command, "TogglePanel") && now - lastToggleMs >= DuplicateMs) {
			lastToggleMs = now;
			Queue(OnListToggled);
		}
		return Events::UiMessageAction::Continue;
	}
	if (std::strncmp(event->text, GroupPrefix, std::strlen(GroupPrefix)) == 0) {
		GroupStep = Is(event->text + std::strlen(GroupPrefix), "Next") ? 1 : -1;
		Queue(OnGroupStep);
		return Events::UiMessageAction::Consume;
	}
	const std::size_t prefix = std::strlen(PagePrefix);
	if (std::strncmp(event->text, PagePrefix, prefix) == 0) {
		const int page = std::atoi(event->text + prefix);
		if (!Config.keepOpen) Queue(OnCloseList);
		else ReopenPolls = 6; // the game may close the list as the page changes
		GoTo(page);
		return Events::UiMessageAction::Consume;
	}
	return Events::UiMessageAction::Continue;
}

// ---- console ----

std::string Args(const D2RL::ConsoleCommandContext* command) {
	return command && command->args ? Trim(std::string_view(command->args, command->argsLength)) : std::string();
}

auto GoCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
	if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
	const int page = ParseInt(Args(command), 0, 0, 1000);
	if (page < 1 || page > Config.pageCount) {
		Say("Usage: stash-goto <1-%d>", Config.pageCount);
		return D2RL::ConsoleCommandResult::Failed;
	}
	struct Job {
		static void Run(const D2RL::PluginContext*, void* data) noexcept { GoTo(static_cast<int>(reinterpret_cast<std::intptr_t>(data))); }
	};
	if (Threads) (void)Threads->runOnUiThread(Context, Job::Run, reinterpret_cast<void*>(static_cast<std::intptr_t>(page)));
	Say("Stash Tab Dropdown: going to page %d (the stash must be open on the shared tab)", page);
	return D2RL::ConsoleCommandResult::Handled;
}

auto PageCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
	if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
	if (const int page = ParseInt(Args(command), 0, 0, 1000); page >= 1 && page <= Config.pageCount) {
		struct Job {
			static void Run(const D2RL::PluginContext*, void* data) noexcept { CurrentPage = static_cast<int>(reinterpret_cast<std::intptr_t>(data)); }
		};
		if (Threads) (void)Threads->runOnUiThread(Context, Job::Run, reinterpret_cast<void*>(static_cast<std::intptr_t>(page)));
		Say("Stash Tab Dropdown: the stash is now taken to be on page %d", page);
	} else
		Say("Stash Tab Dropdown %s: on page %d of %d (as tracked). Use \"stash-page <n>\" if that is wrong.", PluginVersion, CurrentPage, Config.pageCount);
	return D2RL::ConsoleCommandResult::Handled;
}

// The "Open/close the page list" action in D2R's Controls menu (unbound by
// default). Opens the list when the stash shows the shared tab.
void OnListKey(const D2RL::PluginContext*, void*) noexcept {
	bool open = false;
	for (int group = 0; group < static_cast<int>(GroupHandles.size()); ++group) open = open || IsOpen(group);
	if (open) {
		ReopenPolls = 0;
		CloseList();
		return;
	}
	if (!ListPlace() && (GamePageKnown || ReadNativePage() == 0)) return; // no shared tab (or stash) showing
	RefreshPageCount();
	Resync();
	ShowGroup(GroupOf(CurrentPage));
}

auto OnListAction(const D2RL::PluginContext*, const D2RL::Input::ActionEvent* event, void*) noexcept -> D2RL::Input::ActionResult {
	if (!D2RL::Input::HasActionEventField(event, D2RL::Input::ActionEventRequiredSize) || event->kind != D2RL::Input::ActionEventKind::Pressed)
		return D2RL::Input::ActionResult::Ignored;
	Queue(OnListKey);
	return D2RL::Input::ActionResult::Handled;
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
		Log(false, "Stash Tab Dropdown %s: disabled in config", PluginVersion);
		return true;
	}
	if (Config.usePageText) LoadPageTextNames();
	Config.rememberOpen = Config.rememberOpen && Config.keepOpen;
	if (Config.listScale != 1.0f) {
		const auto scaled = [](int value, int least) { return std::max(least, static_cast<int>(value * Config.listScale + 0.5f)); };
		Config.columnWidth  = scaled(Config.columnWidth, 16);
		Config.rowHeight    = scaled(Config.rowHeight, 16);
		Config.buttonWidth  = scaled(Config.buttonWidth, 8);
		Config.buttonHeight = scaled(Config.buttonHeight, 8);
		Config.fontSize     = scaled(Config.fontSize, 8);
		if (!Config.buttonSprite.empty()) Config.buttonScale = Config.listScale; // a game sprite has one size: scale the button
	}
	Log(false, "config: keep_open %s, remember_open %s", Config.keepOpen ? "on" : "off", Config.rememberOpen ? "on" : "off");
	if (Config.rememberOpen) LoadOpenState();

	const auto* resources = Query<D2RL::ResourceService>("resource");
	Threads               = Query<D2RL::ThreadService>("thread");
	PanelApi              = Query<D2RL::PanelService>("panel");
	WidgetApi             = Query<D2RL::WidgetService>("widget");
	EventApi              = Query<D2RL::SharedEventService>("shared event");
	if (resources && !D2RL::HasResourceServiceField(resources, D2RL::ResourceServiceRequiredSize)) resources = nullptr;
	if (Threads && !D2RL::HasThreadServiceField(Threads, D2RL::ThreadServiceRequiredSize)) Threads = nullptr;
	if (PanelApi && !D2RL::HasPanelServiceField(PanelApi, D2RL::PanelServiceTogglePanelFieldEnd)) PanelApi = nullptr;
	if (WidgetApi && !D2RL::HasWidgetServiceField(WidgetApi, D2RL::WidgetServiceRequiredSize)) WidgetApi = nullptr;
	if (EventApi && !D2RL::HasSharedEventServiceField(EventApi, D2RL::SharedEventServiceRequiredSize)) EventApi = nullptr;
	if (!resources || !Threads || !PanelApi || !WidgetApi || !EventApi) {
		Log(true, "inactive: missing services (resource %s, thread %s, panel %s, widget %s, events %s)", resources ? "ok" : "missing", Threads ? "ok" : "missing",
		    PanelApi ? "ok" : "missing", WidgetApi ? "ok" : "missing", EventApi ? "ok" : "missing");
		return true;
	}

	RefreshPageCount();
	if (Config.buttonSprite.empty()) {
		// Low-end graphics read a half-size .lowend.sprite next to the HD one.
		static const std::string sprite = BuildButtonSprite(Config.buttonWidth, Config.buttonHeight);
		static const std::string lowend = BuildButtonSprite(std::max(8, Config.buttonWidth / 2), std::max(8, Config.buttonHeight / 2));
		static const std::string paths[] { std::string(OwnSpritePath) + ".sprite", std::string(OwnSpritePath) + ".lowend.sprite" };
		for (int i = 0; i < 2; ++i) {
			const std::string& bytes = i == 0 ? sprite : lowend;
			const D2RL::Resources::ResourceRegistration resource {
				.structSize = D2RL::Resources::ResourceRegistrationSize,
				.path       = paths[i].c_str(),
				.bytes      = bytes.data(),
				.byteCount  = bytes.size(),
			};
			D2RL::Resources::RegistrationHandle handle = D2RL::Resources::InvalidHandle;
			if (resources->registerResource(context, &resource, &handle) != D2RL::Resources::Result::Success) Log(true, "could not register the button sprite %s", paths[i].c_str());
		}
	}
	// Only the pages the stash has (hidden widgets would block the mouse); a
	// page count raised while the game runs needs a restart to be listed.
	BuiltPages = std::min({ MaxPages, MaxGroups * PerGroup(), std::max(1, Config.pageCount) });
	static std::vector<std::string> layouts;
	static std::vector<std::string> paths, localIds;
	const int                       groups = (BuiltPages + PerGroup() - 1) / PerGroup();
	layouts.reserve(groups), paths.reserve(groups), localIds.reserve(groups);
	std::size_t layoutBytes = 0;
	for (int group = 0; group < groups; ++group) {
		layouts.push_back(BuildListLayout(group));
		localIds.push_back(GroupLocalId(group));
		paths.push_back("data/global/ui/layouts/stash-tab-dropdown/" + localIds.back() + "hd.json");
		layoutBytes += layouts.back().size();
		const D2RL::Resources::ResourceRegistration resource {
			.structSize = D2RL::Resources::ResourceRegistrationSize,
			.path       = paths.back().c_str(),
			.bytes      = layouts.back().data(),
			.byteCount  = layouts.back().size(),
		};
		D2RL::Resources::RegistrationHandle resourceHandle = D2RL::Resources::InvalidHandle;
		const D2RL::Panels::PanelRegistration registration {
			.structSize = D2RL::Panels::PanelRegistrationSize,
			.flags      = Config.escapeCloses ? D2RL::Panels::PanelFlags::CloseOnEscape : D2RL::Panels::PanelFlags::None,
			.localId    = localIds.back().c_str(),
		};
		Panels::RegistrationHandle handle = Panels::InvalidHandle;
		if (resources->registerResource(context, &resource, &resourceHandle) != D2RL::Resources::Result::Success ||
		    PanelApi->registerPanel(context, &registration, &handle) != D2RL::Panels::Result::Success) {
			Log(true, "%s: could not register the page list panel %s", group == 0 ? "inactive" : "page list cut short", localIds.back().c_str());
			if (group == 0) return true;
			BuiltPages = group * PerGroup();
			break;
		}
		GroupHandles.push_back(handle);
	}
	if (BuiltPages < Config.pageCount) Log(true, "page list: holds the first %d pages; raise columns or rows for more", BuiltPages);

	GamePageKnown = Config.readGamePage && GameCodeMatches();
	if (Config.readGamePage && !GamePageKnown) {
		Log(true, "this game build's stash code is not the known one: no list button, page tracked from the arrows");
		LogGameCode();
	}
	InstantJump = GamePageKnown && Config.instantJump && CoreMatches();
	if (GamePageKnown && Config.instantJump && !InstantJump) Log(true, "instant jumps: D2RCore.dll is not the known D2RLoader 1.3.1 one, pages are reached with the arrows");
	if (GamePageKnown && Config.stashButton) {
		char button[640];
		std::snprintf(button, sizeof(button),
		              R"({"type": "Panel", "name": "stash-tab-dropdown/StashButton", "fields": {"priority": 110, "rect": "$LeftPanelRect_ExpandedStash", "anchor": "$LeftPanelAnchor"}, )"
		              R"("children": [{"type": "ButtonWidget", "name": "ListButton", "fields": {"rect": {"x": %d, "y": %d, "scale": %.3f}, "filename": "PANEL\\Chronicle\\Btn_collapse", )"
		              R"("normalFrame": 0, "pressedFrame": 2, "hoveredFrame": 1, "disabledFrame": 3, "sound": "cursor_menu_select_hd", "onClickMessage": "PanelManager:TogglePanel:%s"}}]})",
		              Config.stashButtonX, Config.stashButtonY, Config.stashButtonScale, ListPanelName);
		static const std::string layout = button;
		const D2RL::Resources::ResourceRegistration resource {
			.structSize = D2RL::Resources::ResourceRegistrationSize,
			.path       = "data/global/ui/layouts/stash-tab-dropdown/StashButtonhd.json",
			.bytes      = layout.data(),
			.byteCount  = layout.size(),
		};
		D2RL::Resources::RegistrationHandle resourceHandle = D2RL::Resources::InvalidHandle;
		const D2RL::Panels::PanelRegistration registration {
			.structSize = D2RL::Panels::PanelRegistrationSize,
			.flags      = D2RL::Panels::PanelFlags::None,
			.localId    = "StashButton",
		};
		if (resources->registerResource(context, &resource, &resourceHandle) != D2RL::Resources::Result::Success ||
		    PanelApi->registerPanel(context, &registration, &ButtonHandle) != D2RL::Panels::Result::Success) {
			Log(true, "stash button: could not register the list button");
			ButtonHandle = Panels::InvalidHandle;
		}
	}

	const Events::UiMessageListener listener {
		.structSize = Events::UiMessageListenerSize,
		.priority   = 100,
		.callback   = OnUiMessage,
	};
	Events::ListenerHandle listenerHandle = Events::InvalidHandle;
	if (EventApi->registerUiMessageListener(context, &listener, &listenerHandle) != Events::Result::Success) {
		Log(true, "inactive: could not listen to UI messages");
		return true;
	}

	if (const auto* input = Query<D2RL::InputService>("input"); input && D2RL::HasInputServiceField(input, D2RL::InputServiceRequiredSize)) {
		const D2RL::Input::ActionRegistration action {
			.structSize       = D2RL::Input::ActionRegistrationSize,
			.logicalId        = "toggle-page-list",
			.displayName      = "Open/close the stash page list",
			.category         = "Stash Tab Dropdown",
			.defaultPrimary   = { .key = D2RL::Input::Key::None },
			.defaultSecondary = { .key = D2RL::Input::Key::None },
			.callback         = OnListAction,
		};
		D2RL::Input::ActionHandle handle = D2RL::Input::InvalidHandle;
		if (const auto result = input->registerAction(context, &action, &handle); result != D2RL::Input::Result::Success)
			Log(true, "keybind: could not add the page list action to the Controls menu (result %u)", static_cast<unsigned>(result));
	}

	Polling       = true;
	PollThread    = CreateThread(nullptr, 0, PollLoop, nullptr, 0, nullptr);

	(void)context->RegisterConsoleCommand("stash-goto", GoCommand, "Go to a shared stash page: stash-goto <n>.");
	(void)context->RegisterConsoleCommand("stash-page", PageCommand, "Show the tracked shared stash page, or correct it: stash-page <n>.");

	Log(false, "Stash Tab Dropdown %s: active on %s %s; %d pages (%zu screens built), %zu named, current page %s, %s jumps (layout %zu bytes)", PluginVersion,
	    Context->buildVersion ? Context->buildVersion : "?", Context->buildName ? Context->buildName : "?", Config.pageCount, GroupHandles.size(),
	    static_cast<std::size_t>(std::count_if(PageTextPages.begin(), PageTextPages.end(), [](const auto& p) { return !p.second.text.empty(); })) + Config.pages.size(),
	    GamePageKnown                                                                 ? "read from the game"
	    : D2RL::HasWidgetServiceField(WidgetApi, D2RL::WidgetServiceGetTextFieldEnd) ? "read from the stash label"
	                                                                                  : "tracked from the arrows (keybinds are missed on this build)",
	    InstantJump ? "instant" : "arrow", layoutBytes);
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	Pending = 0;
	Polling = false;
	if (PollThread) {
		WaitForSingleObject(PollThread, 1000);
		CloseHandle(PollThread);
		PollThread = nullptr;
	}
}
