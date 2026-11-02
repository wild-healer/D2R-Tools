// A TOML reader for the I Hate TOMLs's config pages. It keeps where each value
// sits in the file, so a change rewrites that value only and every comment,
// blank line and spacing stays as it was.
//
// Value types (TOML 1.0, plus 1.1's times without seconds): string (basic,
// literal, multi-line basic, multi-line literal), integer (decimal, 0x, 0o,
// 0b), float (incl. inf, nan), boolean, offset date-time, local date-time,
// local date, local time, array, inline table. Lines: comments, [tables],
// [[arrays of tables]], key = value with bare, quoted or dotted keys.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace TomlDoc {

enum class Type { String, Integer, Float, Boolean, OffsetDateTime, LocalDateTime, LocalDate, LocalTime, Array, InlineTable };
enum class Style { Basic, Literal, MultiBasic, MultiLiteral };

struct Value {
	Type               type = Type::String;
	Style              style = Style::Basic; // strings
	std::size_t        start {}, end {};     // the value's bytes in the file
	std::string        text;                 // a string's contents, else the value as written
	std::vector<Value> items;                // array items, inline table values
	bool               multiline {};         // spans lines
	bool               comments {};          // an array with comments inside
	bool               trailingComma {};
};

enum class Kind { Comment, Blank, Table, ArrayTable, Key };

struct Item {
	Kind        kind = Kind::Blank;
	std::string text;    // comment text (after '#'), table name, key as written
	std::string comment; // a comment after a table or value on the same line
	std::string indent;  // the line's leading spaces
	Value       value;   // Key
	int         line {}; // 1-based
};

struct Document {
	std::string       bytes;
	std::vector<Item> items;
	std::string       error; // set when the file is not valid TOML
};

