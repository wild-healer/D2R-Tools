// D2R GUI: the settings art in the stash's look (Grail On!'s window): the
// game's own parts read by Kit::Read, scaled by area average, and the slot
// stone of the stash where a part is missing. Like Kit, every function gives
// a sprite name for "filename", registered the first time a size is asked
// for (UI thread); Kit::Read makes them again with the game's art.
//
// Frames, as the plugin's buttons take them (layout.h Frames {normal, pressed, hovered}):
//   SlotButton, Tick, Arrow, Add, GroupArrow, Ticks   {0, 2, 1}
//   DropBox, StepBox, PlainButton, Remove              {0, 1, 3}
//   DropTile                                           {0, 1, 1}
#pragma once
#include "item_art.h"
#include "kit.h"
#include "stash_art.h"
#include <string>
#include <vector>

namespace D2RGui::StashKit {

namespace Detail {
inline const std::vector<Picture>& Part(Kit::Part part) { return Kit::Get().parts[part]; }
inline const Picture&              At(const std::vector<Picture>& frames, std::size_t f) { return frames[std::min(f, frames.size() - 1)]; }
inline constexpr unsigned char     Gold[3] { 199, 179, 119 };
template <typename Make>
std::string Once(const std::string& name, Make make) { return Kit::Detail::Once("s-" + name, make); }
inline std::string Size(int width, int height) { return std::to_string(width) + "x" + std::to_string(height); }
} // namespace Detail

// A square or tab button: an empty slot, lighter hovered, darker pressed;
// gold-rimmed when `open` (the current menu or category).
inline std::string SlotButton(int width, int height, bool open = false) {
	return Detail::Once("button" + Detail::Size(width, height) + (open ? "on" : ""), [&] { return D2RGui::SlotButton(width, height, open); });
}

// The plugin's own check box (a slot with a gold mark), 56 square.
inline std::string Check(bool on) {
	return Detail::Once(on ? "checkon" : "check", [&] { return CheckBox(on); });
}

// The loot filter's check box fitted in `size` square (the plugin's own without it).
inline std::string Tick(bool on, int size = 56) {
	return Detail::Once("tick" + std::to_string(size) + (on ? "on" : ""), [&] {
		const auto& box = Detail::Part(Kit::CheckArt);
		if (box.size() < 7) {
			auto own = CheckBox(on);
			if (size != 56)
				for (auto& f : own) f = FitBox(f, size, size);
			return own;
		}
		std::vector<Picture> frames;
		for (const int f : { 0, 1, 2 }) frames.push_back(FitBox(box[f + (on ? 4 : 0)], size, size));
		return frames;
	});
}

// The small check boxes of a list of groups: 0 none ticked, 1 all, 2 some (the
// ticked box dimmed), 3 off (dimmed, one frame: nothing to click).
inline std::string Ticks(int state, int size) {
	return Detail::Once("ticks" + std::to_string(size) + "-" + std::to_string(state), [&] {
		const auto&          box = Detail::Part(Kit::CheckArt);
		std::vector<Picture> off, on;
		if (box.size() >= 7)
			for (const int f : { 0, 1, 2 }) off.push_back(FitBox(box[f], size, size)), on.push_back(FitBox(box[f + 4], size, size));
		else {
			off = CheckBox(false), on = CheckBox(true);
			for (auto& f : off) f = FitBox(f, size, size);
			for (auto& f : on) f = FitBox(f, size, size);
			if (state == 2) return on;
		}
		if (state == 1) return on;
		if (state == 2) {
			for (auto& f : on) f = Darkened(f);
			return on;
		}
		if (state == 3) return std::vector<Picture> { Faded(off[0], 2, 5) };
		return off;
	});
}

// The line between sections, `width` wide (the pause menu's divider), and its height.
inline int SectionHeight(int width) {
	const auto& line = Detail::Part(Kit::Divider);
	return line.empty() ? 4 : std::max(2, line[0].height * width / std::max(1, line[0].width));
}
inline std::string Section(int width) {
	return Detail::Once("section" + std::to_string(width), [&] {
		const auto& line = Detail::Part(Kit::Divider);
		return std::vector<Picture> { line.empty() ? FadedRule(width, 4) : ResizeBox(line[0], width, SectionHeight(width)) };
	});
}

// A gold line fading out at both ends, `width` x 4 (under a set's name).
inline std::string Divider(int width) {
	return Detail::Once("divider" + std::to_string(width), [&] { return std::vector<Picture> { FadedRule(width, 4) }; });
}

// A table's row (the loot filter list's line, stretched) and its column line.
inline std::string TableRow(int width, int height) {
	return Detail::Once("tablerow" + Detail::Size(width, height), [&] {
		const auto& row = Detail::Part(Kit::ListRow);
		return std::vector<Picture> { row.empty() ? D2RGui::SlotButton(width, height)[0] : Stretch9(row[0], width, height, 24, 16) };
	});
}
inline std::string TableColumn(int height) {
	return Detail::Once("tablecol" + std::to_string(height), [&] {
		const auto& column = Detail::Part(Kit::ListColumn);
		return std::vector<Picture> { column.empty() ? FadedRule(2, height) : ResizeBox(column[0], column[0].width, height) };
	});
}

// A drop-down's box (frames 0 normal, 1 pressed, 2 disabled, 3 hovered).
inline std::string DropBox(int width, int height) {
	return Detail::Once("dropbox" + Detail::Size(width, height), [&] {
		const auto &l = Detail::Part(Kit::DropLeft), &m = Detail::Part(Kit::DropCenter), &r = Detail::Part(Kit::DropRight);
		if (!l.empty() && !m.empty() && !r.empty()) {
			std::vector<Picture> box;
			for (std::size_t f = 0; f < 4; ++f) box.push_back(JoinStretched(Detail::At(l, f), Detail::At(m, f), Detail::At(r, f), width, height));
			return box;
		}
		auto frames = D2RGui::SlotButton(width, height);
		for (auto& frame : frames) DrawDownArrow(frame, width - 30, height / 2, Detail::Gold);
		return std::vector<Picture> { frames[0], frames[2], frames[0], frames[1] };
	});
}

// A line of a drop-down's open list (frames 0 normal, 1 hovered).
inline std::string DropTile(int width, int height) {
	return Detail::Once("droptile" + Detail::Size(width, height), [&] {
		const auto &l = Detail::Part(Kit::TileLeft), &m = Detail::Part(Kit::TileCenter), &r = Detail::Part(Kit::TileRight);
		if (!l.empty() && !m.empty() && !r.empty()) {
			std::vector<Picture> tile;
			for (std::size_t f = 0; f < 2; ++f) tile.push_back(JoinStretched(Detail::At(l, f), Detail::At(m, f), Detail::At(r, f), width, height));
			return tile;
		}
		auto frames = D2RGui::SlotButton(width, height);
		return std::vector<Picture> { frames[0], frames[1] };
	});
}

// A small plate for a step button (<, >): the drop-down's left end on both
// sides (its right end has the arrow); frames as DropBox.
inline std::string StepBox(int width, int height) {
	return Detail::Once("stepbox" + Detail::Size(width, height), [&] {
		const auto &l = Detail::Part(Kit::DropLeft), &m = Detail::Part(Kit::DropCenter);
		if (!l.empty() && !m.empty()) {
			std::vector<Picture> box;
			for (std::size_t f = 0; f < 4; ++f) {
				const Picture& left = Detail::At(l, f);
				box.push_back(JoinStretched(left, Detail::At(m, f), Mirror(left), width, height));
			}
			return box;
		}
		auto plain = D2RGui::SlotButton(width, height);
		return std::vector<Picture> { plain[0], plain[2], plain[0], plain[1] };
	});
}

// A plain slot plate for a button with text on it (frames as DropBox).
inline std::string PlainButton(int width, int height) {
	return Detail::Once("plain" + Detail::Size(width, height), [&] {
		auto plain = D2RGui::SlotButton(width, height);
		return std::vector<Picture> { plain[0], plain[2], plain[0], plain[1] };
	});
}

// A text box's frame: a line of the drop-down's list (one frame).
inline std::string InputBox(int width, int height) {
	return Detail::Once("input" + Detail::Size(width, height), [&] {
		const auto &l = Detail::Part(Kit::TileLeft), &m = Detail::Part(Kit::TileCenter), &r = Detail::Part(Kit::TileRight);
		if (!l.empty() && !m.empty() && !r.empty()) return std::vector<Picture> { JoinStretched(l[0], m[0], r[0], width, height) };
		return std::vector<Picture> { D2RGui::SlotButton(width, height)[0] };
	});
}

// The loot filter list's up or down arrow, `size` square (frames 0 normal, 1 hovered, 2 pressed).
inline std::string Arrow(bool up, int size) {
	return Detail::Once(std::string(up ? "up" : "down") + std::to_string(size), [&] {
		const auto& art = Detail::Part(up ? Kit::ListUp : Kit::ListDown);
		if (art.size() >= 3) return Trimmed(art, size, size);
		auto plain = D2RGui::SlotButton(size, size);
		for (auto& frame : plain) {
			DrawDownArrow(frame, size / 2, size / 2, Detail::Gold);
			if (up) frame = UpsideDown(frame);
		}
		return plain;
	});
}

// A group's arrow, `size` square: the loot filter's down arrow while open,
// turned to point right while closed (frames 0 normal, 1 hovered, 2 pressed).
inline std::string GroupArrow(bool open, int size) {
	return Detail::Once(std::string(open ? "open" : "closed") + std::to_string(size), [&] {
		const auto&          art = Detail::Part(Kit::ListDown);
		std::vector<Picture> down;
		if (art.size() >= 3) down = Trimmed(art, size, size);
		else {
			auto plain = D2RGui::SlotButton(size, size);
			for (auto& frame : plain) DrawDownArrow(frame, size / 2, size / 2, Detail::Gold);
			down = plain;
		}
		if (!open)
			for (auto& frame : down) frame = TurnedRight(frame);
		return down;
	});
}

// The Chronicle's New Rule button as Add, width x height (frames 0 normal, 1 hovered, 2 pressed).
inline std::string Add(int width, int height) {
	return Detail::Once("add" + Detail::Size(width, height), [&] {
		const auto& art = Detail::Part(Kit::AddArt);
		if (art.size() >= 3) return Trimmed(art, width, height);
		auto plain = D2RGui::SlotButton(width, height);
		for (auto& frame : plain) DrawPlus(frame, Detail::Gold);
		return plain;
	});
}

// The game's close button as a remove button, `size` square (frames as DropBox).
inline std::string Remove(int size) {
	return Detail::Once("remove" + std::to_string(size), [&] {
		const auto& art = Detail::Part(Kit::Close);
		if (art.size() >= 4) return Trimmed(art, size, size);
		auto plain = D2RGui::SlotButton(size, size);
		return std::vector<Picture> { plain[0], plain[2], plain[0], plain[1] };
	});
}

// The loot filter list's line stretched to width x height (a box's back).
inline std::string Back(int width, int height) {
	return Detail::Once("back" + Detail::Size(width, height), [&] {
		const auto& row = Detail::Part(Kit::ListRow);
		return std::vector<Picture> { row.empty() ? D2RGui::SlotButton(width, height)[0] : Stretch9(row[0], width, height, 24, 16) };
	});
}

// The lobby's refresh button fitted in `size` square, all its frames; empty
// when the game's was not found. RefreshFrames gives its frames.
inline std::string Refresh(int size) {
	auto& kit = Kit::Get();
	if (kit.refreshFrames.empty()) return {};
	return Detail::Once("refresh" + std::to_string(size), [&] {
		auto frames = kit.refreshFrames;
		for (auto& frame : frames) frame = FitBox(frame, size, size);
		return frames;
	});
}
inline Frames RefreshFrames() {
	const auto& kit   = Kit::Get();
	const int   count = std::max(1, static_cast<int>(kit.refreshFrames.size()));
	return { std::clamp(kit.refresh.normal, 0, count - 1), std::clamp(kit.refresh.pressed, 0, count - 1), std::clamp(kit.refresh.hovered, 0, count - 1) };
}

// An empty stash slot width x height (one frame).
inline std::string Slot(int width, int height) {
	return Detail::Once("slot" + Detail::Size(width, height), [&] { return std::vector<Picture> { D2RGui::Slot(width, height) }; });
}

// The top of a slot cut by the view's end: its left, right and top edges.
inline std::string SlotTop(int width, int height) {
	return Detail::Once("slottop" + Detail::Size(width, height), [&] { return std::vector<Picture> { D2RGui::SlotTop(width, height) }; });
}

// A see-through black veil over width x height (175 of 255).
inline std::string Veil(int width, int height) {
	return Detail::Once("veil" + Detail::Size(width, height), [&] { return std::vector<Picture> { D2RGui::Veil(width, height, 175) }; });
}

// A perfect item's glow around a slot width x height (GlowFrames).
inline std::string Glow(int width, int height, int pad, int frames) {
	return Detail::Once("glow" + Detail::Size(width, height) + "p" + std::to_string(pad) + "f" + std::to_string(frames), [&] { return GlowFrames(width, height, pad, frames); });
}

// The stash's grained dark stone, width x height (a cover over a grid).
inline std::string Stone(int width, int height, const int color[3], int grain, std::uint32_t seed) {
	return Detail::Once("stone" + Detail::Size(width, height) + "-" + std::to_string(color[0]) + "." + std::to_string(color[1]) + "." + std::to_string(color[2]) + "g" + std::to_string(grain) + "s"
	                        + std::to_string(seed),
	                    [&] { return std::vector<Picture> { D2RGui::Stone(width, height, color, grain, seed) }; });
}

// A ring chart's sprite (Ring), named by its numbers: a changed chart is a new sprite.
inline std::string Chart(const std::vector<std::pair<int, Rgb>>& parts, int size, int hole, bool square, Rgb empty) {
	std::string name = std::string(square ? "square" : "chart") + std::to_string(size) + "h" + std::to_string(hole);
	for (const auto& [value, color] : parts) name += "-" + std::to_string(value) + "_" + std::to_string(color.r) + "." + std::to_string(color.g) + "." + std::to_string(color.b);
	return Detail::Once(name, [&] { return std::vector<Picture> { Ring(parts, size, hole, square, empty) }; });
}

} // namespace D2RGui::StashKit
