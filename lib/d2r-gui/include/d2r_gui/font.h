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
	double mean {};        // mean advance of English text, in ems a character
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
};

inline State& Get() {
	static State state;
	return state;
}

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
	double     sum     = 0;
	const std::string_view sample = "Master switch. false disables the Controls action, button, resources and deposit logic.";
	for (const char c : sample) sum += advance(c);
	for (int c = 32; c < 127; ++c) m.advance[c - 32] = static_cast<double>(advance(static_cast<char>(c))) / m.em;
	m.line = static_cast<double>(m.ascent - m.descent + m.gap) / m.em;
	m.mean = sum / static_cast<double>(sample.size()) / m.em;
	m.ok   = m.line > 0.5 && m.line < 4 && m.mean > 0.1 && m.mean < 2;
	return m;
}

// The game's Low Vision Mode setting (D2R 3.3.94995 / 3.4.95041): its getter
// and the call that reads a setting's value, checked before use.
inline bool LowVisionOn() {
	constexpr std::uint64_t Get_ = 0xE1920, Value = 0xD2E490;
	constexpr std::uint8_t  GetBytes[] { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x50, 0x65, 0x48, 0x8B, 0x04, 0x25, 0x58 };
	constexpr std::uint8_t  ValueBytes[] { 0x40, 0x53, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x80, 0x79, 0x08, 0x00 };
	const auto              base = ExeBase();
	if (!base || !Readable(base + Get_, sizeof(GetBytes)) || !Readable(base + Value, sizeof(ValueBytes)) || std::memcmp(reinterpret_cast<const void*>(base + Get_), GetBytes, sizeof(GetBytes)) != 0
	    || std::memcmp(reinterpret_cast<const void*>(base + Value), ValueBytes, sizeof(ValueBytes)) != 0)
		return false;
	void* setting = reinterpret_cast<void* (*)() noexcept>(base + Get_)();
	return setting && reinterpret_cast<int (*)(void*) noexcept>(base + Value)(setting) != 0;
}

// UI thread, when a window opens: sizes for the profile in use.
inline void Refresh() {
	auto&     s  = Get();
	const int lv = LowVisionOn() ? 1 : 0;
	s.lowVision  = lv;
	s.smallEm    = 30.45 * s.profileSmall[lv] / 22;
	s.mediumEm   = 33.9 * s.profileMedium[lv] / 26;
}

// Game thread, once after the data tables loaded.
inline void Measure() {
	auto&       s = Get();
	const char* paths[2] { "data/global/ui/layouts/_profilehd.json", "data/global/ui/layouts/_profilelv.json" };
	for (int k = 0; k < 2; ++k) {
		const std::string text = ArchiveFile(paths[k]);
		s.profileSmall[k]      = ProfileNumber(text, "SmallFontSize", s.profileSmall[k], 8, 120);
		s.profileMedium[k]     = ProfileNumber(text, "MediumFontSize", s.profileMedium[k], 8, 120);
	}
	std::string   trouble;
	const auto    file = ArchiveFile("data/hd/ui/fonts/ExocetBlizzardOT-Medium.otf", &trouble); // the Latin font the menus draw with
	const Metrics m    = file.empty() ? Metrics {} : Read(file);
	if (m.ok) s.latin = m, s.measured = true;
	else Log(true, "d2r-gui: the game's font was not measured (%s); text sizes are estimates", file.empty() ? trouble.c_str() : "its tables could not be read");
	Refresh();
}

// The width of `text` in layout units at Small or Medium size (UTF-8: a
// character outside ASCII counts as the mean advance).
inline int TextWidth(std::string_view text, bool medium = true) {
	const auto&  s  = Get();
	const double em = medium ? s.mediumEm : s.smallEm;
	double       ems = 0;
	for (std::size_t i = 0; i < text.size(); ++i) {
		const auto c = static_cast<unsigned char>(text[i]);
		if ((c & 0xC0) == 0x80) continue;
		ems += s.measured && c >= 32 && c < 127 ? s.latin.advance[c - 32] : (s.measured ? s.latin.mean : 0.5);
	}
	return static_cast<int>(std::ceil(ems * em));
}

// A line's height in layout units at Small or Medium size.
inline int LineHeight(bool medium = true) {
	const auto& s = Get();
	return static_cast<int>(std::ceil((medium ? s.mediumEm : s.smallEm) * (s.measured ? s.latin.line : 1.1)));
}

} // namespace D2RGui::Font
