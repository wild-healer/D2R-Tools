// D2R GUI: a scroll bar beside content taller than its room.
//
// A recessed track cut into steps of `stepHeight`, a bronze thumb as tall as
// the share shown, and one invisible button per step that scrolls to its share
// of the content (command "<name>step<i>"). With WindowHook running, the left
// button held on the bar drags it. The first click on a step also teaches the
// bar where the panel is in the window (exact even when the panel is not at
// $LeftPanelAnchor or the UI is scaled).
//
// Two ways to lay it out:
//   Layout(top, room, total, scroll): steps from the room, the thumb from the share shown.
//   Steps(top, steps, thumb, max, at, of): the plugin's own count of steps and
//     thumb, the thumb at at/of of the way down (fractions: scroll frames).
// Look sets the art (the default: Kit's track and thumb, steps lit on hover).
//
// Declare it as a global; Attach() (UI thread, after WindowHook::Start) the
// bar on screen: one bar is dragged at a time.
#pragma once
#include "kit.h"
#include "layout.h"
#include "window_hook.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <functional>
#include <string>

namespace D2RGui {

class ScrollBar {
public:
	struct Look {
		std::function<std::string(int width, int height)> track; // the track's sprite (default Kit::Track)
		std::function<std::string(int width, int height)> thumb; // the thumb's sprite (default Kit::Thumb)
		int  trackHeight = 0;   // the track sprite's own height: a shorter bar shows its top and its bottom `edge` (0: a sprite per height)
		int  edge        = 16;
		bool quietSteps  = false; // steps neither lit nor heard (ClickArea), else lit on hover with the click sound
		int  nestedX     = 0;     // x of widgets inside the track's clips (a plugin that shifts every x of its layout afterwards sets minus that shift)
	};

	// `left` in panel units; `panelX`, `panelY` where the panel is from
	// $LeftPanelAnchor (to find the bar before the first click; SetPanel changes it).
	ScrollBar(std::string name, int left, int width = 30, int stepHeight = 30, int panelX = 236, int panelY = -651)
	    : name_(std::move(name)), left_(left), width_(width), stepHeight_(stepHeight), panelX_(panelX), panelY_(panelY) {}

	void SetLook(Look look) { look_ = std::move(look); }

	// UI thread: called with the new scroll offset (0 .. max) when the bar is clicked or dragged.
	void OnScroll(std::function<void(int scroll)> fn) { onScroll_ = std::move(fn); }
	// UI thread: drags report here instead (when set), e.g. to coalesce them.
	void OnDrag(std::function<void(int scroll)> fn) { onDrag_ = std::move(fn); }

	// The bar's widgets for content `total` tall shown in `room` from `top`,
	// scrolled by `scroll`; nothing when it all fits.
	std::string Layout(int top, int room, int total, int scroll) {
		const int steps = std::max(2, room / stepHeight_);
		const int max   = std::max(0, total - room);
		Set(top, steps, max);
		if (max <= 0) return {};
		return Steps(top, steps, ThumbSteps(steps, room, total), max, std::clamp(scroll, 0, max), max);
	}

	// The thumb's height in steps for content `total` tall shown in `room`.
	static int ThumbSteps(int steps, int room, int total) { return std::clamp((steps * room + total / 2) / std::max(1, total), 1, steps - 1); }

	// The bar with `steps` steps from `top`, scrolling 0..max, its thumb
	// `thumb` steps tall at `at` / `of` of the way down. Nothing when max <= 0.
	std::string Steps(int top, int steps, int thumb, int max, int at, int of) {
		Set(top, steps, max);
		if (max <= 0) return {};
		std::string out = TrackLayout(top, steps * stepHeight_);
		out += Thumb("Thumb", top, steps, thumb, at, of);
		for (int i = 0; i < steps; ++i) {
			const std::string name = name_ + "Step" + std::to_string(i), command = name_ + "step" + std::to_string(i);
			const int         y    = top + i * stepHeight_;
			out += look_.quietSteps ? ClickArea(name, left_, y, width_, stepHeight_, command) : HitButton(name, left_, y, width_, stepHeight_, command);
		}
		return out;
	}

