// D2R GUI: RGBA pictures and the game's sprite format (SpA1 version 31).
//
// Two families of scaling are kept on purpose, because the plugins they come
// from look the way they do with them:
//   Resize / HalfBilinear / StretchEnds / JoinTiled  - I Hate TOMLs (bilinear)
//   ResizeBox / HalfBox / Stretch9 / JoinStretched   - Grail On! (area average)
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace D2RGui {

struct Picture {
	int                        width {}, height {};
	std::vector<unsigned char> rgba;
	unsigned char*             at(int x, int y) { return rgba.data() + (static_cast<std::size_t>(y) * width + x) * 4; }
	const unsigned char*       at(int x, int y) const { return rgba.data() + (static_cast<std::size_t>(y) * width + x) * 4; }
	bool                       empty() const { return width <= 0 || height <= 0; }
};

inline Picture Blank(int width, int height) {
	width = std::max(0, width), height = std::max(0, height);
	return { width, height, std::vector<unsigned char>(static_cast<std::size_t>(width) * height * 4) };
}

// A picture of one colour.
inline Picture Fill(int width, int height, unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
	Picture out = Blank(width, height);
	for (std::size_t p = 0; p < out.rgba.size(); p += 4) out.rgba[p] = r, out.rgba[p + 1] = g, out.rgba[p + 2] = b, out.rgba[p + 3] = a;
	return out;
}

// ---- sprites ----

// The frames of a game sprite (frames side by side in one sheet). `trouble`
// says what was read, or why nothing was.
inline std::vector<Picture> ReadSprite(const std::string& file, std::string* trouble = nullptr) {
	if (file.size() < 0x28 || file.compare(0, 4, "SpA1")) {
		if (trouble) *trouble = "not a sprite";
		return {};
	}
	const auto    u32 = [&](std::size_t at) { std::uint32_t v; std::memcpy(&v, file.data() + at, 4); return v; };
	std::uint16_t version = 0, frameWidth = 0;
	std::memcpy(&version, file.data() + 4, 2), std::memcpy(&frameWidth, file.data() + 6, 2);
	const std::uint32_t width = u32(8), height = u32(12), count = std::max<std::uint32_t>(1, u32(20));
	if (trouble) {
		char what[160] {};
		std::snprintf(what, sizeof(what), "version %u, frame %ux%u, sheet %ux%u, %u frames, %zu bytes", version, frameWidth, height, width, height, count, file.size());
		*trouble = what;
	}
	if (version != 31 || !frameWidth || !height || width < frameWidth || width > 8192 || height > 4096 || file.size() < 0x28 + static_cast<std::size_t>(width) * height * 4) return {};
	std::vector<Picture> frames;
	for (std::uint32_t f = 0; f < count && (f + 1) * frameWidth <= width; ++f) {
		Picture frame = Blank(frameWidth, static_cast<int>(height));
		for (std::uint32_t y = 0; y < height; ++y)
			std::memcpy(frame.at(0, static_cast<int>(y)), file.data() + 0x28 + (static_cast<std::size_t>(y) * width + f * frameWidth) * 4, static_cast<std::size_t>(frameWidth) * 4);
		frames.push_back(std::move(frame));
	}
	return frames;
}

inline Picture FirstFrame(const std::string& file, std::string* trouble = nullptr) {
	auto frames = ReadSprite(file, trouble);
	return frames.empty() ? Picture {} : std::move(frames.front());
}

// A sprite file of `frames` (all the size of the first).
inline std::string WriteSprite(const std::vector<Picture>& frames) {
	if (frames.empty()) return {};
	const auto& first = frames.front();
	const auto  count = static_cast<std::uint32_t>(frames.size());
	std::string out(40, '\0');
	const auto  put = [&](std::size_t at, std::uint32_t value, int bytes) {
        for (int i = 0; i < bytes; ++i) out[at + i] = static_cast<char>((value >> (8 * i)) & 0xFF);
	};
	std::memcpy(out.data(), "SpA1", 4);
	put(4, 31, 2);
	put(6, static_cast<std::uint32_t>(first.width), 2);
	put(8, static_cast<std::uint32_t>(first.width) * count, 4);
	put(12, static_cast<std::uint32_t>(first.height), 4);
	put(20, count, 4);
	const std::size_t row = static_cast<std::size_t>(first.width) * 4;
	for (int y = 0; y < first.height; ++y)
		for (const auto& frame : frames) {
			if (frame.width == first.width && frame.height == first.height) out.append(reinterpret_cast<const char*>(frame.rgba.data()) + y * row, row);
			else out.append(row, '\0');
		}
	return out;
}

// ---- noise and stone ----

inline double Noise(std::uint32_t x, std::uint32_t y, std::uint32_t seed) {
	std::uint32_t h = x * 374761393u + y * 668265263u + seed * 2246822519u;
	h               = (h ^ (h >> 13)) * 1274126177u;
	return ((h ^ (h >> 16)) & 0xFF) / 255.0;
}

// The fine grain of the game's dark stone: single pixels and 3x3 blotches.
inline Picture Stone(int width, int height, const int color[3], int grain, std::uint32_t seed) {
	Picture out = Blank(width, height);
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x) {
			const double n = 0.6 * Noise(x, y, seed) + 0.4 * Noise(x / 3, y / 3, seed + 1) - 0.5;
			auto*        p = out.at(x, y);
			for (int c = 0; c < 3; ++c) p[c] = static_cast<unsigned char>(std::clamp(static_cast<int>(color[c] + n * grain), 0, 255));
			p[3] = 255;
		}
	return out;
}

