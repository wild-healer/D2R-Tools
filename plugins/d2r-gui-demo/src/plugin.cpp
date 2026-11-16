// D2R GUI Demo (plugin id "d2r-gui-demo"): a window built only from the D2R
// GUI library (lib/d2r-gui), to try its parts in game and as the example of
// how a plugin uses it: the plugin declares its window (menus, pages and the
// components on them, bound to its own variables) and App does the rest.
// It changes nothing in the game.
//
// Open it with the console command "guidemo" or the Controls menu
// (D2R GUI Demo > Open D2R GUI Demo).

#include <d2r_gui.h>
#include <d2r_gui/icons.h>
#include <d2r_gui/app.h>
#include <string>
#include <vector>

namespace {

namespace Gui = D2RGui;
namespace App = D2RGui::App;

constexpr const char* PluginId      = "d2r-gui-demo";
constexpr const char* PluginVersion = "0.0.2";

const D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = PluginId,
	.name        = "D2R GUI Demo",
	.version     = PluginVersion,
	.author      = "wild-healer",
	.description = "A window made with the D2R GUI library, to try it. Changes nothing in the game.",
	.flags       = D2RL::PluginFlags::Client,
};

// ---- the plugin's data ----
bool        ShowOdd = true;
bool        Loud    = false;
int         Mode    = 0;
int         Monster = 1;
int         Number  = 10;
std::string Search;
std::string Picked = "Click a row";
double      Scale  = 1.0;
int         PosX = 0, PosY = 0;
std::string Hovered;
std::string Objective;
int         ObjectiveIcon = 0;
const char* ObjectiveIcons[] { "target-arrows", "skull-crossed-bones", "crown", "trophy-cup", "gem-pendant", "castle" };

std::vector<Gui::DropDown::Entry> Monsters() {
	std::vector<Gui::DropDown::Entry> entries { { "Mods", true } };
	for (const char* mod : { "Extra fast", "Extra strong", "Cursed", "Fire enchanted", "Lightning enchanted", "Cold enchanted", "Mana burn", "Spectral hit", "Stone skin", "Teleport" })
		entries.push_back({ mod });
	entries.push_back({ "Monsters", true });
	for (int i = 1; i <= 120; ++i) entries.push_back({ "Monster " + std::to_string(i) });
	return entries;
}

std::vector<App::Row> Rows() {
	std::vector<App::Row> rows;
	std::string           query = Search;
	for (auto& c : query) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	for (int i = 1; i <= 60; ++i) {
		if (!ShowOdd && i % 2) continue;
		const std::string name = "Row " + std::to_string(i);
		if (!query.empty() && name.find(query) == std::string::npos && ("row " + std::to_string(i)).find(query) == std::string::npos) continue;
		rows.push_back({ .text = name, .tooltip = "Click to open it", .value = i % 7 == 0 ? "seven" : "" });
	}
	return rows;
}

