// D2R GUI: items drawn in a layout as the game's grids draw them: their art
// recoloured by the game's colours (UniqueItems invtransform), in gray for
// what is not found yet, their sockets and runes, the quality backgrounds of
// Item Quality Backgrounds, and the Chronicle's grey rune stones.
#pragma once
#include "core.h"
#include "json_reader.h"
#include "layout.h"
#include "picture.h"
#include "resources.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace D2RGui::ItemArt {

// An image's recolouring fields: every pixel whose hue, saturation and value
// are in range ([low, high] each) gets the hsv change, then the tint.
inline std::string RecolorFields(const float hue[2], const float saturation[2], const float value[2], const float hsv[4], const float tint[4]) {
	char text[400] {};
	std::snprintf(text, sizeof(text),
	              R"(, "useAdvancedColoring": true, "recolorRange": {"hue": [%g, %g], "saturation": [%g, %g], "value": [%g, %g]}, "hsvTransform": [%g, %g, %g, %g], "tintTransform": [%g, %g, %g, %g])",
	              hue[0], hue[1], saturation[0], saturation[1], value[0], value[1], hsv[0], hsv[1], hsv[2], hsv[3], tint[0], tint[1], tint[2], tint[3]);
	return text;
}

// Item art in gray, the way the Item Dictionary mod recolours item art: every
// hue, saturation and brightness is in range ([middle, half width]), then
// saturation and brightness are shifted (in %, -100 .. 100).
inline std::string GrayFields(int saturation, int brightness) {
	char text[320] {};
	std::snprintf(text, sizeof(text),
	              R"(, "useAdvancedColoring": true, "recolorRange": {"hue": [0.5, 0.501], "saturation": [0.5, 0.501], "value": [0.5, 0.501]}, "hsvTransform": [0.0, %.2f, %.2f, 0.0], "tintTransform": [0.0, 0.0, 0.0, 0.0])",
	              saturation / 100.0, brightness / 100.0);
	return text;
}

// The see-through look of ethereal items.
inline constexpr const char* EthLook = ", \"transparency\": 0.45";

// ---- the game's item colours ----
// GlobalData's SpriteColoringHelper (data/global/ui/layouts/globaldatahd.json)
// gives each colour an hsv and a tint transform, applied to the pixels in the
// art's colour range (by its base's InvTrans class, or by its sprite).
struct ColorRange {
	bool  empty = true;
	float hue[2] {}, saturation[2] {}, value[2] {};
};

struct Colors {
	std::mutex                        lock;
	std::vector<ColorRange>           ranges;     // by colour class
	std::vector<std::array<float, 8>> transforms; // by colour: hsv[4], tint[4]
	std::map<std::string, ColorRange> overrides;  // by sprite ("items/armor/helmet/helm")
};

inline Colors& GetColors() {
	static Colors colors;
	return colors;
}

inline ColorRange ReadRange(const JsonValue& v) {
	ColorRange range;
	if (v.kind != JsonValue::Object) return range;
	const auto pair = [&](const char* key, float* out) {
		const auto* field = v.get(key);
		if (!field || field->items.size() < 2) return false;
		out[0] = static_cast<float>(field->items[0].number), out[1] = static_cast<float>(field->items[1].number);
		return true;
	};
	range.empty = !(pair("hue", range.hue) && pair("saturation", range.saturation) && pair("value", range.value));
	return range;
}

