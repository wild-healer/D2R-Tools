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
#include "i_hate_tomls_client.h"
#include <windows.h>
#include <shlobj.h>
#ifdef _WIN32
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <climits>
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
#include <d2r_gui.h>

// The stash frame's PNGs, compiled into the DLL's read-only data (not
// resources: see D2R GUI's png.h).
asm(".section .rdata,\"dr\"\n"
    ".balign 16\n"
    "grailon_panel_png: .incbin \"art/stash_tall.png\"\n"
    "grailon_panel_png_end:\n"
    ".balign 16\n"
    "grailon_panel_lowend_png: .incbin \"art/stash_tall.lowend.png\"\n"
    "grailon_panel_lowend_png_end:\n"
    ".globl grailon_panel_png, grailon_panel_png_end, grailon_panel_lowend_png, grailon_panel_lowend_png_end\n"
    ".text\n");
extern "C" const unsigned char grailon_panel_png[], grailon_panel_png_end[], grailon_panel_lowend_png[], grailon_panel_lowend_png_end[];

namespace {

constexpr const char* PluginId      = "grail-on";
constexpr const char* PluginVersion = "1.2.2-dev-1";

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
namespace Gui    = D2RGui;
namespace Kit    = Gui::StashKit;
using Picture    = Gui::Picture;

struct AssetEntry {
	const char* key;
	const char* asset;
};
#include "art.inc"
#include "builtin_art.inc"
#include "icons.inc"

using Gui::Languages; // the game's 13, in its string tables' order
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
constexpr int ExclInputWidth = 1220 - 56 - 20, ExclRowHeight = 46, ExclIndent = 44, ExclMark = 36; // the Loot page's exclusions list
constexpr int ObjInputWidth = 1220 - 56 - 20; // the objectives page's text box: the table less the + button
constexpr int DropWidth = 610, MiscDrop = 260, DropHeight = 56, SmallBox = 64, ResetBox = 120, AxisWidth = 30 + 2 * SmallBox + 100 + 20, TileHeight = 52, TableLabel = 300, TableColumn = 230, TableRow = 72, TableWidth = TableLabel + 4 * TableColumn;
int           LineHeight = 4; // the section divider's, once made
// The scroll bar sits on the window's right border, past the grid; its track is cut into ScrollSteps buttons that each scroll to their
// share of the rows.
constexpr int BarWidth = 40, BarLeft = GridLeft + 16 * Cell + 10, BarTop = ViewTop + 64, ScrollSteps = 34, BarHeight = ScrollSteps * 50, StepHeight = BarHeight / ScrollSteps;
// The category name: under the frame (1829 tall), outside it, inside the panel.
constexpr int TitleTop = 1829, TitleHeight = PanelHeight - TitleTop, TitleSize = 36;
constexpr int ViewCount = 40;    // D2RLoader allows 64 panels per plugin
// Over the stash, the grail's panels draw above the panels other plugins put on it
// (Stash Tab Dropdown's button is priority 110, its page list 120).
constexpr int OverPriority = 130;
bool SetHostLayout(bool now);
std::atomic<bool> OverShown = false; // the shown (or prepared) view is over the stash
constexpr int OverViews = 8;     // the last ones: opened over the open stash (it stays open under them, as under the cube)
constexpr int SettingsView = -1; // CurrentCategory while the settings are shown
constexpr int StatsView    = -2; // and while the stats are (empty for now); the view opens on them

// The menus: the stats, unique weapons, unique armor and accessories, their
// ethereal copies (the same categories, eth tiles only), sets, runewords and
// the settings.
constexpr const char* Menus[] { "Stats", "Unique Weapons", "Unique Armor & Misc", "Ethereal Weapons", "Ethereal Armor", "Sets", "Runewords", "Misc", "Objectives", "Lost items", "Config" };
constexpr int StatsMenu = 0, WeaponMenu = 1, ArmorMenu = 2, EthWeaponMenu = 3, EthArmorMenu = 4, SetMenu = 5, RunewordMenu = 6, MiscMenu = 7, ObjMenu = 8, LostMenu = 9, ConfigMenu = 10;
std::map<std::uint32_t, int> LostOf; // lost records' fingerprints -> their entries (the Lost items tiles); guarded by Lock

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
	bool lootUnidOnly = true; // only unidentified ground items are hidden
	bool lootUnidStats {};    // an unidentified item as good as its grail copy in every varying stat shows
	bool lootAmbiguous {};  // unidentified items hidden by what they are, not only when every item of their base would be
	bool lootGraphic {};    // ground items with graphic variants hidden by their own picture's tile, not only when every picture's would be
	int  compare = CompareAlways; // the grail copy in the comparison: never, with Shift held, always
	bool perfectFrames = true;     // perfect copies' glow around their tile
	bool trace = false; // grailon trace 1: each view step and the cursor item logged
	bool hold = false; // the cursor item kept aside while views change (grailon hold 1), the old way
	bool compact {}; // the window against the screen's left edge, its menus on its right
	bool swapColumns {}; // the categories in the outer icon column, the menus in the inner one
	bool middleClick = true; // a middle click on a grail item shows it in the grail
	bool objectives = true; // the custom objectives shown (their list is kept either way)
	bool affixVariants {};   // items with a choice roll get a tile per choice (but those in AffixOff)
	bool graphicVariants {}; // items whose base has several pictures get a tile per picture (but those in GraphicOff)
	bool frameAnimations = true, scrollAnimations = true; int frameMs = 30; // perfect glows and wheel steps animated (settings page); each scroll frame's time (grailon frame <n>)
	int  language = -1; // the texts' language: an index in Languages, or -1 for the game's
	int  buttonX = 3, buttonY = 954; // the inventory button's place, in the inventory panel's units
	int  searchX = -220, searchY = 984, searchWidth = 480; // the search bar's place, in the window's own frame (its rect is x WindowX() - MenuRoom, y PanelY there): compact mode leaves it; and its length
	int  version {}; // changes with every setting, so built views are not reused
} Config;
constexpr int DefaultButtonX = 3, DefaultButtonY = 954, DefaultSearchX = -220, DefaultSearchY = 984, DefaultSearchWidth = 480;

