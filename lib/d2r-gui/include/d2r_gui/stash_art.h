// D2R GUI: art in the stash's look (Grail On!'s window): area-average
// scaling, slot stone, gold rims, ring charts, glows, icons from masks.
// Everything here returns pictures; resources.h registers them.
#pragma once
#include "art.h"
#include "picture.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

namespace D2RGui {

struct Rgb {
	int r, g, b;
};

// `in` shrunk (area average) to fit width x height, centred, never enlarged.
inline Picture FitBox(const Picture& in, int width, int height) {
	Picture out = Blank(width, height);
	if (in.width <= 0 || in.height <= 0 || width <= 0 || height <= 0) return out;
	const double scale = std::min({ 1.0, static_cast<double>(width) / in.width, static_cast<double>(height) / in.height });
	const int    w = std::max(1, static_cast<int>(in.width * scale + 0.5)), h = std::max(1, static_cast<int>(in.height * scale + 0.5));
	Paste(out, ResizeBox(in, w, h), (width - w) / 2, (height - h) / 2);
	return out;
}

// A sprite's frames cut to what they draw first (alpha above 24 in any frame;
// game sheets pad their art differently), each fitted (FitBox) in width x height.
inline std::vector<Picture> Trimmed(const std::vector<Picture>& frames, int width, int height) {
	int left = 1 << 30, top = 1 << 30, right = -1, bottom = -1;
	for (const auto& frame : frames)
		for (int y = 0; y < frame.height; ++y)
			for (int x = 0; x < frame.width; ++x)
				if (frame.at(x, y)[3] > 24) left = std::min(left, x), right = std::max(right, x), top = std::min(top, y), bottom = std::max(bottom, y);
	std::vector<Picture> out;
	for (const auto& frame : frames) {
		if (right < left || right >= frame.width || bottom >= frame.height) out.push_back(FitBox(frame, width, height));
		else out.push_back(FitBox(Crop(frame, left, top, right - left + 1, bottom - top + 1), width, height));
	}
	return out;
}

// A gold line, fading out at both ends (under a set's name).
inline Picture FadedRule(int width, int height) {
	Picture out = Blank(width, height);
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x) {
			const double fade = std::clamp(std::min(x, width - 1 - x) / (width * 0.2), 0.0, 1.0);
			const bool   lit  = y < height / 2;
			auto*        px   = out.at(x, y);
			px[0] = lit ? 195 : 80, px[1] = lit ? 180 : 66, px[2] = lit ? 111 : 40;
			px[3] = static_cast<unsigned char>(230 * fade);
		}
	return out;
}

// A see-through black veil (`alpha` 0..255) over width x height.
inline Picture Veil(int width, int height, unsigned char alpha) { return Fill(width, height, 0, 0, 0, alpha); }

// The picture turned a quarter clockwise.
inline Picture TurnedRight(const Picture& in) {
	Picture out = Blank(in.height, in.width);
	for (int y = 0; y < in.height; ++y)
		for (int x = 0; x < in.width; ++x) std::memcpy(out.at(y, in.width - 1 - x), in.at(x, y), 4);
	return out;
}

// The picture upside down.
inline Picture UpsideDown(Picture in) {
	for (int y = 0; y < in.height / 2; ++y)
		for (int x = 0; x < in.width; ++x) std::swap_ranges(in.at(x, y), in.at(x, y) + 4, in.at(x, in.height - 1 - y));
	return in;
}

// Colours halved (a dimmed frame).
inline Picture Darkened(Picture in) {
	for (std::size_t k = 0; k + 3 < in.rgba.size(); k += 4)
		in.rgba[k] = static_cast<unsigned char>(in.rgba[k] / 2), in.rgba[k + 1] = static_cast<unsigned char>(in.rgba[k + 1] / 2), in.rgba[k + 2] = static_cast<unsigned char>(in.rgba[k + 2] / 2);
	return in;
}

// Alpha times `numerator` / `denominator` (a faded frame).
inline Picture Faded(Picture in, int numerator, int denominator) {
	for (std::size_t k = 0; k + 3 < in.rgba.size(); k += 4) in.rgba[k + 3] = static_cast<unsigned char>(in.rgba[k + 3] * numerator / denominator);
	return in;
}

