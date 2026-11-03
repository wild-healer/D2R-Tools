// Orb Values: the life and mana numbers are always shown, without their
// "Life:" / "Mana:" prefix, in the middle of each orb.
//
// How (Steam 3.3.93787 / 94995 RVAs):
//   - AttributeBallWidget (HUDPanel > HealthBall / ManaBall) keeps its text
//     widget, ValueDisplay, at +0x618 and updates it in vtable slot 2
//     (0x14CAF10): the text is shown when the orb's "show values" setting is on
//     or the mouse is over the orb (0x876180), formatted from the string
//     "Life: %d / %d" into the text box's string ({char*, size, capacity} at
//     +0x88, or +0x200 when the bytes at +0x1F6 and +0x1F7 are both set).
//   - Always shown: the update's "mouse over the orb" call (0x14CB0A3) goes
//     through a relay in an int3 gap (0x14CB1F2) and answers yes while the
//     plugin is on. The game still hides the text under its own panels, and
//     other plugins can still hide it.
//   - No toggle: the click handler's "mouse over the orb" call (0x14CAE82,
//     relay 0x14CACB1) answers no while the plugin is on, so the setting
//     toggle is skipped and the setting is left as it is.
//   - No prefix: after each update, everything before the first digit of the
//     text is removed (works for every language).
//   - On top of the liquid: children draw in order (Widget::Draw 0x855B00,
//     vector at +0x58, count at +0x60) and ValueDisplay comes first, under
//     BallImage (the liquid). It is moved to the end of its orb's children.
//   - In the middle: ValueDisplay's local rect (+0x70) is set after each
//     update: the box spans the orb's width and sits on its middle.
//   - Every setting applies at once (I Hate TOMLs or the TOML): turned off,
//     the relays ask the game again and each orb gets its text's place and
//     draw order back.

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

constexpr const char* PluginId      = "orb-values";
constexpr const char* PluginVersion = "1.0.1";

const D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = PluginId,
	.name        = "Orb Values",
	.version     = PluginVersion,
	.author      = "wild-healer",
	.description = "Life and mana numbers always shown in the middle of the orbs, without their prefix.",
	.flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

struct Settings {
	std::atomic<bool> enabled { true };
	std::atomic<int>  offsetX { 0 };
	std::atomic<int>  offsetY { 0 };
} Config;
constexpr int OffsetLimit = 2000;

const D2RL::PluginContext* Context = nullptr;
std::uintptr_t             Base    = 0;

// ---- native layout (Steam 3.3.93787 / 94995) ----

struct Native {
	std::uint64_t             rva;
	std::vector<std::uint8_t> bytes;
};
constexpr std::uint64_t UpdateRva = 0x14CAF10, HoveredRva = 0x876180, ClickCallRva = 0x14CAE82, ShowCallRva = 0x14CB0A3, ClickRelayRva = 0x14CACB1,
                        ShowRelayRva = 0x14CB1F2;
