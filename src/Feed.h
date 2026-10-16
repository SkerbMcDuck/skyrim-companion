#pragma once

// Game events for the hub's alert window (pickups, skill ups, level ups, discoveries) and
// the reference under the crosshair. Everything here only reacts when the game sends an event.
namespace companion::feed
{
	void Install();

	// Recent events as [{ "id": n, "text": "...", "kind": "pickup" }, ...]. Safe from any thread.
	json Recent();

	// Forget queued events (called when a save loads).
	void Clear();

	// The reference under the crosshair, or null. Game thread only.
	RE::NiPointer<RE::TESObjectREFR> CrosshairRef();
}
