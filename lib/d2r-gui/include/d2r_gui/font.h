// D2R GUI: how big the game's text is, read from the fonts and layout
// profiles the game loads (a font mod's when one is installed), so a layout
// can size rows and wrap text before the game draws it.
//
// Font::Measure() once on the game thread after the data tables loaded,
// Font::Refresh() when a window opens (Low Vision Mode may have changed).
#pragma once
#include "game_files.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace D2RGui::Font {

struct Metrics {
	bool   ok {};
	double line {};        // line height, in ems
	double comment {};     // mean advance of English prose, in ems a character
	double key {};         // mean advance of a TOML-style key, in ems a character
	double advance[95] {}; // printable ASCII, in ems
	int    em {}, ascent {}, descent {}, gap {};
};

struct State {
	bool    measured {};
	Metrics latin;
	int     profileSmall[2] { 22, 28 }, profileMedium[2] { 26, 32 }; // HD and Low Vision profiles
	bool    lowVision {};
	// Layout units an em of Small and Medium text spans (measured on the HD
	// profile's 22 and 26; scaled for Low Vision Mode).
	double smallEm = 30.45, mediumEm = 33.9;
	// Units a character when the font was not measured.
	int fallbackChar = 20;
};

inline State& Get() {
	static State state;
	return state;
}

inline bool Measured() { return Get().measured && Get().latin.comment > 0; }

namespace Detail {
inline std::uint16_t U16(const std::string& b, std::size_t at) { return at + 2 <= b.size() ? static_cast<std::uint16_t>((static_cast<unsigned char>(b[at]) << 8) | static_cast<unsigned char>(b[at + 1])) : 0; }
inline std::uint32_t U32(const std::string& b, std::size_t at) { return (static_cast<std::uint32_t>(U16(b, at)) << 16) | U16(b, at + 2); }
} // namespace Detail

// An OpenType/TrueType font's line height and ASCII advances.
inline Metrics Read(const std::string& b) {
	using Detail::U16, Detail::U32;
	Metrics m;
	if (b.size() < 12) return m;
	std::size_t head = 0, hhea = 0, hmtx = 0, cmap = 0;
	const int   tables = U16(b, 4);
	for (int k = 0; k < tables; ++k) {
		const std::size_t at = 12 + static_cast<std::size_t>(k) * 16;
		if (at + 16 > b.size()) return m;
		const std::string tag = b.substr(at, 4);
		const std::size_t off = U32(b, at + 8);
		if (tag == "head") head = off;
		else if (tag == "hhea") hhea = off;
		else if (tag == "hmtx") hmtx = off;
		else if (tag == "cmap") cmap = off;
	}
	if (!head || !hhea || !hmtx || !cmap || hhea + 36 > b.size() || head + 20 > b.size()) return m;
	m.em      = U16(b, head + 18);
	m.ascent  = static_cast<std::int16_t>(U16(b, hhea + 4));
	m.descent = static_cast<std::int16_t>(U16(b, hhea + 6));
	m.gap     = static_cast<std::int16_t>(U16(b, hhea + 8));
	const int metrics = U16(b, hhea + 34);
	if (m.em < 16 || metrics < 1) return m;
	std::size_t sub = 0; // the Unicode character map: format 4 (BMP) or 12
	int         format = 0;
	for (int k = 0, count = U16(b, cmap + 2); k < count; ++k) {
		const std::size_t at = cmap + 4 + static_cast<std::size_t>(k) * 8;
		const int         platform = U16(b, at), encoding = U16(b, at + 2);
		const std::size_t off = cmap + U32(b, at + 4);
		const int         f   = U16(b, off);
		if ((platform == 3 && (encoding == 1 || encoding == 10)) || platform == 0)
			if (f == 12 || (f == 4 && format != 12)) sub = off, format = f;
	}
	if (!sub) return m;
	const auto glyph = [&](std::uint32_t c) -> int {
		if (format == 4) {
			const int         segs = U16(b, sub + 6) / 2;
			const std::size_t ends = sub + 14, starts = ends + segs * 2 + 2, deltas = starts + segs * 2, ranges = deltas + segs * 2;
			for (int k = 0; k < segs; ++k) {
				if (c > U16(b, ends + k * 2)) continue;
				const std::uint32_t start = U16(b, starts + k * 2);
				if (c < start) return 0;
				const int delta = U16(b, deltas + k * 2), range = U16(b, ranges + k * 2);
				if (!range) return (c + delta) & 0xFFFF;
				const int g = U16(b, ranges + k * 2 + range + (c - start) * 2);
				return g ? (g + delta) & 0xFFFF : 0;
			}
			return 0;
		}
		const std::uint32_t groups = U32(b, sub + 12);
		for (std::uint32_t k = 0; k < groups && sub + 16 + (k + 1) * 12 <= b.size(); ++k) {
			const std::size_t at = sub + 16 + k * 12;
			if (c >= U32(b, at) && c <= U32(b, at + 4)) return static_cast<int>(U32(b, at + 8) + (c - U32(b, at)));
		}
		return 0;
	};
	const auto advance = [&](char c) { const int g = std::min(glyph(static_cast<unsigned char>(c)), metrics - 1); return U16(b, hmtx + static_cast<std::size_t>(g) * 4); };
	const auto mean    = [&](std::string_view text) {
        double sum = 0;
        for (const char c : text) sum += advance(c);
        return sum / static_cast<double>(text.size()) / m.em;
	};
	for (int c = 32; c < 127; ++c) m.advance[c - 32] = static_cast<double>(advance(static_cast<char>(c))) / m.em;
	m.line    = static_cast<double>(m.ascent - m.descent + m.gap) / m.em;
	m.comment = mean("Master switch. false disables the Controls action, button, resources and deposit logic. A non-empty list narrows the candidates to these item codes.");
	m.key     = mean("inventory_button_enabled include_item_codes show_tag position_x max_items");
	m.ok      = m.line > 0.5 && m.line < 4 && m.comment > 0.1 && m.comment < 2 && m.key > 0.1 && m.key < 2;
	return m;
}

