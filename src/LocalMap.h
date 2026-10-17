#pragma once

// Local map built from the navmesh (the walkable floor plan the game's AI uses). Read once per area,
// never rendered in the game. Explored parts are tracked for this play session only (nothing saved).
namespace companion::localmap
{
	void        Tick(RE::PlayerCharacter* a_player);  // game thread, a few times a second: marks explored floor
	std::string Get(std::uint32_t a_haveVersion);     // game thread: full map if it changed, else {"same":true}
	void        Reset();                              // forget explored areas (new save loaded)
}
