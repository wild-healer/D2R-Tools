// D2R GUI: composite controls built from layout.h pieces (the settings
// controls of I Hate TOMLs and Grail On!).
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
// `charWidth`: units a character, to cut a long text.
inline std::string TypedBox(const std::string& name, const std::string& key, int x, int y, int width, int height, const std::string& text, const std::string& hint,
                            const std::string& tooltip = {}, int charWidth = 17) {
	std::string       out    = Image(name + "Frame", x, y, Kit::Box(width, height));
	const bool        typing = TextInput::Key() == key;
	const bool        empty  = text.empty() && !typing;
	const std::string shown  = empty ? hint : Fit(text, width - 40, typing, charWidth) + (typing ? "|" : "");
	out += R"(, {"type": "TextBoxWidget", "name": )" + Json(name) + R"(, "fields": {)" + Rect(x + 20, y, width - 40, height) + R"(, "text": )" + Json(shown)
	     + R"(, "style": {"fontColor": ")" + (empty ? "$FontColorLightGray" : "$FontColorWhite")
	     + R"(", "pointSize": "$MediumFontSize", "dropShadow": "$DefaultDropShadow", "spacing": "$ReducedSpacing", "alignment": {"h": "left", "v": "center"}}}})";
	return out + HitButton(name + "Hit", x, y, width, height, "type" + key, tooltip);
}

// A drop-down (Grail On!'s, as in I Hate TOMLs): the closed box with the
// choice on it, and the open list's lines (draw them last, over the rest,
// with a HitButton over the whole panel first so a click elsewhere closes it).
namespace DropDown {

// The closed box (frames: normal, pressed, disabled, hovered). The text is
// Small when it would not fit at Medium.
inline std::string Box(const std::string& name, int x, int y, int width, int height, const std::string& shown, const std::string& command, const std::string& tooltip = {}) {
	std::string out = R"(, {"type": "ButtonWidget", "name": )" + Json(name) + R"(, "fields": {)" + Rect(x, y, width, height) + R"(, "filename": ")" + Kit::DropBox(width, height)
	                + R"(", "normalFrame": 0, "pressedFrame": 1, "disabledFrame": 2, "hoveredFrame": 3, "textString": )" + Json(GameText(shown))
	                + R"(, "textColor": "$FontColorLightGold", "text/style": {"pointSize": ")" + (Font::TextWidth(shown) > width - 110 ? "$SmallFontSize" : "$MediumFontSize")
	                + R"(", "dropShadow": "$DefaultDropShadow", "spacing": "$ReducedSpacing", "alignment": {"h": "center", "v": "center"}})";
	if (!tooltip.empty()) out += R"(, "tooltipString": )" + Json(GameText(tooltip));
	return out + R"(, "sound": "cursor_button_click_hd", "onClickMessage": )" + Command(command) + "}}";
}

// One line of the open list; `current` in gold.
inline std::string Line(const std::string& name, int x, int y, int width, int height, const std::string& text, bool current, const std::string& command) {
	return R"(, {"type": "ButtonWidget", "name": )" + Json(name) + R"(, "fields": {)" + Rect(x, y, width, height) + R"(, "filename": ")" + Kit::DropTile(width, height)
	     + R"(", "normalFrame": 0, "pressedFrame": 1, "hoveredFrame": 1, "textString": )" + Json(GameText(text)) + R"(, "textColor": ")"
	     + (current ? "$FontColorGoldYellow" : "$FontColorWhite") + R"(", "text/style": {"pointSize": ")" + (Font::TextWidth(text) > width - 40 ? "$SmallFontSize" : "$MediumFontSize")
	     + R"(", "dropShadow": "$DefaultDropShadow", "spacing": "$ReducedSpacing", "alignment": {"h": "center", "v": "center"}}, "sound": "cursor_button_click_hd", "onClickMessage": )"
	     + Command(command) + "}}";
}

// The whole open list: a catcher over `catchWidth` x `catchHeight` sending
// `closeCommand`, then line k sending "<command><k>".
inline std::string List(const std::string& name, int x, int y, int width, int lineHeight, const std::vector<std::string>& choices, int current, const std::string& command,
                        const std::string& closeCommand, int catchWidth, int catchHeight) {
	std::string out = HitButton(name + "Catch", 0, 0, catchWidth, catchHeight, closeCommand);
	for (int k = 0; k < static_cast<int>(choices.size()); ++k)
		out += Line(name + "Item" + std::to_string(k), x, y + k * lineHeight, width, lineHeight, choices[static_cast<std::size_t>(k)], k == current, command + std::to_string(k));
	return out;
}

} // namespace DropDown

// The loot filter list's small up/down arrow (Kit::Arrow), as a button.
inline std::string ArrowButton(const std::string& name, int x, int y, bool up, int size, const std::string& command, const std::string& tooltip = {}) {
	return Button(name, x, y, Kit::Arrow(up, size), { 0, 2, 1 }, command, tooltip);
}

// The reset button (the lobby's refresh button) `size` square.
inline std::string ResetButton(const std::string& name, int x, int y, int size, const std::string& command, const std::string& tooltip = {}) {
	return Button(name, x, y, Kit::Reset(size), { 0, 2, 1 }, command, tooltip);
}

// The gear button `size` square.
inline std::string GearButton(const std::string& name, int x, int y, int size, const std::string& command, const std::string& tooltip = {}) {
	return Button(name, x, y, Kit::Gear(size), { 0, 2, 1 }, command, tooltip);
}

// The close button made small (remove a list item).
inline std::string RemoveButton(const std::string& name, int x, int y, int size, const std::string& command, const std::string& tooltip = {}) {
	return Button(name, x, y, Kit::Remove(size), { 0, 1, 3 }, command, tooltip);
}

// A value between the options screen's left and right arrows (commands
// `less` and `more`), Small when it would not fit.
inline std::string Stepper(const std::string& name, int x, int y, int width, int height, const std::string& value, const std::string& less, const std::string& more,
                           const std::string& tooltip = {}) {
	std::string out = Button(name + "Left", x, y + 24, Kit::Native::LeftArrow, { 0, 1, 3 }, less, tooltip);
	const int   room = width - 120;
	out += Text(name + "Value", x + 60, y, room, height, value, Font::TextWidth(value) > room ? Style::SmallValue : Style::Value, tooltip);
	out += Button(name + "Right", x + width - 50, y + 24, Kit::Native::RightArrow, { 0, 1, 3 }, more, tooltip);
	return out;
}

// A section title with a gold rule under it (I Hate TOMLs' header row).
inline std::string SectionTitle(const std::string& name, int x, int y, int width, const std::string& title, const std::string& tooltip = {}) {
	return Text("Section" + name, x, y + 10, width, 70, title, Style::Section, tooltip) + Image("Line" + name, x, y + 82, Kit::Rule(width));
}

// An icon with a tooltip over it.
inline std::string Icon(const std::string& name, int x, int y, int size, const std::string& sprite, const std::string& tooltip = {}) {
	std::string out = Image(name, x, y, sprite);
	if (!tooltip.empty()) out += Text(name + "Tip", x, y, size, size, "", Style::Note, tooltip);
	return out;
}

} // namespace D2RGui
