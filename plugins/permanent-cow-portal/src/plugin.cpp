// Permanent Cow Portal: a red portal to the Moo Moo Farm stands in the Rogue
// Encampment in every game, at a spot you choose near the stash, once you have beaten Baal
// on that difficulty (Diablo in a Classic game). Offline and in games you
// host, like the online "Legacy of Terror" event.
//
// How the game opens the cow portal (Steam 3.3.93787 / 3.3.94995 / 3.4.95041,
// the same code at the same addresses):
//   - The Horadric Cube "Cow Portal" output runs OpenCowPortal 0x5DA2A0
//     (game, player). It fails when the game's quest state A1Q4 flag 11 is
//     set (QuestGlobalGet 0x514FD0 (game, 4, 11): a cow portal was already
//     opened in this game), when the player has not beaten Baal (quest 40,
//     flag 0, read with QuestFlagGet 0x325C50 on PlayerData 0x34B240 +0x40
//     + difficulty*8; Diablo, quest 26, in a Classic game: game +0x106 == 1),
//     or when the player is not in the Rogue Encampment (level 1). Else it
//     finds a free spot (FindFreeSpot 0x517F90 (game, room, &xy, 3, 0x400,
//     &room, 4, 100)), calls CreatePortal 0x432CE0 (game, player, room, x, y,
//     level 39, nullptr, object 60 PortalPermanent, 0, 0), which also builds
//     the Moo Moo Farm and its return portal, and sets the A1Q4 flag 11
//     (QuestGlobalSet 0x5181C0).
// The plugin does the same, with the same calls, without the cube:
//   - The game update (UpdateGameFrame 0x406550 (game), once per server frame)
//     is hooked. Once a second it checks that game: no cow portal yet (A1Q4
//     flag 11 clear), a player in the game (client list game +0x138, next
//     +0x628, state +4 == 4, ClientPlayer 0x484C90 (client, 0)) who has beaten
//     Baal on this difficulty, and the Rogue Encampment stash (object 267 in
//     level 1) built. Then it opens the portal at the stash + offset_x/y and
//     sets the flag, as the cube would.
//   - Units are found in the game's unit hash: game + (0x446 + type*0x80) * 8,
//     128 buckets per type (UnitBucket 0x4925B0), next unit at +0x158 (FindUnit
//     0x48FE80). The portal's room is the one holding the spot (RoomAtCoords
//     0x38DFB0 (room, x, y), the stash's room or a neighbour).
//   - "permanent-cow-portal here" (console, or the I Hate TOMLs button) saves
//     the offset from the stash to the item on the ground nearest you (within
//     10 tiles), or to where you stand. The stash is the anchor because the
//     Rogue Encampment has 4 layouts (TownN1/E1/S1/W1.ds1) and the camp itself
//     is the same in all of them.
// Everything runs on the game server thread, between two frames, like a cube
// transmute. The portal is a normal game object, so it stays for the whole
// game, players who join see it, and the cube's cow portal recipe then says
// the portal is already open.

#include <D2RLPlugin/api.h>
#include <windows.h>

#include "code_bytes.h"
#include "i_hate_tomls_client.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr const char* PluginId      = "permanent-cow-portal";
constexpr const char* PluginVersion = "0.0.2";

const D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = PluginId,
	.name        = "Permanent Cow Portal",
	.version     = PluginVersion,
	.author      = "wild-healer",
	.description = "A cow level portal stands in the Rogue Encampment once you have beaten Baal on that difficulty.",
	.flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

struct Settings {
	std::atomic<bool> enabled { true };
	std::atomic<bool> requireBaal { true };
	std::atomic<int>  offsetX { -8 };
	std::atomic<int>  offsetY { 2 };
} Config;

constexpr int OffsetLimit = 30;

// ---- native layout ----

