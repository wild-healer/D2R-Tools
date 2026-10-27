// ---- stats panel ----
// One panel with four tabs (Overview, Equipment, Misc, Reset), opened by the
// "Show Run Stats" control. D2R keeps a panel's widgets (and their text) from
// the first time it loads it, and D2RLoader 1.3.1 cannot change a widget's
// text, so each time the stats are shown the plugin writes a new layout with
// the numbers in it, asks D2R to unload the panel it will use, and opens it.
// Panels can only be registered at load (64 at most), so two of them take
// turns; if D2R turns out to keep the old layout anyway (a probe widget named
// after each layout tells), every view takes a panel never opened before. Text boxes ignore color codes, so every line and
// bar takes its color from its own widget. Button labels are plugin strings
// ("@run-counter:..."), registered again before each new panel. Buttons send
// "PanelManager:ClosePanel:run-counter/Cmd-<command>", a message the
// UI-message listener consumes. Nothing here uses widget handles, so the
// small panel keeps the ones it needs.
// Included from plugin.cpp, inside its anonymous namespace.

constexpr int         StatsTabs     = 5;
constexpr const char* TabNames[]    = { "Overview", "Equipment", "Misc", "Reset", "Settings" };
constexpr const char* StringsPath   = "data/local/lng/strings/d2rloader/run-counter/stats.json";
constexpr const char* Languages[]   = { "enUS", "deDE", "esES", "esMX", "frFR", "itIT", "jaJP", "koKR", "plPL", "ptBR", "ruRU", "zhCN", "zhTW" };
constexpr const char* CommandPrefix = "run-counter/Cmd-";
constexpr int         Columns = 3, Rows = 31;

// Geometry, in the layout's 4K units around the screen centre.
constexpr int PanelLeft = -1520, PanelTop = -870, PanelWidth = 3040, PanelHeight = 1740;
constexpr int TabTop = -850, TabWidth = 540, TabHeight = 90, TabStep = 560;
constexpr int SelectorTop = -740, SelectorWidth = 720, SelectorStep = 750, ButtonHeight = 76, SmallWidth = 76, WideWidth = 580, RowButtonWidth = 72, ToggleWidth = 462;
constexpr int ItemWidth = SelectorWidth, ItemHeight = 54, ItemStep = 58, ItemRows = 25;
constexpr int RowsTop = -640, RowHeight = 47, ColumnWidth = 980, LabelWidth = 280;
constexpr int ColumnX[Columns] = { -1500, -480, 540 };
constexpr int ResetButtonRows[2] = { 9, 11 };
constexpr int GradeStep = 110; // width of a column of the gem table

std::atomic_bool StatsQueued {};
int              CurrentTab {};
int              OpenList = -1; // the selector whose list is open, or -1

// The panels registered for the stats, newest last.
struct StatsPanel {
	std::string                         id;
	D2RL::Panels::RegistrationHandle    panel  = D2RL::Panels::InvalidHandle;
	D2RL::Resources::RegistrationHandle layout = D2RL::Resources::InvalidHandle;
	bool                                opened {};
};
constexpr int                       PoolSize = 60; // the small panel uses one of the 64
std::vector<StatsPanel>             StatsPanels;
int                                 ShownPanel = -1;
bool                                ReuseWorks = true;
unsigned                            StatsSerial {};
std::string                         ProbeName, ProbePanel; // the probe of the last reused panel, until found
ULONGLONG                           ProbeUntil {};
D2RL::Resources::RegistrationHandle StringsHandle = D2RL::Resources::InvalidHandle;

// ---- text helpers ----

// A color code, as in the game's string tables, as a font color of the UI profile.
// Also the closest color for the codes mods put in item names (the
// character after "ÿc").
const char* FontColor(char code) {
	switch (code) {
		case 'E': return "$FontColorLightRed";
		case '1':
		case 'U':
		case 'S': return "$FontColorRed";
		case '2':
		case 'Q':
		case 'C':
		case '<': return "$FontColorGreen";
		case ':':
		case 'A': return "$FontColorDarkGreen";
		case 'N': return "$FontColorLightTeal";
		case 'T':
		case 'F':
		case 'P': return "$FontColorLightBlue";
		case '3':
		case 'B': return "$FontColorBlue";
		case '4':
		case 'D': return "$FontColorGoldYellow";
		case 'H': return "$FontColorLightGold";
		case '5': return "$FontColorLightGray";
		case 'K':
		case 'I': return "$FontColorGray";
		case '6':
		case 'M': return "$FontColorBlack";
		case '7': return "$FontColorDarkGrayGold";
		case '8':
		case '@':
		case 'J':
		case 'L': return "$FontColorOrange";
		case '9':
		case 'R': return "$FontColorYellow";
		case ';':
		case 'G':
		case 'O': return "$FontColorLightPurple";
		default:  return "$FontColorWhite";
	}
}

// The same colors for bars, as "r, g, b" between 0 and 1.
std::string RectColor(char code) {
	switch (code) {
		case '1': return "0.99, 0.27, 0.27";
		case '2': return "0.0, 0.85, 0.0";
		case '3': return "0.43, 0.43, 1.0";
		case '4': return "0.78, 0.70, 0.47";
		case '5': return "0.58, 0.58, 0.58";
		case '7': return "0.68, 0.66, 0.58";
		case '8': return "1.0, 0.66, 0.0";
		case '9': return "1.0, 1.0, 0.39";
		case ';': return "0.75, 0.50, 0.95";
		case ':': return "0.0, 0.55, 0.0";
		default:  return "0.94, 0.94, 0.94";
	}
}

// Characters shown.
std::size_t Visible(const std::string& text) {
	std::size_t count = 0;
	for (const char c : text) count += (static_cast<unsigned char>(c) & 0xC0) != 0x80;
	return count;
}

// Text boxes do not clip their text, so long lines are cut to fit.
std::string Fit(const std::string& text, std::size_t chars) {
	if (Visible(text) <= chars) return text;
	std::size_t count = 0;
	for (std::size_t i = 0; i < text.size(); ++i)
		if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80 && ++count > chars - 3) return text.substr(0, i) + "...";
	return text;
}

std::string Percent(std::uint64_t part, std::uint64_t whole) { return whole ? std::to_string((part * 100 + whole / 2) / whole) + "%" : "-"; }

std::string PerHour(std::uint64_t count, ULONGLONG ms) { return ms ? Fixed(count * 3600000.0 / static_cast<double>(ms), 2) : std::string("-"); }

struct Part {
	std::string   name;
	char          color;
	std::uint64_t count;
};

// The widgets of one tab's contents, as layout children.
struct Page {
	std::string json;
	int         widgets {};

	std::string Name() { return "W" + std::to_string(++widgets); }
	void Text(int x, int y, int width, const std::string& text, char color, const char* align = "left") {
		if (text.empty()) return;
		json += ", " + TextBox(Name(), x, y, width, RowHeight, text, FontColor(color), "$MediumFontSize", align);
	}
	void Rect(int x, int y, int width, int height, char color) {
		char rect[160] {};
		std::snprintf(rect, sizeof(rect), R"("rect": {"x": %d, "y": %d, "width": %d, "height": %d})", x, y, width, height);
		json += R"(, {"type": "RectangleWidget", "name": ")" + Name() + R"(", "fields": {)" + rect + R"(, "color": [)" + RectColor(color) + ", 0.9]}}";
	}
	static int Y(int row) { return RowsTop + row * RowHeight; }
	// A whole row (text runs across the column) or a label and a value.
	void Set(int column, int row, const std::string& a, const std::string& b = "", char color = '0') {
		if (column < 0 || column >= Columns || row < 0 || row >= Rows) return;
		Text(ColumnX[column], Y(row), b.empty() ? ColumnWidth : LabelWidth, Fit(a, b.empty() ? 48 : 14), color);
		Text(ColumnX[column] + LabelWidth, Y(row), ColumnWidth - LabelWidth, Fit(b, 44), color);
	}
	void Header(int column, int row, const std::string& text) { Set(column, row, text, "", '4'); }
	// A labelled bar, `count` out of `scale` long, then a text.
	void BarRow(int column, int row, const std::string& label, std::uint64_t count, std::uint64_t scale, const std::string& text, char color) {
		if (column < 0 || column >= Columns || row < 0 || row >= Rows) return;
		BarAt(column, Y(row), label, count, scale, text, color);
	}
	// The same at any height, for lists longer than the rows.
	void BarAt(int column, int y, const std::string& label, std::uint64_t count, std::uint64_t scale, const std::string& text, char color) {
		const int most  = std::clamp(Config.barChars * 10, 100, 560);
		int       width = scale ? static_cast<int>((count * static_cast<std::uint64_t>(most) + scale / 2) / scale) : 0;
		if (count && width < 4) width = 4;
		const int x = ColumnX[column] + LabelWidth;
		Text(ColumnX[column], y, LabelWidth, Fit(label, 14), color);
		if (width) Rect(x, y + 9, width, RowHeight - 18, color);
		Text(x + width + 14, y, ColumnWidth - LabelWidth - width - 14, text, color);
	}
	// A chart: its title, then one bar per part, sized by the part's share.
	// Returns the next free row.
	int Chart(int column, int row, const std::string& title, const std::vector<Part>& parts) {
		std::uint64_t total = 0;
		for (const auto& part : parts) total += part.count;
		Header(column, row++, title);
		for (const auto& part : parts) BarRow(column, row++, part.name, part.count, total, std::to_string(part.count) + " (" + Percent(part.count, total) + ")", part.color);
		return row + 1;
	}
};

// ---- filters ----