// ---- copying ----

inline Picture Crop(const Picture& in, int left, int top, int width, int height) {
	left = std::clamp(left, 0, in.width), top = std::clamp(top, 0, in.height);
	width = std::clamp(width, 0, in.width - left), height = std::clamp(height, 0, in.height - top);
	Picture out = Blank(width, height);
	for (int y = 0; y < height; ++y) std::memcpy(out.at(0, y), in.at(left, top + y), static_cast<std::size_t>(width) * 4);
	return out;
}

// `piece` copied onto `onto` at left, top (pixels replaced, not blended),
// from its column `sourceLeft`, `width` columns (all when negative).
inline void Paste(Picture& onto, const Picture& piece, int left, int top, int sourceLeft = 0, int width = -1) {
	if (width < 0) width = piece.width - sourceLeft;
	for (int y = 0; y < piece.height && top + y < onto.height; ++y)
		for (int x = 0; x < width && left + x < onto.width; ++x)
			if (left + x >= 0 && top + y >= 0 && sourceLeft + x >= 0 && sourceLeft + x < piece.width) std::memcpy(onto.at(left + x, top + y), piece.at(sourceLeft + x, y), 4);
}

// `piece` drawn over `onto` (alpha blended).
inline void Blend(Picture& onto, const Picture& piece, int left, int top) {
	for (int y = 0; y < piece.height; ++y)
		for (int x = 0; x < piece.width; ++x) {
			const int tx = left + x, ty = top + y;
			if (tx < 0 || ty < 0 || tx >= onto.width || ty >= onto.height) continue;
			const auto*  s = piece.at(x, y);
			auto*        d = onto.at(tx, ty);
			const double a = s[3] / 255.0, keep = 1 - a;
			for (int k = 0; k < 3; ++k) d[k] = static_cast<unsigned char>(std::lround(s[k] * a + d[k] * keep));
			d[3] = static_cast<unsigned char>(std::lround(255 * (a + d[3] / 255.0 * keep)));
		}
}

inline Picture Mirror(const Picture& in) {
	Picture out = Blank(in.width, in.height);
	for (int y = 0; y < in.height; ++y)
		for (int x = 0; x < in.width; ++x) std::memcpy(out.at(x, y), in.at(in.width - 1 - x, y), 4);
	return out;
}

// Every pixel's colour shifted by `amount` (lighter or darker), alpha kept.
inline void Shade(Picture& picture, int amount) {
	for (std::size_t p = 0; p < picture.rgba.size(); p += 4)
		for (int k = 0; k < 3; ++k) picture.rgba[p + k] = static_cast<unsigned char>(std::clamp(picture.rgba[p + k] + amount, 0, 255));
}

// ---- scaling, bilinear (I Hate TOMLs) ----

