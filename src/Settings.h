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

		// Buying a meal or a drink at an inn, out of the follower's own purse. Most vanilla
		// followers carry very little gold, so in practice this only matters once the player has
		// funded them - which is the intent.
		bool buyAtInns{ true };
		// Whether to spend coin before raiding their own pack. Off (the default) means they eat
		// what they're carrying first and only buy when they have nothing suitable.
		bool preferBuying{ false };
		// Buying ale: partly a cheap way to deal with thirst, partly just what people do in taverns.
		bool buyAlcohol{ true };
		// Whether enough drinks actually makes them drunk, using SunHelm's own drunk spell.
		bool drunkEffects{ true };

		// Minimum game time between one follower's meals, and separately between their drinks.
		// Without it, consumption is attempted every tick and a Ravenous follower empties their
		// pack in seconds. At a default timescale 0.25 is roughly 45 real seconds.
		float consumeCooldownHours{ 0.25f };
		// Longer leash on buying, which is attempted every tick while consumption is rate-limited.
		// Turn both of these down if you'd rather your followers drink themselves silly.
		float purchaseCooldownHours{ 1.0f };
		// Chance per eligible tick that a follower in a tavern buys a drink they don't strictly
		// need. The only purchase that isn't driven by hunger or thirst.
		int socialDrinkChance{ 25 };

		bool notifyNeed[static_cast<std::size_t>(SunHelm::Need::kTotal)]{ true, true, true, true };
		bool notifyConsumption{ true };

		// Deliberately off by default: losing a companion because you forgot to feed them is a
		// much harsher failure than the player taking the same damage themselves.
		bool needsDamage{ false };

		float pollSeconds{ 5.0f };
	};

	Data& Get();

	// These are mod configuration, not save state, so they live in a file next to the DLL rather
	// than in the storage globals - changing save shouldn't change your settings.
	void Load();
	void Save();
}
