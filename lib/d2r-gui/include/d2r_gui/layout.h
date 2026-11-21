// D2R GUI: layout JSON for the game's widgets.
//
// Every widget function returns ", {...}" (a leading comma), so a panel's
// children are built by adding strings; Children() drops the first comma.
// Buttons send "PanelManager:ClosePanel:<id>/Cmd-<command>"; Commands
// (commands.h) catches it and hands <command> to the plugin.
//
// Names: every name looked up later with findPanel/findWidget takes one of
// the loader's ~255 widget handles for good, so look up only fixed names
// (Probe0/Probe1, not Probe<serial>). Names that are only drawn cost nothing.
#pragma once
#include "core.h"
#include "kit.h"
#include "text.h"
#include <cstdio>
#include <cstdlib>
#include <string>

namespace D2RGui {

// A text as the layout gets it: through GameText, unless the plugin keeps
// its texts as they are (Services::plainText).
inline std::string ShownText(const std::string& text) { return Api().plainText ? text : GameText(text); }

// The game's text styles (layout units; colours and sizes from the profile).
namespace Style {
inline constexpr const char* Label =
    R"({"fontColor": "$FontColorLightGold", "pointSize": "$MediumFontSize", "spacing": "$ReducedSpacing", "dropShadow": "$DefaultDropShadow", "alignment": {"h": "left", "v": "center"}, "options": {"lineWrap": true}})";
inline constexpr const char* SmallLabel =
    R"({"fontColor": "$FontColorLightGold", "pointSize": "$SmallFontSize", "spacing": "$ReducedSpacing", "dropShadow": "$DefaultDropShadow", "alignment": {"h": "left", "v": "center"}, "options": {"lineWrap": true}})";
inline constexpr const char* Value =
    R"({"fontColor": "$FontColorWhite", "pointSize": "$MediumFontSize", "spacing": "$ReducedSpacing", "dropShadow": "$DefaultDropShadow", "alignment": {"h": "center", "v": "center"}})";
inline constexpr const char* SmallValue =
    R"({"fontColor": "$FontColorWhite", "pointSize": "$SmallFontSize", "spacing": "$ReducedSpacing", "dropShadow": "$DefaultDropShadow", "alignment": {"h": "center", "v": "center"}, "options": {"lineWrap": true}})";
inline constexpr const char* Axis = R"({"fontColor": "$FontColorGoldYellow", "pointSize": "$MediumFontSize", "dropShadow": "$DefaultDropShadow", "alignment": {"h": "center", "v": "center"}})";
inline constexpr const char* Section =
    R"({"fontColor": "$FontColorGoldYellow", "pointSize": "$MediumLargeFontSize", "dropShadow": "$DefaultDropShadow", "alignment": {"h": "left", "v": "center"}})";
inline constexpr const char* Name = R"({"fontColor": "$FontColorWhite", "pointSize": "$LargeFontSize", "dropShadow": "$DefaultDropShadow", "alignment": {"h": "center", "v": "center"}})";
inline constexpr const char* Note =
    R"({"fontColor": "$FontColorLightGold", "pointSize": "$SmallFontSize", "dropShadow": "$DefaultDropShadow", "alignment": {"h": "center", "v": "center"}, "options": {"lineWrap": true}})";
inline constexpr const char* Warn =
    R"({"fontColor": "$FontColorLightRed", "pointSize": "$SmallFontSize", "dropShadow": "$DefaultDropShadow", "alignment": {"h": "center", "v": "center"}, "options": {"lineWrap": true}})";
inline constexpr const char* Comment =
    R"({"fontColor": "$FontColorWhite", "pointSize": "$SmallFontSize", "dropShadow": "$DefaultDropShadow", "alignment": {"h": "left", "v": "top"}, "options": {"lineWrap": true, "newlineHandling": "standard"}})";
inline constexpr const char* SmallLeft = R"({"fontColor": "$FontColorLightGold", "pointSize": "$SmallFontSize", "dropShadow": "$DefaultDropShadow", "alignment": {"h": "left", "v": "center"}})";
inline constexpr const char* LabelOneLine =
    R"({"fontColor": "$FontColorLightGold", "pointSize": "$MediumFontSize", "spacing": "$ReducedSpacing", "dropShadow": "$DefaultDropShadow", "alignment": {"h": "left", "v": "center"}})";
inline constexpr const char* SmallRight = R"({"fontColor": "$FontColorLightGold", "pointSize": "$SmallFontSize", "dropShadow": "$DefaultDropShadow", "alignment": {"h": "right", "v": "center"}})";
} // namespace Style

inline std::string Rect(int x, int y, int width = -1, int height = -1) {
	char text[112] {};
	if (width < 0) std::snprintf(text, sizeof(text), R"("rect": {"x": %d, "y": %d})", x, y);
	else std::snprintf(text, sizeof(text), R"("rect": {"x": %d, "y": %d, "width": %d, "height": %d})", x, y, width, height);
	return text;
}

// The prefix every button command of the plugin carries.
inline std::string CommandPrefix() { return Api().pluginId + "/Cmd-"; }

// A button's onClickMessage for `command`.
inline std::string Command(const std::string& command) { return Json("PanelManager:ClosePanel:" + CommandPrefix() + command); }

// "a, b, c" from ", a, , b, , c" fragments.
inline std::string Children(const std::string& fragments) { return fragments.empty() ? std::string() : fragments.substr(fragments[0] == ',' ? 1 : 0); }

// A tooltip on any widget: added as its child (multi-line text kept).
inline std::string TooltipChild(const std::string& tooltip) {
	return R"(, "children": [{"type": "FocusableWidget", "name": "Tooltip", "fields": {"fitToParent": true, "tooltipString": )" + Json(ShownText(tooltip))
	     + R"(, "tooltipStyle": {"fontStyle": {"options": {"newlineHandling": "standard"}}}}}])";
}

// Text in a box. `style` is a Style:: string or your own JSON object.
inline std::string Text(const std::string& name, int x, int y, int width, int height, const std::string& text, const char* style, const std::string& tooltip = {}) {
	std::string widget = R"(, {"type": "TextBoxWidget", "name": )" + Json(name) + R"(, "fields": {)" + Rect(x, y, width, height) + R"(, "text": )" + Json(ShownText(text)) + R"(, "style": )" + style + "}";
	if (!tooltip.empty()) widget += TooltipChild(tooltip);
	return widget + "}";
}

// An image. `file` is a native name (Kit::Native::...) or a plugin sprite
// (Kit::..., SpriteName()); it is written into the JSON as is. `extra` adds
// fields (", \"frame\": 1").
inline std::string Image(const std::string& name, int x, int y, const std::string& file, const std::string& extra = {}) {
	return R"(, {"type": "ImageWidget", "name": )" + Json(name) + R"(, "fields": {)" + Rect(x, y) + R"(, "filename": ")" + file + "\"" + extra + "}}";
}

