// D2R GUI: a row (or column) of icon tabs, as Grail On!'s menus and
// categories: each tab a square slot button with an icon of icons.h on it,
// lighter hovered, darker pressed, gold-rimmed and its icon brighter when it
// is the current one. A click sends the tab's command (commands.h).
//
//   std::vector<Tabs::Tab> tabs { {"scroll-unfurled", "Rules", "tab0"}, {"pentagram-rose", "Offering", "tab1"} };
//   c += Tabs::Bar("Tab", x, y, tabs, current);
//
// Opt-in with icons.h: #include <d2r_gui/tabs.h>. UI thread (the sprites are
// registered the first time a size is used).
#pragma once
#include "icons.h"
#include "layout.h"
#include "stash_kit.h"
#include <string>
#include <vector>

namespace D2RGui::Tabs {

struct Tab {
	std::string icon;    // an icons.h name ("cog")
	std::string tooltip; // its tooltip (text or "@<id>:<key>")
	std::string command; // what a click sends
	std::string sprite;  // instead of `icon`: any sprite name (escaped, as SpriteName gives), drawn at iconSize, e.g. item art
	std::string badge;   // a sprite drawn over the button under the icon (a progress ring), `size` + 2 * badgePad square
	int         badgePad = 0; // how far the badge reaches past the button on each side
	std::string art;     // instead of `icon`: an item art path (raw, as in the game's files), artWidth x artHeight cells of artUnit, scaled to iconSize
	int         artWidth = 1, artHeight = 1, artUnit = 98;
	bool        seeThrough = false; // the icon or art drawn see-through (ethereal items' look)
	int         gapBefore = 0;      // more room before this tab (a new group)
	bool        shown = true;
};

struct Look {
	int  size     = 84;  // a tab's square
	int  iconSize = 50;  // the icon inside it
	int  gap      = 18;  // between two tabs
	bool vertical = false;
	Rgb  icon { 215, 200, 150 };   // the game's gold text colour
	Rgb  current { 255, 240, 190 }; // the current tab's icon
};

// Where tab `index` goes, from the bar's x, y.
inline int TabX(int x, int index, const Look& look = {}) { return look.vertical ? x : x + index * (look.size + look.gap); }
inline int TabY(int y, int index, const Look& look = {}) { return look.vertical ? y + index * (look.size + look.gap) : y; }

// The bar's length along its direction.
inline int Length(std::size_t count, const Look& look = {}) { return count ? static_cast<int>(count) * (look.size + look.gap) - look.gap : 0; }

// The tabs at x, y (widgets <name>0, <name>0Icon, <name>1...), `current` the
// selected one (-1: none). Hidden tabs (shown false) take no place.
inline std::string Bar(const std::string& name, int x, int y, const std::vector<Tab>& tabs, int current, const Look& look = {}) {
	std::string out;
	const int   inset = (look.size - look.iconSize) / 2;
	int         slot  = 0, extra = 0; // extra: the gaps before groups so far
	for (std::size_t i = 0; i < tabs.size(); ++i) {
		if (!tabs[i].shown) continue;
		const bool on = static_cast<int>(i) == current;
		const auto n  = name + std::to_string(i);
		extra += tabs[i].gapBefore;
		const int  tx = TabX(x, slot, look) + (look.vertical ? 0 : extra), ty = TabY(y, slot, look) + (look.vertical ? extra : 0);
		++slot;
		out += Button(n, tx, ty, StashKit::SlotButton(look.size, look.size, on), { .normal = 0, .pressed = 2, .hovered = 1 }, tabs[i].command, tabs[i].tooltip, look.size, look.size);
		if (!tabs[i].badge.empty()) out += Image(n + "Badge", tx - tabs[i].badgePad, ty - tabs[i].badgePad, tabs[i].badge);
		const std::string look2 = tabs[i].seeThrough ? R"(, "transparency": 0.55)" : "";
		if (!tabs[i].art.empty()) {
			const int    w = std::max(1, tabs[i].artWidth), h = std::max(1, tabs[i].artHeight), unit = std::max(1, tabs[i].artUnit);
			const double scale = static_cast<double>(look.iconSize) / (std::max(w, h) * unit);
			out += ScaledImage(n + "Icon", tx + inset + static_cast<int>((look.iconSize - w * unit * scale) / 2), ty + inset + static_cast<int>((look.iconSize - h * unit * scale) / 2), scale, Path(tabs[i].art), look2);
			continue;
		}
		const auto icon = !tabs[i].sprite.empty() ? tabs[i].sprite : Icons::Sprite(tabs[i].icon.c_str(), look.iconSize, on ? look.current : look.icon);
		if (!icon.empty()) out += Image(n + "Icon", tx + inset, ty + inset, icon, look2);
	}
	return out;
}

} // namespace D2RGui::Tabs
