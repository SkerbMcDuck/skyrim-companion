#pragma once

// Enemies and other characters around you for the mini map radar and the opponent panel.
// Follows the rules from the design review: an NPC only appears after you've had line of sight,
// stays until it's far away and things are quiet, and anyone fighting you shows while in range.
namespace companion::radar
{
	inline constexpr float kRange = 6000.0f;      // mini map edge (~85 m)
	inline constexpr float kNameRange = 1500.0f;  // close enough to see a name (~21 m)

	json Build(RE::PlayerCharacter* a_player);  // game thread, every tick
	void Reset();

	// Dragonborn's Bestiary resistances/weaknesses for an actor, or null. Game thread.
	json Bestiary(RE::Actor* a_actor);
}