// Any thread: reads the colours from GlobalData's text. False when it has no
// SpriteColoringHelper (art then keeps its own colours). Counts for the log.
inline bool ReadColors(const std::string& text, std::size_t* counts = nullptr) {
	if (text.empty()) return false;
	const JsonValue               root = ReadJson(text);
	std::vector<const JsonValue*> stack { &root };
	const JsonValue*              fields = nullptr;
	while (!stack.empty() && !fields) {
		const auto* node = stack.back();
		stack.pop_back();
		if (const auto* type = node->get("type"); type && type->text == "SpriteColoringHelper") fields = node->get("fields");
		if (const auto* children = node->get("children"))
			for (const auto& child : children->items) stack.push_back(&child);
	}
	if (!fields) return false;
	std::vector<ColorRange>           ranges;
	std::vector<std::array<float, 8>> transforms;
	std::map<std::string, ColorRange> overrides;
	if (const auto* list = fields->get("colorRange"))
		for (const auto& item : list->items) ranges.push_back(ReadRange(item));
	if (const auto* list = fields->get("colorTransform"))
		for (const auto& item : list->items) {
			std::array<float, 8> t {};
			for (int half = 0; half < 2; ++half)
				if (const auto* values = item.get(half ? "tintTransform" : "hsvTransform"))
					for (std::size_t i = 0; i < 4 && i < values->items.size(); ++i) t[half * 4 + i] = static_cast<float>(values->items[i].number);
			transforms.push_back(t);
		}
	if (const auto* list = fields->get("colorRangeOverride"))
		for (const auto& item : list->items)
			if (item.items.size() >= 2) {
				std::string path = item.items[0].text;
				for (char& c : path) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c == '\\' ? '/' : c)));
				if (path.rfind("data/hd/global/ui/", 0) == 0) path.erase(0, 18);
				overrides[path] = ReadRange(item.items[1]);
			}
	auto&           c = GetColors();
	std::lock_guard guard(c.lock);
	c.ranges.swap(ranges);
	c.transforms.swap(transforms);
	c.overrides.swap(overrides);
	if (counts) counts[0] = c.ranges.size(), counts[1] = c.transforms.size(), counts[2] = c.overrides.size();
	return true;
}

// The recolouring fields of art `art` (its sprite path, as the tables give
// it) in colour `color` (invtransform, -1 none), its base's colour class
// `colorClass` (InvTrans); empty when it keeps its colours.
inline std::string ColorFields(int color, int colorClass, const std::string& art) {
	auto&           c = GetColors();
	std::lock_guard guard(c.lock);
	if (color < 0 || color >= static_cast<int>(c.transforms.size())) return {};
	std::string key = art;
	for (char& ch : key) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch == '\\' ? '/' : ch)));
	const ColorRange* range = nullptr;
	if (const auto found = c.overrides.find(key); found != c.overrides.end()) range = &found->second;
	else if (colorClass >= 0 && colorClass < static_cast<int>(c.ranges.size())) range = &c.ranges[colorClass];
	if (!range || range->empty) return {};
	const auto& t = c.transforms[color];
	return RecolorFields(range->hue, range->saturation, range->value, t.data(), t.data() + 4);
}

// ---- sockets ----

// Where an item of width x height cells has its sockets, in socket order:
// their centres in cells, and the room each has (1 for a whole cell). One
// column for narrow items and up to 3 sockets, two columns from 4 (5 as
// 2-1-2), like the game's sockets.
inline std::pair<std::vector<std::pair<double, double>>, double> SocketSpots(int width, int height, int count) {
	std::vector<std::pair<double, double>> spots;
	if (count <= 0) return { spots, 1.0 };
	const double     midX = width / 2.0, midY = height / 2.0;
	std::vector<int> rows; // sockets per row
	if (width < 2 || (count <= 3 && height >= count)) rows.assign(count, 1);
	else if (count == 3) rows = { 2, 1 };
	else if (count == 5) rows = { 2, 1, 2 };
	else rows.assign((count + 1) / 2, 2);
	const double step = std::min(1.0, static_cast<double>(height) / rows.size());
	for (std::size_t r = 0; r < rows.size(); ++r) {
		const double y = midY + (r - (rows.size() - 1) / 2.0) * step;
		if (rows[r] == 1) spots.push_back({ midX, y });
		else spots.push_back({ midX - 0.5, y }), spots.push_back({ midX + 0.5, y });
	}
	// The game's order: 2-2 top left, bottom right, bottom left, top right;
	// 2-1-2 the middle one, then the same.
	const auto order = [&](std::initializer_list<int> at) {
		std::vector<std::pair<double, double>> sorted;
		for (const int k : at) sorted.push_back(spots[k]);
		spots = std::move(sorted);
	};
	if (rows.size() == 2 && rows[0] == 2 && rows[1] == 2) order({ 0, 3, 2, 1 });
	else if (rows.size() == 3 && rows[0] == 2 && rows[1] == 1 && rows[2] == 2) order({ 2, 0, 4, 3, 1 });
	return { spots, step };
}

