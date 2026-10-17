#pragma once

// Batch 1 readers from the design review. All must run on the game's main thread.
namespace companion::extras
{
	json Progress(RE::PlayerCharacter* a_player);      // level XP and skill progress
	json Warmth(RE::PlayerCharacter* a_player);        // warmth rating, cap, heat source state
	json Supplies(RE::PlayerCharacter* a_player);      // lockpicks, soul gems, food hunger tiers, bounties
	json Quests(RE::PlayerCharacter* a_player);        // objectives of active (tracked) quests
	json Crosshair(RE::PlayerCharacter* a_player);     // details for whatever is under the crosshair
	float ShoutCooldown(RE::PlayerCharacter* a_player);
	json  OreVein(RE::TESObjectREFR* a_ref);
	json  Books(RE::PlayerCharacter* a_player);        // books, notes and maps you carry
	std::string BookText(RE::FormID a_id);             // game thread: the book's own text
	json  Character(RE::PlayerCharacter* a_player);    // race start values and level-up spread
	json  Stress();                                    // Stress and Fear
	json  World();                                     // season and moon timing
	void  RequestSeason();                             // asks Seasons of Skyrim (answer arrives later)
	json  OreRange(const json& a_vein, RE::PlayerCharacter* a_player);  // Random Mining values, or null if not an ore vein
}
