// D2R GUI: a window whose layout changes while it is open.
//
// D2R keeps a panel's widgets from the first time it loads it, and plugins can
// only register panels while they load. So a pool of panels is registered at
// load, each with a placeholder layout, and every new view of the window is a
// new layout on one of them: two take turns, each unloaded before it opens
// again (Run Counter's method, as in I Hate TOMLs and Grail On!).
//
// Rebuilds are coalesced (one per frame) and wait for the last view's layout
// to finish loading (at most 1 s): a panel opened while another is still
// loading can leave the game with a widget it failed to create, and a panel
// reopened before its load ended keeps its old layout.
//
// Each view carries a probe widget, Probe0 or Probe1 (alternating per panel):
// fixed names, because every name looked up with findWidget takes one of the
// loader's ~255 widget handles for good. If a reopened panel keeps showing
// its old layout three times in a row, the pool gives up reuse for a minute
// and takes a fresh panel per view (unloading the one left behind).
//
// Declare the pool as a global (callbacks keep a pointer to it). UI thread
// for everything but Register.
#pragma once
#include "core.h"
#include "layout.h"
#include <functional>
#include <string>
#include <vector>

namespace D2RGui {

class PanelPool {
public:
	// The panel's JSON for its next view: `panelName` is the panel's name
	// (use it as the Panel's name), `probe` the probe widget it must hold
	// (layout.h Probe(probe)). Panel() and Probe() build both.
	using Builder = std::function<std::string(const std::string& panelName, const std::string& probe)>;

	// While the plugin loads, after Init. `localPrefix` names the panels
	// (<id>/<prefix>1 ..), `count` of them (at least 2; more make the
	// fallback after a lost layout last longer; the loader allows 64 panels a
	// plugin in all). Flags: CloseOnEscape for a window of its own.
	bool Register(Builder builder, const std::string& localPrefix = "View", int count = 16, D2RL::Panels::PanelFlags flags = D2RL::Panels::PanelFlags::CloseOnEscape) {
		build_     = std::move(builder);
		const auto& api = Api();
		if (!api.panels || !api.resources) return false;
		for (int i = 1; i <= count; ++i) {
			Slot slot { .id = localPrefix + std::to_string(i) };
			// The game only finds a panel's layout if one was registered at load.
			const std::string placeholder = R"({"type": "Panel", "name": )" + Json(api.pluginId + "/" + slot.id) + R"(, "fields": {"priority": 10}, "children": []})";
			if (!RegisterResource(LayoutPath(slot.id), placeholder, &slot.layout) && i == 1) Log(true, "d2r-gui: first layout for %s refused", slot.id.c_str());
			const D2RL::Panels::PanelRegistration registration {
				.structSize = D2RL::Panels::PanelRegistrationSize,
				.flags      = flags,
				.localId    = slot.id.c_str(),
			};
			const auto result = api.panels->registerPanel(api.context, &registration, &slot.panel);
			if (result != D2RL::Panels::Result::Success) {
				if (slots_.size() < 2) Log(true, "d2r-gui: panel %s refused (result %u)", slot.id.c_str(), static_cast<unsigned>(result));
				break;
			}
			slots_.push_back(std::move(slot));
		}
		if (slots_.size() < 2) Log(true, "d2r-gui: only %zu panels could be registered for %s", slots_.size(), localPrefix.c_str());
		return slots_.size() >= 2;
	}

	// The view again, at the next frame (several calls in a frame make one view).
	void Show() {
		wanted_ = true;
		if (queued_) return;
		queued_ = RunOnUi([this] { Task(); });
		if (!queued_) wanted_ = false, ShowNow();
	}

