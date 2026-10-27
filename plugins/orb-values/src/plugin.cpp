// Orb Values: the life and mana numbers are always shown, without their
// "Life:" / "Mana:" prefix, in the middle of each orb.
//
// How (Steam 3.3.93787 / 94995 RVAs):
//   - AttributeBallWidget (HUDPanel > HealthBall / ManaBall) keeps its text
//     widget, ValueDisplay, at +0x618 and updates it in vtable slot 2
//     (0x14CAF10): the text is shown when the orb's "show values" setting is on
//     or the mouse is over the orb, formatted from the string "Life: %d / %d"
//     into the text box's string ({char*, size, capacity} at +0x88, or +0x200
//     when the bytes at +0x1F6 and +0x1F7 are both set).
//   - Always shown: the "mouse over the orb" test that hides it otherwise
//     (je at 0x14CB0AA) is removed. The game still hides it under its own
//     panels, and other plugins can still hide it.
//   - No toggle: the click handler's "mouse over the orb" test at 0x14CAE89
//     always skips the toggle, so the setting is left as it is.
//   - No prefix: after each update, everything before the first digit of the
//     text is removed (works for every language).
//   - On top of the liquid: children draw in order (Widget::Draw 0x855B00,
//     vector at +0x58, count at +0x60) and ValueDisplay comes first, under
//     BallImage (the liquid). It is moved to the end of its orb's children.
//   - In the middle: ValueDisplay's local rect (+0x70) is set after each
//     update: the box spans the orb's width and sits on its middle (the text is centred and bottom
//     aligned in the stock HD HUD).

#include <D2RLPlugin/api.h>
#include <windows.h>

#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr const char* PluginId      = "orb-values";
constexpr const char* PluginVersion = "1.0.0";

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
	bool enabled = true;
	int  offsetX = 0;
	int  offsetY = 0;
} Config;

const D2RL::PluginContext* Context = nullptr;
std::uintptr_t             Base    = 0;

// ---- native layout (Steam 3.3.93787 / 94995) ----

struct Native {
	std::uint64_t             rva;
	std::vector<std::uint8_t> bytes;
};
constexpr std::uint64_t UpdateRva = 0x14CAF10, ClickTestRva = 0x14CAE89, ShowTestRva = 0x14CB0AA;
const std::array<Native, 8> Natives { {
	// AttributeBallWidget update (vtable slot 2), hooked.
	{ UpdateRva, { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83 } },
	// Click: test al, al / je skip-toggle / mov eax, [rsi+0x558].
	{ ClickTestRva - 2, { 0x84, 0xC0, 0x74, 0x51, 0x8B, 0x86, 0x58, 0x05, 0x00, 0x00 } },
	// Update: test al, al / je hide / lea rcx, [rsi+0x5D0].
	{ ShowTestRva - 2, { 0x84, 0xC0, 0x0F, 0x84, 0x19, 0x01, 0x00, 0x00, 0x48, 0x8D, 0x8E, 0xD0, 0x05, 0x00, 0x00 } },
	// Update: cmp qword [rsi+0x618], 0 (the text widget).
	{ 0x14CB04C, { 0x48, 0x83, 0xBE, 0x18, 0x06, 0x00, 0x00, 0x00 } },
	// Update: the text box's string, +0x200 if [+0x1F6] && [+0x1F7], else +0x88.
	{ 0x14CB15A, { 0x48, 0x8B, 0x86, 0x18, 0x06, 0x00, 0x00, 0x80, 0xB8, 0xF6, 0x01, 0x00, 0x00, 0x00, 0x74, 0x0E, 0x80, 0xB8, 0xF7, 0x01,
	               0x00, 0x00, 0x00, 0xB9, 0x00, 0x02, 0x00, 0x00, 0x75, 0x05, 0xB9, 0x88, 0x00, 0x00, 0x00 } },
	// Widget::Draw walks the children: rbx = [rdi+0x58], rax = [rdi+0x60].
	{ 0x855BC2, { 0x48, 0x8B, 0x5F, 0x58, 0x48, 0x8B, 0x47, 0x60 } },
	// Widget rect getter: [+0x52] = "fills its parent", else the local rect {x, y, w, h} at +0x70.
	{ 0x8562A0, { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x80, 0x79, 0x52, 0x00, 0x48, 0x8B, 0xDA, 0x74, 0x2A, 0x48, 0x8B, 0x49, 0x30 } },
	{ 0x8562D9, { 0x0F, 0x10, 0x41, 0x70 } },
} };
constexpr std::size_t TextWidgetOffset = 0x618, ChildrenOffset = 0x58, ChildCountOffset = 0x60, ParentOffset = 0x30, FillsParentOffset = 0x52, RectOffset = 0x70;

using UpdateFn = void (*)(void*) noexcept;
UpdateFn Update = nullptr;

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
	SIZE_T copied = 0;
	return address != 0 && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), output, size, &copied) && copied == size;
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

