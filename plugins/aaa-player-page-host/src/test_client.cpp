// Page host test client. One source, built twice:
//   PH_EARLY -> page-host-test-early (aa-page-host-test-early.dll, loads BEFORE the host):
//               acquires in its first DataTablesLoaded listener, 8x4 area at 0,12,
//               the host's default layout, takes anything. Console: pagetest1
//   PH_LATE  -> page-host-test-late (d2rl-page-host-test-late.dll, loads after the host):
//               acquires at load, 6x4 area, its own layout, takes only charms, and
//               checks that an overlapping request is refused. Console: pagetest2
// Every step goes to the plugin's own log as "CHECK n ok|FAIL".
#include "page_host_api.h"

#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

namespace Comm = D2RL::PluginCommunication;

#if defined(PH_EARLY)
constexpr const char* PluginId = "page-host-test-early";
constexpr const char* PluginName = "Page Host Test (early)";
constexpr const char* CommandName = "pagetest1";
#else
constexpr const char* PluginId = "page-host-test-late";
constexpr const char* PluginName = "Page Host Test (late)";
constexpr const char* CommandName = "pagetest2";
#endif

const D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = PluginId,
	.name        = PluginName,
	.version     = "1.0.0",
	.author      = "wild-healer",
	.description = "Test client of Player Page Host. Remove after the test.",
	.flags       = D2RL::PluginFlags::Client,
};

const D2RL::PluginContext*              Context {};
const D2RL::PluginCommunicationService* CommApi {};
Comm::ServiceLease<PageHost::Service>   Host;
PageHost::AreaHandle                    Area {};
PageHost::AreaInfo                      AreaNow { .structSize = sizeof(PageHost::AreaInfo) };

// The area's place, once the host has packed the areas.
void Refresh() {
	if (Area && Host) (void)Host->getAreaInfo(Area, &AreaNow);
}

void Log(const char* format, ...) noexcept {
	char    line[768] {};
	va_list args;
	va_start(args, format);
	std::vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	Context->LogInfo(line);
}

#if !defined(PH_EARLY)
auto __cdecl CharmsOnly(PageHost::AreaHandle, const D2RL::Inventory::ItemPolicyRequest* request, void*) noexcept -> D2RL::Inventory::ItemPolicyDecision {
	const bool charm = D2RL::Inventory::ItemPolicyRequestMatchesType(request, D2RL::Inventory::MakeItemTypeFourCC("char"));
	Log("CHECK 5 policy asked: %s", charm ? "a charm, allowed" : "not a charm, refused");
	return charm ? D2RL::Inventory::ItemPolicyDecision::Allow : D2RL::Inventory::ItemPolicyDecision::Reject;
}

// The late client's own panel: the host's 6x4 background (its cells 48,110 in), a
// title and a hint, the grid at 60,140.
std::string OwnLayout() {
	return R"({"type": "Panel", "name": "@PANEL@", "fields": {"priority": 10, "rect": {"x": 236, "y": -856, "width": 708, "height": 900}, "anchor": "$LeftPanelAnchor"}, "children": [)"
	       R"({"type": "ClickCatcherWidget", "name": "PanelClickCatcher", "fields": {"rect": {"x": 0, "y": 0, "width": 708, "height": 900}}})"
	       R"(, {"type": "ImageWidget", "name": "Background", "fields": {"rect": {"x": 12, "y": 30}, "filename": "d2rloader\\aaa-player-page-host\\bg6x4"}})"
	       R"(, {"type": "TextBoxWidget", "name": "Title", "fields": {"rect": {"x": 0, "y": 10, "width": 708, "height": 70}, "text": "Late client: active charms", "style": {"fontColor": "$FontColorGreen", "pointSize": 40, "dropShadow": "$DefaultDropShadow", "alignment": {"h": "center", "v": "center"}}}})"
	       R"(, @GRID@)"
	       R"(, {"type": "TextBoxWidget", "name": "Hint", "fields": {"rect": {"x": 0, "y": 820, "width": 708, "height": 60}, "text": "Esc closes", "style": {"fontColor": "$FontColorGray", "pointSize": 28, "alignment": {"h": "center", "v": "center"}}}})"
	       R"(]})";
}
#endif

