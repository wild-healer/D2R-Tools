// Permanent Cow Portal: a red portal to the Moo Moo Farm stands in the Rogue
// Encampment in every game, next to the waypoint, once you have beaten Baal
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
//   - The Rogue Encampment waypoint is found when the game creates it: the
//     waypoint InitFn (entry 17 of the object InitFn table 0x239B980, read
//     from the dispatcher at 0x510CBF) is wrapped. The wrapper keeps the
//     waypoint's unit id for that game when it stands in level 1.
//   - The game update (UpdateGameFrame 0x406550 (game), once per server frame)
//     is hooked. Once a second it checks that game: no cow portal yet (A1Q4
//     flag 11 clear), the town waypoint known (FindUnit 0x48FE80 (game, 2,
//     id), still a waypoint in level 1), and a player in the game (client list
//     game +0x138, next +0x628, state +4 == 4, ClientPlayer 0x484C90 (client,
//     0)) who has beaten Baal on this difficulty. Then it opens the portal next
//     to the waypoint and sets the flag, as the cube would.
// Everything runs on the game server thread, between two frames, like a cube
// transmute. The portal is a normal game object, so it stays for the whole
// game, players who join see it, and the cube's cow portal recipe then says
// the portal is already open.

#include <D2RLPlugin/api.h>
#include <windows.h>

#include "code_bytes.h"
#include "i_hate_tomls_client.h"

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

constexpr const char* PluginId      = "permanent-cow-portal";
constexpr const char* PluginVersion = "0.0.1";

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
	std::atomic<int>  offsetX { 4 };
	std::atomic<int>  offsetY { 4 };
} Config;

constexpr int OffsetLimit = 30;

// ---- native layout ----

constexpr std::uint64_t CowPortalRva        = 0x5DA2A0;
constexpr std::uint64_t CowPortalBodyRva    = 0x5DA2A5;
constexpr std::uint64_t ClientLoopRva       = 0x4066F9;
constexpr std::uint64_t FindUnitRva         = 0x48FE80;
constexpr std::uint64_t CreatePortalRva     = 0x432CE0;
constexpr std::uint64_t FrameRva            = 0x406550;
constexpr std::uint64_t InitDispatchRva     = 0x510CBF;
constexpr std::uint64_t WaypointInitRva     = 0x512A50;
constexpr std::uint64_t ClientPlayerRva     = 0x484C90;
constexpr std::uint64_t QuestGlobalGetRva   = 0x514FD0;
constexpr std::uint64_t QuestGlobalSetRva   = 0x5181C0;
constexpr std::uint64_t PlayerDataRva       = 0x34B240;
constexpr std::uint64_t QuestFlagGetRva     = 0x325C50;
constexpr std::uint64_t UnitRoomRva         = 0x34B440;
constexpr std::uint64_t LevelOfRoomRva      = 0x2EFC10;
constexpr std::uint64_t GetCoordsRva        = 0x34A040;
constexpr std::uint64_t FindFreeSpotRva     = 0x517F90;

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
constexpr std::uintptr_t InitGame         = 0x00;
constexpr std::uintptr_t InitObject       = 0x08;
constexpr std::uintptr_t InitRoom         = 0x10;
constexpr std::uint32_t  ClientInGame     = 4;
constexpr std::uint32_t  TypeObject       = 2;
constexpr int            WaypointInitFn   = 17;
constexpr int            RogueEncampment  = 1;
constexpr int            MooMooFarm       = 39;
constexpr int            PortalPermanent  = 60;
constexpr int            QuestCain        = 4;  // A1Q4, holds the game's "cow portal opened" flag
constexpr int            FlagCowPortal    = 11;
constexpr int            QuestDiablo      = 26;
constexpr int            QuestBaal        = 40;
constexpr std::uint32_t  CheckEveryFrames = 25; // once a second
constexpr int            MaxAttempts      = 5;

