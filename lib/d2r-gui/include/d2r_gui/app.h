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
//       .changed = [] { SaveSettings(); },
//   };
//
//   D2RLoaderLoadPlugin:   App::Start(context, "my-plugin", std::move(window));
//   D2RLoaderUnloadPlugin: App::Stop();
//   data changed elsewhere: App::Refresh();
//
// Values: a component takes a pointer to the plugin's variable, or a Bind
// (App::Bound(get, set)) when the value lives elsewhere (another plugin's
// callbacks, a TOML text). A set may refuse: it returns the reason, shown in
// the window's notice line (a set returning void or bool works too).
//
// A page's components come from `items`, or from `content` (called at every
// view). Each page and the window can have a fixed `header` and `footer`
// (components outside the scrolled part). App::Push(page) opens a page over
// the current one with a back arrow, its title and subtitle; App::Back()
// returns, each level keeping its scroll.
//
// Components:
//   Section(title)                      a gold title with a rule under it
//   Text(text)                          a paragraph, wrapped
//   Check(label, flag)                  a check box
//   Choice(label, index, choices)       a drop-down; long lists (more than 12,
//   Entries(label, index, entries)      or .Filter()) scroll, take a filter box
//                                       and can have section headers
//   Number(label, value, min, max)      a number between arrows (.Decimals, .Step,
//                                       Shift and Ctrl steps)
//   Position(label, x, y)               X and Y between arrows, a reset (.Reset)
//   Table(title, columns, rows, get, set) a check box per row and column
//   Field(label, text, hint)            a text box (.Draft: Enter saves, Esc cancels)
//   Value(label, text)                  read-only text on the right
//   Action(label, button, fn)           a button (.Arrow, .Confirm)
//   Adder(label, count, fn)             a count and an add button
//   List(rows, onClick)                 rows: icon, two lines, a value or button, a gear
//   Cells(text)                         the window's ItemCells, a text beside each
//   HostGrid(columns, rows)             the Player Page Host area's grid (window.host)
//   Tiles(rows)                         rows of item tiles (any size in cells, a label
//                                       above, left/right/middle clicks, hover; App::Reveal)
//   Chart(slices, size, big, small)     a ring chart with text in it
//   Squares(squares)                    square buttons with a ring around their icon
//   Legend({sprite, word}...)           icons with a word each, in a line
//   Buttons(left, fn, right, fn)        a text button at each end of a row
//   Custom(height, fn)                  the plugin's own widgets in a row
// Each takes .Tip(), .Note() (text under it), .Restart() (" *" and the
// footer's restart line), .OnChange(fn), .When(fn: shown while true), and
// .Remove(fn) (a remove button at the row's end). Field(...).Icon(sprite, fn)
// puts an icon button before a field; App::PickIcon shows a grid of icons to
// pick from. List rows can be a tree (indent, an open/close arrow, a check box
// that can be partly ticked) and carry buttons (.confirm: a second click).
//
// window.overStash: opened while the stash is open, the window goes over it
// (the stash stays open under it, as under the cube) and closes with it.
//
// Other commands (Custom buttons, list row buttons) go to window.command; the
// library's own start with "a.", "typea.", "anav" and "abar". One window
// per plugin. The plugin no longer calls Init, Commands::Listen, Kit::Read,
// Font::Measure, WindowHook or LeftPanel itself.
//
// Opt-in: #include <d2r_gui/app.h> (with icons.h, menus.h, item_cells.h).
#pragma once
#include "character_window.h"
#include "commands.h"
#include "core.h"
#include "font.h"
#include "hud.h"
#include "inventory_button.h"
#include "item_cells.h"
#include "kit.h"
#include "layout.h"
#include "left_panel.h"
#include "menus.h"
#include "panel_pool.h"
#include "scroll_bar.h"
#include "stash_kit.h"
#include "stash_window.h"
#include "text_input.h"
#include "widgets.h"
#include "window_hook.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace D2RGui::App {

// ---- values ----

// A value the window reads with `get` and writes with `set`; `set` returns
// why it refused the value (empty: taken).
template <class T>
struct Bind {
	std::function<T()>                   get;
	std::function<std::string(const T&)> set;
	Bind() = default;
	Bind(T* p) : get([p] { return *p; }), set([p](const T& v) { *p = v; return std::string(); }) {}
	Bind(std::function<T()> g, std::function<std::string(const T&)> s) : get(std::move(g)), set(std::move(s)) {}
	explicit operator bool() const { return static_cast<bool>(get); }
	T           Get() const { return get ? get() : T {}; }
	std::string Set(const T& v) const { return set ? set(v) : std::string(); }
};

// A Bind from a getter and a setter; the setter may return the refusal's
// reason (std::string), whether it took the value (bool), or nothing.
template <class G, class S>
auto Bound(G get, S set) {
	using T = std::decay_t<decltype(get())>;
	Bind<T> b;
	b.get   = std::move(get);
	using R = std::invoke_result_t<S, const T&>;
	if constexpr (std::is_void_v<R>) b.set = [set](const T& v) { set(v); return std::string(); };
	else if constexpr (std::is_same_v<R, bool>) b.set = [set](const T& v) { return set(v) ? std::string() : std::string("The value was refused."); };
	else b.set = [set](const T& v) { return std::string(set(v)); };
	return b;
}

// ---- components ----

// Where a Custom component draws: names start with `name`, the row is at
// x, y (panel units, scrolled already) and `width` wide.
struct Place {
	std::string name;
	int         x = 0, y = 0, width = 0, height = 0;
};

// A button at a row's right end: text (a text button) or a sprite.
struct RowButton {
	std::string text, sprite, command, tooltip;
	bool        confirm = false; // a second click within 10 s sends it
	int         width   = 0;     // a text button's width (default 160); a sprite's square (default 56)
};

struct Row {
	std::string text, tooltip;                    // the name (and its tooltip)
	int         indent   = 0;                     // tree depth (40 units a level)
	int         expander = -1;                    // a tree node's arrow: -1 none, 0 closed, 1 open (sends expandCommand)
	std::string expandCommand;
	int         check    = -1;                    // a check box: -1 none, 0 off, 1 on, 2 partly (sends checkCommand)
	std::string checkCommand, checkTip;
	std::vector<RowButton> buttons;               // right to left, before the gear
	std::string sub;                              // a smaller second line under it
	std::string value, valueTip;                  // text on the right, its tooltip
	std::string icon, iconTip;                    // a sprite on the left, 40 square (Kit::Gem, an Icons::Sprite)
	std::string button, buttonCommand, buttonTip; // a text button on the right (sends buttonCommand)
	std::string gear, gearTip;                    // a gear button at the far right (sends `gear`)
	std::string command;                          // a click on the row sends it (else List's onClick)
};

// An item tile on a Tiles page: `width` x `height` cells, its picture a
// sprite (or `draw`), its label above it. Clicks: left through a button,
// right and middle through the mouse hook (over the tile only), `hover`
// when the mouse comes over it and leaves.
struct Tile {
	std::string                              id;      // the plugin's name for it (App::Reveal)
	int                                      column = -1; // its first column (-1: after the previous tile)
	int                                      width = 1, height = 1;
	std::string                              sprite, label, tooltip;
	bool                                     slot = true; // the stash's empty slot under it
	std::function<std::string(const Place&)> draw; // instead of the sprite: its widgets
	std::function<void()>                    click, rightClick, middleClick;
	std::function<void(bool over)>           hover;
};

struct TileRow {
	std::string       header; // a gold title over the row (none: empty)
	std::vector<Tile> tiles;
};

// A read-only table's cell: text (colour codes allowed: App::Red...), an
// icon before it, its colour (a profile colour such as "$FontColorGoldYellow"; default light gold).
struct GridCell {
	std::string text, icon, color, tooltip;
};
struct GridColumn {
	std::string title;
	int         width = 0; // 0: the room left, shared
};

// A part of a ring chart.
struct Slice {
	int         count = 0;
	Rgb         color { 200, 170, 90 };
	std::string name;
};

// A square tile button with a ring chart around its icon (a stats page's categories).
struct Square {
	std::string           icon, tooltip, command; // command: to window.command (else click)
	std::vector<Slice>    ring;
	std::function<void()> click;
};

struct Item {
	enum class Kind { Section, Text, Check, Choice, Number, Position, Table, Field, Value, Action, Adder, List, Cells, HostGrid, Tiles, Chart, Squares, Legend, Buttons, Grid, Bullet, CardStart, CardEnd, Custom };
	Kind        kind = Kind::Text;
	std::string label, tooltip, note, button, hint, value;
	bool        restart = false, confirm = false, arrow = false, draft = false, clearable = false, filter = false, warn = false, above = false;
	std::string id;                                // its name for App::Focus
	std::vector<std::pair<std::string, std::string>> legend; // Legend: sprite and word
	std::string button2;                           // Buttons: the right one (button: the left one)
	std::function<void()>                          click2;
	std::vector<GridColumn>                        gridColumns2;
	std::vector<std::vector<GridCell>>             gridCells;

	Bind<bool>                         flag;
	Bind<int>                          index;
	Bind<double>                       number;
	Bind<std::string>                  text;
	Bind<int>                          px, py;
	std::vector<DropDown::Entry>       entries;
	double                             min = 0, max = 0, step = 1, shift = 0, ctrl = 0;
	int                                decimals = 0;
	std::function<std::string(double)> format; // Number: the value as shown

	std::vector<std::string>                        columns, rowNames; // Table
	std::function<bool(int row, int column)>        cell;
	std::function<std::string(int, int, bool)>      setCell;
	std::function<std::string(int row, int column)> disabled; // a cell's "Needs ..." tooltip when it can't change (empty: it can)

	std::function<std::vector<Row>()>        rows; // List
	std::function<void(int row)>             pick;
	std::string                              empty;    // List: shown when there is no row
	std::function<std::string(int cell)>     cellText; // Cells
	int*                                     gridColumns = nullptr;
	int*                                     gridRows    = nullptr;
	std::function<std::string(const Place&)> custom;
	int                                      height = 0; // Custom
	std::function<std::vector<TileRow>()>    tileRows;   // Tiles
	int                                      cellSize = 98; // Tiles: a cell's size
	std::vector<Slice>                       slices;     // Chart
	int                                      size = 0;   // Chart: the ring's size; Squares: a square's
	std::string                              big, small; // Chart: the text in the ring, under it
	std::vector<Square>                      squares;    // Squares
	std::string                              icon;       // Field: an icon button at its left (sends iconClick)
	std::function<void()>                    iconClick;
	std::function<void()>                    changed, click, reset, remove;
	std::function<bool()>                    shown;

	Item Tip(std::string tip) const { auto c = *this; c.tooltip = std::move(tip); return c; }
	Item Note(std::string n) const { auto c = *this; c.note = std::move(n); return c; }
	Item Restart(bool on = true) const { auto c = *this; c.restart = on; return c; }
	Item OnChange(std::function<void()> fn) const { auto c = *this; c.changed = std::move(fn); return c; }
	Item When(std::function<bool()> fn) const { auto c = *this; c.shown = std::move(fn); return c; }
	Item Filter(bool on = true) const { auto c = *this; c.filter = on; return c; }
	// Number, Position: a click's step, Shift+click's and Ctrl+click's (default 10 and 100 steps)
	Item Step(double s, double withShift = 0, double withCtrl = 0) const { auto c = *this; c.step = s > 0 ? s : 1, c.shift = withShift, c.ctrl = withCtrl; return c; }
	Item Decimals(int d) const { auto c = *this; c.decimals = std::clamp(d, 0, 6); return c; }
	Item Format(std::function<std::string(double)> fn) const { auto c = *this; c.format = std::move(fn); return c; }
	Item Reset(std::function<void()> fn) const { auto c = *this; c.reset = std::move(fn); return c; }   // Position, Number: a reset button
	Item Remove(std::function<void()> fn) const { auto c = *this; c.remove = std::move(fn); return c; } // a remove button at the row's end
	Item Draft(bool on = true) const { auto c = *this; c.draft = on; return c; }         // Field: Enter saves, Esc cancels (else every key saves, Esc puts the old text back)
	Item Clearable(bool on = true) const { auto c = *this; c.clearable = on; return c; } // Field: a clear button while not empty
	Item Warn(bool on = true) const { auto c = *this; c.warn = on; return c; }   // Text: in red
	Item Above(bool on = true) const { auto c = *this; c.above = on; return c; } // its label on a line of its own above the control (also when it doesn't fit beside it)
	Item Id(std::string name) const { auto c = *this; c.id = std::move(name); return c; } // App::Focus
	Item Hint(std::string h) const { auto c = *this; c.hint = std::move(h); return c; }
	Item Confirm(bool on = true) const { auto c = *this; c.confirm = on; return c; } // Action: a second click within 10 s runs it
	Item Arrow(bool on = true) const { auto c = *this; c.arrow = on; return c; }     // Action: the right arrow beside the label instead of a text button
	Item Empty(std::string text) const { auto c = *this; c.empty = std::move(text); return c; }                           // List
	Item Disabled(std::function<std::string(int, int)> fn) const { auto c = *this; c.disabled = std::move(fn); return c; } // Table
	Item Icon(std::string sprite, std::function<void()> click) const { auto c = *this; c.icon = std::move(sprite), c.iconClick = std::move(click); return c; } // Field
};

