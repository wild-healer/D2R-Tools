// D2R GUI: text helpers for layouts (JSON strings, text the game's font can draw).
#pragma once
#include <windows.h>
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace D2RGui {

// A JSON string literal, quotes included. Newlines become \n, tabs a space,
// other control characters are dropped.
inline std::string Json(std::string_view text) {
	std::string out = "\"";
	for (const char c : text) {
		if (c == '"' || c == '\\') out += '\\';
		if (c == '\n') out += "\\n";
		else if (c == '\t') out += ' ';
		else if (static_cast<unsigned char>(c) >= 0x20) out += c;
	}
	return out + '"';
}

// Text the game draws in its own font. A character the font lacks (box
// drawing, curly quotes, arrows, dashes, emoji) makes the game draw the whole
// TextBox in a bigger fallback font, so they become ASCII; lines made only
// of box rules are dropped.
inline std::string GameText(std::string_view text) {
	std::string out;
	out.reserve(text.size());
	bool rule = false; // box lines skipped since the last plain character
	for (std::size_t i = 0; i < text.size();) {
		const auto  c = static_cast<unsigned char>(text[i]);
		char32_t    u = c;
		std::size_t n = 1;
		if (c >= 0xF0 && i + 3 < text.size()) u = ((c & 7u) << 18) | ((text[i + 1] & 0x3Fu) << 12) | ((text[i + 2] & 0x3Fu) << 6) | (text[i + 3] & 0x3Fu), n = 4;
		else if (c >= 0xE0 && i + 2 < text.size()) u = ((c & 0xFu) << 12) | ((text[i + 1] & 0x3Fu) << 6) | (text[i + 2] & 0x3Fu), n = 3;
		else if (c >= 0xC0 && i + 1 < text.size()) u = ((c & 0x1Fu) << 6) | (text[i + 1] & 0x3Fu), n = 2;
		const std::string_view raw = text.substr(i, n);
		i += n;
		if (u == '\r') continue;
		if (u >= 0x2500 && u <= 0x259F) { // box drawing and blocks
			if (u == 0x2502 || u == 0x2503 || u == 0x2551) out += '|';
			else rule = true;
			continue;
		}
		if (rule) {
			rule = false;
			if (u != '\n' && u != ' ' && !out.empty() && out.back() != ' ' && out.back() != '\n') out += ' ';
		}
		switch (u) {
		case 0x00A0: case 0x2002: case 0x2003: case 0x2009: case 0x202F: out += ' '; break;
		case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2015: case 0x2212: out += '-'; break;
		case 0x2018: case 0x2019: case 0x201A: case 0x2032: out += '\''; break;
		case 0x201C: case 0x201D: case 0x201E: case 0x2033: out += '"'; break;
		case 0x2026: out += "..."; break;
		case 0x2022: case 0x2023: case 0x25CF: case 0x2043: out += '*'; break;
		case 0x2190: case 0x21D0: out += "<-"; break;
		case 0x2192: case 0x21D2: case 0x279C: case 0x27A4: out += "->"; break;
		case 0x2194: case 0x21D4: out += "<->"; break;
		case 0x2191: out += '^'; break;
		case 0x2193: out += 'v'; break;
		case 0x2264: out += "<="; break;
		case 0x2265: out += ">="; break;
		case 0x2260: out += "!="; break;
		case 0x2248: out += '~'; break;
		case 0x2713: case 0x2714: out += "yes"; break;
		case 0x2717: case 0x2718: out += "no"; break;
		default:
			if (u >= 0x2000 && u < 0x3000) out += '?'; // other symbols
			else if (u >= 0x1F000) out += '?';        // emoji
			else out += raw;
		}
	}
	// spaces left at line ends by removed rules
	std::string clean;
	clean.reserve(out.size());
	for (const char c : out) {
		if (c == '\n')
			while (!clean.empty() && clean.back() == ' ') clean.pop_back();
		clean += c;
	}
	while (!clean.empty() && clean.back() == ' ') clean.pop_back();
	return clean;
}

inline std::string Narrow(const std::wstring& text) {
	if (text.empty()) return {};
	const int   size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
	std::string out(static_cast<std::size_t>(size > 0 ? size : 0), '\0');
	if (size > 0) WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
	return out;
}

inline std::wstring Wide(const std::string& text) {
	if (text.empty()) return {};
	const int    size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
	std::wstring out(static_cast<std::size_t>(size > 0 ? size : 0), L'\0');
	if (size > 0) MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size);
	return out;
}

// What fits in a box `width` units wide at `charWidth` units a character:
// the start of a long text, or its end while typing (where the caret is),
// cut with `ellipsis`.
inline std::string Fit(const std::string& text, int width, bool end, int charWidth = 17, const char* ellipsis = "\u2026") {
	std::vector<std::size_t> starts; // each character's first byte
	for (std::size_t i = 0; i < text.size(); ++i)
		if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) starts.push_back(i);
	const std::size_t room = static_cast<std::size_t>(std::max(4, width / std::max(1, charWidth)));
	if (starts.size() <= room) return text;
	if (end) return ellipsis + text.substr(starts[starts.size() - room + 1]);
	return text.substr(0, starts[room - 1]) + ellipsis;
}

} // namespace D2RGui
