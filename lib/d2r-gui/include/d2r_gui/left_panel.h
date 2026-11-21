// D2R GUI: a plugin window that behaves like one of the game's left panels
// (character screen, quests, waypoints, stash...): opening it closes them,
// one of them opening closes it, and the view shifts right while it is open,
// as it does for theirs. (I Hate TOMLs' rules, made reusable.)
//
//   LeftPanel::Opening()            UI thread, before the window opens
//   LeftPanel::Watch(open)          UI thread, every ~250 ms (WindowHook watch):
//                                   true when a game left panel just opened
//                                   (close the window then)
//   LeftPanel::Closed(now)          UI thread, after the window closed
//   LeftPanel::InGame(bool)         from LocalPlayerReady / GameLeft
#pragma once
#include "core.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>

namespace D2RGui::LeftPanel {

// The game's left panels by widget name (closed with PanelManager:ClosePanel).
inline constexpr const char* Names[] { "CharacterStatsPanel", "QuestLogPanel", "PartyPanel", "WaypointsPanel", "HirelingInventoryPanel", "BankExpansionLayout", "BankOriginalLayout",
	                                   "VendorPanelLayout", "HoradricCubeLayout", "TradePanel", "MessageLogPanel", "ImbueItemsPanel", "HireMenuPanel" };

// The game's UI state bytes, read with D2R+0xCE500(id); the states the game
// itself counts as a left panel when it moves the view (D2R+0xC01D0).
inline constexpr std::uint64_t StateRva = 0xCE500;
inline constexpr std::uint8_t  StateStart[] { 0x48, 0x63, 0xC1, 0x48, 0x8D, 0x0D };
inline constexpr int           LeftIds[] { 0x02, 0x0B, 0x0D, 0x0E, 0x0F, 0x13, 0x15, 0x18, 0x19, 0x1E };

// The view shift: D2R+0xB9C20(mode) sets the panel mode (0 none, 1 right, 2 left, 3 both).
inline constexpr std::uint64_t CameraRva = 0xB9C20;
inline constexpr std::uint8_t  CameraStart[] { 0x48, 0x83, 0xEC, 0x38, 0x44, 0x8B, 0x0D }; // sub rsp, 38h; mov r9d, [width]
inline constexpr std::uint8_t  CameraStore[] { 0x89, 0x0D };                               // at +0x0F: mov [mode], ecx

struct State {
	std::uint8_t        last[0x40] {};
	bool                known {};
	bool                shown[std::size(Names)] {};
	bool                inGame {};
	bool                ours {};
	int                 closed {};
	int                 statesOk = -1, cameraOk = -1;
	void                (*setMode)(int) noexcept {};
	const volatile int* mode {};
};

inline State& Get() {
	static State state;
	return state;
}

inline void InGame(bool in) { Get().inGame = in, Get().ours = in && Get().ours; }

namespace Detail {
using StateFn = std::uint8_t (*)(int) noexcept;

inline StateFn States() {
	auto&      s    = Get();
	const auto base = ExeBase();
	if (s.statesOk < 0) {
		s.statesOk = base && Readable(base + StateRva, sizeof(StateStart)) && std::memcmp(reinterpret_cast<const void*>(base + StateRva), StateStart, sizeof(StateStart)) == 0;
		if (!s.statesOk) Log(true, "d2r-gui: ui states not where this build expects them; left panels followed by their widgets only");
	}
	return s.statesOk ? reinterpret_cast<StateFn>(base + StateRva) : nullptr;
}

inline bool LeftOpen(const std::uint8_t* states) {
	for (const int id : LeftIds)
		if (states[id]) return true;
	return false;
}

// Reads the bytes; true when a left panel opened since the last read.
inline bool Update() {
	auto&      s    = Get();
	const auto read = States();
	if (!read) return false;
	std::uint8_t now[0x40] {};
	for (int id = 0; id < 0x40; ++id) now[id] = read(id);
	bool opened = false;
	if (s.known)
		for (const int id : LeftIds)
			if (!s.last[id] && now[id]) opened = true;
	std::memcpy(s.last, now, sizeof(now));
	s.known = true;
	return opened;
}

inline bool Visible(const char* name) {
	const auto& api = Api();
	if (!api.widgets || !D2RL::HasWidgetServiceField(api.widgets, D2RL::WidgetServiceGetWidgetStateFieldEnd) || !api.widgets->getWidgetState) return false;
	D2RL::Widgets::WidgetHandle panel = D2RL::Widgets::InvalidHandle;
	D2RL::Widgets::State        state {};
	return api.widgets->findPanel(api.context, name, &panel) == D2RL::Widgets::Result::Success && api.widgets->getWidgetState(api.context, panel, &state) == D2RL::Widgets::Result::Success
	    && state.visible;
}

inline bool FindCamera() {
	auto& s = Get();
	if (s.cameraOk < 0) {
		const auto base = ExeBase();
		s.cameraOk      = base && Readable(base + CameraRva, 0x15) && std::memcmp(reinterpret_cast<const void*>(base + CameraRva), CameraStart, sizeof(CameraStart)) == 0
		          && std::memcmp(reinterpret_cast<const void*>(base + CameraRva + 0x0F), CameraStore, sizeof(CameraStore)) == 0;
		if (s.cameraOk) {
			std::int32_t disp = 0;
			std::memcpy(&disp, reinterpret_cast<const void*>(base + CameraRva + 0x11), sizeof(disp));
			s.setMode  = reinterpret_cast<void (*)(int) noexcept>(base + CameraRva);
			s.mode     = reinterpret_cast<const volatile int*>(base + CameraRva + 0x15 + disp);
			s.cameraOk = Readable(reinterpret_cast<std::uintptr_t>(s.mode), sizeof(int));
		}
		if (!s.cameraOk) Log(true, "d2r-gui: the panel mode is not where this build expects it; the view does not move for the window");
	}
	return s.cameraOk > 0;
}

inline void Hold() {
	auto& s  = Get();
	s.closed = 0;
	if (!s.inGame || !FindCamera()) return;
	const int mode = *s.mode;
	if (mode < 0 || mode > 3 || (mode & 2)) return;
	s.setMode(mode | 2);
	s.ours = true;
}

inline void Release(bool now) {
	auto& s = Get();
	if (!s.ours || (!now && ++s.closed < 2)) return;
	s.ours = false;
	if (!s.inGame || !FindCamera()) return;
	if (s.known && LeftOpen(s.last)) return; // a game panel took the left side
	const int mode = *s.mode;
	if (mode >= 0 && mode <= 3 && (mode & 2)) s.setMode(mode & 1);
}
} // namespace Detail

// UI thread, before the window opens: the game's left panels closed, the view shifted.
inline void Opening() {
	auto& s = Get();
	(void)Detail::Update();
	const bool anyLeft = s.known && Detail::LeftOpen(s.last);
	const auto& api    = Api();
	for (std::size_t k = 0; k < std::size(Names); ++k) {
		s.shown[k] = Detail::Visible(Names[k]); // only a panel opening from here on closes the window
		if (!api.widgets || !(s.shown[k] || anyLeft)) continue;
		const D2RL::Widgets::UiAction close { .structSize = D2RL::Widgets::UiActionSize, .flags = 0, .target = "PanelManager", .command = "ClosePanel", .text = Names[k] };
		(void)api.widgets->dispatchUiAction(api.context, &close);
	}
	Detail::Hold();
}

// UI thread, now and then. `open`: whether the window is open now; `busy`:
// a new view of it is on its way (PanelPool::Busy). True when a game left
// panel opened while it was open: close the window then (and call Closed).
inline bool Watch(bool open, bool busy = false) {
	auto& s      = Get();
	bool  opened = Detail::Update() && open;
	for (std::size_t k = 0; k < std::size(Names); ++k) {
		const bool shown = open && Detail::Visible(Names[k]);
		opened           = opened || (shown && !s.shown[k]);
		s.shown[k]       = shown;
	}
	if (opened) return true;
	if (open) Detail::Hold();
	else Detail::Release(!busy); // closed by the game (Escape): at once unless a new view is coming
	return false;
}

// UI thread, after the plugin closed its window.
inline void Closed() { Detail::Release(true); }

} // namespace D2RGui::LeftPanel
