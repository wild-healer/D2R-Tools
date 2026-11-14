// D2R GUI: the plugin's own texts, as a string table the layouts name with
// "@<plugin-id>:<key>" (data/local/lng/strings/d2rloader/<id>/<file>.json).
// Each entry has one text per language (the game's 13, in Languages order) or
// one text for all. Register again whenever the texts change: the previous
// table is replaced.
#pragma once
#include "resources.h"
#include "text.h"
#include <iterator>
#include <string>
#include <vector>

namespace D2RGui {

inline constexpr const char* Languages[] { "enUS", "deDE", "esES", "esMX", "frFR", "itIT", "jaJP", "koKR", "plPL", "ptBR", "ruRU", "zhCN", "zhTW" };
inline constexpr int         LanguageCount = static_cast<int>(std::size(Languages));

class StringTable {
public:
	struct Entry {
		std::string              key;
		std::vector<std::string> texts; // LanguageCount texts, or one for every language
	};

	explicit StringTable(std::string file = "view") : file_(std::move(file)) {}

	std::string Path() const { return "data/local/lng/strings/d2rloader/" + Api().pluginId + "/" + file_ + ".json"; }

	// The text key a layout names for entry `key`.
	static std::string Ref(const std::string& key) { return "@" + Api().pluginId + ":" + key; }

	// The table's JSON (an empty text becomes one space: the game shows the key for an empty one).
	static std::string Build(const std::vector<Entry>& entries) {
		std::string json = "[";
		for (const auto& entry : entries) {
			json += std::string(json.size() > 1 ? ",\n" : "\n") + R"({"Key": )" + Json(entry.key);
			for (int l = 0; l < LanguageCount; ++l) {
				const std::string& text = entry.texts.empty() ? std::string() : entry.texts[entry.texts.size() == 1 ? 0 : std::min<std::size_t>(l, entry.texts.size() - 1)];
				json += std::string(", \"") + Languages[l] + "\": " + Json(text.empty() ? " " : text);
			}
			json += "}";
		}
		return json + "\n]";
	}

	// Registers `entries`, replacing the previous table. False when the game refused it.
	bool Register(const std::vector<Entry>& entries) {
		Forget();
		if (RegisterResource(Path(), Build(entries), &handle_)) return true;
		handle_ = D2RL::Resources::InvalidHandle;
		Log(true, "d2r-gui: could not register the texts");
		return false;
	}

	void Forget() {
		const auto& api = Api();
		if (handle_ != D2RL::Resources::InvalidHandle && api.resources) (void)api.resources->unregisterResource(api.context, handle_);
		handle_ = D2RL::Resources::InvalidHandle;
	}

private:
	std::string                         file_;
	D2RL::Resources::RegistrationHandle handle_ = D2RL::Resources::InvalidHandle;
};

} // namespace D2RGui
