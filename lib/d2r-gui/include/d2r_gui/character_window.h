// D2R GUI: a window drawn like the game's character screen (I Hate TOMLs'):
// its background read from the game, the stat boxes and the experience bar
// covered with plain stone made from the same art, the game's side pillar,
// hinge, title and close button. It hangs where the game's left panels do.
#pragma once
#include "kit.h"
#include "layout.h"
#include <cstring>
#include <string>

namespace D2RGui::CharacterWindow {

// The character screen's size and place (HD layout units, from $LeftPanelAnchor).
inline constexpr int X = 236, Y = -651, Width = 1162, Height = 1507;
// The parts covered with its own stone (measured on Character_Background):
// the stat boxes' area, the plain stone between two stat boxes it is made of,
// and the experience bar in the header.
inline constexpr int CoverX = 70, CoverY = 306, CoverWidth = 1022, CoverHeight = 1040;
inline constexpr int PatchX = 80, PatchY = 465, PatchWidth = 420, PatchHeight = 165;
inline constexpr int BarX = 120, BarY = 208, BarWidth = 928, BarHeight = 44;
inline constexpr int CloseX = 1075, CloseY = 9;

// The stone between two stat boxes, mirrored over the whole box area so no
// seam shows. Without the background, plain grained stone.
inline Picture MakeCover(const Picture& background) {
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

// The header box's stone over the experience bar: the rows just above it, mirrored downwards.
inline Picture MakeBarCover(const Picture& background) {
	if (background.width < BarX + BarWidth || background.height < BarY + BarHeight) return {};
	Picture out = Blank(BarWidth, BarHeight);
	for (int y = 0; y < BarHeight; ++y)
		for (int x = 0; x < BarWidth; ++x) {
			std::memcpy(out.at(x, y), background.at(BarX + x, BarY - 1 - y), 4);
			out.at(x, y)[3] = 255;
		}
	return out;
}

inline std::string Cover() {
	return Kit::Detail::Once("charcover", [] { return std::vector<Picture> { MakeCover(Kit::Detail::FrameOr(Kit::PartFrames(Kit::Background), 0)) }; });
}

inline std::string BarCover() {
	return Kit::Detail::Once("charbar", [] {
		const Picture bar = MakeBarCover(Kit::Detail::FrameOr(Kit::PartFrames(Kit::Background), 0));
		return bar.empty() ? std::vector<Picture> {} : std::vector<Picture> { bar };
	});
}

// The window's JSON: `panelName` and `probe` as PanelPool gives them, the
// title, the close button's command, then `children` (rects in the window).
inline std::string Layout(const std::string& panelName, const std::string& probe, const std::string& title, const std::string& closeCommand, const std::string& children,
                          int priority = 10) {
	std::string c = ClickCatcher("PanelClickCatcher", 0, 0, Width, Height);
	// the game's side pillar on the screen's left edge, where its own left
	// panels (imbue, hire menu) put it, and clicks on it kept from the game
	c += ClickCatcher("LeftSideCatcher", -236, -205, 236, 2160);
	c += R"(, {"type": "ImageWidget", "name": "LeftSide", "fields": {"rect": {"x": -236, "y": -205}, "filename": "$LeftSideSprite"}})";
	c += R"(, {"type": "ImageWidget", "name": "Background", "fields": {"rect": {"x": 0, "y": 0}, "filename": "PANEL\\Character_Sheet\\Character_Background", "frame": 0}})";
	if (const auto cover = Cover(); !cover.empty()) c += Image("Cover", CoverX, CoverY, cover);
	if (const auto bar = BarCover(); !bar.empty()) c += Image("BarCover", BarX, BarY, bar);
	c += R"(, {"type": "ImageWidget", "name": "LeftHinge", "fields": {"rect": "$LeftHingeRect", "filename": "$LeftHingeSprite"}})";
	c += R"(, {"type": "TextBoxWidget", "name": "Title", "fields": {"rect": "$InGamePanelTitleRect", "text": )" + Json(GameText(title)) + R"(, "style": "$StyleTitleBlock"}})";
	c += R"(, {"type": "ButtonWidget", "name": "CloseButton", "fields": {"rect": {"x": )" + std::to_string(CloseX) + R"(, "y": )" + std::to_string(CloseY)
	   + R"(}, "filename": "PANEL\\closebtn_4x", "hoveredFrame": 3, "tooltipString": "@d2r:strClose", "sound": "cursor_close_window_hd", "onClickMessage": )" + Command(closeCommand) + "}}";
	c += Probe(probe);
	return Panel(panelName, X, Y, Width, Height, c + children, priority, LeftAnchor);
}

} // namespace D2RGui::CharacterWindow
