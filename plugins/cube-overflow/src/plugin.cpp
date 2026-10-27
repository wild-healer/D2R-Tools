// Cube Overflow: an item you get goes into the Horadric Cube when your
// inventory has no room for it (the cube must be in the inventory).
//
// Game code used (Steam 3.3.93787 / 94995 RVAs, server side):
//   Pickup 0x471950 (Unit* player, u32 itemGuid, bool toInventory,
//                    u32 maxDistance, bool checkCollision, bool toCube) -> bool
//   - toInventory: scrolls into tomes, then a stack, the belt, a free
//     inventory spot. With no spot the item bounces back to the ground and the
//     character says "no room".
//   - neither flag: the item goes to the cursor.
//   - toCube (toInventory false): a free spot on page 3, the cube. The game
//     uses it for its own "pick up into the cube" packet (0x5F, 0x4BB600).
//   AutoDestination 0x46DED0 (Dest* out, Game*, Unit* player, Unit* item):
//     stack, belt or a free inventory spot for an item given to the player
//     (store purchases and gambling go through it); out+0x10 = found.
//   QuestItem 0x517530 (Game*, Unit* player, ...): creates a quest reward and
//     puts it on a free inventory spot (FindSpot, then Dest 0x382E10 with
//     page 0), or drops it at the player's feet.
//   Dest 0x382E10 (Dest* out, u32 page, u32 x | y << 16): a grid destination.
// Store purchases and gambling (client side, D2Client npcmenu 0x1114C0): the
// client picks the destination itself (FindSpot page 0 at 0x1121A4, then Dest
// page 0 at 0x1121CC) and sends it in packet 0x32 as x/y with no page. The
// server rebuilds it as page 0 (Dest at 0x4C46E5) and refuses the purchase
// ("no room") unless it equals what AutoDestination finds.
// Hooks:
//   - Pickup: before an inventory pickup, ask AutoDestination where it would
//     go; when nowhere and the cube has a spot, pick it up with toCube (no
//     failed first try, so no bounce). Cursor pickups (cursor_pickups) the
//     same when the inventory has no spot.
//   - AutoDestination (purchases_and_rewards): when it finds nothing, a cube
//     spot.
//   - FindSpot + Dest at the client purchase sites (purchases_and_rewards):
//     the client's failed search returns the cube spot and its destination
//     gets page 3; the server's rebuilt destination for that x/y gets page 3
//     too, so it equals the cube spot AutoDestination now returns.
//   - QuestItem + FindSpot + Dest (purchases_and_rewards): inside QuestItem,
//     a failed inventory search returns a cube spot and the next destination
//     built on that thread gets page 3.
// All of it runs on the game's server thread: single player and games you host
// (for every player), not games you join. Purchases also need the client
// side, so in games you host they overflow for you only. Each function's first bytes are
// checked at load; on a mismatch the plugin stays inactive.

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

constexpr const char* PluginId      = "cube-overflow";
constexpr const char* PluginVersion = "1.1.0";

const D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = PluginId,
	.name        = "Cube Overflow",
	.version     = PluginVersion,
	.author      = "wild-healer",
	.description = "Items you get with a full inventory go into the Horadric Cube.",
	.flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

struct Settings {
	bool enabled             = true;
	bool cursorPickups       = true;
	bool purchasesAndRewards = true;
} Config;

// ---- native layout (Steam 3.3.93787 / 94995) ----

using Code = std::array<std::uint8_t, 16>;