constexpr std::uint64_t CowPortalRva        = 0x5DA2A0;
constexpr std::uint64_t CowPortalBodyRva    = 0x5DA2A5;
constexpr std::uint64_t ClientLoopRva       = 0x4066F9;
constexpr std::uint64_t FindUnitRva         = 0x48FE80;
constexpr std::uint64_t CreatePortalRva     = 0x432CE0;
constexpr std::uint64_t FrameRva            = 0x406550;
constexpr std::uint64_t ClientPlayerRva     = 0x484C90;
constexpr std::uint64_t QuestGlobalGetRva   = 0x514FD0;
constexpr std::uint64_t QuestGlobalSetRva   = 0x5181C0;
constexpr std::uint64_t PlayerDataRva       = 0x34B240;
constexpr std::uint64_t QuestFlagGetRva     = 0x325C50;
constexpr std::uint64_t UnitRoomRva         = 0x34B440;
constexpr std::uint64_t LevelOfRoomRva      = 0x2EFC10;
constexpr std::uint64_t GetCoordsRva        = 0x34A040;
constexpr std::uint64_t FindFreeSpotRva     = 0x517F90;
constexpr std::uint64_t UnitBucketRva       = 0x4925B0;
constexpr std::uint64_t BucketWalkRva       = 0x48FECF;
constexpr std::uint64_t RoomAtRva           = 0x38DFB0;

constexpr std::uintptr_t GameDifficulty   = 0x104;   // u8
constexpr std::uintptr_t GameExpansion    = 0x106;   // u8, 1 = Classic
constexpr std::uintptr_t GameClients      = 0x138;   // Client*
constexpr std::uintptr_t GameFrame        = 0x170;   // u32
constexpr std::uintptr_t ClientState      = 0x04;    // u32, 4 = in game
constexpr std::uintptr_t ClientNext       = 0x628;   // Client*
constexpr std::uintptr_t PlayerQuests     = 0x40;    // QuestRecord*[3]
constexpr std::uintptr_t UnitType         = 0x00;
constexpr std::uintptr_t UnitClass        = 0x04;
constexpr std::uintptr_t UnitId           = 0x08;
constexpr std::uint32_t  ClientInGame     = 4;
constexpr std::uint32_t  TypeObject       = 2;
constexpr std::uint32_t  TypeItem         = 4;
constexpr std::uint32_t  StashClass       = 267; // objects.txt Bank
constexpr std::uintptr_t UnitHashBase     = 0x446;
constexpr std::uint32_t  UnitHashBuckets  = 0x80;
constexpr std::uintptr_t UnitHashNext     = 0x158;
constexpr int            PlaceItemRange   = 10;
constexpr int            RogueEncampment  = 1;
constexpr int            MooMooFarm       = 39;
constexpr int            PortalPermanent  = 60;
constexpr int            QuestCain        = 4;  // A1Q4, holds the game's "cow portal opened" flag
constexpr int            FlagCowPortal    = 11;
constexpr int            QuestDiablo      = 26;
constexpr int            QuestBaal        = 40;
constexpr std::uint32_t  CheckEveryFrames = 25; // once a second
constexpr int            MaxAttempts      = 30;

struct Coord {
	std::int32_t x;
	std::int32_t y;
};

using FrameFn          = std::uint64_t (*)(void* game) noexcept;
using FindUnitFn       = void* (*)(void* game, std::uint32_t type, std::uint32_t id) noexcept;
using ClientPlayerFn   = void* (*)(void* client, int flag) noexcept;
using QuestGlobalGetFn = int (*)(void* game, int quest, int flag) noexcept;
using QuestGlobalSetFn = void (*)(void* game, int quest, int flag) noexcept;
using PlayerDataFn     = std::uint8_t* (*)(void* unit) noexcept;
using QuestFlagGetFn   = int (*)(void* record, int quest, int flag) noexcept;
using UnitRoomFn       = void* (*)(void* unit) noexcept;
using LevelOfRoomFn    = int (*)(void* room) noexcept;
using GetCoordsFn      = void (*)(void* unit, Coord* out) noexcept;
using FindFreeSpotFn   = std::uint64_t (*)(void* game, void* room, Coord* coord, int size, int mask, void** outRoom, int a7, int a8) noexcept;
using RoomAtFn         = void* (*)(void* room, int x, int y) noexcept;
using CreatePortalFn   = int (*)(void* game, void* player, void* room, int x, int y, int destLevel, void** outPortal, int objectClass, int a9, int a10) noexcept;

