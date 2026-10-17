#include "Hotkeys.h"

namespace companion::hotkeys
{
	namespace
	{
		// DirectInput scan codes for the number pad (rarely bound by mods).
		struct Binding
		{
			std::uint32_t key;
			const char*   cmd;
		};
		constexpr Binding kBindings[] = {
			{ 0x4E, "zoomIn" },              // NumPad +
			{ 0x4A, "zoomOut" },             // NumPad -
			{ 0x37, "cycleView" },           // NumPad *
			{ 0x53, "toggleUndiscovered" },  // NumPad .
			{ 0xB5, "toggleAbilities" },     // NumPad /
			{ 0x52, "selectNext" },          // NumPad 0: step through active effects for details
		};

		std::mutex                                     g_lock;
		std::deque<std::pair<std::uint64_t, std::string>> g_recent;
		std::uint64_t                                  g_nextId = 1;

		void Push(const char* a_cmd)
		{
			std::scoped_lock l{ g_lock };
			g_recent.emplace_back(g_nextId++, a_cmd);
			while (g_recent.size() > 20) {
				g_recent.pop_front();
			}
		}

		class InputSink : public RE::BSTEventSink<RE::InputEvent*>
		{
		public:
			static InputSink* Get()
			{
				static InputSink sink;
				return &sink;
			}

			RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*) override
			{
				if (!a_event) {
					return RE::BSEventNotifyControl::kContinue;
				}
				// Ignore typing in the console or text boxes.
				if (const auto ui = RE::UI::GetSingleton(); ui && (ui->IsMenuOpen("Console") || ui->IsMenuOpen("TextEntryMenu"))) {
					return RE::BSEventNotifyControl::kContinue;
				}
				for (auto e = *a_event; e; e = e->next) {
					if (e->GetEventType() != RE::INPUT_EVENT_TYPE::kButton || e->GetDevice() != RE::INPUT_DEVICE::kKeyboard) {
						continue;
					}
					const auto button = e->AsButtonEvent();
					if (!button || !button->IsDown()) {
						continue;
					}
					const auto code = button->GetIDCode();
					for (const auto& b : kBindings) {
						if (b.key == code) {
							Push(b.cmd);
						}
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	void Install()
	{
		if (const auto input = RE::BSInputDeviceManager::GetSingleton(); input) {
			input->AddEventSink(InputSink::Get());
			logs::info("Hotkeys ready (number pad + - * . /)");
		} else {
			logs::error("Hotkeys unavailable: input manager not found");
		}
	}

	json Recent()
	{
		json list = json::array();
		std::scoped_lock l{ g_lock };
		for (const auto& [id, cmd] : g_recent) {
			list.push_back({ { "id", id }, { "cmd", cmd } });
		}
		return list;
	}
}
