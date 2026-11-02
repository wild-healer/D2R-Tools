// I Hate TOMLs window demo: a plugin that has a settings window of its own and
// declares it to the menu, whose Open button then calls it. Here the "window"
// is a console line, enough to see the menu hand over.
#include "../include/i_hate_tomls_client.h"
#include <D2RLPlugin/api.h>

namespace {

const D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = "i-hate-tomls-window-demo",
	.name        = "I Hate TOMLs Window Demo",
	.version     = "1.0.0",
	.author      = "wild-healer",
	.description = "Declares its own settings window to the I Hate TOMLs. Changes nothing in the game.",
	.flags       = D2RL::PluginFlags::Client,
};

const D2RL::PluginContext* Context {};
PluginMenu::Connection     Menu;

void __cdecl OpenWindow(void*) noexcept {
	Context->WriteConsoleMessage("I Hate TOMLs Window Demo: the I Hate TOMLs asked for this plugin's own settings window.");
	Context->LogInfo("own settings window opened from the I Hate TOMLs");
}

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* { return &Info; }

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (!D2RL::HasContext(context) || !context->GetApi()) return false;
	Context = context;
	Context->LogInfo(Menu.StartInterface(context, OpenWindow, nullptr) ? "declared its window to the I Hate TOMLs at load" : "I Hate TOMLs not loaded yet: will declare its window once every plugin has loaded");
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept { Menu.Stop(); }
