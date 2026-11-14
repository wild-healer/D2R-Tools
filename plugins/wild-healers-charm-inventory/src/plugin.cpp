// Wild Healer's Charm Inventory: a charm grid on Player Page Host's page.
//   - reserves one area of the host's page (10x4 by default, the size of the inventory),
//     with active charms, and takes only charms;
//   - shows it in a game-style left panel (D2R GUI's character screen window)
//     with the area's cells;
//   - a button on the inventory's left edge (D2R GUI's inventory button, the
//     "rune-stone" icon), under Grail On!'s, opens and closes it;
//   - charms in the normal inventory give no bonus (the local player's), and
//     the charm inventory holds one of each unique charm (a second copy can
//     only be swapped with the first);
//   - every setting applies at once, from the config or I Hate TOMLs.
// Every window part comes from the D2R GUI library (lib/d2r-gui).
#include "page_host_api.h"
#include "i_hate_tomls_client.h"

#include <d2r_gui.h>
#include <d2r_gui/icons.h>

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace Comm = D2RL::PluginCommunication;
namespace Gui  = D2RGui;

constexpr const char* PluginId      = "wild-healers-charm-inventory";
constexpr const char* PluginVersion = "1.1.2-dev-1";

const D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = PluginId,
	.name        = "Wild Healer's Charm Inventory",
	.version     = PluginVersion,
	.author      = "wild-healer",
	.description = "A charm inventory on Player Page Host's page, in a game-style panel opened from a button beside the inventory or a key.",
	.flags       = D2RL::PluginFlags::Client,
};

const D2RL::PluginContext*              Context {};
const D2RL::PluginCommunicationService* CommApi {};
const D2RL::InventoryService*           Inventory {};
const D2RL::ItemService*                ItemApi {};
Comm::ServiceLease<PageHost::Service>   Host;
PageHost::AreaHandle                    Area {};
constexpr int                           Cell = 98;
// Size from the config (columns 1-10 fit inside the frame, rows 1-12).
constexpr int                           MaxColumns = 10, MaxRows = 12;
int                                     Columns = 10, Rows = 4;
std::atomic<bool>                       Enabled { true };
std::atomic<bool>                       InventoryCharms { false }; // charms in the normal inventory give their bonuses
bool                                    GuiReady = false; // D2R GUI has its services

void Log(bool error, const char* format, ...) noexcept {
	char    line[512] {};
	va_list args;
	va_start(args, format);
	std::vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	if (error) Context->LogError(line);
	else Context->LogInfo(line);
}

// ---- the inventory button (D2R GUI) ----

// Under Grail On!'s button (its mold at 3,954). While the plugin is off it is
// moved off the screen (in the game's widget and in its layout).
constexpr int        ButtonX = 3, ButtonY = 954 + Gui::InventoryButton::Height, HiddenX = -5000;
constexpr int        IconSize = 98, IconDrawn = 42;
Gui::InventoryButton Button;

void AddButton() {
	const auto icon = Gui::Icons::Mask("rune-stone", IconSize);
	if (icon.empty() || !Gui::RegisterSprite("mold", { Gui::MoldPicture() })
	    || !Gui::RegisterSprite("button", Gui::MaskButton(icon.data(), IconSize, Gui::InventoryButton::Width - 2, 60, IconDrawn)))
		Log(true, "could not register the button art");
	const Gui::InventoryButton::Look look { .moldSprite = "mold", .buttonSprite = "button", .button = "CharmButton", .tooltip = "Charms", .command = "toggle" };
	if (!Button.Register("InventoryButton", look, Enabled.load() ? ButtonX : HiddenX, ButtonY)) Log(true, "could not add the inventory button");
}

void ShowButton() {
	if (GuiReady) (void)Gui::RunOnUi([] { Button.Move(Enabled.load() ? ButtonX : HiddenX, ButtonY); });
}

// ---- the panel (D2R GUI's character screen window) ----