inline Item Section(std::string title) { return { .kind = Item::Kind::Section, .label = std::move(title) }; }
inline Item Text(std::string text) { return { .kind = Item::Kind::Text, .label = std::move(text) }; }
inline Item Check(std::string label, Bind<bool> flag) { return { .kind = Item::Kind::Check, .label = std::move(label), .flag = std::move(flag) }; }
inline Item Choice(std::string label, Bind<int> index, const std::vector<std::string>& choices) {
	Item item { .kind = Item::Kind::Choice, .label = std::move(label), .index = std::move(index) };
	for (const auto& choice : choices) item.entries.push_back({ choice });
	return item;
}
// A choice with section headers (`index` counts the headers too).
inline Item Entries(std::string label, Bind<int> index, std::vector<DropDown::Entry> entries) {
	return { .kind = Item::Kind::Choice, .label = std::move(label), .index = std::move(index), .entries = std::move(entries) };
}
inline Item Number(std::string label, Bind<double> value, double min, double max) { return { .kind = Item::Kind::Number, .label = std::move(label), .number = std::move(value), .min = min, .max = max }; }
inline Bind<double> AsDouble(Bind<int> b) {
	return { [g = b.get] { return g ? static_cast<double>(g()) : 0.0; }, [s = b.set](const double& v) { return s ? s(static_cast<int>(std::lround(v))) : std::string(); } };
}
inline Item Number(std::string label, int* value, int min, int max) { return Number(std::move(label), AsDouble(Bind<int>(value)), min, max); }
inline Item Number(std::string label, Bind<int> value, int min, int max) { return Number(std::move(label), AsDouble(std::move(value)), min, max); }
inline Item Number(std::string label, double* value, double min, double max) { return Number(std::move(label), Bind<double>(value), min, max); }
inline Item Position(std::string label, Bind<int> x, Bind<int> y) { return { .kind = Item::Kind::Position, .label = std::move(label), .px = std::move(x), .py = std::move(y) }; }
inline Item Table(std::string title, std::vector<std::string> columns, std::vector<std::string> rows, std::function<bool(int, int)> get, std::function<std::string(int, int, bool)> set) {
	return { .kind = Item::Kind::Table, .label = std::move(title), .columns = std::move(columns), .rowNames = std::move(rows), .cell = std::move(get), .setCell = std::move(set) };
}
inline Item Field(std::string label, Bind<std::string> text, std::string hint = {}) { return { .kind = Item::Kind::Field, .label = std::move(label), .hint = std::move(hint), .text = std::move(text) }; }
inline Item Value(std::string label, std::string text) { return { .kind = Item::Kind::Value, .label = std::move(label), .value = std::move(text) }; }
inline Item Action(std::string label, std::string button, std::function<void()> click) {
	return { .kind = Item::Kind::Action, .label = std::move(label), .button = std::move(button), .click = std::move(click) };
}
inline Item Adder(std::string label, std::string count, std::function<void()> add) { return { .kind = Item::Kind::Adder, .label = std::move(label), .value = std::move(count), .click = std::move(add) }; }
inline Item List(std::function<std::vector<Row>()> rows, std::function<void(int row)> pick = {}) { return { .kind = Item::Kind::List, .rows = std::move(rows), .pick = std::move(pick) }; }
inline Item Cells(std::function<std::string(int cell)> text = {}) { return { .kind = Item::Kind::Cells, .cellText = std::move(text) }; }
inline Item HostGrid(int* columns, int* rows) { return { .kind = Item::Kind::HostGrid, .gridColumns = columns, .gridRows = rows }; }
// A read-only table: a title, column headers, rows of text cells.
inline Item Grid(std::string title, std::vector<GridColumn> columns, std::vector<std::vector<GridCell>> rows) {
	Item item { .kind = Item::Kind::Grid, .label = std::move(title) };
	item.gridColumns2 = std::move(columns), item.gridCells = std::move(rows);
	return item;
}
// A list entry: an icon (or a dot) and text wrapped over as many lines as it takes.
inline Item Bullet(std::string text, std::string icon = {}) { return { .kind = Item::Kind::Bullet, .label = std::move(text), .icon = std::move(icon) }; }
// A framed box with a heading around the components up to CardEnd().
inline Item Card(std::string heading) { return { .kind = Item::Kind::CardStart, .label = std::move(heading) }; }
inline Item CardEnd() { return { .kind = Item::Kind::CardEnd }; }

// Colour codes for text in any component (the game's own: Text, labels, rows, cells).
inline std::string Colored(const std::string& text, char code) { return std::string("\xC3\xBF" "c") + code + text + "\xC3\xBF" "c0"; }
inline std::string Red(const std::string& text) { return Colored(text, '1'); }
inline std::string Green(const std::string& text) { return Colored(text, '2'); }
inline std::string Blue(const std::string& text) { return Colored(text, '3'); }
inline std::string Gold(const std::string& text) { return Colored(text, '4'); }
inline std::string Gray(const std::string& text) { return Colored(text, '5'); }
inline std::string Orange(const std::string& text) { return Colored(text, '8'); }
inline std::string Yellow(const std::string& text) { return Colored(text, '9'); }
inline std::string Purple(const std::string& text) { return Colored(text, ';'); }

// Icons with a word each, in one centred line (a footer's legend).
inline Item Legend(std::vector<std::pair<std::string, std::string>> parts) { return { .kind = Item::Kind::Legend, .legend = std::move(parts) }; }
// Two text buttons in one row, one at the left and one at the right (empty text: none).
inline Item Buttons(std::string left, std::function<void()> leftClick, std::string right, std::function<void()> rightClick) {
	Item item { .kind = Item::Kind::Buttons, .button = std::move(left), .button2 = std::move(right), .click2 = std::move(rightClick) };
	item.click = std::move(leftClick);
	return item;
}
// Rows of item tiles (a grail page); `cell` the cell's size.
inline Item Tiles(std::function<std::vector<TileRow>()> rows, int cell = 98) { return { .kind = Item::Kind::Tiles, .tileRows = std::move(rows), .cellSize = cell }; }
// A ring chart `size` across, `big` in it, `small` under that.
inline Item Chart(std::vector<Slice> parts, int size, std::string big = {}, std::string small = {}) {
	return { .kind = Item::Kind::Chart, .slices = std::move(parts), .size = size, .big = std::move(big), .small = std::move(small) };
}
// Square buttons with ring charts, in centred rows.
inline Item Squares(std::vector<Square> squares, int size = 170) { return { .kind = Item::Kind::Squares, .size = size, .squares = std::move(squares) }; }
inline Item Custom(int height, std::function<std::string(const Place&)> draw) { return { .kind = Item::Kind::Custom, .custom = std::move(draw), .height = height }; }

// ---- structure ----

using Content = std::function<std::vector<Item>()>;

struct Page {
	Tabs::Tab         tab;             // icon and tooltip (the command is the library's)
	std::string       title;           // the window's title on this page (else the menu's); a pushed page's big title
	std::string       subtitle;        // a pushed page: the line under its title
	std::vector<Item> items;
	Content           content;         // instead of items
	Content           header, footer;  // fixed, above and under the scrolled part
	std::string       backTip = "Back"; // a pushed page's back arrow
	std::function<bool()> canLeave;     // a pushed page: false keeps it on Back (say why with App::Notice; a second Back can then return true)
};

struct Menu {
	Tabs::Tab         tab;
	std::string       title;
	std::vector<Page> pages; // none: the menu is a page itself
	std::vector<Item> items;
	Content           content;
	Content           header, footer;
};

enum class Frame { Stash, Character };

struct InventoryLink {
	std::string sprite, tooltip; // the button's sprite (frames 0 normal, 1 pressed, 3 hovered; 52 x 60)
	int         x = 0, y = 0;    // where on the inventory
};

// The window as a Player Page Host area's panel instead of the plugin's own
// panels: one view, sent with `layout` (its "@PANEL@" and "@GRID@" tokens,
// the grid at gridX, gridY: HostGrid's place), applied at the next open.
struct HostView {
	std::function<void(const std::string& layout, int gridX, int gridY)> layout; // Host->setAreaLayout
	std::function<void()>                                                open, close;
	std::function<bool()>                                                isOpen;
};

struct Window {
	std::string                       title;
	Frame                             frame = Frame::Stash;
	std::vector<Menu>                 menus; // or one page:
	std::vector<Item>                 items;
	Content                           content;
	Content                           header, footer; // on every page
	std::string                       console;        // a console command that opens and closes it
	std::function<bool(const D2RL::ConsoleCommandContext*)> onConsole; // called first on that command (any thread; false: the window stays as it is)
	std::string                       keybind;        // a key binding's name in the Controls menu
	std::string                       keyCategory;    // its group there (default: the title)
	std::optional<InventoryLink>      inventory;      // a button on the inventory that opens it
	std::optional<ItemCells::Options> cells;          // the Cells component's item cells
	std::optional<HostView>           host;           // on a Player Page Host area (Stash frame)
	std::string                       restartNote = "* applies after a restart";
	int                               views       = 16; // panels in the pool
	int                               priority    = 10;
	bool                              mouseAlways = false; // `mouse` (and the hook) from the first game on, not from the first open
	bool                              overStash   = false; // opened while the stash is open: over it (priority 130), the stash left open, closing with it as the cube does