struct Filters {
	std::string area;          // empty: all
	std::string character;     // empty: all
	int         terror {};     // 0 all, 1 terrorized, 2 not
	int         slot {};       // Equipment tab: 0 all, else ItemClass slot
	int         misc {};       // Misc tab: 0 all, 1 runes, 2 gems, 3 charms, 4 jewels, 5 keys and essences, 6 other
	int         date {};       // Reset tab: 0 all, 1 today, 2 before today, 3 older than 7 days, 4 older than 30 days
} Filter;

constexpr const char* SlotNames[]  = { "All", "Helm", "Armor", "Weapon", "Shield", "Gloves", "Belt", "Boots", "Ring", "Amulet" };
constexpr const char* MiscNames[]  = { "All", "Runes", "Gems", "Charms", "Jewels", "Keys/essences", "Other" };
constexpr const char* DateNames[]  = { "All dates", "Today", "Before today", "Older than 7 days", "Older than 30 days" };
constexpr const char* TerrorNames[] = { "All", "Terrorized", "Not terrorized" };
constexpr const char* GemNames[]   = { "", "Amethyst", "Diamond", "Emerald", "Ruby", "Sapphire", "Topaz", "Skull" };
constexpr const char* GradeNames[] = { "", "Chipped", "Flawed", "Normal", "Flawless", "Perfect" };

// Caller holds Lock. Areas by number of runs, most first.
std::vector<std::string> AreaValues() {
	std::map<std::string, int> counts;
	for (const auto& run : AllRuns)
		if (run.id && !run.area.empty()) ++counts[run.area];
	std::vector<std::pair<std::string, int>> sorted(counts.begin(), counts.end());
	std::stable_sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
	std::vector<std::string> out { "" };
	for (const auto& [area, count] : sorted) out.push_back(area);
	return out;
}

std::vector<std::string> CharacterValues() {
	std::vector<std::string> out { "" };
	for (const auto& run : AllRuns)
		if (!run.character.empty() && std::find(out.begin(), out.end(), run.character) == out.end()) out.push_back(run.character);
	return out;
}

std::int64_t Today() { return Stamp(Now().substr(0, 10) + " 00:00:00"); }

bool MatchRun(const RunRecord& run, bool dates) {
	if (!Filter.area.empty() && run.area != Filter.area) return false;
	if (!Filter.character.empty() && run.character != Filter.character) return false;
	if (Filter.terror && run.terror != (Filter.terror == 1)) return false;
	if (dates && Filter.date) {
		const auto today = Today();
		const auto limit = Filter.date == 1 ? today : Filter.date == 2 ? today : Filter.date == 3 ? today - 7 * 86400 : today - 30 * 86400;
		if (Filter.date == 1 ? run.stamp < limit : run.stamp >= limit) return false;
	}
	return true;
}

bool MatchItem(const ItemRecord& item) {
	if (!Filter.area.empty() && item.runArea != Filter.area) return false;
	if (!Filter.character.empty() && item.character != Filter.character) return false;
	if (Filter.terror && item.terror != (Filter.terror == 1)) return false;
	return true;
}

int MiscType(const ItemRecord& item) {
	if (!item.cls) return 6;
	switch (item.cls->group) {
		case 1: return 1;
		case 2: return 2;
		case 3: return 3;
		case 4: return 4;
		case 5: return 5;
		default: return 6;
	}
}

bool IsEquipment(const ItemRecord& item) { return item.cls && item.cls->group == 0; }

// ---- selectors ----
// Each selector is a button showing its value; clicking it lists every value,
// and clicking one picks it.

constexpr const char* SelectorNames[] = { "Area", "Character", "Terror zone" };

struct Choices {
	std::vector<std::string> labels;
	int                      current {};
};

// Caller holds Lock.
Choices SelectorChoices(int tab, int selector) {
	Choices    out;
	const auto named = [&out](const std::vector<std::string>& values, const std::string& current) {
		for (const auto& value : values) out.labels.push_back(value.empty() ? "All" : value);
		const auto found = std::find(values.begin(), values.end(), current);
		out.current      = found == values.end() ? 0 : static_cast<int>(found - values.begin());
	};
	const auto fixed = [&out](const char* const* names, int count, int current) {
		out.labels.assign(names, names + count);
		out.current = current;
	};
	switch (selector) {
		case 0: named(AreaValues(), Filter.area); break;
		case 1: named(CharacterValues(), Filter.character); break;
		case 2: fixed(TerrorNames, 3, Filter.terror); break;
		default:
			if (tab == 1) fixed(SlotNames, 10, Filter.slot);
			else if (tab == 2) fixed(MiscNames, 7, Filter.misc);
			else fixed(DateNames, 5, Filter.date);
	}
	return out;
}

std::string SelectorName(int tab, int selector) { return selector < 3 ? SelectorNames[selector] : tab == 1 ? "Slot" : tab == 2 ? "Type" : "Date"; }

// Caller holds Lock.
void PickChoice(int tab, int selector, int index) {
	if (index < 0) return;
	switch (selector) {
		case 0: {
			const auto values = AreaValues();
			if (index < static_cast<int>(values.size())) Filter.area = values[static_cast<std::size_t>(index)];
			break;
		}
		case 1: {
			const auto values = CharacterValues();
			if (index < static_cast<int>(values.size())) Filter.character = values[static_cast<std::size_t>(index)];
			break;
		}
		case 2: Filter.terror = std::min(index, 2); break;
		default:
			if (tab == 1) Filter.slot = std::min(index, 9);
			else if (tab == 2) Filter.misc = std::min(index, 6);
			else Filter.date = std::min(index, 4);
	}
}

// ---- totals (caller holds Lock) ----

struct RunTotals {
	std::uint32_t runs {}, townGames {};
	ULONGLONG     farmMs {}, townMs {};
	std::int64_t  gold {};
	bool          goldKnown {};
	ULONGLONG     goldFarmMs {}; // farming time of the runs whose gold is known
};

RunTotals Totals(bool filtered, bool dates = false) {
	RunTotals totals;
	for (const auto& run : AllRuns) {
		if (filtered && !MatchRun(run, dates)) continue;
		if (run.id) {
			++totals.runs;
			totals.farmMs += run.farm * 1000ULL;
		} else ++totals.townGames;
		totals.townMs += run.town * 1000ULL;
		if (run.goldKnown) {
			totals.goldKnown = true;
			totals.gold += run.gold;
			totals.goldFarmMs += run.farm * 1000ULL;
		}
	}
	return totals;
}

std::string GoldText(const RunTotals& totals) { return totals.goldKnown ? Thousands(totals.gold) : std::string("-"); }

std::string GoldPerHour(const RunTotals& totals) {
	if (!totals.goldKnown || !totals.goldFarmMs) return "-";
	return Thousands(static_cast<std::int64_t>(static_cast<double>(totals.gold) * 3600000.0 / static_cast<double>(totals.goldFarmMs)));
}

std::vector<Part> SourceParts(const std::vector<const ItemRecord*>& items) {
	std::vector<Part> parts { { "Trash", '0', 0 }, { "Champion", '3', 0 }, { "Elite/minion", '4', 0 }, { "Boss", '8', 0 }, { "Town", '5', 0 }, { "Other", '7', 0 } };
	static constexpr int order[SourceCount] { 5, 0, 1, 2, 3, 4 }; // by Source value
	for (const auto* item : items) ++parts[static_cast<std::size_t>(order[item->source])].count;
	return parts;
}

std::vector<Part> QualityParts(const std::vector<const ItemRecord*>& items) {
	std::vector<Part> parts { { "Unique", '4', 0 }, { "Set", '2', 0 }, { "Rare", '9', 0 }, { "Magic", '3', 0 }, { "Superior", '7', 0 }, { "Normal", '0', 0 }, { "Low", '5', 0 } };
	for (const auto* item : items) {
		switch (item->quality) {
			case Items::Quality::Unique:   ++parts[0].count; break;
			case Items::Quality::Set:      ++parts[1].count; break;
			case Items::Quality::Rare:     ++parts[2].count; break;
			case Items::Quality::Magic:    ++parts[3].count; break;
			case Items::Quality::Superior: ++parts[4].count; break;
			case Items::Quality::Inferior: ++parts[6].count; break;
			case Items::Quality::Crafted:  break; // equipment is found, not crafted
			default:                       ++parts[5].count; break;
		}
	}
	return parts;
}

constexpr char SlotColors[]  = "0179385;:"; // Helm Armor Weapon Shield Gloves Belt Boots Ring Amulet
constexpr char GemColors[]   = " ;021395";  // Amethyst Diamond Emerald Ruby Sapphire Topaz Skull, from 1
constexpr char GradeColors[] = " 57098";    // Chipped Flawed Normal Flawless Perfect, from 1
constexpr char TypeColors[]  = " 832917";   // Runes Gems Charms Jewels Keys/essences Other, from 1

std::string ItemName(const ItemRecord& item) { return item.name + (item.ethereal ? " (eth)" : ""); }
std::string RunTag(std::uint32_t run) { return run ? "#" + std::to_string(run) : std::string("town"); }

// ---- tab contents (caller holds Lock) ----

