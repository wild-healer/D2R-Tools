// Wild Healer's Charm Inventory: a charm grid on Player Page Host's page.
//   - reserves one 10x4 area of the host's page (the size of the inventory),
//     with active charms, and takes only charms;
//   - shows it in a game-style left panel: the stash frame Run Counter ships
//     (its border in src/frame_art.inc, the plain stone inside drawn here), and the
//     area's cells;
//   - a button on the inventory's left edge, made like Grail On!'s (the mold
//     and Grail On!'s charm category icon), under Grail On!'s, opens and
//     closes it;
//   - charms in the normal inventory give no bonus (the local player's), and
//     the charm inventory holds one of each unique charm (a second copy can
//     only be swapped with the first).
#include "page_host_api.h"

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "mold.inc"
#include "charm_icon.inc"
#include "frame_art.inc"

namespace {

namespace Comm   = D2RL::PluginCommunication;
namespace Events = D2RL::SharedEvents;

constexpr const char* PluginId      = "wild-healers-charm-inventory";
constexpr const char* PluginVersion = "1.0.3";
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
const D2RL::InventoryService*           Inventory {};
const D2RL::ItemService*                ItemApi {};
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

// A 256-colour piece of the frame (tools/make_art.py), opaque.
Picture Unpack(const ArtPiece& piece) {
	Picture out = Blank(piece.width, piece.height);
	for (std::size_t p = 0; p < static_cast<std::size_t>(piece.width) * piece.height; ++p) {
		std::memcpy(out.rgba.data() + p * 4, piece.palette + piece.pixels[p] * 3, 3);
		out.rgba[p * 4 + 3] = 255;
	}
	return out;
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

// The frame: the border pieces around dark stone with the grain of the
// game's panels (fine noise over a coarser 3x3 one).
bool MakeFrame(Picture& frame) {
	const Picture top = Unpack(TopArt), bottom = Unpack(BottomArt), left = Unpack(LeftArt), right = Unpack(RightArt);
	const int width = top.width, height = top.height + left.height + bottom.height;
	if (bottom.width != width || right.height != left.height || left.width + right.width >= width) return false;
	frame = Blank(width, height);
	const auto noise = [](std::uint32_t x, std::uint32_t y, std::uint32_t seed) {
		std::uint32_t h = x * 374761393u + y * 668265263u + seed * 2246822519u;
		h               = (h ^ (h >> 13)) * 1274126177u;
		return ((h ^ (h >> 16)) & 0xFF) / 255.0;
	};
	for (int y = top.height; y < top.height + left.height; ++y)
		for (int x = left.width; x < width - right.width; ++x) {
			const double n  = 0.6 * noise(x, y, 1) + 0.4 * noise(x / 3, y / 3, 2) - 0.5;
			auto*        px = frame.at(x, y);
			px[0] = px[1] = static_cast<unsigned char>(std::clamp(18 + static_cast<int>(n * 12), 0, 255));
			px[2]         = static_cast<unsigned char>(std::clamp(19 + static_cast<int>(n * 12), 0, 255));
			px[3]         = 255;
		}
	const auto paste = [&](const Picture& part, int x0, int y0) {
		for (int y = 0; y < part.height; ++y) std::memcpy(frame.at(x0, y0 + y), part.at(0, y), static_cast<std::size_t>(part.width) * 4);
	};
	paste(top, 0, 0);
	paste(bottom, 0, height - bottom.height);
	paste(left, 0, top.height);
	paste(right, width - right.width, top.height);
	return true;
}

bool RegisterPanelArt() {
	Picture frame;
	if (!MakeFrame(frame) || frame.width != PanelWidth || frame.height != PanelHeight || !RegisterSprite("frame", { frame })) return false;
	const Picture slot = Unpack(SlotArt);
	if (slot.width != Cell || slot.height != Cell) return false;
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

// ---- no bonus from inventory charms ----
// The game asks whether a charm gives its bonuses through a D2RCore jump table
// slot (thunk exe+0x3E2B61C: jmp [exe+0x3E2ACA0]) that Player Page Host wraps
// for its charm areas. This plugin wraps it too, in either order: a charm of
// the local player that isn't on the custom page (the stock inventory) gives
// nothing; anything else goes to the slot's previous target. Other players'
// charms (a TCP/IP game this player hosts) keep the game's rule.

constexpr std::uint64_t CharmThunkRva = 0x3E2B61C, CharmSlotRva = 0x3E2ACA0, LocalIndexRva = 0x8B2D0, PlayerUnitRva = 0x9A480;
constexpr std::uint8_t  PlayerUnitBytes[] { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x83, 0xF9, 0x08, 0x0F, 0x83, 0x85 };
constexpr std::size_t   UnitId = 0x08, UnitData = 0x10, DataPage = 0x55;
constexpr std::uint8_t  CustomPage = 6; // the custom page's item page byte (D2RCore "custom page-6")
using CharmFn       = std::int32_t (*)(void* item, void* unit) noexcept;
using LocalIndexFn  = int (*)() noexcept;
using PlayerUnitFn  = void* (*)(int index) noexcept;
std::uintptr_t Base {};
CharmFn        NextCharm {};
LocalIndexFn   LocalIndex {};
PlayerUnitFn   PlayerUnit {};

bool IsLocalPlayer(void* unit) noexcept {
	if (!unit) return false;
	std::uint32_t type = ~0U, id = 0;
	std::memcpy(&type, unit, sizeof(type));
	if (type != 0) return false;
	std::memcpy(&id, static_cast<const std::uint8_t*>(unit) + UnitId, sizeof(id));
	const auto* local = static_cast<const std::uint8_t*>(PlayerUnit(LocalIndex()));
	if (!local) return false;
	std::uint32_t localId = 0;
	std::memcpy(&localId, local + UnitId, sizeof(localId));
	return id == localId;
}

std::int32_t HookCharm(void* item, void* unit) noexcept {
	const auto* data = item ? *reinterpret_cast<std::uint8_t* const*>(static_cast<const std::uint8_t*>(item) + UnitData) : nullptr;
	if (data && data[DataPage] != CustomPage && IsLocalPlayer(unit)) return 0;
	return NextCharm(item, unit);
}

bool SwapSlot(std::uint64_t* slot, std::uint64_t from, std::uint64_t to) noexcept {
	DWORD keep = 0;
	if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &keep)) return false;
	const bool swapped = InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(slot), static_cast<LONG64>(to), static_cast<LONG64>(from)) == static_cast<LONG64>(from);
	VirtualProtect(slot, sizeof(*slot), keep, &keep);
	return swapped;
}

