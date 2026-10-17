#include "Readouts.h"
#include "Log.h"

namespace companion::readouts
{
	namespace
	{
		std::string Str(const char* a_text)
		{
			return a_text ? std::string(a_text) : std::string();
		}

		float GlobalValue(const char* a_editorID, float a_fallback = -1.0f)
		{
			const auto global = RE::TESForm::LookupByEditorID<RE::TESGlobal>(a_editorID);
			return global ? global->value : a_fallback;
		}

		std::string EnchantName(RE::InventoryEntryData* a_entry)
		{
			if (!a_entry) {
				return {};
			}
			const auto ench = a_entry->GetEnchantment();
			return ench ? Str(ench->GetFullName()) : std::string();
		}

		json EffectSummary(const RE::MagicItem* a_item)
		{
			json list = json::array();
			if (!a_item) {
				return list;
			}
			for (const auto effect : a_item->effects) {
				if (!effect || !effect->baseEffect) {
					continue;
				}
				const auto mgef = effect->baseEffect;
				list.push_back({
					{ "name", Str(mgef->GetFullName()) },
					{ "mag", effect->GetMagnitude() },
					{ "dur", effect->GetDuration() },
					{ "av", static_cast<int>(mgef->data.primaryAV) },
					{ "archetype", static_cast<int>(mgef->data.archetype) },
					{ "detrimental", mgef->data.flags.all(RE::EffectSetting::EffectSettingData::Flag::kDetrimental) },
					{ "recover", mgef->data.flags.all(RE::EffectSetting::EffectSettingData::Flag::kRecover) }
				});
			}
			return list;
		}
	}

	std::string RefKey(const RE::TESForm* a_form)
	{
		if (!a_form) {
			return {};
		}
		const auto file = a_form->GetFile(0);
		if (!file) {
			return {};
		}
		const auto id = a_form->GetFormID();
		const auto local = file->IsLight() ? (id & 0xFFF) : (id & 0xFFFFFF);
		return std::format("{}|{:X}", file->GetFilename(), local);
	}

	json ActiveEffects(RE::PlayerCharacter* a_player)
	{
		json list = json::array();
		const auto target = a_player->AsMagicTarget();
		const auto effects = target ? target->GetActiveEffectList() : nullptr;
		if (!effects) {
			return list;
		}

		using AEFlag = RE::ActiveEffect::Flag;
		using MFlag = RE::EffectSetting::EffectSettingData::Flag;

		for (const auto ae : *effects) {
			if (!ae) {
				continue;
			}
			const auto mgef = ae->GetBaseObject();
			if (!mgef) {
				continue;
			}

			std::string caster;
			if (const auto actor = ae->GetCasterActor(); actor) {
				caster = actor->IsPlayerRef() ? "You" : Str(actor->GetName());
			}

			list.push_back({
				{ "name", Str(mgef->GetFullName()) },
				{ "desc", Str(mgef->magicItemDescription.c_str()) },
				{ "spell", ae->spell ? Str(ae->spell->GetFullName()) : "" },
				{ "spellType", ae->spell ? static_cast<int>(ae->spell->GetSpellType()) : -1 },
				{ "item", ae->source ? Str(ae->source->GetName()) : "" },
				{ "caster", caster },
				{ "mag", ae->magnitude },
				{ "dur", ae->duration },
				{ "elapsed", ae->elapsedSeconds },
				{ "archetype", static_cast<int>(mgef->data.archetype) },
				{ "av", static_cast<int>(mgef->data.primaryAV) },
				{ "resist", static_cast<int>(mgef->data.resistVariable) },
				{ "detrimental", mgef->data.flags.all(MFlag::kDetrimental) },
				{ "recover", mgef->data.flags.all(MFlag::kRecover) },
				{ "hidden", mgef->data.flags.all(MFlag::kHideInUI) },
				{ "inactive", ae->flags.any(AEFlag::kInactive, AEFlag::kDispelled) }
			});
		}
		return list;
	}

