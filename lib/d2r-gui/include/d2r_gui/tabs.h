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
// selected one (-1: none).
inline std::string Bar(const std::string& name, int x, int y, const std::vector<Tab>& tabs, int current, const Look& look = {}) {
	std::string out;
	const int   inset = (look.size - look.iconSize) / 2;
	for (std::size_t i = 0; i < tabs.size(); ++i) {
		const bool  on  = static_cast<int>(i) == current;
		const auto  n   = name + std::to_string(i);
		const int   tx  = TabX(x, static_cast<int>(i), look), ty = TabY(y, static_cast<int>(i), look);
		out += Button(n, tx, ty, StashKit::SlotButton(look.size, look.size, on), { .normal = 0, .pressed = 2, .hovered = 1 }, tabs[i].command, tabs[i].tooltip, look.size, look.size);
		if (const auto icon = Icons::Sprite(tabs[i].icon.c_str(), look.iconSize, on ? look.current : look.icon); !icon.empty()) out += Image(n + "Icon", tx + inset, ty + inset, icon);
	}
	return out;
}

} // namespace D2RGui::Tabs
