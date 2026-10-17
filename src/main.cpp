#include "Server.h"
#include "State.h"
#include "Hotkeys.h"
#include "Feed.h"
#include "GameThread.h"
#include "Readouts.h"
#include "Extras.h"
#include "LocalMap.h"
#include "Log.h"

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
			// Add recent in-game hotkey presses to the snapshot.
			auto text = companion::state::GetStateJson();
			const auto end = text.rfind('}');
			if (end != std::string::npos) {
				text.insert(end, ",\"commands\":" + companion::hotkeys::Recent().dump() +
				                     ",\"events\":" + companion::feed::Recent().dump(-1, ' ', false, json::error_handler_t::replace));
			}
			return { 200, "application/json", text };
		}
		if (a_path == "/api/markers") {
			return { 200, "application/json", companion::state::GetMarkersJson() };
		}
		if (a_path == "/api/mapmarker-configs") {
			return { 200, "application/json", MapMarkerConfigs() };
		}
		// On-demand lookups: these run on the game thread only when the page asks.
		auto onGame = [](std::function<std::string()> a_job) -> companion::server::Response {
			if (const auto text = companion::gamethread::Run(std::move(a_job)); text) {
				return { 200, "application/json", *text };
			}
			return { 503, "application/json", R"({"error":"game busy"})" };
		};
		auto hexTail = [&](std::string_view a_prefix) -> std::optional<RE::FormID> {
			if (!a_path.starts_with(a_prefix)) {
				return std::nullopt;
			}
			try {
				return static_cast<RE::FormID>(std::stoul(a_path.substr(a_prefix.size()), nullptr, 16));
			} catch (...) {
				return std::nullopt;
			}
		};
		if (a_path.starts_with("/api/localmap/")) {
			std::uint32_t have = 0;
			try {
				have = static_cast<std::uint32_t>(std::stoul(a_path.substr(14)));
			} catch (...) {
			}
			return onGame([have] { return companion::localmap::Get(have); });
		}
		if (const auto id = hexTail("/api/follower/"); id) {
			return onGame([id] { return companion::readouts::FollowerDetails(*id); });
		}
		if (const auto id = hexTail("/api/find/"); id) {
			return onGame([id] { return companion::readouts::FindActor(*id); });
		}
		if (const auto id = hexTail("/api/book/"); id) {
			return onGame([id] { return companion::extras::BookText(*id); });
		}
		if (a_path == "/api/log") {
			return { 200, "text/plain; charset=utf-8", companion::textlog::Read() };
		}
		if (a_path == "/api/logfolder") {
			return { 200, "application/json", json{ { "folder", companion::textlog::Folder() } }.dump() };
		}
		if (a_path.starts_with("/api/setlogfolder/")) {
			std::string error;
			const bool ok = companion::textlog::SetFolder(a_path.substr(18), error);
			return { 200, "application/json", json{ { "ok", ok }, { "error", error }, { "folder", companion::textlog::Folder() } }.dump() };
		}
		if (a_path == "/api/hotkeys") {
			// Hotkey Reminder's key list, if installed (read-only).
			const auto data = g_webRoot.parent_path().parent_path().parent_path() / "Interface";
			for (const auto* file : { "hotkeysreminder_data.json", "hotkeysreminder_default.json" }) {
				std::ifstream in(data / file, std::ios::binary);
				if (in) {
					std::ostringstream ss;
					ss << in.rdbuf();
					return { 200, "application/json", ss.str() };
				}
			}
			return { 404, "application/json", "{}" };
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
			companion::hotkeys::Install();
			companion::feed::Install();
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
