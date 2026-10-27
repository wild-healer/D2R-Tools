// Cube Empty: middle-click the Horadric Cube in your inventory to move
// everything inside it into your inventory. Items that don't fit stay in the
// cube.
//
// Moving uses plugin SDK calls only:
//   - Hover: an item tooltip listener records which item the game is showing a
//     tooltip for, and where the mouse was. A click counts as "on the cube"
//     when that item is the cube in the inventory and the mouse has not moved
//     away from it since.
//   - Click: a small thread watches the mouse button while the game window is
//     in front (the SDK's key listener only sees keyboard keys).
//   - Move: one ExistingItemTransaction per cube item, each moving it to the
//     inventory with automatic placement, on the game thread. A transaction is
//     all or nothing, so one item per transaction lets the ones that fit move.
// When something stays in the cube, the character says "impossible", like the
// game does. That needs the game itself (RVAs of Steam 3.3.93787 / 94995):
//   - PlaySound 0x1A0780 (int sound, Unit* unit, int, int, bool): sounds.txt
//     row, played on the unit (0 = no unit, e.g. 3 cursor_error).
//   - Player sound table 0x22A4110: 0x30 bytes per character, in sounds.txt
//     order (amazon, barbarian, necromancer, paladin, sorceress, assassin,
//     druid, warlock), "impossible" at +0x08 (amazon_impossible_1 = 2953).
//   - Local player: 0x9A480 (0x8B2D0()); unit +0x04 = class.
// A code mismatch only leaves the character silent.

#include <D2RLPlugin/api.h>
#include <windows.h>

#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr const char* PluginId      = "cube-empty";
constexpr const char* PluginVersion = "1.0.0";

const D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = PluginId,
	.name        = "Cube Empty",
	.version     = PluginVersion,
	.author      = "wild-healer",
	.description = "Middle-click the Horadric Cube to move its contents into the inventory.",
	.flags       = D2RL::PluginFlags::Client,
};

namespace Items = D2RL::Items;

// Item code of the Horadric Cube, as ItemInfo::code packs it ("box ").
constexpr std::uint32_t CubeCode = 'b' | ('o' << 8) | ('x' << 16) | (' ' << 24);

// A tooltip this recent is still on screen.
constexpr std::uint64_t TooltipFreshMs = 250;
// Otherwise the mouse must still be this close to where it was when the cube's
// tooltip appeared (screen pixels; the cube is about 2 cells wide).
constexpr long HoverRadius = 40;

struct Settings {
	bool        enabled      = true;
	bool        requireHover = true;
	int         button       = VK_MBUTTON;
	std::string buttonName   = "middle";
} Config;

// ---- native layout (Steam 3.3.93787 / 94995) ----

// Functions with the bytes their entry must start with; -1 = any byte (another
// plugin may have put a jump at the start of PlaySound).
constexpr std::uint64_t LocalIndexRva = 0x8B2D0; // int ()
constexpr std::array<int, 7> LocalIndexBytes { 0x8B, 0x05, -1, -1, -1, -1, 0xC3 };
constexpr std::uint64_t PlayerUnitRva = 0x9A480; // Unit* (int index)
constexpr std::array<int, 16> PlayerUnitBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x83, 0xF9, 0x08, 0x0F, 0x83, 0x85 };
constexpr std::uint64_t PlaySoundRva = 0x1A0780; // void (int sound, Unit* unit, int, int, bool)
constexpr std::array<int, 20> PlaySoundBytes { -1, -1, -1, -1, -1, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xD1, 0x48, 0x81, 0xEC, 0x00, 0x01, 0x00, 0x00 };
constexpr std::uint64_t ImpossibleRva    = 0x22A4118; // int[class * 12]
constexpr std::uintptr_t ImpossibleStride = 0x30;
constexpr std::uint32_t  ClassCount       = 8;
constexpr std::int32_t   AmazonImpossible = 2953;
// Sound table row of each class (playerclass.txt order: amazon, sorceress,
// necromancer, paladin, barbarian, druid, assassin, warlock).
constexpr std::array<std::uint32_t, ClassCount> SoundRow { 0, 4, 2, 3, 1, 6, 5, 7 };
constexpr std::uintptr_t UnitClass        = 0x04;

