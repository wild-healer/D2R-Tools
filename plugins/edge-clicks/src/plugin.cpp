// Edge Clicks: clicking past the edge of the map moves you toward the click
// instead of doing nothing.
//
// Why the game ignores those clicks (Steam 3.3.94995 / 93787 RVAs):
// 1. A click on the black void never becomes a walk. The mouse handlers
//    (0x839E0 for a left click) turn the cursor into a ground spot with
//    0x8F3D0, which in the 3D graphics casts a ray onto the level's ground
//    (0x7E7CE0). With no ground under the cursor it fails and the click is
//    dropped. The plugin learns the screen-to-ground mapping around you from
//    the casts that hit and answers a missed cast from a mouse handler with
//    it, as the classic graphics do.
// 2. Walking (path type 7, 0x3ABC40) heads straight for the click (0x3AACF0),
//    and that straight path can end past a wall, where you then stop; the real
//    path search around obstacles (A*, 0x3F6930) only runs within 18 steps.
//    A target outside every room (0x38DFB0 finds none) fails outright.
//    The plugin hooks the path finder 0x340E70 (Path*, Unit*, int, int),
//    shared by the client and the server, for a player walking to a spot (not
//    to a unit). When the game's path doesn't reach the click, or one of its
//    straight legs crosses a wall, it floods the free ground (room present,
//    0x364C70 collision check clear) reachable within 17 steps and walks to
//    the reached spot nearest the click: along the wall, or around it.
//    Path: +0x02 / +0x06 position (u16), +0x10 / +0x12 target, +0x20 Room*,
//    +0x34 point count, +0xC4 points, +0x48 flags, +0x50 type,
//    +0x5C / +0x64 collision pattern / mask, +0x70 target unit.
// Monsters and every click the game already handles are left alone.
// The code bytes of every function are checked at load; on a mismatch the
// plugin stays inactive.

#include <D2RLPlugin/api.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr const char* PluginId      = "edge-clicks";
constexpr const char* PluginVersion = "1.0.0";

const D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = PluginId,
	.name        = "Edge Clicks",
	.version     = PluginVersion,
	.author      = "wild-healer",
	.description = "Clicking past the edge of the map moves you toward the click.",
	.flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

struct Settings {
	bool enabled  = true;
	bool logMoves = true;
} Config;

// ---- native layout (Steam 3.3.93787 / 94995) ----

using Code = std::array<std::uint8_t, 16>;

constexpr std::uint64_t FindPathRva = 0x340E70; // int (Path* path, Unit* unit, int, int)
constexpr Code FindPathBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89, 0x7C, 0x24, 0x18, 0x55 };
constexpr std::uint64_t RoomAtRva = 0x38DFB0; // Room* (Room* room, int x, int y): room or a neighbour holding (x, y)
constexpr Code RoomAtBytes { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x55, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41, 0x8B, 0xF0, 0x8B };
constexpr std::uint64_t BlockedRva = 0x364C70; // int (Room*, int x, int y, int pattern, int mask): nonzero when the spot is blocked
constexpr Code BlockedBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57 };
constexpr std::uint64_t UnitTypeRva = 0x34B9D0; // int (Unit*), 0 = player
constexpr Code UnitTypeBytes { 0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1D, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C };

constexpr std::size_t   PathPosX       = 0x02;
constexpr std::size_t   PathPosY       = 0x06;
constexpr std::size_t   PathTargetX    = 0x10;
constexpr std::size_t   PathTargetY    = 0x12;
constexpr std::size_t   PathRoom       = 0x20;
constexpr std::size_t   PathFlags      = 0x48;
constexpr std::size_t   PathType       = 0x50;
constexpr std::size_t   PathTargetUnit = 0x70;    // set when walking to a unit
constexpr std::size_t   PathIndex      = 0x30;    // current point
constexpr std::size_t   PathCount      = 0x34;    // number of points
constexpr std::size_t   PathPoints     = 0xC4;    // u16 x, u16 y each; point i at +0xC4 + 4 * i, the end at i = count
constexpr int           MaxPoints      = 78;      // room for the points up to the next field (checked loosely)
constexpr std::uint32_t FlagOtherPath  = 0x40000; // the path finder takes another branch, no room lookup
constexpr std::uint32_t TypeNoRoom     = 0x11;    // a path type that accepts a target without a room
constexpr int           UnitTypePlayer = 0;
constexpr std::size_t   PathPattern    = 0x5C;    // collision pattern of the walker
constexpr std::size_t   PathMask       = 0x64;    // collision mask of the walker
constexpr int           AStarRange     = 324;     // the walk path searches around walls only within 18 steps (squared)
constexpr int           GridRadius     = 17;      // the flood of reachable ground stays within 17 steps
constexpr int           GridSize       = GridRadius * 2 + 1;
constexpr int           MaxRuns        = 6;       // extra path finder runs at most per click