// A coverage mask `size` square in one colour.
inline Picture MaskIcon(const unsigned char* mask, int size, int r, int g, int b) {
	Picture icon = Blank(size, size);
	for (int p = 0; p < size * size; ++p) {
		icon.rgba[p * 4] = static_cast<unsigned char>(r), icon.rgba[p * 4 + 1] = static_cast<unsigned char>(g), icon.rgba[p * 4 + 2] = static_cast<unsigned char>(b);
		icon.rgba[p * 4 + 3] = mask[p];
	}
	return icon;
}

// `in` fitted in `inner` square, centred in a clear `box` square.
inline Picture Padded(const Picture& in, int box, int inner) {
	const Picture small = FitBox(in, inner, inner);
	Picture       out   = Blank(box, box);
	for (int y = 0; y < small.height; ++y)
		std::memcpy(out.at((box - small.width) / 2, (box - small.height) / 2 + y), small.rgba.data() + static_cast<std::size_t>(y) * small.width * 4, static_cast<std::size_t>(small.width) * 4);
	return out;
}

// A plus sign in `color` across a button (an Add button of the plugin's own).
inline void DrawPlus(Picture& frame, const unsigned char* color) {
	for (int d = -12; d <= 12; ++d)
		for (int w = -2; w <= 2; ++w) {
			auto* a = frame.at(frame.width / 2 + d, frame.height / 2 + w);
			auto* b = frame.at(frame.width / 2 + w, frame.height / 2 + d);
			std::memcpy(a, color, 3), a[3] = 255;
			std::memcpy(b, color, 3), b[3] = 255;
		}
}

// A ring chart: the parts (value, colour) clockwise from the top, a thin dark
// gap between them, lighter towards the outer edge; smooth edges. `hole`: the
// inner edge, in percent of the outer. A square one is a pie seen only in a
// square band (a button's padding around its icon). With nothing in any part,
// the ring is all `empty`.
inline Picture Ring(const std::vector<std::pair<int, Rgb>>& parts, int size, int hole, bool square, Rgb empty) {
	Picture      picture = Blank(size, size);
	const double outer = size / 2.0 - 1, inner = outer * hole / 100.0, pi = 3.14159265358979, centre = size / 2.0;
	double       total = 0;
	int          used  = 0;
	for (const auto& [value, color] : parts) total += value, used += value > 0;
	std::vector<double> ends; // where each part ends, 0..1
	double              sum = 0;
	for (const auto& part : parts) ends.push_back((sum += part.first) / std::max(1.0, total));
	for (int y = 0; y < size; ++y)
		for (int x = 0; x < size; ++x) {
			const double dx = x + 0.5 - centre, dy = y + 0.5 - centre, d = square ? std::max(std::abs(dx), std::abs(dy)) : std::sqrt(dx * dx + dy * dy);
			const double r     = std::sqrt(dx * dx + dy * dy);
			double       cover = std::clamp(outer - d + 0.5, 0.0, 1.0) * std::clamp(d - inner + 0.5, 0.0, 1.0);
			if (cover <= 0) continue;
			double turn = std::atan2(dx, -dy) / (2 * pi); // 0 at the top, clockwise
			if (turn < 0) turn += 1;
			std::size_t which = 0;
			while (which + 1 < ends.size() && turn >= ends[which]) ++which;
			Rgb color = total > 0 ? parts[which].second : empty;
			// The gap: within a pixel of a part's start, when parts meet.
			if (used > 1)
				for (std::size_t e = 0; e < ends.size(); ++e) {
					if (parts[e].first <= 0) continue;
					const double edge = (e ? ends[e - 1] : 0.0) * 2 * pi, off = std::abs(std::sin(std::atan2(dx, -dy) - edge)) * r;
					const double along = std::cos(std::atan2(dx, -dy) - edge);
					if (along > 0 && off < 1.6) cover *= std::clamp(off - 0.6, 0.0, 1.0);
				}
			const double light = 0.78 + 0.32 * (d - inner) / (outer - inner);
			auto*        px    = picture.at(x, y);
			px[0]              = static_cast<unsigned char>(std::clamp(color.r * light, 0.0, 255.0));
			px[1]              = static_cast<unsigned char>(std::clamp(color.g * light, 0.0, 255.0));
			px[2]              = static_cast<unsigned char>(std::clamp(color.b * light, 0.0, 255.0));
			px[3]              = static_cast<unsigned char>(255 * cover);
		}
	return picture;
}

