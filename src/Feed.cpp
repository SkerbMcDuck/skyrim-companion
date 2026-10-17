#include "Feed.h"
#include "State.h"

namespace companion::feed
{
	namespace
	{
		struct Entry
		{
			std::uint64_t id;
			std::string   text;
			std::string   kind;
		};

		std::mutex          g_lock;
		std::deque<Entry>   g_recent;
		std::uint64_t       g_nextId = 1;
		std::atomic<std::uint32_t> g_crosshair{ 0 };

		void Push(std::string a_text, const char* a_kind)
		{
			std::scoped_lock l{ g_lock };
			g_recent.push_back({ g_nextId++, std::move(a_text), a_kind });
			while (g_recent.size() > 30) {
				g_recent.pop_front();
			}
		}

		std::string Name(const RE::TESForm* a_form)
		{
			if (!a_form) {
				return {};
			}
			const auto n = a_form->GetName();
			return n ? std::string(n) : std::string();
		}

		const char* SkillName(RE::ActorValue a_av)
		{
			static constexpr const char* kNames[] = { "One-handed", "Two-handed", "Archery", "Block", "Smithing", "Heavy Armor", "Light Armor", "Pickpocket", "Lockpicking",
				"Sneak", "Alchemy", "Speech", "Alteration", "Conjuration", "Destruction", "Illusion", "Restoration", "Enchanting" };
			const auto i = static_cast<int>(a_av) - static_cast<int>(RE::ActorValue::kOneHanded);
			return i >= 0 && i < 18 ? kNames[i] : nullptr;
		}

		// New gear alert: only when the base value beats what's equipped in the same slot and class
		// (or the same weapon type). Clothing and jewelry are skipped.
		void CheckUpgrade(RE::TESForm* a_form)
		{
			const auto player = RE::PlayerCharacter::GetSingleton();
			if (!player || !a_form) {
				return;
			}
			if (const auto weap = a_form->As<RE::TESObjectWEAP>(); weap) {
				const auto eq = player->GetEquippedObject(false);
				const auto mine = eq ? eq->As<RE::TESObjectWEAP>() : nullptr;
				if (mine && mine != weap && mine->GetWeaponType() == weap->GetWeaponType() && weap->GetAttackDamage() > mine->GetAttackDamage()) {
					Push(std::format("Upgrade: {} ({} damage vs {} on {})", Name(weap), static_cast<int>(weap->GetAttackDamage()), static_cast<int>(mine->GetAttackDamage()), Name(mine)), "upgrade");
				}
				return;
			}
			if (const auto armo = a_form->As<RE::TESObjectARMO>(); armo) {
				if (!armo->IsHeavyArmor() && !armo->IsLightArmor()) {
					return;
				}
				using Slot = RE::BGSBipedObjectForm::BipedObjectSlot;
				for (const auto slot : { Slot::kBody, Slot::kHead, Slot::kHands, Slot::kFeet, Slot::kShield }) {
					if (!armo->GetSlotMask().all(slot)) {
						continue;
					}
					const auto worn = player->GetWornArmor(slot);
					if (worn && worn != armo && worn->IsHeavyArmor() == armo->IsHeavyArmor() && worn->IsLightArmor() == armo->IsLightArmor() &&
						armo->GetArmorRating() > worn->GetArmorRating()) {
						Push(std::format("Upgrade: {} ({} armor vs {} on {})", Name(armo), static_cast<int>(armo->GetArmorRating()), static_cast<int>(worn->GetArmorRating()), Name(worn)), "upgrade");
					}
					return;
				}
			}
		}

