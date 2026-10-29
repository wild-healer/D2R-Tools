// Wild Healer's Charm Inventory: a charm grid on Player Page Host's page.
//   - reserves one 10x4 area of the host's page (the size of the inventory),
//     with active charms, and takes only charms;
//   - shows it in a game-style left panel: the stash frame Run Counter ships
//     (src/art/frame.png), its plain stone inside, and the area's cells;
//   - a button on the inventory's left edge, made like Grail On!'s (the mold
//     and Grail On!'s charm category icon), under Grail On!'s, opens and
//     closes it.
#include "page_host_api.h"

#include <windows.h>
#include <wincodec.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include "mold.inc"
#include "charm_icon.inc"

namespace {

namespace Comm   = D2RL::PluginCommunication;
namespace Events = D2RL::SharedEvents;

constexpr const char* PluginId      = "wild-healers-charm-inventory";
constexpr const char* PluginVersion = "1.0.0";
constexpr const char* CommandPrefix = "wild-healers-charm-inventory/Cmd-";

const D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = PluginId,
	.name        = "Wild Healer's Charm Inventory",
	.version     = PluginVersion,
	.author      = "wild-healer",
	.description = "A 10x4 charm inventory on Player Page Host's page, in a game-style panel opened from a button beside the inventory or a key.",
	.flags       = D2RL::PluginFlags::Client,
};

const D2RL::PluginContext*              Context {};
const D2RL::PluginCommunicationService* CommApi {};
const D2RL::ResourceService*            ResourceApi {};
const D2RL::PanelService*               PanelApi {};
const D2RL::SharedEventService*         EventApi {};
Comm::ServiceLease<PageHost::Service>   Host;
PageHost::AreaHandle                    Area {};
constexpr int                           Columns = 10, Rows = 4, Cell = 98;

void Log(bool error, const char* format, ...) noexcept {
	char    line[512] {};
	va_list args;
	va_start(args, format);
	std::vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	if (error) Context->LogError(line);
	else Context->LogInfo(line);
}

// ---- pictures ----

struct Picture {
	int                        width {}, height {};
	std::vector<unsigned char> rgba;
	unsigned char*             at(int x, int y) { return rgba.data() + (static_cast<std::size_t>(y) * width + x) * 4; }
	const unsigned char*       at(int x, int y) const { return rgba.data() + (static_cast<std::size_t>(y) * width + x) * 4; }
};

Picture Blank(int width, int height) { return { width, height, std::vector<unsigned char>(static_cast<std::size_t>(width) * height * 4) }; }

Picture Half(const Picture& in) {
	Picture out = Blank(std::max(1, in.width / 2), std::max(1, in.height / 2));
	for (int y = 0; y < out.height; ++y)
		for (int x = 0; x < out.width; ++x)
			for (int k = 0; k < 4; ++k) {
				int total = 0;
				for (int dy = 0; dy < 2; ++dy)
					for (int dx = 0; dx < 2; ++dx) total += in.at(std::min(in.width - 1, 2 * x + dx), std::min(in.height - 1, 2 * y + dy))[k];
				out.at(x, y)[k] = static_cast<unsigned char>(total / 4);
			}
	return out;
}

// Frames side by side, all the same size (SpA1 version 31).
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

bool RegisterResource(const std::string& path, const std::string& bytes) {
	static std::vector<std::string> kept;
	kept.push_back(bytes);
	const D2RL::Resources::ResourceRegistration resource {
		.structSize = D2RL::Resources::ResourceRegistrationSize,
		.path       = path.c_str(),
		.bytes      = kept.back().data(),
		.byteCount  = kept.back().size(),
	};
	D2RL::Resources::RegistrationHandle ignored = D2RL::Resources::InvalidHandle;
	return ResourceApi->registerResource(Context, &resource, &ignored) == D2RL::Resources::Result::Success;
}

// A sprite and its low-end copy (half size unless given).
bool RegisterSprite(const std::string& name, const std::vector<Picture>& frames, std::vector<Picture> low = {}) {
	if (low.empty())
		for (const auto& frame : frames) low.push_back(Half(frame));
	const std::string path = std::string("data/hd/global/ui/d2rloader/") + PluginId + "/" + name;
	return RegisterResource(path + ".sprite", SpriteFile(frames))
	    && RegisterResource(path + ".lowend.sprite", SpriteFile(low));
}

// ---- packaged art ----