// The cells centred on the window's plain stone, or between its header and
// its bottom when taller (the cells cover what is under them).
namespace Window = Gui::CharacterWindow;
int           GridX = 0, GridY = 0;
void PlaceGrid() noexcept {
	const int height = Rows * Cell;
	GridX = Window::CoverX + (Window::CoverWidth - Columns * Cell) / 2;
	GridY = height <= Window::CoverHeight ? Window::CoverY + (Window::CoverHeight - height) / 2 : 178 + (1451 - 178 - height) / 2;
}

// The host's layout: @PANEL@ and @GRID@ are its tokens.
std::string PanelLayout() {
	std::string cells;
	const auto  slot = Gui::StashKit::Slot(Cell, Cell);
	for (int y = 0; y < Rows; ++y)
		for (int x = 0; x < Columns; ++x) cells += Gui::Image("Cell" + std::to_string(y * MaxColumns + x), GridX + x * Cell, GridY + y * Cell, slot);
	return Window::Layout("@PANEL@", "Probe0", "Charms", "close", cells + ", @GRID@");
}

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
	if (!PlayerUnit) return true; // no local player lookup on this build: every player
	const auto* local = static_cast<const std::uint8_t*>(PlayerUnit(LocalIndex()));
	if (!local) return false;
	std::uint32_t localId = 0;
	std::memcpy(&localId, local + UnitId, sizeof(localId));
	return id == localId;
}

std::int32_t HookCharm(void* item, void* unit) noexcept {
	const auto* data = item ? *reinterpret_cast<std::uint8_t* const*>(static_cast<const std::uint8_t*>(item) + UnitData) : nullptr;
	if (data && data[DataPage] != CustomPage && Enabled.load(std::memory_order_relaxed) && !InventoryCharms.load(std::memory_order_relaxed) && IsLocalPlayer(unit)) return 0;
	return NextCharm(item, unit);
}

bool SwapSlot(std::uint64_t* slot, std::uint64_t from, std::uint64_t to) noexcept {
	DWORD keep = 0;
	if (!VirtualProtect(slot, sizeof(*slot), PAGE_EXECUTE_READWRITE, &keep)) return false; // may sit in another plugin's code page
	const bool swapped = InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(slot), static_cast<LONG64>(to), static_cast<LONG64>(from)) == static_cast<LONG64>(from);
	VirtualProtect(slot, sizeof(*slot), keep, &keep);
	return swapped;
}

// Checks the game code it relies on first; on another build nothing is wrapped.
// Names the module an address is in, for the log.
void ModuleOf(const void* address, char* name, DWORD size) noexcept {
	HMODULE module = nullptr;
	name[0]        = 0;
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>(address), &module) || !module) {
		std::snprintf(name, size, "no module");
		return;
	}
	char path[MAX_PATH] {};
	if (!GetModuleFileNameA(module, path, MAX_PATH)) {
		std::snprintf(name, size, "unnamed module");
		return;
	}
	const char* file = std::strrchr(path, '\\');
	std::snprintf(name, size, "%s+0x%llX", file ? file + 1 : path, static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(address) - reinterpret_cast<std::uintptr_t>(module)));
}

// Where the hook sits: the thunk's slot, or, when another plugin (Charm Dedup)
// took the thunk itself (jmp rel32 to a stub "jmp [rip+0]; abs64" that skips
// the slot), that stub's address word. In the stub case the slot (Player Page
// Host's area rule) is asked first for page charms, then the stub's hook.
std::uint64_t* Wrapped {};
CharmFn        SlotCharm {};

std::int32_t HookCharmAfterThunk(void* item, void* unit) noexcept {
	const auto* data = item ? *reinterpret_cast<std::uint8_t* const*>(static_cast<const std::uint8_t*>(item) + UnitData) : nullptr;
	if (data && data[DataPage] != CustomPage && Enabled.load(std::memory_order_relaxed) && !InventoryCharms.load(std::memory_order_relaxed) && IsLocalPlayer(unit)) return 0;
	if (data && data[DataPage] == CustomPage && SlotCharm && !SlotCharm(item, unit)) return 0;
	return NextCharm(item, unit);
}

