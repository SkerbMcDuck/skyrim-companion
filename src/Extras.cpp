#include "Extras.h"
#include "Feed.h"
#include "Readouts.h"
#include "Radar.h"

namespace companion::extras
{
	namespace
	{
		constexpr const char* kSkillNames[18] = { "One-handed", "Two-handed", "Archery", "Block", "Smithing", "Heavy Armor", "Light Armor", "Pickpocket", "Lockpicking",
			"Sneak", "Alchemy", "Speech", "Alteration", "Conjuration", "Destruction", "Illusion", "Restoration", "Enchanting" };

		std::string Str(const char* a_text) { return a_text ? std::string(a_text) : std::string(); }
		std::string Name(const RE::TESForm* a_form) { return a_form ? Str(a_form->GetName()) : std::string(); }

		float GlobalValue(const char* a_editorID, float a_fallback)
		{
			const auto global = RE::TESForm::LookupByEditorID<RE::TESGlobal>(a_editorID);
			return global ? global->value : a_fallback;
		}

		const char* LockName(RE::LOCK_LEVEL a_level)
		{
			switch (a_level) {
			case RE::LOCK_LEVEL::kVeryEasy: return "Novice";
			case RE::LOCK_LEVEL::kEasy: return "Apprentice";
			case RE::LOCK_LEVEL::kAverage: return "Adept";
			case RE::LOCK_LEVEL::kHard: return "Expert";
			case RE::LOCK_LEVEL::kVeryHard: return "Master";
			case RE::LOCK_LEVEL::kRequiresKey: return "Requires key";
			default: return "";
			}
		}

		// Survival Mode's four "restores hunger" food effects live in Update.esm.
		std::array<RE::EffectSetting*, 4> HungerEffects()
		{
			static std::array<RE::EffectSetting*, 4> effects{};
			static bool loaded = false;
			if (!loaded) {
				loaded = true;
				if (const auto dh = RE::TESDataHandler::GetSingleton(); dh) {
					const RE::FormID ids[4] = { 0x2EE1, 0x2EE2, 0x2EE3, 0x2EE4 };  // very small, small, medium, large
					for (int i = 0; i < 4; ++i) {
						effects[i] = dh->LookupForm<RE::EffectSetting>(ids[i], "Update.esm");
					}
				}
			}
			return effects;
		}

		int HungerTier(const RE::AlchemyItem* a_item)
		{
			const auto tiers = HungerEffects();
			for (const auto effect : a_item->effects) {
				if (!effect || !effect->baseEffect) {
					continue;
				}
				for (int i = 0; i < 4; ++i) {
					if (tiers[i] && effect->baseEffect == tiers[i]) {
						return i + 1;
					}
				}
			}
			return 0;
		}

		// Same game function Survival Mode Improved calls for the warmth rating (Address Library IDs SE 25834 / AE 26394).
		float GetWarmthRating(RE::Actor* a_actor)
		{
			using func_t = float(RE::Actor*);
			static REL::Relocation<func_t> func{ RELOCATION_ID(25834, 26394) };
			return func(a_actor);
		}

		const char* ArmorClass(RE::TESObjectARMO* a_armor)
		{
			return a_armor->IsHeavyArmor() ? "heavy" : a_armor->IsLightArmor() ? "light" : "clothing";
		}

		std::string OwnerName(RE::TESObjectREFR* a_ref)
		{
			const auto owner = a_ref->GetOwner();
			return owner ? Name(owner) : std::string();
		}

	}

	// Reads Random Mining's MineOreScript values off an ore vein. Read-only.
	json OreVein(RE::TESObjectREFR* a_ref)
		{
			const auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			if (!vm) {
				return nullptr;
			}
			const auto policy = vm->GetObjectHandlePolicy();
			if (!policy) {
				return nullptr;
			}
			const auto handle = policy->GetHandleForObject(a_ref->GetFormType(), a_ref);
			RE::BSTSmartPointer<RE::BSScript::Object> object;
			if (!handle || !vm->FindBoundObject(handle, "MineOreScript", object) || !object) {
				return nullptr;
			}
			auto readInt = [&](const char* a_name) -> int {
				const auto v = object->GetProperty(a_name);
				return v && v->IsInt() ? v->GetSInt() : -999;
			};
			return json{
				{ "oreLeft", readInt("ResourceCountCurrent") },    // -1 = not started yet
				{ "orePerStrike", readInt("ResourceCount") },
				{ "oreTotal", readInt("ResourceCountTotal") },
				{ "swingsLeft", readInt("StrikesCurrent") },       // -1 = not started yet
				{ "swingsEach", readInt("StrikesBeforeCollection") }
			};
	}