const D2RL::PluginContext* Context {};
std::uintptr_t             Base {};
FrameFn                    OriginalFrame {};
FindUnitFn                 FindUnit {};
ClientPlayerFn             ClientPlayer {};
QuestGlobalGetFn           QuestGlobalGet {};
QuestGlobalSetFn           QuestGlobalSet {};
PlayerDataFn               PlayerData {};
QuestFlagGetFn             QuestFlagGet {};
UnitRoomFn                 UnitRoom {};
LevelOfRoomFn              LevelOfRoom {};
GetCoordsFn                GetCoords {};
FindFreeSpotFn             FindFreeSpot {};
CreatePortalFn             CreatePortal {};
RoomAtFn                   RoomAt {};
std::atomic<bool>          Active { false };
std::atomic<unsigned>      Opened { 0 };
std::string                Problem;

void Log(bool warn, const char* format, ...) noexcept {
	if (!Context) return;
	char    line[512] {};
	va_list args;
	va_start(args, format);
	std::vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	warn ? Context->LogWarn(line) : Context->LogInfo(line);
}

bool Read(std::uintptr_t address, void* output, std::size_t size) noexcept {
	SIZE_T copied = 0;
	return address != 0 && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), output, size, &copied) && copied == size;
}

template <std::size_t N>
bool CodeMatches(std::uint64_t rva, const std::array<std::uint8_t, N>& bytes) noexcept {
	std::array<std::uint8_t, N> code {};
	return Read(Base + rva, code.data(), N) && code == bytes;
}

template <typename T>
T Field(const void* base, std::uintptr_t offset) noexcept {
	T value {};
	std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(T));
	return value;
}

// ---- units ----

// Calls f(unit) for every unit of a type in the game's unit hash, until f returns true.
template <typename F>
void ForEachUnit(void* game, std::uint32_t type, F f) noexcept {
	auto* buckets = static_cast<std::uint8_t*>(game) + (UnitHashBase + type * UnitHashBuckets) * sizeof(void*);
	for (std::uint32_t bucket = 0; bucket < UnitHashBuckets; ++bucket) {
		auto* unit = Field<std::uint8_t*>(buckets, bucket * sizeof(void*));
		for (int guard = 0; unit && guard < 4096; ++guard, unit = Field<std::uint8_t*>(unit, UnitHashNext))
			if (f(unit)) return;
	}
}

bool InTown(void* unit, void** roomOut = nullptr) noexcept {
	void* room = UnitRoom(unit);
	if (roomOut) *roomOut = room;
	return room && LevelOfRoom(room) == RogueEncampment;
}

// The Rogue Encampment stash: its spot and room.
bool FindStash(void* game, Coord& at, void*& room) noexcept {
	bool found = false;
	ForEachUnit(game, TypeObject, [&](std::uint8_t* unit) {
		if (Field<std::uint32_t>(unit, UnitClass) != StashClass || !InTown(unit, &room)) return false;
		GetCoords(unit, &at);
		return found = true;
	});
	return found;
}

// ---- per game ----

struct GameState {
	void*         game {};
	std::uint32_t lastFrame {};
	int           attempts {};
	bool          gaveUp {};
};

std::array<GameState, 8> Games {};
std::size_t              NextGame {};

// Only used on the game server thread. A new game can reuse an old game's
// address; its frame counter starting again tells them apart.
GameState& StateOf(void* game) noexcept {
	const std::uint32_t frame = Field<std::uint32_t>(game, GameFrame);
	GameState*          state = nullptr;
	for (auto& entry : Games)
		if (entry.game == game) state = &entry;
	if (!state || frame < state->lastFrame) {
		if (!state) {
			state    = &Games[NextGame];
			NextGame = (NextGame + 1) % Games.size();
		}
		*state = GameState { game };
	}
	state->lastFrame = frame;
	return *state;
}

// ---- opening the portal ----

