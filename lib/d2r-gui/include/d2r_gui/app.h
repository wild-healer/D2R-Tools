// D2R GUI: the whole window from a description. A plugin says what its
// window holds (a frame, menus, pages, and on each page components bound to
// the plugin's own data) and the library does the rest: the layout, the
// views, the clicks, typing, drop-downs, scrolling and the wheel, the game's
// left-panel behaviour, the console command and the key binding.
//
//   namespace App = D2RGui::App;
//   bool ShowOdd = true; int Mode = 0, Count = 10; std::string Name;
//
//   App::Window window {
//       .title   = "My Plugin",
//       .menus   = {
//           { .tab = {"scroll-unfurled", "Rules"}, .items = { App::Text("How it works...") } },
//           { .tab = {"cog", "Settings"}, .pages = {
//               { .tab = {"eyeball", "Display"}, .items = {
//                   App::Section("Display"),
//                   App::Check("Show odd rows", &ShowOdd).Tip("Shows or hides them"),
//                   App::Choice("Mode", &Mode, {"Fast", "Safe"}),
//                   App::Number("Count", &Count, 0, 99),
//                   App::Field("Name", &Name, "Type a name") } } } } },
//       .console = "myplugin",
//       .keybind = "Open My Plugin",
//   };
//   window.changed = [] { SaveSettings(); };   // after any change by the player
//
//   D2RLoaderLoadPlugin: App::Start(context, "my-plugin", std::move(window));
//   D2RLoaderUnloadPlugin: App::Stop();
//   Data changed elsewhere: App::Refresh();
//
// A page's components come from `items`, or from `content` (called at every
// view, for pages whose components depend on the data: lists, objectives).
// A component keeps a pointer to the plugin's variable: the window shows it
// and writes the player's changes back, then calls the component's
// OnChange, then the window's `changed`. Components:
//   Section(title)                      a gold title with a rule under it
//   Text(text)                          a paragraph, wrapped
//   Check(label, &flag)                 a check box
//   Choice(label, &index, choices)      a drop-down; long lists (more than 12,
//                                       or Filter()) scroll, take a filter box
//                                       and can have section headers (Entries)
//   Number(label, &value, min, max)     a number between arrows
//   Field(label, &text, hint)           a text box
//   Action(label, button, fn)           a button
//   List(rows, onClick)                 rows of text with a value, clickable
//   Cells(label)                        the plugin's ItemCells (window.cells), one row each
//   Custom(height, fn)                  the plugin's own widgets in a row
// Each takes .Tip(tooltip), .OnChange(fn), .When(fn: shown while true).
//
// Other commands (a Custom component's buttons) go to window.command; the
// library's own start with "a.", "typea.", "anav" and "abar". One
// window per plugin. The plugin no longer calls Init, Commands::Listen,
// Kit::Read, Font::Measure or LeftPanel itself.
//
// Opt-in: #include <d2r_gui/app.h> (with icons.h, menus.h, item_cells.h).
#pragma once
#include "character_window.h"
#include "commands.h"
#include "core.h"
#include "font.h"
#include "inventory_button.h"
#include "item_cells.h"
#include "kit.h"
#include "layout.h"
#include "left_panel.h"
#include "menus.h"
#include "panel_pool.h"
#include "scroll_bar.h"
#include "stash_window.h"
#include "text_input.h"
#include "widgets.h"
#include "window_hook.h"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace D2RGui::App {

// ---- components ----

struct Row {
	std::string text, value, tooltip; // the row's name, a value on its right, its tooltip
};

// Where a Custom component draws: names start with `name`, the row is at
// x, y (panel units, scrolled already) and `width` wide.
struct Place {
	std::string name;
	int         x = 0, y = 0, width = 0, height = 0;
};

struct Item {
	enum class Kind { Section, Text, Check, Choice, Number, Field, Action, List, Cells, Custom };
	Kind                                       kind = Kind::Text;
	std::string                                label, tooltip, button, hint;
	bool*                                      flag   = nullptr;
	int*                                       index  = nullptr;
	int*                                       number = nullptr;
	std::string*                               text   = nullptr;
	std::vector<DropDown::Entry>               entries;
	bool                                       filter = false;
	int                                        min = 0, max = 0, step = 1;
	std::function<std::string(int)>            format;   // Number: the value as shown
	std::function<std::vector<Row>()>          rows;     // List
	std::function<void(int row)>               pick;     // List: a row clicked
	std::function<std::string(int cell)>       cellText; // Cells: the text beside cell i
	std::function<std::string(const Place&)>   custom;
	int                                        height = 0; // Custom
	std::function<void()>                      changed, click;
	std::function<bool()>                      shown;

