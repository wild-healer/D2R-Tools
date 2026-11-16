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
		rows.push_back({ name, i % 7 == 0 ? "seven" : "", "Click to pick it" });
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
