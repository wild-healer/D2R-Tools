// D2R GUI: an icon set for buttons, menus and tiles: 120 icons from
// game-icons.net (CC BY 3.0, see ICONS-CREDITS.txt: a plugin using them
// credits them), named as on game-icons.net ("visored-helm", "cog"...).
// Preview: docs/icons.png. tools/gen_icons.py makes the set again (add a
// name to its list to get another of the site's 4000 icons).
//
// Opt-in: #include <d2r_gui/icons.h> (about 450 KB of coded masks in the DLL).
//
//   Icons::Picture("cog", 64, {215, 200, 150})  the icon, any size and colour
//   Icons::Sprite("cog", 64, {215, 200, 150})   a sprite name for "filename",
//                                               registered the first time
//   Icons::Mask("cog", 64)                      its coverage, one byte a pixel
#pragma once
#include "icons_data.inc"
#include "picture.h"
#include "resources.h"
#include "stash_art.h"
#include <cstdio>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace D2RGui::Icons {

// Whether the set has icon `name`.
inline bool Has(const char* name) {
	for (const auto& icon : Detail::Table)
		if (!std::strcmp(icon.name, name)) return true;
	return false;
}

// Every icon's name.
inline std::vector<const char*> Names() {
	std::vector<const char*> names;
	for (const auto& icon : Detail::Table) names.push_back(icon.name);
	return names;
}

// Icon `name`'s coverage, `size` square (area average from 196 px); empty
// when the set has no such icon.
inline std::vector<unsigned char> Mask(const char* name, int size = Detail::Stored) {
	for (const auto& icon : Detail::Table) {
		if (std::strcmp(icon.name, name)) continue;
		std::vector<unsigned char> full;
		full.reserve(static_cast<std::size_t>(Detail::Stored) * Detail::Stored);
		for (unsigned i = 0; i < icon.size; ++i) {
			const unsigned char v = icon.data[i];
			if ((v == 0 || v == 255) && i + 1 < icon.size) full.insert(full.end(), icon.data[++i], v);
			else full.push_back(v);
		}
		full.resize(static_cast<std::size_t>(Detail::Stored) * Detail::Stored);
		if (size == Detail::Stored || size <= 0) return full;
		D2RGui::Picture coverage = Blank(Detail::Stored, Detail::Stored);
		for (std::size_t p = 0; p < full.size(); ++p) coverage.rgba[p * 4 + 3] = full[p];
		const auto small = ResizeBox(coverage, size, size);
		std::vector<unsigned char> out(static_cast<std::size_t>(size) * size);
		for (std::size_t p = 0; p < out.size(); ++p) out[p] = small.rgba[p * 4 + 3];
		return out;
	}
	return {};
}

// Icon `name` in `colour`, `size` square; empty when the set has no such icon.
inline D2RGui::Picture Picture(const char* name, int size, Rgb colour) {
	const auto mask = Mask(name, size);
	if (mask.empty()) return {};
	return MaskIcon(mask.data(), size, colour.r, colour.g, colour.b);
}

// The sprite name (JSON-escaped, for "filename") of icon `name` at `size`
// in `colour`, registered the first time (UI thread). Empty when the set has
// no such icon or it could not be registered.
inline std::string Sprite(const char* name, int size, Rgb colour) {
	static std::mutex            lock;
	static std::set<std::string> made;
	char                         key[128];
	std::snprintf(key, sizeof(key), "icon-%s-%d-%02x%02x%02x", name, size, colour.r & 0xFF, colour.g & 0xFF, colour.b & 0xFF);
	std::lock_guard guard(lock);
	if (!made.count(key)) {
		auto picture = Picture(name, size, colour);
		if (picture.rgba.empty() || !RegisterSprite(key, { picture })) return {};
		made.insert(key);
	}
	return SpriteName(key);
}

} // namespace D2RGui::Icons
