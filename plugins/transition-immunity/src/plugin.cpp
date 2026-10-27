// Transition Immunity: after anything with a loading screen (portal, waypoint,
// entrance, stairs, joining a game) the player can't be hurt or cursed until
// they move, or for duration_seconds at most. A symbol floating over the
// character's head (the armor shrine shield by default) shows it.
//
// Game code used (Steam 3.3.93787 / 94995 RVAs):
//   Server
//   - ApplyHit 0x44A9B0 (Game*, Unit* attacker, Unit* defender, Damage*): the
//     step every hit goes through once it landed (melee, missiles, spells,
//     traps): life, mana and stamina loss, poison, burning, freeze, chill,
//     stun, knockback, hit recovery, leech. Skipping it for the protected
//     player means the hit does nothing.
//   - CurseTarget 0x55A7E0 (Unit* target, CurseContext*) -> int: the per-target
//     step of srvdofunc 30 (all curses, the monsters' too). Returning 0 skips
//     that target, as the game does for targets it rejects.
//   Client (UI thread)
//   - local player: PlayerByIndex 0x9A480 (LocalIndex 0x8B2D0).
//   - level: LevelOfRoom 0x2EFC10 (UnitRoom 0x34B440 (unit)), mode unit+0xC,
//     position path(unit+0x38)+2 / +6.
//   - indicator: CreateOverlay 0x150280 (unit, overlay, 3, 0, 0, 0, 0) and
//     RemoveOverlay 0x152090 (unit, 0, overlay), as the game does for state
//     overlays and shrine symbols.
// The protection starts when the client sees the player in a new level, and
// also covers the loading time itself: while the server already has the
// player in another level than the client shows, the player is protected too.
// The server checks run on the game's server thread, so it works in single
// player and in games you host (for you only), not in games you join.

#include <D2RLPlugin/api.h>
#include <windows.h>

#include <array>
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

constexpr const char* PluginId      = "transition-immunity";
constexpr const char* PluginVersion = "1.0.0";

const D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = PluginId,
	.name        = "Transition Immunity",
	.version     = PluginVersion,
	.author      = "wild-healer",
	.description = "Immune to attacks after a loading screen until you move or for a minute.",
	.flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

namespace Tables = D2RL::DataTables;

struct Settings {
	bool        enabled   = true;
	int         seconds   = 60;
	bool        indicator = true;
	std::string overlay   = "ShrineArmor";
} Config;

// ---- native layout (Steam 3.3.93787 / 94995) ----

using Code = std::array<std::uint8_t, 16>;

constexpr std::uint64_t ApplyHitRva = 0x44A9B0;
constexpr Code ApplyHitBytes { 0x40, 0x53, 0x55, 0x57, 0x41, 0x56, 0x48, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00, 0x49, 0x8B, 0xD9 };
constexpr std::uint64_t CurseTargetRva = 0x55A7E0;
constexpr Code CurseTargetBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57 };
// Hooked by Visual Clutter: its first 5 bytes may be a jump, the rest is checked.
constexpr std::uint64_t CreateOverlayRva = 0x150280;
constexpr Code CreateOverlayBytes { 0x40, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x57, 0x48, 0x81, 0xEC, 0x30, 0x01, 0x00, 0x00, 0x45 };
constexpr std::uint64_t RemoveOverlayRva = 0x152090;
constexpr Code RemoveOverlayBytes { 0x48, 0x85, 0xC9, 0x0F, 0x84, 0x98, 0x00, 0x00, 0x00, 0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48 };
constexpr std::uint64_t PlayerByIndexRva = 0x9A480;
constexpr Code PlayerByIndexBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x83, 0xF9, 0x08, 0x0F, 0x83, 0x85 };
constexpr std::uint64_t LocalIndexRva = 0x8B2D0; // mov eax, [rip+x]; ret
constexpr std::uint64_t UnitRoomRva = 0x34B440;
constexpr Code UnitRoomBytes { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C };
constexpr std::uint64_t LevelOfRoomRva = 0x2EFC10;
constexpr Code LevelOfRoomBytes { 0x48, 0x85, 0xC9, 0x75, 0x03, 0x33, 0xC0, 0xC3, 0x48, 0x8B, 0x49, 0x18, 0xE9, 0x9F, 0x13, 0x07 };