// Checks the game code it relies on first; on another build nothing is wrapped.
void WrapCharmSlot() noexcept {
	Base = static_cast<std::uintptr_t>(Context->exeBase);
	const auto* thunk = reinterpret_cast<const std::uint8_t*>(Base + CharmThunkRva);
	const auto* index = reinterpret_cast<const std::uint8_t*>(Base + LocalIndexRva);
	std::int32_t rel  = 0;
	if (Base) std::memcpy(&rel, thunk + 2, sizeof(rel));
	if (!Base || thunk[0] != 0xFF || thunk[1] != 0x25 || CharmThunkRva + 6 + rel != CharmSlotRva || std::memcmp(reinterpret_cast<const void*>(Base + PlayerUnitRva), PlayerUnitBytes, sizeof(PlayerUnitBytes)) != 0
	    || index[0] != 0x8B || index[1] != 0x05 || index[6] != 0xC3) {
		Log(true, "this game build's charm check isn't the expected one: inventory charms keep their bonuses");
		return;
	}
	LocalIndex          = reinterpret_cast<LocalIndexFn>(Base + LocalIndexRva);
	PlayerUnit          = reinterpret_cast<PlayerUnitFn>(Base + PlayerUnitRva);
	auto* const   slot    = reinterpret_cast<std::uint64_t*>(Base + CharmSlotRva);
	const auto    current = *slot;
	NextCharm             = reinterpret_cast<CharmFn>(current);
	if (!current || !SwapSlot(slot, current, reinterpret_cast<std::uint64_t>(&HookCharm))) {
		NextCharm = nullptr;
		Log(true, "the charm check could not be wrapped: inventory charms keep their bonuses");
		return;
	}
	Log(false, "charm check wrapped: inventory charms give no bonus");
}