using FindPathFn = int (*)(std::uint8_t* path, void* unit, int a3, int a4) noexcept;
using RoomAtFn   = void* (*)(void* room, int x, int y) noexcept;
using UnitTypeFn = int (*)(void* unit) noexcept;
using BlockedFn  = int (*)(void* room, int x, int y, int pattern, int mask) noexcept;

std::uintptr_t Base {};
FindPathFn     FindPath {}; // original, after the hook
RoomAtFn       RoomAt {};
UnitTypeFn     UnitType {};
BlockedFn      Blocked {};

const D2RL::PluginContext* Context {};
DWORD                      LastLog {};


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

bool CodeMatches(std::uint64_t rva, const Code& bytes) noexcept {
	Code code {};
	return Read(Base + rva, code.data(), code.size()) && code == bytes;
}

template <typename T>
T& Field(std::uint8_t* path, std::size_t offset) noexcept {
	return *reinterpret_cast<T*>(path + offset);
}

// ---- client and server threads ----

bool IsBlocked(void* room, int x, int y, int pattern, int mask) noexcept {
	return Blocked(room, x, y, pattern, mask) != 0;
}

void LogMove(const char* format, ...) noexcept {
	if (!Config.logMoves || !Context) return;
	const DWORD now = GetTickCount();
	if (now - LastLog < 250) return;
	LastLog = now;
	char    line[512] {};
	va_list args;
	va_start(args, format);
	std::vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	Context->LogInfo(line);
}

void SetTarget(std::uint8_t* path, int x, int y) noexcept {
	Field<std::uint16_t>(path, PathTargetX) = static_cast<std::uint16_t>(x);
	Field<std::uint16_t>(path, PathTargetY) = static_cast<std::uint16_t>(y);
}

// A player walking to a spot on the ground (not to a unit), on the path
// finder branch that looks up the target's room.
bool IsPlayerWalk(std::uint8_t* path, void* unit) noexcept {
	if (!unit || Field<void*>(path, PathTargetUnit)) return false;
	if (Field<std::uint32_t>(path, PathFlags) & FlagOtherPath) return false;
	if (Field<std::uint32_t>(path, PathType) == TypeNoRoom) return false;
	return Field<void*>(path, PathRoom) && UnitType(unit) == UnitTypePlayer;
}

