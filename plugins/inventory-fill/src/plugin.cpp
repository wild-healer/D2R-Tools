// Inventory Fill: while the inventory panel is closed, draws the player's
// inventory as a small grid on the skill bar, over the demon that holds the
// mana orb: free cells as dark tiles, items with their own pictures (or as
// filled cells), so the room left is visible without opening the inventory.
//
// How (Steam 3.3.94995, same code as 3.3.93787; every function is checked
// against its first bytes at load, nothing is touched on a mismatch):
//   - The skill bar is the HUDPanelHD widget (hudpanelhd.json "HUDPanel").
//     Its class keeps the base Widget::Draw (0x855B00) in vtable slot 3
//     (vtable Base+0x1FDF990, entry +0x18); only that vtable entry is
//     replaced, so other widgets and other plugins' hooks are untouched. The
//     grid is drawn after the bar's own children, so it lies on top of the
//     front panel art (the angel and the demon).
//   - Bar position on screen: 0x8562F0(widget, int out[2]) (adds the parents'
//     rects, anchors and scales); scale: 0x1E6750(widget) (product of +0x80
//     up the parents). A child at bar units (x, y) is at pos + (x, y) * scale.
//   - Inventory open: UI state byte 1 (0xCE500(id) reads the state array).
//   - Items: local player 0x9A480(0x8B2D0()); inventory 0x34A360(unit, file,
//     line); its grids are a vector at +0x20 (count +0x28) of 32-byte grids,
//     page 0 at index 2: width +0x10, height +0x11, cells +0x18 (item
//     pointers, y * width + x).
//   - Drawing: FillRect 0x657820 (x0, y0, x1, y1, r, g, b, mode, 1), mode 0/1/2
//     = 25/50/75 % opaque; DrawItemAt 0x15BB80 (item, x | y << 32, scale,
//     {float, bool}*), the game's own item renderer (98 art units per cell).

#include <D2RLPlugin/api.h>
#include <windows.h>

#include "i_hate_tomls_client.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr const char* PluginId      = "inventory-fill";
constexpr const char* PluginVersion = "1.0.0";

const D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = PluginId,
	.name        = "Inventory Fill",
	.version     = PluginVersion,
	.author      = "wild-healer",
	.description = "Shows the inventory grid on the skill bar while the inventory is closed.",
	.flags       = D2RL::PluginFlags::Client | D2RL::PluginFlags::NativeHooks,
};

// Read by the drawing every frame, so I Hate TOMLs' changes show at once.
constexpr int   DefaultPosition[2] { 2710, 290 };
constexpr float BaseCell = 22.0f; // bar units per cell at scale 1
struct Settings {
	std::atomic<bool>  enabled { true };
	std::atomic<bool>  sprites { true }; // false: filled cells only
	std::atomic<int>   background { 1 }; // FillRect mode: 0 = 25 %, 1 = 50 %, 2 = 75 %
	std::atomic<float> scale { 1.0f };
	std::atomic<int>   position[2] { DefaultPosition[0], DefaultPosition[1] }; // grid center, bar units
} Config;

const D2RL::PluginContext* Context {};

void Log(bool warn, const char* format, ...) noexcept {
	if (!Context) return;
	char    line[768] {};
	va_list args;
	va_start(args, format);
	std::vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	warn ? Context->LogWarn(line) : Context->LogInfo(line);
}

// ---- game code ----

struct NativeFn {
	std::uint64_t rva;
	std::uint8_t  bytes[16];
};

constexpr std::uint64_t WidgetDrawRva = 0x855B00, ScreenPosRva = 0x8562F0, ScaleRva = 0x1E6750, UiStateRva = 0xCE500, InventoryRva = 0x34A360,
                        LocalIndexRva = 0x8B2D0, PlayerUnitRva = 0x9A480, DrawItemAtRva = 0x15BB80, FillRectRva = 0x657820;
constexpr std::uint64_t HudVtableRva = 0x1FDF990, AttributeBallVtableRva = 0x1FD8A40;
constexpr std::size_t   DrawSlot     = 3;