constexpr std::uint64_t PickupRva = 0x471950;
constexpr Code PickupBytes { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57 };
constexpr std::uint64_t AutoDestRva = 0x46DED0; // Dest* (Dest* out, Game*, Unit* player, Unit* item)
constexpr Code AutoDestBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57 };
constexpr std::uint64_t QuestItemRva = 0x517530; // Unit* (Game*, Unit* player, 4 more)
constexpr Code QuestItemBytes { 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x81, 0xEC, 0xA0, 0x00 };
constexpr std::uint64_t DestRva = 0x382E10; // Dest* (Dest* out, u32 page, u32 x | y << 16)
constexpr Code DestBytes { 0x33, 0xC0, 0x89, 0x51, 0x04, 0x89, 0x01, 0x89, 0x41, 0x0C, 0x48, 0x8B, 0xC1, 0x44, 0x89, 0x41 };
constexpr std::uint64_t IsTypeRva = 0x373890; // int (Unit* item, int itemType)
constexpr Code IsTypeBytes { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57 };
constexpr std::uint64_t GetCubeRva = 0x3887E0; // Unit* (Unit* player): the cube on inventory page 0
constexpr Code GetCubeBytes { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x41, 0xB8, 0xB3, 0x14, 0x00, 0x00, 0x48, 0x8D, 0x15, 0x9D };
constexpr std::uint64_t FindSpotRva = 0x3865B0; // int (Inventory*, Unit* item, int grid, x*, y*, u8 page)
constexpr Code FindSpotBytes { 0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8B, 0xEC, 0x48, 0x83 };
constexpr std::uint64_t PageGridRva = 0x34A410; // int (Unit* player, u8 page, int flag)
constexpr Code PageGridBytes { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57 };
constexpr std::uint64_t InventoryRva = 0x34A360; // Inventory* (Unit*, const char* file, int line)
constexpr Code InventoryBytes { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF1, 0x48, 0x85, 0xC9 };
constexpr std::uint64_t UnitGameRva = 0x48FF00; // Game* (Unit*)
constexpr Code UnitGameBytes { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x75, 0x20, 0x88, 0x4C };
constexpr std::uint64_t FindUnitRva = 0x48FE80; // Unit* (Game*, int type, u32 guid)
constexpr Code FindUnitBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41 };
constexpr std::uint64_t ItemModeRva = 0x34AB60; // int (Unit* item), 3 = on the ground
constexpr Code ItemModeBytes { 0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1A, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C };
constexpr std::uint64_t ItemCodeRva = 0x36EF50; // u32 (Unit* item)
constexpr Code ItemCodeBytes { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF9, 0x48, 0x85, 0xC9 };
// Client: the local player and the grid flag the purchase code uses.
constexpr std::uint64_t ClientPlayerIdRva = 0x8B2D0; // u32 ()
constexpr Code ClientPlayerIdBytes { 0x8B, 0x05, 0x2E, 0x84, 0x99, 0x02, 0xC3, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC };
constexpr std::uint64_t ClientPlayerRva = 0x9A480; // Unit* (u32 id)
constexpr Code ClientPlayerBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x83, 0xF9, 0x08, 0x0F, 0x83, 0x85 };
constexpr std::uint64_t ClientFlagRva = 0x8D3B0; // bool (); the client passes (!value) to PageGrid
constexpr Code ClientFlagBytes { 0x48, 0x8B, 0x05, 0x29, 0x63, 0x99, 0x02, 0x48, 0x85, 0xC0, 0x74, 0x08, 0x80, 0x78, 0x5C, 0x01 };
// Call sites, checked so the return addresses below are the right ones.
constexpr std::uint64_t BuySearchSiteRva = 0x1121A4; // client purchase: FindSpot page 0
constexpr Code BuySearchSiteBytes { 0xE8, 0x07, 0x44, 0x27, 0x00, 0x85, 0xC0, 0x74, 0x29, 0x0F, 0xB7, 0x44, 0x24, 0x68, 0x48, 0x8D };
constexpr std::uint64_t BuyDestSiteRva = 0x1121CC; // client purchase: Dest page 0
constexpr Code BuyDestSiteBytes { 0xE8, 0x3F, 0x0C, 0x27, 0x00, 0xE9, 0x6A, 0xFF, 0xFF, 0xFF, 0x0F, 0xB6, 0x4D, 0xD0, 0x84, 0xC9 };
constexpr std::uint64_t BuyPacketDestSiteRva = 0x4C46E5; // server, packet 0x32: Dest page 0 from the client's x/y
constexpr Code BuyPacketDestSiteBytes { 0xE8, 0x26, 0xE7, 0xEB, 0xFF, 0x0F, 0x10, 0x00, 0x0F, 0x11, 0x45, 0x80, 0x45, 0x32, 0xFF, 0x48 };
constexpr std::uint64_t CallSize = 5;
// The source file name the game passes to the inventory getter (only used in
// its assert messages), and the expected text there.
constexpr std::uint64_t ItemModeFileRva  = 0x1D1C010;
constexpr char          ItemModeFile[]   = "D2Game\\src\\Items\\ItemMode.cpp";
constexpr std::uintptr_t GameGridFlag    = 0x106; // the game passes (byte != 1) to PageGrid
constexpr std::uint32_t  CubeCode        = 'b' | ('o' << 8) | ('x' << 16) | (' ' << 24);
constexpr int            ItemTypeScroll  = 0x16; // goes into a tome first
constexpr std::size_t    DestFound       = 0x10; // AutoDestination result flag
constexpr int            UnitTypeItem    = 4;
constexpr int            ItemModeGround  = 3;
constexpr std::uint8_t   PageInventory   = 0;
constexpr std::uint8_t   PageCube        = 3;

