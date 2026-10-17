#pragma once

// Runs a read-only job on Skyrim's main thread from the web server thread and waits for the answer.
// Used for on-demand lookups (a follower's inventory, a book's text) so nothing runs until you ask.
namespace companion::gamethread
{
	inline std::optional<std::string> Run(std::function<std::string()> a_job, std::chrono::milliseconds a_timeout = std::chrono::milliseconds(800))
	{
		const auto tasks = SKSE::GetTaskInterface();
		if (!tasks) {
			return std::nullopt;
		}
		auto promise = std::make_shared<std::promise<std::string>>();
		auto future = promise->get_future();
		tasks->AddTask([promise, job = std::move(a_job)]() {
			try {
				promise->set_value(job());
			} catch (const std::exception& e) {
				promise->set_value(std::string(R"({"error":")") + e.what() + "\"}");
			} catch (...) {
				promise->set_value(R"({"error":"failed"})");
			}
		});
		if (future.wait_for(a_timeout) != std::future_status::ready) {
			return std::nullopt;
		}
		return future.get();
	}
}
