#pragma once

// Keyboard shortcuts read while playing, so the companion window can be controlled
// without alt-tabbing (which pauses Skyrim).
namespace companion::hotkeys
{
	void Install();

	// Recent presses as [{ "id": n, "cmd": "zoomIn" }, ...]. Safe from any thread.
	json Recent();
}