using LocalIndexFn = int (*)() noexcept;
using PlayerUnitFn = void* (*)(int index) noexcept;
using PlaySoundFn  = void (*)(int sound, void* unit, int, int, bool) noexcept;

std::uintptr_t Base {};
LocalIndexFn   LocalIndex {};
PlayerUnitFn   PlayerUnit {};
PlaySoundFn    PlaySound {};

const D2RL::PluginContext*       Context {};
const D2RL::InventoryService*    Inventory {};
const D2RL::ItemService*         ItemApi {};
const D2RL::ThreadService*       Threads {};
const D2RL::SharedEventService*  Events {};
D2RL::SharedEvents::ListenerHandle TooltipListener {};

// Last tooltip, written and read on the UI thread only.
struct Hover {
	bool          cube {};
	std::uint64_t at {};
	POINT         mouse {};
} LastTooltip;

std::atomic<bool> Running {};
// Set while a click is being handled; queued work can be dropped on a game
// change, so it also expires.
std::atomic<std::uint64_t> BusySince {};
constexpr std::uint64_t    BusyTimeoutMs = 3000;
HANDLE            Watcher {};

void Log(bool warn, const char* format, ...) noexcept {
	if (!Context) return;
	char    line[512] {};
	va_list args;
	va_start(args, format);
	std::vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	warn ? Context->LogWarn(line) : Context->LogInfo(line);
}

std::string CodeText(std::uint32_t code) {
	std::string text;
	for (int i = 0; i < 4; ++i) {
		const char c = static_cast<char>(code >> (i * 8));
		if (c && c != ' ') text += c;
	}
	return text;
}

// ---- impossible ----

bool Read(std::uintptr_t address, void* output, std::size_t size) noexcept {
	SIZE_T copied = 0;
	return address != 0 && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), output, size, &copied) && copied == size;
}

template <std::size_t N>
bool CodeMatches(std::uint64_t rva, const std::array<int, N>& bytes) noexcept {
	std::array<std::uint8_t, N> code {};
	if (!Read(Base + rva, code.data(), N)) return false;
	for (std::size_t i = 0; i < N; ++i)
		if (bytes[i] >= 0 && code[i] != bytes[i]) return false;
	return true;
}

bool LocateSpeech() noexcept {
	std::int32_t amazon = 0;
	if (!Base || !CodeMatches(LocalIndexRva, LocalIndexBytes) || !CodeMatches(PlayerUnitRva, PlayerUnitBytes) || !CodeMatches(PlaySoundRva, PlaySoundBytes) ||
	    !Read(Base + ImpossibleRva, &amazon, sizeof(amazon)) || amazon != AmazonImpossible)
		return false;
	LocalIndex = reinterpret_cast<LocalIndexFn>(Base + LocalIndexRva);
	PlayerUnit = reinterpret_cast<PlayerUnitFn>(Base + PlayerUnitRva);
	PlaySound  = reinterpret_cast<PlaySoundFn>(Base + PlaySoundRva);
	return true;
}

// UI thread: the character says "impossible".
void __cdecl SayImpossible(const D2RL::PluginContext*, void*) noexcept {
	if (!PlaySound) return;
	auto* const   player = static_cast<std::uint8_t*>(PlayerUnit(LocalIndex()));
	std::uint32_t cls    = ClassCount;
	if (!player || !Read(reinterpret_cast<std::uintptr_t>(player) + UnitClass, &cls, sizeof(cls)) || cls >= ClassCount) return;
	std::int32_t sound = 0;
	if (!Read(Base + ImpossibleRva + SoundRow[cls] * ImpossibleStride, &sound, sizeof(sound)) || sound <= 0) return;
	PlaySound(sound, player, 0, 0, false);
}

// ---- hover ----

void __cdecl OnTooltip(const D2RL::PluginContext* context, D2RL::SharedEvents::ItemTooltipEvent* event, void*) noexcept {
	if (!event || !ItemApi) return;
	Items::ItemInfo info { .structSize = Items::ItemInfoSize };
	const bool      known = ItemApi->getItemInfo(context, event->item, &info) == Items::Result::Success;
	LastTooltip.cube      = known && info.code == CubeCode && info.container == Items::ItemContainer::Inventory;
	LastTooltip.at        = GetTickCount64();
	GetCursorPos(&LastTooltip.mouse);
	// Nothing is added to the tooltip: length stays 0.
}