int FixPath(std::uint8_t* path, void* unit, int a3, int a4) noexcept {
	void* const room = Field<void*>(path, PathRoom);
	const int   px   = Field<std::uint16_t>(path, PathPosX);
	const int   py   = Field<std::uint16_t>(path, PathPosY);
	const int   tx   = Field<std::uint16_t>(path, PathTargetX);
	const int   ty   = Field<std::uint16_t>(path, PathTargetY);
	const int   dx   = tx - px;
	const int   dy   = ty - py;
	if ((px == 0 && py == 0) || (tx == 0 && ty == 0) || std::max(std::abs(dx), std::abs(dy)) < 2) return FindPath(path, unit, a3, a4);

	// The ground around you, looked up lazily: free when it has a room and the
	// collision check is clear for your size. The spots right next to you are
	// taken as free, since your own body blocks them in the collision map.
	const int pattern = static_cast<int>(Field<std::uint32_t>(path, PathPattern));
	const int mask    = static_cast<int>(Field<std::uint32_t>(path, PathMask));
	std::array<std::int8_t, GridSize * GridSize> cell {}; // 0 not looked at, 1 free, 2 blocked or off the map
	auto isFree = [&](int gx, int gy) noexcept {
		if (gx < 0 || gy < 0 || gx >= GridSize || gy >= GridSize) return false;
		std::int8_t& state = cell[gy * GridSize + gx];
		if (state == 0) {
			const int ox = gx - GridRadius, oy = gy - GridRadius;
			const int x = px + ox, y = py + oy;
			const bool beside = std::abs(ox) <= 1 && std::abs(oy) <= 1;
			state = ox * ox + oy * oy <= AStarRange && RoomAt(room, x, y) && (beside || !IsBlocked(room, x, y, pattern, mask)) ? 1 : 2;
		}
		return state == 1;
	};
	// A straight walk between two spots crosses no wall (checked inside the
	// mapped area only).
	auto clearLine = [&](int x0, int y0, int x1, int y1) noexcept {
		int       x = x0 - px + GridRadius, y = y0 - py + GridRadius;
		const int ex = x1 - px + GridRadius, ey = y1 - py + GridRadius;
		const int sx = ex > x ? 1 : -1, sy = ey > y ? 1 : -1;
		const int ax = std::abs(ex - x), ay = std::abs(ey - y);
		int       error = ax - ay;
		while (x != ex || y != ey) {
			const int twice = error * 2;
			int       nx = x, ny = y;
			if (twice > -ay) { error -= ay; nx += sx; }
			if (twice < ax) { error += ax; ny += sy; }
			const bool inside = nx >= 0 && ny >= 0 && nx < GridSize && ny < GridSize;
			if (!inside) return true;
			if (!isFree(nx, ny)) return false;
			if (nx != x && ny != y && !isFree(nx, y) && !isFree(x, ny)) return false; // squeezing between two walls
			x = nx;
			y = ny;
		}
		return true;
	};

	auto pathEnd = [&](int& ex, int& ey) noexcept {
		const int count = static_cast<int>(Field<std::uint32_t>(path, PathCount));
		if (count <= 0 || count >= MaxPoints) return false;
		ex = Field<std::uint16_t>(path, PathPoints + count * 4);
		ey = Field<std::uint16_t>(path, PathPoints + count * 4 + 2);
		return true;
	};
	// The path ends at the spot and none of its straight legs crosses a wall:
	// the straight walk part of the path finder happily ends past a wall, and
	// you then stop at the wall.
	auto reaches = [&](int result, int x, int y) noexcept {
		int ex = 0, ey = 0;
		if (result <= 0 || !pathEnd(ex, ey) || std::max(std::abs(ex - x), std::abs(ey - y)) > 1) return false;
		const int count = static_cast<int>(Field<std::uint32_t>(path, PathCount));
		int       lx = px, ly = py;
		for (int i = 1; i <= count; ++i) {
			const int nx = Field<std::uint16_t>(path, PathPoints + i * 4);
			const int ny = Field<std::uint16_t>(path, PathPoints + i * 4 + 2);
			if (!clearLine(lx, ly, nx, ny)) return false;
			lx = nx;
			ly = ny;
		}
		return true;
	};

	// The normal case: the game reaches the click by itself.
	int result = RoomAt(room, tx, ty) ? FindPath(path, unit, a3, a4) : 0;
	if (reaches(result, tx, ty)) return result;

	// Walking (path type 7) first heads straight for the click; only a click
	// within 18 steps also gets the real path search around obstacles. A click
	// farther away, behind a wall, or on a spot nobody can stand on (a wall,
	// the void) leaves you stuck at the wall. Flood the free ground you can
	// reach within 17 steps, and walk to the reached spot nearest the click
	// (sliding along a wall, or around it).
	std::array<std::int16_t, GridSize * GridSize> steps {};
	std::array<std::int16_t, GridSize * GridSize> queue {};
	steps.fill(-1);
	const int center = GridRadius * GridSize + GridRadius;
	int       head = 0, tail = 0;
	steps[center]  = 0;
	queue[tail++]  = static_cast<std::int16_t>(center);
	while (head < tail) {
		const int current = queue[head++];
		const int cx = current % GridSize, cy = current / GridSize;
		for (int oy = -1; oy <= 1; ++oy) {
			for (int ox = -1; ox <= 1; ++ox) {
				const int nx = cx + ox, ny = cy + oy;
				if ((ox == 0 && oy == 0) || !isFree(nx, ny)) continue;
				const int next = ny * GridSize + nx;
				if (steps[next] >= 0) continue;
				if (ox != 0 && oy != 0 && (!isFree(cx + ox, cy) || !isFree(cx, cy + oy))) continue; // no cutting wall corners
				steps[next]   = static_cast<std::int16_t>(steps[current] + 1);
				queue[tail++] = static_cast<std::int16_t>(next);
			}
		}
	}

	// Reached spots that bring you at least 2 steps nearer the click, nearest first.
	auto spotX = [&](int index) noexcept { return px + index % GridSize - GridRadius; };
	auto spotY = [&](int index) noexcept { return py + index / GridSize - GridRadius; };
	auto clickDistance = [&](int index) noexcept {
		const long long x = spotX(index) - tx, y = spotY(index) - ty;
		return x * x + y * y;
	};
	const double here = std::sqrt(static_cast<double>(clickDistance(center)));
	std::vector<int> spots;
	for (int i = 1; i < tail; ++i) {
		if (std::sqrt(static_cast<double>(clickDistance(queue[i]))) <= here - 2.0) spots.push_back(queue[i]);
	}
	std::sort(spots.begin(), spots.end(), [&](int a, int b) {
		const long long da = clickDistance(a), db = clickDistance(b);
		return da != db ? da < db : steps[a] < steps[b];
	});

	// The nearest spots first, whatever the path finder makes of them; then
	// the nearest one in a straight clear line, which the straight walk does
	// reach.
	int runs = 0, chosen = -1;
	for (const int spot : spots) {
		if (runs >= MaxRuns) break;
		++runs;
		SetTarget(path, spotX(spot), spotY(spot));
		result = FindPath(path, unit, a3, a4);
		if (reaches(result, spotX(spot), spotY(spot))) {
			chosen = spot;
			break;
		}
	}
	if (chosen < 0) {
		for (const int spot : spots) {
			if (!clearLine(px, py, spotX(spot), spotY(spot))) continue;
			++runs;
			SetTarget(path, spotX(spot), spotY(spot));
			result = FindPath(path, unit, a3, a4);
			if (reaches(result, spotX(spot), spotY(spot))) chosen = spot;
			break;
		}
	}

	if (chosen >= 0) {
		LogMove("click at (%d, %d) from (%d, %d): walking to (%d, %d) instead%s", tx, ty, px, py, spotX(chosen), spotY(chosen),
		        RoomAt(room, tx, ty) ? "" : " (the click is off the map)");
		return result;
	}

	// Nothing better: let the game do what it would have done.
	LogMove("click at (%d, %d) from (%d, %d): no reachable spot nearer to it (%d reached, %d tried)", tx, ty, px, py, tail - 1, runs);
	SetTarget(path, tx, ty);
	return FindPath(path, unit, a3, a4);
}