constexpr int FrameArtId = 201, FrameArtLowEndId = 202, SlotArtId = 203;

// A PNG resource of this DLL, decoded with Windows' imaging component.
bool DecodePng(int id, Picture& out) {
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
	bool done = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) && SUCCEEDED(factory->CreateStream(&stream))
	         && SUCCEEDED(stream->InitializeFromMemory(bytes, size)) && SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder))
	         && SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(factory->CreateFormatConverter(&converter))
	         && SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))
	         && SUCCEEDED(converter->GetSize(&width, &height));
	if (done) {
		out  = Blank(static_cast<int>(width), static_cast<int>(height));
		done = SUCCEEDED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(out.rgba.size()), out.rgba.data()));
	}
	for (IUnknown* object : std::initializer_list<IUnknown*> { converter, frame, decoder, stream, factory })
		if (object) object->Release();
	if (SUCCEEDED(com)) CoUninitialize();
	return done;
}

// ---- the inventory button (as Grail On!'s) ----

// Under Grail On!'s button (its mold at 3,954). Frames: 0 normal, 1 pressed,
// 2 disabled, 3 hovered.
constexpr int ButtonX = 3, ButtonY = 954 + MoldHeight, ButtonWidth = 52, ButtonHeight = 60, IconDrawn = 42;

// The charm icon in the game's gold, alone (no button frame), with a soft
// light behind it on hover.
std::vector<Picture> CharmButton() {
	std::vector<double> cover(static_cast<std::size_t>(IconDrawn) * IconDrawn);
	const double        step = static_cast<double>(IconSize) / IconDrawn;
	for (int y = 0; y < IconDrawn; ++y)
		for (int x = 0; x < IconDrawn; ++x) {
			double sum = 0;
			int    n   = 0;
			for (int sy = static_cast<int>(y * step); sy < static_cast<int>((y + 1) * step) && sy < IconSize; ++sy)
				for (int sx = static_cast<int>(x * step); sx < static_cast<int>((x + 1) * step) && sx < IconSize; ++sx) sum += Icon_char[sy * IconSize + sx], ++n;
			cover[static_cast<std::size_t>(y) * IconDrawn + x] = n ? sum / n / 255.0 : 0;
		}
	const struct {
		int    r, g, b, dy;
		double glow;
	} looks[] { { 215, 200, 150, 0, 0 }, { 170, 155, 110, 1, 0 }, { 110, 105, 95, 0, 0 }, { 255, 238, 180, 0, 0.35 } };
	std::vector<Picture> frames;
	for (const auto& look : looks) {
		Picture   frame = Blank(ButtonWidth, ButtonHeight);
		const int left = (ButtonWidth - IconDrawn) / 2, top = (ButtonHeight - IconDrawn) / 2 + look.dy;
		for (int y = 0; y < ButtonHeight; ++y)
			for (int x = 0; x < ButtonWidth; ++x) {
				const int    ix = x - left, iy = y - top;
				const double a  = ix >= 0 && iy >= 0 && ix < IconDrawn && iy < IconDrawn ? cover[static_cast<std::size_t>(iy) * IconDrawn + ix] : 0;
				double       light = 0;
				if (look.glow > 0) {
					const double dx = (x + 0.5 - ButtonWidth / 2.0) / (ButtonWidth / 2.0), dy = (y + 0.5 - ButtonHeight / 2.0) / (ButtonHeight / 2.0);
					light           = look.glow * std::max(0.0, 1.0 - std::sqrt(dx * dx + dy * dy));
				}
				const double alpha = a + light * (1 - a);
				if (alpha <= 0) continue;
				auto* px = frame.at(x, y);
				px[0]    = static_cast<unsigned char>((look.r * a + 200 * light * (1 - a)) / alpha);
				px[1]    = static_cast<unsigned char>((look.g * a + 160 * light * (1 - a)) / alpha);
				px[2]    = static_cast<unsigned char>((look.b * a + 80 * light * (1 - a)) / alpha);
				px[3]    = static_cast<unsigned char>(255 * alpha);
			}
		frames.push_back(std::move(frame));
	}
	return frames;
}

std::string Json(const std::string& text) {
	std::string out = "\"";
	for (const char c : text) {
		if (c == '"' || c == '\\') out += '\\';
		out += c;
	}
	return out + "\"";
}

