// D2R GUI: the plugin's button clicks. Buttons built by layout.h send
// "PanelManager:ClosePanel:<id>/Cmd-<command>" (a message the game ignores);
// Commands::Listen catches it, drops the game's double report of one click,
// and runs the handler on the UI thread with <command>.
#pragma once
#include "core.h"
#include "layout.h"
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <string>

namespace D2RGui::Commands {

using Handler = std::function<void(const std::string& command)>;

using Watcher = std::function<void(const D2RL::SharedEvents::UiMessageEvent& event)>;

struct State {
	Handler                 handler;
	Watcher                 watcher;
	bool                    direct {};
	std::mutex              lock;
	std::deque<std::string> queue;
	std::string             last;
	ULONGLONG               lastMs {};
	bool                    queued {};
};

inline State& Get() {
	static State state;
	return state;
}

inline void Drain() {
	auto& s = Get();
	for (;;) {
		std::string command;
		{
			std::lock_guard guard(s.lock);
			if (s.queue.empty()) {
				s.queued = false;
				return;
			}
			command = std::move(s.queue.front());
			s.queue.pop_front();
		}
		if (s.handler) s.handler(command);
	}
}

// Runs `command` through the handler on the UI thread, as a click would
// (for keys, the console, the wheel).
inline void Post(const std::string& command) {
	auto& s = Get();
	bool  run = false;
	{
		std::lock_guard guard(s.lock);
		s.queue.push_back(command);
		if (s.queue.size() > 64) s.queue.pop_front();
		run = !s.queued;
		s.queued = true;
	}
	if (run && !RunOnUi(Drain)) {
		std::lock_guard guard(s.lock);
		s.queued = false;
	}
}

inline auto OnUiMessage(const D2RL::PluginContext*, const D2RL::SharedEvents::UiMessageEvent* event, void*) noexcept -> D2RL::SharedEvents::UiMessageAction {
	if (event && Get().watcher) Get().watcher(*event);
	if (!event || !event->text || !event->target || std::strcmp(event->target, "PanelManager") != 0) return D2RL::SharedEvents::UiMessageAction::Continue;
	const std::string prefix = CommandPrefix();
	if (std::strncmp(event->text, prefix.c_str(), prefix.size()) != 0) return D2RL::SharedEvents::UiMessageAction::Continue;
	auto& s = Get();
	// D2R reports a button's message twice, about a millisecond apart.
	const ULONGLONG now = GetTickCount64();
	if (s.last == event->text && now - s.lastMs < 150) return D2RL::SharedEvents::UiMessageAction::Consume;
	s.last   = event->text;
	s.lastMs = now;
	if (s.direct) {
		if (s.handler) s.handler(event->text + prefix.size());
	} else Post(event->text + prefix.size());
	return D2RL::SharedEvents::UiMessageAction::Consume;
}

// Every UI message the game sends (the plugin's own commands too), first, on
// the thread that sends it (the game's own panels closing, say). Before Listen.
inline void Watch(Watcher watcher) { Get().watcher = std::move(watcher); }

// While the plugin loads, after Init. `direct`: the handler runs at once on the
// thread that reported the click (the plugin queues it itself), else on the UI
// thread after it.
inline bool Listen(Handler handler, int priority = 100, bool direct = false) {
	Get().handler    = std::move(handler);
	Get().direct     = direct;
	const auto& api = Api();
	if (!api.events) return false;
	const D2RL::SharedEvents::UiMessageListener listener {
		.structSize = D2RL::SharedEvents::UiMessageListenerSize,
		.priority   = priority,
		.callback   = OnUiMessage,
	};
	D2RL::SharedEvents::ListenerHandle handle = D2RL::SharedEvents::InvalidHandle;
	const bool ok = api.events->registerUiMessageListener(api.context, &listener, &handle) == D2RL::SharedEvents::Result::Success;
	if (!ok) Log(true, "d2r-gui: could not listen to button clicks");
	return ok;
}

} // namespace D2RGui::Commands
