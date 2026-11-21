// D2R GUI: the game's own screen around the plugin's window.
//
// HideLifeNumber: the number over the life globe (HUDPanel/HealthBall/
// ValueDisplay). The game hides it under its own left panels; a plugin's left
// window should hide it too while open (call it every tick: the game may show
// it again), and show it once closed.
//
// StashShown: whether the game's stash panel (id 0x18) is open, on the builds
// whose code is known (checked once; false on others).
#pragma once
#include "core.h"
#include <cstdint>
#include <cstring>

namespace D2RGui::Hud {

namespace Detail {
inline bool& LifeHidden() {
	static bool hidden {};
	return hidden;
}
} // namespace Detail

// UI thread.
inline void HideLifeNumber(bool hide) {
	const auto& api = Api();
	if (!api.widgets || (!hide && !Detail::LifeHidden())) return;
	D2RL::Widgets::WidgetHandle hud {}, ball {}, text {};
	if (api.widgets->findPanel(api.context, "HUDPanel", &hud) != D2RL::Widgets::Result::Success
	    || api.widgets->findWidget(api.context, hud, "HealthBall", &ball) != D2RL::Widgets::Result::Success
	    || api.widgets->findWidget(api.context, ball, "ValueDisplay", &text) != D2RL::Widgets::Result::Success) {
		static bool told = false;
		if (!told) Log(false, "d2r-gui: the life globe's number was not found");
		told = true;
		return;
	}
	(void)api.widgets->setWidgetVisible(api.context, text, !hide);
	Detail::LifeHidden() = hide;
}

inline constexpr std::uint64_t StashPanelRva = 0x846190;
inline constexpr std::uint8_t  StashPanelCode[] {
	0x48, 0x83, 0xEC, 0x28, 0x48, 0x83, 0x3D, 0xD4, 0x9F, 0xBF, 0x02, 0x00, 0x74, 0x18, 0xE8, 0xFD,
	0x18, 0x88, 0xFF, 0x48, 0x8B, 0x0D, 0xC6, 0x9F, 0xBF, 0x02, 0x48, 0x8B, 0xD0, 0x48, 0x83, 0xC4,
	0x28, 0xE9, 0xAA, 0x95, 0x05, 0x00, 0x33, 0xC0, 0x48, 0x83, 0xC4, 0x28, 0xC3,
};

// Whether this game build's stash panel code is the known one (checked once).
inline bool StashKnown() {
	static const bool known = [] {
		const auto base = ExeBase();
		return base && Readable(base + StashPanelRva, sizeof(StashPanelCode)) && std::memcmp(reinterpret_cast<const void*>(base + StashPanelRva), StashPanelCode, sizeof(StashPanelCode)) == 0;
	}();
	return known;
}

inline bool StashShown() {
	if (!StashKnown()) return false;
	using PanelFn     = void*(__cdecl*)(std::uint32_t);
	const auto* panel = static_cast<const std::uint8_t*>(reinterpret_cast<PanelFn>(ExeBase() + StashPanelRva)(0x18));
	return panel && panel[0x51];
}

} // namespace D2RGui::Hud