// The game's Low Vision Mode setting (D2R 3.3.94995 / 3.4.95041): its getter
// and the call that reads a setting's value, checked before use.
inline bool LowVisionOn() {
	constexpr std::uint64_t Getter = 0xE1920, Value = 0xD2E490;
	constexpr std::uint8_t  GetBytes[] { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x50, 0x65, 0x48, 0x8B, 0x04, 0x25, 0x58 };
	constexpr std::uint8_t  ValueBytes[] { 0x40, 0x53, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x80, 0x79, 0x08, 0x00 };
	const auto              base = ExeBase();
	if (!base || !Readable(base + Getter, sizeof(GetBytes)) || !Readable(base + Value, sizeof(ValueBytes)) || std::memcmp(reinterpret_cast<const void*>(base + Getter), GetBytes, sizeof(GetBytes)) != 0
	    || std::memcmp(reinterpret_cast<const void*>(base + Value), ValueBytes, sizeof(ValueBytes)) != 0)
		return false;
	void* setting = reinterpret_cast<void* (*)() noexcept>(base + Getter)();
	return setting && reinterpret_cast<int (*)(void*) noexcept>(base + Value)(setting) != 0;
}

// UI thread, when a window opens: sizes for the profile in use. True when
// the profile changed since the last call.
inline bool Refresh() {
	auto&      s  = Get();
	static int shown = -1;
	const int  lv = LowVisionOn() ? 1 : 0;
	s.lowVision   = lv;
	s.smallEm     = 30.45 * s.profileSmall[lv] / 22;
	s.mediumEm    = 33.9 * s.profileMedium[lv] / 26;
	if (shown == lv) return false;
	shown = lv;
	Log(false, "d2r-gui: text: %s profile, %.1f and %.1f units an em", lv ? "Low Vision" : "HD", s.smallEm, s.mediumEm);
	return true;
}

