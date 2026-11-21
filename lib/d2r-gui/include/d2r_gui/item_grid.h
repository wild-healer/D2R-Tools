// D2R GUI: the plugin's item grid (InventoryGridWidget) drawn its way.
//
// The game's grid vtable (D2R 3.3.94995 / 3.4.95041, checked before use) is
// wrapped once, for the plugin's grids only (marked by their
// chronicleFlourishOffset, read at +0x634): the cell rect (vt31) and the
// cell under the mouse (vt19) move a tile's items by half cells (a 1-wide
// item centred in a 2-wide slot), and cells past the grid's own can be
// measured (TileRect, PeekAt: a picture tile's tooltip). The draw (vt3) can be
// watched and skipped, and while the plugin's grid draws, the game's
// requirement check (and Item Quality Backgrounds 1.2.42's) say every item can
// be used: no cannot-wear colour on the plugin's items. Wrapped on top of
// whatever is there (Inventory Slots wraps the same entries).
#pragma once
#include "core.h"
#include <atomic>
#include <climits>
#include <cstdint>
#include <cstring>
#include <functional>
#include <utility>
#include <vector>

namespace D2RGui::TileGrid {

// The marker a layout gives the plugin's grid: "chronicleFlourishOffset": {"x": 7100, "y": 7100}.
inline constexpr float       Marker       = 7100.0f;
inline constexpr const char* MarkerFields = R"(, "chronicleFlourishOffset": {"x": 7100, "y": 7100})";

// The cells a tile covers, and the half cells its items move right and down.
struct Shift {
	int x {}, y {}, width {}, height {}, hx {}, hy {};
};

struct Options {
	std::function<bool(const void* widget)>      mine;   // the plugin's grid (Marked(widget, Marker, Marker) for its own)
	std::function<std::pair<int, int>()>         origin; // the grid cell of the plugin's cell 0, 0 (a shared page's area)
	std::function<bool(void* widget, bool mine)> draw;   // before a grid draws: false skips its draw
	std::function<void(void* widget, bool mine)> drawn;  // after
	bool                                         clearRequirements = true;
	std::function<void(const char* what)>        problem; // a part that could not be set up (the log, a settings page)
};

namespace Detail {
using CellRectFn    = std::int32_t* (*)(void* widget, std::int32_t* out, std::uint64_t size, std::uint64_t cell) noexcept;
using CellAtPointFn = std::uint64_t* (*)(void* widget, std::uint64_t* out, std::uint64_t point) noexcept;
using DrawFn        = std::uint64_t (*)(void*, void*, void*, void*) noexcept;
using RequirementFn = std::int32_t (*)(void*, void*, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t) noexcept;
using IqbCannotFn   = bool (*)(void* state, void* item) noexcept;
inline constexpr int VtDraw = 3, VtCellAtPoint = 19, VtCellRect = 31;

struct State {
	Options                    options;
	SRWLOCK                    lock = SRWLOCK_INIT;
	std::vector<Shift>         shifts;
	CellRectFn                 cellRect {};
	CellAtPointFn              cellAtPoint {};
	DrawFn                     draw {};
	void**                     vtable {};
	RequirementFn              requirement {};
	IqbCannotFn                iqbCannot {};
	std::uint8_t*              iqbCallSite {};
	void*                      iqbStub {};
	bool                       inDraw {};
	bool                       tried {};
	std::atomic<std::uint64_t> hover { ~std::uint64_t { 0 } };
};
inline State& Get() {
	static State state;
	return state;
}
inline thread_local bool          peekOn {};
inline thread_local std::uint64_t peekCell {};

inline bool Mine(const void* widget) { return widget && Get().options.mine && Get().options.mine(widget); }
inline std::pair<int, int> Origin() { return Get().options.origin ? Get().options.origin() : std::pair<int, int> {}; }

// The shift (in half cells) of the tile holding cell `cell`; false for none.
inline bool ShiftOf(std::uint64_t cell, int& hx, int& hy) {
	if (cell == ~std::uint64_t { 0 }) return false;
	auto&      s      = Get();
	const auto [ox, oy] = Origin();
	const int  x = static_cast<std::int32_t>(cell) - ox, y = static_cast<std::int32_t>(cell >> 32) - oy;
	bool       found = false;
	AcquireSRWLockShared(&s.lock);
	for (const auto& tile : s.shifts)
		if (y >= tile.y && y < tile.y + tile.height && x >= tile.x && x < tile.x + tile.width) hx = tile.hx, hy = tile.hy, found = true;
	ReleaseSRWLockShared(&s.lock);
	return found;
}

// Half a cell across and down, in pixels.
inline void HalfCell(void* widget, int& across, int& down) {
	std::int32_t first[4] {}, right[4] {}, below[4] {};
	Get().cellRect(widget, first, 1 | (std::uint64_t { 1 } << 32), 0);
	Get().cellRect(widget, right, 1 | (std::uint64_t { 1 } << 32), 1);
	Get().cellRect(widget, below, 1 | (std::uint64_t { 1 } << 32), std::uint64_t { 1 } << 32);
	across = (right[0] - first[0]) / 2;
	down   = (below[1] - first[1]) / 2;
}
} // namespace Detail

// The screen rect (x, y, w, h) of `size` cells (width low 32 bits, height
// high) from the plugin's cell (x, y), also past the grid's own cells:
// measured on its first cells and carried on, as the game would lay out a
// bigger grid. Only while wrapped.
inline void TileRect(void* widget, std::int32_t* out, std::uint64_t size, int x, int y) {
	const auto [ox, oy] = Detail::Origin();
	const auto   cellAt = [](int cx, int cy) { return static_cast<std::uint32_t>(cx) | (std::uint64_t { static_cast<std::uint32_t>(cy) } << 32); };
	std::int32_t right[4] {}, below[4] {};
	Detail::Get().cellRect(widget, out, size, cellAt(ox, oy));
	Detail::Get().cellRect(widget, right, size, cellAt(ox + 1, oy));
	Detail::Get().cellRect(widget, below, size, cellAt(ox, oy + 1));
	out[0] += x * (right[0] - out[0]);
	out[1] += y * (below[1] - out[1]);
}

namespace Detail {
inline std::int32_t* HookCellRect(void* widget, std::int32_t* out, std::uint64_t size, std::uint64_t cell) noexcept {
	if (peekOn && Mine(widget)) return TileRect(widget, out, size, static_cast<std::int32_t>(peekCell), static_cast<std::int32_t>(peekCell >> 32)), out;
	Get().cellRect(widget, out, size, cell);
	int hx = 0, hy = 0;
	if (Mine(widget) && ShiftOf(cell, hx, hy)) {
		int across = 0, down = 0;
		HalfCell(widget, across, down);
		out[0] += hx * across;
		out[1] += hy * down;
	}
	return out;
}

inline std::uint64_t* CellAtPointShifted(void* widget, std::uint64_t* out, std::uint64_t point) noexcept {
	auto& s      = Get();
	int   across = 0, down = 0;
	HalfCell(widget, across, down);
	const auto back = [&](int hx, int hy) {
		const auto x = static_cast<std::uint32_t>(static_cast<std::int32_t>(point) - hx * across);
		const auto y = static_cast<std::uint32_t>(static_cast<std::int32_t>(point >> 32) - hy * down);
		return x | (std::uint64_t { y } << 32);
	};
	s.cellAtPoint(widget, out, point);
	// Over a shifted tile's cells: the cell its shifted items show there.
	int hx = 0, hy = 0;
	if (ShiftOf(*out, hx, hy)) return s.cellAtPoint(widget, out, back(hx, hy));
	// Over a shifted item's part that sticks out of its tile's cells.
	std::vector<Shift> tiles;
	AcquireSRWLockShared(&s.lock);
	tiles = s.shifts;
	ReleaseSRWLockShared(&s.lock);
	const auto [ox, oy] = Origin();
	const auto plain    = *out;
	for (const auto& tile : tiles) {
		std::uint64_t cell = ~std::uint64_t { 0 };
		s.cellAtPoint(widget, &cell, back(tile.hx, tile.hy));
		const int x = static_cast<std::int32_t>(cell) - ox, y = static_cast<std::int32_t>(cell >> 32) - oy;
		if (cell != ~std::uint64_t { 0 } && y >= tile.y && y < tile.y + tile.height && x >= tile.x && x < tile.x + tile.width) return *out = cell, out;
	}
	*out = plain;
	return out;
}

inline std::uint64_t* HookCellAtPoint(void* widget, std::uint64_t* out, std::uint64_t point) noexcept {
	if (!Mine(widget)) return Get().cellAtPoint(widget, out, point);
	CellAtPointShifted(widget, out, point);
	Get().hover = *out;
	return out;
}

inline std::uint64_t HookDraw(void* widget, void* a, void* b, void* c) noexcept {
	auto&      s    = Get();
	const bool mine = Mine(widget);
	if (widget && s.options.draw && !s.options.draw(widget, mine)) return 0;
	const bool before = s.inDraw;
	s.inDraw          = mine;
	const auto result = s.draw(widget, a, b, c);
	s.inDraw          = before;
	if (widget && s.options.drawn) s.options.drawn(widget, mine);
	return result;
}

inline std::int32_t HookRequirement(void* item, void* player, std::uint64_t a, std::uint64_t b, std::uint64_t c, std::uint64_t d, std::uint64_t e) noexcept {
	return Get().inDraw ? 1 : Get().requirement(item, player, a, b, c, d, e);
}
inline bool HookIqbCannot(void* state, void* item) noexcept { return Get().inDraw ? false : Get().iqbCannot(state, item); }

// Points the rel32 call at `site` to `target`.
inline bool PointCall(std::uint8_t* site, const void* target) {
	const auto rel = reinterpret_cast<std::intptr_t>(target) - reinterpret_cast<std::intptr_t>(site + 5);
	if (rel < INT32_MIN || rel > INT32_MAX) return false;
	DWORD keep = 0;
	if (!VirtualProtect(site + 1, 4, PAGE_EXECUTE_READWRITE, &keep)) return false;
	const auto value = static_cast<std::int32_t>(rel);
	std::memcpy(site + 1, &value, 4);
	VirtualProtect(site + 1, 4, keep, &keep);
	FlushInstructionCache(GetCurrentProcess(), site, 5);
	return true;
}

inline bool SwapAt(std::uint64_t* slot, std::uint64_t from, std::uint64_t to) {
	DWORD keep = 0;
	if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &keep)) return false;
	const bool swapped = InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(slot), static_cast<LONG64>(to), static_cast<LONG64>(from)) == static_cast<LONG64>(from);
	VirtualProtect(slot, sizeof(*slot), keep, &keep);
	return swapped;
}