void BuildOverview(Page& page, ULONGLONG now) {
	const auto all = Totals(false);
	page.Header(0, 0, "All time");
	page.Set(0, 1, "Runs", std::to_string(all.runs) + (all.townGames ? " (+" + std::to_string(all.townGames) + " in town only)" : ""));
	page.Set(0, 2, "Farming", Hours(all.farmMs));
	page.Set(0, 3, "In town", Hours(all.townMs) + " (" + Percent(all.townMs, all.townMs + all.farmMs) + ")");
	page.Set(0, 4, "Runs/hour", all.farmMs + all.townMs ? Fixed(all.runs * 3600000.0 / static_cast<double>(all.farmMs + all.townMs), 1) : "-");
	if (all.runs) page.Set(0, 5, "Avg run", Duration(all.farmMs / all.runs) + " farming, " + Duration(all.townMs / all.runs) + " town");
	const RunRecord *fastest = nullptr, *slowest = nullptr;
	for (const auto& run : AllRuns) {
		if (!run.id) continue;
		if (!fastest || run.farm < fastest->farm) fastest = &run;
		if (!slowest || run.farm > slowest->farm) slowest = &run;
	}
	if (fastest) page.Set(0, 6, "Fastest", Duration(fastest->farm * 1000ULL) + " (run " + std::to_string(fastest->id) + ")");
	if (slowest) page.Set(0, 7, "Slowest", Duration(slowest->farm * 1000ULL) + " (run " + std::to_string(slowest->id) + ")");
	std::uint32_t uniques = 0, sets = 0, runes = 0;
	for (const auto& item : AllItems) {
		if (item.quality == Items::Quality::Unique) ++uniques;
		else if (item.quality == Items::Quality::Set) ++sets;
		if (item.rune) ++runes;
	}
	page.Set(0, 8, "Items", std::to_string(AllItems.size()) + " (" + PerHour(AllItems.size(), all.farmMs) + "/h)");
	page.Set(0, 9, "Uniques", std::to_string(uniques) + " (" + PerHour(uniques, all.farmMs) + "/h)", '4');
	page.Set(0, 10, "Sets", std::to_string(sets) + " (" + PerHour(sets, all.farmMs) + "/h)", '2');
	page.Set(0, 11, "Runes", std::to_string(runes) + " (" + PerHour(runes, all.farmMs) + "/h)", '8');
	page.Set(0, 12, "Gold", GoldText(all) + (all.goldKnown ? " (" + GoldPerHour(all) + "/h)" : ""));

	RunTotals     session;
	std::uint32_t sessionItems = 0;
	for (const auto& run : AllRuns) {
		if (run.stamp < SessionStamp) continue;
		if (run.id) ++session.runs, session.farmMs += run.farm * 1000ULL;
		if (run.goldKnown) session.goldKnown = true, session.gold += run.gold, session.goldFarmMs += run.farm * 1000ULL;
	}
	for (const auto& item : AllItems) sessionItems += Stamp(item.time) >= SessionStamp;
	page.Header(0, 14, "This session");
	page.Set(0, 15, "Runs", std::to_string(session.runs));
	page.Set(0, 16, "Farming", Hours(session.farmMs));
	page.Set(0, 17, "Items", std::to_string(sessionItems) + " (" + PerHour(sessionItems, session.farmMs) + "/h)");
	page.Set(0, 18, "Gold", GoldText(session));
	if (InGame) {
		page.Header(0, 20, "This game");
		page.Set(0, 21, Current.number ? "Run" : "Next run", std::to_string(Current.number ? Current.number : NextRun));
		page.Set(0, 22, "Farming", Duration(CurrentFarm(now)) + ", town " + Duration(CurrentTown(now)));
		page.Set(0, 23, "Items", std::to_string(Current.items.size()));
	}
	if (PendingFinishes) page.Set(0, 25, "The last game is still being saved.", "", '5');

	// Areas, most runs first, on two lines each.
	struct Area {
		std::uint32_t runs {}, items {};
		ULONGLONG     farmMs {};
	};
	std::map<std::string, Area> areas;
	for (const auto& run : AllRuns)
		if (run.id) {
			auto& area = areas[run.area];
			++area.runs, area.items += run.items, area.farmMs += run.farm * 1000ULL;
		}
	std::vector<std::pair<std::string, Area>> sorted(areas.begin(), areas.end());
	std::stable_sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second.runs > b.second.runs; });
	page.Header(1, 0, "Areas");
	int row = 1;
	for (const auto& [name, area] : sorted) {
		if (row + 1 >= Rows) break;
		page.Set(1, row++, std::to_string(area.runs) + (area.runs == 1 ? " run" : " runs"), name.empty() ? "?" : name);
		page.Set(1, row++, "", "avg " + Duration(area.farmMs / area.runs) + ", " + Fixed(static_cast<double>(area.items) / area.runs, 1) + " items/run", '5');
	}

	page.Header(2, 0, "Last runs");
	row = 1;
	for (auto run = AllRuns.rbegin(); run != AllRuns.rend() && row < 11; ++run) {
		if (!run->id) continue;
		page.Set(2, row++, "#" + std::to_string(run->id) + "  " + Duration(run->farm * 1000ULL), run->area + (run->terror ? " (TZ)" : ""));
		page.Set(2, row++, "", std::to_string(run->items) + (run->items == 1 ? " item" : " items") + (run->character.empty() ? "" : ", " + run->character), '5');
	}
	// The last finds of each kind.
	const auto finds = [&page](int first, int count, char color, const auto& match) {
		int row = first;
		for (auto item = AllItems.rbegin(); item != AllItems.rend() && row < first + count; ++item)
			if (match(*item)) page.Set(2, row++, RunTag(item->run), ItemName(*item), color);
	};
	page.Header(2, 12, "Last uniques");
	finds(13, 5, '4', [](const ItemRecord& item) { return item.quality == Items::Quality::Unique; });
	page.Header(2, 19, "Last sets");
	finds(20, 5, '2', [](const ItemRecord& item) { return item.quality == Items::Quality::Set; });
	page.Header(2, 26, "Last runes");
	finds(27, 4, '8', [](const ItemRecord& item) { return item.rune != 0; });
}

void BuildEquipment(Page& page) {
	const auto                     runs = Totals(true);
	std::vector<const ItemRecord*> items;
	for (const auto& item : AllItems)
		if (IsEquipment(item) && MatchItem(item) && (!Filter.slot || item.cls->slot == Filter.slot)) items.push_back(&item);
	std::uint32_t quality[9] {}, ethereal = 0, socketed = 0;
	std::uint64_t levels = 0;
	for (const auto* item : items) {
		++quality[std::min<std::uint32_t>(static_cast<std::uint32_t>(item->quality), 8)];
		ethereal += item->ethereal;
		socketed += item->sockets > 0;
		levels += item->itemLevel;
	}
	const auto count = items.size();
	page.Header(0, 0, "Equipment");
	page.Set(0, 1, "Items", std::to_string(count));
	page.Set(0, 2, "Per hour", PerHour(count, runs.farmMs));
	page.Set(0, 3, "Per run", runs.runs ? Fixed(static_cast<double>(count) / runs.runs, 2) : "-");
	const auto qualityLine = [&](int row, const char* name, char color, std::initializer_list<Items::Quality> qualities) {
		std::uint32_t n = 0;
		for (const auto q : qualities) n += quality[static_cast<int>(q)];
		page.Set(0, row, name, std::to_string(n) + " (" + PerHour(n, runs.farmMs) + "/h)", color);
	};
	qualityLine(4, "Unique", '4', { Items::Quality::Unique });
	qualityLine(5, "Set", '2', { Items::Quality::Set });
	qualityLine(6, "Rare", '9', { Items::Quality::Rare });
	qualityLine(7, "Magic", '3', { Items::Quality::Magic });
	qualityLine(8, "Superior", '7', { Items::Quality::Superior });
	qualityLine(9, "Normal", '0', { Items::Quality::Inferior, Items::Quality::Normal });
	page.Set(0, 10, "Ethereal", std::to_string(ethereal) + " (" + Percent(ethereal, count) + ")");
	page.Set(0, 11, "Socketed", std::to_string(socketed) + " (" + Percent(socketed, count) + ")");
	page.Set(0, 12, "Avg ilvl", count ? Fixed(static_cast<double>(levels) / count, 1) : "-");
	page.Header(0, 14, "Runs matching");
	page.Set(0, 15, "Runs", std::to_string(runs.runs));
	page.Set(0, 16, "Farming", Hours(runs.farmMs));
	page.Set(0, 17, "Avg run", runs.runs ? Duration(runs.farmMs / runs.runs) : "-");
	page.Set(0, 18, "Gold", GoldText(runs));
	page.Set(0, 19, "Gold/hour", GoldPerHour(runs));
	const auto latest = [&](int first, int rows, Items::Quality wanted, char color) {
		int row = first;
		for (auto item = items.rbegin(); item != items.rend() && row < first + rows; ++item)
			if ((*item)->quality == wanted) page.Set(0, row++, RunTag((*item)->run), ItemName(**item), color);
	};
	page.Header(0, 21, "Latest uniques");
	latest(22, 4, Items::Quality::Unique, '4');
	page.Header(0, 26, "Latest sets");
	latest(27, 4, Items::Quality::Set, '2');

	std::vector<Part> slots;
	for (int s = 1; s <= 9; ++s) slots.push_back({ SlotNames[s], SlotColors[s - 1], 0 });
	std::vector<Part> tiers { { "Normal", '0', 0 }, { "Exceptional", '3', 0 }, { "Elite", '9', 0 } };
	for (const auto* item : items) {
		if (item->cls->slot >= 1 && item->cls->slot <= 9) ++slots[item->cls->slot - 1u].count;
		if (item->cls->tier >= 1 && item->cls->tier <= 3) ++tiers[item->cls->tier - 1u].count;
	}
	page.Chart(1, 0, "Slot", slots);
	page.Chart(1, 11, "Tier", tiers);
	page.Chart(1, 16, "Rarity", QualityParts(items));
	page.Chart(2, 0, "Ethereal", { { "Ethereal", '5', ethereal }, { "Not eth", '0', count - ethereal } });
	page.Chart(2, 4, "Dropped by", SourceParts(items));

	// Areas the items came from, with the rate per hour of farming there.
	std::map<std::string, std::uint32_t> perArea;
	for (const auto* item : items) ++perArea[item->runArea];
	std::map<std::string, ULONGLONG> areaMs;
	for (const auto& run : AllRuns)
		if (run.id && MatchRun(run, false)) areaMs[run.area] += run.farm * 1000ULL;
	std::vector<std::pair<std::string, std::uint32_t>> sorted(perArea.begin(), perArea.end());
	std::stable_sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
	page.Header(2, 12, "By area (items, per hour)");
	int row = 12;
	for (std::size_t i = 0; i < sorted.size() && row + 1 < Rows; ++i)
		page.Set(2, ++row, std::to_string(sorted[i].second) + "  " + PerHour(sorted[i].second, areaMs[sorted[i].first]), sorted[i].first.empty() ? "In town" : sorted[i].first);
}