bool HoveringCube() noexcept {
	if (!LastTooltip.cube) return false;
	if (GetTickCount64() - LastTooltip.at <= TooltipFreshMs) return true;
	POINT mouse {};
	GetCursorPos(&mouse);
	return labs(mouse.x - LastTooltip.mouse.x) <= HoverRadius && labs(mouse.y - LastTooltip.mouse.y) <= HoverRadius;
}

// ---- moving ----

struct EmptyJob {
	D2RL::PlayerHandle player {};
};

Items::Result MoveToInventory(D2RL::PlayerHandle player, D2RL::ItemHandle item) noexcept {
	Items::ExistingItemOperation operation {};
	operation.structSize                         = Items::ExistingItemOperationSize;
	operation.kind                               = Items::ExistingItemOperationKind::Move;
	operation.item                               = item;
	operation.move.destination.structSize        = Items::ItemDestinationSize;
	operation.move.destination.container         = Items::ItemContainer::Inventory;
	operation.move.destination.placement         = Items::Placement::Automatic;
	const Items::ExistingItemTransaction transaction {
		.structSize     = Items::ExistingItemTransactionSize,
		.player         = player,
		.operationCount = 1,
		.operations     = &operation,
	};
	Items::ExistingItemTransactionResult result { .structSize = Items::ExistingItemTransactionResultSize };
	return ItemApi->executeExistingItemTransaction(Context, &transaction, &result);
}

D2RL::Inventory::IterationAction __cdecl CollectCubeItem(const D2RL::PluginContext*, const Items::ItemInfo* item, void* userData) noexcept {
	if (item && item->container == Items::ItemContainer::Cube) static_cast<std::vector<Items::ItemInfo>*>(userData)->push_back(*item);
	return D2RL::Inventory::IterationAction::Continue;
}

void __cdecl EmptyCube(const D2RL::PluginContext* context, void* userData) noexcept {
	auto* job = static_cast<EmptyJob*>(userData);
	std::vector<Items::ItemInfo> items;
	const D2RL::Inventory::ItemFilter filter {
		.structSize    = D2RL::Inventory::ItemFilterSize,
		.containerMask = Items::ContainerBit(Items::ItemContainer::Cube),
	};
	const auto scan = Inventory->forEachInventoryItem(context, job->player, &filter, CollectCubeItem, &items);
	if (scan != D2RL::Inventory::Result::Success) {
		Log(true, "could not list the cube (result %u)", static_cast<unsigned>(scan));
	} else {
		// Top-left first, like a player would take them out.
		std::size_t moved = 0;
		for (const auto& item : items) {
			const auto result = MoveToInventory(job->player, item.handle);
			if (result == Items::Result::Success) ++moved;
			else Log(false, "%s at %d,%d stays in the cube (result %u)", CodeText(item.code).c_str(), item.x, item.y, static_cast<unsigned>(result));
		}
		Log(false, "cube emptied: %zu of %zu items moved to the inventory", moved, items.size());
		if (moved < items.size()) (void)Threads->runOnUiThread(context, SayImpossible, nullptr);
	}
	delete job;
	BusySince.store(0);
}

// Runs on the UI thread after a click.
void __cdecl OnClick(const D2RL::PluginContext* context, void*) noexcept {
	if (Config.requireHover && !HoveringCube()) {
		BusySince.store(0);
		return;
	}
	auto* job = new EmptyJob;
	if (Inventory->getLocalPlayer(context, &job->player) != D2RL::Inventory::Result::Success) {
		delete job;
		BusySince.store(0);
		return;
	}
	D2RL::ItemHandle cursor {};
	if (Inventory->getCursorItem(context, job->player, &cursor) == D2RL::Inventory::Result::Success && cursor != D2RL::ItemHandle {}) {
		Log(false, "an item is on the cursor, cube left as it is");
		delete job;
		BusySince.store(0);
		return;
	}
	const auto queued = Threads->runOnGameThread(context, EmptyCube, job);
	if (queued != D2RL::Threads::Result::Success) {
		Log(true, "could not reach the game thread (result %u); in someone else's game items can't be moved", static_cast<unsigned>(queued));
		delete job;
		BusySince.store(0);
	}
}

