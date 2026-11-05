// I Hate TOMLs (the I Hate TOMLs) client: lets a D2RLoader plugin list its settings in the Plugin
// Menu (plugin id "i-hate-tomls"), and does nothing when the menu is missing, so
// the plugin keeps working from its TOML alone. Header only, no link
// dependency: copy it into the plugin's sources.
//
// Use, in D2RLoaderLoadPlugin:
//
//   static PluginMenu::Connection Menu;
//   Menu.Start(context, SchemaToml, GetSetting, SetSetting, nullptr);
//
// and Menu.Stop() in D2RLoaderUnloadPlugin. Start registers at once when the
// menu has loaded, else once every plugin has loaded (the first
// DataTablesLoaded event): plugins cannot finish this while they load.
//
// The schema is TOML text, copied by the menu:
//
//   title = "Ethereal Ground Tag"      # optional, else the plugin's name
//   [[setting]]
//   key = "text"                       # passed to get and set
//   type = "string"                    # bool, int, float, enum, string, position, table, action, header, text
//   label = "Tag text"                 # text: the text itself (\n for a new line)
//   hint = "Shown when the mouse is over the label"
//   note = "Shown under the row, like a TOML comment"
//   values = ["before", "after"]       # enum (a drop-down list): the TOML values, in order
//   labels = ["Before", "After"]       # enum: what the menu shows (optional)
//   min = 0  max = 100  step = 1  decimals = 0   # int, float, position (one per line)
//   shift_step = 10  ctrl_step = 100   # int, float, position: Shift+click, Ctrl+click
//                                      # (default 10 and 100 steps)
//   default = [0, 0]                   # position: its reset button sets this
//   restart = true                     # marked "applies after a restart"
//   confirm = true                     # action: asks for a second click
//
// A position's value is a two-number array, [x, y]; get and set use it so.
//
// A table is a grid of check boxes, one per row and column; it needs no key.
// Each box is the bool "<row>.<column>" for get and set:
//
//   [[setting]]
//   type = "table"
//   label = "Auras on"                 # optional title over the table
//   rows = ["on_self", "on_merc"]
//   row_labels = ["You", "Your merc"]  # optional, else the row names
//   columns = ["self", "merc"]
//   column_labels = ["From you", "From your merc"]
//   -> get("on_self.merc") / set("on_self.merc", "true")
//
// A plugin with a settings window of its own declares it instead, and the
// menu's Open button then opens that window:
//
//   Menu.StartInterface(context, OpenMyWindow, nullptr);
//
// open(user) runs on the game's main thread, after the menu has closed.
//
// Plugins that do neither are still listed; when they have a TOML config the
// menu shows that file, edited as text and saved with an Apply button.
//
// get(key) writes the current value as a TOML value: true, 3, 0.25, "Eth".
// set(key, value) gets a TOML value the same way, applies and saves it, and
// returns false with a short reason in error when it refuses. The menu calls
// both on the game's main thread. An action's set gets the value "run".
#pragma once

#include <D2RLPlugin/api.h>
#include <cstdint>

namespace PluginMenu {

inline constexpr const char* MenuPluginId = "i-hate-tomls";
inline constexpr const char* ServiceName  = "settings-menu";

using GetFn = bool(__cdecl*)(const char* key, char* out, uint32_t outSize, void* user) noexcept;
using SetFn = bool(__cdecl*)(const char* key, const char* tomlValue, char* error, uint32_t errorSize, void* user) noexcept;

struct ClientInfo {
	uint32_t    structSize;
	uint32_t    flags;
	const char* schemaToml;
	GetFn       get;
	SetFn       set;
	void*       user;
};

using OpenFn = void(__cdecl*)(void* user) noexcept;

struct InterfaceInfo {
	uint32_t structSize;
	uint32_t flags;
	OpenFn   open;
	void*    user;
};

// "settings-menu" version 1. New fields go at the end.
struct Service {
	uint32_t serviceSize;
	uint32_t serviceVersion;
	uint64_t (__cdecl* registerClient)(const D2RL::PluginContext* caller, const ClientInfo* client) noexcept;
	void (__cdecl* unregisterClient)(uint64_t handle) noexcept;
	// The plugin changed a setting itself (key, or null for all): the menu redraws if it shows them.
	void (__cdecl* valueChanged)(uint64_t handle, const char* key) noexcept;
	// Opens the menu on this plugin's page.
	void (__cdecl* open)(uint64_t handle) noexcept;
	// The plugin has its own settings window: the menu's Open button calls open.
	uint64_t (__cdecl* registerInterface)(const D2RL::PluginContext* caller, const InterfaceInfo* info) noexcept;
};

class Connection {
  public:
	bool Start(const D2RL::PluginContext* context, const char* schemaToml, GetFn get, SetFn set, void* user) noexcept {
		info_ = { sizeof(ClientInfo), 0, schemaToml, get, set, user };
		return Begin(context);
	}