void AddButton() {
	const Picture mold { MoldWidth, MoldHeight, std::vector<unsigned char>(MoldRgba, MoldRgba + sizeof(MoldRgba)) };
	if (!RegisterSprite("mold", { mold }) || !RegisterSprite("button", CharmButton())) Log(true, "could not register the button art");
	const std::string click = Json(std::string("PanelManager:ClosePanel:") + CommandPrefix + "toggle");
	char              button[1024] {};
	std::snprintf(button, sizeof(button),
	              R"({"type": "ImageWidget", "name": "%s/InventoryButton", "fields": {"rect": {"x": %d, "y": %d, "width": %d, "height": %d}, "filename": "d2rloader/%s/mold"}, "children": [)"
	              R"({"type": "ButtonWidget", "name": "%s/CharmButton", "fields": {"rect": {"x": 1, "y": 39}, "filename": "d2rloader/%s/button", "normalFrame": 0, "pressedFrame": 1, "hoveredFrame": 3, )"
	              R"("pressLabelOffset": {"x": 0, "y": 2}, "tooltipString": "Charms", "sound": "cursor_menu_select_hd", "onClickMessage": %s}}]})",
	              PluginId, ButtonX, ButtonY, MoldWidth, MoldHeight, PluginId, PluginId, PluginId, click.c_str());
	(void)RegisterResource(std::string("data/global/ui/layouts/") + PluginId + "/InventoryButtonhd.json", button);
	const D2RL::Panels::ChildLayoutRegistration child {
		.structSize = D2RL::Panels::ChildLayoutRegistrationSize,
		.flags      = D2RL::Panels::ChildLayoutFlags::KeyboardMouseOnly,
		.stockPanel = D2RL::Panels::StockPanel::PlayerInventory,
		.localId    = "InventoryButton",
	};
	D2RL::Panels::ChildLayoutHandle handle {};
	if (PanelApi->registerChildLayout(Context, &child, &handle) != D2RL::Panels::Result::Success) Log(true, "could not add the inventory button");
}

// ---- the panel ----

// The size and place of the game's left panels (as Run Counter's window),
// hung from the vanilla $LeftPanelAnchor's place (x 0, y 39.7% of the screen)
// written out, not from $LeftPanelAnchor: mods that move that anchor would
// carry the panel off screen.
// Inside the frame's gold line: x 59..1103, y 178..1451; the cells centred there.
constexpr int PanelWidth = 1162, PanelHeight = 1507, PanelY = -651;
constexpr int GridX = (PanelWidth - Columns * Cell) / 2, GridY = 178 + (1451 - 178 - Rows * Cell) / 2;

bool RegisterPanelArt() {
	Picture frame, frameLow, slot;
	if (!DecodePng(FrameArtId, frame) || !DecodePng(FrameArtLowEndId, frameLow) || !RegisterSprite("frame", { frame }, { frameLow })) return false;
	if (!DecodePng(SlotArtId, slot) || slot.width != Cell || slot.height != Cell) return false;
	Picture cells = Blank(Columns * Cell, Rows * Cell);
	for (int y = 0; y < cells.height; ++y)
		for (int x = 0; x < cells.width; ++x) std::memcpy(cells.at(x, y), slot.at(x % Cell, y % Cell), 4);
	return RegisterSprite("cells", { cells });
}

std::string PanelLayout() {
	char layout[2048] {};
	std::snprintf(layout, sizeof(layout),
	              R"({"type": "Panel", "name": "@PANEL@", "fields": {"priority": 10, "rect": {"x": 236, "y": %d, "width": %d, "height": %d}, "anchor": {"x": 0.0, "y": 0.397}}, "children": [)"
	              R"({"type": "ClickCatcherWidget", "name": "PanelClickCatcher", "fields": {"rect": {"x": 0, "y": 0, "width": %d, "height": %d}}})"
	              R"(, {"type": "ImageWidget", "name": "Background", "fields": {"rect": {"x": 0, "y": 0}, "filename": "d2rloader\\%s\\frame"}})"
	              R"(, {"type": "TextBoxWidget", "name": "Title", "fields": {"rect": {"x": 0, "y": 74, "width": %d, "height": 84}, "text": "Charms", )"
	              R"("style": {"fontColor": "$FontColorGoldYellow", "pointSize": 52, "dropShadow": "$DefaultDropShadow", "alignment": {"h": "center", "v": "center"}}}})"
	              R"(, {"type": "ImageWidget", "name": "Cells", "fields": {"rect": {"x": %d, "y": %d}, "filename": "d2rloader\\%s\\cells"}})"
	              R"(, @GRID@)"
	              R"(, {"type": "ButtonWidget", "name": "CloseButton", "fields": {"rect": {"x": %d, "y": 9}, "filename": "PANEL\\closebtn_4x", "hoveredFrame": 3, "tooltipString": "@d2r:strClose", )"
	              R"("sound": "cursor_close_window_hd", "onClickMessage": "PanelManager:ClosePanel:@PANEL@"}}]})",
	              PanelY, PanelWidth, PanelHeight, PanelWidth, PanelHeight, PluginId, PanelWidth, GridX, GridY, PluginId, PanelWidth - 87);
	return layout;
}

