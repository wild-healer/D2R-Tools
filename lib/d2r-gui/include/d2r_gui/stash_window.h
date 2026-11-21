// D2R GUI: a window in the stash's look (Charm Inventory's, the frame of
// Run Counter's stats window): the stash's corners, edges and gold inner line
// around dark grained stone, a gold title and the game's close button. It
// hangs where the game's left panels do (the left-panel anchor written out).
//
// Opt-in: #include <d2r_gui/stash_window.h> (about 420 KB of frame art in the DLL).
//
//   StashWindow::Layout(panel, title, close, children)   the window's JSON
//   StashWindow::InnerX/InnerY/InnerWidth/InnerHeight    the stone inside the gold line
//   StashKit::Slot(...) / Kit::Slot(...)                 cells to put in it
#pragma once
#include "layout.h"
#include "picture.h"
#include "resources.h"
#include "stash_frame_data.inc"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>

namespace D2RGui::StashWindow {

// The window's size and place (HD layout units, as the game's left panels),
// the stone inside the frame's gold line, and the close button.
inline constexpr int X = 236, Y = -651, Width = 1162, Height = 1507;
inline constexpr int InnerX = 59, InnerY = 178, InnerWidth = 1103 - 59, InnerHeight = 1451 - 178;
inline constexpr int CloseX = Width - 87, CloseY = 9;
inline constexpr int TitleY = 74, TitleHeight = 84;

namespace Detail {
inline Picture Unpack(const Data::ArtPiece& piece) {
	Picture out = Blank(piece.width, piece.height);
	for (std::size_t p = 0; p < static_cast<std::size_t>(piece.width) * piece.height; ++p) {
		std::memcpy(out.rgba.data() + p * 4, piece.palette + piece.pixels[p] * 3, 3);
		out.rgba[p * 4 + 3] = 255;
	}
	return out;
}
} // namespace Detail

// The frame: the border pieces around dark stone with the grain of the
// game's panels (fine noise over a coarser 3x3 one). Empty if the pieces
// don't fit together.
inline Picture Frame() {
	using Detail::Unpack;
	const Picture top = Unpack(Data::TopArt), bottom = Unpack(Data::BottomArt), left = Unpack(Data::LeftArt), right = Unpack(Data::RightArt);
	const int     width = top.width, height = top.height + left.height + bottom.height;
	if (bottom.width != width || right.height != left.height || left.width + right.width >= width) return {};
	Picture    frame = Blank(width, height);
	const auto noise = [](std::uint32_t x, std::uint32_t y, std::uint32_t seed) {
		std::uint32_t h = x * 374761393u + y * 668265263u + seed * 2246822519u;
		h               = (h ^ (h >> 13)) * 1274126177u;
		return ((h ^ (h >> 16)) & 0xFF) / 255.0;
	};
	for (int y = top.height; y < top.height + left.height; ++y)
		for (int x = left.width; x < width - right.width; ++x) {
			const double n  = 0.6 * noise(x, y, 1) + 0.4 * noise(x / 3, y / 3, 2) - 0.5;
			auto*        px = frame.at(x, y);
			px[0] = px[1] = static_cast<unsigned char>(std::clamp(18 + static_cast<int>(n * 12), 0, 255));
			px[2]         = static_cast<unsigned char>(std::clamp(19 + static_cast<int>(n * 12), 0, 255));
			px[3]         = 255;
		}
	const auto paste = [&](const Picture& part, int x0, int y0) {
		for (int y = 0; y < part.height; ++y) std::memcpy(frame.at(x0, y0 + y), part.at(0, y), static_cast<std::size_t>(part.width) * 4);
	};
	paste(top, 0, 0);
	paste(bottom, 0, height - bottom.height);
	paste(left, 0, top.height);
	paste(right, width - right.width, top.height);
	return frame;
}

// One empty stash cell (98 x 98), cut from the same art.
inline Picture Cell() { return Detail::Unpack(Data::SlotArt); }

// The frame's sprite name (for "filename"), registered the first time (UI
// thread, or while loading). Empty if it could not be made.
inline std::string FrameSprite() {
	static std::mutex lock;
	static bool       made = false;
	std::lock_guard   guard(lock);
	if (!made) {
		const Picture frame = Frame();
		if (frame.width != Width || frame.height != Height || !RegisterSprite("stashwindow", { frame })) return {};
		made = true;
	}
	return SpriteName("stashwindow");
}

// The window's JSON: panel `panelName` (or a host's token such as
// "@PANEL@"), the title, then `children` (rects in the window). The close
// button sends `closeCommand` to the plugin (commands.h), or, when it is
// empty, closes the panel as the game's own close buttons do.
inline std::string Layout(const std::string& panelName, const std::string& title, const std::string& closeCommand, const std::string& children, int priority = 10) {
	std::string c = ClickCatcher("PanelClickCatcher", 0, 0, Width, Height);
	if (const auto frame = FrameSprite(); !frame.empty()) c += Image("Background", 0, 0, frame);
	c += R"(, {"type": "TextBoxWidget", "name": "Title", "fields": {)" + Rect(0, TitleY, Width, TitleHeight) + R"(, "text": )" + Json(GameText(title))
	   + R"(, "style": {"fontColor": "$FontColorGoldYellow", "pointSize": 52, "dropShadow": "$DefaultDropShadow", "alignment": {"h": "center", "v": "center"}}}})";
	if (!closeCommand.empty()) c += CloseButton("CloseButton", CloseX, CloseY, closeCommand);
	else
		c += R"(, {"type": "ButtonWidget", "name": "CloseButton", "fields": {)" + Rect(CloseX, CloseY) + R"(, "filename": "PANEL\\closebtn_4x", "hoveredFrame": 3, "tooltipString": "@d2r:strClose", )"
		     R"("sound": "cursor_close_window_hd", "onClickMessage": )" + Json("PanelManager:ClosePanel:" + panelName) + "}}";
	return Panel(panelName, X, Y, Width, Height, c + children, priority, LeftAnchor);
}

} // namespace D2RGui::StashWindow
