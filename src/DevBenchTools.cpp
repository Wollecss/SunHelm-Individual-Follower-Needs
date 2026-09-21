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
	// blocking hand-off is the only way to satisfy both requirements at once.
	//
	// Timing out does NOT cancel the queued task: it runs whenever the main thread next pumps,
	// which may be long after this returns. So every piece of state it touches is owned by a
	// shared_ptr the task co-owns, and the result is returned by value. An earlier version had the
	// caller pass a lambda capturing a local by reference, which wrote into a destroyed object once
	// a timeout let the caller return first - a use-after-free that crashed the game.
	template <class T>
	std::optional<T> RunOnMainThreadBlocking(std::function<T()> a_fn, std::chrono::milliseconds a_timeout)
	{
		auto* task = SKSE::GetTaskInterface();
		if (!task) {
			return std::nullopt;
		}

		struct Shared
		{
			std::mutex              mutex;
			std::condition_variable cv;
			bool                    done{ false };
			T                       value{};
		};
		auto shared = std::make_shared<Shared>();

		task->AddTask([a_fn, shared]() {
			auto value = a_fn();
			{
				std::lock_guard lock(shared->mutex);
				shared->value = std::move(value);
				shared->done = true;
			}
			shared->cv.notify_all();
		});

		std::unique_lock lock(shared->mutex);
		if (!shared->cv.wait_for(lock, a_timeout, [&shared]() { return shared->done; })) {
			return std::nullopt;
		}
		return std::move(shared->value);
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

	// Which stage ability each tracked follower is actually carrying, read off the actor rather
	// than from what we think we applied - so a mismatch between the two is visible instead of
	// assumed away. Needs the main thread because it touches actors.
	using AbilityMap = std::unordered_map<RE::FormID, json>;

	std::optional<AbilityMap> CollectAppliedAbilities()
	{
		// Returns by value into the shared state rather than capturing anything from this scope, so
		// a timed-out task has nothing of ours left to write into.
		return RunOnMainThreadBlocking<AbilityMap>(
			[]() {
				AbilityMap collected;
				Followers::ForEachTracked([&collected](Followers::State& a_state, RE::Actor& a_actor) {
					json entry;

					// Everything past this gate reaches into state that only exists for a loaded
					// actor: inventory changes for the gold count, the current location for the inn
					// check, the applied spell list. Needs::Update keeps all of it behind
					// State::active for that reason, and this tool ignoring the same guard is what
					// crashed the game - reading an unloaded follower's inventory faults inside the
					// engine, so no amount of null-checking on our side would have caught it.
					// Reported rather than skipped, because "not loaded" is a different answer from
					// "no abilities applied" and the whole point of this tool is telling them apart.
					const bool loaded = a_actor.Is3DLoaded();
					entry["loaded"] = loaded;
					entry["active"] = a_state.active;
					if (!loaded) {
						collected[a_state.formID] = std::move(entry);
						return;
					}

					for (const auto need : SunHelm::kAllNeeds) {
						entry[std::string(SunHelm::NeedName(need))] =
							SunHelm::AppliedStageOn(&a_actor, need);
					}
					entry["diseased"] = SunHelm::IsDiseased(&a_actor);
					entry["gold"] = SunHelm::GoldAmount(&a_actor);
					entry["in_inn"] = SunHelm::IsInInn(&a_actor);
					collected[a_state.formID] = std::move(entry);
				});
				return collected;
			},
			std::chrono::milliseconds(5000));
	}

	void StatusHandler(void* /*a_ctx*/, const char* /*a_argsJson*/, void* a_sink, DevBenchAPI::WriteFn a_write)
	{
		const auto abilities = CollectAppliedAbilities();

		auto out = BuildStatus();
		if (!abilities) {
			// Said out loud rather than just leaving the field off - an absent field is
			// indistinguishable from "this follower has no abilities", which is a different thing.
			out["applied_abilities_error"] =
				"main thread did not respond within 5000ms (game paused, in a menu, or loading)";
		} else {
			for (auto& follower : out["followers"]) {
				const auto formID = static_cast<RE::FormID>(
					std::stoul(follower["formID"].get<std::string>(), nullptr, 16));
				if (const auto it = abilities->find(formID); it != abilities->end()) {
					follower["applied_abilities"] = it->second;
				}
			}
		}

		a_write(a_sink, out.dump().c_str());
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
		const auto ranInTime = RunOnMainThreadBlocking<bool>(
			[]() {
				Needs::Update();
				return true;
			},
			std::chrono::milliseconds(5000));

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
