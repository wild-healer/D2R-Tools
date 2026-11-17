// D2R GUI: a window's menu structure, as Grail On!'s: a bar of menus (icon
// tabs), and for the current menu a bar of its pages (icon tabs), with the
// window's title. Picking a menu shows the page last seen in it. The
// plugin keeps what its pages show; Menus keeps which one is current, makes
// the bars and handles their clicks.
//
//   Gui::Menus::Nav nav("nav");                 // command prefix
//   nav.Set({ { .tab = {"scroll-unfurled", "Rules"}, .title = "Rules", .pages = {} },
//             { .tab = {"cog", "Settings"}, .title = "Settings",
//               .pages = { {.tab = {"eyeball", "Display"}}, {.tab = {"checklist", "Misc"}} } } });
//   c += nav.Layout(look);                      // in the window's layout
//   if (nav.Handle(command)) Rebuild();         // in the command handler
//   nav.Menu(), nav.Page()                      // what to show
//
// A menu without pages is a page itself (its bar of pages is not drawn).
// Opt-in with icons.h: #include <d2r_gui/menus.h>. UI thread.
#pragma once
#include "layout.h"
#include "tabs.h"
#include <cctype>
#include <cstdlib>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace D2RGui::Menus {

struct Page {
	Tabs::Tab tab; // its icon, tooltip (the command is set by Nav), sprite, badge, shown
};

struct Menu {
	Tabs::Tab                 tab;
	std::string               title; // the window's title while the menu is current
	std::vector<Page>         pages;
	std::optional<Tabs::Look> pageLook; // its pages' bar look (else Look.pages)
};

// Where the bars go, in the window (Grail On!: menus in a column left of the
// window, pages in a column right of it; TZ Plus: a row of tabs under the title).
struct Look {
	int        menuX = 20, menuY = 170;  // the menus bar
	int        pageX = 20, pageY = 290;  // the pages bar
	Tabs::Look menus { .size = 104, .iconSize = 64, .gap = 16 };
	Tabs::Look pages { .size = 84, .iconSize = 50, .gap = 18, .vertical = true };
	bool       title = true;             // the current menu's (or page's) title in the title band
	int        titleX = 0, titleY = 74, titleWidth = 1162, titleHeight = 84;
	const char* titleStyle =
	    R"({"fontColor": "$FontColorGoldYellow", "pointSize": 52, "dropShadow": "$DefaultDropShadow", "alignment": {"h": "center", "v": "center"}})";
};

class Nav {
public:
	explicit Nav(std::string prefix = "nav") : prefix_(std::move(prefix)) {}

	// The menus (again whenever they change: the current one and each menu's
	// last page stay when they still exist).
	void Set(std::vector<Menu> menus) {
		menus_ = std::move(menus);
		for (std::size_t m = 0; m < menus_.size(); ++m) {
			menus_[m].tab.command = prefix_ + "m" + std::to_string(m);
			for (std::size_t p = 0; p < menus_[m].pages.size(); ++p) menus_[m].pages[p].tab.command = prefix_ + "p" + std::to_string(p);
		}
		if (menu_ >= static_cast<int>(menus_.size()) || (menu_ >= 0 && !menus_[static_cast<std::size_t>(menu_)].tab.shown)) menu_ = First();
		if (menu_ < 0) menu_ = First();
		ClampPage();
	}

	int Menu() const { return menu_; }
	int Page() const { return page_; } // -1 for a menu without pages
	const std::vector<Menus::Menu>& Items() const { return menus_; }

	// Shows menu `m` (its last page) or page `p` of the current menu.
	void Go(int m, int p = -2) {
		if (m < 0 || m >= static_cast<int>(menus_.size())) return;
		if (menu_ >= 0) last_[menu_] = page_;
		menu_ = m;
		if (p == -2) {
			const auto found = last_.find(m);
			p                = found != last_.end() ? found->second : 0;
		}
		page_ = p;
		ClampPage();
		if (changed_) changed_(menu_, page_);
	}

	// Called after a click changed the menu or page.
	void OnChange(std::function<void(int menu, int page)> changed) { changed_ = std::move(changed); }

	// A click: true when it was one of the bars' ("<prefix>m<i>", "<prefix>p<i>").
	bool Handle(const std::string& command) {
		if (command.rfind(prefix_, 0) != 0 || command.size() < prefix_.size() + 2) return false;
		const char kind = command[prefix_.size()];
		const auto rest = command.c_str() + prefix_.size() + 1;
		if ((kind != 'm' && kind != 'p') || !std::isdigit(static_cast<unsigned char>(*rest))) return false;
		const int n = std::atoi(rest);
		if (kind == 'm') Go(n);
		else Go(menu_, n);
		return true;
	}

	// The current title: the menu's, or the page's tooltip when the menu has no title.
	std::string Title() const {
		if (menu_ < 0) return {};
		const auto& menu = menus_[static_cast<std::size_t>(menu_)];
		return !menu.title.empty() ? menu.title : menu.tab.tooltip;
	}

	// The bars (and the title) for the window's layout.
	std::string Layout(const Look& look = {}) const {
		std::string             out;
		std::vector<Tabs::Tab>  menus;
		for (const auto& menu : menus_) menus.push_back(menu.tab);
		if (menus.size() > 1) out += Tabs::Bar(prefix_ + "Menu", look.menuX, look.menuY, menus, menu_, look.menus);
		if (menu_ >= 0) {
			const auto& pages = menus_[static_cast<std::size_t>(menu_)].pages;
			if (!pages.empty()) {
				std::vector<Tabs::Tab> tabs;
				for (const auto& page : pages) tabs.push_back(page.tab);
				const auto& own = menus_[static_cast<std::size_t>(menu_)].pageLook;
				out += Tabs::Bar(prefix_ + "Page", look.pageX, look.pageY, tabs, page_, own ? *own : look.pages);
			}
		}
		if (look.title) out += Text(prefix_ + "Title", look.titleX, look.titleY, look.titleWidth, look.titleHeight, Title(), look.titleStyle);
		return out;
	}

private:
	int First() const {
		for (std::size_t m = 0; m < menus_.size(); ++m)
			if (menus_[m].tab.shown) return static_cast<int>(m);
		return -1;
	}
	void ClampPage() {
		if (menu_ < 0) {
			page_ = -1;
			return;
		}
		const auto& pages = menus_[static_cast<std::size_t>(menu_)].pages;
		if (pages.empty()) {
			page_ = -1;
			return;
		}
		if (page_ < 0 || page_ >= static_cast<int>(pages.size()) || !pages[static_cast<std::size_t>(page_)].tab.shown) {
			page_ = -1;
			for (std::size_t p = 0; p < pages.size() && page_ < 0; ++p)
				if (pages[p].tab.shown) page_ = static_cast<int>(p);
		}
	}

	std::string                             prefix_;
	std::vector<Menus::Menu>                menus_;
	int                                     menu_ = -1, page_ = -1;
	std::map<int, int>                      last_;
	std::function<void(int, int)>           changed_;
};

} // namespace D2RGui::Menus