void UnwrapCharmSlot() noexcept {
	// Only when nothing wrapped it after this plugin (else that wrapper keeps calling HookCharm, which stays in place).
	if (NextCharm && SwapSlot(reinterpret_cast<std::uint64_t*>(Base + CharmSlotRva), reinterpret_cast<std::uint64_t>(&HookCharm), reinterpret_cast<std::uint64_t>(NextCharm))) NextCharm = nullptr;
}

// ---- one of each unique charm ----

namespace Items = D2RL::Items;

struct UniqueSearch {
	PageHost::AreaInfo area;
	std::int32_t       record;
	std::uint32_t      skip; // the item being placed
	bool               found;
	std::uint32_t      copy; // the copy found
};

auto __cdecl FindUnique(const D2RL::PluginContext*, const Items::ItemInfo* item, void* user) noexcept -> D2RL::Inventory::IterationAction {
	auto& search = *static_cast<UniqueSearch*>(user);
	const auto& a = search.area;
	if (item->container == Items::ItemContainer::CustomPage && item->quality == Items::Quality::Unique && item->qualityRecordId == search.record && item->runtimeId != search.skip
	    && item->x >= a.x && item->y >= a.y && item->x < a.x + static_cast<std::int32_t>(a.width) && item->y < a.y + static_cast<std::int32_t>(a.height)) {
		search.found = true;
		search.copy  = item->runtimeId;
		return D2RL::Inventory::IterationAction::Stop;
	}
	return D2RL::Inventory::IterationAction::Continue;
}

// Whether `item` is a unique charm with a copy already in the charm area
// (`ids` gets the item's and the copy's runtime ids). UI thread or a
// game-thread callback.
bool UniqueAlreadyIn(D2RL::PlayerHandle player, D2RL::ItemHandle item, std::uint32_t* ids = nullptr) {
	if (!Inventory || !ItemApi || !Area || !Host) return false;
	Items::ItemInfo info { .structSize = Items::ItemInfoSize };
	if (ItemApi->getItemInfo(Context, item, &info) != Items::Result::Success || info.quality != Items::Quality::Unique) return false;
	UniqueSearch search { .area = { .structSize = sizeof(PageHost::AreaInfo) }, .record = info.qualityRecordId, .skip = info.runtimeId, .found = false };
	if (Host->getAreaInfo(Area, &search.area) != PageHost::Result::Success || search.area.x < 0) return false;
	const D2RL::Inventory::ItemFilter filter { .structSize = sizeof(D2RL::Inventory::ItemFilter), .containerMask = Items::ContainerBit(Items::ItemContainer::CustomPage) };
	(void)Inventory->forEachInventoryItem(Context, player, &filter, FindUnique, &search);
	if (ids) ids[0] = info.runtimeId, ids[1] = search.copy;
	return search.found;
}

void SayDuplicate() {
	static ULONGLONG last {};
	if (const ULONGLONG now = GetTickCount64(); now - last > 1000) {
		last = now;
		Context->WriteConsoleMessage("Charms: only one of each unique charm fits in the charm inventory");
	}
}

struct FindById {
	std::uint32_t    id;
	D2RL::ItemHandle handle;
	std::int32_t     x, y;
};

auto __cdecl FindItem(const D2RL::PluginContext*, const Items::ItemInfo* item, void* user) noexcept -> D2RL::Inventory::IterationAction {
	auto& find = *static_cast<FindById*>(user);
	if (item->runtimeId != find.id) return D2RL::Inventory::IterationAction::Continue;
	find.handle = item->handle, find.x = item->x, find.y = item->y;
	return D2RL::Inventory::IterationAction::Stop;
}

// ---- where a drop lands ----
// A second copy may only be dropped onto the copy: the game swaps them. The
// policy doesn't see the cell, so the grid's "element under the mouse" (the
// game's grid vtable, Base+0x1CF3800, entry 19) is wrapped on top of Player
// Page Host's, which already answers with the held item's top-left PAGE cell
// for an area grid: the last answer for this plugin's grid is kept.

