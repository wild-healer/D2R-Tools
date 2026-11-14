// D2R GUI: art drawn by the plugin in the game's look, for when the game's
// own art is missing or there is none (slots, gear and reset buttons, check
// boxes, scroll bar parts, gems, gold rules).
#pragma once
#include "art_data.inc"
#include "picture.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace D2RGui {

// Frame shifts and gold tones of a slot button: normal, hovered, pressed.
inline constexpr int ButtonShift[3] { 0, 22, -8 };
inline constexpr int ButtonGold[3][3] { { 215, 200, 150 }, { 255, 240, 190 }, { 180, 165, 120 } };

// An empty stash slot of any size: the slot's corners and edges stretched
// around grained stone of its inside colour.
inline Picture Slot(int width, int height) {
	using namespace ArtData;
	const int  s = SlotSize, c = SlotCorner;
	const auto src = [&](int x, int y) { return SlotPixels + (static_cast<std::size_t>(y) * s + x) * 4; };
	long       sum[3] {}, count = 0;
	for (int y = c; y < s - c; ++y)
		for (int x = c; x < s - c; ++x, ++count)
			for (int k = 0; k < 3; ++k) sum[k] += src(x, y)[k];
	const int inside[3] { static_cast<int>(sum[0] / count), static_cast<int>(sum[1] / count), static_cast<int>(sum[2] / count) };
	Picture   out = Stone(width, height, inside, 10, 7);
	const int cw = std::min(c, width / 2), ch = std::min(c, height / 2);
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x) {
			const bool left = x < cw, right = x >= width - cw, top = y < ch, bottom = y >= height - ch;
			if (!left && !right && !top && !bottom) continue;
			const int sx = left ? x : right ? s - (width - x) : s / 2;
			const int sy = top ? y : bottom ? s - (height - y) : s / 2;
			std::memcpy(out.at(x, y), src(sx, sy), 4);
		}
	return out;
}

// A slot cut by a view's bottom edge: its left, right and top edges only.
inline Picture SlotTop(int width, int height) {
	using namespace ArtData;
	const int s = SlotSize, c = SlotCorner;
	Picture   out = Blank(width, height);
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x) {
			const bool left = x < c, right = x >= width - c, top = y < c;
			if (!left && !right && !top) continue;
			const int sx = left ? x : right ? s - (width - x) : s / 2;
			const int sy = top ? y : s / 2;
			std::memcpy(out.at(x, y), SlotPixels + (static_cast<std::size_t>(sy) * s + sx) * 4, 4);
		}
	return out;
}

// A slot button (frames 0 normal, 1 hovered, 2 pressed); `open` gives it the
// stash grid's gold rim (a selected tab).
inline std::vector<Picture> SlotButton(int width, int height, bool open = false) {
	std::vector<Picture> frames;
	for (const int shift : ButtonShift) {
		Picture picture = Slot(width, height);
		for (int y = 0; y < height; ++y)
			for (int x = 0; x < width; ++x) {
				auto*     p    = picture.at(x, y);
				const int edge = std::min({ x, y, width - 1 - x, height - 1 - y });
				if (open && edge < 3) {
					static constexpr unsigned char gold[3][3] { { 80, 66, 40 }, { 195, 180, 111 }, { 253, 252, 213 } };
					std::memcpy(p, gold[edge], 3);
					continue;
				}
				for (int k = 0; k < 3; ++k) p[k] = static_cast<unsigned char>(std::clamp(p[k] + shift, 0, 255));
			}
		frames.push_back(std::move(picture));
	}
	return frames;
}