	json Survival()
	{
		// Global names from Survival Mode Improved / Creation Club Survival Mode.
		auto need = [](const char* a_name, const char* a_value, const char* a_max, const char* a_stage, const char* a_stagePrefix, int a_defaultMax) {
			const float value = GlobalValue(a_value);
			if (value < 0.0f) {
				return json(nullptr);
			}
			json stages = json::array();
			for (int i = 1; i <= 5; ++i) {
				stages.push_back(GlobalValue(std::format("{}{}Value", a_stagePrefix, i).c_str(), 0.0f));
			}
			return json{
				{ "name", a_name },
				{ "value", value },
				{ "max", GlobalValue(a_max, static_cast<float>(a_defaultMax)) },
				{ "stage", GlobalValue(a_stage, -1.0f) },
				{ "thresholds", stages }
			};
		};

		const float enabled = GlobalValue("Survival_ModeEnabled", -1.0f);
		return json{
			{ "enabled", enabled },
			{ "hunger", need("Hunger", "Survival_HungerNeedValue", "Survival_HungerNeedMaxValue", "SMI_CurrentHungerStage", "Survival_HungerStage", 1000) },
			{ "cold", need("Cold", "Survival_ColdNeedValue", "Survival_ColdNeedMaxValue", "SMI_CurrentColdStage", "Survival_ColdStage", 1000) },
			{ "fatigue", need("Fatigue", "Survival_ExhaustionNeedValue", "Survival_ExhaustionNeedMaxValue", "SMI_CurrentExhaustionStage", "Survival_ExhaustionStage", 1000) },
			{ "ambientCold", GlobalValue("SMI_CurrentAmbientTemp", -1.0f) }
		};
	}