struct Coord {
	std::int32_t x;
	std::int32_t y;
};

using FrameFn          = std::uint64_t (*)(void* game) noexcept;
using InitFn           = void (*)(void* params) noexcept;
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
using CreatePortalFn   = int (*)(void* game, void* player, void* room, int x, int y, int destLevel, void** outPortal, int objectClass, int a9, int a10) noexcept;

const D2RL::PluginContext* Context {};
std::uintptr_t             Base {};
FrameFn                    OriginalFrame {};
std::atomic<InitFn>        OriginalWaypointInit {};
InitFn*                    InitSlot {};
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

// ---- the Rogue Encampment waypoint, per game ----

struct GameWaypoint {
	void*         game {};
	std::uint32_t id {};
	std::uint32_t classId {};
	int           attempts {};
	bool          gaveUp {};
};

SRWLOCK                     WaypointLock = SRWLOCK_INIT;
std::array<GameWaypoint, 8> Waypoints {};
std::size_t                 NextWaypoint {};

void RememberWaypoint(void* game, std::uint32_t id, std::uint32_t classId) noexcept {
	AcquireSRWLockExclusive(&WaypointLock);
	GameWaypoint* slot = nullptr;
	for (auto& entry : Waypoints)
		if (entry.game == game) slot = &entry;
	if (!slot) {
		slot         = &Waypoints[NextWaypoint];
		NextWaypoint = (NextWaypoint + 1) % Waypoints.size();
	}
	*slot = GameWaypoint { game, id, classId, 0, false };
	ReleaseSRWLockExclusive(&WaypointLock);
}

bool WaypointOf(void* game, GameWaypoint& out) noexcept {
	AcquireSRWLockShared(&WaypointLock);
	bool found = false;
	for (const auto& entry : Waypoints)
		if (entry.game == game) {
			out   = entry;
			found = true;
		}
	ReleaseSRWLockShared(&WaypointLock);
	return found;
}

// Counts a failed try; true when it is time to give up on this game.
bool Failed(void* game) noexcept {
	AcquireSRWLockExclusive(&WaypointLock);
	bool giveUp = false;
	for (auto& entry : Waypoints)
		if (entry.game == game && ++entry.attempts >= MaxAttempts) giveUp = entry.gaveUp = true;
	ReleaseSRWLockExclusive(&WaypointLock);
	return giveUp;
}