// Which part of a ring chart (as Ring draws it) is at x, y of its square
// `size` wide; -1 off the ring.
inline int RingPartAt(const std::vector<int>& values, int size, int hole, double x, double y) {
	double total = 0;
	for (const int v : values) total += v;
	if (total <= 0) return -1;
	std::vector<double> ends;
	double              sum = 0;
	for (const int v : values) ends.push_back((sum += v) / total);
	const double pi = 3.14159265358979, centre = size / 2.0, outer = size / 2.0 - 1, inner = outer * hole / 100.0;
	const double dx = x - centre, dy = y - centre, r = std::sqrt(dx * dx + dy * dy);
	if (r > outer || r < inner) return -1;
	double turn = std::atan2(dx, -dy) / (2 * pi);
	if (turn < 0) turn += 1;
	int part = 0;
	while (part + 1 < static_cast<int>(values.size()) && turn >= ends[part]) ++part;
	return part;
}

// A perfect item's glow around a slot width x height: a gold halo `pad`
// beyond it, a bright rim, and a glow fading in from the edges. `count`
// frames: the light breathes and a brighter band runs around.
inline std::vector<Picture> GlowFrames(int width, int height, int pad, int count) {
	std::vector<Picture> frames;
	const int            w = width + 2 * pad, h = height + 2 * pad;
	const double         perimeter = 2.0 * (width + height), pi = 3.14159265358979;
	for (int f = 0; f < count; ++f) {
		Picture      picture = Blank(w, h);
		const double breath  = 0.78 + 0.22 * std::cos(2 * pi * f / count);
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x) {
				// Distance from the slot's edge: < 0 outside, > 0 inside.
				const int    sx = x - pad, sy = y - pad;
				const int    dx = sx < 0 ? sx : sx >= width ? width - 1 - sx : 0, dy = sy < 0 ? sy : sy >= height ? height - 1 - sy : 0;
				const double outside = std::sqrt(static_cast<double>(dx * dx + dy * dy));
				const double inside  = std::min({ sx, sy, width - 1 - sx, height - 1 - sy });
				// Where the point is along the rim, 0..1 clockwise from the top left.
				const double cx = std::clamp(sx, 0, width - 1), cy = std::clamp(sy, 0, height - 1);
				const double edge  = std::min({ cx, cy, width - 1 - cx, height - 1 - cy });
				double       along = 0;
				if (cy == edge) along = cx;
				else if (width - 1 - cx == edge) along = width + cy;
				else if (height - 1 - cy == edge) along = width + height + (width - cx);
				else along = 2.0 * width + height + (height - cy);
				const double phase = along / perimeter - static_cast<double>(f) / count;
				const double band  = std::pow(0.5 + 0.5 * std::cos(2 * pi * (phase - std::floor(phase))), 8);
				double       alpha = 0, light = 0;
				if (outside > 0) alpha = 235 * std::pow(std::max(0.0, 1 - outside / pad), 1.6) * (breath + 0.35 * band), light = 0.4;
				else if (inside < 3) alpha = 255, light = 0.55 + 0.45 * std::max(band, breath - 0.6);
				else alpha = 150 * std::pow(std::max(0.0, 1 - (inside - 3) / 26.0), 2) * (breath + 0.3 * band), light = 0.25;
				auto* px = picture.at(x, y);
				px[0]    = static_cast<unsigned char>(std::min(255.0, 255 * (0.8 + 0.2 * light) + 30 * band));
				px[1]    = static_cast<unsigned char>(std::min(255.0, 200 * (0.75 + 0.25 * light) + 40 * band));
				px[2]    = static_cast<unsigned char>(std::min(255.0, 70 + 110 * light + 80 * band));
				px[3]    = static_cast<unsigned char>(std::clamp(alpha, 0.0, 255.0));
			}
		frames.push_back(std::move(picture));
	}
	return frames;
}

// A square button in the Advanced Stats button's materials (that button is an
// arrow): its straight left border on all four sides around its dark field.
inline Picture SquareFromBorder(const Picture& in, int width, int height) {
	constexpr int Left = 3, Border = 8, Top = 22, Length = 24, Field = 10;
	Picture       out = Blank(width, height);
	if (in.width < Field + 1 || in.height < Top + Length) return FitBox(in, width, height);
	unsigned field[4] {};
	for (int j = 0; j < Length; ++j)
		for (int c = 0; c < 4; ++c) field[c] += in.at(Field, Top + j)[c];
	for (int y = Border; y < height - Border; ++y)
		for (int x = Border; x < width - Border; ++x)
			for (int c = 0; c < 4; ++c) out.at(x, y)[c] = static_cast<unsigned char>(field[c] / Length);
	const auto strip = [&](int i, int j) { return in.at(Left + i, Top + j % Length); };
	for (int j = 0; j < height; ++j)
		for (int i = 0; i < Border; ++i) std::memcpy(out.at(i, j), strip(i, j), 4), std::memcpy(out.at(width - 1 - i, j), strip(i, j), 4);
	for (int j = 0; j < width; ++j)
		for (int i = 0; i < Border; ++i) std::memcpy(out.at(j, i), strip(i, j), 4), std::memcpy(out.at(j, height - 1 - i), strip(i, j), 4);
	return out;
}