bool HasBeaten(void* game, void* player) noexcept {
	const std::uint8_t* data = PlayerData(player);
	if (!data) return false;
	const std::uint8_t difficulty = Field<std::uint8_t>(game, GameDifficulty);
	if (difficulty > 2) return false;
	void* record = Field<void*>(data, PlayerQuests + difficulty * sizeof(void*));
	if (!record) return false;
	const int quest = Field<std::uint8_t>(game, GameExpansion) == 1 ? QuestDiablo : QuestBaal;
	return QuestFlagGet(record, quest, 0) != 0;
}

// The first player in the game that passes the test.
template <typename F>
void* FindPlayer(void* game, F test) noexcept {
	auto* client = Field<std::uint8_t*>(game, GameClients);
	for (int guard = 0; client && guard < 64; ++guard, client = Field<std::uint8_t*>(client, ClientNext)) {
		if (Field<std::uint32_t>(client, ClientState) != ClientInGame) continue;
		void* player = ClientPlayer(client, 0);
		if (player && test(player)) return player;
	}
	return nullptr;
}

void TryOpen(void* game) noexcept {
	GameState& state = StateOf(game);
	if (state.gaveUp) return;
	if (QuestGlobalGet(game, QuestCain, FlagCowPortal)) return; // already open in this game
	const bool needBaal = Config.requireBaal.load(std::memory_order_relaxed);
	void*      player   = FindPlayer(game, [&](void* unit) { return !needBaal || HasBeaten(game, unit); });
	if (!player) return;
	Coord stash {};
	void* stashRoom = nullptr;
	if (!FindStash(game, stash, stashRoom)) return; // not built yet: the town fills in around the players

	Coord wanted { stash.x + Config.offsetX.load(std::memory_order_relaxed), stash.y + Config.offsetY.load(std::memory_order_relaxed) };
	Coord spot   = wanted;
	void* room   = RoomAt(stashRoom, spot.x, spot.y);
	if (!room) room = stashRoom;
	void* freeRoom = nullptr;
	FindFreeSpot(game, room, &spot, 3, 0x400, &freeRoom, 4, 100);
	const bool created = freeRoom && CreatePortal(game, player, freeRoom, spot.x, spot.y, MooMooFarm, nullptr, PortalPermanent, 0, 0);
	if (created) {
		QuestGlobalSet(game, QuestCain, FlagCowPortal);
		Opened.fetch_add(1, std::memory_order_relaxed);
		Log(false, "cow portal opened in the Rogue Encampment at %d,%d (asked %d,%d = stash %d,%d %+d,%+d), game frame %u", spot.x, spot.y, wanted.x, wanted.y, stash.x,
		    stash.y, wanted.x - stash.x, wanted.y - stash.y, Field<std::uint32_t>(game, GameFrame));
		return;
	}
	++state.attempts;
	if (state.attempts == 1 || state.attempts == MaxAttempts)
		Log(true, "try %d: could not open the cow portal at %d,%d (stash %d,%d): %s", state.attempts, wanted.x, wanted.y, stash.x, stash.y,
		    freeRoom ? "the game refused the portal" : "no free spot found there");
	if (state.attempts >= MaxAttempts) {
		state.gaveUp = true;
		Log(true, "giving up on the cow portal for this game");
	}
}

// ---- "here": the portal's spot from where you stand ----

std::atomic<bool> PlaceRequested { false };
std::atomic<bool> PlaceDone { false };
SRWLOCK           PlaceLock = SRWLOCK_INIT;
std::string       PlaceResult;

void SaveConfig();

void SetPlaceResult(const char* format, ...) noexcept {
	char    line[300] {};
	va_list args;
	va_start(args, format);
	std::vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	Log(false, "%s", line);
	try {
		AcquireSRWLockExclusive(&PlaceLock);
		PlaceResult = line;
		ReleaseSRWLockExclusive(&PlaceLock);
	} catch (...) {
		ReleaseSRWLockExclusive(&PlaceLock);
	}
	PlaceDone.store(true, std::memory_order_release);
}

