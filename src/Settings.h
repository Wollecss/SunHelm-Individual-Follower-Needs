#pragma once

#include "SunHelm.h"

namespace Settings
{
	// How many follower slots the quest will ever be able to persist. Raising this past what the
	// ESP actually declares would silently drop state on save, so it is the hard ceiling for
	// maxTracked rather than a suggestion.
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