constexpr NativeFn Natives[] {
	{ ScreenPosRva, { 0x48, 0x8b, 0xc4, 0x48, 0x89, 0x70, 0x20, 0x57, 0x48, 0x81, 0xec, 0xa0, 0x00, 0x00, 0x00, 0x0f } },
	{ ScaleRva, { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0x48, 0x8b, 0x49, 0x30, 0x48, 0x85, 0xc9 } },
	{ UiStateRva, { 0x48, 0x63, 0xc1, 0x48, 0x8d, 0x0d, 0x96, 0xc8, 0x95, 0x02, 0x0f, 0xb6, 0x04, 0x08, 0xc3, 0xcc } },
	{ InventoryRva, { 0x48, 0x89, 0x5c, 0x24, 0x18, 0x56, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xf1, 0x48, 0x85, 0xc9 } },
	{ LocalIndexRva, { 0x8b, 0x05, 0x2e, 0x84, 0x99, 0x02, 0xc3, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc } },
	{ PlayerUnitRva, { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x83, 0xf9, 0x08, 0x0f, 0x83, 0x85 } },
	{ DrawItemAtRva, { 0x40, 0x53, 0x55, 0x56, 0x48, 0x81, 0xec, 0x40, 0x01, 0x00, 0x00, 0x0f, 0x29, 0xb4, 0x24, 0x20 } },
	{ FillRectRva, { 0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8d, 0x6c } },
};
// Widget::Draw itself is only called: the skill bar's vtable entry must hold it.

struct OptFloat {
	float value;
	bool  valid;
};

using DrawFn       = void (*)(void* widget) noexcept;
using ScreenPosFn  = std::int32_t* (*)(void* widget, std::int32_t* out) noexcept;
using ScaleFn      = float (*)(void* widget) noexcept;
using UiStateFn    = std::uint8_t (*)(int id) noexcept;
using InventoryFn  = void* (*)(void* unit, const char* file, int line) noexcept;
using LocalIndexFn = int (*)() noexcept;
using PlayerUnitFn = void* (*)(int index) noexcept;
using DrawItemAtFn = void (*)(void* item, std::uint64_t pos, float scale, OptFloat* opt) noexcept;
using FillRectFn   = void (*)(int x0, int y0, int x1, int y1, std::uint8_t r, std::uint8_t g, std::uint8_t b, int mode, int hd) noexcept;

std::uintptr_t Base {};
DrawFn         OriginalDraw {};
template <typename Fn>
Fn At(std::uint64_t rva) {
	return reinterpret_cast<Fn>(Base + rva);
}
constexpr const char* SourceName = "inventory-fill";

bool Read(std::uintptr_t address, void* out, std::size_t size) noexcept {
	SIZE_T copied = 0;
	return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), out, size, &copied) && copied == size;
}

bool NativesMatch() noexcept {
	for (const auto& fn : Natives) {
		std::uint8_t code[16] {};
		if (!Read(Base + fn.rva, code, sizeof(code)) || std::memcmp(code, fn.bytes, sizeof(code)) != 0) {
			Log(true, "game code at D2R+0x%llX differs from 3.3.94995", static_cast<unsigned long long>(fn.rva));
			return false;
		}
	}
	return true;
}

// ---- inventory ----

constexpr std::size_t InventoryGrids = 0x20, InventoryGridCount = 0x28, GridStride = 0x20, PageGridIndex = 2;
constexpr std::size_t GridWidth = 0x10, GridHeight = 0x11, GridCells = 0x18;

struct Grid {
	int          width = 0, height = 0;
	void* const* cells = nullptr;
};

Grid LocalInventoryGrid() noexcept {
	void* const player = At<PlayerUnitFn>(PlayerUnitRva)(At<LocalIndexFn>(LocalIndexRva)());
	if (!player) return {};
	auto* const inventory = static_cast<std::uint8_t*>(At<InventoryFn>(InventoryRva)(player, SourceName, __LINE__));
	if (!inventory) return {};
	auto* const         grids = *reinterpret_cast<std::uint8_t**>(inventory + InventoryGrids);
	const std::uint64_t count = *reinterpret_cast<std::uint64_t*>(inventory + InventoryGridCount);
	if (!grids || count <= PageGridIndex) return {};
	const std::uint8_t* grid = grids + PageGridIndex * GridStride;
	Grid                out { grid[GridWidth], grid[GridHeight], *reinterpret_cast<void* const* const*>(grid + GridCells) };
	if (!out.cells || out.width <= 0 || out.height <= 0) return {};
	return out;
}

// Inventory Slots (our plugin) adds 2 rows under the grid for its cube, tome
// and key slots; they aren't inventory room, so they are left out here.
int SlotRows() noexcept {
	static const bool slots = GetModuleHandleW(L"d2rl-inventory-slots.dll") != nullptr;
	return slots ? 2 : 0;
}

// ---- drawing ----

