// Grail On!: the holy grail as a tile view. It draws rows of labelled tiles
// around a custom player page and puts items dropped on the page straight into
// their own tile. The grail file holds every stored item; only the tiles on
// screen hold real items on the page. With Player Page Host installed the page
// is a 12-row area of the host's shared page, else the plugin's own 16x16 page.
//
// Open it with the "G" button on the left edge of the inventory, or the
// console (Ctrl+`):
//   grailon                 open or close the view
//   grailon font 26         label text size
//   grailon gray -100 -35   saturation and brightness change of the item art, in %
//   grailon timing 1        print how long each view takes to open
//   grailon dump            write the raw game tables next to the log
//   grailon rolls           print each stored copy's rolls against the best
//   grailon forget          entries found but no longer stored go back to never found
//   grailon scrolltest      open or close the scroll test (see below)
//
// The lists come from the running game, so they follow the active mod: every
// spawnable unique of UniqueItems (quest items left out) and set item of
// SetItems, its base from Items, its name from the game's strings. A unique's
// category comes from its item types (ItemTypes), the grail tracker's
// categories, with "Other" for anything new. A set goes to a class when it has
// an item only that class can use, when the game's Sets table gives it one
// (UIClass), or when its items give that class's skills; else to the best tier
// of its bases. Item art comes from the game's own data/hd/items/items.json,
// uniques.json and sets.json (the active mod's when it has them), read through
// the game's file system; a built-in copy when they cannot be read. Nothing
// else needs to be installed.
//
// The mouse wheel over the view scrolls it, and so does a click on the scroll
// bar at its right.
//
// The window is Expanded Stash Plus's stash: its frame (BasicStash_Expanded,
// packaged in the plugin as PNG resources) with the grid covered by plain stash
// background, and an empty slot like the advanced stash's under each tile.
// Both are drawn at load from art.inc, cut from the stash sprite.
//
// The square icons along the top pick a menu (Menus); the column of square
// icons on the left picks one of its categories. Each unique and set item has
// a tile, and a unique an ethereal tile beside it when it can be ethereal
// (set items never are): five tiles per row, three cells apart, between the
// category column and the scroll bar, each with a two-cell label above it, its
// text at the bottom. Set items are grouped by set under the set's name
// ("Unknown set" with spoilers off until one of its items is found). A tile
// shows the item's art in gray.
//
// Settings, saved in Saved Games\Diablo II Resurrected\grail-storage\grail-on.toml
// (outside the mod, so reinstalling it keeps them):
//   spoilers (off by default): unfound uniques show their own name and art;
//   off, they show their base's until found.
//   compare (on): while the view is open, hovering a grail item elsewhere shows
//   its copy from the page as the game's comparison tooltip, without Shift.
//
// A stored copy whose variable rolls are all the best gets a shimmering gold
// frame in its tile.
//
// Only a grail item without a twin already stored gets on the page: D2RLoader
// asks the plugin before the item leaves the cursor, and Ctrl+click on another
// item does nothing. A dropped or Ctrl+clicked item goes straight to its tile;
// when the tile is elsewhere, the item waits (on the cursor, or where it was)
// until the view with its tile is open. Items are moved by item transactions,
// which make no sound, before a new view opens.
//
// Every label string is registered once the lists are built, and up to
// ViewCount views stay built: showing a view seen before only opens its panel.
//
// The page is the single custom page D2RLoader 1.3.1 allows, so Charm Inventory
// must be turned off while testing.

#include <D2RLPlugin/api.h>
#include "page_host_api.h"
#include <windows.h>
#include <shlobj.h>
#ifdef _WIN32
#include <wincodec.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <numeric>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

namespace {

constexpr const char* PluginId      = "grail-on";
constexpr const char* PluginVersion = "1.1.1";

const D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = PluginId,
	.name        = "Grail On!",
	.version     = PluginVersion,
	.author      = "wild-healer",
	.description = "Holy grail storage and tracker: every unique, set item and runeword in its own tile on a custom page.",
	.flags       = D2RL::PluginFlags::Client | D2RL::PluginFlags::NativeHooks, // the tooltip hooks
};

namespace Items  = D2RL::Items;
namespace Events = D2RL::SharedEvents;
namespace Tables = D2RL::DataTables;

struct AssetEntry {
	const char* key;
	const char* asset;
};
#include "art.inc"
#include "builtin_art.inc"
#include "icons.inc"
#include "mold.inc"

constexpr const char* Languages[]   = { "enUS", "deDE", "esES", "esMX", "frFR", "itIT", "jaJP", "koKR", "plPL", "ptBR", "ruRU", "zhCN", "zhTW" };
constexpr const char* CommandPrefix = "grail-on/Cmd-";
constexpr const char* StringsPath   = "data/local/lng/strings/d2rloader/grail-on/view.json";
// The panel is Expanded Stash Plus's stash made two cells taller
// (tools/tall_stash.py): 1687x1829 and the title under it, a view 18 cells of 98 units tall at 58,32
// and the category name in the band under it. The 16x16 page grid sits 1.5
// cells down the view: the first row's label is above it and the last row's
// half cell of room below it, the labels between rows take whole cells.
constexpr int PageSize = 16, Cell = 98, PanelWidth = 1687, PanelHeight = 1889, GridLeft = 58, ViewTop = 32, ViewRows = 18;
constexpr double GlowOpacity = 0.7; // the glow drawn at 70%
constexpr int GlowFrames = 8, GlowPad = 18; // perfect glow frames; how far it reaches beyond the slot
// Every panel hangs from the vanilla $LeftPanelAnchor's place (x 0, y 39.7% of
// the screen) written out, not from $LeftPanelAnchor: mods that move that
// anchor and redefine their own panel rects would carry the grail off screen.
constexpr int GridTop = ViewTop + Cell * 3 / 2, PanelY = -856;
constexpr int ButtonSize = 56; // check boxes
// The menus: square icons along the band above the grid, the category name at
// its right. The current menu's categories: a column of square icons in the
// grid's first column (never used by tiles), a gap between groups.
// The menus and the categories hang in two columns left of the window (in
// place of its hinge), menus outermost: the panel
// reaches MenuRoom further left than the window it draws, and the view's
// widgets are placed as if it did not (ShiftLayout moves them).
constexpr int MenuRoom = 236, MenuSize = 104, MenuStep = 120, MenuLeft = 8 - MenuRoom, MenuTop = ViewTop, MenuIcon = 64;
constexpr int CatSize = 84, CatLeft = -CatSize - 12, CatIcon = 50, CatGap = 18; // left of the window
// Tiles: five per row, starting in these columns, each as wide as two cells
// (a 1-wide item sits in the first); labels are two cells tall, 40% of a cell
// wider than that on each side.
constexpr int SlotColumns[] { 1, 4, 7, 10, 13 };
constexpr int SlotWidth = 2, LabelRows = 2, LabelMargin = Cell * 4 / 10;
// A set's name takes a row of cells above the set's first tile row.
constexpr int HeaderRows = 1;
constexpr int HeaderLeft = GridLeft + Cell + 20, HeaderWidth = 14 * Cell - 40;
// The settings page's sizes.
constexpr int TabWidth = 290, TabGap = 20; // the settings' page tabs
constexpr int ObjInputWidth = 1220 - 56 - 20; // the objectives page's text box: the table less the + button
constexpr int DropWidth = 610, MiscDrop = 260, DropHeight = 56, SmallBox = 64, ResetBox = 120, AxisWidth = 30 + 2 * SmallBox + 100 + 20, TileHeight = 52, TableLabel = 300, TableColumn = 230, TableRow = 72, TableWidth = TableLabel + 4 * TableColumn;
int           LineHeight = 4; // the section divider's, once made
// The scroll bar sits on the window's right border, past the grid; its track is cut into ScrollSteps buttons that each scroll to their
// share of the rows.
constexpr int BarWidth = 40, BarLeft = GridLeft + 16 * Cell + 10, BarTop = ViewTop + 64, ScrollSteps = 34, BarHeight = ScrollSteps * 50, StepHeight = BarHeight / ScrollSteps;
// The category name: under the frame (1829 tall), outside it, inside the panel.
constexpr int TitleTop = 1829, TitleHeight = PanelHeight - TitleTop, TitleSize = 36;
constexpr int ViewCount = 40;    // D2RLoader allows 64 panels per plugin
constexpr int SettingsView = -1; // CurrentCategory while the settings are shown
constexpr int StatsView    = -2; // and while the stats are (empty for now); the view opens on them

// The menus: the stats, unique weapons, unique armor and accessories, their
// ethereal copies (the same categories, eth tiles only), sets, runewords and
// the settings.
constexpr const char* Menus[] { "Stats", "Unique Weapons", "Unique Armor & Misc", "Ethereal Weapons", "Ethereal Armor", "Sets", "Runewords", "Misc", "Objectives", "Config" };
constexpr int StatsMenu = 0, WeaponMenu = 1, ArmorMenu = 2, EthWeaponMenu = 3, EthArmorMenu = 4, SetMenu = 5, RunewordMenu = 6, MiscMenu = 7, ObjMenu = 8, ConfigMenu = 9;

const D2RL::PluginContext*          Context {};
const D2RL::InventoryService*       Inventory {};
const D2RL::ItemService*            ItemApi {};
const D2RL::ThreadService*          Threads {};
const D2RL::PanelService*           PanelApi {};
const D2RL::WidgetService*          WidgetApi {};
const D2RL::ResourceService*        ResourceApi {};
const D2RL::SharedEventService*     EventApi {};
const D2RL::DataTableService*       TableApi {};
const D2RL::LocalizationService*    TextApi {};
const D2RL::ItemInteractionService* ClickApi {}; // optional: Ctrl+click on an item, before the game moves it

D2RL::Inventory::PageRegistrationHandle Page = D2RL::Inventory::InvalidHandle;

// ---- Player Page Host ----
// With Player Page Host installed (it loads first and owns the single custom
// page), the grail is a 3x5 area of the host's page; else the plugin's own
// 3x5 page. The page is only a staging place: every tile is a picture of its
// stored item, an item taken out is made on a free cell then put on the
// cursor, an item put in lands on a free cell then goes to the grail file
// (3x5 so the biggest items and custom ones fit). Everywhere else page cells
// are the grail's own: (0, 0) is the area's corner. Items of the host's page
// outside the area belong to other plugins and are never seen (Localize).
constexpr int StageCols = 3, StageRows = 5;
namespace Host {
D2RL::PluginCommunication::ServiceLease<PageHost::Service> Service;
PageHost::AreaHandle                                       Area = PageHost::InvalidArea;
PageHost::AreaInfo                                         Info { .structSize = sizeof(PageHost::AreaInfo) };
bool                                                       On() { return Area != PageHost::InvalidArea; }
} // namespace Host
constexpr int PageRows = 0; // the page rows that show tiles: none, every tile is a picture

// A page item in the grail's own cells; one outside the host area is no page item of ours.
void Localize(Items::ItemInfo& item) {
	if (!Host::On() || item.container != Items::ItemContainer::CustomPage) return;
	const int x = item.x - Host::Info.x, y = item.y - Host::Info.y;
	if (x < 0 || y < 0 || x >= static_cast<int>(Host::Info.width) || y >= static_cast<int>(Host::Info.height)) {
		item.container = Items::ItemContainer::Unknown;
		return;
	}
	item.x = x, item.y = y;
}

// A destination on the plugin's own page gets its handle; the host takes the
// grail's cells as they are and puts its own.
Items::ItemDestination OnPage(Items::ItemDestination destination) {
	if (destination.container == Items::ItemContainer::CustomPage && !Host::On()) destination.customPageHandle = Page;
	return destination;
}

// Item creation and moves: by the host when it owns the page. Handles stay
// with the plugin that got them, so the host takes and gives runtime ids.
bool FindItem(D2RL::PlayerHandle player, std::uint32_t runtimeId, Items::ItemInfo& found);
Items::Result CreateItem(D2RL::PlayerHandle player, Items::ItemCreateSpec& spec, D2RL::ItemHandle* made) {
	spec.destination = OnPage(spec.destination);
	if (!Host::On() || spec.destination.container != Items::ItemContainer::CustomPage) return ItemApi->createItem(Context, player, &spec, made);
	std::uint32_t runtimeId = 0, itemResult = 0;
	if (Host::Service->createItem(Host::Area, &spec, &runtimeId, &itemResult) != PageHost::Result::Success) return Items::Result::Unavailable;
	if (static_cast<Items::Result>(itemResult) != Items::Result::Success) return static_cast<Items::Result>(itemResult);
	Items::ItemInfo item { .structSize = Items::ItemInfoSize };
	if (made) *made = runtimeId && FindItem(player, runtimeId, item) ? item.handle : D2RL::ItemHandle {};
	return Items::Result::Success;
}
Items::Result Transact(Items::ExistingItemTransaction transaction, Items::ExistingItemTransactionResult& result) {
	if (!Host::On()) return ItemApi->executeExistingItemTransaction(Context, &transaction, &result);
	std::vector<Items::ExistingItemOperation> operations(transaction.operations, transaction.operations + transaction.operationCount);
	for (auto& operation : operations) {
		Items::ItemInfo item { .structSize = Items::ItemInfoSize };
		if (ItemApi->getItemInfo(Context, operation.item, &item) != Items::Result::Success) return Items::Result::InvalidArgument;
		operation.item = static_cast<D2RL::ItemHandle>(item.runtimeId); // the host finds it by its runtime id
	}
	transaction.operations   = operations.data();
	std::uint32_t itemResult = 0;
	const auto    hosted     = Host::Service->moveItems(Host::Area, &transaction, &result, &itemResult);
	return hosted != PageHost::Result::Success ? Items::Result::Unavailable : static_cast<Items::Result>(itemResult);
}

// The page is there: the plugin's own, or its area of the host's.
bool HavePage() { return Host::On() || Page != D2RL::Inventory::InvalidHandle; }

enum CompareMode { CompareNone, CompareShift, CompareAlways };
// A kind's goal (the overview counts the kinds with one; a stats page's
// centre shows how far its goal is), and what the loot filter hides of a kind.
enum GoalMode { GoalNone, GoalFind, GoalPerfect };
enum LootMode { LootAll, LootFound, LootPerfect };
struct Settings {
	int  font = 26, saturation = -100, brightness = -35;
	bool timing {};
	// Spoilers: what an unfound entry shows (its own name, its own art or
	// runes); set names show once one of the set's items is found.
	bool uniqueNames {}, uniqueArt {}, setItemNames {}, setItemArt {}, setNames {}, runewordNames {}, runewordRunes {};
	int  goal[4] { GoalFind, GoalFind, GoalFind, GoalFind }; // by Kind: uniques, ethereal uniques, set items, runewords
	int  loot[3] { LootAll, LootAll, LootAll };               // uniques, ethereal uniques, set items on the ground
	bool lootAmbiguous {};  // unidentified items hidden by what they are, not only when every item of their base would be
	int  compare = CompareAlways; // the grail copy in the comparison: never, with Shift held, always
	bool perfectFrames = true;     // perfect copies' glow around their tile
	bool trace = false; // grailon trace 1: each view step and the cursor item logged
	bool hold = false; // the cursor item kept aside while views change (grailon hold 1), the old way
	bool compact {}; // the window against the screen's left edge, its menus on its right
	bool swapColumns {}; // the categories in the outer icon column, the menus in the inner one
	bool middleClick = true; // a middle click on a grail item shows it in the grail
	bool objectives = true; // the custom objectives shown (their list is kept either way)
	bool frameAnimations = true, scrollAnimations = true; int frameMs = 30; // perfect glows and wheel steps animated (settings page); each scroll frame's time (grailon frame <n>)
	int  language = -1; // the texts' language: an index in Languages, or -1 for the game's
	int  buttonX = 3, buttonY = 954; // the inventory button's place, in the inventory panel's units
	int  searchX = -220, searchY = 984, searchWidth = 480; // the search bar's place, in the window's own frame (its rect is x WindowX() - MenuRoom, y PanelY there): compact mode leaves it; and its length
	int  version {}; // changes with every setting, so built views are not reused
} Config;
constexpr int DefaultButtonX = 3, DefaultButtonY = 954, DefaultSearchX = -220, DefaultSearchY = 984, DefaultSearchWidth = 480;

// The settings page's check boxes, by group; the settings file uses the same
// keys. Each box turns its field on.
enum ToggleGroup { ShowNames, ShowArt, DisplayGroup, LootGroup, ObjectiveGroup };
struct Toggle {
	const char* key;
	bool Settings::*field;
	const char* name;
	const char* help;
	int         group;
};
constexpr Toggle Toggles[] {
	{ "unique_names", &Settings::uniqueNames, "Uniques", "Unfound uniques show their own name. Off: their base item's name until you find them.", ShowNames },
	{ "set_item_names", &Settings::setItemNames, "Set items", "Unfound set items show their own name. Off: their base item's name until you find them.", ShowNames },
	{ "set_names", &Settings::setNames, "Sets", "Every set's name shows. Off: \"Unknown set\" until you find one of its items.", ShowNames },
	{ "runeword_names", &Settings::runewordNames, "Runewords", "Unfound runewords show their name. Off: \"Unknown runeword\" until you find them.", ShowNames },
	{ "unique_art", &Settings::uniqueArt, "Uniques", "Unfound uniques show their own picture. Off: their base item's picture until you find them.", ShowArt },
	{ "set_item_art", &Settings::setItemArt, "Set items", "Unfound set items show their own picture. Off: their base item's picture until you find them.", ShowArt },
	{ "runeword_runes", &Settings::runewordRunes, "Runewords", "Unfound runewords show their runes. Off: blank stones until you find them.", ShowArt },
	{ "perfect_frames", &Settings::perfectFrames, "Perfect frames", "Perfect copies glow around their tile.", DisplayGroup },
	{ "scroll_animations", &Settings::scrollAnimations, "Scroll animations", "The mouse wheel scrolls smoothly. Off: it jumps a row at a time.", DisplayGroup },
	{ "loot_ambiguous", &Settings::lootAmbiguous, "Filter ambiguous bases",
	  "Unidentified items are hidden by what they really are, even when other items share their base. Off: an unidentified item is only hidden when every unique or set item of its base would be.",
	  LootGroup },
	{ "frame_animations", &Settings::frameAnimations, "Frame animations", "The perfect glow moves. Off: it stays still.", DisplayGroup },
	{ "compact", &Settings::compact, "Compact", "The window sits against the screen's left edge, with its menus on its right. Off: the menus are on its left, where the game's panels start.", DisplayGroup },
	{ "swap_columns", &Settings::swapColumns, "Swap columns", "The categories go in the left icon column and the menus in the right one. Off: the menus on the left.", DisplayGroup },
	{ "middle_click", &Settings::middleClick, "Middle click", "A middle click on an item shows it in Grail On!, and its tooltip says so. Off: middle clicks are the game's.", DisplayGroup },
	{ "objectives", &Settings::objectives, "Custom objectives", "Your objectives have their own menu. Off: the menu is hidden and the list is kept.", ObjectiveGroup },
};

// Where the window (the stash frame) starts from the panels' anchor, and where
// its two icon columns go: in the MenuRoom left of it, or right of it when
// compact, the same way round on both sides (menus left, categories right,
// unless swapped). The panel always starts MenuRoom left of the window
// (ShiftLayout); compact, it reaches MenuRoom past it on the right as well.
int WindowX() { return Config.compact ? 0 : 236; }
int PanelSpan() { return PanelWidth + MenuRoom + (Config.compact ? MenuRoom : 0); }
int ColumnRoom() { return Config.compact ? PanelWidth : -MenuRoom; } // the columns' room, from the window's corner
// The left column's slot is MenuSize wide from MenuLeft; the right one ends
// at CatLeft + CatSize (12 from the window). A narrower button is centred in the left slot.
int MenuX() { return ColumnRoom() + (Config.swapColumns ? CatLeft + CatSize - MenuSize : MenuLeft) + MenuRoom; }
int CatX() { return ColumnRoom() + (Config.swapColumns ? MenuLeft + (MenuSize - CatSize) / 2 : CatLeft) + MenuRoom; }
constexpr const char* CompareNames[] { "None", "Shift", "Always" };
int OpenDrop = -1; // the settings drop-down whose list is open (goals 0-3, loot 4-6, compare 7, language 8)
constexpr int SettingsPageCount = 5;
int SettingsPage = 0; // the settings' page: general, display, loot, misc tracking
int ObjPicking = -1; // the objective whose icon picker is open, or -1
int ObjFirst = 0, ObjPerPage = 1; // the objectives page's first objective shown, and how many fit
int MiscFirst = 0, MiscPerPage = 1; // the misc tracking page's first type shown, and how many fit
constexpr const char* GoalNames[] { "None", "Find", "Perfect" };
constexpr const char* GoalKeys[] { "none", "find", "perfect" };
constexpr const char* GoalSettings[] { "goal_uniques", "goal_ethereal", "goal_sets", "goal_runewords" };
constexpr const char* LootNames[] { "Show all", "Hide found", "Hide perfect" };
constexpr const char* LootKeys[] { "all", "found", "perfect" };
constexpr const char* LootSettings[] { "loot_uniques", "loot_ethereal", "loot_sets" };
constexpr const char* GoalRowNames[] { "Uniques", "Ethereal uniques", "Set items", "Runewords" };

void Say(bool warn, const char* format, ...) noexcept {
	char    line[1024] {};
	va_list args;
	va_start(args, format);
	std::vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	if (!Context) return;
	warn ? Context->LogWarn(line) : Context->LogInfo(line);
	if (auto* api = Context->GetApi(); api && api->writeConsoleMessage)
		api->writeConsoleMessage(Context, line, warn ? D2RL::ConsoleMessageKind::Warning : D2RL::ConsoleMessageKind::Output);
}

// What is switched off, for the grail page to say: a blocking one (the grail
// cannot work: no lists, no storage, no page) replaces the page with the
// message; the others are listed on the settings page and noted at the
// bottom of every view.
struct ProblemNote {
	std::string text;
	bool        blocking {};
};
std::mutex               ProblemLock;
std::vector<ProblemNote> Problems;
std::atomic<int>         ProblemCount {}, BlockingCount {};

void Problem(bool blocking, const char* format, ...) noexcept {
	char    line[1024] {};
	va_list args;
	va_start(args, format);
	std::vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	Say(true, "%s", line);
	std::string text = line;
	for (const char* prefix : { "grailon: ", "grail-on: " })
		if (text.rfind(prefix, 0) == 0) text.erase(0, std::strlen(prefix));
	if (!text.empty()) text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
	if (!text.empty() && text.back() != '.' && text.back() != ')') text += '.';
	std::lock_guard guard(ProblemLock);
	for (const auto& note : Problems)
		if (note.text == text) return;
	Problems.push_back({ text, blocking });
	++ProblemCount;
	if (blocking) ++BlockingCount;
}

std::vector<ProblemNote> ProblemList() {
	std::lock_guard guard(ProblemLock);
	return Problems;
}

std::string CodeText(std::uint32_t code) {
	std::string text;
	for (int i = 0; i < 4; ++i) {
		const char c = static_cast<char>(code >> (i * 8));
		if (c && c != ' ') text += c;
	}
	return text;
}

std::string Key(const std::string& name) {
	std::string key;
	for (const char c : name)
		if (std::isalnum(static_cast<unsigned char>(c))) key += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return key;
}

// ---- the lists, built from the game's tables ----

struct CategoryEntry {
	int         menu {};    // index in Menus
	std::string group, name; // a gap between groups on the button row
	std::string icon; // its icon sprite
	int         width = 1, height = 1;
	int         artSize {}; // the icon's size in pixels when it is not a cell's
};
// A unique or set item of the grail.
struct UniqueEntry {
	int         category {}, row {}, kind {}, width {}, height {};
	bool        eth {}, set {};
	bool        onlyEth {}; // always ethereal: no plain tile
	int         setId = -1, setIndex {}; // a set item's set and its place in it
	std::string id; // stable across game versions: "u:"/"s:" + its index, "r:" + its runeword key (the storage file uses it)
	std::string name, base, code;
	std::string ghost; // the base's art
	std::string art;   // the unique's own art
	int         color = -1, colorClass = -1; // its colour (GlobalData colorTransform) and its base art's colour class (colorRange)
	// A runeword: its runes (item codes) and the item types it goes in (and not in).
	bool                     runeword {};
	int                      rule = -1; // a runeword's kind (RunewordRules)
	int                      level {}; // a runeword's: its highest rune's required level
	std::vector<std::string> runes, types, excluded;
	bool                     secret {}; // the Standard of Heroes: a tile of its own, counted nowhere
	bool                     misc {};   // a misc base tracked by its item type (Misc menu): one tile per base, counted in no kind
	std::string              miscType;  // its item type's code
	bool                     objective {}; // a player's own objective (Objectives menu): a checkbox tile with an icon
};
struct BaseInfo {
	int  width = 1, height = 1, kind = 2; // kind: 0 weapon, 1 armor, 2 other
	bool durable {}, quest {};
	int  category = -1; // index in CategoryRules
	int  tier {};       // 0 normal, 1 exceptional, 2 elite
	int  classRule = -1; // index in SetRules of its class, -1 for everyone's
	int  maxDefense {};   // maxac, 0 for no defense
	int  levelReq {};
	int  dropSound {};    // sounds row, 0 for none
	int  sockets = -1;    // gemsockets: the most sockets it can have, -1 when not known
	int  colorClass = -1; // InvTrans
	int  row = -1;        // its Items row (an item unit's class)
	std::string type;     // its own item type (Items type column)
	std::string name, asset;
	std::vector<std::string> types; // its item types and their parents
};

// The grail tracker's categories, in its order, and the item type that puts a
// base in each. A base takes the first rule, in RulePriority order, that one
// of its types or their parents (Equiv1/Equiv2) matches; "Other" otherwise.
// Class items come first, then the specific weapon types before their parents.
struct CategoryRule {
	const char* group;
	const char* name;
	const char* type;
};
constexpr CategoryRule CategoryRules[] {
	{ "Armor", "Helms", "helm" },
	{ "Armor", "Armor", "tors" },
	{ "Armor", "Shields", "shld" },
	{ "Armor", "Gloves", "glov" },
	{ "Armor", "Boots", "boot" },
	{ "Armor", "Belts", "belt" },
	// Class items, by the game's names for their types (ItemTypes).
	{ "Armor", "Primal Helms", "barb" },
	{ "Armor", "Pelts", "drui" },
	{ "Armor", "Auric Shields", "pala" },
	{ "Armor", "Voodoo Heads", "necr" },
	{ "Armor", "Grimoires", "warl" },
	// Weapons in alphabetical order. Amazon weapons go with their kind (bows,
	// javelins, spears); assassin claws and sorceress orbs have their own.
	{ "Weapons", "Axes", "axe" },
	{ "Weapons", "Bows", "bow" },
	{ "Weapons", "Claws", "assn" },
	{ "Weapons", "Crossbows", "xbow" },
	{ "Weapons", "Daggers", "knif" },
	{ "Weapons", "Javelins", "jave" },
	{ "Weapons", "Maces", "blun" },
	{ "Weapons", "Orbs", "sorc" },
	{ "Weapons", "Polearms", "pole" },
	{ "Weapons", "Scepters", "scep" },
	{ "Weapons", "Spears", "spea" },
	{ "Weapons", "Staves", "staf" },
	{ "Weapons", "Swords", "swor" },
	{ "Weapons", "Throwing", "thro" },
	{ "Weapons", "Wands", "wand" },
	{ "Accessories", "Charms", "char" },
	{ "Accessories", "Amulets", "amul" },
	{ "Accessories", "Rings", "ring" },
	{ "Accessories", "Jewels", "jewl" },
	{ "Accessories", "Other", "" },
};
constexpr const char* RulePriority[] { "assn", "sorc", "pala", "necr", "barb", "drui", "warl", "jave", "thro", "axe", "xbow", "bow", "knif", "scep", "staf",
	                                   "wand", "blun", "pole", "spea", "swor", "helm", "tors", "shld", "glov", "boot", "belt", "char", "amul", "ring", "jewl" };
constexpr int OtherRule = static_cast<int>(std::size(CategoryRules)) - 1;

// Set categories: a set with a class item goes to its class, any other to the
// tier of its best base.
constexpr CategoryRule SetRules[] {
	{ "Class", "Amazon Sets", "amaz" },     { "Class", "Assassin Sets", "assn" }, { "Class", "Barbarian Sets", "barb" },  { "Class", "Druid Sets", "drui" },
	{ "Class", "Necromancer Sets", "necr" }, { "Class", "Paladin Sets", "pala" },  { "Class", "Sorceress Sets", "sorc" }, { "Class", "Warlock Sets", "warl" },
	{ "Tier", "Normal Sets", "" },          { "Tier", "Exceptional Sets", "" },   { "Tier", "Elite Sets", "" },
};
constexpr int FirstTierRule = 8;
// Runeword categories: the first item type a runeword goes in decides (its
// own type or a parent); the icon is the unique category's of that kind.
struct RunewordRule {
	std::string name, type, icon;
	std::string game; // its RunewordUICategories name ("r_mel"), empty for the plugin's own
};
// Gloves, belts and boots only have runewords in mods that give them sockets
// and runewords; their categories show only then, like every category. The
// game's runeword categories (RunewordUICategories, given to item types in
// ItemTypes) come first: the first five are the game's own, a mod's added
// ones follow them (BuildLists).
const RunewordRule BaseRunewordRules[] {
	{ "Melee Weapons", "mele", "swor" }, { "Missile Weapons", "miss", "bow" }, { "Body Armor", "tors", "tors" }, { "Helms", "helm", "helm" }, { "Shields", "shld", "shld" },
	{ "Gloves", "glov", "glov" },        { "Belts", "belt", "belt" },          { "Boots", "boot", "boot" },        { "Other", "", "other" },          { "Weapons", "", "weapons" },
};
constexpr const char* GameRunewordCategories[] { "r_mel", "r_mis", "r_arm", "r_hel", "r_off" }; // rules 0-4
constexpr int         OtherRunewordRule = 8; // any type not above
constexpr int         AnyWeaponRule     = 9; // runewords for melee and missile weapons alike: one tile (same stats on any base)
std::vector<RunewordRule> RunewordRules(std::begin(BaseRunewordRules), std::end(BaseRunewordRules)); // a mod's categories past AnyWeaponRule (lock held)
std::vector<int>          RunewordRuleOrder { AnyWeaponRule, 0, 1, 2, 3, 4, 5, 6, 7, 8 }; // their categories' order
// Each item type's runeword rules from the game's categories (by type code);
// empty when the game's categories could not be read (the plugin's own rules then).
std::map<std::string, std::vector<int>> TypeRunewordRules;
// A base's runeword kind: the first rule its types (with their parents) have;
// other weapons (orbs) are melee weapons, never shields.
int RunewordRuleOf(const BaseInfo& base) {
	const auto has = [&](const std::string& type) { return std::find(base.types.begin(), base.types.end(), type) != base.types.end(); };
	if (!TypeRunewordRules.empty())
		for (const auto& type : base.types)
			if (const auto rules = TypeRunewordRules.find(type); rules != TypeRunewordRules.end() && !rules->second.empty()) return rules->second.front();
	for (int r = 0; r < OtherRunewordRule; ++r)
		if (!RunewordRules[r].type.empty() && has(RunewordRules[r].type)) return r;
	return has("weap") ? 0 : OtherRunewordRule;
}
// Every runeword rule a base's types (with their parents) have.
std::vector<int> BaseRunewordRules_(const BaseInfo& base) {
	std::vector<int> rules;
	if (!TypeRunewordRules.empty())
		for (const auto& type : base.types)
			if (const auto found = TypeRunewordRules.find(type); found != TypeRunewordRules.end())
				for (const int r : found->second)
					if (std::find(rules.begin(), rules.end(), r) == rules.end()) rules.push_back(r);
	if (rules.empty()) rules.push_back(RunewordRuleOf(base));
	return rules;
}
// Whether a runeword tile of kind `rule` takes a base.
bool RuleTakes(int rule, const BaseInfo& base) {
	const auto rules = BaseRunewordRules_(base);
	const auto has   = [&](int r) { return std::find(rules.begin(), rules.end(), r) != rules.end(); };
	return has(rule) || (rule == AnyWeaponRule && (has(0) || has(1)));
}
// A class (its PlayerClass row: ama sor nec pal bar dru ass war) -> its rule in SetRules.
constexpr int ClassRule[] { 0, 6, 4, 5, 2, 3, 1, 7 };
constexpr int ClassCount = static_cast<int>(std::size(ClassRule));

// Where the fields sit in the compiled rows (game build 3.3.94995; matched
// against the txt files). A table whose row size differs is not read.
constexpr std::uint32_t TypeRowSize = 232, ItemRowSize = 448, UniqueRowSize = 348, SetRowSize = 460;
namespace TypeRow {
constexpr int Code = 0, Equiv1 = 4, Equiv2 = 6; // codes; Equiv: u16 rows, 0 for none
constexpr int RunewordCategory1 = 228, RunewordCategory2 = 229; // RunewordUICategories rows, 0xFF for none
}
namespace ItemRow {
constexpr int Code = 128, NormalCode = 132, ExceptionalCode = 136, EliteCode = 140, NameString = 252, Width = 286, Height = 287, NoDurability = 290, Type = 302, Type2 = 304, Quest = 314;
constexpr int MaxDefense = 216; // maxac, u32
constexpr int DropSound  = 308; // dropsound, u16 sounds row (what the game plays when the item lands in a grid)
constexpr int InvTrans   = 338; // u8: its inventory art's colour class (GlobalData colorRange)
constexpr int LevelReq   = 335; // u8: levelreq
}
namespace UniqueRow {
constexpr int Index = 2, IndexLength = 32, NameString = 34, Code = 40, Flags = 44, Spawnable = 1;
constexpr int Props = 152, PropCount = 12; // {i32 prop, par, min, max}
constexpr int InvTransform = 61; // u8: its colour (colors.txt row, GlobalData colorTransform), 255 for none
}
// Property functions (Properties func1) the ethereal rules come from.
constexpr int IndestructibleFunc = 20, EtherealFunc = 23;
namespace SetRow {
constexpr int Index = 2, IndexLength = 32, NameString = 36, Code = 40, Flags = 44, Set = 48, SetIndex = 50;
constexpr int Props = 152, PropCount = 9, BonusProps = 296, BonusPropCount = 10; // {i32 prop, par, min, max}
constexpr int InvTransform = 69;
}
namespace SetsRow {
constexpr int NameString = 2; // the set's name (Sets)
}

template <typename Value>
Value Field(const void* row, int offset) {
	Value value {};
	std::memcpy(&value, static_cast<const unsigned char*>(row) + offset, sizeof(value));
	return value;
}

std::string FourCC(const void* row, int offset) { return CodeText(Field<std::uint32_t>(row, offset)); }

std::recursive_mutex       Lock; // guards the lists and the view state, shared with the game thread (and the tooltip hooks)
std::vector<CategoryEntry> Categories; // the categories with uniques, in the tracker's order
std::vector<UniqueEntry>   Uniques;    // by category, then unique row
std::vector<int>           UniqueByRow; // UniqueItems row -> index in Uniques, or -1
std::vector<int>           SetByRow;    // SetItems row -> index in Uniques, or -1
int                        SecretEntry = -1; // the Standard of Heroes' entry (last in Uniques), or -1
std::uint32_t              SecretWanted {};  // the standard going in (runtime id) until it is recorded or gone
std::vector<int>           RunewordByRow; // Runes row -> index in Uniques, or -1
std::map<std::uint32_t, std::vector<int>> RunewordIds; // a runeword item's name string -> its entries (Hustle has two)
std::map<std::uint32_t, int> RunewordUnits; // runtime id -> entry, for the native tooltip hooks
std::map<std::uint32_t, int> BaseFor; // runtime id -> the runeword tile a base (to make it in later) was put in
int StoredBaseEntry(std::uint32_t runtimeId); // the tile of a stored base on the page, or -1
std::map<std::string, BaseInfo> Bases;
std::map<int, std::string> SetNames; // Sets row -> its name
std::atomic<bool>          ListsReady {}, ListsWanted { true }, StringsWanted {}, LanguageRefresh {}, ViewAfterStrings {};
std::uint64_t              ViewRetryAt = 0; // UI thread: a view that found no free panel is shown again then
bool                       SetClassFound {}; // the Sets UIClass column was found

int UniqueCount() { return static_cast<int>(Uniques.size()); }
int CategoryCount() { return static_cast<int>(Categories.size()); }

// ---- languages ----
// Every text the plugin shows is written in English in the code and goes
// through Tr. The game's own translation wins when one of its strings has the
// same English text (the active mod's strings files, so a mod's translations
// come along), else the plugin's own (translations.inc), else the English.
// The language is the game's (found by comparing the game's text for some
// ids with each language's), or the one chosen in the settings; item names
// then come from the strings files in that language too.
#include "translations.inc"
constexpr int LanguageCount = static_cast<int>(std::size(Languages));
constexpr const char* LanguageNames[] { "English", "Deutsch", "Español (España)", "Español (Latinoamérica)", "Français", "Italiano", "日本語", "한국어", "Polski", "Português (Brasil)", "Русский", "简体中文", "繁體中文" };
struct GameTexts {
	std::vector<std::array<std::string, std::size(Languages)>> texts;
	std::map<std::uint32_t, int>                                byId;
	std::map<std::string, int>                                  byEnglish;
	std::map<std::string, int>                                  byKey; // lower case
};
std::mutex TextLock;
GameTexts  Texts;            // guarded by TextLock
int        GameLanguage = 0; // guarded by TextLock

int UiLanguage() {
	if (Config.language >= 0 && Config.language < LanguageCount) return Config.language;
	std::lock_guard guard(TextLock);
	return GameLanguage;
}

// `english` in `language` (-1: the texts' language).
std::string Tr(const std::string& english, int language = -1) {
	if (language < 0) language = UiLanguage();
	if (english.empty() || language <= 0 || language >= LanguageCount) return english;
	{
		std::lock_guard guard(TextLock);
		if (const auto game = Texts.byEnglish.find(english); game != Texts.byEnglish.end())
			if (const auto& text = Texts.texts[game->second][language]; !text.empty()) return text;
	}
	for (const auto& translation : Translations)
		if (english == translation.english) return translation.other[language - 1];
	return english;
}

// A game string by its key ("r_arm"), in the texts' language (English when it has none there).
std::string GameKeyText(std::string key) {
	for (auto& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	const int       language = UiLanguage();
	std::lock_guard guard(TextLock);
	const auto      found = Texts.byKey.find(key);
	if (found == Texts.byKey.end()) return {};
	const auto& entry = Texts.texts[found->second];
	return !entry[language].empty() ? entry[language] : entry[0];
}

// A translated format with one value in it ("Ethereal %s", "%d Sockets").
std::string TrFormat(const char* format, const std::string& value, int language = -1) {
	const std::string pattern = Tr(format, language);
	const auto        at      = pattern.find('%');
	if (at == std::string::npos || at + 1 >= pattern.size()) return pattern;
	return pattern.substr(0, at) + value + pattern.substr(at + 2);
}

// One JSON string from `at` (its opening quote), \u escapes made UTF-8; `at` ends past it.
std::string JsonText(const std::string& text, std::size_t& at) {
	std::string out;
	const auto  put = [&](std::uint32_t c) {
        if (c < 0x80) out += static_cast<char>(c);
        else if (c < 0x800) out += static_cast<char>(0xC0 | (c >> 6)), out += static_cast<char>(0x80 | (c & 0x3F));
        else if (c < 0x10000) out += static_cast<char>(0xE0 | (c >> 12)), out += static_cast<char>(0x80 | ((c >> 6) & 0x3F)), out += static_cast<char>(0x80 | (c & 0x3F));
        else
            out += static_cast<char>(0xF0 | (c >> 18)), out += static_cast<char>(0x80 | ((c >> 12) & 0x3F)), out += static_cast<char>(0x80 | ((c >> 6) & 0x3F)),
                out += static_cast<char>(0x80 | (c & 0x3F));
	};
	const auto hex = [&](std::size_t from) { return from + 4 <= text.size() ? static_cast<std::uint32_t>(std::strtoul(text.substr(from, 4).c_str(), nullptr, 16)) : 0U; };
	for (++at; at < text.size() && text[at] != '"'; ++at) {
		if (text[at] != '\\' || at + 1 >= text.size()) {
			out += text[at];
			continue;
		}
		const char e = text[++at];
		if (e == 'n') out += '\n';
		else if (e == 't') out += '\t';
		else if (e == 'r') out += '\r';
		else if (e == 'u') {
			std::uint32_t c = hex(at + 1);
			at += 4;
			if (c >= 0xD800 && c < 0xDC00 && at + 6 < text.size() && text[at + 1] == '\\' && text[at + 2] == 'u') {
				const std::uint32_t low = hex(at + 3);
				if (low >= 0xDC00 && low < 0xE000) c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00), at += 6;
			}
			put(c);
		} else out += e;
	}
	++at;
	return out;
}

// A strings file (an array of flat objects: "id", "Key", "enUS", "deDE"...) into `texts`.
// A game text without its colour codes: "ÿc" and the colour's letter (as the
// strings files have it, UTF-8 or Latin-1), or the private-use character the
// game turns "ÿc" into and its letter. The grail's labels have their own colours.
// Of a text with gendered or plural forms, the first form.
std::string StripColors(const std::string& text) {
	std::string out;
	out.reserve(text.size());
	for (std::size_t i = 0; i < text.size(); ++i) {
		const auto c = static_cast<unsigned char>(text[i]);
		if (c == 0xC3 && i + 2 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0xBF && text[i + 2] == 'c') {
			i += 3; // and the colour letter
			continue;
		}
		if (c == 0xFF && i + 1 < text.size() && text[i + 1] == 'c') {
			i += 2;
			continue;
		}
		if (c == 0xEE && i + 2 < text.size()) { // U+E000..U+EFFF
			i += 3;
			continue;
		}
		out += static_cast<char>(c);
	}
	// Gendered and plural forms ("[ms]compact[mp]compacts[fs]compacte..."):
	// the first one, after what comes before the first tag.
	const auto tagAt = [&](std::size_t from) {
		for (std::size_t i = out.find('[', from); i != std::string::npos; i = out.find('[', i + 1))
			if (i + 3 < out.size() && out[i + 3] == ']' && std::islower(static_cast<unsigned char>(out[i + 1])) && std::islower(static_cast<unsigned char>(out[i + 2]))) return i;
		return std::string::npos;
	};
	if (const auto first = tagAt(0); first != std::string::npos) {
		const auto next = tagAt(first + 4);
		out         = out.substr(0, first) + out.substr(first + 4, next == std::string::npos ? std::string::npos : next - first - 4);
	}
	return out;
}

void ReadTexts(const std::string& file, GameTexts& texts) {
	std::size_t at = file.find('{');
	while (at != std::string::npos && at < file.size()) {
		std::array<std::string, std::size(Languages)> entry;
		long                                          id = -1;
		std::string                                   key;
		++at;
		while (at < file.size()) {
			at = file.find_first_of("\"}", at);
			if (at == std::string::npos || file[at] == '}') break;
			const std::string name = JsonText(file, at);
			at                     = file.find_first_not_of(" \t\r\n:", at);
			if (at == std::string::npos) break;
			if (file[at] == '"') {
				const std::string value = StripColors(JsonText(file, at));
				for (int l = 0; l < LanguageCount; ++l)
					if (name == Languages[l]) entry[l] = value;
				if (name == "Key") key = value;
			} else {
				const auto end = file.find_first_of(",}", at);
				if (name == "id") id = std::strtol(file.substr(at, end - at).c_str(), nullptr, 10);
				at = end;
			}
		}
		if (at == std::string::npos) break;
		if (id >= 0 || !entry[0].empty()) {
			const int index = static_cast<int>(texts.texts.size());
			texts.texts.push_back(entry);
			if (id >= 0) texts.byId.try_emplace(static_cast<std::uint32_t>(id), index);
			if (!entry[0].empty()) texts.byEnglish.try_emplace(entry[0], index);
			if (!key.empty()) {
				for (auto& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				texts.byKey[key] = index; // a later file (a mod's) wins
			}
		}
		at = file.find('{', at);
	}
}

// Game thread. The game's strings files, and which language the game shows.
std::string ArchiveFile(const char* path);
std::string ResourceFile(const char* path);
void ReadGameTexts() {
	GameTexts texts;
	// The interface's first: its English texts are the ones the plugin's are compared with.
	for (const char* name : { "ui", "item-names", "item-nameaffixes", "item-runes", "item-modifiers", "skills", "ui-controller" }) {
		const std::string path = std::string("data/local/lng/strings/") + name + ".json";
		std::string       file = ArchiveFile(path.c_str()); // quietly: a file a game version lacks is no problem
		if (file.empty()) file = ResourceFile(path.c_str());
		ReadTexts(file, texts);
	}
	// The game's language: the one whose text the game gives for most of a sample of ids.
	int votes[std::size(Languages)] {}, asked = 0;
	for (const auto& [id, index] : texts.byId) {
		if (asked >= 60 || !TextApi) break;
		const auto& entry = texts.texts[index];
		if (entry[0].empty() || entry[0] == entry[LanguageCount - 1]) continue; // the same in every language
		char shown[512] {};
		if (TextApi->getStringById(Context, id, shown, sizeof(shown), nullptr) != D2RL::Localization::Result::Success) continue;
		++asked;
		for (int l = 0; l < LanguageCount; ++l)
			if (entry[l] == StripColors(shown)) {
				++votes[l];
				break;
			}
	}
	int language = 0;
	for (int l = 1; l < LanguageCount; ++l)
		if (votes[l] > votes[language]) language = l;
	Say(false, "grailon: %zu game texts read, the game shows %s (%d of %d checked texts)", texts.texts.size(), Languages[language], votes[language], asked);
	std::lock_guard guard(TextLock);
	Texts        = std::move(texts);
	GameLanguage = language;
}

// A name from the game's strings, without the tier tag some mods add (" (n)").
using NameLookup = std::string (*)(std::uint16_t id);
std::string GameText(std::uint16_t id) {
	std::string name;
	// A language chosen in the settings: from the strings files.
	if (Config.language >= 0 && Config.language < LanguageCount) {
		std::lock_guard guard(TextLock);
		if (const auto found = Texts.byId.find(id); found != Texts.byId.end()) name = Texts.texts[found->second][Config.language];
	}
	if (name.empty()) {
		char text[256] {};
		if (!TextApi || TextApi->getStringById(Context, id, text, sizeof(text), nullptr) != D2RL::Localization::Result::Success) return {};
		name = StripColors(text);
	}
	if (name.size() > 4 && name[name.size() - 4] == ' ' && name[name.size() - 3] == '(' && name.back() == ')') name.resize(name.size() - 4);
	return name;
}

std::string KeyText(const char* key) {
	char text[256] {};
	if (!TextApi || TextApi->getStringByKey(Context, key, text, sizeof(text), nullptr) != D2RL::Localization::Result::Success) return {};
	return StripColors(text);
}

// The runewords' names by their Runes key ("Runeword1", or a mod's own such as "Friendship"), from the game's
// strings file (data/local/lng/strings/item-runes.json: objects with "id",
// "Key", "enUS"...): the string by its id in the game's language, else its
// English text. The localization service does not find these keys by name.
std::map<std::string, std::string> RunewordNames(const std::string& text) {
	std::map<std::string, std::string> names;
	const auto value = [&](std::size_t from, std::size_t end, const char* field) {
		const std::string key = std::string("\"") + field + "\"";
		const auto        at  = text.find(key, from);
		if (at == std::string::npos || at > end) return std::string();
		auto open = text.find_first_not_of(" \t\r\n:", at + key.size());
		if (open == std::string::npos) return std::string();
		if (text[open] != '"') return text.substr(open, text.find_first_of(",}\r\n", open) - open); // a number
		std::string out;
		for (auto i = open + 1; i < text.size() && text[i] != '"'; ++i) {
			if (text[i] == '\\' && i + 1 < text.size()) {
				++i;
				out += text[i] == 'n' ? '\n' : text[i];
			} else out += text[i];
		}
		return out;
	};
	for (std::size_t open = text.find('{'); open != std::string::npos; open = text.find('{', open + 1)) {
		const auto end = text.find('}', open);
		if (end == std::string::npos) break;
		const auto key = value(open, end, "Key");
		if (key.empty()) continue; // every key: mods name runewords "Friendship", "Beast1"...
		const long  id   = std::strtol(value(open, end, "id").c_str(), nullptr, 10);
		std::string name = id > 0 && id <= 0xFFFF ? GameText(static_cast<std::uint16_t>(id)) : std::string();
		if (name.empty()) name = value(open, end, "enUS");
		if (!name.empty()) names[key] = name;
	}
	return names;
}

// Asset maps: "key" -> "asset" pairs from a json file like data/hd/items/items.json.
std::map<std::string, std::string> ParseAssets(const std::string& text) {
	std::map<std::string, std::string> assets;
	// Each entry is {"key": {"asset": "value", ...}} (items.json) or {"key":
	// {"normal": "value", "uber": ...}} (uniques.json, sets.json): take the
	// string before every "asset" or "normal" key's object as its key.
	const char* field = text.find("\"asset\"") != std::string::npos ? "\"asset\"" : "\"normal\"";
	for (std::size_t at = text.find(field); at != std::string::npos; at = text.find(field, at + 7)) {
		const std::size_t open  = text.rfind('{', at);
		const std::size_t close = open == std::string::npos ? std::string::npos : text.rfind('"', open);
		const std::size_t start = close == std::string::npos || close == 0 ? std::string::npos : text.rfind('"', close - 1);
		const std::size_t value = text.find('"', text.find(':', at) + 1);
		const std::size_t end   = value == std::string::npos ? std::string::npos : text.find('"', value + 1);
		if (start == std::string::npos || end == std::string::npos) continue;
		assets[text.substr(start + 1, close - start - 1)] = text.substr(value + 1, end - value - 1);
	}
	return assets;
}

// Where Properties keeps func1, stat1 and val1: found from rows every build
// has (ac, str, dmg%, res-all, ama, sor), not assumed.
struct PropLayout {
	int func = -1, stat = -1, value = -1;
};

PropLayout FindPropLayout(const unsigned char* rows, std::uint32_t count, std::uint32_t size) {
	PropLayout layout;
	if (count < 71 || size < 8) return layout;
	const auto row = [&](int r) { return rows + static_cast<std::size_t>(r) * size; };
	const auto u16 = [&](int r, int at) { return Field<std::uint16_t>(row(r), at); };
	for (int f = 0; f + 2 < static_cast<int>(size) && layout.func < 0; ++f)
		if (row(0)[f] == 1 && row(7)[f] == 1 && row(29)[f] == 7 && row(41)[f] == 1 && row(41)[f + 1] == 3 && row(67)[f] == 21 && row(70)[f] == 21) layout.func = f;
	for (int s = 0; s + 4 <= static_cast<int>(size) && layout.stat < 0; s += 2)
		if (u16(0, s) == 31 && u16(7, s) == 0 && u16(41, s) == 39 && u16(41, s + 2) == 41 && u16(67, s) == 83 && u16(70, s) == 83) layout.stat = s;
	for (int v = 0; v + 2 <= static_cast<int>(size) && layout.value < 0; v += 2)
		if (v != layout.stat && u16(67, v) == 0 && u16(70, v) == 1 && u16(0, v) == 0 && u16(7, v) == 0) layout.value = v;
	return layout;
}

// The compiled rows of the tables the lists come from.
struct TableRows {
	const unsigned char* rows {};
	std::uint32_t        count {};
};

// The art of uniques and set items: "index" -> "asset", keyed by Key(index).
struct ArtMaps {
	std::map<std::string, std::string> bases, uniques, sets;
};

// What decides a set's class besides a class item: the game's own class for
// the set (Sets UIClass) and class skills its items give (Properties, Skills).
struct SetTables {
	TableRows     sets;
	std::uint32_t setSize {};
	TableRows     props;
	std::uint32_t propSize {};
	TableRows     skills;
	std::uint32_t skillSize {};
	TableRows     runes;
	std::uint32_t runeSize {};
	std::map<std::string, std::string> typeNames; // item type names by code (itemtypes.txt's ItemType column), for the misc categories
	TableRows     runewordCategories; // RunewordUICategories
	std::uint32_t runewordCategorySize {};
	std::string (*keyText)(const char* key) {}; // a string by its key ("Runeword1")
	std::map<std::string, std::string> keyNames; // the runewords' names by key, from the game's strings file
};

// Where Runes keeps its fields: found from the first rows of the game's own
// table (Ancients' Pledge: shield, Ral Ort Tal, cold resist 30, all resists 13,
// defense 50%, damage to mana 10; Beast: axe or scepter, Ber Tir Um), not assumed.
struct RuneLayout {
	int name = -1, types = -1, runes = -1, props = -1;
	int complete = -1; // the "complete" byte, when the rows show which one it is
	bool byShape {}; // found from the rows' shape: the first rows were modded
	static constexpr int TypeCount = 6, ExcludedCount = 3, RuneCount = 6, PropCount = 7;
	bool found() const { return name >= 0 && types >= 0 && runes >= 0 && props >= 0; }
};
RuneLayout RuneFields; // found by BuildLists

RuneLayout FindRuneLayout(TableRows runes, std::uint32_t size, const std::map<std::string, int>& typeRow, const std::map<std::string, int>& itemRow) {
	RuneLayout layout;
	if (runes.count < 4 || size < 64) return layout;
	const auto* first = runes.rows;
	const auto* beast = runes.rows + static_cast<std::size_t>(3) * size;
	const auto  type  = [&](const char* code) { const auto t = typeRow.find(code); return t == typeRow.end() ? -1 : t->second; };
	const auto  item  = [&](const char* code) { const auto t = itemRow.find(code); return t == itemRow.end() ? -1 : t->second; };
	for (int o = 0; o + 10 <= static_cast<int>(size) && layout.name < 0; ++o)
		if (!std::memcmp(first + o, "Runeword1", 10) && !std::memcmp(beast + o, "Runeword4", 10)) layout.name = o;
	for (int o = 0; o + 4 <= static_cast<int>(size) && layout.types < 0; o += 2)
		if (Field<std::uint16_t>(first, o) == type("shld") && Field<std::uint16_t>(first, o + 2) == 0 && Field<std::uint16_t>(beast, o) == type("axe")
		    && Field<std::uint16_t>(beast, o + 2) == type("scep"))
			layout.types = o;
	for (int o = 0; o + 12 <= static_cast<int>(size) && layout.runes < 0; o += 4)
		if (Field<std::int32_t>(first, o) == item("r08") && Field<std::int32_t>(first, o + 4) == item("r09") && Field<std::int32_t>(first, o + 8) == item("r07")
		    && Field<std::int32_t>(beast, o) == item("r30") && Field<std::int32_t>(beast, o + 4) == item("r03") && Field<std::int32_t>(beast, o + 8) == item("r22"))
			layout.runes = o;
	for (int o = 0; o + 64 <= static_cast<int>(size) && layout.props < 0; o += 4)
		if (Field<std::int32_t>(first, o + 8) == 30 && Field<std::int32_t>(first, o + 12) == 30 && Field<std::int32_t>(first, o + 24) == 13 && Field<std::int32_t>(first, o + 28) == 13
		    && Field<std::int32_t>(first, o + 40) == 50 && Field<std::int32_t>(first, o + 44) == 50 && Field<std::int32_t>(first, o + 56) == 10 && Field<std::int32_t>(first, o + 60) == 10)
			layout.props = o;
	return layout;
}

// The same from the rows' shape alone, for a mod that changed the first rows
// (another recipe, other runes, other stats): the field that holds runes in
// most rows, the item types before it, the stats after it, the "Runeword"
// keys. `rune` tells an Items row that is a rune; `typeCount`, `propCount`
// the ItemTypes and Properties rows.
RuneLayout FindRuneLayoutByShape(TableRows runes, std::uint32_t size, const std::function<bool(int)>& rune, std::uint32_t typeCount, std::uint32_t propCount) {
	RuneLayout layout;
	if (runes.count < 4 || size < 64) return layout;
	const auto row  = [&](std::uint32_t k) { return runes.rows + static_cast<std::size_t>(k) * size; };
	const auto best = [](int& at, int score, int& top, int o) { if (score > top) top = score, at = o; };
	// The keys: "Runeword..." in most rows.
	for (int o = 0, top = 0; o + 9 <= static_cast<int>(size); ++o) {
		int score = 0;
		for (std::uint32_t k = 0; k < runes.count; ++k) score += !std::memcmp(row(k) + o, "Runeword", 8);
		if (score * 2 > static_cast<int>(runes.count)) best(layout.name, score, top, o);
		if (layout.name >= 0) break; // the first such field
	}
	// The runes: six i32 Items rows, runes first, then none (0 or -1).
	int runeRows = 0;
	for (int o = 0; o + 4 * RuneLayout::RuneCount <= static_cast<int>(size); o += 4) {
		int score = 0;
		for (std::uint32_t k = 0; k < runes.count; ++k) {
			int  count = 0;
			bool ok    = true, ended = false;
			for (int r = 0; r < RuneLayout::RuneCount && ok; ++r) {
				const auto value = Field<std::int32_t>(row(k), o + 4 * r);
				if (value <= 0) ended = true;
				else if (ended || !rune(value)) ok = false;
				else ++count;
			}
			score += ok && count > 0;
		}
		best(layout.runes, score, runeRows, o);
	}
	if (runeRows * 4 < static_cast<int>(runes.count)) return RuneLayout {};
	// The item types: nine u16 (six allowed, three excluded) before the runes,
	// every one an ItemTypes row or 0, the first one given wherever runes are.
	for (int o = layout.runes - 2 * (RuneLayout::TypeCount + RuneLayout::ExcludedCount), top = 0; o >= 0 && o + 2 * (RuneLayout::TypeCount + RuneLayout::ExcludedCount) <= layout.runes + 4; o -= 2) {
		int score = 0;
		for (std::uint32_t k = 0; k < runes.count; ++k) {
			if (Field<std::int32_t>(row(k), layout.runes) <= 0) continue;
			bool ok = Field<std::uint16_t>(row(k), o) != 0;
			for (int t = 0; t < RuneLayout::TypeCount + RuneLayout::ExcludedCount && ok; ++t) ok = Field<std::uint16_t>(row(k), o + 2 * t) < typeCount;
			score += ok;
		}
		best(layout.types, score, top, o);
	}
	// The stats: seven {code, parameter, min, max} of i32 after the runes.
	for (int o = layout.runes + 4 * RuneLayout::RuneCount, top = 0; o + 16 * RuneLayout::PropCount <= static_cast<int>(size) && o <= layout.runes + 4 * RuneLayout::RuneCount + 64; o += 4) {
		int score = 0;
		for (std::uint32_t k = 0; k < runes.count; ++k) {
			if (Field<std::int32_t>(row(k), layout.runes) <= 0) continue;
			const auto code = Field<std::int32_t>(row(k), o);
			bool       ok   = code >= 0 && static_cast<std::uint32_t>(code) < propCount;
			for (int p = 0; p < RuneLayout::PropCount && ok; ++p) {
				const auto c = Field<std::int32_t>(row(k), o + 16 * p);
				ok = c >= -1 && (c < 0 || static_cast<std::uint32_t>(c) < propCount);
			}
			score += ok;
		}
		best(layout.props, score, top, o);
	}
	// Keys that are not "Runeword..." (a mod naming its runewords "Friendship",
	// "Beast1"...): the first text field before the item types, filled in
	// nearly every row and different in most rows.
	if (layout.name < 0 && layout.types > 0) {
		const auto text = [&](const unsigned char* at, int room) {
			int n = 0;
			while (n < room && at[n] >= 0x20 && at[n] != 0x7F) ++n;
			return n >= 2 && n < room ? n : 0; // ends with a 0 before the room does
		};
		for (int o = 0; o < layout.types && layout.name < 0; ++o) {
			const int room = std::min(64, layout.types - o);
			int       given = 0;
			std::set<std::string> keys;
			for (std::uint32_t k = 0; k < runes.count; ++k)
				if (const int n = text(row(k) + o, room)) ++given, keys.emplace(reinterpret_cast<const char*>(row(k) + o), n);
			if (given * 10 >= static_cast<int>(runes.count) * 9 && keys.size() * 2 > runes.count) layout.name = o;
		}
	}
	if (layout.found()) layout.byShape = true;
	return layout;
}

// The "complete" byte: before the item types, 0 or 1 in every row, 1 in
// nearly every row with types and runes (a mod turns a runeword off with 0).
// The closest such byte to the types; -1 when no byte fits.
int FindRuneComplete(TableRows runes, std::uint32_t size, const RuneLayout& layout) {
	if (!layout.found()) return -1;
	const auto row = [&](std::uint32_t k) { return runes.rows + static_cast<std::size_t>(k) * size; };
	for (int o = layout.types - 1; o > layout.name + 8; --o) {
		bool binary = true;
		int  given = 0, on = 0;
		for (std::uint32_t k = 0; k < runes.count && binary; ++k) {
			const auto value = row(k)[o];
			binary = value <= 1;
			if (Field<std::uint16_t>(row(k), layout.types) && Field<std::int32_t>(row(k), layout.runes) > 0) ++given, on += value;
		}
		if (binary && given > 0 && on * 10 >= given * 9) return o;
	}
	return -1;
}

// Where Sets keeps UIClass, the class the game lists a set under: the byte that
// gives every set with a class item that item's class. The class may be kept
// from 0 or from 1, and counting PlayerClass's "Expansion" row or not, so all
// four are tried. Found from the rows, not assumed; offset -1 when not found.
struct ClassField {
	int  offset = -1, shift {};
	bool gap {}; // the Expansion row counts: druid, assassin and warlock are one higher
	int  Code(int c) const { return c + shift + (gap && c >= 5 ? 1 : 0); }
	int  Class(int value) const {
		for (int c = 0; c < ClassCount; ++c)
			if (Code(c) == value) return c;
		return -1;
	}
};

ClassField FindSetClass(const TableRows& sets, std::uint32_t size, const std::map<int, int>& known) {
	std::set<int> classes;
	for (const auto& [set, c] : known) classes.insert(c);
	if (!sets.rows || classes.size() < 3) return {};
	ClassField found;
	int        matches = 0;
	for (int f = 0; f < static_cast<int>(size); ++f)
		for (const int shift : { 0, 1 })
			for (const bool gap : { false, true }) {
				const ClassField field { f, shift, gap };
				const auto       at = [&](int r) { return static_cast<int>(sets.rows[static_cast<std::size_t>(r) * size + f]); };
				bool             ok = true;
				for (const auto& [set, c] : known)
					if (set < 0 || static_cast<std::uint32_t>(set) >= sets.count || at(set) != field.Code(c)) ok = false;
				// Every other set has a class or none (and some have none).
				int none = 0;
				for (std::uint32_t r = 0; ok && r < sets.count; ++r) {
					const int v = at(static_cast<int>(r));
					if (field.Class(v) >= 0) continue;
					if (shift == 1 ? v != 0 : v < field.Code(ClassCount - 1)) ok = false; // neither a class nor a "none" value
					++none;
				}
				if (ok && none > 0) {
					++matches;
					found = field;
				}
			}
	return matches == 1 ? found : ClassField {};
}

// Where Skills keeps charclass: the byte that gives skills every build has
// (Magic Arrow 6, Fire Bolt 36, Amplify Damage 66, Sacrifice 96, Bash 126,
// Raven 221, Fire Trauma 251) their class, and Attack (0) none.
int FindSkillClass(const TableRows& skills, std::uint32_t size) {
	constexpr int known[][2] { { 6, 0 }, { 36, 1 }, { 66, 2 }, { 96, 3 }, { 126, 4 }, { 221, 5 }, { 251, 6 } };
	if (!skills.rows || skills.count <= 251) return -1;
	for (int f = 0; f < static_cast<int>(size); ++f) {
		const auto at = [&](int r) { return static_cast<int>(skills.rows[static_cast<std::size_t>(r) * size + f]); };
		bool       ok = at(0) >= ClassCount;
		for (const auto& [skill, c] : known) ok = ok && at(skill) == c;
		if (ok) return f;
	}
	return -1;
}

// The class a prop's skills belong to, or -1: class skills (ama, sor...),
// a class skill tab, or a single skill of one class.
int PropClass(int prop, int par, const SetTables& tables, const PropLayout& layout, const std::vector<int>& skillClass) {
	if (prop < 0 || static_cast<std::uint32_t>(prop) >= tables.props.count || layout.stat < 0) return -1;
	const unsigned char* text = tables.props.rows + static_cast<std::size_t>(prop) * tables.propSize;
	const int            stat = Field<std::uint16_t>(text, layout.stat);
	int                  c    = -1;
	if (stat == 83 && layout.value >= 0) c = Field<std::uint16_t>(text, layout.value); // item_addclassskills: the class is its value
	else if (stat == 188) c = par / 3;                                                 // item_addskill_tab: three tabs per class
	else if (stat == 107 && par >= 0 && par < static_cast<int>(skillClass.size())) c = skillClass[par]; // item_singleskill
	return c >= 0 && c < ClassCount ? c : -1;
}

// Builds the lists from the compiled rows. Kept apart from the table service so
// the test harness can feed it the dumped rows.
std::map<std::string, int> EntryById; // UniqueEntry::id -> index in Uniques; guarded by Lock
void                       RebuildStored();

// The misc categories (one per item type with misc bases) and which the settings track. Lock held.
struct MiscCategory {
	int         category {};
	std::string type;
	int         count {};
};
std::vector<MiscCategory>  MiscCategories;
std::map<std::string, int> MiscByCode; // a misc base's entry
enum MiscModeKind { MiscOff, MiscCheck, MiscItems };
std::map<std::string, int> MiscMode; // item type code -> MiscCheck (settings: misc_checkbox) or MiscItems (misc_tracked); absent: not tracked
std::set<std::string>      CheckedIds; // checkbox entries ticked ("m:<code>"), kept in the grail file's found list as "c|<id>"
int MiscModeOf(const std::string& type) {
	const auto mode = MiscMode.find(type);
	return mode == MiscMode.end() ? MiscOff : mode->second;
}
// The player's own objectives (settings: objective = id|icon|text), a
// checkbox tile each in the Objectives menu, ticked like a misc checkbox ("o:<id>").
struct Objective {
	int         id {}, icon {};
	std::string text;
	bool        enabled = true; // off: no tile, not counted (the settings line ends with |off)
};
std::vector<Objective> Objectives;
int                    NextObjective = 1, ObjCategory = -1;
// Their icons (game-icons.net), registered as "objicon<n>" at two cells.
// The objectives' own icons, then every other icon of the plugin's (menus, categories, classes).
constexpr int ObjOwnIcons = static_cast<int>(std::size(ObjIconMasks)), ObjectiveIconCount = ObjOwnIcons + static_cast<int>(std::size(IconMasks)), ObjTileIcon = Cell * 60 / 100;
// The icon picker: icons in rows of PickColumns, on a box of PickWidth x PickHeight.
constexpr int PickCell = 96, PickGap = 10, PickColumns = 10, PickIcon = 76, PickRows = (ObjectiveIconCount + PickColumns - 1) / PickColumns;
constexpr int PickWidth = PickColumns * PickCell + (PickColumns - 1) * PickGap + 80, PickHeight = PickRows * PickCell + (PickRows - 1) * PickGap + 150;
constexpr int AddWidth = 56; // the Add button: the loot filter's square + button, at the drop-down's height
// A tile ticked by a click instead of holding an item.
bool IsCheckbox(const UniqueEntry& entry) { return entry.objective || (entry.misc && MiscModeOf(entry.miscType) == MiscCheck); }
std::vector<char>          CategoryOff; // a misc category not tracked: no tiles
std::vector<std::array<char, 2>> CategoryHas; // whether a category has plain / ethereal tiles
bool HasTile(int u, bool eth);
void UpdateMiscTracking() {
	CategoryOff.assign(Categories.size(), 0);
	for (const auto& misc : MiscCategories)
		if (misc.category >= 0 && misc.category < static_cast<int>(CategoryOff.size())) CategoryOff[misc.category] = MiscModeOf(misc.type) == MiscOff;
	CategoryHas.assign(Categories.size(), { 0, 0 });
	for (int u = 0; u < static_cast<int>(Uniques.size()); ++u)
		if (const int c = Uniques[u].category; c >= 0 && c < static_cast<int>(CategoryHas.size()))
			for (const bool eth : { false, true })
				if (HasTile(u, eth)) CategoryHas[c][eth] = 1;
}

// A misc category's name in the texts' language: the words its items' names
// all start or end with ("Rune" of "El Rune", "Eld Rune"...; "Topaz" of
// "Chipped Topaz", "Topaz"...), else the game's text for its type name, else
// the type name.
std::string Tr(const std::string& english, int language);
int         UiLanguage();
std::string MiscCategoryName(const std::string& typeName, const std::vector<std::string>& itemNames) {
	const auto words = [](const std::string& text) {
		std::vector<std::string> out;
		std::string              word;
		for (const char c : text + " ")
			if (c == ' ') {
				if (!word.empty()) out.push_back(word);
				word.clear();
			} else word += c;
		return out;
	};
	std::set<std::string> distinct(itemNames.begin(), itemNames.end());
	if (distinct.size() >= 2) {
		std::vector<std::vector<std::string>> all;
		for (const auto& name : distinct) all.push_back(words(name));
		const auto common = [&](bool fromEnd) {
			std::vector<std::string> out;
			for (std::size_t k = 0;; ++k) {
				std::string word;
				for (const auto& name : all) {
					if (k >= name.size()) return out;
					const auto& w = fromEnd ? name[name.size() - 1 - k] : name[k];
					if (word.empty()) word = w;
					else if (w != word) return out;
				}
				fromEnd ? (void)out.insert(out.begin(), word) : out.push_back(word);
			}
		};
		auto        front = common(false), back = common(true);
		const auto& pick  = back.size() > front.size() ? back : front;
		if (!pick.empty()) {
			std::string out;
			for (const auto& w : pick) out += (out.empty() ? "" : " ") + w;
			if (!out.empty() && out[0] >= 'a' && out[0] <= 'z') out[0] = static_cast<char>(out[0] - 'a' + 'A');
			else if (out.size() > 1 && static_cast<unsigned char>(out[0]) == 0xC3 && static_cast<unsigned char>(out[1]) >= 0xA0 && static_cast<unsigned char>(out[1]) <= 0xBE
			         && static_cast<unsigned char>(out[1]) != 0xB7)
				out[1] = static_cast<char>(static_cast<unsigned char>(out[1]) - 0x20); // é -> É
			return out;
		}
	}
	const int language = UiLanguage();
	if (language > 0 && !typeName.empty())
		if (const auto text = Tr(typeName, language); !text.empty() && text != typeName) return text;
	return typeName;
}

void BuildObjectives();
bool RegisterStrings();
std::string ObjTypedText();
std::string TypedText(int target);
extern std::atomic<bool> ObjTyping;
extern std::atomic<int>  TypeTarget; // what the keys go to while typing: 0 the objectives' box, 1 the search bar
constexpr int            TypeObjective = 0, TypeSearch = 1;
void BuildLists(TableRows types, TableRows items, TableRows uniques, TableRows sets, NameLookup names, const ArtMaps& mod, const SetTables& setTables = {}) {
	std::vector<std::string> typeCodes(types.count);
	for (std::uint32_t t = 0; t < types.count; ++t) typeCodes[t] = FourCC(types.rows + t * TypeRowSize, TypeRow::Code);
	// Every type of a base, with its parents.
	const auto lineage = [&](std::uint16_t first, std::uint16_t second) {
		std::vector<std::string>   found;
		std::vector<std::uint16_t> todo { first, second };
		std::set<std::uint16_t>    seen;
		while (!todo.empty()) {
			const auto t = todo.back();
			todo.pop_back();
			if (!t || t >= types.count || !seen.insert(t).second) continue;
			found.push_back(typeCodes[t]);
			todo.push_back(Field<std::uint16_t>(types.rows + t * TypeRowSize, TypeRow::Equiv2));
			todo.push_back(Field<std::uint16_t>(types.rows + t * TypeRowSize, TypeRow::Equiv1));
		}
		return found;
	};
	std::map<std::string, std::string> builtin;
	for (const auto& entry : BaseAssets) builtin[entry.key] = entry.asset;
	const auto folder = [](int kind) { return std::string("items/") + (kind == 0 ? "weapon/" : kind == 1 ? "armor/" : "misc/"); };
	// The gemsockets column: the byte where these bases (vanilla values) all
	// have theirs, so a modded layout is found too.
	int socketsAt = -1;
	{
		const std::pair<const char*, int> known[] { { "lgl", 0 }, { "wnd", 1 }, { "hax", 2 }, { "gsc", 3 }, { "axe", 4 }, { "2ax", 5 }, { "mpi", 6 }, { "9mt", 3 } };
		std::map<std::string, const unsigned char*> byCode;
		for (std::uint32_t i = 0; i < items.count; ++i) byCode[FourCC(items.rows + static_cast<std::size_t>(i) * ItemRowSize, ItemRow::Code)] = items.rows + static_cast<std::size_t>(i) * ItemRowSize;
		for (int at = 132; at < static_cast<int>(ItemRowSize) && socketsAt < 0; ++at) {
			bool all = true;
			for (const auto& [code, sockets] : known)
				if (const auto row = byCode.find(code); row == byCode.end() || row->second[at] != sockets) all = false;
			if (all) socketsAt = at;
		}
		if (socketsAt < 0) Say(true, "grailon: the Items sockets column was not found; socket rolls are taken as listed");
	}
	Bases.clear();
	for (std::uint32_t i = 0; i < items.count; ++i) {
		const unsigned char* row  = items.rows + static_cast<std::size_t>(i) * ItemRowSize;
		const std::string    code = FourCC(row, ItemRow::Code);
		if (code.empty()) continue;
		BaseInfo   base;
		base.row         = static_cast<int>(i);
		const auto found = lineage(Field<std::uint16_t>(row, ItemRow::Type), Field<std::uint16_t>(row, ItemRow::Type2));
		const auto has   = [&](const char* type) { return std::find(found.begin(), found.end(), type) != found.end(); };
		base.kind        = has("weap") ? 0 : has("armo") ? 1 : 2;
		base.width       = std::max<int>(1, row[ItemRow::Width]);
		base.height      = std::max<int>(1, row[ItemRow::Height]);
		base.durable     = !row[ItemRow::NoDurability];
		base.quest       = row[ItemRow::Quest] != 0;
		base.levelReq    = Field<std::uint8_t>(row, ItemRow::LevelReq);
		base.maxDefense  = base.kind == 1 ? static_cast<int>(Field<std::uint32_t>(row, ItemRow::MaxDefense)) : 0;
		base.colorClass  = row[ItemRow::InvTrans];
		base.dropSound   = Field<std::uint16_t>(row, ItemRow::DropSound);
		base.sockets     = socketsAt >= 0 ? row[socketsAt] : -1;
		base.types       = found;
		if (const auto own = Field<std::uint16_t>(row, ItemRow::Type); own && own < types.count) base.type = typeCodes[own];
		base.category    = OtherRule;
		for (const char* type : RulePriority)
			if (has(type)) {
				for (int r = 0; r < OtherRule; ++r)
					if (!std::strcmp(CategoryRules[r].type, type)) base.category = r;
				break;
			}
		for (int r = 0; r < FirstTierRule; ++r)
			if (has(SetRules[r].type)) base.classRule = r;
		if (code == FourCC(row, ItemRow::EliteCode) && code != FourCC(row, ItemRow::NormalCode)) base.tier = 2;
		else if (code == FourCC(row, ItemRow::ExceptionalCode) && code != FourCC(row, ItemRow::NormalCode)) base.tier = 1;
		base.name = names(Field<std::uint16_t>(row, ItemRow::NameString));
		if (base.name.empty()) base.name = code;
		const auto asset = mod.bases.count(code) ? mod.bases.at(code) : builtin.count(code) ? builtin[code] : "";
		if (!asset.empty()) base.asset = folder(base.kind) + asset;
		Bases[code] = base;
	}
	std::map<std::string, std::string> uniqueArt;
	for (const auto& entry : UniqueAssets) uniqueArt[entry.key] = entry.asset;
	std::vector<UniqueEntry> found;
	std::vector<std::pair<std::string, std::string>> aliases; // older runeword ids -> the ids of the tiles that took them over
	std::vector<int>         rule; // index in CategoryRules, then SetRules
	// Ethereal rules from the unique's own properties, by the game's property
	// functions (so they hold for modded tables): an "ethereal" unique always
	// is, any other indestructible one never is.
	const PropLayout propLayout = FindPropLayout(setTables.props.rows, setTables.props.count, setTables.propSize);
	const auto       hasFunc    = [&](const unsigned char* row, int first, int count, int func) {
        if (propLayout.func < 0) return false;
        for (int p = 0; p < count; ++p) {
            const auto prop = Field<std::int32_t>(row, first + p * 16);
            if (prop < 0 || static_cast<std::uint32_t>(prop) >= setTables.props.count) continue;
            if (setTables.props.rows[static_cast<std::size_t>(prop) * setTables.propSize + propLayout.func] == func) return true;
        }
        return false;
	};
	// An entry of the grail for row `k` of a table: its base and art.
	const auto add = [&](const unsigned char* row, std::uint32_t k, int indexAt, int nameAt, int codeAt, const std::map<std::string, std::string>& ownArt, bool set) -> UniqueEntry* {
		const int colorAt = set ? SetRow::InvTransform : UniqueRow::InvTransform;
		const auto base = Bases.find(FourCC(row, codeAt));
		if (base == Bases.end() || base->second.quest) return nullptr;
		const std::string index(reinterpret_cast<const char*>(row + indexAt), strnlen(reinterpret_cast<const char*>(row + indexAt), UniqueRow::IndexLength));
		UniqueEntry entry;
		entry.row    = static_cast<int>(k);
		entry.id     = (set ? "s:" : "u:") + index;
		entry.set    = set;
		entry.kind   = base->second.kind;
		entry.width  = base->second.width;
		entry.height = base->second.height;
		entry.eth    = !set && entry.kind < 2 && base->second.durable; // unique weapons and armor with durability; set items never are
		entry.name   = names(Field<std::uint16_t>(row, nameAt));
		if (entry.name.empty()) entry.name = index;
		entry.base  = base->second.name;
		entry.code  = base->first;
		entry.ghost = base->second.asset;
		entry.color      = row[colorAt] == 255 ? -1 : row[colorAt];
		entry.colorClass = base->second.colorClass;
		// Its own art: the mod's uniques.json / sets.json, the built-in list, or its base's.
		if (const auto own = ownArt.find(Key(index)); own != ownArt.end()) entry.art = folder(entry.kind) + own->second;
		else if (const auto known = uniqueArt.find(Key(index)); !set && known != uniqueArt.end()) entry.art = known->second;
		else entry.art = entry.ghost;
		found.push_back(entry);
		rule.push_back(base->second.category);
		return &found.back();
	};
	for (std::uint32_t k = 0; k < uniques.count; ++k) {
		const unsigned char* row = uniques.rows + static_cast<std::size_t>(k) * UniqueRowSize;
		if (!(Field<std::uint32_t>(row, UniqueRow::Flags) & UniqueRow::Spawnable)) continue;
		auto* entry = add(row, k, UniqueRow::Index, UniqueRow::NameString, UniqueRow::Code, mod.uniques, false);
		if (!entry || !entry->eth) continue;
		// "ethereal" first: Ethereal Edge is both, and always ethereal.
		if (hasFunc(row, UniqueRow::Props, UniqueRow::PropCount, EtherealFunc)) entry->onlyEth = true;
		else if (hasFunc(row, UniqueRow::Props, UniqueRow::PropCount, IndestructibleFunc)) entry->eth = false;
	}
	// Set items, with their set's category: the class of a class item in it,
	// else the class the game lists the set under (Sets UIClass), else the class
	// whose skills its items give most, else the best tier of its bases.
	const std::size_t firstSet = found.size();
	std::map<int, int> setRule, classItem; // set -> rule; set -> class of its class item
	std::map<int, std::array<int, ClassCount>> skillVotes;
	const PropLayout   layout = FindPropLayout(setTables.props.rows, setTables.props.count, setTables.propSize);
	std::vector<int>   skillClass;
	if (const int at = FindSkillClass(setTables.skills, setTables.skillSize); at >= 0)
		for (std::uint32_t k = 0; k < setTables.skills.count; ++k) {
			const int c = setTables.skills.rows[static_cast<std::size_t>(k) * setTables.skillSize + at];
			skillClass.push_back(c < ClassCount ? c : -1);
		}
	for (std::uint32_t k = 0; k < sets.count; ++k) {
		const unsigned char* row = sets.rows + static_cast<std::size_t>(k) * SetRowSize;
		if (!(Field<std::uint32_t>(row, SetRow::Flags) & UniqueRow::Spawnable)) continue;
		auto* entry = add(row, k, SetRow::Index, SetRow::NameString, SetRow::Code, mod.sets, true);
		if (!entry) continue;
		entry->setId    = Field<std::uint16_t>(row, SetRow::Set);
		entry->setIndex = Field<std::uint16_t>(row, SetRow::SetIndex);
		const auto& base = Bases[entry->code];
		auto&       best = setRule.try_emplace(entry->setId, FirstTierRule).first->second;
		if (base.classRule >= 0) {
			best = base.classRule;
			for (int c = 0; c < ClassCount; ++c)
				if (ClassRule[c] == base.classRule) classItem[entry->setId] = c;
		} else if (best >= FirstTierRule) best = std::max(best, FirstTierRule + base.tier);
		// Class skills on the item, alone or with more of the set.
		auto& votes = skillVotes.try_emplace(entry->setId).first->second;
		for (const auto& [first, count] : { std::pair { SetRow::Props, SetRow::PropCount }, std::pair { SetRow::BonusProps, SetRow::BonusPropCount } })
			for (int p = 0; p < count; ++p) {
				const int at = first + p * 16;
				if (const int c = PropClass(Field<std::int32_t>(row, at), Field<std::int32_t>(row, at + 4), setTables, layout, skillClass); c >= 0) ++votes[c];
			}
	}
	const ClassField uiClass = FindSetClass(setTables.sets, setTables.setSize, classItem);
	for (auto& [set, rule] : setRule) {
		if (classItem.count(set)) continue;
		int c = -1;
		if (uiClass.offset >= 0 && set >= 0 && static_cast<std::uint32_t>(set) < setTables.sets.count) {
			c = uiClass.Class(setTables.sets.rows[static_cast<std::size_t>(set) * setTables.setSize + uiClass.offset]);
		}
		if (c < 0)
			if (const auto& votes = skillVotes[set]; *std::max_element(votes.begin(), votes.end()) > 0) c = static_cast<int>(std::max_element(votes.begin(), votes.end()) - votes.begin());
		if (c >= 0) rule = ClassRule[c];
	}
	SetClassFound = uiClass.offset >= 0;
	SetNames.clear();
	for (const auto& [set, rule] : setRule)
		if (set >= 0 && static_cast<std::uint32_t>(set) < setTables.sets.count && setTables.setSize > SetsRow::NameString + 2)
			SetNames[set] = names(Field<std::uint16_t>(setTables.sets.rows + static_cast<std::size_t>(set) * setTables.setSize, SetsRow::NameString));
	for (std::size_t i = firstSet; i < found.size(); ++i) rule[i] = static_cast<int>(std::size(CategoryRules)) + setRule[found[i].setId];
	// Runewords: the complete ones (item types and runes given), in a tile as
	// big as the biggest base they go in, by the kind of their first type.
	const int firstRunewordRule = static_cast<int>(std::size(CategoryRules) + std::size(SetRules));
	// The game's runeword categories (the Chronicle's): RunewordUICategories
	// names, given to item types by ItemTypes' RunewordCategory1/2 (bytes
	// 228/229, 0xFF for none). The plugin's own rules when they cannot be read.
	RunewordRules.assign(std::begin(BaseRunewordRules), std::end(BaseRunewordRules));
	RunewordRuleOrder = { AnyWeaponRule, 0, 1, 2, 3, 4, 5, 6, 7, OtherRunewordRule };
	TypeRunewordRules.clear();
	{
		std::vector<std::string> names;
		const std::uint32_t      size = setTables.runewordCategorySize;
		for (std::uint32_t c = 0; c < setTables.runewordCategories.count && c < 0xFF && size >= 2 && size <= 256; ++c) {
			const unsigned char* row = setTables.runewordCategories.rows + static_cast<std::size_t>(c) * size;
			// Its name: the first run of at least two name characters.
			std::string name;
			for (std::uint32_t at = 0; at < size && name.size() < 2;) {
				name.clear();
				while (at < size && (std::isalnum(row[at]) || row[at] == '_')) name += static_cast<char>(row[at++]);
				if (name.size() < 2) ++at;
			}
			names.push_back(name.size() >= 2 ? name : std::string());
		}
		bool ok = !names.empty() && std::none_of(names.begin(), names.end(), [](const std::string& name) { return name.empty(); }), any = false;
		for (std::uint32_t t = 0; ok && t < types.count; ++t)
			for (const int at : { TypeRow::RunewordCategory1, TypeRow::RunewordCategory2 }) {
				const auto c = types.rows[static_cast<std::size_t>(t) * TypeRowSize + at];
				if (c != 0xFF && c >= names.size()) ok = false;
				any = any || c != 0xFF;
			}
		if (ok && any) {
			std::vector<int> ruleOf(names.size(), -1);
			for (std::size_t c = 0; c < names.size(); ++c) {
				for (int k = 0; k < static_cast<int>(std::size(GameRunewordCategories)); ++k)
					if (!_stricmp(names[c].c_str(), GameRunewordCategories[k])) ruleOf[c] = k;
				if (ruleOf[c] >= 0) continue;
				// A mod's category: its name from the game's strings, the icon of its first item type that has one.
				RunewordRule added { GameKeyText(names[c]), "", "other", names[c] };
				if (added.name.empty()) added.name = names[c];
				for (std::uint32_t t = 0; t < types.count && added.icon == "other"; ++t) {
					const unsigned char* row = types.rows + static_cast<std::size_t>(t) * TypeRowSize;
					if (row[TypeRow::RunewordCategory1] != c && row[TypeRow::RunewordCategory2] != c) continue;
					for (const auto& icon : IconMasks)
						if (typeCodes[t] == icon.name) added.icon = icon.name;
				}
				ruleOf[c] = static_cast<int>(RunewordRules.size());
				RunewordRules.push_back(added);
				RunewordRuleOrder.insert(RunewordRuleOrder.end() - 1, ruleOf[c]);
			}
			for (std::uint32_t t = 0; t < types.count; ++t) {
				if (typeCodes[t].empty()) continue;
				std::vector<int> rules;
				// Only a mod's own categories: the game's five keep the plugin's
				// rules (the same tiles as before, with the game's odd ones such as
				// bows as melee weapons and voodoo heads as body armor left out).
				for (const int at : { TypeRow::RunewordCategory1, TypeRow::RunewordCategory2 })
					if (const auto c = types.rows[static_cast<std::size_t>(t) * TypeRowSize + at];
					    c != 0xFF && ruleOf[c] >= static_cast<int>(std::size(BaseRunewordRules)) && std::find(rules.begin(), rules.end(), ruleOf[c]) == rules.end())
						rules.push_back(ruleOf[c]);
				if (!rules.empty()) TypeRunewordRules.try_emplace(typeCodes[t], rules);
			}
			std::string list;
			for (const auto& name : names) list += (list.empty() ? "" : ", ") + name;
			Say(false, "grailon: runeword categories from the game: %s (%zu item types have one a mod added)", list.c_str(), TypeRunewordRules.size());
		} else Say(false, "grailon: the game's runeword categories could not be read (%u rows of %u bytes); the plugin's own are used", setTables.runewordCategories.count, size);
	}
	{
		std::map<std::string, int> typeRow, itemRow;
		for (std::uint32_t t = 0; t < types.count; ++t)
			if (!typeCodes[t].empty()) typeRow.try_emplace(typeCodes[t], static_cast<int>(t));
		std::vector<std::string> itemCodes(items.count);
		for (std::uint32_t i = 0; i < items.count; ++i) {
			itemCodes[i] = FourCC(items.rows + static_cast<std::size_t>(i) * ItemRowSize, ItemRow::Code);
			if (!itemCodes[i].empty()) itemRow.try_emplace(itemCodes[i], static_cast<int>(i));
		}
		RuneFields = FindRuneLayout(setTables.runes, setTables.runeSize, typeRow, itemRow);
		if (!RuneFields.found()) {
			const auto isRune = [&](int item) {
				if (item <= 0 || static_cast<std::uint32_t>(item) >= items.count) return false;
				const auto base = Bases.find(itemCodes[item]);
				return base != Bases.end() && std::find(base->second.types.begin(), base->second.types.end(), "rune") != base->second.types.end();
			};
			RuneFields = FindRuneLayoutByShape(setTables.runes, setTables.runeSize, isRune, types.count, setTables.props.count);
		}
		RuneFields.complete = FindRuneComplete(setTables.runes, setTables.runeSize, RuneFields);
		for (std::uint32_t k = 0; RuneFields.found() && k < setTables.runes.count; ++k) {
			const unsigned char* row = setTables.runes.rows + static_cast<std::size_t>(k) * setTables.runeSize;
			UniqueEntry          entry;
			entry.row      = static_cast<int>(k);
			entry.runeword = true;
			for (int t = 0; t < RuneLayout::TypeCount + RuneLayout::ExcludedCount; ++t) {
				const auto type = Field<std::uint16_t>(row, RuneFields.types + 2 * t);
				if (type && type < types.count) (t < RuneLayout::TypeCount ? entry.types : entry.excluded).push_back(typeCodes[type]);
			}
			std::string runeNames;
			for (int r = 0; r < RuneLayout::RuneCount; ++r) {
				const auto rune = Field<std::int32_t>(row, RuneFields.runes + 4 * r);
				if (rune <= 0 || static_cast<std::uint32_t>(rune) >= items.count) continue;
				const auto base = Bases.find(itemCodes[rune]);
				if (base == Bases.end() || std::find(base->second.types.begin(), base->second.types.end(), "rune") == base->second.types.end()) continue;
				entry.runes.push_back(base->first);
				entry.level = std::max(entry.level, base->second.levelReq);
				std::string name = base->second.name;
				if (const auto cut = name.rfind(' '); cut != std::string::npos && name.size() > 5) name.resize(cut); // "Ral Rune" -> "Ral"
				runeNames += (runeNames.empty() ? "" : " ") + name;
			}
			if (entry.types.empty() || entry.runes.empty()) continue; // not in the game
			if (RuneFields.complete >= 0 && !row[RuneFields.complete]) continue; // turned off
			const char* key = reinterpret_cast<const char*>(row + RuneFields.name);
			const std::string index(key, strnlen(key, 64));
			if (const auto known = setTables.keyNames.find(index); known != setTables.keyNames.end()) entry.name = known->second;
			if (entry.name.empty() && setTables.keyText) entry.name = setTables.keyText(index.c_str());
			if (entry.name.empty()) entry.name = index;
			entry.base = runeNames;
			// Its categories: those of its item types (or of their nearest parent with one).
			std::vector<int> wordRules;
			for (const auto& type : entry.types) {
				const auto row = typeRow.find(type);
				if (row == typeRow.end()) continue;
				for (const auto& code : lineage(static_cast<std::uint16_t>(row->second), 0))
					if (const auto rules = TypeRunewordRules.find(code); rules != TypeRunewordRules.end()) {
						for (const int r : rules->second)
							if (std::find(wordRules.begin(), wordRules.end(), r) == wordRules.end()) wordRules.push_back(r);
						break;
					}
			}
			// The bases it goes in, by kind (RunewordRules): a tile per kind
			// (Dream as a helm and as a shield), as big as its biggest base.
			std::map<int, UniqueEntry> kinds;
			std::map<int, int>         baseCount; // bases per kind
			for (const auto& [code, base] : Bases) {
				if (base.quest || base.kind == 2) continue;
				const auto has = [&](const std::vector<std::string>& list) {
					return std::any_of(list.begin(), list.end(), [&](const std::string& type) { return std::find(base.types.begin(), base.types.end(), type) != base.types.end(); });
				};
				if (!has(entry.types) || has(entry.excluded) || has({ "tpot" })) continue; // throwing potions take no runes
				// The base's own category among the runeword's (a bow takes Harmony as a missile weapon).
				int which = -1;
				for (const auto& type : base.types) {
					if (const auto rules = TypeRunewordRules.find(type); rules != TypeRunewordRules.end())
						for (const int r : rules->second)
							if (which < 0 && std::find(wordRules.begin(), wordRules.end(), r) != wordRules.end()) which = r;
					if (which >= 0) break;
				}
				if (which < 0) which = RunewordRuleOf(base);
				++baseCount[which];
				auto [kind, added] = kinds.try_emplace(which, entry);
				auto& split = kind->second;
				if (added) split.width = split.height = 1, split.rule = which;
				split.width  = std::max(split.width, base.width);
				split.height = std::max(split.height, base.height);
				split.kind   = base.kind;
			}
			// Melee and missile weapons both: one Weapons tile.
			if (kinds.count(0) && kinds.count(1)) {
				auto merged = kinds[0];
				const auto& missile = kinds[1];
				merged.width  = std::max(merged.width, missile.width);
				merged.height = std::max(merged.height, missile.height);
				merged.rule   = AnyWeaponRule;
				kinds.erase(0), kinds.erase(1);
				kinds[AnyWeaponRule] = merged;
				// Records made before keep their tile: the melee and missile ids lead to it.
				const std::string ids[] { "r:" + index + "/" + RunewordRules[0].icon, "r:" + index + "/" + RunewordRules[1].icon };
				for (const auto& id : ids) aliases.push_back({ id, "r:" + index + (kinds.size() > 1 ? std::string("/") + RunewordRules[AnyWeaponRule].icon : std::string()) });
			}
			// Ids stay as they were for a runeword of one kind.
			for (auto& [which, split] : kinds) {
				split.id = "r:" + index + (kinds.size() > 1 ? std::string("/") + RunewordRules[which].icon : std::string());
				// A key a mod gives twice (Doom1 for staves and for melee weapons): told apart by kind, else by row.
				const auto used = [&](const std::string& id) { return std::any_of(found.begin(), found.end(), [&](const UniqueEntry& e) { return e.id == id; }); };
				if (used(split.id) && kinds.size() == 1) split.id += std::string("/") + RunewordRules[which].icon;
				if (used(split.id)) split.id += "#" + std::to_string(k);
				found.push_back(split);
				rule.push_back(firstRunewordRule + which);
			}
			// Split in kinds (a mod gave some of its bases a category of their
			// own): records of the one tile it had lead to the kind with most bases.
			if (kinds.size() > 1) {
				int most = kinds.begin()->first;
				for (const auto& [which, count] : baseCount)
					if (kinds.count(which) && count > baseCount[most]) most = which;
				aliases.push_back({ "r:" + index, kinds[most].id });
			}
		}
	}
	// Only the categories with items get a button, in the tracker's order.
	Categories.clear();
	const int        ruleCount = firstRunewordRule + static_cast<int>(RunewordRules.size());
	std::vector<int> categoryOf(ruleCount, -1);
	std::vector<int> order;
	for (int r = 0; r < firstRunewordRule; ++r) order.push_back(r);
	for (const int r : RunewordRuleOrder) order.push_back(firstRunewordRule + r);
	for (const int r : order)
		for (std::size_t i = 0; i < found.size(); ++i)
			if (rule[i] == r) {
				categoryOf[r] = static_cast<int>(Categories.size());
				if (r >= firstRunewordRule) {
					const auto&   def = RunewordRules[r - firstRunewordRule];
					CategoryEntry category { RunewordMenu, "Runewords", def.name };
					category.icon = std::string("d2rloader\\grail-on\\icon-") + def.icon;
					Categories.push_back(category);
					break;
				}
				const bool  set = r >= static_cast<int>(std::size(CategoryRules));
				const auto& def = set ? SetRules[r - std::size(CategoryRules)] : CategoryRules[r];
				CategoryEntry category { set ? SetMenu : !std::strcmp(def.group, "Weapons") ? WeaponMenu : ArmorMenu, def.group, def.name };
				// Its icon: the class's for class sets (in SetRules order), the tier's, else the type's.
				if (set && r - static_cast<int>(std::size(CategoryRules)) < FirstTierRule) category.icon = "d2rloader\\grail-on\\icon-cls" + std::to_string(r - std::size(CategoryRules));
				else if (set) category.icon = "d2rloader\\grail-on\\icon-tier" + std::to_string(r - std::size(CategoryRules) - FirstTierRule);
				else category.icon = std::string("d2rloader\\grail-on\\icon-") + (*def.type ? def.type : "other");
				Categories.push_back(category);
				break;
			}
	for (std::size_t i = 0; i < found.size(); ++i) found[i].category = categoryOf[rule[i]];
	// By category; sets by set, then their order in it; uniques by row.
	std::stable_sort(found.begin(), found.end(), [](const UniqueEntry& a, const UniqueEntry& b) {
		if (a.category != b.category) return a.category < b.category;
		if (a.set != b.set) return !a.set;
		// Runewords by socket count, then required level, then name.
		if (a.runeword && b.runeword) {
			if (a.runes.size() != b.runes.size()) return a.runes.size() < b.runes.size();
			if (a.level != b.level) return a.level < b.level;
			return a.name < b.name;
		}
		return a.set && (a.setId != b.setId ? a.setId < b.setId : a.setIndex < b.setIndex);
	});
	// Built again (another language): each entry keeps its index, so what
	// points at one (stored items, the open view) stays right.
	// (The Standard and the misc entries come after these, built the same way each time.)
	if (!Uniques.empty() && Uniques.size() >= found.size()) {
		std::map<std::string, std::size_t> before;
		for (std::size_t i = 0; i < Uniques.size(); ++i) before[Uniques[i].id] = i;
		if (std::all_of(found.begin(), found.end(), [&](const UniqueEntry& entry) { return before.count(entry.id); }))
			std::stable_sort(found.begin(), found.end(), [&](const UniqueEntry& a, const UniqueEntry& b) { return before[a.id] < before[b.id]; });
	}
	Uniques = std::move(found);
	UniqueByRow.assign(uniques.count, -1);
	SetByRow.assign(sets.count, -1);
	RunewordByRow.assign(setTables.runes.count, -1);
	RunewordIds.clear();
	for (int u = 0; u < UniqueCount(); ++u) (Uniques[u].runeword ? RunewordByRow : Uniques[u].set ? SetByRow : UniqueByRow)[Uniques[u].row] = u;
	// The Standard of Heroes: one more tile, at the bottom of the stats overview.
	SecretEntry = -1;
	if (const auto standard = Bases.find("std"); standard != Bases.end()) {
		UniqueEntry entry;
		entry.category = StatsView;
		entry.row      = -1;
		entry.secret   = true;
		entry.id       = "x:std";
		entry.kind     = 2;
		entry.width    = standard->second.width;
		entry.height   = standard->second.height;
		entry.name = entry.base = standard->second.name;
		entry.code              = standard->first;
		entry.ghost = entry.art = standard->second.asset;
		entry.colorClass        = standard->second.colorClass;
		Uniques.push_back(entry);
		SecretEntry = UniqueCount() - 1;
	}
	// Misc bases, by their own item type: a category each in the Misc menu,
	// shown when the settings track that type (all built, so indices never
	// change with the settings). Not the Standard, nor gold.
	MiscByCode.clear();
	MiscCategories.clear();
	{
		std::map<std::string, std::vector<const std::pair<const std::string, BaseInfo>*>> byType;
		std::vector<std::string>                                                         typeOrder;
		std::vector<const std::pair<const std::string, BaseInfo>*>                       bases;
		for (const auto& base : Bases) bases.push_back(&base);
		std::sort(bases.begin(), bases.end(), [](const auto* a, const auto* b) { return a->second.row < b->second.row; });
		// Not offered: items nobody collects (potions, quivers, keys, scrolls,
		// tomes, quest items, body parts), and the types the grail already
		// tracks through their uniques or set items (amulets, rings, charms,
		// jewels, a mod's sunder charms...).
		static const std::set<std::string> pointless { "poti", "hpot", "mpot", "spot", "apot", "rpot", "wpot", "elix", "bowq", "xboq", "key", "ques", "body", "play", "scro", "book" };
		std::set<std::string>              trackedBases;
		// (Any unique or set item row's base, also those that do not drop: the
		// Hellfire Torch, a mod's sunder charms and colossal jewels.)
		for (std::uint32_t k = 0; k < uniques.count; ++k) trackedBases.insert(FourCC(uniques.rows + static_cast<std::size_t>(k) * UniqueRowSize, UniqueRow::Code));
		for (std::uint32_t k = 0; k < sets.count; ++k) trackedBases.insert(FourCC(sets.rows + static_cast<std::size_t>(k) * SetRowSize, SetRow::Code));
		std::set<std::string> trackedTypes;
		for (const auto* base : bases) {
			if (base->second.kind != 2 || base->second.type.empty() || base->first == "std" || base->second.type == "gold" || base->second.quest) continue;
			if (std::any_of(base->second.types.begin(), base->second.types.end(), [](const std::string& t) { return pointless.count(t) != 0; }) || pointless.count(base->second.type)) continue;
			if (trackedBases.count(base->first)) trackedTypes.insert(base->second.type);
			if (!byType.count(base->second.type)) typeOrder.push_back(base->second.type);
			byType[base->second.type].push_back(base);
		}
		typeOrder.erase(std::remove_if(typeOrder.begin(), typeOrder.end(), [&](const std::string& type) { return trackedTypes.count(type) != 0; }), typeOrder.end());
		for (const auto& type : typeOrder) {
			const auto& list = byType[type];
			CategoryEntry category { MiscMenu, "Misc", type };
			if (const auto name = setTables.typeNames.find(type); name != setTables.typeNames.end() && !name->second.empty()) category.name = name->second;
			{
				std::vector<std::string> itemNames;
				for (const auto* base : list) itemNames.push_back(base->second.name);
				category.name = MiscCategoryName(category.name, itemNames);
			}
			const auto& first = list.front()->second;
			if (!first.asset.empty()) category.icon = first.asset, category.width = first.width, category.height = first.height;
			else category.icon = "d2rloader\\grail-on\\icon-other";
			const int c = static_cast<int>(Categories.size());
			Categories.push_back(category);
			MiscCategories.push_back({ c, type, static_cast<int>(list.size()) });
			for (const auto* base : list) {
				UniqueEntry entry;
				entry.category   = c;
				entry.row        = -1;
				entry.misc       = true;
				entry.miscType   = type;
				entry.id         = "m:" + base->first;
				entry.kind       = 2;
				entry.width      = base->second.width;
				entry.height     = base->second.height;
				entry.name = entry.base = base->second.name;
				entry.code       = base->first;
				entry.ghost = entry.art = base->second.asset;
				entry.colorClass = base->second.colorClass;
				Uniques.push_back(entry);
				MiscByCode[base->first] = UniqueCount() - 1;
			}
		}
	}
	UpdateMiscTracking();
	// Rows sharing an index (the Rainbow Facets): numbered in row order.
	EntryById.clear();
	std::map<std::string, std::vector<int>> byIndex;
	for (int u = 0; u < UniqueCount(); ++u) byIndex[Uniques[u].id].push_back(u);
	for (auto& [id, list] : byIndex) {
		std::sort(list.begin(), list.end(), [](int a, int b) { return Uniques[a].row < Uniques[b].row; });
		for (std::size_t n = 0; n < list.size(); ++n) {
			if (n) Uniques[list[n]].id += "#" + std::to_string(n + 1);
			EntryById[Uniques[list[n]].id] = list[n];
		}
	}
	for (const auto& [from, to] : aliases)
		if (const auto entry = EntryById.find(to); entry != EntryById.end() && !EntryById.count(from)) EntryById[from] = entry->second;
	BuildObjectives();
	RebuildStored(); // records read before the lists
}

// ---- game files (game thread) ----
// The game's own files are read the way the game reads its json files: its
// resource manager (0x9F3540) loads the file (0x9F3DA0, what Fog's JSON loader
// 0xA0ACA0 calls), so the active mod's copy wins over the game's. The result is
// a shared pointer {resource, control}; the resource holds {u32 state (3 =
// loaded), data at +8, size at +0x10}. If that fails, D2RCore's archive open
// (export ordinal 160, what Stash Search reads its layout with) with the game's
// size, read and close. D2R 3.3.93787 / 3.3.94995, checked before use.

namespace Resource {
constexpr std::uint64_t Manager = 0x9F3540, Load = 0x9F3DA0;
constexpr std::uint8_t  ManagerBytes[] { 0x48, 0x83, 0xEC, 0x38, 0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00 };
constexpr std::uint8_t  LoadBytes[] { 0x40, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B, 0xFA, 0x48, 0x8D, 0x54, 0x24, 0x20 };
} // namespace Resource

struct SharedRef {
	unsigned char* object {};
	unsigned char* control {}; // vtable, strong count at +8, weak count at +0xC
};
using ManagerFn = void* (*)() noexcept;
// The load request, as Fog's JSON loader builds it on its stack: the path as
// the game's string (text, length, capacity with the "not owned" bit) with its
// inline buffer at +0x18, a byte 0x64 at +0xB8, then two empty callbacks
// (tagged 1) at +0xC0 and +0x108.
struct alignas(16) LoadRequest {
	unsigned char bytes[0x180] {};
};
using LoadFn    = SharedRef* (*)(void* manager, SharedRef* out, LoadRequest* request, int flags) noexcept;
using ReleaseFn = void (*)(void* control) noexcept;

// Why the last read failed, for the log.
std::string FileTrouble;

template <typename T>
T Read(const unsigned char* at, std::size_t offset) {
	T value {};
	std::memcpy(&value, at + offset, sizeof(T));
	return value;
}

std::string ResourceFile(const char* path) {
	const auto base = static_cast<std::uintptr_t>(Context->exeBase);
	if (!base || std::memcmp(reinterpret_cast<const void*>(base + Resource::Manager), Resource::ManagerBytes, sizeof(Resource::ManagerBytes)) != 0
	    || std::memcmp(reinterpret_cast<const void*>(base + Resource::Load), Resource::LoadBytes, sizeof(Resource::LoadBytes)) != 0) {
		FileTrouble += "; game loader: the game's file loader is not the expected one";
		return {};
	}
	void* manager = reinterpret_cast<ManagerFn>(base + Resource::Manager)();
	if (!manager) {
		FileTrouble += "; game loader: no file manager yet";
		return {};
	}
	const std::size_t length = std::strlen(path);
	if (length >= 0x9A) {
		FileTrouble += "; game loader: the path is too long";
		return {};
	}
	LoadRequest request;
	const auto  put = [&](std::size_t offset, std::uint64_t value) { std::memcpy(request.bytes + offset, &value, sizeof(value)); };
	std::memcpy(request.bytes + 0x18, path, length);
	put(0, reinterpret_cast<std::uint64_t>(request.bytes + 0x18));
	put(8, length);
	put(0x10, 0x800000000000009AULL);
	request.bytes[0xB8] = 0x64;
	put(0xC0, 1);
	put(0x108, 1);
	SharedRef ref;
	reinterpret_cast<LoadFn>(base + Resource::Load)(manager, &ref, &request, 0);
	std::string text;
	if (!ref.object) FileTrouble += "; game loader: the loader returned nothing";
	else if (const auto state = Read<std::uint32_t>(ref.object, 0); state != 3) FileTrouble += "; game loader: the loader state is " + std::to_string(state);
	else {
		const auto* data = Read<const char*>(ref.object, 8);
		const auto  size = Read<std::uint64_t>(ref.object, 0x10);
		if (data && size > 0 && size < (64u << 20)) text.assign(data, size);
		else FileTrouble += "; game loader: the file is empty";
	}
	if (ref.control) {
		auto* strong = reinterpret_cast<volatile long*>(ref.control + 8);
		auto* weak   = reinterpret_cast<volatile long*>(ref.control + 0xC);
		auto* table  = *reinterpret_cast<ReleaseFn* const*>(ref.control);
		if (InterlockedDecrement(strong) == 0) {
			table[2](ref.control);
			if (InterlockedDecrement(weak) == 0) table[3](ref.control);
		}
	}
	return text;
}

namespace FileApi {
constexpr std::uint16_t OpenOrdinal = 160;
constexpr std::uint64_t Size = 0xA00050, Read = 0xA00EC0, Close = 0x9FFE50;
constexpr std::uint8_t  SizeBytes[] { 0x48, 0x8B, 0x01, 0x48, 0xFF, 0x60, 0x30 };                    // mov rax, [rcx]; jmp [rax+0x30]
constexpr std::uint8_t  ReadBytes[] { 0x4C, 0x89, 0x44, 0x24, 0x18, 0x48, 0x89, 0x54, 0x24, 0x10 }; // mov [rsp+18h], r8; mov [rsp+10h], rdx
constexpr std::uint8_t  CloseBytes[] { 0xE9 };                                                     // jmp
} // namespace FileApi

using FileOpenFn  = void* (*)(const char* path) noexcept;
using FileSizeFn  = std::uint64_t (*)(void* file) noexcept;
using FileReadFn  = bool (*)(void* file, void* buffer, std::uint64_t size, std::uint64_t* read) noexcept;
using FileCloseFn = void (*)(void* file) noexcept;

// A copy of `bytes` in d2rloader\logs next to this plugin's folder (for
// looking at the game's art the plugin uses).
void DumpFile(const wchar_t* name, const std::string& bytes) {
	HMODULE module = nullptr;
	wchar_t path[MAX_PATH] {};
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&DumpFile), &module)
	    || !GetModuleFileNameW(module, path, MAX_PATH))
		return;
	std::wstring folder = path;
	folder.resize(folder.find_last_of(L"\\/") == std::wstring::npos ? 0 : folder.find_last_of(L"\\/"));
	folder.resize(folder.find_last_of(L"\\/") == std::wstring::npos ? 0 : folder.find_last_of(L"\\/"));
	if (folder.empty()) return;
	const std::wstring file = folder + L"\\logs\\grail-on-" + name;
	HANDLE out = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (out == INVALID_HANDLE_VALUE) return;
	DWORD written = 0;
	(void)WriteFile(out, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
	CloseHandle(out);
}

// The whole file, or empty when it is not there or cannot be read.
std::string ArchiveFile(const char* path) {
	static const FileOpenFn open = [] {
		const auto base = static_cast<std::uintptr_t>(Context->exeBase);
		const auto same = [&](std::uint64_t rva, const std::uint8_t* bytes, std::size_t count) { return std::memcmp(reinterpret_cast<const void*>(base + rva), bytes, count) == 0; };
		HMODULE    core = GetModuleHandleW(L"D2RCore.dll");
		if (!base || !core || !same(FileApi::Size, FileApi::SizeBytes, sizeof(FileApi::SizeBytes)) || !same(FileApi::Read, FileApi::ReadBytes, sizeof(FileApi::ReadBytes))
		    || !same(FileApi::Close, FileApi::CloseBytes, sizeof(FileApi::CloseBytes)))
			return FileOpenFn {};
		return reinterpret_cast<FileOpenFn>(reinterpret_cast<void*>(GetProcAddress(core, MAKEINTRESOURCEA(FileApi::OpenOrdinal))));
	}();
	if (!open) {
		FileTrouble += "D2RCore's archive open is missing";
		return {};
	}
	const auto base = static_cast<std::uintptr_t>(Context->exeBase);
	void*      file = open(path);
	if (!file) {
		FileTrouble += "D2RCore's archive does not have it";
		return {};
	}
	std::string         text;
	const std::uint64_t size = reinterpret_cast<FileSizeFn>(base + FileApi::Size)(file);
	if (size > 0 && size < (64u << 20)) {
		text.resize(size);
		std::uint64_t read = 0;
		if (!reinterpret_cast<FileReadFn>(base + FileApi::Read)(file, text.data(), size, &read) || read != size) text.clear();
	}
	reinterpret_cast<FileCloseFn>(base + FileApi::Close)(file);
	if (text.empty()) FileTrouble += "D2RCore's archive read failed";
	return text;
}


// The Chronicle's grey rune stones (a sprite of 98x98 cells, the 33 runes by
// English name, Amn to Zod, in reading order): read on the game thread, cut and
// registered on the UI thread, with a blank stone made from them.
std::string ChronicleRunesFile;
std::string AdvancedButtonFile;
// A ButtonWidget named like "...Refresh..." in the lobby panels' layouts: its
// sprite ($names looked up in the profile) and its frames.
std::string GameFile(const char* path);
std::string ArchiveFile(const char* path);
std::string ResourceFile(const char* path);

// The game's own settings art (check box, dividers, list rows, drop-down
// parts), read with the lists; the settings page's sprites are made from it.
struct GameArt {
	const char* path;
	std::string file;
};
GameArt SettingsArt[] {
	{ "data/hd/global/ui/panel/loot_filter/lootfilter_large_list_btn_checkbox.sprite" }, { "data/hd/global/ui/pausemenu/divider.sprite" },
	{ "data/hd/global/ui/panel/loot_filter/slot_large_list.sprite" },                    { "data/hd/global/ui/panel/loot_filter/slot_large_list_vertical_divider.sprite" },
	{ "data/hd/global/ui/panel/dropdownbutton_left.sprite" },                            { "data/hd/global/ui/panel/dropdownbutton_center.sprite" },
	{ "data/hd/global/ui/panel/dropdownbutton_right.sprite" },                           { "data/hd/global/ui/panel/dropdowntile_left.sprite" },
	{ "data/hd/global/ui/panel/dropdowntile_center.sprite" },                            { "data/hd/global/ui/panel/dropdowntile_right.sprite" },
	{ "data/hd/global/ui/panel/loot_filter/slot_large_list_btn_1.sprite" },              { "data/hd/global/ui/panel/loot_filter/slot_large_list_btn_2.sprite" },
	{ "data/hd/global/ui/panel/loot_filter/chronicle_btn_add.sprite" },                       { "data/hd/global/ui/panel/closebtn_4x.sprite" },
};
enum { ArtCheck, ArtLine, ArtRow, ArtColumn, ArtDropLeft, ArtDropCenter, ArtDropRight, ArtTileLeft, ArtTileCenter, ArtTileRight, ArtUp, ArtDown, ArtNew, ArtClose };
std::atomic<bool> SettingsArtRead {};
// The lobby's refresh button (the grail button's reset): its sprite file and
// frames, found in the lobby panels' layouts.
struct NativeButton {
	std::string file;
	int         normal {}, hovered {}, pressed {};
};
NativeButton RefreshArt;
NativeButton FindRefreshButton();
std::atomic<int> GenericRunes {}; // rune stones registered as grune<n>, and grune-blank
std::atomic<int> GemSocketSize { Cell }; // the game's socket picture's height, in pixels
// A rune's cell (r01 El .. r33 Zod -> its place by name).
constexpr int RuneCell[] { 4, 5, 29, 19, 6, 12, 27, 23, 21, 28, 0, 25, 24, 3, 9, 10, 17, 14, 7, 15, 22, 30, 18, 11, 8, 31, 20, 16, 26, 1, 13, 2, 32 };

NativeButton FindRefreshButton() {
	NativeButton found;
	const std::string profile = GameFile("data/global/ui/layouts/_profilehd.json");
	const auto        stringAfter = [](const std::string& text, std::size_t from, const char* key, std::size_t limit) -> std::string {
        const auto at = text.find(std::string("\"") + key + "\"", from);
        if (at == std::string::npos || at > limit) return {};
        const auto open = text.find('"', text.find(':', at) + 1);
        const auto close = open == std::string::npos ? open : text.find('"', open + 1);
        return close == std::string::npos ? std::string() : text.substr(open + 1, close - open - 1);
	};
	const auto numberAfter = [](const std::string& text, std::size_t from, const char* key, std::size_t limit, int fallback) {
		const auto at = text.find(std::string("\"") + key + "\"", from);
		if (at == std::string::npos || at > limit) return fallback;
		return std::atoi(text.c_str() + text.find(':', at) + 1);
	};
	for (const char* panel : { "joingamepanelhd", "gamelistpanelhd", "ladderpanelhd", "friendslistpanelhd", "consolelobbypanelhd", "createGamepanelhd" }) {
		const std::string path   = std::string("data/global/ui/layouts/") + panel + ".json";
		const std::string layout = GameFile(path.c_str());
		if (layout.empty()) continue;
		for (std::size_t at = 0; (at = layout.find("\"name\"", at)) != std::string::npos; ++at) {
			const auto value = stringAfter(layout, at, "name", at + 8);
			std::string lower = value;
			for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			if (lower.find("refresh") == std::string::npos) continue;
			const auto next  = layout.find("\"type\"", at + 8);
			const auto limit = next == std::string::npos ? layout.size() : next;
			std::string filename = stringAfter(layout, at, "filename", limit);
			if (filename.empty()) continue;
			if (filename[0] == '$') filename = stringAfter(profile, 0, filename.c_str() + 1, profile.size());
			if (filename.empty()) continue;
			std::string file = "data/hd/global/ui/";
			for (const char c : filename) file += c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			file += ".sprite";
			found.file = ArchiveFile(file.c_str());
			if (found.file.empty()) found.file = ResourceFile(file.c_str());
			found.normal  = numberAfter(layout, at, "normalFrame", limit, 0);
			found.hovered = numberAfter(layout, at, "hoveredFrame", limit, found.normal);
			found.pressed = numberAfter(layout, at, "pressedFrame", limit, found.normal);
			Say(false, "grailon: refresh button %s in %s: %s (%s), frames %d/%d/%d", value.c_str(), panel, filename.c_str(), found.file.empty() ? "not read" : "read", found.normal, found.hovered,
			    found.pressed);
			if (!found.file.empty()) return found;
		}
	}
	Say(false, "grailon: the lobby's refresh button was not found; the reset button is the plugin's own");
	return {};
}

// ---- item colours ----
// The grid colours a unique's art by its colour (UniqueItems invtransform):
// GlobalData's SpriteColoringHelper (data/global/ui/layouts/globaldatahd.json)
// gives each colour an hsv and a tint transform, applied to the pixels in the
// art's colour range (by its base's InvTrans class, or by its sprite). The
// plugin's own pictures of stored items get the same fields.
struct ColorRange {
	bool  empty = true;
	float hue[2] {}, saturation[2] {}, value[2] {};
};
std::vector<ColorRange>               ColorRanges;     // by colour class
std::vector<std::array<float, 8>>     ColorTransforms; // by colour: hsv[4], tint[4]
std::map<std::string, ColorRange>     RangeOverrides;  // by sprite ("items/armor/helmet/helm")

// A lenient JSON reader for the game's layout files (comments, trailing commas).
struct JsonValue {
	enum Kind { Null, Number, String, Array, Object } kind = Null;
	double                                         number {};
	std::string                                    text;
	std::vector<JsonValue>                         items;
	std::vector<std::pair<std::string, JsonValue>> fields;
	const JsonValue* get(const std::string& key) const {
		for (const auto& [name, value] : fields)
			if (name == key) return &value;
		return nullptr;
	}
};

struct JsonReader {
	const std::string& s;
	std::size_t        at = 0;
	void skip() {
		for (;;) {
			while (at < s.size() && std::isspace(static_cast<unsigned char>(s[at]))) ++at;
			if (s.compare(at, 2, "//") == 0) at = s.find('\n', at) == std::string::npos ? s.size() : s.find('\n', at);
			else if (s.compare(at, 2, "/*") == 0) at = s.find("*/", at) == std::string::npos ? s.size() : s.find("*/", at) + 2;
			else return;
		}
	}
	std::string str() {
		std::string out;
		for (++at; at < s.size() && s[at] != '"'; ++at) out += s[at] == '\\' && at + 1 < s.size() ? s[++at] : s[at];
		++at;
		return out;
	}
	JsonValue value(int depth = 0) {
		JsonValue v;
		skip();
		if (at >= s.size() || depth > 64) return v;
		const char c = s[at];
		if (c == '{' || c == '[') {
			v.kind = c == '{' ? JsonValue::Object : JsonValue::Array;
			++at;
			for (;;) {
				skip();
				if (at >= s.size()) return v;
				if (s[at] == '}' || s[at] == ']') {
					++at;
					return v;
				}
				if (s[at] == ',') {
					++at;
					continue;
				}
				if (v.kind == JsonValue::Object) {
					std::string key = s[at] == '"' ? str() : std::string();
					skip();
					if (at < s.size() && s[at] == ':') ++at;
					v.fields.emplace_back(std::move(key), value(depth + 1));
				} else v.items.push_back(value(depth + 1));
			}
		}
		if (c == '"') {
			v.kind = JsonValue::String;
			v.text = str();
			return v;
		}
		char* end = nullptr;
		v.number  = std::strtod(s.c_str() + at, &end);
		if (end && end != s.c_str() + at) {
			v.kind = JsonValue::Number;
			at     = static_cast<std::size_t>(end - s.c_str());
		} else
			while (at < s.size() && !std::strchr(",]}", s[at])) ++at; // true, false, null
		return v;
	}
};

ColorRange ReadRange(const JsonValue& v) {
	ColorRange range;
	if (v.kind != JsonValue::Object) return range;
	const auto pair = [&](const char* key, float* out) {
		const auto* field = v.get(key);
		if (!field || field->items.size() < 2) return false;
		out[0] = static_cast<float>(field->items[0].number), out[1] = static_cast<float>(field->items[1].number);
		return true;
	};
	range.empty = !(pair("hue", range.hue) && pair("saturation", range.saturation) && pair("value", range.value));
	return range;
}

// Game thread, with the lists. Finds the SpriteColoringHelper's fields.
void ReadItemColors(const std::string& text) {
	if (text.empty()) return;
	JsonReader reader { text };
	const JsonValue root = reader.value();
	std::vector<const JsonValue*> stack { &root };
	const JsonValue*              fields = nullptr;
	while (!stack.empty() && !fields) {
		const auto* node = stack.back();
		stack.pop_back();
		if (const auto* type = node->get("type"); type && type->text == "SpriteColoringHelper") fields = node->get("fields");
		if (const auto* children = node->get("children"))
			for (const auto& child : children->items) stack.push_back(&child);
	}
	if (!fields) {
		Problem(false, "grailon: no SpriteColoringHelper in the game's GlobalData; drawn items keep their art's colours");
		return;
	}
	std::vector<ColorRange> ranges;
	std::vector<std::array<float, 8>> transforms;
	std::map<std::string, ColorRange> overrides;
	if (const auto* list = fields->get("colorRange"))
		for (const auto& item : list->items) ranges.push_back(ReadRange(item));
	if (const auto* list = fields->get("colorTransform"))
		for (const auto& item : list->items) {
			std::array<float, 8> t {};
			for (int half = 0; half < 2; ++half)
				if (const auto* values = item.get(half ? "tintTransform" : "hsvTransform"))
					for (std::size_t i = 0; i < 4 && i < values->items.size(); ++i) t[half * 4 + i] = static_cast<float>(values->items[i].number);
			transforms.push_back(t);
		}
	if (const auto* list = fields->get("colorRangeOverride"))
		for (const auto& item : list->items)
			if (item.items.size() >= 2) {
				std::string path = item.items[0].text;
				for (char& c : path) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c == '\\' ? '/' : c)));
				if (path.rfind("data/hd/global/ui/", 0) == 0) path.erase(0, 18);
				overrides[path] = ReadRange(item.items[1]);
			}
	std::lock_guard guard(Lock);
	ColorRanges.swap(ranges);
	ColorTransforms.swap(transforms);
	RangeOverrides.swap(overrides);
	Say(false, "grailon: item colours: %zu ranges, %zu colours, %zu sprite ranges", ColorRanges.size(), ColorTransforms.size(), RangeOverrides.size());
}

// Lock held. The coloring fields of a stored item's art, as the grid draws it.
std::string ColorFields(const UniqueEntry& entry, const std::string& art) {
	if (entry.color < 0 || entry.color >= static_cast<int>(ColorTransforms.size())) return {};
	std::string key = art;
	for (char& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c == '\\' ? '/' : c)));
	const ColorRange* range = nullptr;
	if (const auto found = RangeOverrides.find(key); found != RangeOverrides.end()) range = &found->second;
	else if (entry.colorClass >= 0 && entry.colorClass < static_cast<int>(ColorRanges.size())) range = &ColorRanges[entry.colorClass];
	if (!range || range->empty) return {};
	const auto& t = ColorTransforms[entry.color];
	char        text[400] {};
	std::snprintf(text, sizeof(text),
	              R"(, "useAdvancedColoring": true, "recolorRange": {"hue": [%g, %g], "saturation": [%g, %g], "value": [%g, %g]}, "hsvTransform": [%g, %g, %g, %g], "tintTransform": [%g, %g, %g, %g])",
	              range->hue[0], range->hue[1], range->saturation[0], range->saturation[1], range->value[0], range->value[1], t[0], t[1], t[2], t[3], t[4], t[5], t[6], t[7]);
	return text;
}

std::string GameFile(const char* path) {
	FileTrouble.clear();
	std::string text = ArchiveFile(path);
	if (text.empty()) text = ResourceFile(path);
	if (text.empty()) Problem(false, "grailon: could not read %s: %s", path, FileTrouble.c_str());
	else {
		std::string start = text.substr(0, 48);
		for (char& c : start)
			if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) > 126) c = '.';
		Say(false, "grailon: read %s: %zu bytes (%s), %zu art entries, starts \"%s\"", path, text.size(), FileTrouble.empty() ? "D2RCore's archive" : "the game's loader",
		    ParseAssets(text).size(), start.c_str());
	}
	return text;
}

// One of the item art lists, under data/hd/items.
std::map<std::string, std::string> GameAssets(const char* name) { return ParseAssets(GameFile((std::string("data/hd/items/") + name).c_str())); }

void BuildRolls(const D2RL::PluginContext* context);

// Game thread: reads the tables and builds the lists.
void __cdecl BuildFromGame(const D2RL::PluginContext* context, void*) noexcept {
	Tables::TableView views[4] {};
	const Tables::TableId ids[4] { Tables::TableId::ItemTypes, Tables::TableId::Items, Tables::TableId::UniqueItems, Tables::TableId::SetItems };
	const std::uint32_t sizes[4] { TypeRowSize, ItemRowSize, UniqueRowSize, SetRowSize };
	for (int t = 0; t < 4; ++t) {
		views[t].structSize = Tables::TableViewSize;
		if (!TableApi || TableApi->getTable(context, Tables::Bank::Rotw, ids[t], &views[t]) != Tables::Result::Success) {
			ListsWanted = true; // try again later
			return;
		}
	}
	for (int t = 0; t < 4; ++t)
		if (views[t].rowSize != sizes[t]) {
			Problem(true, "grailon: this game build's tables are laid out differently (rows of %u, %u, %u, %u bytes); the grail lists cannot be built", views[0].rowSize,
			    views[1].rowSize, views[2].rowSize, views[3].rowSize);
			return;
		}
	ReadGameTexts();
	if (ChronicleRunesFile.empty()) {
		constexpr const char* path = "data/hd/global/ui/panel/chronicle/chronicle_runes.sprite";
		ChronicleRunesFile         = ArchiveFile(path);
		if (ChronicleRunesFile.empty()) ChronicleRunesFile = ResourceFile(path);
		constexpr const char* socket = "data/hd/global/ui/panel/gemsocket.sprite";
		std::string           file   = ArchiveFile(socket);
		if (file.empty()) file = ResourceFile(socket);
		if (file.size() >= 0x10 && !file.compare(0, 4, "SpA1")) {
			std::uint32_t height = 0;
			std::memcpy(&height, file.data() + 12, 4);
			if (height >= 16 && height <= 1024) GemSocketSize = static_cast<int>(height);
		}
		Say(false, "grailon: socket picture %d px high", GemSocketSize.load());
		constexpr const char* advanced = "data/hd/global/ui/panel/character_sheet/advancedstatsbutton.sprite";
		AdvancedButtonFile             = ArchiveFile(advanced);
		if (AdvancedButtonFile.empty()) AdvancedButtonFile = ResourceFile(advanced);
		if (AdvancedButtonFile.empty()) Say(false, "grailon: the Advanced Stats button picture was not found");
		else DumpFile(L"advancedstatsbutton.sprite", AdvancedButtonFile);
		for (auto& art : SettingsArt) {
			art.file = ArchiveFile(art.path);
			if (art.file.empty()) art.file = ResourceFile(art.path);
			const std::string name = std::strrchr(art.path, '/') + 1;
			if (art.file.empty()) Say(false, "grailon: settings art %s not found", name.c_str());
			else DumpFile(std::wstring(name.begin(), name.end()).c_str(), art.file);
		}
		RefreshArt = FindRefreshButton();
		SettingsArtRead = true;
	}
	ReadItemColors(GameFile("data/global/ui/layouts/globaldatahd.json"));
	ArtMaps art;
	art.bases = GameAssets("items.json");
	for (const auto& [key, asset] : GameAssets("uniques.json")) art.uniques[Key(key)] = asset;
	for (const auto& [key, asset] : GameAssets("sets.json")) art.sets[Key(key)] = asset;
	const auto rows = [&](int t) { return TableRows { static_cast<const unsigned char*>(views[t].rows), views[t].rowCount }; };
	// Optional: set names and classes.
	SetTables extra;
	const auto optional = [&](Tables::TableId id, TableRows& rows, std::uint32_t& size) {
		Tables::TableView view { .structSize = Tables::TableViewSize };
		if (TableApi->getTable(context, Tables::Bank::Rotw, id, &view) != Tables::Result::Success || !view.rows) return;
		rows = { static_cast<const unsigned char*>(view.rows), view.rowCount };
		size = view.rowSize;
	};
	optional(Tables::TableId::Sets, extra.sets, extra.setSize);
	optional(Tables::TableId::Properties, extra.props, extra.propSize);
	optional(Tables::TableId::Skills, extra.skills, extra.skillSize);
	optional(Tables::TableId::Runes, extra.runes, extra.runeSize);
	optional(Tables::TableId::RunewordUiCategories, extra.runewordCategories, extra.runewordCategorySize);
	// The item types' names (a comment column, not in the compiled table): from the game's itemtypes.txt.
	{
		constexpr const char* path = "data/global/excel/itemtypes.txt";
		std::string           file = ArchiveFile(path);
		if (file.empty()) file = ResourceFile(path);
		std::vector<std::string> lines;
		for (std::size_t at = 0; at < file.size();) {
			auto end = file.find('\n', at);
			if (end == std::string::npos) end = file.size();
			lines.push_back(file.substr(at, end - at));
			at = end + 1;
		}
		const auto cells = [](const std::string& line) {
			std::vector<std::string> out;
			std::size_t              at = 0;
			for (;;) {
				const auto tab = line.find('\t', at);
				out.push_back(line.substr(at, tab == std::string::npos ? std::string::npos : tab - at));
				if (tab == std::string::npos) break;
				at = tab + 1;
			}
			for (auto& cell : out)
				while (!cell.empty() && (cell.back() == '\r' || cell.back() == ' ')) cell.pop_back();
			return out;
		};
		if (!lines.empty()) {
			const auto head = cells(lines[0]);
			int        nameAt = -1, codeAt = -1;
			for (int k = 0; k < static_cast<int>(head.size()); ++k) {
				if (head[k] == "ItemType") nameAt = k;
				if (head[k] == "Code") codeAt = k;
			}
			for (std::size_t l = 1; nameAt >= 0 && codeAt >= 0 && l < lines.size(); ++l)
				if (const auto row = cells(lines[l]); static_cast<int>(row.size()) > std::max(nameAt, codeAt) && !row[codeAt].empty())
					extra.typeNames.try_emplace(row[codeAt], row[nameAt]);
		}
		Say(false, "grailon: %zu item type names read from %s", extra.typeNames.size(), path);
	}
	extra.keyText  = KeyText;
	extra.keyNames = RunewordNames(GameFile("data/local/lng/strings/item-runes.json"));
	{
		std::lock_guard guard(Lock);
		BuildLists(rows(0), rows(1), rows(2), rows(3), GameText, art, extra);
	}
	if (!SetClassFound) Say(false, "grailon: the game's set classes were not found; sets go by their class items and class skills");
	if (RuneFields.found())
		Say(false, "grailon: Runes table: %u rows of %u bytes, name %d, types %d, runes %d, stats %d, complete %d%s", extra.runes.count, extra.runeSize, RuneFields.name, RuneFields.types,
		    RuneFields.runes, RuneFields.props, RuneFields.complete, RuneFields.byShape ? " (found from the rows' shape: its first rows are modded)" : "");
	if (!RuneFields.found())
		Problem(false, "grailon: the Runes table (%u rows of %u bytes) is laid out differently (name %d, types %d, runes %d, props %d); no runewords", extra.runes.count, extra.runeSize, RuneFields.name,
		    RuneFields.types, RuneFields.runes, RuneFields.props);
	Say(false, "grailon: %d uniques and set items in %d categories (item art: %s)", UniqueCount(), CategoryCount(),
	    art.uniques.empty() ? "built-in, the game's item art files could not be read" : "the game's");
	// Built again for another language: the rolls are the same.
	if (!ListsReady) BuildRolls(context);
	else LanguageRefresh = true;
	StringsWanted = true; // the UI thread registers their texts before any view opens
	ListsReady    = true;
}

// ---- settings file ----

std::wstring SettingsPath() {
	PWSTR folder = nullptr;
	if (SHGetKnownFolderPath(FOLDERID_SavedGames, 0, nullptr, &folder) != S_OK) return {};
	std::wstring path = std::wstring(folder) + L"\\Diablo II Resurrected\\grail-storage";
	CoTaskMemFree(folder);
	CreateDirectoryW(path.c_str(), nullptr);
	// Settings from before the rename (grail-ui.toml) carry over once.
	const std::wstring file = path + L"\\grail-on.toml", old = path + L"\\grail-ui.toml";
	if (GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES) MoveFileW(old.c_str(), file.c_str());
	return file;
}

// The objectives a new player starts with.
constexpr const char* DefaultObjective = "Find the easter egg in Grail On!";
void LoadSettings() {
	FILE* file = _wfopen(SettingsPath().c_str(), L"r");
	Objectives = { { 1, 14, DefaultObjective }, { 2, 1, "Find a 3/20/20 small charm" } }, NextObjective = 3;
	if (!file) return;
	bool objectivesKnown = false; // the file has the objectives' switch: its list is the player's
	char line[4096];
	while (std::fgets(line, sizeof(line), file)) {
		// The tracked misc item types: a comma list.
		if (!std::strncmp(line, "misc_tracked", 12) || !std::strncmp(line, "misc_checkbox", 13)) {
			const int mode = line[5] == 't' ? MiscItems : MiscCheck;
			for (auto it = MiscMode.begin(); it != MiscMode.end();) it = it->second == mode ? MiscMode.erase(it) : std::next(it);
			if (const char* at = std::strchr(line, '=')) {
				std::string type;
				for (++at;; ++at) {
					if (!*at || *at == ',' || *at == '\n' || *at == '\r') {
						if (!type.empty()) MiscMode[type] = mode;
						type.clear();
						if (!*at) break;
					} else if (*at != ' ' && *at != '\t') type += *at;
				}
			}
			continue;
		}
		// An objective: "objective = id|icon|text".
		if (!std::strncmp(line, "objectives", 10) && !objectivesKnown) Objectives.clear(), NextObjective = 1, objectivesKnown = true;
		if (!std::strncmp(line, "objective ", 10) || !std::strncmp(line, "objective=", 10)) {
			if (!objectivesKnown) Objectives.clear(), NextObjective = 1, objectivesKnown = true;
			const char* at = std::strchr(line, '=');
			Objective   objective;
			char*       end = nullptr;
			objective.id    = static_cast<int>(std::strtol(at + 1, &end, 10));
			if (end && *end == '|') objective.icon = static_cast<int>(std::strtol(end + 1, &end, 10));
			if (end && *end == '|') objective.text = end + 1;
			while (!objective.text.empty() && (objective.text.back() == '\n' || objective.text.back() == '\r' || objective.text.back() == ' ')) objective.text.pop_back();
			if (objective.text.size() > 4 && objective.text.compare(objective.text.size() - 4, 4, "|off") == 0) objective.enabled = false, objective.text.resize(objective.text.size() - 4);
			else if (objective.text.size() > 3 && objective.text.compare(objective.text.size() - 3, 3, "|on") == 0) objective.text.resize(objective.text.size() - 3);
			if (objective.id > 0 && !objective.text.empty()) {
				objective.icon = std::clamp(objective.icon, 0, ObjectiveIconCount - 1);
				Objectives.push_back(objective);
				NextObjective = std::max(NextObjective, objective.id + 1);
			}
			continue;
		}
		char key[64] {}, value[16] {};
		if (std::sscanf(line, " %63[a-z_] = %15s", key, value) != 2) continue;
		const bool on = !std::strncmp(value, "true", 4);
		for (const auto& toggle : Toggles)
			if (!std::strcmp(key, toggle.key)) Config.*toggle.field = on;
		for (int k = 0; k < 4; ++k)
			if (!std::strcmp(key, GoalSettings[k]))
				for (int mode = 0; mode < 3; ++mode)
					if (!std::strcmp(value, GoalKeys[mode])) Config.goal[k] = mode;
		for (int k = 0; k < 3; ++k)
			if (!std::strcmp(key, LootSettings[k]))
				for (int mode = 0; mode < 3; ++mode)
					if (!std::strcmp(value, LootKeys[mode])) Config.loot[k] = mode;
		if (!std::strcmp(key, "language")) {
			Config.language = -1;
			for (int k = 0; k < LanguageCount; ++k)
				if (!_stricmp(value, Languages[k])) Config.language = k;
		}
		if (!std::strcmp(key, "button_x")) Config.buttonX = std::clamp(std::atoi(value), -1000, 3000);
		// search_x/search_y/search_width (from the window, which compact mode moves) are no longer read.
		// (search_left/search_top: a test build's, its top from the window: not read.)
		if (!std::strcmp(key, "search_screen_x")) Config.searchX = std::clamp(std::atoi(value), -3000, 4000);
		if (!std::strcmp(key, "search_screen_y")) Config.searchY = std::clamp(std::atoi(value), -3000, 4000);
		if (!std::strcmp(key, "search_length")) Config.searchWidth = std::clamp(std::atoi(value), 300, 2000);
		if (!std::strcmp(key, "button_y")) Config.buttonY = std::clamp(std::atoi(value), -1000, 3000);
		if (!std::strcmp(key, "compare")) Config.compare = !std::strcmp(value, "none") || !std::strcmp(value, "false") ? CompareNone : !std::strcmp(value, "shift") ? CompareShift : CompareAlways;
		// The settings before they were split.
		if (!std::strcmp(key, "spoilers"))
			Config.uniqueNames = Config.uniqueArt = Config.setItemNames = Config.setItemArt = Config.setNames = Config.runewordNames = Config.runewordRunes = on;
		if (!std::strcmp(key, "animations")) Config.frameAnimations = Config.scrollAnimations = on;
	}
	std::fclose(file);
}

void SaveSettings() {
	FILE* file = _wfopen(SettingsPath().c_str(), L"w");
	if (!file) {
		Problem(false, "grailon: could not save the settings");
		return;
	}
	std::fprintf(file, "# Grail On! settings, changed from its settings page.\n# Spoilers: true shows an unfound entry's own name, art or runes; false hides them until it is found.\n");
	for (const auto& toggle : Toggles) {
		if (!std::strcmp(toggle.key, "perfect_frames")) {
			std::fprintf(file, "# Goals (none, find or perfect): the kinds the overview counts, and what each stats page's centre shows.\n");
			for (int k = 0; k < 4; ++k) std::fprintf(file, "%s = %s\n", GoalSettings[k], GoalKeys[Config.goal[k]]);
			std::fprintf(file, "# Loot (all, found or perfect): ground items hidden when the grail has them, or has them perfect.\n");
			for (int k = 0; k < 3; ++k) std::fprintf(file, "%s = %s\n", LootSettings[k], LootKeys[Config.loot[k]]);
		}
		if (toggle.group >= DisplayGroup) std::fprintf(file, "# %s\n", toggle.help);
		std::fprintf(file, "%s = %s\n", toggle.key, Config.*toggle.field ? "true" : "false");
		if (!std::strcmp(toggle.key, "scroll_animations"))
			std::fprintf(file, "# The grail copy beside a hovered grail item while the grail is open: none, shift (while Shift is held) or always.\ncompare = %s\n",
			             Config.compare == CompareNone ? "none" : Config.compare == CompareShift ? "shift" : "always");
	}
	std::fprintf(file, "# The language of Grail On!'s texts and item names: game (the game's) or enUS, deDE, esES, esMX, frFR, itIT, jaJP, koKR, plPL, ptBR, ruRU, zhCN, zhTW.\nlanguage = %s\n",
	             Config.language >= 0 && Config.language < LanguageCount ? Languages[Config.language] : "game");
	std::fprintf(file, "# The grail button's place in the inventory (default %d, %d).\nbutton_x = %d\nbutton_y = %d\n", DefaultButtonX, DefaultButtonY, Config.buttonX, Config.buttonY);
	std::fprintf(file, "# The search bar's place on the screen, compact mode or not (default %d, %d), and its length (default %d).\nsearch_screen_x = %d\nsearch_screen_y = %d\nsearch_length = %d\n", DefaultSearchX,
	             DefaultSearchY, DefaultSearchWidth, Config.searchX, Config.searchY, Config.searchWidth);
	std::string tracked, checked;
	for (const auto& [type, mode] : MiscMode) (mode == MiscItems ? tracked : checked) += ((mode == MiscItems ? tracked : checked).empty() ? "" : ",") + type;
	std::fprintf(file, "# The misc item types (item type codes) with a tile per item in the Misc menu: items stored there (misc_tracked), or ticked by a click (misc_checkbox).\nmisc_tracked = %s\nmisc_checkbox = %s\n",
	             tracked.c_str(), checked.c_str());
	std::fprintf(file, "# The player's own objectives, ticked in the Objectives menu: number|icon (0-%d)|text.\n", ObjectiveIconCount - 1);
	for (const auto& objective : Objectives) std::fprintf(file, "objective = %d|%d|%s%s\n", objective.id, objective.icon, objective.text.c_str(), objective.enabled ? "" : "|off");
	std::fclose(file);
}

int GrailIndex(const Items::ItemInfo& item);
// Ethereal for the grail: a runeword has one tile, ethereal or not.
bool IsEth(const Items::ItemInfo& item) {
	if (!(item.stateFlags & Items::ItemStateEthereal)) return false;
	const int u = GrailIndex(item);
	return u < 0 || !Uniques[u].runeword;
}

// The runeword entry of a runeword item: its name (a completed runeword's
// prefixIds[0] is its name string), the one whose types fit its base when two
// share it. Lock held.
int RunewordFor(std::uint32_t nameId, const std::string& code) {
	auto known = RunewordIds.find(nameId);
	if (known == RunewordIds.end()) {
		std::vector<int> entries;
		if (const std::string name = nameId <= 0xFFFF ? GameText(static_cast<std::uint16_t>(nameId)) : std::string(); !name.empty())
			for (int u = 0; u < UniqueCount(); ++u)
				if (Uniques[u].runeword && Uniques[u].name == name) entries.push_back(u);
		known = RunewordIds.emplace(nameId, std::move(entries)).first;
	}
	if (known->second.empty()) return -1;
	if (const auto base = Bases.find(code); base != Bases.end()) {
		const auto has = [&](const std::vector<std::string>& list) {
			return std::any_of(list.begin(), list.end(), [&](const std::string& type) { return std::find(base->second.types.begin(), base->second.types.end(), type) != base->second.types.end(); });
		};
		for (const int u : known->second)
			if (has(Uniques[u].types) && !has(Uniques[u].excluded) && RuleTakes(Uniques[u].rule, base->second)) return u;
		for (const int u : known->second)
			if (has(Uniques[u].types) && !has(Uniques[u].excluded)) return u;
	}
	return known->second.front();
}

// Lock held. The entry of Runes row `row` for an item of base `code`: the
// tile of its base's kind.
int RunewordEntry(int row, const std::string& code) {
	const auto base  = Bases.find(code);
	if (base == Bases.end()) return -1;
	int        any   = -1;
	const auto has   = [&](const std::vector<std::string>& list) {
        return std::any_of(list.begin(), list.end(), [&](const std::string& type) { return std::find(base->second.types.begin(), base->second.types.end(), type) != base->second.types.end(); });
	};
	for (int u = 0; u < UniqueCount(); ++u) {
		// Its base must be one the runeword goes in.
		if (!Uniques[u].runeword || Uniques[u].row != row || !has(Uniques[u].types) || has(Uniques[u].excluded)) continue;
		if (RuleTakes(Uniques[u].rule, base->second)) return u;
		if (any < 0) any = u;
	}
	return any;
}

int RunewordRowOf(const Items::ItemInfo& item); // from its save bytes

// The grail entry of an item: its index in Uniques, or -1.
int GrailIndex(const Items::ItemInfo& item) {
	std::lock_guard guard(Lock);
	// A base kept in a runeword tile.
	if (const auto base = BaseFor.find(item.runtimeId); base != BaseFor.end()) return base->second;
	if (const int u = StoredBaseEntry(item.runtimeId); u >= 0) return u;
	if (SecretEntry >= 0 && CodeText(item.code) == Uniques[SecretEntry].code) return SecretEntry;
	// A tracked misc base: any item of it that is not a unique or set item
	// (before the runeword checks, which answer for every plain item).
	if (item.quality != Items::Quality::Unique && item.quality != Items::Quality::Set)
		if (const auto misc = MiscByCode.find(CodeText(item.code)); misc != MiscByCode.end())
			return HasTile(misc->second, false) && MiscModeOf(Uniques[misc->second].miscType) == MiscItems ? misc->second : -1;
	// A runeword: the game's own record of it, when the loader gives it.
	if ((item.quality == Items::Quality::Normal || item.quality == Items::Quality::Superior) && item.socketedItemCount > 0 && ItemApi
	    && D2RL::HasItemServiceField(ItemApi, D2RL::ItemServiceDetailsFieldEnd) && ItemApi->getItemDetails) {
		Items::ItemDetails details { .structSize = Items::ItemDetailsSize, .runewordRecordId = -1 };
		const auto         result = ItemApi->getItemDetails(Context, item.handle, &details, nullptr, 0);
		if (result == Items::Result::Success || result == Items::Result::InvalidArgument) {
			if (!(details.flags & Items::ItemDetailRuneword)) return -1;
			const int row = details.runewordRecordId;
			int       u   = row >= 0 ? RunewordEntry(row, CodeText(item.code)) : -1;
			if (u < 0 && details.runewordNameId) u = RunewordFor(details.runewordNameId, CodeText(item.code));
			if (u >= 0) {
				RunewordUnits[item.runtimeId] = u;
				return u;
			}
			static int told = 0;
			if (told++ < 3) Say(false, "grailon: runeword %s (Runes row %d, name %u) is not in the grail", CodeText(item.code).c_str(), row, details.runewordNameId);
			return -1;
		}
	}
	// Else its save bytes: the loader's ItemInfo (1.3.1) has no runeword name,
	// nor socket counts.
	if (item.quality == Items::Quality::Normal || item.quality == Items::Quality::Superior)
		if (const int row = RunewordRowOf(item); row != -2) {
			const int u = row >= 0 ? RunewordEntry(row, CodeText(item.code)) : -1;
			if (u >= 0) RunewordUnits[item.runtimeId] = u;
			static int told = 0;
			if (row >= 0 && u < 0 && told++ < 40) Say(false, "grailon: runeword %s (Runes row %d) is not in the grail", CodeText(item.code).c_str(), row);
			return u;
		}
	if ((item.quality == Items::Quality::Normal || item.quality == Items::Quality::Superior) && item.socketedItemCount > 0 && item.prefixIds[0]) {
		const int u = RunewordFor(item.prefixIds[0], CodeText(item.code));
		if (u >= 0) RunewordUnits[item.runtimeId] = u;
		static int told = 0;
		if (u < 0 && told < 3) {
			++told;
			Say(false, "grailon: socketed %s with name %u is not a known runeword", CodeText(item.code).c_str(), item.prefixIds[0]);
		}
		return u;
	}
	const auto* byRow = item.quality == Items::Quality::Unique ? &UniqueByRow : item.quality == Items::Quality::Set ? &SetByRow : nullptr;
	if (!byRow) return -1;
	if (byRow && !(item.stateFlags & Items::ItemStateIdentified)) return -1; // the grail takes identified items only
	if (!byRow || item.qualityRecordId < 0 || item.qualityRecordId >= static_cast<int>(byRow->size())) return -1;
	return (*byRow)[item.qualityRecordId];
}

bool ItemSize(const std::string& code, int& width, int& height) {
	const auto base = Bases.find(code);
	if (base == Bases.end()) return false;
	width  = base->second.width;
	height = base->second.height;
	return true;
}

// ---- tiles ----

// One tile of the view. x/y/width/height are page cells; slot is its place
// in the row (0-4).
struct Tile {
	int  unique {}; // index in Uniques
	bool eth {};
	int  x {}, y {}, width {}, height {}, slot {};
};

int                 CurrentCategory = StatsView; // index in Categories, SettingsView or StatsView
bool                ViewEth {};         // the category's ethereal tiles (the ethereal menus) instead of its plain ones
int                 Scroll {};          // first tile row shown
int                 RowCount {};        // tile rows of the current category
int                 LastRow {};
int                 MaxScroll {};       // first row once the last row is on screen
std::atomic<int>    BarMax {};          // MaxScroll for the window thread (0: no scroll bar)
std::vector<Tile>   Shown;              // tiles on screen, page coordinates
std::vector<std::pair<int, int>> ShownHeaders; // set names on screen: set, view row
std::vector<Tile>   Peeking;            // tiles shown as pictures: past the view's end, or past the page's rows (page coordinates)
struct Row;
std::vector<Row>    PlacedRows;         // the current category's rows (PlaceRows), for the scroll frames

// How much of a peeking tile's slot is in the view, in half cells (the view
// ends half a cell under the grid's last row).
int PeekHalves(const Tile& tile) { return std::clamp(std::min(tile.height * 2, (PageSize - tile.y) * 2 + 1), 0, 8); }
std::set<int>       Present;            // entries stored this session (normal or eth), index in Uniques
int                 JumpTo = -1;        // index in Uniques of an item whose tile must be shown
bool                JumpEth {};         // its ethereal tile (it may be on the next row)
// Stored items by tile, (unique, eth) -> runtime id: on the page, or waiting
// off screen in the stash (Parked).
std::map<std::pair<int, bool>, std::uint32_t> Stored;
std::map<std::pair<int, bool>, std::string>   StoredCodes; // the stored item's base: a runeword's art
std::set<std::uint32_t>                       Parked;
std::set<std::pair<int, bool>>                Perfect; // stored tiles whose copy has every best roll
std::set<std::pair<int, bool>>                BaseTiles; // runeword tiles holding a base, not the runeword (not counted, never perfect)
bool RefreshPerfect();

// Whether an item may be stored: a grail unique whose tile has no other item
// yet. Lock held.
// Whether unique `u` has a tile for its plain or its ethereal copy.
bool HasTile(int u, bool eth) {
	const auto& entry = Uniques[u];
	if (entry.misc && (eth || (entry.category >= 0 && entry.category < static_cast<int>(CategoryOff.size()) && CategoryOff[entry.category]))) return false;
	return eth ? entry.eth : !entry.onlyEth;
}

bool IsEthMenu(int menu) { return menu == EthWeaponMenu || menu == EthArmorMenu; }
// The menu whose categories an ethereal menu shows.
int BaseMenu(int menu) { return menu == EthWeaponMenu ? WeaponMenu : menu == EthArmorMenu ? ArmorMenu : menu; }
// The menu that shows category `c`'s plain or ethereal tiles.
int MenuOf(int c, bool eth) {
	const int menu = Categories[c].menu;
	return eth && menu == WeaponMenu ? EthWeaponMenu : eth && menu == ArmorMenu ? EthArmorMenu : menu;
}
// Whether category `c` has a button in `menu`: a tile there.
bool InMenu(int c, int menu) {
	if (Categories[c].menu != BaseMenu(menu)) return false;
	if (c < static_cast<int>(CategoryHas.size())) return CategoryHas[c][IsEthMenu(menu)] != 0;
	for (int u = 0; u < UniqueCount(); ++u)
		if (Uniques[u].category == c && HasTile(u, IsEthMenu(menu))) return true;
	return false;
}

bool Accepts(const Items::ItemInfo& item) {
	const int u = GrailIndex(item);
	if (u < 0 || !HasTile(u, IsEth(item))) return false;
	const auto twin = Stored.find({ u, IsEth(item) });
	return twin == Stored.end() || twin->second == item.runtimeId;
}

// A row header: a set (its id), or a runeword socket count (RunewordGroup + count).
constexpr int RunewordGroup = 1000;

// The tiles of a category, in rows of five: its plain tiles, or its
// ethereal ones (ViewEth). A row is as tall as its tallest tile plus the labels above; a set's
// first row also has the set's name above.
struct Row {
	std::vector<Tile> tiles;
	int               height {};
	int               header = -1; // the set named above the row
};

std::vector<Row> BuildRows(int category) {
	std::vector<Row> rows;
	int              lastSet = -1;
	const auto       add = [&](int unique, bool eth) {
        // Each set starts a row, under its name; so does each runeword socket count.
        const int  group  = Uniques[unique].set ? Uniques[unique].setId : Uniques[unique].runeword ? RunewordGroup + static_cast<int>(Uniques[unique].runes.size()) : -1;
        const bool newSet = group >= 0 && group != lastSet;
        lastSet           = group;
        if (rows.empty() || rows.back().tiles.size() == std::size(SlotColumns) || (newSet && !rows.back().tiles.empty())) rows.emplace_back();
        auto&       row   = rows.back();
        const auto& entry = Uniques[unique];
        const int   slot  = static_cast<int>(row.tiles.size());
        if (newSet) row.header = group;
        row.tiles.push_back(Tile { unique, eth, SlotColumns[slot], 0, entry.width, entry.height, slot });
        row.height = std::max(row.height, (row.header >= 0 ? HeaderRows : 0) + entry.height + LabelRows);
	};
	for (int i = 0; i < UniqueCount(); ++i) {
		if (Uniques[i].category != category || Uniques[i].secret) continue;
		if (HasTile(i, ViewEth)) add(i, ViewEth);
	}
	return rows;
}

// Lock held. Picks the rows that fit on the page from Scroll on.
// ---- 1-wide items centred in their tile ----
// A tile is two cells wide; a 1-wide item sits in its first column. The page
// grid is drawn half a cell further right in those two columns: the grid
// widgets share the game's grid vtable (Base+0x1CF3800), whose element rect
// (vt31, which item rects, item backgrounds and highlights all come from) and
// element under the mouse (vt19) are wrapped for the plugin's grid only (marked
// by chronicleFlourishOffset {GridMarker, GridMarker}, +0x634). The items stay
// in their real cells. Wrapped on the first view, after every plugin loaded,
// on top of whatever is there (Inventory Slots wraps the same entries).
struct HalfTile {
	int x {}, y {}, width {}, height {}, hx {}, hy {}; // cells it covers; half cells its items move right, down
};
constexpr float  GridMarker = 7100.0f;
constexpr int    VtCellAtPoint = 19, VtCellRect = 31;
SRWLOCK          HalfLock = SRWLOCK_INIT;
std::vector<HalfTile> HalfTiles; // the current view's, under HalfLock

using CellRectFn    = std::int32_t* (*)(void* widget, std::int32_t* out, std::uint64_t size, std::uint64_t cell) noexcept;
using CellAtPointFn = std::uint64_t* (*)(void* widget, std::uint64_t* out, std::uint64_t point) noexcept;
CellRectFn    GridCellRect {};
CellAtPointFn GridCellAtPoint {};
void**        GridVtable {};

void SetHalfTiles(std::vector<HalfTile> tiles) {
	AcquireSRWLockExclusive(&HalfLock);
	HalfTiles = std::move(tiles);
	ReleaseSRWLockExclusive(&HalfLock);
}

// The next view's half cells, applied as it opens: before, the view on
// screen would draw its 1-wide items with the next view's offsets. Lock held.
std::vector<HalfTile> NextHalfTiles;
bool                  HalfTilesWaiting {};
void ApplyHalfTiles() {
	if (!HalfTilesWaiting) return;
	HalfTilesWaiting = false;
	SetHalfTiles(std::move(NextHalfTiles));
	NextHalfTiles.clear();
}

// The plugin's own grid, or the host's grid of the grail's area (its cells
// stay the host page's: OriginX/Y take them to the grail's).
bool IsGrailGrid(const void* widget) {
	float marker[2] {};
	std::memcpy(marker, static_cast<const std::uint8_t*>(widget) + 0x634, sizeof(marker));
	if (Host::On()) return marker[0] == Host::Info.gridMarker[0] && marker[1] == Host::Info.gridMarker[1];
	return marker[0] == GridMarker && marker[1] == GridMarker;
}
int OriginX() { return Host::On() ? Host::Info.x : 0; }
int OriginY() { return Host::On() ? Host::Info.y : 0; }

// The shift (in half cells) of the tile holding cell `cell`; false for none.
bool HalfShift(std::uint64_t cell, int& hx, int& hy) {
	if (cell == ~std::uint64_t { 0 }) return false;
	const int x = static_cast<std::int32_t>(cell) - OriginX(), y = static_cast<std::int32_t>(cell >> 32) - OriginY();
	bool      found = false;
	AcquireSRWLockShared(&HalfLock);
	for (const auto& tile : HalfTiles)
		if (y >= tile.y && y < tile.y + tile.height && x >= tile.x && x < tile.x + tile.width) hx = tile.hx, hy = tile.hy, found = true;
	ReleaseSRWLockShared(&HalfLock);
	return found;
}

// Half a cell across and down, in pixels.
void HalfCell(void* widget, int& across, int& down) {
	std::int32_t first[4] {}, right[4] {}, below[4] {};
	GridCellRect(widget, first, 1 | (std::uint64_t { 1 } << 32), 0);
	GridCellRect(widget, right, 1 | (std::uint64_t { 1 } << 32), 1);
	GridCellRect(widget, below, 1 | (std::uint64_t { 1 } << 32), std::uint64_t { 1 } << 32);
	across = (right[0] - first[0]) / 2;
	down   = (below[1] - first[1]) / 2;
}

thread_local bool          PeekCellOn {}; // a picture tile's tooltip being shown: its item is at PeekCell
thread_local std::uint64_t PeekCell {}; // grail cells (x low 32 bits, y high)

// The screen rect (x, y, w, h) of `size` cells from grail cell (x, y), also
// past the grid's own cells (it is only 3x5): measured on its first cells and
// carried on, as the game would lay out a bigger grid.
void TileRect(void* widget, std::int32_t* out, std::uint64_t size, int x, int y) {
	const auto cellAt = [](int cx, int cy) { return static_cast<std::uint32_t>(cx) | (std::uint64_t { static_cast<std::uint32_t>(cy) } << 32); };
	std::int32_t right[4] {}, below[4] {};
	GridCellRect(widget, out, size, cellAt(OriginX(), OriginY()));
	GridCellRect(widget, right, size, cellAt(OriginX() + 1, OriginY()));
	GridCellRect(widget, below, size, cellAt(OriginX(), OriginY() + 1));
	out[0] += x * (right[0] - out[0]);
	out[1] += y * (below[1] - out[1]);
}

std::int32_t* HookGridCellRect(void* widget, std::int32_t* out, std::uint64_t size, std::uint64_t cell) noexcept {
	if (PeekCellOn && widget && IsGrailGrid(widget)) return TileRect(widget, out, size, static_cast<std::int32_t>(PeekCell), static_cast<std::int32_t>(PeekCell >> 32)), out;
	GridCellRect(widget, out, size, cell);
	int hx = 0, hy = 0;
	if (widget && IsGrailGrid(widget) && HalfShift(cell, hx, hy)) {
		int across = 0, down = 0;
		HalfCell(widget, across, down);
		out[0] += hx * across;
		out[1] += hy * down;
	}
	return out;
}

std::atomic<std::uint64_t> HoverCell { ~std::uint64_t { 0 } }; // the grail grid's last cell looked up under a point (the mouse)
std::uint64_t* HookGridCellAtPointInner(void* widget, std::uint64_t* out, std::uint64_t point) noexcept;
std::uint64_t* HookGridCellAtPoint(void* widget, std::uint64_t* out, std::uint64_t point) noexcept {
	if (!widget || !IsGrailGrid(widget)) return GridCellAtPoint(widget, out, point);
	HookGridCellAtPointInner(widget, out, point);
	HoverCell = *out;
	return out;
}
std::uint64_t* HookGridCellAtPointInner(void* widget, std::uint64_t* out, std::uint64_t point) noexcept {
	int across = 0, down = 0;
	HalfCell(widget, across, down);
	const auto back = [&](int hx, int hy) {
		const auto x = static_cast<std::uint32_t>(static_cast<std::int32_t>(point) - hx * across);
		const auto y = static_cast<std::uint32_t>(static_cast<std::int32_t>(point >> 32) - hy * down);
		return x | (std::uint64_t { y } << 32);
	};
	GridCellAtPoint(widget, out, point);
	// Over a shifted tile's cells: the cell its shifted items show there.
	int hx = 0, hy = 0;
	if (HalfShift(*out, hx, hy)) return GridCellAtPoint(widget, out, back(hx, hy));
	// Over a shifted item's part that sticks out of its tile's cells.
	std::vector<HalfTile> tiles;
	AcquireSRWLockShared(&HalfLock);
	tiles = HalfTiles;
	ReleaseSRWLockShared(&HalfLock);
	const auto plain = *out;
	for (const auto& tile : tiles) {
		std::uint64_t cell = ~std::uint64_t { 0 };
		GridCellAtPoint(widget, &cell, back(tile.hx, tile.hy));
		const int x = static_cast<std::int32_t>(cell) - OriginX(), y = static_cast<std::int32_t>(cell >> 32) - OriginY();
		if (cell != ~std::uint64_t { 0 } && y >= tile.y && y < tile.y + tile.height && x >= tile.x && x < tile.x + tile.width) return *out = cell, out;
	}
	*out = plain;
	return out;
}

// ---- no "cannot wear" on the grail's items ----
// The grid's draw (vt3) marks the grail grid's drawing; meanwhile the item
// requirement check says every item can be used: the game's (thunk slot
// Base+0x3E2A808, called from Base+0x2C3774, D2RCore's
// CheckInventoryItemRequirementsForDisplay); and Item Quality Backgrounds'
// cannot-equip check (1.2.42: module+0xECD0, called once from +0xAD04, al 0 =
// no mark) answers "can equip" (its call goes through a stub near the module).
constexpr int VtDraw = 3;
using GridDrawFn     = std::uint64_t (*)(void*, void*, void*, void*) noexcept;
using RequirementFn  = std::int32_t (*)(void*, void*, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t) noexcept;
GridDrawFn    GridDraw {};
using IqbCannotFn    = bool (*)(void* state, void* item) noexcept;
RequirementFn GameRequirement {};
IqbCannotFn   IqbCannot {};
std::uint8_t* IqbCallSite {};
void*         IqbStub {};
bool          InGrailDraw {};

bool ViewOpen();
void PeekTooltip(void* widget);
void MoveMoldFrom(void* grid);
std::uint64_t HookGridDraw(void* widget, void* a, void* b, void* c) noexcept {
	const bool grail = widget && IsGrailGrid(widget);
	// The host's grid is in the host's panel, closed a moment after the view: not drawn meanwhile.
	if (grail && Host::On() && !ViewOpen()) return 0;
	if (!grail && widget) MoveMoldFrom(widget); // the inventory's grid: the grail button, moved by the settings
	const bool before = InGrailDraw;
	InGrailDraw       = grail;
	const auto result = GridDraw(widget, a, b, c);
	InGrailDraw       = before;
	if (grail) PeekTooltip(widget);
	return result;
}
std::int32_t HookGameRequirement(void* item, void* player, std::uint64_t a, std::uint64_t b, std::uint64_t c, std::uint64_t d, std::uint64_t e) noexcept {
	return InGrailDraw ? 1 : GameRequirement(item, player, a, b, c, d, e);
}
bool HookIqbCannot(void* state, void* item) noexcept { return InGrailDraw ? false : IqbCannot(state, item); }

// Points the rel32 call at `site` to `target`.
bool PointCall(std::uint8_t* site, const void* target) {
	const auto rel = reinterpret_cast<std::intptr_t>(target) - reinterpret_cast<std::intptr_t>(site + 5);
	if (rel < INT32_MIN || rel > INT32_MAX) return false;
	DWORD keep = 0;
	if (!VirtualProtect(site + 1, 4, PAGE_EXECUTE_READWRITE, &keep)) return false;
	const auto value = static_cast<std::int32_t>(rel);
	std::memcpy(site + 1, &value, 4);
	VirtualProtect(site + 1, 4, keep, &keep);
	FlushInstructionCache(GetCurrentProcess(), site, 5);
	return true;
}

bool SwapAt(std::uint64_t* slot, std::uint64_t from, std::uint64_t to) {
	DWORD keep = 0;
	if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &keep)) return false;
	const bool swapped = InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(slot), static_cast<LONG64>(to), static_cast<LONG64>(from)) == static_cast<LONG64>(from);
	VirtualProtect(slot, sizeof(*slot), keep, &keep);
	return swapped;
}

void InstallRequirementHooks() {
	const auto base  = static_cast<std::uintptr_t>(Context->exeBase);
	const auto* thunk = reinterpret_cast<const std::uint8_t*>(base + 0x3E2B298);
	std::int32_t rel  = 0;
	std::memcpy(&rel, thunk + 2, 4);
	if (thunk[0] == 0xFF && thunk[1] == 0x25 && rel == -0xA96) {
		auto*      slot    = reinterpret_cast<std::uint64_t*>(base + 0x3E2A808);
		const auto current = *slot;
		if (current && SwapAt(slot, current, reinterpret_cast<std::uint64_t>(&HookGameRequirement))) GameRequirement = reinterpret_cast<RequirementFn>(current);
	}
	if (!GameRequirement) Problem(false, "grailon: the game's cannot-wear colour stays on grail items (requirement check not where expected)");
	if (HMODULE iqb = GetModuleHandleW(L"d2rl-item-quality-backgrounds.dll")) {
		auto*                  module = reinterpret_cast<std::uint8_t*>(iqb);
		constexpr std::uint8_t call[] { 0xE8, 0xC7, 0x3F, 0x00, 0x00 }, start[] { 0x48, 0x89, 0x5C, 0x24, 0x18 };
		if (std::memcmp(module + 0xAD04, call, sizeof(call)) == 0 && std::memcmp(module + 0xECD0, start, sizeof(start)) == 0) {
			// A jump to the hook within reach of the call.
			for (std::uintptr_t at = (reinterpret_cast<std::uintptr_t>(module) & ~std::uintptr_t { 0xFFFF }) - 0x10000; !IqbStub && at > reinterpret_cast<std::uintptr_t>(module) - 0x70000000;
			     at -= 0x10000)
				IqbStub = VirtualAlloc(reinterpret_cast<void*>(at), 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
			if (IqbStub) {
				std::uint8_t jump[14] { 0xFF, 0x25, 0, 0, 0, 0 };
				const auto   hook = reinterpret_cast<std::uint64_t>(&HookIqbCannot);
				std::memcpy(jump + 6, &hook, 8);
				std::memcpy(IqbStub, jump, sizeof(jump));
				FlushInstructionCache(GetCurrentProcess(), IqbStub, sizeof(jump));
				IqbCannot = reinterpret_cast<IqbCannotFn>(module + 0xECD0);
				if (PointCall(module + 0xAD04, IqbStub)) IqbCallSite = module + 0xAD04;
			}
		}
		if (!IqbCallSite) Problem(false, "grailon: Item Quality Backgrounds' cannot-equip mark stays on grail items (not version 1.2.42)");
	}
}

void UninstallRequirementHooks() {
	if (GameRequirement) (void)SwapAt(reinterpret_cast<std::uint64_t*>(static_cast<std::uintptr_t>(Context->exeBase) + 0x3E2A808), reinterpret_cast<std::uint64_t>(&HookGameRequirement), reinterpret_cast<std::uint64_t>(GameRequirement));
	if (IqbCallSite) (void)PointCall(IqbCallSite, reinterpret_cast<const void*>(IqbCannot));
	GameRequirement = nullptr;
	IqbCallSite     = nullptr; // the stub stays: a draw may still be in it
}

void InstallSoundMute();

// UI thread, once a view opens.
void WrapGridVtable() {
	static bool tried = false;
	if (tried) return;
	tried      = true;
	InstallSoundMute();
	GridVtable = reinterpret_cast<void**>(static_cast<std::uintptr_t>(Context->exeBase) + 0x1CF3800);
	// Entries nobody wraps tell the build; the wrapped ones are chained as they are.
	const auto base = static_cast<std::uintptr_t>(Context->exeBase);
	if (GridVtable[25] != reinterpret_cast<void*>(base + 0x2C49F0) || GridVtable[33] != reinterpret_cast<void*>(base + 0x2C3310)) {
		Problem(false, "grailon: the item grid is not the expected one; 1-wide items stay in their first column");
		GridVtable = nullptr;
		return;
	}
	DWORD protect = 0;
	if (!VirtualProtect(GridVtable, (VtCellRect + 1) * sizeof(void*), PAGE_READWRITE, &protect)) {
		GridVtable = nullptr;
		return;
	}
	GridCellRect             = reinterpret_cast<CellRectFn>(GridVtable[VtCellRect]);
	GridCellAtPoint          = reinterpret_cast<CellAtPointFn>(GridVtable[VtCellAtPoint]);
	GridDraw                 = reinterpret_cast<GridDrawFn>(GridVtable[VtDraw]);
	GridVtable[VtCellRect]    = reinterpret_cast<void*>(&HookGridCellRect);
	GridVtable[VtCellAtPoint] = reinterpret_cast<void*>(&HookGridCellAtPoint);
	GridVtable[VtDraw]        = reinterpret_cast<void*>(&HookGridDraw);
	VirtualProtect(GridVtable, (VtCellRect + 1) * sizeof(void*), protect, &protect);
	InstallRequirementHooks();
}

void UnwrapGridVtable() {
	if (!GridVtable || !GridCellRect) return;
	DWORD protect = 0;
	if (!VirtualProtect(GridVtable, (VtCellRect + 1) * sizeof(void*), PAGE_READWRITE, &protect)) return;
	if (GridVtable[VtCellRect] == reinterpret_cast<void*>(&HookGridCellRect)) GridVtable[VtCellRect] = reinterpret_cast<void*>(GridCellRect);
	if (GridVtable[VtCellAtPoint] == reinterpret_cast<void*>(&HookGridCellAtPoint)) GridVtable[VtCellAtPoint] = reinterpret_cast<void*>(GridCellAtPoint);
	if (GridDraw && GridVtable[VtDraw] == reinterpret_cast<void*>(&HookGridDraw)) GridVtable[VtDraw] = reinterpret_cast<void*>(GridDraw);
	UninstallRequirementHooks();
	VirtualProtect(GridVtable, (VtCellRect + 1) * sizeof(void*), protect, &protect);
}

// Lock held. Where a tile's item sits in it: centred, half cells included (a
// 1-wide item in its two-cell slot, a stored runeword smaller than its tile).
// `dx`, `dy` in pixels from the tile's corner; its size in cells.
struct ItemBox {
	int dx {}, dy {}, width {}, height {};
};
ItemBox ItemBoxOf(const Tile& tile) {
	int width = tile.width, height = tile.height;
	if (Uniques[tile.unique].runeword)
		if (const auto code = StoredCodes.find({ tile.unique, tile.eth }); code != StoredCodes.end()) (void)ItemSize(code->second, width, height);
	width  = std::clamp(width, 1, std::max(tile.width, 1));
	height = std::clamp(height, 1, std::max(tile.height, 1));
	return { (std::max(tile.width, SlotWidth) - width) * Cell / 2, (tile.height - height) * Cell / 2, width, height };
}

// Lock held. The grid moves a tile's cells by the half cell its item's whole
// cells leave (the item itself sits on the whole cells, centred).
void AddHalf(std::vector<HalfTile>& half, const Tile& tile) {
	const auto box   = ItemBoxOf(tile);
	const int  width = std::max(tile.width, SlotWidth);
	const int  hx = (width - box.width) % 2, hy = (tile.height - box.height) % 2;
	if (hx || hy) half.push_back({ tile.x, tile.y, width, tile.height, hx, hy });
}

void PlaceRows(const std::vector<Row>& rows) {
	PlacedRows = rows;
	RowCount  = static_cast<int>(rows.size());
	MaxScroll = RowCount;
	for (int height = 0; MaxScroll > 0 && height + rows[MaxScroll - 1].height <= ViewRows;) height += rows[--MaxScroll].height;
	BarMax = MaxScroll;
	// An item dropped for another category or row: show the row with its tile.
	// An item dropped for another category or row: scroll as little as it takes
	// to show the row with its tile.
	if (JumpTo >= 0)
		for (int r = 0; r < RowCount; ++r)
			for (const auto& tile : rows[r].tiles)
				if (tile.unique == JumpTo && tile.eth == JumpEth) {
					if (r < Scroll) Scroll = r;
					for (int height = 0;; ++Scroll) {
						height = 0;
						for (int k = Scroll; k <= r; ++k) height += rows[k].height;
						if (height <= ViewRows || Scroll >= r) break;
					}
				}
	JumpTo = -1;
	Scroll = std::clamp(Scroll, 0, MaxScroll);
	std::vector<HalfTile> half;
	Shown.clear();
	ShownHeaders.clear();
	Peeking.clear();
	int y = 0, row = Scroll;
	for (; row < RowCount && y + rows[row].height <= ViewRows; ++row) {
		const int header = rows[row].header >= 0 ? HeaderRows : 0;
		if (header) ShownHeaders.push_back({ rows[row].header, y });
		for (auto tile : rows[row].tiles) {
			tile.y = y + header; // page row: its label is in the rows above (above the grid for the first row)
			// Under the host the page stops at PageRows: tiles past it are pictures.
			if (tile.y + tile.height > PageRows) {
				Peeking.push_back(tile);
				continue;
			}
			Shown.push_back(tile);
			AddHalf(half, tile);
		}
		y += rows[row].height;
	}
	LastRow = row - 1;
	// The next row: its set name and its labels when they fit; its items that
	// fit whole are shown, the others only as cut slots.
	if (row < RowCount) {
		const int header = rows[row].header >= 0 ? HeaderRows : 0;
		if (header && y + header <= ViewRows) ShownHeaders.push_back({ rows[row].header, y });
		if (y + header + LabelRows <= ViewRows)
			for (auto tile : rows[row].tiles) {
				tile.y = y + header;
				if (tile.y + tile.height <= PageRows) {
					Shown.push_back(tile);
					AddHalf(half, tile);
				} else Peeking.push_back(tile);
			}
	}
	NextHalfTiles    = std::move(half);
	HalfTilesWaiting = true;
}

// Lock held. Whether an unfound entry shows its own name, and its own art (a
// runeword: its runes): found, or with that spoiler on.
bool NameShown(int u) {
	const auto& entry = Uniques[u];
	return Present.count(u) || entry.misc || (entry.runeword ? Config.runewordNames : entry.set ? Config.setItemNames : Config.uniqueNames);
}
bool ArtShown(int u) {
	const auto& entry = Uniques[u];
	return Present.count(u) || entry.misc || (entry.runeword ? Config.runewordRunes : entry.set ? Config.setItemArt : Config.uniqueArt);
}

// Lock held. Whether a set's name shows: with spoilers, or once one of its
// items is found.
bool SetKnown(int set) {
	if (Config.setNames) return true;
	for (const int u : Present)
		if (u >= 0 && u < UniqueCount() && Uniques[u].set && Uniques[u].setId == set) return true;
	return false;
}

// A row header's string key: its set's name ("Unknown set" until one of its
// items is found, spoilers off), or its runewords' socket count.
std::string HeaderKey(int header) {
	if (header >= RunewordGroup) return "RS" + std::to_string(header - RunewordGroup);
	return SetKnown(header) && SetNames.count(header) && !SetNames.at(header).empty() ? "S" + std::to_string(header) : std::string("UnknownSet");
}

// Set names in the set items' green, runeword groups in the runewords' gold.
const char* HeaderColor(int header) { return header >= RunewordGroup ? "$FontColorGoldYellow" : "$FontColorGreen"; }

bool TileShown(int unique, bool eth) {
	for (const auto& tile : Shown)
		if (tile.unique == unique && tile.eth == eth) return true;
	return false;
}

// ---- layout ----

std::string Json(const std::string& text) {
	std::string out = "\"";
	for (const char c : text) {
		if (c == '"' || c == '\\') out += '\\';
		if (c == '\n') out += "\\n";
		if (static_cast<unsigned char>(c) >= 0x20) out += c;
	}
	return out + '"';
}

std::string Rect(int x, int y, int width, int height) {
	char text[128] {};
	std::snprintf(text, sizeof(text), R"("rect": {"x": %d, "y": %d, "width": %d, "height": %d})", x, y, width, height);
	return text;
}

// A wrapped label: the game's own text style for item names in slots, its
// lines at the bottom of the box.
std::string Label(const std::string& name, int x, int y, int width, int height, const std::string& key, const char* color, int size, const char* align = "center",
                  const char* vertical = "bottom") {
	// An objective's name: its own text (the registered texts stay as they are when objectives change).
	std::string text = "@grail-on:" + key;
	if (key.size() > 1 && (key[0] == 'N' || key[0] == 'B') && std::isdigit(static_cast<unsigned char>(key[1])))
		if (const int u = std::atoi(key.c_str() + 1); u >= 0 && u < UniqueCount() && Uniques[u].objective) {
			text = Uniques[u].name;
			while (!text.empty() && (text.front() == '@' || text.front() == '$')) text.erase(text.begin());
		}
	return R"(, {"type": "TextBoxWidget", "name": )" + Json(name) + R"(, "fields": {)" + Rect(x, y, width, height) + R"(, "text": )" + Json(text)
	     + R"(, "style": {"fontColor": ")" + color + R"(", "pointSize": )" + std::to_string(size) + R"(, "dropShadow": "$DefaultDropShadow", "spacing": "$ReducedSpacing", "alignment": {"h": ")"
	     + align + R"(", "v": ")" + vertical + R"("}, "options": {"lineWrap": true}}}})";
}

std::string Image(const std::string& name, int x, int y, const std::string& filename, const std::string& extra = "") {
	char rect[96] {};
	std::snprintf(rect, sizeof(rect), R"("rect": {"x": %d, "y": %d})", x, y);
	return R"(, {"type": "ImageWidget", "name": )" + Json(name) + R"(, "fields": {)" + rect + R"(, "filename": )" + Json(filename) + extra + "}}";
}

// Item art in gray, the way the Item Dictionary mod recolours item art: every
// hue, saturation and brightness is in range ([middle, half width]), then
// saturation and brightness are shifted.
std::string GrayFields() {
	char text[320] {};
	std::snprintf(text, sizeof(text),
	              R"(, "useAdvancedColoring": true, "recolorRange": {"hue": [0.5, 0.501], "saturation": [0.5, 0.501], "value": [0.5, 0.501]}, "hsvTransform": [0.0, %.2f, %.2f, 0.0], "tintTransform": [0.0, 0.0, 0.0, 0.0])",
	              Config.saturation / 100.0, Config.brightness / 100.0);
	return text;
}

std::string Command(const std::string& command) { return Json(std::string("PanelManager:ClosePanel:") + CommandPrefix + command); }

// A square button: an empty slot, gold-rimmed when on, sending `command`.
std::string SquareButton(const std::string& name, int x, int y, bool on, const char* sprite, const std::string& tooltip, const std::string& command, int size = ButtonSize) {
	char button[768] {};
	std::snprintf(button, sizeof(button),
	              R"(, {"type": "ButtonWidget", "name": %s, "fields": {"rect": {"x": %d, "y": %d, "width": %d, "height": %d}, "filename": "d2rloader\\grail-on\\%s%s", "normalFrame": 0, "hoveredFrame": 1, "pressedFrame": 2, "tooltipString": %s, "sound": "cursor_menu_select_hd", "onClickMessage": %s}})",
	              Json(name).c_str(), x, y, size, size, sprite, on ? "On" : "", Json(tooltip).c_str(), Command(command).c_str());
	return button;
}

// An invisible button: its tooltip on hover, `command` on a click (none when empty).
std::string HitButton(const std::string& name, int x, int y, int width, int height, const std::string& tooltip, const std::string& command) {
	std::string out = R"(, {"type": "ButtonWidget", "name": )" + Json(name) + R"(, "fields": {)" + Rect(x, y, width, height)
	                + R"(, "filename": "d2rloader\\grail-on\\blank", "normalFrame": 0, "hoveredFrame": 1, "pressedFrame": 1, "tooltipString": )" + Json(tooltip);
	if (!command.empty()) out += R"(, "sound": "cursor_menu_select_hd", "onClickMessage": )" + Command(command);
	return out + "}}";
}

// A button on a settings sprite, its text on it (centred).
std::string TextButton(const std::string& name, int x, int y, int width, int height, const char* sprite, int normal, int hovered, int pressed, const std::string& text, const char* color,
                       const std::string& tooltip, const std::string& command) {
	std::string out = R"(, {"type": "ButtonWidget", "name": )" + Json(name) + R"(, "fields": {)" + Rect(x, y, width, height) + R"(, "filename": "d2rloader\\grail-on\\)" + sprite
	                + R"(", "normalFrame": )" + std::to_string(normal) + R"(, "hoveredFrame": )" + std::to_string(hovered) + R"(, "pressedFrame": )" + std::to_string(pressed);
	if (!text.empty())
		out += R"(, "textString": )" + Json(Tr(text)) + R"(, "textColor": ")" + color
		     + R"(", "text/style": {"pointSize": 30, "dropShadow": "$DefaultDropShadow", "spacing": "$ReducedSpacing", "alignment": {"h": "center", "v": "center"}})";
	if (!tooltip.empty()) out += R"(, "tooltipString": )" + Json(tooltip);
	if (!command.empty()) out += R"(, "sound": "cursor_menu_select_hd", "onClickMessage": )" + Command(command);
	return out + "}}";
}

// A tooltip's text in lines of about `width` characters.
// The text is translated first. Widths count characters, Chinese, Japanese
// and Korean ones as two; Chinese and Japanese lines break between any two of
// their characters, others at spaces.
std::string Wrap(const std::string& english, std::size_t width = 46) {
	const std::string text = Tr(english);
	struct Piece {
		std::string text;
		std::size_t width {};
		bool        space {}; // a space before it
	};
	std::vector<Piece> pieces;
	Piece              word;
	const auto         flush = [&]() {
        if (!word.text.empty()) pieces.push_back(word);
        word = {};
	};
	for (std::size_t at = 0; at < text.size();) {
		const auto    c    = static_cast<unsigned char>(text[at]);
		const int     size = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
		std::uint32_t code = size == 1 ? c : c & (0x3F >> (size - 1));
		for (int k = 1; k < size && at + k < text.size(); ++k) code = (code << 6) | (static_cast<unsigned char>(text[at + k]) & 0x3F);
		if (c == ' ' || c == '\n') {
			flush();
			if (c == '\n') pieces.push_back({ "\n" });
			else word.space = true;
			++at;
			continue;
		}
		const bool hangul = (code >= 0x1100 && code <= 0x115F) || (code >= 0xAC00 && code <= 0xD7A3);
		const bool wide   = hangul || (code >= 0x2E80 && code <= 0xA4CF) || (code >= 0xF900 && code <= 0xFAFF) || (code >= 0xFE30 && code <= 0xFE4F) || (code >= 0xFF00 && code <= 0xFF60)
		               || (code >= 0xFFE0 && code <= 0xFFE6);
		if (wide && !hangul) {
			const bool space = word.space && word.text.empty();
			flush();
			word = { text.substr(at, size), 2, space };
			flush();
		} else word.text += text.substr(at, size), word.width += wide ? 2 : 1;
		at += size;
	}
	flush();
	std::string out, line;
	std::size_t used = 0;
	for (const auto& piece : pieces) {
		if (piece.text == "\n") {
			out += line + "\n", line.clear(), used = 0;
			continue;
		}
		const std::size_t gap = !line.empty() && piece.space ? 1 : 0;
		if (!line.empty() && used + gap + piece.width > width) out += line + "\n", line.clear(), used = 0;
		else if (gap) line += ' ', ++used;
		line += piece.text, used += piece.width;
	}
	return out + line;
}

// The see-through look of ethereal items, for the ethereal menus' icons.
const std::string EthLook = ", \"transparency\": 0.45";

// Item art (or a plugin sprite) fitted in a square box at x, y, centered.
std::string Icon(const std::string& name, int x, int y, int box, const std::string& art, int width, int height, const std::string& extra = "", int artSize = 0) {
	const int    unit  = artSize > 0 ? artSize : Cell; // the art's pixels per width/height unit
	const double scale = static_cast<double>(box) / (std::max(width, height) * unit);
	char         icon[512] {};
	std::snprintf(icon, sizeof(icon), R"(, {"type": "ImageWidget", "name": %s, "fields": {"rect": {"x": %d, "y": %d, "scale": %.4f}, "filename": %s%s}})", Json(name).c_str(),
	              x + static_cast<int>((box - width * unit * scale) / 2), y + static_cast<int>((box - height * unit * scale) / 2), scale, Json(art).c_str(), extra.c_str());
	return icon;
}

// Whether a menu has a button: Misc only while one of its categories has tiles.
bool MenuShown(int menu) {
	if (menu == ObjMenu) return Config.objectives && std::any_of(Objectives.begin(), Objectives.end(), [](const Objective& objective) { return objective.enabled; });
	if (menu != MiscMenu) return true;
	for (int c = 0; c < CategoryCount(); ++c)
		if (InMenu(c, MiscMenu)) return true;
	return false;
}

// The menu on screen: the current category's, or the settings.
int CurrentMenu() {
	if (CurrentCategory == StatsView) return StatsMenu;
	return CurrentCategory < 0 || CurrentCategory >= CategoryCount() ? ConfigMenu : MenuOf(CurrentCategory, ViewEth);
}

// Where category button `c` sits: in the column on the left, with the others
// of its menu, a gap between groups.
int ButtonTop(int c) {
	const int menu  = CurrentMenu();
	int       index = 0, gaps = 0, count = 0, groups = 0, previous = -1;
	for (int o = 0; o < CategoryCount(); ++o) {
		if (!InMenu(o, menu)) continue;
		if (previous >= 0 && Categories[o].group != Categories[previous].group) ++groups;
		if (o <= c) index = count, gaps = groups;
		++count;
		previous = o;
	}
	const int step = std::min(Cell, (ViewRows * Cell - groups * CatGap) / std::max(1, count));
	return ViewTop + index * step + gaps * CatGap + (step - CatSize) / 2;
}

// A menu's icon: a weapon, an armor, the first set's first item, or the
// plugin's own (stats, settings). Its base size in cells.
std::string MenuArt(int menu, int& width, int& height) {
	width = height = 1;
	switch (menu) {
	case StatsMenu: return "d2rloader\\grail-on\\icon-stats";
	case WeaponMenu:
	case EthWeaponMenu: return "d2rloader\\grail-on\\icon-weapons";
	case ArmorMenu:
	case EthArmorMenu: return "d2rloader\\grail-on\\icon-armor";
	case SetMenu: return "d2rloader\\grail-on\\icon-sets";
	case RunewordMenu: return "d2rloader\\grail-on\\icon-runewords";
	case MiscMenu: return "d2rloader\\grail-on\\icon-other";
	case ObjMenu: return "d2rloader\\grail-on\\icon-objectives";
	default: return "d2rloader\\grail-on\\icon-config";
	}
}

// Moves every widget right by MenuRoom: the view is laid out from the window's
// corner, the panel starts MenuRoom left of it.
std::string ShiftLayout(const std::string& layout) {
	static const std::string key = R"("rect": {"x": )";
	std::string              shifted;
	std::size_t              from = 0;
	for (std::size_t at; (at = layout.find(key, from)) != std::string::npos;) {
		at += key.size();
		char*      end = nullptr;
		const long x   = std::strtol(layout.c_str() + at, &end, 10);
		shifted += layout.substr(from, at - from) + std::to_string(x + MenuRoom);
		from = static_cast<std::size_t>(end - layout.c_str());
	}
	return shifted + layout.substr(from);
}

// Whether Item Quality Backgrounds is loaded: its background sprites are then
// drawn under the tiles the plugin draws itself (the grid's items get them from
// it directly).
bool QualityBackgrounds() { return GetModuleHandleW(L"d2rl-item-quality-backgrounds.dll") != nullptr; }

// Where an item of width x height cells has its sockets, in socket order (rows
// top to bottom, left to right): their centres in cells, and the room each has
// (1 for a whole cell). One column for narrow items and up to 3 sockets, two
// columns from 4 (5 as 2-1-2, the middle one first), like the game's sockets.
std::pair<std::vector<std::pair<double, double>>, double> SocketSpots(int width, int height, int count) {
	std::vector<std::pair<double, double>> spots;
	if (count <= 0) return { spots, 1.0 };
	const double midX = width / 2.0, midY = height / 2.0;
	std::vector<int> rows; // sockets per row
	if (width < 2 || (count <= 3 && height >= count)) rows.assign(count, 1);
	else if (count == 3) rows = { 2, 1 };
	else if (count == 5) rows = { 2, 1, 2 };
	else rows.assign((count + 1) / 2, 2);
	const double step = std::min(1.0, static_cast<double>(height) / rows.size());
	for (std::size_t r = 0; r < rows.size(); ++r) {
		const double y = midY + (r - (rows.size() - 1) / 2.0) * step;
		if (rows[r] == 1) spots.push_back({ midX, y });
		else spots.push_back({ midX - 0.5, y }), spots.push_back({ midX + 0.5, y });
	}
	// The game fills 2-1-2 sockets from the middle one (Call to Arms in game).
	if (rows.size() == 3 && rows[1] == 1 && spots.size() == 5) std::rotate(spots.begin(), spots.begin() + 2, spots.begin() + 3);
	return { spots, step };
}
constexpr double RuneScale = 0.8; // a rune stone's size in its socket

// Lock held. A perfect tile's glow around its slot (`left`, `top`: the tile's
// corner): the game plays its frames itself (D2UI AnimatedImageWidget: fps,
// loop, playing), so nothing has to find and switch them. Without
// animations, or in a scroll's frames, its first frame alone.
std::string StillGlow(const Tile& tile, const std::string& n, int left, int top, const std::string& extra = "");
std::string GlowImage(const Tile& tile, const std::string& n, int left, int top) {
	if (!Config.frameAnimations) return StillGlow(tile, n, left, top);
	char rect[96] {};
	std::snprintf(rect, sizeof(rect), R"("rect": {"x": %d, "y": %d})", left + (tile.width == 1 ? Cell / 2 : 0) - GlowPad, top - GlowPad);
	return R"(, {"type": "AnimatedImageWidget", "name": )" + Json(n) + R"(, "fields": {)" + rect + R"(, "filename": )"
	     + Json("d2rloader\\grail-on\\glow" + std::to_string(std::clamp(tile.width, 1, 4)) + "x" + std::to_string(std::clamp(tile.height, 1, 4)))
	     + R"(, "fps": 8, "loop": true, "playing": true, "transparency": )" + std::to_string(GlowOpacity) + "}}";
}
std::string StillGlow(const Tile& tile, const std::string& n, int left, int top, const std::string& extra) {
	return Image(n, left + (tile.width == 1 ? Cell / 2 : 0) - GlowPad, top - GlowPad,
	             "d2rloader\\grail-on\\glow" + std::to_string(std::clamp(tile.width, 1, 4)) + "x" + std::to_string(std::clamp(tile.height, 1, 4)), extra + ", \"transparency\": " + std::to_string(GlowOpacity));
}

// Lock held. A runeword tile's picture: its stored item's base, centred by
// whole cells as the item sits in the tile, else its runes, in rows of the
// tile's width, scaled to fit. `left`, `top`: the tile's corner.
std::string RunewordArt(const Tile& tile, const std::string& n, int left, int top, bool stored, const std::string& extra) {
	const auto& entry = Uniques[tile.unique];
	std::string out;
	if (stored) {
		// The stored item as the grid shows it: centred in the tile, on its
		// quality background, its runes in its sockets.
		const auto code = StoredCodes.find({ tile.unique, tile.eth });
		const auto base = code == StoredCodes.end() ? Bases.end() : Bases.find(code->second);
		if (base == Bases.end() || base->second.asset.empty()) return out;
		const auto box = ItemBoxOf(tile);
		const int  x = left + box.dx, y = top + box.dy;
		const bool bare = BaseTiles.count({ tile.unique, tile.eth }) != 0; // a base: empty sockets
		if (QualityBackgrounds())
			out += Image(n + "Back", x, y, std::string("d2rloader\\item-quality-backgrounds\\") + (bare ? "normal\\" : "runeword\\") + std::to_string(box.width) + "x" + std::to_string(box.height),
			             bare ? ", \"transparency\": 0.65" : ", \"transparency\": 0.55");
		// The game's sockets are about 62% of a cell, their runes 55%. The
		// sockets are under the item, at about 60% (the game's grid in Théo's
		// screenshot), the runes over it.
		const auto   spots = SocketSpots(box.width, box.height, static_cast<int>(entry.runes.size()));
		const double room  = Cell * std::min(1.0, spots.second) * 0.62;
		char         rect[256] {};
		for (std::size_t k = 0; k < spots.first.size(); ++k) {
			const auto [cx, cy] = spots.first[k];
			const int  sx = x + static_cast<int>(cx * Cell - room / 2), sy = y + static_cast<int>(cy * Cell - room / 2);
			std::snprintf(rect, sizeof(rect), R"(, {"type": "ImageWidget", "name": "%sSocket%zu", "fields": {"rect": {"x": %d, "y": %d, "scale": %.4f}, "filename": "PANEL\\gemsocket", "transparency": 0.6}})", n.c_str(), k,
			              sx, sy, room / std::max(GemSocketSize.load(), 1));
			out += rect;
		}
		out += Image(n, x, y, base->second.asset);
		for (std::size_t k = 0; k < spots.first.size() && !bare; ++k) {
			const auto rune = Bases.find(entry.runes[k]);
			if (rune == Bases.end() || rune->second.asset.empty()) continue;
			const auto [cx, cy] = spots.first[k];
			const double size = room * 0.55 / 0.62;
			const int    rx = x + static_cast<int>(cx * Cell - size / 2), ry = y + static_cast<int>(cy * Cell - size / 2);
			std::snprintf(rect, sizeof(rect), R"(, {"type": "ImageWidget", "name": "%sRune%zu", "fields": {"rect": {"x": %d, "y": %d, "scale": %.4f}, "filename": )", n.c_str(), k, rx, ry, size / Cell);
			out += rect + Json(rune->second.asset) + "}}";
		}
		return out;
	}
	if (tile.width == 1) left += Cell / 2; // a 1-wide tile is centred in its slot
	// Its runes where an item of the tile's size with that many sockets has
	// them, in socket order, as the Chronicle's grey stones; blank stones while
	// it is unfound with spoilers off.
	const bool hidden = !ArtShown(tile.unique);
	const auto spots = SocketSpots(tile.width, tile.height, static_cast<int>(entry.runes.size()));
	const double size = RuneScale * Cell * std::min(1.0, spots.second);
	for (std::size_t k = 0; k < entry.runes.size() && k < spots.first.size(); ++k) {
		std::string art;
		const auto& code = entry.runes[k];
		const int   index = code.size() == 3 && code[0] == 'r' ? std::atoi(code.c_str() + 1) - 1 : -1;
		if (GenericRunes && hidden) art = "d2rloader\\grail-on\\grune-blank";
		else if (index >= 0 && index < static_cast<int>(std::size(RuneCell)) && RuneCell[index] < GenericRunes) art = "d2rloader\\grail-on\\grune" + std::to_string(RuneCell[index]);
		else if (const auto rune = Bases.find(code); rune != Bases.end()) art = rune->second.asset;
		if (art.empty()) continue;
		const auto [cx, cy] = spots.first[k];
		char rect[200] {};
		std::snprintf(rect, sizeof(rect), R"(, {"type": "ImageWidget", "name": "%s_%zu", "fields": {"rect": {"x": %d, "y": %d, "scale": %.4f}, "filename": )", n.c_str(), k,
		              left + static_cast<int>(cx * Cell - size / 2), top + static_cast<int>(cy * Cell - size / 2), size / Cell);
		out += rect + Json(art) + extra + "}}";
	}
	return out;
}

// Lock held. A tile drawn as pictures (a cut row, a scroll frame), for a
// ClipViewWidget whose corner is at GridLeft, ViewTop: `top` is the item's top
// from ViewTop. Child rects are from the clip's corner; ShiftLayout moves them
// by MenuRoom like every rect, so that is taken off here. A stored item is
// parked off the page: its art stands in for it, on its quality background.
bool Dimmed(int u, bool eth);
std::string StoredArt(int u, bool eth, const std::string& art);
// A tile the search leaves out: a dark veil over its slot and art.
std::string DimVeil(const Tile& tile, const std::string& name, int left, int top) {
	return Image(name, left, top, "d2rloader\\grail-on\\dim" + std::to_string(std::clamp(tile.width, 1, 4)) + "x" + std::to_string(std::clamp(tile.height, 1, 4)));
}
std::string FakeTile(const Tile& tile, int top, const std::string& n, bool label, bool frames = false) {
	const auto& entry   = Uniques[tile.unique];
	const bool  stored  = Stored.count({ tile.unique, tile.eth }) != 0;
	const bool  present = Present.count(tile.unique) != 0;
	const bool  named   = NameShown(tile.unique), drawn = ArtShown(tile.unique);
	const int   left    = tile.x * Cell - MenuRoom, itemLeft = left + (tile.width == 1 ? Cell / 2 : 0);
	const auto  size    = std::to_string(tile.width) + "x" + std::to_string(tile.height);
	std::string out     = Image("FSlot" + n, itemLeft, top, "d2rloader\\grail-on\\slot" + size);
	if (stored && Perfect.count({ tile.unique, tile.eth }) && Config.perfectFrames) {
		out += frames ? GlowImage(tile, "FGlow" + n, left, top) : StillGlow(tile, "FGlow" + n, left, top);
	}
	if (entry.runeword) {
		out += RunewordArt(tile, "FArt" + n, left, top, stored, stored ? "" : GrayFields());
	} else if (stored && !entry.misc && QualityBackgrounds())
		out += Image("FBack" + n, itemLeft, top, std::string("d2rloader\\item-quality-backgrounds\\") + (entry.set ? "set\\" : "unique\\") + size, ", \"transparency\": 0.65");
	if (const auto& shown = drawn ? entry.art : entry.ghost; !entry.runeword && !shown.empty()) {
		const std::string art = stored ? StoredArt(tile.unique, tile.eth, shown) : shown; // a ring's or amulet's own picture
		out += Image("FArt" + n, itemLeft, top, art, stored ? ColorFields(entry, art) + (tile.eth ? EthLook : "") : GrayFields());
	}
	const bool dim = Dimmed(tile.unique, tile.eth);
	if (dim) out += DimVeil(tile, "FDim" + n, itemLeft, top);
	if (label) {
		const auto key = std::string(named ? "N" : "B") + (tile.eth ? "E" : "") + std::to_string(tile.unique);
		out += Label("FLabel" + n, left - LabelMargin, top - LabelRows * Cell + Cell / 2, SlotWidth * Cell + 2 * LabelMargin, LabelRows * Cell - Cell / 2, key,
		             present && !dim ? "$FontColorGoldYellow" : "$FontColorGray", Config.font, "center", "center");
	}
	return out;
}

// A ClipViewWidget over the view (GridLeft, ViewTop, 16 cells by ViewRows)
// holding `children`; the game clips their drawing to it (D2UI ClipViewWidget).
std::string ClipView(const std::string& name, const std::string& children, bool visible) {
	char clip[320] {};
	std::snprintf(clip, sizeof(clip), R"(, {"type": "ClipViewWidget", "name": "%s", "fields": {"rect": {"x": %d, "y": %d, "width": %d, "height": %d}, "clipMouseInput": true%s}, "children": [)",
	              name.c_str(), GridLeft, ViewTop, PageSize * Cell, ViewRows * Cell, visible ? "" : R"(, "visible": false)");
	return clip + (children.empty() ? std::string() : children.substr(1)) + "]}";
}

// Lock held. Rows `first`.. as pictures, the first row's top `top` pixels under
// the view's top: one frame of a scroll between two views.
std::string FakeRows(int first, int top, const std::string& prefix) {
	std::string out;
	int         n = 0;
	for (int r = std::max(first, 0), y = top; r < static_cast<int>(PlacedRows.size()) && y < ViewRows * Cell; ++r) {
		const auto& row    = PlacedRows[r];
		const int   header = row.header >= 0 ? HeaderRows : 0;
		if (y + row.height * Cell > 0) {
			if (header) {
				const auto name = HeaderKey(row.header);
				out += Label(prefix + "Set" + std::to_string(r), HeaderLeft - GridLeft - MenuRoom, y, HeaderWidth, Cell - 14, name, HeaderColor(row.header), Config.font + 8, "center", "center");
				out += Image(prefix + "Line" + std::to_string(r), HeaderLeft - GridLeft - MenuRoom, y + Cell - 14, "d2rloader\\grail-on\\divider");
			}
			for (const auto& tile : row.tiles) out += FakeTile(tile, y + (header + LabelRows) * Cell - Cell / 2, prefix + std::to_string(n++), true, true);
		}
		y += row.height * Cell;
	}
	return out;
}

// A scroll by one row is animated on a panel of its own over the view
// ("Scroll"), made for that step: the view's background over its tiles, and
// ScrollFrames - 1 frames of the rows moving, shown in turn; then the next view
// opens and it closes. (Frames kept in every view ran the game out of widgets.)
constexpr int ScrollFrames = 4;

// Lock held. The scroll bar's thumb: its height in steps (the share of rows on
// screen), and its top at a scroll of `quarter` / ScrollFrames rows.
// The same all the way down (rows differ in height, so the rows on screen vary).
int ThumbSteps() { return RowCount ? std::clamp((ScrollSteps * (RowCount - MaxScroll) + RowCount / 2) / RowCount, 1, ScrollSteps - 1) : 1; }
int ThumbTop(int steps, int quarter) { return BarTop + (ScrollSteps - steps) * StepHeight * quarter / std::max(1, MaxScroll * ScrollFrames); }

// Lock held. The scroll panel's layout for one row down (or up) from Scroll.
std::string ScrollLayout(bool down) {
	const int first = down ? Scroll : Scroll - 1;
	if (first < 0 || first >= static_cast<int>(PlacedRows.size())) return {};
	const int   height = PlacedRows[first].height * Cell;
	std::string layout;
	// The cover hides the view's rows, but not its close button (a copy over it
	// doubled the button's shadow): two clips leave the button's corner out.
	constexpr int coverLeft = 57, buttonLeft = 1600, buttonBottom = 100, coverWidth = 1571;
	const int     coverTop = ViewTop - 2, coverHeight = ViewRows * Cell + 2;
	char          cover[1024] {};
	std::snprintf(cover, sizeof(cover),
	              R"(, {"type": "ClipViewWidget", "name": "CoverLeft", "fields": {"rect": {"x": %d, "y": %d, "width": %d, "height": %d}}, "children": [)"
	              R"({"type": "ImageWidget", "name": "Cover", "fields": {"rect": {"x": %d, "y": 0}, "filename": "d2rloader\\grail-on\\cover"}}]})"
	              R"(, {"type": "ClipViewWidget", "name": "CoverRight", "fields": {"rect": {"x": %d, "y": %d, "width": %d, "height": %d}}, "children": [)"
	              R"({"type": "ImageWidget", "name": "Cover2", "fields": {"rect": {"x": %d, "y": %d}, "filename": "d2rloader\\grail-on\\cover"}}]})",
	              coverLeft, coverTop, buttonLeft - coverLeft, coverHeight, -MenuRoom, buttonLeft, buttonBottom, coverLeft + coverWidth - buttonLeft, coverTop + coverHeight - buttonBottom,
	              -MenuRoom - (buttonLeft - coverLeft), coverTop - buttonBottom);
	layout += cover;
	// The scroll bar's track hides the view's thumb; each frame has its own.
	const bool bar   = MaxScroll > 0;
	const int  steps = ThumbSteps();
	if (bar) layout += Image("Track", BarLeft, BarTop, "d2rloader\\grail-on\\track");
	for (int k = 1; k < ScrollFrames; ++k) {
		const std::string name = "Frame" + std::to_string(k);
		// A frame: a plain container (from the panel's corner: ShiftLayout moves
		// it and its children, so it starts MenuRoom left) with the rows and the thumb.
		std::string children = ClipView(name + "Rows", FakeRows(first, down ? -height * k / ScrollFrames : -height + height * k / ScrollFrames, name), true);
		if (bar) children += Image(name + "Thumb", BarLeft, ThumbTop(steps, Scroll * ScrollFrames + (down ? k : -k)), "d2rloader\\grail-on\\thumb" + std::to_string(steps));
		char frame[160] {};
		std::snprintf(frame, sizeof(frame), R"(, {"type": "Widget", "name": "%s", "fields": {"rect": {"x": %d, "y": 0}}, "children": [)", name.c_str(), -MenuRoom);
		layout += frame + children.substr(1) + "]}";
	}
	char open[512] {};
	std::snprintf(open, sizeof(open), R"({"type": "Panel", "name": "grail-on/Scroll", "fields": {"priority": 12, "rect": {"x": %d, "y": %d, "width": %d, "height": %d}, "anchor": {"x": 0.0, "y": 0.397}}, "children": [)",
	              WindowX() - MenuRoom, PanelY, PanelSpan(), PanelHeight);
	return open + ShiftLayout(layout).substr(2) + "]}";
}

// A text box showing `text` as it is (not a string key).
// `color`: a profile colour ("$FontColorWhite") or a colour object ({"r": ...}).
std::string TextLabel(const std::string& name, int x, int y, int width, int height, const std::string& text, const char* color, int size, const char* align = "left") {
	const std::string fill = color[0] == '{' ? std::string(color) : "\"" + std::string(color) + "\"";
	return R"(, {"type": "TextBoxWidget", "name": )" + Json(name) + R"(, "fields": {)" + Rect(x, y, width, height) + R"(, "text": )" + Json(Tr(text)) + R"(, "style": {"fontColor": )" + fill
	     + R"(, "pointSize": )" + std::to_string(size) + R"(, "dropShadow": "$DefaultDropShadow", "spacing": "$ReducedSpacing", "alignment": {"h": ")" + align + R"(", "v": "center"}}}})";
}

// Text as it is, wrapped in its box, from the top.
std::string TextBlock(const std::string& name, int x, int y, int width, int height, const std::string& text, const char* color, int size) {
	return R"(, {"type": "TextBoxWidget", "name": )" + Json(name) + R"(, "fields": {)" + Rect(x, y, width, height) + R"(, "text": )" + Json(Tr(text)) + R"(, "style": {"fontColor": ")" + color
	     + R"(", "pointSize": )" + std::to_string(size) + R"(, "dropShadow": "$DefaultDropShadow", "spacing": "$ReducedSpacing", "alignment": {"h": "left", "v": "top"}, "options": {"lineWrap": true}}}})";
}

// The grail page when something it needs is switched off: the frame, a title
// and what is wrong. Nothing on the characters was changed.
std::string ErrorLayout() {
	char open[1024] {};
	std::snprintf(open, sizeof(open),
	              R"({"type": "Panel", "name": "grail-on/Error", "fields": {"priority": 10, "rect": {"x": %d, "y": %d, "width": %d, "height": %d}, "anchor": {"x": 0.0, "y": 0.397}}, "children": [)"
	              R"({"type": "ClickCatcherWidget", "name": "PanelClickCatcher", "fields": {"rect": {"x": 0, "y": 0, "width": %d, "height": %d}}})"
	              R"(, {"type": "ImageWidget", "name": "Background", "fields": {"rect": {"x": 0, "y": 0}, "filename": "d2rloader\\grail-on\\panel"}})"
	              R"(, {"type": "ImageWidget", "name": "Cover", "fields": {"rect": {"x": 57, "y": %d}, "filename": "d2rloader\\grail-on\\cover"}})"
	              R"(, {"type": "ButtonWidget", "name": "CloseButton", "fields": {"rect": {"x": 1600, "y": 9}, "filename": "PANEL\\closebtn_4x", "hoveredFrame": 3, "tooltipString": "@d2r:strClose", "sound": "cursor_close_window_hd", "onClickMessage": %s}})",
	              WindowX(), PanelY, PanelWidth, PanelHeight, PanelWidth, PanelHeight, ViewTop - 2, Command("close").c_str());
	std::string layout = open;
	const int   left = GridLeft + Cell, width = 14 * Cell;
	int         y    = ViewTop + 120;
	layout += TextLabel("ErrorTitle", GridLeft, y, PageSize * Cell, 70, "The grail cannot work here", "$FontColorRed", 44, "center");
	y += 130;
	const auto notes = ProblemList();
	const auto add   = [&](const std::string& name, const std::string& text, const char* color) {
        const int lines = 1 + static_cast<int>(text.size()) / 70; // about 70 characters a line at this size
        layout += TextBlock(name, left, y, width, lines * 42 + 10, "- " + text, color, 30);
        y += lines * 42 + 26;
	};
	int n = 0;
	for (const auto& note : notes)
		if (note.blocking) add("Error" + std::to_string(n++), note.text, "$FontColorWhite");
	if (notes.size() > static_cast<std::size_t>(n)) {
		y += 40;
		layout += TextLabel("AlsoTitle", left, y, width, 50, "Also switched off", "$FontColorGoldYellow", 34);
		y += 70;
		for (const auto& note : notes)
			if (!note.blocking) add("Also" + std::to_string(n++), note.text, "$FontColorGray");
	}
	y += 40;
	layout += TextBlock("ErrorFoot", left, y, width, 100, "Nothing was changed on your characters or in the grail file. The details are in the grail-on log (d2rloader\\logs).", "$FontColorGray", 26);
	return layout + "]}";
}

// ---- stats ----
// The stats pages: an overview with the whole grail's ring chart, and a page
// per kind (uniques, ethereal copies, set items, runewords) with a tile per
// category, its ring around its icon. Every menu and category button has its
// ring too. The charts are pictures the plugin draws (ChartSprite), registered
// under a name made from their numbers, so a changed chart is a new picture.
int StatsPage = 0; // 0 overview, then the kinds
// Whether the view has its grid on screen and its tiles arranged: a
// category's, and the stats overview's (the standard's tile). Lock held.
bool TilesView() { return CurrentCategory >= 0 || (CurrentCategory == StatsView && StatsPage == 0); }
enum class Kind { Uniques, Ethereal, Sets, Runewords, Misc, Objectives };
constexpr const char* KindNames[] { "Uniques", "Ethereal", "Set Items", "Runewords", "Misc", "Objectives" };
constexpr Kind        AllKinds[] { Kind::Uniques, Kind::Ethereal, Kind::Sets, Kind::Runewords, Kind::Misc, Kind::Objectives };
constexpr int         KindCountAll = static_cast<int>(std::size(AllKinds));
bool                  MenuShown(int menu);
struct Rgb {
	int r, g, b;
};
// Perfect copies in the set items' green, stored ones in the uniques' gold,
// missing ones grey. An item taken out of the grail is missing again.
constexpr Rgb PerfectColor { 30, 205, 40 }, StoredColor { 199, 179, 119 }, BaseColor { 225, 225, 225 }, MissingColor { 92, 89, 84 };

struct KindCount {
	int perfect {}, stored {}, bases {}, missing {}; // stored: not perfect; bases: a runeword tile holding a base
	int total() const { return perfect + stored + bases + missing; }
	int inGrail() const { return perfect + stored; }
};
// Lock held. The tiles pick(u, eth) keeps, by state.
template <typename Pick>
KindCount CountTiles(Pick pick) {
	KindCount count;
	for (int u = 0; u < UniqueCount(); ++u)
		for (const bool eth : { false, true }) {
			if (Uniques[u].secret || !HasTile(u, eth) || !pick(u, eth)) continue;
			if (BaseTiles.count({ u, eth })) ++count.bases; // a base is not the runeword
			else if (!Stored.count({ u, eth })) ++count.missing;
			else if (Perfect.count({ u, eth })) ++count.perfect;
			else ++count.stored;
		}
	return count;
}
bool InKind(int u, bool eth, Kind kind) {
	const auto& entry = Uniques[u];
	switch (kind) {
	case Kind::Uniques: return !entry.set && !entry.runeword && !entry.misc && !eth;
	case Kind::Ethereal: return !entry.set && !entry.runeword && !entry.misc && eth;
	case Kind::Sets: return entry.set;
	case Kind::Misc: return entry.misc && !entry.objective;
	case Kind::Objectives: return entry.objective;
	default: return entry.runeword;
	}
}
KindCount CountKind(Kind kind) {
	return CountTiles([&](int u, bool eth) { return InKind(u, eth, kind); });
}
// Whether a stats page has its button: misc while something is tracked, objectives while there are some.
bool StatsPageShown(int page) { return page == 6 ? MenuShown(ObjMenu) : page == 5 ? MenuShown(MiscMenu) : page >= 0 && page < 5; }
// Whether the overview counts a kind: it has a goal (all of them when none has).
bool InGoals(Kind kind) {
	if (kind == Kind::Misc) return MenuShown(MiscMenu); // misc: counted while something is tracked
	if (kind == Kind::Objectives) return MenuShown(ObjMenu); // objectives: while there are some
	return Config.goal[static_cast<int>(kind)] != GoalNone || std::all_of(std::begin(Config.goal), std::end(Config.goal), [](int goal) { return goal == GoalNone; });
}
// A menu's tiles: the kinds with a goal for the stats, none for the settings.
KindCount CountMenu(int menu) {
	if (menu == ConfigMenu) return {};
	if (menu == StatsMenu)
		return CountTiles([](int u, bool eth) {
			for (const Kind kind : AllKinds)
				if (InKind(u, eth, kind)) return InGoals(kind);
			return false;
		});
	return CountTiles([&](int u, bool eth) {
		const int c = Uniques[u].category;
		return c >= 0 && c < CategoryCount() && Categories[c].menu == BaseMenu(menu) && eth == IsEthMenu(menu);
	});
}
KindCount CountCategory(int c, bool eth) {
	return CountTiles([&](int u, bool tileEth) { return Uniques[u].category == c && tileEth == eth; });
}

std::string ChartSprite(const std::vector<std::pair<int, Rgb>>& parts, int size, int hole, bool square = false); // defined with the pictures

std::vector<std::pair<int, Rgb>> ChartParts(const KindCount& count) { return { { count.perfect, PerfectColor }, { count.stored, StoredColor }, { count.bases, BaseColor }, { count.missing, MissingColor } }; }
std::string Percent(int part, int whole) { return std::to_string(whole ? part * 100 / whole : 0) + "%"; }
// A tooltip's numbers.
std::string CountText(const std::string& name, const KindCount& count) {
	return Tr(name) + ": " + std::to_string(count.inGrail()) + " / " + std::to_string(count.total()) + " (" + Percent(count.inGrail(), count.total()) + "), " + std::to_string(count.perfect) + " "
	     + Tr("perfect") + ", " + std::to_string(count.stored) + " " + Tr("stored") + ", " + (count.bases ? std::to_string(count.bases) + " " + Tr("bases") + ", " : std::string())
	     + std::to_string(count.missing) + " " + Tr("missing");
}

// A button's chart: a square pie in its padding, around its icon, inside its
// frame (a round ring for the stats tiles). `hole`: the inner edge, in percent
// of the outer.
constexpr int RingPad = 6, ButtonHole = 78;
std::string ButtonRing(const std::string& name, int x, int y, int size, const KindCount& count, int pad = RingPad, int hole = ButtonHole, bool square = true) {
	if (!count.total()) return "";
	return Image(name, x + pad, y + pad, ChartSprite(ChartParts(count), size - 2 * pad, hole, square));
}

// Invisible buttons over a ring chart's parts, for their tooltips: along each
// row of small squares, one button per run of squares whose centre is in the
// same part. A part with nothing in it gets none (no "Bases" for uniques).
std::string SliceHits(const std::string& name, int x, int y, int size, int hole, const KindCount& count) {
	const std::pair<const char*, int> parts[] { { "Perfect", count.perfect }, { "Stored", count.stored }, { "Bases", count.bases }, { "Missing", count.missing } };
	const int                         total = count.total();
	if (!total) return "";
	std::vector<double> ends; // where each part ends, 0..1, as Ring draws them
	double              sum = 0;
	for (const auto& part : parts) ends.push_back((sum += part.second) / total);
	const double pi = 3.14159265358979, centre = size / 2.0, outer = size / 2.0 - 1, inner = outer * hole / 100.0;
	const int    step = std::max(12, size / 32);
	std::string  out;
	int          n = 0;
	for (int top = 0; top < size; top += step) {
		int        start = 0, run = -1;
		const auto flush = [&](int end) {
			if (run >= 0)
				out += HitButton(name + "Hit" + std::to_string(n++), x + start, y + top, end - start, std::min(step, size - top),
				                 Tr(parts[run].first) + ": " + std::to_string(parts[run].second) + " (" + Percent(parts[run].second, total) + ")", "");
			run = -1;
		};
		for (int left = 0; left < size; left += step) {
			const double dx = left + step / 2.0 - centre, dy = top + step / 2.0 - centre, r = std::sqrt(dx * dx + dy * dy);
			int          part = -1;
			if (r <= outer && r >= inner) {
				double turn = std::atan2(dx, -dy) / (2 * pi); // 0 at the top, clockwise
				if (turn < 0) turn += 1;
				part = 0;
				while (part + 1 < 4 && turn >= ends[part]) ++part;
			}
			if (part == run) continue;
			flush(left);
			if (part >= 0) start = left, run = part;
		}
		flush(size);
	}
	return out;
}

// A colour for a text: the colour object of a chart part.
std::string ColorText(Rgb color) {
	return "{\"r\": " + std::to_string(color.r) + ", \"g\": " + std::to_string(color.g) + ", \"b\": " + std::to_string(color.b) + ", \"a\": 255}";
}

// How far a goal is: the tiles found (perfect ones only for a perfect goal).
struct Progress {
	int  done {}, total {};
	bool perfect {}; // every goal counted is a perfect one
};
Progress GoalProgress(Kind kind, const KindCount& count) {
	const bool perfect = static_cast<int>(kind) < 4 && Config.goal[static_cast<int>(kind)] == GoalPerfect;
	return { perfect ? count.perfect : count.inGrail(), count.total(), perfect };
}
Progress OverviewProgress() {
	Progress all { 0, 0, true };
	for (const Kind kind : AllKinds) {
		if (!InGoals(kind)) continue;
		const Progress part = GoalProgress(kind, CountKind(kind));
		all.done += part.done, all.total += part.total, all.perfect = all.perfect && part.perfect;
	}
	return all;
}

// A ring chart with its goal's share in the middle.
std::string RingChart(const std::string& name, int x, int y, int size, const KindCount& count, const Progress& goal) {
	std::string out = Image(name + "Ring", x, y, ChartSprite(ChartParts(count), size, 58));
	// The share done, in the colour of what counts (stored, or perfect).
	out += TextLabel(name + "Pct", x, y + size / 2 - 52, size, 60, Percent(goal.done, goal.total), ColorText(goal.perfect ? PerfectColor : StoredColor).c_str(), Config.font + 16, "center");
	out += TextLabel(name + "Num", x, y + size / 2 + 8, size, 44, std::to_string(goal.done) + " / " + std::to_string(goal.total), "$FontColorGray", Config.font, "center");
	return out + SliceHits(name, x, y, size, 58, count);
}

// A detail tile: a large square button, its ring as background around its
// icon, its name and numbers on hover; a click opens the category.
constexpr int StatTile = 170, StatTileIcon = 70, StatTileHole = 62;
std::string StatTileAt(const std::string& name, int x, int y, const std::string& icon, const std::string& label, const KindCount& count, const std::string& command,
                       const std::string& extra = "") {
	char button[1024] {};
	std::snprintf(button, sizeof(button),
	              R"(, {"type": "ButtonWidget", "name": %s, "fields": {"rect": {"x": %d, "y": %d, "width": %d, "height": %d}, "filename": "d2rloader\\grail-on\\stattile", "normalFrame": 0, "hoveredFrame": 1, "pressedFrame": 2, "tooltipString": %s, "sound": "cursor_menu_select_hd", "onClickMessage": %s}})",
	              Json(name).c_str(), x, y, StatTile, StatTile, Json(CountText(label, count)).c_str(), Command(command).c_str());
	std::string out = button;
	out += ButtonRing(name + "Ring", x, y, StatTile, count, 9, StatTileHole);
	if (!icon.empty()) out += Icon(name + "Icon", x + (StatTile - StatTileIcon) / 2, y + (StatTile - StatTileIcon) / 2, StatTileIcon, icon, 1, 1, extra);
	return out;
}

std::string StatsLayout() {
	std::string layout;
	const int   left = GridLeft + 40, width = PageSize * Cell - 80, bottom = ViewTop + ViewRows * Cell - 30;
	if (StatsPage <= 0) {
		// The overview: the whole grail in one ring, its parts named on hover.
		const KindCount all = CountMenu(StatsMenu);
		layout += TextLabel("StatsTitle", left, ViewTop + 40, width, 110, "Grail On!", "$FontColorGoldYellow", 80, "center");
		const int ring = 760;
		const int x = left + (width - ring) / 2, y = ViewTop + 150 + (bottom - ViewTop - 150 - ring) / 2 - 150;
		layout += RingChart("All", x, y, ring, all, OverviewProgress());
		// Under its tile, when the Standard of Heroes is in it.
		std::vector<Tile> tiles = Shown;
		tiles.insert(tiles.end(), Peeking.begin(), Peeking.end()); // a picture under the host
		for (const auto& tile : tiles)
			if (Uniques[tile.unique].secret && Stored.count({ tile.unique, tile.eth }))
				layout += TextLabel("Purpose", left, GridTop + (tile.y + tile.height) * Cell + 24, width, 60, "The standard of heroes has finally found a purpose", "$FontColorGoldYellow", Config.font + 4,
				                    "center");
		return layout;
	}
	// A kind's page: its ring on top, a tile per category below.
	const auto      kind  = static_cast<Kind>(std::clamp(StatsPage - 1, 0, KindCountAll - 1));
	const int       k     = static_cast<int>(kind);
	const KindCount count = CountKind(kind);
	const int       ring  = 420;
	layout += TextLabel("StatsTitle", left, ViewTop + 10, width, 60, KindNames[k], "$FontColorGoldYellow", 44, "center");
	const int ringX = left + (width - ring) / 2;
	layout += RingChart("K", ringX, ViewTop + 80, ring, count, GoalProgress(kind, count));
	const bool eth  = kind == Kind::Ethereal;
	const int  top  = ViewTop + ring + 130, gapX = 36, gapY = 36;
	const int  columns = std::max(1, (width + gapX) / (StatTile + gapX));
	const int  startX  = left + (width - (columns * StatTile + (columns - 1) * gapX)) / 2;
	// In the menus' order: weapons before armor.
	std::vector<int> order(CategoryCount());
	std::iota(order.begin(), order.end(), 0);
	std::stable_sort(order.begin(), order.end(), [](int a, int b) { return Categories[a].menu < Categories[b].menu; });
	int n = 0;
	for (const int c : order) {
		const auto tiles = CountTiles([&](int u, bool tileEth) { return Uniques[u].category == c && InKind(u, tileEth, kind); });
		if (!tiles.total()) continue;
		const int x = startX + (n % columns) * (StatTile + gapX), y = top + (n / columns) * (StatTile + gapY);
		if (y + StatTile > bottom) break;
		layout += StatTileAt("Tile" + std::to_string(c), x, y, Categories[c].icon, Categories[c].name, tiles, "open" + std::to_string(c) + (eth ? "e" : ""), eth ? EthLook : "");
		++n;
	}
	return layout;
}

// ---- search ----
// The search bar under the window: what is typed finds the grail's entries
// whose shown name has it (any case), the first one shown at once; Enter or
// the arrows go to the next and previous; the count says where.
std::vector<std::pair<int, bool>> SearchMatches; // (entry, eth) in the lists' order; Lock held
std::set<std::pair<int, bool>>    SearchHits;    // the same, to dim the others
bool                              SearchOn {};   // something typed: the tiles not matching are dimmed
constexpr int                     SearchGap = 8; // between the bar and its result line
// Lock held. A tile the search leaves out.
bool Dimmed(int u, bool eth) { return SearchOn && !SearchHits.count({ u, eth }); }
int                               SearchAt = 0;

std::wstring Lower(const std::string& text) {
	const int    size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
	std::wstring wide(static_cast<std::size_t>(std::max(0, size)), L'\0');
	if (size > 0) MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), wide.data(), size), CharLowerBuffW(wide.data(), static_cast<DWORD>(wide.size()));
	return wide;
}

// Lock held. The matches of `text` (none for an empty one).
std::string SearchedText; // what the matches are of
void FindMatches(const std::string& text) {
	SearchedText = text;
	SearchMatches.clear();
	SearchHits.clear();
	SearchOn = false;
	SearchAt = 0;
	std::wstring query = Lower(text);
	while (!query.empty() && query.back() == L' ') query.pop_back();
	while (!query.empty() && query.front() == L' ') query.erase(query.begin());
	if (query.empty()) return;
	SearchOn = true;
	for (int u = 0; u < UniqueCount(); ++u) {
		const auto& entry = Uniques[u];
		if (entry.secret) continue;
		// Only what the grail shows: an unfound item's base name while names are hidden.
		const std::string& name = NameShown(u) ? entry.name : entry.base;
		if (Lower(StripColors(name)).find(query) == std::wstring::npos) continue;
		for (const bool eth : { false, true })
			if (HasTile(u, eth)) SearchMatches.push_back({ u, eth }), SearchHits.insert({ u, eth });
	}
}

// Lock held. Shows match `SearchAt` next.
void JumpToMatch() {
	if (SearchMatches.empty()) return;
	SearchAt                  = (SearchAt % static_cast<int>(SearchMatches.size()) + static_cast<int>(SearchMatches.size())) % static_cast<int>(SearchMatches.size());
	std::tie(JumpTo, JumpEth) = SearchMatches[SearchAt];
}

std::string SearchBox(int width); // its sprite, made once per length

// Lock held. The bar: its box, the text (a caret while typing), the count,
// previous and next, and a clear button while something is typed.
std::string SearchBar() {
	// Fixed on the screen: the panel starts further left in compact mode.
	const int         x = Config.searchX - (WindowX() - MenuRoom), y = Config.searchY - PanelY, width = Config.searchWidth;
	const std::string typed  = TypedText(TypeSearch);
	const bool        typing = ObjTyping && TypeTarget == TypeSearch;
	const bool        empty  = typed.empty() && !typing;
	std::string       out    = Image("SearchBack", x, y, SearchBox(width));
	out += R"(, {"type": "TextBoxWidget", "name": "SearchText", "fields": {)" + Rect(x + 24, y, width - 48, DropHeight) + R"(, "text": )" + Json(empty ? Tr("Search") : typed + (typing ? "|" : ""))
	     + R"(, "style": {"fontColor": ")" + (empty ? "$FontColorGray" : "$FontColorWhite") + R"(", "pointSize": 30, "dropShadow": "$DefaultDropShadow", "spacing": "$ReducedSpacing", "alignment": {"h": "left", "v": "center"}}}})";
	out += HitButton("SearchHit", x, y, width, DropHeight, Wrap("Type a name: the grail shows the first item that has it. Enter or the arrows: the next one."), "searchtype");
	if (typed.empty()) return out;
	// The result line under the bar, as wide and high as it: the count, then
	// the arrows and the clear button at its right end.
	const int         line  = y + DropHeight + SearchGap;
	const std::string count = SearchMatches.empty() ? Tr("No match") : std::to_string(SearchAt + 1) + " / " + std::to_string(SearchMatches.size());
	out += Image("SearchLineBack", x, line, SearchBox(width));
	int at = x + width - 12 - DropHeight;
	out += TextButton("SearchClear", at, line, DropHeight, DropHeight, "removebutton", 0, 3, 1, "", "", Wrap("Clear the search"), "searchclear");
	if (SearchMatches.size() > 1) {
		at -= DropHeight + 10;
		out += TextButton("SearchNext", at, line, DropHeight, DropHeight, "downarrow", 0, 1, 2, "", "", Wrap("Next"), "searchnext");
		at -= DropHeight + 10;
		out += TextButton("SearchPrev", at, line, DropHeight, DropHeight, "uparrow", 0, 1, 2, "", "", Wrap("Previous"), "searchprev");
	}
	return out + TextLabel("SearchCount", x + 24, line, at - x - 34, DropHeight, count, SearchMatches.empty() ? "$FontColorGray" : "$FontColorWhite", 30, "left");
}

// Lock held. The layout of the view in panel `id`: the current category's
// tiles, the settings or the stats. Its texts are the strings registered with the lists.
std::string ViewLayout(const std::string& id) {
	const bool settings = CurrentCategory < 0; // no tiles: the settings or the stats
	char       open[512] {}, head[2048] {};
	// The window's top is where Expanded Stash Plus's $LeftPanelRect_ExpandedStash
	// puts it (x 236, y -856, at the top of the screen; it reaches 88 units lower with the title, still above the skill bar);
	// the panel starts MenuRoom further left, for the menus.
	std::snprintf(open, sizeof(open),
	              R"({"type": "Panel", "name": "grail-on/%s", "fields": {"priority": 10, "rect": {"x": %d, "y": %d, "width": %d, "height": %d}, "anchor": {"x": 0.0, "y": 0.397}}, "children": [)"
	              R"({"type": "ClickCatcherWidget", "name": "PanelClickCatcher", "fields": {"rect": {"x": 0, "y": 0, "width": %d, "height": %d}}})",
	              id.c_str(), WindowX() - MenuRoom, PanelY, PanelSpan(), std::max(PanelHeight, Config.searchY - PanelY + 2 * DropHeight + SearchGap + 8), PanelSpan(), PanelHeight);
	std::snprintf(head, sizeof(head),
	              R"(, {"type": "ImageWidget", "name": "Background", "fields": {"rect": {"x": 0, "y": 0}, "filename": "d2rloader\\grail-on\\panel"}})"
	              R"(, {"type": "ImageWidget", "name": "Cover", "fields": {"rect": {"x": 57, "y": %d}, "filename": "d2rloader\\grail-on\\cover"}})"
	              R"(, {"type": "ButtonWidget", "name": "CloseButton", "fields": {"rect": {"x": 1600, "y": 9}, "filename": "PANEL\\closebtn_4x", "hoveredFrame": 3, "tooltipString": "@d2r:strClose", "sound": "cursor_close_window_hd", "onClickMessage": %s}})",
	              ViewTop - 2, Command("close").c_str());
	// The page's grid (3x5, its items only passing through) first, under the
	// window's background, so nothing of it shows: every tile is a picture.
	// The settings keep it off screen. The host's grid is in its own panel,
	// under this one.
	char grid[512] {};
	std::snprintf(grid, sizeof(grid),
	              R"(, {"type": "InventoryGridWidget", "name": "grail-on/Grid", "fields": {"rect": {"x": %d, "y": %d}, "cellCount": {"x": %d, "y": %d}, "cellSize": "$ItemCellSize", "gemSocketFilename": "PANEL\\gemsocket", "chronicleFlourishOffset": {"x": 7100, "y": 7100}}})",
	              TilesView() ? GridLeft : -20000, GridTop, StageCols, StageRows);
	std::string layout = (Host::On() ? std::string() : std::string(grid)) + head;
	const int menu = CurrentMenu();
	// The menus down the left, the stats first; Misc while it tracks something.
	for (int m = 0, slot = 0; m < static_cast<int>(std::size(Menus)); ++m) {
		if (!MenuShown(m)) continue;
		const int  x = MenuX(), y = MenuTop + slot++ * MenuStep;
		layout += SquareButton("Menu" + std::to_string(m), x, y, m == menu, "menu", "@grail-on:M" + std::to_string(m), "menu" + std::to_string(m), MenuSize);
		layout += ButtonRing("MenuRing" + std::to_string(m), x, y, MenuSize, CountMenu(m));
		// The ethereal menus' icons are see-through, like ethereal items.
		int width = 1, height = 1;
		if (const auto art = MenuArt(m, width, height); !art.empty())
			layout += Icon("MenuIcon" + std::to_string(m), x + (MenuSize - MenuIcon) / 2, y + (MenuSize - MenuIcon) / 2, MenuIcon, art, width, height, IsEthMenu(m) ? EthLook : "");
	}
	// The view's tiles go in one container ("Rest"), hidden while a scroll's
	// frames show. ShiftLayout moves its children too: it starts MenuRoom left.
	const std::string layoutBefore = layout;
	layout.clear();
	// The set names above their sets: "Unknown set" until one of its items is
	// found, with spoilers off.
	for (std::size_t h = 0; h < ShownHeaders.size(); ++h) {
		const auto [set, row] = ShownHeaders[h];
		const int  top        = ViewTop + row * Cell;
		const auto name       = HeaderKey(set);
		layout += Label("SetName" + std::to_string(h), HeaderLeft, top, HeaderWidth, Cell - 14, name, HeaderColor(set), Config.font + 8, "center", "center");
		layout += Image("SetLine" + std::to_string(h), HeaderLeft, top + Cell - 14, "d2rloader\\grail-on\\divider");
	}
	// Slots, item art and labels go first, so the grid draws its items over them.
	const std::string gray = GrayFields();
	std::string       veils; // the search's veils over the tiles left out
	for (std::size_t i = 0; i < Shown.size(); ++i) {
		const auto& tile    = Shown[i];
		const auto& entry   = Uniques[tile.unique];
		const bool  present = Present.count(tile.unique) != 0;
		const auto  n       = std::to_string(i);
		const int   left = GridLeft + tile.x * Cell, top = GridTop + tile.y * Cell;
		const int   itemLeft = left + (tile.width == 1 ? Cell / 2 : 0); // a 1-wide item is centred, like the grid draws it
		layout += Image("Slot" + n, itemLeft, top, "d2rloader\\grail-on\\slot" + std::to_string(tile.width) + "x" + std::to_string(tile.height));
		if (entry.secret) { // its glow, its item, nothing more
			if (Config.perfectFrames && Stored.count({ tile.unique, tile.eth })) layout += GlowImage(tile, "Glow" + n, left, top);
			continue;
		}
		// A perfect copy: its glow over the slot, under the item; its frames
		// shown in turn (Animate), or the first alone without animations.
		if (Perfect.count({ tile.unique, tile.eth }) && Config.perfectFrames)
			layout += GlowImage(tile, "Glow" + n, left, top);
		// Unfound entries show their own name and art only with spoilers on.
		const bool named = NameShown(tile.unique);
		if (entry.runeword) {
			if (!Stored.count({ tile.unique, tile.eth })) layout += RunewordArt(tile, "Art" + n, left, top, false, gray);
		} else if (const auto& art = ArtShown(tile.unique) ? entry.art : entry.ghost; !art.empty()) layout += Image("Art" + n, itemLeft, top, art, gray);
		// The label spans the slot (two cells, also for a 1-wide item) and a
		// margin on each side.
		const bool dim    = Dimmed(tile.unique, tile.eth);
		if (dim) veils += DimVeil(tile, "Dim" + n, itemLeft, top); // over the items too: after the grid
		const auto key    = std::string(named ? "N" : "B") + (tile.eth ? "E" : "") + std::to_string(tile.unique);
		const int  margin = LabelMargin;
		layout += Label("Label" + n, left - margin, top - LabelRows * Cell + Cell / 2, SlotWidth * Cell + margin + LabelMargin, LabelRows * Cell - Cell / 2, key,
		                present && !dim ? "$FontColorGoldYellow" : "$FontColorGray", Config.font, "center", "center");
	}
	// The row whose items do not fit: their whole slots and art, inside the
	// game's ClipViewWidget, so they are cut where the view ends (half a cell
	// under the grid). A click on one shows it.
	if (!Peeking.empty()) {
		std::string clipped;
		for (std::size_t i = 0; i < Peeking.size(); ++i) clipped += FakeTile(Peeking[i], GridTop - ViewTop + Peeking[i].y * Cell, "P" + std::to_string(i), false, true);
		layout += ClipView("PeekClip", clipped, true);
	}
	// The labels of a row whose items do not fit.
	for (std::size_t i = 0; i < Peeking.size(); ++i) {
		const auto& tile  = Peeking[i];
		if (Uniques[tile.unique].secret) continue;
		const bool  named = NameShown(tile.unique);
		const auto  key   = std::string(named ? "N" : "B") + (tile.eth ? "E" : "") + std::to_string(tile.unique);
		const int   left = GridLeft + tile.x * Cell, top = GridTop + tile.y * Cell;
		layout += Label("PeekLabel" + std::to_string(i), left - LabelMargin, top - LabelRows * Cell + Cell / 2, SlotWidth * Cell + 2 * LabelMargin, LabelRows * Cell - Cell / 2, key,
		                Present.count(tile.unique) && !Dimmed(tile.unique, tile.eth) ? "$FontColorGoldYellow" : "$FontColorGray", Config.font, "center", "center");
	}
	layout += veils;
	{
		char rest[160] {};
		std::snprintf(rest, sizeof(rest), R"(, {"type": "Widget", "name": "Rest", "fields": {"rect": {"x": %d, "y": 0}}, "children": [)", -MenuRoom);
		layout = layoutBefore + (layout.empty() ? std::string() : rest + layout.substr(1) + "]}");
	}
	if (TilesView()) {
		// Every tile is a picture: an item dropped anywhere on the view goes to
		// its tile (over the grid too, so the grid never takes a drop itself).
		char lower[512] {};
		std::snprintf(lower, sizeof(lower),
		              R"(, {"type": "ButtonWidget", "name": "LowerHit", "fields": {"rect": {"x": %d, "y": %d, "width": %d, "height": %d}, "filename": "d2rloader\\grail-on\\blank", "normalFrame": 0, "hoveredFrame": 0, "pressedFrame": 0, "onClickMessage": %s}})",
		              GridLeft, ViewTop, PageSize * Cell, ViewRows * Cell, Command("lower").c_str());
		layout += lower;
	}
	// The menu's categories down the left (over the grid's empty first column,
	// after the grid so their clicks reach them), the category's first base on them.
	for (int c = 0; c < CategoryCount() && !settings; ++c) {
		if (!InMenu(c, menu)) continue;
		const int y = ButtonTop(c);
		layout += SquareButton("Cat" + std::to_string(c), CatX(), y, c == CurrentCategory, "category", "@grail-on:C" + std::to_string(c),
		                       "cat" + std::to_string(c), CatSize);
		layout += ButtonRing("CatRing" + std::to_string(c), CatX(), y, CatSize, CountCategory(c, ViewEth));
		const auto& category = Categories[c];
		if (!category.icon.empty())
			layout += Icon("CatIcon" + std::to_string(c), CatX() + (CatSize - CatIcon) / 2, y + (CatSize - CatIcon) / 2, CatIcon, category.icon, category.width, category.height, ViewEth ? EthLook : "",
			               category.artSize);
	}
	// The clicks on the cut slots, after the grid so they reach them.
	for (std::size_t i = 0; i < Peeking.size(); ++i) {
		const auto& tile   = Peeking[i];
		const int   halves = PeekHalves(tile);
		if (halves <= 0) continue;
		const int width = std::clamp(tile.width, 1, 2);
		const int left  = GridLeft + tile.x * Cell + (width == 1 ? Cell / 2 : 0), top = GridTop + tile.y * Cell;
		char      button[512] {};
		std::snprintf(button, sizeof(button),
		              R"(, {"type": "ButtonWidget", "name": "PeekHit%zu", "fields": {"rect": {"x": %d, "y": %d, "width": %d, "height": %d}, "filename": "d2rloader\\grail-on\\blank", "normalFrame": 0, "hoveredFrame": 1, "pressedFrame": 1, "onClickMessage": %s}})",
		              i, left, top, width * Cell, halves * Cell / 2, Command("peek" + std::to_string(i)).c_str());
		layout += button;
	}
	if (CurrentCategory == StatsView) {
		// The stats pages down the left, like a menu's categories.
		for (int p = 0, shown = 0; p <= KindCountAll; ++p) {
			if (!StatsPageShown(p)) continue; // misc: while something is tracked; objectives: while there are some
			const int y = ViewTop + shown++ * (Cell + 20) + (Cell - CatSize) / 2;
			layout += SquareButton("StatPage" + std::to_string(p), CatX(), y, p == StatsPage, "category", "@grail-on:SP" + std::to_string(p), "stats" + std::to_string(p), CatSize);
			layout += ButtonRing("StatPageRing" + std::to_string(p), CatX(), y, CatSize, p ? CountKind(static_cast<Kind>(p - 1)) : CountMenu(StatsMenu));
			int        width = 1, height = 1;
			const auto art = MenuArt(p == 0 ? StatsMenu : p == 1 || p == 2 ? WeaponMenu : p == 3 ? SetMenu : p == 4 ? RunewordMenu : p == 5 ? MiscMenu : ObjMenu, width, height);
			layout += Icon("StatPageIcon" + std::to_string(p), CatX() + (CatSize - CatIcon) / 2, y + (CatSize - CatIcon) / 2, CatIcon, art, width, height, p == 2 ? EthLook : "");
		}
		layout += StatsLayout();
	}
	// The settings, after the grid so their clicks reach them: under each
	// header a line per setting, its check box and name; what it does shows
	// on hover (both the box and the name take the click).
	if (CurrentCategory == SettingsView) {
		const int left = GridLeft + (PageSize * Cell - TableWidth) / 2, line = 64, dropX = left + TableWidth - DropWidth;
		const int x = left;
		int       y = ViewTop + 16;
		// A section: a line above it (not the first), its name.
		const auto header = [&](const char* text) {
			if (y > ViewTop + 16) {
				y += 8;
				layout += Image(std::string("Line") + text, HeaderLeft, y, "d2rloader\\grail-on\\section");
				y += LineHeight + 10;
			}
			layout += TextLabel(std::string("Head") + text, HeaderLeft, y, HeaderWidth, 56, text, "$FontColorGoldYellow", Config.font + 6, "center");
			y += 64;
		};
		const auto setting = [&](const std::string& id, int sx, const std::string& name, bool on, const std::string& help, const std::string& command, int width = TableWidth / 2) {
			layout += SquareButton("Set" + id, sx, y, on, "tick", help.empty() ? std::string() : Wrap(help), command);
			layout += TextLabel("SetName" + id, sx + ButtonSize + 20, y, width - ButtonSize - 40, ButtonSize, name, "$FontColorWhite", 30);
			layout += HitButton("SetHit" + id, sx + ButtonSize + 20, y, width - ButtonSize - 40, ButtonSize, help.empty() ? std::string() : Wrap(help), command);
		};
		// A drop-down: its name on the left, the box from the middle on; its
		// list (drawn last, over the rest) while open. `nameX`..`boxX`: the
		// name's room; a narrower box for the misc types.
		struct List {
			int                      x {}, y {}, width = DropWidth;
			const char*              tile = "droptile";
			std::vector<std::string> names, commands;
			int                      current {};
		} list;
		const auto dropdownAt = [&](int id, int nameX, int boxX, int boxWidth, const std::string& name, const std::vector<std::string>& names, int current, const std::string& help,
		                            const std::string& prefix, bool raw) {
			const bool  narrow = boxWidth != DropWidth;
			const auto  tip    = help.empty() ? std::string() : Wrap(help);
			layout += TextLabel("DropName" + std::to_string(id), nameX, y, boxX - nameX - 20, DropHeight, raw ? name : Tr(name), "$FontColorWhite", 30);
			layout += HitButton("DropHit" + std::to_string(id), nameX, y, boxX - nameX - 20, DropHeight, tip, "");
			layout += TextButton("Drop" + std::to_string(id), boxX, y, boxWidth, DropHeight, narrow ? "dropboxs" : "dropbox", 0, 3, 1, names[current], "$FontColorLightGold", tip,
			                     "drop" + std::to_string(id));
			if (OpenDrop == id) {
				list.x = boxX, list.y = y + DropHeight, list.current = current, list.width = boxWidth, list.tile = narrow ? "droptiles" : "droptile";
				for (int k = 0; k < static_cast<int>(names.size()); ++k) list.names.push_back(names[k]), list.commands.push_back(prefix + std::to_string(k));
			}
		};
		const auto dropdown = [&](int id, const char* name, std::vector<std::string> names, int current, const std::string& help, const std::string& prefix) {
			dropdownAt(id, x, dropX, DropWidth, name, names, current, help, prefix, false);
			y += line;
		};
		const auto choices = [](const char* const* names, int count) { return std::vector<std::string>(names, names + count); };
		// The pages: a square icon each in the category column, like a menu's categories.
		{
			constexpr const char* icons[] { "setgen", "setdisp", "setloot", "setmisc", "objectives" };
			for (int p = 0; p < SettingsPageCount; ++p) {
				const int top = ViewTop + p * (Cell + 20) + (Cell - CatSize) / 2;
				layout += SquareButton("SetPage" + std::to_string(p), CatX(), top, p == SettingsPage, "category", "@grail-on:SG" + std::to_string(p), "spage" + std::to_string(p), CatSize);
				layout += Icon("SetPageIcon" + std::to_string(p), CatX() + (CatSize - CatIcon) / 2, top + (CatSize - CatIcon) / 2, CatIcon, std::string("d2rloader\\grail-on\\icon-") + icons[p], 1, 1);
			}
		}
		if (SettingsPage == 0) {
		header("Spoilers");
		{
			// Shown before they are found: a column per kind, a row for names and one for art.
			const char* columns[] { "Uniques", "Set items", "Sets", "Runewords" };
			const char* names[] { "unique_names", "set_item_names", "set_names", "runeword_names" };
			const char* art[] { "unique_art", "set_item_art", nullptr, "runeword_runes" };
			for (int c = 0; c < 4; ++c)
				layout += TextLabel(std::string("SpoilCol") + columns[c], left + TableLabel + c * TableColumn, y, TableColumn, 44, columns[c], "$FontColorGoldYellow", 30, "center");
			y += 50;
			for (int r = 0; r < 2; ++r) {
				const std::string row = r ? "Art" : "Names";
				layout += Image("SpoilRow" + row, left, y, "d2rloader\\grail-on\\tablerow");
				for (int c = 0; c < 4; ++c) layout += Image("SpoilLine" + row + std::to_string(c), left + TableLabel + c * TableColumn - 2, y + 7, "d2rloader\\grail-on\\tablecol");
				layout += TextLabel("SpoilName" + row, left + 28, y, TableLabel - 40, TableRow, r ? "Show art" : "Show names", "$FontColorWhite", 30);
				for (int c = 0; c < 4; ++c) {
					const char* key = r ? art[c] : names[c];
					if (!key) continue;
					for (const auto& toggle : Toggles)
						if (!std::strcmp(toggle.key, key))
							layout += SquareButton("Set" + std::string(key), left + TableLabel + c * TableColumn + (TableColumn - ButtonSize) / 2, y + (TableRow - ButtonSize) / 2, Config.*toggle.field, "tick",
							                       Wrap(toggle.help), std::string("toggle:") + key);
				}
				y += TableRow + 6;
			}
		}
		header("Grail");
		for (int k = 0; k < 4; ++k)
			dropdown(k, GoalRowNames[k], choices(GoalNames, 3), Config.goal[k],
			         std::string("Your goal for ") + (k == 0 ? "uniques" : k == 1 ? "ethereal uniques" : k == 2 ? "set items" : "runewords")
			             + ". None: left out of the overview. Find: one copy counts. Perfect: only a perfect copy counts.",
			         "goal" + std::to_string(k));
		}
		if (SettingsPage == 1) {
		header("Display");
		{
			// The language: the game's, or one of the game's languages by its own name.
			std::vector<std::string> names { Tr("Game language") };
			for (const char* name : LanguageNames) names.emplace_back(name);
			dropdown(8, "Language", names, Config.language + 1, "The language of Grail On!'s texts and item names. Game language: the same as the game.", "lang");
		}
		dropdown(7, "Compare with grail", choices(CompareNames, 3), Config.compare,
		         "While the grail is open, a hovered grail item shows its grail copy beside it. None: the game's own comparison. Shift: while Shift is held. Always: without Shift.", "compare");
		dropdown(9, "Perfect frame", { "None", "Static", "Animated" }, !Config.perfectFrames ? 0 : Config.frameAnimations ? 2 : 1,
		         "Perfect copies glow around their tile. Static: the glow stays still. Animated: it moves.", "pick9:");
		dropdown(10, "Menu direction", { "Left to right", "Right to left" }, Config.swapColumns ? 1 : 0,
		         "The order of the two icon columns. Left to right: the menus, then their categories. Right to left: the categories, then the menus.", "pick10:");
		dropdown(11, "Layout", { "Standard", "Compact" }, Config.compact ? 1 : 0,
		         "Standard: the menus are left of the window, where the game's panels start. Compact: the window sits against the screen's left edge, with its menus on its right.", "pick11:");
		dropdown(12, "Scrolling", { "Instant", "Animated" }, Config.scrollAnimations ? 1 : 0, "The mouse wheel jumps a row at a time (Instant) or scrolls smoothly (Animated).", "pick12:");
		{
			// The inventory button's place: X and Y, each with a step down and
			// up (the game's drop-down plates), and the lobby's refresh button to reset.
			const std::string help = Wrap("Moves the grail button on the inventory's left edge, in the inventory's units. Click: 1, Shift+click: 10, Ctrl+click: 100.");
			const std::string resetHelp = Wrap("Puts the grail button back in its first place.");
			const int         resetWidth = RefreshArt.file.empty() ? ResetBox : DropHeight;
			const int         controls   = dropX + DropWidth - 2 * AxisWidth - resetWidth;
			layout += TextLabel("ButtonName", x, y, controls - x - 20, DropHeight, "Grail button", "$FontColorWhite", 30);
			layout += HitButton("ButtonHit", x, y, controls - x - 20, DropHeight, help, "");
			int at = controls;
			for (const auto& [axis, value] : { std::pair { "X", Config.buttonX }, { "Y", Config.buttonY } }) {
				layout += TextLabel(std::string("ButtonAxis") + axis, at, y, 30, DropHeight, axis, "$FontColorGoldYellow", 30, "center");
				at += 30;
				layout += TextButton(std::string("ButtonDown") + axis, at + (SmallBox - DropHeight) / 2, y, DropHeight, DropHeight, "downarrow", 0, 1, 2, "", "", help, std::string("btn") + axis + "-");
				at += SmallBox;
				layout += TextLabel(std::string("ButtonValue") + axis, at, y, 100, DropHeight, std::to_string(value), "$FontColorWhite", 30, "center");
				at += 100;
				layout += TextButton(std::string("ButtonUp") + axis, at + (SmallBox - DropHeight) / 2, y, DropHeight, DropHeight, "uparrow", 0, 1, 2, "", "", help, std::string("btn") + axis + "+");
				at += SmallBox + 20;
			}
			if (RefreshArt.file.empty()) layout += TextButton("ButtonReset", at, y, ResetBox, DropHeight, "resetbox", 0, 3, 1, "Reset", "$FontColorLightGold", resetHelp, "btnreset");
			else layout += TextButton("ButtonReset", at, y, DropHeight, DropHeight, "refresh", RefreshArt.normal, RefreshArt.hovered, RefreshArt.pressed, "", "", resetHelp, "btnreset");
			y += line;
			// The search bar: its place (X, Y), then its length, and a reset.
			const std::string searchHelp = Wrap("Moves the search bar, from the window's top left. Click: 1, Shift+click: 10, Ctrl+click: 100.");
			const std::string lengthHelp = Wrap("The search bar's length. Click: 1, Shift+click: 10, Ctrl+click: 100.");
			const std::string searchReset = Wrap("Puts the search bar back under the window, at its first length.");
			const auto stepper = [&](const std::string& id, int sx, const char* axis, int value, const std::string& tip, const std::string& command) {
				if (axis[0]) layout += TextLabel("SAxis" + id, sx, y, 30, DropHeight, axis, "$FontColorGoldYellow", 30, "center");
				sx += 30;
				// The loot filter's arrows: down lowers, up raises.
				layout += TextButton("SDown" + id, sx + (SmallBox - DropHeight) / 2, y, DropHeight, DropHeight, "downarrow", 0, 1, 2, "", "", tip, command + "-");
				layout += TextLabel("SValue" + id, sx + SmallBox, y, 100, DropHeight, std::to_string(value), "$FontColorWhite", 30, "center");
				layout += TextButton("SUp" + id, sx + SmallBox + 100 + (SmallBox - DropHeight) / 2, y, DropHeight, DropHeight, "uparrow", 0, 1, 2, "", "", tip, command + "+");
			};
			layout += TextLabel("SearchName", x, y, controls - x - 20, DropHeight, "Search bar", "$FontColorWhite", 30);
			layout += HitButton("SearchNameHit", x, y, controls - x - 20, DropHeight, searchHelp, "");
			stepper("X", controls, "X", Config.searchX, searchHelp, "srchX");
			stepper("Y", controls + AxisWidth, "Y", Config.searchY, searchHelp, "srchY");
			if (RefreshArt.file.empty()) layout += TextButton("SearchReset", controls + 2 * AxisWidth, y, ResetBox, DropHeight, "resetbox", 0, 3, 1, "Reset", "$FontColorLightGold", searchReset, "srchreset");
			else layout += TextButton("SearchReset", controls + 2 * AxisWidth, y, DropHeight, DropHeight, "refresh", RefreshArt.normal, RefreshArt.hovered, RefreshArt.pressed, "", "", searchReset, "srchreset");
			y += line;
			layout += TextLabel("SearchLength", x, y, controls - x - 20, DropHeight, "Search bar length", "$FontColorWhite", 30);
			layout += HitButton("SearchLengthHit", x, y, controls - x - 20, DropHeight, lengthHelp, "");
			stepper("W", controls + AxisWidth, "", Config.searchWidth, lengthHelp, "srchW");
			y += line;
		}
		}
		if (SettingsPage == 2) {
		header("Loot");
		for (int k = 0; k < 3; ++k)
			dropdown(4 + k, GoalRowNames[k], choices(LootNames, 3), Config.loot[k],
			         std::string("Ground ") + (k == 0 ? "uniques" : k == 1 ? "ethereal uniques" : "set items")
			             + ", after the game's filter and other filters. Hide found: hidden when the grail has one. Hide perfect: hidden when its grail copy is perfect.",
			         "loot" + std::to_string(k));
		for (const auto& toggle : Toggles)
			if (toggle.group == LootGroup) {
				setting(toggle.key, left, toggle.name, Config.*toggle.field, toggle.help, std::string("toggle:") + toggle.key);
				y += line;
			}
		}
		if (SettingsPage == 3) {
			// Misc tracking: a drop-down per item type with misc items, one
			// under the other like the other settings; the Misc menu shows the tracked ones.
			header("Misc tracking");
			layout += TextBlock("MiscHelp", x, y, TableWidth, 80,
			                    "No tracking: no tiles. Checkbox: a click on a tile ticks it, nothing is stored. Items: the items are stored in their tiles. Hover a type's name to see its items.",
			                    "$FontColorGray", 26);
			y += 124; // a margin between the help text and the list
			const int rowStep = line;
			const int rows    = std::max(1, (ViewTop + ViewRows * Cell - 80 - y) / rowStep);
			const int perPage = rows;
			const int count   = static_cast<int>(MiscCategories.size());
			MiscFirst         = std::clamp(MiscFirst, 0, std::max(0, (count - 1) / perPage * perPage));
			const int top     = y;
			const std::vector<std::string> modes { "No tracking", "Checkbox", "Items" };
			for (int i = MiscFirst; i < count && i < MiscFirst + perPage; ++i) {
				const auto& misc = MiscCategories[i];
				y                = top + (i - MiscFirst) * rowStep;
				const auto& name = misc.category >= 0 && misc.category < CategoryCount() ? Categories[misc.category].name : misc.type;
				// The name's tooltip: some of the type's items.
				dropdownAt(100 + i, x, dropX, DropWidth, name + " (" + std::to_string(misc.count) + ")", modes, MiscModeOf(misc.type), "", "miscmode" + std::to_string(i) + ":", true);
				layout += HitButton("MiscTip" + std::to_string(i), x, y, dropX - x - 20, DropHeight, "@grail-on:CT" + std::to_string(misc.category), "");
			}
			y = top + rows * rowStep;
			if (count > perPage) {
				// More types than fit: pages of them.
				const std::string pageText = std::to_string(MiscFirst / perPage + 1) + " / " + std::to_string((count + perPage - 1) / perPage);
				layout += TextButton("MiscPrev", left, y, SmallBox, DropHeight, "stepbox", 0, 3, 1, "<", "$FontColorLightGold", "", "miscpage-");
				layout += TextLabel("MiscPage", left + SmallBox, y, 160, DropHeight, pageText, "$FontColorWhite", 30, "center");
				layout += TextButton("MiscNext", left + SmallBox + 160, y, SmallBox, DropHeight, "stepbox", 0, 3, 1, ">", "$FontColorLightGold", "", "miscpage+");
				MiscPerPage = perPage;
			}
			if (count == 0) layout += TextLabel("MiscNone", x, top, TableWidth, 56, "This game has no misc items.", "$FontColorGray", 30);
		}
		if (SettingsPage == 4) {
			// Custom objectives: a text box and Add, then one line per
			// objective (its icon, a click changes it; its text; remove).
			header("Custom objectives");
			for (const auto& toggle : Toggles)
				if (toggle.group == ObjectiveGroup) {
					setting(toggle.key, x, toggle.name, Config.*toggle.field, toggle.help, std::string("toggle:") + toggle.key, TableWidth);
					y += line + 8;
				}
			layout += TextBlock("ObjHelp", x, y, TableWidth, 80,
			                    "Click the box, type an objective and press Enter or click Add. Tick it in the Objectives menu. Click an objective's icon to change it.", "$FontColorGray", 26);
			y += 100;
			// The box: the text typed (a hidden Windows text box takes the keys
			// while it is active, so the game's keys don't fire), a caret while typing.
			layout += Image("ObjInputBack", x, y, "d2rloader\\grail-on\\inputbox");
			{
				const std::string typed = ObjTypedText();
				const bool        empty = typed.empty() && !ObjTyping;
				layout += R"(, {"type": "TextBoxWidget", "name": "ObjTyped", "fields": {)" + Rect(x + 24, y, ObjInputWidth - 48, DropHeight) + R"(, "text": )"
				        + Json(empty ? Tr("A new objective") : typed + (ObjTyping ? "|" : "")) + R"(, "style": {"fontColor": ")" + (empty ? "$FontColorGray" : "$FontColorWhite")
				        + R"(", "pointSize": 30, "dropShadow": "$DefaultDropShadow", "spacing": "$ReducedSpacing", "alignment": {"h": "left", "v": "center"}}}})";
				layout += HitButton("ObjTypeHit", x, y, ObjInputWidth, DropHeight, "", "objtype");
			}
			layout += TextButton("ObjAdd", x + TableWidth - AddWidth, y, AddWidth, DropHeight, "addbutton", 0, 1, 2, "", "", Tr("Add"), "objadd");
			y += line + 16;
			// A line per objective: on/off, its icon (a click opens the picker),
			// its text, up, down, remove.
			const int rows  = std::max(1, (ViewTop + ViewRows * Cell - 80 - y) / line);
			const int count = static_cast<int>(Objectives.size());
			ObjFirst        = std::clamp(ObjFirst, 0, std::max(0, (count - 1) / rows * rows));
			ObjPerPage      = rows;
			const int  top      = y;
			const auto iconHelp = Wrap("Click to choose its icon."), removeHelp = Wrap("Removes this objective."), onHelp = Wrap("On: it has a tile in the Objectives menu and counts in the stats.");
			const int  arrows   = x + TableWidth - 3 * (DropHeight + 12);
			for (int i = ObjFirst; i < count && i < ObjFirst + rows; ++i) {
				const auto& objective = Objectives[i];
				const auto  n         = std::to_string(i);
				y                     = top + (i - ObjFirst) * line;
				layout += SquareButton("ObjOn" + n, x, y, objective.enabled, "tick", onHelp, "objon" + n);
				const int iconX = x + ButtonSize + 16;
				layout += TextButton("ObjIcon" + n, iconX, y, DropHeight, DropHeight, ("objrow" + std::to_string(std::clamp(objective.icon, 0, ObjectiveIconCount - 1))).c_str(), 0, 1, 1, "", "", iconHelp, "objpick" + n);
				const int textX = iconX + DropHeight + 20;
				layout += R"(, {"type": "TextBoxWidget", "name": )" + Json("ObjText" + n) + R"(, "fields": {)" + Rect(textX, y, arrows - textX - 12, DropHeight)
				        + R"(, "text": )" + Json(objective.text) + R"(, "style": {"fontColor": ")" + (objective.enabled ? "$FontColorWhite" : "$FontColorGray")
				        + R"(", "pointSize": 30, "dropShadow": "$DefaultDropShadow", "spacing": "$ReducedSpacing", "alignment": {"h": "left", "v": "center"}}}})";
				if (i > 0) layout += TextButton("ObjUp" + n, arrows, y, DropHeight, DropHeight, "uparrow", 0, 1, 2, "", "", Wrap("Move up"), "objup" + n);
				if (i + 1 < count) layout += TextButton("ObjDown" + n, arrows + DropHeight + 12, y, DropHeight, DropHeight, "downarrow", 0, 1, 2, "", "", Wrap("Move down"), "objdown" + n);
				layout += TextButton("ObjDel" + n, arrows + 2 * (DropHeight + 12), y, DropHeight, DropHeight, "removebutton", 0, 3, 1, "", "", removeHelp, "objdel" + n);
			}
			y = top + rows * line;
			if (count > rows) {
				const std::string pageText = std::to_string(ObjFirst / rows + 1) + " / " + std::to_string((count + rows - 1) / rows);
				layout += TextButton("ObjPrev", left, y, SmallBox, DropHeight, "stepbox", 0, 3, 1, "<", "$FontColorLightGold", "", "objpage-");
				layout += TextLabel("ObjPage", left + SmallBox, y, 160, DropHeight, pageText, "$FontColorWhite", 30, "center");
				layout += TextButton("ObjNext", left + SmallBox + 160, y, SmallBox, DropHeight, "stepbox", 0, 3, 1, ">", "$FontColorLightGold", "", "objpage+");
			}
			if (count == 0) layout += TextLabel("ObjNone", x, top, TableWidth, 56, "No objective yet.", "$FontColorGray", 30);
			// The icon picker, over everything: every icon, the current one
			// rimmed; a click on one picks it, anywhere else closes it.
			if (ObjPicking >= 0 && ObjPicking < count) {
				const int     boxW = PickWidth, boxH = PickHeight;
				const int     boxX = GridLeft + (PageSize * Cell - boxW) / 2, boxY = ViewTop + (ViewRows * Cell - boxH) / 2;
				layout += HitButton("PickCatch", GridLeft, ViewTop, PageSize * Cell, ViewRows * Cell, "", "objpickx");
				layout += Image("PickBack", boxX, boxY, "d2rloader\\grail-on\\pickback");
				layout += TextLabel("PickTitle", boxX, boxY + 20, boxW, 60, "Choose an icon", "$FontColorGoldYellow", Config.font + 6, "center");
				// The icons alone: gold, brighter on hover; the current one bright.
				for (int k = 0; k < ObjectiveIconCount; ++k) {
					const int  cx = boxX + 40 + (k % PickColumns) * (PickCell + PickGap), cy = boxY + 100 + (k / PickColumns) * (PickCell + PickGap);
					const bool on = k == Objectives[ObjPicking].icon;
					layout += TextButton("Pick" + std::to_string(k), cx, cy, PickCell, PickCell, ("objpick" + std::to_string(k)).c_str(), on ? 1 : 0, 1, 1, "", "", "", "objset" + std::to_string(k));
				}
			}
		}
		// The open list, over everything: a click anywhere else closes it.
		if (!list.names.empty()) {
			layout += HitButton("DropCatch", GridLeft, ViewTop, PageSize * Cell, ViewRows * Cell, "", "dropx");
			for (std::size_t k = 0; k < list.names.size(); ++k)
				layout += TextButton("DropItem" + std::to_string(k), list.x, list.y + static_cast<int>(k) * TileHeight, list.width, TileHeight, list.tile, 0, 1, 1, list.names[k],
				                     static_cast<int>(k) == list.current ? "$FontColorGoldYellow" : "$FontColorWhite", "", list.commands[k]);
		}
		// What is switched off on this game (the grail itself works).
		if (const auto notes = ProblemList(); !notes.empty() && SettingsPage == 0) {
			y += line / 2;
			layout += TextLabel("HeadOff", HeaderLeft, y, HeaderWidth, Cell - 14, "Switched off", "$FontColorRed", Config.font + 8, "center");
			y += Cell;
			for (std::size_t k = 0; k < notes.size(); ++k) {
				const int lines = 1 + static_cast<int>(notes[k].text.size()) / 75;
				layout += TextBlock("Off" + std::to_string(k), x, y, 14 * Cell, lines * 36 + 8, notes[k].text, "$FontColorGray", 26);
				y += lines * 36 + 20;
			}
		}
	}
	// Something switched off: a line under the view points to the settings page.
	if (ProblemCount > 0 && CurrentCategory != SettingsView)
		layout += TextLabel("OffNote", GridLeft, ViewTop + ViewRows * Cell + 6, PageSize * Cell, 40, "Some features are switched off on this game: see Config.", "$FontColorRed", 24, "center");
	layout += SearchBar();
	// The scroll bar, after the grid so its clicks reach it: a recessed track,
	// the thumb (as tall as the share of rows on screen, in whole steps), and
	// one invisible button per step.
	if (MaxScroll > 0) {
		const int steps  = ThumbSteps();
		const int thumbY = ThumbTop(steps, Scroll * ScrollFrames);
		layout += Image("Track", BarLeft, BarTop, "d2rloader\\grail-on\\track");
		layout += Image("Thumb", BarLeft, thumbY, "d2rloader\\grail-on\\thumb" + std::to_string(steps));
		for (int i = 0; i < ScrollSteps; ++i) {
			char step[512] {};
			std::snprintf(step, sizeof(step),
			              R"(, {"type": "ButtonWidget", "name": "Step%d", "fields": {"rect": {"x": %d, "y": %d, "width": %d, "height": %d}, "filename": "d2rloader\\grail-on\\step", "normalFrame": 0, "hoveredFrame": 0, "pressedFrame": 0, "onClickMessage": %s}})",
			              i, BarLeft, BarTop + i * StepHeight, BarWidth, StepHeight, Command("step" + std::to_string(i)).c_str());
			layout += step;
		}
	}
	return open + ShiftLayout(layout) + "]}";
}

// Lock held. What a view shows: two views with the same key are the same.
std::string ViewKey() {
	if (CurrentCategory != SettingsView) OpenDrop = -1; // a list closes with its page
	std::string key = "q" + TypedText(TypeSearch) + (ObjTyping && TypeTarget == TypeSearch ? "|" : "") + std::to_string(SearchAt) + "/" + std::to_string(SearchMatches.size()) + ":" + std::to_string(Config.searchX) + ","
	                + std::to_string(Config.searchY) + "," + std::to_string(Config.searchWidth) + ":" + (CurrentCategory == SettingsView && SettingsPage == 4 ? "t" + ObjTypedText() + (ObjTyping ? "|" : "") + ":" : std::string()) + std::to_string(Config.version) + ":d" + std::to_string(OpenDrop) + ":" + std::to_string(ProblemCount.load()) + ":" + std::to_string(CurrentCategory) + (ViewEth ? "e:" : ":") + std::to_string(Scroll) + ":";
	for (const auto& tile : Shown) key += Present.count(tile.unique) ? Perfect.count({ tile.unique, tile.eth }) ? '2' : '1' : '0';
	key += ':';
	// The cut slots and pictures, which hold no real item: drawn stored or not.
	for (const auto& tile : Peeking) key += std::to_string(tile.unique) + (Stored.count({ tile.unique, tile.eth }) ? Perfect.count({ tile.unique, tile.eth }) ? 'S' : 's' : '0');
	key += ':';
	for (const auto& [set, row] : ShownHeaders) key += SetKnown(set) ? '1' : '0';
	key += ':';
	for (const auto& row : PlacedRows)
		for (const auto& tile : row.tiles) key += BaseTiles.count({ tile.unique, tile.eth }) ? 'b' : Stored.count({ tile.unique, tile.eth }) ? 's' : Present.count(tile.unique) ? 'p' : '0';
	// The rings show the counts: every menu's, and the stats pages' or the menu's categories'.
	const auto add = [&](const KindCount& count) { key += ":" + std::to_string(count.perfect) + "/" + std::to_string(count.stored) + "/" + std::to_string(count.bases) + "/" + std::to_string(count.missing); };
	for (int m = 0; m < static_cast<int>(std::size(Menus)); ++m) add(CountMenu(m));
	if (CurrentCategory == StatsView) {
		key += ":p" + std::to_string(StatsPage);
		for (int k = 0; k < KindCountAll; ++k) add(CountKind(static_cast<Kind>(k)));
		if (StatsPage > 0)
			for (int c = 0; c < CategoryCount(); ++c) add(CountCategory(c, false)), add(CountCategory(c, true));
	} else if (CurrentCategory >= 0)
		for (int c = 0; c < CategoryCount(); ++c)
			if (InMenu(c, CurrentMenu())) add(CountCategory(c, ViewEth));
	return key;
}

// Every text the views use: category names, and per unique its base and name,
// each plain and ethereal. The plugin's texts in each language's column (all
// in the chosen one when there is one); item names come in the texts' language.
std::string StringsJson() {
	enum Kind { Plain, Raw, EthCategory, Sockets };
	struct Text {
		std::string key, english;
		Kind        kind = Plain;
	};
	std::vector<Text> strings {
		{ "Btn", "G", Raw },
		{ "Settings", "Settings" },
		{ "Soon", "Not made yet" },
		{ "GrailTip", "Grail" },
	};
	strings.push_back({ "ScrollTest", "Scroll test", Raw });
	for (int r = 0; r < 48; ++r) strings.push_back({ "Row" + std::to_string(r), "Row " + std::to_string(r + 1), Raw });
	for (int m = 0; m < static_cast<int>(std::size(Menus)); ++m) strings.push_back({ "M" + std::to_string(m), Menus[m] });
	for (int c = 0; c < CategoryCount(); ++c) {
		if (Categories[c].menu == MiscMenu) {
			// A misc category: its name already in the texts' language; its
			// button's tooltip names some of its items.
			strings.push_back({ "C" + std::to_string(c), Categories[c].name, Raw });
			std::string tip = Categories[c].name;
			int         shown = 0, all = 0;
			for (int u = 0; u < UniqueCount(); ++u)
				if (Uniques[u].category == c && Uniques[u].misc && ++all <= 8) tip += "\n" + Uniques[u].name, ++shown;
			if (all > shown) tip += "\n" + TrFormat("and %s more", std::to_string(all - shown));
			strings.push_back({ "CT" + std::to_string(c), tip, Raw });
			continue;
		}
		strings.push_back({ "C" + std::to_string(c), Categories[c].name }), strings.push_back({ "CE" + std::to_string(c), Categories[c].name, EthCategory });
	}
	strings.push_back({ "UnknownSet", "Unknown set" });
	for (int n = 1; n <= 6; ++n) strings.push_back({ "RS" + std::to_string(n), std::to_string(n), Sockets });
	for (const auto& [key, text] : { std::pair { "SP0", "Overview" }, { "SP1", "Uniques" }, { "SP2", "Ethereal" }, { "SP3", "Set Items" }, { "SP4", "Runewords" }, { "SP5", "Misc" }, { "SP6", "Objectives" }, { "SG0", "General" }, { "SG1", "Display" }, { "SG2", "Loot" }, { "SG3", "Misc tracking" }, { "SG4", "Custom objectives" } }) strings.push_back({ key, text });
	for (const auto& [set, name] : SetNames) strings.push_back({ "S" + std::to_string(set), name, Raw });
	for (int u = 0; u < UniqueCount(); ++u) {
		const auto n = std::to_string(u);
		if (Uniques[u].runeword) strings.push_back({ "B" + n, "Unknown runeword" }); // shown until found, spoilers off
		else strings.push_back({ "B" + n, Uniques[u].base, Raw });
		strings.push_back({ "N" + n, Uniques[u].name, Raw });
		if (!Uniques[u].eth) continue;
		strings.push_back({ "BE" + n, Uniques[u].base, Raw }); // in the ethereal menus: no mark needed
		strings.push_back({ "NE" + n, Uniques[u].name, Raw });
	}
	const int   chosen = Config.language >= 0 && Config.language < LanguageCount ? Config.language : -1;
	std::string json   = "[";
	for (const auto& text : strings) {
		json += std::string(json.size() > 1 ? ",\n" : "\n") + R"({"Key": )" + Json(text.key);
		for (int l = 0; l < LanguageCount; ++l) {
			const int   language = chosen >= 0 ? chosen : l;
			std::string value    = text.kind == Raw           ? text.english
			                     : text.kind == EthCategory ? TrFormat("Ethereal %s", Tr(text.english, language), language)
			                     : text.kind == Sockets     ? TrFormat(text.english == "1" ? "%d Socket" : "%d Sockets", text.english, language)
			                                                : Tr(text.english, language);
			json += std::string(", \"") + Languages[l] + "\": " + Json(value.empty() ? " " : value);
		}
		json += "}";
	}
	return json + "\n]";
}

// Sprites drawn at the size they are used: a dark plate with a bronze bevel;
// frames 0 normal, 1 hovered, 2 pressed. Active ones are brighter.
std::string BuildSprite(int width, int height, bool active) {
	constexpr int frames = 3;
	std::string   out(40 + static_cast<std::size_t>(width) * frames * height * 4, '\0');
	const auto    put = [&](std::size_t at, std::uint32_t value, int bytes) {
        for (int i = 0; i < bytes; ++i) out[at + i] = static_cast<char>((value >> (8 * i)) & 0xFF);
	};
	std::memcpy(out.data(), "SpA1", 4);
	put(4, 31, 2);
	put(6, static_cast<std::uint32_t>(width), 2);
	put(8, static_cast<std::uint32_t>(width * frames), 4);
	put(12, static_cast<std::uint32_t>(height), 4);
	put(20, frames, 4);
	const int border = std::max(2, height / 20);
	for (int frame = 0; frame < frames; ++frame)
		for (int y = 0; y < height; ++y)
			for (int x = 0; x < width; ++x) {
				const int edge = std::min({ x, y, width - 1 - x, height - 1 - y });
				int       rgba[4] {};
				if (edge < 1) rgba[0] = 20, rgba[1] = 16, rgba[2] = 12, rgba[3] = 255;
				else if (edge < 1 + border) {
					const int  base[3] { active ? 222 : 128, active ? 186 : 110, active ? 100 : 76 };
					const bool lit   = std::min(x, y) == edge;
					const int  shade = (frame == 2 ? -1 : 1) * (lit ? 24 : -24) + (frame == 1 ? 40 : 0);
					for (int c = 0; c < 3; ++c) rgba[c] = std::clamp(base[c] + shade, 0, 255);
					rgba[3] = 255;
				} else {
					const int fill = (active ? 46 : 22) + (frame == 1 ? 18 : 0) - (frame == 2 ? 10 : 0);
					rgba[0] = fill + 6, rgba[1] = fill + 3, rgba[2] = fill, rgba[3] = 235;
				}
				const std::size_t at = 40 + (static_cast<std::size_t>(y) * width * frames + static_cast<std::size_t>(frame) * width + x) * 4;
				for (int c = 0; c < 4; ++c) out[at + c] = static_cast<char>(rgba[c]);
			}
	return out;
}

// ---- native-looking art (same algorithms as tools/art.py) ----

struct Picture {
	int                        width {}, height {};
	std::vector<unsigned char> rgba;
	unsigned char*             at(int x, int y) { return rgba.data() + (static_cast<std::size_t>(y) * width + x) * 4; }
};

double Noise(std::uint32_t x, std::uint32_t y, std::uint32_t seed) {
	std::uint32_t h = x * 374761393u + y * 668265263u + seed * 2246822519u;
	h               = (h ^ (h >> 13)) * 1274126177u;
	return ((h ^ (h >> 16)) & 0xFF) / 255.0;
}

// The fine grain of the game's dark stone: single pixels and 3x3 blotches.
Picture Stone(int width, int height, const int color[3], int grain, std::uint32_t seed) {
	Picture out { width, height, std::vector<unsigned char>(static_cast<std::size_t>(width) * height * 4) };
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x) {
			const double n = 0.6 * Noise(x, y, seed) + 0.4 * Noise(x / 3, y / 3, seed + 1) - 0.5;
			auto*        p = out.at(x, y);
			for (int c = 0; c < 3; ++c) p[c] = static_cast<unsigned char>(std::clamp(static_cast<int>(color[c] + n * grain), 0, 255));
			p[3] = 255;
		}
	return out;
}

// An empty slot of any size: the stash slot's corners, its frame edges
// stretched, around grained stone of the slot's inside colour.
Picture Slot(int width, int height) {
	const int  s = SlotSize, c = SlotCorner;
	const auto src = [&](int x, int y) { return SlotPixels + (static_cast<std::size_t>(y) * s + x) * 4; };
	long       sum[3] {}, count = 0;
	for (int y = c; y < s - c; ++y)
		for (int x = c; x < s - c; ++x, ++count)
			for (int k = 0; k < 3; ++k) sum[k] += src(x, y)[k];
	const int inside[3] { static_cast<int>(sum[0] / count), static_cast<int>(sum[1] / count), static_cast<int>(sum[2] / count) };
	Picture   out = Stone(width, height, inside, 10, 7);
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x) {
			const bool left = x < c, right = x >= width - c, top = y < c, bottom = y >= height - c;
			if (!left && !right && !top && !bottom) continue;
			const int sx = left ? x : right ? s - (width - x) : s / 2;
			const int sy = top ? y : bottom ? s - (height - y) : s / 2;
			std::memcpy(out.at(x, y), src(sx, sy), 4);
		}
	return out;
}

// The top of a slot cut by the view's end: its left, right and top edges,
// nothing inside and no bottom edge.
Picture PeekBox(int width, int height) {
	const int s = SlotSize, c = SlotCorner;
	Picture   out { width, height, std::vector<unsigned char>(static_cast<std::size_t>(width) * height * 4) };
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x) {
			const bool left = x < c, right = x >= width - c, top = y < c;
			if (!left && !right && !top) continue;
			const int sx = left ? x : right ? s - (width - x) : s / 2;
			const int sy = top ? y : s / 2;
			std::memcpy(out.at(x, y), SlotPixels + (static_cast<std::size_t>(sy) * s + sx) * 4, 4);
		}
	return out;
}

// A square or tab button: an empty slot, lighter when hovered, darker when
// pressed. The open one has the grid's gold rim.
std::vector<Picture> CategoryButton(bool open, int width = ButtonSize, int height = ButtonSize) {
	std::vector<Picture> frames;
	for (const int shift : { 0, 22, -8 }) {
		Picture picture = Slot(width, height);
		for (int y = 0; y < height; ++y)
			for (int x = 0; x < width; ++x) {
				auto*     p    = picture.at(x, y);
				const int edge = std::min({ x, y, width - 1 - x, height - 1 - y });
				if (open && edge < 3) {
					static constexpr unsigned char gold[3][3] { { 80, 66, 40 }, { 195, 180, 111 }, { 253, 252, 213 } };
					std::memcpy(p, gold[edge], 3);
					continue;
				}
				for (int k = 0; k < 3; ++k) p[k] = static_cast<unsigned char>(std::clamp(p[k] + shift, 0, 255));
			}
		frames.push_back(std::move(picture));
	}
	return frames;
}

// A check box: a small empty slot, with a gold check mark when on.
std::vector<Picture> CheckBox(bool on) {
	auto frames = CategoryButton(false);
	if (!on) return frames;
	for (auto& picture : frames)
		for (int y = 0; y < ButtonSize; ++y)
			for (int x = 0; x < ButtonSize; ++x) {
				// Two strokes: down from (14,28) to (24,40), up to (43,15).
				const auto onStroke = [&](double ax, double ay, double bx, double by) {
					const double vx = bx - ax, vy = by - ay, t = std::clamp(((x - ax) * vx + (y - ay) * vy) / (vx * vx + vy * vy), 0.0, 1.0);
					const double ex = x - (ax + t * vx), ey = y - (ay + t * vy);
					return ex * ex + ey * ey < 12.0;
				};
				if (onStroke(14, 28, 24, 40) || onStroke(24, 40, 43, 15)) std::memcpy(picture.at(x, y), "\xFD\xD8\x7A", 3);
			}
	return frames;
}

// The line under a set's name: gold, fading out at both ends.
Picture Divider(int width, int height) {
	Picture out { width, height, std::vector<unsigned char>(static_cast<std::size_t>(width) * height * 4) };
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x) {
			const double fade = std::clamp(std::min(x, width - 1 - x) / (width * 0.2), 0.0, 1.0);
			const bool   lit  = y < height / 2;
			auto*        px   = out.at(x, y);
			px[0] = lit ? 195 : 80, px[1] = lit ? 180 : 66, px[2] = lit ? 111 : 40;
			px[3] = static_cast<unsigned char>(230 * fade);
		}
	return out;
}

// The scroll bar: the track is a long empty slot; the thumb a bronze bar
// `steps` steps tall; a step button is invisible, faintly lit when hovered.
Picture ScrollThumb(int steps) {
	const int height = steps * StepHeight;
	Picture   out { BarWidth, height, std::vector<unsigned char>(static_cast<std::size_t>(BarWidth) * height * 4) };
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < BarWidth; ++x) {
			const int edge = std::min({ x, y, BarWidth - 1 - x, height - 1 - y });
			const int lit  = edge < 3 ? (std::min(x, y) == edge ? 50 : -50) : static_cast<int>(18 * Noise(x, y, 11)) - 9;
			const int rgba[4] { 128 + lit, 102 + lit, 62 + lit, edge < 1 ? 255 : 235 };
			for (int k = 0; k < 4; ++k) out.at(x, y)[k] = static_cast<unsigned char>(std::clamp(rgba[k], 0, 255));
		}
	return out;
}

std::vector<Picture> ScrollStep() {
	Picture idle { BarWidth, StepHeight, std::vector<unsigned char>(static_cast<std::size_t>(BarWidth) * StepHeight * 4) };
	Picture hover = idle;
	for (int y = 0; y < StepHeight; ++y)
		for (int x = 0; x < BarWidth; ++x) std::memcpy(hover.at(x, y), "\xFF\xF0\xC8\x28", 4);
	return { idle, hover };
}

Picture Half(const Picture& in) {
	Picture out { std::max(1, in.width / 2), std::max(1, in.height / 2), {} };
	out.rgba.resize(static_cast<std::size_t>(out.width) * out.height * 4);
	for (int y = 0; y < out.height; ++y)
		for (int x = 0; x < out.width; ++x)
			for (int k = 0; k < 4; ++k) {
				int total = 0;
				for (int dy = 0; dy < 2; ++dy)
					for (int dx = 0; dx < 2; ++dx) total += in.rgba[(static_cast<std::size_t>(std::min(in.height - 1, 2 * y + dy)) * in.width + std::min(in.width - 1, 2 * x + dx)) * 4 + k];
				out.rgba[(static_cast<std::size_t>(y) * out.width + x) * 4 + k] = static_cast<unsigned char>(total / 4);
			}
	return out;
}

// Frames side by side, all the same size.
std::string SpriteFile(const std::vector<Picture>& frames) {
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
		for (const auto& frame : frames) out.append(reinterpret_cast<const char*>(frame.rgba.data()) + y * row, row);
	return out;
}

bool RegisterResource(const std::string& path, const std::string& bytes, D2RL::Resources::RegistrationHandle* handle = nullptr) {
	static std::vector<std::string> kept; // the loader copies the bytes, but keep them anyway
	kept.push_back(bytes);
	const D2RL::Resources::ResourceRegistration resource {
		.structSize = D2RL::Resources::ResourceRegistrationSize,
		.path       = path.c_str(),
		.bytes      = kept.back().data(),
		.byteCount  = kept.back().size(),
	};
	D2RL::Resources::RegistrationHandle ignored = D2RL::Resources::InvalidHandle;
	if (!ResourceApi) return false;
	return ResourceApi->registerResource(Context, &resource, handle ? handle : &ignored) == D2RL::Resources::Result::Success;
}

// The grail button's sprite, again when the game's button art comes in.
D2RL::Resources::RegistrationHandle GrailButtonHandles[2] { D2RL::Resources::InvalidHandle, D2RL::Resources::InvalidHandle };
std::vector<Picture>                 GrailButton(const std::vector<Picture>& backs);
bool RegisterGrailButton(const std::vector<Picture>& backs = {}) {
	for (auto& handle : GrailButtonHandles)
		if (handle != D2RL::Resources::InvalidHandle) (void)ResourceApi->unregisterResource(Context, handle), handle = D2RL::Resources::InvalidHandle;
	const auto           frames = GrailButton(backs);
	std::vector<Picture> half;
	for (const auto& frame : frames) half.push_back(Half(frame));
	const std::string path = "data/hd/global/ui/d2rloader/grail-on/grailbutton";
	return RegisterResource(path + ".sprite", SpriteFile(frames), &GrailButtonHandles[0]) && RegisterResource(path + ".lowend.sprite", SpriteFile(half), &GrailButtonHandles[1]);
}

bool RegisterSprite(const std::string& name, const std::vector<Picture>& frames) {
	std::vector<Picture> half;
	for (const auto& frame : frames) half.push_back(Half(frame));
	const std::string path = "data/hd/global/ui/d2rloader/grail-on/" + name;
	return RegisterResource(path + ".sprite", SpriteFile(frames)) && RegisterResource(path + ".lowend.sprite", SpriteFile(half));
}

// ---- stats charts (UI thread) ----

// A ring chart: the parts clockwise from the top, a thin dark gap between
// them, lighter towards the outer edge; smooth edges. A square one is a pie
// seen only in a square band (a button's padding around its icon).
Picture Ring(const std::vector<std::pair<int, Rgb>>& parts, int size, int hole, bool square) {
	Picture      picture { size, size, std::vector<unsigned char>(static_cast<std::size_t>(size) * size * 4) };
	const double outer = size / 2.0 - 1, inner = outer * hole / 100.0, pi = 3.14159265358979, centre = size / 2.0;
	double       total = 0;
	int          used  = 0;
	for (const auto& [value, color] : parts) total += value, used += value > 0;
	std::vector<double> ends; // where each part ends, 0..1
	double              sum = 0;
	for (const auto& part : parts) ends.push_back((sum += part.first) / std::max(1.0, total));
	for (int y = 0; y < size; ++y)
		for (int x = 0; x < size; ++x) {
			const double dx = x + 0.5 - centre, dy = y + 0.5 - centre, d = square ? std::max(std::abs(dx), std::abs(dy)) : std::sqrt(dx * dx + dy * dy);
			const double r = std::sqrt(dx * dx + dy * dy);
			double       cover = std::clamp(outer - d + 0.5, 0.0, 1.0) * std::clamp(d - inner + 0.5, 0.0, 1.0);
			if (cover <= 0) continue;
			double turn = std::atan2(dx, -dy) / (2 * pi); // 0 at the top, clockwise
			if (turn < 0) turn += 1;
			std::size_t which = 0;
			while (which + 1 < ends.size() && turn >= ends[which]) ++which;
			Rgb color = total > 0 ? parts[which].second : MissingColor;
			// The gap: within a pixel of a part's start, when parts meet.
			if (used > 1)
				for (std::size_t e = 0; e < ends.size(); ++e) {
					if (parts[e].first <= 0) continue;
					const double edge = (e ? ends[e - 1] : 0.0) * 2 * pi, off = std::abs(std::sin(std::atan2(dx, -dy) - edge)) * r;
					const double along = std::cos(std::atan2(dx, -dy) - edge);
					if (along > 0 && off < 1.6) cover *= std::clamp(off - 0.6, 0.0, 1.0);
				}
			const double light = 0.78 + 0.32 * (d - inner) / (outer - inner);
			auto*        px    = picture.at(x, y);
			px[0]              = static_cast<unsigned char>(std::clamp(color.r * light, 0.0, 255.0));
			px[1]              = static_cast<unsigned char>(std::clamp(color.g * light, 0.0, 255.0));
			px[2]              = static_cast<unsigned char>(std::clamp(color.b * light, 0.0, 255.0));
			px[3]              = static_cast<unsigned char>(255 * cover);
		}
	return picture;
}

// The inventory button: the mold, and the button in it (frames: 0 normal,
// 1 pressed, 2 disabled, 3 hovered), the grail icon in the game's gold,
// brighter on hover.
constexpr int ButtonX = 3, ButtonY = 954, GrailButtonWidth = 52, GrailButtonHeight = 60, GrailIconSize = 42;
Picture Mold() {
	Picture mold { MoldWidth, MoldHeight, std::vector<unsigned char>(MoldRgba, MoldRgba + sizeof(MoldRgba)) };
	return mold;
}
// `in` shrunk (area average) to fit width x height, centred, never enlarged.
Picture Fit(const Picture& in, int width, int height) {
	Picture      out { width, height, std::vector<unsigned char>(static_cast<std::size_t>(width) * height * 4) };
	if (in.width <= 0 || in.height <= 0 || width <= 0 || height <= 0) return out;
	const double scale = std::min({ 1.0, static_cast<double>(width) / in.width, static_cast<double>(height) / in.height });
	const int    w = std::max(1, static_cast<int>(in.width * scale + 0.5)), h = std::max(1, static_cast<int>(in.height * scale + 0.5));
	const int    left = (width - w) / 2, top = (height - h) / 2;
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			const int x0 = x * in.width / w, x1 = std::max(x0 + 1, (x + 1) * in.width / w), y0 = y * in.height / h, y1 = std::max(y0 + 1, (y + 1) * in.height / h);
			double    sum[4] {}, n = 0;
			for (int sy = y0; sy < y1; ++sy)
				for (int sx = x0; sx < x1; ++sx) {
					const auto* px = in.rgba.data() + (static_cast<std::size_t>(sy) * in.width + sx) * 4;
					for (int c = 0; c < 3; ++c) sum[c] += px[c] * (px[3] / 255.0);
					sum[3] += px[3], ++n;
				}
			auto* px = out.at(left + x, top + y);
			for (int c = 0; c < 3; ++c) px[c] = static_cast<unsigned char>(sum[3] > 0 ? std::clamp(sum[c] * 255.0 / sum[3], 0.0, 255.0) : 0);
			px[3] = static_cast<unsigned char>(sum[3] / n);
		}
	return out;
}

// The game's Advanced Stats button (character sheet), read from the game's
// files on the game thread: the grail button is drawn on it when it is there.
std::vector<Picture> AdvancedButtonFrames() {
	const std::string& file = AdvancedButtonFile;
	if (file.size() < 0x28 || file.compare(0, 4, "SpA1")) return {};
	const auto u32 = [&](std::size_t at) { std::uint32_t v; std::memcpy(&v, file.data() + at, 4); return v; };
	std::uint16_t version = 0, frameWidth = 0;
	std::memcpy(&version, file.data() + 4, 2), std::memcpy(&frameWidth, file.data() + 6, 2);
	const std::uint32_t width = u32(8), height = u32(12), count = u32(20);
	if (version != 31 || !frameWidth || !height || !count || count > 16 || width != frameWidth * count || height > 1024 || file.size() < 0x28 + static_cast<std::size_t>(width) * height * 4) {
		Say(false, "grailon: the Advanced Stats button picture is not one this plugin reads (version %u, %ux%u, %u frames, %zu bytes)", version, frameWidth, height, count, file.size());
		return {};
	}
	std::vector<Picture> frames;
	for (std::uint32_t f = 0; f < count; ++f) {
		Picture frame { frameWidth, static_cast<int>(height), std::vector<unsigned char>(static_cast<std::size_t>(frameWidth) * height * 4) };
		for (std::uint32_t y = 0; y < height; ++y) std::memcpy(frame.at(0, static_cast<int>(y)), file.data() + 0x28 + (static_cast<std::size_t>(y) * width + f * frameWidth) * 4, static_cast<std::size_t>(frameWidth) * 4);
		frames.push_back(std::move(frame));
	}
	Say(false, "grailon: Advanced Stats button picture: %u frames of %ux%u", count, frameWidth, height);
	return frames;
}

// A game sprite's frames (SpA1 version 31: RGBA frames side by side), or none.
std::vector<Picture> SpriteFrames(const std::string& file, const char* what) {
	if (file.size() < 0x28 || file.compare(0, 4, "SpA1")) return {};
	const auto u32 = [&](std::size_t at) { std::uint32_t v; std::memcpy(&v, file.data() + at, 4); return v; };
	std::uint16_t version = 0, frameWidth = 0;
	std::memcpy(&version, file.data() + 4, 2), std::memcpy(&frameWidth, file.data() + 6, 2);
	const std::uint32_t width = u32(8), height = u32(12), count = u32(20);
	if (version != 31 || !frameWidth || !height || !count || count > 32 || width < frameWidth * count || height > 2048 || width > 8192
	    || file.size() < 0x28 + static_cast<std::size_t>(width) * height * 4) {
		Say(false, "grailon: %s is not a picture this plugin reads (version %u, %ux%u, %u frames)", what, version, frameWidth, height, count);
		return {};
	}
	std::vector<Picture> frames;
	for (std::uint32_t f = 0; f < count; ++f) {
		Picture frame { frameWidth, static_cast<int>(height), std::vector<unsigned char>(static_cast<std::size_t>(frameWidth) * height * 4) };
		for (std::uint32_t y = 0; y < height; ++y) std::memcpy(frame.at(0, static_cast<int>(y)), file.data() + 0x28 + (static_cast<std::size_t>(y) * width + f * (width / count)) * 4, static_cast<std::size_t>(frameWidth) * 4);
		frames.push_back(std::move(frame));
	}
	Say(false, "grailon: %s: %u frames of %ux%u", what, count, frameWidth, height);
	return frames;
}

// `in` resized to width x height (each pixel the average of its source box,
// alpha weighted; the nearest pixel when enlarging).
Picture Resize(const Picture& in, int width, int height) {
	Picture out { width, height, std::vector<unsigned char>(static_cast<std::size_t>(width) * height * 4) };
	if (in.width <= 0 || in.height <= 0) return out;
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x) {
			const int x0 = x * in.width / width, x1 = std::max(x0 + 1, (x + 1) * in.width / width), y0 = y * in.height / height, y1 = std::max(y0 + 1, (y + 1) * in.height / height);
			double    sum[4] {}, n = 0;
			for (int sy = y0; sy < y1; ++sy)
				for (int sx = x0; sx < x1; ++sx) {
					const auto* px = in.rgba.data() + (static_cast<std::size_t>(sy) * in.width + sx) * 4;
					for (int c = 0; c < 3; ++c) sum[c] += px[c] * (px[3] / 255.0);
					sum[3] += px[3], ++n;
				}
			auto* px = out.at(x, y);
			for (int c = 0; c < 3; ++c) px[c] = static_cast<unsigned char>(sum[3] > 0 ? std::clamp(sum[c] * 255.0 / sum[3], 0.0, 255.0) : 0);
			px[3] = static_cast<unsigned char>(sum[3] / n);
		}
	return out;
}
// Part of a picture.
Picture Crop(const Picture& in, int left, int top, int width, int height) {
	Picture out { std::max(0, width), std::max(0, height), std::vector<unsigned char>(static_cast<std::size_t>(std::max(0, width)) * std::max(0, height) * 4) };
	for (int y = 0; y < out.height; ++y) std::memcpy(out.at(0, y), in.rgba.data() + (static_cast<std::size_t>(top + y) * in.width + left) * 4, static_cast<std::size_t>(out.width) * 4);
	return out;
}
void Paste(Picture& onto, const Picture& part, int left, int top) {
	for (int y = 0; y < part.height && top + y < onto.height; ++y)
		if (top + y >= 0 && left >= 0 && left < onto.width && std::min(part.width, onto.width - left) > 0) std::memcpy(onto.at(left, top + y), part.rgba.data() + static_cast<std::size_t>(y) * part.width * 4, static_cast<std::size_t>(std::min(part.width, onto.width - left)) * 4);
}
// A frame stretched to width x height, its borders kept: the corners as they
// are (scaled down to fit), the edges stretched along, the middle both ways.
Picture Stretch(const Picture& in, int width, int height, int capX, int capY) {
	capX = std::min({ capX, in.width / 2 - 1, width / 2 }), capY = std::min({ capY, in.height / 2 - 1, height / 2 });
	if (capX < 0 || capY < 0) return Resize(in, width, height);
	Picture   out { width, height, std::vector<unsigned char>(static_cast<std::size_t>(width) * height * 4) };
	const int sx[] { 0, capX, in.width - capX }, sw[] { capX, in.width - 2 * capX, capX };
	const int sy[] { 0, capY, in.height - capY }, sh[] { capY, in.height - 2 * capY, capY };
	const int dx[] { 0, capX, width - capX }, dw[] { capX, width - 2 * capX, capX };
	const int dy[] { 0, capY, height - capY }, dh[] { capY, height - 2 * capY, capY };
	for (int r = 0; r < 3; ++r)
		for (int c = 0; c < 3; ++c)
			if (sw[c] > 0 && sh[r] > 0 && dw[c] > 0 && dh[r] > 0) Paste(out, Resize(Crop(in, sx[c], sy[r], sw[c], sh[r]), dw[c], dh[r]), dx[c], dy[r]);
	return out;
}
// A left cap, a middle stretched and a right cap side by side, `height` high
// (the caps scaled to it), `width` wide.
Picture Join(const Picture& left, const Picture& middle, const Picture& right, int width, int height) {
	const auto scaled = [&](const Picture& part) { return Resize(part, std::max(1, part.width * height / std::max(1, part.height)), height); };
	Picture l = scaled(left), r = scaled(right);
	// Ends wider than the box together: both shrunk so they share it.
	if (l.width + r.width > width) {
		const int total = l.width + r.width, lw = std::max(1, width * l.width / total), rw = std::max(1, width - lw);
		l = Resize(l, lw, height), r = Resize(r, rw, height);
	}
	Picture       out { width, height, std::vector<unsigned char>(static_cast<std::size_t>(width) * height * 4) };
	Paste(out, Resize(middle, std::max(1, width - l.width - r.width), height), l.width, 0);
	Paste(out, l, 0, 0);
	Paste(out, r, width - r.width, 0);
	return out;
}


// A small downward arrow, for a drop-down made by the plugin.
void Arrow(Picture& picture, int cx, int cy, const unsigned char* color) {
	for (int y = 0; y < 9; ++y)
		for (int x = -8 + y; x <= 8 - y; ++x)
			if (cx + x >= 0 && cx + x < picture.width && cy + y - 4 >= 0 && cy + y - 4 < picture.height) {
				auto* p = picture.at(cx + x, cy + y - 4);
				std::memcpy(p, color, 3), p[3] = 255;
			}
}

// The settings page's sprites: the game's art when it was read, else the
// plugin's own. Once, after the lists are read.
// The search bar's box sprites, one per length (the drop-down's list line, as the objectives' box).
std::vector<Picture> TileArt[3]; // its left, middle and right parts, kept from the settings' art
std::set<int>        SearchBoxes;
std::string SearchBox(int width) {
	if (SearchBoxes.insert(width).second) {
		const auto& [l, m, r] = TileArt;
		if (!l.empty() && !m.empty() && !r.empty()) (void)RegisterSprite("searchbox" + std::to_string(width), { Join(l[0], m[0], r[0], width, DropHeight) });
		else (void)RegisterSprite("searchbox" + std::to_string(width), { CategoryButton(false, width, DropHeight)[0] });
	}
	return "d2rloader\\grail-on\\searchbox" + std::to_string(width);
}

void RegisterSettingsArt() {
	std::vector<std::vector<Picture>> art;
	for (const auto& entry : SettingsArt) art.push_back(SpriteFrames(entry.file, std::strrchr(entry.path, '/') + 1));
	bool ok = true;
	// The check box: frames 0-3 off, 4-7 on (normal, hovered, pressed, disabled).
	if (const auto& box = art[ArtCheck]; box.size() >= 7) {
		std::vector<Picture> off, on;
		for (const int f : { 0, 1, 2 }) off.push_back(Fit(box[f], ButtonSize, ButtonSize)), on.push_back(Fit(box[f + 4], ButtonSize, ButtonSize));
		ok = RegisterSprite("tick", off) && RegisterSprite("tickOn", on) && ok;
	} else ok = RegisterSprite("tick", CheckBox(false)) && RegisterSprite("tickOn", CheckBox(true)) && ok;
	// The line between sections.
	if (const auto& line = art[ArtLine]; !line.empty()) {
		LineHeight = std::max(2, line[0].height * HeaderWidth / std::max(1, line[0].width));
		ok         = RegisterSprite("section", { Resize(line[0], HeaderWidth, LineHeight) }) && ok;
	} else ok = RegisterSprite("section", { Divider(HeaderWidth, LineHeight = 4) }) && ok;
	// The spoiler table's rows and column lines.
	if (const auto& row = art[ArtRow]; !row.empty()) ok = RegisterSprite("tablerow", { Stretch(row[0], TableWidth, TableRow, 24, 16) }) && ok;
	else ok = RegisterSprite("tablerow", { CategoryButton(false, TableWidth, TableRow)[0] }) && ok;
	if (const auto& column = art[ArtColumn]; !column.empty()) ok = RegisterSprite("tablecol", { Resize(column[0], column[0].width, TableRow - 14) }) && ok;
	else ok = RegisterSprite("tablecol", { Divider(2, TableRow - 14) }) && ok;
	// The drop-down: frames 0 normal, 1 pressed, 2 disabled, 3 hovered; its
	// list's lines: 0 normal, 1 hovered when there is one. Two widths: the
	// settings' (half the table) and the misc types' (narrow).
	for (const auto& [suffix, width] : { std::pair { "", DropWidth }, { "s", MiscDrop } }) {
		if (const auto &l = art[ArtDropLeft], &m = art[ArtDropCenter], &r = art[ArtDropRight]; !l.empty() && !m.empty() && !r.empty()) {
			std::vector<Picture> box;
			for (std::size_t f = 0; f < 4; ++f) {
				const auto at = [&](const std::vector<Picture>& part) { return part[std::min(f, part.size() - 1)]; };
				box.push_back(Join(at(l), at(m), at(r), width, DropHeight));
			}
			ok = RegisterSprite(std::string("dropbox") + suffix, box) && ok;
		} else {
			static constexpr unsigned char gold[3] { 199, 179, 119 };
			auto frames = CategoryButton(false, width, DropHeight);
			for (auto& frame : frames) Arrow(frame, width - 30, DropHeight / 2, gold);
			std::vector<Picture> box { frames[0], frames[2], frames[0], frames[1] };
			ok = RegisterSprite(std::string("dropbox") + suffix, box) && ok;
		}
		if (const auto &l = art[ArtTileLeft], &m = art[ArtTileCenter], &r = art[ArtTileRight]; !l.empty() && !m.empty() && !r.empty()) {
			std::vector<Picture> tile;
			for (std::size_t f = 0; f < 2; ++f) {
				const auto at = [&](const std::vector<Picture>& part) { return part[std::min(f, part.size() - 1)]; };
				tile.push_back(Join(at(l), at(m), at(r), width, TileHeight));
			}
			ok = RegisterSprite(std::string("droptile") + suffix, tile) && ok;
		} else {
			auto frames = CategoryButton(false, width, TileHeight);
			ok          = RegisterSprite(std::string("droptile") + suffix, { frames[0], frames[1] }) && ok;
		}
	}
	// The grail button's steps: the drop-down's plate with its left end on
	// both sides (its right end has the arrow); frames as the drop-down's.
	if (const auto &l = art[ArtDropLeft], &m = art[ArtDropCenter]; !l.empty() && !m.empty()) {
		std::vector<Picture> box;
		for (std::size_t f = 0; f < 4; ++f) {
			const Picture& left = l[std::min(f, l.size() - 1)];
			Picture        right { left.width, left.height, std::vector<unsigned char>(left.rgba.size()) };
			for (int y = 0; y < left.height; ++y)
				for (int x = 0; x < left.width; ++x) std::memcpy(right.at(left.width - 1 - x, y), const_cast<Picture&>(left).at(x, y), 4);
			box.push_back(Join(left, m[std::min(f, m.size() - 1)], right, SmallBox, DropHeight));
		}
		ok = RegisterSprite("stepbox", box) && ok;
	} else {
		auto plain = CategoryButton(false, SmallBox, DropHeight);
		ok         = RegisterSprite("stepbox", { plain[0], plain[2], plain[0], plain[1] }) && ok;
	}
	{
		auto plain = CategoryButton(false, ResetBox, DropHeight);
		ok         = RegisterSprite("resetbox", { plain[0], plain[2], plain[0], plain[1] }) && ok;
	}
	TileArt[0] = art[ArtTileLeft], TileArt[1] = art[ArtTileCenter], TileArt[2] = art[ArtTileRight];
	SearchBoxes.clear();
	// The objectives' text box: a line of the drop-down's list, at the drop-down's height.
	if (const auto &l = art[ArtTileLeft], &m = art[ArtTileCenter], &r = art[ArtTileRight]; !l.empty() && !m.empty() && !r.empty())
		ok = RegisterSprite("inputbox", { Join(l[0], m[0], r[0], ObjInputWidth, DropHeight) }) && ok;
	else ok = RegisterSprite("inputbox", { CategoryButton(false, ObjInputWidth, DropHeight)[0] }) && ok;
	// The objectives' buttons, the loot filter's: up and down arrows (frames 0
	// normal, 1 hovered, 2 pressed), New Rule as Add, the close button as remove.
	// Each sheet cut to what its frames draw first (the sheets pad their art
	// differently), so the buttons line up side by side.
	const auto fitted = [&](const std::vector<Picture>& frames, int width, int height) {
		int left = 1 << 30, top = 1 << 30, right = -1, bottom = -1;
		for (const auto& frame : frames)
			for (int y = 0; y < frame.height; ++y)
				for (int x = 0; x < frame.width; ++x)
					if (frame.rgba[(static_cast<std::size_t>(y) * frame.width + x) * 4 + 3] > 24) left = std::min(left, x), right = std::max(right, x), top = std::min(top, y), bottom = std::max(bottom, y);
		std::vector<Picture> out;
		for (const auto& frame : frames) {
			if (right < left || right >= frame.width || bottom >= frame.height) {
				out.push_back(Fit(frame, width, height));
				continue;
			}
			Picture cut { right - left + 1, bottom - top + 1, std::vector<unsigned char>(static_cast<std::size_t>(right - left + 1) * (bottom - top + 1) * 4) };
			for (int y = 0; y < cut.height; ++y) std::memcpy(cut.at(0, y), frame.rgba.data() + (static_cast<std::size_t>(top + y) * frame.width + left) * 4, static_cast<std::size_t>(cut.width) * 4);
			out.push_back(Fit(cut, width, height));
		}
		return out;
	};
	for (const auto& [name, index, text] : { std::tuple { "uparrow", ArtUp, "^" }, { "downarrow", ArtDown, "v" } }) {
		if (art[index].size() >= 3) ok = RegisterSprite(name, fitted(art[index], DropHeight, DropHeight)) && ok;
		else {
			auto plain = CategoryButton(false, DropHeight, DropHeight);
			static constexpr unsigned char gold[3] { 199, 179, 119 };
			for (auto& frame : plain) Arrow(frame, DropHeight / 2, DropHeight / 2, gold);
			if (text[0] == '^')
				for (auto& frame : plain) // pointing up: the frame upside down
					for (int y = 0; y < frame.height / 2; ++y)
						for (int x = 0; x < frame.width; ++x) std::swap_ranges(frame.at(x, y), frame.at(x, y) + 4, frame.at(x, frame.height - 1 - y));
			ok = RegisterSprite(name, { plain[0], plain[1], plain[2] }) && ok;
		}
	}
	if (const auto& add = art[ArtNew]; add.size() >= 3) ok = RegisterSprite("addbutton", fitted(add, AddWidth, DropHeight)) && ok;
	else {
		auto plain = CategoryButton(false, AddWidth, DropHeight);
		static constexpr unsigned char gold[3] { 199, 179, 119 };
		for (auto& frame : plain) // a plus
			for (int d = -12; d <= 12; ++d)
				for (int w = -2; w <= 2; ++w) std::memcpy(frame.at(AddWidth / 2 + d, DropHeight / 2 + w), gold, 3), frame.at(AddWidth / 2 + d, DropHeight / 2 + w)[3] = 255,
				                                std::memcpy(frame.at(AddWidth / 2 + w, DropHeight / 2 + d), gold, 3), frame.at(AddWidth / 2 + w, DropHeight / 2 + d)[3] = 255;
		ok = RegisterSprite("addbutton", { plain[0], plain[1], plain[2] }) && ok;
	}
	if (const auto& close = art[ArtClose]; close.size() >= 4) ok = RegisterSprite("removebutton", fitted(close, DropHeight, DropHeight)) && ok;
	else {
		auto plain = CategoryButton(false, DropHeight, DropHeight);
		ok         = RegisterSprite("removebutton", { plain[0], plain[2], plain[0], plain[1] }) && ok;
	}
	// The icon picker's box: a loot filter list line, stretched.
	if (const auto& row = art[ArtRow]; !row.empty()) ok = RegisterSprite("pickback", { Stretch(row[0], PickWidth, PickHeight, 24, 16) }) && ok;
	else ok = RegisterSprite("pickback", { CategoryButton(false, PickWidth, PickHeight)[0] }) && ok;
	// The reset: the lobby's refresh button, at the drop-down's height.
	if (!RefreshArt.file.empty()) {
		auto frames = SpriteFrames(RefreshArt.file, "refresh button");
		for (auto& frame : frames) frame = Fit(frame, DropHeight, DropHeight);
		if (frames.empty() || !RegisterSprite("refresh", frames)) RefreshArt.file.clear();
		const int count = static_cast<int>(frames.size());
		RefreshArt.normal = std::clamp(RefreshArt.normal, 0, std::max(0, count - 1)), RefreshArt.hovered = std::clamp(RefreshArt.hovered, 0, std::max(0, count - 1)),
		RefreshArt.pressed = std::clamp(RefreshArt.pressed, 0, std::max(0, count - 1));
	}
	if (!ok) Say(true, "grailon: could not register the settings page's pictures");
}

// A square button in the Advanced Stats button's materials (that button is an
// arrow): its straight left border on all four sides around its dark field.
Picture SquareButton(Picture in, int width, int height) {
	constexpr int Left = 3, Border = 8, Top = 22, Length = 24, Field = 10;
	Picture out { width, height, std::vector<unsigned char>(static_cast<std::size_t>(width) * height * 4) };
	if (in.width < Field + 1 || in.height < Top + Length) return Fit(in, width, height);
	// The dark field: the column inside the border, averaged.
	unsigned field[4] {};
	for (int j = 0; j < Length; ++j)
		for (int c = 0; c < 4; ++c) field[c] += in.at(Field, Top + j)[c];
	for (int y = Border; y < height - Border; ++y)
		for (int x = Border; x < width - Border; ++x)
			for (int c = 0; c < 4; ++c) out.at(x, y)[c] = static_cast<unsigned char>(field[c] / Length);
	// Pixel (i, j) of the border strip: i across (0 outside), j along it.
	const auto strip = [&](int i, int j) { return in.at(Left + i, Top + j % Length); };
	for (int j = 0; j < height; ++j)
		for (int i = 0; i < Border; ++i) std::memcpy(out.at(i, j), strip(i, j), 4), std::memcpy(out.at(width - 1 - i, j), strip(i, j), 4);
	for (int j = 0; j < width; ++j)
		for (int i = 0; i < Border; ++i) std::memcpy(out.at(j, i), strip(i, j), 4), std::memcpy(out.at(j, height - 1 - i), strip(i, j), 4);
	return out;
}

std::vector<Picture> GrailButton(const std::vector<Picture>& backs = {}) {
	const unsigned char* mask = nullptr;
	for (const auto& icon : IconMasks)
		if (!std::strcmp(icon.name, "grail")) mask = icon.alpha;
	// The icon's coverage at GrailIconSize: each pixel the average of the mask's.
	// On the game's button: the button fitted to the mold's hole, the icon
	// smaller, inside its frame.
	std::vector<Picture> fitted;
	for (const auto& back : backs) fitted.push_back(SquareButton(back, GrailButtonWidth, GrailButtonHeight));
	const int iconSize = fitted.empty() ? GrailIconSize : std::min(GrailButtonWidth, GrailButtonHeight) * 58 / 100;
	std::vector<double> cover(static_cast<std::size_t>(iconSize) * iconSize);
	const double        step = static_cast<double>(IconSize) / iconSize;
	for (int y = 0; mask && y < iconSize; ++y)
		for (int x = 0; x < iconSize; ++x) {
			double sum = 0;
			int    n   = 0;
			for (int sy = static_cast<int>(y * step); sy < static_cast<int>((y + 1) * step) && sy < IconSize; ++sy)
				for (int sx = static_cast<int>(x * step); sx < static_cast<int>((x + 1) * step) && sx < IconSize; ++sx) sum += mask[sy * IconSize + sx], ++n;
			cover[static_cast<std::size_t>(y) * iconSize + x] = n ? sum / n / 255.0 : 0;
		}
	std::vector<Picture> frames;
	const struct {
		int r, g, b, dy;
		double glow;
	} looks[] { { 215, 200, 150, 0, 0 }, { 170, 155, 110, 1, 0 }, { 110, 105, 95, 0, 0 }, { 255, 238, 180, 0, 0.35 } };
	for (std::size_t k = 0; k < std::size(looks); ++k) {
		const auto& look = looks[k];
		Picture frame = fitted.empty() ? Picture { GrailButtonWidth, GrailButtonHeight, std::vector<unsigned char>(static_cast<std::size_t>(GrailButtonWidth) * GrailButtonHeight * 4) }
		                               : fitted[std::min(k, fitted.size() - 1)];
		const int left = (GrailButtonWidth - iconSize) / 2, top = (GrailButtonHeight - iconSize) / 2 + look.dy;
		for (int y = 0; y < GrailButtonHeight; ++y)
			for (int x = 0; x < GrailButtonWidth; ++x) {
				const int ix = x - left, iy = y - top;
				double    a  = ix >= 0 && iy >= 0 && ix < iconSize && iy < iconSize ? cover[static_cast<std::size_t>(iy) * iconSize + ix] : 0;
				if (!fitted.empty()) { // the icon over the game's button
					if (a <= 0) continue;
					auto* px = frame.at(x, y);
					const double under = px[3] / 255.0, alpha = a + under * (1 - a);
					px[0] = static_cast<unsigned char>((look.r * a + px[0] * under * (1 - a)) / alpha);
					px[1] = static_cast<unsigned char>((look.g * a + px[1] * under * (1 - a)) / alpha);
					px[2] = static_cast<unsigned char>((look.b * a + px[2] * under * (1 - a)) / alpha);
					px[3] = static_cast<unsigned char>(255 * alpha);
					continue;
				}
				// Hovered: a soft gold light behind the icon.
				double light = 0;
				if (look.glow > 0) {
					const double dx = (x + 0.5 - GrailButtonWidth / 2.0) / (GrailButtonWidth / 2.0), dy = (y + 0.5 - GrailButtonHeight / 2.0) / (GrailButtonHeight / 2.0);
					light           = look.glow * std::max(0.0, 1.0 - std::sqrt(dx * dx + dy * dy));
				}
				auto*        px    = frame.at(x, y);
				const double alpha = a + light * (1 - a);
				if (alpha <= 0) continue;
				px[0] = static_cast<unsigned char>((look.r * a + 200 * light * (1 - a)) / alpha);
				px[1] = static_cast<unsigned char>((look.g * a + 160 * light * (1 - a)) / alpha);
				px[2] = static_cast<unsigned char>((look.b * a + 80 * light * (1 - a)) / alpha);
				px[3] = static_cast<unsigned char>(255 * alpha);
			}
		frames.push_back(std::move(frame));
	}
	return frames;
}

std::set<std::string> ChartsMade;

// The sprite of a ring chart, made once for its numbers.
std::string ChartSprite(const std::vector<std::pair<int, Rgb>>& parts, int size, int hole, bool square) {
	std::string name = std::string(square ? "square" : "chart") + std::to_string(size) + "h" + std::to_string(hole);
	for (const auto& [value, color] : parts) name += "-" + std::to_string(value) + "_" + std::to_string(color.r) + "." + std::to_string(color.g) + "." + std::to_string(color.b);
	if (!ChartsMade.count(name) && RegisterSprite(name, { Ring(parts, size, hole, square) })) ChartsMade.insert(name);
	return "d2rloader\\grail-on\\" + name;
}

// ---- panels (UI thread) ----
// D2R keeps a panel's widgets after its first load, which is what makes a new
// view slow and a seen one fast. So ViewCount panels each keep one view: a view
// seen before only opens its panel again, and a new one replaces the view
// unused for longest (its panel is unloaded and gets the new layout).
//
// A view opens in two steps: ShowView makes its panel ready, the game thread
// moves the items into the new tiles (Arrange), then OpenPrepared opens it. So
// the items are already in place when the view appears.

std::map<int, int> LastInMenu; // menu -> its category shown last; guarded by Lock

struct ViewPanel {
	std::string                         id;
	D2RL::Panels::RegistrationHandle    panel  = D2RL::Panels::InvalidHandle;
	D2RL::Resources::RegistrationHandle layout = D2RL::Resources::InvalidHandle;
	bool                                opened {};
	std::string                         key;    // what it shows, empty before the first use
	unsigned long long                  used {}; // when it was last shown
	ULONGLONG                           closedAt {}; // when it was last closed (GetTickCount64)
};
ViewPanel                       Views[ViewCount];
int                             ShownView = -1, PreparedView = -1;
unsigned                        OpenSequence {}; // the latest prepared view; older ones never open
unsigned long long              UseClock {}, PrepareStart {};
bool                            PreparedNew {};
std::atomic<bool>               RebuildWanted {}, ArrangeRunning {}, ViewIsOpen {}, PluginMoving {};
std::atomic<D2RL::PlayerHandle> LocalPlayer {};
std::atomic<std::uint32_t>      ArrangeAgain {}; // asked for while one was running: 1, or the runtime id to wait for
D2RL::Resources::RegistrationHandle StringsHandle = D2RL::Resources::InvalidHandle;

bool IsOpen(D2RL::Panels::RegistrationHandle handle) {
	D2RL::Panels::PanelInfo info { .structSize = D2RL::Panels::PanelInfoSize };
	return handle != D2RL::Panels::InvalidHandle && PanelApi->getPanelInfo(Context, handle, &info) == D2RL::Panels::Result::Success
	    && info.presentationState == D2RL::Panels::PresentationState::Open;
}

bool ViewOpen() { return ShownView >= 0 && IsOpen(Views[ShownView].panel); }

void Animate();
std::set<int> LateClose; // views left open under the shown one while an item is on the cursor (UI thread)

// UI thread. Whether the local player has an item on the cursor.
bool CursorHasItem() {
	D2RL::ItemHandle handle {};
	const auto       player = LocalPlayer.load();
	return player && Inventory->getCursorItem(Context, player, &handle) == D2RL::Inventory::Result::Success && handle;
}

// UI thread, with `grailon trace 1`: a view step and whether the cursor holds an item.
void Trace(const char* step, int value = 0) {
	if (Config.trace) Say(false, "grailon trace: %s %d, cursor %s, shown %d, prepared %d, late %zu", step, value, CursorHasItem() ? "item" : "empty", ShownView, PreparedView, LateClose.size());
}

// UI thread: the views left open under the shown one close once the cursor is empty.
void CloseLateViews() {
	if (LateClose.empty() || CursorHasItem()) return;
	for (const int view : LateClose)
		if (view != ShownView && IsOpen(Views[view].panel)) (void)PanelApi->closePanel(Context, Views[view].panel), Views[view].closedAt = GetTickCount64(), Trace("late close", view);
	LateClose.clear();
}

void CloseScrollPanel();
void SyncArea();
D2RL::Panels::RegistrationHandle    ErrorPanel       = D2RL::Panels::InvalidHandle;
D2RL::Resources::RegistrationHandle ErrorLayoutHandle = D2RL::Resources::InvalidHandle;
bool                                ErrorLoaded {};
void CloseView() {
	++OpenSequence; // a view being prepared stays closed
	LateClose.clear();
	CloseScrollPanel();
	if (ErrorPanel != D2RL::Panels::InvalidHandle && IsOpen(ErrorPanel)) (void)PanelApi->closePanel(Context, ErrorPanel);
	PreparedView = -1;
	for (const auto& view : Views)
		if (IsOpen(view.panel)) (void)PanelApi->closePanel(Context, view.panel);
	ShownView  = -1;
	ViewIsOpen = false;
	SyncArea();
}

// An item waiting for its tile: dropped (on the cursor until then) or
// Ctrl+clicked (where it was until then).
struct Drop {
	bool          wanted {}, cursor = true;
	int           unique = -1;
	bool          eth {};
	std::uint32_t runtimeId {};
	int           width = 1, height = 1;
	// Ctrl+click on an item whose tile holds its twin: the twin goes where the
	// clicked item was (that container, else the stash or the inventory).
	std::uint32_t        swapOut {};
	Items::ItemContainer from {};
	std::uint32_t        code {}; // its base
	bool                 base {}; // a base for its runeword tile, not a grail item
};

struct ArrangeJob {
	D2RL::PlayerHandle player {};
	bool               open {};       // opens view `sequence` once done
	unsigned           sequence {};
	std::uint32_t      expect {};     // an item about to land on the page: wait for it
	int                tries {};
	Drop               drop;
};

void __cdecl Arrange(const D2RL::PluginContext* context, void* userData) noexcept;
void         OpenPrepared(unsigned sequence);
void __cdecl OpenPreparedOnUi(const D2RL::PluginContext*, void* userData) noexcept;

// An item for a tile of a view still being prepared: it stays where it is (on
// the cursor, or where it was Ctrl+clicked) until that view is open, so it is
// never seen anywhere but in its tile. Guarded by Lock.
Drop LateDrop;
// The cursor item, kept off the cursor while the views change (the switch
// lost it on the client), and a cut slot's stored item on its way to the
// cursor. Items in Busy are left alone by Arrange. Busy is guarded by Lock.
std::atomic<std::uint32_t> HeldId {}, TakeId {};
std::atomic<bool>          Restoring {}, Taking {};
std::set<std::uint32_t>    Busy;
bool                       Holding {}; // UI thread: a hold for a scroll is on its way
int                        HoldRows {};
unsigned                   HoldSequence {}; // the view switch a hold was tried for
void                       KickPending();
bool                       HoldCursor(D2RL::Threads::Callback then, void* data);

// A scroll by one row, animated (UI thread): the frames of the view on screen
// show in turn while the next view is made; it opens after the last frame.
struct ScrollAnimation {
	bool                                     running {};
	int                                      frame {};       // shown so far
	bool                                     down {};
	unsigned                                 deferred {};    // the next view, ready before the frames ended
	int                                      queued {};      // wheel steps that came meanwhile
	std::vector<D2RL::Widgets::WidgetHandle> frames;
} Anim;
D2RL::Panels::RegistrationHandle    ScrollPanel  = D2RL::Panels::InvalidHandle;
D2RL::Resources::RegistrationHandle ScrollLayoutHandle = D2RL::Resources::InvalidHandle;
bool                                ScrollLoaded {};
void CloseScrollPanel() {
	if (ScrollPanel != D2RL::Panels::InvalidHandle && IsOpen(ScrollPanel)) (void)PanelApi->closePanel(Context, ScrollPanel), Trace("scroll frames closed");
}
double UiScale();
void   RunScroll(int rows, bool afterHold = false);

std::size_t RecordCount();
bool Remote();
bool RemoteArrange(ArrangeJob* job);
bool QueueArrange(ArrangeJob* job) {
	if (Remote()) return RemoteArrange(job);
	if (Threads->runOnGameThread(Context, Arrange, job) == D2RL::Threads::Result::Success) return true;
	delete job;
	return false;
}

// UI thread: the grail page with what is switched off, made again each time
// (its panel unloaded so the new layout is read).
void ShowError() {
	CloseView();
	if (ErrorPanel == D2RL::Panels::InvalidHandle) return;
	if (ErrorLoaded) {
		const D2RL::Widgets::UiAction unload { .structSize = D2RL::Widgets::UiActionSize, .flags = 0, .target = "PanelManager", .command = "UnloadPanel", .text = "grail-on/Error" };
		(void)WidgetApi->dispatchUiAction(Context, &unload);
	}
	if (ErrorLayoutHandle != D2RL::Resources::InvalidHandle) (void)ResourceApi->unregisterResource(Context, ErrorLayoutHandle);
	ErrorLayoutHandle = D2RL::Resources::InvalidHandle;
	if (!RegisterResource("data/global/ui/layouts/grail-on/Errorhd.json", ErrorLayout(), &ErrorLayoutHandle)) return;
	if (const auto result = PanelApi->openPanel(Context, ErrorPanel); result != D2RL::Panels::Result::Success) Say(true, "grailon: could not open the error page (result %u)", static_cast<unsigned>(result));
	ErrorLoaded = true;
}

// Makes the view of the current category and scroll ready and has the items
// moved into its tiles; it opens after that. `drop` is put in its tile on the
// way. With something the grail needs switched off, the page says what instead.
void ShowView(const Drop& drop = {}) {
	if (BlockingCount > 0 || !HavePage()) {
		if (!BlockingCount) Problem(true, "grailon: the grail page is not available");
		ShowError();
		return;
	}
	if (ListsReady && StringsWanted) {
		ViewAfterStrings = true; // the tiles' names first: the view right after
		return;
	}
	if (!ListsReady) {
		Say(true, "grailon: the item lists are not ready yet");
		return;
	}
	int         index = -1;
	bool        settings {};
	std::string layout;
	{
		std::lock_guard guard(Lock);
		if (JumpTo >= 0 && JumpTo < UniqueCount()) CurrentCategory = Uniques[JumpTo].category, ViewEth = JumpEth;
		const bool secretJump = JumpTo >= 0 && JumpTo == SecretEntry;
		if (secretJump) StatsPage = 0;
		CurrentCategory = std::clamp(CurrentCategory, StatsView, CategoryCount() - 1);
		settings        = CurrentCategory < 0;
		// Only unique weapons and armor have ethereal views.
		if (!settings) ViewEth = ViewEth && IsEthMenu(MenuOf(CurrentCategory, true)) && InMenu(CurrentCategory, MenuOf(CurrentCategory, true));
		if (!settings) LastInMenu[CurrentMenu()] = CurrentCategory;
		PlaceRows(BuildRows(CurrentCategory));
		// The stats overview: the Standard of Heroes' tile at the bottom, only
		// while it is in the grail (or going in).
		if (CurrentCategory == StatsView && StatsPage == 0 && SecretEntry >= 0 && (secretJump || SecretWanted || Stored.count({ SecretEntry, false }))) {
			const auto& entry = Uniques[SecretEntry];
			const Tile  tile { SecretEntry, false, SlotColumns[2], PageSize - entry.height - 1, entry.width, entry.height, 2 };
			NextHalfTiles.clear();
			// Under the host its rows are past the page's: a picture, like a cut slot.
			if (tile.y + tile.height > PageRows) Peeking.push_back(tile);
			else {
				Shown.push_back(tile);
				AddHalf(NextHalfTiles, tile);
			}
		}
		(void)RefreshPerfect();
		if (static int told = 0; Remote() && told < 6) {
			++told;
			Say(false, "grailon: joined game view: category %d, %zu tiles on the page, %zu pictures, %zu stored, %zu records, lists %d", CurrentCategory, Shown.size(), Peeking.size(), Stored.size(),
			    RecordCount(), ListsReady.load());
		}
		settings              = !TilesView(); // the stats overview keeps its grid for the standard's tile
		const std::string key = ViewKey();
		for (int i = 0; i < ViewCount; ++i)
			if (Views[i].key == key) index = i;
		if (index >= 0 && index == ShownView && IsOpen(Views[index].panel) && !drop.wanted) {
			++OpenSequence; // the view on screen is already this one
			PreparedView = -1;
			ApplyHalfTiles();
			return;
		}
		if (index < 0) {
			// The view unused for longest. Never one still in use: on screen, open
			// (kept under a cursor item, its close pending), being prepared, or
			// closed just now (the game may still be letting its widgets go);
			// unloading such a panel crashed the game in its layout loader.
			const ULONGLONG now = GetTickCount64();
			for (const bool allowRecent : { false }) { // a panel closed under 1.5 s ago is never loaded again: the game's layout loader crashed on one
				for (int i = 0; i < ViewCount; ++i) {
					const auto& view = Views[i];
					if (i == ShownView || i == PreparedView || view.panel == D2RL::Panels::InvalidHandle || LateClose.count(i) || IsOpen(view.panel)) continue;
					if (!allowRecent && view.opened && now - view.closedAt < 1500) continue;
					if (index < 0 || view.used < Views[index].used) index = i;
				}
				if (index >= 0) break;
			}
			if (index < 0) {
				static int told = 0;
				if (told++ < 3) Say(false, "grailon: no view panel is free yet; the view waits a moment");
				ViewRetryAt = GetTickCount64() + 300;
				return;
			}
			if (Views[index].opened) Say(false, "grailon: view panel %d recycled (unused for %llu views)", index, UseClock - Views[index].used);
			layout           = ViewLayout(Views[index].id);
			Views[index].key = key;
		}
	}
	auto& next   = Views[index];
	PrepareStart = GetTickCount64();
	PreparedNew  = !layout.empty();
	if (!layout.empty()) {
		if (next.opened && WidgetApi) {
			const std::string             name = std::string("grail-on/") + next.id;
			const D2RL::Widgets::UiAction unload { .structSize = D2RL::Widgets::UiActionSize, .flags = 0, .target = "PanelManager", .command = "UnloadPanel", .text = name.c_str() };
			(void)WidgetApi->dispatchUiAction(Context, &unload);
		}
		if (next.layout != D2RL::Resources::InvalidHandle) (void)ResourceApi->unregisterResource(Context, next.layout);
		next.layout = D2RL::Resources::InvalidHandle;
		if (!RegisterResource("data/global/ui/layouts/grail-on/" + next.id + "hd.json", layout, &next.layout)) Say(true, "grailon: could not register the view layout");
	}
	PreparedView        = index;
	const auto sequence = ++OpenSequence;
	// The settings show no tiles: their grid is off screen, the items stay.
	if (const auto player = LocalPlayer.load(); player && !settings) {
		auto* job = new ArrangeJob { .player = player, .open = true, .sequence = sequence, .drop = drop };
		if (QueueArrange(job)) return;
	}
	OpenPrepared(sequence);
}

void OpenPrepared(unsigned sequence) {
	if (sequence != OpenSequence || PreparedView < 0) return; // a newer view is on its way
	// The frames of a scroll are still showing: the view opens after them.
	if (Anim.running) {
		Anim.deferred = sequence;
		return;
	}
	WrapGridVtable();
	// The items found a tile that changes what the view shows (a unique just
	// found): make that view instead.
	if (RebuildWanted.exchange(false)) {
		ShowView();
		return;
	}
	// An item on the cursor is kept off it while the views change, unless it is
	// the one about to go in its tile.
	bool dropping = false;
	{
		std::lock_guard guard(Lock);
		dropping = LateDrop.wanted && LateDrop.cursor;
	}
	if (Config.hold && !dropping && HoldSequence != sequence && ViewOpen()) {
		HoldSequence = sequence;
		if (HoldCursor(OpenPreparedOnUi, reinterpret_cast<void*>(static_cast<std::uintptr_t>(sequence)))) return;
	}
	const int index    = PreparedView;
	const int previous = ShownView;
	PreparedView       = -1;
	auto&      next    = Views[index];
	const bool holding = CursorHasItem();
	Trace("view open", index);
	{
		std::lock_guard guard(Lock);
		ApplyHalfTiles();
	}
	const auto result  = PanelApi->openPanel(Context, next.panel);
	Trace("view opened", index);
	if (holding && !CursorHasItem()) Say(false, "grailon: the cursor item went when the next view opened");
	// The new view opens first and the old one closes after, so the window never
	// disappears between two views. With an item on the cursor the old one stays
	// open under it until the cursor is empty (closing it may drop the item).
	if (previous >= 0 && previous != index && IsOpen(Views[previous].panel)) {
		if (holding && !Config.hold) LateClose.insert(previous);
		else (void)PanelApi->closePanel(Context, Views[previous].panel), Views[previous].closedAt = GetTickCount64(), Trace("old view closed", previous);
	}
	LateClose.erase(index);
	CloseScrollPanel();
	if (holding && !CursorHasItem()) Say(false, "grailon: the cursor item went when the scroll frames closed");
	if (Config.timing) Say(false, "grailon: %s view, shown after %llu ms", PreparedNew ? "new" : "cached", static_cast<unsigned long long>(GetTickCount64() - PrepareStart));
	if (result != D2RL::Panels::Result::Success) Say(true, "grailon: could not open the view (result %u)", static_cast<unsigned>(result));
	next.opened = true;
	next.used   = ++UseClock;
	ShownView   = index;
	ViewIsOpen  = true;
	SyncArea();
	// The item waiting for this view goes into its tile now.
	Drop late;
	{
		std::lock_guard guard(Lock);
		late              = LateDrop;
		LateDrop.wanted   = false;
	}
	if (late.wanted)
		if (const auto player = LocalPlayer.load()) (void)QueueArrange(new ArrangeJob { .player = player, .drop = late });
	KickPending();
}

void __cdecl OpenPreparedOnUi(const D2RL::PluginContext*, void* userData) noexcept { OpenPrepared(static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(userData))); }

// ---- moves (game thread) ----
// Items are moved by item transactions, which make no sound. Stored items whose
// tile is not on screen wait in the personal stash (Parked, the inventory when
// the stash is full) and come back when their tile is shown again.

D2RL::Inventory::IterationAction __cdecl Collect(const D2RL::PluginContext*, const Items::ItemInfo* item, void* userData) noexcept {
	if (!item) return D2RL::Inventory::IterationAction::Continue;
	Items::ItemInfo info = *item;
	Localize(info);
	if (info.container != Items::ItemContainer::Unknown || item->container != Items::ItemContainer::CustomPage) static_cast<std::vector<Items::ItemInfo>*>(userData)->push_back(info);
	return D2RL::Inventory::IterationAction::Continue;
}

// Every item as D2RLoader gives it (host page cells, nothing left out).
D2RL::Inventory::IterationAction __cdecl CollectRaw(const D2RL::PluginContext*, const Items::ItemInfo* item, void* userData) noexcept {
	if (item) static_cast<std::vector<Items::ItemInfo>*>(userData)->push_back(*item);
	return D2RL::Inventory::IterationAction::Continue;
}

// An item's info, page cells in the grail's own.
Items::Result GetInfo(const D2RL::PluginContext* context, D2RL::ItemHandle handle, Items::ItemInfo* info) {
	const auto result = ItemApi->getItemInfo(context, handle, info);
	if (result == Items::Result::Success && info) Localize(*info);
	return result;
}

Items::Result MoveItem(D2RL::PlayerHandle player, D2RL::ItemHandle item, const Items::ItemDestination& destination) {
	Items::ExistingItemOperation operation {};
	operation.structSize       = Items::ExistingItemOperationSize;
	operation.kind             = Items::ExistingItemOperationKind::Move;
	operation.item             = item;
	operation.move.destination = OnPage(destination);
	const Items::ExistingItemTransaction transaction { .structSize = Items::ExistingItemTransactionSize, .player = player, .operationCount = 1, .operations = &operation };
	Items::ExistingItemTransactionResult result { .structSize = Items::ExistingItemTransactionResultSize };
	return Transact(transaction, result);
}

// ---- item sound ----
// Item transactions are silent, so a Ctrl+clicked item that lands in its tile
// gets its drop sound played the way Cube Empty plays sounds (D2R 3.3.93787 /
// 94995, entry bytes checked; -1 = any byte, PlaySound may carry a jump).
namespace Sound {
constexpr std::uint64_t LocalIndex = 0x8B2D0, PlayerUnit = 0x9A480, Play = 0x1A0780;
constexpr int           LocalIndexBytes[] { 0x8B, 0x05, -1, -1, -1, -1, 0xC3 };
constexpr int           PlayerUnitBytes[] { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x83, 0xF9, 0x08, 0x0F, 0x83, 0x85 };
constexpr int           PlayBytes[] { -1, -1, -1, -1, -1, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xD1, 0x48, 0x81, 0xEC, 0x00, 0x01, 0x00, 0x00 };
} // namespace Sound

using LocalIndexFn = int (*)() noexcept;
using PlayerUnitFn = void* (*)(int index) noexcept;
using PlaySoundFn  = void (*)(int sound, void* unit, int, int, bool) noexcept;

template <std::size_t N>
bool CodeIs(std::uint64_t rva, const int (&bytes)[N]) {
	const auto* code = reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(Context->exeBase) + rva);
	for (std::size_t i = 0; i < N; ++i)
		if (bytes[i] >= 0 && code[i] != bytes[i]) return false;
	return true;
}

// ---- muted item sounds ----
// Giving the client its cursor item back (down on the page and up again) plays
// the item sounds. The game's sound function (Sound::Play) is entered through a
// stub that returns at once while SoundMuted is set. Another module already
// jumps from its start: the stub then goes on to that module's code.
volatile std::uint8_t      SoundMuted {};
std::atomic<std::uint64_t> MuteUntil {};
void*                      SoundStub {};

void MuteSounds(unsigned ms) {
	if (!SoundStub) return;
	MuteUntil  = GetTickCount64() + ms;
	SoundMuted = 1;
}

// UI thread.
void UnmuteSoundsWhenDue() {
	if (SoundMuted && GetTickCount64() >= MuteUntil.load()) SoundMuted = 0;
}

void InstallSoundMute() {
	const auto base = static_cast<std::uintptr_t>(Context->exeBase);
	auto*      at   = reinterpret_cast<std::uint8_t*>(base + Sound::Play);
	if (!CodeIs(Sound::Play, Sound::PlayBytes)) return;
	for (std::uintptr_t spot = (base & ~std::uintptr_t { 0xFFFF }) - 0x10000; !SoundStub && spot > base - 0x70000000; spot -= 0x10000)
		SoundStub = VirtualAlloc(reinterpret_cast<void*>(spot), 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (!SoundStub) return;
	// mov rax, &SoundMuted / cmp byte [rax], 0 / je go / ret / go: jmp [rip] next
	std::uint8_t stub[] { 0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0x80, 0x38, 0x00, 0x74, 0x01, 0xC3, 0xFF, 0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	const auto   flag = reinterpret_cast<std::uint64_t>(&SoundMuted);
	std::memcpy(stub + 2, &flag, 8);
	auto*      code = static_cast<std::uint8_t*>(SoundStub);
	const auto ret  = reinterpret_cast<std::uint64_t>(code + 15); // until the next one is known
	std::memcpy(stub + 22, &ret, 8);
	std::memcpy(code, stub, sizeof(stub));
	FlushInstructionCache(GetCurrentProcess(), code, sizeof(stub));
	auto* const next = reinterpret_cast<volatile LONG64*>(code + 22);
	if (at[0] == 0xE9) {
		std::int32_t rel = 0;
		std::memcpy(&rel, at + 1, 4);
		const auto   target = reinterpret_cast<std::uint64_t>(at + 5) + static_cast<std::int64_t>(rel);
		const auto   to     = reinterpret_cast<std::int64_t>(code) - reinterpret_cast<std::int64_t>(at + 5);
		InterlockedExchange64(next, static_cast<LONG64>(target));
		LONG64 old = 0;
		std::memcpy(&old, at, 8);
		LONG64 now = old;
		const auto to32 = static_cast<std::int32_t>(to);
		std::memcpy(reinterpret_cast<std::uint8_t*>(&now) + 1, &to32, 4);
		DWORD protect = 0;
		if (VirtualProtect(at, 8, PAGE_EXECUTE_READWRITE, &protect)) {
			if (InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(at), now, old) != old) SoundStub = nullptr;
			VirtualProtect(at, 8, protect, &protect);
			FlushInstructionCache(GetCurrentProcess(), at, 8);
		} else SoundStub = nullptr;
	} else {
		std::uint8_t live[sizeof(Sound::PlayBytes) / sizeof(int)] {};
		std::memcpy(live, at, sizeof(live));
		void* original = nullptr;
		if (Context->InstallInlineHook(Sound::Play, live, sizeof(live), SoundStub, &original) && original) InterlockedExchange64(next, reinterpret_cast<LONG64>(original));
		else SoundStub = nullptr;
	}
	if (!SoundStub) Problem(false, "grailon: the item sounds are not muted while the cursor item is given back");
}


// UI thread: plays sounds row `sound` at the player.
void __cdecl PlayItemSound(const D2RL::PluginContext*, void* userData) noexcept {
	static const bool fits = Context->exeBase && CodeIs(Sound::LocalIndex, Sound::LocalIndexBytes) && CodeIs(Sound::PlayerUnit, Sound::PlayerUnitBytes) && CodeIs(Sound::Play, Sound::PlayBytes);
	const int         sound = static_cast<int>(reinterpret_cast<std::uintptr_t>(userData));
	if (!fits || sound <= 0) return;
	const auto base   = static_cast<std::uintptr_t>(Context->exeBase);
	void*      player = reinterpret_cast<PlayerUnitFn>(base + Sound::PlayerUnit)(reinterpret_cast<LocalIndexFn>(base + Sound::LocalIndex)());
	if (player) reinterpret_cast<PlaySoundFn>(base + Sound::Play)(sound, player, 0, 0, false);
}

Items::Result MoveTo(D2RL::PlayerHandle player, D2RL::ItemHandle item, Items::ItemContainer container) {
	return MoveItem(player, item, { .structSize = Items::ItemDestinationRequiredSize, .container = container, .placement = Items::Placement::Automatic });
}

Items::Result MoveToCell(D2RL::PlayerHandle player, D2RL::ItemHandle item, int x, int y) {
	return MoveItem(player, item,
	                { .structSize       = Items::ItemDestinationRequiredSize,
	                  .container        = Items::ItemContainer::CustomPage,
	                  .placement        = Items::Placement::Exact,
	                  .customPageHandle = Page,
	                  .x                = static_cast<std::uint32_t>(x),
	                  .y                = static_cast<std::uint32_t>(y) });
}

// Moves an item already on the page with the cursor, the way the player would
// (this one makes the item sounds). Only used when a transaction is refused.
bool PageMove(D2RL::Inventory::LocalPlayerPageMoveKind kind, int x, int y);
bool MoveOnPageByCursor(const Items::ItemInfo& item, int x, int y) {
	if (Host::On()) {
		if (!PageMove(D2RL::Inventory::LocalPlayerPageMoveKind::PageToCursor, item.x, item.y)) return false;
		if (PageMove(D2RL::Inventory::LocalPlayerPageMoveKind::CursorToPage, x, y)) return true;
		(void)PageMove(D2RL::Inventory::LocalPlayerPageMoveKind::CursorToPage, item.x, item.y);
		return false;
	}
	D2RL::Inventory::LocalPlayerPageMoveRequest take { .structSize = D2RL::Inventory::LocalPlayerPageMoveRequestSize, .kind = D2RL::Inventory::LocalPlayerPageMoveKind::PageToCursor,
		                                               .x = static_cast<std::uint32_t>(item.x), .y = static_cast<std::uint32_t>(item.y) };
	if (Inventory->executeLocalPlayerMove(Context, Page, &take) != D2RL::Inventory::Result::Success) return false;
	D2RL::Inventory::LocalPlayerPageMoveRequest put { .structSize = D2RL::Inventory::LocalPlayerPageMoveRequestSize, .kind = D2RL::Inventory::LocalPlayerPageMoveKind::CursorToPage,
		                                              .x = static_cast<std::uint32_t>(x), .y = static_cast<std::uint32_t>(y) };
	if (Inventory->executeLocalPlayerMove(Context, Page, &put) == D2RL::Inventory::Result::Success) return true;
	put.x = static_cast<std::uint32_t>(item.x);
	put.y = static_cast<std::uint32_t>(item.y);
	(void)Inventory->executeLocalPlayerMove(Context, Page, &put);
	return false;
}

// ---- global storage ----
// Stored items live in one file shared by every character (softcore and
// hardcore apart), not in a character: "Saved Games/Diablo II Resurrected/
// grail-storage/grail-<mode>.dat". Each record is the item as the game's own
// save writer writes it (socketed items included), so it comes back exactly
// (the probes proved uniques, sets, runewords and socketed items rebuild byte
// for byte). Only the items of the tiles on screen exist in the game: they are
// made from their records when their tile shows (Materialize) and destroyed
// when it goes (their record stays). An item the player takes off the page
// leaves the grail: its record goes and its fingerprint is kept (Tombstones),
// so a copy another character's page still holds is destroyed, never taken.
// A page item nobody knows (an older version's, or stored before a crash) is
// recorded.
//
// Native (Steam 3.3.93787 / 94995, D2RCore of D2RLoader 1.3.1), as the probe:
//   exe+0x3E2B6A0  thunk to the item save writer: int (Unit*, u8*, size, 1, 1, 0, &sourceInfo)
//   exe+0x3E2B6AC  thunk to the item header reader: int (gameFlag, u8*, size, 1, header, version)
//   exe+0x43D900   item save loader: Unit* (Game*, 0, u8*, size, 1, header, int* read, version)
//   exe+0x1D16810  PlrSave.cpp source info; game+0x106 the header's game flag; version 0x69
//   D2RCore+0x6FFC58 {pointer, rva 0x43CD50}: D2RCore's pointer to the game's
//                  CreateItem, called at D2RCore+0x423492: swapped for one
//                  createItem call so it returns the decoded item instead.
//   exe+0x48FE80 GetUnitById, exe+0x46EB00 socket an item (game, player, child, parent, 1),
//   exe+0x48FAA0 free a loose item.
//   ItemData (unit+0x10) +0x10: the item's fingerprint (its save identity).

namespace Store {
constexpr std::uint64_t  SaveWriter = 0x3E2B6A0, HeaderReader = 0x3E2B6AC, SaveLoader = 0x43D900, SourceInfo = 0x1D16810, CreateItem = 0x43CD50;
constexpr std::uint64_t  UnitById = 0x48FE80, SocketItem = 0x46EB00, FreeItem = 0x48FAA0;
constexpr std::uintptr_t CoreSlot = 0x6FFC58, CoreCall = 0x423492, GameFlag = 0x106;
constexpr int            Version = 0x69;
constexpr std::size_t    BufferSize = 0x4000;
constexpr std::uint8_t   CoreCallBytes[] { 0xFF, 0x15, 0xC0, 0xC7, 0x2D, 0x00 };
constexpr std::uint8_t   SaveLoaderBytes[] { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x48, 0x89, 0x68, 0x18, 0x48, 0x89, 0x70, 0x20, 0x57 };
constexpr std::uint8_t   CreateItemBytes[] { 0x40, 0x55, 0x53, 0x56, 0x41, 0x54, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xC9, 0x48, 0x81, 0xEC };
constexpr std::uint8_t   UnitByIdBytes[] { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41 };
constexpr std::uint8_t   SocketItemBytes[] { 0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x70, 0x48, 0x8B, 0x05, 0xB6, 0xC7 };
} // namespace Store

using SaveWriterFn   = int (*)(void* item, std::uint8_t* buffer, std::size_t capacity, int, int, int, void** sourceInfo) noexcept;
using HeaderReaderFn = int (*)(int gameFlag, const std::uint8_t* buffer, std::size_t size, int, void* header, int version) noexcept;
using SaveLoaderFn   = void* (*)(void* game, void* owner, const std::uint8_t* buffer, int size, int, void* header, int* read, int version) noexcept;
using UnitByIdFn     = void* (*)(void* game, int type, std::uint32_t id) noexcept;
using SocketItemFn   = int (*)(void* game, void* player, std::uint32_t childId, std::uint32_t parentId, int fromSave) noexcept;
using FreeItemFn     = void (*)(void* game, void* item) noexcept;

struct Record {
	std::string               id;  // the grail entry (UniqueEntry::id)
	bool                      eth {}, perfect {};
	bool                      base {}; // a base kept in a runeword tile (saved as "b|" + id)
	std::uint32_t             fingerprint {};
	std::uint32_t             code {}, quality {}, itemLevel {}, stateFlags {};
	std::int32_t              qualityRecordId = -1;
	std::vector<std::uint8_t> bytes;
};
// All guarded by Lock.
std::map<std::uint32_t, Record>       Records;      // by fingerprint
std::size_t RecordCount() { return Records.size(); }
std::set<std::uint32_t>               Tombstones;   // fingerprints taken out of the grail
std::set<std::string>                 FoundIds;     // entries ever stored
std::map<std::uint32_t, std::uint32_t> Materialized; // runtime id of a record's item on the page -> fingerprint
std::map<std::uint32_t, std::uint32_t> Fingerprints; // runtime id -> fingerprint, read once
std::wstring                          StorePath;    // the open storage file, empty before a character
bool                                  StoreReady {}; // the native calls are there
D2RL::PlayerHandle                    StoreOwner {}; // the character it was opened for

SaveWriterFn   SaveItem {};
HeaderReaderFn ReadHeader {};
SaveLoaderFn   LoadItem {};
UnitByIdFn     UnitById {};
SocketItemFn   SocketItem {};
FreeItemFn     FreeItem {};
std::uintptr_t CoreModule {};

template <std::size_t N>
bool BytesAt(std::uintptr_t address, const std::uint8_t (&bytes)[N]) {
	std::uint8_t code[N] {};
	SIZE_T       copied = 0;
	return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), code, N, &copied) && copied == N && !std::memcmp(code, bytes, N);
}

bool LocateStore() {
	const auto exe = static_cast<std::uintptr_t>(Context->exeBase);
	CoreModule     = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"D2RCore.dll"));
	std::uint64_t slotRva = 0;
	SIZE_T        copied  = 0;
	const bool    core    = CoreModule && BytesAt(CoreModule + Store::CoreCall, Store::CoreCallBytes)
	                && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(CoreModule + Store::CoreSlot + 8), &slotRva, 8, &copied) && slotRva == Store::CreateItem;
	const auto thunk = [&](std::uint64_t rva) { const auto* code = reinterpret_cast<const std::uint8_t*>(exe + rva); return code[0] == 0xFF && code[1] == 0x25; };
	const bool game  = thunk(Store::SaveWriter) && thunk(Store::HeaderReader) && BytesAt(exe + Store::SaveLoader, Store::SaveLoaderBytes) && BytesAt(exe + Store::CreateItem, Store::CreateItemBytes)
	                && BytesAt(exe + Store::UnitById, Store::UnitByIdBytes) && BytesAt(exe + Store::SocketItem, Store::SocketItemBytes);
	if (!core || !game) {
		Problem(true, "grailon: this game or D2RLoader build is not the one the storage was made for (D2RCore %s, game %s): the grail cannot store items",
		    core ? "fits" : "differs", game ? "fits" : "differs");
		return false;
	}
	SaveItem   = reinterpret_cast<SaveWriterFn>(exe + Store::SaveWriter);
	ReadHeader = reinterpret_cast<HeaderReaderFn>(exe + Store::HeaderReader);
	LoadItem   = reinterpret_cast<SaveLoaderFn>(exe + Store::SaveLoader);
	UnitById   = reinterpret_cast<UnitByIdFn>(exe + Store::UnitById);
	SocketItem = reinterpret_cast<SocketItemFn>(exe + Store::SocketItem);
	FreeItem   = reinterpret_cast<FreeItemFn>(exe + Store::FreeItem);
	return true;
}

// ---- the file ----

// Where the active mod keeps its characters (Saved Games\Diablo II
// Resurrected\mods\<mod>, the game's own folder without a mod): the grail
// files sit beside them, so every mod has its own grail.
std::wstring SaveFolder() {
	PWSTR folder = nullptr;
	if (SHGetKnownFolderPath(FOLDERID_SavedGames, 0, nullptr, &folder) != S_OK) return {};
	std::wstring path = std::wstring(folder) + L"\\Diablo II Resurrected";
	CoTaskMemFree(folder);
	if (Context && Context->contextSize > offsetof(D2RL::PluginContext, activeMod) && Context->activeMod && *Context->activeMod) {
		const char* mod    = Context->activeMod;
		const int   length = MultiByteToWideChar(CP_UTF8, 0, mod, -1, nullptr, 0);
		std::wstring wide(length > 0 ? length - 1 : 0, L'\0');
		if (length > 1) MultiByteToWideChar(CP_UTF8, 0, mod, -1, wide.data(), length);
		path += L"\\mods";
		CreateDirectoryW(path.c_str(), nullptr);
		path += L"\\" + wide;
	}
	CreateDirectoryW(path.c_str(), nullptr);
	return path;
}
std::wstring StoreFolder() { return SaveFolder(); }

// The grail file `name` in the mod's save folder: each mod starts with an
// empty grail (the old shared grail-storage files are left alone).
std::wstring StoreFile(const std::wstring& folder, const wchar_t* name) { return folder + L"\\" + name; }

struct Writer {
	std::string out;
	void        u8(std::uint32_t v) { out += static_cast<char>(v); }
	void        u16(std::uint32_t v) { out.append(reinterpret_cast<const char*>(&v), 2); }
	void        u32(std::uint32_t v) { out.append(reinterpret_cast<const char*>(&v), 4); }
	void        text(const std::string& v) { u16(static_cast<std::uint32_t>(v.size())), out += v; }
};
struct Reader {
	const std::string& in;
	std::size_t        at {};
	bool               ok = true;
	std::uint32_t      get(std::size_t n) {
        std::uint32_t v = 0;
        if (at + n > in.size()) ok = false;
        else std::memcpy(&v, in.data() + at, n);
        at += n;
        return v;
	}
	std::string text() {
		const auto n = get(2);
		if (!ok || at + n > in.size()) return ok = false, std::string();
		std::string v = in.substr(at, n);
		at += n;
		return v;
	}
};

// Lock held. Writes the storage file (a temporary copy first, then swapped in).
void SaveStore() {
	if (StorePath.empty()) return;
	Writer w;
	w.out = "GRL1";
	w.u32(2);
	w.u32(static_cast<std::uint32_t>(Records.size()));
	for (const auto& [fingerprint, record] : Records) {
		w.text(record.base ? "b|" + record.id : record.id);
		w.u8(record.eth), w.u8(record.perfect);
		w.u32(record.fingerprint), w.u32(record.code), w.u32(record.quality), w.u32(static_cast<std::uint32_t>(record.qualityRecordId)), w.u32(record.itemLevel), w.u32(record.stateFlags);
		w.u32(static_cast<std::uint32_t>(record.bytes.size()));
		w.out.append(reinterpret_cast<const char*>(record.bytes.data()), record.bytes.size());
	}
	w.u32(static_cast<std::uint32_t>(Tombstones.size()));
	for (const auto fingerprint : Tombstones) w.u32(fingerprint);
	w.u32(static_cast<std::uint32_t>(FoundIds.size() + CheckedIds.size()));
	for (const auto& id : FoundIds) w.text(id);
	for (const auto& id : CheckedIds) w.text("c|" + id);
	const std::wstring temporary = StorePath + L".tmp";
	FILE*              file      = _wfopen(temporary.c_str(), L"wb");
	if (!file || std::fwrite(w.out.data(), 1, w.out.size(), file) != w.out.size()) {
		if (file) std::fclose(file);
		Say(true, "grailon: could not write the grail storage file");
		return;
	}
	std::fflush(file);
	std::fclose(file);
	if (!MoveFileExW(temporary.c_str(), StorePath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) Say(true, "grailon: could not replace the grail storage file (%lu)", GetLastError());
}

// Lock held. Reads the storage file at `path` (none yet: an empty grail).
std::uint32_t FingerprintInBytes(const std::vector<std::uint8_t>& bytes, std::uint32_t itemLevel, std::uint32_t quality);
bool          LoadStore(const std::wstring& path) {
	Records.clear(), Tombstones.clear(), FoundIds.clear(), CheckedIds.clear(), Materialized.clear(), Fingerprints.clear();
	StorePath = path;
	FILE* file = _wfopen(path.c_str(), L"rb");
	if (!file && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
		// There but not readable now: never saved over.
		StorePath.clear();
		Problem(true, "grailon: the grail storage file could not be read (%lu); stored items are not shown this time", GetLastError());
		return false;
	}
	if (!file) return true;
	std::string data;
	char        chunk[65536];
	for (std::size_t n; (n = std::fread(chunk, 1, sizeof(chunk), file)) > 0;) data.append(chunk, n);
	std::fclose(file);
	Reader r { data };
	const std::uint32_t version = data.size() >= 8 && !data.compare(0, 4, "GRL1") ? (r.at = 4, r.get(4)) : 0;
	if (version != 1 && version != 2) {
		Problem(true, "grailon: the grail storage file is not one this version reads; it is left as it is and not used");
		StorePath.clear();
		return false;
	}
	for (std::uint32_t n = r.get(4); r.ok && n--;) {
		Record record;
		record.id              = r.text();
		if (record.id.rfind("b|", 0) == 0) record.id.erase(0, 2), record.base = true;
		record.eth             = r.get(1) != 0;
		record.perfect         = r.get(1) != 0;
		record.fingerprint     = r.get(4);
		record.code            = r.get(4);
		record.quality         = r.get(4);
		record.qualityRecordId = static_cast<std::int32_t>(r.get(4));
		record.itemLevel       = r.get(4);
		record.stateFlags      = r.get(4);
		const auto size        = r.get(4);
		if (!r.ok || r.at + size > data.size()) break;
		record.bytes.assign(data.begin() + static_cast<std::ptrdiff_t>(r.at), data.begin() + static_cast<std::ptrdiff_t>(r.at + size));
		r.at += size;
		// Version 1 kept a value that changes on each load: the real one is in the bytes.
		if (version == 1) record.fingerprint = FingerprintInBytes(record.bytes, record.itemLevel, record.quality);
		Records[record.fingerprint] = std::move(record);
	}
	for (std::uint32_t n = r.get(4); r.ok && n--;)
		if (const auto fingerprint = r.get(4); version > 1) Tombstones.insert(fingerprint);
	CheckedIds.clear();
	for (std::uint32_t n = r.get(4); r.ok && n--;)
		if (auto id = r.text(); id.rfind("c|", 0) == 0) CheckedIds.insert(id.substr(2)); // a ticked checkbox entry
		else FoundIds.insert(std::move(id));
	if (!r.ok) {
		// Kept aside before the next save writes over it.
		(void)CopyFileW(path.c_str(), (path + L".damaged").c_str(), FALSE);
		Say(true, "grailon: the grail storage file ends early; %zu items read (copy kept as .damaged)", Records.size());
	}
	if (version == 1) {
		(void)CopyFileW(path.c_str(), (path + L".v1").c_str(), FALSE);
		SaveStore();
	}
	return true;
}

// ---- the character's mode ----
// The client player's name (PlayerData at unit+0x10 starts with it), then the
// newest <name>.d2s in the save folder or a mod's: byte 0x24 bit 0x04 is
// hardcore.
bool Hardcore() {
	const auto base = static_cast<std::uintptr_t>(Context->exeBase);
	void*      unit = reinterpret_cast<PlayerUnitFn>(base + Sound::PlayerUnit)(reinterpret_cast<LocalIndexFn>(base + Sound::LocalIndex)());
	char       name[17] {};
	SIZE_T     copied = 0;
	void*      data   = nullptr;
	if (!unit || !ReadProcessMemory(GetCurrentProcess(), static_cast<const char*>(unit) + 0x10, &data, sizeof(data), &copied) || !data
	    || !ReadProcessMemory(GetCurrentProcess(), data, name, 16, &copied))
		return false;
	for (const char c : std::string(name))
		if (static_cast<unsigned char>(c) < 32) return false;
	PWSTR folder = nullptr;
	if (!*name || SHGetKnownFolderPath(FOLDERID_SavedGames, 0, nullptr, &folder) != S_OK) return false;
	const std::wstring saves = std::wstring(folder) + L"\\Diablo II Resurrected";
	CoTaskMemFree(folder);
	// The game keeps the name in UTF-8 (Chinese, Korean, Cyrillic... names).
	std::wstring wide(32, L'\0');
	const int    length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, wide.data(), static_cast<int>(wide.size()));
	if (length > 0) wide.resize(static_cast<std::size_t>(length) - 1);
	else {
		wide.clear();
		for (const char* c = name; *c; ++c) wide += static_cast<wchar_t>(static_cast<unsigned char>(*c));
	}
	// The active mod's save first: that is the character playing.
	if (const std::wstring own = SaveFolder() + L"\\" + wide + L".d2s"; GetFileAttributesW(own.c_str()) != INVALID_FILE_ATTRIBUTES) {
		FILE*         file = _wfopen(own.c_str(), L"rb");
		unsigned char header[0x25] {};
		const bool    read = file && std::fread(header, 1, sizeof(header), file) == sizeof(header);
		if (file) std::fclose(file);
		if (read) return (header[0x24] & 0x04) != 0;
	}
	std::vector<std::wstring> candidates { saves + L"\\" + wide + L".d2s" };
	WIN32_FIND_DATAW found {};
	if (HANDLE find = FindFirstFileW((saves + L"\\mods\\*").c_str(), &found); find != INVALID_HANDLE_VALUE) {
		do
			if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && found.cFileName[0] != L'.') candidates.push_back(saves + L"\\mods\\" + found.cFileName + L"\\" + wide + L".d2s");
		while (FindNextFileW(find, &found));
		FindClose(find);
	}
	std::wstring newest;
	FILETIME     latest {};
	for (const auto& path : candidates) {
		WIN32_FILE_ATTRIBUTE_DATA attributes {};
		if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes)) continue;
		if (newest.empty() || CompareFileTime(&attributes.ftLastWriteTime, &latest) > 0) newest = path, latest = attributes.ftLastWriteTime;
	}
	FILE* file = newest.empty() ? nullptr : _wfopen(newest.c_str(), L"rb");
	if (!file) {
		Say(true, "grailon: %s's save file was not found; the softcore grail is used", name);
		return false;
	}
	unsigned char header[0x25] {};
	const bool    read = std::fread(header, 1, sizeof(header), file) == sizeof(header);
	std::fclose(file);
	return read && (header[0x24] & 0x04) != 0;
}

// UI thread: opens the storage of the character that just came in.
void OpenStoreFor(D2RL::PlayerHandle player) {
	if (!StoreReady) return;
	const bool         hardcore = Hardcore();
	const std::wstring folder   = StoreFolder();
	std::lock_guard    guard(Lock);
	StoreOwner = player;
	if (folder.empty()) {
		StorePath.clear();
		Problem(true, "grailon: no Saved Games folder: the grail cannot store items");
		return;
	}
	(void)LoadStore(StoreFile(folder, hardcore ? L"grail-hardcore.dat" : L"grail-softcore.dat"));
	RebuildStored(); // the tiles from the records now (a joined game has no arrange that would)
	Say(false, "grailon: %s grail: %zu items", hardcore ? "hardcore" : "softcore", Records.size());
}

// ---- items <-> records (game thread) ----

struct Snapshot {
	std::vector<std::uint8_t> bytes;
	std::uint32_t             fingerprint {};
	std::uint32_t             itemLevel {}, quality {}; // to find the fingerprint in the bytes
	bool                      ok {};
	std::uint32_t             code {}; // its base: a simple item's (gem, rune...) identity
};

// The item's fingerprint as its save bytes hold it (what stays the same
// across saves and loads; ItemData+0x10 is NOT it, it changes on each load).
// The extended part starts with it, then the item level (7 bits) and the
// quality (4 bits): the fingerprint is the 32 bits before the first such
// level+quality after the compact part (53 bits + the code + 3 socket bits,
// so from bit 64 on). Read from the bytes alone, so the same item always
// gives the same value. 0 when none.
std::uint32_t FingerprintInBytes(const std::vector<std::uint8_t>& bytes, std::uint32_t itemLevel, std::uint32_t quality) {
	const auto bit = [&](std::size_t i) { return (bytes[i >> 3] >> (i & 7)) & 1u; };
	const auto get = [&](std::size_t at, int n) {
		std::uint32_t v = 0;
		for (int k = 0; k < n; ++k) v |= bit(at + k) << k;
		return v;
	};
	const std::size_t total = bytes.size() * 8;
	for (std::size_t at = 64 + 32; at + 11 <= total && at < 64 + 32 + 160; ++at) {
		if (get(at, 7) != (itemLevel & 0x7F) || get(at + 7, 4) != (quality & 0xF)) continue;
		return get(at - 32, 32);
	}
	return 0;
}

// The picture a ring or amulet was given (the game picks one of its base's
// for each item): saved right after the item level and quality, a bit "has
// one" then 3 bits. -1 when none.
int PictureInBytes(const std::vector<std::uint8_t>& bytes, std::uint32_t itemLevel, std::uint32_t quality) {
	const auto bit = [&](std::size_t i) { return (bytes[i >> 3] >> (i & 7)) & 1u; };
	const auto get = [&](std::size_t at, int n) {
		std::uint32_t v = 0;
		for (int k = 0; k < n; ++k) v |= bit(at + k) << k;
		return v;
	};
	const std::size_t total = bytes.size() * 8;
	for (std::size_t at = 64 + 32; at + 15 <= total && at < 64 + 32 + 160; ++at) {
		if (get(at, 7) != (itemLevel & 0x7F) || get(at + 7, 4) != (quality & 0xF)) continue;
		return get(at + 11, 1) ? static_cast<int>(get(at + 12, 3)) : -1;
	}
	return -1;
}

Record* RecordAt(int u, bool eth);
// Lock held. A stored ring's or amulet's tile: the picture its item has
// (ring1..5, amulet1..3 next to the base's own), not the base's.
std::string StoredArt(int u, bool eth, const std::string& art) {
	const auto& entry = Uniques[u];
	if (entry.code != "rin" && entry.code != "amu") return art;
	const Record* record = RecordAt(u, eth);
	if (!record) return art;
	const int picture = PictureInBytes(record->bytes, record->itemLevel, record->quality);
	const int count   = entry.code == "rin" ? 5 : 3;
	if (picture < 0 || picture >= count || entry.ghost.empty()) return art;
	return entry.ghost + std::to_string(picture + 1);
}

// A simple item's stand-in fingerprint, from its base's code.
std::uint32_t SimplePrint(std::uint32_t code) {
	std::uint32_t hash = 2166136261u;
	for (int k = 0; k < 4; ++k) hash = (hash ^ ((code >> (8 * k)) & 0xFF)) * 16777619u;
	return (hash ^ 0x5A17C0DEu) | 1u;
}

void __cdecl TakeSnapshot(const D2RL::PluginContext*, void* native, void* userData) noexcept {
	auto* snapshot = static_cast<Snapshot*>(userData);
	if (!native) return;
	snapshot->bytes.assign(Store::BufferSize, 0);
	void*     source = reinterpret_cast<void*>(static_cast<std::uintptr_t>(Context->exeBase) + Store::SourceInfo);
	const int size   = SaveItem(native, snapshot->bytes.data(), snapshot->bytes.size(), 1, 1, 0, &source);
	snapshot->bytes.resize(size > 0 ? static_cast<std::size_t>(size) : 0);
	snapshot->ok = size > 0;
	if (snapshot->ok) snapshot->fingerprint = FingerprintInBytes(snapshot->bytes, snapshot->itemLevel, snapshot->quality);
	// A simple item (gems, runes, skulls...) has no fingerprint: every copy of
	// a base is the same, so its base stands for it.
	if (snapshot->ok && !snapshot->fingerprint && snapshot->bytes.size() <= 24 && snapshot->code) snapshot->fingerprint = SimplePrint(snapshot->code);
}

// The fingerprint of an item (read once per item).
std::uint32_t FingerprintOf(const Items::ItemInfo& item) {
	{
		std::lock_guard guard(Lock);
		if (const auto known = Fingerprints.find(item.runtimeId); known != Fingerprints.end()) return known->second;
	}
	Snapshot snapshot { .itemLevel = item.itemLevel, .quality = static_cast<std::uint32_t>(item.quality) };
	snapshot.code = item.code;
	if (ItemApi->editNativeItem(Context, item.handle, TakeSnapshot, &snapshot) != Items::Result::Success || !snapshot.ok) return 0;
	std::lock_guard guard(Lock);
	if (!snapshot.fingerprint && MiscByCode.count(CodeText(item.code))) snapshot.fingerprint = SimplePrint(item.code); // a misc base: its base is its identity
	Fingerprints[item.runtimeId] = snapshot.fingerprint;
	return snapshot.fingerprint;
}

// A runeword's Runes row in an item's save bytes, -1 for none: after the
// item level and quality, the picture (1 bit, + 3 when set), class (1 bit,
// + 16 on RotW, a Paladin shield's Ancients' Pledge and Rhyme; 11 before), the quality's own bits (3 for inferior and superior; normal has
// none), then 12 bits when the runeword flag (bit 26 of the first 32) is set:
// the Runes row + 27 (Grief, row 46, is 73; checked on Grief and Rift), with
// 2718 for row 48.
// Every place the level and quality could be gives a candidate (the
// fingerprint's bits can look like them too); the caller keeps the one that
// fits the item.
// Each candidate: the Runes row the 12 bits give, and the 16 bits with the 4
// after them, which are the runeword's name string id (Grief 73 | 5 << 12;
// the 2.4 and later runewords only have that, with 6 << 12).
std::vector<std::pair<int, std::uint32_t>> RunewordsInBytes(const std::vector<std::uint8_t>& bytes, std::uint32_t itemLevel, std::uint32_t quality) {
	std::vector<std::pair<int, std::uint32_t>> rows;
	const auto bit = [&](std::size_t i) { return (bytes[i >> 3] >> (i & 7)) & 1u; };
	const auto get = [&](std::size_t at, int n) {
		std::uint32_t v = 0;
		for (int k = 0; k < n; ++k) v |= bit(at + k) << k;
		return v;
	};
	const std::size_t total = bytes.size() * 8;
	if (total < 32 || !get(26, 1)) return rows;
	for (std::size_t at = 64 + 32; at + 11 <= total && at < 64 + 32 + 160; ++at) {
		if (get(at, 7) != (itemLevel & 0x7F) || get(at + 7, 4) != (quality & 0xF)) continue;
		std::size_t pos = at + 11;
		if (pos + 2 > total) break;
		pos += get(pos, 1) ? 4 : 1;
		// Class-specific items: 16 bits of class data (11 before), both tried.
		const bool classed = get(pos, 1) != 0;
		for (const int width : { 16, 11 }) {
			std::size_t at2 = pos + (classed ? 1 + width : 1);
			if (quality == 1 || quality == 3) at2 += 3;
			if (at2 + 16 > total) continue;
			const int id = static_cast<int>(get(at2, 12));
			rows.push_back({ id == 2718 ? 48 : id >= 27 ? id - 27 : -1, get(at2, 16) });
			if (!classed) break;
		}
	}
	return rows;
}

// Lock held. A normal or superior item's Runes row (-1 for none, -2 while
// not known yet), once per item. Its bytes can only be read from a game-thread
// job: from anywhere else (hovering it, the page's checks) a job reads them,
// and the item is known from then on (hovered before it is dropped).
std::map<std::pair<std::uint32_t, std::uint32_t>, int> RunewordRows; // (runtime id, code) -> row, -3 while a job reads it
int RunewordFromSnapshot(const Items::ItemInfo& item, const Snapshot& snapshot) {
	const auto rows = RunewordsInBytes(snapshot.bytes, item.itemLevel, static_cast<std::uint32_t>(item.quality));
	int        row  = rows.empty() ? -1 : rows.front().first;
	bool       found = false;
	for (const auto& [r, nameId] : rows)
		if (const int u = RunewordFor(nameId, CodeText(item.code)); u >= 0 && RunewordEntry(Uniques[u].row, CodeText(item.code)) >= 0) {
			row   = Uniques[u].row;
			found = true;
			break;
		}
	for (const auto& [r, nameId] : rows)
		if (!found && r >= 0 && RunewordEntry(r, CodeText(item.code)) >= 0) {
			row   = r;
			found = true;
		}
	static int told = 0;
	if (!rows.empty() && told++ < 40) {
		std::string all;
		for (const auto& [r, nameId] : rows) all += " " + std::to_string(r) + "/" + std::to_string(nameId);
		std::string bits;
		for (std::size_t i = 0; i < snapshot.bytes.size() && i < 40; ++i) {
			char hex[4];
			std::snprintf(hex, sizeof(hex), "%02X", snapshot.bytes[i]);
			bits += hex;
		}
		Say(false, "grailon: %s (level %u, quality %u): runeword row %d (candidates%s) bytes %s", CodeText(item.code).c_str(), item.itemLevel, static_cast<unsigned>(item.quality), row,
		    all.c_str(), bits.c_str());
	}
	return RunewordRows[{ item.runtimeId, item.code }] = row;
}
void __cdecl ReadRunewordRow(const D2RL::PluginContext*, void* userData) noexcept {
	std::unique_ptr<Items::ItemInfo> item(static_cast<Items::ItemInfo*>(userData));
	Snapshot snapshot { .itemLevel = item->itemLevel, .quality = static_cast<std::uint32_t>(item->quality) };
	snapshot.code = item->code;
	auto     result = ItemApi->editNativeItem(Context, item->handle, TakeSnapshot, &snapshot);
	if (result == Items::Result::StaleHandle) {
		// The hovered item's handle is not good here: the item again by its id.
		std::vector<Items::ItemInfo>      items;
		const D2RL::Inventory::ItemFilter filter {
			.structSize    = D2RL::Inventory::ItemFilterSize,
			.containerMask = Items::ContainerBit(Items::ItemContainer::CustomPage) | Items::ContainerBit(Items::ItemContainer::PersonalStash) | Items::ContainerBit(Items::ItemContainer::Inventory)
			               | Items::ContainerBit(Items::ItemContainer::Cube) | Items::ContainerBit(Items::ItemContainer::SharedStash) | Items::ContainerBit(Items::ItemContainer::Cursor)
			               | Items::ContainerBit(Items::ItemContainer::Equipment),
		};
		if (const D2RL::PlayerHandle player = LocalPlayer; player) (void)Inventory->forEachInventoryItem(Context, player, &filter, Collect, &items);
		for (const auto& found : items)
			if (found.runtimeId == item->runtimeId) result = ItemApi->editNativeItem(Context, found.handle, TakeSnapshot, &snapshot);
	}
	std::lock_guard guard(Lock);
	if (result != Items::Result::Success || !snapshot.ok) {
		RunewordRows.erase({ item->runtimeId, item->code });
		static int failed = 0;
		if (failed++ < 5) Say(false, "grailon: %s: its bytes could not be read (result %u)", CodeText(item->code).c_str(), static_cast<unsigned>(result));
		return;
	}
	RunewordFromSnapshot(*item, snapshot);
}
void RemoteRunewordRow(const Items::ItemInfo& item);
int RunewordRowOf(const Items::ItemInfo& item) {
	const auto key = std::make_pair(item.runtimeId, item.code);
	if (const auto known = RunewordRows.find(key); known != RunewordRows.end()) return known->second == -3 ? -2 : known->second;
	if (!ItemApi) return -2;
	Snapshot snapshot { .itemLevel = item.itemLevel, .quality = static_cast<std::uint32_t>(item.quality) };
	snapshot.code = item.code;
	if (ItemApi->editNativeItem(Context, item.handle, TakeSnapshot, &snapshot) == Items::Result::Success && snapshot.ok) return RunewordFromSnapshot(item, snapshot);
	// Not from here: a game-thread job reads it (joined: the hosting game's bytes).
	RunewordRows[key] = -3;
	if (Remote()) {
		RemoteRunewordRow(item);
		return -2;
	}
	auto* copy = new Items::ItemInfo(item);
	if (Threads->runOnGameThread(Context, ReadRunewordRow, copy) != D2RL::Threads::Result::Success) RunewordRows.erase(key), delete copy;
	return -2;
}

// Lock held. A new record for page item `item` of entry `u`.
bool RecordItem(const Items::ItemInfo& item, int u, bool eth, bool perfect) {
	if (Uniques[u].misc) perfect = true; // nothing on a misc item varies: every copy is perfect
	Snapshot snapshot { .itemLevel = item.itemLevel, .quality = static_cast<std::uint32_t>(item.quality) };
	snapshot.code = item.code;
	const auto read = ItemApi->editNativeItem(Context, item.handle, TakeSnapshot, &snapshot);
	// A misc base (gems, runes...): every copy is the same, its base is its identity.
	if (read == Items::Result::Success && snapshot.ok && !snapshot.fingerprint && Uniques[u].misc) snapshot.fingerprint = SimplePrint(item.code);
	if (read != Items::Result::Success || !snapshot.ok || !snapshot.fingerprint) {
		static int told = 0;
		if (told++ < 8)
			Say(true, "grailon: %s could not be stored: read %u, %zu bytes, fingerprint %08X", CodeText(item.code).c_str(), static_cast<unsigned>(read), snapshot.bytes.size(), snapshot.fingerprint);
		return false; // without its identity it could come back twice
	}
	const auto base = BaseFor.find(item.runtimeId);
	Record     record { .id = Uniques[u].id, .eth = eth, .perfect = perfect, .base = base != BaseFor.end() && base->second == u, .fingerprint = snapshot.fingerprint, .code = item.code,
		            .quality = static_cast<std::uint32_t>(item.quality), .itemLevel = item.itemLevel, .stateFlags = item.stateFlags, .qualityRecordId = item.qualityRecordId,
		            .bytes = std::move(snapshot.bytes) };
	Fingerprints[item.runtimeId] = record.fingerprint;
	Tombstones.erase(record.fingerprint);
	if (record.base) record.perfect = false;
	else FoundIds.insert(record.id); // a base finds nothing
	Records[record.fingerprint] = std::move(record);
	return true;
}

// The decoder D2RCore calls instead of the game's CreateItem for one createItem.
struct Decoding {
	const std::vector<std::uint8_t>* bytes {};
	void*                            item {};
	bool                             called {};
	std::uint32_t                    playerId = 0xFFFFFFFF;
} Decode;

// Socketed items follow the parent in the bytes, as in a .d2s: they go into it
// before D2RCore places it, so the client gets the whole item at once.
void SocketChildren(void* game, std::size_t offset) {
	const auto& bytes  = *Decode.bytes;
	void*       player = Decode.playerId != 0xFFFFFFFF ? UnitById(game, 0, Decode.playerId) : nullptr;
	const auto  parent = *reinterpret_cast<std::uint32_t*>(static_cast<char*>(Decode.item) + 8);
	while (player && offset < bytes.size()) {
		alignas(16) std::uint8_t header[0x100] {};
		const auto               flag = *reinterpret_cast<std::uint8_t*>(static_cast<char*>(game) + Store::GameFlag);
		(void)ReadHeader(flag, bytes.data() + offset, bytes.size() - offset, 1, header, Store::Version);
		std::int32_t first = 0;
		std::memcpy(&first, header, 4);
		int   read  = 0;
		void* child = first == -1 ? nullptr : LoadItem(game, nullptr, bytes.data() + offset, static_cast<int>(bytes.size() - offset), 1, header, &read, Store::Version);
		if (!child) break;
		if (!SocketItem(game, player, *reinterpret_cast<std::uint32_t*>(static_cast<char*>(child) + 8), parent, 1)) FreeItem(game, child);
		if (read <= 0) break;
		offset += static_cast<std::size_t>(read);
	}
}

std::atomic<void*> ServerGame {}; // the game createItem last ran in (this character's)

void* DecodeInstead(void* game, void*, int) noexcept {
	Decode.called = true;
	if (game) ServerGame = game;
	if (!game || !Decode.bytes || Decode.bytes->empty()) return nullptr;
	alignas(16) std::uint8_t header[0x100] {};
	const auto               flag = *reinterpret_cast<std::uint8_t*>(static_cast<char*>(game) + Store::GameFlag);
	(void)ReadHeader(flag, Decode.bytes->data(), Decode.bytes->size(), 1, header, Store::Version);
	std::int32_t first = 0;
	std::memcpy(&first, header, 4);
	if (first == -1) return nullptr;
	int read    = 0;
	Decode.item = LoadItem(game, nullptr, Decode.bytes->data(), static_cast<int>(Decode.bytes->size()), 1, header, &read, Store::Version);
	if (Decode.item && read > 0) SocketChildren(game, static_cast<std::size_t>(read));
	return Decode.item;
}

// Game thread: the record's item, made again where `destination` says.
Items::ItemCreateSpec RecordSpec(const Record& record, const Items::ItemDestination& destination);
Items::Result Materialize(D2RL::PlayerHandle player, const Record& record, const Items::ItemDestination& destination, D2RL::ItemHandle* made) {
	auto* slot = reinterpret_cast<void**>(CoreModule + Store::CoreSlot);
	DWORD old  = 0;
	if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return Items::Result::Unavailable;
	const auto base = static_cast<std::uintptr_t>(Context->exeBase);
	void*      unit = reinterpret_cast<PlayerUnitFn>(base + Sound::PlayerUnit)(reinterpret_cast<LocalIndexFn>(base + Sound::LocalIndex)());
	Decode          = { .bytes = &record.bytes, .playerId = unit ? *reinterpret_cast<std::uint32_t*>(static_cast<char*>(unit) + 8) : 0xFFFFFFFF };
	Items::ItemCreateSpec spec = RecordSpec(record, destination);
	void* const original = *slot;
	*slot                = reinterpret_cast<void*>(&DecodeInstead);
	auto result          = CreateItem(player, spec, made);
	if (result == Items::Result::InvalidArgument && !Decode.called) {
		// Only D2RLoader's checks see the spec (the decoded item replaces it):
		// a plain item of the same base passes them.
		spec.flags           = 0;
		spec.quality         = Items::Quality::Normal;
		spec.qualityRecordId = Items::RandomQualityRecord;
		result               = CreateItem(player, spec, made);
	}
	*slot = original;
	VirtualProtect(slot, sizeof(void*), old, &old);
	Decode = {};
	return result;
}

// The creation spec of a record's item (what D2RLoader checks; the decoded
// item replaces it).
Items::ItemCreateSpec RecordSpec(const Record& record, const Items::ItemDestination& destination) {
	Items::ItemCreateSpec spec {};
	spec.structSize      = Items::ItemCreateSpecSize;
	spec.code            = record.code;
	spec.quality         = static_cast<Items::Quality>(record.quality);
	spec.qualityRecordId = (spec.quality == Items::Quality::Unique || spec.quality == Items::Quality::Set) && record.qualityRecordId >= 0 ? static_cast<std::uint32_t>(record.qualityRecordId)
	                                                                                                                                     : Items::RandomQualityRecord;
	if (spec.quality == Items::Quality::Unique) spec.flags = Items::ItemCreateFlagBit(Items::ItemCreateFlag::AllowDuplicateUnique);
	spec.itemLevel   = record.itemLevel;
	spec.seedMode    = Items::SeedMode::Random;
	spec.quantity    = Items::DefaultValue;
	spec.durability  = Items::DefaultValue;
	spec.stateFlags  = record.stateFlags & Items::AllItemStates;
	spec.destination = destination;
	return spec;
}

// ---- copies for the comparison (game thread) ----
// A stored item whose tile is not on screen has no item in the game: its
// comparison uses a "ghost", the record decoded by the game's save loader into
// an item that is in no inventory and never reaches the client. Ghosts are
// freed when the view closes, and forgotten with the game.
bool DestroyItem(D2RL::PlayerHandle player, D2RL::ItemHandle item);
struct Ghost {
	void* unit {};
	void* game {};
};
std::map<std::uint32_t, Ghost> Ghosts;      // by fingerprint; guarded by Lock
std::set<std::uint32_t>        GhostWanted; // guarded by Lock
std::atomic<bool>              GhostQueued {}, GhostsFreeing {};

// The server game, learnt from a createItem whose decode makes nothing.
void* LearnGame(D2RL::PlayerHandle player, std::uint32_t code) {
	if (const auto game = ServerGame.load()) return game;
	static const std::vector<std::uint8_t> none;
	Record probe;
	probe.code    = code;
	probe.quality = static_cast<std::uint32_t>(Items::Quality::Normal);
	auto* slot    = reinterpret_cast<void**>(CoreModule + Store::CoreSlot);
	DWORD old     = 0;
	if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return nullptr;
	Decode = { .bytes = &none };
	Items::ItemCreateSpec spec {};
	spec.structSize      = Items::ItemCreateSpecSize;
	spec.code            = code;
	spec.quality         = Items::Quality::Normal;
	spec.qualityRecordId = Items::RandomQualityRecord;
	spec.itemLevel       = 1;
	spec.seedMode        = Items::SeedMode::Random;
	spec.quantity = spec.durability = Items::DefaultValue;
	spec.destination                = { .structSize = Items::ItemDestinationRequiredSize, .container = Items::ItemContainer::Inventory, .placement = Items::Placement::Automatic };
	void* const original            = *slot;
	*slot                           = reinterpret_cast<void*>(&DecodeInstead); // returns nothing: no item is made
	D2RL::ItemHandle made {};
	const auto       result = ItemApi->createItem(Context, player, &spec, &made);
	*slot                   = original;
	VirtualProtect(slot, sizeof(void*), old, &old);
	Decode = {};
	if (result == Items::Result::Success && made) (void)DestroyItem(player, made); // should not happen
	return ServerGame.load();
}

void __cdecl MakeGhosts(const D2RL::PluginContext*, void*) noexcept {
	const auto                                                   player = LocalPlayer.load();
	std::vector<std::pair<std::uint32_t, std::vector<std::uint8_t>>> todo;
	std::uint32_t                                                code = 0;
	{
		std::lock_guard guard(Lock);
		for (const auto fingerprint : GhostWanted)
			if (const auto record = Records.find(fingerprint); record != Records.end() && !Ghosts.count(fingerprint)) todo.push_back({ fingerprint, record->second.bytes }), code = record->second.code;
		GhostWanted.clear();
	}
	void* game = player && !todo.empty() ? LearnGame(player, code) : nullptr;
	static bool told = false;
	if (!game && !todo.empty() && !told) told = true, Say(true, "grailon: the game was not found; stored items whose tile is not shown cannot be compared");
	for (const auto& [fingerprint, bytes] : todo) {
		void* unit = nullptr;
		if (game) {
			alignas(16) std::uint8_t header[0x100] {};
			const auto               flag = *reinterpret_cast<std::uint8_t*>(static_cast<char*>(game) + Store::GameFlag);
			(void)ReadHeader(flag, bytes.data(), bytes.size(), 1, header, Store::Version);
			std::int32_t first = 0;
			std::memcpy(&first, header, 4);
			int read = 0;
			if (first != -1) unit = LoadItem(game, nullptr, bytes.data(), static_cast<int>(bytes.size()), 1, header, &read, Store::Version);
		}
		std::lock_guard guard(Lock);
		Ghosts[fingerprint] = { unit, game }; // kept even when empty: not tried again
	}
	GhostQueued = false;
}

void __cdecl FreeGhosts(const D2RL::PluginContext*, void*) noexcept {
	std::map<std::uint32_t, Ghost> gone;
	{
		std::lock_guard guard(Lock);
		gone.swap(Ghosts);
	}
	for (const auto& [fingerprint, ghost] : gone)
		if (ghost.unit && ghost.game && ghost.game == ServerGame.load()) FreeItem(ghost.game, ghost.unit);
	GhostsFreeing = false;
}

// ---- an empty page outside the view (game thread) ----
// The grail file holds every stored item; the page only holds copies of the
// tiles on screen. Once the view closes (and when a character loads) the page
// is emptied, so the character's save never holds page items: the game loads
// it without the plugin too, and the items come back from the file when the
// view opens again. Grail items nobody recorded are recorded first; anything
// else goes to the inventory.
Record* RecordAt(int u, bool eth);
std::atomic<bool> PageClearing {};
std::atomic<D2RL::PlayerHandle> ClearedFor {};
void __cdecl ClearPage(const D2RL::PluginContext* context, void*) noexcept {
	const auto player = LocalPlayer.load();
	bool       ready  = false;
	{
		std::lock_guard guard(Lock);
		ready = player && StoreReady && !StorePath.empty() && StoreOwner == player;
	}
	if (!ready || ViewIsOpen || PreparedView >= 0) {
		PageClearing = false;
		return;
	}
	ClearedFor = player;
	std::vector<Items::ItemInfo>      items;
	const D2RL::Inventory::ItemFilter filter { .structSize = D2RL::Inventory::ItemFilterSize, .containerMask = Items::ContainerBit(Items::ItemContainer::CustomPage) };
	(void)Inventory->forEachInventoryItem(context, player, &filter, Collect, &items);
	bool dirty = false;
	int  kept  = 0;
	for (const auto& item : items) {
		std::lock_guard guard(Lock);
		if (Busy.count(item.runtimeId)) {
			++kept;
			continue;
		}
		const auto print = FingerprintOf(item);
		bool       copy  = print && (Records.count(print) || Tombstones.count(print)); // the file has it (or it left the grail elsewhere)
		if (!copy) {
			const int u = GrailIndex(item);
			if (u >= 0 && HasTile(u, IsEth(item)) && !RecordAt(u, IsEth(item)) && RecordItem(item, u, IsEth(item), false)) copy = dirty = true;
		}
		if (copy) {
			Materialized.erase(item.runtimeId);
			if (DestroyItem(player, item.handle)) continue;
		} else if (MoveTo(player, item.handle, Items::ItemContainer::Inventory) == Items::Result::Success) continue;
		++kept;
	}
	{
		std::lock_guard guard(Lock);
		RebuildStored();
		if (dirty) SaveStore();
	}
	if (kept) Say(false, "grailon: %d item(s) stay on the grail page (no room in the inventory?)", kept);
	PageClearing = false;
}

// Lock held (UI thread). The ghost of a record, asked for when missing.
void* GhostOf(std::uint32_t fingerprint) {
	if (const auto ghost = Ghosts.find(fingerprint); ghost != Ghosts.end()) return ghost->second.unit;
	if (GhostsFreeing || Remote()) return nullptr; // joined: the items live in the hosting game, no copies here
	GhostWanted.insert(fingerprint);
	if (!GhostQueued.exchange(true) && Threads->runOnGameThread(Context, MakeGhosts, nullptr) != D2RL::Threads::Result::Success) GhostQueued = false;
	return nullptr;
}

Items::ItemDestination CellDestination(int x, int y) {
	return { .structSize = Items::ItemDestinationRequiredSize, .container = Items::ItemContainer::CustomPage, .placement = Items::Placement::Exact, .customPageHandle = Page,
		     .x = static_cast<std::uint32_t>(x), .y = static_cast<std::uint32_t>(y) };
}

bool DestroyItem(D2RL::PlayerHandle player, D2RL::ItemHandle item) {
	PluginMoving = true;
	// An item in the host's area: the host destroys it (by runtime id).
	Items::ItemInfo info { .structSize = Items::ItemInfoSize };
	if (Host::On() && GetInfo(Context, item, &info) == Items::Result::Success && info.container == Items::ItemContainer::CustomPage) {
		std::uint32_t itemResult = 0;
		const bool    done       = Host::Service->destroyItem(Host::Area, info.runtimeId, Items::SocketedItemPolicy::DestroyContents, &itemResult) == PageHost::Result::Success
		                  && static_cast<Items::Result>(itemResult) == Items::Result::Success;
		PluginMoving = false;
		return done;
	}
	const bool done = ItemApi->destroyItem(Context, player, item, Items::SocketedItemPolicy::DestroyContents) == Items::Result::Success;
	PluginMoving    = false;
	return done;
}

// Lock held. The record of a tile, or null.
Record* RecordAt(int u, bool eth) {
	for (auto& [fingerprint, record] : Records)
		if (record.eth == eth && EntryById.count(record.id) && EntryById.at(record.id) == u) return &record;
	return nullptr;
}

// Lock held. An item left the grail: its record goes, its fingerprint stays.
void TakeOut(std::uint32_t fingerprint) {
	for (const auto& [runtime, print] : Materialized)
		if (print == fingerprint) BaseFor.erase(runtime);
	// A simple item's stand-in is every copy's: no tombstone, or the next copy put in would go.
	const auto record = Records.find(fingerprint);
	const bool simple = record != Records.end() && record->second.id.rfind("m:", 0) == 0 && fingerprint == SimplePrint(record->second.code);
	Records.erase(fingerprint);
	if (!simple) Tombstones.insert(fingerprint);
	std::erase_if(Materialized, [&](const auto& item) { return item.second == fingerprint; });
}

// Lock held. What the views show from the records: each tile's item (its
// runtime id when on the page, else 0), the entries found, the perfect ones.
std::uint64_t GrailChanges {}; // counts the grail's rebuilds, for what is kept about its copies
void RebuildStored() {
	++GrailChanges;
	Stored.clear();
	StoredCodes.clear();
	Perfect.clear();
	BaseTiles.clear();
	Present.clear();
	for (const auto& [fingerprint, record] : Records) {
		const auto entry = EntryById.find(record.id);
		if (entry == EntryById.end()) continue;
		std::uint32_t id = 0;
		for (const auto& [runtime, print] : Materialized)
			if (print == fingerprint) id = runtime;
		Stored[{ entry->second, record.eth }]      = id;
		StoredCodes[{ entry->second, record.eth }] = CodeText(record.code);
		if (record.base) BaseTiles.insert({ entry->second, record.eth });
		else if (record.perfect || Uniques[entry->second].secret) Perfect.insert({ entry->second, record.eth });
	}
	for (const auto& id : FoundIds)
		if (const auto entry = EntryById.find(id); entry != EntryById.end()) Present.insert(entry->second);
	// Ticked checkbox entries (their type in checkbox mode): shown and counted as complete.
	for (const auto& id : CheckedIds)
		if (const auto entry = EntryById.find(id); entry != EntryById.end() && IsCheckbox(Uniques[entry->second])
		    && !Stored.count({ entry->second, false })) {
			Stored[{ entry->second, false }] = 0;
			Perfect.insert({ entry->second, false });
			Present.insert(entry->second);
		}
	if (SecretEntry >= 0 && Stored.count({ SecretEntry, false })) {
		SecretWanted = 0;
	}
}

// Lock held.
int StoredBaseEntry(std::uint32_t runtimeId) {
	const auto print  = Materialized.find(runtimeId);
	const auto record = print == Materialized.end() ? Records.end() : Records.find(print->second);
	if (record == Records.end() || !record->second.base) return -1;
	const auto entry = EntryById.find(record->second.id);
	return entry == EntryById.end() ? -1 : entry->second;
}

void SayOnce(bool& told, const char* format, unsigned value) {
	if (!told) Say(true, format, value);
	told = true;
}

bool StaleChecked {}; // the page this character came in with was checked against the records; guarded by Lock

bool FreeCell(D2RL::PlayerHandle player, std::uint32_t code, int& freeX, int& freeY);
bool PageMove(D2RL::Inventory::LocalPlayerPageMoveKind kind, int x, int y);
void Unbusy(std::uint32_t id);
bool PerfectNow(int u, std::uint32_t runtimeId, bool eth);
std::atomic<bool> ForgetWanted {}; // grailon forget, done by the next arrange
void __cdecl Arrange(const D2RL::PluginContext* context, void* userData) noexcept {
	auto* job = static_cast<ArrangeJob*>(userData);
	Trace(job->open ? "arrange for view" : "arrange", job->tries);
	// A Ctrl+clicked item whose twin is stored but not on the page (its tile is
	// cut or elsewhere): the twin is made where the clicked item is, and leaves
	// the grail, so the clicked one can take its tile.
	if (job->drop.wanted && !job->open && !job->drop.cursor && !job->drop.swapOut) {
		Record twin;
		{
			std::lock_guard guard(Lock);
			const auto record = StoreReady && !StorePath.empty() && StoreOwner == job->player ? RecordAt(job->drop.unique, job->drop.eth) : nullptr;
			if (record && std::none_of(Materialized.begin(), Materialized.end(), [&](const auto& made) { return made.second == record->fingerprint; })) twin = *record;
		}
		if (!twin.bytes.empty()) {
			auto status = Items::Result::NotFound;
			for (const auto where : { job->drop.from, Items::ItemContainer::Inventory, Items::ItemContainer::PersonalStash }) {
				if (status == Items::Result::Success || where == Items::ItemContainer::CustomPage) continue;
				D2RL::ItemHandle made {};
				Items::ItemInfo  info { .structSize = Items::ItemInfoSize };
				PluginMoving = true;
				status       = Materialize(job->player, twin, { .structSize = Items::ItemDestinationRequiredSize, .container = where, .placement = Items::Placement::Automatic }, &made);
				PluginMoving = false;
				if (status == Items::Result::Success && made && (GetInfo(context, made, &info) != Items::Result::Success || info.container != where)) {
					(void)DestroyItem(job->player, made); // not where asked: the record keeps it
					status = Items::Result::InvalidArgument;
				}
			}
			std::lock_guard guard(Lock);
			if (status == Items::Result::Success) {
				TakeOut(twin.fingerprint);
				SaveStore();
				RebuildStored();
			} else Say(true, "grailon: no room for the stored copy the Ctrl+clicked item replaces (result %u)", static_cast<unsigned>(status));
		}
	}
	std::vector<Items::ItemInfo> items;
	const D2RL::Inventory::ItemFilter filter {
		.structSize    = D2RL::Inventory::ItemFilterSize,
		.containerMask = Items::ContainerBit(Items::ItemContainer::CustomPage) | Items::ContainerBit(Items::ItemContainer::PersonalStash)
		               | Items::ContainerBit(Items::ItemContainer::Inventory) | Items::ContainerBit(Items::ItemContainer::Cube),
	};
	(void)Inventory->forEachInventoryItem(context, job->player, &filter, Collect, &items);
	bool storage = false;
	{
		std::lock_guard guard(Lock);
		std::erase_if(items, [](const Items::ItemInfo& item) { return Busy.count(item.runtimeId) != 0; });
		storage = StoreReady && !StorePath.empty() && StoreOwner == job->player;
	}
	// Copies of grail items left on the host's page outside the grail's area
	// (1.0.4 had a 16x12 area there): the grail file keeps them, the copies go,
	// once per character.
	static D2RL::PlayerHandle leftoversChecked {};
	if (storage && Host::On() && leftoversChecked != job->player && Host::Info.x >= 0) {
		leftoversChecked = job->player;
		std::vector<Items::ItemInfo> raw;
		const D2RL::Inventory::ItemFilter onPage { .structSize = D2RL::Inventory::ItemFilterSize, .containerMask = Items::ContainerBit(Items::ItemContainer::CustomPage) };
		(void)Inventory->forEachInventoryItem(context, job->player, &onPage, CollectRaw, &raw);
		int found = 0, gone = 0;
		for (const auto& item : raw) {
			const int x = item.x - Host::Info.x, y = item.y - Host::Info.y;
			if (x >= 0 && y >= 0 && x < static_cast<int>(Host::Info.width) && y < static_cast<int>(Host::Info.height)) continue;
			bool copy = false;
			{
				std::lock_guard guard(Lock);
				copy = Records.count(FingerprintOf(item)) != 0;
			}
			if (!copy) continue;
			++found;
			PluginMoving = true;
			if (ItemApi->destroyItem(context, job->player, item.handle, Items::SocketedItemPolicy::DestroyContents) == Items::Result::Success) ++gone;
			PluginMoving = false;
		}
		if (found) Say(false, "grailon: %d grail copies left on the host page outside the grail's area, %d removed (the grail file keeps them)", found, gone);
	}

	// An item about to land on the page (Ctrl+click) is not there yet: try again
	// on a later frame.
	if (job->expect) {
		const bool arrived = std::any_of(items.begin(), items.end(), [&](const Items::ItemInfo& item) { return item.runtimeId == job->expect && item.container == Items::ItemContainer::CustomPage; });
		if (!arrived && ++job->tries < 20) {
			if (Threads->runOnGameThread(context, Arrange, job) == D2RL::Threads::Result::Success) return;
		}
		job->expect = 0;
	}

	// An item on the cursor: the game's own (the client's copy can go, below).
	D2RL::ItemHandle cursorItem {};
	const bool       cursorHeld = Inventory->getCursorItem(context, job->player, &cursorItem) == D2RL::Inventory::Result::Success && cursorItem;
	bool             touched    = false; // the page changed
	// The page items' fingerprints (their identity in the records).
	std::map<std::uint32_t, std::uint32_t> printOf;
	for (const auto& item : items)
		if (item.container == Items::ItemContainer::CustomPage && storage) printOf[item.runtimeId] = FingerprintOf(item);

	// The plan, under the lock: where each item goes.
	//   Stay / Cell: in its tile (Cell: moved there); Unload: its tile is not
	//   shown: destroyed, its record keeps it (made first when it has none);
	//   Destroy: an extra or stale copy of a record; Out: not for the grail;
	//   Swap: leaves its tile to the Ctrl+clicked twin.
	enum class Action { Stay, Cell, Unload, Destroy, Out, Swap };
	struct Step {
		Items::ItemInfo item;
		Action          action = Action::Stay;
		int             x {}, y {}, unique = -1;
		bool            eth {};
		std::uint32_t   fingerprint {};
		bool            record {}; // Unload: record it first
	};
	struct Make {
		Record record;
		int    x {}, y {};
	};
	std::vector<Step> steps;
	std::vector<Make> makes;
	bool              changed = false, jump = false, dirty = false;
	int               dropX = -1, dropY = -1;
	bool              pictureDrop = false; // the dropped item's tile is a picture: stored, not shown
	std::size_t       foundBefore = 0;
	static bool       toldStorage = false;
	if (!storage && !toldStorage) {
		toldStorage = true;
		Say(true, "grailon: the grail storage is not open; items are left where they are");
	}
	{
		std::lock_guard guard(Lock);
		foundBefore              = FoundIds.size();
		const bool      settings = !TilesView() || !storage; // no tiles
		const bool      stale    = !StaleChecked; // the page as the character came in
		StaleChecked             = StaleChecked || storage;
		std::vector<bool> taken(Shown.size());
		std::set<std::pair<int, bool>> claimed; // tiles with an item, shown or not
		const auto place = [&](int u, bool eth, const std::string& code, int& tile, int& x, int& y) {
			for (std::size_t t = 0; t < Shown.size(); ++t) {
				const auto& shown = Shown[t];
				if (shown.unique != u || shown.eth != eth) continue;
				int width = shown.width, height = shown.height;
				(void)ItemSize(code, width, height);
				tile = static_cast<int>(t);
				x    = shown.x + std::max(0, shown.width - width) / 2;
				y    = shown.y + std::max(0, shown.height - height) / 2;
				return true;
			}
			return false;
		};
		const auto target = [&](const Items::ItemInfo& item, int& tile, int& x, int& y) { return place(GrailIndex(item), IsEth(item), CodeText(item.code), tile, x, y); };
		const auto grail  = [&](const Items::ItemInfo& item) {
            const int u = GrailIndex(item);
            return u >= 0 && HasTile(u, IsEth(item));
		};
		std::vector<const Items::ItemInfo*> page;
		for (const auto& item : items)
			if (item.container == Items::ItemContainer::CustomPage) page.push_back(&item);
		// A record's item that left the page: the player took it out of the grail.
		if (storage)
			for (const auto& [runtime, fingerprint] : std::map(Materialized))
				if (std::none_of(page.begin(), page.end(), [&](const Items::ItemInfo* item) { return item->runtimeId == runtime; }) && !Busy.count(runtime)) {
					TakeOut(fingerprint);
					dirty = true;
				}
		// Those already in their tile first, so they keep it.
		std::stable_partition(page.begin(), page.end(), [&](const Items::ItemInfo* item) {
			int tile = -1, x = 0, y = 0;
			return grail(*item) && target(*item, tile, x, y) && item->x == x && item->y == y;
		});
		// A Ctrl+clicked item whose twin is in the grail: the twin leaves its tile.
		if (job->drop.wanted && !job->open && !job->drop.cursor && !job->drop.swapOut)
			if (const auto twin = Stored.find({ job->drop.unique, job->drop.eth }); twin != Stored.end() && twin->second != job->drop.runtimeId) job->drop.swapOut = twin->second;
		std::set<std::uint32_t> seen; // record fingerprints with an item on the page
		for (const auto* item : page) {
			Step step { *item };
			step.unique      = GrailIndex(*item);
			step.eth         = IsEth(*item);
			step.fingerprint = storage ? printOf[item->runtimeId] : 0;
			int tile = -1, x = 0, y = 0;
			if (job->drop.wanted && !job->open && job->drop.swapOut == item->runtimeId) {
				step.action = Action::Swap;
				steps.push_back(step);
				continue;
			}
			const auto record = step.fingerprint ? Records.find(step.fingerprint) : Records.end();
			if (record != Records.end()) {
				// A record's item: in its tile when shown, else away; a second copy goes.
				if (!seen.insert(step.fingerprint).second) step.action = Action::Destroy;
				else {
					Materialized[item->runtimeId] = step.fingerprint;
					const auto entry              = EntryById.find(record->second.id);
					if (entry != EntryById.end() && !settings && place(entry->second, record->second.eth, CodeText(item->code), tile, x, y) && !taken[tile]) {
						taken[tile] = true;
						step.x = x, step.y = y;
						step.action = item->x == x && item->y == y ? Action::Stay : Action::Cell;
					} else step.action = settings ? Action::Stay : Action::Unload;
					if (entry != EntryById.end()) claimed.insert({ entry->second, record->second.eth });
				}
				steps.push_back(step);
				continue;
			}
			if (stale && step.fingerprint && Tombstones.count(step.fingerprint)) step.action = Action::Destroy; // taken out on another character since
			else if (!grail(*item) || claimed.count({ step.unique, step.eth })
			         || (storage && RecordAt(step.unique, step.eth) && !(job->drop.swapOut && item->runtimeId == job->drop.runtimeId)))
				step.action = Action::Out; // not for the grail, or a twin (not the one swapping in)
			else if (settings) step.action = Action::Stay;
			else if (target(*item, tile, x, y) && !taken[tile]) {
				taken[tile] = true;
				step.x = x, step.y = y;
				step.action = item->x == x && item->y == y ? Action::Stay : Action::Cell; // recorded once in its tile
			} else {
				// Every tile is a picture: put on the page (Ctrl+click, a drop on
				// the grid), it goes to the grail file; its tile is shown.
				step.action = Action::Unload;
				step.record = true;
				if (!job->open && JumpTo < 0) {
					JumpTo  = step.unique;
					JumpEth = step.eth;
					jump    = true;
				}
			}
			claimed.insert({ step.unique, step.eth });
			steps.push_back(step);
		}
		// The records of the tiles on screen with no item there: made again.
		if (!settings)
			for (const auto& [fingerprint, record] : Records) {
				const auto entry = EntryById.find(record.id);
				int        tile = -1, x = 0, y = 0;
				if (seen.count(fingerprint) || entry == EntryById.end() || !place(entry->second, record.eth, CodeText(record.code), tile, x, y) || taken[tile]) continue;
				taken[tile] = true;
				makes.push_back({ record, x, y });
			}
		if (job->drop.wanted && job->open) LateDrop = job->drop; // placed once the view is open (OpenPrepared)
		else if (job->drop.wanted) {
			int tile = -1;
			for (std::size_t t = 0; t < Shown.size(); ++t)
				if (Shown[t].unique == job->drop.unique && Shown[t].eth == job->drop.eth) tile = static_cast<int>(t);
			// The swapped twin frees its tile.
			bool free = tile >= 0 && !taken[tile];
			if (tile >= 0 && job->drop.swapOut)
				for (const auto& step : steps)
					if (step.action == Action::Swap) free = true;
			if (free) {
				dropX = Shown[tile].x + std::max(0, Shown[tile].width - job->drop.width) / 2;
				dropY = Shown[tile].y + std::max(0, Shown[tile].height - job->drop.height) / 2;
			}
			// Its tile a picture (past the host's rows): onto a free cell, then stored off the page.
			if (tile < 0)
				for (const auto& peek : Peeking)
					if (peek.unique == job->drop.unique && peek.eth == job->drop.eth) pictureDrop = true;
		}
	}
	if (pictureDrop && !FreeCell(job->player, job->drop.code, dropX, dropY)) dropX = dropY = -1;

	// The moves: out of the way first, so the others find their cells free.
	std::optional<Record> swapped; // a Ctrl+click's replaced copy, destroyed until it is made again
	PluginMoving = true;
	static bool toldOut = false, toldUnload = false, toldCell = false, toldDrop = false, toldSwap = false, toldMake = false;
	for (auto& step : steps) {
		touched = touched || step.action != Action::Stay;
		if (step.action == Action::Swap && step.fingerprint) {
			// A stored copy: destroyed now (at once, unlike a move, so the tile is
			// free for the clicked item), made again in the clicked item's place
			// once that one is in. Its record waits in `swapped` meanwhile.
			std::lock_guard guard(Lock);
			if (const auto record = Records.find(step.fingerprint); record != Records.end()) {
				const Record keep = record->second;
				Materialized.erase(step.item.runtimeId);
				if (DestroyItem(job->player, step.item.handle)) {
					swapped = keep;
					TakeOut(keep.fingerprint);
					dirty = true;
					PluginMoving = true;
					continue;
				}
				Materialized[step.item.runtimeId] = keep.fingerprint;
				PluginMoving = true;
			}
		}
		if (step.action == Action::Swap) {
			// Out of the tile for now; it takes the clicked item's place once that one is in.
			auto status = Items::Result::InvalidArgument;
			for (const auto where : { Items::ItemContainer::PersonalStash, Items::ItemContainer::Inventory, job->drop.from })
				if (status != Items::Result::Success) status = MoveTo(job->player, step.item.handle, where);
			if (status != Items::Result::Success) {
				SayOnce(toldSwap, "grailon: no room for the grail copy the Ctrl+clicked item replaces (result %u)", static_cast<unsigned>(status));
				step.action = Action::Stay;
				dropX       = -1;
			} else if (step.fingerprint) {
				std::lock_guard guard(Lock);
				if (Records.count(step.fingerprint)) TakeOut(step.fingerprint), dirty = true;
			}
		}
		if (step.action == Action::Out) {
			if (const auto status = MoveTo(job->player, step.item.handle, Items::ItemContainer::Inventory); status != Items::Result::Success)
				SayOnce(toldOut, "grailon: could not move an item off the page to the inventory (result %u; is it full?)", static_cast<unsigned>(status));
		} else if (step.action == Action::Unload || step.action == Action::Destroy) {
			std::lock_guard guard(Lock);
			if (step.record) {
				if (!RecordItem(step.item, step.unique, step.eth, false)) {
					SayOnce(toldUnload, "grailon: could not store an item whose tile is not shown (%u); it stays on the page", 0);
					step.action = Action::Stay;
					continue;
				}
				dirty = true;
			}
			Materialized.erase(step.item.runtimeId);
			if (!DestroyItem(job->player, step.item.handle)) {
				SayOnce(toldUnload, "grailon: could not take an item off the page (%u)", 1);
				if (step.action == Action::Unload && Records.count(Fingerprints[step.item.runtimeId])) Materialized[step.item.runtimeId] = Fingerprints[step.item.runtimeId];
			}
		}
	}
	// Into the tiles. A move can wait for a cell another item leaves, so the
	// refused ones get a second pass; a record's item that still cannot go is
	// destroyed (made again in its tile next time), any other goes through the
	// stash or the cursor.
	std::vector<Step*> pending;
	for (auto& step : steps)
		if (step.action == Action::Cell) pending.push_back(&step);
	for (int pass = 0; pass < 3 && !pending.empty(); ++pass) {
		std::vector<Step*> refused;
		for (auto* step : pending) {
			auto status = MoveToCell(job->player, step->item.handle, step->x, step->y);
			if (status != Items::Result::Success && pass == 2) {
				std::lock_guard guard(Lock);
				if (Materialized.count(step->item.runtimeId)) {
					Materialized.erase(step->item.runtimeId);
					if (DestroyItem(job->player, step->item.handle)) status = Items::Result::Success;
				} else if (MoveOnPageByCursor(step->item, step->x, step->y)) status = Items::Result::Success;
			}
			if (status == Items::Result::Success) continue;
			if (pass == 2) SayOnce(toldCell, "grailon: could not move an item into its tile (result %u)", static_cast<unsigned>(status));
			else refused.push_back(step);
		}
		pending.swap(refused);
	}
	// The stored items of the tiles on screen.
	for (const auto& make : makes) {
		D2RL::ItemHandle made {};
		const auto       status = Materialize(job->player, make.record, CellDestination(make.x, make.y), &made);
		Items::ItemInfo  info { .structSize = Items::ItemInfoSize };
		if (status != Items::Result::Success || !made || GetInfo(context, made, &info) != Items::Result::Success) {
			SayOnce(toldMake, "grailon: could not bring a stored item back into its tile (result %u)", static_cast<unsigned>(status));
			continue;
		}
		if (info.container != Items::ItemContainer::CustomPage || info.x != make.x || info.y != make.y) (void)MoveToCell(job->player, made, make.x, make.y);
		// The copy must carry the record's identity, or a later load would not know it.
		static bool toldPrint = false;
		if (const auto print = FingerprintOf(info); print != make.record.fingerprint && !toldPrint) {
			toldPrint = true;
			Say(true, "grailon: a rebuilt item does not carry its stored identity (%08X, stored %08X)", print, make.record.fingerprint);
		}
		std::lock_guard guard(Lock);
		Materialized[info.runtimeId] = make.record.fingerprint;
		Fingerprints[info.runtimeId] = make.record.fingerprint;
	}
	// A Ctrl+clicked item: from where it is straight into its tile.
	if (dropX >= 0 && !job->drop.cursor) {
		const auto item = std::find_if(items.begin(), items.end(), [&](const Items::ItemInfo& item) { return item.runtimeId == job->drop.runtimeId; });
		if (item == items.end()) dropX = -1;
		else if (item->container != Items::ItemContainer::CustomPage || item->x != dropX || item->y != dropY) {
			if (const auto status = MoveToCell(job->player, item->handle, dropX, dropY); status != Items::Result::Success) {
				SayOnce(toldDrop, "grailon: could not put the item in its tile (result %u)", static_cast<unsigned>(status));
				dropX = -1;
			} else {
				// The copy it replaced takes its place.
				if (swapped) {
					auto status = Items::Result::InvalidArgument;
					for (const auto& where : { Items::ItemDestination { .structSize = sizeof(Items::ItemDestination), .container = item->container, .placement = Items::Placement::Exact,
					                                                    .x = static_cast<std::uint32_t>(item->x), .y = static_cast<std::uint32_t>(item->y), .sharedStashPage = item->sharedStashPage },
					                           Items::ItemDestination { .structSize = Items::ItemDestinationRequiredSize, .container = Items::ItemContainer::Inventory, .placement = Items::Placement::Automatic },
					                           Items::ItemDestination { .structSize = Items::ItemDestinationRequiredSize, .container = Items::ItemContainer::PersonalStash, .placement = Items::Placement::Automatic } }) {
						if (status == Items::Result::Success) break;
						D2RL::ItemHandle made {};
						status = Materialize(job->player, *swapped, where, &made);
					}
					PluginMoving = true;
					if (status == Items::Result::Success) swapped.reset();
					else Say(true, "grailon: no room for the stored copy the Ctrl+clicked item replaces (result %u); it stays in the grail file", static_cast<unsigned>(status));
				}
				for (const auto& step : steps)
					if (step.action == Action::Swap && !step.fingerprint) {
						const auto status = MoveItem(job->player, step.item.handle,
						                             { .structSize      = sizeof(Items::ItemDestination), // with the shared stash page
						                               .container       = item->container,
						                               .placement       = Items::Placement::Exact,
						                               .x               = static_cast<std::uint32_t>(item->x),
						                               .y               = static_cast<std::uint32_t>(item->y),
						                               .sharedStashPage = item->sharedStashPage });
						if (status != Items::Result::Success) Say(false, "grailon: the replaced copy could not take the clicked item's place (result %u)", static_cast<unsigned>(status));
					}
				int sound = 0;
				{
					std::lock_guard guard(Lock);
					if (const auto base = Bases.find(CodeText(item->code)); base != Bases.end()) sound = base->second.dropSound;
				}
				if (sound > 0) (void)Threads->runOnUiThread(context, PlayItemSound, reinterpret_cast<void*>(static_cast<std::uintptr_t>(sound)));
			}
		}
	}
	// The replaced copy found no place (or the clicked item stayed out): its
	// record comes back, so nothing is lost; the next arrange sorts the tile out.
	if (swapped) {
		std::lock_guard guard(Lock);
		Tombstones.erase(swapped->fingerprint);
		Records[swapped->fingerprint] = *swapped;
		dirty                         = true;
	}
	if (job->drop.wanted) Trace("arrange drop at", dropX);
	// A dropped item: from the cursor straight into its tile.
	if (dropX >= 0 && job->drop.cursor) {
		const D2RL::Inventory::LocalPlayerPageMoveRequest put { .structSize = D2RL::Inventory::LocalPlayerPageMoveRequestSize, .kind = D2RL::Inventory::LocalPlayerPageMoveKind::CursorToPage,
			                                                    .x = static_cast<std::uint32_t>(dropX), .y = static_cast<std::uint32_t>(dropY) };
		if (const auto result = Host::On() ? static_cast<unsigned>(Host::Service->moveItem(Host::Area, PageHost::MoveKind::CursorToArea, dropX, dropY))
		                                   : static_cast<unsigned>(Inventory->executeLocalPlayerMove(context, Page, &put));
		    result != 0) {
			SayOnce(toldDrop, "grailon: could not put the item in its tile (result %u)", static_cast<unsigned>(result));
			dropX = -1;
		}
	}
	PluginMoving = false;

	// Grail items now in their tile with no record (dropped, Ctrl+clicked, or
	// from before): recorded.
	if (storage) {
		std::vector<Items::ItemInfo> now;
		const D2RL::Inventory::ItemFilter onPage { .structSize = D2RL::Inventory::ItemFilterSize, .containerMask = Items::ContainerBit(Items::ItemContainer::CustomPage) };
		(void)Inventory->forEachInventoryItem(context, job->player, &onPage, Collect, &now);
		std::lock_guard guard(Lock);
		for (const auto& item : now) {
			if (Busy.count(item.runtimeId) || Materialized.count(item.runtimeId)) continue;
			if (pictureDrop && dropX >= 0 && item.runtimeId == job->drop.runtimeId) {
				if (RecordItem(item, job->drop.unique, job->drop.eth, PerfectNow(job->drop.unique, item.runtimeId, job->drop.eth))) {
					dirty = true;
					if (!DestroyItem(job->player, item.handle)) Materialized[item.runtimeId] = Fingerprints[item.runtimeId]; // the next arrange sends it off
				} else SayOnce(toldUnload, "grailon: could not store an item (%u)", 3);
				continue;
			}
			const int u = GrailIndex(item);
			int       tile = -1, x = 0, y = 0;
			bool      shown = false;
			for (std::size_t t = 0; t < Shown.size() && !shown; ++t)
				if (Shown[t].unique == u && Shown[t].eth == IsEth(item)) shown = true, tile = static_cast<int>(t);
			if (u < 0 || !shown || RecordAt(u, IsEth(item))) continue;
			(void)x, (void)y, (void)tile;
			if (RecordItem(item, u, IsEth(item), false)) {
				Materialized[item.runtimeId] = Fingerprints[item.runtimeId];
				dirty                        = true;
			} else SayOnce(toldUnload, "grailon: could not store an item (%u)", 2);
		}
		// The standard that was going in and is not on the page (nor on its way): its tile goes.
		if (SecretWanted && !Busy.count(SecretWanted) && !(job->drop.wanted && job->drop.runtimeId == SecretWanted) && !(LateDrop.wanted && LateDrop.runtimeId == SecretWanted)
		    && std::none_of(now.begin(), now.end(), [](const Items::ItemInfo& item) { return item.runtimeId == SecretWanted; }))
			SecretWanted = 0;
	}

	// What the views show now, and which entries were found.
	{
		std::lock_guard guard(Lock);
		// grailon forget: found but not stored any more, back to never found.
		if (storage && ForgetWanted.exchange(false)) {
			std::set<std::string> kept;
			for (const auto& [fingerprint, record] : Records)
				if (!record.base) kept.insert(record.id);
			std::size_t forgotten = 0;
			for (auto it = FoundIds.begin(); it != FoundIds.end();)
				if (kept.count(*it)) ++it;
				else it = FoundIds.erase(it), ++forgotten;
			Say(false, "grailon: %zu missing entries back to never found", forgotten);
			if (forgotten) dirty = true, jump = true;
		}
		if (storage) RebuildStored();
		changed = FoundIds.size() != foundBefore;
		if (dirty) SaveStore();
	}
	// The page changed under an item on the cursor: the game's client drops its
	// copy of the cursor item when the server puts items in a page (it takes
	// them for its own cursor item going down), so the item vanished from the
	// hand while the game still held it. Down and back up again in the same
	// frame gives the client its cursor item back.
	if (cursorHeld && (touched || !makes.empty())) {
		D2RL::ItemHandle cursor {};
		Items::ItemInfo  info { .structSize = Items::ItemInfoSize };
		int              x = 0, y = 0;
		if (Inventory->getCursorItem(context, job->player, &cursor) == D2RL::Inventory::Result::Success && cursor && GetInfo(context, cursor, &info) == Items::Result::Success
		    && FreeCell(job->player, info.code, x, y)) {
			{
				std::lock_guard guard(Lock);
				Busy.insert(info.runtimeId);
			}
			MuteSounds(400);
			PluginMoving   = true;
			const bool put = PageMove(D2RL::Inventory::LocalPlayerPageMoveKind::CursorToPage, x, y);
			const bool up  = put && PageMove(D2RL::Inventory::LocalPlayerPageMoveKind::PageToCursor, x, y);
			PluginMoving   = false;
			Unbusy(info.runtimeId);
			Trace(up ? "cursor item given back" : put ? "cursor item left on the page" : "cursor item not given back", x + y * 100);
		}
	}
	if (changed || jump || dirty) RebuildWanted = true; // dirty: an item went in or out
	if (job->open) (void)Threads->runOnUiThread(context, OpenPreparedOnUi, reinterpret_cast<void*>(static_cast<std::uintptr_t>(job->sequence)));
	else if (const auto again = ArrangeAgain.exchange(0)) {
		// Asked for again while this one ran: run once more, at once.
		if (QueueArrange(new ArrangeJob { .player = job->player, .expect = again == 1 ? 0 : again })) {
			delete job;
			return;
		}
		ArrangeRunning = false;
	} else ArrangeRunning = false;
	delete job;
}

// ---- the cursor around view changes and cut slots (game thread) ----
// Each step is its own game-thread job, so the client has the item where the
// last step put it before the next moves it again.

enum class CursorStep { HoldPut, HoldPark, RestoreCell, RestoreTake, TakeCell, TakeCursor, PutPage, PutPark, CtrlOut, SwapPut, SwapIn, SwapOut, SwapCursor };
struct CursorJob {
	CursorStep    step {};
	std::uint32_t id {};   // the item's runtime id (TakeCell, CtrlOut: its record's fingerprint)
	std::uint32_t made {}; // TakeCursor: the item made from the record
	// A swap with the stored copy: the clicked or held item takes its record's
	// place, the copy goes where the item was (Ctrl+click) or on the cursor.
	bool                   cursor {};
	int                    sound {}; // the item's drop sound, played once it is in
	std::optional<Record>  twin;
	Items::ItemDestination back {};
	int           unique = -1;
	bool          eth {};
	int           x {}, y {};
	D2RL::Threads::Callback then {}; // UI thread, once the job is done
	void* thenData {};
};
void __cdecl RunCursorJob(const D2RL::PluginContext* context, void* userData) noexcept;

bool RemoteCursorJob(CursorJob* job);
bool QueueCursorJob(CursorJob* job) {
	if (Remote()) return RemoteCursorJob(job);
	if (Threads->runOnGameThread(Context, RunCursorJob, job) == D2RL::Threads::Result::Success) return true;
	delete job;
	return false;
}

bool FindItem(D2RL::PlayerHandle player, std::uint32_t id, Items::ItemInfo& found) {
	std::vector<Items::ItemInfo>      items;
	const D2RL::Inventory::ItemFilter filter {
		.structSize    = D2RL::Inventory::ItemFilterSize,
		.containerMask = Items::ContainerBit(Items::ItemContainer::CustomPage) | Items::ContainerBit(Items::ItemContainer::PersonalStash)
		               | Items::ContainerBit(Items::ItemContainer::Inventory) | Items::ContainerBit(Items::ItemContainer::Cube) | Items::ContainerBit(Items::ItemContainer::SharedStash),
	};
	(void)Inventory->forEachInventoryItem(Context, player, &filter, Collect, &items);
	for (const auto& item : items)
		if (item.runtimeId == id) {
			found = item;
			return true;
		}
	return false;
}

// A free spot for a `code` item on the page, out of the shown tiles if it can.
bool FreeCell(D2RL::PlayerHandle player, std::uint32_t code, int& freeX, int& freeY) {
	std::vector<Items::ItemInfo>      items;
	const D2RL::Inventory::ItemFilter filter { .structSize = D2RL::Inventory::ItemFilterSize, .containerMask = Items::ContainerBit(Items::ItemContainer::CustomPage) };
	(void)Inventory->forEachInventoryItem(Context, player, &filter, Collect, &items);
	bool used[PageSize][PageSize] {}, tiles[PageSize][PageSize] {};
	int  width = 1, height = 1;
	{
		std::lock_guard guard(Lock);
		(void)ItemSize(CodeText(code), width, height);
		const auto mark = [](bool (&cells)[PageSize][PageSize], int x, int y, int w, int h) {
			for (int row = std::max(0, y); row < std::min(PageSize, y + h); ++row)
				for (int column = std::max(0, x); column < std::min(PageSize, x + w); ++column) cells[row][column] = true;
		};
		for (const auto& item : items) {
			int w = 1, h = 1;
			(void)ItemSize(CodeText(item.code), w, h);
			mark(used, item.x, item.y, w, h);
		}
		for (const auto& tile : Shown) mark(tiles, tile.x, tile.y, tile.width, tile.height);
	}
	for (const bool avoidTiles : { true, false })
		for (int y = 0; y + height <= StageRows; ++y)
			for (int x = 0; x + width <= StageCols; ++x) {
				bool free = true;
				for (int row = y; row < y + height && free; ++row)
					for (int column = x; column < x + width && free; ++column) free = !used[row][column] && !(avoidTiles && tiles[row][column]);
				if (free) {
					freeX = x, freeY = y;
					return true;
				}
			}
	return false;
}

bool PageMove(D2RL::Inventory::LocalPlayerPageMoveKind kind, int x, int y) {
	const D2RL::Inventory::LocalPlayerPageMoveRequest move { .structSize = D2RL::Inventory::LocalPlayerPageMoveRequestSize, .kind = kind,
		                                                     .x = static_cast<std::uint32_t>(x), .y = static_cast<std::uint32_t>(y) };
	PluginMoving    = true;
	const bool done = Host::On() ? Host::Service->moveItem(Host::Area, kind == D2RL::Inventory::LocalPlayerPageMoveKind::CursorToPage ? PageHost::MoveKind::CursorToArea : PageHost::MoveKind::AreaToCursor, x, y)
	                                   == PageHost::Result::Success
	                             : Inventory->executeLocalPlayerMove(Context, Page, &move) == D2RL::Inventory::Result::Success;
	PluginMoving    = false;
	return done;
}

// Out of the way: the personal stash, else the inventory.
bool ParkItem(D2RL::PlayerHandle player, D2RL::ItemHandle item, Items::ItemContainer& where) {
	PluginMoving = true;
	where        = Items::ItemContainer::PersonalStash;
	bool parked  = MoveTo(player, item, where) == Items::Result::Success;
	if (!parked) parked = MoveTo(player, item, where = Items::ItemContainer::Inventory) == Items::Result::Success;
	PluginMoving = false;
	return parked;
}

void Unbusy(std::uint32_t id) {
	std::lock_guard guard(Lock);
	Busy.erase(id);
}

void __cdecl RefreshOnUi(const D2RL::PluginContext*, void*) noexcept {
	if (ViewOpen() || PreparedView >= 0) ShowView();
}

// UI thread: the view moves to a tile (unique * 2 + eth).
void __cdecl ShowSwappedTile(const D2RL::PluginContext*, void* userData) noexcept {
	const auto value = static_cast<int>(reinterpret_cast<std::uintptr_t>(userData));
	{
		std::lock_guard guard(Lock);
		JumpTo = value / 2, JumpEth = (value & 1) != 0;
	}
	if (ViewOpen() || PreparedView >= 0) ShowView();
}

void __cdecl RunCursorJob(const D2RL::PluginContext* context, void* userData) noexcept {
	auto*            job    = static_cast<CursorJob*>(userData);
	const auto       player = LocalPlayer.load();
	D2RL::ItemHandle cursor {};
	const bool       holding = player && Inventory->getCursorItem(context, player, &cursor) == D2RL::Inventory::Result::Success && cursor;
	Items::ItemInfo  item { .structSize = Items::ItemInfoSize };
	bool             next = false; // the job goes on with its next step
	static bool      toldHold = false, toldTake = false, toldPut = false, toldOut = false;
	switch (job->step) {
	case CursorStep::HoldPut:
		// The cursor item onto a free cell of the page, then away.
		if (holding && GetInfo(context, cursor, &item) == Items::Result::Success && FreeCell(player, item.code, job->x, job->y)) {
			{
				std::lock_guard guard(Lock);
				Busy.insert(item.runtimeId);
			}
			if (PageMove(D2RL::Inventory::LocalPlayerPageMoveKind::CursorToPage, job->x, job->y)) {
				HeldId    = item.runtimeId;
				job->id   = item.runtimeId;
				job->step = CursorStep::HoldPark;
				next      = true;
			} else {
				Unbusy(item.runtimeId);
				SayOnce(toldHold, "grailon: could not keep the cursor item aside while the view changes (%u)", 0);
			}
		}
		break;
	case CursorStep::HoldPark:
		if (Items::ItemContainer where {}; FindItem(player, job->id, item) && item.container == Items::ItemContainer::CustomPage) (void)ParkItem(player, item.handle, where);
		break;
	case CursorStep::RestoreCell:
		// Back from where it was kept: onto a free cell, then the cursor.
		if (!holding && FindItem(player, job->id, item)) {
			job->x = item.x, job->y = item.y;
			if (item.container == Items::ItemContainer::CustomPage) next = true;
			else if (FreeCell(player, item.code, job->x, job->y)) {
				PluginMoving = true;
				next         = MoveToCell(player, item.handle, job->x, job->y) == Items::Result::Success;
				PluginMoving = false;
			}
			if (next) job->step = CursorStep::RestoreTake;
		}
		if (!next) {
			// Something else is on the cursor: the kept item stays where it is.
			HeldId    = 0;
			Restoring = false;
			Unbusy(job->id);
		}
		break;
	case CursorStep::RestoreTake:
		if (!PageMove(D2RL::Inventory::LocalPlayerPageMoveKind::PageToCursor, job->x, job->y)) SayOnce(toldHold, "grailon: could not give the cursor item back (%u)", 1);
		HeldId    = 0;
		Restoring = false;
		Unbusy(job->id);
		break;
	case CursorStep::TakeCell: {
		// A cut slot's stored item: made from its record on a free cell, then the cursor.
		Record record;
		{
			std::lock_guard guard(Lock);
			if (const auto found = Records.find(job->id); found != Records.end()) record = found->second;
		}
		D2RL::ItemHandle made {};
		if (!holding && !record.bytes.empty() && FreeCell(player, record.code, job->x, job->y)) {
			PluginMoving = true;
			const auto status = Materialize(player, record, CellDestination(job->x, job->y), &made);
			PluginMoving = false;
			if (status == Items::Result::Success && made && GetInfo(context, made, &item) == Items::Result::Success && item.container == Items::ItemContainer::CustomPage) {
				std::lock_guard guard(Lock);
				Busy.insert(item.runtimeId);
				job->made = item.runtimeId, job->x = item.x, job->y = item.y;
				job->step = CursorStep::TakeCursor;
				next      = true;
			}
		}
		if (!next) {
			SayOnce(toldTake, "grailon: could not take the item of the clicked slot (%u)", 0);
			TakeId = 0;
			Taking = false;
			job->then = RefreshOnUi;
		}
		break;
	}
	case CursorStep::TakeCursor: {
		// On the cursor it has left the grail.
		const bool taken = PageMove(D2RL::Inventory::LocalPlayerPageMoveKind::PageToCursor, job->x, job->y);
		if (!taken) SayOnce(toldTake, "grailon: could not take the item of the clicked slot (%u)", 1);
		{
			std::lock_guard guard(Lock);
			if (taken) TakeOut(job->id), SaveStore(), RebuildStored();
			else Materialized[job->made] = job->id; // still on the page: the record's item
			Busy.erase(job->made);
		}
		TakeId = 0;
		Taking = false;
		job->then = RefreshOnUi;
		break;
	}
	case CursorStep::PutPage:
		// The cursor item for a cut slot: onto a free cell, recorded, then off the page.
		if (holding && GetInfo(context, cursor, &item) == Items::Result::Success && item.runtimeId == job->id && FreeCell(player, item.code, job->x, job->y)
		    && PageMove(D2RL::Inventory::LocalPlayerPageMoveKind::CursorToPage, job->x, job->y)) {
			job->step = CursorStep::PutPark;
			next      = true;
		} else {
			SayOnce(toldPut, "grailon: could not put the item in its slot (%u)", 0);
			Unbusy(job->id);
		}
		break;
	case CursorStep::PutPark: {
		bool stored = false;
		if (FindItem(player, job->id, item) && item.container == Items::ItemContainer::CustomPage) {
			std::lock_guard guard(Lock);
			if (RecordItem(item, job->unique, job->eth, false)) {
				stored = true;
				if (!DestroyItem(player, item.handle)) Materialized[item.runtimeId] = Fingerprints[item.runtimeId]; // the next arrange sends it off
				SaveStore();
				RebuildStored();
			}
		}
		if (!stored) SayOnce(toldPut, "grailon: could not put the item in its slot (%u)", 1); // on the page: the next arrange records it or sends it out
		Unbusy(job->id);
		job->then = RefreshOnUi;
		break;
	}
	case CursorStep::SwapPut:
		// The held item onto a free cell first (a cursor item cannot be read
		// from everywhere), then swapped from there.
		if (holding && GetInfo(context, cursor, &item) == Items::Result::Success && item.runtimeId == job->id && FreeCell(player, item.code, job->x, job->y)) {
			{
				std::lock_guard guard(Lock);
				Busy.insert(item.runtimeId);
			}
			if (PageMove(D2RL::Inventory::LocalPlayerPageMoveKind::CursorToPage, job->x, job->y)) {
				job->step = CursorStep::SwapIn;
				next      = true;
			} else Unbusy(item.runtimeId);
		}
		if (!next) SayOnce(toldPut, "grailon: could not swap the held item with its stored copy (%u)", 0);
		break;
	case CursorStep::SwapIn: {
		// The item becomes the tile's record; the stored copy's record leaves
		// the grail (kept in the job) and its item on the page, if any, goes.
		bool done = false;
		if (FindItem(player, job->id, item)) {
			const auto print = FingerprintOf(item);
			std::lock_guard guard(Lock);
			const auto twin = RecordAt(job->unique, job->eth);
			if (twin && twin->fingerprint != print) {
				const Record keep = *twin;
				job->back = { .structSize = sizeof(Items::ItemDestination), .container = item.container, .placement = Items::Placement::Exact, .x = item.x, .y = item.y,
					          .sharedStashPage = item.sharedStashPage };
				if (const auto base = Bases.find(CodeText(item.code)); base != Bases.end()) job->sound = base->second.dropSound;
				if (RecordItem(item, job->unique, job->eth, false)) {
					Records.erase(keep.fingerprint);
					for (const auto& [runtime, fingerprint] : std::map(Materialized))
						if (fingerprint == keep.fingerprint) {
							Items::ItemInfo onPage { .structSize = Items::ItemInfoSize };
							Materialized.erase(runtime);
							if (FindItem(player, runtime, onPage)) (void)DestroyItem(player, onPage.handle);
						}
					if (DestroyItem(player, item.handle)) {
						job->twin = keep;
						done      = true;
					} else { // could not take it in: as it was
						Records.erase(Fingerprints[item.runtimeId]);
						Records[keep.fingerprint] = keep;
					}
					RebuildStored();
				}
			}
		}
		if (done) {
			job->step = CursorStep::SwapOut;
			next      = true;
		} else {
			SayOnce(toldPut, "grailon: could not swap the item with its stored copy (%u)", 1);
			Unbusy(job->id);
			job->then = RefreshOnUi;
		}
		break;
	}
	case CursorStep::SwapOut: {
		// The stored copy comes out where the item was (Ctrl+click), or on a
		// free cell on its way to the cursor.
		auto             status = Items::Result::InvalidArgument;
		D2RL::ItemHandle made {};
		if (job->cursor) {
			if (FreeCell(player, job->twin->code, job->x, job->y)) status = Materialize(player, *job->twin, CellDestination(job->x, job->y), &made);
		} else {
			const Items::ItemDestination places[] { job->back,
				                                    { .structSize = Items::ItemDestinationRequiredSize, .container = Items::ItemContainer::Inventory, .placement = Items::Placement::Automatic },
				                                    { .structSize = Items::ItemDestinationRequiredSize, .container = Items::ItemContainer::PersonalStash, .placement = Items::Placement::Automatic } };
			for (const auto& where : places)
				if (status != Items::Result::Success && where.container != Items::ItemContainer::CustomPage) status = Materialize(player, *job->twin, where, &made);
		}
		std::lock_guard guard(Lock);
		if (status == Items::Result::Success && made) {
			Tombstones.insert(job->twin->fingerprint);
			if (job->cursor && GetInfo(context, made, &item) == Items::Result::Success) {
				Busy.insert(item.runtimeId);
				job->made = item.runtimeId, job->x = item.x, job->y = item.y;
				job->step = CursorStep::SwapCursor;
				next      = true;
			}
		} else {
			// Nowhere to go: the copy stays in the grail and the item comes back out.
			Say(true, "grailon: no room for the stored copy (result %u); the swap is undone", static_cast<unsigned>(status));
			const auto mine = RecordAt(job->unique, job->eth);
			if (mine) {
				const Record taken = *mine;
				Records.erase(taken.fingerprint);
				Records[job->twin->fingerprint] = *job->twin;
				D2RL::ItemHandle back {};
				PluginMoving = true;
				if (Materialize(player, taken, { .structSize = Items::ItemDestinationRequiredSize, .container = Items::ItemContainer::Inventory, .placement = Items::Placement::Automatic }, &back)
				    != Items::Result::Success)
					Records[taken.fingerprint] = taken, Records.erase(job->twin->fingerprint); // nothing works: the item stays stored instead
				PluginMoving = false;
			}
		}
		Busy.erase(job->id);
		SaveStore();
		RebuildStored();
		if (!next) {
			// A Ctrl+click: its tile comes into view with the item in it, with its sound.
			job->then = RefreshOnUi;
			if (status == Items::Result::Success && !job->cursor) {
				if (job->sound > 0) (void)Threads->runOnUiThread(context, PlayItemSound, reinterpret_cast<void*>(static_cast<std::uintptr_t>(job->sound)));
				job->then     = ShowSwappedTile;
				job->thenData = reinterpret_cast<void*>(static_cast<std::uintptr_t>(job->unique * 2 + (job->eth ? 1 : 0)));
			}
		}
		break;
	}
	case CursorStep::SwapCursor: {
		if (!PageMove(D2RL::Inventory::LocalPlayerPageMoveKind::PageToCursor, job->x, job->y)) SayOnce(toldTake, "grailon: the stored copy stays on the page (%u)", 2); // the next arrange sends it out
		std::lock_guard guard(Lock);
		Busy.erase(job->made);
		job->then = RefreshOnUi;
		break;
	}
	case CursorStep::CtrlOut: {
		// Ctrl+click on a cut slot: its stored item is made in the inventory.
		Record record;
		{
			std::lock_guard guard(Lock);
			if (const auto found = Records.find(job->id); found != Records.end()) record = found->second;
		}
		D2RL::ItemHandle made {};
		PluginMoving      = true;
		const auto status = record.bytes.empty() ? Items::Result::NotFound
		                                         : Materialize(player, record, { .structSize = Items::ItemDestinationRequiredSize, .container = Items::ItemContainer::Inventory, .placement = Items::Placement::Automatic }, &made);
		PluginMoving      = false;
		if (status == Items::Result::Success && made && GetInfo(context, made, &item) == Items::Result::Success && item.container != Items::ItemContainer::CustomPage) {
			std::lock_guard guard(Lock);
			TakeOut(job->id);
			SaveStore();
			RebuildStored();
		} else {
			if (status == Items::Result::Success && made) (void)DestroyItem(player, made); // not where it should be: the record keeps it
			SayOnce(toldOut, "grailon: no room in the inventory for the item of the clicked slot (result %u)", static_cast<unsigned>(status));
		}
		job->then = RefreshOnUi;
		break;
	}
	}
	if (next && QueueCursorJob(job)) return;
	if (next) { // could not go on: give up on it
		HeldId = 0, TakeId = 0, Restoring = false, Taking = false;
		std::lock_guard guard(Lock);
		Busy.clear();
		return;
	}
	if (job->then) (void)Threads->runOnUiThread(context, job->then, job->thenData);
	delete job;
}

// UI thread: keeps the cursor item aside, then runs `then`. False when the
// cursor is empty (nothing to wait for).
bool HoldCursor(D2RL::Threads::Callback then, void* data) {
	D2RL::ItemHandle handle {};
	const auto       player = LocalPlayer.load();
	if (HeldId || !player || Inventory->getCursorItem(Context, player, &handle) != D2RL::Inventory::Result::Success || !handle) return false;
	return QueueCursorJob(new CursorJob { .step = CursorStep::HoldPut, .then = then, .thenData = data });
}

// UI thread, once no view change is under way: the kept cursor item comes
// back, and a clicked cut slot's item goes on the cursor.
void KickPending() {
	if (Anim.running || Anim.queued || Holding || PreparedView >= 0) return;
	if (const auto id = HeldId.load(); id && !Restoring.exchange(true) && !QueueCursorJob(new CursorJob { .step = CursorStep::RestoreCell, .id = id })) Restoring = false;
	if (const auto id = TakeId.load(); id && !HeldId && !Taking.exchange(true) && !QueueCursorJob(new CursorJob { .step = CursorStep::TakeCell, .id = id })) Taking = false;
}

// ---- admission (before any move) ----

void Queue(const std::string& command);
void LearnBar(int step);
int  StepRow(int step, int maxScroll);
Drop PendingDrop; // guarded by Lock

// Any thread: arranges the page now instead of on the next tick, once the
// item `expect` is on it.
void ArrangeSoon(D2RL::PlayerHandle player, std::uint32_t expect) {
	if (ArrangeRunning.exchange(true)) {
		if (expect || !ArrangeAgain) ArrangeAgain = expect ? expect : 1; // the running one runs again after
		return;
	}
	if (!QueueArrange(new ArrangeJob { .player = player, .expect = expect })) ArrangeRunning = false;
}

// The page asks before an item lands on it. Only a grail item without a twin
// already stored may come. A dropped or Ctrl+clicked item is refused, then the
// plugin puts it straight into its tile (from the cursor, or from where it
// is), so it never shows anywhere else. While the mouse button is up the game
// is only asking for the hover highlight. Items the save restores are kept.
std::atomic<std::uint32_t> HoveredWith {}; // the cursor item last seen with the button up
std::atomic<bool>          ScrollTestOpen {}; // the scroll test takes any item where it is dropped
// The item the game last showed a tooltip for, and when: the one a Ctrl+click
// is on when D2RLoader has no Ctrl+click listener (the cursor is empty then).
std::atomic<D2RL::ItemHandle> HoveredItem {};
std::atomic<std::uint64_t>    HoveredAt {};
constexpr std::uint64_t       HoverFreshMs = 400;


// ---- joined TCP/IP games ----
// A player who joined another player's TCP/IP game cannot change items: the
// hosting game does it. Player Page Host (1.0.2) asks it (requests answered
// later, on the UI thread). Here the page is a way in and out only: a grail
// item put there is saved (its bytes from the hosting game), recorded and
// destroyed; a stored item comes out made from its bytes. No comparison
// copies of items not on screen.
bool Remote() {
	return Host::On() && PageHost::HasRequests(Host::Service) && Host::Service->itemsWhere() == PageHost::ItemsWhere::Remote;
}

struct RemoteTask {
	std::function<void(const PageHost::RequestAnswer&)> done;
};
void __cdecl RemoteDone(void* userData, const PageHost::RequestAnswer* answer) noexcept {
	std::unique_ptr<RemoteTask> task(static_cast<RemoteTask*>(userData));
	PageHost::RequestAnswer     copy { .structSize = sizeof(PageHost::RequestAnswer), .result = PageHost::Result::Failed };
	if (answer) copy = *answer;
	if (task->done) task->done(copy);
}
// A request: `start` sends it with (done, userData); refused at once, `done`
// gets the refusal (later never).
template <typename Start>
void Ask(const char* what, Start start, std::function<void(const PageHost::RequestAnswer&)> done) {
	auto*      task   = new RemoteTask { std::move(done) };
	const auto result = start(RemoteDone, static_cast<void*>(task));
	if (result != PageHost::Result::Success) {
		Say(true, "grailon: joined game: %s was refused (result %u)", what, static_cast<unsigned>(result));
		PageHost::RequestAnswer refused { .structSize = sizeof(PageHost::RequestAnswer), .result = result };
		RemoteDone(task, &refused);
	}
}
bool Answered(const PageHost::RequestAnswer& answer, const char* what) {
	if (answer.result == PageHost::Result::Success) return true;
	Say(true, "grailon: joined game: %s failed (result %u, item result %u)", what, static_cast<unsigned>(answer.result), answer.itemResult);
	return false;
}
void RemoteSave(std::uint32_t id, std::function<void(const PageHost::RequestAnswer&)> done) {
	Ask("saving an item", [&](PageHost::RequestDone cb, void* data) { return Host::Service->requestSaveItem(Host::Area, id, cb, data); }, std::move(done));
}
void RemoteMove(std::uint32_t id, Items::ItemDestination where, std::function<void(const PageHost::RequestAnswer&)> done) {
	where.structSize = Items::ItemDestinationRequiredSize;
	Ask("moving an item", [&](PageHost::RequestDone cb, void* data) { return Host::Service->requestMoveItem(Host::Area, id, &where, cb, data); }, std::move(done));
}
void RemoteDestroy(std::uint32_t id, std::function<void(const PageHost::RequestAnswer&)> done) {
	Ask("removing an item", [&](PageHost::RequestDone cb, void* data) { return Host::Service->requestDestroyItem(Host::Area, id, Items::SocketedItemPolicy::DestroyContents, cb, data); },
	    std::move(done));
}
void RemoteCreate(const Record& record, const Items::ItemDestination& where, std::function<void(const PageHost::RequestAnswer&)> done) {
	const auto spec = RecordSpec(record, where);
	Ask("making a stored item", [&](PageHost::RequestDone cb, void* data) {
		return Host::Service->requestCreateFromSave(Host::Area, &spec, record.bytes.data(), static_cast<std::uint32_t>(record.bytes.size()), cb, data);
	}, std::move(done));
}
Items::ItemDestination Where(Items::ItemContainer container) {
	return { .structSize = Items::ItemDestinationRequiredSize, .container = container, .placement = Items::Placement::Automatic };
}
void RemoteRefresh() { RefreshOnUi(Context, nullptr); }

// An item anywhere the player has one, the cursor too.
bool FindAnywhere(D2RL::PlayerHandle player, std::uint32_t id, Items::ItemInfo& found) {
	std::vector<Items::ItemInfo>      items;
	const D2RL::Inventory::ItemFilter filter {
		.structSize    = D2RL::Inventory::ItemFilterSize,
		.containerMask = Items::ContainerBit(Items::ItemContainer::CustomPage) | Items::ContainerBit(Items::ItemContainer::PersonalStash) | Items::ContainerBit(Items::ItemContainer::Inventory)
		               | Items::ContainerBit(Items::ItemContainer::Cube) | Items::ContainerBit(Items::ItemContainer::SharedStash) | Items::ContainerBit(Items::ItemContainer::Cursor),
	};
	(void)Inventory->forEachInventoryItem(Context, player, &filter, Collect, &items);
	for (const auto& item : items)
		if (item.runtimeId == id) return found = item, true;
	return false;
}

// The bytes the hosting game saved, as a snapshot (fingerprint as RecordItem finds it).
Snapshot SnapshotOf(const Items::ItemInfo& item, const PageHost::RequestAnswer& answer) {
	Snapshot snapshot { .itemLevel = item.itemLevel, .quality = static_cast<std::uint32_t>(item.quality) };
	snapshot.code = item.code;
	if (answer.bytes && answer.byteCount) snapshot.bytes.assign(answer.bytes, answer.bytes + answer.byteCount);
	snapshot.ok = !snapshot.bytes.empty();
	if (snapshot.ok) snapshot.fingerprint = FingerprintInBytes(snapshot.bytes, snapshot.itemLevel, snapshot.quality);
	if (snapshot.ok && !snapshot.fingerprint && snapshot.code && MiscByCode.count(CodeText(snapshot.code))) snapshot.fingerprint = SimplePrint(snapshot.code);
	return snapshot;
}

// Lock held. A record from saved bytes (RecordItem's, without the game thread).
bool RecordFromSnapshot(const Items::ItemInfo& item, int u, bool eth, Snapshot snapshot) {
	if (!snapshot.ok || !snapshot.fingerprint) return false;
	Record record { .id = Uniques[u].id, .eth = eth, .perfect = PerfectNow(u, item.runtimeId, eth), .fingerprint = snapshot.fingerprint, .code = item.code,
		            .quality = static_cast<std::uint32_t>(item.quality), .itemLevel = item.itemLevel, .stateFlags = item.stateFlags, .qualityRecordId = item.qualityRecordId,
		            .bytes = std::move(snapshot.bytes) };
	Fingerprints[item.runtimeId] = record.fingerprint;
	Tombstones.erase(record.fingerprint);
	FoundIds.insert(record.id);
	Records[record.fingerprint] = std::move(record);
	return true;
}

void RemoteSweep();
void RemoteRunewordRow(const Items::ItemInfo& item) {
	RemoteSave(item.runtimeId, [item](const PageHost::RequestAnswer& answer) {
		std::lock_guard guard(Lock);
		if (answer.result != PageHost::Result::Success) {
			RunewordRows.erase({ item.runtimeId, item.code });
			return;
		}
		RunewordFromSnapshot(item, SnapshotOf(item, answer));
		if (item.container == Items::ItemContainer::CustomPage) RemoteSweep(); // it was waiting on the page
	});
}

// A stored item out of the grail: made on a free page cell then onto the
// cursor, or (`toCursor` false) into the inventory.
void RemoteTake(std::uint32_t fingerprint, bool toCursor) {
	const auto player = LocalPlayer.load();
	Record     record;
	{
		std::lock_guard guard(Lock);
		if (const auto found = Records.find(fingerprint); found != Records.end()) record = found->second;
	}
	const auto finish = [] {
		TakeId = 0, Taking = false;
		RemoteRefresh();
	};
	const auto out = [fingerprint] {
		std::lock_guard guard(Lock);
		TakeOut(fingerprint);
		SaveStore();
		RebuildStored();
	};
	int x = 0, y = 0;
	if (!player || record.bytes.empty() || (toCursor && !FreeCell(player, record.code, x, y))) {
		Say(true, "grailon: joined game: no room on the page to take the stored item out");
		finish();
		return;
	}
	if (!toCursor) {
		RemoteCreate(record, Where(Items::ItemContainer::Inventory), [=](const PageHost::RequestAnswer& made) {
			if (Answered(made, "making the stored item in the inventory")) out();
			finish();
		});
		return;
	}
	RemoteCreate(record, CellDestination(x, y), [=](const PageHost::RequestAnswer& made) {
		if (!Answered(made, "making the stored item") || !made.runtimeId) return finish();
		{
			std::lock_guard guard(Lock);
			Busy.insert(made.runtimeId);
		}
		RemoteMove(made.runtimeId, Where(Items::ItemContainer::Cursor), [=](const PageHost::RequestAnswer& moved) {
			if (Answered(moved, "giving the stored item to the cursor")) {
				out();
				Unbusy(made.runtimeId);
				return finish();
			}
			// Still on the page: it goes again (the record keeps it).
			RemoteDestroy(made.runtimeId, [=](const PageHost::RequestAnswer&) {
				Unbusy(made.runtimeId);
				finish();
			});
		});
	});
}

// Item `id` (cursor, inventory, stash...) into the grail as entry `u`: onto
// a free page cell, saved by the hosting game, recorded, removed. A stored
// copy of the same tile comes out first: onto the cursor (`twinToCursor`)
// or into the inventory.
void RemotePut(std::uint32_t id, int u, bool eth, bool twinToCursor) {
	const auto      player = LocalPlayer.load();
	Items::ItemInfo item { .structSize = Items::ItemInfoSize };
	const auto      finish = [id] {
        Unbusy(id);
        HeldId = 0;
        RemoteRefresh();
	};
	{
		std::lock_guard guard(Lock);
		Busy.insert(id);
	}
	if (!player || !FindAnywhere(player, id, item)) return finish();
	// Then saved, recorded and removed (the stored twin out first).
	const auto store = [=](const Items::ItemInfo& onPage) {
		RemoteSave(id, [=](const PageHost::RequestAnswer& saved) {
			if (!Answered(saved, "saving the item")) return finish();
			auto snapshot = SnapshotOf(onPage, saved);
			int  unique   = u;
			if (unique < 0) { // a runeword not known before: from its bytes
				std::lock_guard guard(Lock);
				RunewordFromSnapshot(onPage, snapshot);
				unique = GrailIndex(onPage);
			}
			std::optional<Record> twin;
			{
				std::lock_guard guard(Lock);
				if (unique < 0 || !HasTile(unique, eth)) return finish();
				if (const auto* stored = RecordAt(unique, eth); stored && stored->fingerprint != snapshot.fingerprint) twin = *stored;
			}
			const auto keep = [=](Snapshot shot) {
				bool recorded = false;
				{
					std::lock_guard guard(Lock);
					recorded = RecordFromSnapshot(onPage, unique, eth, std::move(shot));
					if (recorded) SaveStore(), RebuildStored();
				}
				if (!recorded) {
					Say(true, "grailon: joined game: the item could not be stored (no identity in its bytes)");
					return RemoteMove(id, Where(Items::ItemContainer::Inventory), [=](const PageHost::RequestAnswer&) { finish(); });
				}
				RemoteDestroy(id, [=](const PageHost::RequestAnswer& gone) {
					if (!Answered(gone, "removing the stored item from the page")) {
						// Still on the page: not stored after all.
						std::lock_guard guard(Lock);
						for (auto it = Records.begin(); it != Records.end(); ++it)
							if (it->second.id == Uniques[unique].id && it->second.eth == eth) {
								Records.erase(it);
								break;
							}
						SaveStore(), RebuildStored();
					}
					finish();
				});
			};
			if (!twin) return keep(std::move(snapshot));
			// The stored twin out first (the cursor is free: the item is on the page).
			int tx = 0, ty = 0;
			const bool cursor = twinToCursor && FreeCell(player, twin->code, tx, ty);
			RemoteCreate(*twin, cursor ? CellDestination(tx, ty) : Where(Items::ItemContainer::Inventory), [=](const PageHost::RequestAnswer& made) {
				if (!Answered(made, "making the stored copy")) {
					// The copy stays stored; the item goes back to the inventory.
					return RemoteMove(id, Where(Items::ItemContainer::Inventory), [=](const PageHost::RequestAnswer&) { finish(); });
				}
				{
					std::lock_guard guard(Lock);
					TakeOut(twin->fingerprint);
					if (cursor) Busy.insert(made.runtimeId);
				}
				if (cursor)
					RemoteMove(made.runtimeId, Where(Items::ItemContainer::Cursor), [=](const PageHost::RequestAnswer& moved) {
						if (!Answered(moved, "giving the stored copy to the cursor"))
							RemoteMove(made.runtimeId, Where(Items::ItemContainer::Inventory), [](const PageHost::RequestAnswer&) {});
						Unbusy(made.runtimeId);
					});
				keep(snapshot);
			});
		});
	};
	if (item.container == Items::ItemContainer::CustomPage) return store(item);
	int x = 0, y = 0;
	if (!FreeCell(player, item.code, x, y)) {
		Say(true, "grailon: joined game: no room on the page for the item");
		return finish();
	}
	RemoteMove(id, CellDestination(x, y), [=](const PageHost::RequestAnswer& moved) {
		if (!Answered(moved, "putting the item on the page")) return finish();
		Items::ItemInfo onPage = item;
		onPage.container       = Items::ItemContainer::CustomPage, onPage.x = x, onPage.y = y;
		store(onPage);
	});
}

// The page swept: grail items there are stored, the rest goes to the inventory.
void RemoteSweep() {
	const auto player = LocalPlayer.load();
	if (!player) return;
	bool ready = false;
	{
		std::lock_guard guard(Lock);
		ready = StoreReady && !StorePath.empty() && StoreOwner == player;
	}
	if (!ready) return;
	std::vector<Items::ItemInfo>      items;
	const D2RL::Inventory::ItemFilter filter { .structSize = D2RL::Inventory::ItemFilterSize, .containerMask = Items::ContainerBit(Items::ItemContainer::CustomPage) };
	(void)Inventory->forEachInventoryItem(Context, player, &filter, Collect, &items);
	for (const auto& item : items) {
		if (item.container != Items::ItemContainer::CustomPage) continue; // outside the grail's area
		int  u    = -1;
		bool busy = false;
		{
			std::lock_guard guard(Lock);
			busy = Busy.count(item.runtimeId) != 0;
			if (!busy) u = GrailIndex(item);
		}
		if (busy || u == -2) continue; // a runeword not known yet: the next sweep
		const bool eth = IsEth(item);
		bool       take = false;
		{
			std::lock_guard guard(Lock);
			take = u >= 0 && HasTile(u, eth);
		}
		if (take) RemotePut(item.runtimeId, u, eth, false);
		else {
			{
				std::lock_guard guard(Lock);
				Busy.insert(item.runtimeId);
			}
			const auto id = item.runtimeId;
			RemoteMove(id, Where(Items::ItemContainer::Inventory), [id](const PageHost::RequestAnswer& moved) {
				(void)Answered(moved, "moving a page item to the inventory");
				Unbusy(id);
			});
		}
	}
}

// The arrange of a joined game: the page swept, the dropped item stored, the view opened.
bool RemoteArrange(ArrangeJob* job) {
	std::unique_ptr<ArrangeJob> owned(job);
	if (job->drop.wanted && job->open) {
		std::lock_guard guard(Lock);
		LateDrop = job->drop; // placed once the view is open
	} else if (job->drop.wanted) RemotePut(job->drop.runtimeId, job->drop.unique, job->drop.eth, job->drop.cursor);
	RemoteSweep();
	{
		std::lock_guard guard(Lock);
		RebuildStored();
	}
	ArrangeAgain   = 0;
	ArrangeRunning = false;
	if (job->open && Threads->runOnUiThread(Context, OpenPreparedOnUi, reinterpret_cast<void*>(static_cast<std::uintptr_t>(job->sequence))) != D2RL::Threads::Result::Success) {
		Say(false, "grailon: joined game: the view opens at once");
		OpenPrepared(job->sequence); // already on the UI thread
	}
	return true;
}

// The cursor jobs of a joined game.
bool RemoteCursorJob(CursorJob* job) {
	std::unique_ptr<CursorJob> owned(job);
	switch (job->step) {
	case CursorStep::TakeCell: RemoteTake(job->id, true); return true;
	case CursorStep::CtrlOut: RemoteTake(job->id, false); return true;
	case CursorStep::PutPage: HeldId = job->id, RemotePut(job->id, job->unique, job->eth, false); return true;
	case CursorStep::SwapPut: HeldId = job->id, RemotePut(job->id, job->unique, job->eth, true); return true;
	case CursorStep::SwapIn: RemotePut(job->id, job->unique, job->eth, false); return true;
	default: return false; // keeping the cursor item aside: not needed, the cursor is the game's
	}
}

// Lock held. Takes a Ctrl+clicked grail item: it goes from where it is
// straight into its tile (Queue("drop") runs RunDrop).
bool QueueCursorJob(CursorJob* job);
// Lock held. A grail item whose tile holds a stored copy: swapped with it.
bool QueueSwap(const Items::ItemInfo& item, int u, bool cursor) {
	const auto twin = StoreReady && !StorePath.empty() ? RecordAt(u, IsEth(item)) : nullptr;
	if (!twin || Materialized.count(item.runtimeId) || Busy.count(item.runtimeId)) return false; // no copy, or the item is the copy
	Busy.insert(item.runtimeId);
	if (!QueueCursorJob(new CursorJob { .step = cursor ? CursorStep::SwapPut : CursorStep::SwapIn, .id = item.runtimeId, .cursor = cursor, .unique = u, .eth = IsEth(item) })) {
		Busy.erase(item.runtimeId);
		return false;
	}
	return true;
}

bool AssignBase(const Items::ItemInfo& item, bool hovered);
bool TakeCtrlClick(const Items::ItemInfo& item) {
	(void)AssignBase(item, false);
	const int u = GrailIndex(item);
	if (u < 0 || !HasTile(u, IsEth(item))) return false;
	if (QueueSwap(item, u, false)) return true;
	const auto    twin    = Stored.find({ u, IsEth(item) });
	std::uint32_t swapOut = twin != Stored.end() && twin->second != item.runtimeId ? twin->second : 0;
	int           width = 1, height = 1;
	(void)ItemSize(CodeText(item.code), width, height);
	PendingDrop = { true, false, u, IsEth(item), item.runtimeId, width, height, swapOut, item.container, item.code, BaseFor.count(item.runtimeId) != 0 };
	if (!TileShown(u, IsEth(item))) JumpTo = u, JumpEth = IsEth(item);
	return true;
}

// The item the click that took it out of its tile is on (its tile shows empty
// from that click on).
std::atomic<std::uint32_t> TakenNow {};

// Lock held. Item `id`, leaving its tile: the tile shows empty from now on
// (the next arrange removes its record). Whether a tile held it.
bool PredictTakeOut(std::uint32_t id) {
	if (!id) return false;
	for (auto it = Stored.begin(); it != Stored.end(); ++it)
		if (it->second == id) {
			const auto key = it->first;
			Stored.erase(it);
			StoredCodes.erase(key);
			Perfect.erase(key);
			BaseTiles.erase(key);
			BaseFor.erase(id);
			return true;
		}
	return false;
}

auto __cdecl OnPagePolicy(const D2RL::PluginContext* context, const D2RL::Inventory::ItemPolicyRequest* request, void*) noexcept -> D2RL::Inventory::ItemPolicyDecision {
	using Decision = D2RL::Inventory::ItemPolicyDecision;
	if (!request || request->reason != D2RL::Inventory::ItemPlacementReason::LocalMove || PluginMoving || ScrollTestOpen) return Decision::Allow;
	if (!ListsReady) return Decision::Reject;
	D2RL::ItemHandle handle {};
	Items::ItemInfo  item { .structSize = Items::ItemInfoSize };
	const auto       player = LocalPlayer.load();
	const bool       click  = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	const bool       ctrl   = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
	// Without the cursor item there is nothing to judge: the next arrange sends
	// a wrong item back.
	if (!player || Inventory->getCursorItem(context, player, &handle) != D2RL::Inventory::Result::Success || !handle
	    || GetInfo(context, handle, &item) != Items::Result::Success) {
		// A Ctrl+click carries no cursor item: it is on the hovered item.
		if (const auto hovered = HoveredItem.load(); click && ctrl && player && hovered && GetTickCount64() - HoveredAt.load() <= HoverFreshMs
		    && GetInfo(context, hovered, &item) == Items::Result::Success && item.container != Items::ItemContainer::CustomPage) {
			bool taken = false;
			{
				std::lock_guard guard(Lock);
				taken = TakeCtrlClick(item);
			}
			if (!taken) return Decision::Reject;
			Queue("drop");
			return Decision::Reject;
		}
		if (click && player) ArrangeSoon(player, 0);
		return Decision::Allow;
	}
	if (!click) {
		HoveredWith = item.runtimeId;
		if (TakenNow == item.runtimeId) TakenNow = 0; // that click is over
	}
	bool jump = false, refresh = false;
	{
		std::lock_guard guard(Lock);
		if (click) (void)AssignBase(item, true);
		if (const int u = GrailIndex(item); click && !ctrl && u >= 0 && HasTile(u, IsEth(item)) && QueueSwap(item, u, true)) {
			Trace("policy swap", static_cast<int>(item.runtimeId));
			return Decision::Reject;
		}
		if (!Accepts(item)) {
			if (click) Trace("policy refused", static_cast<int>(item.runtimeId));
			return Decision::Reject;
		}
		if (!click) return Decision::Allow;
		const int  u      = GrailIndex(item);
		const auto stored = Stored.find({ u, IsEth(item) });
		// The click that just took this item out of its tile: it stays on the
		// cursor, and its tile shows empty at once (the record goes on the next
		// arrange). Putting it back takes a new click.
		if (!ctrl && ((stored != Stored.end() && stored->second == item.runtimeId) || TakenNow == item.runtimeId) && HoveredWith != item.runtimeId) {
			if (TakenNow.exchange(item.runtimeId) != item.runtimeId) refresh = PredictTakeOut(item.runtimeId);
			if (!refresh) {
				Trace("policy waits for a new click", static_cast<int>(item.runtimeId));
				return Decision::Reject;
			}
		} else {
			int width = 1, height = 1;
			(void)ItemSize(CodeText(item.code), width, height);
			PendingDrop = { true, !ctrl, u, IsEth(item), item.runtimeId, width, height, 0, {}, item.code, BaseFor.count(item.runtimeId) != 0 };
			jump        = !TileShown(u, IsEth(item));
			if (jump) JumpTo = u, JumpEth = IsEth(item);
		}
	}
	Trace(refresh ? "policy take" : "policy drop", static_cast<int>(item.runtimeId));
	Queue(refresh ? "refresh" : "drop");
	return Decision::Reject;
}

// UI thread: puts the dropped item in its tile, showing the tile first.
void PredictDrop(const Drop& drop);
void RunDrop() {
	Drop drop;
	bool jump = false;
	{
		std::lock_guard guard(Lock);
		drop               = PendingDrop;
		PendingDrop.wanted = false;
		jump               = JumpTo >= 0 || CurrentCategory < 0;
		if (CurrentCategory < 0 && drop.wanted) JumpTo = drop.unique, JumpEth = drop.eth;
	}
	if (!drop.wanted) return;
	{
		// Shown as it will be (found, stored, perfect or not) before the item
		// goes in, and the item put in as that view opens: both at once, and
		// one view instead of one per change.
		std::lock_guard guard(Lock);
		if (!drop.base && Present.insert(drop.unique).second) jump = true;
		PredictDrop(drop);
		if (ShownView < 0 || Views[ShownView].key != ViewKey()) jump = true;
	}
	if (jump) {
		ShowView(drop);
		return;
	}
	if (const auto player = LocalPlayer.load()) (void)QueueArrange(new ArrangeJob { .player = player, .drop = drop });
}

// UI thread: a Ctrl+click on a grail item while the view is open is the
// plugin's: the game does not move it, the plugin puts it in its tile.
auto __cdecl OnItemClick(const D2RL::PluginContext* context, const D2RL::ItemInteractions::ItemInteractionEvent* event, void*) noexcept -> D2RL::ItemInteractions::Decision {
	using Decision = D2RL::ItemInteractions::Decision;
	const bool ctrl = event && ((event->modifiers & D2RL::ItemInteractions::ModifierBit(D2RL::ItemInteractions::Modifier::Control)) || (GetAsyncKeyState(VK_CONTROL) & 0x8000));
	// Ctrl+click on a grail item: it goes to the inventory, its tile shows empty at once.
	if (event && ctrl && event->action == D2RL::ItemInteractions::Action::Activate && event->container == Items::ItemContainer::CustomPage && ViewIsOpen && ListsReady && !ScrollTestOpen) {
		Items::ItemInfo item { .structSize = Items::ItemInfoSize };
		bool            refresh = false;
		if (GetInfo(context, event->item, &item) == Items::Result::Success) {
			std::lock_guard guard(Lock);
			refresh = PredictTakeOut(item.runtimeId);
		}
		if (refresh) Queue("refresh");
		return Decision::Continue;
	}
	if (!event || !ctrl || event->action != D2RL::ItemInteractions::Action::Activate || event->container == Items::ItemContainer::CustomPage || !ViewIsOpen || !ListsReady
	    || ScrollTestOpen || (GetAsyncKeyState(VK_RBUTTON) & 0x8000))
		return Decision::Continue;
	Items::ItemInfo item { .structSize = Items::ItemInfoSize };
	if (GetInfo(context, event->item, &item) != Items::Result::Success) return Decision::Continue;
	{
		std::lock_guard guard(Lock);
		if (!TakeCtrlClick(item)) return Decision::Continue; // not for the grail: the page refuses it anyway
	}
	Queue("drop");
	return Decision::Consume;
}

std::atomic<D2RL::ItemHandle> ShowItemWanted {};
std::atomic<std::uint32_t>    GridHoverId {}; // the unit id of the item a grid shows the tooltip of (HookShowTooltip)
std::atomic<std::uint64_t>    GridHoverAt {};
// Whether a tooltip event's item is the one under the mouse, not a text the plugin built.
bool RealHover(const Items::ItemInfo& item) { return GetTickCount64() - GridHoverAt.load() > HoverFreshMs || item.runtimeId == GridHoverId.load(); }
std::atomic<std::uint64_t>    MiddleSeenAt {}; // the window's last middle click on a grail item
// The grail entry of the item a grid shows the tooltip of (HookShowTooltip),
// -1 when it has no tile: the middle click's second source, for when the
// tooltip events carry the grail copies' texts (the view open).
std::atomic<int>              HoverEntry { -1 };
std::atomic<bool>             HoverEntryEth {};
std::atomic<int>              ShowEntryWanted { -1 };
std::atomic<bool>             ShowEntryEth {};
bool HoverEntryFresh() { return HoverEntry.load() >= 0 && GetTickCount64() - GridHoverAt.load() <= HoverFreshMs; }
bool ShowsInGrail(D2RL::ItemHandle handle);
// Any thread: a middle click now. Whether it was a grail item's.
bool MiddleClickNow(const char* from) {
	const auto hovered = HoveredItem.load();
	if (hovered && GetTickCount64() - HoveredAt.load() <= HoverFreshMs && ShowsInGrail(hovered)) {
		MiddleSeenAt   = GetTickCount64();
		ShowItemWanted = hovered;
		Queue("showitem");
		return true;
	}
	if (HoverEntryFresh()) {
		MiddleSeenAt    = GetTickCount64();
		ShowEntryEth    = HoverEntryEth.load();
		ShowEntryWanted = HoverEntry.load();
		Queue("showitem");
		return true;
	}
	(void)from;
	return false;
}

// UI thread, each frame: a middle click the window did not pass on (a panel
// open over the game may take it) is seen by the button's state.
bool GameInFront();
bool ShowsInGrail(D2RL::ItemHandle handle);
void WatchMiddleClick() {
	static bool down = false;
	const bool  now  = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
	const bool  press = now && !down;
	down              = now;
	if (!press || !Config.middleClick || !ListsReady || ScrollTestOpen || !GameInFront()) return;
	if (GetTickCount64() - MiddleSeenAt.load() < 500) return; // the window had it
	(void)MiddleClickNow("polled");
}

// A grail item with a tile: a middle click shows it.
bool ShowsInGrail(D2RL::ItemHandle handle) {
	Items::ItemInfo item { .structSize = Items::ItemInfoSize };
	if (!ItemApi || !ListsReady || GetInfo(Context, handle, &item) != Items::Result::Success) return false;
	std::lock_guard guard(Lock);
	const int u = GrailIndex(item);
	return u >= 0 && HasTile(u, IsEth(item));
}

// UI thread: the tooltip's action line for grail items.
// The grail's comparison (the view open) shows a tooltip without the action
// footer: the line goes under the description then, on the hovered item's
// side only. `userData` is 1 for the description's listener.
std::atomic<std::uint64_t> ForcedCompareAt {}; // HookShowTooltip: the last tooltip shown with the grail copy beside it
void __cdecl OnTooltipFooter(const D2RL::PluginContext*, Events::ItemTooltipEvent* event, void* userData) noexcept {
	if (!event || !event->text || event->capacity < 2 || !Config.middleClick) return;
	Items::ItemInfo item { .structSize = Items::ItemInfoSize };
	const bool      known    = ItemApi && GetInfo(Context, event->item, &item) == Items::Result::Success;
	const bool      compared = GetTickCount64() - ForcedCompareAt.load() <= HoverFreshMs;
	bool            real     = false;
	if (known && ShowsInGrail(event->item)) {
		real = RealHover(item);
		if (!real && HoverEntryFresh()) { // the view open: the hovered item's entry
			std::lock_guard guard(Lock);
			real = GrailIndex(item) == HoverEntry.load() && IsEth(item) == HoverEntryEth.load();
		}
	}
	if (userData) real = real && compared;      // under the description: only while compared (no footer then)
	else if (compared) real = false;            // no footer drawn: the description's listener has it
	if (!real) return;
	const std::string line = Tr("Middle click to show in Grail On!");
	const auto        size = std::min<std::size_t>(line.size(), event->capacity - 1);
	std::memcpy(event->text, line.data(), size);
	event->text[size] = 0;
	event->length     = static_cast<std::uint32_t>(size);
}

// UI thread: remembers the hovered item (nothing is added to the tooltip).
void __cdecl OnTooltip(const D2RL::PluginContext*, Events::ItemTooltipEvent* event, void*) noexcept {
	if (!event) return;
	Items::ItemInfo item { .structSize = Items::ItemInfoSize };
	const bool      known = ItemApi && GetInfo(Context, event->item, &item) == Items::Result::Success;
	if (known && !RealHover(item)) return; // a grail copy's text, built by the plugin
	HoveredItem = event->item;
	HoveredAt   = GetTickCount64();
	// A hovered normal or superior item's runeword is read now, so the click
	// that takes it into the grail already knows it.
	if (known && ListsReady && (item.quality == Items::Quality::Normal || item.quality == Items::Quality::Superior)) {
		std::lock_guard guard(Lock);
		(void)RunewordRowOf(item);
	}
}

// ---- table dump (game thread) ----
// Writes the raw compiled rows of the tables the lists come from, next to the
// plugin's log, to check their layout on a new game build (`grailon dump`).

std::atomic<bool> DumpWanted {};

void __cdecl Dump(const D2RL::PluginContext* context, void*) noexcept {
	std::wstring folder = context->pluginLogPath ? context->pluginLogPath : L"";
	folder              = folder.substr(0, folder.find_last_of(L"\\/") + 1);
	const auto open     = [&](const wchar_t* name, const wchar_t* mode) { return _wfopen((folder + name).c_str(), mode); };
	FILE*      report   = open(L"grail-on-dump.txt", L"w");
	if (!report) {
		Say(true, "grailon: could not write the dump next to the log");
		return;
	}
	std::fprintf(report, "build %s, mod %s, data table service %s, localization service %s\n", context->buildVersion ? context->buildVersion : "?",
	             context->activeMod ? context->activeMod : "?", TableApi ? "yes" : "no", TextApi ? "yes" : "no");
	if (context->modDirectory) std::fprintf(report, "mod directory %ls\n", context->modDirectory);
	const struct {
		Tables::TableId id;
		const wchar_t*  file;
		const char*     name;
	} tables[] {
		{ Tables::TableId::UniqueItems, L"grail-on-uniqueitems.bin", "UniqueItems" },
		{ Tables::TableId::SetItems, L"grail-on-setitems.bin", "SetItems" },
		{ Tables::TableId::Items, L"grail-on-items.bin", "Items" },
		{ Tables::TableId::ItemTypes, L"grail-on-itemtypes.bin", "ItemTypes" },
		{ Tables::TableId::Properties, L"grail-on-properties.bin", "Properties" },
		{ Tables::TableId::Sets, L"grail-on-sets.bin", "Sets" },
		{ Tables::TableId::Skills, L"grail-on-skills.bin", "Skills" },
		{ Tables::TableId::Runes, L"grail-on-runes.bin", "Runes" },
	};
	for (const auto& table : tables) {
		if (!TableApi) break;
		Tables::TableView view { .structSize = Tables::TableViewSize };
		const auto        result = TableApi->getTable(context, Tables::Bank::Rotw, table.id, &view);
		std::fprintf(report, "%s: result %u, %u rows of %u bytes\n", table.name, static_cast<unsigned>(result), view.rowCount, view.rowSize);
		if (result != Tables::Result::Success || !view.rows) continue;
		if (FILE* out = open(table.file, L"wb")) {
			std::fwrite(view.rows, view.rowSize, view.rowCount, out);
			std::fclose(out);
		}
	}
	std::fprintf(report, "lists: %d uniques in %d categories\n", UniqueCount(), CategoryCount());
	std::fclose(report);
	Say(false, "grailon: table dump written to %ls", folder.c_str());
}

// ---- comparison with the grail copy (native: D2R 3.3.93787 / 3.3.94995) ----
// While the view is open, hovering a grail item somewhere else compares it with
// its copy on the page (the same ethereal state first, else the other):
//  - the game's Shift comparison shows the copy, without Shift. The grid's
//    tooltip (ShowTooltip) is wrapped to force it, and the comparison text
//    D2RCore builds for an equipped item is swapped for the copy's;
// A stored item whose variable rolls are all the best gets a gold frame that
// shimmers (Glow sprites, one per tile size, frames shown in turn).
// The swaps are pointer and call changes, so D2RCore's own tooltip hooks (and
// Item Roll Ranges on top of them) keep working under them.

namespace Game {
constexpr std::uint64_t  ShowTooltip = 0x2C3C30; // InventoryGridWidget: the tooltip of its hovered item (widget, item)
constexpr std::uint8_t   ShowTooltipBytes[] { 0x40, 0x55, 0x53, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0xC0, 0xFD, 0xFF, 0xFF, 0x48, 0x81, 0xEC, 0x40, 0x03, 0x00, 0x00 };
constexpr std::uint64_t  TooltipSlot = 0x3E2AF58; // D2RCore thunk pointer: an item's tooltip text (D2RCore BuildItemTooltipWithStatRanges)
constexpr std::uint64_t  CompareSlot = 0x3E2AF60; // the same for the equipped item at a body location (BuildComparedItemTooltipWithStatRanges)
constexpr std::uint64_t  LineLoop    = 0x2DA760;  // stat line loop (D2RCore replaces it with a jump)
constexpr std::uint64_t  ResolveSlot = 0x3E2AE18; // D2RCore thunk pointer: a string key's text (ResolveNamespacedStringKey)
constexpr std::uint64_t  LineCat     = 0x2DACBE;  // stat line loop: strcat_s(text, size, line), r14d = stat, rbp = the loop's frame
constexpr std::uint8_t   LineCatBytes[] { 0xE8, 0xDD, 0xC9, 0xF4, 0x00 };
constexpr std::uint64_t  Strcat = 0x12276A0, GetUnit = 0x9A5D0, PlayerIndex = 0x8B2D0, Player = 0x9A480, Reserve = 0x76210;
constexpr std::uintptr_t Compare = 0x631, AlwaysCompare = 0x632; // InventoryGridWidget: comparison on, and without Shift
constexpr std::uintptr_t UnitId = 0x08, UnitData = 0x10, DataQuality = 0x00, DataFlags = 0x18, DataFile = 0x34;
constexpr std::uint32_t  Ethereal = 0x400000, RunewordFlag = 0x4000000;
constexpr int            ItemUnit = 4, UniqueQuality = 7, SetQuality = 5;
// The stat line loop's frame: the entry index, the entry count, and the
// entries of the current stat {u16 layer, u16, i32 value}.
constexpr std::ptrdiff_t LineIndex = -0x80, LineCount = -0x68, LineEntries = 0x140;
} // namespace Game

struct GameString {
	char*         text;
	std::uint64_t size;
	std::uint64_t capacity; // top bit: the text is in the inline buffer below
	char          buffer[16];
};
using TooltipFn     = GameString* (*)(GameString*, void*, void*, void*, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, void*) noexcept;
using CompareFn     = GameString* (*)(GameString*, int) noexcept;
using ShowTooltipFn = void (*)(void* widget, void* item) noexcept;
using StrcatFn      = int (*)(char*, std::uint64_t, const char*) noexcept;
using GetUnitFn     = void* (*)(std::uint32_t id, int type) noexcept;
using IndexFn       = std::uint32_t (*)() noexcept;
using PlayerFn      = void* (*)(std::uint32_t) noexcept;
using ReserveFn     = void (*)(GameString*, std::uint64_t) noexcept;
using StatFn        = int (*)(void* unit, int stat, int layer) noexcept; // D2RCore ReadWideUnitStat

std::uintptr_t Base {};
TooltipFn      Tooltip {};
CompareFn      Compared {};
ShowTooltipFn  ShowTooltip {};
StrcatFn       Strcat {};
StatFn         ReadStat {};

constexpr int SocketsStat = 194; // item_numsockets

// A variable roll of a unique or set item: the stats it sets (the line shows
// one of them), their layer, and its worst and best value.
struct Roll {
	int stats[4] { -1, -1, -1, -1 };
	int layer {}, worst {}, best {};
};
std::vector<std::vector<Roll>> Rolls; // by index in Uniques; guarded by Lock
std::vector<bool>              EdRolls; // the item has a variable Enhanced Defense roll
// The best defense stat (31) a copy can have, plain and ethereal: the base's
// maxac (+1 with Enhanced Defense, as the game rolls it), x1.5 when ethereal,
// plus the best flat defense. 0 when it has no defense. By index in Uniques.
std::vector<std::pair<int, int>> BestDefense;

// Lock held. The objectives' entries, after every other (their indices are the
// last, so the others' never change), in their own category of the Objectives menu.
void BuildObjectives() {
	while (!Uniques.empty() && Uniques.back().objective) {
		EntryById.erase(Uniques.back().id);
		Uniques.pop_back();
	}
	ObjCategory = -1;
	for (int c = 0; c < CategoryCount(); ++c)
		if (Categories[c].menu == ObjMenu) ObjCategory = c;
	if (ObjCategory < 0) {
		CategoryEntry category { ObjMenu, "Objectives", "Objectives" };
		category.icon = "d2rloader\\grail-on\\icon-objectives";
		ObjCategory   = CategoryCount();
		Categories.push_back(category);
	}
	for (const auto& objective : Objectives) {
		if (!Config.objectives) break; // off: the list is kept, no tiles
		if (!objective.enabled) continue;
		UniqueEntry entry;
		entry.category  = ObjCategory;
		entry.row       = -1;
		entry.misc      = true;
		entry.objective = true;
		entry.id        = "o:" + std::to_string(objective.id);
		entry.kind      = 2;
		entry.width = entry.height = 1;
		entry.name = entry.base = objective.text;
		entry.ghost = entry.art = "d2rloader\\grail-on\\objtile" + std::to_string(std::clamp(objective.icon, 0, ObjectiveIconCount - 1));
		Uniques.push_back(entry);
		EntryById[entry.id] = UniqueCount() - 1;
	}
	// What is kept per entry (their rolls: none).
	if (!Rolls.empty()) Rolls.resize(Uniques.size()), EdRolls.resize(Uniques.size()), BestDefense.resize(Uniques.size());
	UpdateMiscTracking();
}

std::atomic<bool> RollsReady {};
std::string       DefenseLabel; // "Defense:", from the game's strings
std::string       EdLabel;      // "Enhanced Damage", from the game's strings
std::string       EdefLabel;    // "Enhanced Defense", from the game's strings
std::string       SocketLabel;  // "Socketed", from the game's strings
std::vector<std::string> LineWords(std::string_view line);

// ---- rolls from the tables (game thread) ----

// Which props of a row are rolls, as the grail tracker's perfect rolls count
// them (tools/genrolls.py): min != max, and not a fixed-range func.
void AddRolls(std::vector<Roll>& out, bool& ed, const unsigned char* row, int first, int count, const unsigned char* props, std::uint32_t propCount, std::uint32_t propSize,
              const PropLayout& layout) {
	constexpr int AcPercent = 16; // item_armor_percent
	for (int p = 0; p < count; ++p) {
		const int at = first + p * 16;
		const int prop = Field<std::int32_t>(row, at), par = Field<std::int32_t>(row, at + 4), low = Field<std::int32_t>(row, at + 8), high = Field<std::int32_t>(row, at + 12);
		if (prop < 0 || static_cast<std::uint32_t>(prop) >= propCount || low == high) continue;
		const unsigned char* text = props + static_cast<std::size_t>(prop) * propSize;
		const int            func = text[layout.func];
		if (func == 11 || func == 12 || func == 15 || func == 16 || func == 19 || func == 36 || (func == 17 && par)) continue;
		Roll roll { .worst = low, .best = high };
		// Min/max damage: the line can come from the one- or two-handed or the throw stat.
		if (func == 5) roll.stats[0] = 21, roll.stats[1] = 23, roll.stats[2] = 159;
		else if (func == 6) roll.stats[0] = 22, roll.stats[1] = 24, roll.stats[2] = 160;
		else if (func == 7) roll.stats[0] = 17, roll.stats[1] = 18; // item_maxdamage_percent, item_mindamage_percent
		else
			for (int s = 0; s < 4; ++s) {
				const int stat = Field<std::uint16_t>(text, layout.stat + 2 * s);
				if (stat != 0xFFFF && (s == 0 || text[layout.func + s])) roll.stats[s] = stat;
			}
		if (roll.stats[0] < 0) continue;
		roll.layer = func == 21 ? Field<std::uint16_t>(text, layout.value) : func == 22 || func == 10 ? par : 0;
		if (roll.stats[0] == AcPercent) ed = true;
		out.push_back(roll);
	}
}

void BuildRolls(const D2RL::PluginContext* context) {
	Tables::TableView props { .structSize = Tables::TableViewSize }, uniques { .structSize = Tables::TableViewSize }, sets { .structSize = Tables::TableViewSize },
	    runes { .structSize = Tables::TableViewSize };
	if (TableApi) (void)TableApi->getTable(context, Tables::Bank::Rotw, Tables::TableId::Runes, &runes);
	if (!TableApi || TableApi->getTable(context, Tables::Bank::Rotw, Tables::TableId::Properties, &props) != Tables::Result::Success
	    || TableApi->getTable(context, Tables::Bank::Rotw, Tables::TableId::UniqueItems, &uniques) != Tables::Result::Success
	    || TableApi->getTable(context, Tables::Bank::Rotw, Tables::TableId::SetItems, &sets) != Tables::Result::Success) {
		Problem(false, "grailon: no Properties table, so no perfect check");
		return;
	}
	const auto* rows   = static_cast<const unsigned char*>(props.rows);
	const auto  layout = FindPropLayout(rows, props.rowCount, props.rowSize);
	if (layout.func < 0 || layout.stat < 0 || layout.value < 0) {
		Problem(false, "grailon: Properties rows (%u of %u bytes) are laid out differently; no perfect check", props.rowCount, props.rowSize);
		return;
	}
	std::lock_guard guard(Lock);
	Rolls.assign(Uniques.size(), {});
	EdRolls.assign(Uniques.size(), false);
	BestDefense.assign(Uniques.size(), {});
	for (std::size_t u = 0; u < Uniques.size(); ++u) {
		const auto& entry = Uniques[u];
		const auto& table = entry.runeword ? runes : entry.set ? sets : uniques;
		if (entry.row < 0 || static_cast<std::uint32_t>(entry.row) >= table.rowCount || !table.rows) continue;
		const auto* row = static_cast<const unsigned char*>(table.rows) + static_cast<std::size_t>(entry.row) * table.rowSize;
		bool        ed  = false;
		if (entry.runeword) {
			// Its base's defense is not known: only the runeword's own rolls count.
			if (RuneFields.props >= 0) AddRolls(Rolls[u], ed, row, RuneFields.props, RuneLayout::PropCount, rows, props.rowCount, props.rowSize, layout);
			EdRolls[u] = ed;
			continue;
		}
		AddRolls(Rolls[u], ed, row, 152, entry.set ? 9 : 12, rows, props.rowCount, props.rowSize, layout);
		EdRolls[u] = ed;
		// Sockets: never more than the base can have (Aldur's Rhythm rolls 2-5
		// on a Jagged Star, which takes 3).
		if (const auto base = Bases.find(entry.code); base != Bases.end() && base->second.sockets > 0)
			for (auto& roll : Rolls[u])
				if (roll.stats[0] == SocketsStat) roll.best = std::min(roll.best, base->second.sockets), roll.worst = std::min(roll.worst, roll.best);
		// Defense: Enhanced Defense (stat 16) at any value, flat defense (31) at its best.
		bool anyEd = false;
		int  flat  = 0;
		for (int p = 0; p < (entry.set ? 9 : 12); ++p) {
			const int prop = Field<std::int32_t>(row, 152 + p * 16), high = Field<std::int32_t>(row, 152 + p * 16 + 12);
			if (prop < 0 || static_cast<std::uint32_t>(prop) >= props.rowCount) continue;
			const int stat = Field<std::uint16_t>(rows + static_cast<std::size_t>(prop) * props.rowSize, layout.stat);
			if (stat == 16 && high > 0) anyEd = true;
			if (stat == 31) flat += high;
		}
		if (const auto base = Bases.find(entry.code); base != Bases.end() && base->second.maxDefense > 0) {
			const int top  = base->second.maxDefense + (anyEd ? 1 : 0);
			BestDefense[u] = { top + flat, top * 3 / 2 + flat };
		}
	}
	// "Defense: %d" from the game's strings, up to the colon; English when the
	// key is not found.
	for (const char* key : { "ItemStats1h", "strItemStats1h" }) {
		char label[64] {};
		if (!TextApi || TextApi->getStringByKey(context, key, label, sizeof(label), nullptr) != D2RL::Localization::Result::Success) continue;
		std::string text = label;
		if (const auto colon = text.find(':'); colon != std::string::npos) text.resize(colon + 1);
		if (!text.empty() && text.find('%') == std::string::npos) {
			DefenseLabel = text;
			break;
		}
	}
	if (DefenseLabel.empty()) DefenseLabel = "Defense:";
	// "+%d%% Enhanced Damage" and "+%d%% Enhanced Defense": their words,
	// without the format codes (%d, %+d, %%).
	const auto words = [&](std::initializer_list<const char*> keys, const char* fallback) {
		for (const char* key : keys) {
			char label[96] {};
			if (!TextApi || TextApi->getStringByKey(context, key, label, sizeof(label), nullptr) != D2RL::Localization::Result::Success) continue;
			std::string text;
			for (const char* c = label; *c; ++c) {
				if (*c != '%') {
					text += *c;
					continue;
				}
				++c;
				while (*c == '+' || *c == '-' || std::isdigit(static_cast<unsigned char>(*c))) ++c;
				if (!*c) break;
			}
			if (!LineWords(text).empty()) return text;
		}
		return std::string(fallback);
	};
	EdLabel   = words({ "strModEnhancedDamage", "ModStr2a" }, "Enhanced Damage");
	EdefLabel = words({ "Modstr2v", "ModStr2v" }, "Enhanced Defense");
	SocketLabel = words({ "Socketable" }, "Socketed");
	RollsReady = true;
}

// ---- items (UI thread, native) ----

template <typename T>
T At(const void* base, std::ptrdiff_t offset) {
	T value {};
	if (base) std::memcpy(&value, static_cast<const unsigned char*>(base) + offset, sizeof(T));
	return value;
}



// The grail entry and ethereal state of a native item, or -1. Lock held.
int EntryOf(const void* item, bool& eth) {
	const void* data = At<const void*>(item, Game::UnitData);
	if (!data || At<std::uint32_t>(item, 0) != Game::ItemUnit) return -1;
	const auto  quality = At<std::uint32_t>(data, Game::DataQuality);
	const auto  file    = At<std::int32_t>(data, Game::DataFile);
	const auto* byRow   = quality == Game::UniqueQuality ? &UniqueByRow : quality == Game::SetQuality ? &SetByRow : nullptr;
	eth                 = (At<std::uint32_t>(data, Game::DataFlags) & Game::Ethereal) != 0;
	// A runeword: known from when the plugin last saw the item (one tile, eth or not).
	if (const auto runeword = RunewordUnits.find(At<std::uint32_t>(item, Game::UnitId)); runeword != RunewordUnits.end() && (quality == 2 || quality == 3)) {
		eth = false;
		return runeword->second;
	}
	if (!byRow || file < 0 || file >= static_cast<int>(byRow->size())) return -1;
	return (*byRow)[file];
}

// The stored copy of an entry in the same ethereal state: an ethereal unique
// only compares with the ethereal tile's copy; an item with one tile for both
// (set items) with that one. Lock held.
void* CopyOf(int entry, bool eth) {
	if (!HasTile(entry, eth)) eth = !eth;
	for (const bool want : { eth }) {
		const auto stored = Stored.find({ entry, want });
		if (stored == Stored.end()) continue;
		if (void* unit = stored->second ? reinterpret_cast<GetUnitFn>(Base + Game::GetUnit)(stored->second, Game::ItemUnit) : nullptr) return unit;
		// Not on the page: its ghost (the next tooltip frame has it).
		if (const auto record = RecordAt(entry, want)) return GhostOf(record->fingerprint);
	}
	return nullptr;
}

HWND GameWindow {}; // the game's window (the wheel goes through it)

// UI thread (the host grid's draw). The mouse over a picture tile (past the
// host's rows) with its item stored: the game's tooltip for its copy, as the
// grid shows it for its own items.
void* LocalUnit();
GameString* EmptyText(GameString* out);
void* PeekShown {}; // UI thread: the copy whose tooltip PeekTooltip shows (the grid's own call for it is dropped)
int   HoveredPeek = -1; // UI thread: the picture tile under the mouse at the grid's last draw (index in Peeking), -1 for none

void PeekTooltip(void* widget) {
	PeekShown   = nullptr;
	HoveredPeek = -1;
	if (!GameWindow || !ViewIsOpen || Anim.running || !Context) return;
	POINT mouse {};
	if (!GetCursorPos(&mouse) || !ScreenToClient(GameWindow, &mouse)) return;
	const auto cellAt = [](int x, int y) { return static_cast<std::uint32_t>(x) | (std::uint64_t { static_cast<std::uint32_t>(y) } << 32); };
	const auto one    = cellAt(1, 1);
	void*         copy = nullptr;
	std::uint64_t cell = 0;
	{
		std::lock_guard guard(Lock);
		for (std::size_t i = 0; i < Peeking.size(); ++i) {
			const auto&  tile  = Peeking[i];
			const int    width = std::max(tile.width, SlotWidth), halves = PeekHalves(tile);
			if (halves <= 0) continue;
			std::int32_t from[4] {}, to[4] {};
			TileRect(widget, from, one, tile.x, tile.y);
			TileRect(widget, to, one, tile.x + width, tile.y + tile.height);
			const int bottom = from[1] + (to[1] - from[1]) * std::min(halves, tile.height * 2) / (tile.height * 2);
			if (mouse.x >= from[0] && mouse.x < to[0] && mouse.y >= from[1] && mouse.y < bottom) {
				HoveredPeek = static_cast<int>(i);
				if (!Stored.count({ tile.unique, tile.eth })) continue;
				copy = CopyOf(tile.unique, tile.eth);
				cell = cellAt(tile.x + (tile.width == 1 ? 1 : 0), tile.y); // grail cells (TileRect)
			}
		}
	}
	if (!ShowTooltip) return;
	// The grid reads an item's cell from its path (unit+0x38) unchecked.
	if (!copy || !*reinterpret_cast<void* const*>(static_cast<const std::uint8_t*>(copy) + 0x38) || CursorHasItem()) return;
	// The grid shows the text it keeps for its hovered item (widget+0x558, a
	// game string, built when the hover starts): the copy's text goes there
	// for the call, and the copy's place is its tile (PeekCell, CellRect hook).
	static GameString  text {};
	static bool        built = false;
	const auto freeText = [] {
		if (built && !(text.capacity & 0x8000000000000000ULL) && text.text) {
			using AllocFn = void* (*)() noexcept;
			using FreeFn  = void (*)(void*, void*) noexcept;
			void* allocator = reinterpret_cast<AllocFn>(Base + 0x12063C0)();
			if (allocator) reinterpret_cast<FreeFn>((*static_cast<void***>(allocator))[4])(allocator, text.text);
		}
		built = false;
	};
	// Built again every frame, as the game does for its hovered item: D2RCore
	// adds its "Hold Ctrl to Show Ranges" line only to the text it built last,
	// so a cached text got it only when something else had just built it.
	{
		freeText();
		// The arguments the grid's own hover passes (0x2C8BBC): its owner, its
		// +0x5D0, +0x5C0 and +0x633, so the text is the one it would build
		// (D2RCore's extra lines included) and both never alternate.
		using OwnerFn = void* (*)(void*) noexcept;
		const auto* w     = static_cast<const std::uint8_t*>(widget);
		void*       owner = reinterpret_cast<OwnerFn>(Base + 0x2A7810)(widget);
		if (!owner) owner = LocalUnit();
		if (!owner || !Tooltip) return;
		void* other = *reinterpret_cast<void* const*>(w + 0x5D0);
		std::uint64_t scratch[2] {};
		EmptyText(&text);
		GameString* made = Tooltip(&text, owner, other, copy, *reinterpret_cast<const std::uint32_t*>(w + 0x5C0), 1, w[0x633], 0, scratch);
		if (made != &text && made) text = *made; // (the builder writes into `text`)
		built = true;
	}
	if (!text.size || !text.text) return;
	auto* slot = static_cast<std::uint8_t*>(widget) + 0x558;
	std::uint8_t saved[sizeof(GameString)];
	std::memcpy(saved, slot, sizeof(saved));
	std::memcpy(slot, &text, sizeof(GameString));
	auto*      flags = static_cast<unsigned char*>(widget);
	const auto compare = flags[Game::Compare];
	flags[Game::Compare] = 0; // no Shift comparison for a copy
	PeekCell   = cell;
	PeekCellOn = true;
	PeekShown  = copy;
	ShowTooltip(widget, copy);
	PeekCellOn = false;
	flags[Game::Compare] = compare;
	std::memcpy(slot, saved, sizeof(saved));
}

void* LocalUnit() { return reinterpret_cast<PlayerFn>(Base + Game::Player)(reinterpret_cast<IndexFn>(Base + Game::PlayerIndex)()); }

int Stat(void* unit, int stat, int layer) { return ReadStat && unit ? ReadStat(unit, stat, layer) : 0; }

// Lock held. A plain or superior base (not a runeword) dropped or Ctrl+clicked
// for the grail: kept in an empty runeword tile it can be made into, with
// exactly that runeword's socket count (stat 194). Dropped on a tile (`hovered`):
// that one, else the first on screen, else the first anywhere. Whether it got one.
bool AssignBase(const Items::ItemInfo& item, bool hovered) {
	if (item.quality != Items::Quality::Normal && item.quality != Items::Quality::Superior) return false;
	if (BaseFor.count(item.runtimeId) || StoredBaseEntry(item.runtimeId) >= 0) return false;
	if (RunewordRowOf(item) != -1) return false; // a runeword, or not known yet
	const auto code = CodeText(item.code);
	if (!Bases.count(code)) return false;
	void*     unit    = reinterpret_cast<GetUnitFn>(Base + Game::GetUnit)(item.runtimeId, Game::ItemUnit);
	const int sockets = Stat(unit, 194, 0);
	if (sockets <= 0) return false;
	const auto fits = [&](int u) {
		return u >= 0 && u < UniqueCount() && Uniques[u].runeword && static_cast<int>(Uniques[u].runes.size()) == sockets && !Stored.count({ u, false })
		    && RunewordEntry(Uniques[u].row, code) == u;
	};
	int pick = -1;
	if (hovered)
		if (const auto cell = HoverCell.load(); cell != ~std::uint64_t { 0 }) {
			const int x = static_cast<std::int32_t>(cell) - OriginX(), y = static_cast<std::int32_t>(cell >> 32) - OriginY();
			for (const auto& tile : Shown)
				if (x >= tile.x && x < tile.x + std::max(tile.width, SlotWidth) && y >= tile.y && y < tile.y + tile.height && fits(tile.unique)) pick = tile.unique;
		}
	for (std::size_t t = 0; pick < 0 && t < Shown.size(); ++t)
		if (fits(Shown[t].unique)) pick = Shown[t].unique;
	for (int u = 0; pick < 0 && u < UniqueCount(); ++u)
		if (fits(u)) pick = u;
	if (pick < 0) return false;
	BaseFor[item.runtimeId] = pick;
	return true;
}

// Life, mana and stamina keep 8 fraction bits.
int Display(int stat, int value) { return stat >= 6 && stat <= 11 ? value >> 8 : value; }


// Whether a roll is the best: past best, on the best's side of worst.
bool IsBest(const Roll& roll, int value) { return roll.best >= roll.worst ? value >= roll.best : value <= roll.best; }

constexpr int DefenseStat = 31;

// The best defense of an entry, or 0.
int BestDefenseOf(int entry, bool eth) {
	if (entry < 0 || entry >= static_cast<int>(BestDefense.size())) return 0;
	return eth ? BestDefense[entry].second : BestDefense[entry].first;
}

// Every variable roll at its best, defense included (flat defense rolls are
// part of it). An item with nothing that varies is perfect.
struct TextRolls {
	int damage = -1, defense = -1, sockets = 0; // Enhanced Damage, Enhanced Defense, sockets (no line: none)
};
TextRolls ItemTextRolls(void* item, int entry);
bool IsPerfect(int entry, void* item, bool eth) {
	if (entry >= 0 && entry < UniqueCount() && Uniques[entry].misc) return true; // nothing on it varies
	if (entry < 0 || entry >= static_cast<int>(Rolls.size())) return false;
	const int defense = BestDefenseOf(entry, eth);
	if (defense > 0 && Stat(item, DefenseStat, 0) < defense) return false;
	for (const auto& roll : Rolls[entry]) {
		if (roll.stats[0] == DefenseStat) continue;
		// The game's stat reader gives 0 for Enhanced Damage and Enhanced Defense: the tooltip says them.
		const int stat  = roll.stats[0];
		const int value = stat == 17 || stat == 18 ? ItemTextRolls(item, entry).damage
		                : stat == 16               ? ItemTextRolls(item, entry).defense
		                                           : Display(stat, Stat(item, stat, roll.layer));
		if (!IsBest(roll, value)) return false;
	}
	return true;
}

// ---- tooltip text ----

// Whether an item is identified (unidentified ones get no comparison).
bool Identified(const void* item) {
	const void* data = item ? At<const void*>(item, Game::UnitData) : nullptr;
	return data && (At<std::uint32_t>(data, Game::DataFlags) & 0x10) != 0;
}

std::atomic<bool> CompareOn { true }, Hooked {};
std::atomic<int>  HoverLogs {};


// A line's words (letters only: numbers, ranges and colours left out), to
// find it again in the finished text.
std::vector<std::string> LineWords(std::string_view line) {
	std::vector<std::string> words;
	std::string              word;
	for (std::size_t i = 0; i < line.size(); ++i) {
		const unsigned char c = line[i];
		if (c == 0xEE && i + 3 < line.size()) { // a colour: U+E07E and its character
			i += 3;
			continue;
		}
		if (std::isalpha(c) || c >= 0x80) word += static_cast<char>(std::tolower(c));
		else if (!word.empty()) words.push_back(std::move(word)), word.clear();
	}
	if (!word.empty()) words.push_back(std::move(word));
	return words;
}

// Whether a line's words are `want`, or start with them (with the roll ranges
// shown, Item Roll Ranges adds words after the line's own).
bool SameStart(const std::vector<std::string>& words, const std::vector<std::string>& want) {
	return words.size() >= want.size() && std::equal(want.begin(), want.end(), words.begin());
}

// Whether `want` is a run of a line's words.
bool HasRun(const std::vector<std::string>& words, const std::vector<std::string>& want) {
	return !want.empty() && std::search(words.begin(), words.end(), want.begin(), want.end()) != words.end();
}

// The number on a tooltip's line for `label` (its words starting the line,
// else anywhere in it), or -1: the first number before a '%' (the roll, not a
// range shown after it), else the first number.
int NumberInText(const GameString* text, const std::string& label) {
	if (!text || !text->text) return -1;
	const std::string_view all(text->text, text->size);
	const auto             want = LineWords(label);
	for (const bool loose : { false, true })
		for (std::size_t start = 0; start <= all.size();) {
			std::size_t end = all.find('\n', start);
			if (end == std::string_view::npos) end = all.size();
			const auto line  = all.substr(start, end - start);
			const auto words = LineWords(line);
			start            = end + 1;
			if (loose ? !HasRun(words, want) : !SameStart(words, want)) continue;
			int first = -1;
			for (std::size_t i = 0; i < line.size(); ++i) {
				if (static_cast<unsigned char>(line[i]) == 0xEE) { // a colour code
					i += 3;
					continue;
				}
				if (!std::isdigit(static_cast<unsigned char>(line[i]))) continue;
				int value = 0;
				while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) value = value * 10 + (line[i++] - '0');
				if (i < line.size() && line[i] == '%') return value;
				if (first < 0) first = value;
			}
			if (first >= 0) return first;
		}
	return -1;
}

GameString* EmptyText(GameString* out);
std::atomic<DWORD> UiThread {}; // the UI thread, from its tick
// An item's Enhanced Damage and Enhanced Defense, from its own tooltip (built
// once per item and grail state; Lock may be held).
TextRolls ItemTextRolls(void* copy, int entry) {
	static std::map<std::tuple<void*, int, std::uint64_t>, TextRolls> known;
	if (!copy) return {};
	const auto key = std::tuple { copy, entry, GrailChanges };
	if (const auto found = known.find(key); found != known.end()) return found->second;
	if (GetCurrentThreadId() != UiThread.load()) return {}; // tooltips are built on the UI thread only
	// The tooltip builder D2RCore puts behind the game's thunk pointer.
	if (!Tooltip && Context) Tooltip = *reinterpret_cast<TooltipFn*>(static_cast<std::uintptr_t>(Context->exeBase) + Game::TooltipSlot);
	void* player = LocalUnit();
	if (!player || !Tooltip) return {};
	GameString    text {};
	std::uint64_t scratch[2] {};
	EmptyText(&text);
	GameString*     built = Tooltip(&text, player, player, copy, 0, 1, 0, 0, scratch);
	const TextRolls rolls { NumberInText(built, EdLabel), NumberInText(built, EdefLabel), std::max(0, NumberInText(built, SocketLabel)) };
	if (known.size() > 256) known.clear();
	return known[key] = rolls;
}

// The hovered item's copy, for the comparison being built (UI thread).
thread_local void* CompareWith {};
thread_local bool  CompareGiven {};
thread_local bool  InGrailCompare {};
std::string        GrailHeader = "Currently in Grail";

GameString* EmptyText(GameString* out) {
	out->text      = out->buffer;
	out->size      = 0;
	out->capacity  = 0x800000000000000FULL;
	out->buffer[0] = 0;
	return out;
}

GameString* HookCompared(GameString* out, int bodyLocation) noexcept {
	if (!Tooltip && Context) Tooltip = *reinterpret_cast<TooltipFn*>(static_cast<std::uintptr_t>(Context->exeBase) + Game::TooltipSlot);
	if (!CompareWith || !Tooltip) return Compared(out, bodyLocation);
	if (CompareGiven) return EmptyText(out); // one comparison: the copy
	CompareGiven = true;
	void*         player = LocalUnit();
	std::uint64_t scratch[2] {};
	return Tooltip(out, player, player, CompareWith, 0, 1, 0, 0, scratch);
}

// The comparison's "Currently Equipped" header says the copy is in the grail:
// the game resolves the header's string key through D2RCore's
// ResolveNamespacedStringKey (exe thunk pointer 0x3E2AE18).
struct StringKey {
	const char*   text;
	std::uint64_t length;
};
using ResolveFn = const char* (*)(const StringKey*, void*, void*, void*) noexcept;
ResolveFn Resolve {};

const char* HookResolve(const StringKey* key, void* a, void* b, void* c) noexcept {
	if (InGrailCompare && key && key->text && key->length == 12 && !std::memcmp(key->text, "currentEquip", 12)) return GrailHeader.c_str();
	return Resolve(key, a, b, c);
}

void HookShowTooltip(void* widget, void* item) noexcept {
	// The grid hovers a picture tile's copy too, now and then, with its own
	// text: a second tooltip that flashes over PeekTooltip's.
	if (item && item == PeekShown && !PeekCellOn) return;
	// The item really under the mouse (the tooltip events also come for the
	// texts the plugin builds itself: grail copies).
	if (item) {
		const auto id = At<std::uint32_t>(item, Game::UnitId);
		if (static std::uint32_t seen = 0; (id != seen || GetTickCount64() - GridHoverAt.load() > HoverFreshMs) && ListsReady) {
			seen = id;
			std::lock_guard guard(Lock);
			bool      eth   = false;
			const int entry = Identified(item) ? EntryOf(item, eth) : -1;
			HoverEntry      = entry >= 0 && HasTile(entry, eth) ? entry : -1;
			HoverEntryEth   = eth;
		}
		GridHoverId = id, GridHoverAt = GetTickCount64();
	}
	void* copy = nullptr;
	if (ViewIsOpen && CompareOn && ListsReady && widget && item && Identified(item) && (Config.compare == CompareAlways || (GetKeyState(VK_SHIFT) & 0x8000))) {
		std::lock_guard guard(Lock);
		bool            eth   = false;
		const int       entry = EntryOf(item, eth);
		if (entry >= 0) copy = CopyOf(entry, eth);
		if (copy == item) copy = nullptr;
		if (HoverLogs < 3 && entry >= 0) {
			++HoverLogs;
			Say(false, "grailon: hovered %s (entry %d%s), copy %s", Uniques[entry].name.c_str(), entry, eth ? ", eth" : "", copy ? "found" : "none");
		}
	}
	if (!copy) {
		ShowTooltip(widget, item);
		return;
	}
	auto*      flags  = static_cast<unsigned char*>(widget);
	const auto saved  = std::make_pair(flags[Game::Compare], flags[Game::AlwaysCompare]);
	flags[Game::Compare] = flags[Game::AlwaysCompare] = 1;
	ForcedCompareAt      = GetTickCount64();
	CompareWith          = copy;
	CompareGiven         = false;
	InGrailCompare       = true;
	ShowTooltip(widget, item);
	InGrailCompare       = false;
	CompareWith          = nullptr;
	flags[Game::Compare] = saved.first, flags[Game::AlwaysCompare] = saved.second;
}

// Swaps a D2RCore thunk pointer for a hook, keeping the old one to call.
template <typename Fn>
bool SwapPointer(std::uint64_t rva, Fn hook, Fn* original) {
	std::uint64_t current = 0;
	std::memcpy(&current, reinterpret_cast<const void*>(Base + rva), sizeof(current));
	if (!current) return false;
	const auto replacement = reinterpret_cast<std::uint64_t>(hook);
	*original              = reinterpret_cast<Fn>(current);
	if (Context->PatchBytes(rva, &current, sizeof(current), &replacement, sizeof(replacement))) return true;
	// The loader may only patch code: write the pointer itself.
	auto* slot = reinterpret_cast<std::uint64_t*>(Base + rva);
	DWORD old  = 0;
	if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old)) return false;
	const bool swapped = InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(slot), static_cast<LONG64>(replacement), static_cast<LONG64>(current)) == static_cast<LONG64>(current);
	VirtualProtect(slot, sizeof(*slot), old, &old);
	return swapped;
}

void InstallComparison() {
	Base           = static_cast<std::uintptr_t>(Context->exeBase);
	Strcat         = reinterpret_cast<StrcatFn>(Base + Game::Strcat);
	HMODULE core   = GetModuleHandleW(L"D2RCore.dll");
	ReadStat       = core ? reinterpret_cast<StatFn>(reinterpret_cast<void*>(GetProcAddress(core, "ReadWideUnitStat"))) : nullptr;
	const bool fit = std::memcmp(reinterpret_cast<const void*>(Base + Game::ShowTooltip), Game::ShowTooltipBytes, sizeof(Game::ShowTooltipBytes)) == 0;
	if (!fit || !ReadStat) {
		Problem(false, "grailon: this game build is not the one the comparison was made for; no comparison");
		CompareOn = false;
		return;
	}
	if (!Context->InstallInlineHook(Game::ShowTooltip, Game::ShowTooltipBytes, sizeof(Game::ShowTooltipBytes), &HookShowTooltip, &ShowTooltip) || !ShowTooltip
	    || !SwapPointer(Game::CompareSlot, &HookCompared, &Compared)) {
		Problem(false, "grailon: could not hook the item tooltips (another plugin may hook them); no comparison");
		CompareOn = false;
		return;
	}
	// The tooltip builder D2RCore puts behind the game's thunk pointer, for the copy's text.
	Tooltip = *reinterpret_cast<TooltipFn*>(Base + Game::TooltipSlot);
	Hooked  = true;
	if (!SwapPointer(Game::ResolveSlot, &HookResolve, &Resolve)) Problem(false, "grailon: the comparison keeps its \"Currently Equipped\" header");
}

// ---- the grail's loot filter ----

// Ground uniques and set items the grail has (or has perfect) hidden, after
// every other verdict: the four calls of the loot filter's IsItemVisible
// (ground item drawing) go through a jump in int3 padding to GrailVisible<n>,
// which first calls whatever the call reached before (the game, a plugin
// hooking IsItemVisible like Loot Filter Plus, or one that moved the call),
// then hides what the grail's table says. The table is rebuilt on the UI
// thread and read without the lock.
namespace Loot {
constexpr std::uint64_t Sites[] { 0x1517169, 0x15176F9, 0x1519539, 0x15196F5 }; // calls of IsItemVisible 0x1C1120 (93787, 94995)
constexpr std::uintptr_t UnitClass = 0x04;
constexpr std::uint32_t  Identified = 0x10; // item data flags
} // namespace Loot
using VisibleFn = bool (*)(void* filter, void* unit) noexcept;
struct LootRow {
	int          item = -1; // the base's Items row: the unit's class must match
	std::uint8_t hide {};   // 1: hidden plain, 2: hidden ethereal
	bool operator==(const LootRow&) const = default;
};
struct LootTable {
	std::vector<LootRow> uniques, sets; // by UniqueItems / SetItems row
	// By Items row: hidden whatever unique (set item) of that base it is, for
	// unidentified items when ambiguous bases are not filtered.
	std::vector<std::uint8_t> uniqueBases, setBases;
	bool                      exact {}; // unidentified items by their row
	bool operator==(const LootTable&) const = default;
};
std::atomic<std::shared_ptr<const LootTable>> LootRows;
std::atomic<bool>                             LootOn {};
VisibleFn                                     LootBefore[std::size(Loot::Sites)] {};
std::uint8_t*                                 LootSite[std::size(Loot::Sites)] {};
bool                                          LootTried {};

bool LootHidden(const void* unit) noexcept {
	if (!LootOn.load(std::memory_order_relaxed) || !unit || At<std::uint32_t>(unit, 0) != Game::ItemUnit) return false;
	const void* data = At<const void*>(unit, Game::UnitData);
	if (!data) return false;
	const auto table = LootRows.load(std::memory_order_acquire);
	if (!table) return false;
	const auto  quality = At<std::uint32_t>(data, Game::DataQuality);
	const auto* rows    = quality == Game::UniqueQuality ? &table->uniques : quality == Game::SetQuality ? &table->sets : nullptr;
	if (!rows) return false;
	const auto flags = At<std::uint32_t>(data, Game::DataFlags);
	const auto item  = static_cast<int>(At<std::uint32_t>(unit, Loot::UnitClass));
	const int  bit   = (flags & Game::Ethereal) ? 2 : 1;
	// Unidentified, filtered as its base only: hidden when every item of that base would be.
	if (!(flags & Loot::Identified) && !table->exact) {
		const auto& bases = quality == Game::UniqueQuality ? table->uniqueBases : table->setBases;
		return item >= 0 && item < static_cast<int>(bases.size()) && (bases[item] & bit) != 0;
	}
	const auto file = At<std::int32_t>(data, Game::DataFile);
	if (file < 0 || file >= static_cast<int>(rows->size())) return false;
	const LootRow& row = (*rows)[file];
	// An unidentified item's row is only trusted when its base is the row's.
	if (!row.hide || row.item != item) return false;
	return (row.hide & bit) != 0;
}

template <int N>
bool GrailVisible(void* filter, void* unit) noexcept {
	return LootBefore[N](filter, unit) && !LootHidden(unit);
}
template <std::size_t... N>
constexpr std::array<VisibleFn, sizeof...(N)> VisibleHooks(std::index_sequence<N...>) {
	return { &GrailVisible<static_cast<int>(N)>... };
}
constexpr auto GrailVisibleHooks = VisibleHooks(std::make_index_sequence<std::size(Loot::Sites)>());

// A 14-byte jump to `target` in int3 padding inside D2R.exe (for a rel32
// call), or 0.
std::uint64_t PaddingJump(const void* target) {
	const auto                   base = static_cast<std::uintptr_t>(Context->exeBase);
	std::array<std::uint8_t, 14> jump { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
	const auto                   address = reinterpret_cast<std::uint64_t>(target);
	std::memcpy(jump.data() + 6, &address, sizeof(address));
	std::array<std::uint8_t, 16> padding {};
	padding.fill(0xCC);
	int tries = 0;
	for (std::uint64_t start = 0x2D0000; start < 0x1400000 && tries < 32; start += 16) {
		if (std::memcmp(reinterpret_cast<const void*>(base + start), padding.data(), padding.size()) != 0) continue;
		++tries;
		if (Context->PatchBytes(start + 1, padding.data(), 14, jump.data(), 14)) return start + 1;
	}
	return 0;
}

// The calls go through GrailVisible, the first time a loot setting hides something.
void HookLoot() {
	const auto base   = static_cast<std::uintptr_t>(Context->exeBase);
	int        hooked = 0;
	for (std::size_t n = 0; n < std::size(Loot::Sites); ++n) {
		auto* site = reinterpret_cast<std::uint8_t*>(base + Loot::Sites[n]);
		if (site[0] != 0xE8) {
			Say(true, "grailon: loot filter call %zu at D2R+0x%llX is not a call; left as it is", n, static_cast<unsigned long long>(Loot::Sites[n]));
			continue;
		}
		std::uint8_t bytes[5];
		std::memcpy(bytes, site, 5);
		std::int32_t rel = 0;
		std::memcpy(&rel, site + 1, 4);
		const auto before = reinterpret_cast<VisibleFn>(site + 5 + rel);
		if (reinterpret_cast<std::uintptr_t>(site + 5 + rel) != base + 0x1C1120)
			Say(false, "grailon: loot filter call %zu already goes elsewhere (another plugin); the grail filters after it", n);
		LootBefore[n] = before;
		const std::uint64_t jump = PaddingJump(reinterpret_cast<const void*>(GrailVisibleHooks[n]));
		if (!jump || !D2RL::PatchCallRel32(Context, Loot::Sites[n], bytes, 5, jump)) {
			Say(true, "grailon: could not reach loot filter call %zu", n);
			continue;
		}
		LootSite[n] = site;
		++hooked;
	}
	if (!hooked) Problem(false, "grailon: the loot filter's calls were not found in this game build; the Loot settings hide nothing");
	else Say(false, "grailon: loot filter: %d of %zu calls filtered", hooked, std::size(Loot::Sites));
}

void UnhookLoot() {
	LootOn = false;
	for (std::size_t n = 0; n < std::size(Loot::Sites); ++n)
		if (LootSite[n]) (void)PointCall(LootSite[n], reinterpret_cast<const void*>(LootBefore[n])); // the jumps stay: a draw may still be in one
}

// Lock held. The table of what the settings hide, from what the grail holds now.
void PublishLootTable() {
	if (!LootOn) return;
	auto       table = std::make_shared<LootTable>();
	const auto hides = [&](int u, bool eth, int policy) {
		const std::pair key { u, eth };
		return policy == LootFound ? Stored.count(key) && !BaseTiles.count(key) : policy == LootPerfect && Perfect.count(key);
	};
	const auto itemRow = [&](int u) {
		const auto base = Bases.find(Uniques[u].code);
		return base == Bases.end() ? -1 : base->second.row;
	};
	table->uniques.resize(UniqueByRow.size());
	for (std::size_t r = 0; r < UniqueByRow.size(); ++r) {
		const int u = UniqueByRow[r];
		if (u < 0 || u >= UniqueCount() || Uniques[u].secret) continue;
		auto& row = table->uniques[r];
		row.item  = itemRow(u);
		row.hide  = (HasTile(u, false) && hides(u, false, Config.loot[0]) ? 1 : 0) | (HasTile(u, true) && hides(u, true, Config.loot[1]) ? 2 : 0);
	}
	// A set item has one tile, ethereal or not.
	table->sets.resize(SetByRow.size());
	for (std::size_t r = 0; r < SetByRow.size(); ++r) {
		const int u = SetByRow[r];
		if (u < 0 || u >= UniqueCount()) continue;
		auto& row = table->sets[r];
		row.item  = itemRow(u);
		row.hide  = hides(u, false, Config.loot[2]) || hides(u, true, Config.loot[2]) ? 3 : 0;
	}
	// Per base: every unique (set item) that can be it, plain and ethereal.
	const auto byBase = [&](const std::vector<LootRow>& rows, const std::vector<int>& entries, std::vector<std::uint8_t>& bases) {
		std::vector<std::uint8_t> seen;
		for (std::size_t r = 0; r < rows.size(); ++r) {
			const int item = rows[r].item, u = entries[r];
			if (item < 0 || u < 0 || u >= UniqueCount()) continue;
			if (static_cast<int>(bases.size()) <= item) bases.resize(item + 1, 3), seen.resize(item + 1, 0);
			// What it can be: plain unless only ethereal, ethereal when it can be (a set item's one tile is both).
			const std::uint8_t can = Uniques[u].set ? 3 : (HasTile(u, false) ? 1 : 0) | (HasTile(u, true) ? 2 : 0);
			bases[item] &= static_cast<std::uint8_t>(rows[r].hide | ~can);
			seen[item] |= can;
		}
		for (std::size_t i = 0; i < bases.size(); ++i) bases[i] &= seen[i];
	};
	byBase(table->uniques, UniqueByRow, table->uniqueBases);
	byBase(table->sets, SetByRow, table->setBases);
	table->exact = Config.lootAmbiguous;
	if (const auto now = LootRows.load(); !now || !(*now == *table)) LootRows.store(std::move(table), std::memory_order_release);
}

void ApplyLootSettings() {
	const bool on = std::any_of(std::begin(Config.loot), std::end(Config.loot), [](int mode) { return mode != LootAll; });
	if (on && !LootTried) {
		LootTried = true;
		HookLoot();
	}
	LootOn = on;
}

// The tooltip hooks as the settings want them, from the start or when one
// changes: nothing is hooked while the comparison is off; the hook is made
// once, the first time it is wanted, and stays (off is a flag).
bool HookTried {};
void ApplyTooltipSettings() {
	const bool compare = Config.compare != CompareNone;
	if (compare && !Hooked && !HookTried) {
		HookTried = true;
		InstallComparison();
	}
	CompareOn = Hooked && compare;
	ApplyLootSettings();
}

// Puts D2RCore's tooltip pointers back.
void UnhookComparison() {
	if (!Hooked) return;
	Hooked = CompareOn = false;
	const auto hookCompare = reinterpret_cast<std::uint64_t>(&HookCompared), oldCompare = reinterpret_cast<std::uint64_t>(Compared);
	const auto hookResolve = reinterpret_cast<std::uint64_t>(&HookResolve), oldResolve = reinterpret_cast<std::uint64_t>(Resolve);
	for (const auto& [rva, hook, old] : { std::tuple { Game::CompareSlot, hookCompare, oldCompare }, std::tuple { Game::ResolveSlot, hookResolve, oldResolve } }) {
		if (!old) continue;
		if (Context->PatchBytes(rva, &hook, sizeof(hook), &old, sizeof(old))) continue;
		auto* slot = reinterpret_cast<std::uint64_t*>(Base + rva);
		DWORD keep = 0;
		if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &keep)) continue;
		(void)InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(slot), static_cast<LONG64>(old), static_cast<LONG64>(hook));
		VirtualProtect(slot, sizeof(*slot), keep, &keep);
	}
}

// ---- no placement squares over the grail (UI thread) ----
// With an item on the cursor over a grid, the grid's draw (Base+0x2C3570)
// shows where it would land: D2RCore's DrawInventoryGridItemPlacement through
// the thunk slot Base+0x3E2A7C8 (called from Base+0x2C39B8), or the cell
// background call Base+0x2C3AD0 (from 0x2C3A6A and 0x2C3A7E). Both are
// skipped for the grail grid, from those call sites only (the item loop draws
// item backgrounds through 0x2C3AD0 too). Checked on 3.3.94995.
namespace Placement {
constexpr std::uint64_t Slot = 0x3E2A7C8, SlotReturn = 0x2C39BD, Background = 0x2C3AD0, BackgroundReturns[] { 0x2C3A6F, 0x2C3A83 };
constexpr std::uint8_t  BackgroundBytes[] { 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x60 };
} // namespace Placement

using PlacementFn = std::uint64_t (*)(void* widget, void* rect, void* item, std::uint64_t colour) noexcept;
PlacementFn DrawPlacement {}, DrawCellBackground {};
bool        PlacementSwapped {};

__attribute__((noinline)) std::uint64_t HookDrawPlacement(void* widget, void* rect, void* item, std::uint64_t colour) noexcept {
	const auto from = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0)) - Base;
	if (widget && from == Placement::SlotReturn && IsGrailGrid(widget)) return 0;
	return DrawPlacement(widget, rect, item, colour);
}

__attribute__((noinline)) std::uint64_t HookCellBackground(void* widget, void* rect, void* item, std::uint64_t colour) noexcept {
	const auto from = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0)) - Base;
	if (widget && (from == Placement::BackgroundReturns[0] || from == Placement::BackgroundReturns[1]) && IsGrailGrid(widget)) return 0;
	return DrawCellBackground(widget, rect, item, colour);
}

void InstallPlacementHooks() {
	Base = static_cast<std::uintptr_t>(Context->exeBase);
	const auto* thunk = reinterpret_cast<const std::uint8_t*>(Base + 0x3E2B268);
	// The thunk is jmp [rip-0xAA6], which is the slot.
	if (thunk[0] == 0xFF && thunk[1] == 0x25 && SwapPointer(Placement::Slot, &HookDrawPlacement, &DrawPlacement)) PlacementSwapped = true;
	else Problem(false, "grailon: the placement squares stay (D2RCore's placement draw is not where expected)");
	if (!Context->InstallInlineHook(Placement::Background, Placement::BackgroundBytes, sizeof(Placement::BackgroundBytes), &HookCellBackground, &DrawCellBackground) || !DrawCellBackground)
		Problem(false, "grailon: the placement cell colours stay (the grid draw is not the expected one)");
}

void UnhookPlacement() {
	if (!PlacementSwapped) return;
	PlacementSwapped = false;
	const auto hook = reinterpret_cast<std::uint64_t>(&HookDrawPlacement), old = reinterpret_cast<std::uint64_t>(DrawPlacement);
	if (Context->PatchBytes(Placement::Slot, &hook, sizeof(hook), &old, sizeof(old))) return;
	auto* slot = reinterpret_cast<std::uint64_t*>(Base + Placement::Slot);
	DWORD keep = 0;
	if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &keep)) return;
	(void)InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(slot), static_cast<LONG64>(old), static_cast<LONG64>(hook));
	VirtualProtect(slot, sizeof(*slot), keep, &keep);
}

// ---- perfect items (UI thread) ----

// Lock held. A dropped item's tile as it will be once the item is in: stored,
// its base, perfect or not. The next arrange puts the records' state back.
void PredictDrop(const Drop& drop) {
	if (drop.unique < 0 || drop.unique >= UniqueCount()) return;
	if (drop.unique == SecretEntry) SecretWanted = drop.runtimeId;
	const std::pair key { drop.unique, drop.eth };
	Stored[key] = drop.runtimeId;
	if (drop.code) StoredCodes[key] = CodeText(drop.code);
	if (drop.base) {
		BaseTiles.insert(key);
		Perfect.erase(key);
		return;
	}
	BaseTiles.erase(key);
	if (!RollsReady || !ReadStat) return;
	if (void* unit = reinterpret_cast<GetUnitFn>(Base + Game::GetUnit)(drop.runtimeId, Game::ItemUnit)) {
		if (IsPerfect(drop.unique, unit, drop.eth)) Perfect.insert(key);
		else Perfect.erase(key);
	}
}

// Lock held. Whether the perfect tiles changed.
bool RefreshPerfect() {
	if (!RollsReady || !ReadStat) return false;
	// Only items on the page can be read; the records keep the others' flag.
	bool changed = false;
	for (const auto& [runtime, fingerprint] : Materialized) {
		const auto record = Records.find(fingerprint);
		const auto entry  = record == Records.end() ? EntryById.end() : EntryById.find(record->second.id);
		if (entry == EntryById.end() || record->second.base) continue;
		void* unit = reinterpret_cast<GetUnitFn>(Base + Game::GetUnit)(runtime, Game::ItemUnit);
		if (!unit) continue;
		const bool perfect = Uniques[entry->second].secret || IsPerfect(entry->second, unit, record->second.eth);
		if (perfect != record->second.perfect) record->second.perfect = perfect, changed = true;
	}
	if (!changed) return false;
	SaveStore();
	RebuildStored();
	return true;
}

// Lock held. Whether item `runtimeId` of entry `u` has every best roll.
bool PerfectNow(int u, std::uint32_t runtimeId, bool eth) {
	void* unit = reinterpret_cast<GetUnitFn>(Base + Game::GetUnit)(runtimeId, Game::ItemUnit);
	return Uniques[u].secret || (unit && IsPerfect(u, unit, eth));
}

// A perfect tile's glow, drawn over its slot and under its item: a gold halo
// around the slot (GlowPad beyond it), a bright rim, and a glow fading in from
// the edges. Frames: the light breathes and a brighter band runs around.

std::vector<Picture> Glow(int width, int height) {
	std::vector<Picture> frames;
	const int            w = width + 2 * GlowPad, h = height + 2 * GlowPad;
	const double         perimeter = 2.0 * (width + height), pi = 3.14159265358979;
	for (int f = 0; f < GlowFrames; ++f) {
		Picture      picture { w, h, std::vector<unsigned char>(static_cast<std::size_t>(w) * h * 4) };
		const double breath = 0.78 + 0.22 * std::cos(2 * pi * f / GlowFrames);
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x) {
				// Distance from the slot's edge: < 0 outside, > 0 inside.
				const int    sx = x - GlowPad, sy = y - GlowPad;
				const int    dx = sx < 0 ? sx : sx >= width ? width - 1 - sx : 0, dy = sy < 0 ? sy : sy >= height ? height - 1 - sy : 0;
				const double outside = std::sqrt(static_cast<double>(dx * dx + dy * dy));
				const double inside  = std::min({ sx, sy, width - 1 - sx, height - 1 - sy });
				// Where the point is along the rim, 0..1 clockwise from the top left.
				const double cx = std::clamp(sx, 0, width - 1), cy = std::clamp(sy, 0, height - 1);
				const double edge = std::min({ cx, cy, width - 1 - cx, height - 1 - cy });
				double       along = 0;
				if (cy == edge) along = cx;
				else if (width - 1 - cx == edge) along = width + cy;
				else if (height - 1 - cy == edge) along = width + height + (width - cx);
				else along = 2.0 * width + height + (height - cy);
				const double phase = along / perimeter - static_cast<double>(f) / GlowFrames;
				const double band  = std::pow(0.5 + 0.5 * std::cos(2 * pi * (phase - std::floor(phase))), 8);
				double       alpha = 0, light = 0;
				if (outside > 0) alpha = 235 * std::pow(std::max(0.0, 1 - outside / GlowPad), 1.6) * (breath + 0.35 * band), light = 0.4;
				else if (inside < 3) alpha = 255, light = 0.55 + 0.45 * std::max(band, breath - 0.6);
				else alpha = 150 * std::pow(std::max(0.0, 1 - (inside - 3) / 26.0), 2) * (breath + 0.3 * band), light = 0.25;
				auto* px = picture.at(x, y);
				px[0]    = static_cast<unsigned char>(std::min(255.0, 255 * (0.8 + 0.2 * light) + 30 * band));
				px[1]    = static_cast<unsigned char>(std::min(255.0, 200 * (0.75 + 0.25 * light) + 40 * band));
				px[2]    = static_cast<unsigned char>(std::min(255.0, 70 + 110 * light + 80 * band));
				px[3]    = static_cast<unsigned char>(std::clamp(alpha, 0.0, 255.0));
			}
		frames.push_back(std::move(picture));
	}
	return frames;
}

void RegisterGlows() {
	static std::set<std::pair<int, int>> done;
	for (const auto& entry : Uniques) {
		const int width = std::clamp(entry.width, 1, 4), height = std::clamp(entry.height, 1, 4);
		if (!done.insert({ width, height }).second) continue;
		if (!RegisterSprite("glow" + std::to_string(width) + "x" + std::to_string(height), Glow(width * Cell, height * Cell))) Say(true, "grailon: could not register a glow sprite");
	}
}


// The number over the life globe (HUDPanel/HealthBall/ValueDisplay): the
// game hides it under its own left panels; the grail view hides it too while
// it is open (UI thread, every animation tick, since the game may show it again).
bool LifeTextHidden {};
void HideLifeText(bool hide) {
	if (!WidgetApi || (!hide && !LifeTextHidden)) return;
	D2RL::Widgets::WidgetHandle hud {}, ball {}, text {};
	if (WidgetApi->findPanel(Context, "HUDPanel", &hud) != D2RL::Widgets::Result::Success
	    || WidgetApi->findWidget(Context, hud, "HealthBall", &ball) != D2RL::Widgets::Result::Success
	    || WidgetApi->findWidget(Context, ball, "ValueDisplay", &text) != D2RL::Widgets::Result::Success) {
		static bool told = false;
		if (!told) Say(false, "grailon: the life globe's number was not found");
		told = true;
		return;
	}
	(void)WidgetApi->setWidgetVisible(Context, text, !hide);
	LifeTextHidden = hide;
}

// UI thread, on each animation tick while a view is open (the glows play by
// themselves).
void Animate() {
	CloseLateViews();
	if (!ViewIsOpen || ShownView < 0 || !WidgetApi) return;
	HideLifeText(true);
}

void __cdecl AnimateOnUi(const D2RL::PluginContext*, void*) noexcept { Animate(); }

// `grailon rolls`: each stored copy's variable rolls against the best, to see
// why a copy is or is not perfect.
void SayRolls() {
	if (!RollsReady || !ReadStat || !Base) {
		Say(true, "grailon: rolls cannot be read on this game build");
		return;
	}
	std::lock_guard guard(Lock);
	if (Stored.empty()) Say(false, "grailon: nothing stored yet (open the grail first)");
	for (const auto& [key, id] : Stored) {
		void* unit = reinterpret_cast<GetUnitFn>(Base + Game::GetUnit)(id, Game::ItemUnit);
		if (!unit) continue;
		std::string line = Uniques[key.first].name + (key.second ? " (Eth)" : "") + ":";
		if (Rolls[key.first].empty() && !BestDefenseOf(key.first, key.second)) line += " nothing rolls";
		for (const auto& roll : Rolls[key.first]) {
			const int value = Display(roll.stats[0], Stat(unit, roll.stats[0], roll.layer));
			char      part[96] {};
			std::snprintf(part, sizeof(part), " stat %d%s%d: %d of %d..%d%s", roll.stats[0], roll.layer ? "/" : "", roll.layer, value, roll.worst, roll.best, IsBest(roll, value) ? " best" : "");
			line += part;
		}
		if (const int best = BestDefenseOf(key.first, key.second); best > 0) {
			char part[64] {};
			std::snprintf(part, sizeof(part), " defense %d of best %d", Stat(unit, DefenseStat, 0), best);
			line += part;
		}
		Say(false, "grailon: %s%s", line.c_str(), IsPerfect(key.first, unit, key.second) ? " -> perfect" : "");
	}
}

// ---- commands ----

void ToggleScrollTest();

// A click on a cut slot does what a click on its tile would, and the view
// stays: the item on the cursor goes in (its row scrolls into view when it is
// another tile's), else the stored item goes on the cursor, or with Ctrl to
// the inventory.
void ClickPeek(int index) {
	D2RL::ItemHandle handle {};
	Items::ItemInfo  item { .structSize = Items::ItemInfoSize };
	const auto       player = LocalPlayer.load();
	const bool       held   = player && Inventory->getCursorItem(Context, player, &handle) == D2RL::Inventory::Result::Success && handle
	                 && GetInfo(Context, handle, &item) == Items::Result::Success;
	const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
	CursorJob* job  = nullptr;
	bool       drop = false, take = false, ticked = false;
	int        tickSound = 0;
	{
		std::lock_guard guard(Lock);
		if (index < 0 || index >= static_cast<int>(Peeking.size()) || HeldId || TakeId) return;
		const auto peek = Peeking[index];
		// A checkbox entry: a click ticks it, or clears it.
		if (const auto& entry = Uniques[peek.unique]; IsCheckbox(entry)) {
			if (held || StorePath.empty()) return;
			if (!CheckedIds.erase(entry.id)) CheckedIds.insert(entry.id);
			SaveStore();
			RebuildStored();
			++Config.version;
			ticked = true;
			if (const auto base = Bases.find(entry.code); base != Bases.end()) tickSound = base->second.dropSound;
		} else if (held) {
			if (const int u = GrailIndex(item); u == peek.unique && IsEth(item) == peek.eth && QueueSwap(item, u, true)) return;
			if (!Accepts(item)) return;
			const int u = GrailIndex(item);
			if (u == peek.unique && IsEth(item) == peek.eth) {
				job = new CursorJob { .step = CursorStep::PutPage, .id = item.runtimeId, .unique = u, .eth = IsEth(item) };
				Busy.insert(item.runtimeId);
			} else {
				int width = 1, height = 1;
				(void)ItemSize(CodeText(item.code), width, height);
				PendingDrop = { true, true, u, IsEth(item), item.runtimeId, width, height, 0, {}, item.code, BaseFor.count(item.runtimeId) != 0 };
				JumpTo = u, JumpEth = IsEth(item);
				drop   = true;
			}
		} else {
			// A cut slot's item lives in its record only (just the tiles on screen are made).
			const auto record = RecordAt(peek.unique, peek.eth);
			if (!record) return;
			for (const auto& [runtime, print] : Materialized)
				if (print == record->fingerprint) return; // on the page after all: the grid handles it
			if (ctrl) job = new CursorJob { .step = CursorStep::CtrlOut, .id = record->fingerprint };
			else TakeId = record->fingerprint, take = true;
		}
	}
	if (ticked) {
		if (tickSound > 0) PlayItemSound(Context, reinterpret_cast<void*>(static_cast<std::uintptr_t>(tickSound)));
		ShowView();
	} else if (drop) RunDrop();
	else if (job) (void)QueueCursorJob(job);
	else if (take) KickPending(); // made on a free cell, then onto the cursor
}

// A click under the host's grid (host mode): the item on the cursor goes to
// its tile, as if dropped on the grid.
void ClickLower() {
	D2RL::ItemHandle handle {};
	Items::ItemInfo  item { .structSize = Items::ItemInfoSize };
	const auto       player = LocalPlayer.load();
	if (!player || Inventory->getCursorItem(Context, player, &handle) != D2RL::Inventory::Result::Success || !handle || GetInfo(Context, handle, &item) != Items::Result::Success) return;
	int peek = -1;
	{
		std::lock_guard guard(Lock);
		if (HeldId || TakeId || !Accepts(item)) return;
		const int u = GrailIndex(item);
		for (std::size_t i = 0; i < Peeking.size(); ++i)
			if (Peeking[i].unique == u && Peeking[i].eth == IsEth(item)) peek = static_cast<int>(i);
		if (peek < 0) {
			int width = 1, height = 1;
			(void)ItemSize(CodeText(item.code), width, height);
			PendingDrop = { true, true, u, IsEth(item), item.runtimeId, width, height, 0, {}, item.code, BaseFor.count(item.runtimeId) != 0 };
			JumpTo = u, JumpEth = IsEth(item);
		}
	}
	if (peek >= 0) ClickPeek(peek);
	else RunDrop();
}

void CompactChanged();
void MoveInventoryButton();

// ---- the objectives' text box ----
// The game's own input box doesn't get the keys in this panel (they go to the
// game's key bindings). As Stash Search does: while the box is active, a
// low-level keyboard hook on a thread of the plugin's own sends the keys to a
// hidden Windows EDIT control there (so keyboard layouts, Shift, AltGr and
// paste work) and keeps them from the game; the page shows its text.
std::atomic<bool>  ObjTyping {}, ObjTypedChanged {}, ObjEnter {};
std::atomic<int>   TypeTarget {};
std::atomic<std::uint64_t> TypeClickAt {}, TypeHitAt {}; // the last mouse click, the last click on a text box
std::mutex         ObjTypedLock;
std::wstring       TypedTexts[2]; // by target
std::wstring&      ObjTyped = TypedTexts[0];
HWND               ObjEdit {};
HHOOK              ObjHook {};
HANDLE             ObjThread {};
std::atomic<DWORD> ObjThreadId {};
std::uint8_t       ObjForwarded[256] {};
constexpr UINT     ObjMsgClear = WM_APP + 41, ObjMsgQuit = WM_APP + 42, ObjMsgLoad = WM_APP + 43;
void __cdecl TypedOnUi(const D2RL::PluginContext*, void*) noexcept;
// The page follows a key at once (the UI tick alone came up to 250 ms later).
void TypedNow() { (void)Threads->runOnUiThread(Context, TypedOnUi, nullptr); }

std::string TypedText(int target) {
	std::lock_guard guard(ObjTypedLock);
	const auto& typed = TypedTexts[target & 1];
	if (typed.empty()) return {};
	const int   size = WideCharToMultiByte(CP_UTF8, 0, typed.c_str(), static_cast<int>(typed.size()), nullptr, 0, nullptr, nullptr);
	std::string out(static_cast<std::size_t>(std::max(0, size)), '\0');
	if (size > 0) WideCharToMultiByte(CP_UTF8, 0, typed.c_str(), static_cast<int>(typed.size()), out.data(), size, nullptr, nullptr);
	return out;
}
std::string ObjTypedText() { return TypedText(TypeObjective); }
void ClearTyped(int target) {
	{
		std::lock_guard guard(ObjTypedLock);
		TypedTexts[target & 1].clear();
	}
	if (const DWORD id = ObjThreadId.load(); id && TypeTarget == target) PostThreadMessageW(id, ObjMsgClear, 0, 0);
}
void ObjClearTyped() { ClearTyped(TypeObjective); }

bool GameInFront() {
	const HWND front = GetForegroundWindow();
	DWORD      pid   = 0;
	return front && GetWindowThreadProcessId(front, &pid) && pid == GetCurrentProcessId();
}

LRESULT CALLBACK ObjKeyHook(int code, WPARAM wParam, LPARAM lParam) {
	const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
	if (code < 0 || !key || key->vkCode > 0xFF || (wParam != WM_KEYDOWN && wParam != WM_KEYUP)) return CallNextHookEx(ObjHook, code, wParam, lParam);
	const DWORD vk = key->vkCode;
	if (wParam == WM_KEYUP) {
		if (!ObjForwarded[vk]) return CallNextHookEx(ObjHook, code, wParam, lParam);
		ObjForwarded[vk] = 0;
	} else if (!ObjTyping || !GameInFront()) return CallNextHookEx(ObjHook, code, wParam, lParam);
	switch (vk) {
	case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: case VK_MENU: case VK_LMENU: case VK_RMENU:
	case VK_LWIN: case VK_RWIN: case VK_CAPITAL: case VK_NUMLOCK: case VK_SCROLL: return CallNextHookEx(ObjHook, code, wParam, lParam);
	default: break;
	}
	const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0, alt = (key->flags & LLKHF_ALTDOWN) != 0;
	if (alt && !ctrl) return CallNextHookEx(ObjHook, code, wParam, lParam); // Alt+Tab, Alt+F4, Alt+Enter
	if (wParam == WM_KEYDOWN && vk == VK_RETURN) {
		ObjForwarded[vk] = 1;
		ObjEnter         = true;
		TypedNow();
		return 1;
	}
	if (wParam == WM_KEYDOWN && vk == VK_ESCAPE) {
		ObjForwarded[vk] = 1;
		ObjTyping        = false;
		ObjTypedChanged  = true;
		TypedNow();
		return 1;
	}
	if (!ObjEdit) return CallNextHookEx(ObjHook, code, wParam, lParam);
	LPARAM message = static_cast<LPARAM>(1 | (static_cast<std::uint64_t>(static_cast<std::uint8_t>(key->scanCode)) << 16) | ((key->flags & LLKHF_EXTENDED) ? 0x01000000ULL : 0));
	if (wParam == WM_KEYUP) message |= static_cast<LPARAM>(0xC0000000ULL);
	UINT   id    = static_cast<UINT>(wParam);
	WPARAM value = vk;
	if (wParam == WM_KEYDOWN && ctrl && !alt && (vk == 'A' || vk == 'C' || vk == 'V' || vk == 'X' || vk == 'Z')) id = WM_CHAR, value = vk - 'A' + 1;
	if (!PostMessageW(ObjEdit, id, value, message)) return CallNextHookEx(ObjHook, code, wParam, lParam);
	if (wParam == WM_KEYDOWN) ObjForwarded[vk] = 1;
	return 1;
}

// The game window's keyboard layout and key states, so the EDIT control
// makes the same characters the game would.
void ObjSyncKeyboard() {
	HWND game = GetForegroundWindow();
	if (DWORD pid = 0; game && GetWindowThreadProcessId(game, &pid) && pid == GetCurrentProcessId())
		if (const HKL layout = GetKeyboardLayout(GetWindowThreadProcessId(game, nullptr)); layout && layout != GetKeyboardLayout(0)) ActivateKeyboardLayout(layout, 0);
	BYTE keys[256] {};
	if (!GetKeyboardState(keys)) return;
	for (const int vk : { VK_SHIFT, VK_LSHIFT, VK_RSHIFT, VK_CONTROL, VK_LCONTROL, VK_RCONTROL, VK_MENU, VK_LMENU, VK_RMENU })
		keys[vk] = (GetAsyncKeyState(vk) & 0x8000) ? 0x80 : 0;
	for (const int vk : { VK_CAPITAL, VK_NUMLOCK })
		keys[vk] = static_cast<BYTE>((GetKeyState(vk) & 1) | ((GetAsyncKeyState(vk) & 0x8000) ? 0x80 : 0));
	SetKeyboardState(keys);
}

DWORD WINAPI ObjInputMain(void*) {
	ObjThreadId = GetCurrentThreadId();
	HMODULE self = nullptr;
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&ObjKeyHook), &self);
	ObjEdit = CreateWindowExW(0, L"EDIT", L"", ES_AUTOHSCROLL, 0, 0, 0, 0, HWND_MESSAGE, nullptr, self, nullptr);
	if (ObjEdit) SendMessageW(ObjEdit, EM_LIMITTEXT, 120, 0), SetFocus(ObjEdit);
	ObjHook = SetWindowsHookExW(WH_KEYBOARD_LL, ObjKeyHook, self, 0);
	if (!ObjEdit || !ObjHook) Say(true, "grailon: the objectives' text box could not start (edit %d, hook %d)", ObjEdit != nullptr, ObjHook != nullptr);
	MSG message {};
	while (GetMessageW(&message, nullptr, 0, 0) > 0) {
		if (message.message == ObjMsgQuit) break;
		if (message.message == ObjMsgClear) {
			if (ObjEdit) SetWindowTextW(ObjEdit, L"");
			continue;
		}
		if (message.message == ObjMsgLoad) { // another box: its own text, the caret at its end
			std::wstring text;
			{
				std::lock_guard guard(ObjTypedLock);
				text = TypedTexts[TypeTarget & 1];
			}
			if (ObjEdit) SetWindowTextW(ObjEdit, text.c_str()), SendMessageW(ObjEdit, EM_SETSEL, text.size(), text.size());
			continue;
		}
		const bool key = message.message == WM_KEYDOWN || message.message == WM_KEYUP || message.message == WM_CHAR;
		if (message.message == WM_KEYDOWN || message.message == WM_KEYUP) ObjSyncKeyboard();
		TranslateMessage(&message);
		DispatchMessageW(&message);
		if (key && ObjEdit) {
			// The text after the key (TranslateMessage's WM_CHAR comes next: read again then).
			wchar_t buffer[256] {};
			GetWindowTextW(ObjEdit, buffer, static_cast<int>(std::size(buffer)));
			std::lock_guard guard(ObjTypedLock);
			auto&           typed = TypedTexts[TypeTarget & 1];
			if (typed != buffer) typed = buffer, ObjTypedChanged = true, TypedNow();
		}
	}
	if (ObjHook) UnhookWindowsHookEx(ObjHook), ObjHook = nullptr;
	if (ObjEdit) DestroyWindow(ObjEdit), ObjEdit = nullptr;
	ObjThreadId = 0;
	return 0;
}

// The box activated: the thread started once, the keys taken from now on.
void ObjStartTyping(int target = TypeObjective) {
	if (!ObjThread) ObjThread = CreateThread(nullptr, 0, ObjInputMain, nullptr, 0, nullptr);
	std::memset(ObjForwarded, 0, sizeof(ObjForwarded));
	const bool other = TypeTarget.exchange(target) != target;
	TypeHitAt        = GetTickCount64();
	if (other)
		for (int k = 0; k < 50 && !ObjThreadId; ++k) Sleep(2);
	if (const DWORD id = ObjThreadId.load()) PostThreadMessageW(id, ObjMsgLoad, 0, 0);
	ObjTyping = true;
}
void ObjStopThread() {
	ObjTyping = false;
	if (!ObjThread) return;
	for (int k = 0; k < 50 && !ObjThreadId; ++k) Sleep(10);
	if (const DWORD id = ObjThreadId.load()) PostThreadMessageW(id, ObjMsgQuit, 0, 0);
	WaitForSingleObject(ObjThread, 2000);
	CloseHandle(ObjThread);
	ObjThread = nullptr;
}

// UI thread, each tick: the page follows the typing; Enter adds.
void ObjInputTick() {
	const bool search = TypeTarget == TypeSearch;
	if (ObjTyping && (!ViewOpen() || (!search && (CurrentCategory != SettingsView || SettingsPage != 4)))) ObjTyping = false, ObjTypedChanged = true;
	// A click anywhere but on the box: the box lets the keys go (the box's own
	// click comes as its command right after the click).
	if (const auto click = TypeClickAt.load(); ObjTyping && click && GetTickCount64() - click > 120) {
		TypeClickAt = 0;
		if (TypeHitAt.load() < click) ObjTyping = false, ObjTypedChanged = true;
	}
	if (ObjEnter.exchange(false) && ObjTyping) Queue(search ? "searchnext" : "objadd");
	if (!ObjTypedChanged.exchange(false) || !ViewOpen()) return;
	if (search) {
		Queue("search"); // the matches again, the first shown
		return;
	}
	// The label alone when the runtime can change it, else the page again.
	namespace W = D2RL::Widgets;
	W::WidgetHandle panel = W::InvalidHandle, label = W::InvalidHandle;
	const std::string typed = ObjTypedText();
	(void)panel, (void)label, (void)typed; // setText left the drawn text unchanged: the page again (its key has the text)
	ShowView();
}

void __cdecl TypedOnUi(const D2RL::PluginContext*, void*) noexcept { ObjInputTick(); }

void RunCommand(const std::string& command) {
	// The objectives' box: active on its click, inactive on any other.
	if (command == "objtype" || command == "searchtype") {
		ObjStartTyping(command[0] == 's' ? TypeSearch : TypeObjective);
		ShowView();
		return;
	}
	if ((command.rfind("menu", 0) == 0 || command.rfind("toggle", 0) == 0 || (command.rfind("obj", 0) == 0 && command != "objadd" && command != "objtype")) && ObjTyping) ObjTyping = false, ObjTypedChanged = true;
	if (command == "search" || command == "searchnext" || command == "searchprev" || command == "searchclear") {
		{
			std::lock_guard guard(Lock);
			bool same = false; // the same text (the box only lost the keys): the match shown stays
			if (command == "searchclear") ClearTyped(TypeSearch), FindMatches("");
			else if (command == "search") {
				if (const auto typed = TypedText(TypeSearch); typed != SearchedText) FindMatches(typed);
				else same = true;
			} else if (SearchMatches.empty()) FindMatches(TypedText(TypeSearch));
			else SearchAt += command == "searchnext" ? 1 : -1;
			if (!same) JumpToMatch();
		}
		ShowView();
		return;
	}
	if (command.rfind("srch", 0) == 0) {
		// The search bar's place and length: a step of 1 (10 with Shift, 100 with Ctrl), or back to the default.
		{
			std::lock_guard guard(Lock);
			if (command == "srchreset") Config.searchX = DefaultSearchX, Config.searchY = DefaultSearchY, Config.searchWidth = DefaultSearchWidth;
			else if (command.size() == 6) {
				const int step = (GetKeyState(VK_CONTROL) & 0x8000) ? 100 : (GetKeyState(VK_SHIFT) & 0x8000) ? 10 : 1;
				int&      value = command[4] == 'X' ? Config.searchX : command[4] == 'Y' ? Config.searchY : Config.searchWidth;
				value           = value + (command[5] == '-' ? -step : step);
				Config.searchX = std::clamp(Config.searchX, -3000, 4000), Config.searchY = std::clamp(Config.searchY, -3000, 4000), Config.searchWidth = std::clamp(Config.searchWidth, 300, 2000);
			}
			++Config.version;
		}
		SaveSettings();
		ShowView();
		return;
	}
	if (command == "showitem") {
		// A middle click on a grail item: its tile is shown, the view opened if closed.
		const auto      hovered = ShowItemWanted.exchange({});
		Items::ItemInfo item { .structSize = Items::ItemInfoSize };
		if (const int entry = ShowEntryWanted.exchange(-1); !hovered && entry >= 0 && ListsReady) {
			std::lock_guard guard(Lock);
			if (entry >= UniqueCount() || !HasTile(entry, ShowEntryEth)) return;
			JumpTo = entry, JumpEth = ShowEntryEth;
			Say(false, "grailon: middle click shows %s", Uniques[entry].name.c_str());
		} else if (!hovered || !ListsReady || GetInfo(Context, hovered, &item) != Items::Result::Success) return;
		else {
			std::lock_guard guard(Lock);
			const int u = GrailIndex(item);
			if (u < 0 || !HasTile(u, IsEth(item))) return;
			JumpTo = u, JumpEth = IsEth(item);
			Say(false, "grailon: middle click shows %s", Uniques[u].name.c_str());
		}
		ShowView();
		return;
	}
	if (command == "open") {
		if (ViewOpen() || PreparedView >= 0 || (ErrorPanel != D2RL::Panels::InvalidHandle && IsOpen(ErrorPanel))) CloseView();
		else ShowView();
		return;
	}
	if (command == "close") {
		CloseView();
		return;
	}
	if (command == "scrolltest") {
		ToggleScrollTest();
		return;
	}
	if (command == "drop") {
		RunDrop();
		return;
	}
	if (command == "stage") { // a click on the host's grid panel: the picture tile under it, else a drop
		if (HoveredPeek >= 0) ClickPeek(HoveredPeek);
		else ClickLower();
		return;
	}
	if (command == "lower") {
		ClickLower();
		return;
	}
	if (command.rfind("peek", 0) == 0) {
		ClickPeek(std::atoi(command.c_str() + 4));
		return;
	}
	if (command == "rolls") {
		SayRolls();
		return;
	}
	if (command == "refresh") {
		ShowView();
		return;
	}
	if (command == "forget") {
		// Done by the next arrange, once the items just taken out have left the grail.
		ForgetWanted = true;
		if (const auto player = LocalPlayer.load()) (void)QueueArrange(new ArrangeJob { .player = player });
		return;
	}
	// The settings page: a check box (toggle:<key>) or a comparison choice
	// (compare<n>, goal<kind><n>, loot<kind><n>), saved and in effect at once.
	// A drop-down opened or closed (drop<id>, dropx).
	if (command.size() >= 5 && command.rfind("drop", 0) == 0 && (command[4] == 'x' || std::isdigit(static_cast<unsigned char>(command[4])))) {
		{
			std::lock_guard guard(Lock);
			const int id = command[4] == 'x' ? -1 : std::atoi(command.c_str() + 4);
			OpenDrop     = id < 0 || OpenDrop == id ? -1 : id;
			++Config.version;
		}
		ShowView();
		return;
	}
	// A display choice: pick<id>:<n> (perfect frame, menu direction, layout, scrolling).
	if (command.rfind("pick", 0) == 0) {
		const int  id = std::atoi(command.c_str() + 4);
		const auto colon = command.find(':');
		if (colon == std::string::npos) return;
		const int n = std::atoi(command.c_str() + colon + 1);
		bool      compactBefore = false;
		{
			std::lock_guard guard(Lock);
			OpenDrop      = -1;
			compactBefore = Config.compact;
			if (id == 9) Config.perfectFrames = n > 0, Config.frameAnimations = n == 2;
			if (id == 10) Config.swapColumns = n == 1;
			if (id == 11) Config.compact = n == 1;
			if (id == 12) Config.scrollAnimations = n == 1;
			++Config.version;
		}
		ApplyTooltipSettings();
		SaveSettings();
		if (compactBefore != Config.compact) CompactChanged();
		ShowView();
		return;
	}
	// A misc type's tracking: miscmode<i>:<mode> (no tracking, checkbox, items).
	if (command.rfind("miscmode", 0) == 0) {
		const auto colon = command.find(':');
		if (colon == std::string::npos) return;
		const int i = std::atoi(command.c_str() + 8), mode = std::clamp(std::atoi(command.c_str() + colon + 1), 0, 2);
		{
			std::lock_guard guard(Lock);
			OpenDrop = -1;
			if (i < 0 || i >= static_cast<int>(MiscCategories.size())) return;
			if (mode == MiscOff) MiscMode.erase(MiscCategories[i].type);
			else MiscMode[MiscCategories[i].type] = mode;
			UpdateMiscTracking();
			RebuildStored();
			++Config.version;
		}
		SaveSettings();
		ShowView();
		return;
	}
	// The custom objectives: added from the page's text box, removed, their icon changed.
	if (command == "objadd" || command.rfind("objdel", 0) == 0 || command.rfind("objpick", 0) == 0 || command.rfind("objset", 0) == 0 || command.rfind("objon", 0) == 0
	    || command.rfind("objup", 0) == 0 || command.rfind("objdown", 0) == 0 || command.rfind("objpage", 0) == 0) {
		std::string text;
		if (command == "objadd") {
			// The text typed in the box (the hidden text box's).
			text = ObjTypedText();
			ObjClearTyped();

			// One line, no leading spaces, nothing the game reads as a string key or a colour.
			for (auto& c : text)
				if (c == '\n' || c == '\r' || c == '\t' || c == '|') c = ' ';
			text = StripColors(text);
			while (!text.empty() && (text.front() == ' ' || text.front() == '@' || text.front() == '$')) text.erase(text.begin());
			while (!text.empty() && text.back() == ' ') text.pop_back();
			if (text.empty()) return;
		}
		{
			std::lock_guard guard(Lock);
			OpenDrop = -1;
			if (command == "objadd") {
				Objectives.push_back({ NextObjective++, static_cast<int>(Objectives.size()) % ObjOwnIcons, text });
				ObjFirst = std::max(0, static_cast<int>(Objectives.size()) - 1) / std::max(1, ObjPerPage) * std::max(1, ObjPerPage);
			} else if (command.rfind("objpage", 0) == 0) ObjFirst = std::max(0, ObjFirst + (command.back() == '-' ? -ObjPerPage : ObjPerPage));
			else if (command == "objpickx") ObjPicking = -1;
			else if (command.rfind("objset", 0) == 0) {
				if (ObjPicking >= 0 && ObjPicking < static_cast<int>(Objectives.size())) Objectives[ObjPicking].icon = std::clamp(std::atoi(command.c_str() + 6), 0, ObjectiveIconCount - 1);
				ObjPicking = -1;
			} else {
				const std::size_t digits = command.find_first_of("0123456789");
				const int         i      = digits == std::string::npos ? -1 : std::atoi(command.c_str() + digits);
				if (i < 0 || i >= static_cast<int>(Objectives.size())) return;
				ObjPicking = -1;
				if (command.rfind("objpick", 0) == 0) ObjPicking = i;
				else if (command.rfind("objon", 0) == 0) Objectives[i].enabled = !Objectives[i].enabled;
				else if (command.rfind("objup", 0) == 0) {
					if (i > 0) std::swap(Objectives[i], Objectives[i - 1]);
				} else if (command.rfind("objdown", 0) == 0) {
					if (i + 1 < static_cast<int>(Objectives.size())) std::swap(Objectives[i], Objectives[i + 1]);
				} else {
					if (CheckedIds.erase("o:" + std::to_string(Objectives[i].id)) && !StorePath.empty()) SaveStore();
					Objectives.erase(Objectives.begin() + i);
				}
			}
			if (command.rfind("objpage", 0) != 0 && command.rfind("objpick", 0) != 0) {
				BuildObjectives();
				RebuildStored(); // the tiles' names are their own texts: nothing to register again
			}
			++Config.version;
		}
		if (command.rfind("objpage", 0) != 0 && command.rfind("objpick", 0) != 0) SaveSettings();
		ShowView();
		return;
	}
	// The settings' pages, and the misc tracking page's own pages.
	if (command.rfind("spage", 0) == 0 || command.rfind("miscpage", 0) == 0) {
		{
			std::lock_guard guard(Lock);
			OpenDrop = -1;
			if (command[0] == 's') SettingsPage = std::clamp(std::atoi(command.c_str() + 5), 0, SettingsPageCount - 1);
			else MiscFirst = std::max(0, MiscFirst + (command.back() == '-' ? -MiscPerPage : MiscPerPage));
			++Config.version;
		}
		ShowView();
		return;
	}
	// The language (lang0: the game's, lang<n>: Languages[n - 1]): the lists'
	// names are read again on the game thread, the page follows.
	if (command.rfind("lang", 0) == 0 && command.size() > 4) {
		const int chosen = std::clamp(std::atoi(command.c_str() + 4) - 1, -1, LanguageCount - 1);
		{
			std::lock_guard guard(Lock);
			OpenDrop        = -1;
			Config.language = chosen;
			++Config.version;
		}
		SaveSettings();
		Say(false, "grailon: language %s", chosen < 0 ? "of the game" : Languages[chosen]);
		ListsWanted = true;
		ShowView();
		return;
	}
	// The inventory button's place: a step of 1 (10 with Shift, 100 with Ctrl), or back to the default.
	if (command.rfind("btn", 0) == 0) {
		{
			std::lock_guard guard(Lock);
			if (command == "btnreset") Config.buttonX = DefaultButtonX, Config.buttonY = DefaultButtonY;
			else if (command.size() == 5) {
				const int step  = (GetKeyState(VK_CONTROL) & 0x8000) ? 100 : (GetKeyState(VK_SHIFT) & 0x8000) ? 10 : 1;
				int&      value = command[3] == 'X' ? Config.buttonX : Config.buttonY;
				value           = std::clamp(value + (command[4] == '-' ? -step : step), -1000, 3000);
			}
			++Config.version;
		}
		SaveSettings();
		MoveInventoryButton();
		ShowView();
		return;
	}
	const bool choice = command.size() == 6 && (command.rfind("goal", 0) == 0 || command.rfind("loot", 0) == 0);
	if (command.rfind("toggle:", 0) == 0 || (command.size() == 8 && command.rfind("compare", 0) == 0) || choice) {
		{
			std::lock_guard guard(Lock);
			OpenDrop = -1;
			if (choice) {
				const int k = command[4] - '0', mode = std::clamp(command[5] - '0', 0, 2);
				if (command[0] == 'g' && k >= 0 && k < 4) Config.goal[k] = mode;
				if (command[0] == 'l' && k >= 0 && k < 3) Config.loot[k] = mode;
			} else if (command[0] == 'c') Config.compare = std::clamp(command[7] - '0', 0, 2);
			else
				for (const auto& toggle : Toggles)
					if (command.compare(7, std::string::npos, toggle.key) == 0) Config.*toggle.field = !(Config.*toggle.field);
			++Config.version;
		}
		ApplyTooltipSettings();
		SaveSettings();
		if (command == "toggle:compact") CompactChanged();
		if (command == "toggle:objectives") {
			std::lock_guard guard(Lock);
			BuildObjectives();
			RebuildStored();
		}
		ShowView();
		return;
	}
	{
		std::lock_guard guard(Lock);
		if (command.rfind("menu", 0) == 0) {
			// A menu tab: its category shown last, or its first.
			const int menu = std::atoi(command.c_str() + 4);
			constexpr int none = -100;
			int           pick = menu == ConfigMenu ? SettingsView : menu == StatsMenu ? StatsView : none;
			if (menu >= WeaponMenu && menu <= ObjMenu) {
				if (const auto last = LastInMenu.find(menu); last != LastInMenu.end()) pick = last->second;
				for (int c = 0; c < CategoryCount() && pick < 0; ++c)
					if (InMenu(c, menu) && pick == none) pick = c;
			}
			if (pick == none && menu == ObjMenu) pick = SettingsView, SettingsPage = 4; // no objective yet: where they are made
			if (pick == none || (pick == CurrentCategory && (pick < 0 || ViewEth == IsEthMenu(menu)))) return;
			if (pick < 0) BarMax = 0;
			CurrentCategory = pick;
			ViewEth         = IsEthMenu(menu);
			Scroll          = 0;
		} else if (command.rfind("row", 0) == 0) Scroll = std::clamp(std::atoi(command.c_str() + 3), 0, MaxScroll);
		else if (command.rfind("step", 0) == 0) {
			const int step = std::clamp(std::atoi(command.c_str() + 4), 0, ScrollSteps - 1);
			LearnBar(step);
			Scroll = StepRow(step, MaxScroll);
		}
		else if (command.rfind("stats", 0) == 0) {
			StatsPage       = std::clamp(std::atoi(command.c_str() + 5), 0, KindCountAll);
			if (!StatsPageShown(StatsPage)) StatsPage = 0;
			CurrentCategory = StatsView;
		} else if (command.rfind("open", 0) == 0) {
			// A stats tile: its category, plain or ethereal.
			CurrentCategory = std::clamp(std::atoi(command.c_str() + 4), 0, CategoryCount() - 1);
			ViewEth         = command.back() == 'e';
			Scroll          = 0;
		} else if (command.rfind("cat", 0) == 0) {
			CurrentCategory = std::clamp(std::atoi(command.c_str() + 3), 0, CategoryCount() - 1);
			Scroll          = 0;
		} else return;
	}
	ShowView();
}

// Several wheel notches become one view change.
void __cdecl ScrollFrame(const D2RL::PluginContext*, void*) noexcept;

// Shows the next frame of the scroll (UI thread); after the last, the next
// view opens (when it is ready, else as soon as it is) and closes the panel.
void __cdecl ScrollFrame(const D2RL::PluginContext*, void*) noexcept {
	if (!Anim.running) return;
	const int count = static_cast<int>(Anim.frames.size());
	if (Anim.frame < count) {
		for (int k = 0; k < count; ++k) (void)WidgetApi->setWidgetVisible(Context, Anim.frames[k], k == Anim.frame);
		++Anim.frame;
		std::thread([] {
			std::this_thread::sleep_for(std::chrono::milliseconds(std::clamp(Config.frameMs, 5, 500)));
			if (Threads->runOnUiThread(Context, ScrollFrame, nullptr) != D2RL::Threads::Result::Success) Anim.running = false;
		}).detach();
		return;
	}
	// The last frame stays until the next view covers it.
	Anim.running        = false;
	const auto deferred = std::exchange(Anim.deferred, 0u);
	if (deferred) OpenPrepared(deferred); // the queued steps keep the cursor item aside
	const int queued = std::exchange(Anim.queued, 0);
	if (queued && (ViewOpen() || PreparedView >= 0)) RunScroll(queued, true);
	else KickPending();
}

// Opens the scroll panel for one row down (or up) from Scroll, its first frame
// showing. False when it cannot (the step then just jumps).
bool AnimateScroll(bool down) {
	if (ShownView < 0 || !WidgetApi || !Config.scrollAnimations || ScrollPanel == D2RL::Panels::InvalidHandle) return false;
	std::string layout;
	{
		std::lock_guard guard(Lock);
		if (CurrentCategory < 0) return false;
		layout = ScrollLayout(down);
	}
	if (layout.empty()) return false;
	CloseScrollPanel();
	if (ScrollLoaded) {
		const D2RL::Widgets::UiAction unload { .structSize = D2RL::Widgets::UiActionSize, .flags = 0, .target = "PanelManager", .command = "UnloadPanel", .text = "grail-on/Scroll" };
		(void)WidgetApi->dispatchUiAction(Context, &unload);
	}
	if (ScrollLayoutHandle != D2RL::Resources::InvalidHandle) (void)ResourceApi->unregisterResource(Context, ScrollLayoutHandle);
	ScrollLayoutHandle = D2RL::Resources::InvalidHandle;
	if (!RegisterResource("data/global/ui/layouts/grail-on/Scrollhd.json", layout, &ScrollLayoutHandle)) return false;
	Trace("scroll frames open", down ? 1 : -1);
	if (PanelApi->openPanel(Context, ScrollPanel) != D2RL::Panels::Result::Success) return false;
	Trace("scroll frames opened");
	ScrollLoaded = true;
	D2RL::Widgets::WidgetHandle panel {}, widget {};
	ScrollAnimation             anim { .running = true, .down = down };
	if (WidgetApi->findPanel(Context, "grail-on/Scroll", &panel) == D2RL::Widgets::Result::Success)
		for (int k = 1; k < ScrollFrames; ++k)
			if (WidgetApi->findWidget(Context, panel, ("Frame" + std::to_string(k)).c_str(), &widget) == D2RL::Widgets::Result::Success) anim.frames.push_back(widget);
	if (anim.frames.empty()) {
		CloseScrollPanel();
		return false;
	}
	for (std::size_t k = 1; k < anim.frames.size(); ++k) (void)WidgetApi->setWidgetVisible(Context, anim.frames[k], false);
	anim.frame = 1;
	Anim       = std::move(anim);
	return true;
}

void __cdecl ScrollAfterHold(const D2RL::PluginContext*, void*) noexcept {
	Holding = false;
	if (const int rows = std::exchange(HoldRows, 0); rows) RunScroll(rows, true);
}

void RunScroll(int rows, bool afterHold) {
	Trace("scroll", rows);
	if (Anim.running) {
		Anim.queued += rows; // after the frames on screen
		return;
	}
	// The cursor item is kept aside first (the view change lost it).
	if (Holding) {
		HoldRows += rows;
		return;
	}
	if (!afterHold && Config.hold && ViewOpen() && HoldCursor(ScrollAfterHold, nullptr)) {
		Holding  = true;
		HoldRows = rows;
		return;
	}
	{
		std::lock_guard guard(Lock);
		const int       before = Scroll;
		// Down stops once the last row is on screen.
		Scroll = std::clamp(Scroll + rows, 0, MaxScroll);
		if (Scroll == before) return;
		rows   = Scroll - before;
		Scroll = before; // the frames start from the view on screen
	}
	// One row at a time, its frames showing while the next view is made; the
	// other rows after it (several wheel steps at once no longer jump).
	int later = 0;
	if (Config.scrollAnimations && (rows > 1 || rows < -1)) later = rows - (rows > 0 ? 1 : -1), rows -= later;
	// Not with an item on the cursor: opening the frames' panel makes the game
	// put it back where it came from (grailon trace showed the cursor emptied).
	const bool animate = (rows == 1 || rows == -1) && !CursorHasItem() && AnimateScroll(rows > 0);
	if (!animate) rows += std::exchange(later, 0);
	{
		std::lock_guard guard(Lock);
		Scroll += rows;
	}
	Anim.queued += later;
	ShowView();
	if (animate) {
		std::thread([] {
			std::this_thread::sleep_for(std::chrono::milliseconds(std::clamp(Config.frameMs, 5, 500)));
			if (Threads->runOnUiThread(Context, ScrollFrame, nullptr) != D2RL::Threads::Result::Success) Anim.running = false;
		}).detach();
	}
}

// ---- mouse wheel and scroll bar dragging ----
// No plugin service reports the wheel or the mouse moving, so the game window
// is subclassed: while the view is open, the wheel over the left part of the
// screen scrolls it, and the left button held on the scroll bar drags it.

WNDPROC GameProc {};

// Where the panel is in the window: window = panel * scale + offset. The game
// draws the UI at the window's height / 2160; the panel hangs from
// the vanilla $LeftPanelAnchor place (x 0, y 39.7% of the height) at x 236, y -856. A click on a
// scroll step puts the offset where the click was.
std::atomic<bool>   BarLearned {};
std::atomic<double> BarOffsetX {}, BarOffsetY {};
std::atomic<int>    DragLast { -1 };
bool                Dragging {}; // window thread

double UiScale() {
	RECT client {};
	return GameWindow && GetClientRect(GameWindow, &client) && client.bottom > 0 ? client.bottom / 2160.0 : 0;
}

std::pair<double, double> PanelOffset() {
	if (BarLearned) return { BarOffsetX, BarOffsetY };
	RECT client {};
	if (!GameWindow || !GetClientRect(GameWindow, &client)) return {};
	const double scale = client.bottom / 2160.0;
	return { WindowX() * scale, client.bottom * 0.397 + PanelY * scale };
}

// UI thread, on a click on step `step`: the cursor is on that step.
void LearnBar(int step) {
	POINT        cursor {};
	const double scale = UiScale();
	if (!scale || !GetCursorPos(&cursor) || !ScreenToClient(GameWindow, &cursor)) return;
	BarOffsetX = cursor.x - (BarLeft + BarWidth / 2.0) * scale;
	BarOffsetY = cursor.y - (BarTop + (step + 0.5) * StepHeight) * scale;
	if (!BarLearned.exchange(true)) Say(false, "grailon: scroll bar found at %ld, %ld in the window", cursor.x, cursor.y);
}

int StepRow(int step, int maxScroll) { return (maxScroll * step + (ScrollSteps - 1) / 2) / (ScrollSteps - 1); }

// The step under a window point, or -1 when the point is off the bar (with
// `loose`, any height counts: a drag goes on above and below the bar).
int StepAt(POINT point, bool loose) {
	const double scale = UiScale();
	if (!scale) return -1;
	const auto [ox, oy] = PanelOffset();
	const double x = (point.x - ox) / scale, y = (point.y - oy) / scale - BarTop;
	if (!loose && (x < BarLeft - 12 || x > BarLeft + BarWidth + 12 || y < 0 || y >= BarHeight)) return -1;
	return std::clamp(static_cast<int>(std::floor(y / StepHeight)), 0, ScrollSteps - 1);
}

void DragTo(POINT point) {
	const int max  = BarMax;
	const int step = StepAt(point, true);
	if (max <= 0 || step < 0) return;
	const int row = StepRow(step, max);
	if (DragLast.exchange(row) != row) Queue("row" + std::to_string(row));
}

LRESULT CALLBACK WheelProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
	// Middle click on a hovered grail item: shown in the grail.
	if (message == WM_MBUTTONDOWN && Config.middleClick && ListsReady && !ScrollTestOpen) {
		MiddleSeenAt = GetTickCount64(); // seen here: the poll leaves it
		if (MiddleClickNow("window")) return 0;
	}
	if (message == WM_MOUSEWHEEL && ViewIsOpen) {
		POINT point { static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
		RECT  client {};
		if (ScreenToClient(window, &point) && GetClientRect(window, &client) && point.x < client.right * 55 / 100) {
			Queue(static_cast<short>(HIWORD(wParam)) > 0 ? "up" : "down");
			return 0;
		}
	}
	const POINT point { static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
	// A press with an item in hand is a new click, never the one that took it
	// out: the item may go back in.
	if (message == WM_LBUTTONDOWN && ViewIsOpen && CursorHasItem()) TakenNow = 0;
	// A click on a stored item with nothing on the cursor picks it up: its
	// tile shows empty from now on, while the game moves it (as Ctrl+click).
	if (message == WM_LBUTTONDOWN && ViewIsOpen && !(wParam & (MK_CONTROL | MK_SHIFT)) && !CursorHasItem()) {
		const auto      hovered = HoveredItem.load();
		Items::ItemInfo item { .structSize = Items::ItemInfoSize };
		bool            refresh = false;
		if (hovered && GetTickCount64() - HoveredAt.load() <= 100 && GetInfo(Context, hovered, &item) == Items::Result::Success
		    && item.container == Items::ItemContainer::CustomPage && TakenNow != item.runtimeId) {
			std::lock_guard guard(Lock);
			if (std::any_of(Stored.begin(), Stored.end(), [&](const auto& stored) { return stored.second == item.runtimeId; })) {
				TakenNow = item.runtimeId;
				refresh  = PredictTakeOut(item.runtimeId);
			}
		}
		if (refresh) Queue("refresh");
	}
	if (message == WM_LBUTTONDOWN && ViewIsOpen && BarMax > 0 && StepAt(point, false) >= 0) {
		Dragging = true;
		DragLast = -1;
	} else if (message == WM_MOUSEMOVE && Dragging) {
		if ((wParam & MK_LBUTTON) && ViewIsOpen) DragTo(point);
		else Dragging = false;
	} else if (message == WM_LBUTTONUP) Dragging = false;
	if ((message == WM_LBUTTONUP || message == WM_RBUTTONUP) && ObjTyping) TypeClickAt = GetTickCount64();
	return CallWindowProcW(GameProc, window, message, wParam, lParam);
}

BOOL CALLBACK FindWindowOfGame(HWND window, LPARAM found) {
	DWORD process = 0;
	GetWindowThreadProcessId(window, &process);
	if (process != GetCurrentProcessId() || !IsWindowVisible(window) || GetWindow(window, GW_OWNER)) return TRUE;
	RECT rect {}, best {};
	auto* result = reinterpret_cast<HWND*>(found);
	GetWindowRect(window, &rect);
	if (*result) GetWindowRect(*result, &best);
	if (!*result || (rect.right - rect.left) * (rect.bottom - rect.top) > (best.right - best.left) * (best.bottom - best.top)) *result = window;
	return TRUE;
}

void HookWheel() {
	if (GameWindow) return;
	HWND window {};
	EnumWindows(FindWindowOfGame, reinterpret_cast<LPARAM>(&window));
	if (!window) return;
	GameProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WheelProc)));
	if (GameProc) GameWindow = window;
	else Problem(false, "grailon: could not watch the mouse wheel (error %lu); use the arrows", GetLastError());
}

void UnhookWheel() {
	if (GameWindow && reinterpret_cast<WNDPROC>(GetWindowLongPtrW(GameWindow, GWLP_WNDPROC)) == WheelProc)
		SetWindowLongPtrW(GameWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(GameProc));
	GameWindow = {};
}

// ---- ticks, buttons and console ----

void WatchScrollTest();

std::atomic<bool> Running {};
std::thread       Ticker;
std::string       PendingCommand;   // the last non-scroll command
int               PendingScroll {}; // rows to scroll, summed until the UI thread runs them
bool              CommandScheduled {};
std::mutex        CommandLock;

// UI thread: runs what the buttons, wheel and console asked for.
void __cdecl RunPending(const D2RL::PluginContext*, void*) noexcept {
	std::string command;
	int         rows = 0;
	{
		std::lock_guard guard(CommandLock);
		command.swap(PendingCommand);
		std::swap(rows, PendingScroll);
		CommandScheduled = false;
	}
	if (!command.empty()) RunCommand(command);
	if (rows && ViewOpen()) RunScroll(rows);
}

// Registers every text the views use, replacing the previous ones.
bool RegisterStrings() {
	GrailHeader = Tr("Currently in Grail");
	if (StringsHandle != D2RL::Resources::InvalidHandle) (void)ResourceApi->unregisterResource(Context, StringsHandle);
	StringsHandle = D2RL::Resources::InvalidHandle;
	if (RegisterResource(StringsPath, StringsJson(), &StringsHandle)) return true;
	StringsHandle = D2RL::Resources::InvalidHandle;
	Say(true, "grailon: could not register the texts");
	return false;
}

// One empty slot sprite per tile size the lists need.
// The rune stones, one sprite per cell of the Chronicle's sheet, once.
void RegisterRunes() {
	const std::string& file = ChronicleRunesFile;
	if (GenericRunes || file.size() < 0x28 || file.compare(0, 4, "SpA1")) return;
	const auto u32 = [&](std::size_t at) { std::uint32_t v; std::memcpy(&v, file.data() + at, 4); return v; };
	const int  width = static_cast<int>(u32(8)), height = static_cast<int>(u32(12));
	if (width < Cell || height < Cell || file.size() < 0x28 + static_cast<std::size_t>(width) * height * 4) return;
	const int columns = width / Cell, count = columns * (height / Cell);
	int       made    = 0;
	for (int k = 0; k < count; ++k) {
		Picture cell { Cell, Cell, std::vector<unsigned char>(static_cast<std::size_t>(Cell) * Cell * 4) };
		bool    empty = true;
		for (int y = 0; y < Cell; ++y) {
			std::memcpy(cell.at(0, y), file.data() + 0x28 + (static_cast<std::size_t>((k / columns) * Cell + y) * width + (k % columns) * Cell) * 4, static_cast<std::size_t>(Cell) * 4);
			for (int x = 0; x < Cell && empty; ++x) empty = cell.at(x, y)[3] == 0;
		}
		if (empty || !RegisterSprite("grune" + std::to_string(k), { cell })) break;
		made = k + 1;
	}
	// The blank stone: each pixel the brighter quarter's edge of all the
	// stones' (the glyphs are dark and never in the same place).
	if (made >= 8) {
		Picture blank { Cell, Cell, std::vector<unsigned char>(static_cast<std::size_t>(Cell) * Cell * 4) };
		std::vector<unsigned char> values(made);
		for (int y = 0; y < Cell; ++y)
			for (int x = 0; x < Cell; ++x)
				for (int c = 0; c < 4; ++c) {
					for (int k = 0; k < made; ++k) values[k] = static_cast<unsigned char>(file[0x28 + (static_cast<std::size_t>((k / columns) * Cell + y) * width + (k % columns) * Cell + x) * 4 + c]);
					std::nth_element(values.begin(), values.begin() + made * 3 / 4, values.end());
					blank.at(x, y)[c] = values[made * 3 / 4];
				}
		if (!RegisterSprite("grune-blank", { blank })) made = 0;
	}
	GenericRunes = made;
}

void RegisterSlots() {
	static bool peeks = false;
	if (!peeks) {
		peeks = true;
		for (int width = 1; width <= 2; ++width)
			for (int halves = 1; halves <= 8; ++halves)
				if (!RegisterSprite("peek" + std::to_string(width) + "x" + std::to_string(halves), { PeekBox(width * Cell, halves * Cell / 2) })) Say(true, "grailon: could not register a cut slot sprite");
		Picture clear { 4, 4, std::vector<unsigned char>(64) };
		if (!RegisterSprite("blank", { clear, clear })) Say(true, "grailon: could not register the blank button");
	}
	static std::set<std::pair<int, int>> done;
	for (const auto& entry : Uniques) {
		const int width = std::clamp(entry.width, 1, 4), height = std::clamp(entry.height, 1, 4);
		if (!done.insert({ width, height }).second) continue;
		if (!RegisterSprite("slot" + std::to_string(width) + "x" + std::to_string(height), { Slot(width * Cell, height * Cell) })) Say(true, "grailon: could not register a slot sprite");
	}
	// The search's veils, one per tile size (any size a tile can have).
	static bool veils = false;
	if (!veils) {
		veils = true;
		for (int width = 1; width <= 4; ++width)
			for (int height = 1; height <= 4; ++height) {
				Picture veil { width * Cell, height * Cell, std::vector<unsigned char>(static_cast<std::size_t>(width) * Cell * height * Cell * 4) };
				for (std::size_t p = 3; p < veil.rgba.size(); p += 4) veil.rgba[p] = 175;
				if (!RegisterSprite("dim" + std::to_string(width) + "x" + std::to_string(height), { veil })) Say(true, "grailon: could not register a search veil");
			}
	}
}

// The grail button on the inventory's left edge, in the same stone and gold
// mold as Charm Inventory's and Automatic Materials Deposit's (3, 813), by
// default under the deposit's, where the settings put it.
D2RL::Resources::RegistrationHandle ButtonLayout = D2RL::Resources::InvalidHandle;
std::atomic<bool>                   ButtonReload {}; // moved: the inventory loads its layout again once it is closed
void RegisterInventoryButton() {
	char button[1024] {};
	std::snprintf(button, sizeof(button),
	              R"({"type": "ImageWidget", "name": "grail-on/InventoryButton", "fields": {"rect": {"x": %d, "y": %d, "width": %d, "height": %d}, "filename": "d2rloader/grail-on/mold"}, "children": [)"
	              R"({"type": "ButtonWidget", "name": "grail-on/GrailButton", "fields": {"rect": {"x": 1, "y": 39}, "filename": "d2rloader/grail-on/grailbutton", "normalFrame": 0, "pressedFrame": 1, "hoveredFrame": 3, )"
	              R"("pressLabelOffset": {"x": 0, "y": 2}, "tooltipString": "@grail-on:GrailTip", "sound": "cursor_menu_select_hd", "onClickMessage": %s}}]})",
	              Config.buttonX, Config.buttonY, MoldWidth, MoldHeight, Command("open").c_str());
	if (ButtonLayout != D2RL::Resources::InvalidHandle) (void)ResourceApi->unregisterResource(Context, ButtonLayout);
	ButtonLayout = D2RL::Resources::InvalidHandle;
	if (!RegisterResource("data/global/ui/layouts/grail-on/InventoryButtonhd.json", button, &ButtonLayout)) ButtonLayout = D2RL::Resources::InvalidHandle;
}

constexpr const char* InventoryPanels[] { "PlayerInventoryExpansionLayout", "PlayerInventoryOriginalLayout", "PlayerInventoryExpansionLayoutHD", "PlayerInventoryOriginalLayoutHD" };

// UI thread. The settings moved the button: at once where D2RLoader can move
// a widget (setWidgetRect), else in the game's own widget: the next time the
// inventory's grid is drawn (HookGridDraw), the mold is found in the
// inventory's widget tree (its size, its place, its one child at 1, 39) and
// its local rect (+0x70, read at every draw) is written. The layout is
// registered again too, for the next time the game loads it.
std::atomic<bool> ButtonMoveWanted {};
int               ButtonShownX = 0, ButtonShownY = 0; // where the game's widget is (the layout's place when it was loaded); UI thread
bool              ButtonShownKnown = false;
void MoveInventoryButton() {
	RegisterInventoryButton();
	namespace W = D2RL::Widgets;
	bool moved = false;
	if (WidgetApi && D2RL::HasWidgetServiceField(WidgetApi, D2RL::WidgetServiceSetWidgetRectFieldEnd) && WidgetApi->setWidgetRect)
		for (const char* name : InventoryPanels) {
			W::WidgetHandle panel {}, widget {};
			if (WidgetApi->findPanel(Context, name, &panel) != W::Result::Success || WidgetApi->findWidget(Context, panel, "grail-on/InventoryButton", &widget) != W::Result::Success) continue;
			const W::Rect rect { Config.buttonX, Config.buttonY, MoldWidth, MoldHeight };
			if (WidgetApi->setWidgetRect(Context, widget, &rect) == W::Result::Success) moved = true;
		}
	if (!moved) ButtonMoveWanted = true;
}

// UI thread (a grid's draw). The mold in the widget tree under `root`, at
// (x, y): a widget of the mold's size with one child at 1, 39.
constexpr std::size_t WidgetParent = 0x30, WidgetChildren = 0x58, WidgetChildCount = 0x60, WidgetFills = 0x52, WidgetRect = 0x70;
std::uint8_t* FindMold(std::uint8_t* widget, int x, int y, int depth) {
	if (!widget || depth > 8) return nullptr;
	const auto* rect = reinterpret_cast<const std::int32_t*>(widget + WidgetRect);
	auto**      children = *reinterpret_cast<std::uint8_t***>(widget + WidgetChildren);
	const auto  count    = *reinterpret_cast<const std::uint64_t*>(widget + WidgetChildCount);
	if (!children || count > 512) return nullptr;
	if (!widget[WidgetFills] && rect[0] == x && rect[1] == y && rect[2] == MoldWidth && rect[3] == MoldHeight && count == 1 && children[0]) {
		const auto* inner = reinterpret_cast<const std::int32_t*>(children[0] + WidgetRect);
		if (inner[0] == 1 && inner[1] == 39) return widget;
	}
	for (std::uint64_t i = 0; i < count; ++i)
		if (auto* found = FindMold(children[i], x, y, depth + 1)) return found;
	return nullptr;
}
void MoveMoldFrom(void* grid) {
	if (!ButtonMoveWanted) return;
	auto* root = static_cast<std::uint8_t*>(grid);
	for (int up = 0; up < 12; ++up) {
		auto* parent = *reinterpret_cast<std::uint8_t**>(root + WidgetParent);
		if (!parent) break;
		root = parent;
	}
	// Already at the new place (the layout was loaded again): nothing to do.
	if (FindMold(root, Config.buttonX, Config.buttonY, 0)) {
		ButtonShownX = Config.buttonX, ButtonShownY = Config.buttonY, ButtonShownKnown = true;
		ButtonMoveWanted = false;
		return;
	}
	if (!ButtonShownKnown) return;
	auto* mold = FindMold(root, ButtonShownX, ButtonShownY, 0);
	if (!mold) return; // not this grid's panel
	auto* rect = reinterpret_cast<std::int32_t*>(mold + WidgetRect);
	rect[0] = Config.buttonX, rect[1] = Config.buttonY;
	Say(false, "grailon: grail button moved from %d,%d to %d,%d", ButtonShownX, ButtonShownY, Config.buttonX, Config.buttonY);
	ButtonShownX = Config.buttonX, ButtonShownY = Config.buttonY;
	ButtonMoveWanted = false;
}

// UI thread. A moved button's inventory layout, unloaded while the inventory
// is closed: when D2RLoader tells whether it is shown, else once out of a game.
void ReloadInventoryButton() {
	if (!ButtonReload || !WidgetApi) return;
	namespace W = D2RL::Widgets;
	const bool stateKnown = D2RL::HasWidgetServiceField(WidgetApi, D2RL::WidgetServiceGetWidgetStateFieldEnd) && WidgetApi->getWidgetState;
	if (!stateKnown && LocalPlayer.load()) return;
	std::vector<const char*> loaded;
	for (const char* name : InventoryPanels) {
		W::WidgetHandle panel {};
		if (WidgetApi->findPanel(Context, name, &panel) != W::Result::Success) continue;
		W::State state {};
		if (stateKnown && (WidgetApi->getWidgetState(Context, panel, &state) != W::Result::Success || state.visible)) return; // open: after it closes
		loaded.push_back(name);
	}
	ButtonReload = false;
	for (const char* name : loaded) {
		const W::UiAction unload { .structSize = W::UiActionSize, .flags = 0, .target = "PanelManager", .command = "UnloadPanel", .text = name };
		const auto        result = WidgetApi->dispatchUiAction(Context, &unload);
		Say(false, "grailon: the inventory (%s) loads again with the grail button at %d, %d (result %u)", name, Config.buttonX, Config.buttonY, static_cast<unsigned>(result));
	}
}

// The inventory button, looked up each time the inventory opens: whether the
// game loaded it, where, and whether it is shown (logged once per change; shown
// again when something hid it).
void WatchInventoryButton() {
	if (!WidgetApi) return;
	namespace W = D2RL::Widgets;
	static std::string told;
	W::WidgetHandle    panel {};
	const char*        panelName = nullptr;
	for (const char* name : InventoryPanels)
		if (WidgetApi->findPanel(Context, name, &panel) == W::Result::Success) {
			panelName = name;
			break;
		}
	if (!panelName) return;
	if (D2RL::HasWidgetServiceField(WidgetApi, D2RL::WidgetServiceGetWidgetStateFieldEnd)) {
		W::State state {};
		if (WidgetApi->getWidgetState(Context, panel, &state) != W::Result::Success || !state.visible) return;
	}
	std::string report = std::string("inventory button in ") + panelName + ":";
	for (const char* name : { "grail-on/InventoryButton", "grail-on/GrailButton", "bulk-currency-deposit/inventory-button" }) {
		W::WidgetHandle widget {};
		W::Result       found = WidgetApi->findWidget(Context, panel, name, &widget);
		W::WidgetHandle inner {};
		if (found != W::Result::Success && !std::strcmp(name, "grail-on/GrailButton")) {
			W::WidgetHandle root {};
			if (WidgetApi->findWidget(Context, panel, "grail-on/InventoryButton", &root) == W::Result::Success
			    && WidgetApi->findWidget(Context, root, name, &inner) == W::Result::Success)
				widget = inner, found = W::Result::Success;
		}
		char line[256];
		if (found != W::Result::Success) {
			std::snprintf(line, sizeof(line), " %s not found (%u);", name, static_cast<unsigned>(found));
			report += line;
			continue;
		}
		W::Rect rect {};
		(void)WidgetApi->getWidgetRect(Context, widget, &rect);
		W::State state { true, true, false };
		const bool stateKnown = D2RL::HasWidgetServiceField(WidgetApi, D2RL::WidgetServiceGetWidgetStateFieldEnd) && WidgetApi->getWidgetState(Context, widget, &state) == W::Result::Success;
		W::ScreenRect screen {};
		const bool    screenKnown = D2RL::HasWidgetServiceField(WidgetApi, D2RL::WidgetServiceGetWidgetScreenRectFieldEnd) && WidgetApi->getWidgetScreenRect(Context, widget, &screen) == W::Result::Success;
		std::snprintf(line, sizeof(line), " %s at %d,%d %dx%d, %s, screen %s%.0f,%.0f %.0fx%.0f;", name, rect.x, rect.y, rect.width, rect.height,
		              stateKnown ? (state.visible ? "visible" : "HIDDEN") : "visibility unknown", screenKnown ? "" : "unknown ", screen.x, screen.y, screen.width, screen.height);
		report += line;
		if (stateKnown && !state.visible && !std::strncmp(name, "grail-on/", 9)) (void)WidgetApi->setWidgetVisible(Context, widget, true);
	}
	if (report != told) Say(false, "grailon: %s", report.c_str());
	told = report;
}

void RemoteSweep();
void __cdecl UiTick(const D2RL::PluginContext* context, void*) noexcept {
	UiThread = GetCurrentThreadId();
	UnmuteSoundsWhenDue();
	ReloadInventoryButton();
	WatchInventoryButton();
	D2RL::PlayerHandle player {};
	LocalPlayer = Inventory->getLocalPlayer(context, &player) == D2RL::Inventory::Result::Success ? player : D2RL::PlayerHandle {};
	// Each character opens the storage of its kind (softcore/hardcore); its
	// page is checked against the records once.
	if (static D2RL::PlayerHandle seen {}; player != seen) {
		std::lock_guard guard(Lock);
		if (seen && StoreOwner == seen) SaveStore();
		seen = player;
		Ghosts.clear(), GhostWanted.clear(); // that game's: freed with it
		ServerGame = nullptr;
		if (player) OpenStoreFor(player), StaleChecked = false;
	}
	// What the loot filter hides follows the grail.
	if (LootOn) {
		std::lock_guard guard(Lock);
		PublishLootTable();
	}
	// The lists come from the game's tables, read on the game thread once
	// they are loaded; their texts and slots are registered here after.
	// (A joined TCP/IP game runs no game-thread jobs: the tables and files are
	// read from here, the same main thread.)
	if (ListsWanted.exchange(false) && Threads->runOnGameThread(context, BuildFromGame, nullptr) != D2RL::Threads::Result::Success) BuildFromGame(context, nullptr);
	if (!StringsWanted.exchange(false)) {
	} else if (LanguageRefresh.exchange(false)) {
		// The lists again in a new language: only the texts change.
		(void)RegisterStrings();
		{
			std::lock_guard guard(Lock);
			++Config.version;
		}
		if (ViewIsOpen) ShowView();
	} else {
		RegisterRunes();
		// The grail button stays the icon alone in the mold: the Advanced Stats
		// button under it drew a second square frame.
		if (static bool settingsDone = false; !settingsDone && SettingsArtRead) {
			settingsDone = true;
			RegisterSettingsArt();
		}
		RegisterSlots();
		RegisterGlows();
		(void)RegisterStrings();
		if (ViewAfterStrings.exchange(false)) ShowView();
	}
	if (player && DumpWanted.exchange(false)) (void)Threads->runOnGameThread(context, Dump, nullptr);
	// A stored item just picked up from its tile: the tile shows empty on this
	// frame (the page policy only hears of it on the next click or move).
	// Only an item new on the cursor: one being dropped is in Stored already.
	static std::uint32_t heldBefore = 0;
	std::uint32_t        heldNow    = 0;
	if (D2RL::ItemHandle held {}; ViewIsOpen && player && Inventory->getCursorItem(context, player, &held) == D2RL::Inventory::Result::Success && held) {
		Items::ItemInfo item { .structSize = Items::ItemInfoSize };
		bool            refresh = false;
		if (GetInfo(context, held, &item) == Items::Result::Success) heldNow = item.runtimeId;
		if (heldNow && heldNow != heldBefore && TakenNow != item.runtimeId) {
			std::lock_guard guard(Lock);
			if (std::any_of(Stored.begin(), Stored.end(), [&](const auto& stored) { return stored.second == item.runtimeId; })) {
				TakenNow = item.runtimeId;
				refresh  = PredictTakeOut(item.runtimeId);
			}
		}
		if (refresh) Queue("refresh");
	}
	heldBefore = heldNow;
	RunPending(context, nullptr);
	if (!ViewIsOpen && LifeTextHidden) HideLifeText(false);
	// The scroll panel only lives through a scroll: an open one blocks the game
	// world (chests, NPCs), so it never stays once the frames are done and no
	// view is on its way (or the view was closed with Escape).
	if (!Anim.running && (PreparedView < 0 || !ViewOpen())) CloseScrollPanel();
	KickPending(); // a kept cursor item never stays kept
	ObjInputTick();
	WatchMiddleClick();
	if (ViewRetryAt && GetTickCount64() >= ViewRetryAt) {
		ViewRetryAt = 0;
		if (ViewOpen() || PreparedView >= 0) ShowView();
	}
	WatchScrollTest();
	{
		// Closed (or a character just in): the page is emptied once.
		static bool wasOpen = false;
		const bool  open = ViewOpen();
		if (player && !open && PreparedView < 0 && (wasOpen || ClearedFor.load() != player) && !PageClearing.exchange(true)) {
			if (Remote()) RemoteSweep(), ClearedFor = player, PageClearing = false;
			else if (Host::On() && PageHost::HasRequests(Host::Service) && Host::Service->itemsWhere() == PageHost::ItemsWhere::RemoteNoHost) {
				static D2RL::PlayerHandle told {};
				if (told != player) Problem(false, "grailon: in this joined TCP/IP game the hosting player has no Player Page Host 1.0.2 or later: items can't go in or out of the grail"), told = player;
				ClearedFor = player, PageClearing = false;
			}
			else if (Threads->runOnGameThread(context, ClearPage, nullptr) != D2RL::Threads::Result::Success) PageClearing = false;
		}
		wasOpen = open;
	}
	ViewIsOpen = ViewOpen();
	SyncArea();
	if (!ViewIsOpen && player && !GhostsFreeing) {
		bool any = false;
		{
			std::lock_guard guard(Lock);
			any = !Ghosts.empty();
		}
		if (any && !GhostsFreeing.exchange(true) && Threads->runOnGameThread(context, FreeGhosts, nullptr) != D2RL::Threads::Result::Success) GhostsFreeing = false;
	}
	if (!ViewIsOpen || !player) return;
	HookWheel();
	{
		std::lock_guard guard(Lock);
		if (RefreshPerfect()) RebuildWanted = true;
	}
	if (RebuildWanted.exchange(false)) {
		ShowView();
		return;
	}
	// Items that got on the page some other way go to their tile too.
	if (PreparedView < 0 && CurrentCategory >= 0 && !ArrangeRunning.exchange(true) && !QueueArrange(new ArrangeJob { .player = player })) ArrangeRunning = false;
}

// Any thread. Runs at once on the UI thread instead of waiting for the next tick.
void Queue(const std::string& command) {
	{
		std::lock_guard guard(CommandLock);
		if (command == "up") --PendingScroll;
		else if (command == "down") ++PendingScroll;
		else PendingCommand = command;
		if (CommandScheduled) return;
		CommandScheduled = true;
	}
	if (Threads->runOnUiThread(Context, RunPending, nullptr) != D2RL::Threads::Result::Success) {
		std::lock_guard guard(CommandLock);
		CommandScheduled = false; // the next tick runs it
	}
}

auto __cdecl OnUiMessage(const D2RL::PluginContext*, const Events::UiMessageEvent* event, void*) noexcept -> Events::UiMessageAction {
	if (!event || !event->text) return Events::UiMessageAction::Continue;
	const std::size_t prefix = std::strlen(CommandPrefix);
	if (std::strncmp(event->text, CommandPrefix, prefix) != 0) return Events::UiMessageAction::Continue;
	// D2R reports a button's message twice, about a millisecond apart.
	static std::string last;
	static ULONGLONG   lastMs {};
	const ULONGLONG    now = GetTickCount64();
	if (last == event->text && now - lastMs < 150) return Events::UiMessageAction::Consume;
	last   = event->text;
	lastMs = now;
	Queue(event->text + prefix);
	return Events::UiMessageAction::Consume;
}

// The "Open/close the grail" action of the game's Controls menu (unbound by
// default): the same as the inventory button.
auto OnGrailAction(const D2RL::PluginContext*, const D2RL::Input::ActionEvent* event, void*) noexcept -> D2RL::Input::ActionResult {
	if (!D2RL::Input::HasActionEventField(event, D2RL::Input::ActionEventRequiredSize) || event->kind != D2RL::Input::ActionEventKind::Pressed)
		return D2RL::Input::ActionResult::Ignored;
	Queue("open");
	return D2RL::Input::ActionResult::Handled;
}

D2RL::ConsoleCommandResult __cdecl OnCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept {
	const std::string args = command && command->args ? std::string(command->args, command->argsLength) : "";
	char              key[16] {};
	int               value = 0, second = 0;
	const int         count = std::sscanf(args.c_str(), "%15s %d %d", key, &value, &second);
	if (count >= 1 && !std::strcmp(key, "scrolltest")) {
		Queue("scrolltest");
		return D2RL::ConsoleCommandResult::Handled;
	}
	if (count >= 1 && !std::strcmp(key, "rolls")) {
		Queue("rolls");
		return D2RL::ConsoleCommandResult::Handled;
	}
	if (count >= 1 && !std::strcmp(key, "dump")) {
		DumpWanted = true; // the next tick in game writes it
		return D2RL::ConsoleCommandResult::Handled;
	}
	if (count >= 2) {
		std::lock_guard guard(Lock);
		if (!std::strcmp(key, "font")) Config.font = std::clamp(value, 12, 60);
		else if (!std::strcmp(key, "timing")) Config.timing = value != 0;
		else if (!std::strcmp(key, "animations")) Config.frameAnimations = Config.scrollAnimations = value != 0;
		else if (!std::strcmp(key, "frame")) Config.frameMs = std::clamp(value, 5, 500);
		else if (!std::strcmp(key, "hold")) Config.hold = value != 0;
		else if (!std::strcmp(key, "trace")) Config.trace = value != 0;
		else if (!std::strcmp(key, "gray") && count == 3) {
			Config.saturation = std::clamp(value, -100, 100);
			Config.brightness = std::clamp(second, -100, 100);
		} else {
			Say(true, "grailon: settings are font, gray <saturation> <brightness>, timing, animations 0|1 and frame <ms>");
			return D2RL::ConsoleCommandResult::Handled;
		}
		++Config.version;
		Say(false, "grailon: font %d, gray %d %d", Config.font, Config.saturation, Config.brightness);
		RebuildWanted = true;
		return D2RL::ConsoleCommandResult::Handled;
	}
	Queue("open");
	return D2RL::ConsoleCommandResult::Handled;
}

// ---- packaged art ----
// Expanded Stash Plus's stash frame ships inside the plugin (PNG resources,
// full and low-end size), decoded at load and registered as the panel sprite.

constexpr int PanelArtId = 101, PanelArtLowEndId = 102;

bool DecodePng(int id, Picture& out) {
#ifdef _WIN32
	HMODULE module {};
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&DecodePng), &module)) return false;
	HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10)); // RT_RCDATA
	if (!resource) return false;
	auto*       bytes = static_cast<BYTE*>(LockResource(LoadResource(module, resource)));
	const DWORD size  = SizeofResource(module, resource);
	if (!bytes || !size) return false;
	const HRESULT          com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	IWICImagingFactory*    factory {};
	IWICStream*            stream {};
	IWICBitmapDecoder*     decoder {};
	IWICBitmapFrameDecode* frame {};
	IWICFormatConverter*   converter {};
	UINT                   width = 0, height = 0;
	bool                   done  = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))
	           && SUCCEEDED(factory->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromMemory(bytes, size))
	           && SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) && SUCCEEDED(decoder->GetFrame(0, &frame))
	           && SUCCEEDED(factory->CreateFormatConverter(&converter))
	           && SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))
	           && SUCCEEDED(converter->GetSize(&width, &height));
	if (done) {
		out = { static_cast<int>(width), static_cast<int>(height), std::vector<unsigned char>(static_cast<std::size_t>(width) * height * 4) };
		done = SUCCEEDED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(out.rgba.size()), out.rgba.data()));
	}
	for (IUnknown* object : std::initializer_list<IUnknown*> { converter, frame, decoder, stream, factory })
		if (object) object->Release();
	if (SUCCEEDED(com)) CoUninitialize();
	return done;
#else
	(void)id, (void)out;
	return false;
#endif
}

bool RegisterPanelArt() {
	Picture full, lowEnd;
	if (!DecodePng(PanelArtId, full) || !DecodePng(PanelArtLowEndId, lowEnd)) return false;
	const std::string path = "data/hd/global/ui/d2rloader/grail-on/panel";
	return RegisterResource(path + ".sprite", SpriteFile({ full })) && RegisterResource(path + ".lowend.sprite", SpriteFile({ lowEnd }));
}

// ---- scroll test ----
// Research for tiles that move by less than a cell: the game's own scroll view
// (ScrollViewWidget, as in the options menus) with 48 rows of 98 units, the
// page's grid inside its first row. `grailon scrolltest` opens it. While it is
// open, items land where they are dropped, and the log gets the grid's and the
// rows' rectangles whenever they change, to see whether the plugin can read
// the scroll position.

// Five variants, tried in turn until one opens: 0 the full test, 1 without
// the scroll controller, 2 without the scroll view (table in a plain clip
// widget), 3 the grid alone in the clip widget, 4 the grid alone in the panel
// (like the views). Which one opens tells which widget a plain panel refuses.
constexpr int                    ScrollTestVariants = 5;
D2RL::Panels::RegistrationHandle ScrollTestPanels[ScrollTestVariants] { D2RL::Panels::InvalidHandle, D2RL::Panels::InvalidHandle, D2RL::Panels::InvalidHandle,
	                                                                     D2RL::Panels::InvalidHandle, D2RL::Panels::InvalidHandle };
int                              ScrollTestVariant {};
int                              ScrollTestTries {}; // ticks left to open it
#define ScrollTestPanel ScrollTestPanels[ScrollTestVariant]
constexpr int ScrollTestRows = 48;

std::string ScrollTestName(int variant) { return "ScrollTest" + std::to_string(variant); }

std::string ScrollTestLayout(int variant) {
	char head[3072] {};
	std::snprintf(head, sizeof(head),
	              R"({"type": "Panel", "name": "grail-on/ScrollTest%d", "fields": {"priority": 10, "rect": {"x": 236, "y": -856, "width": %d, "height": %d}, "anchor": {"x": 0.0, "y": 0.397}}, "children": [)"
	              R"({"type": "ClickCatcherWidget", "name": "PanelClickCatcher", "fields": {"rect": {"x": 0, "y": 0, "width": %d, "height": %d}}})"
	              R"(, {"type": "ImageWidget", "name": "Background", "fields": {"rect": {"x": 0, "y": 0}, "filename": "d2rloader\\grail-on\\panel"}})"
	              R"(, {"type": "ImageWidget", "name": "Cover", "fields": {"rect": {"x": 57, "y": 175}, "filename": "d2rloader\\grail-on\\cover"}})"
	              R"(, {"type": "TextBoxWidget", "name": "Title", "fields": {"rect": {"x": 416, "y": 50, "width": 855, "height": 60}, "style": "$StyleTitleBlock", "text": "@grail-on:ScrollTest"}})"
	              R"(, {"type": "ButtonWidget", "name": "CloseButton", "fields": {"rect": {"x": 1600, "y": 9}, "filename": "PANEL\\closebtn_4x", "hoveredFrame": 3, "tooltipString": "@d2r:strClose", "sound": "cursor_close_window_hd", "onClickMessage": %s}})"
	              ,
	              variant, PanelWidth, PanelHeight, PanelWidth, PanelHeight, Command("scrolltest").c_str());
	std::string layout = head;
	const char* grid   = R"({"type": "InventoryGridWidget", "name": "grail-on/Grid", "fields": {"rect": {"x": %d, "y": %d}, "cellCount": {"x": 16, "y": 16}, "cellSize": "$ItemCellSize", "gemSocketFilename": "PANEL\\gemsocket"}})";
	char        text[512] {};
	if (variant == 4) {
		std::snprintf(text, sizeof(text), grid, GridLeft, GridTop);
		return layout + ", " + text + "]}";
	}
	std::snprintf(text, sizeof(text), R"(, {"type": "Widget", "name": "Viewport", "fields": {"rect": {"x": %d, "y": %d, "width": %d, "height": %d}}, "children": [)", GridLeft, GridTop,
	              PageSize * Cell, PageSize * Cell);
	layout += text;
	if (variant == 3) {
		std::snprintf(text, sizeof(text), grid, 0, 0);
		return layout + text + "]}]}";
	}
	if (variant == 0)
		layout += R"({"type": "ImageWidget", "name": "ScrollBarBackground", "fields": {"rect": {"x": -20, "y": 0}, "anchor": {"x": 1.0}, "filename": "PauseMenu\\VerticalScroll"}, "children": [)"
		          R"({"type": "ScrollControllerWidget", "name": "ScrollController", "fields": {"rect": {"x": -31, "y": 29, "width": 26, "height": 1208}, "anchor": {"x": 1.0}, "upArrowFilepath": "FrontEnd\\HD\\Final\\FrontEnd_ScrollUpBtn", "downArrowFilepath": "FrontEnd\\HD\\Final\\FrontEnd_ScrollDownBtn", "barFilepath": "PauseMenu\\VerticalIndicator", "viewName": "ScrollView", "wheelScrollSound": "cursor_scroll_hd", "buttonScrollSound": "cursor_scroll_hd"}}]}, )";
	if (variant == 0) layout += R"({"type": "ScrollViewWidget", "name": "ScrollView", "fields": {"fitToParent": true, "scrollControllerName": "ScrollController"}, "children": [)";
	if (variant == 1) layout += R"({"type": "ScrollViewWidget", "name": "ScrollView", "fields": {"fitToParent": true}, "children": [)";
	char table[256] {};
	std::snprintf(table, sizeof(table), R"({"type": "TableWidget", "name": "Rows", "fields": {"columns": [{"width": %d, "alignment": {"h": "left", "v": "top"}}], "rowHeight": %d}, "children": [)",
	              PageSize * Cell - 60, Cell);
	layout += table;
	for (int r = 0; r < ScrollTestRows; ++r) {
		char row[1024] {};
		std::snprintf(row, sizeof(row),
		              R"(%s{"type": "TableRowWidget", "name": "Row%d", "children": [{"type": "Widget", "name": "Cell%d", "fields": {"rect": {"x": 0, "y": 0, "width": %d, "height": %d}}, "children": [)"
		              R"({"type": "ImageWidget", "name": "Slot%d", "fields": {"rect": {"x": %d, "y": 0}, "filename": "d2rloader\\grail-on\\slot1x1"}})"
		              R"(, {"type": "TextBoxWidget", "name": "Text%d", "fields": {"rect": {"x": 1000, "y": 0, "width": 400, "height": %d}, "text": "@grail-on:Row%d", "style": {"fontColor": "$FontColorGoldYellow", "pointSize": 30, "alignment": {"h": "left", "v": "center"}}}})",
		              r ? ", " : "", r, r, PageSize * Cell - 60, Cell, r, (r % 8) * Cell, r, Cell, r);
		layout += row;
		// The page's grid in the first row, drawn over the rows below it.
		if (r == 0)
			layout += R"(, {"type": "InventoryGridWidget", "name": "grail-on/Grid", "fields": {"rect": {"x": 0, "y": 0}, "cellCount": {"x": 16, "y": 16}, "cellSize": "$ItemCellSize", "gemSocketFilename": "PANEL\\gemsocket"}})";
		layout += "]}]}";
	}
	return layout + (variant < 2 ? "]}]}]}]}" : "]}]}]}");
}

void ToggleScrollTest() {
	if (IsOpen(ScrollTestPanel)) {
		(void)PanelApi->closePanel(Context, ScrollTestPanel);
		ScrollTestOpen = false;
		return;
	}
	// Opened on the next ticks: with the grail view just closed, a panel in
	// the same slot may not open in the same frame.
	CloseView();
	ScrollTestTries = 3;
}

// UI thread: tries each variant; the last try logs every result.
void TryScrollTest() {
	const bool last = --ScrollTestTries == 0;
	for (ScrollTestVariant = 0; ScrollTestVariant < ScrollTestVariants; ++ScrollTestVariant) {
		const auto result = ScrollTestPanel == D2RL::Panels::InvalidHandle ? D2RL::Panels::Result::NotFound : PanelApi->openPanel(Context, ScrollTestPanel);
		ScrollTestOpen    = result == D2RL::Panels::Result::Success;
		if (ScrollTestOpen || last)
			Say(!ScrollTestOpen, "grailon: scroll test variant %d %s (result %u)", ScrollTestVariant, ScrollTestOpen ? "open: scroll it, drop items on it, then close it" : "did not open",
			    static_cast<unsigned>(result));
		if (ScrollTestOpen) {
			ScrollTestTries = 0;
			return;
		}
	}
	ScrollTestVariant = 0;
}

// UI thread, while the scroll test is open: logs where its widgets are.
void WatchScrollTest() {
	if (ScrollTestTries > 0) TryScrollTest();
	if (!ScrollTestOpen) return;
	if (!IsOpen(ScrollTestPanel)) {
		ScrollTestOpen = false;
		return;
	}
	static std::map<std::string, std::string> last;
	D2RL::Widgets::WidgetHandle panel {};
	if (WidgetApi->findPanel(Context, ("grail-on/" + ScrollTestName(ScrollTestVariant)).c_str(), &panel) != D2RL::Widgets::Result::Success) {
		static bool told = false;
		if (!told) Say(true, "grailon: scroll test: the panel is not found by name");
		told = true;
		return;
	}
	for (const char* name : { "grail-on/Grid", "Rows", "Row0", "Row1", "Row20", "Cell0", "Cell20", "ScrollView", "ScrollController" }) {
		D2RL::Widgets::WidgetHandle widget {};
		D2RL::Widgets::Rect         rect {};
		const auto                  found = WidgetApi->findWidget(Context, panel, name, &widget);
		const auto                  read  = found == D2RL::Widgets::Result::Success ? WidgetApi->getWidgetRect(Context, widget, &rect) : found;
		char                        text[160] {};
		std::snprintf(text, sizeof(text), "find %u, rect %u: %d,%d %dx%d", static_cast<unsigned>(found), static_cast<unsigned>(read), rect.x, rect.y, rect.width, rect.height);
		if (last[name] != text) Say(false, "grailon: scroll test %s %s", name, text);
		last[name] = text;
	}
}

template <typename Service>
const Service* Query(const char* name) {
	const Service* service = nullptr;
	if (Context->QueryService(&service) != D2RL::ServiceQueryResult::Success) {
		Say(true, "grail-on: %s service unavailable", name);
		return nullptr;
	}
	return service;
}

// The host's page policy for the grail's area: the plugin's own.
auto __cdecl OnAreaPolicy(PageHost::AreaHandle, const D2RL::Inventory::ItemPolicyRequest* request, void*) noexcept -> D2RL::Inventory::ItemPolicyDecision {
	return OnPagePolicy(Context, request, nullptr);
}

// With Player Page Host: the grail's 3x5 area of its page. The area's grid
// is the host's, in an overlay panel at the view's grid corner, under the view
// (priority 9; the view is 10), so it never shows; open while the view shows tiles.
// The host's grid panel, at the window's grid (it moves with the compact
// setting; `now`: an open grid moves at once).
bool SetHostLayout(bool now) {
	char layout[1024] {};
	std::snprintf(layout, sizeof(layout),
	              R"({"type": "Panel", "name": "@PANEL@", "fields": {"priority": 9, "rect": {"x": %d, "y": %d, "width": %d, "height": %d}, "anchor": {"x": 0.0, "y": 0.397}}, "children": [@GRID@]})",
	              WindowX() + GridLeft, PanelY + GridTop, StageCols * Cell, StageRows * Cell);
	const PageHost::LayoutRequest overlay { .structSize = sizeof(overlay), .flags = now ? PageHost::LayoutApplyNow : 0U, .layout = layout, .gridX = 0, .gridY = 0 };
	return Host::Service->setAreaLayout(Host::Area, &overlay) == PageHost::Result::Success;
}

// The compact setting changed: the window moved, so did the scroll bar and the host's grid.
void CompactChanged() {
	BarLearned = false;
	if (Host::On()) (void)SetHostLayout(true);
}

bool JoinHost() {
	const D2RL::PluginCommunicationService* comm = nullptr;
	if (Context->QueryService(&comm) != D2RL::ServiceQueryResult::Success || !comm) return false;
	if (PageHost::Acquire(Context, comm, &Host::Service) != D2RL::PluginCommunication::Result::Success) return false;
	const PageHost::AreaRequest request { .structSize = sizeof(request), .flags = PageHost::AreaOverlay, .name = "grail", .label = "Grail On!",
		                                  .width = static_cast<std::uint32_t>(StageCols), .height = static_cast<std::uint32_t>(StageRows) };
	PageHost::AreaInfo info { .structSize = sizeof(info) };
	if (const auto result = Host::Service->reserveArea(Context, &request, &info); result != PageHost::Result::Success) {
		Problem(true, "grail-on: Player Page Host has no room for the grail's %dx%d area (result %u)", StageCols, StageRows, static_cast<unsigned>(result));
		return true; // the host has the page: no own page either
	}
	Host::Info = info;
	Host::Area = info.handle;
	if (!SetHostLayout(false)) Problem(true, "grail-on: Player Page Host refused the grail's grid layout");
	if (Host::Service->setItemPolicy(Host::Area, OnAreaPolicy, nullptr) != PageHost::Result::Success) Problem(true, "grail-on: Player Page Host refused the grail's item check");
	Say(false, "grailon: Player Page Host: grail area of %ux%u reserved, grid marker %.0f", info.width, info.height, info.gridMarker[0]);
	return true;
}

// UI thread. The host places the areas once every plugin has loaded: the
// grail's place on its page is read from then on.
void PlaceArea() {
	static bool done = false;
	if (!Host::On() || done) return;
	PageHost::AreaInfo info { .structSize = sizeof(info) };
	const auto         result = Host::Service->getAreaInfo(Host::Area, &info);
	if (result == PageHost::Result::NoRoom) {
		done = true;
		Problem(true, "grail-on: Player Page Host has no room for the grail's %dx%d area beside the other plugins' areas", StageCols, StageRows);
		return;
	}
	if (result != PageHost::Result::Success || info.x < 0 || info.y < 0) return; // not placed yet
	done       = true;
	Host::Info = info;
	Say(false, "grailon: Player Page Host: grail area at %d,%d (%ux%u), grid marker %.0f", info.x, info.y, info.width, info.height, info.gridMarker[0]);
}

// UI thread: the host's grid is open with a view that shows tiles.
void SyncArea() {
	if (!Host::On()) return;
	PlaceArea();
	const bool want = ViewIsOpen && TilesView();
	if (want == Host::Service->isAreaOpen(Host::Area)) return;
	if (want) {
		if (const auto result = Host::Service->openArea(Host::Area); result != PageHost::Result::Success) Say(true, "grailon: could not open the grail's area of the host page (result %u)", static_cast<unsigned>(result));
	} else (void)Host::Service->closeArea(Host::Area);
}

void Register() {
	// The page: the grail's area of Player Page Host's, else its own 3x5,
	// only grail uniques with a free tile.
	const bool hosted = JoinHost();
	const D2RL::Inventory::PlayerPageRegistration page { .structSize = D2RL::Inventory::PlayerPageRegistrationSize, .logicalId = "grail" };
	auto result = hosted ? D2RL::Inventory::Result::Success : Inventory->registerPlayerPage(Context, &page, &Page);
	// Without the page the rest is still registered: the grail button opens
	// the page with the message.
	if (hosted) {
	} else if (result != D2RL::Inventory::Result::Success) {
		Page = D2RL::Inventory::InvalidHandle;
		Problem(true, "grail-on: could not get a custom page (result %u). D2RLoader 1.3.1 allows one custom page: another plugin (Charm Inventory?) has it", static_cast<unsigned>(result));
	} else {
		const D2RL::Inventory::PlayerPageConfiguration size { .structSize = D2RL::Inventory::PlayerPageConfigurationSize, .width = StageCols, .height = StageRows };
		if ((result = Inventory->configurePlayerPage(Context, Page, &size)) != D2RL::Inventory::Result::Success) Problem(true, "grail-on: page size refused (result %u)", static_cast<unsigned>(result));
		const D2RL::Inventory::PlayerPageItemPolicy policy { .structSize = D2RL::Inventory::PlayerPageItemPolicySize, .page = Page, .callback = OnPagePolicy };
		if ((result = Inventory->configurePlayerPageItemPolicy(Context, &policy)) != D2RL::Inventory::Result::Success)
			Problem(true, "grail-on: could not check items before they land on the page (result %u)", static_cast<unsigned>(result));
	}

	// The grid cover, the square buttons, the scroll bar and the inventory
	// button. The slots wait for the lists, which say which sizes are needed.
	if (!RegisterSprite("cover", { Stone(1571, ViewRows * Cell + 2, BackgroundColor, 12, 3) })) Say(true, "grail-on: could not register the cover sprite");
	if (!RegisterPanelArt()) Say(true, "grail-on: could not unpack the stash frame");
	if (!RegisterSprite("category", CategoryButton(false, CatSize, CatSize)) || !RegisterSprite("categoryOn", CategoryButton(true, CatSize, CatSize))
	    || !RegisterSprite("menu", CategoryButton(false, MenuSize, MenuSize)) || !RegisterSprite("menuOn", CategoryButton(true, MenuSize, MenuSize))
	    || !RegisterSprite("stattile", CategoryButton(false, StatTile, StatTile))
	    || !RegisterSprite("check", CheckBox(false)) || !RegisterSprite("checkOn", CheckBox(true)) || !RegisterSprite("divider", { Divider(HeaderWidth, 4) }))
		Say(true, "grail-on: could not register the buttons");
	// The menu and category icons (game-icons.net), in the game's gold text colour.
	for (const auto& mask : IconMasks) {
		Picture icon { IconSize, IconSize, std::vector<unsigned char>(static_cast<std::size_t>(IconSize) * IconSize * 4) };
		for (int p = 0; p < IconSize * IconSize; ++p) {
			icon.rgba[p * 4] = 215, icon.rgba[p * 4 + 1] = 200, icon.rgba[p * 4 + 2] = 150;
			icon.rgba[p * 4 + 3] = mask.alpha[p];
		}
		if (!RegisterSprite(std::string("icon-") + mask.name, { icon })) Say(true, "grail-on: could not register icon %s", mask.name);
	}
	// The objectives' icons: at two cells (the settings), the picker's (gold,
	// then bright), and a tile of one cell.
	for (int k = 0; k < ObjectiveIconCount; ++k) {
		const bool own  = k < ObjOwnIcons;
		const int  size = own ? ObjIconSize : IconSize;
		const auto tint = [&](int r, int g, int b) {
			Picture icon { size, size, std::vector<unsigned char>(static_cast<std::size_t>(size) * size * 4) };
			for (int p = 0; p < size * size; ++p) {
				icon.rgba[p * 4] = static_cast<unsigned char>(r), icon.rgba[p * 4 + 1] = static_cast<unsigned char>(g), icon.rgba[p * 4 + 2] = static_cast<unsigned char>(b);
				icon.rgba[p * 4 + 3] = own ? ObjIconMasks[k][p] : IconMasks[k - ObjOwnIcons].alpha[p];
			}
			return icon;
		};
		const auto pad = [](const Picture& in, int box, int inner) {
			const Picture small = Fit(in, inner, inner);
			Picture       out { box, box, std::vector<unsigned char>(static_cast<std::size_t>(box) * box * 4) };
			for (int y = 0; y < small.height; ++y)
				std::memcpy(out.at((box - small.width) / 2, (box - small.height) / 2 + y), small.rgba.data() + static_cast<std::size_t>(y) * small.width * 4, static_cast<std::size_t>(small.width) * 4);
			return out;
		};
		const Picture icon = own ? tint(215, 200, 150) : Resize(tint(215, 200, 150), ObjIconSize, ObjIconSize);
		if (!RegisterSprite("objicon" + std::to_string(k), { icon })) Say(true, "grail-on: could not register objective icon %d", k);
		if (!RegisterSprite("objpick" + std::to_string(k), { pad(tint(215, 200, 150), PickCell, PickIcon), pad(tint(255, 240, 190), PickCell, PickIcon) }))
			Say(true, "grail-on: could not register objective picker icon %d", k);
		// The settings rows' icon button (gold, bright when hovered), no tile behind it.
		if (!RegisterSprite("objrow" + std::to_string(k), { pad(tint(215, 200, 150), DropHeight, DropHeight - 6), pad(tint(255, 240, 190), DropHeight, DropHeight - 6) }))
			Say(true, "grail-on: could not register objective row icon %d", k);
		// A tile of one cell: the icon with padding around it, like the menus' icons.
		const Picture tile = pad(tint(215, 200, 150), Cell, ObjTileIcon);
		if (!RegisterSprite("objtile" + std::to_string(k), { tile })) Say(true, "grail-on: could not register objective tile %d", k);
	}
	bool bar = RegisterSprite("track", { Slot(BarWidth, BarHeight) }) && RegisterSprite("step", ScrollStep());
	for (int steps = 1; steps < ScrollSteps; ++steps) bar = RegisterSprite("thumb" + std::to_string(steps), { ScrollThumb(steps) }) && bar;
	if (!bar) Say(true, "grail-on: could not register the scroll bar");
	if (!RegisterSprite("mold", { Mold() }) || !RegisterGrailButton()) Say(true, "grail-on: could not register the button sprite");
	// The button's text now; the rest once the lists are built.
	(void)RegisterStrings();
	// The panels, with a first layout each (replaced before their first open).
	std::string layout;
	{
		std::lock_guard guard(Lock);
		PlaceRows(BuildRows(SettingsView));
		ApplyHalfTiles();
		layout = ViewLayout("View");
	}
	for (int i = 0; i < ViewCount; ++i) {
		auto& view = Views[i];
		view.id    = "View" + std::to_string(i);
		(void)RegisterResource("data/global/ui/layouts/grail-on/" + view.id + "hd.json", layout, &view.layout);
		const D2RL::Panels::PanelRegistration registration {
			.structSize = D2RL::Panels::PanelRegistrationSize,
			.flags      = D2RL::Panels::PanelFlags::CloseOnEscape | D2RL::Panels::PanelFlags::GameplayLeftSlot,
			.localId    = view.id.c_str(),
		};
		if (PanelApi->registerPanel(Context, &registration, &view.panel) != D2RL::Panels::Result::Success) {
			Say(true, "grail-on: could not register panel %s", view.id.c_str());
			view.panel = D2RL::Panels::InvalidHandle;
			continue;
		}
		const D2RL::Panels::PlayerPageGridBinding binding {
			.structSize = D2RL::Panels::PlayerPageGridBindingSize,
			.panel      = view.panel,
			.page       = Page,
			.childName  = "grail-on/Grid",
		};
		if (Page == D2RL::Inventory::InvalidHandle) continue;
		if (const auto bound = PanelApi->bindPlayerPageGrid(Context, &binding); bound != D2RL::Panels::Result::Success)
			Say(true, "grail-on: could not bind the grid of %s (result %u)", view.id.c_str(), static_cast<unsigned>(bound));
	}
	// The scroll animation's panel, over the view (its layout is made for each step).
	{
		(void)RegisterResource("data/global/ui/layouts/grail-on/Scrollhd.json", R"({"type": "Panel", "name": "grail-on/Scroll", "fields": {"priority": 12}, "children": []})", &ScrollLayoutHandle);
		const D2RL::Panels::PanelRegistration registration {
			.structSize = D2RL::Panels::PanelRegistrationSize,
			.flags      = D2RL::Panels::PanelFlags::None,
			.localId    = "Scroll",
		};
		if (PanelApi->registerPanel(Context, &registration, &ScrollPanel) != D2RL::Panels::Result::Success) {
			Problem(false, "grail-on: could not register the scroll panel; wheel steps jump");
			ScrollPanel = D2RL::Panels::InvalidHandle;
		}
	}
	// The page that says what is switched off (its layout is made when it opens).
	{
		(void)RegisterResource("data/global/ui/layouts/grail-on/Errorhd.json", ErrorLayout(), &ErrorLayoutHandle);
		const D2RL::Panels::PanelRegistration registration {
			.structSize = D2RL::Panels::PanelRegistrationSize,
			.flags      = D2RL::Panels::PanelFlags::CloseOnEscape | D2RL::Panels::PanelFlags::GameplayLeftSlot,
			.localId    = "Error",
		};
		if (PanelApi->registerPanel(Context, &registration, &ErrorPanel) != D2RL::Panels::Result::Success) {
			Say(true, "grail-on: could not register the error page");
			ErrorPanel = D2RL::Panels::InvalidHandle;
		}
	}
	// The scroll test panels, bound to the same page.
	for (ScrollTestVariant = 0; ScrollTestVariant < ScrollTestVariants; ++ScrollTestVariant) {
		const std::string name = ScrollTestName(ScrollTestVariant);
		(void)RegisterResource(("data/global/ui/layouts/grail-on/" + name + "hd.json").c_str(), ScrollTestLayout(ScrollTestVariant));
		const D2RL::Panels::PanelRegistration registration {
			.structSize = D2RL::Panels::PanelRegistrationSize,
			.flags      = D2RL::Panels::PanelFlags::CloseOnEscape | D2RL::Panels::PanelFlags::GameplayLeftSlot,
			.localId    = name.c_str(),
		};
		if (PanelApi->registerPanel(Context, &registration, &ScrollTestPanel) == D2RL::Panels::Result::Success) {
			const D2RL::Panels::PlayerPageGridBinding binding {
				.structSize = D2RL::Panels::PlayerPageGridBindingSize,
				.panel      = ScrollTestPanel,
				.page       = Page,
				.childName  = "grail-on/Grid",
			};
			if (const auto bound = PanelApi->bindPlayerPageGrid(Context, &binding); bound != D2RL::Panels::Result::Success)
				Say(true, "grail-on: scroll test %d: could not bind its grid (result %u)", ScrollTestVariant, static_cast<unsigned>(bound));
		} else ScrollTestPanel = D2RL::Panels::InvalidHandle;
	}
	ScrollTestVariant = 0;
	RegisterInventoryButton();
	ButtonShownX = Config.buttonX, ButtonShownY = Config.buttonY, ButtonShownKnown = true; // the game loads the layout as registered now
	const D2RL::Panels::ChildLayoutRegistration child {
		.structSize = D2RL::Panels::ChildLayoutRegistrationSize,
		.flags      = D2RL::Panels::ChildLayoutFlags::KeyboardMouseOnly,
		.stockPanel = D2RL::Panels::StockPanel::PlayerInventory,
		.localId    = "InventoryButton",
	};
	D2RL::Panels::ChildLayoutHandle childHandle {};
	if (PanelApi->registerChildLayout(Context, &child, &childHandle) != D2RL::Panels::Result::Success) Problem(false, "grail-on: could not add the inventory button; use the grailon command");

	const Events::UiMessageListener listener { .structSize = Events::UiMessageListenerSize, .priority = 100, .callback = OnUiMessage };
	Events::ListenerHandle          handle = Events::InvalidHandle;
	if (EventApi->registerUiMessageListener(Context, &listener, &handle) != Events::Result::Success) Problem(true, "grail-on: could not listen to button clicks");
	// Ctrl+click: the item listener when D2RLoader has one, the hovered item otherwise.
	const Events::ItemTooltipListener tooltip {
		.structSize = Events::ItemTooltipListenerSize,
		.region     = Events::ItemTooltipRegion::Description,
		.position   = Events::ItemTooltipPosition::Bottom,
		.anchor     = Events::ItemTooltipAnchor::None,
		.fallback   = Events::ItemTooltipFallback::Omit,
		.callback   = OnTooltip,
	};
	Events::ListenerHandle tooltipHandle = Events::InvalidHandle;
	if (EventApi->registerItemTooltipListener(Context, &tooltip, &tooltipHandle) != Events::Result::Success) Problem(false, "grail-on: could not follow the hovered item");
	const Events::ItemTooltipListener footer {
		.structSize = Events::ItemTooltipListenerSize,
		.region     = Events::ItemTooltipRegion::ActionFooter,
		.position   = Events::ItemTooltipPosition::Bottom,
		.anchor     = Events::ItemTooltipAnchor::None,
		.fallback   = Events::ItemTooltipFallback::RegionBottom,
		.callback   = OnTooltipFooter,
	};
	// The action footer when this D2RLoader has it, else the description's bottom.
	Events::ListenerHandle footerHandle = Events::InvalidHandle;
	auto                   tried        = footer;
	Events::Result         got          = EventApi->registerItemTooltipListener(Context, &tried, &footerHandle);
	if (got != Events::Result::Success) {
		const auto first = got;
		tried.fallback   = Events::ItemTooltipFallback::Omit;
		got              = EventApi->registerItemTooltipListener(Context, &tried, &footerHandle);
		if (got != Events::Result::Success) {
			tried.region = Events::ItemTooltipRegion::Description;
			got          = EventApi->registerItemTooltipListener(Context, &tried, &footerHandle);
		}
		Say(false, "grail-on: the middle click's tooltip line: action footer refused (%u), %s (%u)", static_cast<unsigned>(first),
		    got == Events::Result::Success ? (tried.region == Events::ItemTooltipRegion::Description ? "under the description" : "in the footer") : "none", static_cast<unsigned>(got));
	}
	if (ClickApi) {
		namespace Clicks = D2RL::ItemInteractions;
		const bool shared = D2RL::HasItemInteractionServiceField(ClickApi, D2RL::ItemInteractionServiceSupportedContainerMaskFieldEnd)
		                 && (ClickApi->supportedContainerMask & Items::ContainerBit(Items::ItemContainer::SharedStash));
		const Clicks::ItemInteractionListener click {
			.structSize    = Clicks::ItemInteractionListenerSize,
			.priority      = 100,
			.callback      = OnItemClick,
			.containerMask = Clicks::DefaultContainerMask | (shared ? Items::ContainerBit(Items::ItemContainer::SharedStash) : 0u),
		};
		Clicks::ListenerHandle clickHandle = Clicks::InvalidHandle;
		if (ClickApi->registerListener(Context, &click, &clickHandle) != Clicks::Result::Success) Say(false, "grail-on: no item click listener; Ctrl+click goes by the hovered item");
	}
}

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (!D2RL::HasContext(context) || !context->GetApi()) return false;
	Context     = context;
	Inventory   = Query<D2RL::InventoryService>("inventory");
	ItemApi     = Query<D2RL::ItemService>("item");
	Threads     = Query<D2RL::ThreadService>("thread");
	PanelApi    = Query<D2RL::PanelService>("panel");
	WidgetApi   = Query<D2RL::WidgetService>("widget");
	ResourceApi = Query<D2RL::ResourceService>("resource");
	EventApi    = Query<D2RL::SharedEventService>("shared event");
	TableApi    = Query<D2RL::DataTableService>("data table");
	TextApi     = Query<D2RL::LocalizationService>("localization");
	if (Context->QueryService(&ClickApi) != D2RL::ServiceQueryResult::Success) ClickApi = nullptr;
	if (!Inventory || !ItemApi || !Threads || !PanelApi || !WidgetApi || !ResourceApi || !EventApi) return true;
	if (!TableApi) Problem(true, "grail-on: no data table service, so no item lists (D2RLoader 1.3.1 or newer is needed)");
	LoadSettings();
	Register();
	// Both off at start: the tooltips are not hooked at all (another plugin's
	// tooltips can be checked without them).
	// The stat reader is not only for the tooltips: a base's sockets and the
	// perfect checks read stats too.
	if (HMODULE core = GetModuleHandleW(L"D2RCore.dll")) ReadStat = reinterpret_cast<StatFn>(reinterpret_cast<void*>(GetProcAddress(core, "ReadWideUnitStat")));
	ApplyTooltipSettings();
	if (!Hooked) Say(false, "grailon: compare is off, the item tooltips are not hooked");
	InstallPlacementHooks();
	StoreReady = LocateStore();
	if (!StoreReady) Problem(true, "grail-on: the game's item save calls were not found, so the grail cannot store items (game update?)");
	if (const auto* input = Query<D2RL::InputService>("input"); input && D2RL::HasInputServiceField(input, D2RL::InputServiceRequiredSize)) {
		const D2RL::Input::ActionRegistration action {
			.structSize       = D2RL::Input::ActionRegistrationSize,
			.logicalId        = "toggle-grail",
			.displayName      = "Open/close the grail",
			.category         = "Grail On!",
			.defaultPrimary   = { .key = D2RL::Input::Key::None },
			.defaultSecondary = { .key = D2RL::Input::Key::None },
			.callback         = OnGrailAction,
		};
		D2RL::Input::ActionHandle handle = D2RL::Input::InvalidHandle;
		if (const auto result = input->registerAction(context, &action, &handle); result != D2RL::Input::Result::Success)
			Say(true, "grailon: could not add the open/close action to the Controls menu (result %u)", static_cast<unsigned>(result));
	}
	auto command = D2RL::MakeConsoleCommand("grailon", OnCommand, "Grail On!: open or close the grail. grailon forget: found entries no longer stored go back to never found. grailon rolls: stored copies' rolls against the best.");
	if (auto* api = context->GetApi(); !api->registerConsoleCommand || !api->registerConsoleCommand(context, &command)) Say(true, "grail-on: could not register the grailon command");
	Running = true;
	Ticker  = std::thread([] {
        for (unsigned tick = 0; Running; ++tick) {
            std::this_thread::sleep_for(std::chrono::milliseconds(125));
            if (!Running) break;
            if (tick % 2 == 0) (void)Threads->runOnUiThread(Context, UiTick, nullptr);
            if (ViewIsOpen) (void)Threads->runOnUiThread(Context, AnimateOnUi, nullptr);
            if (tick % 2 == 1 && ObjTyping) TypedNow(); // a click off the box lets the keys go sooner
        }
    });
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	ObjStopThread();
	Running = false;
	if (Ticker.joinable()) Ticker.join();
	UnhookWheel();
	UnhookComparison();
	UnhookLoot();
	UnhookPlacement();
	UnwrapGridVtable();
	if (Host::On()) (void)Host::Service->releaseArea(Context, Host::Area);
	Host::Area = PageHost::InvalidArea;
	(void)Host::Service.Reset();
	std::lock_guard guard(Lock);
	if (!StorePath.empty()) SaveStore();
}