namespace detail {

inline bool Digit(char c) { return c >= '0' && c <= '9'; }
inline bool Hex(char c) { return Digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
inline bool BareKeyChar(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || Digit(c) || c == '_' || c == '-'; }
inline bool Space(char c) { return c == ' ' || c == '\t'; }

inline void Utf8(std::string& out, std::uint32_t code) {
	if (code < 0x80) out += static_cast<char>(code);
	else if (code < 0x800) out += static_cast<char>(0xC0 | (code >> 6)), out += static_cast<char>(0x80 | (code & 0x3F));
	else if (code < 0x10000)
		out += static_cast<char>(0xE0 | (code >> 12)), out += static_cast<char>(0x80 | ((code >> 6) & 0x3F)), out += static_cast<char>(0x80 | (code & 0x3F));
	else
		out += static_cast<char>(0xF0 | (code >> 18)), out += static_cast<char>(0x80 | ((code >> 12) & 0x3F)), out += static_cast<char>(0x80 | ((code >> 6) & 0x3F)),
		    out += static_cast<char>(0x80 | (code & 0x3F));
}

// One escape after a backslash (s[i] is the character after it); false when invalid.
inline bool Escape(std::string_view s, std::size_t& i, std::string& out) {
	if (i >= s.size()) return false;
	const char c = s[i++];
	switch (c) {
	case 'b': out += '\b'; return true;
	case 't': out += '\t'; return true;
	case 'n': out += '\n'; return true;
	case 'f': out += '\f'; return true;
	case 'r': out += '\r'; return true;
	case 'e': out += '\x1B'; return true;
	case '"': out += '"'; return true;
	case '\\': out += '\\'; return true;
	case 'x':
	case 'u':
	case 'U': {
		const std::size_t count = c == 'x' ? 2 : c == 'u' ? 4 : 8;
		if (i + count > s.size()) return false;
		std::uint32_t code = 0;
		for (std::size_t k = 0; k < count; ++k) {
			const char h = s[i + k];
			if (!Hex(h)) return false;
			code = code * 16 + static_cast<std::uint32_t>(Digit(h) ? h - '0' : (h | 0x20) - 'a' + 10);
		}
		i += count;
		if (code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) return false;
		Utf8(out, code);
		return true;
	}
	default: return false;
	}
}

inline bool AllDigits(std::string_view s, std::size_t from, std::size_t count) {
	if (from + count > s.size()) return false;
	for (std::size_t k = 0; k < count; ++k)
		if (!Digit(s[from + k])) return false;
	return true;
}

// hh:mm[:ss[.frac]]
inline bool Time(std::string_view s) {
	if (s.size() < 5 || !AllDigits(s, 0, 2) || s[2] != ':' || !AllDigits(s, 3, 2)) return false;
	if (s.size() == 5) return true;
	if (s.size() < 8 || s[5] != ':' || !AllDigits(s, 6, 2)) return false;
	if (s.size() == 8) return true;
	if (s[8] != '.' || s.size() == 9) return false;
	return AllDigits(s, 9, s.size() - 9);
}

inline bool Date(std::string_view s) { return s.size() == 10 && AllDigits(s, 0, 4) && s[4] == '-' && AllDigits(s, 5, 2) && s[7] == '-' && AllDigits(s, 8, 2); }

// Digits with single underscores between them.
inline bool Digits(std::string_view s, bool (*ok)(char)) {
	if (s.empty() || !ok(s.front()) || !ok(s.back())) return false;
	for (std::size_t k = 0; k < s.size(); ++k)
		if (s[k] == '_') {
			if (!ok(s[k - 1]) || !ok(s[k + 1])) return false;
		} else if (!ok(s[k])) return false;
	return true;
}

} // namespace detail

// What a bare value is, or false when it is none.
inline bool Classify(std::string_view s, Type& type) {
	using namespace detail;
	if (s == "true" || s == "false") return type = Type::Boolean, true;
	if (s.size() >= 10 && Date(s.substr(0, 10))) {
		if (s.size() == 10) return type = Type::LocalDate, true;
		const char sep = s[10];
		if (sep != 'T' && sep != 't' && sep != ' ') return false;
		std::string_view rest = s.substr(11);
		bool             offset = false;
		if (!rest.empty() && (rest.back() == 'Z' || rest.back() == 'z')) rest.remove_suffix(1), offset = true;
		else if (rest.size() > 6 && (rest[rest.size() - 6] == '+' || rest[rest.size() - 6] == '-') && Time(rest.substr(rest.size() - 5)) && rest.size() - 5 >= 5) {
			const std::string_view zone = rest.substr(rest.size() - 5);
			if (zone.size() != 5) return false;
			rest.remove_suffix(6), offset = true;
		}
		if (!Time(rest)) return false;
		return type = offset ? Type::OffsetDateTime : Type::LocalDateTime, true;
	}
	if (Time(s)) return type = Type::LocalTime, true;
	std::string_view body = s;
	const bool       sign = !body.empty() && (body[0] == '+' || body[0] == '-');
	if (sign) body.remove_prefix(1);
	if (body == "inf" || body == "nan") return type = Type::Float, true;
	if (!sign && body.size() > 2 && body[0] == '0' && (body[1] == 'x' || body[1] == 'o' || body[1] == 'b')) {
		const auto sub = body.substr(2);
		const bool ok  = body[1] == 'x' ? Digits(sub, Hex) : body[1] == 'o' ? Digits(sub, [](char c) { return c >= '0' && c <= '7'; }) : Digits(sub, [](char c) { return c == '0' || c == '1'; });
		return ok ? (type = Type::Integer, true) : false;
	}
	// decimal: int [. frac] [e exp]
	std::size_t dot = body.find('.'), exp = body.find_first_of("eE");
	const auto  whole = body.substr(0, std::min(dot, exp));
	if (!Digits(whole, Digit) || (whole.size() > 1 && whole[0] == '0')) return false;
	if (dot == std::string_view::npos && exp == std::string_view::npos) return type = Type::Integer, true;
	if (dot != std::string_view::npos) {
		if (exp != std::string_view::npos && exp < dot) return false;
		if (!Digits(body.substr(dot + 1, exp == std::string_view::npos ? std::string_view::npos : exp - dot - 1), Digit)) return false;
	}
	if (exp != std::string_view::npos) {
		auto e = body.substr(exp + 1);
		if (!e.empty() && (e[0] == '+' || e[0] == '-')) e.remove_prefix(1);
		if (!Digits(e, Digit)) return false;
	}
	return type = Type::Float, true;
}

class Parser {
  public:
	explicit Parser(const std::string& s) : s_(s) {}

