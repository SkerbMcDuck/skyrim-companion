#pragma once

namespace companion::state
{
	// Called from SKSE messages so we only read the player while a game is loaded.
	void SetGameReady(bool a_ready);

	// Starts the background timer that asks the game thread for a fresh snapshot ~10x/second.
	void StartPolling();

	// Latest snapshots as JSON text. Safe to call from any thread.
	std::string GetStateJson();
	std::string GetMarkersJson();

	// The map screen was opened; markers load from then on (game thread).
	void OnMapOpened();
}