void BuildMisc(Page& page) {
	const auto                     runs = Totals(true);
	std::vector<const ItemRecord*> items;
	for (const auto& item : AllItems)
		if (!IsEquipment(item) && MatchItem(item) && (!Filter.misc || MiscType(item) == Filter.misc)) items.push_back(&item);
	std::uint32_t types[7] {}, runes[34] {}, gems[8][6] {};
	for (const auto* item : items) {
		++types[MiscType(*item)];
		if (item->rune) ++runes[item->rune];
		if (item->cls && item->cls->group == 2 && item->cls->gem <= 7 && item->cls->grade <= 5) ++gems[item->cls->gem][item->cls->grade];
	}
	const auto count = items.size();
	page.Header(0, 0, "Misc items");
	page.Set(0, 1, "Items", std::to_string(count));
	page.Set(0, 2, "Per hour", PerHour(count, runs.farmMs));
	page.Set(0, 3, "Per run", runs.runs ? Fixed(static_cast<double>(count) / runs.runs, 2) : "-");
	for (int t = 1; t <= 6; ++t)
		page.BarRow(0, 3 + t, MiscNames[t], types[t], count, std::to_string(types[t]) + " (" + Percent(types[t], count) + "), " + PerHour(types[t], runs.farmMs) + "/h", TypeColors[t]);
	int best = 0;
	for (int r = 33; r >= 1 && !best; --r)
		if (runes[r]) best = r;
	page.Set(0, 10, "Best rune", best ? RuneNames[best - 1] : "-", '8');
	page.Set(0, 11, "Farming", Hours(runs.farmMs) + " in " + std::to_string(runs.runs) + " runs");
	page.Chart(0, 13, "Dropped by", SourceParts(items));

	// Runes: all 33, closer together than the rows to fit; bars scaled to the most found.
	page.Header(1, 0, "Runes");
	std::uint32_t most = 0, allRunes = 0;
	for (int r = 1; r <= 33; ++r) most = std::max(most, runes[r]), allRunes += runes[r];
	const int runeStep = (PanelTop + PanelHeight - 6 - Page::Y(1)) / 33;
	for (int r = 1; r <= 33; ++r)
		page.BarAt(1, Page::Y(1) + (r - 1) * runeStep, RuneNames[r - 1], runes[r], most, std::to_string(runes[r]) + " (" + Percent(runes[r], allRunes) + ")", runes[r] ? '8' : '5');

	// Gems: a table of kinds by grade with totals, then charts by grade and by kind.
	page.Header(2, 0, "Gems and skulls");
	static constexpr const char* gradeHeads[] { "Chip", "Flaw", "Norm", "Flwl", "Perf", "All" };
	const auto cell = [&page](int row, int column, const std::string& text, char color) {
		page.Text(ColumnX[2] + LabelWidth + column * GradeStep, Page::Y(row), GradeStep, text, color, "center");
	};
	for (int q = 0; q < 6; ++q) cell(1, q, gradeHeads[q], q < 5 ? GradeColors[q + 1] : '0');
	std::vector<Part> grades, kinds;
	for (int q = 1; q <= 5; ++q) grades.push_back({ GradeNames[q], GradeColors[q], 0 });
	std::uint32_t all = 0;
	for (int g = 1; g <= 7; ++g) {
		std::uint32_t total = 0;
		page.Set(2, 1 + g, GemNames[g], "", GemColors[g]);
		for (int q = 1; q <= 5; ++q) {
			cell(1 + g, q - 1, gems[g][q] ? std::to_string(gems[g][q]) : "-", GradeColors[q]);
			total += gems[g][q];
			grades[q - 1u].count += gems[g][q];
		}
		cell(1 + g, 5, std::to_string(total), '0');
		kinds.push_back({ GemNames[g], GemColors[g], total });
		all += total;
	}
	page.Set(2, 9, "Total", "", '4');
	for (int q = 1; q <= 5; ++q) cell(9, q - 1, std::to_string(grades[q - 1u].count), GradeColors[q]);
	cell(9, 5, std::to_string(all), '4');
	int row = page.Chart(2, 11, "By grade", grades);
	page.Chart(2, row, "By type", kinds);
}

int          ConfirmAction = -1; // 0 matching runs, 1 everything, 2 the run ConfirmStamp
ULONGLONG    ConfirmUntil {};
std::int64_t ConfirmStamp {};

std::string Button(const std::string& name, const char* sprite, int x, int y, const std::string& key, const std::string& command);
std::string ResetResult;

std::vector<std::size_t> MatchingRuns() {
	std::vector<std::size_t> out;
	for (std::size_t i = 0; i < AllRuns.size(); ++i)
		if (MatchRun(AllRuns[i], true)) out.push_back(i);
	return out;
}

void BuildReset(Page& page, std::map<std::string, std::string>& labels) {
	const auto    matching = MatchingRuns();
	std::uint32_t runs = 0, town = 0, items = 0;
	ULONGLONG     farmMs = 0;
	std::int64_t  gold = 0;
	for (const auto i : matching) {
		const auto& run = AllRuns[i];
		(run.id ? runs : town)++;
		items += run.items;
		farmMs += run.farm * 1000ULL;
		gold += run.gold;
	}
	page.Header(0, 0, "Delete saved runs");
	page.Set(0, 1, "Runs", std::to_string(runs));
	page.Set(0, 2, "Town games", std::to_string(town));
	page.Set(0, 3, "Items", std::to_string(items));
	page.Set(0, 4, "Farming", Hours(farmMs));
	page.Set(0, 5, "Gold", Thousands(gold));
	page.Set(0, 7, "Choose runs with the selectors above.", "", '5');
	page.Set(0, 8, "Or click x beside a game, then ok?, to delete it", "", '5');
	const bool confirming = ConfirmAction >= 0 && GetTickCount64() < ConfirmUntil;
	labels["Del0"] = confirming && ConfirmAction == 0 ? "Click again: delete " + std::to_string(matching.size()) + " games" : "Delete matching runs";
	labels["Del1"] = confirming && ConfirmAction == 1 ? std::string("Click again: delete all") : "Delete everything";
	page.Set(0, 13, "Copies of the files are kept as", "", '5');
	page.Set(0, 14, "runs-backup.csv and drops-backup.csv.", "", '5');
	if (!ResetResult.empty()) page.Set(0, 16, ResetResult, "", '2');

	page.Header(1, 0, "Matching games");
	int row = 1, column = 1;
	for (auto i = matching.rbegin(); i != matching.rend(); ++i) {
		if (row >= Rows) {
			if (++column >= Columns) break;
			row = 1;
		}
		// A delete button before each game; the first click asks to click again.
		const auto&       run  = AllRuns[*i];
		const bool        sure = confirming && ConfirmAction == 2 && run.stamp == ConfirmStamp;
		const std::string key  = "Row" + std::to_string(*i);
		const int         x = ColumnX[column], y = Page::Y(row);
		labels[key]            = sure ? "ok?" : "x";
		page.json += Button(key, "row", x, y + 2, key, "run" + std::to_string(*i));
		page.Text(x + RowButtonWidth + 10, y, LabelWidth - RowButtonWidth - 10, run.id ? "#" + std::to_string(run.id) : std::string("town"), sure ? '1' : '0');
		page.Text(x + LabelWidth, y, ColumnWidth - LabelWidth,
		          Fit(run.start.substr(0, 10) + " " + (run.area.empty() ? "in town" : run.area) + (run.character.empty() ? "" : ", " + run.character), 44), sure ? '1' : '0');
		++row;
	}
}

// ---- settings tab ----

struct SettingRow {
	const char* label;
	const char* hint;
	int         column;
};

constexpr SettingRow SettingRows[] {
	{ "Tracking active", "off: games and items are not saved", 0 },
	{ "Uniques", "items tracked, by quality", 0 },
	{ "Sets", "items tracked, by quality", 0 },
	{ "Rares", "items tracked, by quality", 0 },
	{ "Magic", "items tracked, by quality", 0 },
	{ "Normal", "normal, superior and low quality", 0 },
	{ "Gems", "gems and skulls", 0 },
	{ "Essences", "essences, keys, Ancients' statues", 0 },
	{ "Worldstone shards", "the five shards", 0 },
	{ "Consumables", "potions, scrolls, tomes, keys, arrows", 0 },
	{ "Runes from", "lowest rune tracked", 0 },
	{ "Announce from", "lowest rune in the panel's drops list", 0 },
	{ "Log UI messages", "to run-counter-ui.log", 0 },
	{ "Panel", "game UI, overlay or off; after a restart", 1 },
	{ "Position X", "share of the screen; game panel after a restart", 1 },
	{ "Position Y", "share of the screen; game panel after a restart", 1 },
	{ "Spacing", "game panel after a restart, overlay text size", 1 },
	{ "Background", "translucent, behind the game panel", 1 },
	{ "Overlay colors", "drop lines colored by quality", 1 },
	{ "Drops listed", "latest drops under the panel", 1 },
	{ "Scan every", "how often items are checked", 1 },
	{ "Bar length", "bars in the stats tabs", 1 },
};
constexpr int SettingCount = static_cast<int>(sizeof(SettingRows) / sizeof(SettingRows[0]));