using PickupFn    = bool (*)(void* player, std::uint32_t guid, bool toInventory, std::uint32_t maxDistance, bool checkCollision, bool toCube) noexcept;
using AutoDestFn  = std::uint8_t* (*)(std::uint8_t* out, void* game, void* player, void* item) noexcept;
using QuestItemFn = void* (*)(void* game, void* player, std::uint64_t a3, std::uint64_t a4, std::uint64_t a5, std::uint64_t a6) noexcept;
using DestFn      = void* (*)(void* out, std::uint32_t page, std::uint32_t xy) noexcept;
using IsTypeFn    = int (*)(void* item, int itemType) noexcept;
using GetCubeFn   = void* (*)(void* player) noexcept;
using FindSpotFn  = int (*)(void* inventory, void* item, int grid, void* x, void* y, std::uint8_t page) noexcept;
using PageGridFn  = int (*)(void* player, std::uint8_t page, int flag) noexcept;
using InventoryFn = void* (*)(void* unit, const char* file, int line) noexcept;
using UnitGameFn  = std::uint8_t* (*)(void* unit) noexcept;
using FindUnitFn  = void* (*)(void* game, int type, std::uint32_t guid) noexcept;
using ItemModeFn  = int (*)(void* item) noexcept;
using ItemCodeFn  = std::uint32_t (*)(void* item) noexcept;
using ClientPlayerIdFn = std::uint32_t (*)() noexcept;
using ClientPlayerFn   = void* (*)(std::uint32_t id) noexcept;
using ClientFlagFn     = bool (*)() noexcept;

std::uintptr_t Base {};
// Originals, after the hooks.
PickupFn       Pickup {};
AutoDestFn     AutoDest {};
QuestItemFn    QuestItem {};
FindSpotFn     FindSpot {};
DestFn         Dest {};
// Plain calls.
IsTypeFn       IsType {};
GetCubeFn      GetCube {};
PageGridFn     PageGrid {};
InventoryFn    InventoryOf {};
UnitGameFn     UnitGame {};
FindUnitFn     FindUnit {};
ItemModeFn     ItemMode {};
ItemCodeFn     ItemCode {};
ClientPlayerIdFn ClientPlayerId {};
ClientPlayerFn   ClientPlayer {};
ClientFlagFn     ClientFlag {};
const char*    SourceFile {};

const D2RL::PluginContext* Context {};

// Inside QuestItem, on that thread: whose reward it is, whether the inventory
// search was already seen, and whether the next destination goes to the cube.
void* QuestPlayer {};
DWORD QuestThread {};
bool  QuestSearched {};
bool  QuestToCube {};
bool  QuestRedirected {};

// A purchase the client sent with a cube spot: its Dest gets page 3 (client
// thread), then the server's rebuilt Dest for the same x/y (server thread).
std::atomic<bool>          BuyClientToCube {};
std::atomic<std::uint64_t> BuyServerToCube {}; // x | y << 16, plus bit 32 = pending

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

bool Read(std::uintptr_t address, void* output, std::size_t size) noexcept {
	SIZE_T copied = 0;
	return address != 0 && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), output, size, &copied) && copied == size;
}

bool CodeMatches(std::uint64_t rva, const Code& bytes) noexcept {
	Code code {};
	return Read(Base + rva, code.data(), code.size()) && code == bytes;
}

// ---- server thread ----

void* FindItem(void* player, std::uint32_t guid) noexcept {
	void* const game = UnitGame(player);
	return game ? FindUnit(game, UnitTypeItem, guid) : nullptr;
}

int GridOf(void* player, std::uint8_t page) noexcept {
	std::uint8_t* const game = UnitGame(player);
	return PageGrid(player, page, game && game[GameGridFlag] != 1);
}

// The game's free-spot search on that page. x and y get 4 bytes each, like the
// game gives them (2-byte slots corrupted the caller's saved registers in
// the first 1.0.0 build).
bool FindFreeSpot(void* player, void* item, std::uint8_t page, std::uint32_t* x = nullptr, std::uint32_t* y = nullptr) noexcept {
	void* const inventory = InventoryOf(player, SourceFile, __LINE__);
	if (!inventory) return false;
	std::uint64_t spotX = 0, spotY = 0;
	if (FindSpot(inventory, item, GridOf(player, page), &spotX, &spotY, page) == 0) return false;
	if (x) *x = static_cast<std::uint32_t>(spotX & 0xFFFF);
	if (y) *y = static_cast<std::uint32_t>(spotY & 0xFFFF);
	return true;
}