// An icon (a coverage mask, `maskSize` square) in the game's gold on a slot
// button `size` square, filling `percent` of it.
inline std::vector<Picture> IconButton(const unsigned char* mask, int maskSize, int size, int percent = 66) {
	Picture icon = Blank(maskSize, maskSize);
	for (int p = 0; p < maskSize * maskSize; ++p) icon.rgba[static_cast<std::size_t>(p) * 4 + 3] = mask[p];
	const int side = size * percent / 100, at = (size - side) / 2;
	icon           = Resize(icon, side, side);
	std::vector<Picture> frames;
	for (int f = 0; f < 3; ++f) {
		Picture picture = Slot(size, size);
		Shade(picture, ButtonShift[f]);
		for (int y = 0; y < side; ++y)
			for (int x = 0; x < side; ++x) {
				const double a = icon.at(x, y)[3] / 255.0;
				auto*        p = picture.at(at + x, at + y);
				for (int k = 0; k < 3; ++k) p[k] = static_cast<unsigned char>(std::lround(p[k] * (1 - a) + ButtonGold[f][k] * a));
			}
		frames.push_back(std::move(picture));
	}
	return frames;
}

// The square gear button (opens a plugin's settings).
inline std::vector<Picture> GearButton(int size) { return IconButton(ArtData::CogMask, ArtData::CogSize, size); }

// A reset button: a slot with a gold circular arrow (when the lobby's refresh
// button can't be read: see FindRefreshButton).
inline std::vector<Picture> ResetButton(int size) {
	std::vector<Picture> frames;
	const double         c = size / 2.0, r = size * 0.27, w = size * 0.06;
	constexpr double     pi = 3.14159265358979;
	for (int f = 0; f < 3; ++f) {
		Picture picture = Slot(size, size);
		for (int y = 0; y < size; ++y)
			for (int x = 0; x < size; ++x) {
				auto*        p     = picture.at(x, y);
				const double dx    = x + 0.5 - c, dy = y + 0.5 - c, d = std::sqrt(dx * dx + dy * dy);
				const double angle = std::atan2(dy, dx); // 0 right, pi/2 down
				double       a     = 0;
				if (!(angle < -pi / 4 && angle > -pi / 2 - 0.5)) a = std::clamp(w + 0.5 - std::abs(d - r), 0.0, 1.0); // the ring, open at the top right
				const double hx = c + r * std::cos(-pi / 4), hy = c + r * std::sin(-pi / 4);
				const double ux = x + 0.5 - hx, uy = y + 0.5 - hy;
				const double along = (ux * 0.7071 + uy * 0.7071), across = (ux * 0.7071 - uy * 0.7071);
				if (along > -w * 2.6 && along < w * 2.2 && std::abs(across) < (w * 2.2 - along) * 0.9) a = std::max(a, 1.0); // the arrow head
				for (int k = 0; k < 3; ++k) {
					const int base = std::clamp(p[k] + ButtonShift[f], 0, 255);
					p[k]           = static_cast<unsigned char>(std::lround(base * (1 - a) + ButtonGold[f][k] * a));
				}
			}
		frames.push_back(std::move(picture));
	}
	return frames;
}

// A check box `size` square: a slot button, with a gold check mark when `on`.
inline std::vector<Picture> CheckBox(bool on, int size = 56) {
	auto frames = SlotButton(size, size);
	if (!on) return frames;
	const double k = size / 56.0;
	for (auto& picture : frames)
		for (int y = 0; y < size; ++y)
			for (int x = 0; x < size; ++x) {
				const auto onStroke = [&](double ax, double ay, double bx, double by) {
					ax *= k, ay *= k, bx *= k, by *= k;
					const double vx = bx - ax, vy = by - ay, t = std::clamp(((x - ax) * vx + (y - ay) * vy) / (vx * vx + vy * vy), 0.0, 1.0);
					const double ex = x - (ax + t * vx), ey = y - (ay + t * vy);
					return ex * ex + ey * ey < 12.0 * k * k;
				};
				if (onStroke(14, 28, 24, 40) || onStroke(24, 40, 43, 15)) std::memcpy(picture.at(x, y), "\xFD\xD8\x7A", 3);
			}
	return frames;
}