	// For a plugin with its own settings window: the menu lists it with an
	// Open button that calls open(user).
	bool StartInterface(const D2RL::PluginContext* context, OpenFn open, void* user) noexcept {
		interface_ = { sizeof(InterfaceInfo), 0, open, user };
		return Begin(context);
	}

	// True once the menu has the plugin.
	bool Registered() const noexcept { return handle_ != 0; }

	void ValueChanged(const char* key = nullptr) const noexcept {
		if (handle_ && lease_ && !interface_.open) lease_->valueChanged(handle_, key);
	}

	void Open() const noexcept {
		if (handle_ && lease_ && !interface_.open) lease_->open(handle_);
	}

	void Stop() noexcept {
		if (handle_ && lease_) lease_->unregisterClient(handle_);
		handle_ = 0;
		(void)lease_.Reset();
	}

  private:
	bool Begin(const D2RL::PluginContext* context) noexcept {
		context_ = context;
		if (!context || context->QueryService(&comm_) != D2RL::ServiceQueryResult::Success
		    || !D2RL::HasPluginCommunicationServiceField(comm_, D2RL::PluginCommunicationServiceRequiredSize)) {
			comm_ = nullptr;
			return false;
		}
		if (TryRegister()) return true;
		const D2RL::LifecycleService* lifecycle = nullptr;
		if (context->QueryService(&lifecycle) == D2RL::ServiceQueryResult::Success && lifecycle) {
			const D2RL::Lifecycle::DataTablesLoadedListener listener { sizeof(D2RL::Lifecycle::DataTablesLoadedListener), 0, &Connection::AfterLoad, this };
			D2RL::Lifecycle::ListenerHandle handle {};
			(void)lifecycle->registerDataTablesLoadedListener(context, &listener, &handle);
		}
		return false;
	}

	bool TryRegister() noexcept {
		if (handle_) return true;
		if (!comm_ || D2RL::PluginCommunication::Acquire(context_, comm_, MenuPluginId, ServiceName, 1, sizeof(Service), &lease_) != D2RL::PluginCommunication::Result::Success)
			return false;
		if (interface_.open) handle_ = lease_->serviceSize >= sizeof(Service) ? lease_->registerInterface(context_, &interface_) : 0;
		else handle_ = lease_->registerClient(context_, &info_);
		if (!handle_) (void)lease_.Reset();
		return handle_ != 0;
	}

	static void __cdecl AfterLoad(const D2RL::PluginContext*, const D2RL::Lifecycle::DataTablesLoadedEvent*, void* self) noexcept {
		auto* connection = static_cast<Connection*>(self);
		if (connection->tried_) return;
		connection->tried_ = true;
		(void)connection->TryRegister();
	}

	const D2RL::PluginContext*                       context_ {};
	const D2RL::PluginCommunicationService*          comm_ {};
	D2RL::PluginCommunication::ServiceLease<Service> lease_;
	ClientInfo                                       info_ {};
	InterfaceInfo                                    interface_ {};
	uint64_t                                         handle_ {};
	bool                                             tried_ {};
};

} // namespace PluginMenu
