// D2R GUI: a button on the inventory's left edge, in the stone and gold mold
// Charm Inventory and Automatic Materials Deposit use (a child layout of the
// stock inventory panel, keyboard and mouse only).
//
// It can be moved while the game runs: at once where D2RLoader can move a
// widget (setWidgetRect), else in the game's own widget the next time the
// inventory's grid is drawn (MoveFromGrid, from an item grid draw hook:
// item_grid.h), and its layout is registered again for the next time the
// game loads the inventory (Reload, each UI tick, once the inventory is closed).
#pragma once
#include "core.h"
#include "layout.h"
#include "mold_data.inc"
#include "panels.h"
#include "picture.h"
#include "resources.h"
#include <atomic>
#include <cstring>
#include <string>
#include <vector>

namespace D2RGui {

// The mold's picture (54 x 141).
inline Picture MoldPicture() { return { ArtData::MoldWidth, ArtData::MoldHeight, std::vector<unsigned char>(ArtData::MoldRgba, ArtData::MoldRgba + sizeof(ArtData::MoldRgba)) }; }

class InventoryButton {
public:
	static constexpr int Width = ArtData::MoldWidth, Height = ArtData::MoldHeight, InnerX = 1, InnerY = 39;

	// The mold's sprite, the button's sprite (frames 0 normal, 1 pressed, 3
	// hovered; 52 x 60 fits the mold), its tooltip and command; the button's
	// widget name inside the mold (<id>/<button>).
	struct Look {
		std::string moldSprite = "mold", buttonSprite, button = "Button", tooltip, command;
	};

	// While loading: the mold at x, y of the inventory. `localId` names its
	// layout (<id>/<localId>), and the mold widget is <id>/<localId> too.
	bool Register(const std::string& localId, Look look, int x, int y) {
		look_ = std::move(look), id_ = localId, x_ = shownX_ = x, y_ = shownY_ = y, shownKnown_ = true;
		RegisterLayout();
		const auto& api = Api();
		if (!api.panels) return false;
		const D2RL::Panels::ChildLayoutRegistration child {
			.structSize = D2RL::Panels::ChildLayoutRegistrationSize,
			.flags      = D2RL::Panels::ChildLayoutFlags::KeyboardMouseOnly,
			.stockPanel = D2RL::Panels::StockPanel::PlayerInventory,
			.localId    = id_.c_str(),
		};
		D2RL::Panels::ChildLayoutHandle handle {};
		return api.panels->registerChildLayout(api.context, &child, &handle) == D2RL::Panels::Result::Success;
	}

	// UI thread: the button goes to x, y.
	void Move(int x, int y) {
		x_ = x, y_ = y;
		RegisterLayout();
		namespace W     = D2RL::Widgets;
		const auto& api = Api();
		bool        moved = false;
		if (api.widgets && D2RL::HasWidgetServiceField(api.widgets, D2RL::WidgetServiceSetWidgetRectFieldEnd) && api.widgets->setWidgetRect)
			for (const char* name : Panels) {
				W::WidgetHandle panel {}, widget {};
				if (api.widgets->findPanel(api.context, name, &panel) != W::Result::Success || api.widgets->findWidget(api.context, panel, MoldName().c_str(), &widget) != W::Result::Success) continue;
				const W::Rect rect { x_, y_, Width, Height };
				if (api.widgets->setWidgetRect(api.context, widget, &rect) == W::Result::Success) moved = true;
			}
		if (!moved) moveWanted_ = true;
	}

	// The inventory's layout is to be loaded again once it is closed (Reload).
	void ReloadWhenClosed() { reload_ = true; }

	// UI thread, from an item grid's draw (not the plugin's own grids): moves
	// the mold in the game's widget tree of that grid's panel when Move could not.
	void MoveFromGrid(void* grid) {
		if (!moveWanted_) return;
		auto* root = static_cast<std::uint8_t*>(grid);
		for (int up = 0; up < 12; ++up) {
			auto* parent = *reinterpret_cast<std::uint8_t**>(root + WidgetParent);
			if (!parent) break;
			root = parent;
		}
		if (FindMold(root, x_, y_, 0)) { // already there (the layout was loaded again)
			shownX_ = x_, shownY_ = y_, shownKnown_ = true, moveWanted_ = false;
			return;
		}
		if (!shownKnown_) return;
		auto* mold = FindMold(root, shownX_, shownY_, 0);
		if (!mold) return; // not this grid's panel
		auto* rect = reinterpret_cast<std::int32_t*>(mold + WidgetRect);
		rect[0] = x_, rect[1] = y_;
		Log(false, "d2r-gui: inventory button moved from %d,%d to %d,%d", shownX_, shownY_, x_, y_);
		shownX_ = x_, shownY_ = y_, moveWanted_ = false;
	}