	std::function<void()>                     changed; // after any change the player made
	std::function<bool(const std::string&)>   command; // the plugin's own commands (true: show the window again)
	std::function<void(bool open)>            opened;  // the window opened or closed
	std::function<void()>                     loaded;  // the game's tables are loaded (UI thread, once)
	std::function<void(bool inGame)>          inGame;  // a game was joined or left
	std::function<void()>                     tick;    // UI thread, about 4 times a second from `loaded`
	std::function<bool(UINT, WPARAM, LPARAM)> mouse;   // window thread, every message first (true: kept from the game)
	std::function<void(const D2RL::SharedEvents::UiMessageEvent&)> uiMessage; // any thread: every UI message the game sends (Commands::Watch)
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
constexpr int TableTitle = 64, TableHead = 72, TableLine = 84, TableCheck = 79, ArrowSize = 56, NoticeHeight = 50, BackHeight = 120;
constexpr int PanelW = StashWindow::Width, PanelH = StashWindow::Height;
constexpr int HeaderBase = 1000, FooterBase = 2000;

struct Level {
	Page page;
	int  scroll = 0;
};

struct View {
	std::vector<Item> content, header, footer;
	const Page*       pushed = nullptr;
};

struct State {
	Window                     window;
	PanelPool                  pool, over; // over: the views over the open stash
	bool                       onOver {};  // the window is over the stash
	std::atomic<bool>          stashClosing {};
	Menus::Nav                 nav { "anav" };
	std::unique_ptr<ScrollBar> bar;
	std::atomic<bool>          open {};
	bool                       inGame {}, ticking {}, hooked {}, cellsOn {};
	int                        scroll = 0;
	std::vector<Level>         stack;
	int                        drop = -1; // the open drop-down's component
	DropDown::Scroll           list;
	std::string                filter;
	int                        dropY = 0, dropX = 0, dropW = 0;
	int                        cellsX = 0, cellsY = 0;
	bool                       cellsWanted {};
	int                        gridX = 0, gridY = 0;
	InventoryButton            button;
	bool                       hasButton {}, hasCells {};
	std::string                notice;
	bool                       noticeWarn {};
	int                        armed      = -1;
	ULONGLONG                  armedUntil = 0;
	std::string                armedCommand; // a row button armed for its second click
	struct Hit {
		int left, top, right, bottom, item, row, tile;
	};
	std::mutex                 hitLock;
	std::vector<Hit>           hits;      // the tiles on screen (panel units), for right and middle clicks and hovering
	std::atomic<int>           hovered { -1 }; // index in hits
	std::string                reveal;    // a tile to scroll into view at the next view
	std::string                focus;     // a component (Item::id) to scroll into view and type into
	std::string                focusKey;  // then the field to type into, once that view is built
	struct Picker {
		std::vector<std::string> sprites, tips;
		std::function<void(int)> pick;
		std::string              title;
	};
	std::optional<Picker>      picker;
	std::string                draft;    // a Draft field's text while typing
	std::string                original; // a field's text when typing began
};

inline State& Get() {
	static State state;
	return state;
}

inline bool Hosted() { return Get().window.host.has_value(); }
inline PanelPool& Pool() { return Get().onOver ? Get().over : Get().pool; }
constexpr int OverPriority = 130; // over the stash: above the panels other plugins put on it (Stash Tab Dropdown: 110, 120)
inline bool Long(const Item& item) { return item.filter || item.entries.size() > ListAt; }
inline int& Scroll() { return Get().stack.empty() ? Get().scroll : Get().stack.back().scroll; }

inline std::vector<Item> Join(std::vector<Item> a, const Content& b) {
	if (b) {
		auto more = b();
		a.insert(a.end(), std::make_move_iterator(more.begin()), std::make_move_iterator(more.end()));
	}
	return a;
}

// What the window shows now.
inline View Current() {
	auto& s = Get();
	View  v;
	if (!s.stack.empty()) {
		const auto& page = s.stack.back().page;
		v.pushed         = &page;
		v.content        = page.content ? page.content() : page.items;
		v.header         = Join(Join({}, s.window.header), page.header);
		v.footer         = Join(Join({}, page.footer), s.window.footer);
		return v;
	}
	v.header = Join({}, s.window.header);
	if (s.window.menus.empty()) {
		v.content = s.window.content ? s.window.content() : s.window.items;
		v.footer  = Join({}, s.window.footer);
		return v;
	}
	const int m = s.nav.Menu(), p = s.nav.Page();
	if (m < 0 || m >= static_cast<int>(s.window.menus.size())) return v;
	const auto& menu = s.window.menus[static_cast<std::size_t>(m)];
	if (p >= 0 && p < static_cast<int>(menu.pages.size())) {
		const auto& page = menu.pages[static_cast<std::size_t>(p)];
		v.content        = page.content ? page.content() : page.items;
		v.header         = Join(Join(std::move(v.header), menu.header), page.header);
		v.footer         = Join(Join({}, page.footer), menu.footer);
	} else {
		v.content = menu.content ? menu.content() : menu.items;
		v.header  = Join(std::move(v.header), menu.header);
		v.footer  = Join({}, menu.footer);
	}
	v.footer = Join(std::move(v.footer), s.window.footer);
	return v;
}

inline const Item* ItemAt(const View& v, int i) {
	const auto pick = [](const std::vector<Item>& items, int k) -> const Item* { return k >= 0 && k < static_cast<int>(items.size()) ? &items[static_cast<std::size_t>(k)] : nullptr; };
	if (i >= FooterBase) return pick(v.footer, i - FooterBase);
	if (i >= HeaderBase) return pick(v.header, i - HeaderBase);
	return pick(v.content, i);
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

inline int NoteLine() { return std::max(36, Font::LineHeight(false)); }
inline int Lines(const std::string& text, int width) { return std::max(1, Font::WrappedLines(text, width, std::max(20, width / 17), false)); }
inline int NoteHeight(const Item& item, int width) { return item.note.empty() ? 0 : Lines(item.note, width) * NoteLine(); }

// Where a Tiles component's tiles go (y from its top), and its height.
struct Placed {
	int x, y, w, h, row, tile;
};
constexpr int TileHeader = 70, TileLabel = 44, TileGap = 24;

inline std::vector<Placed> PlaceTiles(const std::vector<TileRow>& rows, int cell, int width, int& height) {
	std::vector<Placed> out;
	const int           columns = std::max(1, width / cell), left = (width - columns * cell) / 2;
	int                 y       = 0;
	for (int r = 0; r < static_cast<int>(rows.size()); ++r) {
		const auto& row      = rows[static_cast<std::size_t>(r)];
		bool        labelled = false;
		for (const auto& tile : row.tiles) labelled = labelled || !tile.label.empty();
		if (!row.header.empty()) out.push_back({ left, y, columns * cell, TileHeader, r, -1 }), y += TileHeader;
		const int label = labelled ? TileLabel : 0;
		int       col = 0, line = 0;
		for (int t = 0; t < static_cast<int>(row.tiles.size()); ++t) {
			const auto& tile = row.tiles[static_cast<std::size_t>(t)];
			const int   w = std::clamp(tile.width, 1, columns), h = std::max(1, tile.height);
			if (tile.column >= 0) col = tile.column;
			if (col + w > columns) col = 0, y += label + line * cell + TileGap / 2, line = 0; // the row goes on below
			out.push_back({ left + col * cell, y + label, w * cell, h * cell, r, t });
			col += w, line = std::max(line, h);
		}
		y += label + line * cell + TileGap;
	}
	height = y;
	return out;
}

inline int TilesHeight(const Item& item, int width) {
	if (!item.tileRows) return 0;
	int height = 0;
	(void)PlaceTiles(item.tileRows(), item.cellSize, width, height);
	return height;
}

inline bool AboveOf(const Item& item, int width);
constexpr int AboveLine = 46, GridLine = 56, CardInset = 26;


// ---- text with colour codes ----
// The game's TextBoxWidget doesn't take colour codes, so text holding them
// (App::Red...) is cut into runs, wrapped by the font's widths, each run its
// own text box in its colour.
inline constexpr const char* CodeMark = "\xC3\xBF" "c";
inline bool HasCodes(const std::string& text) { return text.find(CodeMark) != std::string::npos; }

inline std::string ColorJson(const std::string& color) { return !color.empty() && color[0] == '{' ? color : "\"" + color + "\""; }

inline std::string CodeColor(char code, const std::string& base) {
	const auto rgb = [](int r, int g, int b) { return "{\"r\": " + std::to_string(r) + ", \"g\": " + std::to_string(g) + ", \"b\": " + std::to_string(b) + ", \"a\": 255}"; };
	switch (code) {
	case '1': return rgb(255, 90, 90);
	case '2': return rgb(80, 210, 80);
	case '3': return rgb(120, 130, 255);
	case '4': return rgb(200, 180, 120);
	case '5': return rgb(150, 150, 150);
	case '8': return rgb(255, 168, 40);
	case '9': return rgb(255, 255, 110);
	case ';': return rgb(180, 90, 255);
	default: return ColorJson(base);
	}
}

struct Run {
	std::string text, color; // color: a JSON value
};

// The text's words with their colours ("\n" as a word of its own).
inline std::vector<Run> Words(const std::string& text, const std::string& base) {
	std::vector<Run> out;
	std::string      color = ColorJson(base), word;
	const auto       flush = [&] {
		if (!word.empty()) out.push_back({ word, color }), word.clear();
	};
	for (std::size_t i = 0; i < text.size();) {
		if (text.compare(i, 3, CodeMark) == 0 && i + 3 < text.size()) {
			flush();
			color = CodeColor(text[i + 3], base);
			i += 4;
			continue;
		}
		const char c = text[i++];
		if (c == '\n') flush(), out.push_back({ "\n", color });
		else if (c == ' ') word += ' ', flush();
		else word += c;
	}
	flush();
	return out;
}

// A text over lines of `lineHeight` from x, y, `width` wide (wrapped, or
// one line cut with an ellipsis); `lines` gets how many it took.
inline std::string RichText(const std::string& name, int x, int y, int width, int lineHeight, const std::string& text, const std::string& base, bool medium, bool wrap, int* lines = nullptr,
                            const std::string& tooltip = {}) {
	struct Segment {
		std::string text, color;
		int         x, line;
	};
	std::vector<Segment> segments;
	int                  line = 0, at = 0;
	bool                 cut  = false;
	for (const auto& word : Words(text, base)) {
		if (word.text == "\n") {
			if (!wrap) break;
			++line, at = 0;
			continue;
		}
		const int w = Font::TextWidth(word.text, medium);
		if (at > 0 && at + w > width) {
			if (!wrap) {
				cut = true;
				break;
			}
			++line, at = 0;
		}
		if (!segments.empty() && segments.back().line == line && segments.back().color == word.color) segments.back().text += word.text;
		else segments.push_back({ word.text, word.color, at, line });
		at += w;
	}
	if (lines) *lines = line + 1;
	std::string out;
	for (std::size_t k = 0; k < segments.size(); ++k) {
		auto& seg = segments[k];
		if (cut && k + 1 == segments.size()) seg.text += "...";
		const int  room  = std::max(10, width - seg.x + 20);
		const auto style = std::string(R"({"fontColor": )") + seg.color + R"(, "pointSize": ")" + (medium ? "$MediumFontSize" : "$SmallFontSize")
		                 + R"(", "dropShadow": "$DefaultDropShadow", "alignment": {"h": "left", "v": "center"}})";
		out += D2RGui::Text(name + "_" + std::to_string(k), x + seg.x, y + seg.line * lineHeight, room, lineHeight, seg.text, style.c_str(), k == 0 ? tooltip : std::string());
	}
	return out;
}

inline int RichLines(const std::string& text, int width, bool medium) {
	int lines = 1;
	(void)RichText("", 0, 0, width, 1, text, "$FontColorWhite", medium, true, &lines);
	return lines;
}

// `text` cut with an ellipsis to fit `width` (small or medium font).
inline std::string FitWidth(const std::string& text, int width, bool medium) {
	if (Font::TextWidth(text, medium) <= width) return text;
	std::string cut = text;
	while (!cut.empty() && Font::TextWidth(cut + "...", medium) > width) {
		cut.pop_back();
		while (!cut.empty() && (static_cast<unsigned char>(cut.back()) & 0xC0) == 0x80) cut.pop_back(); // whole UTF-8 characters
		if (!cut.empty() && static_cast<unsigned char>(cut.back()) >= 0xC0) cut.pop_back();
	}
	return cut + "...";
}

// A table's column widths in `width`: fixed ones (scaled down when too wide), the rest shared.
inline std::vector<int> GridWidths(const std::vector<GridColumn>& columns, int width) {
	int fixedW = 0, flexible = 0;
	for (const auto& column : columns) fixedW += column.width, flexible += column.width ? 0 : 1;
	const int        least = 120 * flexible;
	const double     scale = fixedW > 0 && fixedW > width - least ? std::max(0.2, static_cast<double>(width - least) / fixedW) : 1.0;
	int              used  = 0;
	std::vector<int> out;
	for (const auto& column : columns) out.push_back(column.width ? static_cast<int>(column.width * scale) : 0), used += out.back();
	for (std::size_t c = 0; c < columns.size(); ++c)
		if (!columns[c].width) out[c] = std::max(60, (width - used) / std::max(1, flexible));
	return out;
}

// A list row's height: its sub-text wraps.
inline int RowHeightOf(const Row& row, int width) {
	if (row.sub.empty()) return 80;
	const int lines = HasCodes(row.sub) ? RichLines(row.sub, std::max(200, width - 380), false) : std::max(1, Font::WrappedLines(row.sub, std::max(200, width - 380), std::max(20, (width - 380) / 14), false));
	return std::max(80, 50 + lines * 32 + 8);
}