void HookWaypointInit(void* params) noexcept {
	if (const auto original = OriginalWaypointInit.load(std::memory_order_acquire)) original(params);
	if (!params || !Active.load(std::memory_order_acquire)) return;
	void* game   = Field<void*>(params, InitGame);
	void* object = Field<void*>(params, InitObject);
	void* room   = Field<void*>(params, InitRoom);
	if (!game || !object || !room || LevelOfRoom(room) != RogueEncampment) return;
	RememberWaypoint(game, Field<std::uint32_t>(object, UnitId), Field<std::uint32_t>(object, UnitClass));
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

void* QualifyingPlayer(void* game) noexcept {
	const bool needBaal = Config.requireBaal.load(std::memory_order_relaxed);
	auto*      client   = Field<std::uint8_t*>(game, GameClients);
	for (int guard = 0; client && guard < 64; ++guard, client = Field<std::uint8_t*>(client, ClientNext)) {
		if (Field<std::uint32_t>(client, ClientState) != ClientInGame) continue;
		void* player = ClientPlayer(client, 0);
		if (player && (!needBaal || HasBeaten(game, player))) return player;
	}
	return nullptr;
}

void TryOpen(void* game) noexcept {
	GameWaypoint waypoint {};
	if (!WaypointOf(game, waypoint) || waypoint.gaveUp) return;
	if (QuestGlobalGet(game, QuestCain, FlagCowPortal)) return; // already open in this game
	void* player = QualifyingPlayer(game);
	if (!player) return;
	// The id was kept for this game pointer; a later game can reuse the pointer, so check the unit is still that waypoint.
	void* object = FindUnit(game, TypeObject, waypoint.id);
	if (!object || Field<std::uint32_t>(object, UnitType) != TypeObject || Field<std::uint32_t>(object, UnitClass) != waypoint.classId) return;
	void* room = UnitRoom(object);
	if (!room || LevelOfRoom(room) != RogueEncampment) return;

	Coord spot {};
	GetCoords(object, &spot);
	const Coord waypointAt = spot;
	spot.x += Config.offsetX.load(std::memory_order_relaxed);
	spot.y += Config.offsetY.load(std::memory_order_relaxed);
	void* freeRoom = nullptr;
	FindFreeSpot(game, room, &spot, 3, 0x400, &freeRoom, 4, 100);
	if (freeRoom && CreatePortal(game, player, freeRoom, spot.x, spot.y, MooMooFarm, nullptr, PortalPermanent, 0, 0)) {
		QuestGlobalSet(game, QuestCain, FlagCowPortal);
		Opened.fetch_add(1, std::memory_order_relaxed);
		Log(false, "cow portal opened in the Rogue Encampment at %d,%d (waypoint %d,%d), game frame %u", spot.x, spot.y, waypointAt.x, waypointAt.y,
		    Field<std::uint32_t>(game, GameFrame));
		return;
	}
	if (Failed(game)) Log(true, "could not open the cow portal near the waypoint (%d,%d) after %d tries, giving up for this game", waypointAt.x, waypointAt.y, MaxAttempts);
}

std::uint64_t HookFrame(void* game) noexcept {
	if (game && Active.load(std::memory_order_acquire) && Config.enabled.load(std::memory_order_relaxed) &&
	    Field<std::uint32_t>(game, GameFrame) % CheckEveryFrames == 0)
		TryOpen(game);
	return OriginalFrame(game);
}

// ---- version check and hooks ----

bool Locate() noexcept {
	if (!CodeMatches(CowPortalBodyRva, CowPortalBody)) return Problem = "cow portal code differs", false;
	if (!CodeMatches(ClientLoopRva, ClientLoopBytes)) return Problem = "game client loop differs", false;
	if (!CodeMatches(FindUnitRva, FindUnitBytes)) return Problem = "unit lookup code differs", false;
	if (!CodeMatches(CreatePortalRva, CreatePortalBytes)) return Problem = "portal code differs", false;
	if (!CodeMatches(InitDispatchRva, InitDispatchBytes)) return Problem = "object init code differs", false;
	if (!CodeMatches(WaypointInitRva, WaypointInitBytes)) return Problem = "waypoint init code differs", false;
	std::int32_t displacement {};
	std::memcpy(&displacement, InitDispatchBytes.data() + 3, sizeof(displacement));
	InitSlot = reinterpret_cast<InitFn*>(Base + InitDispatchRva + 7 + displacement) + WaypointInitFn;
	InitFn current {};
	if (!Read(reinterpret_cast<std::uintptr_t>(InitSlot), &current, sizeof(current)) || !current) return Problem = "waypoint init entry is empty", false;
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
	return true;
}

bool WriteSlot(InitFn value) noexcept {
	DWORD old = 0;
	if (!VirtualProtect(InitSlot, sizeof(InitFn), PAGE_READWRITE, &old)) return false;
	InterlockedExchangePointer(reinterpret_cast<void* volatile*>(InitSlot), reinterpret_cast<void*>(value));
	DWORD ignored = 0;
	VirtualProtect(InitSlot, sizeof(InitFn), old, &ignored);
	return true;
}

bool InstallHooks() noexcept {
	OriginalWaypointInit.store(*InitSlot, std::memory_order_release);
	if (!WriteSlot(&HookWaypointInit)) return Problem = "could not wrap the waypoint init", false;
	if (!Context->InstallInlineHook(FrameRva, FrameBytes.data(), static_cast<std::uint32_t>(FrameBytes.size()), reinterpret_cast<void*>(&HookFrame),
	                                reinterpret_cast<void**>(&OriginalFrame)) ||
	    !OriginalFrame) {
		(void)WriteSlot(OriginalWaypointInit.load());
		return Problem = "could not hook the game update (another plugin may hook it)", false;
	}
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
	if (key == "offset_x" || key == "offset_y") {
		int number = 0;
		if (!ParseInt(value, number)) return false;
		(key == "offset_x" ? Config.offsetX : Config.offsetY).store(number);
		return true;
	}
	return false;
}

bool Get(std::string_view key, char* out, std::uint32_t size) noexcept {
	if (key == "enabled") std::snprintf(out, size, "%s", Bool(Config.enabled));
	else if (key == "require_baal") std::snprintf(out, size, "%s", Bool(Config.requireBaal));
	else if (key == "offset_x") std::snprintf(out, size, "%d", Config.offsetX.load());
	else if (key == "offset_y") std::snprintf(out, size, "%d", Config.offsetY.load());
	else return false;
	return true;
}

std::string Render() {
	return std::string(R"(# Permanent Cow Portal
#
# A red portal to the Moo Moo Farm stands in the Rogue Encampment, next to
# the waypoint, in every game you play once you have beaten Baal on that
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

# Where the portal stands, in tiles from the waypoint (-30 to 30). The game
# moves it to the nearest free spot.
offset_x = )" + std::to_string(Config.offsetX.load()) + R"(
offset_y = )" + std::to_string(Config.offsetY.load()) + "\n";
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
note = "Opens next to the waypoint. Applies to the next game."
[[setting]]
key = "require_baal"
type = "bool"
label = "Only after Baal"
note = "Once you have beaten Baal on that difficulty (Diablo in a Classic game)."
[[setting]]
key = "offset_x"
type = "int"
label = "Distance from the waypoint (x)"
min = -30
max = 30
step = 1
[[setting]]
key = "offset_y"
type = "int"
label = "Distance from the waypoint (y)"
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
	std::string text;
	try {
		text = Trim(value);
	} catch (...) {
		return false;
	}
	if (!Apply(key, text)) {
		std::snprintf(error, size, "%s", std::string_view(key).starts_with("offset") ? "a whole number from -30 to 30" : "true or false");
		return false;
	}
	SaveConfig();
	return true;
}

// ---- console ----

auto StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
	if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
	char line[300] {};
	if (Active.load())
		std::snprintf(line, sizeof(line), "permanent-cow-portal %s: %s, %s, offset %d,%d; portals opened since launch: %u", PluginVersion, Config.enabled ? "on" : "turned off",
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
	(void)context->RegisterConsoleCommand("permanent-cow-portal", StatusCommand, "Show Permanent Cow Portal status.");
	LoadConfig();
	(void)Menu.Start(context, Schema, MenuGet, MenuSet, nullptr);
	// Hooked even when turned off: it can be turned on while playing.
	if (!Base || !Locate() || !InstallHooks()) {
		Log(true, "Permanent Cow Portal %s: inactive, %s (Steam 3.3.93787 / 3.3.94995 / 3.4.95041 are supported)", PluginVersion, Problem.c_str());
		return true;
	}
	Active.store(true, std::memory_order_release);
	Log(false, "Permanent Cow Portal %s: %s (%s, offset %d,%d), game update hooked at D2R+0x%llX, waypoint init wrapped", PluginVersion,
	    Config.enabled ? "active" : "loaded but turned off", Config.requireBaal ? "after Baal" : "always", Config.offsetX.load(), Config.offsetY.load(),
	    static_cast<unsigned long long>(FrameRva));
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	Active.store(false, std::memory_order_release);
	if (InitSlot && OriginalWaypointInit.load() && *InitSlot == &HookWaypointInit) (void)WriteSlot(OriginalWaypointInit.load());
	Menu.Stop();
}