// ---- the area ----

auto __cdecl CharmsOnly(PageHost::AreaHandle, const D2RL::Inventory::ItemPolicyRequest* request, void*) noexcept -> D2RL::Inventory::ItemPolicyDecision {
	return D2RL::Inventory::ItemPolicyRequestMatchesType(request, D2RL::Inventory::MakeItemTypeFourCC("char")) ? D2RL::Inventory::ItemPolicyDecision::Allow
	                                                                                                           : D2RL::Inventory::ItemPolicyDecision::Reject;
}

bool PanelArt = false;

bool Join() {
	if (Area) return true;
	if (!CommApi || PageHost::Acquire(Context, CommApi, &Host) != Comm::Result::Success) return false;
	PageHost::AreaInfo          info { .structSize = sizeof(info) };
	const PageHost::AreaRequest request { .structSize = sizeof(request), .flags = PageHost::AreaCharms, .name = "charms", .label = "Charms", .width = Columns, .height = Rows };
	if (const auto reserved = Host->reserveArea(Context, &request, &info); reserved != PageHost::Result::Success) {
		Log(true, "the host refused a %dx%d area (result %u)", Columns, Rows, static_cast<unsigned>(reserved));
		return false;
	}
	Area = info.handle;
	(void)Host->setItemPolicy(Area, CharmsOnly, nullptr);
	if (PanelArt) {
		static const std::string layout = PanelLayout();
		const PageHost::LayoutRequest request { .structSize = sizeof(request), .flags = 0, .layout = layout.c_str(), .gridX = GridX, .gridY = GridY };
		if (const auto set = Host->setAreaLayout(Area, &request); set != PageHost::Result::Success) Log(true, "the host refused the panel layout (result %u): the plain one shows", static_cast<unsigned>(set));
	}
	Log(false, "%dx%d charm area reserved", Columns, Rows);
	return true;
}

void __cdecl AfterAllLoaded(const D2RL::PluginContext*, const D2RL::Lifecycle::DataTablesLoadedEvent*, void*) noexcept {
	static bool done = false;
	if (done) return;
	done = true;
	if (!Join()) Log(true, "Player Page Host not found: install aaa-player-page-host");
	PageHost::AreaInfo info { .structSize = sizeof(info) };
	if (Area && Host->getAreaInfo(Area, &info) == PageHost::Result::Success)
		Log(false, "area at %d,%d of the page (-1: not placed yet), charms active %u", info.x, info.y, info.charmsActive);
}

void Toggle() {
	if (!Area || !Host) {
		Context->WriteConsoleMessage("Charms: Player Page Host is missing (see the plugin's log)");
		return;
	}
	if (Host->isAreaOpen(Area)) {
		(void)Host->closeArea(Area);
		return;
	}
	const auto opened = Host->openArea(Area);
	if (opened == PageHost::Result::NoRoom) Context->WriteConsoleMessage("Charms: no room on the player page beside the other plugins' areas");
	else if (opened != PageHost::Result::Success) Log(true, "open: result %u", static_cast<unsigned>(opened));
}

auto __cdecl OnUiMessage(const D2RL::PluginContext*, const Events::UiMessageEvent* event, void*) noexcept -> Events::UiMessageAction {
	if (!event || !event->text) return Events::UiMessageAction::Continue;
	const std::size_t prefix = std::strlen(CommandPrefix);
	if (std::strncmp(event->text, CommandPrefix, prefix) != 0) return Events::UiMessageAction::Continue;
	// D2R reports a button's message twice, about a millisecond apart.
	static ULONGLONG last {};
	const ULONGLONG  now = GetTickCount64();
	if (now - last < 150) return Events::UiMessageAction::Consume;
	last = now;
	if (!std::strcmp(event->text + prefix, "toggle")) Toggle();
	return Events::UiMessageAction::Consume;
}