bool Join(const char* when) {
	if (Area) return true;
	if (!CommApi) return false;
	const auto acquired = PageHost::Acquire(Context, CommApi, &Host);
	if (acquired != Comm::Result::Success) {
		Log("%s: host not found (result %u)", when, static_cast<unsigned>(acquired));
		return false;
	}
	Log("CHECK 1 ok: %s: host service v%u acquired", when, Host.Version());
#if defined(PH_EARLY)
	const PageHost::AreaRequest request { .structSize = sizeof(request), .name = "box", .label = "Early client: anything", .width = 8, .height = 4 };
#else
	const PageHost::AreaRequest request { .structSize = sizeof(request), .flags = PageHost::AreaCharms, .name = "box", .label = "Late client", .width = 6, .height = 4 };
#endif
	const auto reserved = Host->reserveArea(Context, &request, &AreaNow);
	if (reserved != PageHost::Result::Success) {
		Log("CHECK 2 FAIL: reserveArea result %u", static_cast<unsigned>(reserved));
		return false;
	}
	Area = AreaNow.handle;
	Log("CHECK 2 ok: area at %d,%d %ux%u, panel %s, charms active %u", AreaNow.x, AreaNow.y, AreaNow.width, AreaNow.height, AreaNow.panelName, AreaNow.charmsActive);
#if !defined(PH_EARLY)
	const PageHost::LayoutRequest layout { .structSize = sizeof(layout), .layout = nullptr, .gridX = 60, .gridY = 140 };
	std::string text = OwnLayout();
	PageHost::LayoutRequest own = layout;
	own.layout = text.c_str();
	Log("CHECK 4 %s: own layout set", Host->setAreaLayout(Area, &own) == PageHost::Result::Success ? "ok" : "FAIL");
	Log("policy set: %u", static_cast<unsigned>(Host->setItemPolicy(Area, CharmsOnly, nullptr)));
#endif
	return true;
}

void __cdecl AfterAllLoaded(const D2RL::PluginContext*, const D2RL::Lifecycle::DataTablesLoadedEvent*, void*) noexcept {
	static bool done = false;
	if (done) return;
	done = true;
	if (!Join("first DataTablesLoaded")) Log("CHECK 1 FAIL: no host after every plugin loaded");
	Refresh();
	Log("area now at %d,%d %ux%u (-1: placed after this plugin's turn)", AreaNow.x, AreaNow.y, AreaNow.width, AreaNow.height);
#if !defined(PH_EARLY)
	// The areas are packed now: a whole-page area can't fit beside them.
	if (Host) {
		PageHost::AreaInfo          clash { .structSize = sizeof(clash) };
		const PageHost::AreaRequest whole { .structSize = sizeof(whole), .name = "clash", .label = "", .width = 16, .height = 16 };
		const auto                  asked = Host->reserveArea(Context, &whole, &clash);
		Log("CHECK 3 %s: a 16x16 area after packing: %u (4 = NoRoom expected)", asked == PageHost::Result::NoRoom ? "ok" : "FAIL", static_cast<unsigned>(asked));
		if (asked == PageHost::Result::Success) (void)Host->releaseArea(Context, clash.handle);
	}
#endif
}