// On/off settings get one button; the others step with < and >.
bool IsToggle(int index) { return index <= 9 || index == 12 || index == 17 || index == 18; }

std::string RuneText(int rune) { return rune ? RuneNames[rune - 1] : "None"; }

std::string SettingValue(int index) {
	const auto onOff = [](bool value) { return std::string(value ? "On" : "Off"); };
	const auto track = [](int value) { return std::string(value == TrackHidden ? "On, no overlay" : value ? "On" : "Off"); };
	switch (index) {
		case 0:  return onOff(Config.active);
		case 1:  return track(Config.uniques);
		case 2:  return track(Config.sets);
		case 3:  return track(Config.rares);
		case 4:  return track(Config.magic);
		case 5:  return track(Config.normal);
		case 6:  return track(Config.gems);
		case 7:  return track(Config.essences);
		case 8:  return track(Config.shards);
		case 9:  return track(Config.consumables);
		case 10: return RuneText(Config.minRune);
		case 11: return RuneText(Config.announceRune);
		case 12: return onOff(Config.logUi);
		case 13: return Config.display == "game" ? "Game UI" : Config.display == "overlay" ? "Overlay" : "Off";
		case 14: return Percent(static_cast<std::uint64_t>(Config.x * 100.0f + 0.5f), 100);
		case 15: return Percent(static_cast<std::uint64_t>(Config.y * 100.0f + 0.5f), 100);
		case 16: return "x" + Decimal(Config.textScale, 1);
		case 17: return onOff(Config.background);
		case 18: return onOff(Config.colors);
		case 19: return std::to_string(Config.lastDrops);
		case 20: return std::to_string(Config.pollMs) + " ms";
		case 21: return std::to_string(Config.barChars);
		default: return "";
	}
}

// Steps a setting down (up = false) or up, then saves.
void ChangeSetting(int index, bool up) {
	const auto step = [up](auto& value, auto by, auto low, auto high) { value = std::clamp(static_cast<std::remove_reference_t<decltype(value)>>(value + (up ? by : -by)), low, high); };
	static const char* panels[] { "game", "overlay", "off" };
	switch (index) {
		case 0:  Config.active = !Config.active; break;
		case 1:  Config.uniques = (Config.uniques + 1) % 3; break;
		case 2:  Config.sets = (Config.sets + 1) % 3; break;
		case 3:  Config.rares = (Config.rares + 1) % 3; break;
		case 4:  Config.magic = (Config.magic + 1) % 3; break;
		case 5:  Config.normal = (Config.normal + 1) % 3; break;
		case 6:  Config.gems = (Config.gems + 1) % 3; break;
		case 7:  Config.essences = (Config.essences + 1) % 3; break;
		case 8:  Config.shards = (Config.shards + 1) % 3; break;
		case 9:  Config.consumables = (Config.consumables + 1) % 3; break;
		case 10: Config.minRune = (Config.minRune + (up ? 1 : 33)) % 34; break;
		case 11: Config.announceRune = (Config.announceRune + (up ? 1 : 33)) % 34; break;
		case 12: Config.logUi = !Config.logUi; break;
		case 13: {
			int at = 0;
			while (at < 2 && Config.display != panels[at]) ++at;
			Config.display = panels[(at + (up ? 1 : 2)) % 3];
			break;
		}
		case 14: step(Config.x, 0.01f, 0.0f, 1.0f); break;
		case 15: step(Config.y, 0.01f, 0.0f, 1.0f); break;
		case 16: step(Config.textScale, 0.1f, 0.5f, 3.0f); break;
		case 17: Config.background = !Config.background; break;
		case 18: Config.colors = !Config.colors; break;
		case 19: step(Config.lastDrops, 1, 0, 20); break;
		case 20: step(Config.pollMs, 100, 100, 10000); break;
		case 21: step(Config.barChars, 10, 10, 120); break;
		default: return;
	}
	SaveSettings();
	Log(false, "settings: %s set to %s", SettingRows[index].label, SettingValue(index).c_str());
}

void BuildSettings(Page& page, std::map<std::string, std::string>& labels) {
	// Tracking active on its own at the top, then one column per group.
	const auto row = [&](int i, int column, int at) {
		const auto&       setting = SettingRows[i];
		const int         x = ColumnX[column], y = Page::Y(at);
		const std::string down = "CfgD" + std::to_string(i), up = "CfgU" + std::to_string(i);
		page.Text(x, y, 400, setting.label, '0');
		if (IsToggle(i)) {
			labels[up] = SettingValue(i);
			page.json += Button(up, "toggle", x + 420, y + 2, up, "cfg" + std::to_string(i) + "-1");
		} else {
			labels[down] = "<";
			labels[up]   = ">";
			page.json += Button(down, "row", x + 420, y + 2, down, "cfg" + std::to_string(i) + "-0");
			page.Text(x + 500, y, 300, SettingValue(i), '4', "center");
			page.json += Button(up, "row", x + 810, y + 2, up, "cfg" + std::to_string(i) + "-1");
		}
		page.Text(x, Page::Y(at + 1), ColumnWidth, setting.hint, '5');
	};
	row(0, 0, 0);
	page.Header(0, 3, "What is tracked");
	page.Header(1, 3, "Panel and stats");
	int rows[2] { 4, 4 };
	for (int i = 1; i < SettingCount; ++i) {
		const int column = SettingRows[i].column;
		row(i, column, rows[column]);
		rows[column] += 2;
	}
}

// Caller holds Lock. Deletes the given games (or all) and their items.
void DeleteRuns(bool everything, const std::vector<std::size_t>& matching = {}) {
	std::unordered_set<std::uint32_t> ids;
	std::vector<std::pair<std::int64_t, std::int64_t>> townSpans; // items of games in town have no run number
	std::vector<bool>         remove(AllRuns.size(), everything);
	for (const auto i : matching) {
		remove[i] = true;
		if (AllRuns[i].id) ids.insert(AllRuns[i].id);
		else townSpans.push_back({ AllRuns[i].stamp, Stamp(AllRuns[i].end) + 2 });
	}
	std::size_t runsBefore = AllRuns.size(), itemsBefore = AllItems.size();
	std::vector<RunRecord> keptRuns;
	for (std::size_t i = 0; i < AllRuns.size(); ++i)
		if (!remove[i]) keptRuns.push_back(AllRuns[i]);
	std::vector<ItemRecord> keptItems;
	for (const auto& item : AllItems) {
		bool drop = everything || ids.contains(item.run);
		if (!drop && !item.run) {
			const auto when = Stamp(item.time);
			for (const auto& [from, to] : townSpans) drop = drop || (when >= from && when <= to);
		}
		if (!drop) keptItems.push_back(item);
	}
	AllRuns  = std::move(keptRuns);
	AllItems = std::move(keptItems);
	if (everything) {
		// Start again from run 1: the game in progress loses what it picked up
		// so far, and games still being saved are not written.
		++Resets;
		Current.items.clear();
		if (InGame && Current.number) Current.number = 1;
		NextRun = InGame && Current.number ? 2 : 1;
	}
	RewriteFiles("backup");
	ResetResult = "Deleted " + std::to_string(runsBefore - AllRuns.size()) + " games and " + std::to_string(itemsBefore - AllItems.size()) + " items.";
	Log(false, "%s", ResetResult.c_str());
	if (!Filter.area.empty() && AreaValues().size() == 1) Filter.area.clear();
}

// ---- strings and layouts ----

std::string StringsJson(const std::map<std::string, std::string>& strings) {
	std::string json = "[";
	for (const auto& [key, value] : strings) {
		json += std::string(json.size() > 1 ? ",\n" : "\n") + R"({"Key": )" + Json(key);
		// An empty translation shows nothing; every language gets the same text.
		for (const char* language : Languages) json += std::string(", \"") + language + "\": " + Json(value.empty() ? " " : value);
		json += "}";
	}
	return json + "\n]";
}

// Registers the stats strings, replacing the previous ones.
bool RegisterStrings(const D2RL::PluginContext* context, const std::string& json) {
	if (!ResourceApi) return false;
	if (StringsHandle != D2RL::Resources::InvalidHandle) {
		(void)ResourceApi->unregisterResource(context, StringsHandle);
		StringsHandle = D2RL::Resources::InvalidHandle;
	}
	const D2RL::Resources::ResourceRegistration strings {
		.structSize = D2RL::Resources::ResourceRegistrationSize,
		.path       = StringsPath,
		.bytes      = json.data(),
		.byteCount  = json.size(),
	};
	const auto result = ResourceApi->registerResource(context, &strings, &StringsHandle);
	if (result != D2RL::Resources::Result::Success) {
		StringsHandle = D2RL::Resources::InvalidHandle;
		Log(true, "stats panel: could not register its text (result %u)", static_cast<unsigned>(result));
		return false;
	}
	return true;
}