// The settings page's check boxes, by group; the settings file uses the same
// keys. Each box turns its field on.
enum ToggleGroup { ShowNames, ShowArt, DisplayGroup, LootGroup, ObjectiveGroup, RpGroup, VariantGroup };
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
	{ "loot_unid_only", &Settings::lootUnidOnly, "Only unidentified items",
	  "Only unidentified items are hidden; identified ones always show. Off: identified items are hidden too.", LootGroup },
	{ "loot_unid_stats", &Settings::lootUnidStats, "Filter unidentified stats",
	  "With Hide perfect: an item whose grail copy is not perfect only shows when it is an upgrade, every varying stat the same or better and at least one better (read from the game you play or host; in games you join it stays hidden). Off: Hide perfect alone decides.", RpGroup },
	{ "loot_ambiguous", &Settings::lootAmbiguous, "Filter ambiguous bases",
	  "Unidentified items are hidden by what they really are, even when other items share their base. Off: an unidentified item is only hidden when every unique or set item of its base would be.",
	  RpGroup },
	{ "loot_graphic", &Settings::lootGraphic, "Filter graphic variants",
	  "With Graphic variants on, a ring, amulet, jewel or charm on the ground is hidden by its own picture's tile, which the ground does not show. Off: it is only hidden when every picture's tile would be.",
	  RpGroup },
	{ "frame_animations", &Settings::frameAnimations, "Frame animations", "The perfect glow moves. Off: it stays still.", DisplayGroup },
	{ "compact", &Settings::compact, "Compact", "The window sits against the screen's left edge, with its menus on its right. Off: the menus are on its left, where the game's panels start.", DisplayGroup },
	{ "swap_columns", &Settings::swapColumns, "Swap columns", "The categories go in the left icon column and the menus in the right one. Off: the menus on the left.", DisplayGroup },
	{ "middle_click", &Settings::middleClick, "Middle click", "A middle click on an item shows it in Grail On!, and its tooltip says so. Off: middle clicks are the game's.", DisplayGroup },
	{ "objectives", &Settings::objectives, "Custom objectives", "Your objectives have their own menu. Off: the menu is hidden and the list is kept.", ObjectiveGroup },
	{ "affix_variants", &Settings::affixVariants, "Affix variants",
	  "Items whose choice roll differs from copy to copy (a skill, a class, an element...) get a tile for each choice. Off: one tile for all of them.", VariantGroup },
	{ "graphic_variants", &Settings::graphicVariants, "Graphic variants",
	  "Items whose picture differs from copy to copy (rings, amulets, jewels, charms without art of their own) get a tile for each picture. Off: one tile for all of them.", VariantGroup },
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
// The Loot page's list of exclusions: the groups open (menus "m<menu>",
// categories "c<menu>.<category>"), its first row shown and how many fit.
std::set<std::string> ExclOpen;
int                   ExclFirst = 0, ExclPerPage = 1, ExclMax = 0, ExclSteps = 1;
// The player's exclusion lists, in the player's order: the loot filter never
// hides an item of an active one. A list's items are grail entry ids ("|eth"
// for an ethereal tile); a list made from a preset uses the preset's items
// until the player changes it (`own`). Guarded by Lock.
struct ExclList {
	std::string           name;
	int                   preset = -1; // 0 very valuable, 1 most interesting, -1 none
	bool                  own {}, active {};
	std::set<std::string> items;
};
constexpr const char* PresetNames[] { "Very valuable items", "Most interesting items" };
constexpr const char* PresetKeys[] { "valuable", "interesting" };
std::vector<ExclList> DefaultExclLists() { return { { PresetNames[0], 0 }, { PresetNames[1], 1 } }; }
std::vector<ExclList> ExclLists = DefaultExclLists();
int                   ExclEditing = -1;     // the list in the editor, or -1: the lists shown
ExclList              ExclDraft;            // the editor's copy of it, saved by Save
int                   ExclRenaming = -1;    // the list whose name is typed
int                   ExclDeleteArmed = -1; // the list whose Delete was clicked once (a second click deletes)
// The Variants section: the items left out of affix / graphic variants (base
// entry ids; new items are in by default), and the editor open for one of
// them (1 affix, 2 graphic; it uses ExclDraft, its items the ones in). Guarded by Lock.
std::set<std::string> AffixOff, GraphicOff;
int                   VarEditing = 0;
// What the editor lists and ticks: exclusions, or one of the variant lists.
int  EditorMode() { return VarEditing; }
// The built-in lists: grail entry ids without their number (every Rainbow
// Facet), both tiles. Most interesting items holds the very valuable ones too.
constexpr const char* PresetValuable[] { "u:Harlequin Crest", "u:Griffon's Eye", "u:Crown of Ages", "u:Andariel's Visage", "u:Nightwing's Veil", "u:Arachnid Mesh", "u:Fathom", "u:Eschuta's temper", "u:Deaths's Web", "u:Mara's Kaleidoscope", "u:The Stone of Jordan", "u:Bul Katho's Wedding Band", "u:Highlord's Wrath", "u:Tyrael's Might", "u:Templar's Might", "u:Windforce", "u:The Grandfather", "u:Doombringer", "u:Deathcleaver", "u:Ethereal Edge", "u:Stormlash", "u:Wisp", "u:Annihilus", "u:Hellfire Torch", "u:Gheed's Fortune", "u:Rainbow Facet", "u:Dracul's Grasp", "u:Herald of Zakarum", "u:Cold Rupture", "u:Flame Rift", "u:Crack of the Heavens", "u:Rotting Fissure", "u:Bone Break", "u:Black Cleft", "u:Azurewrath", "u:Wizardspike", "u:Raven Frost", "u:Metalgrid", "u:Shadowdancer", "u:Mang Song's Lesson", "u:Steel Carapice", "s:Immortal King's Will", "s:Immortal King's Soul Cage", "s:Immortal King's Detail", "s:Immortal King's Forge", "s:Immortal King's Pillar", "s:Immortal King's Stone Crusher", "s:Tal Rasha's Fire-Spun Cloth", "s:Tal Rasha's Adjudication", "s:Tal Rasha's Lidless Eye", "s:Tal Rasha's Howling Wind", "s:Tal Rasha's Horadric Crest",  };
constexpr const char* PresetInteresting[] { "u:Ormus' Robes", "u:Vampiregaze", "u:Kira's Guardian", "u:Giantskull", "u:Veil of Steel", "u:Jalal's Mane", "u:Arreat's Face", "u:Homunculus", "u:Medusa's Gaze", "u:Stormshield", "u:Spirit Ward", "u:Kerke's Sanctuary", "u:Lidless Wall", "u:The Oculus", "u:Titan's Revenge", "u:Thunderstroke", "u:Lycander's Aim", "u:Buriza-Do Kyanon", "u:Eaglehorn", "u:Kuko Shakaku", "u:Whichwild String", "u:Lightsabre", "u:Baranar's Star", "u:Schaefer's Hammer", "u:Messerschmidt's Reaver", "u:Bloodmoon", "u:The Reaper's Toll", "u:Tomb Reaver", "u:Stormspire", "u:Earthshifter", "u:Arioc's Needle", "u:Hone Sundan", "u:Dwarf Star", "u:Nosferatu's Coil", "u:Verdugo's Hearty Cord", "u:Thudergod's Vigor", "u:String of Ears", "u:Seraph's Hymn", "u:Atma's Scarab", "u:The Cat's Eye", "u:Saracen's Chance", "u:Gorerider", "u:Shadowkiller", "u:Sandstorm Trek", "u:Wartraveler", "u:Waterwalk", "u:Marrowwalk", "u:Steelrend", "u:Souldrain", "u:Magefist", "u:Frostburn", "u:Chance Guards", "u:Lavagout", "u:Leviathan", "u:Skin of the Vipermagi", "u:Que-Hegan's Wisdon", "u:Guardian Angel", "u:Shaftstop", "u:Duriel's Shell", "u:Halaberd's Reign", "u:Wolfhowl", "u:Cerebus", "u:Ravenlore", "u:Spiritkeeper", "u:Horizon's Tornado", "u:Boneflame", "u:Darkforge Spawn", "u:Heaven's Light", "u:The Cranium Basher", "u:Nord's Tenderizer", "u:Ribcracker", "u:Fleshripper", "u:Ghostflame", "u:Gimmershred", "u:Lacerator", "u:Deathbit", "u:Warshrike", "u:Demon's Arch", "u:Gargoyle's Bite", "u:Lycander's Flank", "u:Ondal's Wisdom", "u:Nagelring", "u:Crescent Moon", "u:Bloodraven's Charge", "s:Natalya's Totem", "s:Natalya's Mark", "s:Natalya's Shadow", "s:Natalya's Soul", "s:Aldur's Stony Gaze", "s:Aldur's Deception", "s:Aldur's Gauntlet", "s:Aldur's Advance", "s:Griswold's Valor", "s:Griswold's Heart", "s:Griswolds's Redemption", "s:Griswold's Honor", "s:Trang-Oul's Guise", "s:Trang-Oul's Scales", "s:Trang-Oul's Wing", "s:Trang-Oul's Claws", "s:Trang-Oul's Girth", "s:M'avina's True Sight", "s:M'avina's Embrace", "s:M'avina's Icy Clutch", "s:M'avina's Tenet", "s:M'avina's Caster", "s:Telling of Beads", "s:Laying of Hands", "s:Rite of Passage", "s:Spiritual Custodian", "s:Credendum", "s:Dangoon's Teaching", "s:Heaven's Taebaek", "s:Haemosu's Adament", "s:Ondal's Almighty", "s:Hwanin's Splendor", "s:Hwanin's Refuge", "s:Hwanin's Seal", "s:Hwanin's Justice", "s:Sazabi's Cobalt Redeemer", "s:Sazabi's Ghost Liberator", "s:Sazabi's Mental Sheath", "s:Bul-Kathos' Sacred Charge", "s:Bul-Kathos' Tribal Guardian", "s:Naj's Puzzler", "s:Naj's Light Plate", "s:Naj's Circlet", "s:Horazon's Countenance", "s:Horazon's Dominion", "s:Horazon's Hold", "s:Horazon's Legacy", "s:Horazon's Secrets",  };
const std::set<std::string>& PresetIds(int profile) {
	static const std::set<std::string> valuable(std::begin(PresetValuable), std::end(PresetValuable));
	static const std::set<std::string> interesting = [] {
		std::set<std::string> all(std::begin(PresetInteresting), std::end(PresetInteresting));
		all.insert(std::begin(PresetValuable), std::end(PresetValuable));
		return all;
	}();
	return profile == 0 ? valuable : interesting;
}
// Whether an exclusion key ("id" or "id|eth") is in a built-in list.
bool InPreset(int profile, std::string key) {
	if (const auto bar = key.find('|'); bar != std::string::npos) key.resize(bar);
	if (const auto hash = key.find('#'); hash != std::string::npos) key.resize(hash);
	return PresetIds(profile).count(key) != 0;
}
// Whether a list holds a key; whether an active list does.
bool ListHas(const ExclList& list, const std::string& key) {
	if (list.preset >= 0 && !list.own) return InPreset(list.preset, key);
	return list.items.count(key) != 0;
}
bool ExcludedKey(const std::string& key) {
	return std::any_of(ExclLists.begin(), ExclLists.end(), [&](const ExclList& list) { return list.active && ListHas(list, key); });
}
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
// One choice of a choice roll: the stat (and layer) it gives, its value's range, its name.
// An item with several choice rolls (renewed sunders): one choice per roll, in `parts`.
struct VariantChoice {
	int                              stat {}, layer {}, low {}, high {};
	std::string                      label;
	std::vector<std::pair<int, int>> parts; // several rolls: every roll's (stat, layer); empty for one roll
};
// A choice's key in ids and the storage file: "<stat>.<layer>", "+" between several rolls'.
std::string ChoiceKey(const VariantChoice& choice) {
	if (choice.parts.empty()) return std::to_string(choice.stat) + "." + std::to_string(choice.layer);
	std::string key;
	for (const auto& [stat, layer] : choice.parts) key += (key.empty() ? "" : "+") + std::to_string(stat) + "." + std::to_string(layer);
	return key;
}
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
	// Variants: affix variants (a choice roll: a property group, a random
	// skill or class skill; Ormus' Robes' skill, Wraithstep's skill tab...)
	// and graphic variants (the picture the game picks for each item of a
	// ring, amulet, jewel or charm base). Found from the game's tables. A base
	// entry lists a variant entry per affix choice, per picture, and per both;
	// the settings say which ones have tiles.
	std::vector<VariantChoice> affixes; // its choices (a variant keeps its base's)
	std::uint64_t    tooMany {};         // more combinations than variants are made for: their count (no variants; greyed in the list)
	int              pictures {};        // its pictures, 0 for none (only bases without art of their own)
	std::vector<int> variants;           // the base's: its variant entries
	bool             variant {};         // a variant entry
	int              variantOf = -1;     // a variant's: its base entry
	std::string      detail;             // what its name has in brackets (a variant's choice, a Rainbow Facet's element): the tile's tooltip, not its label
	int              affix = -1;         // a variant's: index in affixes, -1 for any
	int              picture = -1;       // a variant's: its picture (0 for the first), -1 for any
	bool             lost {};            // a lost item's tile (Lost items menu): a stored item no other tile shows
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
	int  pictures {};     // its item type's VarInvGfx: how many pictures the game picks from (rings 5...), 0 or 1 for one
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
constexpr int VarInvGfx = 34; // u8: how many inventory pictures its items pick from (InvGfx1-6 follow, 32 bytes each)
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
using Gui::LanguageCount;
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
void ReadGameTexts() {
	GameTexts texts;
	// The interface's first: its English texts are the ones the plugin's are compared with.
	for (const char* name : { "ui", "item-names", "item-nameaffixes", "item-runes", "item-modifiers", "skills", "ui-controller" }) {
		const std::string path = std::string("data/local/lng/strings/") + name + ".json";
		std::string       file = Gui::GameFile(path.c_str()); // quietly: a file a game version lacks is no problem
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
	// The choice rolls' tables and names (variants): PropertyGroups rows, and
	// from the game's txt files (what the compiled rows have no names for).
	TableRows     groups;
	std::uint32_t groupSize {};
	std::map<int, std::string>            skillKeys, skillNames; // skill id -> its name's string key (SkillDesc "str name"), its English name
	std::map<std::string, int>            skillIds;              // English skill name (lower case) -> id
	std::vector<std::string>              classNames;            // by class: CharStats "class" (English)
	std::vector<std::array<std::string, 3>> tabKeys;            // by class: StrSkillTab1-3 (string keys)
	std::map<int, std::string>            statKeys;              // stat id -> descstrpos (string key)
	std::map<int, std::string>            statNames;             // stat id -> its name (ItemStatCost "Stat", lower case)
	std::set<std::string>                 obtainable;            // what drops or a cube makes by its name (TreasureClassEx items, CubeMain outputs)
	std::set<std::string>                 cubeMade;              // what a cube recipe makes by its name (its picture is its recipe's)
	std::vector<std::array<std::string, 8>> groupParNames;      // PropertyGroups rows' ParMin1-8 as written (skill names the compiled rows may leave 0)
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

// A txt table's lines split into cells (tabs), the header first.
std::vector<std::vector<std::string>> TxtCells(const std::string& file) {
	std::vector<std::vector<std::string>> out;
	for (std::size_t at = 0; at < file.size();) {
		auto end = file.find('\n', at);
		if (end == std::string::npos) end = file.size();
		std::vector<std::string> row;
		for (std::size_t from = at;;) {
			const auto tab = file.find('\t', from);
			const auto stop = tab == std::string::npos || tab > end ? end : tab;
			std::string cell = file.substr(from, stop - from);
			while (!cell.empty() && (cell.back() == '\r' || cell.back() == ' ')) cell.pop_back();
			row.push_back(std::move(cell));
			if (stop == end) break;
			from = stop + 1;
		}
		out.push_back(std::move(row));
		at = end + 1;
	}
	return out;
}

// The choice rolls' names, from the game's txt files: skills (and their
// SkillDesc name keys), CharStats (classes, skill tab keys), ItemStatCost
// (description keys), PropertyGroups (parameters as written).
// What the game gives by name: treasure class items (Uber Ancients' colossal
// jewels) and cube outputs (renewed sunders). A unique the tables mark not
// spawnable is still in the grail when one of them names it.
void ReadObtainable(SetTables& tables, const std::string& treasure, const std::string& cube) {
	for (const auto* file : { &treasure, &cube }) {
		const auto rows = TxtCells(*file);
		if (rows.empty()) continue;
		std::vector<int> wanted;
		for (std::size_t k = 0; k < rows[0].size(); ++k) {
			std::string head = rows[0][k];
			for (auto& c : head) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			if (file == &treasure ? head.rfind("item", 0) == 0 : head.rfind("output", 0) == 0) wanted.push_back(static_cast<int>(k));
		}
		for (std::size_t r = 1; r < rows.size(); ++r)
			for (const int k : wanted)
				if (k < static_cast<int>(rows[r].size()) && !rows[r][k].empty()) {
					std::string name = rows[r][k];
					if (const auto comma = name.find(','); comma != std::string::npos) name.resize(comma); // "name,qualifiers"
					if (!name.empty() && name.front() == '"') name.erase(0, 1);
					tables.obtainable.insert(name);
					if (file == &cube) tables.cubeMade.insert(name);
				}
	}
}

void ReadVariantTexts(SetTables& tables, const std::string& skills, const std::string& skillDesc, const std::string& charStats, const std::string& statCost, const std::string& groups) {
	const auto column = [](const std::vector<std::string>& head, const char* name) {
		for (std::size_t k = 0; k < head.size(); ++k)
			if (!_stricmp(head[k].c_str(), name)) return static_cast<int>(k);
		return -1;
	};
	const auto cell = [](const std::vector<std::string>& row, int k) { return k >= 0 && k < static_cast<int>(row.size()) ? row[k] : std::string(); };
	std::map<std::string, std::string> descKeys; // skilldesc -> str name
	if (const auto rows = TxtCells(skillDesc); !rows.empty()) {
		const int name = column(rows[0], "skilldesc"), key = column(rows[0], "str name");
		for (std::size_t r = 1; r < rows.size(); ++r)
			if (!cell(rows[r], name).empty()) descKeys[cell(rows[r], name)] = cell(rows[r], key);
	}
	if (const auto rows = TxtCells(skills); !rows.empty()) {
		const int name = column(rows[0], "skill"), id = column(rows[0], "*Id") >= 0 ? column(rows[0], "*Id") : column(rows[0], "Id"), desc = column(rows[0], "skilldesc");
		for (std::size_t r = 1; r < rows.size(); ++r) {
			const auto text = cell(rows[r], id);
			if (text.empty() || cell(rows[r], name).empty() || !std::isdigit(static_cast<unsigned char>(text[0]))) continue;
			const int skill = std::atoi(text.c_str());
			tables.skillNames[skill] = cell(rows[r], name);
			std::string lower = cell(rows[r], name);
			for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			tables.skillIds.try_emplace(lower, skill);
			if (const auto key = descKeys.find(cell(rows[r], desc)); key != descKeys.end() && !key->second.empty()) tables.skillKeys[skill] = key->second;
		}
	}
	if (const auto rows = TxtCells(charStats); !rows.empty()) {
		const int name = column(rows[0], "class"), tab1 = column(rows[0], "StrSkillTab1"), tab2 = column(rows[0], "StrSkillTab2"), tab3 = column(rows[0], "StrSkillTab3");
		for (std::size_t r = 1; r < rows.size(); ++r) {
			const auto text = cell(rows[r], name);
			if (text.empty() || text == "Expansion") continue; // the row that splits the classic classes from the others
			tables.classNames.push_back(text);
			tables.tabKeys.push_back({ cell(rows[r], tab1), cell(rows[r], tab2), cell(rows[r], tab3) });
		}
	}
	if (const auto rows = TxtCells(statCost); !rows.empty()) {
		const int id = column(rows[0], "*ID") >= 0 ? column(rows[0], "*ID") : column(rows[0], "ID"), key = column(rows[0], "descstrpos"), stat = column(rows[0], "Stat");
		for (std::size_t r = 1; r < rows.size(); ++r) {
			const auto text = cell(rows[r], id);
			if (text.empty() || !std::isdigit(static_cast<unsigned char>(text[0]))) continue;
			if (!cell(rows[r], key).empty()) tables.statKeys[std::atoi(text.c_str())] = cell(rows[r], key);
			if (auto name = cell(rows[r], stat); !name.empty()) {
				for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				tables.statNames[std::atoi(text.c_str())] = name;
			}
		}
	}
	if (const auto rows = TxtCells(groups); !rows.empty()) {
		int par[8];
		for (int k = 0; k < 8; ++k) par[k] = column(rows[0], ("ParMin" + std::to_string(k + 1)).c_str());
		for (std::size_t r = 1; r < rows.size(); ++r) {
			if (cell(rows[r], 0).empty() || cell(rows[r], 0) == "Expansion") continue;
			std::array<std::string, 8> names;
			for (int k = 0; k < 8; ++k) names[k] = cell(rows[r], par[k]);
			tables.groupParNames.push_back(names);
		}
	}
}

// ---- variants: the choice rolls ----
// A game text as a short name: its format codes and numbers gone ("+%d to
// Demon Skills" -> "Demon", "+%d%% to Fire Skill Damage" -> "Fire Skill Damage").
std::string ShortLabel(std::string text) {
	text = StripColors(text);
	std::string out;
	for (std::size_t i = 0; i < text.size(); ++i) {
		const char c = text[i];
		if (c == '%') { // %d, %+d, %i, %s, %%
			std::size_t k = i + 1;
			while (k < text.size() && (text[k] == '+' || text[k] == '-')) ++k;
			if (k < text.size()) i = k;
			continue;
		}
		if ((c >= '0' && c <= '9') || c == '+' || c == '\n') {
			out += ' ';
			continue;
		}
		out += c;
	}
	// Spaces collapsed, then the English words around a name.
	std::string words;
	for (std::size_t i = 0; i < out.size(); ++i)
		if (out[i] != ' ' || (!words.empty() && words.back() != ' ')) words += out[i];
	while (!words.empty() && (words.back() == ' ' || words.back() == ':')) words.pop_back();
	for (const char* lead : { "to ", "To " })
		if (words.rfind(lead, 0) == 0) words.erase(0, std::strlen(lead));
	for (const char* tail : { " Skills", " skills", " Skill Levels" })
		if (const std::size_t n = std::strlen(tail); words.size() > n && words.compare(words.size() - n, n, tail) == 0) words.resize(words.size() - n);
	return words;
}

// A string by its key, in the texts' language (else the game's, else empty).
std::string KeyLabel(const std::string& key, const SetTables& tables) {
	if (key.empty()) return {};
	std::string text = GameKeyText(key);
	if (text.empty() && tables.keyText) text = tables.keyText(key.c_str());
	return text;
}

// The name of one choice: a skill's, a class's, a skill tab's, else the stat's description.
std::string ChoiceLabel(int stat, int layer, const SetTables& tables) {
	std::string label;
	if (stat == 107 || stat == 97) { // item_singleskill / item_nonclassskill: the skill
		if (const auto key = tables.skillKeys.find(layer); key != tables.skillKeys.end()) label = StripColors(KeyLabel(key->second, tables));
		if (label.empty())
			if (const auto name = tables.skillNames.find(layer); name != tables.skillNames.end()) label = name->second;
	} else if (stat == 83) { // item_addclassskills: the class
		if (layer >= 0 && layer < static_cast<int>(tables.classNames.size())) label = Tr(tables.classNames[layer]);
	} else if (stat == 188) { // item_addskill_tab: class * 8 + tab
		const int c = layer / 8, tab = layer % 8;
		if (c < static_cast<int>(tables.tabKeys.size()) && tab < 3) label = ShortLabel(KeyLabel(tables.tabKeys[c][tab], tables));
	} else if (const auto key = tables.statKeys.find(stat); key != tables.statKeys.end())
		label = ShortLabel(KeyLabel(key->second, tables));
	return label;
}

// The choices of a row's choice rolls (its props `first`..`count`): a
// property group (one of its properties), a random skill (func 12: skills
// min-max) or a random class's skills (func 36: classes min-max). Empty when
// it has none; several choice rolls: the first one's.
std::vector<VariantChoice> ChoicesOf(const unsigned char* row, int first, int count, const SetTables& tables, const PropLayout& layout, std::uint64_t* tooMany = nullptr) {
	std::vector<VariantChoice> out;
	if (layout.func < 0 || layout.stat < 0 || !tables.props.rows) return out;
	const auto propRow = [&](int prop) { return tables.props.rows + static_cast<std::size_t>(prop) * tables.propSize; };
	const auto add     = [&](int stat, int layer, int low, int high) {
        if (stat < 0 || stat >= 0xFFFF) return;
        for (const auto& known : out)
            if (known.stat == stat && known.layer == layer) return;
        out.push_back({ stat, layer, std::min(low, high), std::max(low, high), {} });
	};
	// The stat and layer a property gives with parameter `par`.
	const auto choice = [&](int prop, int par, int low, int high) {
		if (prop < 0 || static_cast<std::uint32_t>(prop) >= tables.props.count) return;
		const unsigned char* text = propRow(prop);
		const int            func = text[layout.func], stat = Field<std::uint16_t>(text, layout.stat);
		const int            value = layout.value >= 0 ? Field<std::uint16_t>(text, layout.value) : 0;
		if (func == 10) add(stat, (par / 3) * 8 + par % 3, low, high);   // a skill tab: three per class
		else if (func == 21) add(stat, value, low, high);                 // a class's skills: its value
		else if (func == 22) add(stat, par, low, high);                   // a skill
		else if (func == 7) add(17, 0, low, high);                        // enhanced damage (max and min)
		else if (func == 5 || func == 6 || func == 11 || func == 12 || func == 36) return; // ranges and procs: not choices
		else add(stat, 0, low, high);
	};
	// Each choice roll's choices; several rolls (renewed sunders' affix
	// groups) give a variant per mix.
	std::vector<std::vector<VariantChoice>> rolls;
	const auto                              settle = [&] {
        if (out.empty()) return;
        for (auto& c : out) {
            c.label = ChoiceLabel(c.stat, c.layer, tables);
            if (c.label.empty()) c.label = Tr("Variant") + " " + std::to_string(&c - out.data() + 1);
        }
        rolls.push_back(std::move(out));
        out.clear();
	};
	for (int p = 0; p < count; ++p, settle()) {
		const int at = first + p * 16;
		const int prop = Field<std::int32_t>(row, at), par = Field<std::int32_t>(row, at + 4), low = Field<std::int32_t>(row, at + 8), high = Field<std::int32_t>(row, at + 12);
		if (prop >= 0x10000) { // a property group: one of its properties
			const int g = prop - 0x10000;
			if (!tables.groups.rows || g < 0 || static_cast<std::uint32_t>(g) >= tables.groups.count || tables.groupSize < 8 + 24) continue;
			const unsigned char* group = tables.groups.rows + static_cast<std::size_t>(g) * tables.groupSize;
			if (Field<std::uint32_t>(group, 4) == 0) continue; // every property, no choice
			const int entries = std::min<int>(8, static_cast<int>((tables.groupSize - 8) / 24));
			for (int e = 0; e < entries; ++e) {
				const unsigned char* option = group + 8 + e * 24;
				const int            what = Field<std::int32_t>(option, 0);
				int                  parMin = Field<std::int32_t>(option, 4), parMax = Field<std::int32_t>(option, 8);
				const int            modMin = Field<std::int32_t>(option, 12), modMax = Field<std::int32_t>(option, 16);
				if (what < 0 || static_cast<std::uint32_t>(what) >= tables.props.count) continue;
				// A skill by its name in the txt file (the compiled rows may keep 0).
				if (parMin == 0 && parMax == 0 && g < static_cast<int>(tables.groupParNames.size())) {
					std::string name = tables.groupParNames[g][e];
					for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
					if (const auto id = tables.skillIds.find(name); !name.empty() && id != tables.skillIds.end()) parMin = parMax = id->second;
					else if (!name.empty() && std::all_of(name.begin(), name.end(), [](char c) { return c >= '0' && c <= '9'; })) parMin = parMax = std::atoi(name.c_str());
				}
				if (parMax < parMin || parMax - parMin > 64) parMax = parMin;
				for (int q = parMin; q <= parMax; ++q) choice(what, q, modMin, modMax);
			}
			if (out.size() < 2) out.clear(); // one option: no choice
			continue;
		}
		if (prop < 0 || static_cast<std::uint32_t>(prop) >= tables.props.count) continue;
		const unsigned char* text = propRow(prop);
		const int            func = text[layout.func], stat = Field<std::uint16_t>(text, layout.stat);
		const int            value = layout.value >= 0 ? Field<std::uint16_t>(text, layout.value) : 0;
		if (func == 12 && high > low && high - low <= 400) // a random skill of skills low..high, at level par
			for (int skill = low; skill <= high; ++skill) add(stat, skill, par, par);
		else if (func == 36 && high > low && high - low <= 32) // a random class's skills, classes low..high
			for (int c = low; c <= high; ++c) add(stat, c, value, value);
		if (out.size() < 2) out.clear();
	}
	constexpr std::size_t VariantCeiling = 500;
	// More than VariantCeiling mixes (modded rolls of 30 skills each): no
	// variants for that item, its one tile.
	std::uint64_t total = rolls.empty() ? 0 : 1;
	for (const auto& roll : rolls) total = total > UINT64_MAX / std::max<std::size_t>(1, roll.size()) ? UINT64_MAX : total * roll.size();
	if (total > VariantCeiling) {
		if (tooMany) *tooMany = total;
		return {};
	}
	if (rolls.size() == 1) return rolls[0];
	if (rolls.empty()) return {};
	// Every mix, the first roll's choice varying slowest.
	std::vector<VariantChoice> mixes { VariantChoice {} };
	for (const auto& roll : rolls) {
		std::vector<VariantChoice> next;
		for (const auto& mix : mixes)
			for (const auto& choice : roll) {
				VariantChoice made = mix;
				if (made.parts.empty()) made.stat = choice.stat, made.layer = choice.layer, made.low = choice.low, made.high = choice.high;
				made.parts.push_back({ choice.stat, choice.layer });
				made.label += (made.label.empty() ? "" : ", ") + choice.label;
				next.push_back(std::move(made));
			}
		mixes = std::move(next);
	}
	return mixes;
}

// A variant's id after its base's: "~<choice key>", "~p<picture>", or both.
std::string VariantSuffix(const UniqueEntry& base, const UniqueEntry& entry) {
	std::string suffix;
	if (entry.affix >= 0 && entry.affix < static_cast<int>(base.affixes.size())) suffix += "~" + ChoiceKey(base.affixes[entry.affix]);
	if (entry.picture >= 0) suffix += "~p" + std::to_string(entry.picture + 1);
	return suffix;
}

// The art of picture `p` of a base whose art is `art` (ring1..ring5: its digits replaced).
std::string PictureArt(std::string art, int p) {
	while (!art.empty() && art.back() >= '0' && art.back() <= '9') art.pop_back();
	return art + std::to_string(p + 1);
}

// The variant entries of `base`: per choice, per picture, then per both.
// Its name without the brackets: the tile's label (the brackets are its tooltip).
std::string PlainName(int u);

// A picture variant's word: a jewel's colour (the game's six jewel pictures),
// else "Picture n".
std::string PictureLabel(const UniqueEntry& base, int p) {
	static constexpr const char* colours[] { "Purple", "Blue", "Orange", "Green", "Red", "White" };
	if ((base.code == "jew" || base.code == "cjw") && base.pictures == 6 && p >= 0 && p < 6) return Tr(colours[p]);
	return Tr("Picture") + " " + std::to_string(p + 1);
}

// A variant keeps no list of its own: its base's choices are read from its base.
std::vector<UniqueEntry> VariantEntries(UniqueEntry& base) {
	std::vector<UniqueEntry> out;
	const int   affixes = static_cast<int>(base.affixes.size()), pictures = base.pictures > 1 ? base.pictures : 0;
	UniqueEntry shell;
	{
		auto keep  = std::move(base.affixes);
		auto links = std::move(base.variants);
		shell      = base;
		base.affixes = std::move(keep), base.variants = std::move(links);
	}
	const auto make = [&](int a, int p) {
        UniqueEntry entry = shell;
        entry.variant     = true;
        entry.affix   = a;
        entry.picture = p;
        std::string label;
        if (a >= 0) label = base.affixes[a].label;
        if (p >= 0) {
            label += (label.empty() ? "" : ", ") + PictureLabel(base, p);
            entry.ghost = PictureArt(base.ghost, p);
            if (base.art == base.ghost) entry.art = entry.ghost;
        }
        // In the base's brackets when it has some: "Rainbow Facet (Fire, Die, Red)".
        entry.name   = !base.name.empty() && base.name.back() == ')' ? base.name.substr(0, base.name.size() - 1) + ", " + label + ")" : base.name + " (" + label + ")";
        entry.detail = base.detail.empty() ? label : base.detail + ", " + label;
        entry.id   = base.id + VariantSuffix(base, entry);
        out.push_back(std::move(entry));
	};
	for (int a = 0; a < affixes; ++a) make(a, -1);
	for (int p = 0; p < pictures; ++p) make(-1, p);
	for (int a = 0; a < affixes; ++a)
		for (int p = 0; p < pictures; ++p) make(a, p);
	return out;
}

std::string PlainName(int u) {
	const auto& entry = Uniques[u];
	const auto  tail  = " (" + entry.detail + ")";
	if (!entry.detail.empty() && entry.name.size() > tail.size() && entry.name.compare(entry.name.size() - tail.size(), tail.size(), tail) == 0) return entry.name.substr(0, entry.name.size() - tail.size());
	return entry.name;
}

bool AddVariants(const std::vector<std::string>* order = nullptr);
void BuildObjectives();
void ResizeRolls();

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
// The loot filter's exclusions: by tile (an ethereal tile has its own); only
// uniques and set items are filtered.
// A variant is excluded with its base (one key for all its choices).
std::string ExcludeKey(int u, bool eth) { return Uniques[Uniques[u].variant && Uniques[u].variantOf >= 0 ? Uniques[u].variantOf : u].id + (eth ? "|eth" : ""); }
bool        Excludable(const UniqueEntry& entry) { return !entry.runeword && !entry.misc && !entry.objective && !entry.secret && !entry.variant; }
bool        Excluded(int u, bool eth) { return ExcludedKey(ExcludeKey(u, eth)); }
// Lock held. Whether the editor's list holds a tile.
// The editor's key of a tile: an exclusion's, or (variant lists) the item's id.
std::string DraftKey(int u, bool eth) { return VarEditing ? Uniques[u].id : ExcludeKey(u, eth); }
bool        Drafted(int u, bool eth) { return ListHas(ExclDraft, DraftKey(u, eth)); }
// Lock held. The editor's list about to change: a preset's items become its own.
void OwnBuiltin() {
	auto& list = ExclDraft;
	if (list.preset < 0 || list.own) return;
	list.items.clear();
	for (int u = 0; u < UniqueCount(); ++u)
		for (const bool eth : { false, true })
			if (Excludable(Uniques[u]) && InPreset(list.preset, ExcludeKey(u, eth))) list.items.insert(ExcludeKey(u, eth));
	list.own = true;
}

// A list name not taken yet: `base`, else `base 2`, `base 3`...
std::string FreeListName(const std::string& base) {
	const auto taken = [](const std::string& name) { return std::any_of(ExclLists.begin(), ExclLists.end(), [&](const ExclList& list) { return list.name == name; }); };
	if (!taken(base)) return base;
	for (int n = 2;; ++n)
		if (!taken(base + " " + std::to_string(n))) return base + " " + std::to_string(n);
}
std::vector<char>          CategoryOff; // a misc category not tracked: no tiles
std::vector<std::array<char, 2>> CategoryHas; // whether a category has plain / ethereal tiles
bool HasTile(int u, bool eth);
bool BaseHasTile(int u, bool eth);
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
constexpr int            TypeObjective = 0, TypeSearch = 1, TypeExclude = 2, TypeListName = 3; // the Loot page's list search, its list's name
constexpr int            TypeSlot(int target) { return target < 0 || target > 3 ? 0 : target; }
void BuildLists(TableRows types, TableRows items, TableRows uniques, TableRows sets, NameLookup names, const ArtMaps& mod, const SetTables& setTables = {}) {
	// Built again: the items whose variants were made, in the order they were
	// (so their entries come back at the same indices).
	std::vector<std::string> variantOrder;
	{
		std::set<int> seen;
		for (const auto& entry : Uniques)
			if (entry.variant && entry.variantOf >= 0 && entry.variantOf < UniqueCount() && seen.insert(entry.variantOf).second) variantOrder.push_back(Uniques[entry.variantOf].id);
	}
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
		if (const auto own = Field<std::uint16_t>(row, ItemRow::Type); own && own < types.count) base.type = typeCodes[own], base.pictures = types.rows[static_cast<std::size_t>(own) * TypeRowSize + TypeRow::VarInvGfx];
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
		// A jewel's own art is the game's gem/perfect_diamond1..6 (the plain
		// name is the perfect diamond gem): its first picture by default.
		if ((code == "jew" || code == "cjw") && !mod.bases.count(code) && !base.asset.empty()) base.asset += "1";
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
		// Its variants: its choice rolls, and its pictures when it has no art of its own.
		entry.affixes = ChoicesOf(row, set ? SetRow::Props : UniqueRow::Props, set ? SetRow::PropCount : UniqueRow::PropCount, setTables, propLayout, &entry.tooMany);
		// (Not a cube's unique, only made by a recipe: renewed sunders all look alike.)
		const bool cubeOnly = !set && !(Field<std::uint32_t>(row, UniqueRow::Flags) & UniqueRow::Spawnable) && setTables.cubeMade.count(index);
		if (entry.art == entry.ghost && !cubeOnly && base->second.pictures > 1 && base->second.pictures <= 8) entry.pictures = base->second.pictures;
		found.push_back(entry);
		rule.push_back(base->second.category);
		return &found.back();
	};
	for (std::uint32_t k = 0; k < uniques.count; ++k) {
		const unsigned char* row = uniques.rows + static_cast<std::size_t>(k) * UniqueRowSize;
		if (!(Field<std::uint32_t>(row, UniqueRow::Flags) & UniqueRow::Spawnable)) {
			// Not dropped as a random unique: still in when a treasure class or the cube names it.
			const std::string index(reinterpret_cast<const char*>(row + UniqueRow::Index), strnlen(reinterpret_cast<const char*>(row + UniqueRow::Index), UniqueRow::IndexLength));
			if (index.empty() || !setTables.obtainable.count(index)) continue;
		}
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
	// Uniques sharing a name (the Rainbow Facets): what tells them apart after
	// it, from their properties: "Rainbow Facet (Fire, Die)".
	{
		const auto words = [&](const UniqueEntry& e) {
			std::vector<std::string> out;
			if (e.set || e.runeword || e.misc || e.row < 0 || static_cast<std::uint32_t>(e.row) >= uniques.count || propLayout.stat < 0) return out;
			const unsigned char* row  = uniques.rows + static_cast<std::size_t>(e.row) * UniqueRowSize;
			const auto           push = [&](const char* word) {
                if (std::find(out.begin(), out.end(), word) == out.end()) out.push_back(word);
			};
			for (int p = 0; p < UniqueRow::PropCount; ++p) {
				const int prop = Field<std::int32_t>(row, UniqueRow::Props + p * 16);
				if (prop < 0 || static_cast<std::uint32_t>(prop) >= setTables.props.count) continue;
				const int  stat = Field<std::uint16_t>(setTables.props.rows + static_cast<std::size_t>(prop) * setTables.propSize, propLayout.stat);
				const auto name = setTables.statNames.find(stat);
				if (name == setTables.statNames.end()) continue;
				const std::string& n = name->second;
				if (n == "item_skillondeath") push("Die");
				else if (n == "item_skillonlevelup") push("Level up");
				else if (n == "item_skillonhit") push("Hit");
				else if (n == "item_skillongethit") push("Struck");
				else if (n == "item_skillonattack") push("Attack");
				else if (n == "item_skillonkill") push("Kill");
				else if (n.find("fire") != std::string::npos) push("Fire");
				else if (n.find("cold") != std::string::npos) push("Cold");
				else if (n.find("ltng") != std::string::npos || n.find("light") != std::string::npos) push("Lightning");
				else if (n.find("pois") != std::string::npos) push("Poison");
			}
			return out;
		};
		std::map<std::string, std::vector<std::size_t>> byName;
		for (std::size_t i = 0; i < found.size(); ++i)
			if (!found[i].set && !found[i].runeword && !found[i].misc) byName[found[i].name].push_back(i);
		for (const auto& [name, list] : byName) {
			if (list.size() < 2) continue;
			std::vector<std::vector<std::string>> all;
			for (const auto i : list) all.push_back(words(found[i]));
			std::vector<std::string> labels;
			for (const auto& mine : all) {
				std::string label;
				for (const auto& word : mine) {
					const bool everyone = std::all_of(all.begin(), all.end(), [&](const auto& other) { return std::find(other.begin(), other.end(), word) != other.end(); });
					if (!everyone) label += (label.empty() ? "" : ", ") + Tr(word);
				}
				labels.push_back(label);
			}
			const std::set<std::string> distinct(labels.begin(), labels.end());
			if (distinct.size() != labels.size() || distinct.count("")) continue; // nothing tells them apart: names as they are
			for (std::size_t k = 0; k < list.size(); ++k) found[list[k]].name += " (" + labels[k] + ")", found[list[k]].detail = labels[k];
		}
	}
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
	for (int u = 0; u < UniqueCount(); ++u)
		if (!Uniques[u].variant) (Uniques[u].runeword ? RunewordByRow : Uniques[u].set ? SetByRow : UniqueByRow)[Uniques[u].row] = u;
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
				if (base->second.pictures > 1 && base->second.pictures <= 8) entry.pictures = base->second.pictures;
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
	AddVariants(&variantOrder); // before the objectives (made just after)
	BuildObjectives();
	RebuildStored(); // records read before the lists
}

// ---- game files (game thread) ----
// The game's own files (the active mod's copy first): D2R GUI's GameFile.

// The Chronicle's grey rune stones (a sprite of 98x98 cells, the 33 runes by
// English name, Amn to Zod, in reading order): read on the game thread, cut and
// registered on the UI thread, with a blank stone made from them.
std::string ChronicleRunesFile;
std::string AdvancedButtonFile;
std::string GameFile(const char* path);

std::atomic<bool> SettingsArtRead {};
std::atomic<int> GenericRunes {}; // rune stones registered as grune<n>, and grune-blank
std::atomic<int> GemSocketSize { Cell }; // the game's socket picture's height, in pixels

// ---- item colours ----
// The grid colours a unique's art by its colour (UniqueItems invtransform):
// D2R GUI's ItemArt reads GlobalData's SpriteColoringHelper; the plugin's own
// pictures of stored items get the same fields.

// Game thread, with the lists.
void ReadItemColors(const std::string& text) {
	std::size_t counts[3] {};
	if (text.empty()) return;
	if (!Gui::ItemArt::ReadColors(text, counts)) {
		Problem(false, "grailon: no SpriteColoringHelper in the game's GlobalData; drawn items keep their art's colours");
		return;
	}
	Say(false, "grailon: item colours: %zu ranges, %zu colours, %zu sprite ranges", counts[0], counts[1], counts[2]);
}

// The coloring fields of a stored item's art, as the grid draws it.
std::string ColorFields(const UniqueEntry& entry, const std::string& art) { return Gui::ItemArt::ColorFields(entry.color, entry.colorClass, art); }

std::string GameFile(const char* path) {
	std::string trouble;
	std::string text = Gui::GameFile(path, &trouble);
	if (text.empty()) Problem(false, "grailon: could not read %s: %s", path, trouble.c_str());
	else {
		std::string start = text.substr(0, 48);
		for (char& c : start)
			if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) > 126) c = '.';
		Say(false, "grailon: read %s: %zu bytes (%s), %zu art entries, starts \"%s\"", path, text.size(), trouble.empty() ? "D2RCore's archive" : "the game's loader",
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
		ChronicleRunesFile         = Gui::GameFile(path);
		constexpr const char* socket = "data/hd/global/ui/panel/gemsocket.sprite";
		std::string           file   = Gui::GameFile(socket);
		if (file.size() >= 0x10 && !file.compare(0, 4, "SpA1")) {
			std::uint32_t height = 0;
			std::memcpy(&height, file.data() + 12, 4);
			if (height >= 16 && height <= 1024) GemSocketSize = static_cast<int>(height);
		}
		Say(false, "grailon: socket picture %d px high", GemSocketSize.load());
		constexpr const char* advanced = "data/hd/global/ui/panel/character_sheet/advancedstatsbutton.sprite";
		AdvancedButtonFile             = Gui::GameFile(advanced);
		if (AdvancedButtonFile.empty()) Say(false, "grailon: the Advanced Stats button picture was not found");
		Gui::Kit::Read(); // the settings page's art: the game's check box, dividers, list rows, drop-down parts
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
	optional(Tables::TableId::PropertyGroups, extra.groups, extra.groupSize);
	{
		const auto txt = [](const char* path) {
			return Gui::GameFile(path);
		};
		ReadVariantTexts(extra, txt("data/global/excel/skills.txt"), txt("data/global/excel/skilldesc.txt"), txt("data/global/excel/charstats.txt"),
		                 txt("data/global/excel/itemstatcost.txt"), txt("data/global/excel/propertygroups.txt"));
		ReadObtainable(extra, txt("data/global/excel/treasureclassex.txt"), txt("data/global/excel/cubemain.txt"));
		Say(false, "grailon: variants: %u property groups, %zu skill names, %zu classes, %zu stat texts", extra.groups.count, extra.skillKeys.size(), extra.classNames.size(), extra.statKeys.size());
	}
	// The item types' names (a comment column, not in the compiled table): from the game's itemtypes.txt.
	{
		constexpr const char* path = "data/global/excel/itemtypes.txt";
		std::string           file = Gui::GameFile(path);
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
	ExclLists = DefaultExclLists();
	AffixOff.clear(), GraphicOff.clear();
	if (!file) return;
	// The lists as the file has them ("loot_list" lines), else the default ones;
	// the items by list number.
	std::vector<ExclList>                   lists;
	std::map<int, std::set<std::string>>    listItems;
	std::set<int>                           owned;
	int                                     chosen = -1;
	std::set<int>                           actives;
	bool                                    activesKnown = false;
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
		// The exclusion lists: "loot_list = <preset>|<own>|<name>" each in order
		// (preset: none, valuable or interesting; own: 1 once a preset's list was
		// changed, its items then in the file), "loot_exclude_list = <number>"
		// (the one in use, from 0), "loot_exclude = <number>|<id>" ("|eth" for
		// an ethereal tile). A test build's names (valuable, interesting,
		// custom1-3) stand for the default lists and two more custom ones; an
		// id alone goes to Custom 1.
		if (!std::strncmp(line, "loot_list", 9) || !std::strncmp(line, "loot_exclude", 12)) {
			if (const char* at = std::strchr(line, '=')) {
				std::string value = at + 1;
				while (!value.empty() && (value.back() == '\n' || value.back() == '\r' || value.back() == ' ')) value.pop_back();
				while (!value.empty() && value.front() == ' ') value.erase(0, 1);
				const auto listOf = [](const std::string& key) {
					if (!key.empty() && std::all_of(key.begin(), key.end(), [](char c) { return c >= '0' && c <= '9'; })) return std::min(std::atoi(key.c_str()), 999);
					constexpr const char* old[] { "valuable", "interesting", "custom1", "custom2", "custom3" };
					for (int k = 0; k < 5; ++k)
						if (key == old[k]) return k;
					return -1;
				};
				if (!std::strncmp(line, "loot_list", 9)) {
					ExclList   list;
					const auto first = value.find('|'), second = first == std::string::npos ? first : value.find('|', first + 1);
					if (second != std::string::npos) {
						const auto preset = value.substr(0, first);
						list.preset       = preset == PresetKeys[0] ? 0 : preset == PresetKeys[1] ? 1 : -1;
						list.own          = value.substr(first + 1, second - first - 1) == "1";
						list.name         = value.substr(second + 1);
						if (list.name.empty()) list.name = "List";
						lists.push_back(list);
					}
				} else if (!std::strncmp(line, "loot_exclude_active", 19)) {
					activesKnown = true;
					for (std::size_t from = 0; from < value.size();) {
						const auto comma = value.find(',', from);
						const auto item  = value.substr(from, comma == std::string::npos ? std::string::npos : comma - from);
						if (const int k = listOf(item); k >= 0) actives.insert(k);
						if (comma == std::string::npos) break;
						from = comma + 1;
					}
				} else if (!std::strncmp(line, "loot_exclude_list", 17)) {
					if (const int k = listOf(value); k >= 0) chosen = k;
				} else if (!std::strncmp(line, "loot_exclude_own", 16)) {
					if (const int k = listOf(value); k >= 0) owned.insert(k);
				} else if (!value.empty()) {
					const auto bar = value.find('|');
					int        k   = bar == std::string::npos ? -1 : listOf(value.substr(0, bar));
					if (k >= 0) value.erase(0, bar + 1);
					else k = 2;
					if (!value.empty()) listItems[k].insert(value);
				}
			}
			continue;
		}
		// An objective: "objective = id|icon|text".
		// Items left out of affix / graphic variants: one id a line.
		if (!std::strncmp(line, "affix_variant_off", 17) || !std::strncmp(line, "graphic_variant_off", 19)) {
			if (const char* at = std::strchr(line, '=')) {
				std::string id = at + 1;
				while (!id.empty() && (id.back() == '\n' || id.back() == '\r' || id.back() == ' ')) id.pop_back();
				while (!id.empty() && id.front() == ' ') id.erase(0, 1);
				if (!id.empty()) (line[0] == 'a' ? AffixOff : GraphicOff).insert(id);
			}
			continue;
		}
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
		if (!std::strcmp(key, "variants")) Config.affixVariants = on; // a test build's
	}
	std::fclose(file);
	const bool fromTest = lists.empty();
	if (fromTest) {
		// No lists in the file: the presets, and a test build's custom ones (numbered 2-4 there).
		lists = DefaultExclLists();
		const int last = listItems.empty() ? 0 : std::min(listItems.rbegin()->first, 4);
		for (int k = 2; k <= std::max(last, chosen); ++k) lists.push_back({ "Custom " + std::to_string(k - 1) });
		for (const int k : owned)
			if (k < static_cast<int>(lists.size())) lists[k].own = true;
	}
	for (auto& [k, items] : listItems)
		if (k < static_cast<int>(lists.size())) lists[k].items = std::move(items);
	// Active: as the file says, else (a test build's) the one list in use.
	if (!activesKnown && chosen >= 0) actives.insert(chosen);
	if (fromTest && chosen < 0 && listItems.count(2)) actives.insert(2); // the exclusions from before the lists: on as they were
	for (const int k : actives)
		if (k < static_cast<int>(lists.size())) lists[k].active = true;
	// A test build's custom lists left empty and unused go.
	if (fromTest) std::erase_if(lists, [](const ExclList& list) { return list.preset < 0 && list.items.empty() && !list.active; });
	ExclLists = std::move(lists);
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
	std::fprintf(file, "# Items the loot filter never hides (chosen in the Loot settings). The lists in order: preset (none, valuable, interesting)|1 once a preset's list\n"
	                   "# was changed (its items then follow, else it keeps the preset's)|name; the active lists (numbers from 0); then list number|grail entry id, |eth for the ethereal tile.\n");
	std::string active;
	for (std::size_t k = 0; k < ExclLists.size(); ++k) {
		const auto& list = ExclLists[k];
		std::fprintf(file, "loot_list = %s|%d|%s\n", list.preset >= 0 ? PresetKeys[list.preset] : "none", list.own ? 1 : 0, list.name.c_str());
		if (list.active) active += (active.empty() ? "" : ",") + std::to_string(k);
	}
	std::fprintf(file, "loot_exclude_active = %s\n", active.c_str());
	for (std::size_t k = 0; k < ExclLists.size(); ++k)
		if (ExclLists[k].preset < 0 || ExclLists[k].own)
			for (const auto& id : ExclLists[k].items) std::fprintf(file, "loot_exclude = %zu|%s\n", k, id.c_str());
	std::fprintf(file, "# Items left out of affix / graphic variants (chosen in the Grail settings): one grail entry id a line.\n");
	for (const auto& id : AffixOff) std::fprintf(file, "affix_variant_off = %s\n", id.c_str());
	for (const auto& id : GraphicOff) std::fprintf(file, "graphic_variant_off = %s\n", id.c_str());
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
// The game's runeword flag on an item: 1 or 0, -1 when not known (no unit,
// or the flag found wrong once).
std::atomic<bool> RunewordFlagTrusted { true };
int               RunewordFlagOn(std::uint32_t runtimeId);

int  RawStat(const void* unit, int stat, int layer);
// Whether an item has a choice: its stat, or every roll's for several.
bool ChoiceOn(const void* unit, const VariantChoice& choice) {
	if (choice.parts.empty()) return RawStat(unit, choice.stat, choice.layer) != 0;
	for (const auto& [stat, layer] : choice.parts)
		if (RawStat(unit, stat, layer) == 0) return false;
	return true;
}
void* ClientItem(std::uint32_t runtimeId);
// Lock held. Whether base entry `b`'s affix (graphic) variants have tiles:
// the setting on, and the item not left out of it.
bool AffixOn(int b) { return Config.affixVariants && !Uniques[b].affixes.empty() && !AffixOff.count(Uniques[b].id); }
bool GraphicOn(int b) { return Config.graphicVariants && Uniques[b].pictures > 1 && !GraphicOff.count(Uniques[b].id); }
// Lock held. The entry whose tile an item of base entry `b` with choice `a`
// and picture `p` (-1: not known) goes in: `b` when its variants have no
// tiles, -1 when a part the tiles need is not known.
int VariantOf(int b, int a, int p) {
	if (b < 0 || b >= UniqueCount() || Uniques[b].variant || Uniques[b].variants.empty()) return b;
	const bool affix = AffixOn(b), graphic = GraphicOn(b);
	if (!affix && !graphic) return b;
	if ((affix && a < 0) || (graphic && p < 0)) return -1;
	const int wantA = affix ? a : -1, wantP = graphic ? p : -1;
	for (const int v : Uniques[b].variants)
		if (Uniques[v].affix == wantA && Uniques[v].picture == wantP) return v;
	return -1;
}
// Lock held. The choice item `unit` of base entry `b` has (its choice's stat on it), or -1.
int AffixIndex(int b, const void* unit) {
	if (!unit || b < 0 || b >= UniqueCount()) return -1;
	const auto& choices = Uniques[b].affixes;
	for (std::size_t c = 0; c < choices.size(); ++c)
		if (ChoiceOn(unit, choices[c])) return static_cast<int>(c);
	return -1;
}
int AffixIndexOfKey(int b, const std::string& key) {
	if (key.empty() || b < 0 || b >= UniqueCount()) return -1;
	const auto& choices = Uniques[b].affixes;
	for (std::size_t c = 0; c < choices.size(); ++c)
		if (ChoiceKey(choices[c]) == key) return static_cast<int>(c);
	return -1;
}
// Items' variant entries, by runtime id (read once; cleared with the game and the settings). Guarded by Lock.
std::map<std::uint32_t, int> VariantUnits;
// Items' pictures, by runtime id (-1: none; from their save bytes). Guarded by Lock.
std::map<std::uint32_t, int> Pictures;
int PictureOfItem(const Items::ItemInfo& item);

int GrailEntry(const Items::ItemInfo& item);
// The grail entry of an item: its index in Uniques, or -1. An item with
// variants is its variant's when they have tiles.
int GrailIndex(const Items::ItemInfo& item) {
	const int u = GrailEntry(item);
	bool      graphic = false;
	int       a       = -1;
	{
		std::lock_guard guard(Lock);
		if (u < 0 || u >= UniqueCount() || Uniques[u].variant || Uniques[u].variants.empty() || (!AffixOn(u) && !GraphicOn(u))) return u;
		if (const auto known = VariantUnits.find(item.runtimeId); known != VariantUnits.end() && known->second >= 0 && known->second < UniqueCount() && Uniques[known->second].variantOf == u)
			return known->second;
		if (AffixOn(u)) a = AffixIndex(u, ClientItem(item.runtimeId));
		graphic = GraphicOn(u);
	}
	const int p = graphic ? PictureOfItem(item) : -1;
	std::lock_guard guard(Lock);
	const int v = VariantOf(u, a, p);
	if (v < 0) return u; // not known: its base's (no tile)
	VariantUnits[item.runtimeId] = v;
	return v;
}

int GrailEntryOf(const Items::ItemInfo& item);
bool OldVersion(int u, void* unit);
std::map<std::uint32_t, bool> OldUnits; // runtime id -> an older version of its item (checked once); guarded by Lock
void* ClientItem(std::uint32_t runtimeId);
// The entry, but -1 for an older version of an item the current ladder season
// reworked: that copy is not the item the grail collects any more.
int GrailEntry(const Items::ItemInfo& item) {
	const int u = GrailEntryOf(item);
	if (u < 0) return u;
	std::lock_guard guard(Lock);
	if (u >= UniqueCount() || Uniques[u].misc || !(item.stateFlags & Items::ItemStateIdentified)) return u;
	auto known = OldUnits.find(item.runtimeId);
	if (known == OldUnits.end()) {
		void* unit = ClientItem(item.runtimeId);
		if (!unit) return u;
		known = OldUnits.emplace(item.runtimeId, OldVersion(u, unit)).first;
		if (known->second) {
			static int told = 0;
			if (told++ < 20) Say(false, "grailon: %s is an older version than this season's: not taken", Uniques[u].name.c_str());
		}
	}
	return known->second ? -1 : u;
}

int GrailEntryOf(const Items::ItemInfo& item) {
	std::lock_guard guard(Lock);
	// A base kept in a runeword tile (not once its runes are in).
	if (const auto base = BaseFor.find(item.runtimeId); base != BaseFor.end()) {
		if (RunewordFlagOn(item.runtimeId) != 1) return base->second;
		BaseFor.erase(base);
	}
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
// The scroll bars (D2R GUI's): the view's, and the Loot page's exclusions
// list's (shorter, where the view's is). The track is one long empty slot
// (a shorter bar shows its top and its bottom edge), the steps quiet.
Gui::ScrollBar::Look BarLook() {
	return { .track = [](int w, int h) { return Kit::Slot(w, h); }, .thumb = [](int w, int h) { return Gui::Kit::Thumb(w, h); }, .trackHeight = BarHeight, .edge = 16, .quietSteps = true, .nestedX = -MenuRoom };
}
Gui::ScrollBar GridBar("", BarLeft, BarWidth, StepHeight, 236, PanelY), ExclBar("excl", BarLeft, BarWidth, StepHeight, 236, PanelY);
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
// Whether unique `u` (or its variants, whichever the setting shows) has a
// tile for its plain or its ethereal copy.
// Lock held. Every entry in page order: each item's variants right after it
// (wherever they are in Uniques).
template <typename Visit>
void InTileOrder(Visit visit) {
	for (int u = 0; u < UniqueCount(); ++u) {
		if (Uniques[u].variant) continue;
		visit(u);
		for (const int v : Uniques[u].variants) visit(v);
	}
}
bool BaseHasTile(int u, bool eth) {
	const auto& entry = Uniques[u];
	if (entry.misc && (eth || (entry.category >= 0 && entry.category < static_cast<int>(CategoryOff.size()) && CategoryOff[entry.category]))) return false;
	return eth ? entry.eth : !entry.onlyEth;
}
// Variants on: the variants' tiles, not their base's; off: the base's only.
bool HasTile(int u, bool eth) {
	const auto& entry = Uniques[u];
	if (entry.variant) {
		const int b = entry.variantOf;
		if (b < 0 || (entry.affix >= 0) != AffixOn(b) || (entry.picture >= 0) != GraphicOn(b)) return false;
	} else if (!entry.variants.empty() && (AffixOn(u) || GraphicOn(u))) return false;
	return BaseHasTile(u, eth);
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
	if (menu == LostMenu && c < static_cast<int>(CategoryHas.size())) return CategoryHas[c][0] || CategoryHas[c][1];
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
	InTileOrder([&](int i) {
		if (Uniques[i].category != category || Uniques[i].secret) return;
		const bool eth = Uniques[i].lost ? Uniques[i].onlyEth : ViewEth; // a lost item: its own tile, ethereal or not
		if (HasTile(i, eth)) add(i, eth);
	});
	return rows;
}

// Lock held. Picks the rows that fit on the page from Scroll on.
// ---- 1-wide items centred in their tile ----
// A tile is two cells wide; a 1-wide item sits in its first column. The page
// grid draws it half a cell further right (Gui::TileGrid wraps the game's grid
// vtable for the plugin's grid only); the items stay in their real cells.
// Wrapped on the first view, after every plugin loaded. While the grail's grid
// draws, no "cannot wear" colour on its items (the game's and Item Quality
// Backgrounds').
using HalfTile = Gui::TileGrid::Shift;

// The next view's half cells, applied as it opens: before, the view on
// screen would draw its 1-wide items with the next view's offsets. Lock held.
std::vector<HalfTile> NextHalfTiles;
bool                  HalfTilesWaiting {};
void ApplyHalfTiles() {
	if (!HalfTilesWaiting) return;
	HalfTilesWaiting = false;
	Gui::TileGrid::SetShifts(std::move(NextHalfTiles));
	NextHalfTiles.clear();
}

// The plugin's own grid, or the host's grid of the grail's area (its cells
// stay the host page's: OriginX/Y take them to the grail's).
bool IsGrailGrid(const void* widget) {
	if (Host::On()) return Gui::TileGrid::Marked(widget, Host::Info.gridMarker[0], Host::Info.gridMarker[1]);
	return Gui::TileGrid::Marked(widget, Gui::TileGrid::Marker, Gui::TileGrid::Marker);
}
int OriginX() { return Host::On() ? Host::Info.x : 0; }
int OriginY() { return Host::On() ? Host::Info.y : 0; }

// The screen rect (x, y, w, h) of `size` cells from grail cell (x, y).
void TileRect(void* widget, std::int32_t* out, std::uint64_t size, int x, int y) { Gui::TileGrid::TileRect(widget, out, size, x, y); }

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

bool ViewOpen();
void PeekTooltip(void* widget);
void MoveMoldFrom(void* grid);
void InstallSoundMute();

// UI thread, once a view opens.
void WrapGridVtable() {
	static bool tried = false;
	if (tried) return;
	tried = true;
	InstallSoundMute();
	Gui::TileGrid::Wrap({
		.mine   = [](const void* widget) { return IsGrailGrid(widget); },
		.origin = [] { return std::pair<int, int> { OriginX(), OriginY() }; },
		// The host's grid is in the host's panel, closed a moment after the view: not drawn meanwhile.
		.draw = [](void* widget, bool grail) {
			if (grail && Host::On() && !ViewOpen()) return false;
			if (!grail) MoveMoldFrom(widget); // the inventory's grid: the grail button, moved by the settings
			return true;
		},
		.drawn = [](void* widget, bool grail) {
			if (grail) PeekTooltip(widget);
		},
		.clearRequirements = true,
		.problem = [](const char* what) {
			std::string text(what);
			if (const auto at = text.find("the plugin's items"); at != std::string::npos) text.replace(at, 18, "grail items");
			Problem(false, "grailon: %s", text.c_str());
		},
	});
}

void UnwrapGridVtable() { Gui::TileGrid::Unwrap(); }

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
	GridBar.Set(BarTop, ScrollSteps, MaxScroll);
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

// Every widget is D2R GUI's (layout.h). The plugin's texts go in as they are
// (it translates them itself: Services::plainText). Item art and the plugin's
// own icons are file paths as the tables give them (single backslashes):
// Image and Icon take those; the buttons take D2R GUI's sprite names.
using Gui::Json;
using Gui::Rect;
using Gui::Command;

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
	return Gui::TextBox(name, x, y, width, height, text, { .color = color, .size = size, .h = align, .v = vertical, .wrap = true });
}

// An image of a file path as the tables give it.
std::string Image(const std::string& name, int x, int y, const std::string& filename, const std::string& extra = "") { return Gui::Image(name, x, y, Gui::Path(filename), extra); }

std::string GrayFields() { return Gui::ItemArt::GrayFields(Config.saturation, Config.brightness); }

// A square button on sprite `file` (frames 0 normal, 1 hovered, 2 pressed), sending `command`.
std::string SquareButton(const std::string& name, int x, int y, const std::string& file, const std::string& tooltip, const std::string& command, int size = ButtonSize) {
	return Gui::SpriteButton(name, x, y, size, size, file, { .normal = 0, .pressed = 2, .hovered = 1 }, command, tooltip);
}

// An invisible button: its tooltip on hover, `command` on a click (none when empty).
std::string HitButton(const std::string& name, int x, int y, int width, int height, const std::string& tooltip, const std::string& command) { return Gui::QuietButton(name, x, y, width, height, command, tooltip); }

// A button on sprite `file`, its text on it (translated, centred).
std::string TextButton(const std::string& name, int x, int y, int width, int height, const std::string& file, int normal, int hovered, int pressed, const std::string& text, const char* color,
                       const std::string& tooltip, const std::string& command) {
	return Gui::SpriteButton(name, x, y, width, height, file, { .normal = normal, .pressed = pressed, .hovered = hovered }, command, tooltip, { .text = text.empty() ? std::string() : Tr(text), .color = color ? color : "" });
}

// A tooltip's text, translated, in lines of about `width` characters.
std::string Wrap(const std::string& english, std::size_t width = 46) { return Gui::WrapLines(Tr(english), width); }

// An empty slot, a search veil, a perfect glow: one sprite per tile size.
std::string SlotSprite(int width, int height) { return Kit::Slot(std::clamp(width, 1, 4) * Cell, std::clamp(height, 1, 4) * Cell); }
std::string VeilSprite(int width, int height) { return Kit::Veil(std::clamp(width, 1, 4) * Cell, std::clamp(height, 1, 4) * Cell); }
std::string GlowSprite(int width, int height) { return Kit::Glow(std::clamp(width, 1, 4) * Cell, std::clamp(height, 1, 4) * Cell, GlowPad, GlowFrames); }
// The grid's cover: the stash's stone under the rows.
std::string CoverSprite() { return Kit::Stone(1571, ViewRows * Cell + 2, BackgroundColor, 12, 3); }

// The see-through look of ethereal items, for the ethereal menus' icons.
const std::string EthLook = Gui::ItemArt::EthLook;

// Item art (or a plugin sprite) fitted in a square box at x, y, centered.
std::string Icon(const std::string& name, int x, int y, int box, const std::string& art, int width, int height, const std::string& extra = "", int artSize = 0) {
	const int    unit  = artSize > 0 ? artSize : Cell; // the art's pixels per width/height unit
	const double scale = static_cast<double>(box) / (std::max(width, height) * unit);
	return Gui::ScaledImage(name, x + static_cast<int>((box - width * unit * scale) / 2), y + static_cast<int>((box - height * unit * scale) / 2), scale, Gui::Path(art), extra);
}

// A square button (the menu buttons' plate) with one of the icons on it.
std::string IconButton(const std::string& name, int x, int y, const char* icon, const std::string& tooltip, const std::string& command, int size = DropHeight) {
	const int box = size * 7 / 10;
	return SquareButton(name, x, y, Kit::SlotButton(size, size), tooltip, command, size)
	     + Icon(name + "Icon", x + (size - box) / 2, y + (size - box) / 2, box, std::string("d2rloader\\grail-on\\icon-") + icon, 1, 1, "", IconSize);
}

// Whether a menu has a button: Misc only while one of its categories has tiles.
bool MenuShown(int menu) {
	if (menu == ObjMenu) return Config.objectives && std::any_of(Objectives.begin(), Objectives.end(), [](const Objective& objective) { return objective.enabled; });
	if (menu == LostMenu) return !LostOf.empty();
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
	case LostMenu: return "d2rloader\\grail-on\\icon-lost";
	default: return "d2rloader\\grail-on\\icon-config";
	}
}

// Moves every widget right by MenuRoom: the view is laid out from the window's
// corner, the panel starts MenuRoom left of it.
std::string ShiftLayout(const std::string& layout) { return Gui::ShiftX(layout, MenuRoom); }

using Gui::ItemArt::QualityBackgrounds;
using Gui::ItemArt::SocketSpots;
constexpr double RuneScale = 0.8; // a rune stone's size in its socket

// Lock held. A perfect tile's glow around its slot (`left`, `top`: the tile's
// corner): the game plays its frames itself (D2UI AnimatedImageWidget: fps,
// loop, playing), so nothing has to find and switch them. Without
// animations, or in a scroll's frames, its first frame alone.
std::string StillGlow(const Tile& tile, const std::string& n, int left, int top, const std::string& extra = "");
std::string GlowImage(const Tile& tile, const std::string& n, int left, int top) {
	if (!Config.frameAnimations) return StillGlow(tile, n, left, top);
	return Gui::AnimatedImage(n, left + (tile.width == 1 ? Cell / 2 : 0) - GlowPad, top - GlowPad, GlowSprite(tile.width, tile.height), 8, ", \"transparency\": " + std::to_string(GlowOpacity));
}
std::string StillGlow(const Tile& tile, const std::string& n, int left, int top, const std::string& extra) {
	return Gui::Image(n, left + (tile.width == 1 ? Cell / 2 : 0) - GlowPad, top - GlowPad, GlowSprite(tile.width, tile.height), extra + ", \"transparency\": " + std::to_string(GlowOpacity));
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
		if (QualityBackgrounds()) out += Gui::ItemArt::QualityBackground(n + "Back", x, y, bare ? "normal" : "runeword", box.width, box.height, bare ? ", \"transparency\": 0.65" : ", \"transparency\": 0.55");
		// The game's sockets are about 62% of a cell, their runes 55%. The
		// sockets are under the item, at about 60% (the game's grid in a
		// screenshot), the runes over it.
		const auto   spots = SocketSpots(box.width, box.height, static_cast<int>(entry.runes.size()));
		const double room  = Cell * std::min(1.0, spots.second) * 0.62;
		for (std::size_t k = 0; k < spots.first.size(); ++k) {
			const auto [cx, cy] = spots.first[k];
			out += Gui::ItemArt::Socket(n + "Socket" + std::to_string(k), x + cx * Cell, y + cy * Cell, room, GemSocketSize.load());
		}
		out += Image(n, x, y, base->second.asset);
		for (std::size_t k = 0; k < spots.first.size() && !bare; ++k) {
			const auto rune = Bases.find(entry.runes[k]);
			if (rune == Bases.end() || rune->second.asset.empty()) continue;
			const auto [cx, cy] = spots.first[k];
			const double size = room * 0.55 / 0.62;
			const int    rx = x + static_cast<int>(cx * Cell - size / 2), ry = y + static_cast<int>(cy * Cell - size / 2);
			out += Gui::ScaledImage(n + "Rune" + std::to_string(k), rx, ry, size / Cell, Gui::Path(rune->second.asset));
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
		if (GenericRunes && hidden) art = Gui::SpriteName("grune-blank");
		else if (index >= 0 && index < static_cast<int>(std::size(Gui::ItemArt::RuneStoneCell)) && Gui::ItemArt::RuneStoneCell[index] < GenericRunes) art = Gui::SpriteName("grune" + std::to_string(Gui::ItemArt::RuneStoneCell[index]));
		else if (const auto rune = Bases.find(code); rune != Bases.end()) art = Gui::Path(rune->second.asset);
		if (art.empty()) continue;
		const auto [cx, cy] = spots.first[k];
		out += Gui::ScaledImage(n + "_" + std::to_string(k), left + static_cast<int>(cx * Cell - size / 2), top + static_cast<int>(cy * Cell - size / 2), size / Cell, art, extra);
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
	return Gui::Image(name, left, top, VeilSprite(tile.width, tile.height));
}
// Lock held. How many grail tiles a list holds.
int ListCount(const ExclList& list) {
	int count = 0;
	for (int u = 0; u < UniqueCount(); ++u)
		if (Excludable(Uniques[u]))
			for (const bool eth : { false, true }) count += BaseHasTile(u, eth) && ListHas(list, ExcludeKey(u, eth));
	return count;
}
// ---- the Loot page's exclusions list ----
// An accordion: the grail's menus that hold uniques and set items, their
// categories, their tiles (an ethereal menu's are the ethereal tiles).
struct ExclRow {
	int         level {}, kind {}; // kind: 0 menu, 1 category or set, 2 item
	std::string key, name;         // key: "m<menu>", "c<menu>.<category>", "s<menu>.<category>.<set>", "<entry>p" or "<entry>e"
	int         excluded {}, total {};
	bool        open {};
	bool        locked {}; // an item that cannot be ticked
};
constexpr int ExclMenus[] { WeaponMenu, ArmorMenu, EthWeaponMenu, EthArmorMenu, SetMenu };

std::string AsciiLower(std::string text) {
	for (auto& c : text)
		if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
	return text;
}

// The menus the editor lists: the exclusions', or the variant lists' (no ethereal menus: one line per item).
std::vector<int> EditorMenus() {
	if (VarEditing) return { WeaponMenu, ArmorMenu, SetMenu, MiscMenu };
	return std::vector<int>(std::begin(ExclMenus), std::end(ExclMenus));
}
// Lock held. Whether a base entry is in the variant list `mode` (1 affix, 2 graphic) can hold.
bool VariantCandidate(int u, int mode) {
	const auto& entry = Uniques[u];
	if (entry.variant || entry.objective || entry.secret || entry.lost) return false;
	return mode == 1 ? !entry.affixes.empty() || entry.tooMany : mode == 2 && entry.pictures > 1;
}
// Lock held. Listed but not choosable: too many combinations for variants.
bool VariantLocked(int u, int mode) { return mode == 1 && Uniques[u].affixes.empty() && Uniques[u].tooMany; }
bool RowLocked(int u) { return VarEditing && VariantLocked(u, VarEditing); }

// Lock held. The tiles of menu `m` (category `c`, or all of the menu's when
// c < 0) whose name has `query` (lower case; empty for all).
std::vector<std::pair<int, bool>> ExclTiles(int m, int c, const std::string& query) {
	std::vector<std::pair<int, bool>> out;
	const bool                        eth = IsEthMenu(m);
	for (int u = 0; u < UniqueCount(); ++u) {
		const auto& entry = Uniques[u];
		if (VarEditing ? !VariantCandidate(u, VarEditing) || (!BaseHasTile(u, false) && !BaseHasTile(u, true)) : !Excludable(entry)) continue;
		if (entry.category < 0 || entry.category >= CategoryCount() || (!VarEditing && !BaseHasTile(u, eth))) continue;
		if (c >= 0 ? entry.category != c : Categories[entry.category].menu != BaseMenu(m)) continue;
		// The search: the item's name or its base's (shako finds Harlequin Crest).
		if (!query.empty() && AsciiLower(entry.name).find(query) == std::string::npos && AsciiLower(entry.base).find(query) == std::string::npos) continue;
		out.push_back({ u, eth });
	}
	return out;
}

// Lock held. The list's rows as shown: open groups' children under them; a
// search shows every group that has a match, open, with only the matches.
std::vector<ExclRow> ExclRows(const std::string& typed) {
	const std::string    query = AsciiLower(typed);
	std::vector<ExclRow> rows;
	const auto           counted = [](ExclRow& row, const std::vector<std::pair<int, bool>>& tiles) {
        for (const auto& [u, eth] : tiles)
            if (!RowLocked(u)) ++row.total, row.excluded += Drafted(u, eth);
	};
	for (const int m : EditorMenus()) {
		const auto all = ExclTiles(m, -1, query);
		if (all.empty()) continue;
		ExclRow menu { .level = 0, .kind = 0, .key = "m" + std::to_string(m), .name = Tr(Menus[m]) };
		counted(menu, all);
		menu.open = !query.empty() || ExclOpen.count(menu.key);
		rows.push_back(menu);
		if (!menu.open) continue;
		for (int c = 0; c < CategoryCount(); ++c) {
			if (Categories[c].menu != BaseMenu(m)) continue;
			const auto tiles = ExclTiles(m, c, query);
			if (tiles.empty()) continue;
			ExclRow category { .level = 1, .kind = 1, .key = "c" + std::to_string(m) + "." + std::to_string(c), .name = Tr(Categories[c].name) };
			counted(category, tiles);
			category.open = !query.empty() || ExclOpen.count(category.key);
			rows.push_back(category);
			if (!category.open) continue;
			const auto item = [&](int u, bool eth, int level) {
				// A variant list: the item's number of variants after its name.
				std::string name = Uniques[u].name;
				const bool  locked = RowLocked(u);
				if (VarEditing) name += " (" + (locked ? std::to_string(Uniques[u].tooMany) : std::to_string(VarEditing == 1 ? static_cast<int>(Uniques[u].affixes.size()) : Uniques[u].pictures)) + ")";
				rows.push_back({ .level = level, .kind = 2, .key = std::to_string(u) + (eth ? "e" : "p"), .name = name, .excluded = !locked && Drafted(u, eth) ? 1 : 0, .total = 1, .locked = locked });
			};
			if (m != SetMenu) {
				for (const auto& [u, eth] : tiles) item(u, eth, 2);
				continue;
			}
			// Set items: under their set, the sets in the grail's order.
			std::vector<int> sets;
			for (const auto& tile : tiles)
				if (std::find(sets.begin(), sets.end(), Uniques[tile.first].setId) == sets.end()) sets.push_back(Uniques[tile.first].setId);
			for (const int set : sets) {
				std::vector<std::pair<int, bool>> members;
				for (const auto& tile : tiles)
					if (Uniques[tile.first].setId == set) members.push_back(tile);
				const auto named = SetNames.find(set);
				ExclRow    group { .level = 2, .kind = 1, .key = "s" + std::to_string(m) + "." + std::to_string(c) + "." + std::to_string(set),
				                   .name = named != SetNames.end() && !named->second.empty() ? named->second : Tr("Set") };
				counted(group, members);
				group.open = !query.empty() || ExclOpen.count(group.key);
				rows.push_back(group);
				if (group.open)
					for (const auto& [u, eth] : members) item(u, eth, 3);
			}
		}
	}
	return rows;
}

// Lock held. The tiles a row stands for (the search's matches only).
std::vector<std::pair<int, bool>> ExclRowTiles(const std::string& key, const std::string& typed) {
	const std::string query = AsciiLower(typed);
	if (key.empty()) return {};
	if (key[0] == 'm') return ExclTiles(std::atoi(key.c_str() + 1), -1, query);
	if (key[0] == 'c' || key[0] == 's') {
		const auto dot = key.find('.');
		if (dot == std::string::npos) return {};
		auto tiles = ExclTiles(std::atoi(key.c_str() + 1), std::atoi(key.c_str() + dot + 1), query);
		if (key[0] == 's') { // a set: its items only
			const auto second = key.find('.', dot + 1);
			const int  set    = second == std::string::npos ? -2 : std::atoi(key.c_str() + second + 1);
			std::erase_if(tiles, [&](const auto& tile) { return Uniques[tile.first].setId != set; });
		}
		return tiles;
	}
	const int u = std::atoi(key.c_str());
	if (u < 0 || u >= UniqueCount() || (VarEditing ? !VariantCandidate(u, VarEditing) : !Excludable(Uniques[u]))) return {};
	return { { u, key.back() == 'e' } };
}

std::string FakeTile(const Tile& tile, int top, const std::string& n, bool label, bool frames = false) {
	const auto& entry   = Uniques[tile.unique];
	const bool  stored  = Stored.count({ tile.unique, tile.eth }) != 0;
	const bool  present = Present.count(tile.unique) != 0;
	const bool  named   = NameShown(tile.unique), drawn = ArtShown(tile.unique);
	const int   left    = tile.x * Cell - MenuRoom, itemLeft = left + (tile.width == 1 ? Cell / 2 : 0);
	const auto  size    = std::to_string(tile.width) + "x" + std::to_string(tile.height);
	std::string out     = Gui::Image("FSlot" + n, itemLeft, top, SlotSprite(tile.width, tile.height));
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
	return Gui::ClipBox(name, GridLeft, ViewTop, PageSize * Cell, ViewRows * Cell, children, visible ? R"(, "clipMouseInput": true)" : R"(, "clipMouseInput": true, "visible": false)");
}

// The window's frame: Expanded Stash Plus's stash (the plugin's own picture),
// the stone cover over its grid from `coverY`, the game's close button.
std::string WindowFrame(int coverY) {
	return Gui::Image("Background", 0, 0, Gui::SpriteName("panel")) + Gui::Image("Cover", 57, coverY, CoverSprite()) + Gui::CloseButton("CloseButton", 1600, 9, "close");
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
				out += Gui::Image(prefix + "Line" + std::to_string(r), HeaderLeft - GridLeft - MenuRoom, y + Cell - 14, Kit::Divider(HeaderWidth));
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
	layout += Gui::ClipBox("CoverLeft", coverLeft, coverTop, buttonLeft - coverLeft, coverHeight, Gui::Image("Cover", -MenuRoom, 0, CoverSprite()));
	layout += Gui::ClipBox("CoverRight", buttonLeft, buttonBottom, coverLeft + coverWidth - buttonLeft, coverTop + coverHeight - buttonBottom,
	                       Gui::Image("Cover2", -MenuRoom - (buttonLeft - coverLeft), coverTop - buttonBottom, CoverSprite()));
	// The scroll bar's track hides the view's thumb; each frame has its own.
	const bool bar   = MaxScroll > 0;
	const int  steps = ThumbSteps();
	if (bar) layout += GridBar.TrackLayout(BarTop, BarHeight);
	for (int k = 1; k < ScrollFrames; ++k) {
		const std::string name = "Frame" + std::to_string(k);
		// A frame: a plain container (from the panel's corner: ShiftLayout moves
		// it and its children, so it starts MenuRoom left) with the rows and the thumb.
		std::string children = ClipView(name + "Rows", FakeRows(first, down ? -height * k / ScrollFrames : -height + height * k / ScrollFrames, name), true);
		if (bar) children += GridBar.Thumb(name + "Thumb", BarTop, ScrollSteps, steps, Scroll * ScrollFrames + (down ? k : -k), MaxScroll * ScrollFrames);
		layout += Gui::Group(name, -MenuRoom, 0, children);
	}
	return Gui::Panel("grail-on/Scroll", WindowX() - MenuRoom, PanelY, PanelSpan(), PanelHeight, ShiftLayout(layout), OverShown ? OverPriority + 2 : 12, Gui::LeftAnchor);
}

// A text box showing `text` as it is (not a string key).
// `color`: a profile colour ("$FontColorWhite") or a colour object ({"r": ...}).
std::string TextLabel(const std::string& name, int x, int y, int width, int height, const std::string& text, const char* color, int size, const char* align = "left") {
	return Gui::TextBox(name, x, y, width, height, Tr(text), { .color = color, .size = size, .h = align, .v = "center" });
}

// Text as it is, wrapped in its box, from the top.
std::string TextBlock(const std::string& name, int x, int y, int width, int height, const std::string& text, const char* color, int size) {
	return Gui::TextBox(name, x, y, width, height, Tr(text), { .color = color, .size = size, .h = "left", .v = "top", .wrap = true });
}

// The grail page when something it needs is switched off: the frame, a title
// and what is wrong. Nothing on the characters was changed.
std::string ErrorLayout() {
	std::string layout = Gui::ClickCatcher("PanelClickCatcher", 0, 0, PanelWidth, PanelHeight) + WindowFrame(ViewTop - 2);
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
	return Gui::Panel("grail-on/Error", WindowX(), PanelY, PanelWidth, PanelHeight, layout, 10, Gui::LeftAnchor);
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
using Rgb = Gui::Rgb;
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
			if (Uniques[u].secret || Uniques[u].lost || !HasTile(u, eth) || !pick(u, eth)) continue;
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
	return Gui::Image(name, x + pad, y + pad, ChartSprite(ChartParts(count), size - 2 * pad, hole, square));
}
// The Lost items buttons' ring: all red.
constexpr Rgb LostColor { 200, 30, 30 };
std::string LostRing(const std::string& name, int x, int y, int size) { return Gui::Image(name, x + RingPad, y + RingPad, ChartSprite({ { 1, LostColor } }, size - 2 * RingPad, ButtonHole, true)); }

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
	std::string out = Gui::Image(name + "Ring", x, y, ChartSprite(ChartParts(count), size, 58));
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
	std::string out = SquareButton(name, x, y, Kit::SlotButton(StatTile, StatTile), CountText(label, count), command, StatTile);
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
	InTileOrder([&](int u) {
		const auto& entry = Uniques[u];
		if (entry.secret) return;
		// Only what the grail shows: an unfound item's base name while names are hidden.
		const std::string& name = NameShown(u) ? entry.name : entry.base;
		if (Lower(StripColors(name)).find(query) == std::wstring::npos) return;
		for (const bool eth : { false, true })
			if (HasTile(u, eth)) SearchMatches.push_back({ u, eth }), SearchHits.insert({ u, eth });
	});
}

// Lock held. Shows match `SearchAt` next.
void JumpToMatch() {
	if (SearchMatches.empty()) return;
	SearchAt                  = (SearchAt % static_cast<int>(SearchMatches.size()) + static_cast<int>(SearchMatches.size())) % static_cast<int>(SearchMatches.size());
	std::tie(JumpTo, JumpEth) = SearchMatches[SearchAt];
}

std::string SearchBox(int width) { return Kit::InputBox(width, DropHeight); } // its sprite, made once per length

// Lock held. The bar: its box, the text (a caret while typing), the count,
// previous and next, and a clear button while something is typed.
std::string SearchBar() {
	// Fixed on the screen: the panel starts further left in compact mode.
	const int         x = Config.searchX - (WindowX() - MenuRoom), y = Config.searchY - PanelY, width = Config.searchWidth;
	const std::string typed  = TypedText(TypeSearch);
	const bool        typing = ObjTyping && TypeTarget == TypeSearch;
	const bool        empty  = typed.empty() && !typing;
	std::string       out    = Gui::Image("SearchBack", x, y, SearchBox(width));
	out += Gui::TextBox("SearchText", x + 24, y, width - 48, DropHeight, empty ? Tr("Search") : typed + (typing ? "|" : ""), { .color = empty ? "$FontColorGray" : "$FontColorWhite" });
	out += HitButton("SearchHit", x, y, width, DropHeight, Wrap("Type a name: the grail shows the first item that has it. Enter or the arrows: the next one."), "searchtype");
	if (typed.empty()) return out;
	// The result line under the bar, as wide and high as it: the count, then
	// the arrows and the clear button at its right end.
	const int         line  = y + DropHeight + SearchGap;
	const std::string count = SearchMatches.empty() ? Tr("No match") : std::to_string(SearchAt + 1) + " / " + std::to_string(SearchMatches.size());
	out += Gui::Image("SearchLineBack", x, line, SearchBox(width));
	int at = x + width - 12 - DropHeight;
	out += TextButton("SearchClear", at, line, DropHeight, DropHeight, Kit::Remove(DropHeight), 0, 3, 1, "", "", Wrap("Clear the search"), "searchclear");
	if (SearchMatches.size() > 1) {
		at -= DropHeight + 10;
		out += TextButton("SearchNext", at, line, DropHeight, DropHeight, Kit::Arrow(false, DropHeight), 0, 1, 2, "", "", Wrap("Next"), "searchnext");
		at -= DropHeight + 10;
		out += TextButton("SearchPrev", at, line, DropHeight, DropHeight, Kit::Arrow(true, DropHeight), 0, 1, 2, "", "", Wrap("Previous"), "searchprev");
	}
	return out + TextLabel("SearchCount", x + 24, line, at - x - 34, DropHeight, count, SearchMatches.empty() ? "$FontColorGray" : "$FontColorWhite", 30, "left");
}

// Lock held. The layout of the view in panel `id`: the current category's
// tiles, the settings or the stats. Its texts are the strings registered with the lists.
std::string ViewLayout(const std::string& id, bool over = false) {
	const bool settings = CurrentCategory < 0; // no tiles: the settings or the stats
	// The window's top is where Expanded Stash Plus's $LeftPanelRect_ExpandedStash
	// puts it (x 236, y -856, at the top of the screen; it reaches 88 units lower with the title, still above the skill bar);
	// the panel starts MenuRoom further left, for the menus.
	const std::string catcher = Gui::ClickCatcher("PanelClickCatcher", 0, 0, PanelSpan(), PanelHeight);
	// The page's grid (3x5, its items only passing through) first, under the
	// window's background, so nothing of it shows: every tile is a picture.
	// The settings keep it off screen. The host's grid is in its own panel,
	// under this one.
	const std::string grid   = Gui::ItemGrid("grail-on/Grid", TilesView() ? GridLeft : -20000, GridTop, StageCols, StageRows, Gui::TileGrid::MarkerFields);
	std::string       layout = (Host::On() ? std::string() : grid) + WindowFrame(ViewTop - 2);
	const int menu = CurrentMenu();
	// The menus down the left, the stats first; Misc while it tracks something.
	for (int m = 0, slot = 0; m < static_cast<int>(std::size(Menus)); ++m) {
		if (!MenuShown(m)) continue;
		const int  x = MenuX(), y = MenuTop + slot++ * MenuStep;
		layout += SquareButton("Menu" + std::to_string(m), x, y, Kit::SlotButton(MenuSize, MenuSize, m == menu), "@grail-on:M" + std::to_string(m), "menu" + std::to_string(m), MenuSize);
		layout += m == LostMenu ? LostRing("MenuRing" + std::to_string(m), x, y, MenuSize) : ButtonRing("MenuRing" + std::to_string(m), x, y, MenuSize, CountMenu(m));
		// The ethereal menus' icons are see-through, like ethereal items.
		int width = 1, height = 1;
		if (const auto art = MenuArt(m, width, height); !art.empty())
			layout += Icon("MenuIcon" + std::to_string(m), x + (MenuSize - MenuIcon) / 2, y + (MenuSize - MenuIcon) / 2, MenuIcon, art, width, height, IsEthMenu(m) ? EthLook : "");
	}
	// The view's tiles go in one container ("Rest"), hidden while a scroll's
	// frames show. ShiftLayout moves its children too: it starts MenuRoom left.
	const std::string layoutBefore = layout;
	layout.clear();
	std::string tips; // the labels' tooltips: over LowerHit, which would hide them (a click on one is LowerHit's)
	// The set names above their sets: "Unknown set" until one of its items is
	// found, with spoilers off.
	for (std::size_t h = 0; h < ShownHeaders.size(); ++h) {
		const auto [set, row] = ShownHeaders[h];
		const int  top        = ViewTop + row * Cell;
		const auto name       = HeaderKey(set);
		layout += Label("SetName" + std::to_string(h), HeaderLeft, top, HeaderWidth, Cell - 14, name, HeaderColor(set), Config.font + 8, "center", "center");
		layout += Gui::Image("SetLine" + std::to_string(h), HeaderLeft, top + Cell - 14, Kit::Divider(HeaderWidth));
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
		layout += Gui::Image("Slot" + n, itemLeft, top, SlotSprite(tile.width, tile.height));
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
		// Its brackets (a variant's choice): the label's tooltip, the label stays short.
		if (named && !entry.detail.empty())
			tips += HitButton("LabelTip" + n, left - margin, top - LabelRows * Cell + Cell / 2, SlotWidth * Cell + margin + LabelMargin, LabelRows * Cell - Cell / 2, Wrap(entry.detail), "lower");
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
		if (named && !Uniques[tile.unique].detail.empty())
			tips += HitButton("PeekLabelTip" + std::to_string(i), left - LabelMargin, top - LabelRows * Cell + Cell / 2, SlotWidth * Cell + 2 * LabelMargin, LabelRows * Cell - Cell / 2, Wrap(Uniques[tile.unique].detail), "lower");
	}
	layout += veils;
	{
		layout = layoutBefore + Gui::Group("Rest", -MenuRoom, 0, layout);
	}
	if (TilesView()) {
		// Every tile is a picture: an item dropped anywhere on the view goes to
		// its tile (over the grid too, so the grid never takes a drop itself).
		layout += Gui::ClickArea("LowerHit", GridLeft, ViewTop, PageSize * Cell, ViewRows * Cell, "lower");
	}
	if (!tips.empty()) {
		layout += Gui::Group("Tips", -MenuRoom, 0, tips);
	}
	// The menu's categories down the left (over the grid's empty first column,
	// after the grid so their clicks reach them), the category's first base on them.
	for (int c = 0; c < CategoryCount() && !settings; ++c) {
		if (!InMenu(c, menu)) continue;
		const int y = ButtonTop(c);
		layout += SquareButton("Cat" + std::to_string(c), CatX(), y, Kit::SlotButton(CatSize, CatSize, c == CurrentCategory), "@grail-on:C" + std::to_string(c),
		                       "cat" + std::to_string(c), CatSize);
		layout += Categories[c].menu == LostMenu ? LostRing("CatRing" + std::to_string(c), CatX(), y, CatSize) : ButtonRing("CatRing" + std::to_string(c), CatX(), y, CatSize, CountCategory(c, ViewEth));
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
		layout += Gui::SpriteButton("PeekHit" + std::to_string(i), left, top, width * Cell, halves * Cell / 2, Gui::Kit::Clear(), { .normal = 0, .pressed = 1, .hovered = 1 }, "peek" + std::to_string(i), {}, {}, {}, nullptr);
	}
	if (CurrentCategory == StatsView) {
		// The stats pages down the left, like a menu's categories.
		for (int p = 0, shown = 0; p <= KindCountAll; ++p) {
			if (!StatsPageShown(p)) continue; // misc: while something is tracked; objectives: while there are some
			const int y = ViewTop + shown++ * (Cell + 20) + (Cell - CatSize) / 2;
			layout += SquareButton("StatPage" + std::to_string(p), CatX(), y, Kit::SlotButton(CatSize, CatSize, p == StatsPage), "@grail-on:SP" + std::to_string(p), "stats" + std::to_string(p), CatSize);
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
				layout += Gui::Image(std::string("Line") + text, HeaderLeft, y, Kit::Section(HeaderWidth));
				y += LineHeight + 10;
			}
			layout += TextLabel(std::string("Head") + text, HeaderLeft, y, HeaderWidth, 56, text, "$FontColorGoldYellow", Config.font + 6, "center");
			y += 64;
		};
		const auto setting = [&](const std::string& id, int sx, const std::string& name, bool on, const std::string& help, const std::string& command, int width = TableWidth / 2) {
			layout += SquareButton("Set" + id, sx, y, Kit::Tick(on, ButtonSize), help.empty() ? std::string() : Wrap(help), command);
			layout += TextLabel("SetName" + id, sx + ButtonSize + 20, y, width - ButtonSize - 40, ButtonSize, name, "$FontColorWhite", 30);
			layout += HitButton("SetHit" + id, sx + ButtonSize + 20, y, width - ButtonSize - 40, ButtonSize, help.empty() ? std::string() : Wrap(help), command);
		};
		// A drop-down: its name on the left, the box from the middle on; its
		// list (drawn last, over the rest) while open. `nameX`..`boxX`: the
		// name's room; a narrower box for the misc types.
		struct List {
			int                      x {}, y {}, width = DropWidth;
			std::vector<std::string> names, commands;
			int                      current {};
		} list;
		const auto dropdownAt = [&](int id, int nameX, int boxX, int boxWidth, const std::string& name, const std::vector<std::string>& names, int current, const std::string& help,
		                            const std::string& prefix, bool raw) {
			const auto  tip    = help.empty() ? std::string() : Wrap(help);
			layout += TextLabel("DropName" + std::to_string(id), nameX, y, boxX - nameX - 20, DropHeight, raw ? name : Tr(name), "$FontColorWhite", 30);
			layout += HitButton("DropHit" + std::to_string(id), nameX, y, boxX - nameX - 20, DropHeight, tip, "");
			layout += TextButton("Drop" + std::to_string(id), boxX, y, boxWidth, DropHeight, Kit::DropBox(boxWidth, DropHeight), 0, 3, 1, names[current], "$FontColorLightGold", tip,
			                     "drop" + std::to_string(id));
			if (OpenDrop == id) {
				list.x = boxX, list.y = y + DropHeight, list.current = current, list.width = boxWidth;
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
				layout += SquareButton("SetPage" + std::to_string(p), CatX(), top, Kit::SlotButton(CatSize, CatSize, p == SettingsPage), "@grail-on:SG" + std::to_string(p), "spage" + std::to_string(p), CatSize);
				layout += Icon("SetPageIcon" + std::to_string(p), CatX() + (CatSize - CatIcon) / 2, top + (CatSize - CatIcon) / 2, CatIcon, std::string("d2rloader\\grail-on\\icon-") + icons[p], 1, 1);
			}
		}
		if (SettingsPage == 0 && !VarEditing) {
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
				layout += Gui::Image("SpoilRow" + row, left, y, Kit::TableRow(TableWidth, TableRow));
				for (int c = 0; c < 4; ++c) layout += Gui::Image("SpoilLine" + row + std::to_string(c), left + TableLabel + c * TableColumn - 2, y + 7, Kit::TableColumn(TableRow - 14));
				layout += TextLabel("SpoilName" + row, left + 28, y, TableLabel - 40, TableRow, r ? "Show art" : "Show names", "$FontColorWhite", 30);
				for (int c = 0; c < 4; ++c) {
					const char* key = r ? art[c] : names[c];
					if (!key) continue;
					for (const auto& toggle : Toggles)
						if (!std::strcmp(toggle.key, key))
							layout += SquareButton("Set" + std::string(key), left + TableLabel + c * TableColumn + (TableColumn - ButtonSize) / 2, y + (TableRow - ButtonSize) / 2, Kit::Tick(Config.*toggle.field, ButtonSize),
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
		// Variants: a switch each, how many items it splits and an edit
		// button opening the list of items it can split.
		header("Variants");
		for (const auto& toggle : Toggles)
			if (toggle.group == VariantGroup) {
				const int mode  = !std::strcmp(toggle.key, "affix_variants") ? 1 : 2;
				int       total = 0, in = 0;
				const auto& off = mode == 1 ? AffixOff : GraphicOff;
				for (int u = 0; u < UniqueCount(); ++u)
					if (VariantCandidate(u, mode) && !VariantLocked(u, mode) && (BaseHasTile(u, false) || BaseHasTile(u, true))) ++total, in += !off.count(Uniques[u].id);
				setting(toggle.key, left, toggle.name, Config.*toggle.field, toggle.help, std::string("toggle:") + toggle.key);
				layout += TextLabel(std::string("VarCount") + toggle.key, x + TableWidth / 2, y, TableWidth / 2 - DropHeight - 20, ButtonSize,
				                    std::to_string(in) + " / " + std::to_string(total) + " " + Tr("items"), "$FontColorGray", 26, "right");
				layout += IconButton(std::string("VarEdit") + toggle.key, x + TableWidth - DropHeight, y + (ButtonSize - DropHeight) / 2, "edit",
				                     Wrap(mode == 1 ? "Edit: the items whose choice rolls get a tile each." : "Edit: the items whose pictures get a tile each."), "varedit" + std::to_string(mode));
				y += line;
			}
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
			const int         resetWidth = Kit::Refresh(DropHeight).empty() ? ResetBox : DropHeight;
			const int         controls   = dropX + DropWidth - 2 * AxisWidth - resetWidth;
			layout += TextLabel("ButtonName", x, y, controls - x - 20, DropHeight, "Grail button", "$FontColorWhite", 30);
			layout += HitButton("ButtonHit", x, y, controls - x - 20, DropHeight, help, "");
			int at = controls;
			for (const auto& [axis, value] : { std::pair { "X", Config.buttonX }, { "Y", Config.buttonY } }) {
				layout += TextLabel(std::string("ButtonAxis") + axis, at, y, 30, DropHeight, axis, "$FontColorGoldYellow", 30, "center");
				at += 30;
				layout += TextButton(std::string("ButtonDown") + axis, at + (SmallBox - DropHeight) / 2, y, DropHeight, DropHeight, Kit::Arrow(false, DropHeight), 0, 1, 2, "", "", help, std::string("btn") + axis + "-");
				at += SmallBox;
				layout += TextLabel(std::string("ButtonValue") + axis, at, y, 100, DropHeight, std::to_string(value), "$FontColorWhite", 30, "center");
				at += 100;
				layout += TextButton(std::string("ButtonUp") + axis, at + (SmallBox - DropHeight) / 2, y, DropHeight, DropHeight, Kit::Arrow(true, DropHeight), 0, 1, 2, "", "", help, std::string("btn") + axis + "+");
				at += SmallBox + 20;
			}
			if (Kit::Refresh(DropHeight).empty()) layout += TextButton("ButtonReset", at, y, ResetBox, DropHeight, Kit::PlainButton(ResetBox, DropHeight), 0, 3, 1, "Reset", "$FontColorLightGold", resetHelp, "btnreset");
			else layout += TextButton("ButtonReset", at, y, DropHeight, DropHeight, Kit::Refresh(DropHeight), Kit::RefreshFrames().normal, Kit::RefreshFrames().hovered, Kit::RefreshFrames().pressed, "", "", resetHelp, "btnreset");
			y += line;
			// The search bar: its place (X, Y), then its length, and a reset.
			const std::string searchHelp = Wrap("Moves the search bar, from the window's top left. Click: 1, Shift+click: 10, Ctrl+click: 100.");
			const std::string lengthHelp = Wrap("The search bar's length. Click: 1, Shift+click: 10, Ctrl+click: 100.");
			const std::string searchReset = Wrap("Puts the search bar back under the window, at its first length.");
			const auto stepper = [&](const std::string& id, int sx, const char* axis, int value, const std::string& tip, const std::string& command) {
				if (axis[0]) layout += TextLabel("SAxis" + id, sx, y, 30, DropHeight, axis, "$FontColorGoldYellow", 30, "center");
				sx += 30;
				// The loot filter's arrows: down lowers, up raises.
				layout += TextButton("SDown" + id, sx + (SmallBox - DropHeight) / 2, y, DropHeight, DropHeight, Kit::Arrow(false, DropHeight), 0, 1, 2, "", "", tip, command + "-");
				layout += TextLabel("SValue" + id, sx + SmallBox, y, 100, DropHeight, std::to_string(value), "$FontColorWhite", 30, "center");
				layout += TextButton("SUp" + id, sx + SmallBox + 100 + (SmallBox - DropHeight) / 2, y, DropHeight, DropHeight, Kit::Arrow(true, DropHeight), 0, 1, 2, "", "", tip, command + "+");
			};
			layout += TextLabel("SearchName", x, y, controls - x - 20, DropHeight, "Search bar", "$FontColorWhite", 30);
			layout += HitButton("SearchNameHit", x, y, controls - x - 20, DropHeight, searchHelp, "");
			stepper("X", controls, "X", Config.searchX, searchHelp, "srchX");
			stepper("Y", controls + AxisWidth, "Y", Config.searchY, searchHelp, "srchY");
			if (Kit::Refresh(DropHeight).empty()) layout += TextButton("SearchReset", controls + 2 * AxisWidth, y, ResetBox, DropHeight, Kit::PlainButton(ResetBox, DropHeight), 0, 3, 1, "Reset", "$FontColorLightGold", searchReset, "srchreset");
			else layout += TextButton("SearchReset", controls + 2 * AxisWidth, y, DropHeight, DropHeight, Kit::Refresh(DropHeight), Kit::RefreshFrames().normal, Kit::RefreshFrames().hovered, Kit::RefreshFrames().pressed, "", "", searchReset, "srchreset");
			y += line;
			layout += TextLabel("SearchLength", x, y, controls - x - 20, DropHeight, "Search bar length", "$FontColorWhite", 30);
			layout += HitButton("SearchLengthHit", x, y, controls - x - 20, DropHeight, lengthHelp, "");
			stepper("W", controls + AxisWidth, "", Config.searchWidth, lengthHelp, "srchW");
			y += line;
		}
		}
		if (SettingsPage == 2 || VarEditing) {
		const bool editing = VarEditing || (ExclEditing >= 0 && ExclEditing < static_cast<int>(ExclLists.size()));
		if (!editing) {
		ExclMax = 0;
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
		header("RP Breaking Settings");
		for (const auto& toggle : Toggles)
			if (toggle.group == RpGroup) {
				setting(toggle.key, left, toggle.name, Config.*toggle.field, toggle.help, std::string("toggle:") + toggle.key);
				y += line;
			}
		// The exclusion lists: a line each with its active box, its name (typed
		// in place while renaming), how many items it holds, then move up and
		// down, Rename, Edit (the list's items in the editor) and Delete (a
		// second click deletes); under them, Add (empty or from a preset).
		header("Excluded items");
		{
			const int  count    = static_cast<int>(ExclLists.size());
			const bool renaming = ObjTyping && TypeTarget == TypeListName;
			const auto onHelp = Wrap("Active: the loot filter never hides its items."), editHelp = Wrap("Edit: choose the list's items.");
			const auto renameHelp = Wrap("Rename: type a new name (Enter or a click elsewhere ends)."), deleteHelp = Wrap("Deletes this list (a second click confirms).");
			for (int i = 0; i < count; ++i) {
				const auto& list = ExclLists[i];
				const auto  n    = std::to_string(i);
				layout += SquareButton("ExclOn" + n, x, y, Kit::Tick(list.active, ButtonSize), onHelp, "exlon" + n);
				// The buttons from the right: Delete, Edit, Rename, down, up.
				int        at   = x + TableWidth;
				const bool armed = ExclDeleteArmed == i;
				at -= ResetBox; // Delete's room: its button, or the confirmation as wide as Edit
				if (armed) layout += TextButton("ExclDel" + n, at, y, ResetBox, DropHeight, Kit::PlainButton(ResetBox, DropHeight), 0, 3, 1, "Delete", "$FontColorRed", Wrap("Click again to delete this list."), "exldel" + n);
				else layout += TextButton("ExclDel" + n, at + ResetBox - DropHeight, y, DropHeight, DropHeight, Kit::Remove(DropHeight), 0, 3, 1, "", "", deleteHelp, "exldel" + n);
				at -= 12 + DropHeight;
				layout += IconButton("ExclEdit" + n, at, y, "edit", editHelp, "exledit" + n);
				at -= 12 + DropHeight;
				layout += IconButton("ExclRename" + n, at, y, "rename", renameHelp, "exlren" + n);
				at -= 12 + DropHeight;
				if (i + 1 < count) layout += TextButton("ExclDown" + n, at, y, DropHeight, DropHeight, Kit::Arrow(false, DropHeight), 0, 1, 2, "", "", Wrap("Move down"), "exldown" + n);
				at -= 12 + DropHeight;
				if (i > 0) layout += TextButton("ExclUp" + n, at, y, DropHeight, DropHeight, Kit::Arrow(true, DropHeight), 0, 1, 2, "", "", Wrap("Move up"), "exlup" + n);
				const int nameX = x + ButtonSize + 20, countWidth = 170, nameWidth = at - 20 - countWidth - nameX;
				if (renaming && ExclRenaming == i) {
					layout += TextLabel("ExclName" + n, nameX, y, nameWidth, DropHeight, TypedText(TypeListName) + "|", "$FontColorWhite", 30);
					layout += HitButton("ExclNameHit" + n, nameX, y, nameWidth, DropHeight, "", "exlren" + n);
				} else {
					layout += TextLabel("ExclName" + n, nameX, y, nameWidth, DropHeight, list.name, list.active ? "$FontColorWhite" : "$FontColorGray", 30);
					layout += HitButton("ExclNameHit" + n, nameX, y, nameWidth, DropHeight, onHelp, "exlon" + n);
				}
				const int items = ListCount(list);
				layout += TextLabel("ExclCount" + n, nameX + nameWidth, y, countWidth, DropHeight, std::to_string(items) + (items == 1 ? " item" : " items"), "$FontColorGray", 26, "right");
				y += line;
			}
			if (count == 0) {
				layout += TextLabel("ExclNoList", x, y, TableWidth, DropHeight, "No list: add one", "$FontColorGray", 26);
				y += line;
			}
			layout += TextButton("ExclListAdd", x, y, AddWidth, DropHeight, Kit::Add(AddWidth, DropHeight), 0, 1, 2, "", "", Wrap("Add a list: empty, or with a preset's items."), "drop14");
			layout += TextLabel("ExclAddName", x + AddWidth + 20, y, 400, DropHeight, "Add a list", "$FontColorWhite", 30);
			layout += HitButton("ExclAddHit", x + AddWidth + 20, y, 400, DropHeight, "", "drop14");
			if (OpenDrop == 14) {
				list.x = x, list.y = y + DropHeight, list.current = -1, list.width = DropWidth;
				const char* choices[] { "Empty list", PresetNames[0], PresetNames[1] };
				for (int k = 0; k < 3; ++k) list.names.push_back(choices[k]), list.commands.push_back("exladd" + std::to_string(k));
			}
			y += line;
		}
		}
		// The editor: the list's name, Cancel and Save; a search box; the
		// accordion of the grail's menus, their categories (sets: their sets)
		// and their items, each with a check box (a group's ticks or clears
		// all of it); the window's scroll bar for it.
		if (editing) {
			const std::string title = Tr("Edit") + ": " + (VarEditing ? Tr(ExclDraft.name) : ExclDraft.name);
			header(title.c_str());
			layout += TextLabel("ExclEditHelp", x, y, TableWidth - 2 * (DropHeight + 12), DropHeight,
			                    VarEditing == 1   ? "Ticked items get a tile for each choice of their choice roll."
			                    : VarEditing == 2 ? "Ticked items get a tile for each of their pictures."
			                                      : "Ticked items are never hidden by the loot filter.",
			                    "$FontColorGray", 26);
			layout += IconButton("ExclCancel", x + TableWidth - 2 * DropHeight - 12, y, "cancel", Wrap("Cancel: leaves the list as it was."), "exlcancel");
			layout += IconButton("ExclSave", x + TableWidth - DropHeight, y, "save", Wrap("Save: keeps the changes."), "exlsave");
			y += line;
		}
		if (editing) {
			layout += Gui::Image("ExclInputBack", x, y, Kit::InputBox(ExclInputWidth, DropHeight));
			const std::string typed  = TypedText(TypeExclude);
			const bool        typing = ObjTyping && TypeTarget == TypeExclude;
			const bool        empty  = typed.empty() && !typing;
			layout += Gui::TextBox("ExclTyped", x + 24, y, ExclInputWidth - 48, DropHeight, empty ? Tr("Search items") : typed + (typing ? "|" : ""), { .color = empty ? "$FontColorGray" : "$FontColorWhite" });
			layout += HitButton("ExclTypeHit", x, y, ExclInputWidth - (empty ? 0 : DropHeight), DropHeight, "", "excltype");
			if (!empty) layout += IconButton("ExclTypeClear", x + ExclInputWidth - DropHeight, y, "cancel", Tr("Clear the search"), "excltypeclear");
			const bool builtin = ExclDraft.preset >= 0;
			layout += IconButton("ExclClear", x + TableWidth - DropHeight, y, builtin ? "reset" : "clear",
			                     Wrap(builtin      ? "Reset: the preset's items again, as the list came."
			                          : VarEditing ? "Clear: no item gets tiles of its own."
			                                       : "Clear: every item of the list is filtered again."),
			                     "excludeclear");
			y += line + 6;
			const auto rows    = ExclRows(typed);
			const int  rowStep = ExclRowHeight;
			const int  bottom  = ViewTop + ViewRows * Cell - 20;
			const int  steps   = std::clamp((bottom - y) / StepHeight, 2, ScrollSteps);
			const int  perPage = std::max(1, steps * StepHeight / rowStep);
			const int  count   = static_cast<int>(rows.size());
			ExclPerPage        = perPage;
			ExclMax            = std::max(0, count - perPage);
			ExclSteps          = steps;
			ExclFirst          = std::clamp(ExclFirst, 0, ExclMax);
			const int  top     = y;
			const int  arrowX  = x + TableWidth;
			const auto itemHelp = Wrap(VarEditing ? "Ticked: a tile for each of its variants." : "Ticked: the loot filter never hides it."), groupHelp = Wrap("Ticks every item in it, or clears them all when they are all ticked.");
			const auto lockedHelp = Wrap("Too many combinations for variants (more than 500): it keeps one tile.");
			if (rows.empty()) layout += TextLabel("ExclNone", x, y, TableWidth, ExclRowHeight, typed.empty() ? "No items" : "No item matches", "$FontColorGray", 26);
			for (int i = ExclFirst; i < count && i < ExclFirst + perPage; ++i) {
				const auto& row    = rows[i];
				const auto  n      = std::to_string(i - ExclFirst);
				y                  = top + (i - ExclFirst) * rowStep;
				const int   indent = x + row.level * ExclIndent;
				const int   boxX   = indent + ExclMark + 8;
				const int   nameX  = boxX + ExclMark + 14;
				const int   right  = arrowX - 20;
				const int   markY  = y + (rowStep - ExclMark) / 2;
				if (row.kind < 2) // a group: its arrow opens or closes it
					layout += TextButton("ExclOpen" + n, indent, markY, ExclMark, ExclMark, Kit::GroupArrow(row.open, ExclMark), 0, 1, 2, "", "", "", "exo" + row.key);
				const std::string box = Kit::Ticks(row.excluded == 0 ? 0 : row.excluded == row.total ? 1 : 2, ExclMark);
				// Too many combinations: listed, its box off and dimmed, nothing to click.
				if (row.locked) layout += TextButton("ExclBox" + n, boxX, markY, ExclMark, ExclMark, Kit::Ticks(3, ExclMark), 0, 0, 0, "", "", lockedHelp, "");
				else layout += TextButton("ExclBox" + n, boxX, markY, ExclMark, ExclMark, box, 0, 1, 2, "", "", row.kind < 2 ? groupHelp : itemHelp, (row.kind < 2 ? "exg" : "exi") + row.key);
				const char* color = row.locked ? R"({"r": 90, "g": 90, "b": 90, "a": 255})" : row.kind == 0 ? "$FontColorGoldYellow" : row.kind == 1 ? "$FontColorLightGold" : row.excluded ? "$FontColorWhite" : "$FontColorGray";
				layout += TextLabel("ExclName" + n, nameX, y, right - nameX - 160, rowStep, row.name, color, row.kind == 2 ? 26 : 28);
				if (row.kind < 2)
					layout += TextLabel("ExclCount" + n, right - 160, y, 160, rowStep, std::to_string(row.excluded) + " / " + std::to_string(row.total), "$FontColorGray", 24, "right");
				layout += HitButton("ExclHit" + n, nameX, y, right - nameX, rowStep, row.locked ? lockedHelp : "", row.locked ? "" : (row.kind < 2 ? "exo" : "exi") + row.key);
			}
			// The list's scroll bar: the view's, where the view's is, as tall as
			// the list (its track cut to that height, its bottom edge kept).
			if (ExclMax > 0) {
				const int thumb = std::clamp((steps * perPage + count / 2) / std::max(1, count), 1, steps - 1);
				layout += ExclBar.Steps(top, steps, thumb, ExclMax, ExclFirst, ExclMax);
			} else ExclBar.Set(top, steps, 0);
			ExclBar.Attach();
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
				layout += TextButton("MiscPrev", left, y, SmallBox, DropHeight, Kit::StepBox(SmallBox, DropHeight), 0, 3, 1, "<", "$FontColorLightGold", "", "miscpage-");
				layout += TextLabel("MiscPage", left + SmallBox, y, 160, DropHeight, pageText, "$FontColorWhite", 30, "center");
				layout += TextButton("MiscNext", left + SmallBox + 160, y, SmallBox, DropHeight, Kit::StepBox(SmallBox, DropHeight), 0, 3, 1, ">", "$FontColorLightGold", "", "miscpage+");
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
			layout += Gui::Image("ObjInputBack", x, y, Kit::InputBox(ObjInputWidth, DropHeight));
			{
				const std::string typed = ObjTypedText();
				const bool        empty = typed.empty() && !ObjTyping;
				layout += Gui::TextBox("ObjTyped", x + 24, y, ObjInputWidth - 48, DropHeight, empty ? Tr("A new objective") : typed + (ObjTyping ? "|" : ""), { .color = empty ? "$FontColorGray" : "$FontColorWhite" });
				layout += HitButton("ObjTypeHit", x, y, ObjInputWidth, DropHeight, "", "objtype");
			}
			layout += TextButton("ObjAdd", x + TableWidth - AddWidth, y, AddWidth, DropHeight, Kit::Add(AddWidth, DropHeight), 0, 1, 2, "", "", Tr("Add"), "objadd");
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
				layout += SquareButton("ObjOn" + n, x, y, Kit::Tick(objective.enabled, ButtonSize), onHelp, "objon" + n);
				const int iconX = x + ButtonSize + 16;
				layout += TextButton("ObjIcon" + n, iconX, y, DropHeight, DropHeight, Gui::SpriteName("objrow" + std::to_string(std::clamp(objective.icon, 0, ObjectiveIconCount - 1))), 0, 1, 1, "", "", iconHelp, "objpick" + n);
				const int textX = iconX + DropHeight + 20;
				layout += Gui::TextBox("ObjText" + n, textX, y, arrows - textX - 12, DropHeight, objective.text, { .color = objective.enabled ? "$FontColorWhite" : "$FontColorGray" });
				if (i > 0) layout += TextButton("ObjUp" + n, arrows, y, DropHeight, DropHeight, Kit::Arrow(true, DropHeight), 0, 1, 2, "", "", Wrap("Move up"), "objup" + n);
				if (i + 1 < count) layout += TextButton("ObjDown" + n, arrows + DropHeight + 12, y, DropHeight, DropHeight, Kit::Arrow(false, DropHeight), 0, 1, 2, "", "", Wrap("Move down"), "objdown" + n);
				layout += TextButton("ObjDel" + n, arrows + 2 * (DropHeight + 12), y, DropHeight, DropHeight, Kit::Remove(DropHeight), 0, 3, 1, "", "", removeHelp, "objdel" + n);
			}
			y = top + rows * line;
			if (count > rows) {
				const std::string pageText = std::to_string(ObjFirst / rows + 1) + " / " + std::to_string((count + rows - 1) / rows);
				layout += TextButton("ObjPrev", left, y, SmallBox, DropHeight, Kit::StepBox(SmallBox, DropHeight), 0, 3, 1, "<", "$FontColorLightGold", "", "objpage-");
				layout += TextLabel("ObjPage", left + SmallBox, y, 160, DropHeight, pageText, "$FontColorWhite", 30, "center");
				layout += TextButton("ObjNext", left + SmallBox + 160, y, SmallBox, DropHeight, Kit::StepBox(SmallBox, DropHeight), 0, 3, 1, ">", "$FontColorLightGold", "", "objpage+");
			}
			if (count == 0) layout += TextLabel("ObjNone", x, top, TableWidth, 56, "No objective yet.", "$FontColorGray", 30);
			// The icon picker, over everything: every icon, the current one
			// rimmed; a click on one picks it, anywhere else closes it.
			if (ObjPicking >= 0 && ObjPicking < count) {
				const int     boxW = PickWidth, boxH = PickHeight;
				const int     boxX = GridLeft + (PageSize * Cell - boxW) / 2, boxY = ViewTop + (ViewRows * Cell - boxH) / 2;
				layout += HitButton("PickCatch", GridLeft, ViewTop, PageSize * Cell, ViewRows * Cell, "", "objpickx");
				layout += Gui::Image("PickBack", boxX, boxY, Kit::Back(PickWidth, PickHeight));
				layout += TextLabel("PickTitle", boxX, boxY + 20, boxW, 60, "Choose an icon", "$FontColorGoldYellow", Config.font + 6, "center");
				// The icons alone: gold, brighter on hover; the current one bright.
				for (int k = 0; k < ObjectiveIconCount; ++k) {
					const int  cx = boxX + 40 + (k % PickColumns) * (PickCell + PickGap), cy = boxY + 100 + (k / PickColumns) * (PickCell + PickGap);
					const bool on = k == Objectives[ObjPicking].icon;
					layout += TextButton("Pick" + std::to_string(k), cx, cy, PickCell, PickCell, Gui::SpriteName("objpick" + std::to_string(k)), on ? 1 : 0, 1, 1, "", "", "", "objset" + std::to_string(k));
				}
			}
		}
		// The open list, over everything: a click anywhere else closes it.
		if (!list.names.empty()) {
			layout += HitButton("DropCatch", GridLeft, ViewTop, PageSize * Cell, ViewRows * Cell, "", "dropx");
			for (std::size_t k = 0; k < list.names.size(); ++k)
				layout += TextButton("DropItem" + std::to_string(k), list.x, list.y + static_cast<int>(k) * TileHeight, list.width, TileHeight, Kit::DropTile(list.width, TileHeight), 0, 1, 1, list.names[k],
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
	if (MaxScroll > 0) layout += GridBar.Steps(BarTop, ScrollSteps, ThumbSteps(), MaxScroll, Scroll * ScrollFrames, MaxScroll * ScrollFrames);
	if (CurrentCategory != SettingsView || (SettingsPage != 2 && !VarEditing)) GridBar.Attach();
	return Gui::Panel("grail-on/" + id, WindowX() - MenuRoom, PanelY, PanelSpan(), std::max(PanelHeight, Config.searchY - PanelY + 2 * DropHeight + SearchGap + 8), catcher + ShiftLayout(layout),
	                  over ? OverPriority : 10, Gui::LeftAnchor);
}

// Lock held. What a view shows: two views with the same key are the same.
std::string ViewKey() {
	if (CurrentCategory != SettingsView) OpenDrop = -1; // a list closes with its page
	std::string key = "q" + TypedText(TypeSearch) + (ObjTyping && TypeTarget == TypeSearch ? "|" : "") + std::to_string(SearchAt) + "/" + std::to_string(SearchMatches.size()) + ":" + std::to_string(Config.searchX) + ","
	                + std::to_string(Config.searchY) + "," + std::to_string(Config.searchWidth) + ":" + (CurrentCategory == SettingsView && SettingsPage == 4 ? "t" + ObjTypedText() + (ObjTyping ? "|" : "") + ":" : std::string()) + (CurrentCategory == SettingsView && (SettingsPage == 2 || VarEditing) ? "V" + std::to_string(VarEditing) + "x" + TypedText(TypeExclude) + (ObjTyping && TypeTarget == TypeExclude ? "|" : "") + ":" + std::to_string(ExclFirst) + ":" + (ObjTyping && TypeTarget == TypeListName ? "n" + std::to_string(ExclRenaming) + TypedText(TypeListName) + ":" : std::string()) + "D" + std::to_string(ExclDeleteArmed) + "E" + std::to_string(ExclEditing) + ":" : std::string()) + std::to_string(Config.version) + ":d" + std::to_string(OpenDrop) + ":" + std::to_string(ProblemCount.load()) + ":" + std::to_string(CurrentCategory) + (ViewEth ? "e:" : ":") + std::to_string(Scroll) + ":";
	for (const auto& tile : Shown) key += Present.count(tile.unique) ? Perfect.count({ tile.unique, tile.eth }) ? '2' : '1' : '0';
	key += ':';
	// The cut slots and pictures, which hold no real item: drawn stored or not.
	// A ring's or amulet's picture too: a dropped one is shown stored before its
	// record exists (PredictDrop), with its base's picture until then.
	for (const auto& tile : Peeking)
		key += std::to_string(tile.unique) + (Stored.count({ tile.unique, tile.eth }) ? (Perfect.count({ tile.unique, tile.eth }) ? "S" : "s") + StoredArt(tile.unique, tile.eth, "") : "0");
	key += ':';
	for (const auto& [set, row] : ShownHeaders) key += SetKnown(set) ? '1' : '0';
	key += ':';
	for (const auto& row : PlacedRows)
		for (const auto& tile : row.tiles)
			key += BaseTiles.count({ tile.unique, tile.eth }) ? std::string("b") : Stored.count({ tile.unique, tile.eth }) ? "s" + StoredArt(tile.unique, tile.eth, "") : Present.count(tile.unique) ? "p" : "0";
	// The rings show the counts: every menu's, and the stats pages' or the menu's categories'.
	const auto add = [&](const KindCount& count) { key += ":" + std::to_string(count.perfect) + "/" + std::to_string(count.stored) + "/" + std::to_string(count.bases) + "/" + std::to_string(count.missing); };
	for (int m = 0; m < static_cast<int>(std::size(Menus)); ++m) add(CountMenu(m));
	key += ":l" + std::to_string(LostOf.size());
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
std::vector<Gui::StringTable::Entry> StringsTable() {
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
		strings.push_back({ "N" + n, PlainName(u), Raw });
		if (!Uniques[u].eth) continue;
		strings.push_back({ "BE" + n, Uniques[u].base, Raw }); // in the ethereal menus: no mark needed
		strings.push_back({ "NE" + n, PlainName(u), Raw });
	}
	const int                             chosen = Config.language >= 0 && Config.language < LanguageCount ? Config.language : -1;
	std::vector<Gui::StringTable::Entry> table;
	for (const auto& text : strings) {
		auto& entry = table.emplace_back(Gui::StringTable::Entry { text.key, {} });
		for (int l = 0; l < LanguageCount; ++l) {
			const int language = chosen >= 0 ? chosen : l;
			entry.texts.push_back(text.kind == Raw           ? text.english
			                      : text.kind == EthCategory ? TrFormat("Ethereal %s", Tr(text.english, language), language)
			                      : text.kind == Sockets     ? TrFormat(text.english == "1" ? "%d Socket" : "%d Sockets", text.english, language)
			                                                 : Tr(text.english, language));
		}
	}
	return table;
}

// ---- art ----
// Every picture is D2R GUI's: the stash's look (StashKit: the game's own
// settings parts, scaled by area average, the slot stone where one is
// missing), registered the first time a size is asked for. The plugin's own
// pictures (its icons, the grail button, the stash frame) go through Gui::RegisterSprite.

using Gui::RegisterResource;
using Gui::RegisterSprite;

// The inventory button's grail (frames: 0 normal, 1 pressed, 2 disabled, 3 hovered), in the mold.
constexpr int ButtonX = 3, ButtonY = 954, GrailButtonWidth = 52, GrailButtonHeight = 60, GrailIconSize = 42;
bool RegisterGrailButton() {
	const unsigned char* mask = nullptr;
	for (const auto& icon : IconMasks)
		if (!std::strcmp(icon.name, "grail")) mask = icon.alpha;
	return mask && RegisterSprite("grailbutton", Gui::MaskButton(mask, IconSize, GrailButtonWidth, GrailButtonHeight, GrailIconSize));
}

// The sprite of a ring chart, made once for its numbers.
std::string ChartSprite(const std::vector<std::pair<int, Rgb>>& parts, int size, int hole, bool square) { return Kit::Chart(parts, size, hole, square, MissingColor); }


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

constexpr int KeptLoaded = 8; // closed views the game keeps built; the others are let go (each holds many widgets)
ULONGLONG     LastViewBuild {}; // when ShowView last built or picked a view
Gui::ViewCache                  Views; // ViewCount panels; group 1: over the open stash
int                             ShownView = -1, PreparedView = -1;
extern std::uintptr_t           Base; // the game's exe
bool                            IsOverView(int i) { return i >= ViewCount - OverViews; }
std::atomic<bool>               StashClosing {}; // the game's message that the stash closes (before its panel says so)
// The stash is open (the game's stash panel), on the builds whose code is known (checked once).
bool StashShown() {
	static const bool told = [] {
		const bool ok = Gui::Hud::StashKnown();
		Say(false, "grailon: stash panel code %s: the grail %s over the open stash", ok ? "known" : "not known", ok ? "opens" : "closes the stash; it cannot open");
		return true;
	}();
	(void)told;
	return Gui::Hud::StashShown();
}
unsigned                        OpenSequence {}; // the latest prepared view; older ones never open
unsigned long long              PrepareStart {};
bool                            PreparedNew {};
std::atomic<bool>               RebuildWanted {}, ArrangeRunning {}, ViewIsOpen {}, PluginMoving {};
std::atomic<D2RL::PlayerHandle> LocalPlayer {};
std::atomic<std::uint32_t>      ArrangeAgain {}; // asked for while one was running: 1, or the runtime id to wait for


bool ViewOpen() { return Views.IsOpen(ShownView); }

void Animate();
std::set<int> LateClose; // views left open under the shown one while an item is on the cursor (UI thread)
// A view that keeps its layout: on screen, being prepared, or left open under a cursor item.
bool ViewBusy(int i) { return i == ShownView || i == PreparedView || LateClose.count(i); }

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
		if (view != ShownView && Views.IsOpen(view)) Views.Close(view), Trace("late close", view);
	LateClose.clear();
}

void CloseScrollPanel();
void SyncArea();
Gui::SinglePanel ErrorPage; // what is switched off: made again each time it opens
void CloseView() {
	++OpenSequence; // a view being prepared stays closed
	LateClose.clear();
	CloseScrollPanel();
	ErrorPage.Close();
	PreparedView = -1;
	Views.CloseAll();
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
Gui::SinglePanel ScrollPanel; // the scroll animation's frames: made again for each step
void CloseScrollPanel() {
	if (ScrollPanel.IsOpen()) ScrollPanel.Close(), Trace("scroll frames closed");
}
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
	if (!ErrorPage.Ready()) return;
	D2RL::Panels::Result result {};
	if (!ErrorPage.Show(ErrorLayout(), &result) && result != D2RL::Panels::Result::Success) Say(true, "grailon: could not open the error page (result %u)", static_cast<unsigned>(result));
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
	// Fast clicks (the scroll bar dragged, peek slots, arrows) become one
	// view: never two within 60 ms, and never while the last new layout is
	// still loading (half a second at most). Building a panel over one the game
	// was still making crashed it in its layout loader.
	if (!drop.wanted) {
		const ULONGLONG now = GetTickCount64();
		ULONGLONG       due = 0;
		if (LastViewBuild && now - LastViewBuild < 60) due = LastViewBuild + 60;
		if (PreparedView >= 0 && PreparedNew && now - PrepareStart < 500) due = std::max<ULONGLONG>(due, std::min<ULONGLONG>(PrepareStart + 500, now + 60));
		if (due) {
			ViewRetryAt = ViewRetryAt ? std::min<ULONGLONG>(ViewRetryAt, due) : due;
			return;
		}
	}
	LastViewBuild = GetTickCount64();
	int         index = -1;
	bool        settings {};
	std::string layout;
	std::string newKey; // the key of a view given a new layout
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
		// Over the open stash: a view of the panels that leave it open.
		const bool over = StashShown();
		// The host's grid stays just under the view: it follows the view's priority.
		if (OverShown.exchange(over) != over && Host::On()) (void)SetHostLayout(true);
		index = Views.Find(key, over ? 1 : 0);
		if (index >= 0 && index == ShownView && Views.IsOpen(index) && !drop.wanted) {
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
			// unloading such a panel crashed the game in its layout loader (Settle).
			index = Views.Free(over ? 1 : 0, ViewBusy);
			if (index < 0) {
				static int told = 0;
				if (told++ < 3) Say(false, "grailon: no view panel is free yet; the view waits a moment");
				ViewRetryAt = GetTickCount64() + 300;
				return;
			}
			if (Views[index].opened) Say(false, "grailon: view panel %d recycled (unused for %llu views)", index, Views.Clock() - Views[index].used);
			layout   = ViewLayout(Views[index].id, IsOverView(index));
			newKey   = key;
		}
	}
	PrepareStart = GetTickCount64();
	PreparedNew  = !layout.empty();
	if (!layout.empty() && !Views.Load(index, layout, newKey)) Say(true, "grailon: could not register the view layout");
	PreparedView        = index;
	const auto sequence = ++OpenSequence;
	// The settings show no tiles: their grid is off screen, the items stay.
	if (const auto player = LocalPlayer.load(); player && !settings) {
		auto* job = new ArrangeJob { .player = player, .open = true, .sequence = sequence, .drop = drop };
		if (QueueArrange(job)) return;
	}
	OpenPrepared(sequence);
}

// UI thread. A view put off (fast clicks, no free panel) is made now.
void RetryViewIfDue() {
	if (ViewRetryAt && GetTickCount64() >= ViewRetryAt) {
		ViewRetryAt = 0;
		if (ViewOpen() || PreparedView >= 0) ShowView();
	}
}

// UI thread. Closed views beyond the newest KeptLoaded are let go by the game
// (UnloadPanel): their layout stays registered, so showing one again loads it
// back. Never one in use, closed under 1.5 s ago, or loading.
void LetClosedViewsGo() { Views.LetGo(KeptLoaded, ViewBusy); }

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
	const bool holding = CursorHasItem();
	Trace("view open", index);
	{
		std::lock_guard guard(Lock);
		ApplyHalfTiles();
	}
	const auto result  = Views.Open(index);
	Trace("view opened", index);
	if (holding && !CursorHasItem()) Say(false, "grailon: the cursor item went when the next view opened");
	// The new view opens first and the old one closes after, so the window never
	// disappears between two views. With an item on the cursor the old one stays
	// open under it until the cursor is empty (closing it may drop the item).
	if (previous >= 0 && previous != index && Views.IsOpen(previous)) {
		if (holding && !Config.hold) LateClose.insert(previous);
		else Views.Close(previous), Trace("old view closed", previous);
	}
	LateClose.erase(index);
	CloseScrollPanel();
	if (holding && !CursorHasItem()) Say(false, "grailon: the cursor item went when the scroll frames closed");
	if (Config.timing) Say(false, "grailon: %s view, shown after %llu ms", PreparedNew ? "new" : "cached", static_cast<unsigned long long>(GetTickCount64() - PrepareStart));
	if (result != D2RL::Panels::Result::Success) Say(true, "grailon: could not open the view (result %u)", static_cast<unsigned>(result));
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
	std::string               variant; // an item with a choice roll: its choice's key (ChoiceKey), empty when not known yet
	mutable int               picture = -2; // its picture from its bytes (-1: none), -2 before it is read
};
// All guarded by Lock.
std::map<std::uint32_t, Record>       Records;      // by fingerprint
std::set<std::uint32_t>               Outdated;     // records that are an older version of an item the current season reworked (Lost items)
std::size_t RecordCount() { return Records.size(); }
std::set<std::uint32_t>               Tombstones;   // fingerprints taken out of the grail
std::set<std::string>                 FoundIds;     // entries ever stored
std::map<std::uint32_t, std::uint32_t> Materialized; // runtime id of a record's item on the page -> fingerprint
std::map<std::uint32_t, std::uint32_t> Fingerprints; // runtime id -> fingerprint, read once
std::wstring                          StorePath;    // the open storage file, empty before a character
// Records taken out of the grail, kept until the game has saved the character
// (it leaves the game): after a crash the item taken out was never saved, so
// its record comes back (see RestoreTakenOut).
struct TakenOutRecord {
	Record      record;
	std::string owner; // the character that took it out
};
std::map<std::uint32_t, TakenOutRecord> TakenOut;
std::string                           StoreCharacter; // the open storage's character (UTF-8 name)
bool                                  TakenOutChecked {}; // this character's leftovers from a crash were looked at
bool                                  PicturesScanned {}; // this character's items' pictures read (graphic variants on)
void ReadPictures(D2RL::PlayerHandle player, bool all);
bool                                  OrphansChecked {};  // stored items no tile claims were given back for this character
bool StaleChecked {}; // the page this character came in with was checked against the records; guarded by Lock
bool                                  StoreReady {}; // the native calls are there
D2RL::PlayerHandle                    StoreOwner {}; // the character it was opened for

SaveWriterFn   SaveItem {};
HeaderReaderFn ReadHeader {};
SaveLoaderFn   LoadItem {};
UnitByIdFn     UnitById {};
SocketItemFn   SocketItem {};
FreeItemFn     FreeItem {};
std::uintptr_t CoreModule {};

// Copies `size` bytes of this process at `address`, false when the memory is
// not readable (VirtualQuery, not ReadProcessMemory: antivirus scanners count
// that one against a DLL).
bool ReadOwn(const void* address, void* out, std::size_t size) {
	if (!address) return false;
	const auto* from = static_cast<const unsigned char*>(address);
	std::size_t done = 0;
	while (done < size) {
		MEMORY_BASIC_INFORMATION info {};
		if (!VirtualQuery(from + done, &info, sizeof(info)) || info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))
		    || !(info.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
			return false;
		const auto*       regionEnd = static_cast<const unsigned char*>(info.BaseAddress) + info.RegionSize;
		const std::size_t n         = std::min<std::size_t>(size - done, static_cast<std::size_t>(regionEnd - (from + done)));
		std::memcpy(static_cast<unsigned char*>(out) + done, from + done, n);
		done += n;
	}
	return true;
}

template <std::size_t N>
bool BytesAt(std::uintptr_t address, const std::uint8_t (&bytes)[N]) {
	std::uint8_t code[N] {};
	return address && ReadOwn(reinterpret_cast<const void*>(address), code, N) && !std::memcmp(code, bytes, N);
}

bool LocateStore() {
	const auto exe = static_cast<std::uintptr_t>(Context->exeBase);
	CoreModule     = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"D2RCore.dll"));
	std::uint64_t slotRva = 0;
	const bool    core    = CoreModule && BytesAt(CoreModule + Store::CoreCall, Store::CoreCallBytes)
	                && ReadOwn(reinterpret_cast<const void*>(CoreModule + Store::CoreSlot + 8), &slotRva, 8) && slotRva == Store::CreateItem;
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
	const auto put = [&](const Record& record) {
		w.text(record.base ? "b|" + record.id : record.id);
		w.u8(record.eth), w.u8(record.perfect);
		w.u32(record.fingerprint), w.u32(record.code), w.u32(record.quality), w.u32(static_cast<std::uint32_t>(record.qualityRecordId)), w.u32(record.itemLevel), w.u32(record.stateFlags);
		w.u32(static_cast<std::uint32_t>(record.bytes.size()));
		w.out.append(reinterpret_cast<const char*>(record.bytes.data()), record.bytes.size());
	};
	for (const auto& [fingerprint, record] : Records) put(record);
	w.u32(static_cast<std::uint32_t>(Tombstones.size()));
	for (const auto fingerprint : Tombstones) w.u32(fingerprint);
	w.u32(static_cast<std::uint32_t>(FoundIds.size() + CheckedIds.size()));
	for (const auto& id : FoundIds) w.text(id);
	for (const auto& id : CheckedIds) w.text("c|" + id);
	// After what older versions read (they stop before it): the records taken
	// out and not saved by the game yet.
	std::erase_if(TakenOut, [](const auto& out) { return Records.count(out.first) != 0; }); // back in the grail
	w.out += "OUT1";
	w.u32(static_cast<std::uint32_t>(TakenOut.size()));
	for (const auto& [fingerprint, out] : TakenOut) w.text(out.owner), put(out.record);
	// Then the records' variants (older versions stop before them).
	std::uint32_t variants = 0;
	for (const auto& [fingerprint, record] : Records) variants += !record.variant.empty();
	w.out += "VAR1";
	w.u32(variants);
	for (const auto& [fingerprint, record] : Records)
		if (!record.variant.empty()) w.u32(fingerprint), w.text(record.variant);
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
	Records.clear(), Tombstones.clear(), FoundIds.clear(), CheckedIds.clear(), Materialized.clear(), Fingerprints.clear(), TakenOut.clear();
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
	const auto take = [&](Record& record) {
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
		if (!r.ok || r.at + size > data.size()) return r.ok = false;
		record.bytes.assign(data.begin() + static_cast<std::ptrdiff_t>(r.at), data.begin() + static_cast<std::ptrdiff_t>(r.at + size));
		r.at += size;
		return true;
	};
	for (std::uint32_t n = r.get(4); r.ok && n--;) {
		Record record;
		if (!take(record)) break;
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
	if (r.ok && r.at + 8 <= data.size() && !data.compare(r.at, 4, "OUT1")) {
		r.at += 4;
		for (std::uint32_t n = r.get(4); r.ok && n--;) {
			TakenOutRecord out;
			out.owner = r.text();
			if (!r.ok || !take(out.record)) break;
			TakenOut[out.record.fingerprint] = std::move(out);
		}
	}
	if (r.ok && r.at + 8 <= data.size() && !data.compare(r.at, 4, "VAR1")) {
		r.at += 4;
		for (std::uint32_t n = r.get(4); r.ok && n--;) {
			const auto fingerprint = r.get(4);
			auto       key         = r.text();
			if (const auto record = Records.find(fingerprint); r.ok && record != Records.end()) record->second.variant = std::move(key);
		}
	}
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
// The client player's name as the game keeps it (UTF-8), empty when unknown.
std::string CharacterName() {
	const auto base = static_cast<std::uintptr_t>(Context->exeBase);
	void*      unit = reinterpret_cast<PlayerUnitFn>(base + Sound::PlayerUnit)(reinterpret_cast<LocalIndexFn>(base + Sound::LocalIndex)());
	char       name[17] {};
	void*      data   = nullptr;
	if (!unit || !ReadOwn(static_cast<const char*>(unit) + 0x10, &data, sizeof(data)) || !data
	    || !ReadOwn(data, name, 16))
		return {};
	return name;
}

// The client player's name (PlayerData at unit+0x10 starts with it), then the
// newest <name>.d2s in the save folder or a mod's: byte 0x24 bit 0x04 is
// hardcore.
bool Hardcore() {
	const auto base = static_cast<std::uintptr_t>(Context->exeBase);
	void*      unit = reinterpret_cast<PlayerUnitFn>(base + Sound::PlayerUnit)(reinterpret_cast<LocalIndexFn>(base + Sound::LocalIndex)());
	char       name[17] {};
	void*      data   = nullptr;
	if (!unit || !ReadOwn(static_cast<const char*>(unit) + 0x10, &data, sizeof(data)) || !data
	    || !ReadOwn(data, name, 16))
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
	const std::string  name     = CharacterName();
	std::lock_guard    guard(Lock);
	StoreOwner      = player;
	StoreCharacter  = name;
	TakenOutChecked = false;
	OrphansChecked  = false;
	PicturesScanned = false;
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
	const std::vector<VariantChoice>* variants {}; // set: the item's choices, read on the native item
	std::string               variant;     // its choice's key (ChoiceKey)
};

// The item's fingerprint as its save bytes hold it (what stays the same
// across saves and loads; ItemData+0x10 is NOT it, it changes on each load).
// The extended part starts with it, then the item level (7 bits) and the
// quality (4 bits): the fingerprint is the 32 bits before the first such
// level+quality after the compact part (53 bits + the code + 3 socket bits,
// so from bit 64 on). Read from the bytes alone, so the same item always
// gives the same value. 0 when none.
// The item level as the save writer wrote it: 99 at most (an item dropped at
// 110, the Hellfire Torch, is saved and loaded back at 99).
bool SavedLevel(std::uint32_t saved, std::uint32_t itemLevel) { return saved == (itemLevel & 0x7F) || (itemLevel > 99 && saved == 99); }
std::uint32_t FingerprintInBytes(const std::vector<std::uint8_t>& bytes, std::uint32_t itemLevel, std::uint32_t quality) {
	const auto bit = [&](std::size_t i) { return (bytes[i >> 3] >> (i & 7)) & 1u; };
	const auto get = [&](std::size_t at, int n) {
		std::uint32_t v = 0;
		for (int k = 0; k < n; ++k) v |= bit(at + k) << k;
		return v;
	};
	const std::size_t total = bytes.size() * 8;
	for (std::size_t at = 64 + 32; at + 11 <= total && at < 64 + 32 + 160; ++at) {
		if (!SavedLevel(get(at, 7), itemLevel) || get(at + 7, 4) != (quality & 0xF)) continue;
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
		if (!SavedLevel(get(at, 7), itemLevel) || get(at + 7, 4) != (quality & 0xF)) continue;
		return get(at + 11, 1) ? static_cast<int>(get(at + 12, 3)) : -1;
	}
	return -1;
}

Record* RecordAt(int u, bool eth);
// Lock held. A stored ring's, amulet's, jewel's or charm's tile: the picture
// its item has (ring1..5, amulet1..3, perfect_diamond1..6, charm_small1..3),
// not the base's.
std::string StoredArt(int u, bool eth, const std::string& art) {
	const auto& entry = Uniques[u];
	const int   count = entry.code == "rin" ? 5 : entry.code == "amu" ? 3 : entry.code == "jew" || entry.code == "cjw" ? 6 : entry.code == "cm1" || entry.code == "cm2" || entry.code == "cm3" ? 3 : 0;
	// An item with its own picture (Annihilus, Hellfire Torch, Seraph's Hymn...) keeps it.
	if (!count || entry.art != entry.ghost) return art;
	const Record* record = RecordAt(u, eth);
	if (!record) return art;
	const int picture = PictureInBytes(record->bytes, record->itemLevel, record->quality);
	if (picture < 0 || picture >= count || entry.ghost.empty()) return art;
	std::string name = entry.ghost;
	while (!name.empty() && name.back() >= '0' && name.back() <= '9') name.pop_back(); // the jewel's default picture
	return name + std::to_string(picture + 1);
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
	if (snapshot->variants)
		for (const auto& choice : *snapshot->variants)
			if (ChoiceOn(native, choice)) {
				snapshot->variant = ChoiceKey(choice);
				break;
			}
}

void SetPicture(std::uint32_t runtimeId, int picture);
// Where the game keeps an item's picture in its item data, learnt from the
// player's own items (their pictures read from their save bytes): the one byte
// that holds the picture on every one of them. The loot filter reads it on
// ground items, which have no save bytes. It starts at the place found on the
// current build (+0x5E) and is checked on every sample: a sample that
// disagrees drops it until a new one is learnt. -1 when not known.
constexpr int    KnownPictureOffset = 0x5E;
std::atomic<int> PictureOffset { KnownPictureOffset };
constexpr int    PictureScan = 0x80;
struct PictureLearning {
	std::bitset<PictureScan> out;     // offsets that did not hold the picture once
	std::set<int>            seen;    // the pictures sampled
	std::set<std::uint32_t>  items;   // the items sampled
	bool                     failed {}, confirmed {};
} Learning; // guarded by Lock


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
	SetPicture(item.runtimeId, PictureInBytes(snapshot.bytes, snapshot.itemLevel, snapshot.quality));
	return snapshot.fingerprint;
}


// An item's picture (-1: none), from its save bytes (read once per item).
void NeedPicture(std::uint32_t runtimeId);
// Its picture, from its save bytes. Only the game thread reads them: from any
// other, -1 now and the game thread reads it soon (the next look has it).
int PictureOfItem(const Items::ItemInfo& item) {
	{
		std::lock_guard guard(Lock);
		if (const auto known = Pictures.find(item.runtimeId); known != Pictures.end()) return known->second;
	}
	Snapshot snapshot { .itemLevel = item.itemLevel, .quality = static_cast<std::uint32_t>(item.quality) };
	snapshot.code = item.code;
	if (!ItemApi || ItemApi->editNativeItem(Context, item.handle, TakeSnapshot, &snapshot) != Items::Result::Success || !snapshot.ok) {
		NeedPicture(item.runtimeId);
		return -1;
	}
	std::lock_guard guard(Lock);
	SetPicture(item.runtimeId, PictureInBytes(snapshot.bytes, snapshot.itemLevel, snapshot.quality));
	return Pictures[item.runtimeId];
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
		if (!SavedLevel(get(at, 7), itemLevel) || get(at + 7, 4) != (quality & 0xF)) continue;
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
	// The game's flag disagreeing with the bytes: the flag is not used again.
	if (const int flag = RunewordFlagOn(item.runtimeId); flag >= 0 && (flag == 1) != (row >= 0) && RunewordFlagTrusted.exchange(false))
		Say(false, "grailon: %s: the runeword flag says %d, its bytes row %d; the flag is not used", CodeText(item.code).c_str(), flag, row);
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
	const auto key  = std::make_pair(item.runtimeId, item.code);
	const int  flag = RunewordFlagOn(item.runtimeId);
	if (const auto known = RunewordRows.find(key); known != RunewordRows.end()) {
		// A base read before its runes went in (same item): read again.
		if (known->second != -1 || flag != 1) return known->second == -3 ? -2 : known->second;
		RunewordRows.erase(known);
	}
	// No runeword flag: a plain item, nothing to read (a base dropped at once).
	if (flag == 0) return -1;
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

// By runtime id: whether an item going into the grail is perfect, as the UI
// thread read it (Enhanced Damage and Enhanced Defense are only in its
// tooltip, which the game thread cannot build). Guarded by Lock.
std::map<std::uint32_t, bool> UiPerfect;

void TagVariant(Record& record, const void* unit);
void MarkFound(const Record& record);
// Lock held. A new record for page item `item` of entry `u`.
bool RecordItem(const Items::ItemInfo& item, int u, bool eth, bool perfect) {
	if (Uniques[u].misc) perfect = true; // nothing on a misc item varies: every copy is perfect
	if (const auto known = UiPerfect.find(item.runtimeId); known != UiPerfect.end()) perfect = perfect || known->second, UiPerfect.erase(known);
	Snapshot snapshot { .itemLevel = item.itemLevel, .quality = static_cast<std::uint32_t>(item.quality) };
	snapshot.code = item.code;
	if (const int b = Uniques[u].variant ? Uniques[u].variantOf : u; b >= 0 && !Uniques[b].affixes.empty()) snapshot.variants = &Uniques[b].affixes;
	const auto read = ItemApi->editNativeItem(Context, item.handle, TakeSnapshot, &snapshot);
	// A misc base (gems, runes...): every copy is the same, its base is its identity.
	if (read == Items::Result::Success && snapshot.ok && !snapshot.fingerprint && Uniques[u].misc) snapshot.fingerprint = SimplePrint(item.code);
	if (read != Items::Result::Success || !snapshot.ok || !snapshot.fingerprint) {
		static int told = 0;
		if (told++ < 8)
			Say(true, "grailon: %s could not be stored: read %u, %zu bytes, fingerprint %08X", CodeText(item.code).c_str(), static_cast<unsigned>(read), snapshot.bytes.size(), snapshot.fingerprint);
		return false; // without its identity it could come back twice
	}
	if (Records.count(snapshot.fingerprint)) {
		static int told = 0;
		if (told++ < 8)
			Say(true, "grailon: %s has the same identity as an item already in the grail (copied with an item editor?): it cannot be stored", CodeText(item.code).c_str());
		return false; // it would take the other item's place
	}
	const auto base = BaseFor.find(item.runtimeId);
	Record     record { .id = Uniques[Uniques[u].variant ? Uniques[u].variantOf : u].id, .eth = eth, .perfect = perfect, .base = base != BaseFor.end() && base->second == u, .fingerprint = snapshot.fingerprint, .code = item.code,
		            .quality = static_cast<std::uint32_t>(item.quality), .itemLevel = item.itemLevel, .stateFlags = item.stateFlags, .qualityRecordId = item.qualityRecordId,
		            .bytes = std::move(snapshot.bytes) };
	Fingerprints[item.runtimeId] = record.fingerprint;
	Tombstones.erase(record.fingerprint);
	if (record.base) record.perfect = false;
	else FoundIds.insert(record.id); // a base finds nothing
	// Its choice: the snapshot's (read on the item the game saved), else the client's item, else the entry's.
	record.variant = snapshot.variant;
	if (record.variant.empty()) TagVariant(record, ClientItem(item.runtimeId));
	if (record.variant.empty() && Uniques[u].variant && Uniques[u].affix >= 0 && Uniques[u].variantOf >= 0) record.variant = ChoiceKey(Uniques[Uniques[u].variantOf].affixes[Uniques[u].affix]);
	MarkFound(record);
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
// Ghosts get them the same way: a runeword's tooltip lists its runes and their stats.
// Returns the socketed items.
std::vector<void*> SocketChildren(void* game, void* item, const std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t playerId) {
	std::vector<void*> socketed;
	void*       player = playerId != 0xFFFFFFFF ? UnitById(game, 0, playerId) : nullptr;
	const auto  parent = *reinterpret_cast<std::uint32_t*>(static_cast<char*>(item) + 8);
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
		else socketed.push_back(child);
		if (read <= 0) break;
		offset += static_cast<std::size_t>(read);
	}
	return socketed;
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
	if (Decode.item && read > 0) (void)SocketChildren(game, Decode.item, *Decode.bytes, static_cast<std::size_t>(read), Decode.playerId);
	return Decode.item;
}

// Game thread: the record's item, made again where `destination` says.
Items::ItemCreateSpec RecordSpec(const Record& record, const Items::ItemDestination& destination);
int RecordPicture(const Record& record);
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
	// Its picture is the record's: known at once (no save bytes to read).
	if (Items::ItemInfo info { .structSize = Items::ItemInfoSize }; result == Items::Result::Success && made && *made && GetInfo(Context, *made, &info) == Items::Result::Success) {
		std::lock_guard guard(Lock);
		if (const int picture = RecordPicture(record); picture >= 0) SetPicture(info.runtimeId, picture);
		VariantUnits.erase(info.runtimeId);
	}
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
	spec.itemLevel   = std::min<std::uint32_t>(record.itemLevel, 99); // D2RLoader refuses more (a Torch dropped at 110); the save bytes hold 99 anyway
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
std::atomic<std::uint64_t>     GhostEpoch {};   // counts ghost frees: a freed ghost's address can come back as another item
std::set<std::uint32_t>        PerfectChecked;  // records whose ghost told whether they are perfect (this launch); guarded by Lock
std::set<std::uint32_t>        VariantChecked;  // records whose ghost told their variant (this launch); guarded by Lock
std::set<std::uint32_t>        SweepGhosts;     // ghosts made only for that check, freed after it; guarded by Lock

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

std::mutex                           GhostKidsLock;
std::map<void*, std::vector<void*>> GhostKids; // a ghost's socketed items, freed with it

// Game thread, Lock not held. A record's bytes decoded by the game's save
// loader into a loose server item (in no inventory), or null.
void* GhostFromBytes(void* game, const std::vector<std::uint8_t>& bytes) {
	if (!game || bytes.empty()) return nullptr;
	alignas(16) std::uint8_t header[0x100] {};
	const auto               flag = *reinterpret_cast<std::uint8_t*>(static_cast<char*>(game) + Store::GameFlag);
	(void)ReadHeader(flag, bytes.data(), bytes.size(), 1, header, Store::Version);
	std::int32_t first = 0;
	std::memcpy(&first, header, 4);
	int   read = 0;
	void* item = first != -1 ? LoadItem(game, nullptr, bytes.data(), static_cast<int>(bytes.size()), 1, header, &read, Store::Version) : nullptr;
	// Its socketed items (they follow it in the bytes), in it as in the game.
	if (item && read > 0 && static_cast<std::size_t>(read) < bytes.size()) {
		const auto base = static_cast<std::uintptr_t>(Context->exeBase);
		void*      unit = reinterpret_cast<PlayerUnitFn>(base + Sound::PlayerUnit)(reinterpret_cast<LocalIndexFn>(base + Sound::LocalIndex)());
		if (unit)
			if (auto socketed = SocketChildren(game, item, bytes, static_cast<std::size_t>(read), *reinterpret_cast<std::uint32_t*>(static_cast<char*>(unit) + 8)); !socketed.empty()) {
				std::lock_guard guard(GhostKidsLock);
				GhostKids[item] = std::move(socketed);
			}
	}
	return item;
}

// Game thread. A ghost freed: its socketed items first (each leaves it), then it.
void FreeGhost(void* game, void* unit) {
	std::vector<void*> socketed;
	{
		std::lock_guard guard(GhostKidsLock);
		if (const auto kids = GhostKids.find(unit); kids != GhostKids.end()) socketed = std::move(kids->second), GhostKids.erase(kids);
	}
	for (void* child : socketed) FreeItem(game, child);
	FreeItem(game, unit);
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
		void* unit = game ? GhostFromBytes(game, bytes) : nullptr;
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
	++GhostEpoch;
	for (const auto& [fingerprint, ghost] : gone)
		if (ghost.unit && ghost.game && ghost.game == ServerGame.load()) FreeGhost(ghost.game, ghost.unit);
		else if (ghost.unit) {
			std::lock_guard kids(GhostKidsLock);
			GhostKids.erase(ghost.unit); // freed with its game
		}
	{
		std::lock_guard guard(Lock);
		SweepGhosts.clear();
	}
	GhostsFreeing = false;
}

// Game thread: the ghosts the perfect check is done with.
void __cdecl FreeSomeGhosts(const D2RL::PluginContext*, void* userData) noexcept {
	auto* gone = static_cast<std::vector<Ghost>*>(userData);
	for (const auto& ghost : *gone)
		if (ghost.unit && ghost.game && ghost.game == ServerGame.load()) FreeGhost(ghost.game, ghost.unit);
		else if (ghost.unit) {
			std::lock_guard kids(GhostKidsLock);
			GhostKids.erase(ghost.unit); // freed with its game
		}
	delete gone;
}

// ---- an empty page outside the view (game thread) ----
// The grail file holds every stored item; the page only holds copies of the
// tiles on screen. Once the view closes (and when a character loads) the page
// is emptied, so the character's save never holds page items: the game loads
// it without the plugin too, and the items come back from the file when the
// view opens again. Grail items nobody recorded are recorded first; anything
// else goes to the inventory.
Record* RecordAt(int u, bool eth);

// Lock held. Whether an item with a record's identity is that record's item:
// copies made with an item editor share their identity (an ethereal copy of a
// normal one, say) and must not be taken for it.
bool SameItem(const Record& record, const Items::ItemInfo& item) {
	return record.code == item.code && record.eth == IsEth(item) && record.quality == static_cast<std::uint32_t>(item.quality) && record.qualityRecordId == item.qualityRecordId;
}

// Game thread, once per character. Records this character took out in a game
// that never ended (the game closed unexpectedly before saving it): an item
// the character does not have now was lost with the game, so its record comes
// back into the grail.
void RestoreTakenOut(const D2RL::PluginContext* context, D2RL::PlayerHandle player) {
	std::vector<std::uint32_t> mine;
	{
		std::lock_guard guard(Lock);
		if (TakenOutChecked || StorePath.empty() || StoreOwner != player) return;
		TakenOutChecked = true;
		for (const auto& [fingerprint, out] : TakenOut)
			if (out.owner == StoreCharacter) mine.push_back(fingerprint);
	}
	if (mine.empty()) return;
	std::vector<Items::ItemInfo> items;
	using C = Items::ItemContainer;
	const D2RL::Inventory::ItemFilter everywhere { .structSize = D2RL::Inventory::ItemFilterSize,
		                                           .containerMask = Items::ContainerBit(C::Equipment) | Items::ContainerBit(C::Cursor) | Items::ContainerBit(C::Belt)
		                                                          | Items::ContainerBit(C::Inventory) | Items::ContainerBit(C::Cube) | Items::ContainerBit(C::PersonalStash)
		                                                          | Items::ContainerBit(C::SharedStash) };
	(void)Inventory->forEachInventoryItem(context, player, &everywhere, CollectRaw, &items);
	std::set<std::uint32_t> held;
	for (const auto& item : items)
		if (item.container != C::CustomPage)
			if (const auto print = FingerprintOf(item)) held.insert(print);
	std::lock_guard guard(Lock);
	int back = 0;
	for (const auto fingerprint : mine) {
		const auto out = TakenOut.find(fingerprint);
		if (out == TakenOut.end()) continue;
		if (!held.count(fingerprint) && !Records.count(fingerprint)) {
			Records[fingerprint] = out->second.record;
			Tombstones.erase(fingerprint);
			++back;
		}
		TakenOut.erase(out);
	}
	if (back) Say(false, "grailon: %d item(s) taken out of the grail before the game closed unexpectedly were never saved: they are back in the grail", back);
	RebuildStored();
	SaveStore();
}

// Game thread, once per character once the lists are built. A stored item
// whose entry no tile claims any more (a mod removed it, or a newer version
// named it otherwise and nothing leads its old name to a tile) would stay in
// the file unseen: it comes back to the inventory, else the personal stash.
// Misc items are left alone: their tiles follow the misc tracking settings.
Items::Result Materialize(D2RL::PlayerHandle player, const Record& record, const Items::ItemDestination& destination, D2RL::ItemHandle* made);
void TakeOut(std::uint32_t fingerprint);
bool DestroyItem(D2RL::PlayerHandle player, D2RL::ItemHandle item);
void GiveBackOrphans(D2RL::PlayerHandle player) {
	// Records without a tile now show in the Lost items menu: nothing is made.
	return;
	std::vector<Record> orphans;
	{
		std::lock_guard guard(Lock);
		if (OrphansChecked || !ListsReady || StorePath.empty() || StoreOwner != player || Uniques.empty()) return;
		OrphansChecked = true;
		for (const auto& [fingerprint, record] : Records) {
			if (record.id.rfind("m:", 0) == 0 || record.id.rfind("o:", 0) == 0) continue;
			// (A variant base's: its base's tile, whatever the Variants setting shows.)
			const auto entry = EntryById.find(record.id);
			if (entry == EntryById.end() || !BaseHasTile(entry->second, record.eth)) orphans.push_back(record);
		}
	}
	int back = 0, kept = 0;
	for (const auto& record : orphans) {
		auto status = Items::Result::InvalidArgument;
		for (const auto where : { Items::ItemContainer::Inventory, Items::ItemContainer::PersonalStash }) {
			if (status == Items::Result::Success) break;
			D2RL::ItemHandle made {};
			PluginMoving = true;
			status       = Materialize(player, record, { .structSize = Items::ItemDestinationRequiredSize, .container = where, .placement = Items::Placement::Automatic }, &made);
			PluginMoving = false;
			Items::ItemInfo info { .structSize = Items::ItemInfoSize };
			if (status == Items::Result::Success && made && (GetInfo(Context, made, &info) != Items::Result::Success || info.container != where)) {
				(void)DestroyItem(player, made); // not where asked: the record keeps it
				status = Items::Result::InvalidArgument;
			}
		}
		std::lock_guard guard(Lock);
		if (status == Items::Result::Success) TakeOut(record.fingerprint), ++back;
		else ++kept;
	}
	if (back || kept) {
		std::lock_guard guard(Lock);
		RebuildStored();
		SaveStore();
	}
	if (back) Say(true, "grailon: %d stored item(s) no grail tile holds any more are back in your inventory or personal stash", back);
	if (kept) Say(true, "grailon: %d stored item(s) no grail tile holds found no room in the inventory or personal stash; they stay in the grail file and are tried again the next time this character comes in", kept);
}

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
	RestoreTakenOut(context, player);
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
		const auto print  = FingerprintOf(item);
		const auto record = print ? Records.find(print) : Records.end();
		// The file has it (the page as the character came in, or put there by
		// the grail), or it left the grail elsewhere since.
		bool copy = (record != Records.end() && SameItem(record->second, item) && (!StaleChecked || Materialized.count(item.runtimeId)))
		         || (print && !StaleChecked && Tombstones.count(print));
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
// (The first one by fingerprint: with Variants off, every variant's copy
// is its base's, the others stay in the file, hidden.)
int RecordEntry(const Record& record);
Record* RecordAt(int u, bool eth) {
	for (auto& [fingerprint, record] : Records)
		if (record.eth == eth && RecordEntry(record) == u) return &record;
	return nullptr;
}

// Lock held. The entry whose tile shows a record, or -1: a variant base's
// record is its variant's with Variants on (none while its variant is not
// known), its base's with it off.
int PictureInBytes(const std::vector<std::uint8_t>& bytes, std::uint32_t itemLevel, std::uint32_t quality);
// A record's picture (read from its bytes once).
int RecordPicture(const Record& record) {
	if (record.picture == -2) record.picture = PictureInBytes(record.bytes, record.itemLevel, record.quality);
	return record.picture;
}
void ResizeRolls();
int NormalEntry(const Record& record);
int RecordEntry(const Record& record) {
	if (const auto lost = LostOf.find(record.fingerprint); lost != LostOf.end()) return lost->second;
	return NormalEntry(record);
}
// The tile a record goes in as the lists and settings say (lost items aside).
int NormalEntry(const Record& record) {
	const auto entry = EntryById.find(record.id);
	if (entry == EntryById.end()) return -1;
	const int u = entry->second;
	if (Uniques[u].variants.empty() || record.base) return u;
	const int a = AffixOn(u) ? AffixIndexOfKey(u, record.variant) : -1, p = GraphicOn(u) ? RecordPicture(record) : -1;
	return VariantOf(u, a, p);
}

// Lock held. Every variant a record is (whatever the settings show) counts as found.
void MarkFound(const Record& record) {
	if (record.base) return;
	const auto entry = EntryById.find(record.id);
	if (entry == EntryById.end() || Uniques[entry->second].variants.empty()) return;
	const int u = entry->second, a = AffixIndexOfKey(u, record.variant), p = Uniques[u].pictures > 1 ? RecordPicture(record) : -1;
	for (const int v : Uniques[u].variants) {
		const auto& variant = Uniques[v];
		if ((variant.affix < 0 || variant.affix == a) && (variant.picture < 0 || variant.picture == p)) FoundIds.insert(variant.id);
	}
}

// Lock held. Tags a record of an item with a choice roll with its choice
// (read on `unit`, its item); its variants count as found.
void TagVariant(Record& record, const void* unit) {
	if (!record.variant.empty() || !unit) return;
	const auto entry = EntryById.find(record.id);
	if (entry == EntryById.end() || Uniques[entry->second].affixes.empty()) return;
	if (const int a = AffixIndex(entry->second, unit); a >= 0) record.variant = ChoiceKey(Uniques[entry->second].affixes[a]);
	MarkFound(record);
}

// Lock held. An item left the grail: its record goes, its fingerprint stays.
void TakeOut(std::uint32_t fingerprint) {
	for (const auto& [runtime, print] : Materialized)
		if (print == fingerprint) BaseFor.erase(runtime);
	// A simple item's stand-in is every copy's: no tombstone, or the next copy put in would go.
	const auto record = Records.find(fingerprint);
	const bool simple = record != Records.end() && record->second.id.rfind("m:", 0) == 0 && fingerprint == SimplePrint(record->second.code);
	if (record != Records.end() && !simple) TakenOut[fingerprint] = { record->second, StoreCharacter };
	Records.erase(fingerprint);
	if (!simple) Tombstones.insert(fingerprint);
	std::erase_if(Materialized, [&](const auto& item) { return item.second == fingerprint; });
}

// Lock held. What the views show from the records: each tile's item (its
// runtime id when on the page, else 0), the entries found, the perfect ones.
std::uint64_t GrailChanges {}; // counts the grail's rebuilds, for what is kept about its copies
// Lock held. Whether a record still is the item its entry says (the game's
// files may have changed: another row, another base).
bool RecordMatches(const Record& record, int u) {
	const auto& entry = Uniques[u];
	if (entry.runeword || entry.objective || entry.lost) return true;
	if (entry.misc || entry.secret) return CodeText(record.code) == entry.code;
	const auto quality = static_cast<Items::Quality>(record.quality);
	if (quality != (entry.set ? Items::Quality::Set : Items::Quality::Unique)) return false;
	return CodeText(record.code) == entry.code && record.qualityRecordId == entry.row;
}

// Lock held. The Lost items tiles: one entry per lost record, after every
// other entry (their indices are the last), in the Lost items menu's category.
int LostCategory = -1;
void UpdateMiscTracking();
void BuildLost(const std::vector<std::uint32_t>& prints) {
	while (!Uniques.empty() && Uniques.back().lost) {
		EntryById.erase(Uniques.back().id);
		Uniques.pop_back();
	}
	LostOf.clear();
	if (LostCategory < 0 || LostCategory >= CategoryCount() || Categories[LostCategory].menu != LostMenu) {
		LostCategory = -1;
		for (int c = 0; c < CategoryCount(); ++c)
			if (Categories[c].menu == LostMenu) LostCategory = c;
	}
	if (LostCategory < 0 && !prints.empty()) {
		CategoryEntry category { LostMenu, "Lost items", "Lost items" };
		category.icon = "d2rloader\\grail-on\\icon-lost";
		LostCategory  = CategoryCount();
		Categories.push_back(category);
	}
	for (const auto print : prints) {
		const auto record = Records.find(print);
		if (record == Records.end()) continue;
		const auto& r    = record->second;
		const auto  code = CodeText(r.code);
		const auto  base = Bases.find(code);
		const auto  was  = EntryById.find(r.id);
		UniqueEntry entry;
		entry.category = LostCategory;
		entry.row      = -1;
		entry.lost     = true;
		char id[16] {};
		std::snprintf(id, sizeof(id), "l:%08X", print);
		entry.id      = id;
		entry.kind    = base != Bases.end() ? base->second.kind : 2;
		entry.width   = base != Bases.end() ? base->second.width : 1;
		entry.height  = base != Bases.end() ? base->second.height : 1;
		entry.eth     = entry.onlyEth = r.eth; // its own tile only: ethereal or not
		entry.code    = code;
		entry.base    = base != Bases.end() ? base->second.name : code;
		entry.ghost   = base != Bases.end() ? base->second.asset : std::string();
		entry.art     = entry.ghost;
		entry.colorClass = base != Bases.end() ? base->second.colorClass : -1;
		if (was != EntryById.end() && !Uniques[was->second].lost) {
			const auto& known = Uniques[was->second];
			entry.name = known.name, entry.color = known.color, entry.set = false;
			if (known.code == code) entry.art = known.art;
		} else entry.name = entry.base;
		Uniques.push_back(entry);
		EntryById[entry.id] = UniqueCount() - 1;
		LostOf[print]       = UniqueCount() - 1;
	}
	ResizeRolls();
	UpdateMiscTracking();
}

// Lock held. Runewords stored as empty bases by earlier test builds (their
// runes were in): made runeword records again, in their own runeword's tile.
void FixRunewordBases() {
	bool changed = false;
	for (auto& [fingerprint, record] : Records) {
		if (!record.base || record.bytes.size() < 4 || !((record.bytes[3] >> 2) & 1)) continue; // the runeword flag (bit 26)
		const auto code = CodeText(record.code);
		int        u    = -1;
		for (const auto& [row, nameId] : RunewordsInBytes(record.bytes, record.itemLevel, record.quality)) {
			if (const int v = RunewordFor(nameId, code); v >= 0 && RunewordEntry(Uniques[v].row, code) >= 0) u = v;
			else if (row >= 0) u = RunewordEntry(row, code);
			if (u >= 0) break;
		}
		Say(false, "grailon: %s stored as a base of %s is a runeword: %s", code.c_str(), record.id.c_str(), u >= 0 ? Uniques[u].id.c_str() : "not found, kept as a runeword of that tile");
		if (u >= 0) record.id = Uniques[u].id;
		record.base = false;
		changed     = true;
	}
	if (changed) SaveStore();
}

void RebuildStored() {
	++GrailChanges;
	if (!Uniques.empty()) FixRunewordBases();
	// Lost items: records no tile can show (no entry any more, not the item
	// their entry says, no tile of that kind, or a second copy of an item
	// that has one tile). Settings never make items lost: misc types not
	// tracked and copies of variants hidden while the Variants switches are
	// off stay stored, hidden.
	if (!Uniques.empty()) {
		std::vector<std::uint32_t>     lost;
		std::set<std::pair<int, bool>> taken;
		for (const auto& [fingerprint, record] : Records) {
			if (record.id.rfind("o:", 0) == 0) continue;
			const auto found = EntryById.find(record.id);
			const int  u     = found == EntryById.end() || Uniques[found->second].lost || Uniques[found->second].variant ? -1 : found->second;
			bool       gone  = u < 0 || Outdated.count(fingerprint) || !RecordMatches(record, u) || (!Uniques[u].misc && (record.eth ? !Uniques[u].eth : Uniques[u].onlyEth));
			if (!gone && !record.base && !Uniques[u].misc && Uniques[u].affixes.empty() && Uniques[u].pictures < 2) gone = !taken.insert({ u, record.eth }).second;
			if (gone) lost.push_back(fingerprint);
		}
		std::vector<std::uint32_t> before;
		for (const auto& [print, entry] : LostOf) before.push_back(print);
		if (lost != before) BuildLost(lost); // (both by fingerprint)
	}
	Stored.clear();
	StoredCodes.clear();
	Perfect.clear();
	BaseTiles.clear();
	Present.clear();
	for (const auto& [fingerprint, record] : Records) {
		MarkFound(record);
		const int u = RecordEntry(record);
		if (u < 0 || Stored.count({ u, record.eth })) continue; // the tile's first record (RecordAt's); the others stay hidden
		std::uint32_t id = 0;
		for (const auto& [runtime, print] : Materialized)
			if (print == fingerprint) id = runtime;
		Stored[{ u, record.eth }]      = id;
		StoredCodes[{ u, record.eth }] = CodeText(record.code);
		if (record.base) BaseTiles.insert({ u, record.eth });
		else if (record.perfect || Uniques[u].secret) Perfect.insert({ u, record.eth });
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
	return RecordEntry(record->second);
}

void SayOnce(bool& told, const char* format, unsigned value) {
	if (!told) Say(true, format, value);
	told = true;
}


bool FreeCell(D2RL::PlayerHandle player, std::uint32_t code, int& freeX, int& freeY);
bool PageMove(D2RL::Inventory::LocalPlayerPageMoveKind kind, int x, int y);
void Unbusy(std::uint32_t id);
bool PerfectNow(int u, std::uint32_t runtimeId, bool eth);
std::atomic<bool> ForgetWanted {}; // grailon forget, done by the next arrange
// Game thread: the character's pictures read once, as soon as the lists are
// ready (the loot filter reads ground items' pictures by what it learns there).
std::atomic<bool> PictureScanQueued {};
void __cdecl ScanPictures(const D2RL::PluginContext*, void*) noexcept {
	bool scan = false;
	{
		std::lock_guard guard(Lock);
		scan = !PicturesScanned && Config.graphicVariants && ListsReady;
		if (scan) PicturesScanned = true;
	}
	if (scan) ReadPictures(LocalPlayer.load(), true);
	PictureScanQueued = false;
}

void __cdecl Arrange(const D2RL::PluginContext* context, void* userData) noexcept {
	auto* job = static_cast<ArrangeJob*>(userData);
	Trace(job->open ? "arrange for view" : "arrange", job->tries);
	// First of all (before anything leaves the grail): what a crashed game took
	// out comes back, so its copy on the page is the record's again.
	RestoreTakenOut(context, job->player);
	GiveBackOrphans(job->player);
	{
		bool scan = false;
		{
			std::lock_guard guard(Lock);
			scan = !PicturesScanned && Config.graphicVariants && ListsReady;
			if (scan) PicturesScanned = true;
		}
		if (scan) ReadPictures(job->player, true); // once per character: what is on it
	}
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
	bool storage = false;
	{
		std::lock_guard guard(Lock);
		storage = StoreReady && !StorePath.empty() && StoreOwner == job->player;
	}
	(void)Inventory->forEachInventoryItem(context, job->player, &filter, Collect, &items);
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
				const auto record = Records.find(FingerprintOf(item));
				copy              = record != Records.end() && SameItem(record->second, item);
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
			// An item with a record's identity that is not the record's own (the
			// page as the character came in, or put there by the grail): a copy
			// made with an item editor, or a second one. It goes to the
			// inventory; nothing is destroyed.
			if (record != Records.end()
			    && (!SameItem(record->second, *item) || !(stale || Materialized.count(item->runtimeId)) || seen.count(step.fingerprint))) {
				static int toldTwin = 0;
				if (toldTwin++ < 8) Say(true, "grailon: %s has the same identity as an item already in the grail (copied with an item editor?): it goes to the inventory", CodeText(item->code).c_str());
				step.action = Action::Out;
				steps.push_back(step);
				continue;
			}
			if (record != Records.end()) {
				// A record's item: in its tile when shown, else away.
				seen.insert(step.fingerprint);
				{
					Materialized[item->runtimeId] = step.fingerprint;
					// Only its tile's own record (a hidden variant copy goes back to the file).
					const int entry = RecordEntry(record->second);
					if (entry >= 0 && !settings && RecordAt(entry, record->second.eth) == &record->second && place(entry, record->second.eth, CodeText(item->code), tile, x, y) && !taken[tile]) {
						taken[tile] = true;
						step.x = x, step.y = y;
						step.action = item->x == x && item->y == y ? Action::Stay : Action::Cell;
					} else step.action = settings ? Action::Stay : Action::Unload;
					if (entry >= 0) claimed.insert({ entry, record->second.eth });
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
				const int entry = RecordEntry(record);
				int       tile = -1, x = 0, y = 0;
				if (seen.count(fingerprint) || entry < 0 || RecordAt(entry, record.eth) != &record || !place(entry, record.eth, CodeText(record.code), tile, x, y) || taken[tile]) continue;
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

// Game thread: the pictures of the items graphic variants need, so a look
// from the UI thread (a tooltip, a middle click, a drop) finds the right tile.
constexpr std::uint32_t PictureContainers = Items::ContainerBit(Items::ItemContainer::Equipment) | Items::ContainerBit(Items::ItemContainer::Cursor)
                                          | Items::ContainerBit(Items::ItemContainer::Belt) | Items::ContainerBit(Items::ItemContainer::Inventory)
                                          | Items::ContainerBit(Items::ItemContainer::Cube) | Items::ContainerBit(Items::ItemContainer::PersonalStash)
                                          | Items::ContainerBit(Items::ItemContainer::SharedStash) | Items::ContainerBit(Items::ItemContainer::CustomPage)
                                          | Items::ContainerBit(Items::ItemContainer::Ground);
std::set<std::uint32_t> PictureWanted; // runtime ids; guarded by Lock
std::atomic<bool>       PictureQueued {};
void ReadPictures(D2RL::PlayerHandle player, bool all) {
	std::vector<Items::ItemInfo>      items;
	const D2RL::Inventory::ItemFilter filter { .structSize = D2RL::Inventory::ItemFilterSize, .containerMask = PictureContainers };
	if (!player || Inventory->forEachInventoryItem(Context, player, &filter, Collect, &items) != D2RL::Inventory::Result::Success) return;
	std::set<std::uint32_t> wanted;
	{
		std::lock_guard guard(Lock);
		wanted.swap(PictureWanted);
	}
	for (const auto& item : items) {
		{
			std::lock_guard guard(Lock);
			if (Pictures.count(item.runtimeId) || (!all && !wanted.count(item.runtimeId))) continue;
			const auto base = Bases.find(CodeText(item.code));
			if (base == Bases.end() || base->second.pictures < 2) continue;
		}
		Snapshot snapshot { .itemLevel = item.itemLevel, .quality = static_cast<std::uint32_t>(item.quality) };
		snapshot.code = item.code;
		if (ItemApi->editNativeItem(Context, item.handle, TakeSnapshot, &snapshot) == Items::Result::Success && snapshot.ok) {
			std::lock_guard guard(Lock);
			SetPicture(item.runtimeId, PictureInBytes(snapshot.bytes, snapshot.itemLevel, snapshot.quality));
			VariantUnits.erase(item.runtimeId);
		}
	}
}
void __cdecl ReadWantedPictures(const D2RL::PluginContext*, void*) noexcept {
	PictureQueued = false;
	ReadPictures(LocalPlayer.load(), false);
}
// Any thread, Lock held or not.
void NeedPicture(std::uint32_t runtimeId) {
	if (!runtimeId || !Threads) return;
	{
		std::lock_guard guard(Lock);
		if (!PictureWanted.insert(runtimeId).second && PictureQueued) return;
	}
	if (!PictureQueued.exchange(true) && Threads->runOnGameThread(Context, ReadWantedPictures, nullptr) != D2RL::Threads::Result::Success) PictureQueued = false;
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
			TakenOut[job->twin->fingerprint] = { *job->twin, StoreCharacter };
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
	if (!snapshot.ok || !snapshot.fingerprint || Records.count(snapshot.fingerprint)) return false;
	Record record { .id = Uniques[Uniques[u].variant ? Uniques[u].variantOf : u].id, .eth = eth, .perfect = PerfectNow(u, item.runtimeId, eth), .fingerprint = snapshot.fingerprint, .code = item.code,
		            .quality = static_cast<std::uint32_t>(item.quality), .itemLevel = item.itemLevel, .stateFlags = item.stateFlags, .qualityRecordId = item.qualityRecordId,
		            .bytes = std::move(snapshot.bytes) };
	Fingerprints[item.runtimeId] = record.fingerprint;
	Tombstones.erase(record.fingerprint);
	FoundIds.insert(record.id);
	TagVariant(record, ClientItem(item.runtimeId));
	if (record.variant.empty() && Uniques[u].variant && Uniques[u].affix >= 0 && Uniques[u].variantOf >= 0) record.variant = ChoiceKey(Uniques[Uniques[u].variantOf].affixes[Uniques[u].affix]);
	MarkFound(record);
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
void NoteUiPerfect(int u, std::uint32_t runtimeId, bool eth);
// Lock held. A grail item whose tile holds a stored copy: swapped with it.
bool QueueSwap(const Items::ItemInfo& item, int u, bool cursor) {
	const auto twin = StoreReady && !StorePath.empty() ? RecordAt(u, IsEth(item)) : nullptr;
	if (!twin || Materialized.count(item.runtimeId) || Busy.count(item.runtimeId)) return false; // no copy, or the item is the copy
	Busy.insert(item.runtimeId);
	NoteUiPerfect(u, item.runtimeId, IsEth(item));
	if (!QueueCursorJob(new CursorJob { .step = cursor ? CursorStep::SwapPut : CursorStep::SwapIn, .id = item.runtimeId, .cursor = cursor, .unique = u, .eth = IsEth(item) })) {
		Busy.erase(item.runtimeId);
		return false;
	}
	return true;
}

bool AssignBase(const Items::ItemInfo& item, bool commit, int target = -1);
bool BaseFitsHere(const Items::ItemInfo& item);
bool TakeCtrlClick(const Items::ItemInfo& item) {
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
		// A base: only on a tile it fits (hovering it there shows it can go in).
		if (click) {
			static int told = 0;
			if (told < 20 && item.quality != Items::Quality::Unique && item.quality != Items::Quality::Set && item.socketedItemCount == 0)
				++told, Say(false, "grailon: drop of %s (quality %u, %s) on the grail", CodeText(item.code).c_str(), static_cast<unsigned>(item.quality), ctrl ? "ctrl" : "click");
			(void)AssignBase(item, true);
		}
		else if (BaseFitsHere(item)) return Decision::Allow;
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
bool GameInFront() {
	const HWND front = GetForegroundWindow();
	DWORD      pid   = 0;
	return front && GetWindowThreadProcessId(front, &pid) && pid == GetCurrentProcessId();
}
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
void* ClientItem(std::uint32_t runtimeId) { return Base ? reinterpret_cast<GetUnitFn>(Base + Game::GetUnit)(runtimeId, Game::ItemUnit) : nullptr; }


int   RunewordFlagOn(std::uint32_t runtimeId) {
    if (!RunewordFlagTrusted.load()) return -1;
    const auto* unit = static_cast<const std::uint8_t*>(ClientItem(runtimeId));
    const auto* data = unit ? *reinterpret_cast<const std::uint8_t* const*>(unit + Game::UnitData) : nullptr;
    if (!data) return -1;
    return (*reinterpret_cast<const std::uint32_t*>(data + Game::DataFlags) & Game::RunewordFlag) ? 1 : 0;
}
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
// Items the current ladder season reworked (their row's firstLadderSeason is
// the newest season of the tables): every stat their row gives, with its
// lowest value. A copy below one of them is an older version.
// A stat a reworked row gives: its layer (-1: any), its range (summed over
// the props that give it), whether the value is checked or only its presence.
struct Need {
	int  stat = -1, layer = 0, low = 0, high = 0;
	bool value = true;
};
struct Rework {
	std::vector<Need> needs;   // what the copy must have
	std::vector<Need> bonuses; // a set item's own set bonuses (its lists with a state)
	std::set<int>     allowed; // every stat its props (and their choices) can give
};
std::map<std::pair<int, int>, Rework> ReworkChecks; // (0 unique, 1 set, 2 runeword; row); guarded by Lock
std::set<std::uint32_t>                          VersionChecked; // records whose ghost told whether they are older versions (this launch); guarded by Lock
std::vector<bool>              EdRolls; // the item has a variable Enhanced Defense roll
// The best defense stat (31) a copy can have, plain and ethereal: the base's
// maxac (+1 with Enhanced Defense, as the game rolls it), x1.5 when ethereal,
// plus the best flat defense. 0 when it has no defense. By index in Uniques.
std::vector<std::pair<int, int>> BestDefense;
// Lock held. The roll tables follow Uniques (lost entries appended).
void ResizeRolls() {
	if (!Rolls.empty()) Rolls.resize(Uniques.size()), EdRolls.resize(Uniques.size()), BestDefense.resize(Uniques.size());
}

// Lock held. The variant entries of the items whose variants are on (and,
// built again, of those made before, in `order`): made the first time they
// are wanted and kept, so no entry ever moves. After every item's entry,
// before the objectives and lost items (made again after them). True when
// some were made (their texts are registered again).
bool AddVariants(const std::vector<std::string>* order) {
	std::vector<int> wanted;
	std::set<int>    listed;
	const auto       want = [&](int b) {
        if (b < 0 || b >= UniqueCount()) return;
        const auto& entry = Uniques[b];
        if (entry.variant || entry.lost || entry.objective || !entry.variants.empty() || (entry.affixes.empty() && entry.pictures < 2)) return;
        if (listed.insert(b).second) wanted.push_back(b);
	};
	if (order)
		for (const auto& id : *order)
			if (const auto found = EntryById.find(id); found != EntryById.end()) want(found->second);
	for (int b = 0; b < UniqueCount(); ++b)
		if (AffixOn(b) || GraphicOn(b)) want(b);
	if (wanted.empty()) return false;
	bool tail = false;
	while (!Uniques.empty() && (Uniques.back().lost || Uniques.back().objective)) {
		EntryById.erase(Uniques.back().id);
		Uniques.pop_back();
		tail = true;
	}
	if (tail) LostOf.clear(); // made again by RebuildStored
	const bool rolls = !Rolls.empty();
	for (const int b : wanted) {
		auto made = VariantEntries(Uniques[b]);
		for (auto& variant : made) {
			const int v       = UniqueCount();
			variant.variantOf = b;
			Uniques[b].variants.push_back(v);
			EntryById[variant.id] = v;
			Uniques.push_back(std::move(variant));
			if (rolls) Rolls.push_back(Rolls[b]), EdRolls.push_back(EdRolls[b]), BestDefense.push_back(BestDefense[b]);
		}
	}
	if (tail) BuildObjectives();
	else UpdateMiscTracking();
	return true;
}

// Lock held. The objectives' entries, after every other (their indices are the
// last, so the others' never change), in their own category of the Objectives menu.
void BuildObjectives() {
	while (!Uniques.empty() && Uniques.back().lost) { // made again after them (RebuildStored)
		EntryById.erase(Uniques.back().id);
		Uniques.pop_back();
	}
	LostOf.clear();
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

// Every stat a row's props give: the ones the copy must have (with their
// range when the prop sets a plain value), and every one it may have.
void AddChecks(Rework& out, const unsigned char* row, int first, int count, const unsigned char* props, std::uint32_t propCount, std::uint32_t propSize, const PropLayout& layout,
               const unsigned char* groups, std::uint32_t groupCount, std::uint32_t groupSize, bool bonus = false) {
	const auto statsOf = [&](const unsigned char* text, std::vector<std::pair<int, int>>& out) { // (stat, func)
		for (int s = 0; s < 7; ++s) {
			if (layout.stat + 2 * s + 2 > static_cast<int>(propSize) || layout.func + s >= static_cast<int>(propSize)) break;
			const int stat = Field<std::uint16_t>(text, layout.stat + 2 * s), func = text[layout.func + s];
			if (s > 0 && !func) break;
			if (stat != 0xFFFF) out.push_back({ stat, func });
		}
	};
	const auto allow = [&](int prop) {
		if (prop < 0 || static_cast<std::uint32_t>(prop) >= propCount) return;
		const unsigned char* text = props + static_cast<std::size_t>(prop) * propSize;
		std::vector<std::pair<int, int>> stats;
		statsOf(text, stats);
		for (const auto& [stat, func] : stats) out.allowed.insert(stat);
		const int func = text[layout.func];
		if (func == 5) out.allowed.insert({ 21, 23, 159 });
		if (func == 6) out.allowed.insert({ 22, 24, 160 });
		if (func == 7) out.allowed.insert({ 17, 18 });
	};
	auto&      needs = bonus ? out.bonuses : out.needs;
	const auto need  = [&](int stat, int layer, int low, int high, bool value) {
		for (auto& known : needs)
			if (known.stat == stat && known.layer == layer && known.value == value) {
				known.low += low, known.high += high;
				return;
			}
		needs.push_back({ .stat = stat, .layer = layer, .low = std::min(low, high), .high = std::max(low, high), .value = value });
	};
	for (int p = 0; p < count; ++p) {
		const int at = first + p * 16;
		const int prop = Field<std::int32_t>(row, at), par = Field<std::int32_t>(row, at + 4), low = Field<std::int32_t>(row, at + 8), high = Field<std::int32_t>(row, at + 12);
		if (prop >= 0x10000) { // a property group: any of its choices may be there
			const int g = prop - 0x10000;
			if (!groups || g < 0 || static_cast<std::uint32_t>(g) >= groupCount || groupSize < 8 + 24) continue;
			const unsigned char* group = groups + static_cast<std::size_t>(g) * groupSize;
			for (int e = 0; e < std::min<int>(8, static_cast<int>((groupSize - 8) / 24)); ++e) allow(Field<std::int32_t>(group + 8 + e * 24, 0));
			continue;
		}
		if (prop < 0 || static_cast<std::uint32_t>(prop) >= propCount) continue;
		allow(prop);
		const unsigned char* text = props + static_cast<std::size_t>(prop) * propSize;
		std::vector<std::pair<int, int>> stats;
		statsOf(text, stats);
		const int func = text[layout.func];
		if (func == 7) need(17, 0, low, high, true), need(18, 0, low, high, true);
		else if (func == 21 && !stats.empty()) need(stats[0].first, Field<std::uint16_t>(text, layout.value), low, high, true);
		else if (func == 22 && !stats.empty()) need(stats[0].first, par, low, high, true);
		else if (func == 10 && !stats.empty()) need(stats[0].first, (par / 3) * 8 + par % 3, low, high, true);
		else if (func == 11 || func == 19) {
			if (!stats.empty()) need(stats[0].first, -1, 0, 0, false); // there, its value not checked
		} else
			// Each stat by its own function: the value (1, 3), the min (15), the max (16), per level (17: there).
			for (const auto& [stat, f] : stats) {
				if (f == 1 || f == 3) need(stat, 0, low, high, true);
				else if (f == 15) need(stat, 0, low, low, true);
				else if (f == 16) need(stat, 0, high, high, true);
				else if (f == 17) need(stat, -1, 0, 0, false);
			}
	}
	// Defense, Enhanced Defense and sockets also come from the base or the smith.
	std::erase_if(out.bonuses, [](const Need& n) { return n.stat == 31 || n.stat == 16 || n.stat == SocketsStat; });
	std::erase_if(out.needs, [](const Need& n) { return n.stat == 31 || n.stat == 16 || n.stat == SocketsStat; });
	out.allowed.insert({ 31, 16, SocketsStat, 21, 22, 23, 24, 159, 160 }); // the weapon damage stats come with the base's damage too
}

// Each row's firstLadderSeason in a game table (0 for none), by compiled
// row: the "Expansion" divider lines are not rows of the compiled table.
std::vector<int> FirstSeasons(const char* path) {
	std::string file = Gui::GameFile(path);
	std::vector<int> out;
	int              column = -1;
	bool             head   = true;
	for (std::size_t at = 0; at < file.size();) {
		auto end = file.find('\n', at);
		if (end == std::string::npos) end = file.size();
		const std::string line = file.substr(at, end - at);
		at                     = end + 1;
		if (line.empty() || line == "\r") continue;
		std::size_t from = 0;
		if (!head && line.rfind("Expansion\t", 0) == 0) continue;
		int         k    = 0, value = 0;
		for (;; ++k) {
			const auto  tab  = line.find('\t', from);
			std::string cell = line.substr(from, tab == std::string::npos ? std::string::npos : tab - from);
			while (!cell.empty() && (cell.back() == '\r' || cell.back() == ' ')) cell.pop_back();
			if (head && cell == "firstLadderSeason") column = k;
			if (!head && k == column) value = std::atoi(cell.c_str());
			if (tab == std::string::npos) break;
			from = tab + 1;
		}
		if (!head) out.push_back(value);
		head = false;
		if (column < 0) return {};
	}
	return out;
}

int Stat(void* unit, int stat, int layer);
int Display(int stat, int value);
template <typename T>
T At(const void* base, std::ptrdiff_t offset);
// An item's own magic stats: (stat, layer) -> value, from the lists RawStat
// reads (magic properties, no state). Empty when it has none.
// `which`: 0 its magic properties, 1 every list (runewords keep theirs apart), 2 its set bonuses (magic lists with a state).
std::map<std::pair<int, int>, int> OwnStats(const void* unit, int which = 0) {
	std::map<std::pair<int, int>, int> out;
	const auto* ex = unit ? At<const std::uint8_t*>(unit, 0x88) : nullptr;
	if (!ex || At<std::int32_t>(ex, 0x1C) >= 0) return out;
	for (const std::ptrdiff_t chain : { 0x90, 0x98 }) {
		int guard = 0;
		for (const auto* list = At<const std::uint8_t*>(ex, chain); list && guard < 32; list = At<const std::uint8_t*>(list, 0x68), ++guard) {
			const auto  flags = At<std::uint32_t>(list, 0x1C), state = At<std::uint32_t>(list, 0x20);
			const auto* entries = At<const std::uint8_t*>(list, 0x30);
			const auto  count   = At<std::uint64_t>(list, 0x38);
			const bool take = which == 0 ? state == 0 && (flags & 0x40) : which == 1 ? state == 0 && ((flags & 0x40) || static_cast<std::int32_t>(flags) >= 0) : state != 0 && (flags & 0x40);
			if (!take || !entries || count > 512) continue;
			for (std::uint64_t i = 0; i < count; ++i) {
				const auto* entry = entries + i * 16;
				out[{ static_cast<int>(At<std::uint32_t>(entry, 4)), static_cast<int>(At<std::uint32_t>(entry, 0)) }] += At<std::int32_t>(entry, 8);
			}
		}
	}
	return out;
}

// Lock held. Whether `unit` (entry `u`) is not its item as the current
// season's row makes it: a stat it must have is missing or out of its range,
// or it has a stat the row cannot give. With socketed items in it (their
// stats count too), only missing stats and too low values tell.
bool OldVersion(int u, void* unit) {
	if (u < 0 || u >= UniqueCount() || !unit) return false;
	const auto& entry = Uniques[u];
	const auto  found = ReworkChecks.find({ entry.runeword ? 2 : entry.set ? 1 : 0, entry.row });
	if (found == ReworkChecks.end()) return false;
	const auto own = OwnStats(unit, entry.runeword ? 1 : 0);
	if (own.empty()) {
		static int empty = 0;
		if (empty++ < 10) Say(false, "grailon: %s: no stat list read, not judged", entry.name.c_str());
		return false;
	}
	const bool socketed = entry.runeword || Stat(unit, SocketsStat, 0) > 0 || own.count({ SocketsStat, 0 });
	static int told = 0;
	const auto old = [&](const char* why, int stat, int layer, int value, int low, int high) {
		if (told++ < 40) Say(false, "grailon: %s: stat %d (layer %d) %s (%d; this season's row: %d-%d)", entry.name.c_str(), stat, layer, why, value, low, high);
		return true;
	};
	for (const auto& need : found->second.needs) {
		int  value = 0;
		bool there = false;
		for (const auto& [key, v] : own)
			if (key.first == need.stat && (need.layer < 0 || key.second == need.layer)) value += v, there = true;
		if (!there) return old("missing", need.stat, need.layer, 0, need.low, need.high);
		if (!need.value) continue;
		value = Display(need.stat, value);
		if (value < need.low || (!socketed && value > need.high)) return old("out of range", need.stat, need.layer, value, need.low, need.high);
	}
	if (!socketed)
		for (const auto& [key, v] : own)
			if (!found->second.allowed.count(key.first)) return old("not given by its row", key.first, key.second, v, 0, 0);
	// A set item's own bonuses, when its bonus lists are there.
	if (!found->second.bonuses.empty())
		if (const auto bonus = OwnStats(unit, 2); !bonus.empty()) {
			for (const auto& need : found->second.bonuses) {
				int  value = 0;
				bool there = false;
				for (const auto& [key, v] : bonus)
					if (key.first == need.stat && (need.layer < 0 || key.second == need.layer)) value += v, there = true;
				if (!there) return old("missing from its set bonuses", need.stat, need.layer, 0, need.low, need.high);
				if (need.value && (Display(need.stat, value) < need.low || Display(need.stat, value) > need.high)) return old("out of range in its set bonuses", need.stat, need.layer, Display(need.stat, value), need.low, need.high);
			}
			std::set<int> given;
			for (const auto& need : found->second.bonuses) given.insert(need.stat);
			for (const auto& [key, v] : bonus)
				if (!given.count(key.first) && !found->second.allowed.count(key.first)) return old("not given by its set bonuses", key.first, key.second, v, 0, 0);
		} else {
			static int none = 0;
			if (none++ < 5) Say(false, "grailon: %s: no set bonus list read, its set bonuses not judged", entry.name.c_str());
		}
	return false;
}

void BuildRolls(const D2RL::PluginContext* context) {
	const std::vector<int> seasons[3] { FirstSeasons("data/global/excel/uniqueitems.txt"), FirstSeasons("data/global/excel/setitems.txt"), FirstSeasons("data/global/excel/runes.txt") };
	int season = 0;
	for (const auto& table : seasons)
		for (const int first : table) season = std::max(season, first);
	Tables::TableView props { .structSize = Tables::TableViewSize }, uniques { .structSize = Tables::TableViewSize }, sets { .structSize = Tables::TableViewSize },
	    runes { .structSize = Tables::TableViewSize };
	if (TableApi) (void)TableApi->getTable(context, Tables::Bank::Rotw, Tables::TableId::Runes, &runes);
	Tables::TableView groupView { .structSize = Tables::TableViewSize };
	if (TableApi && TableApi->getTable(context, Tables::Bank::Rotw, Tables::TableId::PropertyGroups, &groupView) != Tables::Result::Success) groupView.rows = nullptr;
	const auto* groupRows = static_cast<const unsigned char*>(groupView.rows);
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
	ReworkChecks.clear();
	VersionChecked.clear(), Outdated.clear(), OldUnits.clear();
	for (std::size_t u = 0; u < Uniques.size(); ++u) {
		const auto& entry = Uniques[u];
		const auto& table = entry.runeword ? runes : entry.set ? sets : uniques;
		if (entry.row < 0 || static_cast<std::uint32_t>(entry.row) >= table.rowCount || !table.rows) continue;
		const auto* row = static_cast<const unsigned char*>(table.rows) + static_cast<std::size_t>(entry.row) * table.rowSize;
		bool        ed  = false;
		// Reworked this season: its stats, to tell older copies.
		if (const int kind = entry.runeword ? 2 : entry.set ? 1 : 0; season > 0 && !entry.misc && entry.row < static_cast<int>(seasons[kind].size()) && seasons[kind][entry.row] == season
		    && !ReworkChecks.count({ kind, entry.row })) {
			auto& checks = ReworkChecks[{ kind, entry.row }];
			if (entry.runeword) {
				if (RuneFields.props >= 0) AddChecks(checks, row, RuneFields.props, RuneLayout::PropCount, rows, props.rowCount, props.rowSize, layout, groupRows, groupView.rowCount, groupView.rowSize);
			} else AddChecks(checks, row, 152, entry.set ? 9 : 12, rows, props.rowCount, props.rowSize, layout, groupRows, groupView.rowCount, groupView.rowSize);
			// A set item's own set bonuses (aprop1a-5b, after its 9 props).
			if (entry.set && sets.rowSize >= 296 + 10 * 16) AddChecks(checks, row, 296, 10, rows, props.rowCount, props.rowSize, layout, groupRows, groupView.rowCount, groupView.rowSize, true);
		}
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
	if (season > 0) Say(false, "grailon: ladder season %d reworked %zu items: older copies of them are not taken (set rows of %u bytes)", season, ReworkChecks.size(), sets.rowSize);
	else Say(false, "grailon: no firstLadderSeason column read (%zu/%zu/%zu rows)", seasons[0].size(), seasons[1].size(), seasons[2].size());
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

// Lock held. One of the player's items' picture: kept, and a sample for PictureOffset.
void SetPicture(std::uint32_t runtimeId, int picture) {
	Pictures[runtimeId] = picture;
	if (picture < 0 || Learning.confirmed || Learning.failed || !Learning.items.insert(runtimeId).second) return;
	const void* unit = ClientItem(runtimeId);
	const auto* data = unit ? At<const std::uint8_t*>(unit, Game::UnitData) : nullptr;
	if (!data) return;
	for (int i = 0; i < PictureScan; ++i)
		if (data[i] != picture) Learning.out.set(i);
	Learning.seen.insert(picture);
	if (const int at = PictureOffset; at >= 0 && Learning.out.test(at)) {
		PictureOffset = -1;
		Say(true, "grailon: an item's picture is not at item data +0x%X: Filter graphic variants waits until its place is learnt", at);
	}
	if (Learning.out.all()) {
		Learning.failed = true;
		Say(true, "grailon: no byte of the item data holds the picture (%zu items): Filter graphic variants cannot read ground items' pictures", Learning.items.size());
		return;
	}
	if (Learning.items.size() < 6 || Learning.seen.size() < 3 || Learning.out.count() != PictureScan - 1) return;
	Learning.confirmed = true;
	for (int i = 0; i < PictureScan; ++i)
		if (!Learning.out.test(i)) PictureOffset = i;
	Say(false, "grailon: items keep their picture at item data +0x%X (learnt from %zu items, %zu pictures)", PictureOffset.load(), Learning.items.size(), Learning.seen.size());
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
	// Its variant: its choice on it, its picture as last read.
	const int b = (*byRow)[file];
	if (b < 0 || Uniques[b].variants.empty()) return b;
	const auto picture = Pictures.find(At<std::uint32_t>(item, Game::UnitId));
	if (picture == Pictures.end() && GraphicOn(b)) NeedPicture(At<std::uint32_t>(item, Game::UnitId)); // read on the game thread: the next look has it
	const int v = VariantOf(b, AffixIndex(b, item), picture == Pictures.end() ? -1 : picture->second);
	return v < 0 ? b : v;
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
	if (!Gui::WindowHook::GameWindow() || !ViewIsOpen || Anim.running || !Context) return;
	POINT mouse {};
	if (!GetCursorPos(&mouse) || !ScreenToClient(Gui::WindowHook::GameWindow(), &mouse)) return;
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
	// for the call, and the copy's place is its tile (Gui::TileGrid::PeekAt).
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
	Gui::TileGrid::PeekAt(true, cell);
	PeekShown = copy;
	ShowTooltip(widget, copy);
	Gui::TileGrid::PeekAt(false);
	flags[Game::Compare] = compare;
	std::memcpy(slot, saved, sizeof(saved));
}

void* LocalUnit() { return reinterpret_cast<PlayerFn>(Base + Game::Player)(reinterpret_cast<IndexFn>(Base + Game::PlayerIndex)()); }

int Stat(void* unit, int stat, int layer) { return ReadStat && unit ? ReadStat(unit, stat, layer) : 0; }

// Lock held. A plain or superior base (not a runeword) held over the grail:
// it goes in only when dropped on an empty runeword tile it can be made
// into, with exactly that runeword's socket count (stat 194). The tile under
// the mouse (or `target`, a tile shown as a picture that was clicked).
// `commit` (the drop's click): the base is kept for it.
bool AssignBase(const Items::ItemInfo& item, bool commit, int target) {
	static int told = 0;
	const auto code = CodeText(item.code);
	const auto no   = [&](const char* why) {
		if (commit && told < 20) ++told, Say(false, "grailon: base %s (quality %u) not taken: %s", code.c_str(), static_cast<unsigned>(item.quality), why);
		return false;
	};
	if (item.quality != Items::Quality::Normal && item.quality != Items::Quality::Superior) return no("not a normal or superior item");
	if (BaseFor.count(item.runtimeId) || StoredBaseEntry(item.runtimeId) >= 0) return no("already a base");
	if (RunewordFlagOn(item.runtimeId) == 1) return no("runeword flag");
	if (!Bases.count(code)) return no("unknown base");
	void*     unit    = reinterpret_cast<GetUnitFn>(Base + Game::GetUnit)(item.runtimeId, Game::ItemUnit);
	const int sockets = Stat(unit, 194, 0);
	if (sockets <= 0) return no(unit ? "no sockets" : "no client unit");
	const int row = RunewordRowOf(item);
	if (row != -1) { // a runeword, or not known yet
		if (commit && told < 20) ++told, Say(false, "grailon: base %s not taken: runeword row %d", code.c_str(), row);
		return false;
	}
	const auto fits = [&](int u) {
		return u >= 0 && u < UniqueCount() && Uniques[u].runeword && static_cast<int>(Uniques[u].runes.size()) == sockets && !Stored.count({ u, false })
		    && RunewordEntry(Uniques[u].row, code) == u;
	};
	int pick = -1, under = target;
	if (target >= 0 && fits(target)) pick = target;
	const auto cell = target >= 0 ? ~std::uint64_t { 0 } : Gui::TileGrid::HoverCell();
	if (cell != ~std::uint64_t { 0 }) {
		const int x = static_cast<std::int32_t>(cell) - OriginX(), y = static_cast<std::int32_t>(cell >> 32) - OriginY();
		for (const auto& tile : Shown)
			if (x >= tile.x && x < tile.x + std::max(tile.width, SlotWidth) && y >= tile.y && y < tile.y + tile.height) {
				under = tile.unique;
				if (fits(tile.unique)) pick = tile.unique;
			}
	}
	if (commit && told < 20) {
		++told;
		Say(false, "grailon: base %s (%d sockets) dropped on %s: %s", code.c_str(), sockets, under >= 0 ? Uniques[under].name.c_str() : cell == ~std::uint64_t { 0 } ? "no cell" : "no tile",
		    pick >= 0 ? (target >= 0 ? "kept (picture tile)" : "kept") : under < 0 ? "refused" : Uniques[under].runeword ? (Stored.count({ under, false }) ? "refused, the tile is full" : "refused, not that runeword's base or sockets") : "refused, not a runeword tile");
	}
	if (pick < 0) return false;
	BaseFor[item.runtimeId] = pick;
	return true;
}
// Lock held. Whether the held base would go in where it is (nothing kept).
bool BaseFitsHere(const Items::ItemInfo& item) {
	if (!AssignBase(item, false)) return false;
	BaseFor.erase(item.runtimeId);
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
	static std::map<std::tuple<void*, int, std::uint64_t, std::uint64_t>, TextRolls> known;
	if (!copy) return {};
	const auto key = std::tuple { copy, entry, GrailChanges, GhostEpoch.load() };
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
	if (item && item == PeekShown && !Gui::TileGrid::Peeking()) return;
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
			if (entry >= 0 && !Uniques[entry].variants.empty() && !HasTile(entry, eth)) seen = 0; // its variant not known yet: looked at again
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
	std::uint8_t check {};  // the same bits: shown only when an upgrade of its grail copy (Filter unidentified stats, Hide perfect)
	std::vector<std::uint8_t> pictures; // Filter graphic variants: the hide bits by the drop's picture (empty: `hide` for all)
	bool operator==(const LootRow&) const = default;
};
struct LootTable {
	std::vector<LootRow> uniques, sets; // by UniqueItems / SetItems row
	// By Items row: hidden whatever unique (set item) of that base it is, for
	// unidentified items when ambiguous bases are not filtered.
	std::vector<std::uint8_t> uniqueBases, setBases;
	// The same by picture (Filter graphic variants): [Items row][picture], empty when not filtered so.
	std::vector<std::vector<std::uint8_t>> uniqueBasePics, setBasePics;
	bool                      exact {};   // unidentified items by their row
	bool                      unidOnly {}; // identified items are never hidden
	bool                      unidStats {}; // unidentified items as good as their grail copy show
	bool operator==(const LootTable&) const = default;
};
std::atomic<std::shared_ptr<const LootTable>> LootRows;
std::atomic<bool>                             LootOn {};
VisibleFn                                     LootBefore[std::size(Loot::Sites)] {};
std::uint8_t*                                 LootSite[std::size(Loot::Sites)] {};
bool                                          LootTried {};


// ---- unidentified drops against their grail copy (game thread) ----
// The game sends no stats of an unidentified item to the client: the server's
// copy has them (in process in single player and in games this player hosts).
// A ground item whose tile asks for it (Filter unidentified stats under Hide
// perfect) is looked up there once and compared with its grail copy's ghost;
// shown only when it beats it, hidden while unknown.
std::mutex                                UpgradeLock;
std::map<std::uint32_t, int>              UnidUpgrade;  // ground item id -> 1 an upgrade of its grail copy, 0 not (or not known), -1 asked; guarded by UpgradeLock
std::vector<std::uint32_t>                UpgradeAsked; // guarded by UpgradeLock
std::atomic<bool>                         UpgradeWanted {}, UpgradeRunning {};
std::map<std::uint32_t, std::vector<int>> CopyValues;   // a record's roll values, by fingerprint; game thread only

// A stat as the item's own magic stat lists hold it (Compare Indicators reads
// them the same way): the unit's stat list at +0x88 (flags at +0x1C negative),
// its lists chained from +0x90 and +0x98 by +0x68, each with its flags at +0x1C
// (0x40: magic properties), state at +0x20 and stats at +0x30 {pointer, count}
// of 16 bytes {u32 layer, u32 stat, i32 value, u32}. The game's reader gives 0
// for Enhanced Damage and Enhanced Defense; these lists keep them.
int RawStat(const void* unit, int stat, int layer) {
	const auto* ex = unit ? At<const std::uint8_t*>(unit, 0x88) : nullptr;
	if (!ex || At<std::int32_t>(ex, 0x1C) >= 0) return 0;
	const auto sum = [&](bool magicOnly, bool& found) {
		int total = 0;
		for (const std::ptrdiff_t chain : { 0x90, 0x98 }) {
			int guard = 0;
			for (const auto* list = At<const std::uint8_t*>(ex, chain); list && guard < 32; list = At<const std::uint8_t*>(list, 0x68), ++guard) {
				const auto  flags   = At<std::uint32_t>(list, 0x1C), state = At<std::uint32_t>(list, 0x20);
				const auto* entries = At<const std::uint8_t*>(list, 0x30);
				const auto  count   = At<std::uint64_t>(list, 0x38);
				if (state != 0 || (magicOnly && !(flags & 0x40)) || (!magicOnly && static_cast<std::int32_t>(flags) < 0) || !entries || count > 512) continue;
				for (std::uint64_t i = 0; i < count; ++i) {
					const auto* entry = entries + i * 16;
					if (At<std::uint32_t>(entry, 4) == static_cast<std::uint32_t>(stat) && At<std::uint32_t>(entry, 0) == static_cast<std::uint32_t>(layer)) total += At<std::int32_t>(entry, 8), found = true;
				}
			}
		}
		return total;
	};
	bool found = false;
	const int magic = sum(true, found);
	if (found) return magic;
	return sum(false, found);
}

// Lock held. Each varying roll's value on `unit`: its defense first (0 when
// it has none), then Rolls[u]'s, in order.
std::vector<int> RollValues(int u, void* unit, bool eth) {
	std::vector<int> out;
	if (u < 0 || u >= static_cast<int>(Rolls.size()) || !unit) return out;
	out.push_back(BestDefenseOf(u, eth) > 0 ? Stat(unit, DefenseStat, 0) : 0);
	for (const auto& roll : Rolls[u]) {
		const int stat = roll.stats[0];
		out.push_back(stat == DefenseStat               ? 0
		              : stat == 16 || stat == 17 || stat == 18 ? RawStat(unit, stat, roll.layer)
		                                                       : Display(stat, Stat(unit, stat, roll.layer)));
	}
	return out;
}

// Lock held. Whether `drop` beats `copy`: every varying roll the same or
// better, and at least one better.
bool Upgrade(int u, const std::vector<int>& drop, const std::vector<int>& copy) {
	if (drop.empty() || drop.size() != copy.size() || drop.size() != Rolls[u].size() + 1) return false;
	if (drop[0] < copy[0]) return false;
	bool better = drop[0] > copy[0];
	for (std::size_t r = 0; r < Rolls[u].size(); ++r) {
		const auto& roll   = Rolls[u][r];
		const bool  higher = roll.best >= roll.worst;
		const int   a = drop[r + 1], b = copy[r + 1];
		if (higher ? a < b : a > b) return false;
		better = better || a != b;
	}
	return better;
}

std::string ValuesText(const std::vector<int>& values) {
	std::string out;
	for (const int value : values) out += (out.empty() ? "" : " ") + std::to_string(value);
	return out;
}

void __cdecl CheckUpgrades(const D2RL::PluginContext*, void*) noexcept {
	std::vector<std::uint32_t> ids;
	{
		std::lock_guard guard(UpgradeLock);
		ids.swap(UpgradeAsked);
	}
	const auto player = LocalPlayer.load();
	void*      game   = ServerGame.load();
	if (!game && player && !Remote() && !ids.empty()) {
		std::uint32_t code = 0;
		{
			std::lock_guard guard(Lock);
			if (!Records.empty()) code = Records.begin()->second.code;
		}
		if (code) game = LearnGame(player, code);
	}
	static int told = 0;
	std::vector<std::pair<std::uint32_t, int>> results;
	for (const auto id : ids) {
		int   result = 0;
		void* unit   = game && UnitById ? UnitById(game, Game::ItemUnit, id) : nullptr;
		const void* data = unit ? At<const void*>(unit, Game::UnitData) : nullptr;
		if (data) {
			// Which entry it is, and its grail copy.
			int                       u = -1;
			bool                      eth = false, tileEth = false, known = false;
			std::uint32_t             print = 0;
			std::vector<std::uint8_t> bytes;
			{
				std::lock_guard guard(Lock);
				const auto      quality = At<std::uint32_t>(data, Game::DataQuality);
				const int       file    = At<std::int32_t>(data, Game::DataFile);
				const auto&     byRow   = quality == Game::UniqueQuality ? UniqueByRow : SetByRow;
				if ((quality == Game::UniqueQuality || quality == Game::SetQuality) && file >= 0 && file < static_cast<int>(byRow.size())) u = byRow[file];
				if (u >= 0 && !Uniques[u].variants.empty()) {
					const auto picture = Pictures.find(id);
					if (const int v = VariantOf(u, AffixIndex(u, unit), picture == Pictures.end() ? -1 : picture->second); v >= 0) u = v;
				}
				eth     = (At<std::uint32_t>(data, Game::DataFlags) & Game::Ethereal) != 0;
				tileEth = u >= 0 && HasTile(u, eth) ? eth : !eth;
				if (const Record* record = u >= 0 ? RecordAt(u, tileEth) : nullptr) print = record->fingerprint, bytes = record->bytes, known = CopyValues.count(print) != 0;
			}
			if (print && !known) {
				void* copy = GhostFromBytes(game, bytes);
				{
					std::lock_guard guard(Lock);
					CopyValues[print] = RollValues(u, copy, tileEth);
				}
				if (copy) FreeGhost(game, copy);
			}
			if (print) {
				std::lock_guard guard(Lock);
				const auto      drop = RollValues(u, unit, eth);
				const auto&     copy = CopyValues[print];
				result               = Upgrade(u, drop, copy) ? 1 : 0;
				if (told < 12) {
					++told;
					Say(false, "grailon: unidentified %s%s on the ground: rolls %s, grail copy %s (defense first): %s", Uniques[u].name.c_str(), eth ? " (ethereal)" : "", ValuesText(drop).c_str(),
					    ValuesText(copy).c_str(), result ? "an upgrade, shown" : "no upgrade, hidden");
				}
			}
		} else if (told < 12) {
			++told;
			Say(false, "grailon: unidentified item %u: no server copy here (%s); hidden as its grail tile says", id, game ? "not found" : "no game");
		}
		results.push_back({ id, result });
	}
	{
		std::lock_guard guard(UpgradeLock);
		for (const auto& [id, result] : results) UnidUpgrade[id] = result;
	}
	UpgradeRunning = false;
}

// The first decisions by picture, logged.
void LootTrace(int item, int file, int picture, bool hidden) noexcept {
	static std::atomic<int>           told {};
	static std::atomic<std::uint64_t> last {};
	const std::uint64_t key = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(item)) << 32) ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(file)) << 8) ^ static_cast<std::uint64_t>(picture & 0xFF) ^ (hidden ? 1ULL << 63 : 0);
	if (last.exchange(key) == key) return;
	if (told.fetch_add(1) < 12) Say(false, "grailon: loot filter by picture: base row %d, %s %d, picture %d: %s", item, file >= 0 ? "item row" : "unidentified, any of its base", file, picture, hidden ? "hidden" : "shown");
}

bool LootDecide(const void* unit, const void* data, const LootTable& table, std::uint32_t quality, int& file) noexcept {
	const auto* rows = quality == Game::UniqueQuality ? &table.uniques : &table.sets;
	const auto flags = At<std::uint32_t>(data, Game::DataFlags);
	const auto item  = static_cast<int>(At<std::uint32_t>(unit, Loot::UnitClass));
	const int  bit   = (flags & Game::Ethereal) ? 2 : 1;
	file             = -1;
	if ((flags & Loot::Identified) && table.unidOnly) return false;
	// Its picture, when Filter graphic variants reads it (-1: not read).
	const int  at      = PictureOffset.load(std::memory_order_relaxed);
	const int  picture = at >= 0 ? At<std::uint8_t>(data, at) : -1;
	// Unidentified, filtered as its base only: hidden when every item of that base would be.
	if (!(flags & Loot::Identified) && !table.exact) {
		const auto& pics = quality == Game::UniqueQuality ? table.uniqueBasePics : table.setBasePics;
		if (picture >= 0 && item >= 0 && item < static_cast<int>(pics.size()) && !pics[item].empty()) {
			const bool hidden = picture < static_cast<int>(pics[item].size()) && (pics[item][picture] & bit) != 0;
			LootTrace(item, -1, picture, hidden);
			return hidden;
		}
		const auto& bases = quality == Game::UniqueQuality ? table.uniqueBases : table.setBases;
		return item >= 0 && item < static_cast<int>(bases.size()) && (bases[item] & bit) != 0;
	}
	file = At<std::int32_t>(data, Game::DataFile);
	if (file < 0 || file >= static_cast<int>(rows->size())) return false;
	const LootRow& row = (*rows)[file];
	// An unidentified item's row is only trusted when its base is the row's.
	if (row.item != item) return false;
	// Filter graphic variants: by its own picture's tiles.
	if (!row.pictures.empty() && picture >= 0) {
		const bool hidden = picture < static_cast<int>(row.pictures.size()) && (row.pictures[picture] & bit) != 0;
		LootTrace(item, file, picture, hidden);
		return hidden;
	}
	return (row.hide & bit) != 0;
}

bool LootHidden(const void* unit) noexcept {
	if (!LootOn.load(std::memory_order_relaxed) || !unit || At<std::uint32_t>(unit, 0) != Game::ItemUnit) return false;
	const void* data = At<const void*>(unit, Game::UnitData);
	if (!data) return false;
	const auto table = LootRows.load(std::memory_order_acquire);
	if (!table) return false;
	const auto quality = At<std::uint32_t>(data, Game::DataQuality);
	if (quality != Game::UniqueQuality && quality != Game::SetQuality) return false;
	int  file   = -1;
	bool hidden = LootDecide(unit, data, *table, quality, file);
	// Filter unidentified stats, Hide perfect, a grail copy not perfect: shown
	// only when it beats that copy (asked once; hidden until known).
	const auto flags = At<std::uint32_t>(data, Game::DataFlags);
	if (!hidden && table->unidStats && !((flags & Loot::Identified) && table->unidOnly)) {
		const auto* rows = quality == Game::UniqueQuality ? &table->uniques : &table->sets;
		const int   row  = At<std::int32_t>(data, Game::DataFile);
		const int   bit  = (flags & Game::Ethereal) ? 2 : 1;
		if (row >= 0 && row < static_cast<int>(rows->size()) && ((*rows)[row].check & bit) && (*rows)[row].item == static_cast<int>(At<std::uint32_t>(unit, Loot::UnitClass))) {
			const auto       id = At<std::uint32_t>(unit, Game::UnitId);
			std::unique_lock lock(UpgradeLock, std::try_to_lock);
			hidden = true;
			if (lock.owns_lock()) {
				if (const auto known = UnidUpgrade.find(id); known == UnidUpgrade.end()) UnidUpgrade[id] = -1, UpgradeAsked.push_back(id), UpgradeWanted = true;
				else if (known->second == 1) hidden = false;
			}
		}
	}
	return hidden;
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
		if (Excluded(u, eth)) return false; // the player's choice: never hidden
		const std::pair key { u, eth };
		return policy == LootFound ? Stored.count(key) && !BaseTiles.count(key) : policy == LootPerfect && Perfect.count(key);
	};
	// Filter unidentified stats under Hide perfect: a copy in the grail, not
	// perfect, lets through only drops that beat it.
	const auto checks = [&](int u, bool eth, int policy) {
		const std::pair key { u, eth };
		return Config.lootUnidStats && policy == LootPerfect && !Excluded(u, eth) && Stored.count(key) && !BaseTiles.count(key) && !Perfect.count(key);
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
		row.check = (HasTile(u, false) && checks(u, false, Config.loot[0]) ? 1 : 0) | (HasTile(u, true) && checks(u, true, Config.loot[1]) ? 2 : 0);
		// Variants with tiles: hidden once every one is (the drop's is not known here).
		if (!Uniques[u].variants.empty() && !HasTile(u, false) && !HasTile(u, true)) {
			int all = 3;
			for (const int v : Uniques[u].variants)
				if (HasTile(v, false) || HasTile(v, true))
					all &= (!HasTile(v, false) || hides(v, false, Config.loot[0]) ? 1 : 0) | (!HasTile(v, true) || hides(v, true, Config.loot[1]) ? 2 : 0);
			row.hide  = (BaseHasTile(u, false) ? all & 1 : 0) | (BaseHasTile(u, true) ? all & 2 : 0);
			row.check = 0;
			// By picture: the variants of that picture (and those of any picture).
			if (Config.lootGraphic && Config.graphicVariants && Uniques[u].pictures > 1) {
				row.pictures.assign(static_cast<std::size_t>(Uniques[u].pictures), 0);
				for (int p = 0; p < Uniques[u].pictures; ++p) {
					int  bits = 3;
					bool any  = false;
					for (const int v : Uniques[u].variants)
						if ((Uniques[v].picture == p || Uniques[v].picture < 0) && (HasTile(v, false) || HasTile(v, true)))
							any = true, bits &= (!HasTile(v, false) || hides(v, false, Config.loot[0]) ? 1 : 0) | (!HasTile(v, true) || hides(v, true, Config.loot[1]) ? 2 : 0);
					row.pictures[p] = static_cast<std::uint8_t>(any ? (BaseHasTile(u, false) ? bits & 1 : 0) | (BaseHasTile(u, true) ? bits & 2 : 0) : 0);
				}
			}
		}
	}
	// A set item has one tile, ethereal or not.
	table->sets.resize(SetByRow.size());
	for (std::size_t r = 0; r < SetByRow.size(); ++r) {
		const int u = SetByRow[r];
		if (u < 0 || u >= UniqueCount()) continue;
		auto& row = table->sets[r];
		row.item  = itemRow(u);
		row.hide  = hides(u, false, Config.loot[2]) || hides(u, true, Config.loot[2]) ? 3 : 0;
		row.check = checks(u, false, Config.loot[2]) ? 3 : 0;
		if (!Uniques[u].variants.empty() && !HasTile(u, false)) {
			bool all = true, any = false;
			for (const int v : Uniques[u].variants)
				if (HasTile(v, false)) any = true, all = all && hides(v, false, Config.loot[2]);
			row.hide  = any && all ? 3 : 0;
			row.check = 0;
			if (Config.lootGraphic && Config.graphicVariants && Uniques[u].pictures > 1) {
				row.pictures.assign(static_cast<std::size_t>(Uniques[u].pictures), 0);
				for (int p = 0; p < Uniques[u].pictures; ++p) {
					bool every = true, some = false;
					for (const int v : Uniques[u].variants)
						if ((Uniques[v].picture == p || Uniques[v].picture < 0) && HasTile(v, false)) some = true, every = every && hides(v, false, Config.loot[2]);
					row.pictures[p] = some && every ? 3 : 0;
				}
			}
		}
	}
	// Per base: every unique (set item) that can be it, plain and ethereal.
	const auto byBase = [&](const std::vector<LootRow>& rows, const std::vector<int>& entries, std::vector<std::uint8_t>& bases) {
		std::vector<std::uint8_t> seen;
		for (std::size_t r = 0; r < rows.size(); ++r) {
			const int item = rows[r].item, u = entries[r];
			if (item < 0 || u < 0 || u >= UniqueCount()) continue;
			if (static_cast<int>(bases.size()) <= item) bases.resize(item + 1, 3), seen.resize(item + 1, 0);
			// What it can be: plain unless only ethereal, ethereal when it can be (a set item's one tile is both).
			const std::uint8_t can = Uniques[u].set ? 3 : (BaseHasTile(u, false) ? 1 : 0) | (BaseHasTile(u, true) ? 2 : 0);
			bases[item] &= static_cast<std::uint8_t>(rows[r].hide | ~can);
			seen[item] |= can;
		}
		for (std::size_t i = 0; i < bases.size(); ++i) bases[i] &= seen[i];
	};
	byBase(table->uniques, UniqueByRow, table->uniqueBases);
	byBase(table->sets, SetByRow, table->setBases);
	// By picture: a base whose items have pictures; an item with no picture
	// tiles counts for every picture.
	const auto byBasePics = [&](const std::vector<LootRow>& rows, const std::vector<int>& entries, std::vector<std::vector<std::uint8_t>>& pics, bool sets) {
		if (!Config.lootGraphic || !Config.graphicVariants) return;
		std::vector<std::vector<std::uint8_t>> seen;
		for (std::size_t r = 0; r < rows.size(); ++r) {
			const int item = rows[r].item, u = entries[r];
			if (item < 0 || u < 0 || u >= UniqueCount() || Uniques[u].secret) continue;
			const auto base = Bases.find(Uniques[u].code);
			const int  n    = base == Bases.end() ? 0 : std::min(base->second.pictures, 8);
			if (n < 2) continue;
			if (static_cast<int>(pics.size()) <= item) pics.resize(item + 1), seen.resize(item + 1);
			if (pics[item].empty()) pics[item].assign(n, 3), seen[item].assign(n, 0);
			const std::uint8_t can = sets ? 3 : (BaseHasTile(u, false) ? 1 : 0) | (BaseHasTile(u, true) ? 2 : 0);
			for (int p = 0; p < static_cast<int>(pics[item].size()); ++p) {
				const std::uint8_t hide = rows[r].pictures.empty() ? rows[r].hide : p < static_cast<int>(rows[r].pictures.size()) ? rows[r].pictures[p] : 0;
				pics[item][p] &= static_cast<std::uint8_t>(hide | ~can);
				seen[item][p] |= can;
			}
		}
		for (std::size_t i = 0; i < pics.size(); ++i)
			for (std::size_t p = 0; p < pics[i].size(); ++p) pics[i][p] &= seen[i][p];
	};
	byBasePics(table->uniques, UniqueByRow, table->uniqueBasePics, false);
	byBasePics(table->sets, SetByRow, table->setBasePics, true);
	table->exact    = Config.lootAmbiguous;
	table->unidOnly  = Config.lootUnidOnly;
	table->unidStats = Config.lootUnidStats;
	if (const auto now = LootRows.load(); !now || !(*now == *table)) {
		{
			std::lock_guard guard(UpgradeLock);
			UnidUpgrade.clear(); // the grail changed: asked again
		}
		LootRows.store(std::move(table), std::memory_order_release);
	}
}

// Lock held. The variants' settings changed: tiles, stored items' tiles, the loot filter follow.
void VariantsChanged() {
	if (AddVariants()) LanguageRefresh = true, StringsWanted = true; // the new tiles' names
	VariantUnits.clear();
	PicturesScanned = false; // graphic variants just on: the items' pictures read on the next arrange
	UpdateMiscTracking();
	RebuildStored();
	if (LootOn) PublishLootTable();
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
		const bool perfect = Uniques[drop.unique].secret || IsPerfect(drop.unique, unit, drop.eth);
		UiPerfect[drop.runtimeId] = perfect; // for its record, made on the game thread
		if (perfect) Perfect.insert(key);
		else Perfect.erase(key);
	}
}

// Lock held (UI thread). Whether an item about to go into the grail is
// perfect, read now for its record (made on the game thread).
void NoteUiPerfect(int u, std::uint32_t runtimeId, bool eth) {
	if (u < 0 || u >= UniqueCount() || !RollsReady || !ReadStat || GetCurrentThreadId() != UiThread.load()) return;
	if (void* unit = reinterpret_cast<GetUnitFn>(Base + Game::GetUnit)(runtimeId, Game::ItemUnit)) UiPerfect[runtimeId] = Uniques[u].secret || IsPerfect(u, unit, eth);
}

// Lock held. Whether the perfect tiles changed.
// Lock held. The entry whose rolls a record's perfection is checked with:
// its variant's when known (its choice's value counts), else its own.
int PerfectEntry(const Record& record) {
	const auto entry = EntryById.find(record.id);
	if (entry == EntryById.end()) return -1;
	const int u = entry->second, a = AffixIndexOfKey(u, record.variant);
	if (a >= 0)
		for (const int v : Uniques[u].variants)
			if (Uniques[v].affix == a && Uniques[v].picture < 0) return v;
	return u;
}

bool RefreshPerfect() {
	if (!RollsReady || !ReadStat) return false;
	// Only items on the page can be read; the records keep the others' flag.
	bool changed = false;
	for (const auto& [runtime, fingerprint] : Materialized) {
		const auto record = Records.find(fingerprint);
		if (record == Records.end() || record->second.base) continue;
		void* unit = reinterpret_cast<GetUnitFn>(Base + Game::GetUnit)(runtime, Game::ItemUnit);
		if (!unit) continue;
		if (record->second.variant.empty()) {
			TagVariant(record->second, unit);
			changed = changed || !record->second.variant.empty();
		}
		const int entry = PerfectEntry(record->second);
		if (entry < 0) continue;
		const bool perfect = Uniques[entry].secret || IsPerfect(entry, unit, record->second.eth);
		if (perfect != record->second.perfect) record->second.perfect = perfect, changed = true;
	}
	// The others: a record stored while its tile was a picture, or while its
	// rolls could not be read (Enhanced Damage is only in the tooltip, built on
	// this thread), kept "not perfect". Its ghost tells, once per launch.
	if (!Remote() && !GhostsFreeing && GetCurrentThreadId() == UiThread.load()) {
		std::set<std::uint32_t> onPage;
		for (const auto& [runtime, fingerprint] : Materialized) onPage.insert(fingerprint);
		auto* done  = new std::vector<Ghost>;
		int   asked = 0;
		// Stored before variants were known (or while their item could not be
		// read): their ghost tells which choice they have.
		for (auto& [fingerprint, record] : Records) {
			if (record.base || !record.variant.empty() || onPage.count(fingerprint) || VariantChecked.count(fingerprint)) continue;
			const auto entry = EntryById.find(record.id);
			if (entry == EntryById.end() || Uniques[entry->second].affixes.empty()) continue;
			const auto ghost = Ghosts.find(fingerprint);
			if (ghost == Ghosts.end()) {
				if (asked < 8 && !GhostWanted.count(fingerprint)) ++asked, SweepGhosts.insert(fingerprint), (void)GhostOf(fingerprint);
				continue;
			}
			VariantChecked.insert(fingerprint);
			TagVariant(record, ghost->second.unit);
			if (!record.variant.empty()) {
				changed = true;
				PerfectChecked.erase(fingerprint); // checked again with its choice's roll
				static int told = 0;
				if (told++ < 40) Say(false, "grailon: stored %s is variant %s", record.id.c_str(), record.variant.c_str());
			}
		}
		// Stored copies of items this season reworked: older versions are lost items.
		for (auto& [fingerprint, record] : Records) {
			if (record.base || VersionChecked.count(fingerprint)) continue;
			const auto entry = EntryById.find(record.id);
			if (entry == EntryById.end()) {
				VersionChecked.insert(fingerprint);
				continue;
			}
			const auto& e = Uniques[entry->second];
			if (!ReworkChecks.count({ e.runeword ? 2 : e.set ? 1 : 0, e.row })) {
				VersionChecked.insert(fingerprint);
				continue;
			}
			void* unit = nullptr;
			for (const auto& [runtime, print] : Materialized)
				if (print == fingerprint) unit = reinterpret_cast<GetUnitFn>(Base + Game::GetUnit)(runtime, Game::ItemUnit);
			const auto ghost = unit ? Ghosts.end() : Ghosts.find(fingerprint);
			if (!unit && ghost == Ghosts.end()) {
				if (asked < 8 && !GhostWanted.count(fingerprint)) ++asked, SweepGhosts.insert(fingerprint), (void)GhostOf(fingerprint);
				continue;
			}
			if (!unit) unit = ghost->second.unit;
			VersionChecked.insert(fingerprint);
			if (unit && OldVersion(entry->second, unit)) {
				Outdated.insert(fingerprint), changed = true;
				Say(false, "grailon: stored %s is an older version than this season's: moved to Lost items", e.name.c_str());
			}
			if (ghost != Ghosts.end() && SweepGhosts.erase(fingerprint) && ghost->second.unit != PeekShown) done->push_back(ghost->second), Ghosts.erase(ghost);
		}
		for (auto& [fingerprint, record] : Records) {
			if (record.perfect || record.base || onPage.count(fingerprint) || PerfectChecked.count(fingerprint)) continue;
			const auto entry = EntryById.find(record.id);
			if (entry == EntryById.end()) {
				PerfectChecked.insert(fingerprint);
				continue;
			}
			const auto ghost = Ghosts.find(fingerprint);
			if (ghost == Ghosts.end()) {
				if (asked < 8 && !GhostWanted.count(fingerprint)) ++asked, SweepGhosts.insert(fingerprint), (void)GhostOf(fingerprint);
				continue;
			}
			if (!ghost->second.unit) {
				PerfectChecked.insert(fingerprint);
				continue;
			}
			const int  rolls   = PerfectEntry(record);
			const bool perfect = Uniques[rolls].secret || IsPerfect(rolls, ghost->second.unit, record.eth);
			PerfectChecked.insert(fingerprint);
			if (perfect) record.perfect = true, changed = true;
			if (SweepGhosts.erase(fingerprint) && ghost->second.unit != PeekShown) done->push_back(ghost->second), Ghosts.erase(ghost);
		}
		if (done->empty()) delete done;
		else {
			++GhostEpoch;
			if (Threads->runOnGameThread(Context, FreeSomeGhosts, done) != D2RL::Threads::Result::Success) delete done; // freed with the game
		}
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


// The number over the life globe: the game hides it under its own left
// panels; the grail view hides it too while it is open (UI thread, every
// animation tick, since the game may show it again).
void HideLifeText(bool hide) { Gui::Hud::HideLifeNumber(hide); }

// UI thread, on each animation tick while a view is open (the glows play by
// themselves).
void Animate() {
	CloseLateViews();
	if (!ViewIsOpen || ShownView < 0 || !WidgetApi) return;
	HideLifeText(true);
}

void __cdecl AnimateOnUi(const D2RL::PluginContext*, void*) noexcept {
	Animate();
	RetryViewIfDue(); // every frame tick, not only every UI tick
}

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
			// A base: only on the runeword tile it is dropped on.
			if (Uniques[peek.unique].runeword && !BaseFor.count(item.runtimeId)) (void)AssignBase(item, true, peek.unique);
			if (const int u = GrailIndex(item); u == peek.unique && IsEth(item) == peek.eth && QueueSwap(item, u, true)) return;
			if (!Accepts(item)) {
				static int told = 0;
				const int  g    = GrailIndex(item);
				const auto twin = g >= 0 ? Stored.find({ g, IsEth(item) }) : Stored.end();
				if (told++ < 20)
					Say(false, "grailon: %s (quality %u) dropped on the picture tile of %s: not for the grail (its entry %d %s, %s)", CodeText(item.code).c_str(), static_cast<unsigned>(item.quality),
					    Uniques[peek.unique].name.c_str(), g, g >= 0 ? Uniques[g].name.c_str() : "", g < 0 ? "not known yet or none" : !HasTile(g, IsEth(item)) ? "no tile" : twin != Stored.end() ? (BaseTiles.count(twin->first) ? "its tile holds a base" : "its tile holds another copy") : "?");
				return;
			}
			const int u = GrailIndex(item);
			if (u == peek.unique && IsEth(item) == peek.eth) {
				job = new CursorJob { .step = CursorStep::PutPage, .id = item.runtimeId, .unique = u, .eth = IsEth(item) };
				Busy.insert(item.runtimeId);
				NoteUiPerfect(u, item.runtimeId, IsEth(item));
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
		if (HeldId || TakeId) return;
		if (!Accepts(item)) {
			static int told = 0;
			if (told++ < 20) Say(false, "grailon: %s (quality %u) dropped under the grid: not for the grail", CodeText(item.code).c_str(), static_cast<unsigned>(item.quality));
			return;
		}
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

// ---- the text boxes (objectives, search, exclusions, a list's name) ----
// D2R GUI's TextInput takes the keys while a box is active (the game's own
// input box gets none in this panel); the plugin keeps each box's text and
// the page shows it. One box at a time: TypeTarget.
std::atomic<bool>  ObjTyping {}, ObjTypedChanged {}, ObjEnter {};
std::atomic<int>   TypeTarget {};
std::mutex         ObjTypedLock;
std::wstring       TypedTexts[4]; // by target
void ObjInputTick();
// The page follows a key at once (the UI tick alone came up to 250 ms later).
void TypedNow() { (void)Gui::RunOnUi(ObjInputTick); }

std::string TypedText(int target) {
	std::lock_guard guard(ObjTypedLock);
	return Gui::Narrow(TypedTexts[TypeSlot(target)]);
}
std::string ObjTypedText() { return TypedText(TypeObjective); }

// The box lets the keys go.
void StopTyping() {
	ObjTyping = false;
	Gui::TextInput::Stop();
}

void ClearTyped(int target) {
	{
		std::lock_guard guard(ObjTypedLock);
		TypedTexts[TypeSlot(target)].clear();
	}
	if (ObjTyping && TypeTarget == target) Gui::TextInput::SetText("");
}
void ObjClearTyped() { ClearTyped(TypeObjective); }

// The box activated (its click): the keys taken from now on, Enter and Esc reported.
void ObjStartTyping(int target = TypeObjective) {
	TypeTarget = target;
	ObjTyping  = true;
	Gui::TextInput::Start(std::to_string(target), TypedText(target),
	                      {
	                          .changed =
	                              [target](const std::string& text) {
		                              {
			                              std::lock_guard guard(ObjTypedLock);
			                              TypedTexts[TypeSlot(target)] = Gui::Wide(text);
		                              }
		                              ObjTypedChanged = true;
		                              ObjInputTick();
	                              },
	                          .enter = [](const std::string&) { ObjEnter = true, ObjInputTick(); },
	                          .escape = [] { ObjTyping = false, ObjTypedChanged = true, ObjInputTick(); },
	                          .away = [](const std::string&) { ObjTyping = false, ObjTypedChanged = true, ObjInputTick(); },
	                          .keepOnEnter = true,
	                      },
	                      120);
}

// UI thread, each tick: the page follows the typing; Enter adds.
void ObjInputTick() {
	const int  target = TypeTarget;
	const bool search = target == TypeSearch;
	const bool here   = search || (CurrentCategory == SettingsView && (SettingsPage == (target == TypeExclude || target == TypeListName ? 2 : 4) || (target == TypeExclude && VarEditing)));
	if (ObjTyping && (!ViewOpen() || !here)) StopTyping(), ObjTypedChanged = true;
	if (ObjEnter.exchange(false) && ObjTyping && target != TypeExclude) {
		if (target == TypeListName) StopTyping(), ObjTypedChanged = true; // the name is done
		else Queue(search ? "searchnext" : "objadd");
	}
	if (!ObjTypedChanged.exchange(false) || !ViewOpen()) return;
	if (search) {
		Queue("search"); // the matches again, the first shown
		return;
	}
	if (target == TypeExclude) ExclFirst = 0; // the list from its top for a new search
	if (target == TypeListName) {
		Queue("exlname"); // the list's name follows, the page with it
		return;
	}
	ShowView(); // the page again (its key has the text)
}


void RunCommand(const std::string& command) {
	// The objectives' box: active on its click, inactive on any other.
	if (command == "objtype" || command == "searchtype" || command == "excltype") {
		ObjStartTyping(command[0] == 's' ? TypeSearch : command[0] == 'e' ? TypeExclude : TypeObjective);
		ShowView();
		return;
	}
	// Delete clicked once: anything else clicked on the settings forgets it.
	if (ExclDeleteArmed >= 0 && command.rfind("exldel", 0) != 0
	    && (command.rfind("ex", 0) == 0 || command.rfind("spage", 0) == 0 || command.rfind("menu", 0) == 0 || command.rfind("drop", 0) == 0 || command.rfind("toggle", 0) == 0 || command.rfind("loot", 0) == 0)) {
		std::lock_guard guard(Lock);
		ExclDeleteArmed = -1;
		++Config.version;
	}
	if ((command.rfind("menu", 0) == 0 || command.rfind("toggle", 0) == 0 || (command.rfind("obj", 0) == 0 && command != "objadd" && command != "objtype")) && ObjTyping) StopTyping(), ObjTypedChanged = true;
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
		if (ViewOpen() || PreparedView >= 0 || ErrorPage.IsOpen()) CloseView();
		else ShowView();
		return;
	}
	if (command == "close") {
		CloseView();
		return;
	}
	if (command == "settings") { // I Hate TOMLs' Open button: the grail on its settings
		{
			std::lock_guard guard(Lock);
			GridBar.Clear();
			CurrentCategory = SettingsView;
			Scroll          = 0;
		}
		ShowView();
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
	// The Loot page's exclusions: a group opened or closed, a group's or an
	// item's box, a step of the list's scroll bar, another list chosen, the
	// list cleared (a built-in one: back to its preset).
	{
		std::unique_lock guard(Lock);
		if (ExclBar.Handle(command)) { // a step of the list's scroll bar
			guard.unlock();
			ShowView();
			return;
		}
	}
	// The lists: switched on or off, renamed (its name typed in place),
	// moved, added, deleted, opened in the editor; the editor saved or left.
	const auto listCommand = [&](const char* prefix) {
		const auto size = std::strlen(prefix);
		return command.size() > size && command.compare(0, size, prefix) == 0 && std::isdigit(static_cast<unsigned char>(command[size])) ? std::atoi(command.c_str() + size) : -1;
	};
	if (const int i = listCommand("exlren"); i >= 0) {
		{
			std::lock_guard guard(Lock);
			if (i >= static_cast<int>(ExclLists.size())) return;
			OpenDrop        = -1;
			ExclDeleteArmed = -1;
			if (!(ObjTyping && TypeTarget == TypeListName && ExclRenaming == i)) {
				const std::string utf = ExclLists[i].name;
				std::wstring      wide(static_cast<std::size_t>(std::max(0, MultiByteToWideChar(CP_UTF8, 0, utf.c_str(), static_cast<int>(utf.size()), nullptr, 0))), L'\0');
				if (!wide.empty()) MultiByteToWideChar(CP_UTF8, 0, utf.c_str(), static_cast<int>(utf.size()), wide.data(), static_cast<int>(wide.size()));
				std::lock_guard typedGuard(ObjTypedLock);
				TypedTexts[TypeListName] = wide;
			}
			ExclRenaming = i;
			++Config.version;
		}
		ObjStartTyping(TypeListName);
		ShowView();
		return;
	}
	if (command == "exlname") { // the name typed so far (an empty one keeps the old name)
		std::string typed = TypedText(TypeListName);
		std::erase_if(typed, [](char c) { return c == '\n' || c == '\r' || c == '\t'; });
		bool changed = false;
		{
			std::lock_guard guard(Lock);
			if (!typed.empty() && ExclRenaming >= 0 && ExclRenaming < static_cast<int>(ExclLists.size()) && ExclLists[ExclRenaming].name != typed)
				ExclLists[ExclRenaming].name = typed, changed = true;
			++Config.version;
		}
		if (changed) SaveSettings();
		ShowView();
		return;
	}
	// The Variants section's lists: varedit1 (affix) / varedit2 (graphic) opens
	// the editor on the items in it; its Save keeps the ones unticked out.
	if (command == "varedit1" || command == "varedit2" || (VarEditing && (command == "exlsave" || command == "exlcancel"))) {
		bool changed = false;
		{
			std::lock_guard guard(Lock);
			OpenDrop = -1;
			if (command[0] == 'v') {
				VarEditing = command.back() - '0';
				ExclDraft  = { VarEditing == 1 ? "Affix variants" : "Graphic variants" };
				const auto& off = VarEditing == 1 ? AffixOff : GraphicOff;
				for (int u = 0; u < UniqueCount(); ++u)
					if (VariantCandidate(u, VarEditing) && !VariantLocked(u, VarEditing) && !off.count(Uniques[u].id)) ExclDraft.items.insert(Uniques[u].id);
				ExclFirst = 0;
				ExclOpen.clear();
				ClearTyped(TypeExclude);
				if (ObjTyping && TypeTarget == TypeExclude) StopTyping();
			} else {
				if (command == "exlsave") {
					auto& off = VarEditing == 1 ? AffixOff : GraphicOff;
					for (int u = 0; u < UniqueCount(); ++u) {
						if (!VariantCandidate(u, VarEditing) || VariantLocked(u, VarEditing)) continue;
						if (ExclDraft.items.count(Uniques[u].id)) off.erase(Uniques[u].id);
						else off.insert(Uniques[u].id);
					}
					changed = true;
				}
				VarEditing = 0;
				if (changed) VariantsChanged();
			}
			++Config.version;
		}
		if (changed) SaveSettings();
		ShowView();
		return;
	}
	{
		const int on = listCommand("exlon"), up = listCommand("exlup"), down = listCommand("exldown"), del = listCommand("exldel"), add = listCommand("exladd"), edit = listCommand("exledit");
		const bool save = command == "exlsave", cancel = command == "exlcancel";
		if (on >= 0 || up >= 0 || down >= 0 || del >= 0 || add >= 0 || edit >= 0 || save || cancel) {
			bool typeName = false, changed = false;
			int  renameAt = -1;
			{
				std::lock_guard guard(Lock);
				OpenDrop        = -1;
				const int count = static_cast<int>(ExclLists.size());
				const int armed = std::exchange(ExclDeleteArmed, -1);
				if (ObjTyping && TypeTarget == TypeListName) StopTyping();
				if (on >= 0 && on < count) ExclLists[on].active = !ExclLists[on].active, changed = true;
				else if (up > 0 && up < count) std::swap(ExclLists[up], ExclLists[up - 1]), changed = true;
				else if (down >= 0 && down + 1 < count) std::swap(ExclLists[down], ExclLists[down + 1]), changed = true;
				else if (del >= 0 && del < count) {
					if (armed != del) ExclDeleteArmed = del; // the second click deletes
					else ExclLists.erase(ExclLists.begin() + del), changed = true;
				} else if (add >= 0) {
					const int preset = std::clamp(add, 0, 2) - 1;
					ExclLists.push_back({ FreeListName(preset < 0 ? "New list" : PresetNames[preset]), preset });
					changed  = true;
					typeName = preset < 0;
					renameAt = static_cast<int>(ExclLists.size()) - 1;
				} else if (edit >= 0 && edit < count) {
					ExclEditing = edit, ExclDraft = ExclLists[edit], ExclFirst = 0;
					ExclOpen.clear();
					ClearTyped(TypeExclude);
				} else if (save && ExclEditing >= 0 && ExclEditing < count) {
					ExclDraft.active = ExclLists[ExclEditing].active, ExclDraft.name = ExclLists[ExclEditing].name;
					ExclLists[ExclEditing] = ExclDraft, ExclEditing = -1, changed = true;
				} else if (save || cancel) ExclEditing = -1;
				++Config.version;
			}
			if (changed) SaveSettings();
			if (typeName) {
				RunCommand("exlren" + std::to_string(renameAt));
				return;
			}
			ShowView();
			return;
		}
	}
	// The editor: a group opened or closed, a group's or an item's box, the
	// list cleared (a preset's: back to the preset's items). Saved by Save.
	if (command == "excltypeclear") {
		{
			std::lock_guard guard(Lock);
			ClearTyped(TypeExclude);
			if (ObjTyping && TypeTarget == TypeExclude) StopTyping();
			ExclFirst = 0;
		}
		ShowView();
		return;
	}
	if (command.rfind("exo", 0) == 0 || command.rfind("exg", 0) == 0 || command.rfind("exi", 0) == 0 || command == "excludeclear") {
		{
			std::lock_guard guard(Lock);
			if (ExclEditing < 0 && !VarEditing) return;
			OpenDrop          = -1;
			const auto key    = command.size() > 3 ? command.substr(3) : std::string();
			const auto typed  = TypedText(TypeExclude);
			if (command == "excludeclear") {
				auto& list = ExclDraft;
				if (list.preset >= 0) list.own = false; // its preset again
				list.items.clear();
			} else if (command[2] == 'o' && (key[0] == 'm' || key[0] == 'c' || key[0] == 's')) {
				if (!typed.empty()) return; // a search shows every group open
				if (!ExclOpen.erase(key)) ExclOpen.insert(key);
			} else {
				// A box: an item's flips; a group's ticks all of it, or clears it when all of it is ticked.
				const auto tiles = ExclRowTiles(key, typed);
				const bool all   = std::all_of(tiles.begin(), tiles.end(), [](const auto& tile) { return RowLocked(tile.first) || Drafted(tile.first, tile.second); });
				if (!tiles.empty()) OwnBuiltin();
				for (const auto& [u, eth] : tiles) {
					if (RowLocked(u)) continue;
					if (all) ExclDraft.items.erase(DraftKey(u, eth));
					else ExclDraft.items.insert(DraftKey(u, eth));
				}
			}
			++Config.version;
		}
		ShowView();
		return;
	}
	// The settings' pages, and the misc tracking page's own pages.
	if (command.rfind("spage", 0) == 0 || command.rfind("miscpage", 0) == 0) {
		{
			std::lock_guard guard(Lock);
			OpenDrop = -1;
			if (command[0] == 's') SettingsPage = std::clamp(std::atoi(command.c_str() + 5), 0, SettingsPageCount - 1), VarEditing = 0;
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
		if (command == "toggle:affix_variants" || command == "toggle:graphic_variants") {
			std::lock_guard guard(Lock);
			VariantsChanged();
		}
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
			if (menu >= WeaponMenu && menu <= LostMenu) {
				if (const auto last = LastInMenu.find(menu); last != LastInMenu.end()) pick = last->second;
				for (int c = 0; c < CategoryCount() && pick < 0; ++c)
					if (InMenu(c, menu) && pick == none) pick = c;
			}
			if (pick == none && menu == ObjMenu) pick = SettingsView, SettingsPage = 4; // no objective yet: where they are made
			if (pick == none || (pick == CurrentCategory && (pick < 0 || ViewEth == IsEthMenu(menu)))) return;
			if (pick < 0) GridBar.Clear();
			CurrentCategory = pick;
			ViewEth         = IsEthMenu(menu);
			Scroll          = 0;
		} else if (command.rfind("row", 0) == 0) {
			// A drag of the scroll bar: the view's rows, or the Loot page's exclusions list.
			if (CurrentCategory == SettingsView && (SettingsPage == 2 || VarEditing)) ExclFirst = std::clamp(std::atoi(command.c_str() + 3), 0, ExclMax);
			else Scroll = std::clamp(std::atoi(command.c_str() + 3), 0, MaxScroll);
		}
		else if (GridBar.Handle(command)) {} // a step of the view's scroll bar: Scroll set

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
	if (ShownView < 0 || !WidgetApi || !Config.scrollAnimations || !ScrollPanel.Ready()) return false;
	std::string layout;
	{
		std::lock_guard guard(Lock);
		if (CurrentCategory < 0) return false;
		layout = ScrollLayout(down);
	}
	if (layout.empty()) return false;
	Trace("scroll frames open", down ? 1 : -1);
	if (!ScrollPanel.Show(layout)) return false;
	Trace("scroll frames opened");
	D2RL::Widgets::WidgetHandle widget {};
	ScrollAnimation             anim { .running = true, .down = down };
	for (int k = 1; k < ScrollFrames; ++k)
		if (ScrollPanel.Find("Frame" + std::to_string(k), &widget)) anim.frames.push_back(widget);
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
// D2R GUI's WindowHook: while the view is open, the wheel over the left part
// of the screen scrolls it, and the left button held on a scroll bar drags it.
// The plugin's own mouse handling (middle click, a stored item picked up)
// sees every message first.

bool WindowMessage(UINT message, WPARAM wParam, LPARAM) {
	// Middle click on a hovered grail item: shown in the grail.
	if (message == WM_MBUTTONDOWN && Config.middleClick && ListsReady && !ScrollTestOpen) {
		MiddleSeenAt = GetTickCount64(); // seen here: the poll leaves it
		if (MiddleClickNow("window")) return true;
	}
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
	return false;
}

// UI thread, once the view first opens in a game.
void HookWheel() {
	Gui::WindowHook::Start({
	    .active     = [] { return ViewIsOpen.load(); },
	    .wheel      = [](int notches) { for (int k = 0; k < std::abs(notches); ++k) Queue(notches > 0 ? "down" : "up"); },
	    .wheelRight = 0.55,
	    .extra      = WindowMessage,
	});
	GridBar.Attach();
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
	if (rows && ViewOpen() && CurrentCategory == SettingsView && (SettingsPage == 2 || VarEditing)) {
		// The wheel over the Loot page scrolls its exclusions list (two rows a notch: about a grail row).
		ExclFirst = std::clamp(ExclFirst + 2 * rows, 0, ExclMax);
		ShowView();
	} else if (rows && ViewOpen()) RunScroll(rows);
}

// Registers every text the views use, replacing the previous ones.
bool RegisterStrings() {
	GrailHeader = Tr("Currently in Grail");
	static Gui::StringTable strings("view"); // data/local/lng/strings/d2rloader/grail-on/view.json
	if (strings.Register(StringsTable())) return true;
	Say(true, "grailon: could not register the texts");
	return false;
}

// The rune stones, one sprite per cell of the Chronicle's sheet (grune<k>), and the blank stone (grune-blank), once.
void RegisterRunes() {
	if (GenericRunes) return;
	GenericRunes = Gui::ItemArt::RegisterRuneStones(ChronicleRunesFile, Cell, "grune");
}

// The grail button on the inventory's left edge, in the same stone and gold
// mold as Charm Inventory's and Automatic Materials Deposit's (3, 813), by
// default under the deposit's, where the settings put it.
Gui::InventoryButton GrailInvButton;
void MoveInventoryButton() { GrailInvButton.Move(Config.buttonX, Config.buttonY); }
void MoveMoldFrom(void* grid) { GrailInvButton.MoveFromGrid(grid); }

void RemoteSweep();
void __cdecl UiTick(const D2RL::PluginContext* context, void*) noexcept {
	UiThread = GetCurrentThreadId();
	UnmuteSoundsWhenDue();
	GrailInvButton.Reload(LocalPlayer.load() != D2RL::PlayerHandle {});
	GrailInvButton.Watch({ "bulk-currency-deposit/inventory-button" });
	D2RL::PlayerHandle player {};
	LocalPlayer = Inventory->getLocalPlayer(context, &player) == D2RL::Inventory::Result::Success ? player : D2RL::PlayerHandle {};
	// Open over the stash: the stash closing (walking away, its close button)
	// closes the grail too, as it does the cube.
	if (const bool closing = StashClosing.exchange(false); ShownView >= 0 && IsOverView(ShownView) && PreparedView < 0 && Views.IsOpen(ShownView) && (closing || !StashShown())) {
		static int told = 0;
		if (told++ < 3) Say(false, "grailon: the stash closed (%s): the grail over it closes", closing ? "message" : "panel");
		CloseView();
	}
	// Each character opens the storage of its kind (softcore/hardcore); its
	// page is checked against the records once.
	if (static D2RL::PlayerHandle seen {}; player != seen) {
		std::lock_guard guard(Lock);
		if (seen && StoreOwner == seen) {
			// Left the game: the game saved the character, so what it took out is
			// in its save now.
			std::erase_if(TakenOut, [](const auto& out) { return out.second.owner == StoreCharacter; });
			SaveStore();
		}
		seen = player;
		Ghosts.clear(), GhostWanted.clear(); // that game's: freed with it
		{
			std::lock_guard kids(GhostKidsLock);
			GhostKids.clear();
		}
		VariantUnits.clear(), Pictures.clear(), Learning.items.clear(), OldUnits.clear();
		{
			std::lock_guard upgrades(UpgradeLock);
			UnidUpgrade.clear(), UpgradeAsked.clear(); // its items' ids
		}
		ServerGame = nullptr;
		if (player) OpenStoreFor(player), StaleChecked = false;
	}
	// The character's pictures, read early for the loot filter.
	if (LootOn && Threads && LocalPlayer.load() && !PictureScanQueued) {
		bool want = false;
		{
			std::lock_guard guard(Lock);
			want = !PicturesScanned && Config.graphicVariants && Config.lootGraphic && ListsReady;
		}
		if (want && !PictureScanQueued.exchange(true) && Threads->runOnGameThread(Context, ScanPictures, nullptr) != D2RL::Threads::Result::Success) PictureScanQueued = false;
	}
	// What the loot filter hides follows the grail.
	if (LootOn) {
		std::lock_guard guard(Lock);
		PublishLootTable();
	}
	// Unidentified drops to compare with their grail copies (game thread; a
	// joined game has no server copies: answered "not known" at once).
	if (UpgradeWanted && !UpgradeRunning.exchange(true)) {
		UpgradeWanted = false;
		if (Remote()) CheckUpgrades(context, nullptr);
		else if (Threads->runOnGameThread(context, CheckUpgrades, nullptr) != D2RL::Threads::Result::Success) UpgradeRunning = false, UpgradeWanted = true;
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
	if (!ViewIsOpen) HideLifeText(false); // nothing when it is shown
	// The scroll panel only lives through a scroll: an open one blocks the game
	// world (chests, NPCs), so it never stays once the frames are done and no
	// view is on its way (or the view was closed with Escape).
	if (!Anim.running && (PreparedView < 0 || !ViewOpen())) CloseScrollPanel();
	KickPending(); // a kept cursor item never stays kept
	ObjInputTick();
	WatchMiddleClick();
	RetryViewIfDue();
	LetClosedViewsGo();
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

// Any thread: every UI message. The stash closing (its panel, or its close
// message): a grail over it closes at once.
void WatchUiMessage(const Events::UiMessageEvent& event) {
	if (event.target && event.command
	    && ((!std::strcmp(event.target, "BankPanelMessage") && !std::strcmp(event.command, "Close"))
	        || (!std::strcmp(event.target, "PanelManager") && event.text && !std::strncmp(event.text, "Bank", 4) && (!std::strcmp(event.command, "ClosePanel") || !std::strcmp(event.command, "UnloadPanel")))))
	{
		// At once, with the game's own panels (the next UI tick comes later).
		if (ShownView >= 0 && IsOverView(ShownView) && PreparedView < 0 && GetCurrentThreadId() == UiThread) {
			static int told = 0;
			if (told++ < 3) Say(false, "grailon: the stash closes: the grail over it closes with it");
			CloseView();
		} else StashClosing = true;
	}
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
// Expanded Stash Plus's stash frame ships inside the plugin (PNGs compiled in,
// full and low-end size), decoded at load and registered as the panel sprite.

bool RegisterPanelArt() {
	Picture full, lowEnd;
	if (!Gui::DecodePng(grailon_panel_png, grailon_panel_png_end, full) || !Gui::DecodePng(grailon_panel_lowend_png, grailon_panel_lowend_png_end, lowEnd)) return false;
	const std::string path = "data/hd/global/ui/d2rloader/grail-on/panel";
	return RegisterResource(path + ".sprite", Gui::WriteSprite({ full })) && RegisterResource(path + ".lowend.sprite", Gui::WriteSprite({ lowEnd }));
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
Gui::SinglePanel                 ScrollTestPanels[ScrollTestVariants];
int                              ScrollTestVariant {};
int                              ScrollTestTries {}; // ticks left to open it
#define ScrollTestPanel ScrollTestPanels[ScrollTestVariant]
constexpr int ScrollTestRows = 48;

std::string ScrollTestName(int variant) { return "ScrollTest" + std::to_string(variant); }

std::string ScrollTestLayout(int variant) {
	std::string layout = Gui::ClickCatcher("PanelClickCatcher", 0, 0, PanelWidth, PanelHeight) + Gui::Image("Background", 0, 0, Gui::SpriteName("panel")) + Gui::Image("Cover", 57, 175, CoverSprite())
	                   + Gui::Node("TextBoxWidget", "Title", Gui::Rect(416, 50, 855, 60) + R"(, "style": "$StyleTitleBlock", "text": "@grail-on:ScrollTest")") + Gui::CloseButton("CloseButton", 1600, 9, "scrolltest");
	const auto panel = [&](const std::string& children) { return Gui::Panel("grail-on/ScrollTest" + std::to_string(variant), 236, -856, PanelWidth, PanelHeight, children, 10, Gui::LeftAnchor); };
	if (variant == 4) return panel(layout + Gui::ItemGrid("grail-on/Grid", GridLeft, GridTop, 16, 16));
	if (variant == 3) return panel(layout + Gui::Node("Widget", "Viewport", Gui::Rect(GridLeft, GridTop, PageSize * Cell, PageSize * Cell), Gui::ItemGrid("grail-on/Grid", 0, 0, 16, 16)));
	std::string rows;
	for (int r = 0; r < ScrollTestRows; ++r) {
		const auto n = std::to_string(r);
		// The page's grid in the first row, drawn over the rows below it.
		const std::string cell = Gui::Image("Slot" + n, (r % 8) * Cell, 0, SlotSprite(1, 1))
		                       + Gui::Node("TextBoxWidget", "Text" + n, Gui::Rect(1000, 0, 400, Cell) + R"(, "text": "@grail-on:Row)" + n + R"(", "style": {"fontColor": "$FontColorGoldYellow", "pointSize": 30, "alignment": {"h": "left", "v": "center"}})")
		                       + (r == 0 ? Gui::ItemGrid("grail-on/Grid", 0, 0, 16, 16) : std::string());
		rows += Gui::Node("TableRowWidget", "Row" + n, {}, Gui::Node("Widget", "Cell" + n, Gui::Rect(0, 0, PageSize * Cell - 60, Cell), cell));
	}
	const std::string table = Gui::Node("TableWidget", "Rows", R"("columns": [{"width": )" + std::to_string(PageSize * Cell - 60) + R"(, "alignment": {"h": "left", "v": "top"}}], "rowHeight": )" + std::to_string(Cell), rows);
	std::string view;
	if (variant == 0)
		view = Gui::Node("ImageWidget", "ScrollBarBackground", R"("rect": {"x": -20, "y": 0}, "anchor": {"x": 1.0}, "filename": "PauseMenu\\VerticalScroll")",
		                 Gui::Node("ScrollControllerWidget", "ScrollController",
		                           R"("rect": {"x": -31, "y": 29, "width": 26, "height": 1208}, "anchor": {"x": 1.0}, "upArrowFilepath": "FrontEnd\\HD\\Final\\FrontEnd_ScrollUpBtn", "downArrowFilepath": "FrontEnd\\HD\\Final\\FrontEnd_ScrollDownBtn", "barFilepath": "PauseMenu\\VerticalIndicator", "viewName": "ScrollView", "wheelScrollSound": "cursor_scroll_hd", "buttonScrollSound": "cursor_scroll_hd")"))
		     + Gui::Node("ScrollViewWidget", "ScrollView", R"("fitToParent": true, "scrollControllerName": "ScrollController")", table);
	else if (variant == 1) view = Gui::Node("ScrollViewWidget", "ScrollView", R"("fitToParent": true)", table);
	else view = table;
	return panel(layout + Gui::Node("Widget", "Viewport", Gui::Rect(GridLeft, GridTop, PageSize * Cell, PageSize * Cell), view));
}

void ToggleScrollTest() {
	if (ScrollTestPanel.IsOpen()) {
		ScrollTestPanel.Close();
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
		auto result = D2RL::Panels::Result::NotFound;
		if (ScrollTestPanel.Ready()) (void)ScrollTestPanel.Open(&result);
		ScrollTestOpen = result == D2RL::Panels::Result::Success;
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
	if (!ScrollTestPanel.IsOpen()) {
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
	const std::string layout = Gui::Panel("@PANEL@", WindowX() + GridLeft, PanelY + GridTop, StageCols * Cell, StageRows * Cell, "@GRID@", OverShown ? OverPriority - 1 : 9, Gui::LeftAnchor);
	const PageHost::LayoutRequest overlay { .structSize = sizeof(overlay), .flags = now ? PageHost::LayoutApplyNow : 0U, .layout = layout.c_str(), .gridX = 0, .gridY = 0 };
	return Host::Service->setAreaLayout(Host::Area, &overlay) == PageHost::Result::Success;
}

// The compact setting changed: the window moved, so did the scroll bar and the host's grid.
void CompactChanged() {
	GridBar.SetPanel(WindowX(), PanelY), ExclBar.SetPanel(WindowX(), PanelY);
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
	if (!RegisterPanelArt()) Say(true, "grail-on: could not unpack the stash frame");
	// The menu and category icons (game-icons.net), in the game's gold text colour.
	for (const auto& mask : IconMasks) {
		Picture icon { IconSize, IconSize, std::vector<unsigned char>(static_cast<std::size_t>(IconSize) * IconSize * 4) };
		for (int p = 0; p < IconSize * IconSize; ++p) {
			const bool lost = std::string_view(mask.name) == "lost"; // the Lost items menu: red
			icon.rgba[p * 4] = lost ? 200 : 215, icon.rgba[p * 4 + 1] = lost ? 30 : 200, icon.rgba[p * 4 + 2] = lost ? 30 : 150;
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
		const auto pad = [](const Picture& in, int box, int inner) { return Gui::Padded(in, box, inner); };
		const Picture icon = own ? tint(215, 200, 150) : Gui::ResizeBox(tint(215, 200, 150), ObjIconSize, ObjIconSize);
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
	if (!RegisterSprite("mold", { Gui::MoldPicture() }) || !RegisterGrailButton()) Say(true, "grail-on: could not register the button sprite");
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
	// The over-the-stash views take no left slot: the stash stays open under them.
	if (!Views.Register(
	        "View", ViewCount, layout, [](int i) { return IsOverView(i) ? 1 : 0; },
	        [](int i) { return IsOverView(i) ? D2RL::Panels::PanelFlags::CloseOnEscape : D2RL::Panels::PanelFlags::CloseOnEscape | D2RL::Panels::PanelFlags::GameplayLeftSlot; }, Page, "grail-on/Grid"))
		Say(true, "grail-on: could not register every view panel");
	// The scroll animation's panel, over the view (its layout is made for each step).
	if (!ScrollPanel.Register("Scroll", Gui::EmptyPanel("grail-on/Scroll", 12), D2RL::Panels::PanelFlags::None)) Problem(false, "grail-on: could not register the scroll panel; wheel steps jump");
	// The page that says what is switched off (its layout is made when it opens).
	if (!ErrorPage.Register("Error", ErrorLayout(), D2RL::Panels::PanelFlags::CloseOnEscape | D2RL::Panels::PanelFlags::GameplayLeftSlot)) Say(true, "grail-on: could not register the error page");
	// The scroll test panels, bound to the same page.
	for (int variant = 0; variant < ScrollTestVariants; ++variant)
		if (!ScrollTestPanels[variant].Register(ScrollTestName(variant), ScrollTestLayout(variant), D2RL::Panels::PanelFlags::CloseOnEscape | D2RL::Panels::PanelFlags::GameplayLeftSlot, Page, "grail-on/Grid"))
			Say(true, "grail-on: scroll test %d: could not register its panel", variant);
	ScrollTestVariant = 0;
	if (!GrailInvButton.Register("InventoryButton", { .moldSprite = "mold", .buttonSprite = "grailbutton", .button = "GrailButton", .tooltip = "@grail-on:GrailTip", .command = "open" }, Config.buttonX, Config.buttonY))
		Problem(false, "grail-on: could not add the inventory button; use the grailon command");

	// The buttons' clicks (D2R GUI's commands), queued as the wheel's and the console's are.
	Gui::Commands::Watch(WatchUiMessage);
	if (!Gui::Commands::Listen(Queue, 100, true)) Problem(true, "grail-on: could not listen to button clicks");
	GridBar.SetLook(BarLook()), ExclBar.SetLook(BarLook());
	GridBar.SetPanel(WindowX(), PanelY), ExclBar.SetPanel(WindowX(), PanelY);
	GridBar.OnScroll([](int row) { Scroll = std::clamp(row, 0, MaxScroll); });
	ExclBar.OnScroll([](int row) { ExclFirst = std::clamp(row, 0, ExclMax); });
	GridBar.OnDrag([](int row) { Queue("row" + std::to_string(row)); });
	ExclBar.OnDrag([](int row) { Queue("row" + std::to_string(row)); });
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

// I Hate TOMLs lists Grail On! with an Open button that opens its settings.
PluginMenu::Connection SettingsMenu;
void __cdecl OpenFromSettingsMenu(void*) noexcept { Queue("settings"); }

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
	(void)Gui::Init(context, PluginId);
	Gui::Api().plainText = true; // the plugin translates its own texts
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
	(void)SettingsMenu.StartInterface(context, OpenFromSettingsMenu, nullptr);
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
	SettingsMenu.Stop();
	Gui::TextInput::Shutdown();
	Running = false;
	if (Ticker.joinable()) Ticker.join();
	Gui::WindowHook::Stop();
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