inline int HeightOf(const Item& item, int width) {
	if (AboveOf(item, width)) return AboveLine + HeightOf([&] { auto c = item; c.label.clear(), c.restart = false; return c; }(), width);
	switch (item.kind) {
	case Item::Kind::Legend: return 60;
	case Item::Kind::Grid: return (item.label.empty() ? 0 : 64) + 56 + static_cast<int>(item.gridCells.size()) * GridLine + 10;
	case Item::Kind::Bullet: return (HasCodes(item.label) ? RichLines(item.label, width - 60, false) : Lines(item.label, width - 60)) * NoteLine() + 14;
	case Item::Kind::CardStart: return item.label.empty() ? 30 : 84;
	case Item::Kind::CardEnd: return 34;
	case Item::Kind::Section: return 100;
	case Item::Kind::Text: return (HasCodes(item.label) ? RichLines(item.label, width, false) : Lines(item.label, width)) * NoteLine() + 16;
	case Item::Kind::Table: return (item.label.empty() ? 0 : TableTitle) + TableHead + static_cast<int>(item.rowNames.size()) * TableLine + NoteHeight(item, width) + 8;
	case Item::Kind::List: {
		const auto rows = item.rows ? item.rows() : std::vector<Row> {};
		int        h    = 0;
		for (const auto& row : rows) h += RowHeightOf(row, width);
		return !rows.empty() ? h : (item.empty.empty() ? 0 : 200);
	}
	case Item::Kind::Cells: return Get().hasCells ? (Get().window.cells->vertical ? ItemCells::Count() * ItemCells::Cell : ItemCells::Cell) + 20 : 0;
	case Item::Kind::HostGrid: return 0; // drawn outside the scrolled part
	case Item::Kind::Tiles: return TilesHeight(item, width);
	case Item::Kind::Chart: return item.size + (item.small.empty() ? 20 : 20);
	case Item::Kind::Squares: {
		const int count = static_cast<int>(item.squares.size()), per = std::max(1, (width + 36) / (item.size + 36));
		return count ? ((count + per - 1) / per) * (item.size + 36) : 0;
	}
	case Item::Kind::Custom: return item.height;
	default: return RowHeight + NoteHeight(item, width);
	}
}

// The room a row's label has beside its control.
inline int LabelRoom(const Item& item, int width) {
	const int w = item.remove ? width - 70 : width;
	switch (item.kind) {
	case Item::Kind::Check: return w - 80;
	case Item::Kind::Choice: return w - std::min(520, w / 2 + 60) - 20;
	case Item::Kind::Number: return w - 420 - (item.reset ? 70 : 0);
	case Item::Kind::Field: return std::max(0, 300 - 20 - (item.icon.empty() ? 0 : 70));
	case Item::Kind::Value: return w / 2;
	case Item::Kind::Action: return w - (item.arrow ? 50 : 420) - 20;
	case Item::Kind::Adder: return w - 400;
	case Item::Kind::Position: return w - 2 * (32 + 56 + 96 + 56 + 14) - (item.reset ? 56 : 0) - 20;
	default: return -1;
	}
}

inline bool AboveOf(const Item& item, int width) {
	if (item.label.empty()) return false;
	const int room = LabelRoom(item, width);
	if (room < 0) return false;
	return item.above || Font::TextWidth(item.label, false) > room;
}

inline bool Visible(const Item& item) { return !item.shown || item.shown(); }

inline std::string Decimal(double value, int decimals) {
	char text[64];
	std::snprintf(text, sizeof(text), "%.*f", decimals, value);
	return text;
}

inline double ShiftStep(const Item& item) { return item.shift > 0 ? item.shift : item.step * 10; }
inline double CtrlStep(const Item& item) { return item.ctrl > 0 ? item.ctrl : item.step * 100; }

inline std::string StepTip(const Item& item) {
	std::string tip = item.tooltip;
	if (!tip.empty()) tip += "\n";
	return tip + "Shift+click: steps of " + Decimal(ShiftStep(item), item.decimals) + ", Ctrl+click: steps of " + Decimal(CtrlStep(item), item.decimals);
}

// The step of a click now (Shift or Ctrl held).
inline double StepNow(const Item& item) {
	if (GetAsyncKeyState(VK_CONTROL) & 0x8000) return CtrlStep(item);
	if (GetAsyncKeyState(VK_SHIFT) & 0x8000) return ShiftStep(item);
	return item.step;
}

inline std::string Shown(const Item& item) {
	const int k = item.index.Get();
	return k >= 0 && k < static_cast<int>(item.entries.size()) ? item.entries[static_cast<std::size_t>(k)].text : std::string();
}

inline constexpr const char* SmallWrap =
    R"({"fontColor": "$FontColorLightGold", "pointSize": "$SmallFontSize", "dropShadow": "$DefaultDropShadow", "alignment": {"h": "left", "v": "top"}, "options": {"lineWrap": true}})";
inline constexpr const char* WarnLeft =
    R"({"fontColor": "$FontColorLightRed", "pointSize": "$SmallFontSize", "dropShadow": "$DefaultDropShadow", "alignment": {"h": "left", "v": "top"}, "options": {"lineWrap": true, "newlineHandling": "standard"}})";

inline std::string LabelText(const Item& item) { return item.restart && !item.label.empty() ? item.label + " *" : item.label; }

// A row's label, small when it would not fit.
inline std::string Label(const std::string& name, const Item& item, int x, int y, int width) {
	const auto label = LabelText(item);
	if (label.empty()) return {};
	return D2RGui::Text(name, x, y, width, RowHeight - 6, label, Font::TextWidth(label) > width ? Style::SmallLabel : Style::Label, item.tooltip);
}

inline std::string NoteText(const std::string& name, const Item& item, int x, int y, int width) {
	if (item.note.empty()) return {};
	return D2RGui::Text(name, x, y, width, NoteHeight(item, width), item.note, Style::Comment);
}