// A cube in the inventory, and the item isn't that cube.
bool CubeCanTake(void* player, void* item) noexcept {
	return player && item && ItemCode(item) != CubeCode && GetCube(player);
}

bool HookPickup(void* player, std::uint32_t guid, bool toInventory, std::uint32_t maxDistance, bool checkCollision, bool toCube) noexcept {
	if (toCube || (!toInventory && !Config.cursorPickups)) return Pickup(player, guid, toInventory, maxDistance, checkCollision, toCube);
	void* const item = player ? FindItem(player, guid) : nullptr;
	if (!item || ItemMode(item) != ItemModeGround || !CubeCanTake(player, item)) return Pickup(player, guid, toInventory, maxDistance, checkCollision, toCube);
	if (toInventory) {
		// Scrolls look for a tome first; leave them to the game.
		if (IsType(item, ItemTypeScroll)) return Pickup(player, guid, toInventory, maxDistance, checkCollision, toCube);
		// Where the inventory pickup would put it: a stack, the belt or a free spot.
		alignas(16) std::uint8_t dest[32] {};
		AutoDest(dest, UnitGame(player), player, item);
		if (dest[DestFound]) return Pickup(player, guid, toInventory, maxDistance, checkCollision, toCube);
	} else if (FindFreeSpot(player, item, PageInventory)) {
		return Pickup(player, guid, toInventory, maxDistance, checkCollision, toCube);
	}
	if (!FindFreeSpot(player, item, PageCube)) return Pickup(player, guid, toInventory, maxDistance, checkCollision, toCube);
	const std::string name = CodeText(ItemCode(item));
	if (Pickup(player, guid, false, maxDistance, checkCollision, true)) {
		Log(false, "%s: no room in the inventory, picked up into the cube%s", name.c_str(), toInventory ? "" : " (cursor pickup)");
		return true;
	}
	Log(false, "%s: cube pickup refused, left to the game", name.c_str());
	return Pickup(player, guid, toInventory, maxDistance, checkCollision, toCube);
}

std::uint8_t* HookAutoDest(std::uint8_t* out, void* game, void* player, void* item) noexcept {
	AutoDest(out, game, player, item);
	if (!Config.purchasesAndRewards || !out || out[DestFound] || !CubeCanTake(player, item)) return out;
	std::uint32_t x = 0, y = 0;
	if (!FindFreeSpot(player, item, PageCube, &x, &y)) return out;
	Dest(out, PageCube, x | (y << 16));
	out[DestFound] = 1;
	Log(false, "%s: no room in the inventory, goes into the cube", CodeText(ItemCode(item)).c_str());
	return out;
}

void* HookQuestItem(void* game, void* player, std::uint64_t a3, std::uint64_t a4, std::uint64_t a5, std::uint64_t a6) noexcept {
	if (!Config.purchasesAndRewards || QuestPlayer || !player || !GetCube(player)) return QuestItem(game, player, a3, a4, a5, a6);
	QuestPlayer     = player;
	QuestThread     = GetCurrentThreadId();
	QuestSearched   = false;
	QuestToCube     = false;
	QuestRedirected = false;
	void* const item = QuestItem(game, player, a3, a4, a5, a6);
	const bool  cube = QuestRedirected && item;
	QuestPlayer = nullptr;
	QuestToCube = false;
	if (cube) Log(false, "%s: quest reward with no room in the inventory, put in the cube", CodeText(ItemCode(item)).c_str());
	return item;
}

// Logs the first failed inventory search from each place in the game, so a
// "no room" the plugin doesn't handle yet can be traced to its code.
void NoteNoRoomSite(std::uintptr_t returnAddress) noexcept {
	static std::array<std::atomic<std::uintptr_t>, 32> seen {};
	if (returnAddress < Base) return;
	const std::uintptr_t rva = returnAddress - Base;
	for (auto& slot : seen) {
		std::uintptr_t value = slot.load();
		if (value == rva) return;
		if (value == 0 && slot.compare_exchange_strong(value, rva)) {
			Log(false, "no inventory room seen by game code at D2R+0x%llX", static_cast<unsigned long long>(rva));
			return;
		}
		if (value == rva) return;
	}
}