// A plain coloured rectangle (r, g, b, a from 0 to 1).
inline std::string Rectangle(const std::string& name, int x, int y, int width, int height, double r, double g, double b, double a) {
	char color[96] {};
	std::snprintf(color, sizeof(color), R"(, "color": [%.3f, %.3f, %.3f, %.3f])", r, g, b, a);
	return R"(, {"type": "RectangleWidget", "name": )" + Json(name) + R"(, "fields": {)" + Rect(x, y, width, height) + color + "}}";
}

struct Frames {
	int normal = 0, pressed = 1, hovered = 3;
};

// A button on a sprite `file` (as Image), sending `command`. `width` and
// `height` may be left -1 (the sprite's size).
inline std::string Button(const std::string& name, int x, int y, const std::string& file, Frames frames, const std::string& command, const std::string& tooltip = {}, int width = -1,
                          int height = -1, const char* sound = "cursor_button_click_hd") {
	char head[160] {};
	std::snprintf(head, sizeof(head), R"(, "normalFrame": %d, "pressedFrame": %d, "hoveredFrame": %d, "sound": "%s")", frames.normal, frames.pressed, frames.hovered, sound);
	std::string out = R"(, {"type": "ButtonWidget", "name": )" + Json(name) + R"(, "fields": {)" + Rect(x, y, width, height) + R"(, "filename": ")" + file + "\"" + head;
	if (!tooltip.empty()) out += R"(, "tooltipString": )" + Json(ShownText(tooltip));
	if (!command.empty()) out += R"(, "onClickMessage": )" + Command(command);
	return out + "}}";
}

