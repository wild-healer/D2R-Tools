// The 34 scheduled terror zones of desecratedzones.json (Steam 3.3.94995),
// matched in game by their first level id. Generated from the game files.
#pragma once

struct ZoneInfo {
	const char* key;
	int         act;
	const char* label;
	int         firstLevel;
	const char* levels;
};

constexpr ZoneInfo Zones[] {
	{ "a1_burial_grounds", 1, "Burial Grounds", 17, "Burial Grounds, Crypt, Mausoleum" },
	{ "a1_cathedral_and_catacombs", 1, "Cathedral and Catacombs", 33, "Cathedral, Catacombs 1, Catacombs 2, Catacombs 3, Catacombs 4, Inner Cloister" },
	{ "a1_cold_plains_and_cave", 1, "Cold Plains and Cave", 3, "Cold Plains, Cave 1, Cave 2" },
	{ "a1_dark_wood", 1, "Dark Wood", 5, "Dark Wood, Underground Passage 1, Underground Passage 2" },
	{ "a1_blood_moor_and_den_of_evil", 1, "Blood Moor and Den of Evil", 2, "Blood Moor, Den of Evil" },
	{ "a1_barracks_and_jail", 1, "Barracks and Jail", 28, "Barracks, Jail 1, Jail 2, Jail 3" },
	{ "a1_secret_cow_level", 1, "Secret Cow Level", 39, "Secret Cow Level" },
	{ "a1_stony_field_and_tristram", 1, "Stony Field and Tristram", 4, "Stony Field, Tristram" },
	{ "a1_black_marsh_and_tower", 1, "Black Marsh and Tower", 6, "Black Marsh, Hole 1, Hole 2, Forgotten Tower, Tower Cellar 1, Tower Cellar 2, Tower Cellar 3, Tower Cellar 4, Tower Cellar 5" },
	{ "a1_tamoe_highland_and_pit", 1, "Tamoe Highland and Pit", 7, "Tamoe Highland, Outer Cloister, Pit 1, Pit 2, Monastery Gate" },
	{ "a2_lut_gholein_sewers", 2, "Lut Gholein Sewers", 47, "Sewers 1, Sewers 2, Sewers 3" },
	{ "a2_rocky_waste", 2, "Rocky Waste", 41, "Rocky Waste, Stony Tomb 1, Stony Tomb 2" },
	{ "a2_dry_hills", 2, "Dry Hills", 42, "Dry Hills, Halls of the Dead 1, Halls of the Dead 2, Halls of the Dead 3" },
	{ "a2_far_oasis", 2, "Far Oasis", 43, "Far Oasis, Maggot Lair 1, Maggot Lair 2, Maggot Lair 3" },
	{ "a2_lost_city", 2, "Lost City", 44, "Lost City, Valley of Snakes, Claw Viper Temple 1, Claw Viper Temple 2, Ancient Tunnels" },
	{ "a2_tal_rashas_tombs", 2, "Tal Rasha's Tombs", 66, "Tal Rasha's Tomb, Duriel's Lair, Canyon of the Magi" },
	{ "a2_arcane_sanctuary", 2, "Arcane Sanctuary", 74, "Arcane Sanctuary, Harem 1, Harem 2, Palace Cellar 1, Palace Cellar 2, Palace Cellar 3" },
	{ "a3_spider_forest", 3, "Spider Forest", 76, "Spider Forest, Spider Cave, Spider Cavern" },
	{ "a3_great_marsh", 3, "Great Marsh", 77, "Great Marsh" },
	{ "a3_flayer_jungle", 3, "Flayer Jungle", 78, "Flayer Jungle, Flayer Dungeon 1, Flayer Dungeon 2, Flayer Dungeon 3, Swampy Pit 1, Swampy Pit 2, Swampy Pit 3" },
	{ "a3_kurast", 3, "Kurast", 80, "Kurast Bazaar, Lower Kurast, Upper Kurast, Kurast Causeway, Sewers 1, Sewers 2, Ruined Temple, Disused Fane, Forgotten Reliquary, Forgotten Temple, Ruined Fane, Disused Reliquary" },
	{ "a3_travincal", 3, "Travincal", 83, "Travincal" },
	{ "a3_durance_of_hate", 3, "Durance of Hate", 100, "Durance of Hate 1, Durance of Hate 2, Durance of Hate 3" },
	{ "a4_outer_steppes", 4, "Outer Steppes", 104, "Outer Steppes, Plains of Despair" },
	{ "a4_river_of_flame", 4, "River of Flame", 106, "City of the Damned, River of Flame" },
	{ "a4_chaos_sanctuary", 4, "Chaos Sanctuary", 108, "Chaos Sanctuary" },
	{ "a5_bloody_foothills", 5, "Bloody Foothills", 110, "Bloody Foothills, Rigid Highlands, Abaddon" },
	{ "a5_arreat_plateau", 5, "Arreat Plateau", 112, "Arreat Plateau, Pit of Acheron" },
	{ "a5_crystalline_passage", 5, "Crystalline Passage", 113, "Crystalline Passage, Frozen River" },
	{ "a5_nihlathaks_temple", 5, "Nihlathak's Temple", 121, "Nihlathak's Temple, Halls of Anguish, Halls of Death's Calling, Halls of Vaught" },
	{ "a5_glacial_trail", 5, "Glacial Trail", 115, "Glacial Trail, Drifter Cavern" },
	{ "a5_ancients_way", 5, "Ancients' Way", 118, "Ancients' Way, Icy Cellar" },
	{ "a5_frozen_tundra", 5, "Frozen Tundra", 117, "Frozen Tundra, Infernal Pit" },
	{ "a5_worldstone_keep", 5, "Worldstone Keep", 128, "The Worldstone Keep 1, The Worldstone Keep 2, The Worldstone Keep 3, Throne of Destruction, The Worldstone Chamber" },
};

constexpr int ZoneCount = sizeof(Zones) / sizeof(Zones[0]);