inline void Problem(const char* what) {
	if (Get().options.problem) Get().options.problem(what);
	else Log(false, "d2r-gui: %s", what);
}

// The game's requirement check through its thunk slot (Base+0x3E2A808,
// called from Base+0x2C3774, D2RCore's CheckInventoryItemRequirementsForDisplay);
// Item Quality Backgrounds' cannot-equip check (1.2.42: module+0xECD0,
// called once from +0xAD04, al 0 = no mark), through a stub near the module.
inline void InstallRequirementHooks() {
	auto&       s     = Get();
	const auto  base  = ExeBase();
	const auto* thunk = reinterpret_cast<const std::uint8_t*>(base + 0x3E2B298);
	std::int32_t rel  = 0;
	if (Readable(base + 0x3E2B298, 6)) std::memcpy(&rel, thunk + 2, 4);
	if (Readable(base + 0x3E2B298, 6) && thunk[0] == 0xFF && thunk[1] == 0x25 && rel == -0xA96) {
		auto*      slot    = reinterpret_cast<std::uint64_t*>(base + 0x3E2A808);
		const auto current = *slot;
		if (current && SwapAt(slot, current, reinterpret_cast<std::uint64_t>(&HookRequirement))) s.requirement = reinterpret_cast<RequirementFn>(current);
	}
	if (!s.requirement) Problem("the game's cannot-wear colour stays on the plugin's items (requirement check not where expected)");
	if (HMODULE iqb = GetModuleHandleW(L"d2rl-item-quality-backgrounds.dll")) {
		auto*                  module = reinterpret_cast<std::uint8_t*>(iqb);
		constexpr std::uint8_t call[] { 0xE8, 0xC7, 0x3F, 0x00, 0x00 }, start[] { 0x48, 0x89, 0x5C, 0x24, 0x18 };
		if (std::memcmp(module + 0xAD04, call, sizeof(call)) == 0 && std::memcmp(module + 0xECD0, start, sizeof(start)) == 0) {
			for (std::uintptr_t at = (reinterpret_cast<std::uintptr_t>(module) & ~std::uintptr_t { 0xFFFF }) - 0x10000; !s.iqbStub && at > reinterpret_cast<std::uintptr_t>(module) - 0x70000000; at -= 0x10000)
				s.iqbStub = VirtualAlloc(reinterpret_cast<void*>(at), 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
			if (s.iqbStub) {
				std::uint8_t jump[14] { 0xFF, 0x25, 0, 0, 0, 0 };
				const auto   hook = reinterpret_cast<std::uint64_t>(&HookIqbCannot);
				std::memcpy(jump + 6, &hook, 8);
				std::memcpy(s.iqbStub, jump, sizeof(jump));
				FlushInstructionCache(GetCurrentProcess(), s.iqbStub, sizeof(jump));
				s.iqbCannot = reinterpret_cast<IqbCannotFn>(module + 0xECD0);
				if (PointCall(module + 0xAD04, s.iqbStub)) s.iqbCallSite = module + 0xAD04;
			}
		}
		if (!s.iqbCallSite) Problem("Item Quality Backgrounds' cannot-equip mark stays on the plugin's items (not version 1.2.42)");
	}
}

inline void UninstallRequirementHooks() {
	auto& s = Get();
	if (s.requirement) (void)SwapAt(reinterpret_cast<std::uint64_t*>(ExeBase() + 0x3E2A808), reinterpret_cast<std::uint64_t>(&HookRequirement), reinterpret_cast<std::uint64_t>(s.requirement));
	if (s.iqbCallSite) (void)PointCall(s.iqbCallSite, reinterpret_cast<const void*>(s.iqbCannot));
	s.requirement = nullptr;
	s.iqbCallSite = nullptr; // the stub stays: a draw may still be in it
}
} // namespace Detail

