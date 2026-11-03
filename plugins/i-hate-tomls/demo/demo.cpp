// I Hate TOMLs demo: a plugin with one setting of each kind, to try the menu.
// Its values live in its config (d2rloader/config/i-hate-tomls-demo.toml) and
// the menu changes them through the client header, like any real plugin.
#include "../include/i_hate_tomls_client.h"
#include <D2RLPlugin/api.h>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

namespace {

const D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = "i-hate-tomls-demo",
	.name        = "I Hate TOMLs Demo",
	.version     = "1.0.0",
	.author      = "wild-healer",
	.description = "One setting of each kind, to try the I Hate TOMLs. Changes nothing in the game.",
	.flags       = D2RL::PluginFlags::Client,
};

constexpr const char* Schema = R"(title = "I Hate TOMLs Demo"
[[setting]]
type = "text"
label = "Every kind of row a plugin can show. Changing them only writes this demo's own config file."
[[setting]]
type = "header"
label = "Switches and choices"
[[setting]]
key = "enabled"
type = "bool"
label = "Enabled"
hint = "An on/off setting: the game's check box."
[[setting]]
key = "position"
type = "enum"
label = "Tag position"
hint = "A choice: a drop-down list."
values = ["before", "after"]
labels = ["Before the name", "After the name"]
[[setting]]
key = "color"
type = "enum"
label = "Colour"
values = ["white", "gray", "gold", "green", "red"]
labels = ["White", "Gray", "Gold", "Green", "Red"]
[[setting]]
type = "header"
label = "Numbers and text"
[[setting]]
key = "count"
type = "int"
label = "Items listed"
min = 0
max = 200
step = 1
shift_step = 5
ctrl_step = 50
note = "Click the arrows: 1 at a time, 5 with Shift, 50 with Ctrl."
[[setting]]
type = "table"
label = "Tag shown on"
note = "One check box per item quality and ground or vendor."
rows = ["ground", "vendor"]
row_labels = ["On the ground", "At vendors"]
columns = ["normal", "magic", "rare", "unique"]
column_labels = ["Normal", "Magic", "Rare", "Unique"]
[[setting]]
key = "scale"
type = "float"
label = "Text scale"
hint = "Applies after a restart."
min = 0.5
max = 2.0
step = 0.1
decimals = 1
restart = true
[[setting]]
key = "text"
type = "string"
label = "Tag text"
hint = "Click, type, Enter."
[[setting]]
key = "button"
type = "position"
label = "Button position"
hint = "X and Y, like Grail On!'s button position."
min = -500
max = 500
default = [0, 0]
[[setting]]
key = "reset"
type = "action"
label = "Reset to defaults"
confirm = true
)";

const D2RL::PluginContext*         Context {};
std::map<std::string, std::string> Values { { "enabled", "true" }, { "position", "\"before\"" }, { "color", "\"gold\"" }, { "count", "3" }, { "scale", "1.0" }, { "text", "\"Eth\"" }, { "button", "[0, 0]" }, { "ground.normal", "true" }, { "ground.magic", "true" }, { "ground.rare", "true" }, { "ground.unique", "true" }, { "vendor.normal", "false" }, { "vendor.magic", "false" }, { "vendor.rare", "true" }, { "vendor.unique", "true" } };
const std::map<std::string, std::string> Defaults = Values;
PluginMenu::Connection             Menu;

void Save() {
	std::string toml = "[i-hate-tomls-demo]\n# Changed in the I Hate TOMLs, or by hand.\n";
	for (const auto& [key, value] : Values) toml += key + " = " + value + "\n";
	if (!Context->WriteConfig(toml.c_str())) Context->LogWarn("could not write the config");
}

void Load() {
	char     text[4096] {};
	uint32_t required = 0;
	if (!Context->ReadConfig(text, sizeof(text), &required)) return;
	std::string rest = text;
	for (std::size_t start = 0; start < rest.size();) {
		auto end = rest.find('\n', start);
		if (end == std::string::npos) end = rest.size();
		const std::string line = rest.substr(start, end - start);
		start                  = end + 1;
		const auto equals      = line.find('=');
		if (equals == std::string::npos || line[0] == '#' || line[0] == '[') continue;
		auto trim = [](std::string s) {
			while (!s.empty() && (s.back() == ' ' || s.back() == '\r')) s.pop_back();
			while (!s.empty() && s.front() == ' ') s.erase(0, 1);
			return s;
		};
		const std::string key = trim(line.substr(0, equals));
		if (Values.count(key)) Values[key] = trim(line.substr(equals + 1));
	}
}

bool __cdecl Get(const char* key, char* out, uint32_t size, void*) noexcept {
	const auto it = Values.find(key ? key : "");
	if (it == Values.end()) return false;
	std::snprintf(out, size, "%s", it->second.c_str());
	return true;
}

bool __cdecl Set(const char* key, const char* value, char* error, uint32_t size, void*) noexcept {
	if (!key || !value) return false;
	if (std::strcmp(key, "reset") == 0) {
		Values = Defaults;
		Save();
		return true;
	}
	if (!Values.count(key)) {
		std::snprintf(error, size, "unknown setting");
		return false;
	}
	if (std::strcmp(key, "text") == 0 && std::strlen(value) > 12) {
		std::snprintf(error, size, "10 characters at most");
		return false;
	}
	Values[key] = value;
	Save();
	char line[256] {};
	std::snprintf(line, sizeof(line), "%s = %s", key, value);
	Context->LogInfo(line);
	return true;
}

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* { return &Info; }

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (!D2RL::HasContext(context) || !context->GetApi()) return false;
	Context = context;
	Load();
	Context->LogInfo(Menu.Start(context, Schema, Get, Set, nullptr) ? "joined the I Hate TOMLs at load" : "I Hate TOMLs not loaded yet: will join once every plugin has loaded");
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept { Menu.Stop(); }
