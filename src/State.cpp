#include "State.h"

namespace companion::state
{
	namespace
	{
		constexpr auto kPollInterval = std::chrono::milliseconds(100);
		constexpr int  kMarkerRefreshTicks = 50;  // ~5 seconds

		std::atomic<bool> g_gameReady{ false };
		std::atomic<bool> g_taskPending{ false };

		std::mutex  g_lock;
		std::string g_stateJson = R"({"ready":false})";
		std::string g_markersJson = R"({"worldspace":0,"markers":[]})";

		// Only touched on the game thread.
		RE::FormID g_lastWorldspace = 0;
		int        g_ticksSinceMarkers = kMarkerRefreshTicks;

		std::string SafeName(const char* a_name)
		{
			return a_name ? std::string(a_name) : std::string();
		}

		json ActorValueJson(RE::PlayerCharacter* a_player, RE::ActorValue a_av)
		{
			const auto owner = a_player->AsActorValueOwner();
			const float current = owner->GetActorValue(a_av);
			const float max = owner->GetPermanentActorValue(a_av) +
			                  a_player->GetActorValueModifier(RE::ACTOR_VALUE_MODIFIER::kTemporary, a_av);
			return json{ { "cur", current }, { "max", max } };
		}

		json BuildState(RE::PlayerCharacter* a_player)
		{
			const auto pos = a_player->GetPosition();
			const auto cell = a_player->GetParentCell();
			const auto worldspace = a_player->GetWorldspace();
			const auto location = a_player->GetCurrentLocation();
			const auto calendar = RE::Calendar::GetSingleton();

			// Skyrim's Z angle: 0 = north (+Y), increasing clockwise, in radians.
			constexpr float kPi = 3.14159265358979f;
			float heading = a_player->GetAngleZ() * 180.0f / kPi;
			heading = std::fmod(std::fmod(heading, 360.0f) + 360.0f, 360.0f);

			json j;
			j["ready"] = true;
			j["player"] = {
				{ "name", SafeName(a_player->GetName()) },
				{ "level", a_player->GetLevel() },
				{ "x", pos.x },
				{ "y", pos.y },
				{ "z", pos.z },
				{ "heading", heading },
				{ "gold", a_player->GetGoldAmount() },
				{ "inCombat", a_player->IsInCombat() },
				{ "sneaking", a_player->IsSneaking() },
				{ "mounted", a_player->IsOnMount() }
			};
			j["stats"] = {
				{ "health", ActorValueJson(a_player, RE::ActorValue::kHealth) },
				{ "magicka", ActorValueJson(a_player, RE::ActorValue::kMagicka) },
				{ "stamina", ActorValueJson(a_player, RE::ActorValue::kStamina) }
			};
			j["where"] = {
				{ "worldspaceId", worldspace ? worldspace->GetFormID() : 0 },
				{ "worldspace", worldspace ? SafeName(worldspace->GetName()) : "" },
				{ "cell", cell ? SafeName(cell->GetName()) : "" },
				{ "interior", cell ? cell->IsInteriorCell() : false },
				{ "location", location ? SafeName(location->GetName()) : "" }
			};
			if (calendar) {
				j["time"] = {
					{ "hour", calendar->GetHour() },
					{ "day", static_cast<int>(calendar->GetDay()) },
					{ "dayName", calendar->GetDayName() },
					{ "month", calendar->GetMonthName() },
					{ "year", calendar->GetYear() }
				};
			}
			return j;
		}

		// Collects every map marker (cities, caves, ruins, ...) in the player's current worldspace.
		json BuildMarkers(RE::TESWorldSpace* a_worldspace)
		{
			json list = json::array();
			if (!a_worldspace) {
				return json{ { "worldspace", 0 }, { "markers", list } };
			}

			const auto& [map, lock] = RE::TESForm::GetAllForms();
			if (map) {
				[[maybe_unused]] const RE::BSReadLockGuard guard{ lock };
				for (const auto& [id, form] : *map) {
					if (!form || !form->Is(RE::FormType::Reference)) {
						continue;
					}
					const auto ref = form->As<RE::TESObjectREFR>();
					if (!ref || ref->IsDeleted()) {
						continue;
					}
					const auto extra = ref->extraList.GetByType<RE::ExtraMapMarker>();
					if (!extra || !extra->mapData) {
						continue;
					}
					if (ref->GetWorldspace() != a_worldspace) {
						continue;
					}
					const auto data = extra->mapData;
					const auto refPos = ref->GetPosition();
					list.push_back({
						{ "name", SafeName(data->locationName.GetFullName()) },
						{ "x", refPos.x },
						{ "y", refPos.y },
						{ "type", data->type.underlying() },
						{ "seen", data->flags.all(RE::MapMarkerData::Flag::kVisible) },
						{ "discovered", data->flags.all(RE::MapMarkerData::Flag::kCanTravelTo) }
					});
				}
			}
			return json{ { "worldspace", a_worldspace->GetFormID() }, { "markers", list } };
		}

		// Runs on the game's main thread (queued through SKSE's task interface).
		void UpdateOnGameThread()
		{
			try {
				const auto player = RE::PlayerCharacter::GetSingleton();
				if (!g_gameReady || !player || !player->GetParentCell()) {
					std::scoped_lock l{ g_lock };
					g_stateJson = R"({"ready":false})";
				} else {
					auto stateText = BuildState(player).dump();

					const auto worldspace = player->GetWorldspace();
					const RE::FormID wsId = worldspace ? worldspace->GetFormID() : 0;
					std::string markersText;
					if (wsId != g_lastWorldspace || ++g_ticksSinceMarkers >= kMarkerRefreshTicks) {
						if (worldspace || wsId != g_lastWorldspace) {
							markersText = BuildMarkers(worldspace).dump();
						}
						g_lastWorldspace = wsId;
						g_ticksSinceMarkers = 0;
					}

					std::scoped_lock l{ g_lock };
					g_stateJson = std::move(stateText);
					if (!markersText.empty()) {
						g_markersJson = std::move(markersText);
					}
				}
			} catch (const std::exception& e) {
				logs::error("Snapshot failed: {}", e.what());
			}
			g_taskPending = false;
		}
	}

	void SetGameReady(bool a_ready)
	{
		g_gameReady = a_ready;
		if (!a_ready) {
			std::scoped_lock l{ g_lock };
			g_stateJson = R"({"ready":false})";
		}
	}

	void StartPolling()
	{
		std::thread([] {
			const auto tasks = SKSE::GetTaskInterface();
			while (true) {
				std::this_thread::sleep_for(kPollInterval);
				if (!tasks || !g_gameReady) {
					continue;
				}
				// Never queue more than one snapshot at a time.
				bool expected = false;
				if (g_taskPending.compare_exchange_strong(expected, true)) {
					tasks->AddTask(UpdateOnGameThread);
				}
			}
		}).detach();
	}

	std::string GetStateJson()
	{
		std::scoped_lock l{ g_lock };
		return g_stateJson;
	}

	std::string GetMarkersJson()
	{
		std::scoped_lock l{ g_lock };
		return g_markersJson;
	}
}