// Game thread, once after the data tables loaded: the text sizes of both PC
// profiles and the Latin font the menus draw with.
inline void Measure() {
	auto&       s = Get();
	const char* paths[2] { "data/global/ui/layouts/_profilehd.json", "data/global/ui/layouts/_profilelv.json" };
	for (int k = 0; k < 2; ++k) {
		std::string       trouble;
		const std::string text = ArchiveFile(paths[k], &trouble);
		s.profileSmall[k]      = ProfileNumber(text, "SmallFontSize", s.profileSmall[k], 8, 120);
		s.profileMedium[k]     = ProfileNumber(text, "MediumFontSize", s.profileMedium[k], 8, 120);
		Log(false, "d2r-gui: %s: %s, Small %d, Medium %d", paths[k], text.empty() ? trouble.c_str() : "read", s.profileSmall[k], s.profileMedium[k]);
	}
	const char*   path = "data/hd/ui/fonts/ExocetBlizzardOT-Medium.otf";
	std::string   trouble;
	const auto    file = ArchiveFile(path, &trouble);
	const Metrics m    = file.empty() ? Metrics {} : Read(file);
	if (m.ok) {
		s.latin = m, s.measured = true;
		Log(false, "d2r-gui: font %s: %zu bytes, %d units an em, ascent %d, descent %d, gap %d: line %.3f em, prose %.3f em, key %.3f em", path, file.size(), m.em, m.ascent, m.descent, m.gap, m.line,
		    m.comment, m.key);
	} else Log(true, "d2r-gui: font %s not measured: %s", path, file.empty() ? trouble.c_str() : "its tables could not be read");
	Refresh();
}

// Units an em spans at Small or Medium size.
inline double Em(bool medium = true) { return medium ? Get().mediumEm : Get().smallEm; }

// A text's advance in ems: printable ASCII from the font, anything else (one
// count per UTF-8 character) at the font's mean.
inline double Ems(std::string_view text) {
	const auto& s   = Get();
	double      sum = 0;
	for (const char c : text) {
		const auto u = static_cast<unsigned char>(c);
		if ((u & 0xC0) == 0x80) continue;
		sum += u >= 32 && u < 127 ? s.latin.advance[u - 32] : s.latin.comment;
	}
	return sum;
}

inline int Chars(std::string_view text) {
	int chars = 0;
	for (const char c : text)
		if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++chars;
	return chars;
}

// The width of `text` in layout units at Medium (labels, values) or Small size;
// without the font, characters times fallbackChar.
inline int TextWidth(std::string_view text, bool medium = true) {
	if (!Measured()) return Chars(text) * Get().fallbackChar;
	return static_cast<int>(std::ceil(Ems(text) * Em(medium)));
}

// A line's height in layout units at Small or Medium size (without the font: 0).
inline int LineHeight(bool medium = true) { return Measured() ? static_cast<int>(std::ceil(Em(medium) * Get().latin.line)) : 0; }

// The lines `text` takes in a box `width` units wide at Small size (or
// Medium): word wrapped with the font's widths when it was measured, else
// `fallbackChars` characters a line.
inline int WrappedLines(const std::string& text, int width, int fallbackChars, bool medium = false) {
	int         lines = 0;
	std::size_t start = 0;
	while (start <= text.size()) {
		const auto        end  = text.find('\n', start);
		const std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
		if (Measured()) {
			const double room  = width / Em(medium);
			const double space = Get().latin.advance[0];
			double       used  = 0;
			int          count = 1;
			std::size_t  at    = 0;
			while (at < line.size()) {
				const auto        stop = line.find(' ', at);
				const std::string word = line.substr(at, stop == std::string::npos ? std::string::npos : stop - at);
				double            w    = Ems(word);
				if (used > 0 && used + space + w > room) ++count, used = 0;
				else if (used > 0) used += space;
				while (w > room) ++count, w -= room; // a word longer than a line is cut
				used += w;
				if (stop == std::string::npos) break;
				at = stop + 1;
			}
			lines += count;
		} else lines += std::max(1, (Chars(line) + fallbackChars - 1) / std::max(1, fallbackChars));
		if (end == std::string::npos) break;
		start = end + 1;
	}
	return lines;
}

} // namespace D2RGui::Font