inline Picture Resize(const Picture& in, int width, int height) {
	Picture out = Blank(std::max(1, width), std::max(1, height));
	if (!in.width || !in.height) return out;
	for (int y = 0; y < out.height; ++y)
		for (int x = 0; x < out.width; ++x) {
			const double sx = std::clamp((x + 0.5) * in.width / out.width - 0.5, 0.0, in.width - 1.0), sy = std::clamp((y + 0.5) * in.height / out.height - 0.5, 0.0, in.height - 1.0);
			const int    x0 = static_cast<int>(sx), y0 = static_cast<int>(sy), x1 = std::min(x0 + 1, in.width - 1), y1 = std::min(y0 + 1, in.height - 1);
			const double fx = sx - x0, fy = sy - y0;
			for (int k = 0; k < 4; ++k) {
				const double top = in.at(x0, y0)[k] * (1 - fx) + in.at(x1, y0)[k] * fx, bottom = in.at(x0, y1)[k] * (1 - fx) + in.at(x1, y1)[k] * fx;
				out.at(x, y)[k] = static_cast<unsigned char>(std::lround(top * (1 - fy) + bottom * fy));
			}
		}
	return out;
}

inline Picture HalfBilinear(const Picture& in) { return Resize(in, in.width / 2, in.height / 2); }

// A box of any width from the game's: its ends kept, its middle tiled. The
// right end is the left end's mirror when none is given.
inline Picture JoinTiled(const Picture& leftEnd, const Picture& middle, const Picture& rightEnd, int width, int height) {
	const auto    scale = [&](const Picture& p) { return p.width ? Resize(p, std::max(1, p.width * height / std::max(1, p.height)), height) : Picture {}; };
	const Picture left = scale(leftEnd), right = rightEnd.width ? scale(rightEnd) : Mirror(left), mid = scale(middle);
	Picture       out = Blank(width, height);
	if (!left.width || !mid.width) return out;
	for (int x = left.width; x < width - right.width; x += mid.width) Paste(out, mid, x, 0, 0, std::min(mid.width, width - right.width - x));
	Paste(out, left, 0, 0, 0, std::min(left.width, width / 2));
	const int keep = std::min(right.width, width - width / 2);
	Paste(out, right, width - keep, 0, right.width - keep, keep);
	return out;
}

// A long piece of art fitted to another size: its ends kept, its middle stretched.
inline Picture StretchEnds(const Picture& in, int width, int height, int leftKeep, int rightKeep) {
	const Picture tall = Resize(in, in.width, height);
	Picture       out  = Blank(width, height);
	const int     l = std::min(leftKeep, width / 2), r = std::min(rightKeep, width - l);
	Paste(out, tall, 0, 0, 0, l);
	Paste(out, tall, width - r, 0, tall.width - r, r);
	Picture middle = Blank(std::max(1, tall.width - leftKeep - rightKeep), height);
	Paste(middle, tall, 0, 0, leftKeep, middle.width);
	Paste(out, Resize(middle, std::max(1, width - l - r), height), l, 0);
	return out;
}

// ---- scaling, area average (Grail On!) ----

// Each pixel the average of its source box, alpha weighted; the nearest pixel when enlarging.
inline Picture ResizeBox(const Picture& in, int width, int height) {
	Picture out = Blank(width, height);
	if (in.width <= 0 || in.height <= 0 || width <= 0 || height <= 0) return out;
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x) {
			const int x0 = x * in.width / width, x1 = std::max(x0 + 1, (x + 1) * in.width / width), y0 = y * in.height / height, y1 = std::max(y0 + 1, (y + 1) * in.height / height);
			double    sum[4] {}, n = 0;
			for (int sy = y0; sy < y1; ++sy)
				for (int sx = x0; sx < x1; ++sx) {
					const auto* px = in.at(sx, sy);
					for (int c = 0; c < 3; ++c) sum[c] += px[c] * (px[3] / 255.0);
					sum[3] += px[3], ++n;
				}
			auto* px = out.at(x, y);
			for (int c = 0; c < 3; ++c) px[c] = static_cast<unsigned char>(sum[3] > 0 ? std::clamp(sum[c] * 255.0 / sum[3], 0.0, 255.0) : 0);
			px[3] = static_cast<unsigned char>(sum[3] / n);
		}
	return out;
}

// Half size, each pixel the plain average of 2x2 (the .lowend sprites).
inline Picture HalfBox(const Picture& in) {
	Picture out = Blank(std::max(1, in.width / 2), std::max(1, in.height / 2));
	if (in.empty()) return out;
	for (int y = 0; y < out.height; ++y)
		for (int x = 0; x < out.width; ++x)
			for (int k = 0; k < 4; ++k) {
				int total = 0;
				for (int dy = 0; dy < 2; ++dy)
					for (int dx = 0; dx < 2; ++dx) total += in.at(std::min(in.width - 1, 2 * x + dx), std::min(in.height - 1, 2 * y + dy))[k];
				out.at(x, y)[k] = static_cast<unsigned char>(total / 4);
			}
	return out;
}