void Place(void* game) noexcept {
	void* player = FindPlayer(game, [](void* unit) { return InTown(unit); });
	if (!player) return SetPlaceResult("permanent-cow-portal: stand in the Rogue Encampment to choose the portal's spot");
	Coord stash {};
	void* stashRoom = nullptr;
	if (!FindStash(game, stash, stashRoom)) return SetPlaceResult("permanent-cow-portal: the stash was not found, walk closer to it and try again");
	Coord standing {};
	GetCoords(player, &standing);
	// An item on the ground near you wins over where you stand.
	Coord item {};
	int   best = PlaceItemRange + 1;
	ForEachUnit(game, TypeItem, [&](std::uint8_t* unit) {
		if (!InTown(unit)) return false;
		Coord at {};
		GetCoords(unit, &at);
		const int distance = std::max(std::abs(at.x - standing.x), std::abs(at.y - standing.y));
		if (distance < best) {
			best = distance;
			item = at;
		}
		return false;
	});
	const bool  onItem = best <= PlaceItemRange;
	const Coord spot   = onItem ? item : standing;
	const int   dx = spot.x - stash.x, dy = spot.y - stash.y;
	if (std::abs(dx) > OffsetLimit || std::abs(dy) > OffsetLimit)
		return SetPlaceResult("permanent-cow-portal: that spot is too far from the stash (%+d,%+d, %d tiles at most)", dx, dy, OffsetLimit);
	Config.offsetX.store(dx);
	Config.offsetY.store(dy);
	SaveConfig();
	SetPlaceResult("permanent-cow-portal: the portal will open %s (stash %+d,%+d) from the next game", onItem ? "where the item on the ground is" : "where you stand", dx, dy);
}

void RequestPlace() noexcept {
	PlaceDone.store(false, std::memory_order_release);
	PlaceRequested.store(true, std::memory_order_release);
}

std::uint64_t HookFrame(void* game) noexcept {
	if (game && Active.load(std::memory_order_acquire)) {
		if (PlaceRequested.exchange(false, std::memory_order_acq_rel)) Place(game);
		if (Config.enabled.load(std::memory_order_relaxed) && Field<std::uint32_t>(game, GameFrame) % CheckEveryFrames == 0) TryOpen(game);
	}
	return OriginalFrame(game);
}

// ---- version check and hooks ----

bool Locate() noexcept {
	if (!CodeMatches(CowPortalBodyRva, CowPortalBody)) return Problem = "cow portal code differs", false;
	if (!CodeMatches(ClientLoopRva, ClientLoopBytes)) return Problem = "game client loop differs", false;
	if (!CodeMatches(FindUnitRva, FindUnitBytes)) return Problem = "unit lookup code differs", false;
	if (!CodeMatches(CreatePortalRva, CreatePortalBytes)) return Problem = "portal code differs", false;
	if (!CodeMatches(UnitBucketRva, UnitBucketBytes)) return Problem = "unit hash code differs", false;
	if (!CodeMatches(BucketWalkRva, BucketWalkBytes)) return Problem = "unit hash walk differs", false;
	if (!CodeMatches(RoomAtRva, RoomAtBytes)) return Problem = "room lookup code differs", false;
	FindUnit       = reinterpret_cast<FindUnitFn>(Base + FindUnitRva);
	ClientPlayer   = reinterpret_cast<ClientPlayerFn>(Base + ClientPlayerRva);
	QuestGlobalGet = reinterpret_cast<QuestGlobalGetFn>(Base + QuestGlobalGetRva);
	QuestGlobalSet = reinterpret_cast<QuestGlobalSetFn>(Base + QuestGlobalSetRva);
	PlayerData     = reinterpret_cast<PlayerDataFn>(Base + PlayerDataRva);
	QuestFlagGet   = reinterpret_cast<QuestFlagGetFn>(Base + QuestFlagGetRva);
	UnitRoom       = reinterpret_cast<UnitRoomFn>(Base + UnitRoomRva);
	LevelOfRoom    = reinterpret_cast<LevelOfRoomFn>(Base + LevelOfRoomRva);
	GetCoords      = reinterpret_cast<GetCoordsFn>(Base + GetCoordsRva);
	FindFreeSpot   = reinterpret_cast<FindFreeSpotFn>(Base + FindFreeSpotRva);
	CreatePortal   = reinterpret_cast<CreatePortalFn>(Base + CreatePortalRva);
	RoomAt         = reinterpret_cast<RoomAtFn>(Base + RoomAtRva);
	return true;
}