// One component at `y` in its region (the content's clip, or the panel for a
// fixed header or footer): `origin` is the region's panel y, `room` its height.
inline std::string Draw(const Item& item, int i, int y, int origin, int room, const Area& a) {
	auto&             s = Get();
	const std::string n = "I" + std::to_string(i), id = std::to_string(i);
	const int         width = a.right - a.left;
	const int         end   = item.remove ? a.right - 70 : a.right; // a remove button takes the row's end
	std::string       out;
	if (AboveOf(item, width)) { // the label on its own line, the control under it
		auto rest = item;
		rest.label.clear(), rest.restart = false;
		return D2RGui::Text(n + "A", a.left, y, width, AboveLine, LabelText(item), Style::SmallLeft, item.tooltip) + Draw(rest, i, y + AboveLine, origin, room, a);
	}
	if (item.remove) out += RemoveButton(n + "X", a.right - 52, y + 9, 52, "a.x" + id, "Remove");
	switch (item.kind) {
	case Item::Kind::Section: out += SectionTitle(n, a.left, y, width, item.label, item.tooltip); break;
	case Item::Kind::Text:
		if (HasCodes(item.label)) out += RichText(n, a.left, y, width, NoteLine(), item.label, item.warn ? "$FontColorLightRed" : "$FontColorWhite", false, true, nullptr, item.tooltip);
		else out += D2RGui::Text(n, a.left, y, width, HeightOf(item, width) - 8, item.label, item.warn ? WarnLeft : Style::Comment, item.tooltip);
		break;
	case Item::Kind::Legend: {
		int total = 0;
		for (const auto& [sprite, word] : item.legend) total += 50 + std::min(260, Font::TextWidth(word, false) + 40);
		int x = a.left + std::max(0, (width - total) / 2), k = 0;
		for (const auto& [sprite, word] : item.legend) {
			const int w = std::min(260, Font::TextWidth(word, false) + 40);
			out += Image(n + "I" + std::to_string(k), x, y + 10, sprite);
			out += D2RGui::Text(n + "W" + std::to_string(k++), x + 50, y, w, 60, word, Style::SmallLeft);
			x += 50 + w;
		}
		break;
	}
	case Item::Kind::Grid: {
		int at = y;
		if (!item.label.empty()) out += D2RGui::Text(n + "T", a.left, at, width, 60, item.label, Style::Label, item.tooltip), at += 64;
		const auto widths = GridWidths(item.gridColumns2, width);
		int        x      = a.left;
		for (int c = 0; c < static_cast<int>(widths.size()); ++c) {
			const auto& title = item.gridColumns2[static_cast<std::size_t>(c)].title;
			const int   w     = widths[static_cast<std::size_t>(c)];
			const auto  shown = FitWidth(title, w - 20, true);
			out += D2RGui::Text(n + "H" + std::to_string(c), x + 10, at, w - 20, 52, shown, Style::Axis, shown != title ? title : std::string());
			x += w;
		}
		out += Image(n + "HR", a.left, at + 50, StashKit::Divider(width));
		at += 56;
		for (int r = 0; r < static_cast<int>(item.gridCells.size()); ++r, at += GridLine) {
			const auto rn = n + "R" + std::to_string(r);
			out += Image(rn + "B", a.left, at, Kit::Stripe(width, GridLine, r % 2 == 0));
			x = a.left;
			const auto& cells = item.gridCells[static_cast<std::size_t>(r)];
			for (int c = 0; c < static_cast<int>(widths.size()) && c < static_cast<int>(cells.size()); ++c) {
				const auto& cell = cells[static_cast<std::size_t>(c)];
				const int   w    = widths[static_cast<std::size_t>(c)];
				int         tx   = x + 14;
				if (!cell.icon.empty() && w > 80) out += Image(rn + "I" + std::to_string(c), tx, at + 8, cell.icon), tx += 48;
				const int         room  = x + w - tx - 12;
				const std::string base  = cell.color.empty() ? "$FontColorLightGold" : cell.color;
				const std::string cname = rn + "C" + std::to_string(c);
				if (HasCodes(cell.text)) out += RichText(cname, tx, at, room, GridLine, cell.text, base, false, false, nullptr, cell.tooltip);
				else {
					const auto shown = FitWidth(cell.text, room, false);
					const auto style = std::string(R"({"fontColor": )") + ColorJson(base) + R"(, "pointSize": "$SmallFontSize", "dropShadow": "$DefaultDropShadow", "alignment": {"h": "left", "v": "center"}})";
					out += D2RGui::Text(cname, tx, at, room + 10, GridLine, shown, style.c_str(), !cell.tooltip.empty() ? cell.tooltip : (shown != cell.text ? cell.text : std::string()));
				}
				x += w;
			}
		}
		break;
	}
	case Item::Kind::Bullet:
		if (!item.icon.empty()) out += Image(n + "I", a.left + 6, y + 4, item.icon);
		else out += D2RGui::Text(n + "D", a.left, y, 40, NoteLine(), "*", Style::Axis);
		if (HasCodes(item.label)) out += RichText(n, a.left + 56, y, width - 60, NoteLine(), item.label, "$FontColorWhite", false, true, nullptr, item.tooltip);
		else out += D2RGui::Text(n, a.left + 56, y, width - 60, HeightOf(item, width) - 8, item.label, Style::Comment, item.tooltip);
		break;
	case Item::Kind::CardStart:
		if (!item.label.empty()) out += D2RGui::Text(n, a.left + 30, y + 14, width - 60, 60, item.label, Style::Section, item.tooltip);
		break;
	case Item::Kind::CardEnd: break; // Build draws the frame
	case Item::Kind::Buttons:
		if (!item.button.empty()) out += TextButton(n + "L", a.left, y + 7, 300, BoxHeight, item.button, "a.b" + id, item.tooltip);
		if (!item.button2.empty()) out += TextButton(n + "R", a.right - 300, y + 7, 300, BoxHeight, item.button2, "a.y" + id, item.tooltip);
		break;
	case Item::Kind::Check:
		out += Label(n + "L", item, a.left, y, end - a.left - 80);
		out += D2RGui::Check(n, end - 60, y + 7, item.flag.Get(), "a.c" + id, item.tooltip);
		out += NoteText(n + "N", item, a.left, y + RowHeight - 10, width);
		break;
	case Item::Kind::Choice: {
		const int w = item.label.empty() ? end - a.left : std::min(520, (end - a.left) / 2 + 60);
		out += Label(n + "L", item, a.left, y, end - a.left - w - 20);
		out += DropDown::Box(n, end - w, y + 7, w, BoxHeight, Shown(item), "a.d" + id, item.tooltip);
		if (s.drop == i) s.dropX = end - w, s.dropY = origin + y + 7, s.dropW = w;
		out += NoteText(n + "N", item, a.left, y + RowHeight - 10, width);
		break;
	}
	case Item::Kind::Number: {
		const double value = item.number.Get();
		const auto   shown = item.format ? item.format(value) : Decimal(value, item.decimals);
		const int    right = item.reset ? end - ArrowSize - 14 : end;
		out += Label(n + "L", item, a.left, y, right - a.left - 420);
		out += Stepper(n, right - 400, y, 400, RowHeight, shown, "a.n" + id + "-", "a.n" + id + "+", StepTip(item));
		if (item.reset) out += ResetButton(n + "Z", end - ArrowSize, y + 7, ArrowSize, "a.z" + id, "Back to the default");
		out += NoteText(n + "N", item, a.left, y + RowHeight - 10, width);
		break;
	}
	case Item::Kind::Position: {
		const std::string tip   = StepTip(item);
		const int         space = 2 * (32 + ArrowSize + 96 + ArrowSize + 14) + (item.reset ? ArrowSize : 0);
		int               at    = end - space;
		const int         top   = y + (RowHeight - ArrowSize) / 2 - 3;
		out += Label(n + "L", item, a.left, y, at - a.left - 20);
		for (const char axis : { 'X', 'Y' }) {
			const std::string ax(1, axis), cmd = std::string("a.") + (axis == 'X' ? "h" : "v") + id;
			const auto        value = Decimal(axis == 'X' ? item.px.Get() : item.py.Get(), 0);
			out += D2RGui::Text(n + ax + "A", at, y, 30, RowHeight - 6, ax, Style::Axis);
			at += 32;
			out += ArrowButton(n + ax + "D", at, top, false, ArrowSize, cmd + "-", tip);
			at += ArrowSize;
			out += D2RGui::Text(n + ax + "V", at, y, 96, RowHeight - 6, value, value.size() > 5 ? Style::SmallValue : Style::Value, tip);
			at += 96;
			out += ArrowButton(n + ax + "U", at, top, true, ArrowSize, cmd + "+", tip);
			at += ArrowSize + 14;
		}
		if (item.reset) out += ResetButton(n + "Z", end - ArrowSize, top, ArrowSize, "a.z" + id, "Back to the default");
		out += NoteText(n + "N", item, a.left, y + RowHeight - 10, width);
		break;
	}
	case Item::Kind::Table: {
		int at = y;
		if (!item.label.empty()) out += D2RGui::Text(n + "T", a.left, at, width, TableTitle - 4, LabelText(item), Style::Label, item.tooltip), at += TableTitle;
		if (!item.note.empty()) out += NoteText(n + "N", item, a.left, at - 6, width), at += NoteHeight(item, width);
		const int columns = std::max<int>(1, static_cast<int>(item.columns.size()));
		const int cw = std::min(150, (width - 260) / columns), left = a.right - cw * columns;
		for (int c = 0; c < static_cast<int>(item.columns.size()); ++c)
			out += D2RGui::Text(n + "H" + std::to_string(c), left + c * cw, at, cw, TableHead - 6, item.columns[static_cast<std::size_t>(c)], Style::SmallValue);
		at += TableHead;
		for (int r = 0; r < static_cast<int>(item.rowNames.size()); ++r, at += TableLine) {
			const auto rn = n + "R" + std::to_string(r);
			out += D2RGui::Text(rn, a.left, at, left - a.left - 10, TableLine, item.rowNames[static_cast<std::size_t>(r)], Style::SmallLabel);
			for (int c = 0; c < static_cast<int>(item.columns.size()); ++c) {
				const bool        on  = item.cell && item.cell(r, c);
				const std::string cn  = rn + "C" + std::to_string(c), cmd = "a.t" + id + ":" + std::to_string(r) + ":" + std::to_string(c);
				const int         x   = left + c * cw + (cw - TableCheck) / 2, cy = at + (TableLine - TableCheck) / 2;
				const std::string why = item.disabled ? item.disabled(r, c) : std::string();
				if (!why.empty()) {
					const int frame = on ? 6 : 2; // greyed, its value kept
					out += Button(cn, x, cy, Kit::Native::CheckBox, { frame, frame, frame }, {}, why);
				} else out += D2RGui::Check(cn, x, cy, on, cmd);
			}
		}
		break;
	}
	case Item::Kind::Field: {
		const std::string key    = "a.f" + id;
		const bool        typing = TextInput::Key() == key;
		const int         start  = item.icon.empty() ? a.left : a.left + 70; // an icon button first
		const int         w      = item.label.empty() ? end - start : std::min(600, end - start - 300);
		if (!item.icon.empty()) {
			out += Button(n + "Ic", a.left, y + 7, StashKit::SlotButton(56, 56), { 0, 2, 1 }, "a.i" + id, "Change the icon", 56, 56);
			out += Image(n + "Im", a.left + 8, y + 15, item.icon);
		}
		const int         right  = item.clearable ? end - 70 : end;
		const std::string text   = typing && item.draft ? s.draft : item.text.Get();
		out += Label(n + "L", item, start, y, end - w - start - 20);
		out += TypedBox(n, key, end - w, y + 7, right - (end - w), BoxHeight, text, item.hint.empty() ? "(empty)" : item.hint,
		                item.tooltip.empty() ? (item.draft ? "Click to type; Enter saves, Esc cancels" : "Click to type") : item.tooltip);
		if (item.clearable && !text.empty()) out += RemoveButton(n + "C", end - 56, y + 9, 52, "a.e" + id, "Clear");
		out += NoteText(n + "N", item, a.left, y + RowHeight - 10, width);
		break;
	}
	case Item::Kind::Value:
		out += Label(n + "L", item, a.left, y, (end - a.left) / 2);
		out += D2RGui::Text(n, a.left + (end - a.left) / 2, y, (end - a.left) / 2, RowHeight - 6, item.value, Style::SmallRight, item.tooltip);
		out += NoteText(n + "N", item, a.left, y + RowHeight - 10, width);
		break;
	case Item::Kind::Action: {
		const bool armed = s.armed == i && GetTickCount64() < s.armedUntil;
		const int  w     = item.arrow ? 50 : (item.label.empty() ? end - a.left : 420);
		out += Label(n + "L", item, a.left, y, end - a.left - w - 20 - (armed ? 400 : 0));
		if (armed) out += D2RGui::Text(n + "W", end - w - 420, y, 400, RowHeight - 6, "Click again to confirm", Style::Warn);
		const std::string tip = !item.tooltip.empty() ? item.tooltip : (item.confirm ? "Click twice to run" : std::string());
		if (item.arrow) out += Button(n, end - 50, y + 24, Kit::Native::RightArrow, { 0, 1, 3 }, "a.b" + id, tip);
		else out += TextButton(n, end - w, y + 7, w, BoxHeight, item.button, "a.b" + id, tip);
		out += NoteText(n + "N", item, a.left, y + RowHeight - 10, width);
		break;
	}
	case Item::Kind::Adder:
		out += Label(n + "L", item, a.left, y, end - a.left - 400);
		out += D2RGui::Text(n + "V", end - 380, y, 300, RowHeight - 6, item.value, Style::SmallRight, item.tooltip);
		out += Button(n, end - 64, y + 7, Kit::Native::AddButton, { 0, 1, 3 }, "a.b" + id, item.tooltip.empty() ? "Add" : item.tooltip);
		out += NoteText(n + "N", item, a.left, y + RowHeight - 10, width);
		break;
	case Item::Kind::List: {
		const auto rows = item.rows ? item.rows() : std::vector<Row> {};
		if (rows.empty() && !item.empty.empty()) out += D2RGui::Text(n + "E", a.left, y, width, 200, item.empty, Style::Note);
		int next = y;
		for (int r = 0; r < static_cast<int>(rows.size()); ++r) {
			const auto& row     = rows[static_cast<std::size_t>(r)];
			const int   ry      = next, ListRow = RowHeightOf(row, width);
			next += ListRow;
			if (ry + ListRow < 0 || ry > room) continue; // outside the room: not drawn
			const auto  rn      = n + "R" + std::to_string(r);
			const bool  clicked = !row.command.empty() || item.pick;
			int         right   = a.right; // the row's right end, taken by its buttons from the right
			int         textX   = a.left + 30 + row.indent * 40;
			out += Image(rn + "B", a.left, ry, Kit::Stripe(width, ListRow, r % 2 == 0));
			if (clicked) out += HitButton(rn + "H", a.left, ry + 3, width, ListRow - 6, !row.command.empty() ? row.command : "a.r" + id + ":" + std::to_string(r), row.tooltip);
			if (row.expander >= 0) out += ArrowButton(rn + "E", textX - 10, ry + (ListRow - 48) / 2, row.expander == 0, 48, row.expandCommand, row.expander ? "Close" : "Open"), textX += 50;
			if (row.check >= 0) {
				const Frames frames = row.check == 1 ? Frames { 4, 5, 7 } : row.check == 2 ? Frames { 6, 5, 7 } : Frames { 0, 1, 3 }; // partly: the greyed tick
				out += Button(rn + "C", textX - 6, ry + (ListRow - 79) / 2, Kit::Native::CheckBox, frames, row.checkCommand, row.checkTip);
				textX += 80;
			}
			if (!row.icon.empty()) out += Icon(rn + "I", textX, ry + (ListRow - 40) / 2, 40, row.icon, row.iconTip), textX += 58;
			if (!row.gear.empty()) out += GearButton(rn + "G", right - 64 - 10, ry + (ListRow - 64) / 2, 64, row.gear, row.gearTip), right -= 74;
			for (int b = 0; b < static_cast<int>(row.buttons.size()); ++b) {
				const auto&       button = row.buttons[static_cast<std::size_t>(b)];
				const std::string cmd    = "a.w" + id + ":" + std::to_string(r) + ":" + std::to_string(b);
				const bool        armed  = button.confirm && s.armedCommand == cmd && GetTickCount64() < s.armedUntil;
				if (!button.sprite.empty()) {
					const int size = button.width > 0 ? button.width : 56;
					out += Button(rn + "W" + std::to_string(b), right - size - 6, ry + (ListRow - size) / 2, button.sprite, { 0, 1, 3 }, cmd, armed ? "Click again to confirm" : button.tooltip, size, size);
					right -= size + 8;
				} else {
					const int bw = button.width > 0 ? button.width : 160;
					out += TextButton(rn + "W" + std::to_string(b), right - bw - 6, ry + (ListRow - BoxHeight) / 2, bw, BoxHeight, armed ? "Sure?" : button.text, cmd,
					                  armed ? "Click again to confirm" : button.tooltip, armed ? "$FontColorLightRed" : "$FontColorLightGold");
					right -= bw + 8;
				}
			}
			const bool side  = !row.button.empty() || !row.value.empty();
			const int  textW = right - textX - (side ? 250 : 20);
			const auto tip   = clicked ? std::string() : row.tooltip;
			if (HasCodes(row.text)) out += RichText(rn + "T", textX, row.sub.empty() ? ry : ry + 4, textW, row.sub.empty() ? ListRow : 40, row.text, "$FontColorLightGold", true, false, nullptr, tip);
			else if (row.sub.empty()) out += D2RGui::Text(rn + "T", textX, ry, textW, ListRow, row.text, Style::LabelOneLine, tip);
			else out += D2RGui::Text(rn + "T", textX, ry + 4, textW, 40, row.text, Style::LabelOneLine, tip);
			if (HasCodes(row.sub)) out += RichText(rn + "S", textX, ry + 44, textW, 32, row.sub, "$FontColorLightGold", false, true);
			else if (!row.sub.empty()) out += D2RGui::Text(rn + "S", textX, ry + 44, textW, ListRow - 50, row.sub, SmallWrap);
			if (!row.button.empty()) out += TextButton(rn + "U", right - 250, ry + (ListRow - BoxHeight) / 2, 240, BoxHeight, row.button, row.buttonCommand, row.buttonTip);
			else if (!row.value.empty()) out += D2RGui::Text(rn + "V", right - 250, ry, 230, ListRow, row.value, Style::SmallRight, row.valueTip);
		}
		break;
	}
	case Item::Kind::Cells: {
		if (!s.hasCells) break;
		const bool vertical = s.window.cells->vertical;
		const int  count = ItemCells::Count(), cell = ItemCells::Cell;
		const int  height = vertical ? count * cell : cell;
		// an overlay: shown only while all of it is in the room
		if (y >= 0 && y + height <= room) s.cellsWanted = true, s.cellsX = StashWindow::X + a.left, s.cellsY = StashWindow::Y + origin + y;
		if (vertical && item.cellText)
			for (int c = 0; c < count; ++c) out += D2RGui::Text(n + "C" + std::to_string(c), a.left + cell + 30, y + c * cell, width - cell - 30, cell, item.cellText(c), Style::Label);
		break;
	}
	case Item::Kind::HostGrid: break; // Build draws it
	case Item::Kind::Tiles: {
		if (!item.tileRows) break;
		const auto rows   = item.tileRows();
		int        height = 0, k = 0;
		for (const auto& p : PlaceTiles(rows, item.cellSize, width, height)) {
			const int ty = y + p.y;
			if (ty + p.h < 0 || ty - TileLabel > room) continue; // outside the room: not drawn
			const auto tn = n + "T" + std::to_string(k++);
			const int  tx = a.left + p.x;
			if (p.tile < 0) {
				out += D2RGui::Text(tn, tx, ty, p.w, TileHeader - 14, rows[static_cast<std::size_t>(p.row)].header, Style::Axis);
				out += Image(tn + "R", tx, ty + TileHeader - 14, StashKit::Divider(p.w));
				continue;
			}
			const auto& tile = rows[static_cast<std::size_t>(p.row)].tiles[static_cast<std::size_t>(p.tile)];
			if (tile.slot) out += Image(tn + "S", tx, ty, StashKit::Slot(p.w, p.h));
			if (tile.draw) out += tile.draw({ .name = tn + "D", .x = tx, .y = ty, .width = p.w, .height = p.h });
			else if (!tile.sprite.empty()) out += Image(tn + "A", tx, ty, tile.sprite);
			if (!tile.label.empty()) out += D2RGui::Text(tn + "N", tx - 30, ty - TileLabel, p.w + 60, TileLabel, tile.label, Style::SmallValue);
			const std::string cmd = tile.click ? "a.k" + id + ":" + std::to_string(p.row) + ":" + std::to_string(p.tile) : std::string();
			out += QuietButton(tn + "H", tx, ty, p.w, p.h, cmd, tile.tooltip);
			if (tile.rightClick || tile.middleClick || tile.hover) {
				const int top = std::max(origin + ty, origin), bottom = std::min(origin + ty + p.h, origin + room);
				if (bottom > top) {
					std::lock_guard guard(s.hitLock);
					s.hits.push_back({ tx, top, tx + p.w, bottom, i, p.row, p.tile });
				}
			}
		}
		break;
	}
	case Item::Kind::Chart: {
		std::vector<std::pair<int, Rgb>> parts;
		std::string                      tip;
		for (const auto& slice : item.slices) {
			parts.push_back({ slice.count, slice.color });
			if (!slice.name.empty()) tip += (tip.empty() ? "" : "\n") + slice.name + ": " + std::to_string(slice.count);
		}
		const int x = a.left + (width - item.size) / 2;
		out += Image(n, x, y, StashKit::Chart(parts, item.size, 58, false, Rgb { 50, 50, 50 }));
		if (!item.big.empty()) out += D2RGui::Text(n + "B", x, y + item.size / 2 - 52, item.size, 60, item.big, Style::Name);
		if (!item.small.empty()) out += D2RGui::Text(n + "S", x, y + item.size / 2 + 8, item.size, 44, item.small, Style::Note);
		if (!tip.empty()) out += D2RGui::Text(n + "T", x, y, item.size, item.size, "", Style::Note, tip);
		break;
	}
	case Item::Kind::Squares: {
		const int size = item.size, gap = 36, count = static_cast<int>(item.squares.size()), per = std::max(1, (width + gap) / (size + gap));
		for (int k = 0; k < count; ++k) {
			const auto& square = item.squares[static_cast<std::size_t>(k)];
			const int   row = k / per, col = k % per, inRow = std::min(per, count - row * per);
			const int   sx = a.left + (width - (inRow * size + (inRow - 1) * gap)) / 2 + col * (size + gap), sy = y + row * (size + gap);
			const auto  qn = n + "Q" + std::to_string(k);
			out += Button(qn, sx, sy, StashKit::SlotButton(size, size), { 0, 2, 1 }, !square.command.empty() ? square.command : "a.q" + id + ":" + std::to_string(k), square.tooltip, size, size);
			if (!square.ring.empty()) {
				std::vector<std::pair<int, Rgb>> parts;
				for (const auto& slice : square.ring) parts.push_back({ slice.count, slice.color });
				out += Image(qn + "R", sx + 9, sy + 9, StashKit::Chart(parts, size - 18, 62, true, Rgb { 50, 50, 50 }));
			}
			if (!square.icon.empty()) out += Image(qn + "I", sx + (size - 70) / 2, sy + (size - 70) / 2, square.icon);
		}
		break;
	}
	case Item::Kind::Custom:
		if (item.custom) out += item.custom({ .name = n, .x = a.left, .y = y, .width = width, .height = item.height });
		break;
	}
	return out;
}