	// Before you start a vein, Random Mining hasn't rolled yet. Work out the possible range from its MCM
	// settings and your Smithing, the same way its MineOreScript does (pickaxe = Smithing).
	json OreRange(const json& a_vein, RE::PlayerCharacter* a_player)
	{
		const auto dh = RE::TESDataHandler::GetSingleton();
		const auto quest = dh ? dh->LookupForm<RE::TESQuest>(0xD62, "Random Mining MCM.esp") : nullptr;
		const auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		if (!quest || !vm) {
			return nullptr;
		}
		const auto policy = vm->GetObjectHandlePolicy();
		const auto handle = policy ? policy->GetHandleForObject(quest->GetFormType(), quest) : 0;
		RE::BSTSmartPointer<RE::BSScript::Object> mcm;
		if (!handle || !vm->FindBoundObject(handle, "abim_RM_MCM_QuestScript", mcm) || !mcm) {
			return nullptr;
		}
		auto num = [&](const char* a_name) -> float {
			const auto v = mcm->GetProperty(a_name);
			if (!v) {
				return -1.0f;
			}
			if (v->IsInt()) {
				return static_cast<float>(v->GetSInt());
			}
			return v->IsFloat() ? v->GetFloat() : -1.0f;
		};
		auto flag = [&](const char* a_name) {
			const auto v = mcm->GetProperty(a_name);
			return v && v->IsBool() && v->GetBool();
		};
		const int perStrike = (std::max)(1, a_vein.value("orePerStrike", 1));
		if (a_vein.value("oreTotal", 3) != 3) {
			return json{ { "mode", "fixed" }, { "oreMin", a_vein.value("oreTotal", 0) * perStrike }, { "oreMax", a_vein.value("oreTotal", 0) * perStrike },
				{ "swingsMin", a_vein.value("swingsEach", 0) }, { "swingsMax", a_vein.value("swingsEach", 0) } };
		}
		if (flag("DoHardWorker")) {
			const float skill = a_player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kSmithing);
			int tier = 5;
			for (int i = 1; i <= 4; ++i) {
				if (skill <= num(std::format("LR{}MAXL", i).c_str())) {
					tier = i;
					break;
				}
			}
			return json{ { "mode", "Hard Worker" }, { "skill", skill },
				{ "oreMin", num(std::format("LR{}MinO", tier).c_str()) * perStrike }, { "oreMax", num(std::format("LR{}MaxO", tier).c_str()) * perStrike },
				{ "swingsMin", num(std::format("LR{}StrikesMin", tier).c_str()) }, { "swingsMax", num(std::format("LR{}StrikesMax", tier).c_str()) } };
		}
		return json{ { "mode", flag("DoLazyman") ? "Lazy Man" : "Default" }, { "oreMin", num("DLMinValue") * perStrike }, { "oreMax", num("DLMaxValue") * perStrike },
			{ "swingsMin", a_vein.value("swingsEach", 0) }, { "swingsMax", a_vein.value("swingsEach", 0) } };
	}

	// ----- calendar extras: Seasons of Skyrim season and moon phase timing -----
	namespace
	{
		std::atomic<int> g_season{ -1 };

		class SeasonCallback : public RE::BSScript::IStackCallbackFunctor
		{
		public:
			void operator()(RE::BSScript::Variable a_result) override
			{
				if (a_result.IsInt()) {
					g_season = a_result.GetSInt();
				}
			}
			void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}
		};
	}

	void RequestSeason()
	{
		// Seasons of Skyrim exposes its season through a Papyrus function; ask the script engine for it.
		const auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		if (!vm) {
			return;
		}
		RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(new SeasonCallback());
		const auto args = RE::MakeFunctionArguments();
		vm->DispatchStaticCall("SeasonsOfSkyrim", "GetCurrentSeason", args, callback);
	}

	json World()
	{
		json out{ { "season", g_season.load() } };
		if (const auto sky = RE::Sky::GetSingleton(); sky && sky->currentClimate) {
			out["moonPhaseLength"] = sky->currentClimate->timing.GetPhaseLength();
		}
		if (const auto cal = RE::Calendar::GetSingleton(); cal) {
			out["daysPassed"] = cal->GetDaysPassed();
		}
		return out;
	}

	json Progress(RE::PlayerCharacter* a_player)
	{
		const auto skills = a_player->GetInfoRuntimeData().skills;
		if (!skills || !skills->data) {
			return nullptr;
		}
		const auto data = skills->data;
		const auto owner = a_player->AsActorValueOwner();
		json list = json::array();
		for (int i = 0; i < 18; ++i) {
			const auto av = static_cast<RE::ActorValue>(static_cast<int>(RE::ActorValue::kOneHanded) + i);
			list.push_back({
				{ "name", kSkillNames[i] },
				{ "level", owner->GetBaseActorValue(av) },
				{ "xp", data->skills[i].xp },
				{ "threshold", data->skills[i].levelThreshold }
			});
		}
		return json{ { "xp", data->xp }, { "threshold", data->levelThreshold }, { "skills", list } };
	}

	// Same check Survival Mode Improved uses: a heat source from its list within 580 units.
	// (SMI also requires you not to be running, sprinting or swimming before it warms you.)
	bool NearHeatSource(RE::PlayerCharacter* a_player)
	{
		static RE::BGSListForm* list = nullptr;
		static bool looked = false;
		if (!looked) {
			looked = true;
			if (const auto dh = RE::TESDataHandler::GetSingleton(); dh) {
				list = dh->LookupForm<RE::BGSListForm>(0x8AA, "ccqdrsse001-survivalmode.esl");
			}
		}
		const auto tes = RE::TES::GetSingleton();
		if (!list || !tes) {
			return false;
		}
		bool near = false;
		tes->ForEachReferenceInRange(a_player, 580.0f, [&](RE::TESObjectREFR* a_ref) {
			if (a_ref && !a_ref->IsDisabled() && a_ref->Is3DLoaded()) {
				if (const auto base = a_ref->GetBaseObject(); base && list->HasForm(base)) {
					near = true;
					return RE::BSContainer::ForEachResult::kStop;
				}
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
		return near;
	}

	json Warmth(RE::PlayerCharacter* a_player)
	{
		const auto state = a_player->AsActorState();
		return json{
			{ "nearHeat", NearHeatSource(a_player) },
			{ "moving", a_player->IsRunning() || (state && (state->IsSprinting() || state->IsSwimming())) },
			{ "rating", GetWarmthRating(a_player) },
			{ "cap", GlobalValue("Survival_ColdResistMaxValue", 0.0f) },
			// Survival Mode Improved's UI state: 0 neutral, 1 near heat (warming), 2 warm, 3 cold, 4 freezing.
			{ "uiLevel", GlobalValue("Survival_TemperatureLevel", -1.0f) },
			{ "restore", { GlobalValue("Survival_HungerRestoreVerySmallAmount", 0.0f), GlobalValue("Survival_HungerRestoreSmallAmount", 0.0f),
							 GlobalValue("Survival_HungerRestoreMediumAmount", 0.0f), GlobalValue("Survival_HungerRestoreLargeAmount", 0.0f) } }
		};
	}

	float ShoutCooldown(RE::PlayerCharacter* a_player)
	{
		return a_player->GetVoiceRecoveryTime();
	}

	json Supplies(RE::PlayerCharacter* a_player)
	{
		json out;

		// Lockpicks (Skyrim.esm 0x0000000A).
		int lockpicks = 0;
		const auto lockpick = RE::TESForm::LookupByID<RE::TESObjectMISC>(0x0000000A);
		const auto counts = a_player->GetInventoryCounts([](RE::TESBoundObject& a_obj) { return a_obj.Is(RE::FormType::Misc); });
		if (lockpick) {
			if (const auto it = counts.find(lockpick); it != counts.end()) {
				lockpicks = it->second;
			}
		}
		out["lockpicks"] = lockpicks;

		// Soul gems: count each stack by gem size and the soul inside it.
		json gems = json::array();
		const auto inv = a_player->GetInventory([](RE::TESBoundObject& a_obj) { return a_obj.Is(RE::FormType::SoulGem) || a_obj.Is(RE::FormType::AlchemyItem); });
		json food = json::array();
		for (const auto& [obj, data] : inv) {
			const auto total = data.first;
			if (total <= 0) {
				continue;
			}
			if (const auto gem = obj->As<RE::TESSoulGem>(); gem) {
				const bool black = gem->CanHoldNPCSoul();
				const int capacity = static_cast<int>(gem->GetMaximumCapacity());
				std::map<int, int> bySoul;
				int counted = 0;
				if (data.second && data.second->extraLists) {
					for (const auto xList : *data.second->extraLists) {
						if (!xList) {
							continue;
						}
						const int n = (std::max)(1, static_cast<int>(xList->GetCount()));
						bySoul[static_cast<int>(xList->GetSoulLevel())] += n;
						counted += n;
					}
				}
				if (total > counted) {
					bySoul[static_cast<int>(gem->GetContainedSoul())] += total - counted;
				}
				for (const auto& [soul, n] : bySoul) {
					gems.push_back({ { "capacity", capacity }, { "soul", soul }, { "count", n }, { "black", black } });
				}
				continue;
			}
			if (const auto alch = obj->As<RE::AlchemyItem>(); alch && alch->IsFood()) {
				const int tier = HungerTier(alch);
				if (tier > 0) {
					food.push_back({ { "name", Name(alch) }, { "count", total }, { "tier", tier } });
				}
			}
		}
		out["soulGems"] = gems;
		out["food"] = food;

		// Bounties in every hold you have one in.
		json bounties = json::array();
		for (const auto& [faction, gold] : a_player->GetCrimeValue().crimeGoldMap) {
			const int bounty = static_cast<int>(gold.violentCur + gold.nonViolentCur);
			if (faction && bounty > 0) {
				bounties.push_back({ { "faction", Name(faction) }, { "bounty", bounty } });
			}
		}
		out["bounties"] = bounties;
		return out;
	}

	json Quests(RE::PlayerCharacter* a_player)
	{
		json list = json::array();
		for (const auto& inst : a_player->GetPlayerRuntimeData().objectives) {
			const auto obj = inst.Objective;
			if (!obj || !obj->ownerQuest || inst.InstanceState != RE::QUEST_OBJECTIVE_STATE::kDisplayed) {
				continue;
			}
			const auto quest = obj->ownerQuest;
			if (!quest->IsActive()) {
				continue;
			}
			json j{ { "quest", Str(quest->GetFullName()) }, { "text", Str(obj->displayText.c_str()) }, { "index", obj->index } };
			// Where the objective points, like the game's quest marker.
			json targets = json::array();
			for (std::uint32_t i = 0; i < obj->numTargets && obj->targets && i < 4; ++i) {
				const auto target = obj->targets[i];
				if (!target) {
					continue;
				}
				RE::ObjectRefHandle handle;
				target->GetTargetRef(handle, true, quest);
				const auto ref = handle.get();
				if (!ref) {
					continue;
				}
				const auto pos = ref->GetPosition();
				const auto cell = ref->GetParentCell();
				const auto ws = ref->GetWorldspace();
				targets.push_back({ { "x", pos.x }, { "y", pos.y }, { "worldspaceId", ws ? ws->GetFormID() : 0 },
					{ "interior", cell && cell->IsInteriorCell() }, { "cell", cell ? Str(cell->GetName()) : "" } });
			}
			j["targets"] = targets;
			list.push_back(std::move(j));
		}
		return list;
	}

	json Crosshair(RE::PlayerCharacter* a_player)
	{
		const auto refPtr = feed::CrosshairRef();
		const auto ref = refPtr.get();
		if (!ref || ref->IsDeleted()) {
			return nullptr;
		}
		const auto base = ref->GetBaseObject();
		if (!base) {
			return nullptr;
		}
		json j{ { "name", Str(ref->GetDisplayFullName()) } };

		if (const auto actor = ref->As<RE::Actor>(); actor) {
			const auto owner = actor->AsActorValueOwner();
			j["kind"] = "actor";
			j["id"] = actor->GetFormID();
			j["level"] = actor->GetLevel();
			j["health"] = { { "cur", owner->GetActorValue(RE::ActorValue::kHealth) },
				{ "max", owner->GetPermanentActorValue(RE::ActorValue::kHealth) + actor->GetActorValueModifier(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kHealth) } };
			j["dead"] = actor->IsDead(false);
			j["hostile"] = actor->IsHostileToActor(a_player);
			j["bestiary"] = radar::Bestiary(actor);
			return j;
		}

		auto item = [&](const char* a_kind) {
			j["kind"] = a_kind;
			j["value"] = base->GetGoldValue();
			j["weight"] = base->GetWeight();
			const auto owner = OwnerName(ref);
			if (!owner.empty()) {
				j["owner"] = owner;
			}
			j["stealing"] = ref->IsCrimeToActivate();
		};

		switch (base->GetFormType()) {
		case RE::FormType::Weapon:
			{
				item("weapon");
				const auto weap = base->As<RE::TESObjectWEAP>();
				j["damage"] = weap->GetAttackDamage();
				j["weaponType"] = static_cast<int>(weap->GetWeaponType());
				if (const auto eq = a_player->GetEquippedObject(false); eq) {
					if (const auto mine = eq->As<RE::TESObjectWEAP>(); mine) {
						j["compare"] = { { "name", Name(mine) }, { "damage", mine->GetAttackDamage() } };
					}
				}
				break;
			}
		case RE::FormType::Armor:
			{
				item("armor");
				const auto armo = base->As<RE::TESObjectARMO>();
				j["armor"] = armo->GetArmorRating();
				j["class"] = ArmorClass(armo);
				using Slot = RE::BGSBipedObjectForm::BipedObjectSlot;
				const std::pair<Slot, const char*> slots[] = { { Slot::kBody, "body" }, { Slot::kHead, "head" }, { Slot::kHands, "hands" }, { Slot::kFeet, "feet" }, { Slot::kShield, "shield" } };
				const auto mask = armo->GetSlotMask();
				for (const auto& [slot, label] : slots) {
					if (!mask.all(slot)) {
						continue;
					}
					j["slot"] = label;
					if (const auto worn = a_player->GetWornArmor(slot); worn) {
						j["compare"] = { { "name", Name(worn) }, { "armor", worn->GetArmorRating() }, { "class", ArmorClass(worn) } };
					}
					break;
				}
				break;
			}
		case RE::FormType::Book:
			{
				item("book");
				const auto book = base->As<RE::TESObjectBOOK>();
				j["read"] = book->IsRead();
				j["note"] = book->IsNoteScroll();
				if (book->TeachesSkill()) {
					const int i = static_cast<int>(book->GetSkill()) - static_cast<int>(RE::ActorValue::kOneHanded);
					if (i >= 0 && i < 18) {
						j["skill"] = kSkillNames[i];
					}
				}
				break;
			}
		case RE::FormType::AlchemyItem:
		case RE::FormType::Ingredient:
		case RE::FormType::Misc:
		case RE::FormType::Ammo:
		case RE::FormType::SoulGem:
		case RE::FormType::KeyMaster:
		case RE::FormType::Scroll:
			item("item");
			break;
		case RE::FormType::Container:
			j["kind"] = "container";
			if (ref->IsLocked()) {
				j["lock"] = LockName(ref->GetLockLevel());
			}
			if (const auto owner = OwnerName(ref); !owner.empty()) {
				j["owner"] = owner;
			}
			break;
		case RE::FormType::Door:
			{
				j["kind"] = "door";
				if (ref->IsLocked()) {
					j["lock"] = LockName(ref->GetLockLevel());
				}
				if (const auto tele = ref->extraList.GetByType<RE::ExtraTeleport>(); tele && tele->teleportData) {
					if (const auto linked = tele->teleportData->linkedDoor.get(); linked) {
						const auto cell = linked->GetParentCell();
						const auto loc = linked->GetCurrentLocation();
						if (cell && cell->IsInteriorCell()) {
							j["leadsTo"] = Str(cell->GetName());
						} else if (loc) {
							j["leadsTo"] = Str(loc->GetName());
						} else if (const auto ws = linked->GetWorldspace(); ws) {
							j["leadsTo"] = Str(ws->GetName());
						}
					}
				}
				break;
			}
		case RE::FormType::Flora:
			{
				j["kind"] = "plant";
				const auto flora = base->As<RE::TESFlora>();
				j["gives"] = flora && flora->produceItem ? Name(flora->produceItem) : "";
				j["harvested"] = (ref->formFlags & RE::TESObjectREFR::RecordFlags::kHarvested) != 0;
				break;
			}
		case RE::FormType::Tree:
			{
				j["kind"] = "plant";
				const auto tree = base->As<RE::TESObjectTREE>();
				j["gives"] = tree && tree->produceItem ? Name(tree->produceItem) : "";
				j["harvested"] = (ref->formFlags & RE::TESObjectREFR::RecordFlags::kHarvested) != 0;
				break;
			}
		case RE::FormType::Activator:
			{
				auto ore = OreVein(ref);
				if (ore.is_null()) {
					return nullptr;  // other activators aren't interesting
				}
				j["kind"] = "ore";
				if (ore.value("oreLeft", -1) < 0) {
					ore["range"] = OreRange(ore, a_player);
				}
				j["ore"] = ore;
				break;
			}
		default:
			return nullptr;
		}
		return j;
	}

	json Books(RE::PlayerCharacter* a_player)
	{
		json list = json::array();
		const auto inv = a_player->GetInventory([](RE::TESBoundObject& a_obj) { return a_obj.Is(RE::FormType::Book); });
		for (const auto& [obj, data] : inv) {
			const auto book = obj ? obj->As<RE::TESObjectBOOK>() : nullptr;
			if (!book || data.first <= 0) {
				continue;
			}
			json j{ { "id", book->GetFormID() }, { "name", data.second ? Str(data.second->GetDisplayName()) : Name(book) }, { "read", book->IsRead() },
				{ "note", book->IsNoteScroll() }, { "quest", data.second && data.second->IsQuestObject() }, { "count", data.first } };
			if (book->TeachesSkill()) {
				const int i = static_cast<int>(book->GetSkill()) - static_cast<int>(RE::ActorValue::kOneHanded);
				if (i >= 0 && i < 18) {
					j["skill"] = kSkillNames[i];
				}
			}
			list.push_back(std::move(j));
		}
		return list;
	}

	std::string BookText(RE::FormID a_id)
	{
		const auto book = RE::TESForm::LookupByID<RE::TESObjectBOOK>(a_id);
		if (!book) {
			return R"({"error":"not found"})";
		}
		RE::BSString text;
		book->GetDescription(text, book);
		return json{ { "id", a_id }, { "name", Name(book) }, { "text", Str(text.c_str()) } }.dump(-1, ' ', false, json::error_handler_t::replace);
	}

	json Character(RE::PlayerCharacter* a_player)
	{
		// Level-up spread: (base value - race starting value) / points per level-up.
		const auto race = a_player->GetRace();
		float perLevel = 10.0f;
		if (const auto gmst = RE::GameSettingCollection::GetSingleton(); gmst) {
			if (const auto s = gmst->GetSetting("iAVDhmsLevelUp"); s) {
				perLevel = static_cast<float>(s->GetInteger());
			}
		}
		const auto owner = a_player->AsActorValueOwner();
		auto spread = [&](RE::ActorValue a_av, float a_start) {
			const float base = owner->GetBaseActorValue(a_av);
			return json{ { "base", base }, { "start", a_start }, { "levels", perLevel > 0 ? std::round((base - a_start) / perLevel) : 0.0f } };
		};
		if (!race) {
			return nullptr;
		}
		return json{ { "race", Name(race) }, { "perLevel", perLevel },
			{ "health", spread(RE::ActorValue::kHealth, race->data.startingHealth) },
			{ "magicka", spread(RE::ActorValue::kMagicka, race->data.startingMagicka) },
			{ "stamina", spread(RE::ActorValue::kStamina, race->data.startingStamina) } };
	}

	json Stress()
	{
		const auto total = RE::TESForm::LookupByEditorID<RE::TESGlobal>("Stress_Total");
		const auto enabled = RE::TESForm::LookupByEditorID<RE::TESGlobal>("Stress_Enabled");
		if (!total) {
			return nullptr;
		}
		return json{ { "value", total->value }, { "max", 100 }, { "enabled", enabled ? enabled->value != 0.0f : true } };
	}
}