void WrapCharmSlot() noexcept {
	Base = static_cast<std::uintptr_t>(Context->exeBase);
	if (!Base) return;
	const auto*  thunk = reinterpret_cast<const std::uint8_t*>(Base + CharmThunkRva);
	const auto*  index = reinterpret_cast<const std::uint8_t*>(Base + LocalIndexRva);
	auto* const  slot  = reinterpret_cast<std::uint64_t*>(Base + CharmSlotRva);
	std::int32_t rel   = 0;
	std::memcpy(&rel, thunk + 2, sizeof(rel));
	std::uint64_t* word = nullptr;
	void*          hook = nullptr;
	if (thunk[0] == 0xFF && thunk[1] == 0x25 && CharmThunkRva + 6 + rel == CharmSlotRva) {
		word = slot;
		hook = reinterpret_cast<void*>(&HookCharm);
	} else if (thunk[0] == 0xE9) {
		std::int32_t to = 0;
		std::memcpy(&to, thunk + 1, sizeof(to));
		const auto*                    stub   = thunk + 5 + to;
		constexpr std::uint8_t         jump[] { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
		MEMORY_BASIC_INFORMATION       memory {};
		if (VirtualQuery(stub, &memory, sizeof(memory)) && memory.State == MEM_COMMIT && !std::memcmp(stub, jump, sizeof(jump))) {
			word      = reinterpret_cast<std::uint64_t*>(const_cast<std::uint8_t*>(stub) + 6);
			hook      = reinterpret_cast<void*>(&HookCharmAfterThunk);
			SlotCharm = reinterpret_cast<CharmFn>(*slot);
			char owner[MAX_PATH + 32] {};
			ModuleOf(reinterpret_cast<const void*>(*word), owner, sizeof(owner));
			Log(false, "the charm check thunk was taken by another plugin (%s): wrapping its jump", owner);
		}
	}
	if (!word) {
		char bytes[3 * 16 + 1] {};
		for (int i = 0; i < 16; ++i) std::snprintf(bytes + 3 * i, 4, "%02X ", thunk[i]);
		Log(true, "this game build's charm check isn't the expected one (thunk %s): inventory charms keep their bonuses", bytes);
		return;
	}
	// The local player lookup only limits the rule to this player (in a TCP/IP
	// game this player hosts); without it the rule covers every player.
	const auto* unit = reinterpret_cast<const std::uint8_t*>(Base + PlayerUnitRva);
	if (!std::memcmp(unit, PlayerUnitBytes, sizeof(PlayerUnitBytes)) && index[0] == 0x8B && index[1] == 0x05 && index[6] == 0xC3) {
		LocalIndex = reinterpret_cast<LocalIndexFn>(Base + LocalIndexRva);
		PlayerUnit = reinterpret_cast<PlayerUnitFn>(Base + PlayerUnitRva);
	} else {
		char bytes[2][3 * 16 + 1] {};
		for (int i = 0; i < 16; ++i) std::snprintf(bytes[0] + 3 * i, 4, "%02X ", unit[i]), std::snprintf(bytes[1] + 3 * i, 4, "%02X ", index[i]);
		Log(false, "the local player lookup differs on this build (player unit: %s/ local index: %s): the rule covers every player in the game", bytes[0], bytes[1]);
	}
	const auto current = *word;
	NextCharm          = reinterpret_cast<CharmFn>(current);
	if (!current || !SwapSlot(word, current, reinterpret_cast<std::uint64_t>(hook))) {
		NextCharm = nullptr;
		Log(true, "the charm check could not be wrapped: inventory charms keep their bonuses");
		return;
	}
	Wrapped = word;
	char next[MAX_PATH + 32] {};
	ModuleOf(reinterpret_cast<const void*>(current), next, sizeof(next));
	Log(false, "charm check wrapped (previous target %s): inventory charms give no bonus", next);
}

void UnwrapCharmSlot() noexcept {
	// Only when nothing wrapped it after this plugin (else that wrapper keeps calling the hook, which stays in place).
	if (!NextCharm || !Wrapped) return;
	MEMORY_BASIC_INFORMATION memory {};
	if (!VirtualQuery(Wrapped, &memory, sizeof(memory)) || memory.State != MEM_COMMIT) return; // the other plugin freed its stub
	const auto hook = Wrapped == reinterpret_cast<std::uint64_t*>(Base + CharmSlotRva) ? reinterpret_cast<std::uint64_t>(&HookCharm) : reinterpret_cast<std::uint64_t>(&HookCharmAfterThunk);
	if (SwapSlot(Wrapped, hook, reinterpret_cast<std::uint64_t>(NextCharm))) NextCharm = nullptr;
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

constexpr std::uint64_t CursorItemRva = 0x15F100, ItemSizeRva = 0x371850;
constexpr std::uint64_t NoCell = ~std::uint64_t { 0 };
constexpr std::uint8_t  CursorItemBytes[] { 0x48, 0x83, 0xEC, 0x28, 0xE8, 0xC7, 0xC1, 0xF2, 0xFF, 0x8B, 0xC8, 0xE8, 0x70, 0xB3, 0xF3, 0xFF };
constexpr std::uint8_t  ItemSizeBytes[] { 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x20 };
using CursorItemFn = void* (*)() noexcept;
using ItemSizeFn   = void (*)(void* item, std::uint8_t* width, std::uint8_t* height, const char* file, int line) noexcept;
CursorItemFn      CursorItem {};
ItemSizeFn        ItemSize {};
float             GridMarker[2] {};
std::atomic<bool> MarkerKnown {};

// UI thread, when the charm inventory opens: the area's grid marker (it
// follows the area's panel, so a new size has a new one), then once the
// item lookups and D2R GUI's grid wrap (on top of Player Page Host's, which
// answers with page cells for an area grid).
void PrepareDrops() noexcept {
	static bool        tried = false;
	PageHost::AreaInfo info { .structSize = sizeof(info) };
	if (Area && Host->getAreaInfo(Area, &info) == PageHost::Result::Success) std::memcpy(GridMarker, info.gridMarker, sizeof(GridMarker)), MarkerKnown.store(true);
	if (tried || !Base || !MarkerKnown.load()) return;
	tried = true;
	if (std::memcmp(reinterpret_cast<const void*>(Base + CursorItemRva), CursorItemBytes, sizeof(CursorItemBytes)) != 0
	    || std::memcmp(reinterpret_cast<const void*>(Base + ItemSizeRva), ItemSizeBytes, sizeof(ItemSizeBytes)) != 0) {
		Log(true, "this game build's item lookups aren't the expected ones: a second copy of a unique charm can't be swapped with the first");
		return;
	}
	CursorItem = reinterpret_cast<CursorItemFn>(Base + CursorItemRva);
	ItemSize   = reinterpret_cast<ItemSizeFn>(Base + ItemSizeRva);
	Gui::TileGrid::Options options;
	options.mine              = [](const void* widget) { return MarkerKnown.load(std::memory_order_relaxed) && Gui::TileGrid::Marked(widget, GridMarker[0], GridMarker[1]); };
	options.clearRequirements = false;
	options.problem           = [](const char* what) { Log(true, "%s: a second copy of a unique charm can't be swapped with the first", what); };
	if (Gui::TileGrid::Wrap(std::move(options))) Log(false, "grid wrapped (charm area marker %.0f,%.0f)", GridMarker[0], GridMarker[1]);
}

// Whether the held item, put at the cell under the mouse, covers the copy
// (the game then swaps them; covering another item too means no drop at all).
bool DropsOnCopy(std::uint32_t copy, D2RL::PlayerHandle player) {
	const std::uint64_t cell = Gui::TileGrid::HoverCell();
	if (!Gui::TileGrid::Wrapped() || !CursorItem || cell == NoCell) return false;
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


bool PanelArt = false; // D2R GUI has read the game's art (after the data tables loaded)

void SetLayout() {
	if (!PanelArt || !Area || !Host) return;
	static std::string layout;
	layout = PanelLayout();
	const PageHost::LayoutRequest request { .structSize = sizeof(request), .flags = 0, .layout = layout.c_str(), .gridX = GridX, .gridY = GridY };
	if (const auto set = Host->setAreaLayout(Area, &request); set != PageHost::Result::Success) Log(true, "the host refused the panel layout (result %u): the plain one shows", static_cast<unsigned>(set));
}

// Gets the host (once), then reserves the area at the current size when the
// plugin is on.
bool Join() {
	if (!Host && (!CommApi || PageHost::Acquire(Context, CommApi, &Host) != Comm::Result::Success)) return false;
	if (Area || !Enabled.load()) return true;
	PageHost::AreaInfo          info { .structSize = sizeof(info) };
	const PageHost::AreaRequest request { .structSize = sizeof(request), .flags = PageHost::AreaCharms, .name = "charms", .label = "Charms", .width = static_cast<std::uint32_t>(Columns), .height = static_cast<std::uint32_t>(Rows) };
	if (const auto reserved = Host->reserveArea(Context, &request, &info); reserved != PageHost::Result::Success) {
		Log(true, "the host refused a %dx%d area (result %u)", Columns, Rows, static_cast<unsigned>(reserved));
		return false;
	}
	Area = info.handle;
	(void)Host->setItemPolicy(Area, CharmsOnly, nullptr);
	SetLayout();
	Log(false, "%dx%d charm area reserved", Columns, Rows);
	return true;
}

void __cdecl AfterAllLoaded(const D2RL::PluginContext*, const D2RL::Lifecycle::DataTablesLoadedEvent*, void*) noexcept {
	static bool done = false;
	if (done) return;
	done = true;
	if (!Join()) Log(true, Host ? "the charm area could not be reserved" : "Player Page Host not found: install aaa-player-page-host");
	PageHost::AreaInfo info { .structSize = sizeof(info) };
	const bool         gotInfo = Area && Host->getAreaInfo(Area, &info) == PageHost::Result::Success;
	WrapCharmSlot();
	if (GuiReady) {
		Gui::Kit::Read();
		PanelArt = true;
		SetLayout();
	}
	if (gotInfo)
		Log(false, "area at %d,%d of the page (-1: not placed yet), charms active %u", info.x, info.y, info.charmsActive);
}

void Toggle() {
	if (!Enabled.load()) {
		Context->WriteConsoleMessage("Charms: the charm inventory is turned off in its settings");
		return;
	}
	if (!Area || !Host) {
		Context->WriteConsoleMessage("Charms: Player Page Host is missing (see the plugin's log)");
		return;
	}
	if (Host->isAreaOpen(Area)) {
		(void)Host->closeArea(Area);
		return;
	}
	PrepareDrops();
	const auto opened = Host->openArea(Area);
	if (opened == PageHost::Result::NoRoom) Context->WriteConsoleMessage("Charms: no room on the player page beside the other plugins' areas");
	else if (opened != PageHost::Result::Success) Log(true, "open: result %u", static_cast<unsigned>(opened));
}

// Button clicks (D2R GUI, UI thread).
void OnCommand(const std::string& command) {
	if (command == "toggle") Toggle();
	else if (command == "close" && Area && Host && Host->isAreaOpen(Area)) (void)Host->closeArea(Area);
}

// A key or the console: on the UI thread, as the button.
void PostToggle() {
	if (GuiReady) Gui::Commands::Post("toggle");
	else Toggle();
}

// The "Open/close charms" action of the game's Controls menu (unbound by
// default): the same as the inventory button.
auto OnCharmsAction(const D2RL::PluginContext*, const D2RL::Input::ActionEvent* event, void*) noexcept -> D2RL::Input::ActionResult {
	if (!D2RL::Input::HasActionEventField(event, D2RL::Input::ActionEventRequiredSize) || event->kind != D2RL::Input::ActionEventKind::Pressed)
		return D2RL::Input::ActionResult::Ignored;
	PostToggle();
	return D2RL::Input::ActionResult::Handled;
}

auto Command(D2R::Game::Client*, const D2RL::ConsoleCommandContext*, void*) noexcept -> D2RL::ConsoleCommandResult {
	PostToggle();
	return D2RL::ConsoleCommandResult::Handled;
}

// ---- settings, applied at once ----

// The height of a charm by its code (cm1 1, cm2 2, cm3 3; else the tallest).
int CharmHeight(std::uint32_t code) {
	const char c0 = static_cast<char>(code & 0xFF), c1 = static_cast<char>(code >> 8 & 0xFF), c2 = static_cast<char>(code >> 16 & 0xFF);
	return c0 == 'c' && c1 == 'm' && c2 >= '1' && c2 <= '3' ? c2 - '0' : 3;
}

struct OutsideSearch {
	PageHost::AreaInfo area;
	int                columns, rows;
	int                inside, outside;
};

auto __cdecl FindOutside(const D2RL::PluginContext*, const Items::ItemInfo* item, void* user) noexcept -> D2RL::Inventory::IterationAction {
	auto&       search = *static_cast<OutsideSearch*>(user);
	const auto& a      = search.area;
	if (item->container != Items::ItemContainer::CustomPage || item->x < a.x || item->y < a.y || item->x >= a.x + static_cast<std::int32_t>(a.width) || item->y >= a.y + static_cast<std::int32_t>(a.height))
		return D2RL::Inventory::IterationAction::Continue;
	++search.inside;
	if (item->x - a.x >= search.columns || item->y - a.y + CharmHeight(item->code) > search.rows) ++search.outside;
	return D2RL::Inventory::IterationAction::Continue;
}

// Counts the local player's charms in the area, and those a columns x rows
// area at the same place would leave out. False when that can't be read.
bool CountCharms(const PageHost::AreaInfo& area, int columns, int rows, int& inside, int& outside) {
	D2RL::PlayerHandle player {};
	if (!Inventory || Inventory->getLocalPlayer(Context, &player) != D2RL::Inventory::Result::Success || !player) return false;
	OutsideSearch search { .area = area, .columns = columns, .rows = rows, .inside = 0, .outside = 0 };
	const D2RL::Inventory::ItemFilter filter { .structSize = sizeof(D2RL::Inventory::ItemFilter), .containerMask = Items::ContainerBit(Items::ItemContainer::CustomPage) };
	if (Inventory->forEachInventoryItem(Context, player, &filter, FindOutside, &search) != D2RL::Inventory::Result::Success) return false;
	inside = search.inside, outside = search.outside;
	return true;
}

void ReleaseArea() {
	if (!Area || !Host) return;
	if (Host->isAreaOpen(Area)) (void)Host->closeArea(Area);
	(void)Host->releaseArea(Context, Area);
	Area = 0;
}

// Main thread (the menu's calls). Off: the area is given back (the charms in
// it stay on the page and give nothing), the button hides, inventory charms
// count again. On: the area comes back at its saved place.
bool SetEnabled(bool on, std::string& error) {
	if (on == Enabled.load()) return true;
	Enabled.store(on);
	if (!on) ReleaseArea();
	else if (!Join() || !Area) {
		Enabled.store(false);
		return error = Host ? "the host has no room for the charm area (see the log)" : "Player Page Host isn't loaded", false;
	}
	ShowButton();
	Log(false, "turned %s in the settings", on ? "on" : "off");
	return true;
}

// Main thread. A new size: the area is reserved again where it is (the host
// keeps its place when the new size fits there). Refused when charms sit in
// the cells it would remove, or when it would have to move with charms in it.
bool SetSize(int columns, int rows, std::string& error) {
	if (columns == Columns && rows == Rows) return true;
	const int oldColumns = Columns, oldRows = Rows;
	PageHost::AreaInfo old { .structSize = sizeof(old) };
	const bool live = Area && Host && Host->getAreaInfo(Area, &old) == PageHost::Result::Success && old.x >= 0;
	int inside = 0, outside = 0;
	if (live) {
		const bool counted = CountCharms(old, columns, rows, inside, outside);
		if (counted && outside) return error = "take out the charms in the cells it would remove first", false;
	}
	const bool wasOpen = live && Host->isAreaOpen(Area);
	ReleaseArea();
	Columns = columns, Rows = rows;
	PlaceGrid();
	bool ok = !Enabled.load() || (Join() && Area);
	PageHost::AreaInfo now { .structSize = sizeof(now) };
	if (ok && live && inside && (Host->getAreaInfo(Area, &now) != PageHost::Result::Success || now.x != old.x || now.y != old.y)) {
		ok    = false;
		error = "no room to grow it where it is on the page";
		ReleaseArea();
	} else if (!ok) error = "the host has no room for that size (see the log)";
	if (!ok) {
		Columns = oldColumns, Rows = oldRows;
		PlaceGrid();
		if (Enabled.load()) (void)Join();
	}
	if (wasOpen && Area) {
		PrepareDrops();
		(void)Host->openArea(Area);
	}
	if (ok) Log(false, "size %dx%d", Columns, Rows);
	return ok;
}

std::string Trim(std::string_view text) {
	const auto first = text.find_first_not_of(" \t\r\n");
	if (first == std::string_view::npos) return {};
	const auto last = text.find_last_not_of(" \t\r\n");
	return std::string(text.substr(first, last - first + 1));
}

std::string Unquote(std::string value) {
	if (const auto hash = value.find('#'); hash != std::string::npos) value = Trim(value.substr(0, hash));
	if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
	return value;
}

bool ParseInt(const std::string& value, int low, int high, int& out, std::string& error) {
	char*      end    = nullptr;
	const long number = std::strtol(value.c_str(), &end, 10);
	if (value.empty() || *end || number < low || number > high) return error = std::to_string(low) + " to " + std::to_string(high), false;
	out = static_cast<int>(number);
	return true;
}

// One setting from its TOML text. live: applied to the running game (the
// menu); else only stored (the config read at load).
bool Apply(const std::string& key, const std::string& value, bool live, std::string& error) {
	if (key == "enabled") {
		if (value != "true" && value != "false") return error = "true or false", false;
		if (!live) return Enabled.store(value == "true"), true;
		return SetEnabled(value == "true", error);
	}
	if (key == "inventory_charms") {
		if (value != "true" && value != "false") return error = "true or false", false;
		InventoryCharms.store(value == "true");
		return true;
	}
	if (key == "columns" || key == "rows") {
		int number = 0;
		if (!ParseInt(value, 1, key == "columns" ? MaxColumns : MaxRows, number, error)) return false;
		const int columns = key == "columns" ? number : Columns, rows = key == "rows" ? number : Rows;
		if (!live) return Columns = columns, Rows = rows, true;
		return SetSize(columns, rows, error);
	}
	return error = "unknown setting", false;
}

std::string ValueOf(const std::string& key) {
	if (key == "enabled") return Enabled.load() ? "true" : "false";
	if (key == "inventory_charms") return InventoryCharms.load() ? "true" : "false";
	if (key == "columns") return std::to_string(Columns);
	if (key == "rows") return std::to_string(Rows);
	return {};
}

std::string Render() {
	return R"(# Wild Healer's Charm Inventory
#
# A charm inventory on Player Page Host's page, opened from the charm
# button on the inventory's left edge (or the console command "charms", or a key set in the game's Controls menu).
# Only charms go in it, and the charms in it give their bonuses.
# Every setting can also be changed in game with I Hate TOMLs and applies at once.

[wild-healers-charm-inventory]
# false: no button or panel, and charms in the normal inventory give their
# bonuses again. Charms left in the charm inventory stay there, without bonuses.
enabled = )" + ValueOf("enabled") + R"(

# true: charms in the normal inventory give their bonuses too.
inventory_charms = )" + ValueOf("inventory_charms") + R"(

# Size of the charm inventory in cells. columns 1 to 10, rows 1 to 12.
# Take the charms out of the cells you remove before making it smaller.
columns = )" + ValueOf("columns") + R"(
rows = )" + ValueOf("rows") + "\n";
}

void SaveConfig() {
	if (!Context->WriteConfig(Render().c_str())) Log(true, "config: could not write the config file");
}

void LoadConfig() {
	(void)Context->EnsureConfig();
	std::uint32_t     required = 0;
	std::vector<char> buffer(4096);
	if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) {
		if (required <= buffer.size()) {
			Log(true, "config: could not read the config file, using defaults");
			return;
		}
		buffer.resize(required);
		if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) return;
	}
	const std::string_view text(buffer.data(), std::strlen(buffer.data()));
	std::size_t            lineStart = 0;
	while (lineStart < text.size()) {
		auto lineEnd = text.find('\n', lineStart);
		if (lineEnd == std::string_view::npos) lineEnd = text.size();
		const std::string line = Trim(text.substr(lineStart, lineEnd - lineStart));
		lineStart              = lineEnd + 1;
		const auto equals      = line.find('=');
		if (line.empty() || line[0] == '#' || line[0] == '[' || equals == std::string::npos) continue;
		const std::string key   = Trim(std::string_view(line).substr(0, equals));
		const std::string value = Unquote(Trim(std::string_view(line).substr(equals + 1)));
		std::string       error;
		if (!Apply(key, value, false, error)) Log(true, "config: %s = %s ignored (%s)", key.c_str(), value.c_str(), error.c_str());
	}
}