constexpr std::size_t RelaySize = 14;
const std::array<Native, 10> Natives { {
	// AttributeBallWidget update (vtable slot 2), hooked.
	{ UpdateRva, { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83 } },
	// Widget "mouse is over it" test, called by the plugin when it is off.
	{ HoveredRva, { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0xE8 } },
	// Click: mov rcx, rsi / call hovered / test al, al / je skip-toggle / mov eax, [rsi+0x558].
	{ ClickCallRva - 3, { 0x48, 0x8B, 0xCE, 0xE8, 0xF9, 0xB2, 0x3A, 0xFF, 0x84, 0xC0, 0x74, 0x51, 0x8B, 0x86, 0x58, 0x05, 0x00, 0x00 } },
	// Update: mov rcx, rsi / call hovered / test al, al / je hide / lea rcx, [rsi+0x5D0].
	{ ShowCallRva - 3, { 0x48, 0x8B, 0xCE, 0xE8, 0xD8, 0xB0, 0x3A, 0xFF, 0x84, 0xC0, 0x0F, 0x84, 0x19, 0x01, 0x00, 0x00, 0x48, 0x8D, 0x8E, 0xD0, 0x05, 0x00, 0x00 } },
	// The two int3 gaps that hold the relays.
	{ ClickRelayRva, std::vector<std::uint8_t>(RelaySize, 0xCC) },
	{ ShowRelayRva, std::vector<std::uint8_t>(RelaySize, 0xCC) },
	// Update: cmp qword [rsi+0x618], 0 (the text widget).
	{ 0x14CB04C, { 0x48, 0x83, 0xBE, 0x18, 0x06, 0x00, 0x00, 0x00 } },
	// Update: the text box's string, +0x200 if [+0x1F6] && [+0x1F7], else +0x88.
	{ 0x14CB15A, { 0x48, 0x8B, 0x86, 0x18, 0x06, 0x00, 0x00, 0x80, 0xB8, 0xF6, 0x01, 0x00, 0x00, 0x00, 0x74, 0x0E, 0x80, 0xB8, 0xF7, 0x01,
	               0x00, 0x00, 0x00, 0xB9, 0x00, 0x02, 0x00, 0x00, 0x75, 0x05, 0xB9, 0x88, 0x00, 0x00, 0x00 } },
	// Widget::Draw walks the children: rbx = [rdi+0x58], rax = [rdi+0x60].
	{ 0x855BC2, { 0x48, 0x8B, 0x5F, 0x58, 0x48, 0x8B, 0x47, 0x60 } },
	// Widget rect getter: [+0x52] = "fills its parent", else the local rect {x, y, w, h} at +0x70.
	{ 0x8562A0, { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x80, 0x79, 0x52, 0x00, 0x48, 0x8B, 0xDA, 0x74, 0x2A, 0x48, 0x8B, 0x49, 0x30, 0xE8 } },
} };
constexpr std::size_t TextWidgetOffset = 0x618, ChildrenOffset = 0x58, ChildCountOffset = 0x60, ParentOffset = 0x30, FillsParentOffset = 0x52, RectOffset = 0x70;

using UpdateFn  = void (*)(void*) noexcept;
using HoveredFn = bool (*)(void*) noexcept;
UpdateFn  Update  = nullptr;
HoveredFn Hovered = nullptr;

void Log(bool warn, const char* format, ...) noexcept {
	if (!Context) return;
	char    line[512] {};
	va_list args;
	va_start(args, format);
	std::vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	warn ? Context->LogWarn(line) : Context->LogInfo(line);
}

// Reads the game's own memory (VirtualQuery + memcpy: no ReadProcessMemory).
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

// ---- the two "mouse over the orb" tests ----

// Update: show the text (the game's own reasons to hide it still apply).
bool __cdecl ShowTest(void* ball) noexcept {
	return Config.enabled || Hovered(ball);
}

// Click: never toggle the orb's "show values" setting.
bool __cdecl ClickTest(void* ball) noexcept {
	return !Config.enabled && Hovered(ball);
}