// ---- clicks on the black void ----
//
// The mouse handlers (0x839E0 for a left click, and the hold / other button
// handlers) turn the cursor into a ground spot with 0x8F3D0 (int* x, int* y)
// -> bool. In the 3D graphics it casts a ray from the camera onto the level's
// ground (0x7E7CE0); on the black void there is no ground to hit, it returns
// false and the click is dropped before it becomes a walk. The plugin learns
// how screen positions map to ground spots around you from the casts that do
// hit, and answers a missed cast from a mouse handler with that mapping, the
// way the classic graphics do.

constexpr std::uint64_t ScreenToWorldRva = 0x8F3D0; // bool (int* x, int* y): screen position in, ground spot out
constexpr Code ScreenToWorldBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x50, 0x48, 0x8B, 0xDA, 0x48, 0x8B, 0xF9 };
constexpr std::uint64_t LocalPlayerIndexRva = 0x8B2D0; // int ()
constexpr Code LocalPlayerIndexBytes { 0x8B, 0x05, 0x2E, 0x84, 0x99, 0x02, 0xC3, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC };
constexpr std::uint64_t PlayerUnitRva = 0x9A480; // Unit* (int index)
constexpr Code PlayerUnitBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x83, 0xF9, 0x08, 0x0F, 0x83, 0x85 };
constexpr std::uint64_t UnitPositionPathRva = 0x34AE80; // Path* (Unit*)
constexpr Code UnitPositionPathBytes { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C };
// The mouse handlers whose ground spot becomes a walk or a skill (return addresses).
constexpr std::array<std::uint64_t, 9> MouseCallers { 0x83AB1, 0x83BD2, 0x84091, 0x8AC82, 0x8AD7E, 0x8B9FF, 0x8BA8C, 0x8BBB7, 0x94271 };
constexpr int SampleCount = 64;