// The text after the liquid, so it draws on top.
void RaiseText(std::uint8_t* ball, void* textBox) noexcept {
	auto** children = *reinterpret_cast<void***>(ball + ChildrenOffset);
	const auto count = *reinterpret_cast<std::uint64_t*>(ball + ChildCountOffset);
	if (!children || count < 2 || count > 64 || children[count - 1] == textBox) return;
	for (std::uint64_t i = 0; i + 1 < count; ++i)
		if (children[i] == textBox) {
			std::memmove(children + i, children + i + 1, (count - 1 - i) * sizeof(void*));
			children[count - 1] = textBox;
			return;
		}
}

// ---- position ----

// The text box's local rect, written directly: it is read back at every draw
// (Widget::Draw 0x875980 -> 0x8562F0 adds the parents' rects), and the game's
// layout only sets it when the HUD is built, so it is written at each update.
// The box spans the orb's whole width: the stock HD text is centred in its box,
// so the number stays centred whatever its length. The stock layout anchors
// the text at x 0.5, which adds half the orb's width to its x: x = -width / 2 (a zero-width box draws the
// text from its left edge). It is bottom aligned, 34 pt: the digits sit about
// 19 units above the bottom of the box.
int CentreLogs = 0;
void Centre(std::uint8_t* ball, std::uint8_t* textBox) noexcept {
	if (*reinterpret_cast<std::uint8_t**>(textBox + ParentOffset) != ball || textBox[FillsParentOffset] || ball[FillsParentOffset]) return;
	const auto* ballRect = reinterpret_cast<const std::int32_t*>(ball + RectOffset);
	auto*       rect     = reinterpret_cast<std::int32_t*>(textBox + RectOffset);
	if (ballRect[2] <= 0 || ballRect[3] <= 0) return;
	const std::int32_t x = Config.offsetX - ballRect[2] / 2, width = ballRect[2], y = ballRect[3] / 2 + 19 + Config.offsetY - rect[3];
	if (rect[0] == x && rect[1] == y && rect[2] == width) return;
	// Diagnostics: what the game had before each change (first 12).
	if (CentreLogs < 12) {
		++CentreLogs;
		float ballScale = 0, textScale = 0;
		std::memcpy(&ballScale, ball + 0x80, sizeof(float));
		std::memcpy(&textScale, textBox + 0x80, sizeof(float));
		Log(false, "orb %d: orb rect %d,%d %dx%d scale %.3f, text rect %d,%d %dx%d scale %.3f -> %d,%d %dx%d", *reinterpret_cast<std::int32_t*>(ball + 0x558), ballRect[0],
		    ballRect[1], ballRect[2], ballRect[3], ballScale, rect[0], rect[1], rect[2], rect[3], textScale, x, y, width, rect[3]);
	}
	rect[0] = x;
	rect[1] = y;
	rect[2] = width;
}

void HookUpdate(void* ball) noexcept {
	Update(ball);
	if (!ball) return;
	auto* const widget  = static_cast<std::uint8_t*>(ball);
	auto* const textBox = *reinterpret_cast<std::uint8_t**>(widget + TextWidgetOffset);
	if (!textBox) return;
	RaiseText(widget, textBox);
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
		const std::string value = Unquote(Trim(std::string_view(line).substr(equals + 1)));
		if (key == "enabled")
			Config.enabled = value != "false";
		else if (key == "offset_x")
			Config.offsetX = std::atoi(value.c_str());
		else if (key == "offset_y")
			Config.offsetY = std::atoi(value.c_str());
		else
			Log(true, "config: unknown key %s", key.c_str());
	}
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
	if (!Config.enabled) {
		Log(false, "Orb Values %s: disabled in config", PluginVersion);
		return true;
	}
	Base = context->exeBase;
	if (!Base || !Locate()) {
		Log(true, "Orb Values %s: inactive, this game build isn't supported (Steam 3.3.93787 / 3.3.94995 are)", PluginVersion);
		return true;
	}
	const auto& update = Natives[0].bytes;
	UpdateFn    trampoline = nullptr;
	if (!Context->InstallInlineHook(UpdateRva, update.data(), static_cast<std::uint32_t>(update.size()), reinterpret_cast<void*>(&HookUpdate),
	                                reinterpret_cast<void**>(&trampoline)) ||
	    !trampoline) {
		Log(true, "Orb Values %s: inactive, the orb update could not be hooked", PluginVersion);
		return true;
	}
	Update = trampoline;

	const std::uint8_t clickExpected[] { 0x74, 0x51 }, jmp[] { 0xEB, 0x51 };
	if (!Context->PatchBytes(ClickTestRva, clickExpected, sizeof(clickExpected), jmp, sizeof(jmp))) Log(true, "clicking an orb could not be disabled");
	const std::uint8_t showExpected[] { 0x0F, 0x84, 0x19, 0x01, 0x00, 0x00 };
	if (!Context->PatchNop(ShowTestRva, showExpected, sizeof(showExpected), sizeof(showExpected))) Log(true, "the numbers could not be made always visible");

	Log(false, "Orb Values %s: active, offset %d,%d", PluginVersion, Config.offsetX, Config.offsetY);
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {}