#if !defined(PH_EARLY)
// "pagetest2 create": a minor healing potion in the area's cell 0,0 with the
// host's page handle straight through D2RLoader (does it take another
// plugin's handle?), then one in cell 1,0 through the host's createItem.
D2RL::PlayerHandle Player {};
void __cdecl CreateTest(const D2RL::PluginContext*, void*) noexcept {
	const D2RL::ItemService* items = nullptr;
	if (Context->QueryService(&items) != D2RL::ServiceQueryResult::Success || !items) {
		Log("CHECK 7 FAIL: no item service");
		return;
	}
	D2RL::Items::ItemCreateSpec spec {};
	spec.structSize  = D2RL::Items::ItemCreateSpecSize;
	spec.code        = D2RL::Items::MakeItemCode("hp1");
	spec.quality     = D2RL::Items::Quality::Normal;
	spec.qualityRecordId = D2RL::Items::RandomQualityRecord;
	spec.itemLevel   = 1;
	spec.seedMode    = D2RL::Items::SeedMode::Random;
	spec.quantity    = D2RL::Items::DefaultValue;   // 0 is refused (InvalidArgument)
	spec.durability  = D2RL::Items::DefaultValue;
	spec.destination = { .structSize = sizeof(D2RL::Items::ItemDestination), .container = D2RL::Items::ItemContainer::CustomPage, .placement = D2RL::Items::Placement::Exact,
		                 .customPageHandle = AreaNow.pageHandle, .x = static_cast<std::uint32_t>(AreaNow.x), .y = static_cast<std::uint32_t>(AreaNow.y) };
	D2RL::ItemHandle item {};
	const auto direct = items->createItem(Context, Player, &spec, &item);
	Log("CHECK 7: createItem with the host's page handle from this plugin: item result %u (%s)", static_cast<unsigned>(direct),
	    direct == D2RL::Items::Result::Success ? "accepted" : direct == D2RL::Items::Result::OwnerMismatch ? "OwnerMismatch: use the host" : "refused");
	spec.destination.x = 1, spec.destination.y = 0; // area-relative through the host
	std::uint32_t itemResult = 0, runtimeId = 0;
	const auto viaHost = Host->createItem(Area, &spec, &runtimeId, &itemResult);
	Log("CHECK 8 %s: host createItem result %u, item result %u, runtime id %u", viaHost == PageHost::Result::Success && runtimeId ? "ok" : "FAIL", static_cast<unsigned>(viaHost), itemResult,
	    runtimeId);
	// The client reads the new item with its own handles.
	struct Seek {
		std::uint32_t runtimeId;
		int           x, y;
		bool          found;
	} seek { runtimeId, -1, -1, false };
	const D2RL::InventoryService* inventory = nullptr;
	(void)Context->QueryService(&inventory);
	const D2RL::Inventory::ItemFilter everything { .structSize = sizeof(D2RL::Inventory::ItemFilter), .containerMask = ~0U };
	const auto look = [&]() {
		seek.found = false;
		(void)inventory->forEachInventoryItem(Context, Player, &everything,
		                                      [](const D2RL::PluginContext*, const D2RL::Items::ItemInfo* info, void* user) noexcept {
			                                      auto* s = static_cast<Seek*>(user);
			                                      if (info && info->runtimeId == s->runtimeId) {
				                                      s->found = info->container == D2RL::Items::ItemContainer::CustomPage, s->x = info->x, s->y = info->y;
				                                      return D2RL::Inventory::IterationAction::Stop;
			                                      }
			                                      return D2RL::Inventory::IterationAction::Continue;
		                                      },
		                                      &seek);
	};
	if (inventory && runtimeId) look();
	Log("CHECK 9 %s: the client sees it on the page at %d,%d (area cell 1,0 = page %d,%d)", seek.found && seek.x == AreaNow.x + 1 && seek.y == AreaNow.y ? "ok" : "FAIL", seek.x, seek.y,
	    AreaNow.x + 1, AreaNow.y);
	// And moves it one cell right through the host, by runtime id.
	D2RL::Items::ExistingItemOperation move {};
	move.structSize = sizeof(move);
	move.kind       = D2RL::Items::ExistingItemOperationKind::Move;
	move.item       = runtimeId;
	move.move.destination = { .structSize = sizeof(D2RL::Items::ItemDestination), .container = D2RL::Items::ItemContainer::CustomPage, .placement = D2RL::Items::Placement::Exact, .x = 2, .y = 0 };
	const D2RL::Items::ExistingItemTransaction transaction { .structSize = sizeof(transaction), .operationCount = 1, .operations = &move };
	D2RL::Items::ExistingItemTransactionResult outcome { .structSize = sizeof(outcome) };
	const auto moved = Host->moveItems(Area, &transaction, &outcome, &itemResult);
	if (inventory && runtimeId) look();
	Log("CHECK 10 %s: host moveItems result %u, item result %u; now at %d,%d", moved == PageHost::Result::Success && seek.x == AreaNow.x + 2 ? "ok" : "FAIL", static_cast<unsigned>(moved), itemResult,
	    seek.x, seek.y);
	// Direct destroy of the potion made straight with the page handle (cell 0,0),
	// with this plugin's own handle; then the host's destroy of the moved one.
	D2RL::ItemHandle own {};
	struct First {
		int x, y;
		D2RL::ItemHandle handle;
	} first { AreaNow.x, AreaNow.y, {} };
	if (inventory)
		(void)inventory->forEachInventoryItem(Context, Player, &everything,
		                                      [](const D2RL::PluginContext*, const D2RL::Items::ItemInfo* info, void* user) noexcept {
			                                      auto* f = static_cast<First*>(user);
			                                      if (info && info->container == D2RL::Items::ItemContainer::CustomPage && info->x == f->x && info->y == f->y) {
				                                      f->handle = info->handle;
				                                      return D2RL::Inventory::IterationAction::Stop;
			                                      }
			                                      return D2RL::Inventory::IterationAction::Continue;
		                                      },
		                                      &first);
	own = first.handle;
	if (own) {
		const auto gone = items->destroyItem(Context, Player, own, D2RL::Items::SocketedItemPolicy::DestroyContents);
		Log("CHECK 11: destroyItem on a page item with this plugin's own handle: item result %u (%s)", static_cast<unsigned>(gone), gone == D2RL::Items::Result::Success ? "works" : "refused: use the host");
	} else Log("CHECK 11: skipped (no item at the area's cell 0,0)");
	const auto destroyed = Host->destroyItem(Area, runtimeId, D2RL::Items::SocketedItemPolicy::DestroyContents, &itemResult);
	Log("CHECK 12 %s: host destroyItem result %u, item result %u", destroyed == PageHost::Result::Success ? "ok" : "FAIL", static_cast<unsigned>(destroyed), itemResult);
}
#endif