App::Window Describe() {
	return {
	    .title = "D2R GUI Demo",
	    .menus = {
	        { .tab = { "scroll-unfurled", "About" }, .title = "D2R GUI Demo", .items = {
	              App::Section("About"),
	              App::Text("This window is declared, not drawn: the plugin lists its menus, pages and components, "
	                        "binds each component to one of its variables, and the library makes the window, "
	                        "the clicks, typing, drop-downs and scrolling."),
	          } },
	        { .tab = { "cog", "Settings" }, .title = "Settings", .pages = {
	              { .tab = { "eyeball", "Display" }, .items = {
	                    App::Section("Display"),
	                    App::Check("Show odd rows", &ShowOdd).Tip("Shows or hides the odd rows of the list"),
	                    App::Choice("Mode", &Mode, { "First choice", "Second choice", "Third choice" }),
	                    App::Number("A number", &Number, 0, 99).Tip("Click the arrows"),
	                    App::Action("Back to 10", "Reset", [] { Number = 10; }),
	                } },
	              { .tab = { "skull-crossed-bones", "Monsters" }, .items = {
	                    App::Section("A long list"),
	                    App::Entries("Monster", &Monster, Monsters()).Filter().Tip("Scroll it, or type to filter it"),
	                    App::Check("Louder", &Loud).When([] { return Monster >= 12; }),
	                } },
	          } },
	        { .tab = { "crossed-swords", "Tiles" }, .title = "Tiles and charts", .pages = {
	              { .tab = { "crossed-swords", "Tiles" }, .content = [] {
	                    return std::vector<App::Item> {
	                        App::Text(Hovered.empty() ? "Right or middle click a tile, or hover it." : Hovered),
	                        App::Tiles([] {
	                            std::vector<App::TileRow> rows;
	                            const char* icons[] { "broadsword", "battle-axe", "pocket-bow", "visored-helm", "breastplate", "ring", "gem-pendant", "boots" };
	                            for (int r = 0; r < 6; ++r) {
	                                App::TileRow row { .header = "Row " + std::to_string(r + 1) };
	                                for (int t = 0; t < 8; ++t) {
	                                    const std::string name = std::string(icons[t]) + " " + std::to_string(r + 1);
	                                    row.tiles.push_back({ .id = name, .width = t < 3 ? 2 : 1, .height = t < 5 ? 3 : 1,
	                                                          .sprite = Gui::Icons::Sprite(icons[t], 80, { 215, 200, 150 }), .label = icons[t], .tooltip = name,
	                                                          .click = [name] { Hovered = "Clicked " + name; },
	                                                          .rightClick = [name] { Hovered = "Right clicked " + name; },
	                                                          .middleClick = [name] { Hovered = "Middle clicked " + name; },
	                                                          .hover = [name](bool over) { if (over) Hovered = "Over " + name; } });
	                                }
	                                rows.push_back(std::move(row));
	                            }
	                            return rows;
	                        }),
	                    };
	                } },
	              { .tab = { "pie-chart", "Charts" }, .items = {
	                    App::Chart({ { 12, { 230, 190, 80 }, "Found" }, { 5, { 120, 180, 90 }, "Perfect" }, { 30, { 70, 70, 70 }, "Missing" } }, 420, "36%", "17 / 47"),
	                    App::Squares({ { .icon = Gui::Icons::Sprite("broadsword", 70, { 215, 200, 150 }), .tooltip = "Weapons", .ring = { { 3, { 230, 190, 80 } }, { 7, { 70, 70, 70 } } }, .click = [] { App::Notice("Weapons"); } },
	                                   { .icon = Gui::Icons::Sprite("visored-helm", 70, { 215, 200, 150 }), .tooltip = "Armor", .ring = { { 6, { 230, 190, 80 } }, { 2, { 70, 70, 70 } } }, .click = [] { App::Notice("Armor"); } },
	                                   { .icon = Gui::Icons::Sprite("ring", 70, { 215, 200, 150 }), .tooltip = "Jewelry", .ring = { { 1, { 230, 190, 80 } }, { 9, { 70, 70, 70 } } }, .click = [] { App::Reveal("ring 5"), App::Go(2, 0); } } }),
	                    App::Field("Objective", &Objective, "Type an objective").Icon(Gui::Icons::Sprite(ObjectiveIcons[ObjectiveIcon], 40, { 215, 200, 150 }), [] {
	                        std::vector<std::string> sprites;
	                        for (const char* icon : ObjectiveIcons) sprites.push_back(Gui::Icons::Sprite(icon, 50, { 215, 200, 150 }));
	                        App::PickIcon(sprites, [](int k) { ObjectiveIcon = k; }, {}, "The objective's icon");
	                    }),
	                } },
	          } },
	        { .tab = { "checklist", "List" }, .title = "A list", .content = [] {
	              return std::vector<App::Item> {
	                  App::Field("", &Search, "Search rows").Tip("Type to filter the list"),
	                  App::Text(Picked),
	                  App::List(Rows, [](int row) {
	                      const auto rows = Rows();
	                      if (row >= static_cast<int>(rows.size())) return;
	                      Picked = "Clicked " + rows[static_cast<std::size_t>(row)].text;
	                      App::Push({ .title = rows[static_cast<std::size_t>(row)].text, .subtitle = "A page opened from the list", .items = {
	                                      App::Text("The arrow at the top left goes back to the list, where it was scrolled."),
	                                      App::Number("A decimal", &Scale, 0.0, 10.0).Decimals(2).Step(0.05).Tip("Shift and Ctrl take bigger steps"),
	                                      App::Position("A position", &PosX, &PosY).Reset([] { PosX = PosY = 0; }),
	                                      App::Action("Twice to run", "Run", [] { App::Notice("Done."); }).Confirm(),
	                                  } });
	                  }).Empty("No row matches"),
	              };
	          } },
	    },
	    .console = "guidemo",
	    .keybind = "Open D2R GUI Demo",
	    .changed = [] { Gui::Log(false, "D2R GUI Demo: a setting changed (odd %d, mode %d, monster %d, number %d)", ShowOdd, Mode, Monster, Number); },
	};
}

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* { return &Info; }

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (!D2RL::HasContext(context) || !context->GetApi()) return false;
	if (!App::Start(context, PluginId, Describe())) {
		Gui::Log(true, "D2R GUI Demo %s: inactive, a service it needs is missing", PluginVersion);
		return true;
	}
	Gui::Log(false, "D2R GUI Demo %s (D2R GUI %s): ready", PluginVersion, D2R_GUI_VERSION);
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept { App::Stop(); }