	bool ParseValue(std::size_t& i, Value& v) {
		using namespace detail;
		v       = {};
		v.start = i;
		if (i >= s_.size()) return Fail(i, "a value is missing");
		const char c = s_[i];
		if (c == '"' || c == '\'') {
			const bool multi = s_.compare(i, 3, c == '"' ? "\"\"\"" : "'''") == 0;
			v.type           = Type::String;
			v.style          = c == '"' ? (multi ? Style::MultiBasic : Style::Basic) : (multi ? Style::MultiLiteral : Style::Literal);
			if (!(multi ? Multi(i, c, v.text) : Single(i, c, v.text))) return false;
		} else if (c == '[') {
			v.type = Type::Array;
			++i;
			while (true) {
				if (!Skip(i, v)) return false;
				if (i < s_.size() && s_[i] == ']') {
					++i;
					break;
				}
				Value item;
				if (!ParseValue(i, item)) return false;
				v.items.push_back(std::move(item));
				if (!Skip(i, v)) return false;
				if (i < s_.size() && s_[i] == ',') {
					++i;
					std::size_t peek = i;
					Value       probe;
					if (!Skip(peek, probe)) return false;
					if (peek < s_.size() && s_[peek] == ']') v.trailingComma = true;
					continue;
				}
				if (i < s_.size() && s_[i] == ']') {
					++i;
					break;
				}
				return Fail(i, "',' or ']' expected in an array");
			}
		} else if (c == '{') {
			v.type = Type::InlineTable;
			++i;
			SkipSpaces(i);
			if (i < s_.size() && s_[i] == '}') ++i;
			else
				while (true) {
					SkipSpaces(i);
					std::string key;
					if (!ParseKey(i, key)) return false;
					SkipSpaces(i);
					if (i >= s_.size() || s_[i] != '=') return Fail(i, "'=' expected in an inline table");
					++i;
					SkipSpaces(i);
					Value item;
					if (!ParseValue(i, item)) return false;
					item.text = key + " = " + s_.substr(item.start, item.end - item.start);
					v.items.push_back(std::move(item));
					SkipSpaces(i);
					if (i < s_.size() && s_[i] == ',') {
						++i;
						continue;
					}
					if (i < s_.size() && s_[i] == '}') {
						++i;
						break;
					}
					return Fail(i, "',' or '}' expected in an inline table");
				}
		} else {
			const std::size_t from = i;
			while (i < s_.size() && (BareKeyChar(s_[i]) || s_[i] == '+' || s_[i] == '.' || s_[i] == ':')) ++i;
			// a date and a time written with a space between them
			if (i - from == 10 && Date(std::string_view(s_).substr(from, 10)) && i + 3 < s_.size() && s_[i] == ' ' && Digit(s_[i + 1]) && Digit(s_[i + 2]) && s_[i + 3] == ':') {
				++i;
				while (i < s_.size() && (BareKeyChar(s_[i]) || s_[i] == '+' || s_[i] == '.' || s_[i] == ':')) ++i;
			}
			v.text = s_.substr(from, i - from);
			if (v.text.empty()) return Fail(i, "a value is missing");
			if (!Classify(v.text, v.type)) return Fail(from, "'" + v.text + "' is not a TOML value");
		}
		v.end       = i;
		v.multiline = s_.find('\n', v.start) < v.end;
		if (v.type != Type::String) {
			if (v.type == Type::Array || v.type == Type::InlineTable) v.text = s_.substr(v.start, v.end - v.start);
		}
		return true;
	}

