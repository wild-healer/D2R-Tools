// D2R GUI: the plugin's own files for the game (sprites made at run time,
// layouts), registered with the loader's resource service.
#pragma once
#include "core.h"
#include "picture.h"
#include <deque>
#include <string>
#include <vector>

namespace D2RGui {

// How the half-size .lowend copy of a sprite is made.
enum class LowEnd { Box, Bilinear };

// Registers `bytes` at `path` (kept alive for good: the loader copies them,
// but older loaders read them later). `handle` receives the registration,
// to unregister it before registering the path again.
inline bool RegisterResource(const std::string& path, const std::string& bytes, D2RL::Resources::RegistrationHandle* handle = nullptr) {
	static std::deque<std::string> kept;
	const auto&                    api = Api();
	if (!api.resources) return false;
	kept.push_back(bytes);
	const D2RL::Resources::ResourceRegistration resource {
		.structSize = D2RL::Resources::ResourceRegistrationSize,
		.path       = path.c_str(),
		.bytes      = kept.back().data(),
		.byteCount  = kept.back().size(),
	};
	D2RL::Resources::RegistrationHandle ignored = D2RL::Resources::InvalidHandle;
	return api.resources->registerResource(api.context, &resource, handle ? handle : &ignored) == D2RL::Resources::Result::Success;
}

// Where the plugin's sprite `name` is, for a layout's "filename"
// ("d2rloader\\<id>\\<name>", backslashes doubled for JSON when `json`).
inline std::string SpriteName(const std::string& name, bool json = true) {
	const char* slash = json ? "\\\\" : "\\";
	return std::string("d2rloader") + slash + Api().pluginId + slash + name;
}

// A sprite of the plugin's own (frames side by side), and its half-size
// .lowend copy. A sprite registered again under the same name replaces the
// last one (its old registration is dropped first).
inline bool RegisterSprite(const std::string& name, const std::vector<Picture>& frames, LowEnd low = LowEnd::Box) {
	struct Kept {
		std::string                         name;
		D2RL::Resources::RegistrationHandle full = D2RL::Resources::InvalidHandle, half = D2RL::Resources::InvalidHandle;
	};
	static std::vector<Kept> registered;
	const auto&              api = Api();
	if (frames.empty() || frames.front().empty() || !api.resources) return false;
	Kept* slot = nullptr;
	for (auto& k : registered)
		if (k.name == name) slot = &k;
	if (!slot) registered.push_back({ name }), slot = &registered.back();
	for (auto* h : { &slot->full, &slot->half })
		if (*h != D2RL::Resources::InvalidHandle) (void)api.resources->unregisterResource(api.context, *h), *h = D2RL::Resources::InvalidHandle;
	std::vector<Picture> half;
	for (const auto& f : frames) half.push_back(low == LowEnd::Box ? HalfBox(f) : HalfBilinear(f));
	const std::string path = "data/hd/global/ui/d2rloader/" + api.pluginId + "/" + name;
	return RegisterResource(path + ".sprite", WriteSprite(frames), &slot->full) && RegisterResource(path + ".lowend.sprite", WriteSprite(half), &slot->half);
}

} // namespace D2RGui