// A 14-byte jmp [rip] to target in an int3 gap of the game, then the call at
// site redirected to it.
bool Relay(std::uint64_t gapRva, const void* target, std::uint64_t siteRva) noexcept {
	const std::vector<std::uint8_t> gap(RelaySize, 0xCC);
	std::array<std::uint8_t, RelaySize> relay { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
	const auto address = reinterpret_cast<std::uint64_t>(target);
	std::memcpy(relay.data() + 6, &address, sizeof(address));
	std::array<std::uint8_t, 5> call {};
	return Read(Base + siteRva, call.data(), call.size()) && Context->PatchBytes(gapRva, gap.data(), RelaySize, relay.data(), RelaySize) &&
	       Context->PatchCallRel32(siteRva, call.data(), static_cast<std::uint32_t>(call.size()), gapRva);
}

// ---- text ----

struct GameString {
	char*         text;
	std::uint64_t size;
	std::uint64_t capacity;
};

void StripPrefix(std::uint8_t* textBox) noexcept {
	const std::size_t offset = textBox[0x1F6] && textBox[0x1F7] ? 0x200 : 0x88;
	auto*             string = reinterpret_cast<GameString*>(textBox + offset);
	if (!string->text || string->size == 0 || string->size > 256) return;
	std::size_t first = 0;
	while (first < string->size && (string->text[first] < '0' || string->text[first] > '9')) ++first;
	if (first == 0 || first == string->size) return;
	std::memmove(string->text, string->text + first, string->size - first + 1); // with the null
	string->size -= first;
}

// What the game had for an orb's text before the plugin changed it, to give
// it back when the plugin is turned off. A rebuilt HUD has new widgets.
struct Original {
	void*                       textBox = nullptr;
	std::uint64_t               index   = 0;
	std::array<std::int32_t, 4> rect {};
};
std::array<Original, 8> Originals {};
std::size_t             NextOriginal = 0;

Original* Find(void* textBox) noexcept {
	for (auto& original : Originals)
		if (original.textBox == textBox) return &original;
	return nullptr;
}

// Moves the text widget to index in its orb's children (the end: drawn last).
void MoveChild(std::uint8_t* ball, void* textBox, std::uint64_t index) noexcept {
	auto** children = *reinterpret_cast<void***>(ball + ChildrenOffset);
	const auto count = *reinterpret_cast<std::uint64_t*>(ball + ChildCountOffset);
	if (!children || count < 2 || count > 64 || index >= count || children[index] == textBox) return;
	for (std::uint64_t i = 0; i < count; ++i)
		if (children[i] == textBox) {
			if (i < index)
				std::memmove(children + i, children + i + 1, (index - i) * sizeof(void*));
			else
				std::memmove(children + index + 1, children + index, (i - index) * sizeof(void*));
			children[index] = textBox;
			return;
		}
}

std::uint64_t IndexOf(std::uint8_t* ball, void* textBox) noexcept {
	auto** children = *reinterpret_cast<void***>(ball + ChildrenOffset);
	const auto count = *reinterpret_cast<std::uint64_t*>(ball + ChildCountOffset);
	for (std::uint64_t i = 0; children && count <= 64 && i < count; ++i)
		if (children[i] == textBox) return i;
	return 0;
}

// ---- position ----

// The text box's local rect, written directly: it is read back at every draw
// (Widget::Draw 0x875980 -> 0x8562F0 adds the parents' rects), and the game's
// layout only sets it when the HUD is built, so it is written at each update.
// The box spans the orb's whole width: the stock HD text is centred in its box,
// so the number stays centred whatever its length. The stock layout anchors
// the text at x 0.5, which adds half the orb's width to its x: x = -width / 2.
// It is bottom aligned, 34 pt: the digits sit about 19 units above the bottom
// of the box.
void Centre(std::uint8_t* ball, std::uint8_t* textBox) noexcept {
	const auto* ballRect = reinterpret_cast<const std::int32_t*>(ball + RectOffset);
	auto*       rect     = reinterpret_cast<std::int32_t*>(textBox + RectOffset);
	if (ballRect[2] <= 0 || ballRect[3] <= 0) return;
	rect[0] = Config.offsetX - ballRect[2] / 2;
	rect[1] = ballRect[3] / 2 + 19 + Config.offsetY - rect[3];
	rect[2] = ballRect[2];
}

void HookUpdate(void* ball) noexcept {
	Update(ball);
	if (!ball) return;
	auto* const widget  = static_cast<std::uint8_t*>(ball);
	auto* const textBox = *reinterpret_cast<std::uint8_t**>(widget + TextWidgetOffset);
	if (!textBox || *reinterpret_cast<std::uint8_t**>(textBox + ParentOffset) != widget || textBox[FillsParentOffset] || widget[FillsParentOffset]) return;
	Original* original = Find(textBox);
	if (!Config.enabled) {
		if (!original) return;
		MoveChild(widget, textBox, original->index);
		std::memcpy(textBox + RectOffset, original->rect.data(), sizeof(original->rect));
		original->textBox = nullptr;
		return;
	}
	if (!original) {
		original          = &Originals[NextOriginal++ % Originals.size()];
		original->textBox = textBox;
		original->index   = IndexOf(widget, textBox);
		std::memcpy(original->rect.data(), textBox + RectOffset, sizeof(original->rect));
	}
	MoveChild(widget, textBox, *reinterpret_cast<std::uint64_t*>(widget + ChildCountOffset) - 1);
	StripPrefix(textBox);
	Centre(widget, textBox);
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

// Applies one setting from its TOML text (strings unquoted); false + reason when refused.
bool Apply(const std::string& key, const std::string& value, std::string& error) {
	const auto flag = [&](std::atomic<bool>& target) {
		if (value != "true" && value != "false") return error = "true or false", false;
		target = value == "true";
		return true;
	};
	const auto number = [&](std::atomic<int>& target) {
		char*      end    = nullptr;
		const long parsed = std::strtol(value.c_str(), &end, 10);
		if (value.empty() || *end || parsed < -OffsetLimit || parsed > OffsetLimit) return error = "a whole number from -2000 to 2000", false;
		target = static_cast<int>(parsed);
		return true;
	};
	if (key == "enabled") return flag(Config.enabled);
	if (key == "offset_x") return number(Config.offsetX);
	if (key == "offset_y") return number(Config.offsetY);
	return error = "unknown setting", false;
}

std::string ValueOf(const std::string& key) {
	if (key == "enabled") return Config.enabled ? "true" : "false";
	if (key == "offset_x") return std::to_string(Config.offsetX.load());
	if (key == "offset_y") return std::to_string(Config.offsetY.load());
	return {};
}

std::string Render() {
	return R"(# Orb Values
#
# The life and mana numbers are always shown, without the "Life:" and "Mana:"
# prefixes, in the middle of each orb. Clicking an orb no longer hides them.
# Every setting can also be changed in game with I Hate TOMLs, and applies at once.

[orb-values]
enabled = )" + ValueOf("enabled") + R"(

# Moves both numbers from the middle of their orb, in HUD units (an orb is
# about 350 units tall). Positive x goes right, positive y goes down.
offset_x = )" + ValueOf("offset_x") + R"(
offset_y = )" + ValueOf("offset_y") + "\n";
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

constexpr const char* Schema = R"toml(title = "Orb Values"
[[setting]]
key = "enabled"
type = "bool"
label = "Numbers in the orbs"
note = "Always shown, without their prefix, in the middle of each orb. Off: the game's own behavior."
[[setting]]
type = "header"
label = "Position"
[[setting]]
key = "offset_x"
type = "int"
label = "Horizontal offset"
note = "From the middle of the orb, in HUD units (an orb is about 350). Positive goes right."
min = -2000
max = 2000
shift_step = 10
ctrl_step = 100
[[setting]]
key = "offset_y"
type = "int"
label = "Vertical offset"
note = "Positive goes down."
min = -2000
max = 2000
shift_step = 10
ctrl_step = 100
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

bool Locate() noexcept {
	for (const auto& native : Natives) {
		std::vector<std::uint8_t> code(native.bytes.size());
		if (!Read(Base + native.rva, code.data(), code.size()) || code != native.bytes) {
			Log(true, "game code differs at D2R+0x%llX", static_cast<unsigned long long>(native.rva));
			return false;
		}
	}
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
		Log(true, "Orb Values %s: inactive, this game build isn't supported (Steam 3.3.93787 / 3.3.94995 are)", PluginVersion);
		return true;
	}
	Hovered = reinterpret_cast<HoveredFn>(Base + HoveredRva);
	const auto& update = Natives[0].bytes;
	if (!Context->InstallInlineHook(UpdateRva, update.data(), static_cast<std::uint32_t>(update.size()), &HookUpdate, &Update) || !Update) {
		Log(true, "Orb Values %s: inactive, the orb update could not be hooked", PluginVersion);
		return true;
	}
	if (!Relay(ShowRelayRva, reinterpret_cast<const void*>(&ShowTest), ShowCallRva)) Log(true, "the numbers could not be made always visible");
	if (!Relay(ClickRelayRva, reinterpret_cast<const void*>(&ClickTest), ClickCallRva)) Log(true, "clicking an orb could not be disabled");

	Log(false, "Orb Values %s: %s, offset %d,%d", PluginVersion, Config.enabled ? "active" : "loaded but turned off", Config.offsetX.load(), Config.offsetY.load());
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	Menu.Stop();
}
