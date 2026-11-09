// Builds a merc's HD definition at load: the game's own <merc>.json with the plugin's entities and dependencies
// added (tables in merc_adds.inc, made by tools/gen_defs.py). The base files are put back together from tables, then edited as text.
#pragma once
#include <cstdio>
#include <string>

namespace MercBuild {

// A string of merc_adds.inc's text pool.
inline const char* Plain(TextAt at) { return TextPool + at; }

inline std::string Quote(const char* text) {
	std::string out = "\"";
	for (const char* c = text; *c; ++c) {
		if (*c == '"' || *c == '\\') out += '\\';
		out += *c;
	}
	return out + "\"";
}

inline std::string Number(float value) {
	char text[32];
	std::snprintf(text, sizeof(text), "%.6g", value);
	std::string out = text;
	if (out.find_first_of(".eE") == std::string::npos) out += ".0";
	return out;
}

// Same JSON as tools/gen_defs.py entity().
inline std::string Entity(const AddedEntity& e, float grip) {
	const std::string n = Plain(e.name), part = Plain(e.part), variant = Plain(e.variant), model = Plain(e.model), attach = Plain(e.attach),
	                  bone = Plain(e.bone), orient = Plain(e.orient);
	std::string out = "{\"type\":\"Entity\",\"name\":" + Quote(("entity_" + n).c_str()) + ",\"id\":" + std::to_string(e.id) + ",\"components\":[";
	out += "{\"type\":\"UnitPartComponent\",\"name\":" + Quote(("component_part_" + n).c_str()) + ",\"part\":" + Quote(part.c_str()) +
	       ",\"variant\":" + Quote(variant.c_str()) + "},";
	out += "{\"type\":\"ModelDefinitionComponent\",\"name\":" + Quote(("component_model_" + n).c_str()) + ",\"filename\":" + Quote(model.c_str()) +
	       ",\"visibleLayers\":1,\"lightMask\":19,\"shadowMask\":3,\"ghostShadows\":false,\"floorModel\":false,"
	       "\"terrainBlendEnableYUpBlend\":false,\"terrainBlendMode\":1}";
	if (!attach.empty()) {
		const std::string a = "entity_" + n + "_UnitAttachment";
		out += ",{\"type\":\"UnitAttachmentComponent\",\"name\":" + Quote(a.c_str()) + ",\"filename\":" + Quote(attach.c_str()) +
		       ",\"overrideBoneName\":" + Quote(bone.c_str()) + ",\"orientTowardBoneName\":" + Quote(orient.c_str()) +
		       ",\"attachmentTransform\":{\"type\":\"Transform\",\"name\":" + Quote((a + "_attachmentTransform").c_str()) +
		       ",\"translation\":{\"x\":0.0,\"y\":" + Number(e.grip ? grip : 0.0f) + ",\"z\":0.0},\"rotation\":{\"x\":0.0,\"y\":0.0,\"z\":0.0},"
		       "\"scale\":{\"x\":1.0,\"y\":1.0,\"z\":1.0}},\"attachmentStateMachineFilename\":\"\",\"part\":" + Quote(part.c_str()) +
		       ",\"variant\":" + Quote(variant.c_str()) + "}";
	}
	return out + "]}";
}

// Index just past the bracket closing the array that opens at text[open] ('['), skipping strings.
inline std::size_t ArrayEnd(const std::string& text, std::size_t open) {
	int  depth = 0;
	bool str   = false;
	for (std::size_t i = open; i < text.size(); ++i) {
		const char c = text[i];
		if (str) {
			if (c == '\\') ++i;
			else if (c == '"') str = false;
			continue;
		}
		if (c == '"') str = true;
		else if (c == '[' || c == '{') ++depth;
		else if ((c == ']' || c == '}') && --depth == 0) return i + 1;
	}
	return std::string::npos;
}

// Appends items (each a JSON value) to the array after key (searched from 'from'); false if not found.
inline bool Append(std::string& text, const char* key, const std::string& items, std::size_t from = 0) {
	if (items.empty()) return true;
	const std::size_t at = text.find(std::string("\"") + key + "\":[", from);
	if (at == std::string::npos) return false;
	const std::size_t open = text.find('[', at), end = ArrayEnd(text, open);
	if (end == std::string::npos) return false;
	const bool empty = text.find_first_not_of(" \t\r\n", open + 1) == end - 1;
	text.insert(end - 1, (empty ? "" : ",") + items);
	return true;
}

// Inserts one item first in the "components" array of the entity named name.
inline bool Prepend(std::string& text, const char* entity, const std::string& item) {
	const std::size_t at = text.find(std::string("\"name\":\"") + entity + "\"");
	if (at == std::string::npos) return false;
	const std::size_t comps = text.find("\"components\":[", at);
	if (comps == std::string::npos) return false;
	const std::size_t open  = text.find('[', comps);
	const bool        empty = text.find_first_not_of(" \t\r\n", open + 1) == ArrayEnd(text, open) - 1;
	text.insert(open + 1, item + (empty ? "" : ","));
	return true;
}

// The game's own <merc>.json for merc (index in MercNames), put back together from merc_adds.inc.
inline std::string BaseText(std::size_t merc) {
	std::string text;
	std::size_t at = 0;
	for (const auto& p : BasePieces) {
		if (at == merc) {
			text += Plain(p.gap);
			if (p.string != NoText) text += '"', text += Plain(p.string), text += '"';
		}
		if (p.string == NoText) ++at;
	}
	return text;
}

// merc: index in MercNames; base: the game's <merc>.json. Empty on failure, with why in 'trouble'.
inline std::string Build(std::size_t merc, std::string text, float grip, std::string& trouble) {
	if (std::string(MercNames[merc]) == "act3hire" &&
	    (!Prepend(text, "entity_rhand", "{\"type\":\"UnitPartComponent\",\"name\":\"component_part_rhand_lsd\",\"part\":\"rhand\",\"variant\":\"lsd\"}") ||
	     !Prepend(text, "entity_shield", "{\"type\":\"UnitPartComponent\",\"name\":\"component_part_shield_buc\",\"part\":\"shield\",\"variant\":\"buc\"}")))
		return trouble = "the Iron Wolf's sword or shield entity is missing", std::string();
	std::string models, textures, entities;
	for (const auto& d : AddedDependencies)
		if (d.merc == merc) (d.texture ? textures : models) += (d.texture ? textures : models).empty() ? "" : ",", (d.texture ? textures : models) += "{\"path\":" + Quote(Plain(d.path)) + "}";
	for (const auto& e : AddedEntities)
		if (e.merc == merc) entities += (entities.empty() ? "" : ",") + Entity(e, grip);
	const std::size_t deps = text.find("\"dependencies\":{");
	if (deps == std::string::npos || !Append(text, "models", models, deps) || !Append(text, "textures", textures, deps))
		return trouble = "its dependency lists are missing", std::string();
	if (!Append(text, "entities", entities)) return trouble = "its entity list is missing", std::string();
	return text;
}

} // namespace MercBuild