// The host's grid: slots centred in the stash's inner area, "@GRID@" over them.
inline std::string GridLayout(const Item& item) {
	auto&     s       = Get();
	const int columns = item.gridColumns ? std::max(1, *item.gridColumns) : 1, rows = item.gridRows ? std::max(1, *item.gridRows) : 1, cell = ItemCells::Cell;
	s.gridX           = StashWindow::InnerX + (StashWindow::InnerWidth - columns * cell) / 2;
	s.gridY           = StashWindow::InnerY + (StashWindow::InnerHeight - rows * cell) / 2;
	static const bool registered = RegisterSprite("a-cell", { StashWindow::Cell() });
	std::string       out;
	if (registered)
		for (int y = 0; y < rows; ++y)
			for (int x = 0; x < columns; ++x) out += Image("GridCell" + std::to_string(y * 100 + x), s.gridX + x * cell, s.gridY + y * cell, SpriteName("a-cell"));
	return out + ", @GRID@";
}

// A fixed region's components from y down; returns where it ended.
inline int DrawFixed(const std::vector<Item>& items, int base, int y, const Area& a, std::string& out) {
	for (int k = 0; k < static_cast<int>(items.size()); ++k) {
		const auto& item = items[static_cast<std::size_t>(k)];
		if (!Visible(item)) continue;
		out += Draw(item, base + k, y, 0, PanelH, a);
		y += HeightOf(item, a.right - a.left);
	}
	return y;
}

inline int FixedHeight(const std::vector<Item>& items, int width) {
	int h = 0;
	for (const auto& item : items)
		if (Visible(item)) h += HeightOf(item, width);
	return h;
}

inline bool AnyRestart(const View& v) {
	for (const auto* list : { &v.content, &v.header, &v.footer })
		for (const auto& item : *list)
			if (item.restart && Visible(item)) return true;
	return false;
}

inline std::string Build(const std::string& panelName, const std::string& probe) {
	auto&       s     = Get();
	Area        a     = AreaOf(Hosted() ? Frame::Stash : s.window.frame);
	const int   width = a.right - a.left;
	const View  v     = Current();
	std::string fixed;
	if (v.pushed) {
		// a pushed page: a back arrow, its title and the line under it
		fixed += Button("Back", a.left, a.top + 22, Kit::Native::LeftArrow, { 0, 1, 3 }, "a.back", v.pushed->backTip);
		fixed += D2RGui::Text("PageName", a.left + 60, a.top, width - 120, 90, v.pushed->title, Style::Name);
		if (!v.pushed->subtitle.empty()) fixed += D2RGui::Text("PageUnder", a.left + 60, a.top + 78, width - 120, 36, v.pushed->subtitle, Style::Note);
		a.top += BackHeight;
	} else if (!s.window.menus.empty()) {
		// the menus and their pages, in rows on top
		Menus::Look look;
		look.title = false;
		look.menus = { .size = 96, .iconSize = 60, .gap = 14 };
		look.pages = { .size = 76, .iconSize = 46, .gap = 12 };
		look.menuX = a.left, look.menuY = a.top;
		look.pageX = a.left, look.pageY = a.top;
		if (s.window.menus.size() > 1) a.top += look.menus.size + 20, look.pageY = a.top;
		const int m = s.nav.Menu();
		if (m >= 0 && !s.window.menus[static_cast<std::size_t>(m)].pages.empty()) a.top += look.pages.size + 20;
		fixed += s.nav.Layout(look);
	}
	s.cellsWanted = false;
	{
		std::lock_guard guard(s.hitLock);
		s.hits.clear();
	}
	a.top         = DrawFixed(v.header, HeaderBase, a.top, a, fixed);
	// the footer from the bottom: the notice line, its components over it
	const std::string notice = !s.notice.empty() ? s.notice : (AnyRestart(v) ? s.window.restartNote : std::string());
	if (!notice.empty()) {
		a.bottom -= NoticeHeight;
		fixed += D2RGui::Text("Notice", a.left, a.bottom, width, NoticeHeight, notice, s.noticeWarn && !s.notice.empty() ? Style::Warn : Style::Note);
	}
	a.bottom -= FixedHeight(v.footer, width);
	DrawFixed(v.footer, FooterBase, a.bottom, a, fixed);

	const int room  = std::max(1, a.bottom - a.top);
	int       total = 0;
	{
		int depth = 0;
		for (const auto& item : v.content) {
			if (!Visible(item)) continue;
			if (item.kind == Item::Kind::CardEnd && depth) --depth;
			total += HeightOf(item, width - 2 * CardInset * depth);
			if (item.kind == Item::Kind::CardStart) ++depth;
		}
	}
	int& scroll = Scroll();
	if (!s.reveal.empty()) { // App::Reveal: the tile a third of the way down
		int at = 0;
		for (const auto& item : v.content) {
			if (!Visible(item)) continue;
			if (item.kind == Item::Kind::Tiles && item.tileRows) {
				const auto rows   = item.tileRows();
				int        height = 0;
				for (const auto& p : PlaceTiles(rows, item.cellSize, width, height))
					if (p.tile >= 0 && rows[static_cast<std::size_t>(p.row)].tiles[static_cast<std::size_t>(p.tile)].id == s.reveal) scroll = at + p.y - room / 3, s.reveal.clear();
			}
			at += HeightOf(item, width);
		}
		s.reveal.clear();
	}
	if (!s.focus.empty()) { // App::Focus: the component in view, typing into it
		int at = 0;
		for (int k = 0; k < static_cast<int>(v.content.size()); ++k) {
			const auto& item = v.content[static_cast<std::size_t>(k)];
			if (!Visible(item)) continue;
			const int h = HeightOf(item, width);
			if (item.id == s.focus) {
				if (at < scroll || at + h > scroll + room) scroll = at + h - room / 2;
				if (item.kind == Item::Kind::Field && item.text) s.focusKey = "a.f" + std::to_string(k);
				break;
			}
			at += h;
		}
		s.focus.clear();
	}
	scroll      = std::clamp(scroll, 0, std::max(0, total - room));
	std::string inside;
	int         y = 0;
	std::vector<std::pair<std::size_t, int>> cards; // open cards: where their frame goes in `inside`, their top
	for (int i = 0; i < static_cast<int>(v.content.size()); ++i) {
		const auto& item = v.content[static_cast<std::size_t>(i)];
		if (!Visible(item)) continue;
		const int depth = static_cast<int>(cards.size()) - (item.kind == Item::Kind::CardEnd && !cards.empty() ? 1 : 0);
		Area      in    = a; // inside cards: narrower
		in.left += CardInset * depth, in.right -= CardInset * depth;
		const int inWidth = in.right - in.left;
		if (item.kind == Item::Kind::CardStart) cards.push_back({ inside.size(), y - scroll });
		if (item.kind == Item::Kind::CardEnd && !cards.empty()) {
			const auto [at, top] = cards.back();
			cards.pop_back();
			const int bottom = y - scroll + HeightOf(item, inWidth) - 10;
			if (bottom > 0 && top < room && bottom - top > 20)
				inside.insert(at, Image("Card" + std::to_string(i), in.left, top, Kit::Card(inWidth, bottom - top)));
		}
		if (item.kind == Item::Kind::HostGrid) {
			fixed += GridLayout(item);
			continue;
		}
		const int h = HeightOf(item, inWidth), at = y - scroll;
		y += h;
		if (item.kind != Item::Kind::List && item.kind != Item::Kind::Cells && (at + h < 0 || at > room)) {
			if (s.drop == i) s.drop = -1;
			continue;
		}
		inside += Draw(item, i, at, a.top, room, in); // a card's components sit one inset inside its frame
	}
	if (s.drop >= 0 && s.drop < HeaderBase && s.dropY < a.top) s.drop = -1;
	std::string c = fixed + (inside.empty() ? std::string() : Clip("Content", 0, a.top, a.bar - 6, room, inside));
	c += s.bar->Layout(a.top, room, total, scroll);
	// the open drop-down, over everything
	if (const Item* item = s.drop >= 0 ? ItemAt(v, s.drop) : nullptr) {
		const int         cur  = item->index.Get();
		const std::string pick = "a.p" + std::to_string(s.drop) + ":";
		if (Long(*item)) {
			const int lines  = static_cast<int>(DropDown::Filtered(item->entries, s.filter).size());
			const int height = (std::min(lines, ListLines) + 1) * LineHeight;
			const int ly     = s.dropY + BoxHeight + height > PanelH - 40 ? s.dropY - height : s.dropY + BoxHeight;
			c += DropDown::ScrollList("Drop", s.dropX, ly, std::max(s.dropW, 520), LineHeight, ListLines, item->entries, cur, pick, "a.dclose", PanelW, PanelH, s.list.first, s.filter, "a.lf", "a.ls");
		} else {
			std::vector<std::string> choices;
			for (const auto& entry : item->entries) choices.push_back(entry.text);
			const int height = static_cast<int>(choices.size()) * LineHeight;
			const int ly     = s.dropY + BoxHeight + height > PanelH - 40 ? s.dropY - height : s.dropY + BoxHeight;
			c += DropDown::List("Drop", s.dropX, ly, s.dropW, LineHeight, choices, cur, pick, "a.dclose", PanelW, PanelH);
		}
	}
	// the icon picker, over everything
	if (s.picker) {
		const int count = static_cast<int>(s.picker->sprites.size()), per = 6, size = 84, gap = 14;
		const int rowsN = (count + per - 1) / per, w = per * (size + gap) + gap + 40, h = rowsN * (size + gap) + gap + 110;
		const int px = (PanelW - w) / 2, py = std::max(200, (PanelH - h) / 2);
		c += HitButton("PickCatch", 0, 0, PanelW, PanelH, "a.ipx");
		c += Image("PickBack", px, py, Kit::Box(w, h));
		c += D2RGui::Text("PickTitle", px, py + 10, w, 70, s.picker->title.empty() ? "Pick an icon" : s.picker->title, Style::Axis);
		for (int k = 0; k < count; ++k) {
			const int x = px + 20 + gap + (k % per) * (size + gap), y = py + 90 + (k / per) * (size + gap);
			c += Button("Pick" + std::to_string(k), x, y, StashKit::SlotButton(size, size), { 0, 2, 1 }, "a.ip" + std::to_string(k),
			            k < static_cast<int>(s.picker->tips.size()) ? s.picker->tips[static_cast<std::size_t>(k)] : std::string(), size, size);
			c += Image("PickIcon" + std::to_string(k), x + 17, y + 17, s.picker->sprites[static_cast<std::size_t>(k)]);
		}
	}
	const auto title = Title();
	if (Hosted()) return StashWindow::Layout("@PANEL@", title, "", c, s.window.priority);
	const int priority = s.onOver ? OverPriority : s.window.priority;
	if (s.window.frame == Frame::Character) return CharacterWindow::Layout(panelName, probe, title, "a.close", c, priority);
	return StashWindow::Layout(panelName, title, "a.close", Probe(probe) + c, priority);
}