	// UI thread, each tick: after ReloadWhenClosed, the inventory's layout is let go once
	// the inventory is closed (when D2RLoader tells whether it is shown, else
	// once out of a game: `inGame`), so the game loads it again with the new place.
	void Reload(bool inGame) {
		const auto& api = Api();
		if (!reload_ || !api.widgets) return;
		namespace W           = D2RL::Widgets;
		const bool stateKnown = D2RL::HasWidgetServiceField(api.widgets, D2RL::WidgetServiceGetWidgetStateFieldEnd) && api.widgets->getWidgetState;
		if (!stateKnown && inGame) return;
		std::vector<const char*> loaded;
		for (const char* name : Panels) {
			W::WidgetHandle panel {};
			if (api.widgets->findPanel(api.context, name, &panel) != W::Result::Success) continue;
			W::State state {};
			if (stateKnown && (api.widgets->getWidgetState(api.context, panel, &state) != W::Result::Success || state.visible)) return; // open: after it closes
			loaded.push_back(name);
		}
		reload_ = false;
		for (const char* name : loaded) {
			UnloadPanel(name);
			Log(false, "d2r-gui: the inventory (%s) loads again with the button at %d, %d", name, x_, y_);
		}
	}

	// UI thread, each tick: while the inventory is open, logs (once per
	// change) where the button and `others` (other plugins' widgets there)
	// are, and shows the button again when something hid it.
	void Watch(const std::vector<std::string>& others = {}) {
		const auto& api = Api();
		if (!api.widgets) return;
		namespace W = D2RL::Widgets;
		W::WidgetHandle panel {};
		const char*     panelName = nullptr;
		for (const char* name : Panels)
			if (api.widgets->findPanel(api.context, name, &panel) == W::Result::Success) {
				panelName = name;
				break;
			}
		if (!panelName) return;
		if (D2RL::HasWidgetServiceField(api.widgets, D2RL::WidgetServiceGetWidgetStateFieldEnd)) {
			W::State state {};
			if (api.widgets->getWidgetState(api.context, panel, &state) != W::Result::Success || !state.visible) return;
		}
		std::vector<std::string> names { MoldName(), ButtonName() };
		names.insert(names.end(), others.begin(), others.end());
		std::string report = std::string("inventory button in ") + panelName + ":";
		for (const auto& name : names) {
			W::WidgetHandle widget {};
			W::Result       found = api.widgets->findWidget(api.context, panel, name.c_str(), &widget);
			if (found != W::Result::Success && name == ButtonName()) {
				W::WidgetHandle root {}, inner {};
				if (api.widgets->findWidget(api.context, panel, MoldName().c_str(), &root) == W::Result::Success && api.widgets->findWidget(api.context, root, name.c_str(), &inner) == W::Result::Success)
					widget = inner, found = W::Result::Success;
			}
			char line[256];
			if (found != W::Result::Success) {
				std::snprintf(line, sizeof(line), " %s not found (%u);", name.c_str(), static_cast<unsigned>(found));
				report += line;
				continue;
			}
			W::Rect rect {};
			(void)api.widgets->getWidgetRect(api.context, widget, &rect);
			W::State      state { true, true, false };
			const bool    stateKnown  = D2RL::HasWidgetServiceField(api.widgets, D2RL::WidgetServiceGetWidgetStateFieldEnd) && api.widgets->getWidgetState(api.context, widget, &state) == W::Result::Success;
			W::ScreenRect screen {};
			const bool    screenKnown = D2RL::HasWidgetServiceField(api.widgets, D2RL::WidgetServiceGetWidgetScreenRectFieldEnd) && api.widgets->getWidgetScreenRect(api.context, widget, &screen) == W::Result::Success;
			std::snprintf(line, sizeof(line), " %s at %d,%d %dx%d, %s, screen %s%.0f,%.0f %.0fx%.0f;", name.c_str(), rect.x, rect.y, rect.width, rect.height,
			              stateKnown ? (state.visible ? "visible" : "HIDDEN") : "visibility unknown", screenKnown ? "" : "unknown ", screen.x, screen.y, screen.width, screen.height);
			report += line;
			if (stateKnown && !state.visible && (name == MoldName() || name == ButtonName())) (void)api.widgets->setWidgetVisible(api.context, widget, true);
		}
		if (report != told_) Log(false, "d2r-gui: %s", report.c_str());
		told_ = report;
	}