void DrawInventory(void* hud) noexcept {
	const std::uint8_t open = At<UiStateFn>(UiStateRva)(1);
	if (open || !Config.enabled) return;
	const Grid grid = LocalInventoryGrid();
	if (!grid.cells) return;
	const int columns = grid.width;
	const int rows    = SlotRows() && grid.height >= 4 + SlotRows() ? grid.height - SlotRows() : grid.height;

	std::int32_t pos[2] {};
	if (!At<ScreenPosFn>(ScreenPosRva)(hud, pos)) return;
	const float scale = At<ScaleFn>(ScaleRva)(hud);
	if (!(scale > 0.0f)) return;

	static std::atomic<bool> described {};
	if (!described.exchange(true)) Log(false, "inventory grid %dx%d, %d rows shown", grid.width, grid.height, rows);

	const float pitch = BaseCell * Config.scale;
	if (!(pitch > 2.0f)) return;
	const float left = Config.position[0] - pitch * columns / 2, top = Config.position[1] - pitch * rows / 2;
	const auto  sx   = [&](float units) { return static_cast<int>(pos[0] + units * scale + 0.5f); };
	const auto  sy   = [&](float units) { return static_cast<int>(pos[1] + units * scale + 0.5f); };
	const float gap  = std::max(1.0f, pitch * 0.08f), margin = pitch * 0.3f;

	const auto fill = At<FillRectFn>(FillRectRva);
	fill(sx(left - margin), sy(top - margin), sx(left + pitch * columns + margin), sy(top + pitch * rows + margin), 0, 0, 0, Config.background, 1);

	std::array<void*, 256> drawn {};
	std::size_t            drawnCount = 0;
	for (int y = 0; y < rows; ++y)
		for (int x = 0; x < columns; ++x) {
			void* const item = grid.cells[y * grid.width + x];
			const float cx = left + x * pitch, cy = top + y * pitch;
			if (!item) {
				fill(sx(cx + gap / 2), sy(cy + gap / 2), sx(cx + pitch - gap / 2), sy(cy + pitch - gap / 2), 82, 76, 68, 0, 1);
				continue;
			}
			if (!Config.sprites) {
				fill(sx(cx + gap / 2), sy(cy + gap / 2), sx(cx + pitch - gap / 2), sy(cy + pitch - gap / 2), 196, 170, 110, 1, 1);
				continue;
			}
			if (std::find(drawn.begin(), drawn.begin() + drawnCount, item) != drawn.begin() + drawnCount) continue;
			if (drawnCount < drawn.size()) drawn[drawnCount++] = item;
			// First cell in row order = the item's top-left corner.
			OptFloat            none {};
			const std::uint32_t px = static_cast<std::uint32_t>(sx(cx)), py = static_cast<std::uint32_t>(sy(cy));
			At<DrawItemAtFn>(DrawItemAtRva)(item, px | static_cast<std::uint64_t>(py) << 32, scale * pitch / 98.0f, &none);
		}
}

void HudDraw(void* hud) noexcept {
	OriginalDraw(hud);
	if (hud) DrawInventory(hud);
}

std::uint64_t VtableEntryRva() noexcept {
	return HudVtableRva + DrawSlot * sizeof(void*);
}

bool WrapHudDraw() noexcept {
	const std::uintptr_t expected = Base + WidgetDrawRva, mine = reinterpret_cast<std::uintptr_t>(&HudDraw);
	std::uintptr_t       current  = 0;
	if (!Read(Base + VtableEntryRva(), &current, sizeof(current)) || current != expected) {
		Log(true, "the skill bar's draw entry is not the game's (another plugin replaced it, or another game build)");
		return false;
	}
	OriginalDraw = reinterpret_cast<DrawFn>(expected);
	if (Context->PatchBytes(VtableEntryRva(), &expected, sizeof(expected), &mine, sizeof(mine))) return true;
	auto* const entry   = reinterpret_cast<std::uintptr_t*>(Base + VtableEntryRva());
	DWORD       protect = 0;
	if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &protect)) return false;
	*entry = mine;
	VirtualProtect(entry, sizeof(*entry), protect, &protect);
	return true;
}

void RestoreHudDraw() noexcept {
	if (!OriginalDraw) return;
	auto* const entry = reinterpret_cast<std::uintptr_t*>(Base + VtableEntryRva());
	if (*entry != reinterpret_cast<std::uintptr_t>(&HudDraw)) return;
	DWORD protect = 0;
	if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &protect)) return;
	*entry = reinterpret_cast<std::uintptr_t>(OriginalDraw);
	VirtualProtect(entry, sizeof(*entry), protect, &protect);
}

// ---- config ----
// The same settings come from the TOML at start and from I Hate TOMLs while
// playing (applied at once, then the whole file is written back).

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

constexpr int BackgroundPercent[3] { 25, 50, 75 };