	json Equipment(RE::PlayerCharacter* a_player)
	{
		json out;

		auto hand = [&](bool a_left) -> json {
			const auto form = a_player->GetEquippedObject(a_left);
			if (!form) {
				return nullptr;
			}
			json j{ { "name", Str(form->GetName()) }, { "formType", static_cast<int>(form->GetFormType()) } };
			if (const auto weap = form->As<RE::TESObjectWEAP>(); weap) {
				j["kind"] = "weapon";
				j["weaponType"] = static_cast<int>(weap->GetWeaponType());
				j["damage"] = weap->GetAttackDamage();
				j["weight"] = weap->GetWeight();
				const auto entry = a_player->GetEquippedEntryData(a_left);
				if (entry) {
					j["name"] = Str(entry->GetDisplayName());
				}
				j["enchant"] = EnchantName(entry);
				if (entry) {
					if (const auto charge = entry->GetEnchantmentCharge(); charge) {
						j["charge"] = *charge;
					}
					if (entry->extraLists) {
						for (const auto xList : *entry->extraLists) {
							const auto poison = xList ? xList->GetByType<RE::ExtraPoison>() : nullptr;
							if (poison && poison->poison && poison->count > 0) {
								j["poison"] = { { "name", Str(poison->poison->GetFullName()) }, { "doses", poison->count } };
							}
						}
					}
				}
			} else if (const auto spell = form->As<RE::SpellItem>(); spell) {
				j["kind"] = "spell";
				j["effects"] = EffectSummary(spell);
			} else if (const auto armo = form->As<RE::TESObjectARMO>(); armo) {
				j["kind"] = "shield";
				j["armor"] = armo->GetArmorRating();
			} else {
				j["kind"] = "other";
			}
			return j;
		};
		out["right"] = hand(false);
		out["left"] = hand(true);

		if (const auto ammo = a_player->GetCurrentAmmo(); ammo) {
			const auto counts = a_player->GetInventoryCounts();
			const auto it = counts.find(ammo);
			out["ammo"] = { { "name", Str(ammo->GetName()) }, { "count", it != counts.end() ? it->second : 0 }, { "bolt", ammo->IsBolt() } };
		} else {
			out["ammo"] = nullptr;
		}

		const auto power = a_player->GetActorRuntimeData().selectedPower;
		out["power"] = power ? json{ { "name", Str(power->GetName()) }, { "shout", power->Is(RE::FormType::Shout) } } : json(nullptr);

		// Worn armor, one entry per item even when it covers several slots.
		using Slot = RE::BGSBipedObjectForm::BipedObjectSlot;
		const std::pair<Slot, const char*> slots[] = {
			{ Slot::kHead, "head" }, { Slot::kHair, "head" }, { Slot::kCirclet, "head" },
			{ Slot::kBody, "body" }, { Slot::kHands, "hands" }, { Slot::kForearms, "forearms" },
			{ Slot::kFeet, "feet" }, { Slot::kCalves, "calves" }, { Slot::kShield, "shield" },
			{ Slot::kAmulet, "amulet" }, { Slot::kRing, "ring" }
		};
		json worn = json::array();
		std::vector<RE::FormID> seen;
		const auto inv = a_player->GetInventory([](RE::TESBoundObject& a_obj) { return a_obj.Is(RE::FormType::Armor); });
		for (const auto& [slot, label] : slots) {
			const auto armo = a_player->GetWornArmor(slot);
			if (!armo || std::find(seen.begin(), seen.end(), armo->GetFormID()) != seen.end()) {
				continue;
			}
			seen.push_back(armo->GetFormID());

			std::string name = Str(armo->GetName());
			std::string enchant = armo->formEnchanting ? Str(armo->formEnchanting->GetFullName()) : "";
			if (const auto it = inv.find(armo); it != inv.end() && it->second.second) {
				name = Str(it->second.second->GetDisplayName());
				if (const auto e = EnchantName(it->second.second.get()); !e.empty()) {
					enchant = e;
				}
			}
			const char* weight = armo->IsHeavyArmor() ? "heavy" : armo->IsLightArmor() ? "light" : "clothing";
			const auto mask = armo->GetSlotMask();
			if (mask.any(Slot::kAmulet, Slot::kRing, Slot::kCirclet) && !mask.any(Slot::kHead, Slot::kBody, Slot::kHands, Slot::kFeet)) {
				weight = "jewelry";
			}
			worn.push_back({
				{ "slot", label },
				{ "name", name },
				{ "armor", armo->GetArmorRating() },
				{ "weight", weight },
				{ "pounds", armo->GetWeight() },
				{ "enchant", enchant }
			});
		}
		out["worn"] = worn;

		const auto owner = a_player->AsActorValueOwner();
		out["armorRating"] = owner->GetActorValue(RE::ActorValue::kDamageResist);
		out["carry"] = {
			{ "cur", owner->GetActorValue(RE::ActorValue::kInventoryWeight) },
			{ "max", owner->GetActorValue(RE::ActorValue::kCarryWeight) }
		};
		out["resists"] = {
			{ "fire", owner->GetActorValue(RE::ActorValue::kResistFire) },
			{ "frost", owner->GetActorValue(RE::ActorValue::kResistFrost) },
			{ "shock", owner->GetActorValue(RE::ActorValue::kResistShock) },
			{ "magic", owner->GetActorValue(RE::ActorValue::kResistMagic) },
			{ "poison", owner->GetActorValue(RE::ActorValue::kPoisonResist) },
			{ "disease", owner->GetActorValue(RE::ActorValue::kResistDisease) }
		};
		return out;
	}

	json Consumables(RE::PlayerCharacter* a_player)
	{
		json list = json::array();
		const auto inv = a_player->GetInventory([](RE::TESBoundObject& a_obj) { return a_obj.Is(RE::FormType::AlchemyItem); });
		for (const auto& [obj, data] : inv) {
			const auto count = data.first;
			if (count <= 0) {
				continue;
			}
			const auto alch = obj->As<RE::AlchemyItem>();
			if (!alch) {
				continue;
			}
			const char* kind = alch->IsPoison() ? "poison" : alch->IsFood() ? "food" : "potion";
			// Stress and Fear marks stress relievers with Stress_ReduceN keywords.
			int stress = 0;
			for (std::uint32_t k = 0; k < alch->numKeywords; ++k) {
				const auto kw = alch->keywords[k];
				const std::string id = kw ? Str(kw->GetFormEditorID()) : std::string();
				if (id.starts_with("Stress_Reduce")) {
					try {
						stress = (std::max)(stress, std::stoi(id.substr(13)));
					} catch (...) {
					}
				}
			}
			list.push_back({
				{ "name", data.second ? Str(data.second->GetDisplayName()) : Str(alch->GetFullName()) },
				{ "count", count },
				{ "kind", kind },
				{ "stress", stress },
				{ "effects", EffectSummary(alch) }
			});
		}
		return list;
	}