// The game's empty socket (its picture is `socketSize` pixels tall), scaled
// to `room` pixels, centred at cx, cy.
inline std::string Socket(const std::string& name, double cx, double cy, double room, int socketSize, const std::string& extra = ", \"transparency\": 0.6") {
	return ScaledImage(name, static_cast<int>(cx - room / 2), static_cast<int>(cy - room / 2), room / std::max(socketSize, 1), "PANEL\\\\gemsocket", extra);
}

// ---- Item Quality Backgrounds ----

// Whether Item Quality Backgrounds is loaded (its background sprites can then
// be drawn under items the plugin draws itself).
inline bool QualityBackgrounds() { return GetModuleHandleW(L"d2rl-item-quality-backgrounds.dll") != nullptr; }

// Its background for `quality` ("unique", "set", "runeword", "normal") under
// an item of width x height cells.
inline std::string QualityBackground(const std::string& name, int x, int y, const char* quality, int width, int height, const std::string& extra) {
	return Image(name, x, y, "d2rloader\\\\item-quality-backgrounds\\\\" + std::string(quality) + "\\\\" + std::to_string(width) + "x" + std::to_string(height), extra);
}

// ---- the Chronicle's rune stones ----
// The Chronicle's grey rune stones: a sprite of `cell`-pixel cells, the 33
// runes by English name (Amn to Zod) in reading order. Registered as
// "<prefix><n>", with a blank stone "<prefix>-blank" made from them (each
// pixel the brighter quarter's edge of all the stones': the glyphs are dark
// and never in the same place). Returns how many stones (0: none).
inline int RegisterRuneStones(const std::string& file, int cell, const std::string& prefix) {
	if (file.size() < 0x28 || file.compare(0, 4, "SpA1")) return 0;
	const auto u32 = [&](std::size_t at) {
		std::uint32_t v;
		std::memcpy(&v, file.data() + at, 4);
		return v;
	};
	const int width = static_cast<int>(u32(8)), height = static_cast<int>(u32(12));
	if (width < cell || height < cell || file.size() < 0x28 + static_cast<std::size_t>(width) * height * 4) return 0;
	const int columns = width / cell, count = columns * (height / cell);
	int       made    = 0;
	for (int k = 0; k < count; ++k) {
		Picture stone = Blank(cell, cell);
		bool    empty = true;
		for (int y = 0; y < cell; ++y) {
			std::memcpy(stone.at(0, y), file.data() + 0x28 + (static_cast<std::size_t>((k / columns) * cell + y) * width + (k % columns) * cell) * 4, static_cast<std::size_t>(cell) * 4);
			for (int x = 0; x < cell && empty; ++x) empty = stone.at(x, y)[3] == 0;
		}
		if (empty || !RegisterSprite(prefix + std::to_string(k), { stone })) break;
		made = k + 1;
	}
	if (made >= 8) {
		Picture                    blank = Blank(cell, cell);
		std::vector<unsigned char> values(made);
		for (int y = 0; y < cell; ++y)
			for (int x = 0; x < cell; ++x)
				for (int c = 0; c < 4; ++c) {
					for (int k = 0; k < made; ++k) values[k] = static_cast<unsigned char>(file[0x28 + (static_cast<std::size_t>((k / columns) * cell + y) * width + (k % columns) * cell + x) * 4 + c]);
					std::nth_element(values.begin(), values.begin() + made * 3 / 4, values.end());
					blank.at(x, y)[c] = values[made * 3 / 4];
				}
		if (!RegisterSprite(prefix + "-blank", { blank })) made = 0;
	}
	return made;
}

// A rune's stone cell (r01 El .. r33 Zod -> its place by name in the sheet).
inline constexpr int RuneStoneCell[] { 4, 5, 29, 19, 6, 12, 27, 23, 21, 28, 0, 25, 24, 3, 9, 10, 17, 14, 7, 15, 22, 30, 18, 11, 8, 31, 20, 16, 26, 1, 13, 2, 32 };

} // namespace D2RGui::ItemArt