	Item Tip(std::string tip) const { auto c = *this; c.tooltip = std::move(tip); return c; }
	Item OnChange(std::function<void()> fn) const { auto c = *this; c.changed = std::move(fn); return c; }
	Item When(std::function<bool()> fn) const { auto c = *this; c.shown = std::move(fn); return c; }
	Item Filter(bool on = true) const { auto c = *this; c.filter = on; return c; }
	Item Step(int s) const { auto c = *this; c.step = std::max(1, s); return c; }
	Item Format(std::function<std::string(int)> fn) const { auto c = *this; c.format = std::move(fn); return c; }
};

inline Item Section(std::string title) { return { .kind = Item::Kind::Section, .label = std::move(title) }; }
inline Item Text(std::string text) { return { .kind = Item::Kind::Text, .label = std::move(text) }; }
inline Item Check(std::string label, bool* flag) { return { .kind = Item::Kind::Check, .label = std::move(label), .flag = flag }; }
inline Item Choice(std::string label, int* index, const std::vector<std::string>& choices) {
	Item item { .kind = Item::Kind::Choice, .label = std::move(label), .index = index };
	for (const auto& choice : choices) item.entries.push_back({ choice });
	return item;
}
// A choice with section headers (`index` counts the headers too).
inline Item Entries(std::string label, int* index, std::vector<DropDown::Entry> entries) {
	return { .kind = Item::Kind::Choice, .label = std::move(label), .index = index, .entries = std::move(entries) };
}
inline Item Number(std::string label, int* value, int min, int max) { return { .kind = Item::Kind::Number, .label = std::move(label), .number = value, .min = min, .max = max }; }
inline Item Field(std::string label, std::string* text, std::string hint = {}) { return { .kind = Item::Kind::Field, .label = std::move(label), .hint = std::move(hint), .text = text }; }
inline Item Action(std::string label, std::string button, std::function<void()> click) {
	return { .kind = Item::Kind::Action, .label = std::move(label), .button = std::move(button), .click = std::move(click) };
}
inline Item List(std::function<std::vector<Row>()> rows, std::function<void(int row)> pick = {}) { return { .kind = Item::Kind::List, .rows = std::move(rows), .pick = std::move(pick) }; }
inline Item Cells(std::function<std::string(int cell)> text = {}) { return { .kind = Item::Kind::Cells, .cellText = std::move(text) }; }
inline Item Custom(int height, std::function<std::string(const Place&)> draw) { return { .kind = Item::Kind::Custom, .custom = std::move(draw), .height = height }; }

// ---- structure ----

using Content = std::function<std::vector<Item>()>;

struct Page {
	Tabs::Tab         tab;     // icon and tooltip (the command is the library's)
	std::string       title;   // the window's title on this page (else the menu's)
	std::vector<Item> items;
	Content           content; // instead of items
};

struct Menu {
	Tabs::Tab         tab;
	std::string       title;
	std::vector<Page> pages;   // none: the menu is a page itself
	std::vector<Item> items;
	Content           content;
};

enum class Frame { Stash, Character };

struct InventoryLink {
	std::string sprite, tooltip; // the button's sprite (frames 0 normal, 1 pressed, 3 hovered; 52 x 60)
	int         x = 0, y = 0;    // where on the inventory
};

struct Window {
	std::string                 title;
	Frame                       frame = Frame::Stash;
	std::vector<Menu>           menus;    // or one page:
	std::vector<Item>           items;
	Content                     content;
	std::string                 console;  // a console command that opens and closes it
	std::string                 keybind;  // a key binding's name in the Controls menu
	std::optional<InventoryLink> inventory; // a button on the inventory that opens it
	std::optional<ItemCells::Options> cells; // the Cells component's item cells
	int                         views = 16; // panels in the pool

	std::function<void()>                         changed;  // after any change the player made
	std::function<bool(const std::string&)>       command;  // the plugin's own commands (Custom's buttons)
	std::function<void(bool open)>                opened;   // the window opened or closed
	std::function<void()>                         loaded;   // the game's tables are loaded (UI thread, once)
	std::function<void(bool inGame)>              inGame;   // a game was joined or left
	std::function<void()>                         tick;     // UI thread, about 4 times a second from `loaded`
	std::function<bool(UINT, WPARAM, LPARAM)>     mouse;    // window thread, every message first (true: kept from the game)
};