	// Followers: anyone flagged as the player's teammate, plus whoever fills the vanilla follower and animal
	// slots (so a follower left far away still shows). Remembered until the next load.
	namespace
	{
		std::vector<RE::FormID>                 g_knownFollowers;
		std::unordered_map<RE::FormID, bool>    g_wasDead;
		std::unordered_set<RE::FormID>          g_inService;
		std::chrono::steady_clock::time_point   g_settleUntil{};

		float Global(const char* a_editorID, float a_fallback)
		{
			const auto g = RE::TESForm::LookupByEditorID<RE::TESGlobal>(a_editorID);
			return g ? g->value : a_fallback;
		}

		std::string WhereIs(RE::Actor* a_actor)
		{
			const auto cell = a_actor->GetParentCell();
			const auto loc = a_actor->GetCurrentLocation();
			if (cell && cell->IsInteriorCell()) {
				return Str(cell->GetName());
			}
			if (loc) {
				return Str(loc->GetName());
			}
			const auto ws = a_actor->GetWorldspace();
			return ws ? Str(ws->GetName()) : std::string();
		}

		void AddVanillaSlots(std::vector<RE::FormID>& a_known)
		{
			// DialogueFollower (Skyrim.esm 0x000750BA) holds the vanilla follower and animal aliases.
			const auto quest = RE::TESForm::LookupByID<RE::TESQuest>(0x000750BA);
			if (!quest) {
				return;
			}
			RE::BSReadLockGuard guard{ quest->aliasAccessLock };
			for (const auto& [aliasID, handle] : quest->refAliasMap) {
				const auto ref = handle.get();
				const auto actor = ref ? ref->As<RE::Actor>() : nullptr;
				if (actor && std::find(a_known.begin(), a_known.end(), actor->GetFormID()) == a_known.end()) {
					a_known.push_back(actor->GetFormID());
				}
			}
		}

		bool IsCreature(RE::Actor* a_actor)
		{
			const auto race = a_actor->GetRace();
			return race && !race->HasKeywordString("ActorTypeNPC");
		}

		json HandName(RE::Actor* a_actor, bool a_left)
		{
			const auto form = a_actor->GetEquippedObject(a_left);
			return form ? json(Str(form->GetName())) : json(nullptr);
		}

		json CombatSpells(RE::Actor* a_actor, std::size_t a_max)
		{
			json list = json::array();
			auto add = [&](RE::SpellItem* a_spell) {
				if (!a_spell || list.size() >= a_max || a_spell->GetSpellType() != RE::MagicSystem::SpellType::kSpell) {
					return;
				}
				const auto name = Str(a_spell->GetFullName());
				if (!name.empty() && std::find(list.begin(), list.end(), name) == list.end()) {
					list.push_back(name);
				}
			};
			for (const auto spell : a_actor->GetActorRuntimeData().addedSpells) {
				add(spell);
			}
			if (const auto base = a_actor->GetActorBase(); base && base->actorEffects) {
				const auto spells = base->actorEffects;
				for (std::uint32_t i = 0; i < spells->numSpells; ++i) {
					add(spells->spells[i]);
				}
			}
			return list;
		}
	}

	void ResetFollowers()
	{
		g_knownFollowers.clear();
		g_wasDead.clear();
		g_inService.clear();
		// Followers found right after a load were already with you; don't log them as hires.
		g_settleUntil = std::chrono::steady_clock::now() + std::chrono::seconds(6);
	}