using ScreenToWorldFn    = bool (*)(int* x, int* y) noexcept;
using LocalPlayerIndexFn = int (*)() noexcept;
using PlayerUnitFn       = void* (*)(int index) noexcept;
using UnitPositionPathFn = std::uint8_t* (*)(void* unit) noexcept;
ScreenToWorldFn    ScreenToWorld {};
LocalPlayerIndexFn LocalPlayerIndex {};
PlayerUnitFn       PlayerUnit {};
UnitPositionPathFn UnitPositionPath {};

struct Sample {
	double sx, sy, wx, wy; // screen position, ground spot relative to you
};
std::array<Sample, SampleCount> Samples {};
int                             SampleNext {}, SampleTotal {};

bool PlayerPosition(int& x, int& y) noexcept {
	void* const player = PlayerUnit(LocalPlayerIndex());
	if (!player) return false;
	std::uint8_t* const path = UnitPositionPath(player);
	if (!path) return false;
	x = Field<std::uint16_t>(path, PathPosX);
	y = Field<std::uint16_t>(path, PathPosY);
	return x != 0 || y != 0;
}

// Least squares fit of ground = a * screen x + b * screen y + c, for each axis.
bool Fit(double (&gx)[3], double (&gy)[3]) noexcept {
	const int count = std::min(SampleTotal, SampleCount);
	if (count < 8) return false;
	double m[3][3] {}, vx[3] {}, vy[3] {};
	for (int i = 0; i < count; ++i) {
		const Sample& s = Samples[i];
		const double  row[3] { s.sx, s.sy, 1.0 };
		for (int r = 0; r < 3; ++r) {
			for (int c = 0; c < 3; ++c) m[r][c] += row[r] * row[c];
			vx[r] += row[r] * s.wx;
			vy[r] += row[r] * s.wy;
		}
	}
	const double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
	                   m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
	if (std::abs(det) < 1e-6 * std::pow(m[0][0] + m[1][1] + 1.0, 2) * count) return false; // the samples don't spread out enough
	auto solve = [&](const double (&v)[3], double (&out)[3]) {
		for (int k = 0; k < 3; ++k) {
			double a[3][3];
			for (int r = 0; r < 3; ++r)
				for (int c = 0; c < 3; ++c) a[r][c] = c == k ? v[r] : m[r][c];
			out[k] = (a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
			          a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0])) / det;
		}
	};
	solve(vx, gx);
	solve(vy, gy);
	return true;
}

bool HookScreenToWorld(int* x, int* y) noexcept {
	const std::uintptr_t caller = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
	const int            sx = x ? *x : 0, sy = y ? *y : 0;
	const bool           hit = ScreenToWorld(x, y);
	if (!x || !y) return hit;
	int px = 0, py = 0;
	if (hit) {
		// Learn from the casts that hit the ground (the cursor moves over the
		// level all the time), skipping a screen spot that repeats the last one.
		if (!PlayerPosition(px, py)) return hit;
		const Sample& last = Samples[(SampleNext + SampleCount - 1) % SampleCount];
		if (SampleTotal > 0 && std::abs(last.sx - sx) < 4 && std::abs(last.sy - sy) < 4) return hit;
		Samples[SampleNext] = { static_cast<double>(sx), static_cast<double>(sy), static_cast<double>(*x - px), static_cast<double>(*y - py) };
		SampleNext          = (SampleNext + 1) % SampleCount;
		++SampleTotal;
		return hit;
	}
	bool mouse = false;
	for (const std::uint64_t rva : MouseCallers) mouse = mouse || caller == Base + rva;
	if (!mouse || !PlayerPosition(px, py)) return hit;
	double gx[3], gy[3];
	if (!Fit(gx, gy)) {
		LogMove("click on the void at screen (%d, %d): not enough ground seen yet to place it (%d samples)", sx, sy, SampleTotal);
		return hit;
	}
	*x = px + static_cast<int>(std::lround(gx[0] * sx + gx[1] * sy + gx[2]));
	*y = py + static_cast<int>(std::lround(gy[0] * sx + gy[1] * sy + gy[2]));
	LogMove("click on the void at screen (%d, %d): ground spot (%d, %d) from (%d, %d)", sx, sy, *x, *y, px, py);
	return true;
}