// The "Open/close charms" action of the game's Controls menu (unbound by
// default): the same as the inventory button.
auto OnCharmsAction(const D2RL::PluginContext*, const D2RL::Input::ActionEvent* event, void*) noexcept -> D2RL::Input::ActionResult {
	if (!D2RL::Input::HasActionEventField(event, D2RL::Input::ActionEventRequiredSize) || event->kind != D2RL::Input::ActionEventKind::Pressed)
		return D2RL::Input::ActionResult::Ignored;
	Toggle();
	return D2RL::Input::ActionResult::Handled;
}

auto Command(D2R::Game::Client*, const D2RL::ConsoleCommandContext*, void*) noexcept -> D2RL::ConsoleCommandResult {
	Toggle();
	return D2RL::ConsoleCommandResult::Handled;
}

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (!D2RL::HasContext(context) || !context->GetApi()) return false;
	Context = context;
	(void)Context->EnsureConfig();
	char          config[2048] {};
	std::uint32_t required = 0;
	if (Context->ReadConfig(config, sizeof(config) - 1, &required) && std::strstr(config, "enabled = false")) {
		Log(false, "%s %s: disabled in config", Info.name, PluginVersion);
		return true;
	}
	Log(false, "%s %s loading", Info.name, PluginVersion);
	if (context->QueryService(&CommApi) != D2RL::ServiceQueryResult::Success) CommApi = nullptr;
	if (context->QueryService(&ResourceApi) != D2RL::ServiceQueryResult::Success) ResourceApi = nullptr;
	if (context->QueryService(&PanelApi) != D2RL::ServiceQueryResult::Success) PanelApi = nullptr;
	if (context->QueryService(&EventApi) != D2RL::ServiceQueryResult::Success) EventApi = nullptr;
	PanelArt = ResourceApi && RegisterPanelArt();
	if (!PanelArt) Log(true, "could not unpack the panel's art: the host's plain panel shows");
	(void)Join(); // the host loads first (its DLL name sorts first); else after every plugin has loaded
	if (const D2RL::LifecycleService* lifecycle = nullptr; context->QueryService(&lifecycle) == D2RL::ServiceQueryResult::Success && lifecycle) {
		const D2RL::Lifecycle::DataTablesLoadedListener listener { .structSize = D2RL::Lifecycle::DataTablesLoadedListenerSize, .callback = AfterAllLoaded };
		D2RL::Lifecycle::ListenerHandle                 handle {};
		(void)lifecycle->registerDataTablesLoadedListener(context, &listener, &handle);
	}
	if (ResourceApi && PanelApi) AddButton();
	else Log(true, "no resource or panel service: no inventory button (use the charms console command)");
	if (EventApi) {
		const Events::UiMessageListener listener { .structSize = Events::UiMessageListenerSize, .priority = 100, .callback = OnUiMessage };
		Events::ListenerHandle          handle = Events::InvalidHandle;
		if (EventApi->registerUiMessageListener(Context, &listener, &handle) != Events::Result::Success) Log(true, "could not listen to button clicks");
	}
	if (const D2RL::InputService* input = nullptr;
	    context->QueryService(&input) == D2RL::ServiceQueryResult::Success && input && D2RL::HasInputServiceField(input, D2RL::InputServiceRequiredSize)) {
		const D2RL::Input::ActionRegistration action {
			.structSize       = D2RL::Input::ActionRegistrationSize,
			.logicalId        = "toggle-charms",
			.displayName      = "Open/close charms",
			.category         = "Wild Healer's Charm Inventory",
			.defaultPrimary   = { .key = D2RL::Input::Key::None },
			.defaultSecondary = { .key = D2RL::Input::Key::None },
			.callback         = OnCharmsAction,
		};
		D2RL::Input::ActionHandle handle = D2RL::Input::InvalidHandle;
		if (const auto result = input->registerAction(context, &action, &handle); result != D2RL::Input::Result::Success)
			Log(true, "could not add the open/close action to the Controls menu (result %u)", static_cast<unsigned>(result));
	}
	(void)context->RegisterConsoleCommand("charms", Command, "Opens or closes the charm inventory.");
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	if (Area && Host) (void)Host->releaseArea(Context, Area);
	Area = 0;
	(void)Host.Reset();
}
