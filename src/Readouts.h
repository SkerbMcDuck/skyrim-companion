#pragma once

// Builders for the extra panels. All of these must run on the game's main thread.
namespace companion::readouts
{
	json ActiveEffects(RE::PlayerCharacter* a_player);
	json Survival();
	json Equipment(RE::PlayerCharacter* a_player);
	json Consumables(RE::PlayerCharacter* a_player);
	json Followers(RE::PlayerCharacter* a_player);
	void ResetFollowers();  // call from the game thread when a save loads

	// "Plugin.esp|00ABC" form used by Map Marker Framework / CoMAP configs.
	std::string RefKey(const RE::TESForm* a_form);
}