constexpr std::uint64_t GridVtableRva = 0x1CF3800, CursorItemRva = 0x15F100, ItemSizeRva = 0x371850;
constexpr int           VtCellAtPoint = 19;
constexpr std::size_t   WidgetMarker = 0x634;
constexpr std::uint64_t NoCell = ~std::uint64_t { 0 };
constexpr std::uint8_t  CursorItemBytes[] { 0x48, 0x83, 0xEC, 0x28, 0xE8, 0xC7, 0xC1, 0xF2, 0xFF, 0x8B, 0xC8, 0xE8, 0x70, 0xB3, 0xF3, 0xFF };
constexpr std::uint8_t  ItemSizeBytes[] { 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x20 };
using CellFn       = std::uint64_t* (*)(void* widget, std::uint64_t* out, std::uint64_t point) noexcept;
using CursorItemFn = void* (*)() noexcept;
using ItemSizeFn   = void (*)(void* item, std::uint8_t* width, std::uint8_t* height, const char* file, int line) noexcept;
CellFn                     NextCellAtPoint {};
CursorItemFn               CursorItem {};
ItemSizeFn                 ItemSize {};
float                      GridMarker[2] {};
std::atomic<std::uint64_t> HoverCell { NoCell }, HoverTick {};

std::uint64_t* HookCellAtPoint(void* widget, std::uint64_t* out, std::uint64_t point) noexcept {
	std::uint64_t* const result = NextCellAtPoint(widget, out, point);
	float marker[2] {};
	if (widget) std::memcpy(marker, static_cast<const std::uint8_t*>(widget) + WidgetMarker, sizeof(marker));
	if (widget && marker[0] == GridMarker[0] && marker[1] == GridMarker[1]) HoverCell = *out, HoverTick = GetTickCount64();
	return result;
}

// Once, when the charm inventory first opens (Player Page Host wrapped the
// grid at load, so this goes on top of it).
void WrapCellAtPoint() noexcept {
	static bool tried = false;
	if (tried || !Base) return;
	tried = true;
	PageHost::AreaInfo info { .structSize = sizeof(info) };
	if (Host->getAreaInfo(Area, &info) != PageHost::Result::Success || std::memcmp(reinterpret_cast<const void*>(Base + CursorItemRva), CursorItemBytes, sizeof(CursorItemBytes)) != 0
	    || std::memcmp(reinterpret_cast<const void*>(Base + ItemSizeRva), ItemSizeBytes, sizeof(ItemSizeBytes)) != 0) {
		Log(true, "this game build's item lookups aren't the expected ones: a second copy of a unique charm can't be swapped with the first");
		return;
	}
	std::memcpy(GridMarker, info.gridMarker, sizeof(GridMarker));
	CursorItem   = reinterpret_cast<CursorItemFn>(Base + CursorItemRva);
	ItemSize     = reinterpret_cast<ItemSizeFn>(Base + ItemSizeRva);
	auto* const entry = reinterpret_cast<std::uint64_t*>(Base + GridVtableRva) + VtCellAtPoint;
	const auto  current = *entry;
	NextCellAtPoint     = reinterpret_cast<CellFn>(current);
	if (!current || !SwapSlot(entry, current, reinterpret_cast<std::uint64_t>(&HookCellAtPoint))) {
		NextCellAtPoint = nullptr;
		Log(true, "the grid's cell lookup could not be wrapped: a second copy of a unique charm can't be swapped with the first");
		return;
	}
	Log(false, "grid cell lookup wrapped (charm area marker %.0f,%.0f)", GridMarker[0], GridMarker[1]);
}

void UnwrapCellAtPoint() noexcept {
	if (NextCellAtPoint && SwapSlot(reinterpret_cast<std::uint64_t*>(Base + GridVtableRva) + VtCellAtPoint, reinterpret_cast<std::uint64_t>(&HookCellAtPoint), reinterpret_cast<std::uint64_t>(NextCellAtPoint)))
		NextCellAtPoint = nullptr;
}