	// key, dotted key; the key as written, its parts' spacing trimmed.
	bool ParseKey(std::size_t& i, std::string& key) {
		using namespace detail;
		const std::size_t from = i;
		while (true) {
			SkipSpaces(i);
			if (i >= s_.size()) return Fail(i, "a key is missing");
			if (s_[i] == '"' || s_[i] == '\'') {
				std::string ignored;
				if (!Single(i, s_[i], ignored)) return false;
			} else {
				const std::size_t start = i;
				while (i < s_.size() && BareKeyChar(s_[i])) ++i;
				if (i == start) return Fail(i, "a key is missing");
			}
			std::size_t peek = i;
			SkipSpaces(peek);
			if (peek < s_.size() && s_[peek] == '.') {
				i = peek + 1;
				continue;
			}
			break;
		}
		key = s_.substr(from, i - from);
		while (!key.empty() && Space(key.front())) key.erase(key.begin());
		return true;
	}

	void SkipSpaces(std::size_t& i) const {
		while (i < s_.size() && detail::Space(s_[i])) ++i;
	}

	int LineOf(std::size_t at) const {
		int line = 1;
		for (std::size_t k = 0; k < at && k < s_.size(); ++k)
			if (s_[k] == '\n') ++line;
		return line;
	}

	bool Fail(std::size_t at, const std::string& why) {
		if (error.empty()) error = "line " + std::to_string(LineOf(at)) + ": " + why;
		return false;
	}

	std::string error;

  private:
	// Spaces, newlines and comments inside an array.
	bool Skip(std::size_t& i, Value& array) {
		while (i < s_.size()) {
			const char c = s_[i];
			if (detail::Space(c) || c == '\n' || c == '\r') ++i;
			else if (c == '#') {
				array.comments = true;
				while (i < s_.size() && s_[i] != '\n') ++i;
			} else break;
		}
		if (i >= s_.size()) return Fail(i, "the array is not closed");
		return true;
	}

	bool Single(std::size_t& i, char quote, std::string& out) {
		++i;
		while (i < s_.size()) {
			const char c = s_[i];
			if (c == quote) {
				++i;
				return true;
			}
			if (c == '\n') break;
			if (c == '\\' && quote == '"') {
				++i;
				if (!detail::Escape(s_, i, out)) return Fail(i, "a bad escape in a string");
				continue;
			}
			out += c;
			++i;
		}
		return Fail(i, "a string is not closed on its line");
	}

	bool Multi(std::size_t& i, char quote, std::string& out) {
		i += 3;
		if (s_.compare(i, 2, "\r\n") == 0) i += 2;
		else if (i < s_.size() && s_[i] == '\n') ++i;
		while (i < s_.size()) {
			const char c = s_[i];
			if (c == quote && s_.compare(i, 3, std::string(3, quote)) == 0) {
				std::size_t run = 0;
				while (i + run < s_.size() && s_[i + run] == quote && run < 5) ++run;
				out.append(run - 3, quote);
				i += run;
				return true;
			}
			if (c == '\\' && quote == '"') {
				// a backslash at the end of a line joins the next one
				std::size_t k = i + 1;
				while (k < s_.size() && detail::Space(s_[k])) ++k;
				if (k < s_.size() && (s_[k] == '\n' || s_[k] == '\r')) {
					while (k < s_.size() && (detail::Space(s_[k]) || s_[k] == '\n' || s_[k] == '\r')) ++k;
					i = k;
					continue;
				}
				++i;
				if (!detail::Escape(s_, i, out)) return Fail(i, "a bad escape in a string");
				continue;
			}
			out += c;
			++i;
		}
		return Fail(i, "a multi-line string is not closed");
	}