	// The thumb alone, named <name><suffix> (scroll frames each have their own).
	std::string Thumb(const std::string& suffix, int top, int steps, int thumb, int at, int of) const {
		const int y = top + (steps - thumb) * stepHeight_ * at / std::max(1, of);
		return Image(name_ + suffix, left_, y, look_.thumb ? look_.thumb(width_, thumb * stepHeight_) : Kit::Thumb(width_, thumb * stepHeight_));
	}

	// The track alone (`height` tall from `top`).
	std::string TrackLayout(int top, int height) const {
		const std::string file = look_.track ? look_.track(width_, look_.trackHeight > 0 ? look_.trackHeight : height) : Kit::Track(width_, height);
		if (look_.trackHeight <= 0 || height >= look_.trackHeight) return Image(name_ + "Track", left_, top, file);
		const int edge = std::min(look_.edge, height);
		return ClipBox(name_ + "Track", left_, top, width_, height - edge, Image(name_ + "TrackTop", look_.nestedX, 0, file))
		     + ClipBox(name_ + "TrackEnd", left_, top + height - edge, width_, edge, Image(name_ + "TrackBottom", look_.nestedX, edge - look_.trackHeight, file));
	}

	// What the mouse drags, when the layout is the plugin's own: `steps` from `top`, scrolling 0..max.
	void Set(int top, int steps, int max) { top_ = top, steps_ = std::max(1, steps), max_ = std::max(0, max); }

	// The view shown has no bar (nothing to drag).
	void Clear() { max_ = 0; }

	// The furthest the content scrolls (from the last Layout).
	int Max() const { return max_; }

	// The panel moved (from $LeftPanelAnchor); the bar is found again.
	void SetPanel(int panelX, int panelY) {
		if (panelX_ != panelX || panelY_ != panelY) learned_ = false;
		panelX_ = panelX, panelY_ = panelY;
	}

	// The bar is found again from the panel's place (the UI was moved or scaled).
	void Forget() { learned_ = false; }

	// The scroll offset of step `step`.
	int RowOf(int step) const {
		const int steps = steps_.load();
		if (steps < 2) return 0;
		return (max_ * std::clamp(step, 0, steps - 1) + (steps - 1) / 2) / (steps - 1);
	}

	// UI thread: a command; true when it was one of this bar's (its scroll then reported).
	bool Handle(const std::string& command) {
		const std::string prefix = name_ + "step";
		if (command.size() <= prefix.size() || command.compare(0, prefix.size(), prefix) != 0 || !std::isdigit(static_cast<unsigned char>(command[prefix.size()]))) return false;
		const int step = std::clamp(std::atoi(command.c_str() + prefix.size()), 0, std::max(0, steps_ - 1));
		Learn(step);
		if (onScroll_) onScroll_(RowOf(step));
		return true;
	}

	// UI thread: the mouse drags this bar (from now on, until another is attached).
	void Attach() {
		Active() = this;
		static bool hooked = false;
		if (hooked) return;
		hooked = true;
		WindowHook::SetDragTarget({
		    .stepAt    = [](POINT p) { ScrollBar* bar = Active(); return bar && bar->max_ > 0 ? bar->StepAt(p, false) : -1; },
		    .stepLoose = [](POINT p) { ScrollBar* bar = Active(); return bar && bar->max_ > 0 ? bar->StepAt(p, true) : -1; },
		    .dragTo    = [](int step) {
			    ScrollBar* bar = Active();
			    if (!bar || bar->max_ <= 0) return;
			    if (bar->onDrag_) bar->onDrag_(bar->RowOf(step));
			    else if (bar->onScroll_) bar->onScroll_(bar->RowOf(step));
		    },
		});
	}

	// Whether this bar learned where it is (its first click).
	bool Learned() const { return learned_; }

private:
	static std::atomic<ScrollBar*>& Active() {
		static std::atomic<ScrollBar*> active {};
		return active;
	}

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
		if (!learned_.exchange(true)) Log(false, "d2r-gui: scroll bar found at %ld, %ld in the window", cursor.x, cursor.y);
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
	int                              left_, width_, stepHeight_;
	std::atomic<int>                 panelX_, panelY_;
	Look                             look_;
	std::atomic<int>                 top_ {}, steps_ { 2 }, max_ {};
	std::atomic<bool>                learned_ {};
	std::atomic<double>              offsetX_ {}, offsetY_ {};
	std::function<void(int scroll)> onScroll_, onDrag_;
};

} // namespace D2RGui
