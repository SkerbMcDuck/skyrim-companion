#include "Log.h"

namespace companion::textlog
{
	namespace
	{
		std::mutex            g_lock;
		std::filesystem::path g_folder;
		bool                  g_loaded = false;

		std::filesystem::path Documents()
		{
			// SKSE's log folder is Documents\My Games\Skyrim Special Edition\SKSE.
			if (const auto dir = SKSE::log::log_directory(); dir) {
				return dir->parent_path().parent_path().parent_path();
			}
			return {};
		}

		std::filesystem::path DefaultFolder() { return Documents() / "Skyrim Companion"; }
		std::filesystem::path SettingsFile() { return DefaultFolder() / "settings.json"; }

		void LoadSettings()
		{
			if (g_loaded) {
				return;
			}
			g_loaded = true;
			g_folder = DefaultFolder();
			std::ifstream in(SettingsFile());
			if (!in) {
				return;
			}
			std::stringstream ss;
			ss << in.rdbuf();
			const auto j = json::parse(ss.str(), nullptr, false);
			if (!j.is_discarded() && j.contains("logFolder") && j["logFolder"].is_string()) {
				const std::filesystem::path p = std::filesystem::path(j["logFolder"].get<std::string>());
				if (p.is_absolute()) {
					g_folder = p;
				}
			}
		}

		std::filesystem::path LogFile() { return g_folder / "Companion log.txt"; }
	}

	std::string GameDate()
	{
		const auto cal = RE::Calendar::GetSingleton();
		if (!cal) {
			return {};
		}
		const float hour = cal->GetHour();
		const int   h = static_cast<int>(hour), m = static_cast<int>((hour - h) * 60.0f);
		return std::format("4E {}, {} {}, {}:{:02} {}", cal->GetYear(), static_cast<int>(cal->GetDay()), cal->GetMonthName(), (h + 11) % 12 + 1, m, h < 12 ? "AM" : "PM");
	}

	void Write(const std::string& a_line)
	{
		std::scoped_lock l{ g_lock };
		LoadSettings();
		std::error_code ec;
		std::filesystem::create_directories(g_folder, ec);
		std::ofstream out(LogFile(), std::ios::app | std::ios::binary);
		if (out) {
			out << a_line << "\r\n";
		}
	}

	std::string Read()
	{
		std::scoped_lock l{ g_lock };
		LoadSettings();
		std::ifstream in(LogFile(), std::ios::binary);
		if (!in) {
			return {};
		}
		in.seekg(0, std::ios::end);
		const auto size = static_cast<std::streamoff>(in.tellg());
		const std::streamoff keep = 200 * 1024;
		in.seekg(size > keep ? size - keep : 0);
		std::stringstream ss;
		ss << in.rdbuf();
		return ss.str();
	}

	std::string Folder()
	{
		std::scoped_lock l{ g_lock };
		LoadSettings();
		return g_folder.string();
	}

	bool SetFolder(const std::string& a_folder, std::string& a_error)
	{
		std::scoped_lock l{ g_lock };
		LoadSettings();
		const std::filesystem::path p = std::filesystem::path(a_folder);
		if (!p.is_absolute()) {
			a_error = "Use a full path, like C:\\Users\\You\\Documents\\Skyrim Companion";
			return false;
		}
		std::error_code ec;
		std::filesystem::create_directories(p, ec);
		if (ec) {
			a_error = "Couldn't create that folder";
			return false;
		}
		g_folder = p;
		std::filesystem::create_directories(DefaultFolder(), ec);
		std::ofstream out(SettingsFile(), std::ios::binary | std::ios::trunc);
		out << json{ { "logFolder", p.string() } }.dump(2);
		return true;
	}
}