	const std::string& s_;
};

inline Document Parse(std::string bytes) {
	Document doc;
	doc.bytes             = std::move(bytes);
	const std::string& s  = doc.bytes;
	Parser             parser(s);
	std::size_t        i  = s.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
	int                line = 1;
	const auto         eol  = [&](std::size_t& k, Item& item) {
        parser.SkipSpaces(k);
        if (k < s.size() && s[k] == '#') {
            const std::size_t from = k + 1;
            while (k < s.size() && s[k] != '\n') ++k;
            item.comment = s.substr(from, k - from);
            while (!item.comment.empty() && (item.comment.back() == '\r' || detail::Space(item.comment.back()))) item.comment.pop_back();
            while (!item.comment.empty() && detail::Space(item.comment.front())) item.comment.erase(item.comment.begin());
        }
        if (k < s.size() && s[k] == '\r') ++k;
        if (k < s.size() && s[k] != '\n') return parser.Fail(k, "unexpected text after the value");
        if (k < s.size()) ++k;
        return true;
	};
	while (i < s.size()) {
		Item              item;
		const std::size_t start = i;
		item.line               = parser.LineOf(start);
		parser.SkipSpaces(i);
		item.indent = s.substr(start, i - start);
		if (i >= s.size()) break;
		const char c = s[i];
		if (c == '\n' || c == '\r') {
			if (c == '\r') ++i;
			if (i < s.size() && s[i] == '\n') ++i;
			item.kind = Kind::Blank;
		} else if (c == '#') {
			const std::size_t from = i + 1;
			while (i < s.size() && s[i] != '\n') ++i;
			item.kind = Kind::Comment;
			item.text = s.substr(from, i - from);
			if (!item.text.empty() && item.text.back() == '\r') item.text.pop_back();
			if (i < s.size()) ++i;
		} else if (c == '[') {
			const bool array = s.compare(i, 2, "[[") == 0;
			i += array ? 2 : 1;
			item.kind = array ? Kind::ArrayTable : Kind::Table;
			if (!parser.ParseKey(i, item.text)) break;
			parser.SkipSpaces(i);
			if (s.compare(i, array ? 2 : 1, array ? "]]" : "]") != 0) {
				parser.Fail(i, array ? "']]' expected" : "']' expected");
				break;
			}
			i += array ? 2 : 1;
			if (!eol(i, item)) break;
		} else {
			item.kind = Kind::Key;
			if (!parser.ParseKey(i, item.text)) break;
			parser.SkipSpaces(i);
			if (i >= s.size() || s[i] != '=') {
				parser.Fail(i, "'=' expected after " + item.text);
				break;
			}
			++i;
			parser.SkipSpaces(i);
			if (!parser.ParseValue(i, item.value)) break;
			if (!eol(i, item)) break;
		}
		doc.items.push_back(std::move(item));
		(void)line;
	}
	doc.error = parser.error;
	return doc;
}

// ---- writing values ----

inline std::string EscapeBasic(std::string_view text, bool quotes) {
	std::string out;
	for (const char ch : text) {
		const auto c = static_cast<unsigned char>(ch);
		switch (ch) {
		case '\\': out += "\\\\"; break;
		case '"':
			if (quotes) out += "\\\"";
			else out += ch;
			break;
		case '\b': out += "\\b"; break;
		case '\t': out += "\\t"; break;
		case '\n': out += "\\n"; break;
		case '\f': out += "\\f"; break;
		case '\r': out += "\\r"; break;
		default:
			if (c < 0x20 || c == 0x7F) {
				char hex[8];
				static const char* digits = "0123456789ABCDEF";
				hex[0] = '\\', hex[1] = 'u', hex[2] = '0', hex[3] = '0', hex[4] = digits[c >> 4], hex[5] = digits[c & 15], hex[6] = 0;
				out += hex;
			} else out += ch;
		}
	}
	return out;
}

inline bool HasControl(std::string_view text) {
	for (const char ch : text)
		if (static_cast<unsigned char>(ch) < 0x20 || ch == 0x7F) return true;
	return false;
}

// A string value in the style it had, when that style can hold it.
inline std::string WriteString(std::string_view text, Style style) {
	if ((style == Style::Literal || style == Style::MultiLiteral) && text.find('\'') == std::string_view::npos && !HasControl(text)) return "'" + std::string(text) + "'";
	return "\"" + EscapeBasic(text, true) + "\"";
}

// Reads text typed as escapes (\n, \t, \\, \", \uXXXX) back into a string.
inline bool Unescape(std::string_view typed, std::string& out) {
	out.clear();
	for (std::size_t i = 0; i < typed.size();) {
		if (typed[i] != '\\') {
			out += typed[i++];
			continue;
		}
		++i;
		if (!detail::Escape(typed, i, out)) return false;
	}
	return true;
}

} // namespace TomlDoc