// Client purchase with no inventory spot: the local player's cube spot.
bool ClientBuyCubeSpot(void* inventory, void* item, void* x, void* y) noexcept {
	void* const player = ClientPlayer(ClientPlayerId());
	if (!player || !CubeCanTake(player, item)) return false;
	std::uint64_t spotX = 0, spotY = 0;
	if (FindSpot(inventory, item, PageGrid(player, PageCube, !ClientFlag()), &spotX, &spotY, PageCube) == 0) return false;
	const std::uint32_t cubeX = static_cast<std::uint32_t>(spotX & 0xFFFF), cubeY = static_cast<std::uint32_t>(spotY & 0xFFFF);
	std::memcpy(x, &cubeX, sizeof(cubeX));
	std::memcpy(y, &cubeY, sizeof(cubeY));
	return true;
}

int HookFindSpot(void* inventory, void* item, int grid, void* x, void* y, std::uint8_t page) noexcept {
	const int found = FindSpot(inventory, item, grid, x, y, page);
	const std::uintptr_t caller = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
	if (!found && page == PageInventory && caller == Base + BuySearchSiteRva + CallSize) {
		if (ClientBuyCubeSpot(inventory, item, x, y)) {
			BuyClientToCube = true;
			return 1;
		}
	}
	if (!found && page == PageInventory) NoteNoRoomSite(caller);
	if (found || !QuestPlayer || QuestSearched || page != PageInventory || GetCurrentThreadId() != QuestThread) return found;
	QuestSearched = true;
	std::uint32_t cubeX = 0, cubeY = 0;
	if (!CubeCanTake(QuestPlayer, item) || !FindFreeSpot(QuestPlayer, item, PageCube, &cubeX, &cubeY)) return found;
	std::memcpy(x, &cubeX, sizeof(cubeX));
	std::memcpy(y, &cubeY, sizeof(cubeY));
	QuestToCube     = true;
	QuestRedirected = true;
	return 1;
}

void* HookDest(void* out, std::uint32_t page, std::uint32_t xy) noexcept {
	const std::uintptr_t caller = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
	if (page == PageInventory && caller == Base + BuyDestSiteRva + CallSize && BuyClientToCube.exchange(false)) {
		BuyServerToCube = xy | (std::uint64_t { 1 } << 32);
		return Dest(out, PageCube, xy);
	}
	if (page == PageInventory && caller == Base + BuyPacketDestSiteRva + CallSize) {
		std::uint64_t pending = BuyServerToCube.load();
		if ((pending >> 32) && static_cast<std::uint32_t>(pending) == xy && BuyServerToCube.compare_exchange_strong(pending, 0)) return Dest(out, PageCube, xy);
	}
	if (QuestToCube && page == PageInventory && GetCurrentThreadId() == QuestThread) {
		QuestToCube = false; // consumed: QuestItem builds its destination right after the search
		page        = PageCube;
	}
	return Dest(out, page, xy);
}

