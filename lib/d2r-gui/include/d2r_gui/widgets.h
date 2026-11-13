// D2R GUI: composite controls built from layout.h pieces.
#pragma once
#include "font.h"
#include "kit.h"
#include "layout.h"
#include "text_input.h"
#include <string>
#include <vector>

namespace D2RGui {

// A text box: its frame, the text (a caret while typing, a grey hint while
// empty) and a button over it sending "type<key>" (start TextInput there).
inline std::string TypedBox(const std::string& name, const std::string& key, int x, int y, int width, int height, const std::string& text, const std::string& hint,
                            const std::string& tooltip = {}) {
	std::string       out    = Image(name + "Frame", x, y, Kit::Box(width, height));
	const bool        typing = TextInput::Key() == key;
	const bool        empty  = text.empty() && !typing;
	const int         room   = width - 40;
	const int         chars  = std::max(8, Font::TextWidth("abcdefghijklmnopqrstuvwxyz") / 26);
	const std::string shown  = empty ? hint : Fit(GameText(text), room, typing, chars) + (typing ? "|" : "");
	out += R"(, {"type": "TextBoxWidget", "name": )" + Json(name) + R"(, "fields": {)" + Rect(x + 20, y, room, height) + R"(, "text": )" + Json(shown)
	     + R"(, "style": {"fontColor": ")" + (empty ? "$FontColorLightGray" : "$FontColorWhite")
	     + R"(", "pointSize": "$MediumFontSize", "dropShadow": "$DefaultDropShadow", "spacing": "$ReducedSpacing", "alignment": {"h": "left", "v": "center"}}}})";
	return out + HitButton(name + "Hit", x, y, width, height, "type" + key, tooltip);
}

// A drop-down: the closed box showing choice `chosen` (command "<command>"
// toggles it), and while `open`, its list under it (commands
// "<command>:<index>"). Draw an open list last so it covers what is below.
struct DropDown {
	static std::string Box(const std::string& name, int x, int y, int width, int height, const std::string& shown, const std::string& command, const std::string& tooltip = {}) {
		std::string out = Button(name, x, y, Kit::DropBox(width, height), { 0, 1, 3 }, command, tooltip, width, height);
		return out + Text(name + "Text", x + 24, y, width - 80, height, shown, Style::Value);
	}

	static std::string List(const std::string& name, int x, int y, int width, const std::vector<std::string>& choices, int chosen, const std::string& command, int lineHeight = 52) {
		std::string out;
		for (std::size_t i = 0; i < choices.size(); ++i) {
			const int         top  = y + static_cast<int>(i) * lineHeight;
			const std::string line = name + "Line" + std::to_string(i);
			out += Button(line, x, top, Kit::DropTile(width, lineHeight), { static_cast<int>(i) == chosen ? 1 : 0, 1, 1 }, command + ":" + std::to_string(i), {}, width, lineHeight,
			              "cursor_menu_select_hd");
			out += Text(line + "Text", x + 24, top, width - 48, lineHeight, choices[i], static_cast<int>(i) == chosen ? Style::Axis : Style::Value);
		}
		return out;
	}
};

// A value between the options screen's left and right arrows (commands
// "<command>-" and "<command>+").
inline std::string Stepper(const std::string& name, int x, int y, int width, int height, const std::string& value, const std::string& command, const std::string& tooltip = {}) {
	std::string out = Button(name + "Less", x, y + (height - 40) / 2, Kit::Native::LeftArrow, { 0, 1, 3 }, command + "-", tooltip);
	out += Text(name + "Value", x + 48, y, width - 96, height, value, Style::Value);
	out += Button(name + "More", x + width - 40, y + (height - 40) / 2, Kit::Native::RightArrow, { 0, 1, 3 }, command + "+", tooltip);
	return out;
}

// A section title with a gold rule under it.
inline std::string SectionTitle(const std::string& name, int x, int y, int width, const std::string& title) {
	return Text(name, x, y, width, 56, title, Style::Section) + Image(name + "Rule", x, y + 56, Kit::Rule(width));
}

} // namespace D2RGui