// A button that is an icon (coverage mask `maskSize` square) in the game's
// gold, width x height; frames 0 normal, 1 pressed (down a pixel), 2
// disabled, 3 hovered (a soft gold light behind it). With `backs` (a game
// button's frames) the icon is drawn on them, fitted to the button, smaller.
inline std::vector<Picture> MaskButton(const unsigned char* mask, int maskSize, int width, int height, int iconSize, const std::vector<Picture>& backs = {}) {
	std::vector<Picture> fitted;
	for (const auto& back : backs) fitted.push_back(SquareFromBorder(back, width, height));
	if (!fitted.empty()) iconSize = std::min(width, height) * 58 / 100;
	std::vector<double> cover(static_cast<std::size_t>(iconSize) * iconSize);
	const double        step = static_cast<double>(maskSize) / iconSize;
	for (int y = 0; mask && y < iconSize; ++y)
		for (int x = 0; x < iconSize; ++x) {
			double sum = 0;
			int    n   = 0;
			for (int sy = static_cast<int>(y * step); sy < static_cast<int>((y + 1) * step) && sy < maskSize; ++sy)
				for (int sx = static_cast<int>(x * step); sx < static_cast<int>((x + 1) * step) && sx < maskSize; ++sx) sum += mask[sy * maskSize + sx], ++n;
			cover[static_cast<std::size_t>(y) * iconSize + x] = n ? sum / n / 255.0 : 0;
		}
	std::vector<Picture> frames;
	const struct {
		int    r, g, b, dy;
		double glow;
	} looks[] { { 215, 200, 150, 0, 0 }, { 170, 155, 110, 1, 0 }, { 110, 105, 95, 0, 0 }, { 255, 238, 180, 0, 0.35 } };
	for (std::size_t k = 0; k < std::size(looks); ++k) {
		const auto& look  = looks[k];
		Picture     frame = fitted.empty() ? Blank(width, height) : fitted[std::min(k, fitted.size() - 1)];
		const int   left = (width - iconSize) / 2, top = (height - iconSize) / 2 + look.dy;
		for (int y = 0; y < height; ++y)
			for (int x = 0; x < width; ++x) {
				const int ix = x - left, iy = y - top;
				double    a  = ix >= 0 && iy >= 0 && ix < iconSize && iy < iconSize ? cover[static_cast<std::size_t>(iy) * iconSize + ix] : 0;
				if (!fitted.empty()) { // the icon over the game's button
					if (a <= 0) continue;
					auto*        px    = frame.at(x, y);
					const double under = px[3] / 255.0, alpha = a + under * (1 - a);
					px[0] = static_cast<unsigned char>((look.r * a + px[0] * under * (1 - a)) / alpha);
					px[1] = static_cast<unsigned char>((look.g * a + px[1] * under * (1 - a)) / alpha);
					px[2] = static_cast<unsigned char>((look.b * a + px[2] * under * (1 - a)) / alpha);
					px[3] = static_cast<unsigned char>(255 * alpha);
					continue;
				}
				double light = 0;
				if (look.glow > 0) {
					const double dx = (x + 0.5 - width / 2.0) / (width / 2.0), dy = (y + 0.5 - height / 2.0) / (height / 2.0);
					light = look.glow * std::max(0.0, 1.0 - std::sqrt(dx * dx + dy * dy));
				}
				auto*        px    = frame.at(x, y);
				const double alpha = a + light * (1 - a);
				if (alpha <= 0) continue;
				px[0] = static_cast<unsigned char>((look.r * a + 200 * light * (1 - a)) / alpha);
				px[1] = static_cast<unsigned char>((look.g * a + 160 * light * (1 - a)) / alpha);
				px[2] = static_cast<unsigned char>((look.b * a + 80 * light * (1 - a)) / alpha);
				px[3] = static_cast<unsigned char>(255 * alpha);
			}
		frames.push_back(std::move(frame));
	}
	return frames;
}

} // namespace D2RGui