inline void SyncCells() {
	auto& s = Get();
	if (!s.hasCells) return;
	const bool on = s.open && s.cellsWanted;
	if (on) ItemCells::Place(s.cellsX, s.cellsY);
	if (on != s.cellsOn || on != ItemCells::Shown()) ItemCells::Show(on), s.cellsOn = on;
}

// The window again with the data as it is now (on a host: the layout for its next open).
inline void Show() {
	auto& s = Get();
	if (Hosted()) {
		const auto layout = Build("@PANEL@", {});
		if (s.window.host->layout) s.window.host->layout(layout, s.gridX, s.gridY);
		return;
	}
	Pool().Show();
}

inline void Type(const std::string& key, const Item& item);

// After a view: App::Focus's field starts typing.
inline void TypeFocus() {
	auto& s = Get();
	if (s.focusKey.empty()) return;
	const auto key = std::move(s.focusKey);
	s.focusKey.clear();
	int i = 0;
	const View v = Current();
	if (const Item* item = std::isdigit(static_cast<unsigned char>(key[3])) && (i = std::atoi(key.c_str() + 3), true) ? ItemAt(v, i) : nullptr; item && item->text) Type(key, *item);
}

inline void Notice(std::string text, bool warn) {
	Get().notice     = std::move(text);
	Get().noticeWarn = warn;
}

// A set's answer: a refusal shows in the notice line.
inline void Took(const Item& item, const std::string& refused) {
	if (!refused.empty()) return Notice(refused, true);
	if (Get().noticeWarn) Notice({}, false);
	if (item.changed) item.changed();
	if (Get().window.changed) Get().window.changed();
}

inline void Leave() {
	auto& s = Get();
	s.drop = -1, s.armed = -1;
	Notice({}, false);
	TextInput::Stop();
}

inline bool IsShown() {
	auto& s = Get();
	if (Hosted()) return s.window.host->isOpen && s.window.host->isOpen();
	return Pool().IsOpen() || Pool().Busy();
}

inline void Close() {
	auto& s = Get();
	TextInput::Stop();
	if (Hosted()) {
		if (s.window.host->close && s.window.host->isOpen && s.window.host->isOpen()) s.window.host->close();
	} else Pool().Close();
	s.drop         = -1;
	const bool was = s.open.exchange(false);
	s.cellsWanted  = false;
	SyncCells();
	if (!Hosted() && !s.onOver) LeftPanel::Closed();
	if (was && s.window.opened) s.window.opened(false);
}

inline void Wheel(int notches) {
	auto& s = Get();
	if (!s.open) return;
	if (s.drop >= 0) {
		const View v = Current();
		if (const Item* item = ItemAt(v, s.drop); item && Long(*item)) {
			s.list.Wheel(notches, static_cast<int>(DropDown::Filtered(item->entries, s.filter).size()), ListLines);
			Show();
		}
		return;
	}
	Scroll() = std::clamp(Scroll() + notches * WheelStep, 0, std::max(0, s.bar->Max()));
	Show();
}

// A tile's callback (UI thread), found again from its indices.
inline void TileCall(int item, int row, int tile, char what) {
	const View  v     = Current();
	const Item* found = ItemAt(v, item);
	if (!found || found->kind != Item::Kind::Tiles || !found->tileRows) return;
	const auto rows = found->tileRows();
	if (row < 0 || row >= static_cast<int>(rows.size()) || tile < 0 || tile >= static_cast<int>(rows[static_cast<std::size_t>(row)].tiles.size())) return;
	const auto& t = rows[static_cast<std::size_t>(row)].tiles[static_cast<std::size_t>(tile)];
	if (what == 'r' && t.rightClick) t.rightClick();
	else if (what == 'm' && t.middleClick) t.middleClick();
	else if ((what == 'h' || what == 'l') && t.hover) t.hover(what == 'h');
	else return;
	if (Get().open && what != 'h' && what != 'l') Show(); // a hover shows nothing new by itself (App::Refresh when it should)
}

// Window thread: right and middle clicks on tiles, the mouse coming over a
// tile and leaving it; then window.mouse.
inline bool Mouse(UINT message, WPARAM wParam, LPARAM lParam) {
	auto& s = Get();
	if (s.window.mouse && s.window.mouse(message, wParam, lParam)) return true;
	if (!s.open || (message != WM_MOUSEMOVE && message != WM_RBUTTONDOWN && message != WM_RBUTTONUP && message != WM_MBUTTONDOWN && message != WM_MBUTTONUP)) return false;
	const double scale = WindowHook::UiScale();
	if (scale <= 0) return false;
	const auto [ox, oy] = WindowHook::LeftAnchorOffset(StashWindow::X, StashWindow::Y);
	const int  x = static_cast<int>((static_cast<short>(LOWORD(lParam)) - ox) / scale), y = static_cast<int>((static_cast<short>(HIWORD(lParam)) - oy) / scale);
	int        at = -1;
	State::Hit hit {};
	{
		std::lock_guard guard(s.hitLock);
		for (int k = 0; k < static_cast<int>(s.hits.size()); ++k)
			if (const auto& h = s.hits[static_cast<std::size_t>(k)]; x >= h.left && x < h.right && y >= h.top && y < h.bottom) at = k, hit = h;
	}
	if (message == WM_MOUSEMOVE) {
		const int was = s.hovered.exchange(at);
		if (was == at) return false;
		State::Hit old {};
		bool       hadOld = false;
		{
			std::lock_guard guard(s.hitLock);
			if (was >= 0 && was < static_cast<int>(s.hits.size())) old = s.hits[static_cast<std::size_t>(was)], hadOld = true;
		}
		if (hadOld) RunOnUi([old] { TileCall(old.item, old.row, old.tile, 'l'); });
		if (at >= 0) RunOnUi([hit] { TileCall(hit.item, hit.row, hit.tile, 'h'); });
		return false;
	}
	if (at < 0) return false;
	if (message == WM_RBUTTONUP) RunOnUi([hit] { TileCall(hit.item, hit.row, hit.tile, 'r'); });
	if (message == WM_MBUTTONUP) RunOnUi([hit] { TileCall(hit.item, hit.row, hit.tile, 'm'); });
	return true; // kept from the game (no skill cast over the window)
}

// The mouse hook (wheel, scroll bar drag, clicks off a text box, window.mouse). UI thread, once.
inline void Hook() {
	auto& s = Get();
	if (s.hooked || Hosted()) return;
	s.hooked = true;
	WindowHook::Start({ .active = [] { return Get().open.load(); }, .wheel = Wheel, .extra = Mouse });
	s.bar->Attach();
}

inline void Open() {
	auto& s = Get();
	if (Hosted()) {
		Font::Refresh();
		Show();
		s.open = true;
		if (s.window.host->open) s.window.host->open();
		if (s.window.opened) s.window.opened(true);
		return;
	}
	// over the open stash (it stays open), else as a left panel (closing the game's)
	const bool over = s.window.overStash && s.over.Ready() && Hud::StashShown();
	if (over != s.onOver) Pool().Close(), s.onOver = over;
	if (!Pool().Ready()) return;
	Font::Refresh();
	Hook();
	s.stashClosing = false;
	if (!s.onOver) LeftPanel::Opening();
	s.open = true;
	Pool().Show();
	if (s.window.opened) s.window.opened(true);
}

inline void Toggle() {
	if (Get().open && IsShown()) Close();
	else Open();
}

inline void Tick() {
	auto& s = Get();
	if (s.open) {
		if (!IsShown()) Close();
		else if (s.onOver && (s.stashClosing.exchange(false) || !Hud::StashShown())) Close(); // the stash closed: the window with it
		else if (!Hosted() && !s.onOver && LeftPanel::Watch(Pool().IsOpen(), Pool().Busy())) Close();
	}
	if ((s.armed >= 0 || !s.armedCommand.empty()) && GetTickCount64() >= s.armedUntil) {
		s.armed = -1, s.armedCommand.clear();
		if (s.open) Show();
	}
	if (s.hasCells) ItemCells::Tick();
	if (s.hasButton) s.button.Watch(), s.button.Reload(s.inGame);
	if (s.window.tick) s.window.tick();
}

inline void Type(const std::string& key, const Item& item) {
	auto& s         = Get();
	s.original      = item.text.Get();
	s.draft         = s.original;
	const auto text = item.text;
	if (item.draft) {
		TextInput::Start(key, s.draft, {
		    .changed = [](const std::string& typed) { Get().draft = typed, Show(); },
		    .enter   = [text, item](const std::string& typed) { Took(item, text.Set(typed)), Show(); },
		    .escape  = [] { Show(); },
		});
	} else {
		TextInput::Start(key, s.original, {
		    .changed = [text, item](const std::string& typed) { Took(item, text.Set(typed)), Show(); },
		    .enter   = [](const std::string&) { Show(); },
		    .escape  = [text, item] { Took(item, text.Set(Get().original)), Show(); },
		});
	}
	Show();
}

inline bool ParseInt(const std::string& rest, int& out) {
	if (rest.empty() || !std::isdigit(static_cast<unsigned char>(rest[0]))) return false;
	out = std::atoi(rest.c_str());
	return true;
}