// A button with its text on it, on the game's drop-down box art.
inline std::string TextButton(const std::string& name, int x, int y, int width, int height, const std::string& text, const std::string& command, const std::string& tooltip = {},
                              const char* color = "$FontColorLightGold", const char* pointSize = "\"$MediumFontSize\"") {
	std::string out = R"(, {"type": "ButtonWidget", "name": )" + Json(name) + R"(, "fields": {)" + Rect(x, y, width, height) + R"(, "filename": ")" + Kit::Button(width, height)
	                + R"(", "normalFrame": 0, "hoveredFrame": 3, "pressedFrame": 1, "textString": )" + Json(ShownText(text)) + R"(, "textColor": ")" + color + R"(", "text/style": {"pointSize": )"
	                + pointSize + R"(, "dropShadow": "$DefaultDropShadow", "spacing": "$ReducedSpacing", "alignment": {"h": "center", "v": "center"}})";
	if (!tooltip.empty()) out += R"(, "tooltipString": )" + Json(ShownText(tooltip));
	return out + R"(, "sound": "cursor_button_click_hd", "onClickMessage": )" + Command(command) + "}}";
}

// An invisible button: `command` on a click (none when empty), a faint light when hovered.
inline std::string HitButton(const std::string& name, int x, int y, int width, int height, const std::string& command, const std::string& tooltip = {}) {
	std::string out = R"(, {"type": "ButtonWidget", "name": )" + Json(name) + R"(, "fields": {)" + Rect(x, y, width, height) + R"(, "filename": ")" + Kit::Hover()
	                + R"(", "normalFrame": 0, "hoveredFrame": 1, "pressedFrame": 1)";
	if (!tooltip.empty()) out += R"(, "tooltipString": )" + Json(ShownText(tooltip));
	if (!command.empty()) out += R"(, "sound": "cursor_menu_select_hd", "onClickMessage": )" + Command(command);
	return out + "}}";
}

// The loot filter's check box (the game's own art).
inline std::string Check(const std::string& name, int x, int y, bool on, const std::string& command, const std::string& tooltip = {}) {
	return Button(name, x, y, Kit::Native::CheckBox, { on ? 4 : 0, on ? 5 : 1, on ? 7 : 3 }, command, tooltip);
}

// A slot button with a text label centred on it (a tab, a category).
inline std::string SlotTextButton(const std::string& name, int x, int y, int width, int height, bool open, const std::string& text, const std::string& command,
                                  const std::string& tooltip = {}) {
	std::string out = Button(name, x, y, Kit::SlotButtonSprite(width, height, open), { 0, 2, 1 }, command, tooltip, width, height, "cursor_menu_select_hd");
	if (!text.empty()) out += Text(name + "Text", x, y, width, height, text, Style::Value);
	return out;
}

// The game's ClipViewWidget: cuts what it holds at its edges (drawing and,
// with clipMouseInput, clicks). Children's rects are relative to it.
inline std::string Clip(const std::string& name, int x, int y, int width, int height, const std::string& children, bool clipMouse = true) {
	if (children.empty()) return {};
	return R"(, {"type": "ClipViewWidget", "name": )" + Json(name) + R"(, "fields": {)" + Rect(x, y, width, height) + R"(, "clipMouseInput": )" + (clipMouse ? "true" : "false")
	     + R"(}, "children": [)" + Children(children) + "]}";
}

