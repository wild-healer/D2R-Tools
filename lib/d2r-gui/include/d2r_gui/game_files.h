// D2R GUI: reading the game's own files (art, layouts, fonts) from its
// archive, so a plugin can look like the game without shipping its art.
// The game thread or the UI thread, after the data tables have loaded.
#pragma once
#include "core.h"
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>

namespace D2RGui {

// D2R 3.3.94995 / 3.4.95041: D2RCore ordinal 160 opens a file in the game's
// archive (mods included); the exe's calls give its size, read it, close it.
// Checked against their first bytes before use.
namespace FileApi {
inline constexpr std::uint16_t OpenOrdinal = 160;
inline constexpr std::uint64_t Size = 0xA00050, Read = 0xA00EC0, Close = 0x9FFE50;
inline constexpr std::uint8_t  SizeBytes[] { 0x48, 0x8B, 0x01, 0x48, 0xFF, 0x60, 0x30 };                    // mov rax, [rcx]; jmp [rax+0x30]
inline constexpr std::uint8_t  ReadBytes[] { 0x4C, 0x89, 0x44, 0x24, 0x18, 0x48, 0x89, 0x54, 0x24, 0x10 }; // mov [rsp+18h], r8; mov [rsp+10h], rdx
inline constexpr std::uint8_t  CloseBytes[] { 0xE9 };                                                     // jmp
using OpenFn  = void* (*)(const char* path) noexcept;
using SizeFn  = std::uint64_t (*)(void* file) noexcept;
using ReadFn  = bool (*)(void* file, void* buffer, std::uint64_t size, std::uint64_t* read) noexcept;
using CloseFn = void (*)(void* file) noexcept;
} // namespace FileApi

// The whole file (path like "data/hd/global/ui/panel/closebtn_4x.sprite"),
// or empty when it is not there or cannot be read; `trouble` says why.
inline std::string ArchiveFile(const char* path, std::string* trouble = nullptr) {
	const auto base = ExeBase();
	const auto same = [&](std::uint64_t rva, const std::uint8_t* bytes, std::size_t count) {
		return Readable(base + rva, count) && std::memcmp(reinterpret_cast<const void*>(base + rva), bytes, count) == 0;
	};
	const auto fail = [&](const char* why) {
		if (trouble) *trouble = why;
		return std::string();
	};
	HMODULE core = GetModuleHandleW(L"D2RCore.dll");
	if (!base || !core || !same(FileApi::Size, FileApi::SizeBytes, sizeof(FileApi::SizeBytes)) || !same(FileApi::Read, FileApi::ReadBytes, sizeof(FileApi::ReadBytes))
	    || !same(FileApi::Close, FileApi::CloseBytes, sizeof(FileApi::CloseBytes)))
		return fail("the game's file calls are not where this build expects them");
	const auto open = reinterpret_cast<FileApi::OpenFn>(reinterpret_cast<void*>(GetProcAddress(core, MAKEINTRESOURCEA(FileApi::OpenOrdinal))));
	if (!open) return fail("D2RCore's archive open is missing");
	void* file = open(path);
	if (!file) return fail("the game's files do not have it");
	std::string         bytes;
	const std::uint64_t size = reinterpret_cast<FileApi::SizeFn>(base + FileApi::Size)(file);
	if (size > 0 && size < (64u << 20)) {
		bytes.resize(size);
		std::uint64_t read = 0;
		if (!reinterpret_cast<FileApi::ReadFn>(base + FileApi::Read)(file, bytes.data(), size, &read) || read != size) bytes.clear();
	}
	reinterpret_cast<FileApi::CloseFn>(base + FileApi::Close)(file);
	if (bytes.empty()) return fail("reading it failed");
	return bytes;
}

// The game's own loader for its json files, used when the archive call
// fails: its resource manager (0x9F3540) loads the file (0x9F3DA0, what
// Fog's JSON loader 0xA0ACA0 calls), so the active mod's copy wins. The
// result is a shared pointer {resource, control}; the resource holds {u32
// state (3 = loaded), data at +8, size at +0x10}. D2R 3.3.93787 / 3.3.94995,
// checked before use.
namespace Loader {
inline constexpr std::uint64_t Manager = 0x9F3540, Load = 0x9F3DA0;
inline constexpr std::uint8_t  ManagerBytes[] { 0x48, 0x83, 0xEC, 0x38, 0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00 };
inline constexpr std::uint8_t  LoadBytes[] { 0x40, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B, 0xFA, 0x48, 0x8D, 0x54, 0x24, 0x20 };
struct SharedRef {
	unsigned char* object {};
	unsigned char* control {}; // vtable, strong count at +8, weak count at +0xC
};
// The load request, as Fog's JSON loader builds it on its stack: the path as
// the game's string (text, length, capacity with the "not owned" bit) with its
// inline buffer at +0x18, a byte 0x64 at +0xB8, then two empty callbacks
// (tagged 1) at +0xC0 and +0x108.
struct alignas(16) Request {
	unsigned char bytes[0x180] {};
};
using ManagerFn = void* (*)() noexcept;
using LoadFn    = SharedRef* (*)(void* manager, SharedRef* out, Request* request, int flags) noexcept;
using ReleaseFn = void (*)(void* control) noexcept;
template <typename T>
T At(const unsigned char* at, std::size_t offset) {
	T value {};
	std::memcpy(&value, at + offset, sizeof(T));
	return value;
}
} // namespace Loader

// Game thread. The file through the game's resource loader, or empty
// (`trouble` gets why).
inline std::string LoadedFile(const char* path, std::string* trouble = nullptr) {
	using namespace Loader;
	const auto base = ExeBase();
	const auto fail = [&](const char* why) {
		if (trouble) *trouble += std::string("; game loader: ") + why;
		return std::string();
	};
	if (!base || !Readable(base + Manager, sizeof(ManagerBytes)) || !Readable(base + Load, sizeof(LoadBytes)) || std::memcmp(reinterpret_cast<const void*>(base + Manager), ManagerBytes, sizeof(ManagerBytes)) != 0
	    || std::memcmp(reinterpret_cast<const void*>(base + Load), LoadBytes, sizeof(LoadBytes)) != 0)
		return fail("the game's file loader is not the expected one");
	void* manager = reinterpret_cast<ManagerFn>(base + Manager)();
	if (!manager) return fail("no file manager yet");
	const std::size_t length = std::strlen(path);
	if (length >= 0x9A) return fail("the path is too long");
	Request    request;
	const auto put = [&](std::size_t offset, std::uint64_t value) { std::memcpy(request.bytes + offset, &value, sizeof(value)); };
	std::memcpy(request.bytes + 0x18, path, length);
	put(0, reinterpret_cast<std::uint64_t>(request.bytes + 0x18));
	put(8, length);
	put(0x10, 0x800000000000009AULL);
	request.bytes[0xB8] = 0x64;
	put(0xC0, 1);
	put(0x108, 1);
	SharedRef ref;
	reinterpret_cast<LoadFn>(base + Load)(manager, &ref, &request, 0);
	std::string text;
	if (!ref.object) fail("the loader returned nothing");
	else if (const auto state = At<std::uint32_t>(ref.object, 0); state != 3) fail(("the loader state is " + std::to_string(state)).c_str());
	else {
		const auto* data = At<const char*>(ref.object, 8);
		const auto  size = At<std::uint64_t>(ref.object, 0x10);
		if (data && size > 0 && size < (64u << 20)) text.assign(data, size);
		else fail("the file is empty");
	}
	if (ref.control) {
		auto* strong = reinterpret_cast<volatile long*>(ref.control + 8);
		auto* weak   = reinterpret_cast<volatile long*>(ref.control + 0xC);
		auto* table  = *reinterpret_cast<ReleaseFn* const*>(ref.control);
		if (InterlockedDecrement(strong) == 0) {
			table[2](ref.control);
			if (InterlockedDecrement(weak) == 0) table[3](ref.control);
		}
	}
	return text;
}

// The game's file: from its archive, else through its loader. `trouble`
// says why it could not be read (empty when the archive had it).
inline std::string GameFile(const char* path, std::string* trouble = nullptr) {
	std::string why;
	std::string bytes = ArchiveFile(path, &why);
	if (bytes.empty()) bytes = LoadedFile(path, &why);
	if (trouble) *trouble = bytes.empty() || !why.empty() ? why : std::string();
	return bytes;
}

// ---- small readers for the game's layout JSON (not a JSON parser) ----

// The string after "key": from `from`, if found before `limit`.
inline std::string StringAfter(const std::string& text, std::size_t from, const char* key, std::size_t limit = std::string::npos) {
	const auto at = text.find(std::string("\"") + key + "\"", from);
	if (at == std::string::npos || at > limit) return {};
	const auto open  = text.find('"', text.find(':', at) + 1);
	const auto close = open == std::string::npos ? open : text.find('"', open + 1);
	return close == std::string::npos ? std::string() : text.substr(open + 1, close - open - 1);
}

inline int NumberAfter(const std::string& text, std::size_t from, const char* key, std::size_t limit, int fallback) {
	const auto at = text.find(std::string("\"") + key + "\"", from);
	if (at == std::string::npos || at > limit) return fallback;
	return std::atoi(text.c_str() + text.find(':', at) + 1);
}

// A layout's image name ("PANEL\\closebtn_4x", backslashes single or
// doubled) as the HD sprite file's archive path.
inline std::string SpritePathOf(const std::string& filename) {
	std::string file = "data/hd/global/ui/";
	for (std::size_t k = 0; k < filename.size(); ++k) {
		if (filename[k] == '\\') {
			file += '/';
			if (k + 1 < filename.size() && filename[k + 1] == '\\') ++k;
		} else file += static_cast<char>(std::tolower(static_cast<unsigned char>(filename[k])));
	}
	return file + ".sprite";
}

// A button of the game's: its sprite file and frames.
struct NativeButton {
	std::string file;
	int         normal {}, hovered {}, pressed {};
};

// The lobby's refresh button (a ButtonWidget named like "...Refresh..." in
// the lobby panels' layouts; $names looked up in the HD profile). The
// plugins use it as a "reset to default" button.
inline NativeButton FindRefreshButton() {
	NativeButton      found;
	std::string       trouble;
	const std::string profile = GameFile("data/global/ui/layouts/_profilehd.json", &trouble);
	for (const char* panel : { "joingamepanelhd", "gamelistpanelhd", "ladderpanelhd", "friendslistpanelhd", "consolelobbypanelhd", "createGamepanelhd" }) {
		const std::string path   = std::string("data/global/ui/layouts/") + panel + ".json";
		const std::string layout = GameFile(path.c_str(), &trouble);
		if (layout.empty()) continue;
		for (std::size_t at = 0; (at = layout.find("\"name\"", at)) != std::string::npos; ++at) {
			const auto  value = StringAfter(layout, at, "name", at + 8);
			std::string lower = value;
			for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			if (lower.find("refresh") == std::string::npos) continue;
			const auto  next     = layout.find("\"type\"", at + 8);
			const auto  limit    = next == std::string::npos ? layout.size() : next;
			std::string filename = StringAfter(layout, at, "filename", limit);
			if (!filename.empty() && filename[0] == '$') filename = StringAfter(profile, 0, filename.c_str() + 1, profile.size());
			if (filename.empty()) continue;
			const std::string file = SpritePathOf(filename);
			found.file             = GameFile(file.c_str(), &trouble);
			found.normal           = NumberAfter(layout, at, "normalFrame", limit, 0);
			found.hovered          = NumberAfter(layout, at, "hoveredFrame", limit, found.normal);
			found.pressed          = NumberAfter(layout, at, "pressedFrame", limit, found.normal);
			Log(false, "d2r-gui: refresh button %s in %s: %s (%s), frames %d/%d/%d", value.c_str(), panel, file.c_str(), found.file.empty() ? "not read" : "read", found.normal, found.hovered,
			    found.pressed);
			if (!found.file.empty()) return found;
		}
	}
	Log(false, "d2r-gui: the lobby's refresh button was not found");
	return {};
}

// A number in a layout profile ("SmallFontSize" in _profilehd.json), or `fallback`.
inline int ProfileNumber(const std::string& profileText, const char* key, int fallback, int low = 1, int high = 100000) {
	const auto at = profileText.find(std::string("\"") + key + "\"");
	if (at == std::string::npos) return fallback;
	const int value = std::atoi(profileText.c_str() + profileText.find(':', at) + 1);
	return value >= low && value <= high ? value : fallback;
}

} // namespace D2RGui
