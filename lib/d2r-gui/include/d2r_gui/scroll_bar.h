// D2R GUI: a scroll bar beside content taller than its room.
//
// A recessed track cut into steps of `stepHeight`, a bronze thumb as tall as
// the share shown, and one invisible button per step that scrolls to its share
// of the content (command "<name>step<i>"). With WindowHook running, the left
// button held on the bar drags it. The first click on a step also teaches the
// bar where the panel is in the window (exact even when the panel is not at
// $LeftPanelAnchor or the UI is scaled).
//
// Declare it as a global; Attach() once (UI thread) after WindowHook::Start.
#pragma once
#include "kit.h"
#include "layout.h"
#include "window_hook.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <string>

namespace D2RGui {

class ScrollBar {
public:
	// `left` in panel units; `panelX`, `panelY` where the panel is from
	// $LeftPanelAnchor (to find the bar before the first click).
	ScrollBar(std::string name, int left, int width = 30, int stepHeight = 30, int panelX = 236, int panelY = -651)
	    : name_(std::move(name)), left_(left), width_(width), stepHeight_(stepHeight), panelX_(panelX), panelY_(panelY) {}

	// UI thread: called with the new scroll offset (0 .. max) when the bar is clicked or dragged.
	void OnScroll(std::function<void(int scroll)> fn) { onScroll_ = std::move(fn); }

	// The bar's widgets for content `total` tall shown in `room` from `top`,
	// scrolled by `scroll`; nothing when it all fits.
	std::string Layout(int top, int room, int total, int scroll) {
		const int steps = std::max(2, room / stepHeight_);
		const int max   = std::max(0, total - room);
		top_ = top, steps_ = steps, max_ = max;
		if (max <= 0) return {};
		const int   thumb = std::clamp((steps * room + total / 2) / std::max(1, total), 1, steps - 1);
		const int   y     = top + (steps - thumb) * stepHeight_ * std::clamp(scroll, 0, max) / max;
		std::string out;
		out += Image(name_ + "Track", left_, top, Kit::Track(width_, steps * stepHeight_));
		out += Image(name_ + "Thumb", left_, y, Kit::Thumb(width_, thumb * stepHeight_));
		for (int i = 0; i < steps; ++i) out += HitButton(name_ + "Step" + std::to_string(i), left_, top + i * stepHeight_, width_, stepHeight_, name_ + "step" + std::to_string(i));
		return out;
	}

	// The view shown has no bar (nothing to drag).
	void Clear() { max_ = 0; }

	// The furthest the content scrolls (from the last Layout).
	int Max() const { return max_; }

	// The scroll offset of step `step`.
	int RowOf(int step) const {
		const int steps = std::max(2, steps_.load());
		return (max_ * std::clamp(step, 0, steps - 1) + (steps - 1) / 2) / (steps - 1);
	}

	// UI thread: a command; true when it was one of this bar's (its scroll then reported).
	bool Handle(const std::string& command) {
		const std::string prefix = name_ + "step";
		if (command.compare(0, prefix.size(), prefix) != 0) return false;
		const int step = std::atoi(command.c_str() + prefix.size());
		Learn(step);
		if (onScroll_) onScroll_(RowOf(step));
		return true;
	}

	// UI thread, once: the mouse drags this bar.
	void Attach() {
		WindowHook::SetDragTarget({
		    .stepAt    = [this](POINT p) { return max_ > 0 ? StepAt(p, false) : -1; },
		    .stepLoose = [this](POINT p) { return max_ > 0 ? StepAt(p, true) : -1; },
		    .dragTo    = [this](int step) { if (onScroll_ && max_ > 0) onScroll_(RowOf(step)); },
		});
	}

private:
	std::pair<double, double> Offset() const {
		if (learned_) return { offsetX_, offsetY_ };
		return WindowHook::LeftAnchorOffset(panelX_, panelY_);
	}

	void Learn(int step) {
		POINT        cursor {};
		const double scale = WindowHook::UiScale();
		if (!scale || !GetCursorPos(&cursor) || !ScreenToClient(WindowHook::GameWindow(), &cursor)) return;
		offsetX_ = cursor.x - (left_ + width_ / 2.0) * scale;
		offsetY_ = cursor.y - (top_ + (step + 0.5) * stepHeight_) * scale;
		learned_ = true;
	}

	// The step under a window point, or -1 off the bar (with `loose`, any
	// height counts: a drag goes on above and below the bar).
	int StepAt(POINT point, bool loose) const {
		const double scale = WindowHook::UiScale();
		if (!scale) return -1;
		const auto [ox, oy] = Offset();
		const int    steps  = steps_;
		const double x = (point.x - ox) / scale, y = (point.y - oy) / scale - top_;
		if (!loose && (x < left_ - 12 || x > left_ + width_ + 12 || y < 0 || y >= steps * stepHeight_)) return -1;
		return std::clamp(static_cast<int>(std::floor(y / stepHeight_)), 0, steps - 1);
	}

	std::string                      name_;
	int                              left_, width_, stepHeight_, panelX_, panelY_;
	std::atomic<int>                 top_ {}, steps_ { 2 }, max_ {};
	std::atomic<bool>                learned_ {};
	std::atomic<double>              offsetX_ {}, offsetY_ {};
	std::function<void(int scroll)> onScroll_;
};

} // namespace D2RGui