constexpr std::uintptr_t UnitTypeOffset = 0x00; // u32, 0 = player
constexpr std::uintptr_t UnitIdOffset   = 0x08; // u32
constexpr std::uintptr_t UnitModeOffset = 0x0C; // u32
constexpr std::uintptr_t UnitPathOffset = 0x38; // Path*: +2 x, +6 y (u16)
constexpr std::uint32_t  NoId           = 0xFFFFFFFF;
constexpr std::uint32_t  ModeWalk = 2, ModeRun = 3, ModeTownWalk = 6;
constexpr int            OverlayType   = 3;    // stays until removed
constexpr ULONGLONG      GraceMs       = 600;  // arrival settles; moves are not counted yet
constexpr int            MoveTolerance = 2;    // sub-tiles

using ApplyHitFn      = void (*)(void* game, void* attacker, void* defender, void* damage) noexcept;
using CurseTargetFn   = int (*)(void* target, void* context) noexcept;
using CreateOverlayFn = void (*)(void* unit, int overlay, int type, int a4, int a5, int a6, int a7) noexcept;
using RemoveOverlayFn = void (*)(void* unit, int unused, int overlay) noexcept;
using PlayerByIndexFn = void* (*)(std::uint32_t index) noexcept;
using LocalIndexFn    = std::uint32_t (*)() noexcept;
using UnitRoomFn      = void* (*)(void* unit) noexcept;
using LevelOfRoomFn   = int (*)(void* room) noexcept;

const D2RL::PluginContext*    Context {};
const D2RL::ThreadService*    Threads {};
const D2RL::DataTableService* TablesApi {};
D2RL::Lifecycle::ListenerHandle TablesListener {};
std::uintptr_t                Base {};

ApplyHitFn      ApplyHit {};    // original, after the hook
CurseTargetFn   CurseTarget {}; // original, after the hook
CreateOverlayFn CreateOverlay {};
RemoveOverlayFn RemoveOverlay {};
PlayerByIndexFn PlayerByIndex {};
LocalIndexFn    LocalIndex {};
UnitRoomFn      UnitRoom {};
LevelOfRoomFn   LevelOfRoom {};

// Shared between the UI thread (writes) and the server thread (reads).
std::atomic<std::uint32_t> PlayerId { NoId };
std::atomic<int>           ClientLevel { 0 };
std::atomic<bool>          Active {};
std::atomic<std::uint32_t> BlockedHits {}, BlockedCurses {}, LoadingBlocks {};
std::atomic<int>           OverlayId { -1 };

// UI thread only.
struct Window {
	int       level      = 0;
	ULONGLONG since      = 0;
	int       x          = 0, y = 0;
	void*     shownOn    = nullptr; // unit the overlay was put on
	int       shownId    = -1;
} Ui;

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

template <typename T>
bool ReadField(const void* base, std::uintptr_t offset, T& value) noexcept {
	return base && Read(reinterpret_cast<std::uintptr_t>(base) + offset, &value, sizeof(T));
}

bool CodeMatches(std::uint64_t rva, const Code& bytes, std::size_t from = 0) noexcept {
	Code code {};
	return Read(Base + rva, code.data(), code.size()) && std::memcmp(code.data() + from, bytes.data() + from, code.size() - from) == 0;
}

int LevelOf(void* unit) noexcept {
	void* const room = UnitRoom(unit);
	return room ? LevelOfRoom(room) : 0;
}

// ---- server thread ----

bool IsProtected(void* unit, bool& loading) noexcept {
	loading = false;
	std::uint32_t type = NoId, id = NoId;
	if (!ReadField(unit, UnitTypeOffset, type) || type != 0 || !ReadField(unit, UnitIdOffset, id)) return false;
	if (id == NoId || id != PlayerId.load(std::memory_order_acquire)) return false;
	if (Active.load(std::memory_order_acquire)) return true;
	// The server moved the player to another level and the client hasn't
	// shown it yet: the loading screen.
	const int client = ClientLevel.load(std::memory_order_acquire);
	const int server = LevelOf(unit);
	loading          = client > 0 && server > 0 && server != client;
	return loading;
}