// A button with the plugin's own sprite and a plugin string as its label.
std::string Button(const std::string& name, const char* sprite, int x, int y, const std::string& key, const std::string& command) {
	char head[256] {};
	std::snprintf(head, sizeof(head), R"(, {"type": "ButtonWidget", "name": "%s", "fields": {"rect": {"x": %d, "y": %d}, "filename": "d2rloader\\run-counter\\%s", )", name.c_str(), x, y, sprite);
	return head + std::string(R"("normalFrame": 0, "hoveredFrame": 1, "pressedFrame": 2, "textString": "@run-counter:)") + key
	     + R"(", "pointSize": "$MediumFontSize", "textColor": "$FontColorWhite", "sound": "cursor_menu_select_hd", "onClickMessage": )"
	     + Json(std::string("PanelManager:ClosePanel:") + CommandPrefix + command) + "}}";
}

// The layout of a tab, with its contents, and the button labels it uses.
std::string TabLayout(const std::string& id, int tab, std::map<std::string, std::string>& labels) {
	Page page;
	{
		std::lock_guard guard(Lock);
		// Drop a filter value that no longer exists (deleted runs).
		const auto areas = AreaValues();
		if (std::find(areas.begin(), areas.end(), Filter.area) == areas.end()) Filter.area.clear();
		// The game in progress counts in the stats tabs until it is written.
		RunRecord               live;
		std::vector<ItemRecord> liveItems;
		const bool              withLive = tab <= 2 && LiveGame(live, liveItems);
		if (withLive) {
			AllRuns.push_back(live);
			AllItems.insert(AllItems.end(), liveItems.begin(), liveItems.end());
		}
		if (tab == 0) BuildOverview(page, GetTickCount64());
		else if (tab == 1) BuildEquipment(page);
		else if (tab == 2) BuildMisc(page);
		else if (tab == 3) BuildReset(page, labels);
		else BuildSettings(page, labels);
		if (withLive) {
			AllRuns.pop_back();
			AllItems.resize(AllItems.size() - liveItems.size());
		}
	}
	char head[640] {};
	std::snprintf(head, sizeof(head),
	              R"({"type": "Panel", "name": "run-counter/%s", "fields": {"anchor": {"x": 0.5, "y": 0.5}, "priority": 8500}, "children": [)"
	              R"({"type": "RectangleWidget", "name": "Background", "fields": {"rect": {"x": %d, "y": %d, "width": %d, "height": %d}, )"
	              R"("color": [0.0, 0.0, 0.0, 0.88], "borderColor": "$TooltipBorderColor"}})"
              // Keeps clicks on the panel from reaching the game (moving the character).
              R"(, {"type": "ClickCatcherWidget", "name": "Catcher", "fields": {"rect": {"x": %d, "y": %d, "width": %d, "height": %d}}})"
	              R"(, {"type": "RectangleWidget", "name": "Probe%u", "fields": {"rect": {"x": 0, "y": 0, "width": 1, "height": 1}, "color": [0.0, 0.0, 0.0, 0.0]}})",
	              id.c_str(), PanelLeft, PanelTop, PanelWidth, PanelHeight, PanelLeft, PanelTop, PanelWidth, PanelHeight, StatsSerial);
	std::string layout = head;
	for (int t = 0; t < StatsTabs; ++t) {
		labels["Tab" + std::to_string(t)] = TabNames[t];
		layout += Button("Tab" + std::to_string(t), t == tab ? "tabon" : "tab", PanelLeft + 20 + t * TabStep, TabTop, "Tab" + std::to_string(t), "tab" + std::to_string(t));
	}
	labels["Close"] = "X";
	layout += Button("Close", "small", PanelLeft + PanelWidth - SmallWidth - 20, TabTop + 7, "Close", "close");
	if (tab == 3)
		for (int b = 0; b < 2; ++b)
			layout += Button("Del" + std::to_string(b), "wide", ColumnX[0], RowsTop + ResetButtonRows[b] * RowHeight, "Del" + std::to_string(b), "del" + std::to_string(b));
	layout += page.json;
	if (tab >= 1 && tab <= 3) {
		// The selectors, then the open one's list on top of everything.
		std::vector<Choices> choices;
		{
			std::lock_guard guard(Lock);
			for (int s = 0; s < 4; ++s) choices.push_back(SelectorChoices(tab, s));
		}
		for (int s = 0; s < 4; ++s) {
			const auto& choice = choices[static_cast<std::size_t>(s)];
			labels["Sel" + std::to_string(s)] = SelectorName(tab, s) + ": " + Fit(choice.labels[static_cast<std::size_t>(choice.current)], 24) + (OpenList == s ? "  ^" : "  v");
			layout += Button("Sel" + std::to_string(s), "select", PanelLeft + 20 + s * SelectorStep, SelectorTop, "Sel" + std::to_string(s), "sel" + std::to_string(s));
		}
		if (OpenList >= 0 && OpenList < 4) {
			const auto& choice  = choices[static_cast<std::size_t>(OpenList)];
			const int   count   = static_cast<int>(std::min<std::size_t>(choice.labels.size(), ItemRows * 4));
			const int   columns = (count + ItemRows - 1) / ItemRows;
			const int   rows    = std::min(count, ItemRows);
			const int   x0      = PanelLeft + 20 + OpenList * SelectorStep;
			const int   step    = OpenList >= 2 ? -(ItemWidth + 10) : ItemWidth + 10; // lists of the right selectors grow to the left
			const int   top     = SelectorTop + ButtonHeight + 6;
			const int   left    = step < 0 ? x0 + (columns - 1) * step : x0;
			char        back[256] {};
			std::snprintf(back, sizeof(back), R"(, {"type": "RectangleWidget", "name": "ListBackground", "fields": {"rect": {"x": %d, "y": %d, "width": %d, "height": %d}, "color": [0.0, 0.0, 0.0, 0.97], "borderColor": "$TooltipBorderColor"}})",
			              left - 8, top - 8, columns * (ItemWidth + 10) + 6, rows * ItemStep + 12);
			layout += back;
			for (int i = 0; i < count; ++i) {
				const std::string key = "Opt" + std::to_string(i);
				labels[key]           = Fit(choice.labels[static_cast<std::size_t>(i)], 34);
				layout += Button(key, i == choice.current ? "itemon" : "item", x0 + (i / ItemRows) * step, top + (i % ItemRows) * ItemStep, key,
				                 "pick" + std::to_string(OpenList) + "-" + std::to_string(i));
			}
		}
	}
	return layout + "]}";
}

// Sprites drawn here at the size they are used (layouts only scale both ways):
// a dark plate with a bronze bevel; frames 0 normal, 1 hovered, 2 pressed.
// "tabon" is the open tab: brighter, with a gold edge.
std::string BuildSprite(int width, int height, bool active) {
	constexpr int frames = 3;
	std::string   out(40 + static_cast<std::size_t>(width) * frames * height * 4, '\0');
	const auto    put = [&](std::size_t at, std::uint32_t value, int bytes) {
        for (int i = 0; i < bytes; ++i) out[at + i] = static_cast<char>((value >> (8 * i)) & 0xFF);
	};
	std::memcpy(out.data(), "SpA1", 4);
	put(4, 31, 2);
	put(6, static_cast<std::uint32_t>(width), 2);
	put(8, static_cast<std::uint32_t>(width * frames), 4);
	put(12, static_cast<std::uint32_t>(height), 4);
	put(20, frames, 4);
	const int border = std::max(2, height / 20);
	for (int frame = 0; frame < frames; ++frame)
		for (int y = 0; y < height; ++y)
			for (int x = 0; x < width; ++x) {
				const int edge = std::min({ x, y, width - 1 - x, height - 1 - y });
				int       rgba[4] {};
				if (edge < 1) rgba[0] = 20, rgba[1] = 16, rgba[2] = 12, rgba[3] = 255;
				else if (edge < 1 + border) {
					const int  base[3] { active ? 222 : 128, active ? 186 : 110, active ? 100 : 76 };
					const bool lit   = std::min(x, y) == edge;
					const int  shade = (frame == 2 ? -1 : 1) * (lit ? 24 : -24) + (frame == 1 ? 40 : 0);
					for (int c = 0; c < 3; ++c) rgba[c] = std::clamp(base[c] + shade, 0, 255);
					rgba[3] = 255;
				} else {
					const int fill = (active ? 46 : 22) + (frame == 1 ? 18 : 0) - (frame == 2 ? 10 : 0);
					rgba[0] = fill + 6, rgba[1] = fill + 3, rgba[2] = fill, rgba[3] = 235;
				}
				const std::size_t at = 40 + (static_cast<std::size_t>(y) * width * frames + static_cast<std::size_t>(frame) * width + x) * 4;
				for (int c = 0; c < 4; ++c) out[at + c] = static_cast<char>(rgba[c]);
			}
	return out;
}

bool RegisterResource(const D2RL::PluginContext* context, const std::string& path, const std::string& bytes) {
	static std::vector<std::string> kept; // the loader copies the bytes, but keep them anyway
	kept.push_back(bytes);
	const D2RL::Resources::ResourceRegistration resource {
		.structSize = D2RL::Resources::ResourceRegistrationSize,
		.path       = path.c_str(),
		.bytes      = kept.back().data(),
		.byteCount  = kept.back().size(),
	};
	D2RL::Resources::RegistrationHandle handle = D2RL::Resources::InvalidHandle;
	return ResourceApi->registerResource(context, &resource, &handle) == D2RL::Resources::Result::Success;
}

// ---- opening tabs (UI thread) ----

void CloseStats() {
	for (const auto& shown : StatsPanels)
		if (IsOpen(shown.panel)) (void)PanelApi->closePanel(Context, shown.panel);
}

bool StatsOpen() { return ShownPanel >= 0 && IsOpen(StatsPanels[static_cast<std::size_t>(ShownPanel)].panel); }