		// Map screen opened: the hub loads map markers then (not at game load).
		class MenuSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			static MenuSink* Get() { static MenuSink s; return &s; }
			RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (a_event && a_event->opening && a_event->menuName == RE::MapMenu::MENU_NAME) {
					state::OnMapOpened();
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		// Items moving into the player's inventory.
		class ContainerSink : public RE::BSTEventSink<RE::TESContainerChangedEvent>
		{
		public:
			static ContainerSink* Get() { static ContainerSink s; return &s; }
			RE::BSEventNotifyControl ProcessEvent(const RE::TESContainerChangedEvent* a_event, RE::BSTEventSource<RE::TESContainerChangedEvent>*) override
			{
				try {
					constexpr RE::FormID kPlayer = 0x14;
					if (a_event && a_event->newContainer == kPlayer && a_event->oldContainer != kPlayer && a_event->itemCount > 0) {
						const auto form = RE::TESForm::LookupByID(a_event->baseObj);
						const auto name = Name(form);
						if (!name.empty()) {
							Push(a_event->itemCount > 1 ? std::format("Picked up {} ({})", name, a_event->itemCount) : "Picked up " + name, "pickup");
							CheckUpgrade(form);
						}
					}
				} catch (...) {
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		class SkillSink : public RE::BSTEventSink<RE::SkillIncrease::Event>
		{
		public:
			static SkillSink* Get() { static SkillSink s; return &s; }
			RE::BSEventNotifyControl ProcessEvent(const RE::SkillIncrease::Event* a_event, RE::BSTEventSource<RE::SkillIncrease::Event>*) override
			{
				try {
					if (a_event) {
						if (const auto name = SkillName(a_event->actorValue); name) {
							const auto player = RE::PlayerCharacter::GetSingleton();
							const int level = player ? static_cast<int>(player->AsActorValueOwner()->GetBaseActorValue(a_event->actorValue)) : 0;
							Push(level > 0 ? std::format("{} increased to {}", name, level) : std::format("{} increased", name), "skill");
						}
					}
				} catch (...) {
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		class LevelSink : public RE::BSTEventSink<RE::LevelIncrease::Event>
		{
		public:
			static LevelSink* Get() { static LevelSink s; return &s; }
			RE::BSEventNotifyControl ProcessEvent(const RE::LevelIncrease::Event* a_event, RE::BSTEventSource<RE::LevelIncrease::Event>*) override
			{
				if (a_event) {
					Push(std::format("Level up: {}", a_event->newLevel), "level");
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		class DiscoverySink : public RE::BSTEventSink<RE::LocationDiscovery::Event>
		{
		public:
			static DiscoverySink* Get() { static DiscoverySink s; return &s; }
			RE::BSEventNotifyControl ProcessEvent(const RE::LocationDiscovery::Event* a_event, RE::BSTEventSource<RE::LocationDiscovery::Event>*) override
			{
				try {
					if (a_event && a_event->mapMarkerData) {
						const auto name = a_event->mapMarkerData->locationName.GetFullName();
						if (name && *name) {
							Push(std::string("Discovered: ") + name, "discovery");
						}
					}
				} catch (...) {
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		class CrosshairSink : public RE::BSTEventSink<SKSE::CrosshairRefEvent>
		{
		public:
			static CrosshairSink* Get() { static CrosshairSink s; return &s; }
			RE::BSEventNotifyControl ProcessEvent(const SKSE::CrosshairRefEvent* a_event, RE::BSTEventSource<SKSE::CrosshairRefEvent>*) override
			{
				std::uint32_t handle = 0;
				if (a_event && a_event->crosshairRef) {
					handle = a_event->crosshairRef->CreateRefHandle().native_handle();
				}
				g_crosshair = handle;
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		template <class Source, class Sink>
		void Add(Source* a_source, Sink* a_sink, const char* a_what)
		{
			if (a_source) {
				a_source->AddEventSink(a_sink);
			} else {
				logs::warn("Event source missing: {}", a_what);
			}
		}
	}

	void Install()
	{
		if (const auto holder = RE::ScriptEventSourceHolder::GetSingleton(); holder) {
			holder->AddEventSink<RE::TESContainerChangedEvent>(ContainerSink::Get());
		}
		Add(RE::SkillIncrease::GetEventSource(), SkillSink::Get(), "skill increase");
		Add(RE::LevelIncrease::GetEventSource(), LevelSink::Get(), "level increase");
		Add(RE::LocationDiscovery::GetEventSource(), DiscoverySink::Get(), "location discovery");
		Add(SKSE::GetCrosshairRefEventSource(), CrosshairSink::Get(), "crosshair");
		if (const auto ui = RE::UI::GetSingleton(); ui) {
			ui->AddEventSink<RE::MenuOpenCloseEvent>(MenuSink::Get());
		}
		logs::info("Event feed ready");
	}

	json Recent()
	{
		json list = json::array();
		std::scoped_lock l{ g_lock };
		for (const auto& e : g_recent) {
			list.push_back({ { "id", e.id }, { "text", e.text }, { "kind", e.kind } });
		}
		return list;
	}

	void Clear()
	{
		std::scoped_lock l{ g_lock };
		g_recent.clear();
		g_crosshair = 0;
	}

	RE::NiPointer<RE::TESObjectREFR> CrosshairRef()
	{
		const auto native = g_crosshair.load();
		if (!native) {
			return nullptr;
		}
		RE::ObjectRefHandle handle{};
		std::memcpy(&handle, &native, sizeof(native));
		return handle.get();
	}
}
