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
}