inline void OnCommand(const std::string& command) {
	auto& s = Get();
	if (command == "a.toggle") return Toggle();
	if (command == "a.open") return s.open ? Show() : Open();
	if (s.open && s.bar->Handle(command)) return;
	if (s.open && s.nav.Handle(command)) {
		s.stack.clear(), s.scroll = 0;
		Leave();
		return Show();
	}
	if (command.rfind("a.", 0) != 0 && command.rfind("typea.", 0) != 0) {
		if (s.window.command && s.window.command(command) && s.open) Show();
		return;
	}
	if (!s.open) return;
	if (command == "a.close") return Close();
	if (command == "a.back") {
		if (!s.stack.empty() && s.stack.back().page.canLeave && !s.stack.back().page.canLeave()) return Show();
		if (!s.stack.empty()) s.stack.pop_back();
		Leave();
		return Show();
	}
	if (command == "a.ipx") return s.picker.reset(), Show();
	if (command.rfind("a.ip", 0) == 0 && s.picker) {
		int        k    = 0;
		const auto pick = s.picker->pick;
		const bool ok   = ParseInt(command.substr(4), k);
		s.picker.reset();
		if (ok && pick) pick(k);
		return Show();
	}
	const View v = Current();
	if (command == "a.dclose") {
		s.drop = -1;
		if (TextInput::Key() == "a.lf") TextInput::Stop();
		return Show();
	}
	if (const Item* open = s.drop >= 0 ? ItemAt(v, s.drop) : nullptr) {
		const int lines = static_cast<int>(DropDown::Filtered(open->entries, s.filter).size());
		if (s.list.Handle(command, "a.ls", lines, ListLines)) return Show();
		if (command == "typea.lf") {
			TextInput::Start("a.lf", s.filter, {
			    .changed = [](const std::string& text) { Get().filter = text, Get().list.first = 0, Show(); },
			    .enter   = [](const std::string&) { Show(); },
			    .escape  = [] { Get().filter.clear(), Show(); },
			});
			return Show();
		}
	}
	if (command.rfind("typea.f", 0) == 0) {
		int i = 0;
		if (const Item* item = ParseInt(command.substr(7), i) ? ItemAt(v, i) : nullptr; item && item->text) Type(command.substr(4), *item);
		return;
	}
	if (command.size() < 4) return;
	const char  kind  = command[2];
	int         i     = 0;
	const Item* found = ParseInt(command.substr(3), i) ? ItemAt(v, i) : nullptr;
	if (!found) return;
	const Item item = *found; // its functions may change the page
	const auto rest = command.substr(3 + std::to_string(i).size());
	if (TextInput::Active() && TextInput::Key() != "a.lf") TextInput::Stop();
	if (kind != 'b') s.armed = -1;
	if (kind != 'w') s.armedCommand.clear();
	// "<row>:<n>" after the component's index
	const auto two = [&rest](int& first, int& second) {
		const auto colon = rest.find(':', 1);
		return rest.size() > 1 && colon != std::string::npos && ParseInt(rest.substr(1), first) && ParseInt(rest.substr(colon + 1), second);
	};
	switch (kind) {
	case 'c':
		if (item.flag) Took(item, item.flag.Set(!item.flag.Get()));
		break;
	case 'd':
		if (s.drop == i) s.drop = -1;
		else s.drop = i, s.list = {}, s.filter.clear();
		if (s.drop >= 0 && Long(item)) { // the current choice in view
			const auto shown = DropDown::Filtered(item.entries, {});
			const auto at    = std::find(shown.begin(), shown.end(), item.index.Get());
			if (at != shown.end()) s.list.first = static_cast<int>(at - shown.begin()) - ListLines / 2, s.list.Clamp(static_cast<int>(shown.size()), ListLines);
		}
		break;
	case 'p': {
		int k = 0;
		if (rest.size() > 1 && ParseInt(rest.substr(1), k) && k < static_cast<int>(item.entries.size()) && !item.entries[static_cast<std::size_t>(k)].header && item.index && item.index.Get() != k)
			Took(item, item.index.Set(k));
		s.drop = -1;
		if (TextInput::Key() == "a.lf") TextInput::Stop();
		break;
	}
	case 'n':
		if (item.number) {
			const double before = item.number.Get();
			const double after  = std::clamp(before + (rest == "-" ? -StepNow(item) : StepNow(item)), item.min, item.max);
			if (after != before) Took(item, item.number.Set(after));
		}
		break;
	case 'h':
	case 'v': {
		const auto& axis = kind == 'h' ? item.px : item.py;
		if (axis) {
			const int step = static_cast<int>(std::lround(StepNow(item)));
			Took(item, axis.Set(axis.Get() + (rest == "-" ? -step : step)));
		}
		break;
	}
	case 'z':
		if (item.reset) item.reset(), Took(item, {});
		break;
	case 't': {
		int        r = 0, c = 0;
		const auto colon = rest.find(':', 1);
		if (rest.size() > 1 && colon != std::string::npos && ParseInt(rest.substr(1), r) && ParseInt(rest.substr(colon + 1), c) && item.setCell && (!item.disabled || item.disabled(r, c).empty()))
			Took(item, item.setCell(r, c, !(item.cell && item.cell(r, c))));
		break;
	}
	case 'e':
		if (item.text) Took(item, item.text.Set({}));
		break;
	case 'x':
		if (item.remove) item.remove(), Took(item, {});
		break;
	case 'b':
		if (item.confirm && !(s.armed == i && GetTickCount64() < s.armedUntil)) {
			s.armed = i, s.armedUntil = GetTickCount64() + 10000;
			break;
		}
		s.armed = -1;
		if (item.click) item.click();
		break;
	case 'r': {
		int r = 0;
		if (item.pick && rest.size() > 1 && ParseInt(rest.substr(1), r)) item.pick(r);
		break;
	}
	case 'w': { // a row's button
		int r = 0, b = 0;
		if (!item.rows || !two(r, b)) break;
		const auto rows = item.rows();
		if (r >= static_cast<int>(rows.size()) || b >= static_cast<int>(rows[static_cast<std::size_t>(r)].buttons.size())) break;
		const auto& button = rows[static_cast<std::size_t>(r)].buttons[static_cast<std::size_t>(b)];
		if (button.confirm && !(s.armedCommand == command && GetTickCount64() < s.armedUntil)) {
			s.armedCommand = command, s.armedUntil = GetTickCount64() + 10000;
			break;
		}
		s.armedCommand.clear();
		if (!button.command.empty() && s.window.command) s.window.command(button.command);
		break;
	}
	case 'k': { // a tile
		int r = 0, t = 0;
		if (!item.tileRows || !two(r, t)) break;
		const auto rows = item.tileRows();
		if (r < static_cast<int>(rows.size()) && t < static_cast<int>(rows[static_cast<std::size_t>(r)].tiles.size()))
			if (const auto& click = rows[static_cast<std::size_t>(r)].tiles[static_cast<std::size_t>(t)].click) click();
		break;
	}
	case 'q': { // a square
		int k = 0;
		if (rest.size() > 1 && ParseInt(rest.substr(1), k) && k < static_cast<int>(item.squares.size()))
			if (const auto& click = item.squares[static_cast<std::size_t>(k)].click) click();
		break;
	}
	case 'i':
		if (item.iconClick) item.iconClick();
		break;
	case 'y':
		if (item.click2) item.click2();
		break;
	default: return;
	}
	Show();
}

// Any thread: every UI message; the stash closing closes a window over it at once.
inline void OnUiMessage(const D2RL::SharedEvents::UiMessageEvent& event) {
	auto& s = Get();
	if (s.window.uiMessage) s.window.uiMessage(event);
	if (!s.onOver || !s.open || !event.target || !event.command) return;
	if ((!std::strcmp(event.target, "BankPanelMessage") && !std::strcmp(event.command, "Close"))
	    || (!std::strcmp(event.target, "PanelManager") && event.text && !std::strncmp(event.text, "Bank", 4) && (!std::strcmp(event.command, "ClosePanel") || !std::strcmp(event.command, "UnloadPanel")))) {
		s.stashClosing = true;
		RunOnUi(Tick);
	}
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
	if (Get().window.mouseAlways) RunOnUi(Hook);
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
	if (Get().window.onConsole && !Get().window.onConsole(command)) return D2RL::ConsoleCommandResult::Handled;
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
	s.bar->OnScroll([](int scroll) { Scroll() = scroll, Show(); });
	SetNav();
	if (!Hosted()) {
		if (!s.pool.Register(Build, "View", std::max(2, s.window.views))) return false;
		s.pool.OnShown([] { SyncCells(), TypeFocus(); });
		if (s.window.overStash) {
			if (s.over.Register(Build, "Over", 4)) s.over.OnShown([] { SyncCells(), TypeFocus(); });
			else Log(true, "d2r-gui: no panels for the window over the stash; it closes the stash instead");
		}
	}
	Commands::Watch(OnUiMessage);
	Commands::Listen(OnCommand);
	if (s.window.cells) s.hasCells = ItemCells::Register(*s.window.cells);
	if (s.window.inventory && !RegisterSprite("mold", { MoldPicture() })) Log(true, "d2r-gui: the inventory button's mold could not be registered");
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
			category = !s.window.keyCategory.empty() ? s.window.keyCategory : !s.window.title.empty() ? s.window.title : std::string(pluginId);
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
inline void Open() { Detail::Get().open ? Detail::Show() : Detail::Open(); }
inline void Close() { Detail::Close(); }
inline void Toggle() { Detail::Toggle(); }
inline void Refresh() {
	if (Detail::Get().open || Detail::Hosted()) Detail::Show();
}
inline bool IsOpen() { return Detail::Get().open; }

// UI thread: a line at the bottom of the window (`warn`: red), until the page changes.
inline void Notice(std::string text, bool warn = false) {
	Detail::Notice(std::move(text), warn);
	Refresh();
}

// UI thread: shows menu `m` (its last page, or page `p`).
inline void Go(int menu, int page = -2) {
	auto& s = Detail::Get();
	s.stack.clear();
	s.nav.Go(menu, page);
	s.scroll = 0;
	Detail::Leave();
	Refresh();
}
inline int CurrentMenu() { return Detail::Get().nav.Menu(); }
inline int CurrentPage() { return Detail::Get().nav.Page(); }

// UI thread: a page over the current one (its back arrow returns), and back.
inline void Push(Page page) {
	Detail::Get().stack.push_back({ std::move(page), 0 });
	Detail::Leave();
	Refresh();
}
inline void Back() {
	if (!Detail::Get().stack.empty()) Detail::Get().stack.pop_back();
	Detail::Leave();
	Refresh();
}
inline int Depth() { return static_cast<int>(Detail::Get().stack.size()); }

// UI thread: opens the window straight on `page` (pushed over the base page).
inline void OpenOn(Page page) {
	Detail::Get().stack.clear();
	Detail::Get().stack.push_back({ std::move(page), 0 });
	Detail::Leave();
	Open();
}

// UI thread: new menus (tabs shown or hidden, pages added); the current one stays when it can.
inline void SetMenus(std::vector<Menu> menus) {
	Detail::Get().window.menus = std::move(menus);
	Detail::SetNav();
	Refresh();
}

// UI thread: scrolls to the component named `id` (Item::Id) and, for a
// field, starts typing in it (after an Adder's new item, say).
inline void Focus(std::string id) {
	Detail::Get().focus = std::move(id);
	Refresh();
}

// UI thread: the scrolled part to `y` (0: the top; a search typed, say).
inline void ScrollTo(int y) {
	Detail::Scroll() = std::max(0, y);
	Refresh();
}

// UI thread: scrolls the Tiles page to tile `id` (Tile::id).
inline void Reveal(std::string id) {
	Detail::Get().reveal = std::move(id);
	Refresh();
}

// UI thread: a grid of icons over the window (sprites about 50 square);
// `pick` gets the one clicked. A click elsewhere closes it.
inline void PickIcon(std::vector<std::string> sprites, std::function<void(int)> pick, std::vector<std::string> tips = {}, std::string title = {}) {
	Detail::Get().picker = Detail::State::Picker { std::move(sprites), std::move(tips), std::move(pick), std::move(title) };
	Refresh();
}

// The inventory button (move it, hide it: InventoryButton).
inline InventoryButton& Button() { return Detail::Get().button; }

// UI thread: the mouse wheel's notches as the window takes them (for a
// plugin running its own WindowHook).
inline void Wheel(int notches) { Detail::Wheel(notches); }

} // namespace D2RGui::App
