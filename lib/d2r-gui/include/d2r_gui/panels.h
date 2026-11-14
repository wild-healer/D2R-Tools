// D2R GUI: panels of the plugin's own beyond PanelPool.
//
//   SinglePanel  one panel: a fixed layout, or a new layout each time it is
//                shown (unloaded first, so the game reads it again).
//   ViewCache    many panels that each keep one view (a layout and the
//                widgets the game built for it): a view seen before only
//                opens its panel again, which is fast; a new one replaces the
//                view unused for longest. Panels are in groups (Grail On!:
//                views over the open stash, views of their own).
//
// Panels can only be registered while the plugin loads (after Init). UI
// thread for the rest. A panel that holds an item grid can have it bound to
// one of the plugin's player pages (the grid widget's name).
#pragma once
#include "core.h"
#include "layout.h"
#include "panel_pool.h"
#include "resources.h"
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace D2RGui {

// UI thread. Has the game let panel `name` ("<id>/<local id>") go, so its
// layout is read again on the next open.
inline void UnloadPanel(const std::string& name) {
	const auto& api = Api();
	if (!api.widgets) return;
	const D2RL::Widgets::UiAction unload { .structSize = D2RL::Widgets::UiActionSize, .flags = 0, .target = "PanelManager", .command = "UnloadPanel", .text = name.c_str() };
	(void)api.widgets->dispatchUiAction(api.context, &unload);
}

// While loading. The item grid `childName` of `panel` shows player page `page`.
inline bool BindPageGrid(D2RL::Panels::RegistrationHandle panel, D2RL::Inventory::PageRegistrationHandle page, const char* childName, D2RL::Panels::Result* result = nullptr) {
	const auto& api = Api();
	if (!api.panels || panel == D2RL::Panels::InvalidHandle || page == D2RL::Inventory::InvalidHandle || !childName) return false;
	const D2RL::Panels::PlayerPageGridBinding binding { .structSize = D2RL::Panels::PlayerPageGridBindingSize, .panel = panel, .page = page, .childName = childName };
	const auto                                bound = api.panels->bindPlayerPageGrid(api.context, &binding);
	if (result) *result = bound;
	return bound == D2RL::Panels::Result::Success;
}

// Where a panel's layout is registered.
inline std::string PanelLayoutPath(const std::string& localId) { return "data/global/ui/layouts/" + Api().pluginId + "/" + localId + "hd.json"; }

class SinglePanel {
public:
	// While loading: panel <id>/<localId> with layout `layout` (a placeholder
	// when it is remade for each show: EmptyPanel). Optionally its item grid
	// bound to `page`.
	bool Register(const std::string& localId, const std::string& layout, D2RL::Panels::PanelFlags flags, D2RL::Inventory::PageRegistrationHandle page = D2RL::Inventory::InvalidHandle,
	              const char* gridChild = nullptr) {
		const auto& api = Api();
		id_             = localId;
		if (!api.panels) return false;
		(void)RegisterResource(PanelLayoutPath(localId), layout, &layout_);
		const D2RL::Panels::PanelRegistration registration { .structSize = D2RL::Panels::PanelRegistrationSize, .flags = flags, .localId = localId.c_str() };
		if (api.panels->registerPanel(api.context, &registration, &panel_) != D2RL::Panels::Result::Success) {
			panel_ = D2RL::Panels::InvalidHandle;
			return false;
		}
		if (page != D2RL::Inventory::InvalidHandle && gridChild) {
			D2RL::Panels::Result bound {};
			if (!BindPageGrid(panel_, page, gridChild, &bound)) Log(true, "d2r-gui: could not bind the grid of %s (result %u)", localId.c_str(), static_cast<unsigned>(bound));
		}
		return true;
	}

	bool Ready() const { return panel_ != D2RL::Panels::InvalidHandle; }

	// The panel with a new layout: closed, let go by the game when it was
	// loaded, the layout registered again, opened. False when it did not open.
	bool Show(const std::string& layout, D2RL::Panels::Result* result = nullptr) {
		const auto& api = Api();
		if (!Ready()) return false;
		Close();
		if (loaded_) UnloadPanel(Name());
		if (layout_ != D2RL::Resources::InvalidHandle) (void)api.resources->unregisterResource(api.context, layout_);
		layout_ = D2RL::Resources::InvalidHandle;
		if (!RegisterResource(PanelLayoutPath(id_), layout, &layout_)) return false;
		return Open(result);
	}

	// Opens it with its layout as it is.
	bool Open(D2RL::Panels::Result* result = nullptr) {
		const auto& api = Api();
		if (!Ready()) return false;
		const auto opened = api.panels->openPanel(api.context, panel_);
		if (result) *result = opened;
		if (opened != D2RL::Panels::Result::Success) return false;
		loaded_ = true;
		return true;
	}

	void Close() {
		const auto& api = Api();
		if (IsOpen()) (void)api.panels->closePanel(api.context, panel_);
	}

	bool IsOpen() const { return Ready() && PanelPool::PanelOpen(panel_); }

	// "<id>/<local id>"
	std::string Name() const { return Api().pluginId + "/" + id_; }

	// A widget of the open panel by name (each name takes a widget handle for good: fixed names only).
	bool Find(const std::string& widget, D2RL::Widgets::WidgetHandle* out) const {
		const auto&                 api  = Api();
		D2RL::Widgets::WidgetHandle root = D2RL::Widgets::InvalidHandle;
		return api.widgets && api.widgets->findPanel(api.context, Name().c_str(), &root) == D2RL::Widgets::Result::Success
		    && api.widgets->findWidget(api.context, root, widget.c_str(), out) == D2RL::Widgets::Result::Success;
	}

	D2RL::Panels::RegistrationHandle Handle() const { return panel_; }

private:
	std::string                         id_;
	D2RL::Panels::RegistrationHandle    panel_  = D2RL::Panels::InvalidHandle;
	D2RL::Resources::RegistrationHandle layout_ = D2RL::Resources::InvalidHandle;
	bool                                loaded_ {};
};

class ViewCache {
public:
	struct View {
		std::string                         id;
		int                                 group {};
		D2RL::Panels::RegistrationHandle    panel  = D2RL::Panels::InvalidHandle;
		D2RL::Resources::RegistrationHandle layout = D2RL::Resources::InvalidHandle;
		bool                                opened {};     // opened at least once
		bool                                loaded {};     // the game has its widgets (opened since its last unload)
		std::string                         key;           // what it shows, empty before the first use
		unsigned long long                  used {};       // when it was last shown (ViewCache's clock)
		ULONGLONG                           closedAt {};   // GetTickCount64 of its last close
		ULONGLONG                           preparedAt {}; // ... of its last new layout
		ULONGLONG                           unloadedAt {}; // ... of the last time the game was told to let it go
	};

	// The game's layout loader crashed on a panel unloaded or loaded again
	// within this long of its last close, new layout or unload.
	static constexpr ULONGLONG Settle = 1500;

	// While loading: `count` panels <id>/<prefix>0 .., each with layout
	// `first` (replaced before it opens), in group `groupOf(i)` with flags
	// `flagsOf(i)`; their item grid `gridChild` bound to `page` when given.
	bool Register(const std::string& prefix, int count, const std::string& first, std::function<int(int)> groupOf, std::function<D2RL::Panels::PanelFlags(int)> flagsOf,
	              D2RL::Inventory::PageRegistrationHandle page = D2RL::Inventory::InvalidHandle, const char* gridChild = nullptr) {
		const auto& api = Api();
		if (!api.panels) return false;
		views_.assign(static_cast<std::size_t>(count), {});
		bool ok = true;
		for (int i = 0; i < count; ++i) {
			auto& view = views_[static_cast<std::size_t>(i)];
			view.id    = prefix + std::to_string(i);
			view.group = groupOf ? groupOf(i) : 0;
			(void)RegisterResource(PanelLayoutPath(view.id), first, &view.layout);
			const D2RL::Panels::PanelRegistration registration { .structSize = D2RL::Panels::PanelRegistrationSize, .flags = flagsOf ? flagsOf(i) : D2RL::Panels::PanelFlags::CloseOnEscape, .localId = view.id.c_str() };
			if (api.panels->registerPanel(api.context, &registration, &view.panel) != D2RL::Panels::Result::Success) {
				Log(true, "d2r-gui: could not register panel %s", view.id.c_str());
				view.panel = D2RL::Panels::InvalidHandle;
				ok         = false;
				continue;
			}
			if (page != D2RL::Inventory::InvalidHandle && gridChild) {
				D2RL::Panels::Result bound {};
				if (!BindPageGrid(view.panel, page, gridChild, &bound)) Log(true, "d2r-gui: could not bind the grid of %s (result %u)", view.id.c_str(), static_cast<unsigned>(bound));
			}
		}
		return ok;
	}

	int         Count() const { return static_cast<int>(views_.size()); }
	View&       operator[](int i) { return views_[static_cast<std::size_t>(i)]; }
	const View& operator[](int i) const { return views_[static_cast<std::size_t>(i)]; }
	std::string Name(int i) const { return Api().pluginId + "/" + views_[static_cast<std::size_t>(i)].id; }
	bool        IsOpen(int i) const { return i >= 0 && i < Count() && PanelPool::PanelOpen(views_[static_cast<std::size_t>(i)].panel); }
	unsigned long long Clock() const { return clock_; }

	// The view of `group` showing `key` (loaded, or let go long enough ago to be loaded again), or -1.
	int Find(const std::string& key, int group) const {
		int found = -1;
		for (int i = 0; i < Count(); ++i) {
			const auto& view = views_[static_cast<std::size_t>(i)];
			if (view.group == group && view.key == key && (view.loaded || GetTickCount64() - view.unloadedAt >= Settle)) found = i;
		}
		return found;
	}

	// The view of `group` unused for longest that may take a new layout, or
	// -1: never one open, `busy` (on screen, being prepared, kept open), or
	// closed, given a layout or let go within Settle.
	int Free(int group, const std::function<bool(int)>& busy) const {
		const ULONGLONG now   = GetTickCount64();
		int             found = -1;
		for (int i = 0; i < Count(); ++i) {
			const auto& view = views_[static_cast<std::size_t>(i)];
			if (view.group != group || view.panel == D2RL::Panels::InvalidHandle || (busy && busy(i)) || IsOpen(i)) continue;
			if (view.opened && now - view.closedAt < Settle) continue;
			if (view.preparedAt && now - view.preparedAt < Settle) continue;
			if (view.unloadedAt && now - view.unloadedAt < Settle) continue;
			if (found < 0 || view.used < views_[static_cast<std::size_t>(found)].used) found = i;
		}
		return found;
	}

	// View `i` gets layout `layout` showing `key` (the game lets its widgets go first).
	bool Load(int i, const std::string& layout, const std::string& key) {
		const auto& api  = Api();
		auto&       view = views_[static_cast<std::size_t>(i)];
		if (view.loaded && api.widgets) {
			UnloadPanel(Name(i));
			view.loaded = false, view.unloadedAt = GetTickCount64();
		}
		if (view.layout != D2RL::Resources::InvalidHandle) (void)api.resources->unregisterResource(api.context, view.layout);
		view.layout     = D2RL::Resources::InvalidHandle;
		view.preparedAt = GetTickCount64();
		view.key        = key;
		return RegisterResource(PanelLayoutPath(view.id), layout, &view.layout);
	}

	// Opens view `i` (marked used now).
	D2RL::Panels::Result Open(int i) {
		const auto& api    = Api();
		auto&       view   = views_[static_cast<std::size_t>(i)];
		const auto  result = api.panels->openPanel(api.context, view.panel);
		view.opened = view.loaded = true;
		view.used                 = ++clock_;
		return result;
	}

	void Close(int i) {
		const auto& api = Api();
		if (!IsOpen(i)) return;
		(void)api.panels->closePanel(api.context, views_[static_cast<std::size_t>(i)].panel);
		views_[static_cast<std::size_t>(i)].closedAt = GetTickCount64();
	}

	void CloseAll() {
		for (int i = 0; i < Count(); ++i) Close(i);
	}

	// Closed views beyond the newest `kept` are let go by the game (their
	// layout stays registered: showing one again loads it back), one per call.
	// Never one `busy`, open, or closed or given a layout within Settle.
	void LetGo(int kept, const std::function<bool(int)>& busy) {
		if (!Api().widgets) return;
		const ULONGLONG  now = GetTickCount64();
		std::vector<int> closed;
		for (int i = 0; i < Count(); ++i) {
			const auto& view = views_[static_cast<std::size_t>(i)];
			if (!view.loaded || (busy && busy(i)) || view.panel == D2RL::Panels::InvalidHandle || IsOpen(i)) continue;
			closed.push_back(i);
		}
		if (static_cast<int>(closed.size()) <= kept) return;
		std::sort(closed.begin(), closed.end(), [&](int a, int b) { return views_[static_cast<std::size_t>(a)].used > views_[static_cast<std::size_t>(b)].used; });
		for (std::size_t k = static_cast<std::size_t>(kept); k < closed.size(); ++k) {
			auto& view = views_[static_cast<std::size_t>(closed[k])];
			if (now - view.closedAt < Settle || now - view.preparedAt < Settle) continue;
			UnloadPanel(Name(closed[k]));
			view.loaded = false, view.unloadedAt = now;
			return;
		}
	}

private:
	std::vector<View>  views_;
	unsigned long long clock_ {};
};

} // namespace D2RGui