// A widget that only keeps clicks from reaching the game under it.
inline std::string ClickCatcher(const std::string& name, int x, int y, int width, int height) {
	return R"(, {"type": "ClickCatcherWidget", "name": )" + Json(name) + R"(, "fields": {)" + Rect(x, y, width, height) + "}}";
}

// A 1x1 invisible widget, looked up to know the layout has loaded. Only use
// fixed names (PanelPool uses Probe0 and Probe1).
inline std::string Probe(const std::string& name) { return Rectangle(name, 0, 0, 1, 1, 0, 0, 0, 0); }

// A panel's JSON: `name` must be "<plugin id>/<local id>" of a registered
// panel. `fields` adds to its fields (LeftAnchor: the vanilla left-panel place written out).
inline std::string Panel(const std::string& name, int x, int y, int width, int height, const std::string& children, int priority = 10, const std::string& fields = {}) {
	return R"({"type": "Panel", "name": )" + Json(name) + R"(, "fields": {"priority": )" + std::to_string(priority) + ", " + Rect(x, y, width, height) + fields + R"(}, "children": [)"
	     + Children(children) + "]}";
}

inline std::string CloseButton(const std::string& name, int x, int y, const std::string& command);

// The vanilla left-panel frame: the side pillar on the screen's left edge
// (where imbue and the hire menu put it, clicks kept from the game), the
// panel's own click catcher, the hinge, a title and the close button sending
// `closeCommand`. For a panel at $LeftPanelAnchor, x 236.
inline std::string LeftPanelFrame(int width, int height, const std::string& title, const std::string& closeCommand, int closeX) {
	std::string out = ClickCatcher("PanelClickCatcher", 0, 0, width, height);
	out += ClickCatcher("LeftSideCatcher", -236, -205, 236, 2160);
	out += R"(, {"type": "ImageWidget", "name": "LeftSide", "fields": {"rect": {"x": -236, "y": -205}, "filename": "$LeftSideSprite"}})";
	out += R"(, {"type": "ImageWidget", "name": "LeftHinge", "fields": {"rect": "$LeftHingeRect", "filename": "$LeftHingeSprite"}})";
	out += R"(, {"type": "TextBoxWidget", "name": "Title", "fields": {"rect": "$InGamePanelTitleRect", "text": )" + Json(ShownText(title)) + R"(, "style": "$StyleTitleBlock"}})";
	return out + CloseButton("CloseButton", closeX, 9, closeCommand);
}

// ---- more of the game's widgets (Grail On!'s window) ----

// A text style written out: `color` a profile colour ("$FontColorWhite") or a
// colour object ({"r": 200, "g": 30, "b": 30, "a": 255}), `size` in points.
struct TextStyle {
	std::string color = "$FontColorWhite";
	int         size  = 30;
	const char* h     = "left";
	const char* v     = "center";
	bool        wrap  = false;
};

inline std::string StyleJson(const TextStyle& style) {
	const std::string color = !style.color.empty() && style.color[0] == '{' ? style.color : "\"" + style.color + "\"";
	return R"({"fontColor": )" + color + R"(, "pointSize": )" + std::to_string(style.size) + R"(, "dropShadow": "$DefaultDropShadow", "spacing": "$ReducedSpacing", "alignment": {"h": ")" + style.h
	     + R"(", "v": ")" + style.v + "\"}" + (style.wrap ? R"(, "options": {"lineWrap": true}})" : "}");
}

