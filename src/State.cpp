#include "State.h"
#include "Readouts.h"
#include "Extras.h"
#include "Feed.h"
#include "Radar.h"
#include "LocalMap.h"
#include "Log.h"

namespace companion::state
{
	namespace
	{
		constexpr auto kPollInterval = std::chrono::milliseconds(100);
		constexpr int  kMarkerRefreshTicks = 50;  // ~5 seconds

		std::atomic<bool> g_gameReady{ false };
		std::atomic<bool> g_taskPending{ false };
		std::atomic<bool> g_rescanMarkers{ true };
		std::atomic<bool> g_mapOpened{ false };  // markers load the first time the map screen opens
		int               g_ticksSinceLocalMap = 0;
		int               g_ticksSinceSeason = 290;

		std::mutex  g_lock;
		std::string g_stateJson = R"({"ready":false})";
		std::string g_markersJson = R"({"worldspace":0,"markers":[]})";

		// Only touched on the game thread.
		RE::FormID g_lastWorldspace = 0xFFFFFFFF;
		int        g_ticksSinceInventory = 1000;
		json       g_slowParts = json::object();  // equipment + consumables, refreshed about once a second
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
				{ "worldspaceEdid", worldspace ? SafeName(worldspace->GetFormEditorID()) : "" },
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

		// Map marker references in the current worldspace, found once per worldspace.
		// Re-reading these few hundred refs is cheap; scanning every form in the game is not.
		std::vector<RE::FormID> g_markerIds;
		RE::FormID              g_markerIdsWorldspace = 0;

		RE::ExtraMapMarker* GetMarkerExtra(RE::TESObjectREFR* a_ref)
		{
			if (!a_ref || a_ref->IsDeleted()) {
				return nullptr;
			}
			const auto extra = a_ref->extraList.GetByType<RE::ExtraMapMarker>();
			return extra && extra->mapData ? extra : nullptr;
		}

		void ScanMarkers(RE::TESWorldSpace* a_worldspace)
		{
			g_markerIds.clear();
			g_markerIdsWorldspace = a_worldspace->GetFormID();

			const auto start = std::chrono::steady_clock::now();
			const auto& [map, lock] = RE::TESForm::GetAllForms();
			if (map) {
				[[maybe_unused]] const RE::BSReadLockGuard guard{ lock };
				for (const auto& entry : *map) {
					const auto form = entry.second;
					if (!form || !form->Is(RE::FormType::Reference)) {
						continue;
					}
					const auto ref = form->As<RE::TESObjectREFR>();
					if (GetMarkerExtra(ref) && ref->GetWorldspace() == a_worldspace) {
						g_markerIds.push_back(ref->GetFormID());
					}
				}
			}
			const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
			logs::info("Found {} map markers in {} ({} ms)", g_markerIds.size(), SafeName(a_worldspace->GetName()), ms);
		}

		// Builds the marker list (cities, caves, ruins, ...) for the player's current worldspace.
		json BuildMarkers(RE::TESWorldSpace* a_worldspace)
		{
			if (g_rescanMarkers || g_markerIdsWorldspace != a_worldspace->GetFormID()) {
				ScanMarkers(a_worldspace);
				g_rescanMarkers = false;
			}

			json list = json::array();
			for (const auto id : g_markerIds) {
				const auto ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(id);
				const auto extra = GetMarkerExtra(ref);
				if (!extra) {
					continue;
				}
				const auto data = extra->mapData;
				const auto refPos = ref->GetPosition();
				list.push_back({
					{ "ref", readouts::RefKey(ref) },
					{ "name", SafeName(data->locationName.GetFullName()) },
					{ "x", refPos.x },
					{ "y", refPos.y },
					{ "type", data->type.underlying() },
					{ "seen", data->flags.all(RE::MapMarkerData::Flag::kVisible) },
					{ "discovered", data->flags.all(RE::MapMarkerData::Flag::kCanTravelTo) }
				});
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
					auto state = BuildState(player);
					auto safely = [](const char* a_what, auto&& a_fn) -> json {
						try {
							return a_fn();
						} catch (const std::exception& e) {
							logs::error("{} failed: {}", a_what, e.what());
							return nullptr;
						}
					};
					state["effects"] = safely("Active effects", [&] { return readouts::ActiveEffects(player); });
					state["survival"] = safely("Survival", [] { return readouts::Survival(); });
					state["followers"] = safely("Followers", [&] { return readouts::Followers(player); });
					state["progress"] = safely("Progress", [&] { return extras::Progress(player); });
					state["warmth"] = safely("Warmth", [&] { return extras::Warmth(player); });
					state["crosshair"] = safely("Crosshair", [&] { return extras::Crosshair(player); });
					state["shoutCooldown"] = safely("Shout cooldown", [&] { return json(extras::ShoutCooldown(player)); });
					state["radar"] = safely("Radar", [&] { return radar::Build(player); });
					state["stress"] = safely("Stress", [] { return extras::Stress(); });
					state["mapOpened"] = g_mapOpened.load();
					if (++g_ticksSinceSeason >= 300) {
						g_ticksSinceSeason = 0;
						safely("Season", [] { extras::RequestSeason(); return json(nullptr); });
					}
					state["world"] = safely("World", [] { return extras::World(); });
					if (++g_ticksSinceLocalMap >= 3) {
						g_ticksSinceLocalMap = 0;
						safely("Local map", [&] { localmap::Tick(player); return json(nullptr); });
					}
					if (++g_ticksSinceInventory >= 10) {
						g_ticksSinceInventory = 0;
						g_slowParts = {
							{ "equipment", safely("Equipment", [&] { return readouts::Equipment(player); }) },
							{ "consumables", safely("Consumables", [&] { return readouts::Consumables(player); }) },
							{ "supplies", safely("Supplies", [&] { return extras::Supplies(player); }) },
							{ "quests", safely("Quests", [&] { return extras::Quests(player); }) },
							{ "books", safely("Books", [&] { return extras::Books(player); }) },
							{ "character", safely("Character", [&] { return extras::Character(player); }) },
							{ "partySlots", safely("Party slots", [] { return readouts::PartySlots(); }) }
						};
					}
					state.update(g_slowParts);
					auto stateText = state.dump(-1, ' ', false, json::error_handler_t::replace);

					const auto worldspace = player->GetWorldspace();
					const RE::FormID wsId = worldspace ? worldspace->GetFormID() : 0;
					std::string markersText;
					if (g_mapOpened && (wsId != g_lastWorldspace || g_rescanMarkers || ++g_ticksSinceMarkers >= kMarkerRefreshTicks)) {
						// Indoors there is no worldspace; keep showing the last outdoor markers.
						if (worldspace) {
							markersText = BuildMarkers(worldspace).dump(-1, ' ', false, json::error_handler_t::replace);
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
		if (a_ready) {
			// SKSE load/new-game messages arrive on the main thread.
			readouts::ResetFollowers();
			feed::Clear();
			radar::Reset();
			localmap::Reset();
			if (const auto player = RE::PlayerCharacter::GetSingleton(); player) {
				textlog::Write(std::format("{} | Loaded a save: {}", textlog::GameDate(), player->GetName() ? player->GetName() : ""));
			}
		}
		g_gameReady = a_ready;
		g_rescanMarkers = true;  // a different save can have different markers
		g_ticksSinceInventory = 1000;
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

	void OnMapOpened()
	{
		if (!g_mapOpened.exchange(true)) {
			g_rescanMarkers = true;
		}
	}
}