// A frame stretched to width x height, its borders kept: the corners as they
// are (scaled down to fit), the edges stretched along, the middle both ways.
inline Picture Stretch9(const Picture& in, int width, int height, int capX, int capY) {
	capX = std::min({ capX, in.width / 2 - 1, width / 2 }), capY = std::min({ capY, in.height / 2 - 1, height / 2 });
	if (capX < 0 || capY < 0) return ResizeBox(in, width, height);
	Picture   out = Blank(width, height);
	const int sx[] { 0, capX, in.width - capX }, sw[] { capX, in.width - 2 * capX, capX };
	const int sy[] { 0, capY, in.height - capY }, sh[] { capY, in.height - 2 * capY, capY };
	const int dx[] { 0, capX, width - capX }, dw[] { capX, width - 2 * capX, capX };
	const int dy[] { 0, capY, height - capY }, dh[] { capY, height - 2 * capY, capY };
	for (int r = 0; r < 3; ++r)
		for (int c = 0; c < 3; ++c)
			if (sw[c] > 0 && sh[r] > 0 && dw[c] > 0 && dh[r] > 0) Paste(out, ResizeBox(Crop(in, sx[c], sy[r], sw[c], sh[r]), dw[c], dh[r]), dx[c], dy[r]);
	return out;
}

// A left cap, a middle stretched and a right cap side by side, `height` high
// (the caps scaled to it), `width` wide. Caps wider than the box share it.
inline Picture JoinStretched(const Picture& left, const Picture& middle, const Picture& right, int width, int height) {
	const auto scaled = [&](const Picture& part) { return ResizeBox(part, std::max(1, part.width * height / std::max(1, part.height)), height); };
	Picture    l = scaled(left), r = scaled(right);
	if (l.width + r.width > width) {
		const int total = l.width + r.width, lw = std::max(1, width * l.width / total), rw = std::max(1, width - lw);
		l = ResizeBox(l, lw, height), r = ResizeBox(r, rw, height);
	}
	Picture out = Blank(width, height);
	Paste(out, ResizeBox(middle, std::max(1, width - l.width - r.width), height), l.width, 0);
	Paste(out, l, 0, 0);
	Paste(out, r, width - r.width, 0);
	return out;
}

// ---- fitting ----

// A sprite's frames cut to what they draw (alpha above 24 in any frame),
// scaled to fit a square `size` wide, centred.
inline std::vector<Picture> Fitted(const std::vector<Picture>& frames, int size) {
	int left = 1 << 30, top = 1 << 30, right = -1, bottom = -1;
	for (const auto& frame : frames)
		for (int y = 0; y < frame.height; ++y)
			for (int x = 0; x < frame.width; ++x)
				if (frame.at(x, y)[3] > 24) left = std::min(left, x), right = std::max(right, x), top = std::min(top, y), bottom = std::max(bottom, y);
	std::vector<Picture> out;
	for (const auto& frame : frames) {
		Picture cut = frame;
		if (right >= left && right < frame.width && bottom < frame.height) cut = Crop(frame, left, top, right - left + 1, bottom - top + 1);
		const double  scale = std::min(static_cast<double>(size) / std::max(1, cut.width), static_cast<double>(size) / std::max(1, cut.height));
		const Picture fit   = Resize(cut, std::max(1, static_cast<int>(cut.width * scale)), std::max(1, static_cast<int>(cut.height * scale)));
		Picture       box   = Blank(size, size);
		Paste(box, fit, (size - fit.width) / 2, (size - fit.height) / 2);
		out.push_back(std::move(box));
	}
	return out;
}

// Each frame scaled whole (padding kept) to fit a square `size` wide, centred.
inline std::vector<Picture> Boxed(const std::vector<Picture>& frames, int size) {
	std::vector<Picture> out;
	for (const auto& frame : frames) {
		const double  scale = std::min(static_cast<double>(size) / std::max(1, frame.width), static_cast<double>(size) / std::max(1, frame.height));
		const Picture fit   = Resize(frame, std::max(1, static_cast<int>(frame.width * scale)), std::max(1, static_cast<int>(frame.height * scale)));
		Picture       box   = Blank(size, size);
		Paste(box, fit, (size - fit.width) / 2, (size - fit.height) / 2);
		out.push_back(std::move(box));
	}
	return out;
}

} // namespace D2RGui