// A scroll bar's thumb: a bronze bar.
inline Picture ScrollThumb(int width, int height) {
	Picture out = Blank(width, height);
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x) {
			const int edge = std::min({ x, y, width - 1 - x, height - 1 - y });
			const int lit  = edge < 3 ? (std::min(x, y) == edge ? 50 : -50) : static_cast<int>(18 * Noise(x, y, 11)) - 9;
			const int rgba[4] { 128 + lit, 102 + lit, 62 + lit, edge < 1 ? 255 : 235 };
			for (int k = 0; k < 4; ++k) out.at(x, y)[k] = static_cast<unsigned char>(std::clamp(rgba[k], 0, 255));
		}
	return out;
}

// An invisible button's sprite: frame 0 clear, frame 1 a faint warm light (hovered).
inline std::vector<Picture> HoverBlank(int width = 4, int height = 4) {
	return { Blank(width, height), Fill(width, height, 0xFF, 0xF0, 0xC8, 0x28) };
}

// A dark box with a thin gold edge (`edge` brightness), when the game's art is missing.
inline Picture PlainBox(int width, int height, int edge) {
	const int color[3] { 20, 20, 22 };
	Picture   out = Stone(width, height, color, 10, 5);
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x)
			if (x < 2 || y < 2 || x >= width - 2 || y >= height - 2) {
				auto* p = out.at(x, y);
				p[0] = static_cast<unsigned char>(edge), p[1] = static_cast<unsigned char>(edge * 82 / 100), p[2] = static_cast<unsigned char>(edge * 45 / 100);
			}
	return out;
}

// A thin gold rule, fading out at both ends (under a section title).
inline Picture GoldRule(int width, int height = 4, int fadeWidth = 80) {
	Picture out = Blank(width, height);
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x) {
			const double fade = std::min(1.0, std::min(x, width - 1 - x) / static_cast<double>(std::max(1, fadeWidth)));
			const int    tone[4][3] { { 60, 48, 24 }, { 150, 126, 70 }, { 120, 98, 47 }, { 40, 32, 16 } };
			auto*        p = out.at(x, y);
			for (int c = 0; c < 3; ++c) p[c] = static_cast<unsigned char>(tone[y * 4 / std::max(1, height)][c]);
			p[3] = static_cast<unsigned char>(255 * fade);
		}
	return out;
}

// A round gem `size` wide in a gold setting, lit from the top left.
inline Picture Gem(const int color[3], int size = 40) {
	Picture      out = Blank(size, size);
	const double r = size / 2.0 - 3, c = size / 2.0;
	for (int y = 0; y < size; ++y)
		for (int x = 0; x < size; ++x) {
			const double dx = x + 0.5 - c, dy = y + 0.5 - c, d = std::sqrt(dx * dx + dy * dy);
			auto*        p = out.at(x, y);
			if (d > r + 2.5) continue;
			if (d > r) {
				const double a = std::clamp(r + 2.5 - d, 0.0, 1.0);
				p[0] = 150, p[1] = 122, p[2] = 64, p[3] = static_cast<unsigned char>(255 * a);
				continue;
			}
			const double light = std::clamp(1.0 - (d / r) * 0.55 + (-dx - dy) / (2.2 * r), 0.25, 1.35);
			const double spot  = std::exp(-((dx + r * 0.35) * (dx + r * 0.35) + (dy + r * 0.35) * (dy + r * 0.35)) / (r * r * 0.05));
			for (int k = 0; k < 3; ++k) p[k] = static_cast<unsigned char>(std::clamp(color[k] * light + 255 * 0.7 * spot, 0.0, 255.0));
			p[3] = 255;
		}
	return out;
}

// A small downward arrow centred at cx, cy (a drop-down's).
inline void DrawDownArrow(Picture& picture, int cx, int cy, const unsigned char* color) {
	for (int y = 0; y < 9; ++y)
		for (int x = -8 + y; x <= 8 - y; ++x)
			if (cx + x >= 0 && cx + x < picture.width && cy + y - 4 >= 0 && cy + y - 4 < picture.height) {
				auto* p = picture.at(cx + x, cy + y - 4);
				std::memcpy(p, color, 3), p[3] = 255;
			}
}

} // namespace D2RGui
