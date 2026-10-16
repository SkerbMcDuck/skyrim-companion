#include "Server.h"
#include "State.h"

namespace
{
	constexpr std::uint16_t kPort = 7171;

	// Folder holding the companion page: Data/SKSE/Plugins/SkyrimCompanion, found next to this DLL.
	std::filesystem::path g_webRoot = "Data/SKSE/Plugins/SkyrimCompanion";

	void FindWebRoot()
	{
		wchar_t buf[1024]{};
		const auto len = REX::W32::GetModuleFileNameW(REX::W32::GetCurrentModule(), buf, 1024);
		if (len > 0 && len < 1024) {
			g_webRoot = std::filesystem::path(buf).parent_path() / "SkyrimCompanion";
		}
		logs::info("Companion page folder: {}", g_webRoot.string());
	}

	std::string ContentTypeFor(const std::filesystem::path& a_file)
	{
		const auto ext = a_file.extension().string();
		if (ext == ".html") return "text/html; charset=utf-8";
		if (ext == ".js") return "text/javascript; charset=utf-8";
		if (ext == ".css") return "text/css; charset=utf-8";
		if (ext == ".json") return "application/json";
		if (ext == ".png") return "image/png";
		if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
		if (ext == ".webp") return "image/webp";
		if (ext == ".svg") return "image/svg+xml";
		if (ext == ".ttf") return "font/ttf";
		return "application/octet-stream";
	}

	companion::server::Response ServeFile(std::string a_path)
	{
		if (a_path == "/" || a_path.empty()) {
			a_path = "/index.html";
		}
		// Refuse anything that tries to climb out of the web folder.
		if (a_path.find("..") != std::string::npos || a_path.find(':') != std::string::npos) {
			return { 404, "text/plain; charset=utf-8", "not found" };
		}

		const auto file = g_webRoot / std::filesystem::path(a_path.substr(1));
		std::ifstream in(file, std::ios::binary);
		if (!in) {
			return { 404, "text/plain; charset=utf-8", "not found: " + a_path };
		}
		std::ostringstream ss;
		ss << in.rdbuf();
		return { 200, ContentTypeFor(file), ss.str() };
	}

	// Map Marker Framework / CoMAP config files from Data/MapMarkers, so the page can
	// show the same custom icons as the in-game map. Sent as raw text (they contain comments).
	std::string MapMarkerConfigs()
	{
		json out = json::object();
		const auto dir = g_webRoot.parent_path().parent_path().parent_path() / "MapMarkers";
		std::error_code ec;
		for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
			if (!entry.is_regular_file() || entry.path().extension() != ".json") {
				continue;
			}
			std::ifstream in(entry.path(), std::ios::binary);
			std::ostringstream ss;
			ss << in.rdbuf();
			out[entry.path().filename().string()] = ss.str();
		}
		return out.dump(-1, ' ', false, json::error_handler_t::replace);
	}

	companion::server::Response Handle(const std::string& a_path)
	{
		if (a_path == "/api/state") {
			return { 200, "application/json", companion::state::GetStateJson() };
		}
		if (a_path == "/api/markers") {
			return { 200, "application/json", companion::state::GetMarkersJson() };
		}
		if (a_path == "/api/mapmarker-configs") {
			return { 200, "application/json", MapMarkerConfigs() };
		}
		if (a_path == "/api/ping") {
			return { 200, "application/json", R"({"ok":true,"app":"SkyrimCompanion"})" };
		}
		return ServeFile(a_path);
	}

	void OnSkseMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			companion::state::StartPolling();
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
			companion::state::SetGameReady(false);
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			companion::state::SetGameReady(true);
			break;
		default:
			break;
		}
	}
}

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	FindWebRoot();

	std::string error;
	if (companion::server::Start(kPort, Handle, error)) {
		logs::info("Skyrim Companion running at http://localhost:{}", kPort);
	} else {
		logs::error("Skyrim Companion could not start its server: {}", error);
	}

	SKSE::GetMessagingInterface()->RegisterListener(OnSkseMessage);
	return true;
}
