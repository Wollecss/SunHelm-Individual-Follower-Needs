#include "DevBenchTools.h"

#include "DevBench/DevBenchAPI.h"
#include "Followers.h"
#include "Needs.h"
#include "Settings.h"
#include "SunHelm.h"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace
{
	// Runs a_fn on the main thread and blocks the calling (listener) thread until it has actually
	// run, unlike SKSE::GetTaskInterface()->AddTask on its own which only queues it. DevBench's
	// handler contract requires a synchronous JSON result, and anything that touches an Actor -
	// GetInventory, EquipObject, the process list - is only safe from the main thread, so a
	// blocking handle-off is the only way to satisfy both requirements at once.
	bool RunOnMainThreadBlocking(const std::function<void()>& a_fn, std::chrono::milliseconds a_timeout)
	{
		auto* task = SKSE::GetTaskInterface();
		if (!task) {
			return false;
		}

		auto                    done = std::make_shared<std::atomic<bool>>(false);
		auto                    mutex = std::make_shared<std::mutex>();
		auto                    cv = std::make_shared<std::condition_variable>();

		task->AddTask([a_fn, done, mutex, cv]() {
			a_fn();
			{
				std::lock_guard lock(*mutex);
				done->store(true);
			}
			cv->notify_all();
		});

		std::unique_lock lock(*mutex);
		return cv->wait_for(lock, a_timeout, [&]() { return done->load(); });
	}

	json StageInfo(SunHelm::Need a_need, float a_level)
	{
		const auto stage = SunHelm::StageOf(a_need, a_level);
		return {
			{ "level", a_level },
			{ "stage", stage },
			{ "label", std::string(SunHelm::StageLabel(a_need, stage)) },
		};
	}

	json BuildStatus()
	{
		json out;
		out["sunhelm_available"] = SunHelm::IsAvailable();
		out["sunhelm_enabled"] = SunHelm::IsAvailable() && SunHelm::IsModEnabled();

		json player;
		if (SunHelm::IsAvailable()) {
			for (const auto need : SunHelm::kAllNeeds) {
				auto entry = StageInfo(need, SunHelm::PlayerLevel(need));
				entry["rate"] = SunHelm::Rate(need);
				entry["tracked"] = SunHelm::IsNeedEnabled(need);
				player[std::string(SunHelm::NeedName(need))] = entry;
			}
		}
		out["player"] = player;

		json followers = json::array();
		for (const auto& follower : Followers::Snapshot()) {
			json entry;
			entry["formID"] = std::format("{:08X}", follower.formID);
			entry["name"] = follower.name;
			entry["active"] = follower.active;
			entry["hunger"] = StageInfo(SunHelm::Need::kHunger, follower.hunger);
			entry["thirst"] = StageInfo(SunHelm::Need::kThirst, follower.thirst);
			followers.push_back(std::move(entry));
		}
		out["followers"] = std::move(followers);
		out["tracked_count"] = Followers::TrackedCount();
		out["max_tracked"] = Settings::Get().maxTracked;

		return out;
	}

	void StatusHandler(void* /*a_ctx*/, const char* /*a_argsJson*/, void* a_sink, DevBenchAPI::WriteFn a_write)
	{
		// Pure reads of our own mutex-guarded state and plain TESGlobal floats - no engine call
		// that requires the main thread, so this answers directly from the listener thread.
		a_write(a_sink, BuildStatus().dump().c_str());
	}

	void SetNeedHandler(void* /*a_ctx*/, const char* a_argsJson, void* a_sink, DevBenchAPI::WriteFn a_write)
	{
		json result;
		try {
			const json args = json::parse(a_argsJson);
			const std::string name = args.value("name", "");
			if (name.empty()) {
				result = { { "ok", false }, { "error", "name is required" } };
			} else {
				std::optional<float> hunger;
				std::optional<float> thirst;
				if (args.contains("hunger") && !args["hunger"].is_null()) {
					hunger = args["hunger"].get<float>();
				}
				if (args.contains("thirst") && !args["thirst"].is_null()) {
					thirst = args["thirst"].get<float>();
				}

				if (Followers::SetNeedsForTesting(name, hunger, thirst)) {
					result = { { "ok", true } };
				} else {
					result = { { "ok", false }, { "error", "no tracked follower matched that name" } };
				}
			}
		} catch (const std::exception& ex) {
			result = { { "ok", false }, { "error", ex.what() } };
		}

		a_write(a_sink, result.dump().c_str());
	}

	void ForceTickHandler(void* /*a_ctx*/, const char* /*a_argsJson*/, void* a_sink, DevBenchAPI::WriteFn a_write)
	{
		const auto ranInTime = RunOnMainThreadBlocking([]() { Needs::Update(); }, std::chrono::milliseconds(5000));

		json result;
		if (!ranInTime) {
			result = { { "ok", false },
				{ "error", "main thread did not run the tick within 5000ms (game paused or hung?)" } };
		} else {
			result = { { "ok", true }, { "status", BuildStatus() } };
		}
		a_write(a_sink, result.dump().c_str());
	}
}

void DevBenchTools::Register()
{
	auto* dvb = DevBenchAPI::GetDevBenchInterface001();
	if (!dvb) {
		logger::info("DevBench not detected; skipping tool registration");
		return;
	}

	dvb->RegisterTool("sunhelm_followers.status",
		R"json({
			"description": "Snapshot of every tracked follower's hunger/thirst plus the player's own live SunHelm values (level, stage, rate).",
			"inputSchema": { "type": "object", "properties": {} },
			"readOnly": true
		})json",
		&StatusHandler, nullptr);

	dvb->RegisterTool("sunhelm_followers.set_need",
		R"json({
			"description": "Testing only: directly overwrites a tracked follower's hunger and/or thirst level by name, without waiting on game time.",
			"inputSchema": {
				"type": "object",
				"properties": {
					"name": { "type": "string", "description": "Follower name, or a substring of it" },
					"hunger": { "type": "number", "description": "New hunger level. 0 is well fed." },
					"thirst": { "type": "number", "description": "New thirst level. 0 is quenched." }
				},
				"required": ["name"]
			},
			"readOnly": false
		})json",
		&SetNeedHandler, nullptr);

	dvb->RegisterTool("sunhelm_followers.force_tick",
		R"json({
			"description": "Testing only: runs one follower-needs tick immediately on the main thread (self-feeding, stage sync, notifications) instead of waiting for the next poll, then returns the resulting status.",
			"inputSchema": { "type": "object", "properties": {} },
			"readOnly": false
		})json",
		&ForceTickHandler, nullptr);

	logger::info("Registered DevBench tools: sunhelm_followers.{{status,set_need,force_tick}}");
}
