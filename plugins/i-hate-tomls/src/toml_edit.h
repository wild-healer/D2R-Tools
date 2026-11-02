// The I Hate TOMLs's editing of a TOML config: one field per key, edited as
// text, then written back into the file at that value's place only.
//
//   boolean                          check box
//   string                           text box (its contents, without quotes)
//   integer, float                   text box (checked when applied)
//   date-time, date, time            text box (checked when applied)
//   array of strings/numbers/...     one text box per item, + and x buttons
//   other arrays, inline tables      one text box with the TOML text
#pragma once

#include "toml_doc.h"
#include <algorithm>

namespace TomlEdit {

using TomlDoc::Type;

enum class Editor { Check, Text, List, Raw, ReadOnly };

struct Field {
	int                      item {};      // index in Document::items
	std::string              key;          // as shown: table.key
	Editor                   editor = Editor::Text;
	Type                     type = Type::String;
	Type                     itemType = Type::String; // List
	bool                     itemTypeKnown {};        // List: false for [] and mixed simple items
	TomlDoc::Style           style = TomlDoc::Style::Basic;
	bool                     escaped {};              // Text string shown with \n etc.
	std::string              text;                    // Check: "true"/"false"
	std::vector<std::string> list;
	std::string              written;                 // the value as first written by Encode
};

inline bool Simple(Type t) { return t != Type::Array && t != Type::InlineTable; }

inline const char* TypeName(Type t) {
	switch (t) {
	case Type::String: return "text";
	case Type::Integer: return "whole number";
	case Type::Float: return "number";
	case Type::Boolean: return "true or false";
	case Type::OffsetDateTime: return "date and time with zone";
	case Type::LocalDateTime: return "date and time";
	case Type::LocalDate: return "date";
	case Type::LocalTime: return "time";
	case Type::Array: return "list";
	default: return "table";
	}
}

// What a text box shows for a simple value.
inline std::string Shown(const TomlDoc::Value& v, bool escaped) {
	if (v.type != Type::String) return v.text;
	return escaped ? TomlDoc::EscapeBasic(v.text, false) : v.text;
}

// One simple value from typed text; false with a reason when it does not fit.
inline bool EncodeSimple(const std::string& typed, Type type, bool known, TomlDoc::Style style, bool escaped, std::string& out, std::string& why) {
	std::string trimmed = typed;
	while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t')) trimmed.erase(trimmed.begin());
	while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t')) trimmed.pop_back();
	Type found = Type::String;
	if (!known) {
		// a new item of an empty list: a number, true/false or a date as such, else text
		if (!trimmed.empty() && TomlDoc::Classify(trimmed, found)) return out = trimmed, true;
		out = TomlDoc::WriteString(typed, TomlDoc::Style::Basic);
		return true;
	}
	if (type == Type::String) {
		std::string text = typed;
		if (escaped && !TomlDoc::Unescape(typed, text)) return why = "a bad \\ escape", false;
		out = TomlDoc::WriteString(text, style);
		return true;
	}
	if (trimmed.empty()) return why = std::string("a ") + TypeName(type) + " is needed", false;
	if (!TomlDoc::Classify(trimmed, found)) return why = "'" + trimmed + "' is not a " + TypeName(type), false;
	if (type == Type::Float && found == Type::Integer && trimmed.find_first_of("xob") == std::string::npos) return out = trimmed + ".0", true;
	if (found != type) return why = "'" + trimmed + "' is not a " + TypeName(type), false;
	out = trimmed;
	return true;
}

// The fields of a document, in file order.
inline std::vector<Field> Fields(const TomlDoc::Document& doc) {
	std::vector<Field> fields;
	std::string        table;
	for (int index = 0; index < static_cast<int>(doc.items.size()); ++index) {
		const auto& item = doc.items[static_cast<std::size_t>(index)];
		if (item.kind == TomlDoc::Kind::Table || item.kind == TomlDoc::Kind::ArrayTable) table = item.text;
		if (item.kind != TomlDoc::Kind::Key) continue;
		const auto& v = item.value;
		Field       f { .item = index, .key = item.text, .type = v.type, .style = v.style };
		if (v.type == Type::Boolean) f.editor = Editor::Check, f.text = v.text;
		else if (Simple(v.type)) {
			f.editor  = Editor::Text;
			f.escaped = v.type == Type::String && TomlDoc::HasControl(v.text);
			f.text    = Shown(v, f.escaped);
		} else if (v.type == Type::Array) {
			bool simple = true, same = true, control = false;
			for (const auto& x : v.items) {
				simple  = simple && Simple(x.type);
				same    = same && x.type == v.items.front().type;
				control = control || (x.type == Type::String && TomlDoc::HasControl(x.text));
			}
			if (simple && !control) {
				f.editor        = Editor::List;
				f.itemTypeKnown = !v.items.empty() && same;
				f.itemType      = v.items.empty() ? Type::String : v.items.front().type;
				for (const auto& x : v.items) f.list.push_back(Shown(x, false));
				// keep each string item's quote style
				if (f.itemTypeKnown && f.itemType == Type::String) f.style = v.items.front().style;
			} else f.editor = Editor::Raw;
		} else f.editor = Editor::Raw;
		if (f.editor == Editor::Raw) {
			f.text = v.text;
			if (v.multiline) f.editor = Editor::ReadOnly;
		}
		fields.push_back(std::move(f));
	}
	return fields;
}

// The value as it will be written, or false with a reason.
inline bool Encode(const TomlDoc::Document& doc, const Field& f, std::string& out, std::string& why) {
	const auto& item = doc.items[static_cast<std::size_t>(f.item)];
	const auto& v    = item.value;
	switch (f.editor) {
	case Editor::Check: out = f.text == "true" ? "true" : "false"; return true;
	case Editor::Text: return EncodeSimple(f.text, f.type, true, f.style, f.escaped, out, why);
	case Editor::ReadOnly: out = doc.bytes.substr(v.start, v.end - v.start); return true;
	case Editor::Raw: {
		TomlDoc::Parser parser(f.text);
		std::size_t     i = 0;
		TomlDoc::Value  parsed;
		if (!parser.ParseValue(i, parsed)) return why = parser.error.substr(parser.error.find(": ") + 2), false;
		parser.SkipSpaces(i);
		if (i != f.text.size()) return why = "unexpected text after the value", false;
		out = f.text;
		return true;
	}
	case Editor::List: {
		std::vector<std::string> items;
		for (const auto& typed : f.list) {
			std::string one;
			if (!EncodeSimple(typed, f.itemType, f.itemTypeKnown, f.style, false, one, why)) return false;
			items.push_back(one);
		}
		const std::string source = doc.bytes.substr(v.start, v.end - v.start);
		if (!v.multiline || items.empty()) {
			const bool padded = source.size() > 2 && source[1] == ' ' && !v.items.empty();
			out               = padded ? "[ " : "[";
			for (std::size_t k = 0; k < items.size(); ++k) out += (k ? ", " : "") + items[k];
			out += padded ? " ]" : "]";
			return true;
		}
		// one item per line, indented as the first item was
		std::string indent = item.indent + "    ";
		if (!v.items.empty()) {
			const std::size_t lineStart = doc.bytes.rfind('\n', v.items.front().start);
			if (lineStart != std::string::npos && lineStart > v.start) {
				const std::string lead = doc.bytes.substr(lineStart + 1, v.items.front().start - lineStart - 1);
				if (lead.find_first_not_of(" \t") == std::string::npos) indent = lead;
			}
		}
		const std::string eol = source.find("\r\n") != std::string::npos ? "\r\n" : "\n";
		out                   = "[" + eol;
		for (std::size_t k = 0; k < items.size(); ++k) out += indent + items[k] + (k + 1 < items.size() || v.trailingComma ? "," : "") + eol;
		out += item.indent + "]";
		return true;
	}
	}
	return false;
}

// Remembers what each field writes now, to tell later which ones changed.
inline void Mark(const TomlDoc::Document& doc, std::vector<Field>& fields) {
	for (auto& f : fields) {
		std::string why;
		if (!Encode(doc, f, f.written, why)) f.written = doc.bytes.substr(doc.items[static_cast<std::size_t>(f.item)].value.start, doc.items[static_cast<std::size_t>(f.item)].value.end - doc.items[static_cast<std::size_t>(f.item)].value.start);
	}
}

// The file with the changed values rewritten; false with "key: reason" when a
// value does not fit. `changed` counts the values rewritten.
inline bool Apply(const TomlDoc::Document& doc, const std::vector<Field>& fields, std::string& out, std::string& why, int& changed) {
	struct Edit {
		std::size_t start, end;
		std::string text;
	};
	std::vector<Edit> edits;
	for (const auto& f : fields) {
		std::string text, reason;
		if (!Encode(doc, f, text, reason)) return why = f.key + ": " + reason, false;
		if (text == f.written) continue;
		const auto& v = doc.items[static_cast<std::size_t>(f.item)].value;
		edits.push_back({ v.start, v.end, text });
	}
	changed = static_cast<int>(edits.size());
	out     = doc.bytes;
	std::sort(edits.begin(), edits.end(), [](const Edit& a, const Edit& b) { return a.start > b.start; });
	for (const auto& e : edits) out.replace(e.start, e.end - e.start, e.text);
	// the result must still read as TOML
	const auto check = TomlDoc::Parse(out);
	if (!check.error.empty()) return why = "the file would not read back (" + check.error + ")", false;
	return true;
}

} // namespace TomlEdit
