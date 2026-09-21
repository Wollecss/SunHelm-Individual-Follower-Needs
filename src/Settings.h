#pragma once

#include "SunHelm.h"

namespace Settings
{
	// How many follower slots exist in storage. This MUST match SLOTS in esp/gen_esp_yaml.sh - the
	// ESP declares four globals per slot, and Persistence::Resolve() fails (disabling persistence
	// entirely, loudly) if it can't find every one of them. Raising it means regenerating the ESP.
	inline constexpr int kMaxSlots = 10;
	inline constexpr int kDefaultTracked = 3;

	struct Data
	{
		bool enabled{ true };
		int  maxTracked{ kDefaultTracked };

		bool trackHunger{ true };
		bool trackThirst{ true };
		bool mirrorFatigue{ true };
		bool mirrorCold{ true };

		// Followers get the same stage abilities SunHelm puts on the player. Off means needs are
		// tracked and reported but carry no mechanical penalty.
		bool applyDebuffs{ true };

		// Followers eating and drinking out of their own inventory. Off means they only ever
		// improve when something else feeds them.
		bool allowSelfFeeding{ true };
		int  eatAtStage{ 2 };   // Peckish
		int  drinkAtStage{ 2 };  // Thirsty

		bool notifyNeed[static_cast<std::size_t>(SunHelm::Need::kTotal)]{ true, true, true, true };
		bool notifyConsumption{ true };

		// Deliberately off by default: losing a companion because you forgot to feed them is a
		// much harsher failure than the player taking the same damage themselves.
		bool needsDamage{ false };

		float pollSeconds{ 5.0f };
	};

	inline Data& Get()
	{
		static Data data;
		return data;
	}
}