	// The view again, now.
	void ShowNow() {
		const auto& api = Api();
		if (slots_.size() < 2 || !api.panels || !api.resources || !build_) return;
		const int index = Next();
		auto&     next  = slots_[static_cast<std::size_t>(index)];
		const unsigned    tag    = static_cast<unsigned>(next.uses) % 2;
		const std::string name   = api.pluginId + "/" + next.id;
		const std::string probe  = "Probe" + std::to_string(tag);
		const std::string layout = build_(name, probe);
		CloseAll();
		// Without reuse each view takes a fresh panel: the one left behind is unloaded.
		if (shown_ >= 0 && shown_ != index && slots_[static_cast<std::size_t>(shown_)].opened) Unload(slots_[static_cast<std::size_t>(shown_)]);
		if (next.opened) Unload(next);
		if (next.layout != D2RL::Resources::InvalidHandle) (void)api.resources->unregisterResource(api.context, next.layout);
		next.layout           = D2RL::Resources::InvalidHandle;
		const bool registered = RegisterResource(LayoutPath(next.id), layout, &next.layout);
		++next.uses;
		const auto opened = api.panels->openPanel(api.context, next.panel);
		if (!registered || opened != D2RL::Panels::Result::Success) Log(true, "d2r-gui: view on %s: layout %s, open result %u (%zu bytes)", next.id.c_str(), registered ? "ok" : "refused", static_cast<unsigned>(opened), layout.size());
		loadingPanel_ = name;
		loadingProbe_ = probe;
		loadingSince_ = GetTickCount64();
		if (reuse_ && next.opened) probeName_ = probe, probePanel_ = name, probeUntil_ = 0, RunOnUi([this] { ProbeTask(); });
		next.opened = true;
		shown_      = index;
		if (onShown_) onShown_();
	}

	// Closes the window.
	void Close() {
		CloseAll();
		wanted_ = false;
		RetryReuseImpl();
	}

	// After a lost layout, each view takes a fresh panel for a minute; leaving
	// a page tries reopening panels again sooner (one miss gives it up again).
	void RetryReuse() { RetryReuseImpl(); }

	// The pool has its panels (Register worked).
	bool Ready() const { return slots_.size() >= 2; }

	bool IsOpen() const { return shown_ >= 0 && PanelOpen(slots_[static_cast<std::size_t>(shown_)].panel); }

	// A view is asked for or still loading.
	bool Busy() const { return wanted_ || queued_ || GetTickCount64() - loadingSince_ < 400; }

	// The shown panel's name ("<id>/View3"), empty before the first view.
	std::string ShownName() const { return shown_ >= 0 ? Api().pluginId + "/" + slots_[static_cast<std::size_t>(shown_)].id : std::string(); }

	// Called after each view opens (to move the camera, start watching the mouse, ...).
	void OnShown(std::function<void()> fn) { onShown_ = std::move(fn); }

	// Whether panel `name` has loaded the layout holding widget `probe`.
	static bool Loaded(const std::string& name, const std::string& probe) {
		const auto&                 api  = Api();
		D2RL::Widgets::WidgetHandle root = D2RL::Widgets::InvalidHandle, widget = D2RL::Widgets::InvalidHandle;
		return api.widgets && api.widgets->findPanel(api.context, name.c_str(), &root) == D2RL::Widgets::Result::Success
		    && api.widgets->findWidget(api.context, root, probe.c_str(), &widget) == D2RL::Widgets::Result::Success;
	}

	static bool PanelOpen(D2RL::Panels::RegistrationHandle handle) {
		const auto&               api = Api();
		D2RL::Panels::PanelInfo info { .structSize = D2RL::Panels::PanelInfoSize };
		return api.panels && handle != D2RL::Panels::InvalidHandle && api.panels->getPanelInfo(api.context, handle, &info) == D2RL::Panels::Result::Success
		    && info.presentationState == D2RL::Panels::PresentationState::Open;
	}

private:
	struct Slot {
		std::string                         id;
		D2RL::Panels::RegistrationHandle    panel  = D2RL::Panels::InvalidHandle;
		D2RL::Resources::RegistrationHandle layout = D2RL::Resources::InvalidHandle;
		bool                                opened {};
		int                                 uses = 1; // layouts registered for it (its placeholder at load)
	};