namespace Detail {

struct Area {
	int left, top, right, bottom, bar;
};

inline Area AreaOf(Frame frame) {
	if (frame == Frame::Character) return { 105, 180, 1044, 1360, 1054 };
	return { StashWindow::InnerX + 30, StashWindow::InnerY + 12, StashWindow::InnerX + StashWindow::InnerWidth - 60, StashWindow::InnerY + StashWindow::InnerHeight - 20,
		     StashWindow::InnerX + StashWindow::InnerWidth - 50 };
}

constexpr int RowHeight = 70, BoxHeight = 56, LineHeight = 52, ListLines = 10, ListRow = 80, WheelStep = 120, ListAt = 12;
constexpr int PanelW = StashWindow::Width, PanelH = StashWindow::Height;

struct State {
	Window                     window;
	PanelPool                  pool;
	Menus::Nav                 nav { "anav" };
	std::unique_ptr<ScrollBar> bar;
	std::atomic<bool>          open {};
	bool                       inGame {}, ticking {}, hooked {}, cellsOn {};
	int                        scroll = 0;
	int                        drop   = -1; // the open drop-down's component
	DropDown::Scroll           list;
	std::string                filter;
	int                        dropY = 0, dropX = 0, dropW = 0;
	int                        cellsX = 0, cellsY = 0;
	bool                       cellsWanted {};
	InventoryButton            button;
	bool                       hasButton {};
	bool                       hasCells {};
};

inline State& Get() {
	static State state;
	return state;
}

inline bool Long(const Item& item) { return item.filter || item.entries.size() > ListAt; }

// The current page's components.
inline std::vector<Item> Items() {
	auto& s = Get();
	if (s.window.menus.empty()) return s.window.content ? s.window.content() : s.window.items;
	const int m = s.nav.Menu(), p = s.nav.Page();
	if (m < 0 || m >= static_cast<int>(s.window.menus.size())) return {};
	const auto& menu = s.window.menus[static_cast<std::size_t>(m)];
	if (p >= 0 && p < static_cast<int>(menu.pages.size())) {
		const auto& page = menu.pages[static_cast<std::size_t>(p)];
		return page.content ? page.content() : page.items;
	}
	return menu.content ? menu.content() : menu.items;
}

inline std::string Title() {
	auto& s = Get();
	if (s.window.menus.empty()) return s.window.title;
	const int m = s.nav.Menu(), p = s.nav.Page();
	if (m < 0 || m >= static_cast<int>(s.window.menus.size())) return s.window.title;
	const auto& menu = s.window.menus[static_cast<std::size_t>(m)];
	if (p >= 0 && p < static_cast<int>(menu.pages.size()) && !menu.pages[static_cast<std::size_t>(p)].title.empty()) return menu.pages[static_cast<std::size_t>(p)].title;
	if (!menu.title.empty()) return menu.title;
	return !s.window.title.empty() ? s.window.title : menu.tab.tooltip;
}

inline void SetNav() {
	auto&                    s = Get();
	std::vector<Menus::Menu> menus;
	for (const auto& menu : s.window.menus) {
		Menus::Menu m { .tab = menu.tab, .title = menu.title };
		for (const auto& page : menu.pages) m.pages.push_back({ page.tab });
		menus.push_back(std::move(m));
	}
	s.nav.Set(std::move(menus));
}

inline int Lines(const std::string& text, int width) { return std::max(1, Font::WrappedLines(text, width, std::max(20, width / 17), false)); }

inline int HeightOf(const Item& item, int width) {
	switch (item.kind) {
	case Item::Kind::Section: return 100;
	case Item::Kind::Text: return Lines(item.label, width) * std::max(36, Font::LineHeight(false)) + 16;
	case Item::Kind::List: return item.rows ? static_cast<int>(item.rows().size()) * ListRow : 0;
	case Item::Kind::Cells: return Get().hasCells ? (Get().window.cells->vertical ? ItemCells::Count() * ItemCells::Cell : ItemCells::Cell) + 20 : 0;
	case Item::Kind::Custom: return item.height;
	default: return RowHeight;
	}
}

inline std::string Shown(const Item& item) {
	if (!item.index || *item.index < 0 || *item.index >= static_cast<int>(item.entries.size())) return {};
	return item.entries[static_cast<std::size_t>(*item.index)].text;
}

// One component at `y` in the content's clip (scrolled), `i` its index;
// the clip is at panel y `a.top`, `room` tall.
inline std::string Draw(const Item& item, int i, int y, int room, const Area& a) {
	auto&             s = Get();
	const std::string n = "I" + std::to_string(i), cmd = "a." + std::to_string(i);
	const int         width = a.right - a.left, labelW = width / 2;
	std::string       out;
	switch (item.kind) {
	case Item::Kind::Section: out += SectionTitle(n, a.left, y, width, item.label, item.tooltip); break;
	case Item::Kind::Text: out += D2RGui::Text(n, a.left, y, width, HeightOf(item, width) - 8, item.label, Style::Comment, item.tooltip); break;
	case Item::Kind::Check:
		out += D2RGui::Text(n + "L", a.left, y, width - 80, RowHeight, item.label, Style::Label, item.tooltip);
		out += D2RGui::Check(n, a.right - 60, y + 7, item.flag && *item.flag, "a.c" + std::to_string(i), item.tooltip);
		break;
	case Item::Kind::Choice: {
		const int w = item.label.empty() ? width : std::min(520, width - labelW + 60);
		if (!item.label.empty()) out += D2RGui::Text(n + "L", a.left, y, width - w - 20, RowHeight, item.label, Style::Label, item.tooltip);
		out += DropDown::Box(n, a.right - w, y + 7, w, BoxHeight, Shown(item), "a.d" + std::to_string(i), item.tooltip);
		if (s.drop == i) s.dropX = a.right - w, s.dropY = a.top + y + 7, s.dropW = w;
		break;
	}
	case Item::Kind::Number: {
		const int  value = item.number ? *item.number : 0;
		const auto shown = item.format ? item.format(value) : std::to_string(value);
		out += D2RGui::Text(n + "L", a.left, y, width - 420, RowHeight, item.label, Style::Label, item.tooltip);
		out += Stepper(n, a.right - 400, y, 400, RowHeight, shown, "a.n" + std::to_string(i) + "-", "a.n" + std::to_string(i) + "+", item.tooltip);
		break;
	}
	case Item::Kind::Field: {
		const int w = item.label.empty() ? width : std::min(600, width - 300);
		if (!item.label.empty()) out += D2RGui::Text(n + "L", a.left, y, width - w - 20, RowHeight, item.label, Style::Label, item.tooltip);
		out += TypedBox(n, "a.f" + std::to_string(i), a.right - w, y + 7, w, BoxHeight, item.text ? *item.text : std::string(), item.hint, item.tooltip);
		break;
	}
	case Item::Kind::Action: {
		const int w = item.label.empty() ? width : 420;
		if (!item.label.empty()) out += D2RGui::Text(n + "L", a.left, y, width - w - 20, RowHeight, item.label, Style::Label, item.tooltip);
		out += TextButton(n, a.right - w, y + 7, w, BoxHeight, item.button, "a.b" + std::to_string(i), item.tooltip);
		break;
	}
	case Item::Kind::List: {
		const auto rows = item.rows ? item.rows() : std::vector<Row> {};
		for (int r = 0; r < static_cast<int>(rows.size()); ++r) {
			const int ry = y + r * ListRow;
			if (ry + ListRow < 0 || ry > room) continue; // outside the room: not drawn
			const auto& row = rows[static_cast<std::size_t>(r)];
			const auto  rn  = n + "R" + std::to_string(r);
			out += Image(rn + "B", a.left, ry + 3, Kit::Row(width, ListRow - 6));
			out += D2RGui::Text(rn + "T", a.left + 30, ry, row.value.empty() ? width - 60 : width * 2 / 3, ListRow, row.text, Style::Label, row.tooltip);
			if (!row.value.empty()) out += D2RGui::Text(rn + "V", a.left + width / 2, ry, width / 2 - 30, ListRow, row.value, Style::SmallRight);
			if (item.pick) out += HitButton(rn + "H", a.left, ry + 3, width, ListRow - 6, "a.r" + std::to_string(i) + ":" + std::to_string(r), row.tooltip);
		}
		break;
	}
	case Item::Kind::Cells: {
		if (!s.hasCells) break;
		const bool vertical = s.window.cells->vertical;
		const int  count    = ItemCells::Count(), cell = ItemCells::Cell;
		const int  height   = (vertical ? count * cell : cell);
		// an overlay: shown only while all of it is in the room
		if (y >= 0 && y + height <= room) s.cellsWanted = true, s.cellsX = StashWindow::X + a.left, s.cellsY = StashWindow::Y + a.top + y;
		if (vertical && item.cellText)
			for (int c = 0; c < count; ++c)
				out += D2RGui::Text(n + "C" + std::to_string(c), a.left + cell + 30, y + c * cell, width - cell - 30, cell, item.cellText(c), Style::Label);
		break;
	}
	case Item::Kind::Custom:
		if (item.custom) out += item.custom({ .name = n, .x = a.left, .y = y, .width = width, .height = item.height });
		break;
	}
	return out;
}

inline std::string Build(const std::string& panelName, const std::string& probe) {
	auto&      s = Get();
	Area       a = AreaOf(s.window.frame);
	std::string fixed;
	// the menus and their pages, in rows on top
	if (!s.window.menus.empty()) {
		Menus::Look look;
		look.title = false;
		look.menus = { .size = 96, .iconSize = 60, .gap = 14 };
		look.pages = { .size = 76, .iconSize = 46, .gap = 12 };
		look.menuX = a.left, look.menuY = a.top;
		look.pageX = a.left, look.pageY = a.top;
		const bool menuBar = s.window.menus.size() > 1;
		if (menuBar) a.top += look.menus.size + 20, look.pageY = a.top;
		const int m = s.nav.Menu();
		if (m >= 0 && !s.window.menus[static_cast<std::size_t>(m)].pages.empty()) a.top += look.pages.size + 20;
		fixed += s.nav.Layout(look);
	}
	const int  room  = a.bottom - a.top, width = a.right - a.left;
	const auto items = Items();
	int        total = 0;
	for (const auto& item : items)
		if (!item.shown || item.shown()) total += HeightOf(item, width);
	s.scroll        = std::clamp(s.scroll, 0, std::max(0, total - room));
	s.cellsWanted   = false;
	std::string inside;
	int         y = 0;
	for (int i = 0; i < static_cast<int>(items.size()); ++i) {
		const auto& item = items[static_cast<std::size_t>(i)];
		if (item.shown && !item.shown()) continue;
		const int h = HeightOf(item, width), at = y - s.scroll;
		y += h;
		if (item.kind != Item::Kind::List && item.kind != Item::Kind::Cells && (at + h < 0 || at > room)) {
			if (s.drop == i) s.drop = -1;
			continue;
		}
		inside += Draw(item, i, at, room, a);
	}
	if (s.drop >= 0 && s.dropY < a.top) s.drop = -1;
	std::string c = fixed + Clip("Content", 0, a.top, a.bar - 6, room, inside);
	c += s.bar->Layout(a.top, room, total, s.scroll);
	// the open drop-down, over everything
	if (s.drop >= 0 && s.drop < static_cast<int>(items.size())) {
		const auto& item = items[static_cast<std::size_t>(s.drop)];
		const int   cur  = item.index ? *item.index : -1;
		const std::string pick = "a.p" + std::to_string(s.drop) + ":";
		if (Long(item)) {
			const int lines  = static_cast<int>(DropDown::Filtered(item.entries, s.filter).size());
			const int height = (std::min(lines, ListLines) + 1) * LineHeight;
			const int ly     = s.dropY + BoxHeight + height > PanelH - 40 ? s.dropY - height : s.dropY + BoxHeight;
			c += DropDown::ScrollList("Drop", s.dropX, ly, std::max(s.dropW, 520), LineHeight, ListLines, item.entries, cur, pick, "a.dclose", PanelW, PanelH, s.list.first, s.filter, "a.lf", "a.ls");
		} else {
			std::vector<std::string> choices;
			for (const auto& entry : item.entries) choices.push_back(entry.text);
			const int height = static_cast<int>(choices.size()) * LineHeight;
			const int ly     = s.dropY + BoxHeight + height > PanelH - 40 ? s.dropY - height : s.dropY + BoxHeight;
			c += DropDown::List("Drop", s.dropX, ly, s.dropW, LineHeight, choices, cur, pick, "a.dclose", PanelW, PanelH);
		}
	}
	const auto title = Title();
	if (s.window.frame == Frame::Character) return CharacterWindow::Layout(panelName, probe, title, "a.close", c);
	return StashWindow::Layout(panelName, title, "a.close", Probe(probe) + c);
}

inline void SyncCells() {
	auto& s = Get();
	if (!s.hasCells) return;
	const bool on = s.open && s.cellsWanted;
	if (on) ItemCells::Place(s.cellsX, s.cellsY);
	if (on != s.cellsOn || on != ItemCells::Shown()) ItemCells::Show(on), s.cellsOn = on;
}

inline void Changed(const Item& item) {
	if (item.changed) item.changed();
	if (Get().window.changed) Get().window.changed();
}

inline void Close() {
	auto& s = Get();
	TextInput::Stop();
	s.pool.Close();
	s.drop = -1;
	const bool was = s.open.exchange(false);
	s.cellsWanted  = false;
	SyncCells();
	LeftPanel::Closed();
	if (was && s.window.opened) s.window.opened(false);
}

inline void Wheel(int notches) {
	auto& s = Get();
	if (s.drop >= 0) {
		const auto items = Items();
		if (s.drop < static_cast<int>(items.size()) && Long(items[static_cast<std::size_t>(s.drop)])) {
			s.list.Wheel(notches, static_cast<int>(DropDown::Filtered(items[static_cast<std::size_t>(s.drop)].entries, s.filter).size()), ListLines);
			s.pool.Show();
		}
		return;
	}
	s.scroll = std::clamp(s.scroll + notches * WheelStep, 0, std::max(0, s.bar->Max()));
	s.pool.Show();
}

inline void Open() {
	auto& s = Get();
	if (!s.pool.Ready()) return;
	Font::Refresh();
	if (!s.hooked) {
		s.hooked = true;
		WindowHook::Start({ .active = [] { return Get().open.load(); }, .wheel = Wheel, .extra = s.window.mouse });
		s.bar->Attach();
	}
	LeftPanel::Opening();
	s.open = true;
	s.pool.Show();
	if (s.window.opened) s.window.opened(true);
}

inline void Toggle() {
	if (Get().pool.IsOpen() || Get().pool.Busy()) Close();
	else Open();
}

inline void Tick() {
	auto& s = Get();
	if (s.open) {
		const bool open = s.pool.IsOpen() || s.pool.Busy();
		if (!open) Close();
		else if (LeftPanel::Watch(s.pool.IsOpen(), s.pool.Busy())) Close();
	}
	if (s.hasCells) ItemCells::Tick();
	if (s.hasButton) s.button.Watch(), s.button.Reload(s.inGame);
	if (s.window.tick) s.window.tick();
}

inline void Type(const std::string& key, std::string* target, Item item) {
	auto& s = Get();
	TextInput::Start(key, target ? *target : std::string(), {
	    .changed = [target, item](const std::string& text) { if (target) *target = text; Changed(item); Get().pool.Show(); },
	    .enter   = [](const std::string&) { Get().pool.Show(); },
	    .escape  = [] { Get().pool.Show(); },
	});
	s.pool.Show();
}

inline bool ParseInt(const std::string& rest, int& out) {
	if (rest.empty() || !std::isdigit(static_cast<unsigned char>(rest[0]))) return false;
	out = std::atoi(rest.c_str());
	return true;
}

inline void OnCommand(const std::string& command) {
	auto& s = Get();
	if (command == "a.toggle") return Toggle();
	if (command == "a.open") return s.pool.IsOpen() ? s.pool.Show() : Open();
	if (s.open && s.bar->Handle(command)) return;
	if (s.open && s.nav.Handle(command)) {
		s.scroll = 0, s.drop = -1;
		TextInput::Stop();
		return s.pool.Show();
	}
	if (command.rfind("a.", 0) != 0 && command.rfind("typea.", 0) != 0) {
		if (s.window.command && s.window.command(command) && s.open) s.pool.Show();
		return;
	}
	if (!s.open) return;
	if (command == "a.close") return Close();
	const auto items = Items();
	if (command == "a.dclose") {
		s.drop = -1;
		if (TextInput::Key() == "a.lf") TextInput::Stop();
		return s.pool.Show();
	}
	if (s.drop >= 0 && s.drop < static_cast<int>(items.size())) {
		const auto& item  = items[static_cast<std::size_t>(s.drop)];
		const int   lines = static_cast<int>(DropDown::Filtered(item.entries, s.filter).size());
		if (s.list.Handle(command, "a.ls", lines, ListLines)) return s.pool.Show();
		if (command == "typea.lf") {
			TextInput::Start("a.lf", s.filter, {
			    .changed = [](const std::string& text) { Get().filter = text, Get().list.first = 0, Get().pool.Show(); },
			    .enter   = [](const std::string&) { Get().pool.Show(); },
			    .escape  = [] { Get().filter.clear(), Get().pool.Show(); },
			});
			return s.pool.Show();
		}
	}
	if (command.rfind("typea.f", 0) == 0) {
		int i = 0;
		if (ParseInt(command.substr(7), i) && i < static_cast<int>(items.size())) Type(command.substr(4), items[static_cast<std::size_t>(i)].text, items[static_cast<std::size_t>(i)]);
		return;
	}
	if (command.size() < 4) return;
	const char kind = command[2];
	int        i    = 0;
	if (!ParseInt(command.substr(3), i) || i >= static_cast<int>(items.size())) return;
	const auto& item = items[static_cast<std::size_t>(i)];
	const auto  rest = command.substr(3 + std::to_string(i).size());
	if (TextInput::Active() && TextInput::Key() != "a.lf") TextInput::Stop();
	switch (kind) {
	case 'c':
		if (item.flag) *item.flag = !*item.flag, Changed(item);
		break;
	case 'd':
		if (s.drop == i) s.drop = -1;
		else s.drop = i, s.list = {}, s.filter.clear();
		if (s.drop >= 0 && item.index && Long(item)) { // the current choice in view
			const auto shown = DropDown::Filtered(item.entries, {});
			const auto at    = std::find(shown.begin(), shown.end(), *item.index);
			if (at != shown.end()) s.list.first = static_cast<int>(at - shown.begin()) - ListLines / 2, s.list.Clamp(static_cast<int>(shown.size()), ListLines);
		}
		break;
	case 'p': {
		int k = 0;
		if (rest.size() > 1 && ParseInt(rest.substr(1), k) && k < static_cast<int>(item.entries.size()) && !item.entries[static_cast<std::size_t>(k)].header && item.index) {
			const bool differs = *item.index != k;
			*item.index        = k;
			if (differs) Changed(item);
		}
		s.drop = -1;
		if (TextInput::Key() == "a.lf") TextInput::Stop();
		break;
	}
	case 'n':
		if (item.number) {
			const int before = *item.number;
			*item.number     = std::clamp(*item.number + (rest == "-" ? -item.step : item.step), item.min, item.max);
			if (*item.number != before) Changed(item);
		}
		break;
	case 'b':
		if (item.click) item.click();
		break;
	case 'r': {
		int r = 0;
		if (item.pick && rest.size() > 1 && ParseInt(rest.substr(1), r)) item.pick(r);
		break;
	}
	default: return;
	}
	s.pool.Show();
}

inline void __cdecl OnLoaded(const D2RL::PluginContext*, const D2RL::Lifecycle::DataTablesLoadedEvent*, void*) noexcept {
	auto& s = Get();
	if (s.ticking) return;
	s.ticking = true;
	Kit::Read();
	Font::Measure();
	if (s.window.loaded) s.window.loaded();
	std::thread([] {
		for (;;) {
			Sleep(250);
			if (!Get().ticking) return;
			RunOnUi(Tick);
		}
	}).detach();
}

inline void __cdecl OnReady(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept {
	Get().inGame = true;
	LeftPanel::InGame(true);
	if (Get().window.inGame) Get().window.inGame(true);
}

inline void __cdecl OnLeft(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept {
	Get().inGame = false;
	LeftPanel::InGame(false);
	if (Get().window.inGame) Get().window.inGame(false);
}

inline auto OnAction(const D2RL::PluginContext*, const D2RL::Input::ActionEvent* event, void*) noexcept -> D2RL::Input::ActionResult {
	if (!D2RL::Input::HasActionEventField(event, D2RL::Input::ActionEventRequiredSize) || event->kind != D2RL::Input::ActionEventKind::Pressed) return D2RL::Input::ActionResult::Ignored;
	Commands::Post("a.toggle");
	return D2RL::Input::ActionResult::Handled;
}

inline auto OnConsole(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
	if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
	Commands::Post("a.toggle");
	return D2RL::ConsoleCommandResult::Handled;
}

} // namespace Detail

// While the plugin loads: the window, its panels and everything around it.
// False when a service it needs is missing (the plugin stays loaded, inactive).
inline bool Start(const D2RL::PluginContext* context, const char* pluginId, Window window) {
	using namespace Detail;
	auto& s  = Get();
	s.window = std::move(window);
	if (!Init(context, pluginId)) return false;
	const Area a = AreaOf(s.window.frame);
	s.bar        = std::make_unique<ScrollBar>("abar", a.bar, 30, 30, StashWindow::X, StashWindow::Y);
	s.bar->OnScroll([](int scroll) { Get().scroll = scroll, Get().pool.Show(); });
	SetNav();
	if (!s.pool.Register(Build, "View", std::max(2, s.window.views))) return false;
	s.pool.OnShown(SyncCells);
	Commands::Listen(OnCommand);
	if (s.window.cells) s.hasCells = ItemCells::Register(*s.window.cells);
	if (s.window.inventory)
		s.hasButton = s.button.Register("OpenButton", { .buttonSprite = s.window.inventory->sprite, .tooltip = s.window.inventory->tooltip, .command = "a.toggle" }, s.window.inventory->x,
		                                s.window.inventory->y);
	if (const auto* lifecycle = D2RGui::Detail::Query<D2RL::LifecycleService>(context, "lifecycle")) {
		D2RL::Lifecycle::ListenerHandle                 handle {};
		const D2RL::Lifecycle::DataTablesLoadedListener loaded { .structSize = sizeof(D2RL::Lifecycle::DataTablesLoadedListener), .callback = OnLoaded };
		(void)lifecycle->registerDataTablesLoadedListener(context, &loaded, &handle);
		const D2RL::Lifecycle::GameplayEventListener ready { .structSize = D2RL::Lifecycle::GameplayEventListenerSize, .kind = D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady, .callback = OnReady };
		(void)lifecycle->registerGameplayEventListener(context, &ready, &handle);
		const D2RL::Lifecycle::GameplayEventListener left { .structSize = D2RL::Lifecycle::GameplayEventListenerSize, .kind = D2RL::Lifecycle::GameplayEventKind::GameLeft, .callback = OnLeft };
		(void)lifecycle->registerGameplayEventListener(context, &left, &handle);
	}
	if (!s.window.keybind.empty())
		if (const auto* input = D2RGui::Detail::Query<D2RL::InputService>(context, "input"); input && D2RL::HasInputServiceField(input, D2RL::InputServiceRequiredSize)) {
			static std::string logical, category;
			logical  = std::string("open-") + pluginId;
			category = !s.window.title.empty() ? s.window.title : std::string(pluginId);
			const D2RL::Input::ActionRegistration action {
				.structSize       = D2RL::Input::ActionRegistrationSize,
				.logicalId        = logical.c_str(),
				.displayName      = s.window.keybind.c_str(),
				.category         = category.c_str(),
				.defaultPrimary   = { .key = D2RL::Input::Key::None },
				.defaultSecondary = { .key = D2RL::Input::Key::None },
				.callback         = OnAction,
			};
			D2RL::Input::ActionHandle handle = D2RL::Input::InvalidHandle;
			(void)input->registerAction(context, &action, &handle);
		}
	if (!s.window.console.empty()) {
		static std::string help;
		help = "Opens or closes the " + (s.window.title.empty() ? std::string(pluginId) : s.window.title) + " window.";
		(void)context->RegisterConsoleCommand(s.window.console.c_str(), OnConsole, help.c_str());
	}
	return true;
}

// While the plugin unloads.
inline void Stop() {
	Detail::Get().ticking = false;
	TextInput::Shutdown();
	WindowHook::Stop();
}

// UI thread: open, close, toggle the window; show it again with the data as it is now.
inline void Open() { Detail::Get().open ? Detail::Get().pool.Show() : Detail::Open(); }
inline void Close() { Detail::Close(); }
inline void Toggle() { Detail::Toggle(); }
inline void Refresh() {
	if (Detail::Get().open) Detail::Get().pool.Show();
}
inline bool IsOpen() { return Detail::Get().open; }

// UI thread: shows menu `m` (its last page, or page `p`).
inline void Go(int menu, int page = -2) {
	Detail::Get().nav.Go(menu, page);
	Detail::Get().scroll = 0, Detail::Get().drop = -1;
	Refresh();
}
inline int CurrentMenu() { return Detail::Get().nav.Menu(); }
inline int CurrentPage() { return Detail::Get().nav.Page(); }

// UI thread: new menus (tabs shown or hidden, pages added); the current one stays when it can.
inline void SetMenus(std::vector<Menu> menus) {
	Detail::Get().window.menus = std::move(menus);
	Detail::SetNav();
	Refresh();
}

// The inventory button (move it, hide it: InventoryButton).
inline InventoryButton& Button() { return Detail::Get().button; }

} // namespace D2RGui::App
