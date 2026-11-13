// D2R GUI: the services every part uses, set once by D2RGui::Init while the
// plugin loads. Header-only: each plugin compiles its own copy into its DLL,
// so plugins stay self-contained and never share state at run time.
#pragma once
#include <D2RLPlugin/api.h>
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <functional>
#include <string>

#define D2R_GUI_VERSION "0.0.1"

namespace D2RGui {

struct Services {
	const D2RL::PluginContext*      context {};
	const D2RL::ThreadService*      threads {};
	const D2RL::PanelService*       panels {};
	const D2RL::WidgetService*      widgets {};
	const D2RL::ResourceService*    resources {};
	const D2RL::SharedEventService* events {};
	std::string                     pluginId; // "my-plugin": sprite folder, panel names, command prefix
	bool                            lowEndBilinear {}; // .lowend sprites halved bilinear (I Hate TOMLs) instead of 2x2 average (Grail On!)
};

inline Services& Api() {
	static Services services;
	return services;
}

inline void Log(bool warn, const char* format, ...) noexcept {
	const auto* context = Api().context;
	if (!context) return;
	char    message[1024] {};
	va_list args;
	va_start(args, format);
	std::vsnprintf(message, sizeof(message), format, args);
	va_end(args);
	if (warn) context->LogWarn(message);
	else context->LogInfo(message);
}

namespace Detail {
template <typename T>
const T* Query(const D2RL::PluginContext* context, const char* name) {
	const T* service = nullptr;
	if (context->QueryService(&service) != D2RL::ServiceQueryResult::Success || !service) {
		Log(true, "d2r-gui: %s service unavailable", name);
		return nullptr;
	}
	return service;
}
} // namespace Detail

// While the plugin loads (D2RLoaderLoadPlugin). `pluginId` must be the
// plugin's id: sprites go to data/hd/global/ui/d2rloader/<id>/, panels are
// named <id>/<name>, buttons send PanelManager:ClosePanel:<id>/Cmd-<command>.
// False when a service the windows need is missing (the plugin should stay
// without its window then, not fail to load).
inline bool Init(const D2RL::PluginContext* context, const char* pluginId) {
	auto& api     = Api();
	api.context   = context;
	api.pluginId  = pluginId ? pluginId : "";
	api.threads   = Detail::Query<D2RL::ThreadService>(context, "thread");
	api.panels    = Detail::Query<D2RL::PanelService>(context, "panel");
	api.widgets   = Detail::Query<D2RL::WidgetService>(context, "widget");
	api.resources = Detail::Query<D2RL::ResourceService>(context, "resource");
	api.events    = Detail::Query<D2RL::SharedEventService>(context, "shared event");
	if (api.threads && !D2RL::HasThreadServiceField(api.threads, D2RL::ThreadServiceRequiredSize)) api.threads = nullptr;
	if (api.widgets && !D2RL::HasWidgetServiceField(api.widgets, D2RL::WidgetServiceRequiredSize)) api.widgets = nullptr;
	return api.threads && api.panels && api.resources && api.events;
}

// `fn` on the UI thread (next frame). False when it could not be queued.
inline bool RunOnUi(std::function<void()> fn) {
	const auto& api = Api();
	if (!api.threads) return false;
	auto* work = new std::function<void()>(std::move(fn));
	const auto run = [](const D2RL::PluginContext*, void* data) noexcept {
		auto* f = static_cast<std::function<void()>*>(data);
		(*f)();
		delete f;
	};
	if (api.threads->runOnUiThread(api.context, run, work) == D2RL::Threads::Result::Success) return true;
	delete work;
	return false;
}

// The game's own module base (exe), 0 before Init.
inline std::uintptr_t ExeBase() { return Api().context ? static_cast<std::uintptr_t>(Api().context->exeBase) : 0; }

// Whether `size` bytes at `at` can be read (for checking code bytes before calling into the game).
inline bool Readable(std::uintptr_t at, std::size_t size) {
	MEMORY_BASIC_INFORMATION info {};
	if (!at || !VirtualQuery(reinterpret_cast<const void*>(at), &info, sizeof(info)) || info.State != MEM_COMMIT) return false;
	if (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
	return at + size <= reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
}

} // namespace D2RGui