// Whether a grid widget carries marker x, y (its chronicleFlourishOffset).
inline bool Marked(const void* widget, float x, float y) {
	float marker[2] {};
	std::memcpy(marker, static_cast<const std::uint8_t*>(widget) + 0x634, sizeof(marker));
	return marker[0] == x && marker[1] == y;
}

// UI thread, once a view with the grid opens (after every plugin loaded).
// Once; false when the grid is not the expected one.
inline bool Wrap(Options options) {
	auto& s = Detail::Get();
	if (s.tried) return s.vtable != nullptr;
	s.tried   = true;
	s.options = std::move(options);
	const auto base = ExeBase();
	s.vtable        = reinterpret_cast<void**>(base + 0x1CF3800);
	// Entries nobody wraps tell the build; the wrapped ones are chained as they are.
	if (!Readable(base + 0x1CF3800, (Detail::VtCellRect + 3) * sizeof(void*)) || s.vtable[25] != reinterpret_cast<void*>(base + 0x2C49F0) || s.vtable[33] != reinterpret_cast<void*>(base + 0x2C3310)) {
		Detail::Problem("the item grid is not the expected one; 1-wide items stay in their first column");
		s.vtable = nullptr;
		return false;
	}
	DWORD protect = 0;
	if (!VirtualProtect(s.vtable, (Detail::VtCellRect + 1) * sizeof(void*), PAGE_READWRITE, &protect)) {
		s.vtable = nullptr;
		return false;
	}
	s.cellRect                       = reinterpret_cast<Detail::CellRectFn>(s.vtable[Detail::VtCellRect]);
	s.cellAtPoint                    = reinterpret_cast<Detail::CellAtPointFn>(s.vtable[Detail::VtCellAtPoint]);
	s.draw                           = reinterpret_cast<Detail::DrawFn>(s.vtable[Detail::VtDraw]);
	s.vtable[Detail::VtCellRect]    = reinterpret_cast<void*>(&Detail::HookCellRect);
	s.vtable[Detail::VtCellAtPoint] = reinterpret_cast<void*>(&Detail::HookCellAtPoint);
	s.vtable[Detail::VtDraw]        = reinterpret_cast<void*>(&Detail::HookDraw);
	VirtualProtect(s.vtable, (Detail::VtCellRect + 1) * sizeof(void*), protect, &protect);
	if (s.options.clearRequirements) Detail::InstallRequirementHooks();
	return true;
}

