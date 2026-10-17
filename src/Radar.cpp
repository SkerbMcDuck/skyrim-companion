#include "Radar.h"

namespace companion::radar
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		struct Track
		{
			bool              seen{ false };
			Clock::time_point lastActive{};
			bool              seesMe{ false };
		};

		std::unordered_map<RE::FormID, Track> g_tracks;
		std::size_t                           g_losCursor = 0;
		std::size_t                           g_detectCursor = 0;

		std::string Str(const char* a_text) { return a_text ? std::string(a_text) : std::string(); }

		bool IsPartyMember(RE::Actor* a_actor)
		{
			return a_actor && (a_actor->IsPlayerRef() || a_actor->IsPlayerTeammate());
		}

		RE::Actor* CombatTarget(RE::Actor* a_actor)
		{
			const auto ptr = a_actor->GetActorRuntimeData().currentCombatTarget.get();
			return ptr.get();
		}

		// ----- Dragonborn's Bestiary data (loose JSON files under Data/interface/creatures) -----
		std::unordered_map<std::string, std::filesystem::path> g_bestiaryFiles;
		std::unordered_map<RE::FormID, json>                   g_bestiaryCache;
		bool                                                   g_bestiaryScanned = false;

		std::string Lower(std::string a_text)
		{
			std::ranges::transform(a_text, a_text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_text;
		}

		void ScanBestiary()
		{
			g_bestiaryScanned = true;
			std::error_code ec;
			const std::filesystem::path root = "Data/interface/creatures";
			for (auto it = std::filesystem::recursive_directory_iterator(root, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
				if (!it->is_regular_file(ec)) {
					continue;
				}
				const auto stem = it->path().stem().string();
				constexpr std::string_view suffix = "_RESIST";
				if (stem.size() > suffix.size() && stem.ends_with(suffix)) {
					g_bestiaryFiles[Lower(stem.substr(0, stem.size() - suffix.size()))] = it->path();
				}
			}
			logs::info("Bestiary: {} creature entries found", g_bestiaryFiles.size());
		}

		std::string BestiaryKey(RE::Actor* a_actor)
		{
			std::string key;
			auto check = [&](const RE::BGSKeywordForm* a_form) {
				if (!a_form || !key.empty()) {
					return;
				}
				for (std::uint32_t i = 0; i < a_form->numKeywords; ++i) {
					const auto kw = a_form->keywords[i];
					if (!kw) {
						continue;
					}
					const std::string id = Str(kw->GetFormEditorID());
					if (id.size() > 8 && id.starts_with("Bestiary") && !id.starts_with("BestiaryKeywords")) {
						key = Lower(id.substr(8));
						return;
					}
				}
			};
			check(a_actor->GetActorBase());
			check(a_actor->GetRace());
			return key;
		}
	}

	json Bestiary(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return nullptr;
		}
		const auto base = a_actor->GetActorBase();
		if (!base) {
			return nullptr;
		}
		if (const auto it = g_bestiaryCache.find(base->GetFormID()); it != g_bestiaryCache.end()) {
			return it->second;
		}
		if (!g_bestiaryScanned) {
			ScanBestiary();
		}
		json result = nullptr;
		const auto key = BestiaryKey(a_actor);
		if (!key.empty()) {
			if (const auto file = g_bestiaryFiles.find(key); file != g_bestiaryFiles.end()) {
				std::ifstream in(file->second, std::ios::binary);
				std::stringstream ss;
				ss << in.rdbuf();
				auto parsed = json::parse(ss.str(), nullptr, false, true);
				if (!parsed.is_discarded()) {
					result = { { "creature", key }, { "resistance", parsed.value("resistance", json::object()) }, { "weakness", parsed.value("weakness", json::object()) } };
				}
			}
		}
		g_bestiaryCache[base->GetFormID()] = result;
		return result;
	}

	void Reset()
	{
		g_tracks.clear();
		g_bestiaryCache.clear();
	}

	json Build(RE::PlayerCharacter* a_player)
	{
		json list = json::array();
		const auto lists = RE::ProcessLists::GetSingleton();
		if (!lists) {
			return list;
		}
		const auto now = Clock::now();
		const auto playerPos = a_player->GetPosition();
		const bool sneaking = a_player->IsSneaking();

		// Detect Life / Detect Dead on the player reveal what they reach.
		bool detectLife = false, detectDead = false;
		if (const auto effects = a_player->AsMagicTarget()->GetActiveEffectList(); effects) {
			for (const auto ae : *effects) {
				const auto mgef = ae ? ae->GetBaseObject() : nullptr;
				if (!mgef || ae->flags.any(RE::ActiveEffect::Flag::kInactive, RE::ActiveEffect::Flag::kDispelled)) {
					continue;
				}
				if (mgef->GetArchetype() == RE::EffectSetting::Archetype::kDetectLife) {
					const std::string name = Str(mgef->GetFullName());
					(name.find("Dead") != std::string::npos ? detectDead : detectLife) = true;
				}
			}
		}

		struct Candidate
		{
			RE::Actor* actor;
			float      dist;
		};
		std::vector<Candidate> nearby;
		for (const auto& handle : lists->highActorHandles) {
			const auto ptr = handle.get();
			const auto actor = ptr.get();
			if (!actor || actor->IsPlayerRef() || actor->IsPlayerTeammate() || actor->IsDisabled() || !actor->Is3DLoaded()) {
				continue;
			}
			const float dist = actor->GetPosition().GetDistance(playerPos);
			if (dist <= kRange * 1.3f) {
				nearby.push_back({ actor, dist });
			}
		}

		// Line-of-sight checks are the expensive part, so only a few per tick, round robin.
		if (!nearby.empty()) {
			constexpr std::size_t kLosPerTick = 4;
			for (std::size_t i = 0; i < (std::min)(kLosPerTick, nearby.size()); ++i) {
				const auto& c = nearby[(g_losCursor + i) % nearby.size()];
				if (c.dist > kRange || c.actor->IsDead(false)) {
					continue;
				}
				bool unused = false;
				if (a_player->HasLineOfSight(c.actor, unused)) {
					auto& t = g_tracks[c.actor->GetFormID()];
					t.seen = true;
					t.lastActive = now;
				}
			}
			g_losCursor = (g_losCursor + kLosPerTick) % (std::max)(std::size_t{ 1 }, nearby.size());

			// "Sees me" only matters while sneaking.
			if (sneaking) {
				constexpr std::size_t kDetectPerTick = 3;
				for (std::size_t i = 0; i < (std::min)(kDetectPerTick, nearby.size()); ++i) {
					const auto& c = nearby[(g_detectCursor + i) % nearby.size()];
					auto it = g_tracks.find(c.actor->GetFormID());
					if (it != g_tracks.end() && it->second.seen && !c.actor->IsDead(false)) {
						it->second.seesMe = c.actor->RequestDetectionLevel(a_player) > 0;
					}
				}
				g_detectCursor = (g_detectCursor + kDetectPerTick) % (std::max)(std::size_t{ 1 }, nearby.size());
			}
		}

		std::unordered_set<RE::FormID> present;
		for (const auto& c : nearby) {
			const auto actor = c.actor;
			const auto id = actor->GetFormID();
			present.insert(id);
			const bool dead = actor->IsDead(false);
			const auto target = dead ? nullptr : CombatTarget(actor);
			const bool fightingYou = !dead && actor->IsInCombat() && IsPartyMember(target);
			const bool otherFight = !dead && actor->IsInCombat() && target && !IsPartyMember(target);

			auto tIt = g_tracks.find(id);
			Track* track = tIt != g_tracks.end() ? &tIt->second : nullptr;
			if (fightingYou || otherFight) {
				if (!track) {
					track = &g_tracks[id];
				}
				track->lastActive = now;
			}
			if (track && !sneaking) {
				track->seesMe = false;
			}

			const bool detected = c.dist <= kRange && ((detectLife && !dead) || (detectDead && dead));
			const bool seen = track && track->seen;
			const bool quietTooLong = track && now - track->lastActive > std::chrono::seconds(20);
			if (track && seen && c.dist > kRange && quietTooLong) {
				g_tracks.erase(id);
				continue;
			}
			const bool show = fightingYou || detected || (seen && c.dist <= kRange * 1.3f);
			if (!show || dead && !detected && !seen) {
				continue;
			}

			const bool close = c.dist <= kNameRange || fightingYou;
			const auto pos = actor->GetPosition();
			json j{
				{ "id", id },
				{ "x", pos.x },
				{ "y", pos.y },
				{ "dist", c.dist },
				{ "state", fightingYou ? (seen ? "combat" : "ping") : detected && !seen ? "detected" : "seen" },
				{ "dead", dead },
				{ "fight", otherFight },
				{ "seesMe", sneaking && track && track->seesMe }
			};
			if (close) {
				const auto owner = actor->AsActorValueOwner();
				j["name"] = Str(actor->GetDisplayFullName());
				j["hostile"] = !dead && actor->IsHostileToActor(a_player);
				j["level"] = actor->GetLevel();
				j["hp"] = owner->GetActorValue(RE::ActorValue::kHealth);
				j["hpMax"] = owner->GetPermanentActorValue(RE::ActorValue::kHealth) + actor->GetActorValueModifier(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kHealth);
				if (j["hostile"].get<bool>()) {
					j["bestiary"] = Bestiary(actor);
				}
			}
			list.push_back(std::move(j));
		}

		// Forget actors that unloaded.
		std::erase_if(g_tracks, [&](const auto& kv) { return !present.contains(kv.first) && now - kv.second.lastActive > std::chrono::seconds(20); });
		return list;
	}
}
