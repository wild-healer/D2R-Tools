// D2R GUI Demo (plugin id "d2r-gui-demo"): a window built only from the D2R
// GUI library (lib/d2r-gui), to try its parts in game and as an example of
// using it. It changes nothing in the game.
//
// Open it with the console command "guidemo" or the Controls menu
// (D2R GUI Demo > Open D2R GUI Demo). It shows: a search box (typing), a
// check box, a drop-down, a number with arrows, a reset button, and a long
// list that scrolls (wheel, scroll bar, drag) inside a clip view. It closes
// the game's left panels and moves the view like they do.

#include <d2r_gui.h>
#include <algorithm>
#include <atomic>
#include <string>
#include <vector>

namespace {

namespace Gui = D2RGui;

constexpr const char* PluginId      = "d2r-gui-demo";
constexpr const char* PluginVersion = "0.0.1";

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

// ---- geometry (HD layout units, the character screen's size) ----
constexpr int PanelX = 236, PanelY = -651, PanelWidth = 1162, PanelHeight = 1507;
constexpr int Left = 105, Right = 1044, BarLeft = 1054;
constexpr int SearchY = 180, BoxHeight = 56;
constexpr int OptionsY = 260, OptionHeight = 70;
constexpr int ListTop = 520, ListBottom = 1360, ListRoom = ListBottom - ListTop, RowHeight = 80;
constexpr int WheelStep = 120;

// ---- state (UI thread) ----
Gui::PanelPool    Window;
Gui::ScrollBar    Bar("list", BarLeft, 30, 30, PanelX, PanelY);
std::atomic<bool> WindowOpen {};
std::string       Search;
bool              ShowOdd = true;
int               Choice  = 0;
int               Number  = 10;
int               Scroll  = 0;
bool              DropOpen {};
int               Picked = -1;
const std::vector<std::string> Choices { "First choice", "Second choice", "Third choice" };

std::vector<std::string> Rows() {
	std::vector<std::string> rows;
	for (int i = 1; i <= 60; ++i) {
		if (!ShowOdd && i % 2) continue;
		const std::string name = "Row " + std::to_string(i) + (i % 7 == 0 ? " (seven)" : "");
		std::string lower = name, query = Search;
		for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		for (auto& c : query) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		if (query.empty() || lower.find(query) != std::string::npos) rows.push_back(name);
	}
	return rows;
}

std::string Background() {
	static std::string name;
	if (name.empty() && Gui::RegisterSprite("background", { Gui::PlainBox(PanelWidth, PanelHeight, 120) })) name = Gui::SpriteName("background");
	return name;
}

std::string Build(const std::string& panelName, const std::string& probe) {
	std::string c = Gui::Image("Background", 0, 0, Background());
	c += Gui::LeftPanelFrame(PanelWidth, PanelHeight, "D2R GUI Demo", "close", 1075);
	c += Gui::Probe(probe);

	c += Gui::TypedBox("Search", "search", Left, SearchY, Right - Left, BoxHeight, Search, "Search rows", "Type to filter the list");

	int y = OptionsY;
	c += Gui::Text("OddLabel", Left, y, 500, OptionHeight, "Show odd rows", Gui::Style::Label);
	c += Gui::Check("Odd", Right - 60, y + 7, ShowOdd, "odd", "Shows or hides the odd rows");
	y += OptionHeight;
	c += Gui::Text("NumberLabel", Left, y, 500, OptionHeight, "A number", Gui::Style::Label);
	c += Gui::Stepper("Number", Right - 480, y, 400, OptionHeight, std::to_string(Number), "number-", "number+", "Click the arrows");
	c += Gui::Button("Reset", Right - 60, y + 7, Gui::Kit::Reset(56), { 0, 2, 1 }, "reset", "Back to 10");
	y += OptionHeight;
	c += Gui::Text("ChoiceLabel", Left, y, 500, OptionHeight, "A choice", Gui::Style::Label);
	const int dropX = Right - 420, dropY = y + 7;
	c += Gui::DropDown::Box("Choice", dropX, dropY, 420, BoxHeight, Choices[static_cast<std::size_t>(Choice)], "drop");
	y += OptionHeight;
	c += Gui::Text("Picked", Left, y, Right - Left, 50, Picked < 0 ? "Click a row" : "Clicked row " + std::to_string(Picked + 1), Gui::Style::Note);

	// the list, clipped, scrolled by pixels
	const auto rows  = Rows();
	const int  total = static_cast<int>(rows.size()) * RowHeight;
	Scroll           = std::clamp(Scroll, 0, std::max(0, total - ListRoom));
	std::string inside;
	const int   first = Scroll / RowHeight, last = std::min(static_cast<int>(rows.size()), (Scroll + ListRoom) / RowHeight + 1);
	for (int i = first; i < last; ++i) {
		const int         top = i * RowHeight - Scroll;
		const std::string n   = "Row" + std::to_string(i - first); // names reused view to view
		inside += Gui::Image(n + "Back", Left, top + 3, Gui::Kit::Row(Right - Left, RowHeight - 6));
		inside += Gui::Text(n + "Name", Left + 30, top, Right - Left - 140, RowHeight, rows[static_cast<std::size_t>(i)], Gui::Style::Label);
		inside += Gui::Button(n + "Gear", Right - 74, top + 8, Gui::Kit::Gear(64), { 0, 2, 1 }, "row" + std::to_string(i), "Click me");
	}
	if (inside.empty()) inside = Gui::Text("Empty", Left, 0, Right - Left, RowHeight, "No row matches", Gui::Style::Note);
	c += Gui::Clip("List", 0, ListTop, BarLeft - 6, ListRoom, inside);
	c += Bar.Layout(ListTop, ListRoom, total, Scroll);

	if (DropOpen) c += Gui::DropDown::List("ChoiceList", dropX, dropY + BoxHeight, 420, 52, Choices, Choice, "pick:", "drop", PanelWidth, PanelHeight);
	return Gui::Panel(panelName, PanelX, PanelY, PanelWidth, PanelHeight, c, 10, Gui::LeftAnchor);
}

void CloseWindow() {
	Gui::TextInput::Stop();
	Window.Close();
	WindowOpen = false;
	DropOpen   = false;
	Gui::LeftPanel::Closed();
}

void Watch() {
	const bool open = Window.IsOpen();
	WindowOpen      = open;
	if (!open && !Gui::TextInput::Key().empty()) Gui::TextInput::Stop();
	if (Gui::LeftPanel::Watch(open, Window.Busy())) CloseWindow();
}

void Toggle() {
	if (Window.IsOpen()) return CloseWindow();
	Gui::Font::Refresh();
	Gui::WindowHook::Start({
	    .active = [] { return WindowOpen.load(); },
	    .wheel  = [](int notches) {
            Scroll = std::clamp(Scroll + notches * WheelStep, 0, std::max(0, Bar.Max()));
            Window.Show();
	    },
	    .watch = Watch,
	});
	static bool attached = false;
	if (!attached) Bar.Attach(), attached = true;
	Gui::LeftPanel::Opening();
	Window.Show();
	WindowOpen = true;
}

void StartSearch() {
	Gui::TextInput::Start("search", Search, {
	    .changed = [](const std::string& text) { Search = text, Scroll = 0, Window.Show(); },
	    .enter   = [](const std::string&) { Window.Show(); },
	    .escape  = [] { Search.clear(), Window.Show(); },
	});
	Window.Show();
}

void OnCommand(const std::string& command) {
	if (command == "toggle") return Toggle();
	if (!Window.IsOpen() && command != "close") return;
	if (Bar.Handle(command)) return;
	if (command == "close") return CloseWindow();
	if (command == "typesearch") return StartSearch();
	if (command == "odd") ShowOdd = !ShowOdd;
	else if (command == "number-") Number = std::max(0, Number - 1);
	else if (command == "number+") Number = std::min(99, Number + 1);
	else if (command == "reset") Number = 10;
	else if (command == "drop") DropOpen = !DropOpen;
	else if (command.rfind("pick:", 0) == 0) Choice = std::clamp(std::atoi(command.c_str() + 5), 0, static_cast<int>(Choices.size()) - 1), DropOpen = false;
	else if (command.rfind("row", 0) == 0) Picked = std::atoi(command.c_str() + 3);
	Window.Show();
}

void __cdecl AfterAllLoaded(const D2RL::PluginContext*, const D2RL::Lifecycle::DataTablesLoadedEvent*, void*) noexcept {
	static bool done = false;
	if (done) return;
	done = true;
	Gui::Kit::Read();
	Gui::Font::Measure();
}

void __cdecl OnPlayerReady(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept { Gui::LeftPanel::InGame(true); }
void __cdecl OnGameLeft(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept { Gui::LeftPanel::InGame(false); }

auto OnOpenAction(const D2RL::PluginContext*, const D2RL::Input::ActionEvent* event, void*) noexcept -> D2RL::Input::ActionResult {
	if (!D2RL::Input::HasActionEventField(event, D2RL::Input::ActionEventRequiredSize) || event->kind != D2RL::Input::ActionEventKind::Pressed) return D2RL::Input::ActionResult::Ignored;
	Gui::Commands::Post("toggle");
	return D2RL::Input::ActionResult::Handled;
}

auto DemoCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
	if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
	Gui::Commands::Post("toggle");
	return D2RL::ConsoleCommandResult::Handled;
}

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* { return &Info; }

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (!D2RL::HasContext(context) || !context->GetApi()) return false;
	if (!Gui::Init(context, PluginId)) {
		Gui::Log(true, "D2R GUI Demo %s: inactive, a service it needs is missing", PluginVersion);
		return true;
	}
	Window.Register(Build, "Demo", 16);
	Bar.OnScroll([](int scroll) { Scroll = scroll, Window.Show(); });
	Gui::Commands::Listen(OnCommand);

	if (const auto* lifecycle = Gui::Detail::Query<D2RL::LifecycleService>(context, "lifecycle")) {
		D2RL::Lifecycle::ListenerHandle                 handle {};
		const D2RL::Lifecycle::DataTablesLoadedListener loaded { .structSize = sizeof(D2RL::Lifecycle::DataTablesLoadedListener), .callback = AfterAllLoaded };
		(void)lifecycle->registerDataTablesLoadedListener(context, &loaded, &handle);
		const D2RL::Lifecycle::GameplayEventListener ready { .structSize = D2RL::Lifecycle::GameplayEventListenerSize, .kind = D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady, .callback = OnPlayerReady };
		(void)lifecycle->registerGameplayEventListener(context, &ready, &handle);
		const D2RL::Lifecycle::GameplayEventListener left { .structSize = D2RL::Lifecycle::GameplayEventListenerSize, .kind = D2RL::Lifecycle::GameplayEventKind::GameLeft, .callback = OnGameLeft };
		(void)lifecycle->registerGameplayEventListener(context, &left, &handle);
	}
	if (const auto* input = Gui::Detail::Query<D2RL::InputService>(context, "input"); input && D2RL::HasInputServiceField(input, D2RL::InputServiceRequiredSize)) {
		const D2RL::Input::ActionRegistration action {
			.structSize       = D2RL::Input::ActionRegistrationSize,
			.logicalId        = "open-d2r-gui-demo",
			.displayName      = "Open D2R GUI Demo",
			.category         = "D2R GUI Demo",
			.defaultPrimary   = { .key = D2RL::Input::Key::None },
			.defaultSecondary = { .key = D2RL::Input::Key::None },
			.callback         = OnOpenAction,
		};
		D2RL::Input::ActionHandle handle = D2RL::Input::InvalidHandle;
		(void)input->registerAction(context, &action, &handle);
	}
	(void)context->RegisterConsoleCommand("guidemo", DemoCommand, "Opens or closes the D2R GUI Demo window.");
	Gui::Log(false, "D2R GUI Demo %s (D2R GUI %s): ready on D2R %s", PluginVersion, D2R_GUI_VERSION, context->buildVersion ? context->buildVersion : "?");
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	Gui::TextInput::Shutdown();
	Gui::WindowHook::Stop();
}