// While the plugin unloads.
inline void Unwrap() {
	auto& s = Detail::Get();
	if (!s.vtable || !s.cellRect) return;
	DWORD protect = 0;
	if (!VirtualProtect(s.vtable, (Detail::VtCellRect + 1) * sizeof(void*), PAGE_READWRITE, &protect)) return;
	if (s.vtable[Detail::VtCellRect] == reinterpret_cast<void*>(&Detail::HookCellRect)) s.vtable[Detail::VtCellRect] = reinterpret_cast<void*>(s.cellRect);
	if (s.vtable[Detail::VtCellAtPoint] == reinterpret_cast<void*>(&Detail::HookCellAtPoint)) s.vtable[Detail::VtCellAtPoint] = reinterpret_cast<void*>(s.cellAtPoint);
	if (s.draw && s.vtable[Detail::VtDraw] == reinterpret_cast<void*>(&Detail::HookDraw)) s.vtable[Detail::VtDraw] = reinterpret_cast<void*>(s.draw);
	Detail::UninstallRequirementHooks();
	VirtualProtect(s.vtable, (Detail::VtCellRect + 1) * sizeof(void*), protect, &protect);
}

inline bool Wrapped() { return Detail::Get().cellRect != nullptr; }

// The tiles whose items move by half cells (the view on screen's).
inline void SetShifts(std::vector<Shift> tiles) {
	auto& s = Detail::Get();
	AcquireSRWLockExclusive(&s.lock);
	s.shifts = std::move(tiles);
	ReleaseSRWLockExclusive(&s.lock);
}

// This thread: while on, the plugin's grid puts every cell at the plugin's
// cell `cell` (x low 32 bits, y high): an item drawn or measured there for a
// picture tile's tooltip.
inline void PeekAt(bool on, std::uint64_t cell = 0) { Detail::peekOn = on, Detail::peekCell = cell; }
inline bool Peeking() { return Detail::peekOn; }

// The plugin's grid's last cell looked up under a point (the mouse).
inline std::uint64_t HoverCell() { return Detail::Get().hover.load(); }

} // namespace D2RGui::TileGrid