bool Locate() noexcept {
	const std::array<std::pair<std::uint64_t, const Code*>, 19> checks { {
		{ PickupRva, &PickupBytes },
		{ AutoDestRva, &AutoDestBytes },
		{ QuestItemRva, &QuestItemBytes },
		{ DestRva, &DestBytes },
		{ IsTypeRva, &IsTypeBytes },
		{ GetCubeRva, &GetCubeBytes },
		{ FindSpotRva, &FindSpotBytes },
		{ PageGridRva, &PageGridBytes },
		{ InventoryRva, &InventoryBytes },
		{ UnitGameRva, &UnitGameBytes },
		{ FindUnitRva, &FindUnitBytes },
		{ ItemModeRva, &ItemModeBytes },
		{ ItemCodeRva, &ItemCodeBytes },
		{ ClientPlayerIdRva, &ClientPlayerIdBytes },
		{ ClientPlayerRva, &ClientPlayerBytes },
		{ ClientFlagRva, &ClientFlagBytes },
		{ BuySearchSiteRva, &BuySearchSiteBytes },
		{ BuyDestSiteRva, &BuyDestSiteBytes },
		{ BuyPacketDestSiteRva, &BuyPacketDestSiteBytes },
	} };
	for (const auto& [rva, bytes] : checks) {
		if (!CodeMatches(rva, *bytes)) {
			Log(true, "game code differs at D2R+0x%llX", static_cast<unsigned long long>(rva));
			return false;
		}
	}
	char file[sizeof(ItemModeFile)] {};
	if (!Read(Base + ItemModeFileRva, file, sizeof(file)) || std::memcmp(file, ItemModeFile, sizeof(file)) != 0) {
		Log(true, "game data differs at D2R+0x%llX", static_cast<unsigned long long>(ItemModeFileRva));
		return false;
	}
	SourceFile  = reinterpret_cast<const char*>(Base + ItemModeFileRva);
	Pickup      = reinterpret_cast<PickupFn>(Base + PickupRva);
	QuestItem   = reinterpret_cast<QuestItemFn>(Base + QuestItemRva);
	GetCube     = reinterpret_cast<GetCubeFn>(Base + GetCubeRva);
	IsType      = reinterpret_cast<IsTypeFn>(Base + IsTypeRva);
	FindSpot    = reinterpret_cast<FindSpotFn>(Base + FindSpotRva);
	AutoDest    = reinterpret_cast<AutoDestFn>(Base + AutoDestRva);
	Dest        = reinterpret_cast<DestFn>(Base + DestRva);
	PageGrid    = reinterpret_cast<PageGridFn>(Base + PageGridRva);
	InventoryOf = reinterpret_cast<InventoryFn>(Base + InventoryRva);
	UnitGame    = reinterpret_cast<UnitGameFn>(Base + UnitGameRva);
	FindUnit    = reinterpret_cast<FindUnitFn>(Base + FindUnitRva);
	ItemMode    = reinterpret_cast<ItemModeFn>(Base + ItemModeRva);
	ItemCode    = reinterpret_cast<ItemCodeFn>(Base + ItemCodeRva);
	ClientPlayerId = reinterpret_cast<ClientPlayerIdFn>(Base + ClientPlayerIdRva);
	ClientPlayer   = reinterpret_cast<ClientPlayerFn>(Base + ClientPlayerRva);
	ClientFlag     = reinterpret_cast<ClientFlagFn>(Base + ClientFlagRva);
	return true;
}

// Installs a hook; on success Original becomes the call into the original
// code, otherwise it keeps pointing at the function.
template <typename Function>
bool Hook(std::uint64_t rva, const Code& bytes, Function target, Function& original) noexcept {
	Function trampoline = nullptr;
	if (!Context->InstallInlineHook(rva, bytes.data(), static_cast<std::uint32_t>(bytes.size()), target, &trampoline) || !trampoline) {
		Log(true, "could not hook D2R+0x%llX", static_cast<unsigned long long>(rva));
		return false;
	}
	original = trampoline;
	return true;
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
		else if (key == "cursor_pickups") Config.cursorPickups = value != "false";
		else if (key == "purchases_and_rewards") Config.purchasesAndRewards = value != "false";
		else Log(true, "config: unknown key %s", key.c_str());
	}
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
		Log(false, "Cube Overflow %s: disabled in config", PluginVersion);
		return true;
	}
	Base = context->exeBase;
	if (!Base || !Locate()) {
		Log(true, "Cube Overflow %s: inactive, this game build isn't supported (Steam 3.3.93787 / 3.3.94995 are)", PluginVersion);
		return true;
	}
	if (!Hook(PickupRva, PickupBytes, &HookPickup, Pickup)) {
		Log(true, "Cube Overflow %s: inactive, could not hook D2R+0x%llX", PluginVersion, static_cast<unsigned long long>(PickupRva));
		return true;
	}
	// Each of these only acts when purchases_and_rewards is on, so a partial
	// failure just turns the option off.
	if (Config.purchasesAndRewards && !(Hook(AutoDestRva, AutoDestBytes, &HookAutoDest, AutoDest) && Hook(FindSpotRva, FindSpotBytes, &HookFindSpot, FindSpot) &&
	                                    Hook(DestRva, DestBytes, &HookDest, Dest) && Hook(QuestItemRva, QuestItemBytes, &HookQuestItem, QuestItem))) {
		Config.purchasesAndRewards = false;
		Log(true, "Cube Overflow %s: purchases and rewards won't use the cube, a hook could not be installed", PluginVersion);
	}
	Log(false, "Cube Overflow %s: active, pickups%s%s with a full inventory go to the cube", PluginVersion, Config.cursorPickups ? " (cursor pickups too)" : "",
	    Config.purchasesAndRewards ? ", purchases, gambling and quest rewards" : "");
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {}