int HookFindPath(std::uint8_t* path, void* unit, int a3, int a4) noexcept {
	return path && IsPlayerWalk(path, unit) ? FixPath(path, unit, a3, a4) : FindPath(path, unit, a3, a4);
}

bool Locate() noexcept {
	const std::array<std::pair<std::uint64_t, const Code*>, 4> checks { {
		{ FindPathRva, &FindPathBytes },
		{ RoomAtRva, &RoomAtBytes },
		{ UnitTypeRva, &UnitTypeBytes },
		{ BlockedRva, &BlockedBytes },
	} };
	for (const auto& [rva, bytes] : checks) {
		if (!CodeMatches(rva, *bytes)) {
			Log(true, "game code differs at D2R+0x%llX", static_cast<unsigned long long>(rva));
			return false;
		}
	}
	RoomAt   = reinterpret_cast<RoomAtFn>(Base + RoomAtRva);
	UnitType = reinterpret_cast<UnitTypeFn>(Base + UnitTypeRva);
	Blocked  = reinterpret_cast<BlockedFn>(Base + BlockedRva);
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
		else if (key == "log_moves") Config.logMoves = value != "false";
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
		Log(false, "Edge Clicks %s: disabled in config", PluginVersion);
		return true;
	}
	Base = context->exeBase;
	if (!Base || !Locate()) {
		Log(true, "Edge Clicks %s: inactive, this game build isn't supported (Steam 3.3.93787 / 3.3.94995 are)", PluginVersion);
		return true;
	}
	if (!context->InstallInlineHook(FindPathRva, FindPathBytes.data(), static_cast<std::uint32_t>(FindPathBytes.size()), &HookFindPath, &FindPath) || !FindPath) {
		Log(true, "Edge Clicks %s: inactive, could not hook D2R+0x%llX", PluginVersion, static_cast<unsigned long long>(FindPathRva));
		return true;
	}
	if (CodeMatches(ScreenToWorldRva, ScreenToWorldBytes) && CodeMatches(LocalPlayerIndexRva, LocalPlayerIndexBytes) && CodeMatches(PlayerUnitRva, PlayerUnitBytes) &&
	    CodeMatches(UnitPositionPathRva, UnitPositionPathBytes)) {
		LocalPlayerIndex = reinterpret_cast<LocalPlayerIndexFn>(Base + LocalPlayerIndexRva);
		PlayerUnit       = reinterpret_cast<PlayerUnitFn>(Base + PlayerUnitRva);
		UnitPositionPath = reinterpret_cast<UnitPositionPathFn>(Base + UnitPositionPathRva);
		if (!context->InstallInlineHook(ScreenToWorldRva, ScreenToWorldBytes.data(), static_cast<std::uint32_t>(ScreenToWorldBytes.size()), &HookScreenToWorld, &ScreenToWorld) || !ScreenToWorld)
			Log(true, "could not hook D2R+0x%llX: clicks on the black void stay ignored", static_cast<unsigned long long>(ScreenToWorldRva));
	} else {
		Log(true, "game code differs at D2R+0x%llX: clicks on the black void stay ignored", static_cast<unsigned long long>(ScreenToWorldRva));
	}
	Log(false, "Edge Clicks %s: active, clicks past the map edge walk toward the click", PluginVersion);
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {}