	json PartySlots()
	{
		return json{
			{ "followers", Global("PlayerFollowerCount", -1.0f) },
			{ "animals", Global("PlayerAnimalCount", -1.0f) },
			{ "tamed1", Global("_AAA_AnimalSlot1Taken", -1.0f) },
			{ "tamed2", Global("_AAA_AnimalSlot2Taken", -1.0f) },
			{ "packLeader", Global("_AAA_PerkRank_PackLeader", -1.0f) },
			{ "pet1Hunger", Global("_AAA_Pet1_Hunger", -1.0f) },
			{ "pet2Hunger", Global("_AAA_Pet2_Hunger", -1.0f) }
		};
	}

	json Followers(RE::PlayerCharacter* a_player)
	{
		auto& known = g_knownFollowers;
		const bool settled = std::chrono::steady_clock::now() > g_settleUntil;

		if (const auto lists = RE::ProcessLists::GetSingleton(); lists) {
			for (const auto& handle : lists->highActorHandles) {
				const auto actor = handle.get();
				if (actor && actor->IsPlayerTeammate() && std::find(known.begin(), known.end(), actor->GetFormID()) == known.end()) {
					known.push_back(actor->GetFormID());
				}
			}
		}
		AddVanillaSlots(known);

		json list = json::array();
		const auto playerPos = a_player->GetPosition();
		const auto playerCell = a_player->GetParentCell();
		const auto playerWs = a_player->GetWorldspace();

		std::vector<RE::FormID> keep;
		for (const auto id : known) {
			const auto actor = RE::TESForm::LookupByID<RE::Actor>(id);
			if (!actor || actor->IsDeleted()) {
				continue;
			}
			const bool dead = actor->IsDead(false);
			const auto owner = actor->AsActorValueOwner();
			const bool waiting = owner->GetActorValue(RE::ActorValue::kWaitingForPlayer) > 0.0f;
			const bool teammate = actor->IsPlayerTeammate();
			const bool inService = teammate || (waiting && g_inService.contains(id));
			const std::string name = Str(actor->GetName());

			// Log hires, dismissals and deaths.
			if (inService && !g_inService.contains(id)) {
				g_inService.insert(id);
				if (settled) {
					textlog::Write(std::format("{} | Hired: {} | {}", textlog::GameDate(), name, WhereIs(actor)));
				}
			}
			if (dead && !g_wasDead[id]) {
				g_wasDead[id] = true;
				if (settled && g_inService.contains(id)) {
					textlog::Write(std::format("{} | Died: {} | {}", textlog::GameDate(), name, WhereIs(actor)));
				}
			}
			if (!inService && !dead) {
				if (g_inService.erase(id) && settled) {
					textlog::Write(std::format("{} | Dismissed: {} | {}", textlog::GameDate(), name, WhereIs(actor)));
				}
				continue;  // dismissed followers drop off the list
			}
			keep.push_back(id);

			auto av = [&](RE::ActorValue a) {
				const float cur = owner->GetActorValue(a);
				const float max = owner->GetPermanentActorValue(a) + actor->GetActorValueModifier(RE::ACTOR_VALUE_MODIFIER::kTemporary, a);
				return json{ { "cur", cur }, { "max", max } };
			};

			const auto cell = actor->GetParentCell();
			const auto ws = actor->GetWorldspace();
			const auto loc = actor->GetCurrentLocation();
			const auto pos = actor->GetPosition();
			const bool sameSpace = cell && playerCell && (cell == playerCell || (ws && ws == playerWs));
			const auto state = actor->AsActorState();

			std::string status = waiting ? "Waiting" : "Following";
			if (dead) {
				status = "Dead";
			} else if (state && state->IsBleedingOut()) {
				status = "Downed";
			} else if (actor->IsInCombat()) {
				status = "In combat";
			} else if (!sameSpace && !waiting) {
				status = "Away";
			}

			json j{
				{ "id", id },
				{ "name", name },
				{ "level", actor->GetLevel() },
				{ "health", av(RE::ActorValue::kHealth) },
				{ "magicka", av(RE::ActorValue::kMagicka) },
				{ "stamina", av(RE::ActorValue::kStamina) },
				{ "status", status },
				{ "dead", dead },
				{ "waiting", waiting },
				{ "essential", actor->IsEssential() || actor->IsProtected() },
				{ "creature", IsCreature(actor) },
				{ "loaded", actor->Is3DLoaded() },
				{ "mounted", actor->IsOnMount() },
				{ "sameSpace", sameSpace },
				{ "distance", sameSpace ? pos.GetDistance(playerPos) : -1.0f },
				{ "x", pos.x },
				{ "y", pos.y },
				{ "worldspaceId", ws ? ws->GetFormID() : 0 },
				{ "interior", cell ? cell->IsInteriorCell() : false },
				{ "cell", cell ? Str(cell->GetName()) : "" },
				{ "location", loc ? Str(loc->GetName()) : "" }
			};
			if (actor->Is3DLoaded() && !dead) {
				json battle{ { "armor", owner->GetActorValue(RE::ActorValue::kDamageResist) }, { "right", HandName(actor, false) }, { "left", HandName(actor, true) },
					{ "carry", { { "cur", owner->GetActorValue(RE::ActorValue::kInventoryWeight) }, { "max", owner->GetActorValue(RE::ActorValue::kCarryWeight) } } },
					{ "spells", CombatSpells(actor, 3) } };
				if (const auto ammo = actor->GetCurrentAmmo(); ammo) {
					const auto counts = actor->GetInventoryCounts([](RE::TESBoundObject& a_obj) { return a_obj.Is(RE::FormType::Ammo); });
					const auto it = counts.find(ammo);
					battle["ammo"] = { { "name", Str(ammo->GetName()) }, { "count", it != counts.end() ? it->second : 0 } };
				}
				j["battle"] = battle;
			}
			list.push_back(std::move(j));
		}
		known = keep;
		return list;
	}

