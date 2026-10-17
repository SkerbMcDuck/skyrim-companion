#pragma once

// Plain text log of follower deaths, hires and dismissals, kept outside the game folders
// (Documents\Skyrim Companion by default). Only written when one of those things happens.
namespace companion::textlog
{
	void        Write(const std::string& a_line);  // adds the in-game date in front when a game is loaded
	std::string Read();                            // whole file (last ~200 KB)
	std::string Folder();
	bool        SetFolder(const std::string& a_folder, std::string& a_error);
	std::string GameDate();  // "4E 201, 17 Last Seed, 9:42 PM"; game thread only
}