void HookApplyHit(void* game, void* attacker, void* defender, void* damage) noexcept {
	bool loading = false;
	if (defender && IsProtected(defender, loading)) {
		BlockedHits.fetch_add(1, std::memory_order_relaxed);
		if (loading) LoadingBlocks.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	ApplyHit(game, attacker, defender, damage);
}

int HookCurseTarget(void* target, void* context) noexcept {
	bool loading = false;
	if (target && IsProtected(target, loading)) {
		BlockedCurses.fetch_add(1, std::memory_order_relaxed);
		if (loading) LoadingBlocks.fetch_add(1, std::memory_order_relaxed);
		return 0;
	}
	return CurseTarget(target, context);
}

// ---- UI thread ----

void* LocalPlayer() noexcept {
	const std::uint32_t index = LocalIndex();
	return index < 8 ? PlayerByIndex(index) : nullptr;
}

bool Position(void* unit, int& x, int& y) noexcept {
	void*         path = nullptr;
	std::uint16_t px = 0, py = 0;
	if (!ReadField(unit, UnitPathOffset, path) || !path || !ReadField(path, 2, px) || !ReadField(path, 6, py)) return false;
	x = px, y = py;
	return true;
}

void ShowIndicator(void* player) noexcept {
	const int overlay = OverlayId.load();
	if (!Config.indicator || overlay < 0 || (Ui.shownOn == player && Ui.shownId == overlay)) return;
	CreateOverlay(player, overlay, OverlayType, 0, 0, 0, 0);
	Ui.shownOn = player;
	Ui.shownId = overlay;
}

void HideIndicator(void* player) noexcept {
	// Only on the unit it was put on, while that unit is still the player.
	if (Ui.shownOn && Ui.shownOn == player && Ui.shownId >= 0) RemoveOverlay(player, 0, Ui.shownId);
	Ui.shownOn = nullptr;
	Ui.shownId = -1;
}

void Start(void* player, int level, ULONGLONG now) noexcept {
	const bool again = Active.exchange(true, std::memory_order_acq_rel);
	if (!again) {
		BlockedHits.store(0);
		BlockedCurses.store(0);
	}
	Ui.since = now;
	(void)Position(player, Ui.x, Ui.y);
	Log(false, "level %d: protected until you move (at most %d s)%s", level, Config.seconds, again ? ", restarted" : "");
}

void End(void* player, ULONGLONG now, const char* why) noexcept {
	if (!Active.exchange(false, std::memory_order_acq_rel)) return;
	HideIndicator(player);
	Log(false, "protection ended after %.1f s (%s): %u hits and %u curses blocked, %u of them while loading", (now - Ui.since) / 1000.0, why, BlockedHits.load(), BlockedCurses.load(),
	    LoadingBlocks.exchange(0));
}

bool Moving(void* player) noexcept {
	std::uint32_t mode = 0;
	if (ReadField(player, UnitModeOffset, mode) && (mode == ModeWalk || mode == ModeRun || mode == ModeTownWalk)) return true;
	int x = 0, y = 0;
	return Position(player, x, y) && (std::abs(x - Ui.x) > MoveTolerance || std::abs(y - Ui.y) > MoveTolerance);
}

std::atomic<bool> TickQueued {};

void __cdecl Tick(const D2RL::PluginContext*, void*) noexcept {
	TickQueued.store(false);
	const ULONGLONG now    = GetTickCount64();
	void* const     player = LocalPlayer();
	std::uint32_t   id     = NoId;
	if (!player || !ReadField(player, UnitIdOffset, id)) {
		if (Active.load()) End(nullptr, now, "left the game");
		Ui.shownOn = nullptr;
		Ui.shownId = -1;
		Ui.level   = 0;
		ClientLevel.store(0);
		PlayerId.store(NoId);
		return;
	}
	if (id != PlayerId.load()) {
		PlayerId.store(id, std::memory_order_release);
		Ui.level = 0;
	}
	const int level = LevelOf(player);
	if (level <= 0) return; // still loading
	ClientLevel.store(level, std::memory_order_release);
	if (level != Ui.level) {
		Ui.level = level;
		Start(player, level, now);
	}
	if (!Active.load()) return;
	if (now - Ui.since < GraceMs) (void)Position(player, Ui.x, Ui.y);
	else if (Moving(player)) {
		End(player, now, "you moved");
		return;
	}
	if (now - Ui.since >= static_cast<ULONGLONG>(Config.seconds) * 1000) {
		End(player, now, "time up");
		return;
	}
	ShowIndicator(player);
}

std::atomic<bool> Running {};

DWORD WINAPI Poll(void*) noexcept {
	while (Running.load()) {
		if (!TickQueued.exchange(true) && Threads->runOnUiThread(Context, Tick, nullptr) != D2RL::Threads::Result::Success) TickQueued.store(false);
		Sleep(50);
	}
	return 0;
}

// ---- overlay row ----

// Row index of the overlay.txt row whose Filename is `filename`, -1 if none.
// The row layout is not documented, so the name is searched for as a whole
// NUL-terminated string anywhere in the row.
std::int32_t FindOverlay(const Tables::TableView& view, const char* filename) noexcept {
	const auto  length = std::strlen(filename);
	const auto* rows   = static_cast<const std::uint8_t*>(view.rows);
	for (std::uint32_t row = 0; row < view.rowCount; ++row) {
		const auto* begin = rows + static_cast<std::size_t>(row) * view.rowSize;
		for (std::uint32_t at = 0; at + length < view.rowSize; ++at) {
			if ((at == 0 || begin[at - 1] == 0) && begin[at + length] == 0 && _strnicmp(reinterpret_cast<const char*>(begin + at), filename, length) == 0)
				return static_cast<std::int32_t>(row);
		}
	}
	return -1;
}

void __cdecl OnTablesLoaded(const D2RL::PluginContext*, const D2RL::Lifecycle::DataTablesLoadedEvent*, void*) noexcept {
	for (const auto bank : { Tables::Bank::Rotw, Tables::Bank::Lod, Tables::Bank::Classic }) {
		Tables::TableView view { .structSize = Tables::TableViewSize };
		if (TablesApi->getTable(Context, bank, Tables::TableId::Overlay, &view) != Tables::Result::Success || !view.rows || !view.rowSize || !view.rowCount) continue;
		const auto overlay = FindOverlay(view, Config.overlay.c_str());
		OverlayId.store(overlay);
		if (overlay < 0) Log(true, "indicator: no overlay.txt row with Filename %s, no indicator", Config.overlay.c_str());
		else Log(false, "indicator: overlay %s = row %d", Config.overlay.c_str(), overlay);
		return;
	}
	Log(true, "indicator: overlay table unavailable, no indicator");
}

// ---- version check ----

bool Locate() noexcept {
	struct Check {
		std::uint64_t rva;
		const Code*   bytes;
		std::size_t   from;
	};
	const std::array<Check, 7> checks { {
		{ ApplyHitRva, &ApplyHitBytes, 0 },
		{ CurseTargetRva, &CurseTargetBytes, 0 },
		{ CreateOverlayRva, &CreateOverlayBytes, 5 },
		{ RemoveOverlayRva, &RemoveOverlayBytes, 0 },
		{ PlayerByIndexRva, &PlayerByIndexBytes, 0 },
		{ UnitRoomRva, &UnitRoomBytes, 0 },
		{ LevelOfRoomRva, &LevelOfRoomBytes, 0 },
	} };
	for (const auto& check : checks) {
		if (!CodeMatches(check.rva, *check.bytes, check.from)) {
			Log(true, "game code differs at D2R+0x%llX", static_cast<unsigned long long>(check.rva));
			return false;
		}
	}
	std::array<std::uint8_t, 7> local {};
	if (!Read(Base + LocalIndexRva, local.data(), local.size()) || local[0] != 0x8B || local[1] != 0x05 || local[6] != 0xC3) {
		Log(true, "game code differs at D2R+0x%llX", static_cast<unsigned long long>(LocalIndexRva));
		return false;
	}
	CreateOverlay = reinterpret_cast<CreateOverlayFn>(Base + CreateOverlayRva);
	RemoveOverlay = reinterpret_cast<RemoveOverlayFn>(Base + RemoveOverlayRva);
	PlayerByIndex = reinterpret_cast<PlayerByIndexFn>(Base + PlayerByIndexRva);
	LocalIndex    = reinterpret_cast<LocalIndexFn>(Base + LocalIndexRva);
	UnitRoom      = reinterpret_cast<UnitRoomFn>(Base + UnitRoomRva);
	LevelOfRoom   = reinterpret_cast<LevelOfRoomFn>(Base + LevelOfRoomRva);
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
		else if (key == "duration_seconds") {
			const int seconds = std::atoi(value.c_str());
			if (seconds > 0 && seconds <= 3600) Config.seconds = seconds;
			else Log(true, "config: duration_seconds %s ignored (1-3600)", value.c_str());
		} else if (key == "indicator") Config.indicator = value != "false";
		else if (key == "indicator_overlay") {
			if (!value.empty()) Config.overlay = value;
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
		Log(false, "Transition Immunity %s: disabled in config", PluginVersion);
		return true;
	}
	Base = context->exeBase;
	if (!Base || !Locate()) {
		Log(true, "Transition Immunity %s: inactive, this game build isn't supported (Steam 3.3.93787 / 3.3.94995 are)", PluginVersion);
		return true;
	}
	Threads = Query<D2RL::ThreadService>("thread");
	if (!D2RL::HasThreadServiceField(Threads, D2RL::ThreadServiceRequiredSize)) {
		Log(true, "Transition Immunity %s: inactive, this D2RLoader lacks the thread service", PluginVersion);
		return true;
	}
	if (!context->InstallInlineHook(ApplyHitRva, ApplyHitBytes.data(), static_cast<std::uint32_t>(ApplyHitBytes.size()), &HookApplyHit, &ApplyHit) || !ApplyHit) {
		Log(true, "Transition Immunity %s: inactive, could not hook D2R+0x%llX (another plugin may hook it)", PluginVersion, static_cast<unsigned long long>(ApplyHitRva));
		return true;
	}
	if (!context->InstallInlineHook(CurseTargetRva, CurseTargetBytes.data(), static_cast<std::uint32_t>(CurseTargetBytes.size()), &HookCurseTarget, &CurseTarget) || !CurseTarget) {
		CurseTarget = nullptr;
		Log(true, "could not hook D2R+0x%llX: curses are not blocked", static_cast<unsigned long long>(CurseTargetRva));
	}
	if (Config.indicator) {
		TablesApi            = Query<D2RL::DataTableService>("data table");
		const auto* lifecycle = Query<D2RL::LifecycleService>("lifecycle");
		const D2RL::Lifecycle::DataTablesLoadedListener listener {
			.structSize = D2RL::Lifecycle::DataTablesLoadedListenerSize,
			.callback   = OnTablesLoaded,
		};
		if (!D2RL::HasDataTableServiceField(TablesApi, D2RL::DataTableServiceRequiredSize) || !D2RL::HasLifecycleServiceField(lifecycle, D2RL::LifecycleServiceRequiredSize) ||
		    lifecycle->registerDataTablesLoadedListener(context, &listener, &TablesListener) != D2RL::Lifecycle::Result::Success)
			Log(true, "indicator: data tables unavailable, no indicator");
	}
	Running.store(true);
	if (HANDLE poller = CreateThread(nullptr, 0, Poll, nullptr, 0, nullptr)) CloseHandle(poller);
	else {
		Running.store(false);
		Log(true, "Transition Immunity %s: inactive, could not start its timer", PluginVersion);
		return true;
	}
	Log(false, "Transition Immunity %s: active, %d s after each loading screen or until you move%s", PluginVersion, Config.seconds, CurseTarget ? "" : " (hits only)");
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	Running.store(false);
}
