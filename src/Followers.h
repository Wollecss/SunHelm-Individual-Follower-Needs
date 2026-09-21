#pragma once

#include "SunHelm.h"

namespace Followers
{
	struct State
	{
		RE::FormID  formID{ 0 };
		std::string name;

		float hunger{ 0.0f };
		float thirst{ 0.0f };

		// Calendar hours at the last tick, so accumulation is driven by elapsed game time the same
		// way SunHelm drives the player's.
		float lastTickHours{ 0.0f };

		// Last stage a notification was shown for, so "X is hungry" fires on the crossing rather
		// than every poll. -1 means nothing announced yet.
		int lastNotifiedStage[2]{ -1, -1 };

		// Same idea for "X has nothing to eat/drink" - fires once per stage, not once per poll.
		// [0] hunger, [1] thirst.
		int outOfSupplyNotifiedStage[2]{ -1, -1 };

		// Calendar hours at the last meal / drink, so a follower doesn't work through their whole
		// pack in one go. Eating and drinking are tracked apart: a meal shouldn't stop them washing
		// it down. [0] hunger, [1] thirst. Not persisted - a fresh session should never inherit a
		// cooldown, and starting at 0 simply means the first one is always allowed.
		float lastConsumedHours[2]{ 0.0f, 0.0f };

		// Mirrored needs don't accumulate, but the applied ability still has to be kept in step.
		int appliedStage[static_cast<std::size_t>(SunHelm::Need::kTotal)]{ -1, -1, -1, -1 };

		bool active{ false };  // with the player right now, as opposed to parked or unloaded
	};

	void Start();
	void Stop();

	// kPostLoadGame / kNewGame set this true, kPreLoadGame clears it. Nothing touches an actor
	// while it is false: kDataLoaded also fires at the main menu, where there is no loaded world.
	void SetGameReady(bool a_ready);
	bool IsGameReady();

	// Drops all tracking and any pending restores. A loaded save brings its own followers, and
	// stale entries from the previous session would otherwise leak across.
	void Reset();

	// Remembers needs read back from a save, to be applied the next time that follower is picked
	// up. Kept pending rather than applied immediately because the actor usually isn't loaded yet
	// at the point a save becomes live.
	void SeedRestoredNeeds(RE::FormID a_formID, float a_hunger, float a_thirst);

	std::vector<State> Snapshot();
	std::size_t        TrackedCount();

	// Visits every tracked follower whose actor can still be resolved, under the state lock.
	// Main thread only: the visitor is handed a live actor to read and modify.
	void ForEachTracked(const std::function<void(State&, RE::Actor&)>& a_visitor);

	// Overwrites a tracked follower's hunger/thirst by name (case-insensitive substring match).
	// Pure container access under the state lock - no engine calls - so this is safe to call from
	// any thread, including DevBench's listener thread, without marshaling to the main thread.
	// Returns false if no tracked follower matched.
	bool SetNeedsForTesting(std::string_view a_nameSubstring, std::optional<float> a_hunger,
		std::optional<float> a_thirst);
}
