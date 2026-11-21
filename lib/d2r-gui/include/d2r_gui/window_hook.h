// D2R GUI: the mouse over the plugin's window.
//
// No plugin service reports the wheel or the mouse moving, so the game window
// is subclassed once: while the plugin's window is open, the wheel over its
// side of the screen scrolls it, the left button held on a scroll bar drags
// it, and clicks tell TextInput when one lands off a text box. Other mouse
// messages can be handed to the plugin (`extra`, return true to keep them
// from the game).
//
// UiScale/PanelPoint map window pixels to layout units: the game draws its UI
// at the window's height / 2160.
#pragma once
#include "core.h"
#include "text_input.h"
#include <atomic>
#include <cmath>
#include <functional>
#include <thread>

namespace D2RGui::WindowHook {

struct Options {
	std::function<bool()> active;       // window thread: whether the plugin's window is open (keep it cheap: read an atomic)
	std::function<void(int notches)> wheel; // UI thread: notches > 0 down, < 0 up
	double wheelLeft = 0, wheelRight = 0.45; // where on the window (share of its width) the wheel scrolls the plugin's window
	std::function<bool(UINT message, WPARAM, LPARAM)> extra; // window thread, every message first
	std::function<void()> watch;        // UI thread, every `watchMs` from the first Start (0: none)
	int                   watchMs = 250;
};

// A scroll bar the mouse can drag (scroll_bar.h fills it).
struct DragTarget {
	std::function<int(POINT client)> stepAt;   // window thread: the bar's step under a point, -1 off it (loose: any height)
	std::function<int(POINT client)> stepLoose;
	std::function<void(int step)>    dragTo;   // UI thread
};

struct State {
	Options               options;
	DragTarget            drag;
	HWND                  window {};
	WNDPROC               proc {};
	std::atomic<int>      wheelSum {}, dragStep { -1 };
	bool                  dragging {};
	bool                  started {};
};

inline State& Get() {
	static State state;
	return state;
}

inline double UiScale() {
	RECT client {};
	const HWND w = Get().window;
	return w && GetClientRect(w, &client) && client.bottom > 0 ? client.bottom / 2160.0 : 0;
}

inline HWND GameWindow() { return Get().window; }

// Where a panel at panelX, panelY hanging from $LeftPanelAnchor (x 0, y
// 39.7% of the height) is in the window: window = panel * scale + offset.
inline std::pair<double, double> LeftAnchorOffset(int panelX, int panelY) {
	RECT       client {};
	const HWND w = Get().window;
	if (!w || !GetClientRect(w, &client)) return {};
	const double scale = client.bottom / 2160.0;
	return { panelX * scale, client.bottom * 0.397 + panelY * scale };
}

namespace Detail {

inline void WheelTask() {
	const int notches = Get().wheelSum.exchange(0);
	if (notches && Get().options.wheel) Get().options.wheel(notches);
}

inline void DragTask() {
	const int step = Get().dragStep.load();
	if (step >= 0 && Get().drag.dragTo) Get().drag.dragTo(step);
}

inline LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
	auto&      s      = Get();
	const bool active = s.options.active && s.options.active();
	if (s.options.extra && s.options.extra(message, wParam, lParam)) return 0;
	if (message == WM_MOUSEWHEEL && active && s.options.wheel) {
		POINT point { static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
		RECT  client {};
		if (ScreenToClient(window, &point) && GetClientRect(window, &client) && point.x >= client.right * s.options.wheelLeft && point.x < client.right * s.options.wheelRight) {
			if (s.wheelSum.fetch_add(static_cast<short>(HIWORD(wParam)) > 0 ? -1 : 1) == 0) RunOnUi(WheelTask);
			return 0;
		}
	}
	const POINT point { static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
	if (message == WM_LBUTTONDOWN && active && s.drag.stepAt && s.drag.stepAt(point) >= 0) s.dragging = true, s.dragStep = -1;
	else if (message == WM_MOUSEMOVE && s.dragging) {
		if ((wParam & MK_LBUTTON) && active && s.drag.stepLoose) {
			const int step = s.drag.stepLoose(point);
			if (step >= 0 && s.dragStep.exchange(step) != step) RunOnUi(DragTask);
		} else s.dragging = false;
	} else if (message == WM_LBUTTONUP) s.dragging = false;
	if (message == WM_LBUTTONUP || message == WM_RBUTTONUP) TextInput::NoteClick();
	return CallWindowProcW(s.proc, window, message, wParam, lParam);
}

inline BOOL CALLBACK FindGameWindow(HWND window, LPARAM found) {
	DWORD process = 0;
	GetWindowThreadProcessId(window, &process);
	if (process != GetCurrentProcessId() || !IsWindowVisible(window) || GetWindow(window, GW_OWNER)) return TRUE;
	RECT  rect {}, best {};
	auto* result = reinterpret_cast<HWND*>(found);
	GetWindowRect(window, &rect);
	if (*result) GetWindowRect(*result, &best);
	if (!*result || (rect.right - rect.left) * (rect.bottom - rect.top) > (best.right - best.left) * (best.bottom - best.top)) *result = window;
	return TRUE;
}

} // namespace Detail

// UI thread, when the plugin's window first opens (the game window exists by then). Once.
inline void Start(Options options) {
	auto& s = Get();
	if (s.started) return;
	s.started = true;
	s.options = std::move(options);
	HWND window {};
	EnumWindows(Detail::FindGameWindow, reinterpret_cast<LPARAM>(&window));
	if (window) s.proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(Detail::Proc)));
	if (s.proc) s.window = window;
	else Log(true, "d2r-gui: could not watch the mouse wheel (error %lu)", GetLastError());
	if (s.options.watch && s.options.watchMs > 0)
		std::thread([] {
			for (;;) {
				Sleep(static_cast<DWORD>(Get().options.watchMs));
				RunOnUi([] { if (Get().options.watch) Get().options.watch(); });
			}
		}).detach();
}

// The scroll bar the mouse drags now (none: an empty target).
inline void SetDragTarget(DragTarget target) { Get().drag = std::move(target); }

// While the plugin unloads: the game window's own procedure back.
inline void Stop() {
	auto& s = Get();
	if (s.window && reinterpret_cast<WNDPROC>(GetWindowLongPtrW(s.window, GWLP_WNDPROC)) == Detail::Proc) SetWindowLongPtrW(s.window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(s.proc));
	s.window = {};
	s.options.watch = nullptr;
}

} // namespace D2RGui::WindowHook
