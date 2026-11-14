// D2R GUI: a lenient JSON reader for the game's layout files (comments,
// trailing commas, true/false/null read as nothing).
#pragma once
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace D2RGui {

struct JsonValue {
	enum Kind { Null, Number, String, Array, Object } kind = Null;
	double                                         number {};
	std::string                                    text;
	std::vector<JsonValue>                         items;
	std::vector<std::pair<std::string, JsonValue>> fields;
	const JsonValue* get(const std::string& key) const {
		for (const auto& [name, value] : fields)
			if (name == key) return &value;
		return nullptr;
	}
};

struct JsonReader {
	const std::string& s;
	std::size_t        at = 0;
	void               skip() {
        for (;;) {
            while (at < s.size() && std::isspace(static_cast<unsigned char>(s[at]))) ++at;
            if (s.compare(at, 2, "//") == 0) at = s.find('\n', at) == std::string::npos ? s.size() : s.find('\n', at);
            else if (s.compare(at, 2, "/*") == 0) at = s.find("*/", at) == std::string::npos ? s.size() : s.find("*/", at) + 2;
            else return;
        }
	}
	std::string str() {
		std::string out;
		for (++at; at < s.size() && s[at] != '"'; ++at) out += s[at] == '\\' && at + 1 < s.size() ? s[++at] : s[at];
		++at;
		return out;
	}
	JsonValue value(int depth = 0) {
		JsonValue v;
		skip();
		if (at >= s.size() || depth > 64) return v;
		const char c = s[at];
		if (c == '{' || c == '[') {
			v.kind = c == '{' ? JsonValue::Object : JsonValue::Array;
			++at;
			for (;;) {
				skip();
				if (at >= s.size()) return v;
				if (s[at] == '}' || s[at] == ']') {
					++at;
					return v;
				}
				if (s[at] == ',') {
					++at;
					continue;
				}
				if (v.kind == JsonValue::Object) {
					std::string key = s[at] == '"' ? str() : std::string();
					skip();
					if (at < s.size() && s[at] == ':') ++at;
					v.fields.emplace_back(std::move(key), value(depth + 1));
				} else v.items.push_back(value(depth + 1));
			}
		}
		if (c == '"') {
			v.kind = JsonValue::String;
			v.text = str();
			return v;
		}
		char* end = nullptr;
		v.number  = std::strtod(s.c_str() + at, &end);
		if (end && end != s.c_str() + at) {
			v.kind = JsonValue::Number;
			at     = static_cast<std::size_t>(end - s.c_str());
		} else
			while (at < s.size() && !std::strchr(",]}", s[at])) ++at; // true, false, null
		return v;
	}
};

// The whole text read.
inline JsonValue ReadJson(const std::string& text) {
	JsonReader reader { text };
	return reader.value();
}

} // namespace D2RGui