// Text in a box, in a style of the caller's (a string key "@id:key" works too).
inline std::string TextBox(const std::string& name, int x, int y, int width, int height, const std::string& text, const TextStyle& style) {
	return R"(, {"type": "TextBoxWidget", "name": )" + Json(name) + R"(, "fields": {)" + Rect(x, y, width, height) + R"(, "text": )" + Json(ShownText(text)) + R"(, "style": )" + StyleJson(style) + "}}";
}

// An image drawn at `scale` (item art fitted in a box).
inline std::string ScaledImage(const std::string& name, int x, int y, double scale, const std::string& file, const std::string& extra = {}) {
	char rect[112] {};
	std::snprintf(rect, sizeof(rect), R"("rect": {"x": %d, "y": %d, "scale": %.4f})", x, y, scale);
	return R"(, {"type": "ImageWidget", "name": )" + Json(name) + R"(, "fields": {)" + rect + R"(, "filename": ")" + file + "\"" + extra + "}}";
}

// A sprite whose frames the game plays by itself (D2UI AnimatedImageWidget).
inline std::string AnimatedImage(const std::string& name, int x, int y, const std::string& file, int fps, const std::string& extra = {}) {
	return R"(, {"type": "AnimatedImageWidget", "name": )" + Json(name) + R"(, "fields": {)" + Rect(x, y) + R"(, "filename": ")" + file + R"(", "fps": )" + std::to_string(fps)
	     + R"(, "loop": true, "playing": true)" + extra + "}}";
}

// A plain container: its children's rects are from x, y; shown or hidden as one.
inline std::string Group(const std::string& name, int x, int y, const std::string& children) {
	if (children.empty()) return {};
	return R"(, {"type": "Widget", "name": )" + Json(name) + R"(, "fields": {)" + Rect(x, y) + R"(}, "children": [)" + Children(children) + "]}";
}

// A ClipViewWidget with fields of the caller's (", \"clipMouseInput\": true",
// ", \"visible\": false"), written even when it holds nothing.
inline std::string ClipBox(const std::string& name, int x, int y, int width, int height, const std::string& children, const std::string& fields = {}) {
	return R"(, {"type": "ClipViewWidget", "name": )" + Json(name) + R"(, "fields": {)" + Rect(x, y, width, height) + fields + R"(}, "children": [)" + Children(children) + "]}";
}

// Any widget of the game's: `fields` the inside of its fields object
// (left out when empty), `children` its children's fragments (left out when empty).
inline std::string Node(const std::string& type, const std::string& name, const std::string& fields, const std::string& children = {}) {
	std::string out = R"(, {"type": )" + Json(type) + R"(, "name": )" + Json(name);
	if (!fields.empty()) out += R"(, "fields": {)" + fields + "}";
	if (!children.empty()) out += R"(, "children": [)" + Children(children) + "]";
	return out + "}";
}

// What a button shows on it: its text, colour and size.
struct ButtonText {
	std::string text;
	std::string color = "$FontColorLightGold";
	int         size  = 30;
};

// A button on sprite `file` (as Image takes it) `width` x `height` (-1: the
// sprite's size). Its text when there is one; its sound only with a command
// (`sound` nullptr: none).
inline std::string SpriteButton(const std::string& name, int x, int y, int width, int height, const std::string& file, Frames frames, const std::string& command,
                                const std::string& tooltip = {}, const ButtonText& text = {}, const std::string& extra = {}, const char* sound = "cursor_menu_select_hd") {
	char head[112] {};
	std::snprintf(head, sizeof(head), R"(, "normalFrame": %d, "hoveredFrame": %d, "pressedFrame": %d)", frames.normal, frames.hovered, frames.pressed);
	std::string out = R"(, {"type": "ButtonWidget", "name": )" + Json(name) + R"(, "fields": {)" + Rect(x, y, width, height) + R"(, "filename": ")" + file + "\"" + head;
	if (!text.text.empty())
		out += R"(, "textString": )" + Json(ShownText(text.text)) + R"(, "textColor": ")" + text.color + R"(", "text/style": {"pointSize": )" + std::to_string(text.size)
		     + R"(, "dropShadow": "$DefaultDropShadow", "spacing": "$ReducedSpacing", "alignment": {"h": "center", "v": "center"}})";
	if (!tooltip.empty()) out += R"(, "tooltipString": )" + Json(ShownText(tooltip));
	out += extra;
	if (!command.empty()) {
		if (sound) out += std::string(R"(, "sound": ")") + sound + "\"";
		out += R"(, "onClickMessage": )" + Command(command);
	}
	return out + "}}";
}