auto Command(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
	if (!Area || !Host) {
		Context->WriteConsoleMessage("no area (see this plugin's log)");
		return D2RL::ConsoleCommandResult::Handled;
	}
	const std::string args = command && command->args ? command->args : "";
	Refresh();
#if !defined(PH_EARLY)
	if (args == "create") {
		const D2RL::InventoryService* inventory = nullptr;
		const D2RL::ThreadService*    threads   = nullptr;
		if (Context->QueryService(&inventory) != D2RL::ServiceQueryResult::Success || !inventory || inventory->getLocalPlayer(Context, &Player) != D2RL::Inventory::Result::Success
		    || Context->QueryService(&threads) != D2RL::ServiceQueryResult::Success || !threads || threads->runOnGameThread(Context, CreateTest, nullptr) != D2RL::Threads::Result::Success)
			Context->WriteConsoleMessage("could not queue the create test (in a game?)");
		return D2RL::ConsoleCommandResult::Handled;
	}
#else
	// A second area claimed in game, over whatever lies there: the host gives
	// items put in no area (or another) back at the next game load.
	static PageHost::AreaHandle spare = PageHost::InvalidArea;
	if (args == "claim" || args == "release") {
		char text[200] {};
		if (args == "release") {
			const auto released = spare ? Host->releaseArea(Context, spare) : PageHost::Result::NotFound;
			spare = PageHost::InvalidArea;
			std::snprintf(text, sizeof(text), "%s: spare release result %u", PluginId, static_cast<unsigned>(released));
		} else {
			const PageHost::AreaRequest request { .structSize = sizeof(request), .name = "spare", .label = "Early client: spare", .width = 2, .height = 4 };
			PageHost::AreaInfo info { .structSize = sizeof(info) };
			const auto reserved = spare ? PageHost::Result::Busy : Host->reserveArea(Context, &request, &info);
			if (reserved == PageHost::Result::Success) spare = info.handle;
			std::snprintf(text, sizeof(text), "%s: spare 2x4 at 14,12 claim result %u", PluginId, static_cast<unsigned>(reserved));
		}
		Log("CHECK 13: %s", text);
		Context->WriteConsoleMessage(text);
		return D2RL::ConsoleCommandResult::Handled;
	}
#endif
	if (args == "close") {
		(void)Host->closeArea(Area);
		return D2RL::ConsoleCommandResult::Handled;
	}
	const auto opened = Host->openArea(Area);
	char text[200] {};
	std::snprintf(text, sizeof(text), "%s: open result %u, area %d,%d %ux%u", PluginId, static_cast<unsigned>(opened), AreaNow.x, AreaNow.y, AreaNow.width, AreaNow.height);
	Log("CHECK 6 %s: %s", opened == PageHost::Result::Success ? "ok" : "FAIL", text);
	Context->WriteConsoleMessage(text);
	return D2RL::ConsoleCommandResult::Handled;
}

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (!D2RL::HasContext(context) || !context->GetApi()) return false;
	Context = context;
	if (context->QueryService(&CommApi) != D2RL::ServiceQueryResult::Success) CommApi = nullptr;
	if (!Join("load")) Log("will try again when every plugin has loaded");
	if (const D2RL::LifecycleService* lifecycle = nullptr; context->QueryService(&lifecycle) == D2RL::ServiceQueryResult::Success && lifecycle) {
		const D2RL::Lifecycle::DataTablesLoadedListener listener { .structSize = D2RL::Lifecycle::DataTablesLoadedListenerSize, .callback = AfterAllLoaded };
		D2RL::Lifecycle::ListenerHandle handle {};
		(void)lifecycle->registerDataTablesLoadedListener(context, &listener, &handle);
	}
	(void)context->RegisterConsoleCommand(CommandName, Command, "Page host test: open this client's area ('close' closes it).");
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	if (Area && Host) (void)Host->releaseArea(Context, Area);
	Area = 0;
	(void)Host.Reset();
}