// ---- I Hate TOMLs ----

constexpr const char* Schema = R"(title = "Wild Healer's Charm Inventory"
[[setting]]
key = "enabled"
type = "bool"
label = "Charm inventory"
note = "Off: no button or panel, and charms in the normal inventory give their bonuses again. Charms left in the charm inventory stay there, without bonuses."
[[setting]]
key = "inventory_charms"
type = "bool"
label = "Inventory charms give bonuses"
note = "Charms in the normal inventory give their bonuses too."
[[setting]]
key = "columns"
type = "int"
label = "Columns"
min = 1
max = 10
step = 1
[[setting]]
key = "rows"
type = "int"
label = "Rows"
note = "Take the charms out of the cells you remove before making it smaller."
min = 1
max = 12
step = 1
)";

PluginMenu::Connection Menu;

bool __cdecl MenuGet(const char* key, char* out, std::uint32_t size, void*) noexcept {
	if (!key) return false;
	const std::string value = ValueOf(key);
	if (value.empty()) return false;
	std::snprintf(out, size, "%s", value.c_str());
	return true;
}

bool __cdecl MenuSet(const char* key, const char* value, char* error, std::uint32_t size, void*) noexcept {
	if (!key || !value) return false;
	std::string reason;
	if (!Apply(key, Unquote(Trim(value)), true, reason)) {
		std::snprintf(error, size, "%s", reason.c_str());
		Menu.ValueChanged(key);
		return false;
	}
	SaveConfig();
	return true;
}

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (!D2RL::HasContext(context) || !context->GetApi()) return false;
	Context = context;
	LoadConfig();
	PlaceGrid();
	Log(false, "%s %s loading, %dx%d%s", Info.name, PluginVersion, Columns, Rows, Enabled.load() ? "" : " (off: enabled = false)");
	(void)Menu.Start(context, Schema, MenuGet, MenuSet, nullptr);
	GuiReady = Gui::Init(context, PluginId);
	if (!GuiReady) Log(true, "a window service is missing: no button or panel art (use the charms console command)");
	if (context->QueryService(&CommApi) != D2RL::ServiceQueryResult::Success) CommApi = nullptr;
	if (context->QueryService(&Inventory) != D2RL::ServiceQueryResult::Success) Inventory = nullptr;
	if (context->QueryService(&ItemApi) != D2RL::ServiceQueryResult::Success) ItemApi = nullptr;
	if (!Inventory || !ItemApi) Log(true, "no inventory or item service: unique charms aren't limited to one each");
	(void)Join(); // the host loads first (its DLL name sorts first); else after every plugin has loaded
	if (const D2RL::LifecycleService* lifecycle = nullptr; context->QueryService(&lifecycle) == D2RL::ServiceQueryResult::Success && lifecycle) {
		const D2RL::Lifecycle::DataTablesLoadedListener listener { .structSize = D2RL::Lifecycle::DataTablesLoadedListenerSize, .callback = AfterAllLoaded };
		D2RL::Lifecycle::ListenerHandle                 handle {};
		(void)lifecycle->registerDataTablesLoadedListener(context, &listener, &handle);
	}
	if (GuiReady) {
		AddButton();
		if (!Gui::Commands::Listen(OnCommand)) Log(true, "could not listen to button clicks");
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
	Menu.Stop();
	Gui::TileGrid::Unwrap();
	UnwrapCharmSlot();
	if (Area && Host) (void)Host->releaseArea(Context, Area);
	Area = 0;
	(void)Host.Reset();
}