// The panel for the next view: the other of the two that take turns, or one
// never opened while reopened panels keep their old layout.
int NextPanel() {
	const int count = static_cast<int>(StatsPanels.size());
	if (ReuseWorks || count < 3) return count < 2 ? 0 : ShownPanel == 0 ? 1 : 0;
	for (int i = 2; i < count; ++i)
		if (!StatsPanels[static_cast<std::size_t>(i)].opened) return i;
	static bool told = false;
	if (!told) Say("Run Counter: the stats panel has run out of fresh panels; restart the game to see new numbers");
	told = true;
	return ShownPanel == 0 ? 1 : 0;
}

// Shows a tab with the current numbers.
void ShowTab(int tab) {
	if (!PanelApi || !ResourceApi || StatsPanels.empty()) {
		Say("Run Counter: the stats panel is not available (see the log); use the runs command");
		return;
	}
	if (tab != CurrentTab) OpenList = -1;
	CurrentTab = tab;
	const int   index = NextPanel();
	auto&       next  = StatsPanels[static_cast<std::size_t>(index)];
	++StatsSerial;
	std::map<std::string, std::string> labels;
	const std::string                  layout = TabLayout(next.id, tab, labels);
	const bool                         text   = RegisterStrings(Context, StringsJson(labels));
	CloseStats();
	// Make D2R drop the widgets it kept for this panel, so it reads the new layout.
	if (next.opened && WidgetApi) {
		const std::string           name = "run-counter/" + next.id;
		const D2RL::Widgets::UiAction unload { .structSize = D2RL::Widgets::UiActionSize, .flags = 0, .target = "PanelManager", .command = "UnloadPanel", .text = name.c_str() };
		(void)WidgetApi->dispatchUiAction(Context, &unload);
	}
	if (next.layout != D2RL::Resources::InvalidHandle) (void)ResourceApi->unregisterResource(Context, next.layout);
	next.layout             = D2RL::Resources::InvalidHandle;
	const std::string path = "data/global/ui/layouts/run-counter/" + next.id + "hd.json";
	const D2RL::Resources::ResourceRegistration resource {
		.structSize = D2RL::Resources::ResourceRegistrationSize,
		.path       = path.c_str(),
		.bytes      = layout.data(),
		.byteCount  = layout.size(),
	};
	const auto registered = ResourceApi->registerResource(Context, &resource, &next.layout);
	const auto result     = PanelApi->openPanel(Context, next.panel);
	if (registered != D2RL::Resources::Result::Success || result != D2RL::Panels::Result::Success || !text)
		Log(true, "stats panel: tab %s on %s: layout result %u, labels %s, open result %u", TabNames[tab], next.id.c_str(), static_cast<unsigned>(registered), text ? "updated" : "not updated",
		    static_cast<unsigned>(result));
	if (ReuseWorks && next.opened) {
		ProbeName  = "Probe" + std::to_string(StatsSerial);
		ProbePanel = "run-counter/" + next.id;
		ProbeUntil = GetTickCount64() + 3000;
	}
	next.opened = true;
	ShownPanel  = index;
}

// UI tick: once a reused panel is open, checks that it shows the new layout.
void StatsProbe() {
	if (ProbeName.empty() || !WidgetApi) return;
	D2RL::Widgets::WidgetHandle root = D2RL::Widgets::InvalidHandle, probe = D2RL::Widgets::InvalidHandle;
	if (WidgetApi->findPanel(Context, ProbePanel.c_str(), &root) == D2RL::Widgets::Result::Success
	    && WidgetApi->findWidget(Context, root, ProbeName.c_str(), &probe) == D2RL::Widgets::Result::Success) {
		static bool logged = false;
		if (!logged) Log(false, "stats panel: a reopened panel shows its new layout");
		logged = true;
		ProbeName.clear();
		return;
	}
	if (GetTickCount64() < ProbeUntil) return;
	ProbeName.clear();
	if (!StatsOpen()) return;
	ReuseWorks = false;
	Log(true, "stats panel: a reopened panel kept its old layout; each view now takes a new panel (%d in all)", PoolSize - 2);
	ShowTab(CurrentTab);
}

// UI thread: opens the stats panel with fresh numbers, or closes it.
void __cdecl ToggleStats(const D2RL::PluginContext*, void*) noexcept {
	StatsQueued.store(false);
	if (StatsOpen()) CloseStats();
	else {
		OpenList = -1;
		ShowTab(CurrentTab);
	}
}

auto __cdecl OnStatsAction(const D2RL::PluginContext*, const D2RL::Input::ActionEvent* event, void*) noexcept -> D2RL::Input::ActionResult {
	if (!D2RL::Input::HasActionEventField(event, D2RL::Input::ActionEventRequiredSize) || event->kind != D2RL::Input::ActionEventKind::Pressed) return D2RL::Input::ActionResult::Ignored;
	if (Threads && !StatsQueued.exchange(true) && Threads->runOnUiThread(Context, ToggleStats, nullptr) != D2RL::Threads::Result::Success) StatsQueued.store(false);
	return D2RL::Input::ActionResult::Handled;
}

auto StatsCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
	if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
	ToggleStats(command->plugin, nullptr);
	return D2RL::ConsoleCommandResult::Handled;
}

// ---- button commands ----

std::string PendingCommand; // UI thread

void __cdecl RunCommand(const D2RL::PluginContext*, void*) noexcept {
	const std::string command = std::move(PendingCommand);
	PendingCommand.clear();
	Log(false, "stats panel: button %s", command.c_str());
	if (command == "close") {
		CloseStats();
		return;
	}
	int tab = CurrentTab;
	if (command.rfind("tab", 0) == 0 && command.size() == 4) tab = std::clamp(command[3] - '0', 0, StatsTabs - 1);
	else if (command.rfind("sel", 0) == 0 && command.size() == 4) {
		const int selector = command[3] - '0';
		OpenList           = OpenList == selector ? -1 : selector;
	} else if (command.rfind("pick", 0) == 0 && command.size() > 6) {
		const int       selector = command[4] - '0';
		const int       index    = std::atoi(command.c_str() + 6);
		std::lock_guard guard(Lock);
		PickChoice(tab, selector, index);
		OpenList      = -1;
		ConfirmAction = -1;
		ResetResult.clear();
	} else if (command.rfind("cfg", 0) == 0 && command.size() > 5) {
		const int index = std::atoi(command.c_str() + 3);
		ChangeSetting(index, command.back() == '1');
	} else if (command.rfind("run", 0) == 0 && command.size() > 3) {
		const auto      index = static_cast<std::size_t>(std::strtoul(command.c_str() + 3, nullptr, 10));
		std::lock_guard guard(Lock);
		OpenList = -1;
		if (index < AllRuns.size()) {
			const auto stamp = AllRuns[index].stamp;
			if (ConfirmAction == 2 && ConfirmStamp == stamp && GetTickCount64() < ConfirmUntil) {
				ConfirmAction = -1;
				DeleteRuns(false, { index });
			} else {
				ConfirmAction = 2;
				ConfirmStamp  = stamp;
				ConfirmUntil  = GetTickCount64() + 10000;
				ResetResult.clear();
			}
		}
	} else if (command == "del0" || command == "del1") {
		const int       action = command[3] - '0';
		std::lock_guard guard(Lock);
		OpenList = -1;
		if (ConfirmAction == action && GetTickCount64() < ConfirmUntil) {
			ConfirmAction = -1;
			DeleteRuns(action == 1, action == 1 ? std::vector<std::size_t>() : MatchingRuns());
		} else {
			ConfirmAction = action;
			ConfirmUntil  = GetTickCount64() + 10000;
			ResetResult.clear();
		}
	}
	ShowTab(tab);
}

// The game's UI messages, written to run-counter-ui.log (distinct ones, up to
// 600 per launch) to find what it reports, such as terror zones.
void LogUiMessage(const Events::UiMessageEvent* event) {
	static std::unordered_set<std::string> seen;
	static FILE*                           out {};
	if (seen.size() >= 600 || LogDirectory.empty()) return;
	const std::string line = std::string(event->target ? event->target : "") + " | " + (event->command ? event->command : "") + " | " + (event->text ? event->text : "");
	if (!seen.insert(line).second) return;
	if (!out) out = _wfopen(Wide(LogDirectory + "run-counter-ui.log").c_str(), L"wb");
	if (!out) return;
	std::fprintf(out, "%s %s\r\n", Now().c_str(), line.substr(0, 400).c_str());
	std::fflush(out);
}

auto OnUiMessage(const D2RL::PluginContext*, const Events::UiMessageEvent* event, void*) noexcept -> Events::UiMessageAction {
	if (!event) return Events::UiMessageAction::Continue;
	if (Config.logUi) LogUiMessage(event);
	// The pause menu (Escape) stops the game offline, so it stops the clocks.
	if (event->target && event->command && event->text && std::strcmp(event->target, "PanelManager") == 0 && std::strncmp(event->text, "PauseLayout", 11) == 0) {
		if (std::strcmp(event->command, "OpenPanel") == 0 || std::strcmp(event->command, "OpenExclusivePanel") == 0) SetPaused(true);
		else if (std::strcmp(event->command, "ClosePanel") == 0 || std::strcmp(event->command, "UnloadPanel") == 0) SetPaused(false);
		return Events::UiMessageAction::Continue;
	}
	const std::size_t prefix = std::strlen(CommandPrefix);
	if (!event->text || std::strncmp(event->text, CommandPrefix, prefix) != 0) return Events::UiMessageAction::Continue;
	// D2R reports a button's message twice, about a millisecond apart.
	static std::string last;
	static ULONGLONG   lastMs {};
	const ULONGLONG    now = GetTickCount64();
	if (last == event->text && now - lastMs < 150) return Events::UiMessageAction::Consume;
	last   = event->text;
	lastMs = now;
	if (!PendingCommand.empty()) return Events::UiMessageAction::Consume; // one at a time
	PendingCommand = event->text + prefix;
	if (!Threads || Threads->runOnUiThread(Context, RunCommand, nullptr) != D2RL::Threads::Result::Success) PendingCommand.clear();
	return Events::UiMessageAction::Consume;
}