bool GameInFront() noexcept {
	DWORD process = 0;
	GetWindowThreadProcessId(GetForegroundWindow(), &process);
	return process == GetCurrentProcessId();
}

DWORD WINAPI WatchMouse(void*) noexcept {
	bool wasDown = false;
	while (Running.load()) {
		const bool down = (GetAsyncKeyState(Config.button) & 0x8000) != 0;
		const std::uint64_t now = GetTickCount64(), since = BusySince.load();
		if (down && !wasDown && GameInFront() && (since == 0 || now - since > BusyTimeoutMs)) {
			BusySince.store(now);
			if (Threads->runOnUiThread(Context, OnClick, nullptr) != D2RL::Threads::Result::Success) BusySince.store(0);
		}
		wasDown = down;
		Sleep(15);
	}
	return 0;
}

// ---- config ----

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
		if (key == "enabled") Config.enabled = value != "false";
		else if (key == "require_hover") Config.requireHover = value != "false";
		else if (key == "button") {
			if (value == "middle") Config.button = VK_MBUTTON;
			else if (value == "back" || value == "x1") Config.button = VK_XBUTTON1;
			else if (value == "forward" || value == "x2") Config.button = VK_XBUTTON2;
			else {
				Log(true, "config: unknown button \"%s\", using middle", value.c_str());
				continue;
			}
			Config.buttonName = value;
		} else Log(true, "config: unknown key %s", key.c_str());
	}
}

template <typename Service>
const Service* Query(const char* name) {
	const Service* service = nullptr;
	if (Context->QueryService(&service) != D2RL::ServiceQueryResult::Success) {
		Log(true, "%s service unavailable", name);
		return nullptr;
	}
	return service;
}

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (!D2RL::HasContext(context) || !context->GetApi()) return false;
	Context = context;
	LoadConfig();
	if (!Config.enabled) {
		Log(false, "Cube Empty %s: disabled in config", PluginVersion);
		return true;
	}
	Inventory = Query<D2RL::InventoryService>("inventory");
	ItemApi   = Query<D2RL::ItemService>("item");
	Threads   = Query<D2RL::ThreadService>("thread");
	Events    = Query<D2RL::SharedEventService>("shared event");
	if (!D2RL::HasInventoryServiceField(Inventory, D2RL::InventoryServiceRequiredSize) || !D2RL::HasItemServiceField(ItemApi, D2RL::ItemServiceRequiredSize) ||
	    !D2RL::HasThreadServiceField(Threads, D2RL::ThreadServiceRequiredSize) || !Events) {
		Log(true, "Cube Empty %s: inactive, this D2RLoader lacks a needed service", PluginVersion);
		return true;
	}
	if (Config.requireHover) {
		const D2RL::SharedEvents::ItemTooltipListener listener {
			.structSize = D2RL::SharedEvents::ItemTooltipListenerSize,
			.region     = D2RL::SharedEvents::ItemTooltipRegion::Description,
			.position   = D2RL::SharedEvents::ItemTooltipPosition::Bottom,
			.anchor     = D2RL::SharedEvents::ItemTooltipAnchor::None,
			.fallback   = D2RL::SharedEvents::ItemTooltipFallback::Omit,
			.callback   = OnTooltip,
		};
		const auto result = Events->registerItemTooltipListener(context, &listener, &TooltipListener);
		if (result != D2RL::SharedEvents::Result::Success) {
			Log(true, "Cube Empty %s: inactive, tooltip listener refused (result %u)", PluginVersion, static_cast<unsigned>(result));
			return true;
		}
	}
	Base = context->exeBase;
	if (!LocateSpeech()) Log(true, "game code differs: the character won't say \"impossible\" when items stay in the cube");
	Running.store(true);
	Watcher = CreateThread(nullptr, 0, WatchMouse, nullptr, 0, nullptr);
	Log(false, "Cube Empty %s: active, %s click %s empties the cube", PluginVersion, Config.buttonName.c_str(), Config.requireHover ? "on the cube" : "anywhere in game");
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	Running.store(false);
	if (Watcher) {
		WaitForSingleObject(Watcher, 1000);
		CloseHandle(Watcher);
		Watcher = nullptr;
	}
}
