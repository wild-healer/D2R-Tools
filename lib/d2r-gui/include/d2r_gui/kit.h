// D2R GUI: the game's own settings art (drop-down box and list, loot filter
// rows and arrows, close button, the lobby's refresh button), read once from
// the game and turned into sprites of any size on demand. Each part falls back
// to art of the plugin's own when the game's can't be read.
//
//   Kit::Read()                 game thread, after the data tables loaded
//   Kit::Box(width, height)     UI thread, while building a layout: the
//   Kit::Button(...), ...       sprite's name for "filename", registered the
//                               first time a size is asked for
#pragma once
#include "art.h"
#include "game_files.h"
#include "resources.h"
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace D2RGui::Kit {

enum Part { DropLeft, DropCenter, DropRight, TileLeft, TileCenter, TileRight, ListRow, ListUp, ListDown, Close, Background, PartCount };

inline constexpr const char* PartPaths[PartCount] {
	"data/hd/global/ui/panel/dropdownbutton_left.sprite",
	"data/hd/global/ui/panel/dropdownbutton_center.sprite",
	"data/hd/global/ui/panel/dropdownbutton_right.sprite",
	"data/hd/global/ui/panel/dropdowntile_left.sprite",
	"data/hd/global/ui/panel/dropdowntile_center.sprite",
	"data/hd/global/ui/panel/dropdowntile_right.sprite",
	"data/hd/global/ui/panel/loot_filter/slot_large_list.sprite",
	"data/hd/global/ui/panel/loot_filter/slot_large_list_btn_1.sprite",
	"data/hd/global/ui/panel/loot_filter/slot_large_list_btn_2.sprite",
	"data/hd/global/ui/panel/closebtn_4x.sprite",
	"data/hd/global/ui/panel/character_sheet/character_background.sprite",
};

// Native image names a layout can use as they are (no plugin sprite needed).
namespace Native {
inline constexpr const char* LeftArrow   = "PauseMenu\\\\LeftArrow";
inline constexpr const char* RightArrow  = "PauseMenu\\\\RightArrow";
inline constexpr const char* CheckBox    = "PANEL\\\\loot_filter\\\\lootfilter_large_list_btn_checkbox"; // 0-3 empty, 4-7 ticked (normal, pressed, disabled, hovered)
inline constexpr const char* OpenArrow   = "PANEL\\\\Character_Sheet\\\\advancedstatsbutton";
inline constexpr const char* AddButton   = "PANEL\\\\loot_filter\\\\chronicle_btn_add";
inline constexpr const char* CloseButton = "PANEL\\\\closebtn_4x";
inline constexpr const char* CharacterBackground = "PANEL\\\\Character_Sheet\\\\Character_Background";
} // namespace Native

struct State {
	std::mutex                         lock;
	bool                               read {};
	std::vector<Picture>               parts[PartCount];
	NativeButton                       refresh;
	std::vector<Picture>               refreshFrames;
	std::map<std::string, std::string> made; // key -> sprite name
};

inline State& Get() {
	static State state;
	return state;
}

// Game thread (or UI thread), after the data tables loaded. Safe to call again.
inline void Read() {
	auto&                 s = Get();
	std::vector<Picture>  parts[PartCount];
	for (int k = 0; k < PartCount; ++k) {
		std::string trouble;
		const auto  file = ArchiveFile(PartPaths[k], &trouble);
		if (!file.empty()) parts[k] = ReadSprite(file, &trouble);
		if (parts[k].empty()) Log(true, "d2r-gui: game art %s not used: %s", PartPaths[k], trouble.c_str());
	}
	NativeButton         refresh = FindRefreshButton();
	std::vector<Picture> refreshFrames;
	if (!refresh.file.empty()) refreshFrames = ReadSprite(refresh.file);
	refresh.file.clear(), refresh.file.shrink_to_fit();
	std::lock_guard guard(s.lock);
	for (int k = 0; k < PartCount; ++k) s.parts[k] = std::move(parts[k]);
	s.refresh       = refresh;
	s.refreshFrames = std::move(refreshFrames);
	s.read          = true;
	s.made.clear();
}

inline const std::vector<Picture>& PartFrames(Part part) { return Get().parts[part]; }

namespace Detail {
inline const Picture& FrameOr(const std::vector<Picture>& frames, std::size_t f) {
	static const Picture none;
	return frames.empty() ? none : frames[std::min(f, frames.size() - 1)];
}

// The sprite named `name` made by `make` once (UI thread).
template <typename Make>
std::string Once(const std::string& name, Make make) {
	auto& s = Get();
	{
		std::lock_guard guard(s.lock);
		if (s.made.count(name)) return s.made[name];
	}
	std::vector<Picture> frames;
	{
		std::lock_guard guard(s.lock);
		frames = make();
	}
	const bool ok = RegisterSprite(name, frames);
	std::lock_guard guard(s.lock);
	if (ok) s.made[name] = SpriteName(name);
	return ok ? s.made[name] : std::string();
}
} // namespace Detail

// A text box's frame (one frame): the drop-down box's left end, its middle
// tiled, the left end mirrored on the right.
inline std::string Box(int width, int height = 56) {
	return Detail::Once("box" + std::to_string(width) + "x" + std::to_string(height), [&] {
		const auto& p = Get().parts;
		const bool  ok = !p[DropLeft].empty() && !p[DropCenter].empty();
		return std::vector<Picture> { ok ? JoinTiled(p[DropLeft][0], p[DropCenter][0], {}, width, height) : PlainBox(width, height, 60) };
	});
}

// A button with text on it: the drop-down box's frames (0 normal, 1 pressed,
// 2 disabled, 3 hovered). Use normalFrame 0, pressedFrame 1, hoveredFrame 3.
inline std::string Button(int width, int height = 56) {
	return Detail::Once("button" + std::to_string(width) + "x" + std::to_string(height), [&] {
		const auto&          p  = Get().parts;
		const bool           ok = p[DropLeft].size() >= 4 && p[DropCenter].size() >= 4;
		std::vector<Picture> frames;
		for (std::size_t f = 0; f < 4; ++f) frames.push_back(ok ? JoinTiled(p[DropLeft][f], p[DropCenter][f], {}, width, height) : PlainBox(width, height, f == 1 || f == 3 ? 170 : 90));
		return frames;
	});
}

// A drop-down's closed box, with its arrow end (frames as Button).
inline std::string DropBox(int width, int height = 56) {
	return Detail::Once("dropbox" + std::to_string(width) + "x" + std::to_string(height), [&] {
		const auto&          p  = Get().parts;
		const bool           ok = !p[DropLeft].empty() && !p[DropCenter].empty();
		std::vector<Picture> frames;
		for (std::size_t f = 0; f < 4; ++f)
			frames.push_back(ok ? JoinTiled(Detail::FrameOr(p[DropLeft], f), Detail::FrameOr(p[DropCenter], f), Detail::FrameOr(p[DropRight], f), width, height)
			                    : PlainBox(width, height, f == 3 ? 170 : 90));
		return frames;
	});
}

// A line of an open drop-down's list (0 normal, 1 hovered).
inline std::string DropTile(int width, int height = 52) {
	return Detail::Once("droptile" + std::to_string(width) + "x" + std::to_string(height), [&] {
		const auto&          p  = Get().parts;
		const bool           ok = !p[TileLeft].empty() && !p[TileCenter].empty() && !p[TileRight].empty();
		std::vector<Picture> frames;
		for (std::size_t f = 0; f < 2; ++f)
			frames.push_back(ok ? JoinTiled(Detail::FrameOr(p[TileLeft], f), Detail::FrameOr(p[TileCenter], f), Detail::FrameOr(p[TileRight], f), width, height)
			                    : PlainBox(width, height, f ? 170 : 70));
		return frames;
	});
}

// The loot filter list's row stretched to `width` x `height` (one frame).
inline std::string Row(int width, int height) {
	return Detail::Once("row" + std::to_string(width) + "x" + std::to_string(height), [&] {
		const auto& p = Get().parts;
		return std::vector<Picture> { !p[ListRow].empty() ? StretchEnds(p[ListRow][0], width, height, 24, 16) : PlainBox(width, height, 90) };
	});
}

// The loot filter list's up or down arrow, cut to its art, `size` square (0 normal, 1 hovered, 2 pressed).
inline std::string Arrow(bool up, int size = 56) {
	return Detail::Once(std::string(up ? "uparrow" : "downarrow") + std::to_string(size), [&] {
		const auto& frames = Get().parts[up ? ListUp : ListDown];
		if (frames.size() >= 3) return Fitted(frames, size);
		std::vector<Picture> own = SlotButton(size, size);
		const unsigned char  gold[3] { 215, 200, 150 };
		for (auto& f : own) {
			Picture arrow = Blank(size, size);
			DrawDownArrow(arrow, size / 2, size / 2, gold);
			if (up)
				for (int y = 0; y < size / 2; ++y)
					for (int x = 0; x < size; ++x) std::swap(*reinterpret_cast<std::uint32_t*>(arrow.at(x, y)), *reinterpret_cast<std::uint32_t*>(arrow.at(x, size - 1 - y)));
			Blend(f, arrow, 0, 0);
		}
		return own;
	});
}

// The close button, `size` square (frames 0-3 as the game's: normal, pressed, disabled, hovered).
inline std::string Remove(int size = 52) {
	return Detail::Once("remove" + std::to_string(size), [&] {
		auto frames = Get().parts[Close];
		if (frames.size() >= 4) {
			for (auto& f : frames) f = Resize(f, size, size);
			return frames;
		}
		auto own = SlotButton(size, size);
		own.insert(own.begin() + 2, own[0]); // normal, pressed, disabled, hovered
		std::swap(own[1], own[3]);
		return own;
	});
}

// The reset button `size` square (0 normal, 1 hovered, 2 pressed): the lobby's
// refresh button when it was found, else a slot with a circular arrow.
inline std::string Reset(int size = 56) {
	return Detail::Once("reset" + std::to_string(size), [&] {
		auto&     s     = Get();
		const int count = static_cast<int>(s.refreshFrames.size());
		if (count > 0) {
			const auto boxed = Boxed(s.refreshFrames, size);
			return std::vector<Picture> { boxed[std::clamp(s.refresh.normal, 0, count - 1)], boxed[std::clamp(s.refresh.hovered, 0, count - 1)], boxed[std::clamp(s.refresh.pressed, 0, count - 1)] };
		}
		return ResetButton(size);
	});
}

// The gear button `size` square (0 normal, 1 hovered, 2 pressed).
inline std::string Gear(int size = 64) {
	return Detail::Once("gear" + std::to_string(size), [&] { return GearButton(size); });
}

// A slot button `width` x `height` (0 normal, 1 hovered, 2 pressed), gold-rimmed when `open`.
inline std::string SlotButtonSprite(int width, int height, bool open = false) {
	return Detail::Once("slot" + std::to_string(width) + "x" + std::to_string(height) + (open ? "on" : ""), [&] { return SlotButton(width, height, open); });
}

// The plugin's check box `size` square (0 normal, 1 hovered, 2 pressed).
inline std::string Check(bool on, int size = 56) {
	return Detail::Once("check" + std::to_string(size) + (on ? "on" : ""), [&] { return CheckBox(on, size); });
}

// The invisible button's sprite (0 clear, 1 faintly lit).
inline std::string Hover() {
	return Detail::Once("blank", [] { return HoverBlank(); });
}

// A recessed scroll track and a bronze thumb.
inline std::string Track(int width, int height) {
	return Detail::Once("track" + std::to_string(width) + "x" + std::to_string(height), [&] { return std::vector<Picture> { Slot(width, height) }; });
}
inline std::string Thumb(int width, int height) {
	return Detail::Once("thumb" + std::to_string(width) + "x" + std::to_string(height), [&] { return std::vector<Picture> { ScrollThumb(width, height) }; });
}

// A round gem in a gold setting, `size` wide (named by the caller: one per colour).
inline std::string Gem(const std::string& name, const int color[3], int size = 40) {
	return Detail::Once("gem" + name + std::to_string(size), [&] { return std::vector<Picture> { D2RGui::Gem(color, size) }; });
}

// A gold rule `width` wide.
inline std::string Rule(int width) {
	return Detail::Once("rule" + std::to_string(width), [&] { return std::vector<Picture> { GoldRule(width) }; });
}

} // namespace D2RGui::Kit