bool InstallHooks() noexcept {
	if (!Context->InstallInlineHook(FrameRva, FrameBytes.data(), static_cast<std::uint32_t>(FrameBytes.size()), reinterpret_cast<void*>(&HookFrame),
	                                reinterpret_cast<void**>(&OriginalFrame)) ||
	    !OriginalFrame)
		return Problem = "could not hook the game update (another plugin may hook it)", false;
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

const char* Bool(bool value) noexcept {
	return value ? "true" : "false";
}

bool ParseInt(const std::string& text, int& out) noexcept {
	if (text.empty()) return false;
	char*      end   = nullptr;
	const long value = std::strtol(text.c_str(), &end, 10);
	if (!end || *end != 0 || value < -OffsetLimit || value > OffsetLimit) return false;
	out = static_cast<int>(value);
	return true;
}

// Applies one key; false when the key or value is not valid.
bool Apply(std::string_view key, const std::string& value) noexcept {
	if (key == "enabled" || key == "require_baal") {
		if (value != "true" && value != "false") return false;
		(key == "enabled" ? Config.enabled : Config.requireBaal).store(value == "true");
		return true;
	}
	if (key == "spot_x" || key == "spot_y") {
		int number = 0;
		if (!ParseInt(value, number)) return false;
		(key == "spot_x" ? Config.offsetX : Config.offsetY).store(number);
		return true;
	}
	return false;
}

bool Get(std::string_view key, char* out, std::uint32_t size) noexcept {
	if (key == "enabled") std::snprintf(out, size, "%s", Bool(Config.enabled));
	else if (key == "require_baal") std::snprintf(out, size, "%s", Bool(Config.requireBaal));
	else if (key == "spot_x") std::snprintf(out, size, "%d", Config.offsetX.load());
	else if (key == "spot_y") std::snprintf(out, size, "%d", Config.offsetY.load());
	else return false;
	return true;
}

std::string Render() {
	return std::string(R"(# Permanent Cow Portal
#
# A red portal to the Moo Moo Farm stands in the Rogue Encampment, near the
# stash, in every game you play once you have beaten Baal on that
# difficulty (Diablo in a Classic game). No Wirt's Leg needed, offline and in
# games you host, like the online Legacy of Terror event. The cow level opens
# even after the Cow King was killed.
#
# D2RLoader writes this file to d2rloader/config/permanent-cow-portal.toml on
# first launch. Every setting can also be changed in game with I Hate TOMLs;
# changes apply to the next game.

[permanent-cow-portal]
enabled = )") + Bool(Config.enabled) + R"(

# Only open the portal once you have beaten Baal on the game's difficulty
# (Diablo in a Classic game). false: the portal is always there.
require_baal = )" + Bool(Config.requireBaal) + R"(

# Where the portal stands, in tiles from the stash (-30 to 30). The game moves
# it to the nearest free spot. Easiest: drop an item where you want it (or
# stand there) and type "permanent-cow-portal here" in the console, or use the
# "Put it here" button in I Hate TOMLs.
spot_x = )" + std::to_string(Config.offsetX.load()) + R"(
spot_y = )" + std::to_string(Config.offsetY.load()) + "\n";
}

void SaveConfig() {
	try {
		if (!Context->WriteConfig(Render().c_str())) Log(true, "config: could not write the config file");
	} catch (...) {
	}
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
		if (!Apply(key, value)) Log(true, "config: ignored %s = %s", key.c_str(), value.c_str());
	}
}

// ---- I Hate TOMLs ----

constexpr const char* Schema = R"S(title = "Permanent Cow Portal"
[[setting]]
key = "enabled"
type = "bool"
label = "Cow portal in the Rogue Encampment"
note = "Applies to the next game."
[[setting]]
key = "require_baal"
type = "bool"
label = "Only after Baal"
note = "Once you have beaten Baal on that difficulty (Diablo in a Classic game)."
[[setting]]
key = "place_here"
type = "action"
label = "Put it here"
note = "From the next game, the portal opens on the item on the ground nearest you (within 10 tiles), or where you stand. In the Rogue Encampment."
[[setting]]
key = "spot_x"
type = "int"
label = "Tiles from the stash (x)"
min = -30
max = 30
step = 1
[[setting]]
key = "spot_y"
type = "int"
label = "Tiles from the stash (y)"
min = -30
max = 30
step = 1
)S";