// Whether the held item, put at the cell under the mouse, covers the copy
// (the game then swaps them; covering another item too means no drop at all).
bool DropsOnCopy(std::uint32_t copy, D2RL::PlayerHandle player) {
	const std::uint64_t cell = HoverCell.load();
	if (!NextCellAtPoint || !CursorItem || cell == NoCell || GetTickCount64() - HoverTick.load() > 500) return false;
	void* const held = CursorItem();
	if (!held) return false;
	std::uint8_t w = 1, h = 1;
	ItemSize(held, &w, &h, PluginId, __LINE__);
	FindById                          find { .id = copy };
	const D2RL::Inventory::ItemFilter filter { .structSize = sizeof(D2RL::Inventory::ItemFilter), .containerMask = Items::ContainerBit(Items::ItemContainer::CustomPage) };
	(void)Inventory->forEachInventoryItem(Context, player, &filter, FindItem, &find);
	if (!find.handle) return false;
	// The copy is the same item, so the same size.
	const int x = static_cast<std::int32_t>(cell & 0xFFFFFFFF), y = static_cast<std::int32_t>(cell >> 32);
	return x < find.x + w && find.x < x + w && y < find.y + h && find.y < y + h;
}

// A drop on the area: charms only, and a second copy of a unique charm only
// onto the copy (a Ctrl+click is checked in OnItemClick before the host moves
// it).
auto __cdecl CharmsOnly(PageHost::AreaHandle, const D2RL::Inventory::ItemPolicyRequest* request, void*) noexcept -> D2RL::Inventory::ItemPolicyDecision {
	using Decision = D2RL::Inventory::ItemPolicyDecision;
	if (!D2RL::Inventory::ItemPolicyRequestMatchesType(request, D2RL::Inventory::MakeItemTypeFourCC("char"))) return Decision::Reject;
	D2RL::PlayerHandle player {};
	D2RL::ItemHandle   cursor {};
	std::uint32_t      ids[2] {};
	if (!Inventory || Inventory->getLocalPlayer(Context, &player) != D2RL::Inventory::Result::Success || Inventory->getCursorItem(Context, player, &cursor) != D2RL::Inventory::Result::Success
	    || !cursor || !UniqueAlreadyIn(player, cursor, ids) || DropsOnCopy(ids[1], player))
		return Decision::Allow;
	SayDuplicate();
	return Decision::Reject;
}

namespace Clicks = D2RL::ItemInteractions;

// Ctrl+click on a unique charm outside the page while the charm inventory is
// open: refused when a copy is already in it (before the host's own Ctrl+click,
// priority -1000, moves it).
auto __cdecl OnItemClick(const D2RL::PluginContext*, const Clicks::ItemInteractionEvent* event, void*) noexcept -> Clicks::Decision {
	if (!event || event->action != Clicks::Action::Activate || event->container == Items::ItemContainer::CustomPage || !Area || !Host) return Clicks::Decision::Continue;
	const bool ctrl = (event->modifiers & Clicks::ModifierBit(Clicks::Modifier::Control)) || (GetAsyncKeyState(VK_CONTROL) & 0x8000);
	if (!ctrl || !Host->isAreaOpen(Area) || !UniqueAlreadyIn(event->player, event->item)) return Clicks::Decision::Continue;
	SayDuplicate();
	return Clicks::Decision::Consume;
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
	WrapCharmSlot();
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
	WrapCellAtPoint();
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
	if (context->QueryService(&Inventory) != D2RL::ServiceQueryResult::Success) Inventory = nullptr;
	if (context->QueryService(&ItemApi) != D2RL::ServiceQueryResult::Success) ItemApi = nullptr;
	if (!Inventory || !ItemApi) Log(true, "no inventory or item service: unique charms aren't limited to one each");
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
	if (const D2RL::ItemInteractionService* clicks = nullptr; context->QueryService(&clicks) == D2RL::ServiceQueryResult::Success && clicks) {
		const Clicks::ItemInteractionListener listener { .structSize = Clicks::ItemInteractionListenerSize, .priority = 0, .callback = OnItemClick };
		Clicks::ListenerHandle                handle {};
		if (clicks->registerListener(Context, &listener, &handle) != Clicks::Result::Success) Log(true, "could not listen to Ctrl+clicks: a second copy of a unique charm can be Ctrl+clicked in");
	}
	(void)context->RegisterConsoleCommand("charms", Command, "Opens or closes the charm inventory.");
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	UnwrapCellAtPoint();
	UnwrapCharmSlot();
	if (Area && Host) (void)Host->releaseArea(Context, Area);
	Area = 0;
	(void)Host.Reset();
}