// An invisible button that stays invisible when hovered (HitButton lights up).
inline std::string QuietButton(const std::string& name, int x, int y, int width, int height, const std::string& command, const std::string& tooltip = {}) {
	return SpriteButton(name, x, y, width, height, Kit::Clear(), { 0, 1, 1 }, command, tooltip);
}

// An invisible area that only takes clicks (no sound, no tooltip): over a
// grid, so a drop anywhere on it is the plugin's.
inline std::string ClickArea(const std::string& name, int x, int y, int width, int height, const std::string& command) {
	return SpriteButton(name, x, y, width, height, Kit::Clear(), { 0, 0, 0 }, command, {}, {}, {}, nullptr);
}

// The game's close button (its own art and sound).
inline std::string CloseButton(const std::string& name, int x, int y, const std::string& command) {
	return R"(, {"type": "ButtonWidget", "name": )" + Json(name) + R"(, "fields": {)" + Rect(x, y) + R"(, "filename": ")" + Kit::Native::CloseButton
	     + R"(", "hoveredFrame": 3, "tooltipString": "@d2r:strClose", "sound": "cursor_close_window_hd", "onClickMessage": )" + Command(command) + "}}";
}

// An item grid (InventoryGridWidget) `columns` x `rows` cells; `extra` adds fields.
inline std::string ItemGrid(const std::string& name, int x, int y, int columns, int rows, const std::string& extra = {}) {
	char text[200] {};
	std::snprintf(text, sizeof(text), R"(, "cellCount": {"x": %d, "y": %d}, "cellSize": "$ItemCellSize", "gemSocketFilename": "PANEL\\gemsocket")", columns, rows);
	return R"(, {"type": "InventoryGridWidget", "name": )" + Json(name) + R"(, "fields": {)" + Rect(x, y) + text + extra + "}}";
}

// The anchor of $LeftPanelAnchor written out (a panel's fields), for panels
// whose place the plugin works out itself.
inline constexpr const char* LeftAnchor = R"(, "anchor": {"x": 0.0, "y": 0.397})";

// A panel with no widgets (a placeholder registered at load).
inline std::string EmptyPanel(const std::string& name, int priority = 10) {
	return R"({"type": "Panel", "name": )" + Json(name) + R"(, "fields": {"priority": )" + std::to_string(priority) + R"(}, "children": []})";
}

// Every rect's x in `layout` moved by `dx` (top-level and nested alike).
inline std::string ShiftX(const std::string& layout, int dx) {
	static const std::string key = R"("rect": {"x": )";
	std::string              shifted;
	std::size_t              from = 0;
	for (std::size_t at; (at = layout.find(key, from)) != std::string::npos;) {
		at += key.size();
		char*      end = nullptr;
		const long x   = std::strtol(layout.c_str() + at, &end, 10);
		shifted += layout.substr(from, at - from) + std::to_string(x + dx);
		from = static_cast<std::size_t>(end - layout.c_str());
	}
	return shifted + layout.substr(from);
}

// A file path as Image and the buttons take it: backslashes doubled for
// JSON ("items\\misc\\ring" from the game's tables, written as it is).
inline std::string Path(const std::string& raw) {
	std::string out;
	for (const char c : raw) {
		if (c == '\\' || c == '"') out += '\\';
		out += c;
	}
	return out;
}

} // namespace D2RGui
