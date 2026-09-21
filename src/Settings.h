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

		// A Ravenous follower will eat raw meat rather than starve, at SunHelm's own risk of food
		// poisoning. Anything less desperate and they stay picky.
		bool eatRawWhenDesperate{ true };
		// Drinking a cure potion out of their own pack when ill. Costs nothing to leave on: they
		// can only do it if the player gave them one.
		bool selfCureWithPotions{ true };

		// Minimum game time between one follower's meals, and separately between their drinks.
		// Without it, consumption is attempted every tick and a Ravenous follower empties their
		// pack in seconds. At a default timescale 0.25 is roughly 45 real seconds.
		float consumeCooldownHours{ 0.25f };
		// Longer leash on buying, which is attempted every tick while consumption is rate-limited.
		// Turn both of these down if you'd rather your followers drink themselves silly.
		float purchaseCooldownHours{ 1.0f };
		// Ale gets its own leash, separate from meals. It's deliberately long: this is the setting
		// that decides what an evening in a tavern costs, since a follower who keeps the bar in
		// sight will keep ordering for as long as they're there. A round every couple of hours is
		// ambience at a few gold an hour; every half hour empties a purse.
		//
		// Drinking themselves under the table on their own coin is not meant to be the fast route.
		// A player who actually wants a follower drunk hands them drinks.
		float aleCooldownHours{ 2.0f };
		// Chance a thirsty follower in a tavern orders ale rather than water. Nobody walks into an
		// inn and asks for water - but ale restores half what water does, so this is a real trade,
		// and anyone who would rather have efficient followers can turn it down to zero.
		int alePreferenceChance{ 65 };
		// Chance per eligible tick that a follower in a tavern buys a drink they don't strictly
		// need. Deliberately still rolled while they're thirsty: being thirsty in a tavern is a
		// reason to have a drink, not a reason to abstain.
		int socialDrinkChance{ 25 };
		// How long a drink keeps counting toward getting drunk. Long on purpose, and tied to the ale
		// cooldown above: if rounds are two hours apart, a window shorter than the time it takes to
		// buy three of them means the count resets before it ever gets there. Keep this comfortably
		// above three times the round gap or drunkenness becomes unreachable by purchase alone.
		float drinkStackWindowHours{ 12.0f };
		// How long after leaving a tavern the drink wears off. Separate from the stacking window
		// above, and shorter, because those two want opposite things: drinks should accumulate
		// slowly, but a follower shouldn't still be staggering a full day after last call. Long
		// enough to enjoy the walk home, which is the entire point of a drunk companion. While
		// they're still in the inn they stay drunk regardless - they're still ordering.
		float soberUpHours{ 4.0f };

		// Publishes follower condition for the CHIM bridge mod to forward to HerikaServer. Inert
		// without it: the queue is capped, so nothing consuming it costs a fixed handful of strings.
		bool chimBridge{ true };

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