PluginMenu::Connection Menu;

bool __cdecl MenuGet(const char* key, char* out, std::uint32_t size, void*) noexcept {
	return key && Get(key, out, size);
}

bool __cdecl MenuSet(const char* key, const char* value, char* error, std::uint32_t size, void*) noexcept {
	if (!key || !value) return false;
	if (std::string_view(key) == "place_here") {
		if (!Active.load()) return std::snprintf(error, size, "%s", "the plugin is inactive"), false;
		RequestPlace();
		return true;
	}
	std::string text;
	try {
		text = Trim(value);
	} catch (...) {
		return false;
	}
	if (!Apply(key, text)) {
		std::snprintf(error, size, "%s", std::string_view(key).starts_with("spot") ? "a whole number from -30 to 30" : "true or false");
		return false;
	}
	SaveConfig();
	return true;
}

// ---- console ----

auto StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
	if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
	char line[300] {};
	if (command->args && Trim(std::string_view(command->args, command->argsLength)) == "here") {
		if (!Active.load()) {
			command->plugin->WriteConsoleMessage("permanent-cow-portal: inactive");
			return D2RL::ConsoleCommandResult::Handled;
		}
		RequestPlace();
		// The game server runs on its own thread: wait a little for its next frame.
		for (int wait = 0; wait < 30 && !PlaceDone.load(std::memory_order_acquire); ++wait) Sleep(10);
		if (PlaceDone.load(std::memory_order_acquire)) {
			AcquireSRWLockShared(&PlaceLock);
			std::snprintf(line, sizeof(line), "%s", PlaceResult.c_str());
			ReleaseSRWLockShared(&PlaceLock);
		} else std::snprintf(line, sizeof(line), "%s", "permanent-cow-portal: asked, the result goes to the log (no game running?)");
		command->plugin->WriteConsoleMessage(line);
		return D2RL::ConsoleCommandResult::Handled;
	}
	if (Active.load())
		std::snprintf(line, sizeof(line), "permanent-cow-portal %s: %s, %s, stash %+d,%+d; portals opened since launch: %u", PluginVersion, Config.enabled ? "on" : "turned off",
		              Config.requireBaal ? "after Baal" : "always", Config.offsetX.load(), Config.offsetY.load(), Opened.load());
	else std::snprintf(line, sizeof(line), "permanent-cow-portal %s: inactive (%s)", PluginVersion, Problem.empty() ? "unknown" : Problem.c_str());
	command->plugin->WriteConsoleMessage(line);
	Log(false, "%s", line);
	return D2RL::ConsoleCommandResult::Handled;
}

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (!D2RL::HasContext(context) || !context->GetApi()) return false;
	Context = context;
	Base    = context->exeBase;
	(void)context->RegisterConsoleCommand("permanent-cow-portal", StatusCommand, "Permanent Cow Portal status; \"here\" puts the portal on the item at your feet (or where you stand).");
	LoadConfig();
	(void)Menu.Start(context, Schema, MenuGet, MenuSet, nullptr);
	// Hooked even when turned off: it can be turned on while playing.
	if (!Base || !Locate() || !InstallHooks()) {
		Log(true, "Permanent Cow Portal %s: inactive, %s (Steam 3.3.93787 / 3.3.94995 / 3.4.95041 are supported)", PluginVersion, Problem.c_str());
		return true;
	}
	Active.store(true, std::memory_order_release);
	Log(false, "Permanent Cow Portal %s: %s (%s, stash %+d,%+d), game update hooked at D2R+0x%llX,", PluginVersion,
	    Config.enabled ? "active" : "loaded but turned off", Config.requireBaal ? "after Baal" : "always", Config.offsetX.load(), Config.offsetY.load(),
	    static_cast<unsigned long long>(FrameRva));
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	Active.store(false, std::memory_order_release);
	Menu.Stop();
}