	std::string FollowerDetails(RE::FormID a_id)
	{
		const auto actor = RE::TESForm::LookupByID<RE::Actor>(a_id);
		if (!actor) {
			return R"({"error":"not found"})";
		}
		json items = json::array();
		const auto inv = actor->GetInventory();
		for (const auto& [obj, data] : inv) {
			if (data.first <= 0 || !obj) {
				continue;
			}
			const auto entry = data.second.get();
			items.push_back({ { "name", entry ? Str(entry->GetDisplayName()) : Str(obj->GetName()) },
				{ "count", data.first },
				{ "type", static_cast<int>(obj->GetFormType()) },
				{ "worn", entry && entry->IsWorn() },
				{ "weight", obj->GetWeight() },
				{ "value", obj->GetGoldValue() } });
		}
		json out{ { "id", a_id }, { "name", Str(actor->GetName()) }, { "items", items }, { "spells", CombatSpells(actor, 40) },
			{ "where", WhereIs(actor) }, { "right", HandName(actor, false) }, { "left", HandName(actor, true) } };
		return out.dump(-1, ' ', false, json::error_handler_t::replace);
	}

	std::string FindActor(RE::FormID a_id)
	{
		const auto actor = RE::TESForm::LookupByID<RE::Actor>(a_id);
		if (!actor) {
			return R"({"error":"not found"})";
		}
		const auto pos = actor->GetPosition();
		const auto ws = actor->GetWorldspace();
		const auto cell = actor->GetParentCell();
		json out{ { "id", a_id }, { "name", Str(actor->GetName()) }, { "where", WhereIs(actor) }, { "x", pos.x }, { "y", pos.y },
			{ "interior", cell ? cell->IsInteriorCell() : false }, { "worldspaceId", ws ? ws->GetFormID() : 0 },
			{ "dead", actor->IsDead(false) } };
		// Inside an interior, point the map at the door that leads outside if the game knows it.
		return out.dump(-1, ' ', false, json::error_handler_t::replace);
	}
}