void RegisterStatsPanels(const D2RL::PluginContext* context) {
	if (!PanelApi || !ResourceApi || !Threads) {
		Log(true, "stats panel: missing services, no stats panel");
		return;
	}
	struct SpriteSpec {
		const char* name;
		int         width, height;
		bool        active;
	};
	for (const auto& sprite : { SpriteSpec { "tab", TabWidth, TabHeight, false }, SpriteSpec { "tabon", TabWidth, TabHeight, true }, SpriteSpec { "wide", WideWidth, ButtonHeight, false },
	                            SpriteSpec { "select", SelectorWidth, ButtonHeight, false }, SpriteSpec { "item", ItemWidth, ItemHeight, false },
	                            SpriteSpec { "itemon", ItemWidth, ItemHeight, true }, SpriteSpec { "small", SmallWidth, ButtonHeight, false },
	                            SpriteSpec { "row", RowButtonWidth, RowHeight - 4, false }, SpriteSpec { "toggle", ToggleWidth, RowHeight - 4, false } }) {
		const std::string path = std::string("data/hd/global/ui/d2rloader/run-counter/") + sprite.name;
		// Low-end graphics read a half-size .lowend.sprite next to the HD one.
		if (!RegisterResource(context, path + ".sprite", BuildSprite(sprite.width, sprite.height, sprite.active))
		    || !RegisterResource(context, path + ".lowend.sprite", BuildSprite(sprite.width / 2, sprite.height / 2, sprite.active)))
			Log(true, "stats panel: could not register the sprite %s", sprite.name);
	}
	for (int i = 1; i <= PoolSize; ++i) {
		StatsPanel                            shown { .id = "Stats" + std::to_string(i) };
		const D2RL::Panels::PanelRegistration registration {
			.structSize = D2RL::Panels::PanelRegistrationSize,
			.flags      = D2RL::Panels::PanelFlags::CloseOnEscape,
			.localId    = shown.id.c_str(),
		};
		if (PanelApi->registerPanel(context, &registration, &shown.panel) != D2RL::Panels::Result::Success) break;
		StatsPanels.push_back(std::move(shown));
	}
	if (StatsPanels.size() < 2) Log(true, "stats panel: could register only %zu panels", StatsPanels.size());
	if (EventApi) {
		const Events::UiMessageListener listener {
			.structSize = Events::UiMessageListenerSize,
			.priority   = 100,
			.callback   = OnUiMessage,
		};
		Events::ListenerHandle handle = Events::InvalidHandle;
		if (EventApi->registerUiMessageListener(context, &listener, &handle) != Events::Result::Success) Log(true, "stats panel: could not listen to button clicks");
	} else Log(true, "stats panel: no shared event service, the tabs and buttons will not respond");
	Log(false, "stats panel: ready, %zu panels registered", StatsPanels.size());
}

// ---- latest drops under the game panel ----
// The game panel can only show and hide its pieces, so the item names go in
// a panel of their own, rebuilt like the stats tabs when the list changes:
// two panels take turns, each unloaded before it is opened again.

struct DropsPanel {
	std::string                         id;
	D2RL::Panels::RegistrationHandle    panel  = D2RL::Panels::InvalidHandle;
	D2RL::Resources::RegistrationHandle layout = D2RL::Resources::InvalidHandle;
	bool                                opened {};
};
DropsPanel  DropsPanels[2];
int         DropsShown = -1;
std::string DropsKey; // what the shown list holds, to rebuild it only when it changes

void RegisterDropsPanels(const D2RL::PluginContext* context) {
	for (int i = 0; i < 2; ++i) {
		auto& drops = DropsPanels[i];
		drops.id    = "Drops" + std::to_string(i + 1);
		const D2RL::Panels::PanelRegistration registration {
			.structSize = D2RL::Panels::PanelRegistrationSize,
			.flags      = D2RL::Panels::PanelFlags::None,
			.localId    = drops.id.c_str(),
		};
		if (PanelApi->registerPanel(context, &registration, &drops.panel) != D2RL::Panels::Result::Success) {
			drops.panel = D2RL::Panels::InvalidHandle;
			Log(true, "game-UI panel: could not register the drops list");
		}
	}
}

char QualityCode(const ItemRecord& item) {
	if (item.rune) return '8';
	switch (item.quality) {
		case Items::Quality::Unique:  return '4';
		case Items::Quality::Set:     return '2';
		case Items::Quality::Rare:    return '9';
		case Items::Quality::Magic:   return '3';
		case Items::Quality::Crafted: return '8';
		default:                      return '0';
	}
}

void CloseDrops() {
	for (const auto& drops : DropsPanels)
		if (IsOpen(drops.panel)) (void)PanelApi->closePanel(Context, drops.panel);
	DropsShown = -1;
	DropsKey.clear();
}

// UI thread: shows this game's latest drops under the game panel, or hides them.
void RefreshDrops(bool want) {
	if (!PanelApi || !ResourceApi || DropsPanels[0].panel == D2RL::Panels::InvalidHandle || DropsPanels[1].panel == D2RL::Panels::InvalidHandle) return;
	std::vector<std::pair<std::string, char>> lines;
	if (want && Config.lastDrops > 0) {
		std::lock_guard guard(Lock);
		// Most recent first, without runes below announce_rune; uniques and
		// sets by their base name, as before identifying them.
		for (std::size_t n = Current.items.size(); n-- > 0 && lines.size() < static_cast<std::size_t>(Config.lastDrops);) {
			const auto& item = Current.items[n].record;
			if (item.rune && (!Config.announceRune || item.rune < Config.announceRune)) continue;
			if (!item.rune && Tracking(item.cls, item.quality, item.code) == TrackHidden) continue;
			const bool  named = item.quality == Items::Quality::Unique || item.quality == Items::Quality::Set;
			// The color a mod gave the name, else the quality's.
			lines.push_back({ Fit((named ? BaseText(item.code) : item.name) + (item.ethereal ? " (eth)" : ""), 13), !named && item.color ? item.color : QualityCode(item) });
		}
	}
	if (lines.empty()) {
		if (DropsShown >= 0) CloseDrops();
		return;
	}
	std::string key = std::to_string(Config.background) + std::to_string(Session);
	for (const auto& [name, color] : lines) key += "|" + name + color;
	if (key == DropsKey && IsOpen(DropsPanels[DropsShown < 0 ? 0 : DropsShown].panel)) return;

	const float s = Config.textScale;
	const int   height = static_cast<int>(36 * s), half = static_cast<int>(12 * s), width = static_cast<int>(130 * s) + 12 * half, top = 6 * height + half;
	const int   index = DropsShown == 0 ? 1 : 0;
	auto&       next  = DropsPanels[index];
	std::string layout = R"({"type": "Panel", "name": "run-counter/)" + next.id + R"(", "fields": {"anchor": {"x": )" + Fraction(Config.x) + R"(, "y": )" + Fraction(Config.y)
	                   + R"(}, "priority": -2}, "children": [)";
	if (Config.background) {
		char back[200] {};
		std::snprintf(back, sizeof(back), R"({"type": "RectangleWidget", "name": "Back", "fields": {"rect": {"x": %d, "y": %d, "width": %d, "height": %d}, "color": [0.0, 0.0, 0.0, 0.55]}}, )", -half,
		              top - half / 2, width, static_cast<int>(lines.size()) * height + half);
		layout += back;
	}
	for (std::size_t i = 0; i < lines.size(); ++i)
		layout += (i ? ", " : "") + TextBox("Drop" + std::to_string(i), 0, top + static_cast<int>(i) * height, width - half, height, lines[i].first, FontColor(lines[i].second));
	layout += "]}";

	if (DropsShown >= 0 && IsOpen(DropsPanels[DropsShown].panel)) (void)PanelApi->closePanel(Context, DropsPanels[DropsShown].panel);
	if (next.opened && WidgetApi) {
		const std::string             name = "run-counter/" + next.id;
		const D2RL::Widgets::UiAction unload { .structSize = D2RL::Widgets::UiActionSize, .flags = 0, .target = "PanelManager", .command = "UnloadPanel", .text = name.c_str() };
		(void)WidgetApi->dispatchUiAction(Context, &unload);
	}
	if (next.layout != D2RL::Resources::InvalidHandle) (void)ResourceApi->unregisterResource(Context, next.layout);
	next.layout             = D2RL::Resources::InvalidHandle;
	const std::string path = "data/global/ui/layouts/run-counter/" + next.id + "hd.json";
	const D2RL::Resources::ResourceRegistration resource {
		.structSize = D2RL::Resources::ResourceRegistrationSize,
		.path       = path.c_str(),
		.bytes      = layout.data(),
		.byteCount  = layout.size(),
	};
	const auto registered = ResourceApi->registerResource(Context, &resource, &next.layout);
	const auto opened     = PanelApi->openPanel(Context, next.panel);
	if (registered != D2RL::Resources::Result::Success || opened != D2RL::Panels::Result::Success)
		Log(true, "game-UI panel: drops list on %s: layout result %u, open result %u", next.id.c_str(), static_cast<unsigned>(registered), static_cast<unsigned>(opened));
	next.opened = true;
	DropsShown  = index;
	DropsKey    = key;
}