	std::string MoldName() const { return Api().pluginId + "/" + id_; }
	std::string ButtonName() const { return Api().pluginId + "/" + look_.button; }

private:
	static constexpr const char* Panels[] { "PlayerInventoryExpansionLayout", "PlayerInventoryOriginalLayout", "PlayerInventoryExpansionLayoutHD", "PlayerInventoryOriginalLayoutHD" };
	// The game's widget: parent, children, child count, whether it fills its parent, its local rect (read at every draw).
	static constexpr std::size_t WidgetParent = 0x30, WidgetChildren = 0x58, WidgetChildCount = 0x60, WidgetFills = 0x52, WidgetRect = 0x70;

	void RegisterLayout() {
		const auto&       api    = Api();
		const std::string folder = "d2rloader/" + api.pluginId + "/";
		char              head[256] {};
		std::snprintf(head, sizeof(head), R"("rect": {"x": %d, "y": %d, "width": %d, "height": %d})", x_, y_, Width, Height);
		const std::string button = R"({"type": "ImageWidget", "name": )" + Json(MoldName()) + R"(, "fields": {)" + head + R"(, "filename": )" + Json(folder + look_.moldSprite)
		                         + R"(}, "children": [{"type": "ButtonWidget", "name": )" + Json(ButtonName()) + R"(, "fields": {"rect": {"x": )" + std::to_string(InnerX) + R"(, "y": )"
		                         + std::to_string(InnerY) + R"(}, "filename": )" + Json(folder + look_.buttonSprite)
		                         + R"(, "normalFrame": 0, "pressedFrame": 1, "hoveredFrame": 3, "pressLabelOffset": {"x": 0, "y": 2}, "tooltipString": )" + Json(look_.tooltip)
		                         + R"(, "sound": "cursor_menu_select_hd", "onClickMessage": )" + Command(look_.command) + "}}]}";
		if (layout_ != D2RL::Resources::InvalidHandle && api.resources) (void)api.resources->unregisterResource(api.context, layout_);
		layout_ = D2RL::Resources::InvalidHandle;
		if (!RegisterResource(PanelLayoutPath(id_), button, &layout_)) layout_ = D2RL::Resources::InvalidHandle;
	}

	// The mold in the widget tree under `widget`, at (x, y): a widget of the
	// mold's size with one child at InnerX, InnerY.
	static std::uint8_t* FindMold(std::uint8_t* widget, int x, int y, int depth) {
		if (!widget || depth > 8) return nullptr;
		const auto* rect     = reinterpret_cast<const std::int32_t*>(widget + WidgetRect);
		auto**      children = *reinterpret_cast<std::uint8_t***>(widget + WidgetChildren);
		const auto  count    = *reinterpret_cast<const std::uint64_t*>(widget + WidgetChildCount);
		if (!children || count > 512) return nullptr;
		if (!widget[WidgetFills] && rect[0] == x && rect[1] == y && rect[2] == Width && rect[3] == Height && count == 1 && children[0]) {
			const auto* inner = reinterpret_cast<const std::int32_t*>(children[0] + WidgetRect);
			if (inner[0] == InnerX && inner[1] == InnerY) return widget;
		}
		for (std::uint64_t i = 0; i < count; ++i)
			if (auto* found = FindMold(children[i], x, y, depth + 1)) return found;
		return nullptr;
	}

	Look                                look_;
	std::string                         id_, told_;
	int                                 x_ {}, y_ {}, shownX_ {}, shownY_ {};
	bool                                shownKnown_ {};
	std::atomic<bool>                   moveWanted_ {}, reload_ {};
	D2RL::Resources::RegistrationHandle layout_ = D2RL::Resources::InvalidHandle;
};

} // namespace D2RGui