// Applies one setting from its TOML text (strings unquoted); false + reason when refused.
bool Apply(const std::string& key, const std::string& value, std::string& error) {
	if (key == "enabled") {
		if (value != "true" && value != "false") return error = "true or false", false;
		Config.enabled = value == "true";
	} else if (key == "display") {
		if (value != "sprites" && value != "cells") return error = "\"sprites\" or \"cells\"", false;
		Config.sprites = value == "sprites";
	} else if (key == "background") {
		const int percent  = std::atoi(value.c_str());
		Config.background = percent <= 25 ? 0 : percent >= 75 ? 2 : 1;
	} else if (key == "scale") {
		const float scale = static_cast<float>(std::atof(value.c_str()));
		if (!(scale >= 0.3f && scale <= 4.0f)) return error = "0.3 to 4", false;
		Config.scale = scale;
	} else if (key == "position") {
		int x = 0, y = 0;
		if (std::sscanf(value.c_str(), " [ %d , %d ]", &x, &y) != 2) return error = "[x, y]", false;
		Config.position[0] = x;
		Config.position[1] = y;
	} else return error = "unknown setting", false;
	return true;
}

std::string ValueOf(const std::string& key) {
	char text[64] {};
	if (key == "enabled") return Config.enabled ? "true" : "false";
	if (key == "display") return Config.sprites ? "\"sprites\"" : "\"cells\"";
	if (key == "background") return std::to_string(BackgroundPercent[Config.background]);
	if (key == "scale") std::snprintf(text, sizeof(text), "%.2f", static_cast<float>(Config.scale));
	else if (key == "position") std::snprintf(text, sizeof(text), "[%d, %d]", Config.position[0].load(), Config.position[1].load());
	return text;
}

std::string Render() {
	return R"(# Inventory Fill
#
# Shows your inventory as a small grid on the skill bar, over the demon that
# holds the mana orb, while the inventory panel is closed: the items' own
# pictures in their spots and the free cells, so you see at a glance how much
# room is left. Every setting can also be changed in game with I Hate TOMLs.

[inventory-fill]
enabled = )" + ValueOf("enabled") + R"(

# "sprites": the real item pictures, drawn small in their inventory spots.
# "cells": taken cells filled, free cells empty, without pictures.
display = )" + ValueOf("display") + R"(

# Darkness of the box behind the grid: 25, 50 or 75 (percent).
background = )" + ValueOf("background") + R"(

# Size of the grid: 1 = 22 skill bar units per cell.
scale = )" + ValueOf("scale") + R"(

# Center of the grid, in the skill bar's own units (2952 wide; the demon's
# body is around 2710, 290).
position = )" + ValueOf("position") + "\n";
}

void SaveConfig() {
	const std::string toml = Render();
	if (!Context->WriteConfig(toml.c_str())) Log(true, "config: could not write the config file");
}

void ApplyText(std::string_view text);

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
	const std::string config(buffer.data(), std::strlen(buffer.data()));
	ApplyText(config);
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

// ---- I Hate TOMLs ----

constexpr const char* Schema = R"(title = "Inventory Fill"
[[setting]]
key = "enabled"
type = "bool"
label = "Show the inventory grid"
note = "On the skill bar, over the mana demon, while the inventory is closed."
[[setting]]
key = "display"
type = "enum"
label = "Display"
values = ["sprites", "cells"]
labels = ["Item pictures", "Filled cells"]
[[setting]]
key = "background"
type = "enum"
label = "Background darkness"
values = [25, 50, 75]
labels = ["25 %", "50 %", "75 %"]
[[setting]]
key = "scale"
type = "float"
label = "Scale"
min = 0.3
max = 4.0
step = 0.05
decimals = 2
shift_step = 0.25
ctrl_step = 1.0
[[setting]]
key = "position"
type = "position"
label = "Position"
hint = "Center of the grid, in skill bar units."
min = -1000
max = 4000
shift_step = 10
ctrl_step = 100
default = [2710, 290]
)";

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
	Base = context->exeBase;
	if (!Base || !NativesMatch()) {
		Log(true, "Inventory Fill %s: inactive, this game build isn't supported (Steam 3.3.93787 / 3.3.94995 are)", PluginVersion);
		return true;
	}
	if (!WrapHudDraw()) {
		Log(true, "Inventory Fill %s: inactive, the skill bar's drawing could not be followed", PluginVersion);
		return true;
	}
	Log(false, "Inventory Fill %s: active%s, %s, scale %.2f, center %d,%d on the skill bar", PluginVersion, Config.enabled ? "" : " (hidden: enabled = false)",
	    Config.sprites ? "item pictures" : "cells", static_cast<float>(Config.scale), Config.position[0].load(), Config.position[1].load());
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	Menu.Stop();
	RestoreHudDraw();
}