	std::string LayoutPath(const std::string& id) const { return "data/global/ui/layouts/" + Api().pluginId + "/" + id + "hd.json"; }

	void CloseAll() {
		const auto& api = Api();
		for (const auto& slot : slots_)
			if (PanelOpen(slot.panel)) (void)api.panels->closePanel(api.context, slot.panel);
	}

	void Unload(const Slot& slot) {
		const auto& api = Api();
		if (!api.widgets) return;
		const std::string             name = api.pluginId + "/" + slot.id;
		const D2RL::Widgets::UiAction unload { .structSize = D2RL::Widgets::UiActionSize, .flags = 0, .target = "PanelManager", .command = "UnloadPanel", .text = name.c_str() };
		(void)api.widgets->dispatchUiAction(api.context, &unload);
	}

	void RetryReuseImpl() {
		if (reuse_) return;
		reuse_         = true;
		misses_        = 1; // one miss gives it up again
		fallbackSince_ = 0;
		Log(false, "d2r-gui: trying to reopen panels again");
	}

	int Next() {
		const int count = static_cast<int>(slots_.size());
		if (!reuse_ && fallbackSince_ && GetTickCount64() - fallbackSince_ > 60000) RetryReuseImpl();
		if (reuse_) return shown_ == 0 ? 1 : 0;
		for (int i = 0; i < count; ++i)
			if (i != shown_ && !slots_[static_cast<std::size_t>(i)].opened) return i;
		if (!toldFull_) Log(true, "d2r-gui: out of fresh panels; the window may show old views until the game restarts");
		toldFull_ = true;
		return shown_ == 0 ? 1 : 0;
	}

	void Task() {
		queued_ = false;
		if (!wanted_) return;
		if (!loadingPanel_.empty() && (!Loaded(loadingPanel_, loadingProbe_) || GetTickCount64() - loadingSince_ < 50) && GetTickCount64() - loadingSince_ < 1000) {
			queued_ = RunOnUi([this] { Task(); });
			if (queued_) return;
		}
		wanted_ = false;
		ShowNow();
	}

	// Each frame until done: once a reused panel is open, checks that it
	// shows the new layout.
	void ProbeTask() {
		if (CheckProbe()) RunOnUi([this] { ProbeTask(); });
	}

	bool CheckProbe() {
		if (probeName_.empty()) return false;
		if (Loaded(probePanel_, probeName_)) {
			probeName_.clear();
			misses_ = 0;
			return false;
		}
		if (probePanel_ != ShownName() || !IsOpen()) { // another view replaced it, or the window closed: no verdict
			if (!IsOpen() || probeName_ != loadingProbe_) probeName_.clear();
			return !probeName_.empty();
		}
		if (!probeUntil_) probeUntil_ = GetTickCount64() + 1000; // views load within a frame or two
		if (GetTickCount64() < probeUntil_) return true;
		probeName_.clear();
		if (++misses_ < 3) {
			Log(true, "d2r-gui: a reopened panel did not show its new layout within 1 s; showing the view again");
			Show();
			return false;
		}
		reuse_         = false;
		fallbackSince_ = GetTickCount64();
		Log(true, "d2r-gui: a reopened panel kept its old layout; each view takes a new panel for a minute");
		Show();
		return false;
	}

	Builder               build_;
	std::function<void()> onShown_;
	std::vector<Slot>     slots_;
	int                   shown_ = -1;
	bool                  reuse_ = true, wanted_ {}, queued_ {}, toldFull_ {};
	int                   misses_ {};
	ULONGLONG             fallbackSince_ {}, loadingSince_ {}, probeUntil_ {};
	std::string           loadingPanel_, loadingProbe_, probeName_, probePanel_;
};

} // namespace D2RGui
